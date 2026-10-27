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
