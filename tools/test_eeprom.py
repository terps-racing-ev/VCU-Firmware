"""Exercise the compiled Cortex-M4 EEPROM driver with simulated STM32 flash.

Requires: pip install unicorn pyelftools
Run: python tools/test_eeprom.py --gcc <path/to/arm-none-eabi-gcc>
The compiler defaults to the workspace's VS Code compilerPath setting.
Only build/eeprom-tests is written; no connected hardware is accessed.
"""

import argparse
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import unittest

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB
from unicorn.arm_const import (
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1,
    UC_ARM_REG_R2, UC_ARM_REG_SP,
)


ROOT = Path(__file__).resolve().parents[1]
PAGE_SIZE = 0x20000
BASE = 0x080A0000
MAGIC = 0x45455031
CAPACITY = (PAGE_SIZE - 16) // 12
ERASED = b"\xff" * (2 * PAGE_SIZE)
OK, NOT_FOUND, INVALID, NOT_INITIALIZED, FLASH_ERROR = range(5)


def record(address, value):
    return struct.pack("<III", value, value ^ 0xFFFFFFFF,
                       ((address ^ 0xFFFF) << 16) | address)


def full_page(generation):
    page = bytearray(b"\xff" * PAGE_SIZE)
    struct.pack_into("<IIII", page, 0, MAGIC, generation, generation ^ 0xFFFFFFFF, 0)
    for index in range(CAPACITY):
        page[16 + index * 12:28 + index * 12] = record(index % 256, index)
    return bytes(page)


class PowerCut(Exception):
    pass


