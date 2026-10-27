#!/usr/bin/env python3
"""Display and log the ADC voltage broadcast by the VCU over CAN1."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from datetime import datetime, timedelta
import math
from pathlib import Path
import queue
import struct
import sys
import threading
import time
from typing import TextIO


DEFAULT_CAN_ID = 0x10000101
DEFAULT_BITRATE = 500_000
DEFAULT_CHANNEL = "PCAN_USBBUS1"
DEFAULT_WINDOW_SECONDS = 10.0
ADC_MIN_VOLTAGE = 0.0
ADC_MAX_VOLTAGE = 3.3


@dataclass(frozen=True)
class Sample:
    """One voltage sample and its host-side receipt time."""

    timestamp: datetime
    voltage_v: float


def parse_can_id(value: str) -> int:
    """Parse a decimal or 0x-prefixed 29-bit CAN identifier."""
    try:
        can_id = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid CAN ID: {value!r}") from exc
    if not 0 <= can_id <= 0x1FFFFFFF:
        raise argparse.ArgumentTypeError("CAN ID must be in the range 0..0x1FFFFFFF")
    return can_id


def decode_voltage(data: bytes | bytearray | memoryview) -> float:
    """Decode the firmware's four-byte little-endian float payload."""
    if len(data) != 4:
        raise ValueError(f"expected a 4-byte voltage payload, received {len(data)} bytes")
    voltage = struct.unpack("<f", data)[0]
    if not math.isfinite(voltage):
        raise ValueError("received a non-finite voltage")
    return voltage


def samples_in_visible_window(
    samples: list[Sample] | tuple[Sample, ...], window_seconds: float
) -> list[Sample]:
    """Return only samples inside the graph window ending at the newest sample."""
    if not samples:
        return []
    cutoff = samples[-1].timestamp - timedelta(seconds=window_seconds)
    return [sample for sample in samples if sample.timestamp >= cutoff]


class CsvLogger:
    """Write samples immediately so useful data survives an interrupted run."""

    def __init__(self, destination: Path | TextIO):
        self._owns_file = isinstance(destination, Path)
        self._file: TextIO
        if self._owns_file:
            destination.parent.mkdir(parents=True, exist_ok=True)
            self._file = destination.open("w", newline="", encoding="utf-8")
        else:
            self._file = destination
        self._writer = csv.writer(self._file, lineterminator="\n")
        self._writer.writerow(("timestamp", "voltage_v"))
        self._file.flush()

    def write(self, sample: Sample) -> None:
        self._writer.writerow(
            (sample.timestamp.isoformat(timespec="milliseconds"), f"{sample.voltage_v:.9g}")
        )
        self._file.flush()

    def close(self) -> None:
        if self._owns_file:
            self._file.close()

    def __enter__(self) -> "CsvLogger":
        return self

    def __exit__(self, *unused: object) -> None:
        self.close()


def receive_pcan(
    output: "queue.Queue[Sample | Exception]",
    stop: threading.Event,
    channel: str,
    bitrate: int,
    can_id: int,
) -> None:
    """Receive matching frames until stop is set; intended for a worker thread."""
    bus = None
    try:
        import can

        bus = can.Bus(
            interface="pcan",
            channel=channel,
            bitrate=bitrate,
            can_filters=[
                {"can_id": can_id, "can_mask": 0x1FFFFFFF, "extended": True}
            ],
        )
        while not stop.is_set():
            message = bus.recv(timeout=0.25)
            if message is None:
                continue
            # Keep a software check as some drivers implement filters coarsely.
            if not message.is_extended_id or message.arbitration_id != can_id:
                continue
            try:
                voltage = decode_voltage(message.data)
            except ValueError:
                continue
            output.put(Sample(datetime.now().astimezone(), voltage))
    except Exception as exc:
        output.put(exc)
    finally:
        if bus is not None:
            bus.shutdown()


def generate_demo(
    output: "queue.Queue[Sample | Exception]", stop: threading.Event
) -> None:
    """Generate a test signal using the same 100 Hz cadence as the firmware."""
    started = time.monotonic()
    next_sample = started
    while not stop.is_set():
        now = time.monotonic()
        if now >= next_sample:
            elapsed = now - started
            voltage = 1.65 + 1.5 * math.sin(elapsed * math.tau / 4.0)
            output.put(Sample(datetime.now().astimezone(), voltage))
            next_sample += 0.010
        else:
            stop.wait(min(next_sample - now, 0.010))


def start_receiver(
    args: argparse.Namespace,
    samples: "queue.Queue[Sample | Exception]",
    stop: threading.Event,
) -> threading.Thread:
    """Start either the real PCAN receiver or the demonstration source."""
    target = generate_demo if args.demo else receive_pcan
    target_args = (samples, stop) if args.demo else (
        samples,
        stop,
        args.channel,
        args.bitrate,
        args.can_id,
    )
    worker = threading.Thread(target=target, args=target_args, daemon=True)
    worker.start()
    return worker


def run_headless(args: argparse.Namespace) -> int:
    """Print samples and optionally log them without creating a plot window."""
    samples: "queue.Queue[Sample | Exception]" = queue.Queue()
    stop = threading.Event()
    logger = CsvLogger(args.csv) if args.csv is not None else None
    worker = start_receiver(args, samples, stop)
    exit_code = 0
    try:
        while True:
            try:
                item = samples.get(timeout=0.5)
            except queue.Empty:
                if not worker.is_alive():
                    break
                continue
            if isinstance(item, Exception):
                print(f"CAN error: {item}", file=sys.stderr)
                exit_code = 1
                break
            timestamp = item.timestamp.isoformat(timespec="milliseconds")
            print(f"{timestamp}  {item.voltage_v:.3f} V", flush=True)
            if logger is not None:
                logger.write(item)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        worker.join(timeout=1.0)
        if logger is not None:
            logger.close()
    return exit_code


