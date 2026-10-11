from __future__ import annotations

import json
import io
from dataclasses import replace
from datetime import datetime, timedelta, timezone
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
from record.leave_items import LeaveItems
from vision.aim_mapper import AimMapper
from vision.http_api import RetraceHttpServer
from record.drawer_items import DrawerItems


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
            leave_items=LeaveItems(root / "data" / "leave_items.json"),
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

    def test_leave_items_are_saved_and_validated(self) -> None:
        self.assertEqual(self.request("/api/leave-items"), (200, {"items": []}))
        self.assertEqual(
            self.request("/api/leave-items", {"items": ["wallet", "keyboard"]}),
            (200, {"ok": True, "items": ["wallet", "keyboard"]}),
        )
        self.assertEqual(self.request("/api/leave-items"), (200, {"items": ["wallet", "keyboard"]}))
        with self.assertRaises(HTTPError) as caught:
            self.request("/api/leave-items", {"items": ["Bad ID"]})
        self.assertEqual(caught.exception.code, 400)

    def test_manual_drawer_storage_and_release(self) -> None:
        self.request("/api/items/keyboard/drawer", {"drawer_id": 1})
        _, record = self.request("/api/items/keyboard")
        self.assertEqual(record["drawer_id"], 1)
        self.assertIsNone(record["pos_x"])
        with self.assertRaises(HTTPError):
            self.request("/api/items/keyboard/aim", {})
        self.assertEqual(self.stm32.points, [])
        from record.drawer_items import DrawerItems
        restored = DrawerItems(self.store.data_dir / "drawer_items.json")
        self.assertEqual(restored.apply(self.store.records["keyboard"]).drawer_id, 1)
        self.request("/api/drawers/1/open", {})
        _, record = self.request("/api/items/keyboard")
        self.assertIsNone(record["drawer_id"])
        self.assertIsNone(record["pos_x"])
        self.assertIsNone(record["seen_at"])
        self.assertEqual(self.store.commands, [("DRAWER", "OPEN:1")])

    def test_failed_open_preserves_storage(self) -> None:
        self.request("/api/items/wallet/drawer", {"drawer_id": 2})
        def fail(*args):
            raise LastSeenError("offline")
        self.store.send_device_command = fail
        with self.assertRaises(HTTPError):
            self.request("/api/drawers/2/open", {})
        _, record = self.request("/api/items/wallet")
        self.assertEqual(record["drawer_id"], 2)

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


class ManualDrawerTest(unittest.TestCase):
    """Exercise request handling and persistence without a network socket."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.store = FakeStore(Path(self.temporary.name))
        self.server = RetraceHttpServer.__new__(RetraceHttpServer)
        self.server.storage = self.store
        self.server.drawer_target = "DRAWER"
        self.server.drawer_items = DrawerItems(self.store.data_dir / "drawer_items.json")
        self.body = None
        self.server._send_json = lambda request, status, body: setattr(self, "body", body)

    def tearDown(self):
        self.temporary.cleanup()

    def post(self, path, body):
        data = json.dumps(body).encode()
        self.server._handle_post(types.SimpleNamespace(path=path, headers={"Content-Length": str(len(data))}, rfile=io.BytesIO(data)))


    def test_unseen_storage_listing_and_persistence(self):
        self.post("/api/items/umbrella/drawer", {"drawer_id": 1})
        self.assertEqual(self.server._get_item("umbrella").drawer_id, 1)
        self.server._handle_get(types.SimpleNamespace(path="/api/items"))
        self.assertIn("umbrella", [record["item"] for record in self.body["items"]])
        self.server.drawer_items = DrawerItems(self.server.drawer_items.path)
        self.assertEqual(self.server._get_item("umbrella").drawer_id, 1)

    def test_custom_name_persists_moves_and_releases(self):
        self.post("/api/drawer-items", {"name": "  여권  ", "drawer_id": 3})
        item = self.body["item"]
        self.assertRegex(item, r"^custom_[0-9a-f]{24}$")
        self.server.drawer_items = DrawerItems(self.server.drawer_items.path)
        self.server._handle_get(types.SimpleNamespace(path=f"/api/items/{item}"))
        self.assertEqual(self.body["display_name"], "여권")
        self.assertEqual(self.body["drawer_id"], 3)
        self.post("/api/drawer-items", {"name": "여권", "drawer_id": 4})
        self.assertEqual(self.body["item"], item)
        self.assertEqual(len(self.server.drawer_items.entries), 1)
        self.post(f"/api/items/{item}/drawer", {"drawer_id": 2})
        self.post("/api/drawers/2/open", {})
        self.server._handle_get(types.SimpleNamespace(path=f"/api/items/{item}"))
        self.assertEqual(self.body["display_name"], "여권")
        self.assertIsNone(self.body["drawer_id"])
        self.assertIsNone(self.body["pos_x"])

    def test_custom_input_is_validated(self):
        for name in ["", "  ", None, "x" * 41, "여\x00권"]:
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.post("/api/drawer-items", {"name": name, "drawer_id": 1})
        for n in [0, 7, True, "1"]:
            with self.subTest(drawer=n), self.assertRaises(ValueError):
                self.post("/api/drawer-items", {"name": "여권", "drawer_id": n})
        self.assertEqual(self.server.drawer_items.entries, {})

    def test_open_releases_all_items_in_only_that_drawer(self):
        for item, n in [("umbrella", 1), ("book", 1), ("wallet", 2)]:
            self.post(f"/api/items/{item}/drawer", {"drawer_id": n})
        self.post("/api/drawers/1/open", {})
        for item in ["umbrella", "book"]:
            record = self.server._get_item(item)
            self.assertIsNone(record.drawer_id)
            self.assertIsNone(record.pos_x)
        self.assertEqual(self.server._get_item("wallet").drawer_id, 2)
        self.assertEqual(self.store.commands, [("DRAWER", "OPEN:1")])

    def test_failed_open_keeps_registration(self):
        self.post("/api/items/umbrella/drawer", {"drawer_id": 1})
        def fail(*args):
            raise LastSeenError("offline")
        self.store.send_device_command = fail
        with self.assertRaises(LastSeenError):
            self.post("/api/drawers/1/open", {})
        self.assertEqual(self.server._get_item("umbrella").drawer_id, 1)

    def test_released_coordinates_require_new_camera_observation(self):
        self.post("/api/items/keyboard/drawer", {"drawer_id": 1})
        self.post("/api/drawers/1/open", {})
        self.assertIsNone(self.server._get_item("keyboard").pos_x)
        self.store.records["keyboard"] = replace(self.store.records["keyboard"], seen_at=(datetime.now(timezone.utc) + timedelta(seconds=1)).isoformat())
        self.assertEqual(self.server._get_item("keyboard").pos_x, 320)


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
