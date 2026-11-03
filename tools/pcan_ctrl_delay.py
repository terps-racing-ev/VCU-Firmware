#!/usr/bin/env python3
"""Read/write CONFIG_CTRL_CAN_MANAGER_DELAY_MS through PEAK PCAN.

Install: py -m pip install python-can
Run:     py tools/pcan_ctrl_delay.py
Requires the PEAK PCAN driver/PCAN-Basic and Python with Tkinter.
"""

from __future__ import annotations

import argparse
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk

import can


CONFIG_ADDRESS = 1
REQUEST_BASE = 0xC00000
REPLY_BASE = 0xC10000
TIMEOUT_SECONDS = 5.0
STATUS_NAMES = {
    1: "No stored value (the CTRL task uses its default of 10 ms)",
    2: "EEPROM_INVALID_ARGUMENT",
    3: "EEPROM_NOT_INITIALIZED",
    4: "EEPROM_FLASH_ERROR",
}


def parse_integer(text: str) -> int:
    """Accept decimal or 0x-prefixed hexadecimal, including decimal 08."""
    text = text.strip()
    return int(text, 16 if text.lower().startswith("0x") else 10)


def request(bus, board_id: int, value: int | None = None,
            timeout: float = TIMEOUT_SECONDS) -> int | None:
    """Send one request; return a read value or wait for a write acknowledgement.

    The firmware has no transaction counter. Run only one config client per board.
    """
    if not 0 <= board_id <= 255:
        raise ValueError("Board ID must be 0..255 (decimal or 0x-prefixed hex).")
    if value is not None and not 0 <= value <= 1000:
        raise ValueError("Delay must be a whole number from 0 to 1000 ms.")
    if timeout <= 0:
        raise ValueError("Timeout must be positive.")

    # Discard queued replies from previous operations. Bound this because the
    # current firmware repeatedly broadcasts its last config reply for debugging.
    drain_until = time.monotonic() + 0.05
    for _ in range(256):
        if time.monotonic() >= drain_until or bus.recv(timeout=0) is None:
            break

    reply_id = REPLY_BASE | (board_id << 8) | CONFIG_ADDRESS
    payload = b"" if value is None else value.to_bytes(2, "little")
    bus.send(can.Message(
        arbitration_id=REQUEST_BASE | (board_id << 8) | CONFIG_ADDRESS,
        is_extended_id=True,
        data=payload,
        check=True,
    ), timeout=timeout)

    expected_length = 3 if value is None else 1
    deadline = time.monotonic() + timeout
    while (remaining := deadline - time.monotonic()) > 0:
        reply = bus.recv(timeout=min(remaining, 0.1))
        if reply is None:
            continue
        if (not reply.is_extended_id or reply.arbitration_id != reply_id
                or reply.is_remote_frame or reply.is_error_frame or reply.is_fd
                or reply.dlc != expected_length or len(reply.data) != expected_length):
            continue
        status = reply.data[0]
        if status != 0:
            raise RuntimeError(STATUS_NAMES.get(status, f"Unknown EEPROM status {status}"))
        return int.from_bytes(reply.data[1:3], "little") if value is None else None

    action = "read reply" if value is None else "write acknowledgement"
    raise TimeoutError(
        f"No {action} from board {board_id} after {timeout:g} s. "
        "Check board ID, bitrate, wiring and running application firmware. "
        "A timed-out write may still have committed; read before retrying."
    )


def change_delay(bus, board_id: int, value: int) -> str:
    """Write once, then read back; never report an unverified value as verified."""
    request(bus, board_id, value)
    try:
        actual = request(bus, board_id)
    except Exception as exc:
        return f"Write acknowledged for {value} ms; readback failed: {exc}"
    if actual == value:
        return f"Write acknowledged and readback matched: {value} ms."
    return (
        f"Write acknowledged for {value} ms, but readback returned {actual} ms. "
        "Value is not verified. Current firmware truncates read replies above 255 ms."
    )


