"""Schema, wire protocol, and GUI lifecycle checks without PCAN hardware."""

import argparse
from collections import deque
import copy
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import tkinter as tk
import unittest
from unittest.mock import patch
import uuid

import can

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pcan_config import (Cancelled, ConfigGui, ConfigValue, DEFAULT_CONFIG,
                         integer, load_config, request)


SETTING = ConfigValue("Test setting", 7, 10, 0, 1000)


def reply(board, address, data, **kwargs):
    return can.Message(arbitration_id=0xC10000 | (board << 8) | address,
                       is_extended_id=True, data=data, **kwargs)


class ScriptedBus:
    def __init__(self, responses=(), stale=()):
        self.responses = deque(responses)
        self.pending = deque(stale)
        self.sent = []
        self.closed = False

    def send(self, message, timeout):
        self.sent.append(message)
        if self.responses:
            self.pending.extend(self.responses.popleft())

    def recv(self, timeout):
        return self.pending.popleft() if self.pending else None

    def shutdown(self):
        self.closed = True


class SchemaTests(unittest.TestCase):
    def load(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(json.dumps(data), encoding="utf-8")
            return load_config(path)

    def test_root_config_loads_all_values(self):
        schema = load_config(DEFAULT_CONFIG)
        self.assertEqual(schema.board_id, 0)
        self.assertEqual([value.address for value in schema.values], [0, 1, 2, 3])
        self.assertEqual(schema.values[1].max_value, 255)

    def test_invalid_schema_rejected_before_can(self):
        original = json.loads(DEFAULT_CONFIG.read_text())
        mutations = [
            lambda d: d.update(board_id=True),
            lambda d: d.update(board_id=256),
            lambda d: d.update(values=[]),
            lambda d: d["values"].append(d["values"][0]),
            lambda d: d["values"][0].update(address=256),
            lambda d: d["values"][0].update(default_value=1),
            lambda d: d["values"][0].update(min_value=-1),
            lambda d: d["values"][0].update(max_value=65536),
            lambda d: d["values"][0].update(min_value=1, max_value=0),
            lambda d: d["values"][0].update(default_value=0.0),
            lambda d: d["values"][0].update(name=""),
            lambda d: d["values"][0].pop("address"),
        ]
        for mutate in mutations:
            data = copy.deepcopy(original)
            mutate(data)
            with self.subTest(data=data), self.assertRaises(ValueError):
                self.load(data)

    def test_decimal_hex_and_invalid_text(self):
        self.assertEqual(integer("08"), 8)
        self.assertEqual(integer(" 0xFF "), 255)
        for value in ("", "1.5", "nan", "True"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                integer(value)


class ProtocolTests(unittest.TestCase):
    def test_write_is_one_message_with_little_endian_and_variable_address(self):
        bus = ScriptedBus([[reply(42, 7, [0])]])
        self.assertIsNone(request(bus, 42, SETTING, 1000))
        self.assertEqual(len(bus.sent), 1)  # No automatic readback.
        message = bus.sent[0]
        self.assertEqual(message.arbitration_id, 0xC02A07)
        self.assertEqual(message.data, b"\xe8\x03")
        self.assertTrue(message.is_extended_id)
        self.assertEqual(message.dlc, 2)

    def test_read_filters_wrong_board_address_frame_and_ack(self):
        bus = ScriptedBus([[
            reply(43, 7, [0, 99, 0]), reply(42, 8, [0, 99, 0]),
            reply(42, 7, [0]), reply(42, 7, [], is_remote_frame=True, dlc=3),
            reply(42, 7, [0, 99, 0], is_error_frame=True),
            reply(42, 7, [0, 99, 0], is_fd=True),
            can.Message(arbitration_id=7, is_extended_id=False, data=[0, 99, 0]),
            reply(42, 7, [0, 0xe8, 3]),
        ]], stale=[reply(42, 7, [0, 77, 0])])
        self.assertEqual(request(bus, 42, SETTING), 1000)
        self.assertFalse(bus.sent[0].is_remote_frame)
        self.assertEqual(bus.sent[0].dlc, 0)

    def test_invalid_values_never_send(self):
        for board, setting, value, timeout in (
            (-1, SETTING, 10, 1), (256, SETTING, 10, 1),
            (True, SETTING, 10, 1), (0, SETTING, -1, 1),
            (0, SETTING, 1001, 1), (0, SETTING, 1.5, 1),
            (0, SETTING, True, 1), (0, SETTING, 0, 0),
            (0, SETTING, 0, float("nan")), (0, SETTING, 0, float("inf")),
            (0, ConfigValue("Bad", 256, 0, 0, 1), None, 1),
        ):
            bus = ScriptedBus()
            with self.subTest(board=board, value=value, timeout=timeout):
                with self.assertRaises(ValueError):
                    request(bus, board, setting, value, timeout)
                self.assertFalse(bus.sent)

    def test_errors_timeout_and_cancel_do_not_retry(self):
        for status in (1, 2, 3, 4, 255):
            bus = ScriptedBus([[reply(0, 7, [status])]])
            with self.subTest(status=status), self.assertRaises(RuntimeError):
                request(bus, 0, SETTING, 10)
            self.assertEqual(len(bus.sent), 1)
        bus = ScriptedBus()
        with self.assertRaisesRegex(TimeoutError, "may still have committed"):
            request(bus, 0, SETTING, 0, timeout=0.002)
        self.assertEqual(len(bus.sent), 1)
        bus = ScriptedBus()
        cancel = threading.Event()
        cancel.set()
        with self.assertRaises(Cancelled):
            request(bus, 0, SETTING, 10, cancel=cancel)
        self.assertFalse(bus.sent)

    def test_virtual_can_round_trip(self):
        channel = str(uuid.uuid4())
        errors = []
        with can.Bus(interface="virtual", channel=channel, ignore_config=True) as client, \
                can.Bus(interface="virtual", channel=channel, ignore_config=True) as device:
            def respond():
                try:
                    write = device.recv(timeout=1)
                    self.assertEqual(write.arbitration_id, 0xC02A07)
                    self.assertEqual(write.data, b"\xe8\x03")
                    device.send(reply(42, 7, [0]))
                    read = device.recv(timeout=1)
                    self.assertEqual(read.dlc, 0)
                    device.send(reply(42, 7, [0, 0xe8, 3]))
                except Exception as exc:
                    errors.append(exc)
            worker = threading.Thread(target=respond)
            worker.start()
            try:
                request(client, 42, SETTING, 1000)
                self.assertEqual(request(client, 42, SETTING), 1000)
            finally:
                worker.join(timeout=3)
            self.assertFalse(worker.is_alive())
            self.assertEqual(errors, [])


class GuiTests(unittest.TestCase):
    def setUp(self):
        self.root = tk.Tk()
        self.root.withdraw()
        args = argparse.Namespace(config=DEFAULT_CONFIG, channel="PCAN_USBBUS1",
                                  bitrate=500000, board_id=None)
        self.gui = ConfigGui(self.root, load_config(DEFAULT_CONFIG), args)
        self.bus = ScriptedBus()
        self.bus_patch = patch("pcan_config.can.Bus", return_value=self.bus)
        self.factory = self.bus_patch.start()

    def tearDown(self):
        self.gui.close()
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            try:
                if not self.root.winfo_exists():
                    break
                self.root.update()
            except tk.TclError:
                break
            time.sleep(0.005)
        self.bus_patch.stop()

    def finish(self):
        deadline = time.monotonic() + 2
        while self.gui.busy and time.monotonic() < deadline:
            self.root.update()
            time.sleep(0.005)
        self.assertFalse(self.gui.busy)

    def connect(self):
        self.gui.toggle_connection()
        self.finish()
        self.assertTrue(self.gui.connected)

    def test_connect_disconnect_never_send_and_settings_are_locked(self):
        self.connect()
        self.assertFalse(self.bus.sent)
        self.assertTrue(all(control.instate(["disabled"]) for control in self.gui.connection_controls))
        self.assertEqual(self.factory.call_args.kwargs["can_filters"], [
            {"can_id": 0xC10000, "can_mask": 0x1FFFFF00, "extended": True}])
        self.gui.toggle_connection()
        self.finish()
        self.assertTrue(self.bus.closed)
        self.assertFalse(self.gui.connected)
        self.assertFalse(self.bus.sent)

    def test_read_all_is_explicit_preserves_edits_and_handles_row_errors(self):
        self.connect()
        self.gui.rows[1]["draft"].set("25")
        self.bus.responses.extend([
            [reply(0, 0, [0, 0, 0])], [reply(0, 1, [0, 12, 0])],
            [reply(0, 2, [4, 0, 0])], [reply(0, 3, [0, 14, 0])],
        ])
        self.gui.read_all()
        self.finish()
        self.assertEqual(len(self.bus.sent), 4)
        self.assertEqual(self.gui.rows[1]["actual"].get(), "12")
        self.assertEqual(self.gui.rows[1]["draft"].get(), "25")
        self.assertEqual(self.gui.rows[3]["draft"].get(), "14")
        self.assertEqual(self.gui.rows[2]["actual"].get(), "Unknown")
        self.assertIn("3/4", self.gui.status.get())

    def test_invalid_write_sends_nothing_and_valid_write_only_gets_ack(self):
        self.connect()
        setting = self.gui.schema.values[1]
        row = self.gui.rows[1]
        for value in ("256", "-1", "1.5", ""):
            row["draft"].set(value)
            self.gui.write_value(setting)
            self.assertFalse(self.bus.sent)
        row["draft"].set("0xFF")
        self.bus.responses.append([reply(0, 1, [0])])
        self.gui.write_value(setting)
        self.finish()
        self.assertEqual(len(self.bus.sent), 1)
        self.assertEqual(self.bus.sent[0].data, b"\xff\x00")
        self.assertEqual(row["actual"].get(), "Not read")
        self.assertIn("ACK", row["status"].get())

    def test_connect_failure_and_invalid_target_keep_gui_disconnected(self):
        self.gui.board.set("256")
        self.gui.toggle_connection()
        self.factory.assert_not_called()
        self.gui.board.set("0")
        self.factory.side_effect = can.CanError("Adapter unavailable")
        self.gui.toggle_connection()
        self.finish()
        self.assertFalse(self.gui.connected)
        self.assertIn("Adapter unavailable", self.gui.status.get())

    def test_close_cancels_read_and_releases_adapter(self):
        self.connect()
        self.gui.read_all()
        self.gui.close()
        deadline = time.monotonic() + 2
        while not self.bus.closed and time.monotonic() < deadline:
            self.root.update()
            time.sleep(0.005)
        self.assertTrue(self.bus.closed)


if __name__ == "__main__":
    unittest.main()