class FlashMachine:
    def __init__(self, elf_bytes, flash=ERASED):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(0x08000000, 0x100000)
        self.uc.mem_map(0x20000000, 0x20000)
        self.uc.mem_map(0x40023000, 0x1000)
        elf = ELFFile(io.BytesIO(elf_bytes))
        self.symbols = {
            symbol.name: symbol.entry.st_value
            for symbol in elf.get_section_by_name(".symtab").iter_symbols()
        }
        for segment in elf.iter_segments():
            if segment.header.p_type == "PT_LOAD" and segment.header.p_filesz:
                self.uc.mem_write(segment.header.p_vaddr, segment.data())
        self.uc.mem_write(BASE, flash)
        # Enable the data-cache branch; memory/cache timing itself is not emulated.
        self.uc.mem_write(0x40023C00, struct.pack("<I", 1 << 10))
        self.locked = True
        self.operations = 0
        self.cut_at = None
        self.partial_cut = False
        self.fail_at = None
        self.hooks = {
            self.symbols[name] & ~1: name for name in (
                "HAL_FLASH_Unlock", "HAL_FLASH_Lock", "HAL_FLASH_Program",
                "HAL_FLASHEx_Erase",
            )
        }
        self.uc.hook_add(UC_HOOK_CODE, self._hook)

    def _hook(self, uc, address, size, user_data):
        if address == 0x08010000:
            uc.emu_stop()
            return
        name = self.hooks.get(address)
        if name is None:
            return
        result = 0
        if name == "HAL_FLASH_Unlock":
            self.locked = False
        elif name == "HAL_FLASH_Lock":
            self.locked = True
        else:
            assert not self.locked, "attempted to modify locked flash"
            self.operations += 1
            if self.operations == self.fail_at:
                uc.reg_write(UC_ARM_REG_R0, 1)
                uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
                return
            interrupted = self.operations == self.cut_at
            if name == "HAL_FLASH_Program":
                kind = uc.reg_read(UC_ARM_REG_R0)
                target = uc.reg_read(UC_ARM_REG_R1)
                value = uc.reg_read(UC_ARM_REG_R2)
                assert kind == 2 and target % 4 == 0
                assert BASE <= target <= BASE + 2 * PAGE_SIZE - 4
                old = struct.unpack("<I", uc.mem_read(target, 4))[0]
                assert old & value == value, "attempted to set a programmed flash bit"
                if not interrupted or self.partial_cut:
                    if interrupted:
                        # One target zero bit remains erased, including at commit words.
                        remaining = (~value & old) & 0xFFFFFFFF
                        value |= remaining & -remaining
                    uc.mem_write(target, struct.pack("<I", value))
            else:
                erase = uc.reg_read(UC_ARM_REG_R0)
                kind, bank, sector, count, voltage = struct.unpack("<IIIII", uc.mem_read(erase, 20))
                assert kind == 0 and sector in (9, 10) and count == 1 and voltage == 2
                target = BASE + (sector - 9) * PAGE_SIZE
                if not interrupted or self.partial_cut:
                    length = PAGE_SIZE // 2 if interrupted else PAGE_SIZE
                    uc.mem_write(target, b"\xff" * length)
                uc.mem_write(uc.reg_read(UC_ARM_REG_R1), b"\xff" * 4)
            if interrupted:
                raise PowerCut()
        uc.reg_write(UC_ARM_REG_R0, result)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def call(self, name, *args):
        self.uc.reg_write(UC_ARM_REG_SP, 0x2001FFF0)
        self.uc.reg_write(UC_ARM_REG_LR, 0x08010001)
        for register, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1), args):
            self.uc.reg_write(register, value)
        self.uc.emu_start(self.symbols[name] | 1, 0, count=20_000_000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == 0x08010000, "driver did not return"
        return self.uc.reg_read(UC_ARM_REG_R0)

    def read(self, address):
        output = 0x20018000
        self.uc.mem_write(output, struct.pack("<I", 0x12345678))
        status = self.call("EEPROM_ReadVariable", address, output)
        return status, struct.unpack("<I", self.uc.mem_read(output, 4))[0]

    def snapshot(self):
        return bytes(self.uc.mem_read(BASE, 2 * PAGE_SIZE))


class EEPROMTests(unittest.TestCase):
    elf_bytes = None

    def machine(self, flash=ERASED):
        return FlashMachine(self.elf_bytes, flash)

    def test_read_write_reboot_and_arguments(self):
        machine = self.machine()
        self.assertEqual(machine.read(0), (NOT_INITIALIZED, 0x12345678))
        self.assertEqual(machine.call("EEPROM_WriteVariable", 0, 42), NOT_INITIALIZED)
        self.assertEqual(machine.call("EEPROM_Init"), OK)
        self.assertEqual(machine.read(0), (NOT_FOUND, 0x12345678))
        self.assertEqual(machine.read(256), (INVALID, 0x12345678))
        self.assertEqual(machine.call("EEPROM_ReadVariable", 0, 0), INVALID)
        self.assertEqual(machine.call("EEPROM_WriteVariable", 256, 42), INVALID)
        for address in range(256):
            self.assertEqual(machine.call("EEPROM_WriteVariable", address, address * 17), OK)
        for value in (0, 0xFFFFFFFF, 0x98765432):
            self.assertEqual(machine.call("EEPROM_WriteVariable", 255, value), OK)
            self.assertEqual(machine.read(255), (OK, value))
        before = machine.operations
        self.assertEqual(machine.call("EEPROM_WriteVariable", 255, 0x98765432), OK)
        self.assertEqual(machine.operations, before)
        reboot = self.machine(machine.snapshot())
        self.assertEqual(reboot.call("EEPROM_Init"), OK)
        for address in range(255):
            self.assertEqual(reboot.read(address), (OK, address * 17))
        self.assertEqual(reboot.read(255), (OK, 0x98765432))
        self.assertTrue(machine.locked)

    def test_rollover_preserves_all_variables(self):
        machine = self.machine(full_page(7) + b"\xff" * PAGE_SIZE)
        self.assertEqual(machine.call("EEPROM_Init"), OK)
        expected = {index % 256: index for index in range(CAPACITY)}
        self.assertEqual(machine.call("EEPROM_WriteVariable", 3, 0xDEADBEEF), OK)
        expected[3] = 0xDEADBEEF
        for address, value in expected.items():
            self.assertEqual(machine.read(address), (OK, value))
        reboot = self.machine(machine.snapshot())
        self.assertEqual(reboot.call("EEPROM_Init"), OK)
        for address, value in expected.items():
            self.assertEqual(reboot.read(address), (OK, value))
        header = struct.unpack("<IIII", reboot.snapshot()[PAGE_SIZE:PAGE_SIZE + 16])
        self.assertEqual(header, (MAGIC, 8, 0xFFFFFFF7, 0))

    def test_append_power_cuts_and_retry(self):
        original = self.machine()
        self.assertEqual(original.call("EEPROM_Init"), OK)
        self.assertEqual(original.call("EEPROM_WriteVariable", 7, 123), OK)
        snapshot = original.snapshot()
        for partial in (False, True):
            for operation in range(1, 4):
                with self.subTest(partial=partial, operation=operation):
                    machine = self.machine(snapshot)
                    self.assertEqual(machine.call("EEPROM_Init"), OK)
                    machine.cut_at = operation
                    machine.partial_cut = partial
                    with self.assertRaises(PowerCut):
                        machine.call("EEPROM_WriteVariable", 7, 456)
                    reboot = self.machine(machine.snapshot())
                    self.assertEqual(reboot.call("EEPROM_Init"), OK)
                    self.assertEqual(reboot.read(7), (OK, 123))
                    self.assertEqual(reboot.call("EEPROM_WriteVariable", 7, 789), OK)
                    self.assertEqual(reboot.read(7), (OK, 789))

    def test_first_initialization_power_cuts(self):
        for partial in (False, True):
            for operation in range(1, 5):
                with self.subTest(partial=partial, operation=operation):
                    machine = self.machine()
                    machine.cut_at = operation
                    machine.partial_cut = partial
                    with self.assertRaises(PowerCut):
                        machine.call("EEPROM_Init")
                    reboot = self.machine(machine.snapshot())
                    self.assertEqual(reboot.call("EEPROM_Init"), OK)
                    self.assertEqual(reboot.read(0), (NOT_FOUND, 0x12345678))

    def test_transfer_power_cuts_and_retry(self):
        # Both pages valid: the older destination must be safely erased first.
        snapshot = full_page(11) + full_page(10)
        expected = {index % 256: index for index in range(CAPACITY)}
        complete = self.machine(snapshot)
        self.assertEqual(complete.call("EEPROM_Init"), OK)
        self.assertEqual(complete.call("EEPROM_WriteVariable", 9, 99999), OK)
        total = complete.operations
        # Cover erase, each header word, early/middle/late copy, and page commit.
        for partial in (False, True):
            for operation in (1, 2, 3, 4, 5, 6, 7, 390, total - 3, total - 2, total - 1, total):
                with self.subTest(partial=partial, operation=operation):
                    machine = self.machine(snapshot)
                    self.assertEqual(machine.call("EEPROM_Init"), OK)
                    machine.cut_at = operation
                    machine.partial_cut = partial
                    with self.assertRaises(PowerCut):
                        machine.call("EEPROM_WriteVariable", 9, 99999)
                    reboot = self.machine(machine.snapshot())
                    self.assertEqual(reboot.call("EEPROM_Init"), OK)
                    for address, value in expected.items():
                        self.assertEqual(reboot.read(address), (OK, value))
                    self.assertEqual(reboot.call("EEPROM_WriteVariable", 9, 55555), OK)
                    self.assertEqual(reboot.read(9), (OK, 55555))

    def test_generation_wrap(self):
        snapshot = full_page(0xFFFFFFFF) + full_page(0xFFFFFFFE)
        machine = self.machine(snapshot)
        self.assertEqual(machine.call("EEPROM_Init"), OK)
        self.assertEqual(machine.call("EEPROM_WriteVariable", 8, 88888), OK)
        reboot = self.machine(machine.snapshot())
        self.assertEqual(reboot.call("EEPROM_Init"), OK)
        self.assertEqual(reboot.read(8), (OK, 88888))

    def test_flash_failures_relock_and_require_init(self):
        for operation in (1, 2, 3):
            with self.subTest(operation=operation):
                machine = self.machine()
                self.assertEqual(machine.call("EEPROM_Init"), OK)
                machine.fail_at = machine.operations + operation
                self.assertEqual(machine.call("EEPROM_WriteVariable", 2, 42), FLASH_ERROR)
                self.assertTrue(machine.locked)
                self.assertEqual(machine.read(2)[0], NOT_INITIALIZED)
                machine.fail_at = None
                self.assertEqual(machine.call("EEPROM_Init"), OK)
                self.assertEqual(machine.call("EEPROM_WriteVariable", 2, 43), OK)
                self.assertEqual(machine.read(2), (OK, 43))

    def test_unknown_storage_and_firmware_overlap_are_rejected(self):
        dirty = bytearray(ERASED)
        dirty[64:68] = b"data"
        machine = self.machine(bytes(dirty))
        self.assertEqual(machine.call("EEPROM_Init"), FLASH_ERROR)
        self.assertEqual(machine.operations, 0)
        # The alternate fixture puts the load-image end inside EEPROM.
        overlap = FlashMachine(build_fixture(GCC, overlap=True), ERASED)
        self.assertEqual(overlap.call("EEPROM_Init"), FLASH_ERROR)
        self.assertEqual(overlap.operations, 0)


def build_fixture(gcc, overlap=False):
    build = ROOT / "build" / "eeprom-tests"
    build.mkdir(parents=True, exist_ok=True)
    stubs = build / "hal_stubs.c"
    stubs.write_text('''#include "stm32f4xx_hal.h"
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_ERROR; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_ERROR; }
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t t, uint32_t a, uint64_t d) { return HAL_ERROR; }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *s) { return HAL_ERROR; }
''')
    linker = build / "fixture.ld"
    linker.write_text('''ENTRY(EEPROM_Init)
MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 640K
         RAM (rw) : ORIGIN = 0x20000000, LENGTH = 128K }
SECTIONS {
 .text : { *(.text*) *(.rodata*) } > FLASH
 .data : { _sdata = .; *(.data*) _edata = .; } > RAM AT> FLASH
 _sidata = ''' + ("0x080A1000" if overlap else "LOADADDR(.data)") + ''';
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
}
''')
    output = build / ("overlap.elf" if overlap else "driver.elf")
    includes = (
        "Core/Inc", "Drivers/STM32F4xx_HAL_Driver/Inc", "Drivers/CMSIS/Include",
        "Drivers/CMSIS/Device/ST/STM32F4xx/Include",
    )
    subprocess.run([
        gcc, "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=soft", "-Og", "-Wall", "-Wextra",
        "-Werror", "-Wno-unused-parameter", "-DUSE_HAL_DRIVER", "-DSTM32F405xx", "-nostdlib",
        *("-I" + str(ROOT / directory) for directory in includes),
        str(ROOT / "Core/Src/drivers/eeprom.c"), str(stubs), "-T", str(linker), "-o", str(output),
    ], cwd=ROOT, check=True)
    return output.read_bytes()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", help="Path to arm-none-eabi-gcc")
    args, remaining = parser.parse_known_args()
    GCC = args.gcc or shutil.which("arm-none-eabi-gcc")
    if GCC is None:
        settings = json.loads((ROOT / ".vscode/c_cpp_properties.json").read_text())
        GCC = settings["configurations"][0]["compilerPath"]
    EEPROMTests.elf_bytes = build_fixture(GCC)
    unittest.main(argv=[__file__, *remaining])
