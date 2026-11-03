"""Check the config wire format and responses without accessing PCAN hardware."""

from collections import deque
from pathlib import Path
import sys
import threading
import unittest

import can

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pcan_ctrl_delay import change_delay, parse_integer, request


def reply(board, data, **kwargs):
    return can.Message(
        arbitration_id=0xC10001 | (board << 8),
        is_extended_id=True, data=data, **kwargs,
    )


class ScriptedBus:
    def __init__(self, responses, stale=()):
        self.responses = deque(responses)
        self.pending = deque(stale)
        self.sent = []

    def send(self, message, timeout):
        self.sent.append(message)
        self.pending.extend(self.responses.popleft())

    def recv(self, timeout):
        return self.pending.popleft() if self.pending else None


class ConfigProtocolTests(unittest.TestCase):
    def test_write_uses_extended_board_address_and_little_endian(self):
        bus = ScriptedBus([[reply(0xE7, [0])]])
        self.assertIsNone(request(bus, 0xE7, 1000))
        self.assertEqual(bus.sent[0].arbitration_id, 0xC0E701)
        self.assertTrue(bus.sent[0].is_extended_id)
        self.assertEqual(bus.sent[0].data, bytes.fromhex("E8 03"))
        self.assertEqual(bus.sent[0].dlc, 2)

    def test_read_uses_zero_length_data_frame_and_decodes_full_value(self):
        bus = ScriptedBus([[reply(0, [0, 0xE8, 3])]])
        self.assertEqual(request(bus, 0), 1000)
        self.assertEqual(bus.sent[0].arbitration_id, 0xC00001)
        self.assertEqual(bus.sent[0].dlc, 0)
        self.assertFalse(bus.sent[0].is_remote_frame)

    def test_ignores_stale_other_board_wrong_length_and_non_data_replies(self):
        bus = ScriptedBus([[
            reply(2, [0, 77, 0]),
            reply(1, [0]),
            reply(1, [], is_remote_frame=True, dlc=3),
            reply(1, [0, 77, 0], is_error_frame=True),
            reply(1, [0, 77, 0], is_fd=True),
            can.Message(arbitration_id=1, is_extended_id=False, data=[0, 77, 0]),
            reply(1, [0, 42, 0]),
        ]], stale=[reply(1, [0, 99, 0])])
        self.assertEqual(request(bus, 1), 42)

    def test_eeprom_failures_are_not_success(self):
        for status, name in ((1, "No stored value"), (2, "INVALID_ARGUMENT"),
                             (3, "NOT_INITIALIZED"), (4, "FLASH_ERROR"), (255, "Unknown")):
            with self.subTest(status=status):
                bus = ScriptedBus([[reply(0, [status])]])
                with self.assertRaisesRegex(RuntimeError, name):
                    request(bus, 0, 10)

    def test_timeout_does_not_retry_write(self):
        bus = ScriptedBus([[]])
        with self.assertRaisesRegex(TimeoutError, "may still have committed"):
            request(bus, 0, 10, timeout=0.002)
        self.assertEqual(len(bus.sent), 1)

    def test_rejects_bad_input_before_sending(self):
        for board, value in ((-1, 10), (256, 10), (0, -1), (0, 1001)):
            with self.subTest(board=board, value=value):
                bus = ScriptedBus([])
                with self.assertRaises(ValueError):
                    request(bus, board, value)
                self.assertFalse(bus.sent)

    def test_zero_delay_and_hex_board_input(self):
        bus = ScriptedBus([[reply(255, [0])]])
        request(bus, parse_integer("0xFF"), 0)
        self.assertEqual(bus.sent[0].data, b"\x00\x00")
        self.assertEqual(parse_integer("08"), 8)

    def test_write_is_verified_only_when_readback_matches(self):
        bus = ScriptedBus([[reply(0, [0])], [reply(0, [0, 10, 0])]])
        self.assertIn("readback matched: 10 ms", change_delay(bus, 0, 10))

    def test_current_firmware_truncated_readback_is_reported(self):
        bus = ScriptedBus([[reply(0, [0])], [reply(0, [0, 0xE8, 0])]])
        result = change_delay(bus, 0, 1000)
        self.assertIn("Write acknowledged for 1000 ms", result)
        self.assertIn("readback returned 232 ms", result)
        self.assertIn("not verified", result)

    def test_readback_failure_preserves_write_acknowledgement(self):
        bus = ScriptedBus([[reply(0, [0])], [reply(0, [4, 0, 0])]])
        result = change_delay(bus, 0, 10)
        self.assertIn("Write acknowledged", result)
        self.assertIn("readback failed", result)

    def test_round_trip_over_python_can_virtual_bus(self):
        channel = "ctrl-delay-test"
        errors = []
        with can.Bus(interface="virtual", channel=channel, ignore_config=True) as client, \
                can.Bus(interface="virtual", channel=channel, ignore_config=True) as device:
            def respond():
                try:
                    write = device.recv(timeout=1)
                    self.assertIsNotNone(write)
                    self.assertEqual(write.arbitration_id, 0xC02A01)
                    self.assertEqual(write.data, b"\xF4\x01")
                    device.send(reply(42, [0]))
                    read = device.recv(timeout=1)
                    self.assertIsNotNone(read)
                    self.assertEqual(read.dlc, 0)
                    device.send(reply(42, [0, 0xF4, 1]))
                except Exception as exc:
                    errors.append(exc)

            worker = threading.Thread(target=respond)
            worker.start()
            result = change_delay(client, 42, 500)
            worker.join(timeout=2)
            self.assertFalse(worker.is_alive())
            self.assertEqual(errors, [])
            self.assertIn("readback matched: 500 ms", result)


if __name__ == "__main__":
    unittest.main()
