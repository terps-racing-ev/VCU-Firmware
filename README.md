# TREV5 VCU

This is the repo for TREV5's VCU, which controls lights (brake, TSSI), torque control, and LV power. It's built for an STM32F405.

## Code layout

All VCU code lives in `Core/`. The breakdown of logic currently:

- `managers/`: Contains logic for management of each of the VCU's responsibilities (reading IO, communicating over CAN, controlling LV + reading metrics, torque control logic, etc).
- `data/`: Contains variables and definitions that allow inter-manager communication. The idea is managers never have to directly interact with each other; they do it through either shared variables, queues, or other FreeRTOS primitives.
- `drivers/`: Contains logic specific to something that's self contained. Examples include EEPROM handling and bootloader interaction (more to come).

## Unit testing

`testing/` contains a CMock unit test framework for the code in `Core/`. To add new test files, tell an agent to read the [AGENTS.md](testing/AGENTS.md) file in there. Tests can be run with `make test`.

## Tools

Tools for debug help/etc are in `tools/`. These are mainly CAN scripts that interact with the firmware, like scripts for reading config variables. This folder contains a lot of vibecoded GUIs and such.

## Flashing

`flashing/` contains all info relevant to flashing, and you should never have to touch this (auto-generated and owned by the trevware extension).

## Software required

| Software | Why it's needed |
| --- | --- |
| VS Code with the `trevware` extension | Build and flash the firmware. |
| GNU Make | Run builds and tests. |
| Ruby | Run unit tests with `make test`. |
| Python | Run the scripts and GUIs in `tools/`. |
| `python-can` and `matplotlib` | Communicate over CAN and graph data. |
| PEAK PCAN driver/PCAN-Basic package | Use the CAN tools with a PCAN adapter. |
| `unicorn` and `pyelftools` | Run the emulated EEPROM and config manager tests. |
| STM32CubeMX | Edit the `.ioc` configuration and regenerate code. |

Install the CAN tools' Python dependencies with `python -m pip install -r tools/requirements.txt`.
