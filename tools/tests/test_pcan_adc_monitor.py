import csv
from datetime import datetime, timedelta, timezone
import io
from pathlib import Path
import struct
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from pcan_adc_monitor import (
    CsvLogger,
    Sample,
    decode_voltage,
    parse_can_id,
    samples_in_visible_window,
)


class DecodeVoltageTests(unittest.TestCase):
    def test_decodes_little_endian_float32(self):
        self.assertAlmostEqual(decode_voltage(struct.pack("<f", 2.75)), 2.75)

    def test_rejects_wrong_dlc(self):
        with self.assertRaisesRegex(ValueError, "4-byte"):
            decode_voltage(b"\x00\x00")

    def test_rejects_non_finite_value(self):
        with self.assertRaisesRegex(ValueError, "non-finite"):
            decode_voltage(struct.pack("<f", float("nan")))

    def test_parses_extended_id(self):
        self.assertEqual(parse_can_id("0x10000101"), 0x10000101)


class CsvLoggerTests(unittest.TestCase):
    def test_writes_only_timestamp_and_voltage(self):
        output = io.StringIO()
        logger = CsvLogger(output)
        logger.write(Sample(datetime(2026, 9, 22, 12, 34, 56, 789000, timezone.utc), 1.25))

        rows = list(csv.reader(io.StringIO(output.getvalue())))
        self.assertEqual(rows[0], ["timestamp", "voltage_v"])
        self.assertEqual(rows[1], ["2026-09-22T12:34:56.789+00:00", "1.25"])
        self.assertTrue(all(len(row) == 2 for row in rows))


class VisibleWindowTests(unittest.TestCase):
    def test_keeps_only_samples_shown_in_graph_window(self):
        started = datetime(2026, 9, 22, tzinfo=timezone.utc)
        samples = [
            Sample(started + timedelta(seconds=second), float(second))
            for second in (0, 4, 5, 10, 15)
        ]

        visible = samples_in_visible_window(samples, 10.0)

        self.assertEqual([sample.voltage_v for sample in visible], [5.0, 10.0, 15.0])


if __name__ == "__main__":
    unittest.main()