class DelayGui:
    def __init__(self, root: tk.Tk, args: argparse.Namespace):
        self.root = root
        self.busy = False
        self.closing = False
        self.results: queue.Queue[tuple[bool, str]] = queue.Queue()
        root.title("VCU CTRL CAN Manager Delay")
        root.resizable(False, False)

        panel = ttk.Frame(root, padding=18)
        panel.grid(sticky="nsew")
        ttk.Label(panel, text="CTRL CAN manager delay", font=("Segoe UI", 16, "bold")).grid(
            row=0, column=0, columnspan=2, sticky="w", pady=(0, 14))
        self.channel = tk.StringVar(value=args.channel)
        self.bitrate = tk.StringVar(value=str(args.bitrate))
        self.board = tk.StringVar(value=args.board_id)
        self.delay = tk.StringVar(value="10")
        self.controls = []
        fields = [
            ("PCAN channel", self.channel),
            ("Bitrate (bit/s)", self.bitrate),
            ("Board ID (0..255 or 0x hex)", self.board),
            ("Delay (0..1000 ms)", self.delay),
        ]
        for row, (label, variable) in enumerate(fields, start=1):
            ttk.Label(panel, text=label).grid(row=row, column=0, sticky="w", padx=(0, 20), pady=5)
            entry = ttk.Entry(panel, textvariable=variable, width=25)
            entry.grid(row=row, column=1, sticky="ew", pady=5)
            self.controls.append(entry)

        buttons = ttk.Frame(panel)
        buttons.grid(row=5, column=0, columnspan=2, sticky="ew", pady=12)
        for label, write in (("Read stored delay", False), ("Write delay", True)):
            button = ttk.Button(buttons, text=label, command=lambda w=write: self.start(w))
            button.pack(side="left", padx=(0, 10))
            self.controls.append(button)

        ttk.Label(panel, text=(
            "Writes persist in EEPROM and the CTRL task reads them each cycle.\n"
            "Firmware readback currently truncates values above 255 ms."
        ), wraplength=470).grid(row=6, column=0, columnspan=2, sticky="w")
        self.status = tk.StringVar(value="Ready. Set your board ID and click Read or Write.")
        ttk.Label(panel, textvariable=self.status, wraplength=470).grid(
            row=7, column=0, columnspan=2, sticky="w", pady=(12, 0))
        self.log = tk.Text(panel, width=65, height=7, state="disabled", wrap="word")
        self.log.grid(row=8, column=0, columnspan=2, sticky="ew", pady=(10, 0))
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(50, self.poll)

    def start(self, write: bool) -> None:
        if self.busy:
            return
        try:
            board_id = parse_integer(self.board.get())
            bitrate = parse_integer(self.bitrate.get())
            channel = self.channel.get().strip()
            value = parse_integer(self.delay.get()) if write else None
            if not 0 <= board_id <= 255:
                raise ValueError("Board ID must be 0..255.")
            if bitrate <= 0 or not channel:
                raise ValueError("Enter a PCAN channel and positive bitrate.")
            if value is not None and not 0 <= value <= 1000:
                raise ValueError("Delay must be 0..1000 ms.")
        except ValueError as exc:
            self.status.set(f"Invalid input: {exc}")
            return

        self.busy = True
        for control in self.controls:
            control.state(["disabled"])
        self.status.set(f"{'Writing' if write else 'Reading'} board {board_id} via {channel}...")

        def work():
            try:
                reply_id = REPLY_BASE | (board_id << 8) | CONFIG_ADDRESS
                with can.Bus(
                    interface="pcan", channel=channel, bitrate=bitrate,
                    ignore_config=True, fd=False, receive_own_messages=False,
                    can_filters=[{"can_id": reply_id, "can_mask": 0x1FFFFFFF, "extended": True}],
                ) as bus:
                    if value is None:
                        text = f"Stored delay reply: {request(bus, board_id)} ms."
                    else:
                        text = change_delay(bus, board_id, value)
                self.results.put((True, text))
            except Exception as exc:
                self.results.put((False, str(exc)))

        threading.Thread(target=work, daemon=True).start()

    def poll(self) -> None:
        try:
            ok, text = self.results.get_nowait()
        except queue.Empty:
            pass
        else:
            self.busy = False
            for control in self.controls:
                control.state(["!disabled"])
            text = text if ok else f"Error: {text}"
            self.status.set(text)
            self.log.configure(state="normal")
            self.log.insert("end", text + "\n\n")
            self.log.see("end")
            self.log.configure(state="disabled")
            if self.closing:
                self.root.destroy()
                return
        self.root.after(50, self.poll)

    def close(self) -> None:
        if self.busy:
            self.closing = True
            self.status.set("Finishing the current CAN operation before closing...")
        else:
            self.root.destroy()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--channel", default="PCAN_USBBUS1")
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument("--board-id", default="0", help="Decimal or 0x-prefixed hex; editable in GUI")
    args = parser.parse_args()
    root = tk.Tk()
    DelayGui(root, args)
    root.mainloop()


if __name__ == "__main__":
    main()
