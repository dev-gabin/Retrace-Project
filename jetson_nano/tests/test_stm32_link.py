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


class LaserRetirementTest(unittest.TestCase):
    def test_all_old_laser_entrypoints_are_blocked_before_serial_write(self):
        link = Stm32Link.__new__(Stm32Link)
        writes = []
        link._command = lambda *args: writes.append(args)
        for action in [lambda: link.aim(85, 42), lambda: link.point(85, 42), lambda: link.laser(True)]:
            with self.assertRaisesRegex(Stm32LinkError, "LASER_DISABLED"):
                action()
        self.assertEqual(writes, [])
        link.laser(False)
        link.home()
        self.assertEqual(writes, [("LASER:OFF", "OK@LASER")] * 2)

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

    def test_old_calibration_environment_cannot_block_pir_link_startup(self):
        class FakeSerial:
            is_open = True
            def close(self):
                self.is_open = False

        with patch.dict(os.environ, {"RETRACE_HOME_PAN": "invalid", "RETRACE_HOME_TILT": "invalid"}), patch("communication.stm32_link.serial.Serial", return_value=FakeSerial()), patch("communication.stm32_link.Stm32Link._read_loop"):
            link = Stm32Link(port="test-port")
            self.assertEqual(link.port, "test-port")
            link.close()


if __name__ == "__main__":
    unittest.main()
