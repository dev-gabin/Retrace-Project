from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

# The Windows review environment does not install pyserial. Production imports
# the real module on Jetson; these tests only need its exception type.
if "serial" not in sys.modules:
    serial_stub = types.ModuleType("serial")
    serial_stub.SerialException = OSError
    serial_stub.Serial = object
    sys.modules["serial"] = serial_stub

from record.last_seen import LastSeenError, LastSeenRecord, LastSeenStore
from vision.aim_mapper import AimMapper
from vision.http_api import RetraceHttpServer


class FakeStore:
    def __init__(self, data_dir: Path) -> None:
        self.data_dir = data_dir
        self.records = {
            "keyboard": LastSeenRecord(
                item="keyboard",
                pos_x=320,
                pos_y=240,
                seen_at="2026-10-10T05:04:14.825325Z",
                snapshot="snapshots/keyboard.jpg",
                drawer_id=None,
                state="visible",
            ),
            "wallet": LastSeenRecord(
                item="wallet",
                pos_x=None,
                pos_y=None,
                seen_at=None,
                snapshot=None,
                drawer_id=None,
                state="uncertain",
            ),
        }
        self.commands: list[tuple[str, str]] = []

    def list_items(self) -> list[LastSeenRecord]:
        return list(self.records.values())

    def get_item(self, item: str) -> LastSeenRecord:
        try:
            return self.records[item]
        except KeyError as error:
            raise LastSeenError("ERR@GET:RT_NOT_FOUND") from error

    def send_device_command(self, target: str, action: str) -> None:
        self.commands.append((target, action))


class FakeStm32:
    def __init__(self) -> None:
        self.points: list[tuple[int, int, float]] = []

    def point(self, pan: int, tilt: int, *, on_seconds: float) -> None:
        self.points.append((pan, tilt, on_seconds))


class HttpApiTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        self.web_root = root / "web"
        self.web_root.mkdir()
        (self.web_root / "index.html").write_text("Retrace test", encoding="utf-8")
        (root / "data" / "snapshots").mkdir(parents=True)
        (root / "data" / "snapshots" / "keyboard.jpg").write_bytes(b"jpeg-test")
        self.store = FakeStore(root / "data")
        self.stm32 = FakeStm32()
        self.server = RetraceHttpServer(
            self.store,  # type: ignore[arg-type]
            self.stm32,  # type: ignore[arg-type]
            AimMapper(640, 480, 135, 45, 60, 120),
            web_root=self.web_root,
            host="127.0.0.1",
            port=0,
            laser_seconds=5.0,
        )
        self.server.start()
        host, port = self.server.address
        self.base_url = f"http://{host}:{port}"

    def tearDown(self) -> None:
        self.server.close()
        self.temporary.cleanup()

    def request(self, path: str, body: object | None = None) -> tuple[int, object]:
        data = None if body is None else json.dumps(body).encode("utf-8")
        request = Request(
            self.base_url + path,
            data=data,
            headers={"Content-Type": "application/json"} if data else {},
            method="POST" if data is not None else "GET",
        )
        with urlopen(request, timeout=2) as response:
            return response.status, json.load(response)

    def test_list_and_get_return_dynamic_records(self) -> None:
        status, body = self.request("/api/items")
        self.assertEqual(status, 200)
        self.assertEqual([item["item"] for item in body["items"]], ["keyboard", "wallet"])
        status, record = self.request("/api/items/keyboard")
        self.assertEqual(status, 200)
        self.assertEqual(record["pos_x"], 320)
        self.assertIsNone(record["drawer_id"])

    def test_aim_drawer_and_buzzer_commands(self) -> None:
        self.assertEqual(self.request("/api/items/keyboard/aim", {}), (200, {"ok": True}))
        self.assertEqual(self.stm32.points, [(90, 90, 5.0)])
        self.assertEqual(self.request("/api/drawers/6/open", {}), (200, {"ok": True}))
        self.assertEqual(self.request("/api/buzzer", {"enabled": True}), (200, {"ok": True}))
        self.assertEqual(self.request("/api/buzzer", {"enabled": False}), (200, {"ok": True}))
        self.assertEqual(
            self.store.commands,
            [("DRAWER", "OPEN:6"), ("TOKEN", "BUZZER:1"), ("TOKEN", "BUZZER:0")],
        )

    def test_snapshot_and_static_web_are_served(self) -> None:
        with urlopen(self.base_url + "/", timeout=2) as response:
            self.assertEqual(response.read(), b"Retrace test")
        with urlopen(self.base_url + "/snapshots/keyboard.jpg", timeout=2) as response:
            self.assertEqual(response.read(), b"jpeg-test")
        with self.assertRaises(HTTPError) as caught:
            urlopen(self.base_url + "/snapshots/%2e%2e/index.html", timeout=2)
        self.assertEqual(caught.exception.code, 400)

    def test_item_without_coordinates_returns_conflict(self) -> None:
        with self.assertRaises(HTTPError) as caught:
            self.request("/api/items/wallet/aim", {})
        self.assertEqual(caught.exception.code, 409)
        error = json.load(caught.exception)
        self.assertEqual(error["error"]["code"], "NO_POSITION")

    def test_unknown_item_returns_not_found(self) -> None:
        with self.assertRaises(HTTPError) as caught:
            self.request("/api/items/missing_item")
        self.assertEqual(caught.exception.code, 404)
        error = json.load(caught.exception)
        self.assertEqual(error["error"]["code"], "ITEM_NOT_FOUND")


class RecordParsingTest(unittest.TestCase):
    def test_record_parser_normalizes_missing_values(self) -> None:
        observed = LastSeenStore._parse_record(
            "GET@car_key:visible:1:335:102:2026-10-10T05%3A04%3A16.205564Z:"
            "snapshots/key.jpg:0",
            "GET",
        )
        self.assertEqual(observed.item, "car_key")
        self.assertEqual(observed.seen_at, "2026-10-10T05:04:16.205564Z")
        self.assertIsNone(observed.drawer_id)

        unseen = LastSeenStore._parse_record(
            "LIST_ITEM@wallet:uncertain:0:-:-:-:-:0",
            "LIST_ITEM",
        )
        self.assertIsNone(unseen.seen_at)
        self.assertIsNone(unseen.snapshot)
        self.assertIsNone(unseen.pos_x)


if __name__ == "__main__":
    unittest.main()
