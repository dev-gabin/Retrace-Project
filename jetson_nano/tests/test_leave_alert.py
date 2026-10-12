from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import threading
import time
import types
import unittest

# The Windows review environment does not install pyserial. Production imports
# the real module on Jetson; these tests only need its exception type.
if "serial" not in sys.modules:
    serial_stub = types.ModuleType("serial")
    serial_stub.SerialException = OSError
    serial_stub.Serial = object
    sys.modules["serial"] = serial_stub

from communication.phone_notifier import build_request, leave_notification
from communication.server_events import DeviceEvent, parse_event_line
from record.last_seen import LastSeenError, LastSeenRecord
from record.leave_items import LeaveItems
from vision.leave_alert import DeviceEventHandler, LeaveVerifier


def record(item: str = "car_key", state: str = "visible",
           x: int | None = 320, y: int | None = 240) -> LastSeenRecord:
    return LastSeenRecord(
        item=item, pos_x=x, pos_y=y, seen_at="2026-10-11T00:00:00Z",
        snapshot=f"snapshots/{item}.jpg", drawer_id=None, state=state,
    )


class FakeStore:
    def __init__(self, records: list[LastSeenRecord] | None) -> None:
        self.records = records
        self.commands: list[tuple[str, str]] = []

    def list_items(self) -> list[LastSeenRecord]:
        if self.records is None:
            raise LastSeenError("ERR@LIST:RT_DB")
        return self.records

    def send_device_command(self, target: str, action: str) -> None:
        self.commands.append((target, action))


class FakeStm32:
    def __init__(self) -> None:
        self.points: list[tuple[int, int]] = []

    def point(self, pan: int, tilt: int, *, on_seconds: float = 1.0) -> None:
        self.points.append((pan, tilt))


class FakeNotifier:
    def __init__(self) -> None:
        self.sent = []

    def send(self, notification) -> None:
        self.sent.append(notification)


class Clock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


PIRON = DeviceEvent("ENTRANCE", "EVT@ENTRANCE:PIRON")


class LeaveAlertTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.leave_items = LeaveItems(Path(self._temp.name) / "leave_items.json")

    def tearDown(self) -> None:
        self._temp.cleanup()

    def make_handler(self, records: list[LastSeenRecord] | None, registered: list[str]):
        self.leave_items.set(registered)
        store, stm32, notifier, clock = FakeStore(records), FakeStm32(), FakeNotifier(), Clock()
        verifier = LeaveVerifier(seconds=0.2)
        handler = DeviceEventHandler(
            store, notifier,
            self.leave_items, verifier, clock=clock, run_async=False,
        )
        return handler, verifier, store, stm32, notifier, clock

    @staticmethod
    def confirm(handler: DeviceEventHandler, verifier: LeaveVerifier, detections: list[dict]) -> None:
        worker = threading.Thread(target=handler.handle, args=(PIRON,))
        worker.start()
        deadline = time.monotonic() + 1.0
        while not verifier.active() and time.monotonic() < deadline:
            time.sleep(0.005)
        for _ in range(3):
            verifier.observe(detections)
        worker.join(timeout=1.0)


class ParseEventLineTest(unittest.TestCase):
    def test_parses_forwarded_event(self) -> None:
        self.assertEqual(parse_event_line("[ENTRANCE]EVT@ENTRANCE:PIRON"), PIRON)

    def test_ignores_non_events(self) -> None:
        for line in ("[SERVER]New connected! (ip:1,fd:2)", "EVT@X:SOS", "[]EVT@X", "[A]OK@PING"):
            self.assertIsNone(parse_event_line(line), line)


class BuildRequestTest(unittest.TestCase):
    def test_leave_notification_request(self) -> None:
        notification = leave_notification(["차키", "지갑"])
        request = build_request("http://jetson:8090/", "retrace", notification, "tk")
        self.assertEqual(request.full_url, "http://jetson:8090/retrace")
        self.assertEqual(request.get_method(), "POST")
        self.assertEqual(request.data, "차키, 지갑 챙기셨나요?".encode("utf-8"))
        self.assertEqual(request.get_header("Title"), "Retrace Leave Check")
        self.assertEqual(request.get_header("Priority"), "4")
        self.assertEqual(request.get_header("Authorization"), "Bearer tk")


