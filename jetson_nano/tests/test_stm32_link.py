from __future__ import annotations

import os
from queue import Queue
import sys
import types
import unittest
from unittest.mock import patch

if "serial" not in sys.modules:
    serial_stub = types.ModuleType("serial")
    serial_stub.SerialException = OSError
    serial_stub.Serial = object
    sys.modules["serial"] = serial_stub

from communication.stm32_link import Stm32Link, Stm32LinkError


class LaserControlTest(unittest.TestCase):
    def test_laser_sequence_and_calibrated_home(self):
        link = Stm32Link.__new__(Stm32Link)
        writes = []
        link._command = lambda *args: writes.append(args)
        link.home_pan, link.home_tilt = 85, 42
        with patch("communication.stm32_link.time.sleep"):
            link.point(90, 50, on_seconds=5)
        self.assertEqual(writes, [("LASER:OFF", "OK@LASER"), ("AIM:90,50", "OK@AIM"), ("LASER:ON", "OK@LASER"), ("LASER:OFF", "OK@LASER"), ("AIM:85,42", "OK@AIM")])

    def test_invalid_angles_and_duration_do_not_write(self):
        link = Stm32Link.__new__(Stm32Link)
        writes = []
        link._command = lambda *args: writes.append(args)
        for action in [lambda: link.aim(-1, 42), lambda: link.aim(85, 181), lambda: link.point(85, 42, on_seconds=6)]:
            with self.assertRaises(ValueError):
                action()
        self.assertEqual(writes, [])

    def test_laser_on_failure_still_sends_off_and_home(self):
        link = Stm32Link.__new__(Stm32Link)
        link.home_pan, link.home_tilt = 85, 42
        writes = []
        def command(value, response):
            writes.append(value)
            if value == "LASER:ON":
                raise Stm32LinkError("timeout")
        link._command = command
        with patch("communication.stm32_link.time.sleep"), self.assertRaises(Stm32LinkError):
            link.point(90, 50)
        self.assertEqual(writes[-2:], ["LASER:OFF", "AIM:85,42"])

    def test_pir_events_and_ping_remain_available(self):
        link = Stm32Link.__new__(Stm32Link)
        link._events = Queue()
        link._events.put("EVT@PIR:1")
        self.assertEqual(link.get_event(timeout=0), "EVT@PIR:1")
        self.assertIsNone(link.get_event(timeout=0))
        writes = []
        link._command = lambda *args: writes.append(args)
        link.ping()
        self.assertEqual(writes, [("PING", "OK@PING")])

    def test_calibrated_home_defaults_and_pir_link_startup(self):
        class FakeSerial:
            is_open = True
            def close(self):
                self.is_open = False

        with patch.dict(os.environ, {"RETRACE_HOME_PAN": "85", "RETRACE_HOME_TILT": "42"}), patch("communication.stm32_link.serial.Serial", return_value=FakeSerial()), patch("communication.stm32_link.Stm32Link._read_loop"):
            link = Stm32Link(port="test-port")
            self.assertEqual(link.port, "test-port")
            self.assertEqual((link.home_pan, link.home_tilt), (85, 42))
            link.close()


if __name__ == "__main__":
    unittest.main()
