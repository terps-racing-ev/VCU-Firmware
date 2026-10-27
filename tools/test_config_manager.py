"""Exercise the compiled config manager with a simulated RTOS queue and flash.

Requires: pip install unicorn pyelftools
Run: python tools/test_config_manager.py --gcc <path/to/arm-none-eabi-gcc>
The compiler defaults to the workspace's VS Code compilerPath setting.
No connected hardware is accessed.
"""

import argparse
from collections import deque
import json
import shutil
import struct
import subprocess
import unittest

from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP

from test_eeprom import ERASED, INVALID, NOT_FOUND, NOT_INITIALIZED, OK, ROOT, FlashMachine


RESOURCE = (-3) & 0xFFFFFFFF
PARAMETER = (-4) & 0xFFFFFFFF


class ConfigMachine(FlashMachine):
    def __init__(self, elf_bytes, flash=ERASED):
        self.queue = deque()
        self.writes = []
        self.fail_allocation = False
        self.allocations = 0
        self.blocked = False
        super().__init__(elf_bytes, flash)
        self.hooks.update({self.symbols[name] & ~1: name for name in (
            "osMessageQueueNew", "osMessageQueuePut", "osMessageQueueGet",
            "EEPROM_WriteVariable",
        )})

    def _hook(self, uc, address, size, user_data):
        name = self.hooks.get(address)
        if name == "EEPROM_WriteVariable":
            self.writes.append((uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)))
            return
        if name not in ("osMessageQueueNew", "osMessageQueuePut", "osMessageQueueGet"):
            return super()._hook(uc, address, size, user_data)
        args = [uc.reg_read(reg) for reg in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        result = OK
        if name == "osMessageQueueNew":
            self.allocations += 1
            self.capacity, self.item_size = args[:2]
            result = 0 if self.fail_allocation else 1
        elif name == "osMessageQueuePut":
            assert args[0] == 1 and args[2:] == [0, 0]
            if len(self.queue) == self.capacity:
                result = RESOURCE
            else:
                self.queue.append(bytes(uc.mem_read(args[1], self.item_size)))
        elif name == "osMessageQueueGet":
            assert args[0] == 1 and args[3] == 0xFFFFFFFF
            if not self.queue:
                self.blocked = True
                uc.emu_stop()
                return
            uc.mem_write(args[1], self.queue.popleft())
        uc.reg_write(UC_ARM_REG_R0, result)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def drain(self):
        self.blocked = False
        self.uc.reg_write(UC_ARM_REG_SP, 0x2001FFF0)
        self.uc.reg_write(UC_ARM_REG_R0, 0)
        self.uc.emu_start(self.symbols["Config_Manager_Task"] | 1, 0, count=20_000_000)
        assert self.blocked, "task did not block waiting for requests"

    def read(self, address):
        output = 0x20018000
        self.uc.mem_write(output, struct.pack("<I", 0x12345678))
        status = self.call("Config_ReadVariable", address, output)
        return status, struct.unpack("<I", self.uc.mem_read(output, 4))[0]

    def submit_write(self, address, value):
        return self.call("Config_WriteVariable", address, value)


class ConfigManagerTests(unittest.TestCase):
    elf_bytes = None

    def machine(self, flash=ERASED):
        return ConfigMachine(self.elf_bytes, flash)

    def test_init_failures_and_retry(self):
        dirty = bytearray(ERASED)
        dirty[64:68] = b"data"
        machine = self.machine(bytes(dirty))
        self.assertEqual(machine.call("Config_Manager_Init"), 1)
        self.assertEqual(machine.allocations, 0)
        self.assertEqual(machine.read(0)[0], NOT_INITIALIZED)

        machine = self.machine()
        machine.fail_allocation = True
        self.assertEqual(machine.call("Config_Manager_Init"), 1)
        self.assertEqual(machine.submit_write(0, 1), RESOURCE)
        machine.fail_allocation = False
        self.assertEqual(machine.call("Config_Manager_Init"), OK)
        self.assertEqual(machine.call("Config_Manager_Init"), OK)
        self.assertEqual(machine.allocations, 2)

    def test_submission_validation(self):
        machine = self.machine()
        self.assertEqual(machine.read(0), (NOT_INITIALIZED, 0x12345678))
        self.assertEqual(machine.submit_write(0, 1), RESOURCE)
        self.assertEqual(machine.call("Config_Manager_Init"), OK)
        self.assertEqual(machine.read(256), (INVALID, 0x12345678))
        self.assertEqual(machine.submit_write(256, 1), PARAMETER)
        self.assertEqual(machine.call("Config_ReadVariable", 0, 0), INVALID)
        self.assertFalse(machine.queue)

    def test_direct_reads_and_queued_writes(self):
        machine = self.machine()
        self.assertEqual(machine.call("Config_Manager_Init"), OK)
        self.assertEqual(machine.read(255), (NOT_FOUND, 0x12345678))
        self.assertFalse(machine.queue)
        self.assertEqual(machine.submit_write(255, 0xFFFFFFFF), OK)
        self.assertEqual(machine.read(255), (NOT_FOUND, 0x12345678))
        self.assertFalse(machine.writes)
        machine.drain()
        self.assertEqual(machine.read(255), (OK, 0xFFFFFFFF))
        self.assertEqual(machine.submit_write(255, 123), OK)
        self.assertEqual(machine.submit_write(255, 0), OK)
        self.assertEqual(machine.read(255), (OK, 0xFFFFFFFF))
        machine.drain()
        self.assertEqual(machine.writes, [(255, 0xFFFFFFFF), (255, 123), (255, 0)])
        self.assertEqual(machine.read(255), (OK, 0))

    def test_queue_backpressure(self):
        machine = self.machine()
        self.assertEqual(machine.call("Config_Manager_Init"), OK)
        for value in range(machine.capacity):
            self.assertEqual(machine.submit_write(0, value), OK)
        self.assertEqual(machine.submit_write(0, 999), RESOURCE)
        self.assertEqual(machine.read(0)[0], NOT_FOUND)
        self.assertEqual(len(machine.queue), machine.capacity)
        machine.drain()
        self.assertEqual(machine.read(0), (OK, machine.capacity - 1))
        self.assertEqual(machine.submit_write(0, 999), OK)
        machine.drain()
        self.assertEqual(len(machine.writes), machine.capacity + 1)
        self.assertEqual(machine.read(0), (OK, 999))

    def test_flash_failure_invalidates_eeprom(self):
        machine = self.machine()
        self.assertEqual(machine.call("Config_Manager_Init"), OK)
        machine.fail_at = machine.operations + 1
        self.assertEqual(machine.submit_write(7, 42), OK)
        machine.drain()
        self.assertEqual(machine.writes, [(7, 42)])
        self.assertEqual(machine.read(7), (NOT_INITIALIZED, 0x12345678))

    def test_default_virtual_address(self):
        machine = self.machine()
        address = machine.call("Test_DefaultAddress")
        self.assertEqual(address, 0)
        self.assertEqual(machine.call("Config_Manager_Init"), OK)
        self.assertEqual(machine.submit_write(address, 42), OK)
        machine.drain()
        self.assertEqual(machine.read(address), (OK, 42))


def build_fixture(gcc):
    build = ROOT / "build" / "config-manager-tests"
    build.mkdir(parents=True, exist_ok=True)
    stubs = build / "stubs.c"
    stubs.write_text('''#include "main.h"
#include "managers/config_manager.h"
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_ERROR; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_ERROR; }
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t t, uint32_t a, uint64_t d) { return HAL_ERROR; }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *s) { return HAL_ERROR; }
osMessageQueueId_t osMessageQueueNew(uint32_t c, uint32_t s, const osMessageQueueAttr_t *a) { return NULL; }
osStatus_t osMessageQueuePut(osMessageQueueId_t q, const void *m, uint8_t p, uint32_t t) { return osError; }
osStatus_t osMessageQueueGet(osMessageQueueId_t q, void *m, uint8_t *p, uint32_t t) { return osError; }
uint16_t Test_DefaultAddress(void) { return CONFIG_VADDR_DEFAULT; }
''')
    linker = build / "fixture.ld"
    linker.write_text('''ENTRY(Config_Manager_Init)
MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 640K
         RAM (rw) : ORIGIN = 0x20000000, LENGTH = 128K }
SECTIONS {
 .text : { *(.text*) *(.rodata*) } > FLASH
 .data : { _sdata = .; *(.data*) _edata = .; } > RAM AT> FLASH
 _sidata = LOADADDR(.data);
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
}
''')
    includes = (
        "Core/Inc", "Drivers/STM32F4xx_HAL_Driver/Inc", "Drivers/CMSIS/Include",
        "Drivers/CMSIS/Device/ST/STM32F4xx/Include",
        "Middlewares/Third_Party/FreeRTOS/Source/include",
        "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2",
        "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F",
    )
    output = build / "manager.elf"
    subprocess.run([
        gcc, "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=soft", "-Og", "-Wall", "-Wextra",
        "-Werror", "-Wno-unused-parameter", "-DUSE_HAL_DRIVER", "-DSTM32F405xx", "-nostdlib",
        *("-I" + str(ROOT / directory) for directory in includes),
        str(ROOT / "Core/Src/managers/config_manager.c"), str(ROOT / "Core/Src/drivers/eeprom.c"),
        str(ROOT / "Core/Src/data/config.c"),
        str(stubs), "-T", str(linker), "-o", str(output),
    ], cwd=ROOT, check=True)
    return output.read_bytes()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", help="Path to arm-none-eabi-gcc")
    args, remaining = parser.parse_known_args()
    gcc = args.gcc or shutil.which("arm-none-eabi-gcc")
    if gcc is None:
        settings = json.loads((ROOT / ".vscode/c_cpp_properties.json").read_text())
        gcc = settings["configurations"][0]["compilerPath"]
    ConfigManagerTests.elf_bytes = build_fixture(gcc)
    unittest.main(argv=[__file__, *remaining])