class LeaveItemsTest(LeaveAlertTestCase):
    def test_missing_file_is_empty_and_set_round_trips(self) -> None:
        self.assertEqual(self.leave_items.get(), [])
        self.assertEqual(self.leave_items.set(["wallet", "car_key", "wallet"]), ["wallet", "car_key"])
        self.assertEqual(self.leave_items.get(), ["wallet", "car_key"])

    def test_rejects_invalid_ids(self) -> None:
        for items in (["Car Key"], [1], "wallet", ["a"] * 21):
            with self.assertRaises(ValueError):
                self.leave_items.set(items)


class DeviceEventHandlerTest(LeaveAlertTestCase):
    def test_freshly_confirmed_items_keep_light_and_phone_without_laser(self) -> None:
        handler, verifier, store, stm32, notifier, _ = self.make_handler(
            [], ["car_key", "wallet", "airpods"])
        self.confirm(handler, verifier, [
            {"item_id": "car_key", "confidence": 0.81, "center": (320, 240)},
            {"item_id": "wallet", "confidence": 0.73, "center": (120, 200)},
            {"item_id": "keyboard", "confidence": 0.99, "center": (1, 1)},
        ])
        self.assertEqual(store.commands, [("ENTRANCE", "LIGHT:ALERT")])
        self.assertEqual(notifier.sent, [leave_notification(["차키", "지갑"])])
        self.assertEqual(stm32.points, [])

    def test_unregistered_and_low_confidence_items_are_ignored(self) -> None:
        handler, verifier, store, _, notifier, _ = self.make_handler([], ["keyboard"])
        self.confirm(handler, verifier, [
            {"item_id": "car_key", "confidence": 0.95, "center": (320, 240)},
            {"item_id": "keyboard", "confidence": 0.49, "center": (100, 100)},
        ])
        self.assertEqual((store.commands, notifier.sent), ([], []))

    def test_nothing_registered_or_freshly_seen_does_nothing(self) -> None:
        handler, _, store, stm32, notifier, _ = self.make_handler([], [])
        handler.handle(PIRON)
        self.assertEqual((store.commands, notifier.sent, stm32.points), ([], [], []))

        handler, verifier, store, stm32, notifier, _ = self.make_handler([], ["car_key"])
        self.confirm(handler, verifier, [])
        self.assertEqual((store.commands, notifier.sent, stm32.points), ([], [], []))

    def test_cooldown_suppresses_repeated_motion(self) -> None:
        handler, verifier, _, _, notifier, clock = self.make_handler([], ["car_key"])
        self.confirm(handler, verifier, [{"item_id": "car_key", "confidence": 0.8, "center": (1, 1)}])
        clock.now += 14
        handler.handle(PIRON)
        self.assertEqual(len(notifier.sent), 1)
        clock.now += 1
        self.confirm(handler, verifier, [{"item_id": "car_key", "confidence": 0.8, "center": (1, 1)}])
        self.assertEqual(len(notifier.sent), 2)

    def test_motion_from_other_client_is_ignored(self) -> None:
        handler, _, store, _, notifier, _ = self.make_handler([], ["car_key"])
        handler.handle(DeviceEvent("pir01", "EVT@pir01:PIRON"))
        self.assertEqual((store.commands, notifier.sent), ([], []))

    def test_sos_sends_urgent_notification(self) -> None:
        handler, _, store, _, notifier, _ = self.make_handler([], ["car_key"])
        handler.handle(DeviceEvent("DRAWER", "EVT@DRAWER:SOS"))
        self.assertEqual(len(notifier.sent), 1)
        self.assertEqual(notifier.sent[0].priority, 5)
        self.assertEqual(store.commands, [])


if __name__ == "__main__":
    unittest.main()