def run_gui(args: argparse.Namespace) -> int:
    """Run the Tk/Matplotlib user interface."""
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox
        from collections import deque
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
        from matplotlib.figure import Figure
    except ImportError as exc:
        print(
            f"Missing GUI dependency: {exc}. Install with "
            "'python -m pip install -r tools/requirements.txt'.",
            file=sys.stderr,
        )
        return 2

    root = tk.Tk()
    root.title("VCU CAN1 ADC Monitor")
    root.geometry("900x560")

    voltage_text = tk.StringVar(value="Waiting for voltage...")
    status_text = tk.StringVar(
        value="Demo signal" if args.demo else f"Connecting to {args.channel}..."
    )
    tk.Label(root, textvariable=voltage_text, font=("Segoe UI", 32, "bold")).pack(
        pady=(14, 0)
    )
    tk.Label(root, textvariable=status_text, font=("Segoe UI", 10)).pack(pady=(0, 6))

    controls = tk.Frame(root)
    controls.pack(fill=tk.X, padx=10, pady=(0, 6))
    tk.Button(
        controls,
        text="Export visible CSV...",
        command=lambda: export_visible_csv(),
    ).pack(side=tk.RIGHT)

    figure = Figure(figsize=(8.8, 4.2), dpi=100, layout="constrained")
    axis = figure.add_subplot(111)
    (line,) = axis.plot([], [], color="#1565c0", linewidth=1.5)
    axis.set_xlabel("Time (seconds)")
    axis.set_ylabel("Voltage (V)")
    axis.set_ylim(ADC_MIN_VOLTAGE, ADC_MAX_VOLTAGE)
    axis.set_yticks((0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.3))
    axis.grid(True, alpha=0.3)
    canvas = FigureCanvasTkAgg(figure, master=root)
    canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 10))

    samples: "queue.Queue[Sample | Exception]" = queue.Queue()
    stop = threading.Event()
    max_samples = max(200, int(args.window * 150))
    times = deque(maxlen=max_samples)
    voltages = deque(maxlen=max_samples)
    displayed_samples = deque(maxlen=max_samples)
    first_timestamp: datetime | None = None

    worker = start_receiver(args, samples, stop)

    def export_visible_csv() -> None:
        visible = samples_in_visible_window(tuple(displayed_samples), args.window)
        if not visible:
            messagebox.showinfo("Export CSV", "There are no visible samples to export.")
            return

        destination = filedialog.asksaveasfilename(
            title="Export visible ADC samples",
            defaultextension=".csv",
            filetypes=(("CSV files", "*.csv"), ("All files", "*.*")),
            initialfile="adc_voltage.csv",
        )
        if not destination:
            return

        try:
            with CsvLogger(Path(destination)) as logger:
                for sample in visible:
                    logger.write(sample)
        except OSError as exc:
            messagebox.showerror("Export CSV", f"Could not write the CSV file:\n{exc}")
            return

        status_text.set(f"Exported {len(visible)} visible samples to {destination}")

    def update_plot() -> None:
        nonlocal first_timestamp
        changed = False
        while True:
            try:
                item = samples.get_nowait()
            except queue.Empty:
                break
            if isinstance(item, Exception):
                status_text.set(f"CAN error: {item}")
                continue
            if first_timestamp is None:
                first_timestamp = item.timestamp
            elapsed = (item.timestamp - first_timestamp).total_seconds()
            times.append(elapsed)
            voltages.append(item.voltage_v)
            displayed_samples.append(item)
            voltage_text.set(f"{item.voltage_v:.3f} V")
            status_text.set(
                f"Last update: {item.timestamp.isoformat(timespec='milliseconds')}"
            )
            changed = True

        if changed:
            line.set_data(times, voltages)
            newest = times[-1]
            axis.set_xlim(max(0.0, newest - args.window), max(args.window, newest))
            canvas.draw_idle()
        if not stop.is_set():
            root.after(50, update_plot)

    def close() -> None:
        stop.set()
        worker.join(timeout=1.0)
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", close)
    root.after(50, update_plot)
    try:
        root.mainloop()
    except KeyboardInterrupt:
        close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Display and optionally CSV-log the VCU ADC voltage from a PCAN adapter."
    )
    parser.add_argument("--channel", default=DEFAULT_CHANNEL, help="python-can PCAN channel")
    parser.add_argument("--bitrate", type=int, default=DEFAULT_BITRATE, help="CAN bitrate")
    parser.add_argument(
        "--can-id", type=parse_can_id, default=DEFAULT_CAN_ID, help="extended voltage CAN ID"
    )
    parser.add_argument(
        "--window", type=float, default=DEFAULT_WINDOW_SECONDS, help="visible graph width in seconds"
    )
    parser.add_argument(
        "--csv",
        type=Path,
        metavar="FILE",
        help="with --no-plot, continuously write timestamp and voltage_v to FILE",
    )
    parser.add_argument(
        "--demo", action="store_true", help="use a generated signal without CAN hardware"
    )
    parser.add_argument(
        "--no-plot", action="store_true", help="print/log samples without opening the graph"
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.bitrate <= 0:
        print("--bitrate must be positive", file=sys.stderr)
        return 2
    if args.window <= 0:
        print("--window must be positive", file=sys.stderr)
        return 2
    if args.csv is not None and not args.no_plot:
        print(
            "--csv is only available with --no-plot; "
            "use the 'Export visible CSV...' button in the graph window.",
            file=sys.stderr,
        )
        return 2
    return run_headless(args) if args.no_plot else run_gui(args)


if __name__ == "__main__":
    raise SystemExit(main())
