# Config editor GUI

`pcan_config.py` loads the repository root's `config.json` and displays every
setting's address, name, default, allowed range, last board value, and editable
new value. It uses Tkinter and `python-can`, with the same PEAK PCAN driver as
the other tools.

```powershell
py -m pip install python-can
py tools/pcan_config.py
```

Choose the PCAN channel, bitrate, and target board ID, then click **Connect**.
The board ID initially comes from `config.json`; changing it selects the target
board and does not change the board's bootloader ID. Connection only opens the
adapter; it does not confirm that the board is responding or send config messages.

Click **Read all**, or a row's **Read**, to fetch board values. Enter a new value
and click that row's **Write** to persist it in EEPROM. Values accept decimal
or `0x` hexadecimal. Before sending, the editor checks that the input is a whole
number within the setting's JSON min/max and the protocol's unsigned 16-bit
range. The JSON schema itself is validated at startup, including board/address
bounds, duplicate addresses, and defaults. The tool does not edit `config.json`.

Writes wait for an acknowledgement and never automatically read back or retry.
Click **Read** to verify a write. Board values remain separate from editable
values; reads preserve any value you have manually edited. A failed read shows
an error on that row and does not prevent the other rows from being read.
Connection settings are locked until you disconnect. CAN operations run in a
worker thread to keep the window responsive.

Defaults are `PCAN_USBBUS1` and `500000` bit/s. The config path is resolved
relative to the script, so it also works when launched from another directory.
Optional overrides:

```powershell
py tools/pcan_config.py --channel PCAN_USBBUS2 --board-id 0x01 --bitrate 500000
py tools/pcan_config.py --config path/to/config.json
```

The protocol below applies to all rows: replace the low address byte `01` with
the setting's address. A successful read may return a firmware default when no
EEPROM value exists. The current firmware truncates the read reply's high byte;
the root JSON currently limits settings to 255, but schemas allowing larger
values require fixing `Config_PackReadReply` for accurate readback. Use one
config client per board because the protocol has no transaction counter. A
timed-out write may still have committed; explicitly read before retrying.

Run the hardware-free schema, CAN protocol, and Tkinter lifecycle checks with:

```powershell
py -m unittest discover -s tools/tests -p test_pcan_config.py -v
```

# PCAN ADC monitor

`pcan_adc_monitor.py` shows the CAN1 ADC voltage as a live number and scrolling
graph with fixed 0.0-3.3 V boundaries. The **Export visible CSV...** button
prompts for a filename and saves only the samples in the graph's visible time
window.

## CAN message

| Setting | Value |
|---|---|
| Bitrate | 500 kbit/s |
| Frame | Extended CAN data frame |
| CAN ID | `0x10000101` |
| DLC | 4 |
| Payload | IEEE-754 float32 voltage, little-endian |
| Nominal rate | 100 Hz (10 ms period) |

## Setup and use

Install the Windows PEAK PCAN driver/PCAN-Basic package for the adapter, then
install the Python dependencies from the repository root:

```powershell
py -m pip install -r tools/requirements.txt
```

Connect the PCAN adapter to CAN1 with the correct bus termination, then run:

```powershell
py tools/pcan_adc_monitor.py
```

Click **Export visible CSV...** to choose a filename and save the samples that
are currently visible in the graph. Earlier samples outside the displayed time
window are not included.

For console display and CSV logging without a graph window:

```powershell
py tools/pcan_adc_monitor.py --no-plot --csv adc_voltage.csv
```

The CSV has exactly two columns, `timestamp` and `voltage_v`. Timestamps are the
PC's local, timezone-aware receipt times.

The default PEAK channel is `PCAN_USBBUS1`. Select another channel if needed:

```powershell
py tools/pcan_adc_monitor.py --channel PCAN_USBBUS2
```

Use the built-in signal to check the graph and CSV output without CAN hardware:

```powershell
py tools/pcan_adc_monitor.py --demo
```

The `--csv` option remains available with `--no-plot` for continuous headless
logging. Stop a headless run with Ctrl+C.

Run `py tools/pcan_adc_monitor.py --help` for all options.

# CTRL CAN manager delay GUI

`pcan_ctrl_delay.py` reads and writes `CONFIG_CTRL_CAN_MANAGER_DELAY_MS`
(config address `1`) using PEAK PCAN. It only needs `python-can` and the
Tkinter included with the standard Windows Python installation, plus the
PEAK PCAN driver/PCAN-Basic package. The adapter must be connected to a
terminated VCU CAN bus with the application firmware running.

```powershell
py -m pip install python-can
py tools/pcan_ctrl_delay.py
```

Set the **Board ID** to the ID stored by the bootloader, choose the PCAN
channel and bitrate, enter a delay from **0 to 1000 ms**, then click
**Write delay**. A write persists in EEPROM; the CTRL CAN task reads the
setting each cycle. Writes wait for an EEPROM acknowledgement and then
read back the value. **Read stored delay** only reads EEPROM and does not
change the delay entry. An unwritten setting returns "No stored value";
the task's default is 10 ms.

Defaults are `PCAN_USBBUS1`, `500000` bit/s, board `0`, and delay `10` ms.
The connection settings can also be supplied when starting the GUI:

```powershell
py tools/pcan_ctrl_delay.py --channel PCAN_USBBUS2 --board-id 0x01 --bitrate 500000
```

The PCAN interface opens for each button action and closes when it finishes.
Communication runs in a worker thread so the GUI stays responsive. See the
[python-can PCAN documentation](https://python-can.readthedocs.io/en/stable/interfaces/pcan.html)
for adapter configuration.

## Config CAN protocol

All requests and replies are extended CAN data frames. `BB` is the one-byte
board ID; the low byte `01` selects the CTRL CAN manager delay.

| Operation | CAN ID | Payload |
|---|---|---|
| Read | `0xC0BB01` | Empty (DLC 0), not an RTR frame |
| Write | `0xC0BB01` | 16-bit delay, little-endian (DLC 2) |
| Read reply | `0xC1BB01` | Status, value low byte, value high byte (DLC 3) |
| Write reply | `0xC1BB01` | EEPROM status (DLC 1); `0` means OK |

For example, board `1`, delay `1000` ms uses extended ID `0xC00101`
with payload `E8 03`.

The current `Config_PackReadReply` in `Core/Src/managers/config_manager.c`
casts `value` to `uint8_t` before shifting right, so read replies lose the
high byte. Values above 255 ms can be written, but cannot be verified with
that firmware. The GUI reports the acknowledgement and readback mismatch
separately. The firmware expression would need to be
`msg->data[2] = (uint8_t)(value >> 8);` to return the full value. This tool
does not modify the firmware.

The firmware also repeatedly sends its last reply as debug traffic and has
no transaction counter. The tool discards queued replies before a request,
but cannot distinguish a repeated old reply sent after the new request;
use one config client per board. A timeout does not prove a write failed:
read the value before retrying. Writes are never automatically retried.

Run the hardware-free protocol checks with:

```powershell
py -m unittest discover -s tools/tests -p test_pcan_ctrl_delay.py -v
```
