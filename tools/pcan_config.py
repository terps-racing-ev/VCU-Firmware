#!/usr/bin/env python3
"""Edit the root config.json's board settings over PEAK PCAN.

Run: py tools/pcan_config.py
Requires python-can, Tkinter, and the PEAK PCAN driver/PCAN-Basic package.
Connecting never sends a config request. Reads and writes are explicit actions.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk

import can


DEFAULT_CONFIG = Path(__file__).resolve().parents[1] / "config.json"
REQUEST_BASE = 0xC00000
REPLY_BASE = 0xC10000
STATUS_NAMES = {
    1: "No stored value / unknown address",
    2: "EEPROM_INVALID_ARGUMENT",
    3: "EEPROM_NOT_INITIALIZED",
    4: "EEPROM_FLASH_ERROR",
}


def integer(text: str) -> int:
    """Accept decimal (including 08) and 0x-prefixed hex, but not floats."""
    text = text.strip()
    try:
        return int(text, 16 if text.lower().startswith("0x") else 10)
    except ValueError:
        raise ValueError("Enter a whole number in decimal or 0x-prefixed hex.") from None


def bounded(value: int, low: int, high: int, label: str) -> int:
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{label} must be a whole number from {low} to {high}.")
    return value


@dataclass(frozen=True)
class ConfigValue:
    name: str
    address: int
    default_value: int
    min_value: int
    max_value: int

    def validate(self, value: int) -> int:
        bounded(self.address, 0, 255, "Config address")
        bounded(self.min_value, 0, 65535, "Minimum")
        bounded(self.max_value, self.min_value, 65535, "Maximum")
        return bounded(value, self.min_value, self.max_value, self.name)


@dataclass(frozen=True)
class ConfigSchema:
    board_id: int
    values: tuple[ConfigValue, ...]


def load_config(path: Path) -> ConfigSchema:
    """Validate the complete schema before enabling any CAN operations."""
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict):
        raise ValueError("Config must be a JSON object.")
    board = bounded(data.get("board_id"), 0, 255, "board_id")
    rows = data.get("values")
    if not isinstance(rows, list) or not rows:
        raise ValueError("Config values must be a nonempty list.")
    values = []
    addresses = set()
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"values[{index}] must be an object.")
        required = ("name", "address", "default_value", "min_value", "max_value")
        if any(key not in row for key in required):
            raise ValueError(f"values[{index}] requires {', '.join(required)}.")
        setting = ConfigValue(**{key: row[key] for key in required})
        if not isinstance(setting.name, str) or not setting.name.strip():
            raise ValueError(f"values[{index}].name must be a nonempty string.")
        setting.validate(setting.default_value)
        if setting.address in addresses:
            raise ValueError(f"Duplicate config address {setting.address}.")
        addresses.add(setting.address)
        values.append(setting)
    return ConfigSchema(board, tuple(values))


class Cancelled(Exception):
    pass


def request(bus, board_id: int, setting: ConfigValue, value: int | None = None,
            timeout: float = 5.0, cancel: threading.Event | None = None) -> int | None:
    """Perform one validated request. Writes await ACK only; never retry or read."""
    bounded(board_id, 0, 255, "Board ID")
    bounded(setting.address, 0, 255, "Config address")
    if value is not None:
        setting.validate(value)
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("Timeout must be finite and positive.")

    def check_cancel():
        if cancel is not None and cancel.is_set():
            raise Cancelled("Operation stopped; a sent write may still have committed.")

    check_cancel()
    # Discard queued replies; no transaction counter exists in this protocol.
    drain_until = time.monotonic() + 0.05
    for _ in range(256):
        check_cancel()
        if time.monotonic() >= drain_until or bus.recv(timeout=0) is None:
            break
    check_cancel()
    bus.send(can.Message(
        arbitration_id=REQUEST_BASE | (board_id << 8) | setting.address,
        is_extended_id=True,
        data=b"" if value is None else value.to_bytes(2, "little"),
        check=True,
    ), timeout=timeout)
    expected_length = 3 if value is None else 1
    reply_id = REPLY_BASE | (board_id << 8) | setting.address
    deadline = time.monotonic() + timeout
    while (remaining := deadline - time.monotonic()) > 0:
        check_cancel()
        reply = bus.recv(timeout=min(remaining, 0.1))
        if reply is None:
            continue
        if (not reply.is_extended_id or reply.arbitration_id != reply_id
                or reply.is_remote_frame or reply.is_error_frame or reply.is_fd
                or reply.dlc != expected_length or len(reply.data) != expected_length):
            continue
        status = reply.data[0]
        if status:
            raise RuntimeError(STATUS_NAMES.get(status, f"Unknown EEPROM status {status}"))
        return int.from_bytes(reply.data[1:3], "little") if value is None else None
    action = "read reply" if value is None else "write acknowledgement"
    detail = " A timed-out write may still have committed; read before retrying." if value is not None else ""
    raise TimeoutError(f"No {action} from board {board_id}, address {setting.address}. "
                       f"Check board ID, bitrate, wiring and application firmware.{detail}")


class ConfigGui:
    def __init__(self, root: tk.Tk, schema: ConfigSchema, args: argparse.Namespace):
        self.root = root
        self.schema = schema
        self.bus = None
        self.connected = False
        self.busy = False
        self.closing = False
        self.cancel = threading.Event()
        self.events = queue.Queue()
        self.connection_controls = []
        self.action_controls = []
        self.rows = {}
        root.title("VCU Configuration")
        root.geometry("1120x530")
        root.minsize(920, 420)
        panel = ttk.Frame(root, padding=16)
        panel.pack(fill="both", expand=True)
        ttk.Label(panel, text="VCU configuration", font=("Segoe UI", 16, "bold")).pack(anchor="w")
        ttk.Label(panel, text=str(args.config)).pack(anchor="w", pady=(2, 12))

        connection = ttk.Frame(panel)
        connection.pack(fill="x")
        self.channel = tk.StringVar(value=args.channel)
        self.bitrate = tk.StringVar(value=str(args.bitrate))
        self.board = tk.StringVar(value=str(schema.board_id) if args.board_id is None else args.board_id)
        for label, variable, width in (("PCAN channel", self.channel, 19),
                                       ("Bitrate", self.bitrate, 10), ("Board ID", self.board, 8)):
            ttk.Label(connection, text=label).pack(side="left", padx=(0, 5))
            entry = ttk.Entry(connection, textvariable=variable, width=width)
            entry.pack(side="left", padx=(0, 12))
            self.connection_controls.append(entry)
        self.connect_button = ttk.Button(connection, text="Connect", command=self.toggle_connection)
        self.connect_button.pack(side="left", padx=(0, 8))
        read_all = ttk.Button(connection, text="Read all", command=self.read_all)
        read_all.pack(side="left")
        self.action_controls.append(read_all)

        table = ttk.Frame(panel)
        table.pack(fill="both", expand=True, pady=14)
        canvas = tk.Canvas(table, highlightthickness=0)
        vertical = ttk.Scrollbar(table, orient="vertical", command=canvas.yview)
        horizontal = ttk.Scrollbar(table, orient="horizontal", command=canvas.xview)
        canvas.configure(yscrollcommand=vertical.set, xscrollcommand=horizontal.set)
        canvas.grid(row=0, column=0, sticky="nsew")
        vertical.grid(row=0, column=1, sticky="ns")
        horizontal.grid(row=1, column=0, sticky="ew")
        table.rowconfigure(0, weight=1)
        table.columnconfigure(0, weight=1)
        grid = ttk.Frame(canvas)
        window = canvas.create_window((0, 0), window=grid, anchor="nw")
        grid.bind("<Configure>", lambda _: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda event: canvas.itemconfigure(
            window, width=max(event.width, grid.winfo_reqwidth())))
        grid.columnconfigure(1, weight=1)
        for column, label in enumerate(("Address", "Config value", "Default", "Range",
                                         "Board value", "New value", "Actions", "Status")):
            ttk.Label(grid, text=label, font=("Segoe UI", 9, "bold")).grid(
                row=0, column=column, sticky="w", padx=7, pady=(0, 9))
        for row_number, setting in enumerate(schema.values, 1):
            draft = tk.StringVar(value=str(setting.default_value))
            actual = tk.StringVar(value="Not read")
            status = tk.StringVar(value="")
            row = {"draft": draft, "actual": actual, "status": status, "dirty": False}
            self.rows[setting.address] = row
            draft.trace_add("write", lambda *_, r=row: r.update(dirty=True))
            for column, text in enumerate((f"0x{setting.address:02X}", setting.name,
                                             str(setting.default_value),
                                             f"{setting.min_value}..{setting.max_value}")):
                ttk.Label(grid, text=text).grid(row=row_number, column=column, sticky="w", padx=7, pady=8)
            ttk.Label(grid, textvariable=actual, width=10).grid(row=row_number, column=4, padx=7)
            entry = ttk.Entry(grid, textvariable=draft, width=10)
            entry.grid(row=row_number, column=5, padx=7)
            self.action_controls.append(entry)
            buttons = ttk.Frame(grid)
            buttons.grid(row=row_number, column=6, padx=7)
            for label, command in (("Read", lambda s=setting: self.read_values((s,))),
                                   ("Write", lambda s=setting: self.write_value(s))):
                button = ttk.Button(buttons, text=label, width=6, command=command)
                button.pack(side="left", padx=2)
                self.action_controls.append(button)
            ttk.Label(grid, textvariable=status, wraplength=190, width=26).grid(
                row=row_number, column=7, sticky="w", padx=7, pady=8)

        ttk.Label(panel, text="Writes persist in EEPROM. Read buttons fetch board values; connecting and writing do not read back.\n"
                  "New values accept decimal or 0x hex and must fit the JSON range. Board ID is a connection target.",
                  wraplength=1050).pack(anchor="w")
        self.status = tk.StringVar(value="Disconnected. Connect, then click Read all to fetch board values.")
        ttk.Label(panel, textvariable=self.status, wraplength=1050).pack(anchor="w", pady=(10, 0))
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.update_controls()
        root.after(50, self.poll)

    def update_controls(self):
        for control in self.connection_controls:
            control.state(["disabled"] if self.connected or self.busy else ["!disabled"])
        for control in self.action_controls:
            control.state(["!disabled"] if self.connected and not self.busy else ["disabled"])
        self.connect_button.configure(text="Disconnect" if self.connected else "Connect")
        self.connect_button.state(["disabled"] if self.busy else ["!disabled"])

    def start(self, work):
        if self.busy or self.closing:
            return
        self.busy = True
        self.cancel.clear()
        self.update_controls()

        def run():
            try:
                work()
            except Exception as exc:
                self.events.put(("error", str(exc)))
            finally:
                self.events.put(("done", None))

        threading.Thread(target=run, daemon=True).start()

    def toggle_connection(self):
        if self.busy:
            return
        if self.connected:
            self.status.set("Disconnecting...")

            def disconnect():
                try:
                    self.bus.shutdown()
                finally:
                    self.bus = None
                    self.events.put(("connection", False))

            self.start(disconnect)
            return
        try:
            board_id = bounded(integer(self.board.get()), 0, 255, "Board ID")
            bitrate = bounded(integer(self.bitrate.get()), 1, 1_000_000, "Bitrate")
            channel = self.channel.get().strip()
            if not channel:
                raise ValueError("Enter a PCAN channel.")
        except ValueError as exc:
            self.status.set(f"Invalid connection: {exc}")
            return
        self.target_board = board_id
        self.status.set(f"Opening {channel} for board {board_id}...")

        def connect():
            self.bus = can.Bus(
                interface="pcan", channel=channel, bitrate=bitrate,
                ignore_config=True, fd=False, receive_own_messages=False,
                can_filters=[{"can_id": REPLY_BASE | (board_id << 8),
                              "can_mask": 0x1FFFFF00, "extended": True}],
            )
            self.events.put(("connection", True))

        self.start(connect)

    def read_all(self):
        self.read_values(self.schema.values)

    def read_values(self, settings):
        if not self.connected or self.busy or self.closing:
            return
        self.status.set("Reading board values...")
        for setting in settings:
            self.rows[setting.address]["actual"].set("Reading...")

        def read():
            failed = 0
            for setting in settings:
                try:
                    value = request(self.bus, self.target_board, setting, cancel=self.cancel)
                    self.events.put(("read", (setting, value)))
                except Cancelled:
                    raise
                except Exception as exc:
                    failed += 1
                    self.events.put(("row_error", (setting.address, str(exc))))
            self.events.put(("status", f"Read complete: {len(settings) - failed}/{len(settings)} values received."
                             if failed else f"Read complete: {len(settings)} values received."))

        self.start(read)

    def write_value(self, setting):
        if not self.connected or self.busy or self.closing:
            return
        row = self.rows[setting.address]
        try:
            value = setting.validate(integer(row["draft"].get()))
        except ValueError as exc:
            row["status"].set(str(exc))
            self.status.set(f"Invalid input: {exc} No CAN message sent.")
            return
        # A write timeout may still mean the board changed. Invalidate old data
        # before sending and never label an ACK as a verified readback.
        row["actual"].set("Not read")
        row["status"].set("Writing...")
        self.status.set(f"Writing {setting.name} = {value}...")

        def write():
            try:
                request(self.bus, self.target_board, setting, value, cancel=self.cancel)
                self.events.put(("written", (setting.address, value)))
            except Exception as exc:
                self.events.put(("row_error", (setting.address, str(exc))))
                raise

        self.start(write)

    def poll(self):
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "done":
                self.busy = False
                self.update_controls()
            elif kind == "connection":
                self.connected = payload
                for row in self.rows.values():
                    row["actual"].set("Not read")
                    row["status"].set("")
                self.status.set(f"Adapter connected for board {self.target_board}. Click Read all to fetch values."
                                if payload else "Disconnected.")
            elif kind == "read":
                setting, value = payload
                row = self.rows[setting.address]
                row["actual"].set(str(value))
                try:
                    setting.validate(value)
                except ValueError:
                    row["status"].set("Read OK; outside JSON range")
                else:
                    row["status"].set("Read OK")
                    if not row["dirty"]:
                        row["draft"].set(str(value))
                        row["dirty"] = False
            elif kind == "written":
                address, value = payload
                self.rows[address]["status"].set(f"ACK: {value}; click Read to verify")
                self.status.set(f"Write acknowledged for address {address}: {value}. Click Read to verify.")
            elif kind == "row_error":
                address, error = payload
                self.rows[address]["actual"].set("Unknown")
                self.rows[address]["status"].set(error)
            elif kind == "error":
                self.status.set(f"Error: {payload}")
            elif kind == "status":
                self.status.set(payload)
        if self.closing and not self.busy:
            if self.connected:
                # Allow the ordinary disconnect worker to release the adapter.
                self.closing = False
                self.toggle_connection()
                self.closing = True
            else:
                self.root.destroy()
                return
        self.root.after(50, self.poll)

    def close(self):
        self.closing = True
        self.cancel.set()
        self.status.set("Closing and releasing the CAN interface...")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--channel", default="PCAN_USBBUS1")
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument("--board-id", help="Override config.json board_id (decimal or 0x hex)")
    args = parser.parse_args()
    try:
        schema = load_config(args.config)
    except (OSError, ValueError, TypeError) as exc:
        parser.error(f"Cannot load {args.config}: {exc}")
    root = tk.Tk()
    ConfigGui(root, schema, args)
    root.mainloop()


if __name__ == "__main__":
    main()
