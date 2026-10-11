"""Small HTTP API that connects the web UI to the running Jetson process."""

from __future__ import annotations

from dataclasses import asdict
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import mimetypes
import os
from pathlib import Path
import re
import threading
from urllib.parse import unquote, urlsplit

from communication.stm32_link import Stm32LinkError
from record.last_seen import LastSeenError, LastSeenRecord, LastSeenStore
from record.leave_items import LeaveItems
from record.drawer_items import DrawerItems


_ITEM_PATTERN = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
_DRAWER_PATH = re.compile(r"^/api/drawers/([1-6])/open$")
_MAX_BODY_SIZE = 1024


class HttpApiError(RuntimeError):
    def __init__(self, status: int, code: str, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.code = code


class RetraceHttpServer:
    def __init__(
        self,
        storage: LastSeenStore,
        *,
        web_root: Path,
        host: str = "0.0.0.0",
        port: int = 8080,
        drawer_target: str = "DRAWER",
        buzzer_target: str = "TOKEN",
        leave_items: LeaveItems | None = None,
    ) -> None:
        self.storage = storage
        self.drawer_items = DrawerItems(storage.data_dir / "drawer_items.json")
        self.leave_items = leave_items
        self.web_root = Path(web_root).resolve()
        self.drawer_target = drawer_target
        self.buzzer_target = buzzer_target
        handler = self._make_handler()
        self._server = ThreadingHTTPServer((host, port), handler)
        self._server.daemon_threads = True
        self._thread = threading.Thread(
            target=self._server.serve_forever,
            name="retrace-http",
            daemon=True,
        )

    @classmethod
    def from_env(
        cls,
        storage: LastSeenStore,
        web_root: Path,
        leave_items: LeaveItems | None = None,
    ) -> "RetraceHttpServer":
        return cls(
            storage,
            web_root=web_root,
            host=os.environ.get("RETRACE_HTTP_HOST", "0.0.0.0"),
            port=int(os.environ.get("RETRACE_HTTP_PORT", "8080")),
            drawer_target=os.environ.get("RETRACE_DRAWER_CLIENT_ID", "DRAWER"),
            buzzer_target=os.environ.get("RETRACE_BUZZER_CLIENT_ID", "TOKEN"),
            leave_items=leave_items,
        )

    @property
    def address(self) -> tuple[str, int]:
        host, port = self._server.server_address[:2]
        return str(host), int(port)

    def start(self) -> None:
        self._thread.start()

    def close(self) -> None:
        self._server.shutdown()
        self._server.server_close()
        self._thread.join(timeout=2.0)

    def _make_handler(self) -> type[BaseHTTPRequestHandler]:
        owner = self

        class Handler(BaseHTTPRequestHandler):
            server_version = "RetraceHTTP/1.0"

            def do_GET(self) -> None:  # noqa: N802 - stdlib callback name
                try:
                    owner._handle_get(self)
                except HttpApiError as error:
                    owner._send_error(self, error)
                except (LastSeenError, Stm32LinkError) as error:
                    owner._send_error(
                        self,
                        HttpApiError(
                            HTTPStatus.SERVICE_UNAVAILABLE,
                            "BACKEND_UNAVAILABLE",
                            str(error),
                        ),
                    )
                except Exception as error:  # keep one bad request from killing the API
                    print(f"[HTTP ERROR] GET {self.path}: {error}")
                    owner._send_error(
                        self,
                        HttpApiError(
                            HTTPStatus.INTERNAL_SERVER_ERROR,
                            "INTERNAL",
                            "요청을 처리하지 못했습니다.",
                        ),
                    )

            def do_POST(self) -> None:  # noqa: N802 - stdlib callback name
                try:
                    owner._handle_post(self)
                except HttpApiError as error:
                    owner._send_error(self, error)
                except (LastSeenError, Stm32LinkError) as error:
                    owner._send_error(
                        self,
                        HttpApiError(
                            HTTPStatus.SERVICE_UNAVAILABLE,
                            "DEVICE_UNAVAILABLE",
                            str(error),
                        ),
                    )
                except ValueError as error:
                    owner._send_error(
                        self,
                        HttpApiError(HTTPStatus.BAD_REQUEST, "INVALID_REQUEST", str(error)),
                    )
                except Exception as error:
                    print(f"[HTTP ERROR] POST {self.path}: {error}")
                    owner._send_error(
                        self,
                        HttpApiError(
                            HTTPStatus.INTERNAL_SERVER_ERROR,
                            "INTERNAL",
                            "요청을 처리하지 못했습니다.",
                        ),
                    )

            def log_message(self, format: str, *args: object) -> None:
                print(f"[HTTP] {self.address_string()} {format % args}")

        return Handler

    def _handle_get(self, request: BaseHTTPRequestHandler) -> None:
        path = unquote(urlsplit(request.path).path)
        if path == "/api/items":
            records = {record.item: record for record in self.storage.list_items()}
            with self.drawer_items.lock:
                for item in self.drawer_items.entries:
                    records.setdefault(item, self._unseen_item(item))
                items = [self._item_json(self.drawer_items.apply(record)) for record in records.values()]
            self._send_json(request, HTTPStatus.OK, {"items": items})
            return
        if path == "/api/leave-items":
            self._send_json(request, HTTPStatus.OK, {"items": self._leave_items().get()})
            return
        if path.startswith("/api/items/"):
            item = path[len("/api/items/"):]
            self._validate_item(item)
            self._send_json(
                request,
                HTTPStatus.OK,
                self._item_json(self._get_item(item)),
            )
            return
        if path.startswith("/snapshots/"):
            self._send_snapshot(request, path[1:])
            return
        self._send_web_file(request, path)

    def _handle_post(self, request: BaseHTTPRequestHandler) -> None:
        path = unquote(urlsplit(request.path).path)
        if path == "/api/drawer-items":
            body = self._read_json_object(request)
            if set(body) != {"name", "drawer_id"}:
                raise ValueError("name과 drawer_id가 필요합니다.")
            item = self.drawer_items.add_custom(body["name"], body["drawer_id"])
            self._send_json(request, HTTPStatus.OK, {"ok": True, "item": item,
                            "display_name": self.drawer_items.display_name(item)})
            return
        if path == "/api/leave-items":
            body = self._read_json_object(request)
            if set(body) != {"items"}:
                raise HttpApiError(
                    HTTPStatus.BAD_REQUEST,
                    "INVALID_LEAVE_ITEMS",
                    "items 목록이 필요합니다.",
                )
            items = self._leave_items().set(body["items"])
            self._send_json(request, HTTPStatus.OK, {"ok": True, "items": items})
            return

        if path.startswith("/api/items/") and path.endswith("/drawer"):
            item = path[len("/api/items/"):-len("/drawer")]
            self._validate_item(item)
            body = self._read_json_object(request)
            drawer = body.get("drawer_id")
            if set(body) != {"drawer_id"} or type(drawer) is not int or not 1 <= drawer <= 6:
                raise ValueError("drawer_id는 1~6이어야 합니다.")
            with self.drawer_items.lock:
                self.drawer_items.assign(item, drawer)
            self._send_json(request, HTTPStatus.OK, {"ok": True})
            return

        if path.startswith("/api/items/") and path.endswith("/aim"):
            item = path[len("/api/items/"):-len("/aim")]
            self._validate_item(item)
            self._read_json_object(request, must_be_empty=True)
            raise HttpApiError(
                HTTPStatus.GONE,
                "LASER_DISABLED",
                "레이저 안내가 종료되었습니다. 마지막 확인 사진과 시각을 확인해 주세요.",
            )

        drawer_match = _DRAWER_PATH.fullmatch(path)
        if drawer_match is not None:
            self._read_json_object(request, must_be_empty=True)
            drawer = int(drawer_match.group(1))
            with self.drawer_items.lock:
                self.storage.send_device_command(self.drawer_target, f"OPEN:{drawer}")
                self.drawer_items.release(drawer)
            self._send_json(request, HTTPStatus.OK, {"ok": True})
            return

        if path == "/api/buzzer":
            body = self._read_json_object(request)
            if set(body) != {"enabled"} or not isinstance(body["enabled"], bool):
                raise HttpApiError(
                    HTTPStatus.BAD_REQUEST,
                    "INVALID_BUZZER",
                    "enabled는 true 또는 false여야 합니다.",
                )
            state = "1" if body["enabled"] else "0"
            self.storage.send_device_command(
                self.buzzer_target, f"BUZZER:{state}"
            )
            self._send_json(request, HTTPStatus.OK, {"ok": True})
            return

        raise HttpApiError(HTTPStatus.NOT_FOUND, "NOT_FOUND", "경로를 찾을 수 없습니다.")

    def _leave_items(self) -> LeaveItems:
        if self.leave_items is None:
            raise HttpApiError(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "LEAVE_ITEMS_UNAVAILABLE",
                "외출 알림 물건 목록을 사용할 수 없습니다.",
            )
        return self.leave_items

    def _get_item(self, item: str) -> LastSeenRecord:
        # Custom drawer items are stored locally, not registered with YOLO's DB.
        # Read their assignment directly instead of querying a camera-only backend.
        with self.drawer_items.lock:
            if item.startswith("custom_") and item in self.drawer_items.entries:
                return self.drawer_items.apply(self._unseen_item(item))
        try:
            return self.drawer_items.apply(self.storage.get_item(item))
        except LastSeenError as error:
            if "RT_NOT_FOUND" in str(error):
                with self.drawer_items.lock:
                    if item in self.drawer_items.entries:
                        return self.drawer_items.apply(self._unseen_item(item))
                raise HttpApiError(
                    HTTPStatus.NOT_FOUND,
                    "ITEM_NOT_FOUND",
                    "등록된 물건을 찾을 수 없습니다.",
                ) from error
            raise

    @staticmethod
    def _unseen_item(item: str) -> LastSeenRecord:
        return LastSeenRecord(item=item, pos_x=None, pos_y=None, seen_at=None,
                              snapshot=None, drawer_id=None, state="uncertain")

    def _item_json(self, record: LastSeenRecord) -> dict:
        data = asdict(record)
        name = self.drawer_items.display_name(record.item)
        if name is not None:
            data["display_name"] = name
        return data

    def _send_snapshot(self, request: BaseHTTPRequestHandler, relative: str) -> None:
        if not relative.startswith("snapshots/"):
            raise HttpApiError(HTTPStatus.NOT_FOUND, "NOT_FOUND", "사진을 찾을 수 없습니다.")
        root = (self.storage.data_dir / "snapshots").resolve()
        candidate = (self.storage.data_dir / relative).resolve()
        try:
            candidate.relative_to(root)
        except ValueError as error:
            raise HttpApiError(
                HTTPStatus.BAD_REQUEST, "INVALID_PATH", "잘못된 사진 경로입니다."
            ) from error
        self._send_file(request, candidate)

    def _send_web_file(self, request: BaseHTTPRequestHandler, path: str) -> None:
        relative = "index.html" if path == "/" else path.lstrip("/")
        candidate = (self.web_root / relative).resolve()
        try:
            candidate.relative_to(self.web_root)
        except ValueError as error:
            raise HttpApiError(
                HTTPStatus.BAD_REQUEST, "INVALID_PATH", "잘못된 파일 경로입니다."
            ) from error
        self._send_file(request, candidate)

    @staticmethod
    def _send_file(request: BaseHTTPRequestHandler, path: Path) -> None:
        if not path.is_file():
            raise HttpApiError(HTTPStatus.NOT_FOUND, "NOT_FOUND", "파일을 찾을 수 없습니다.")
        content = path.read_bytes()
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        request.send_response(HTTPStatus.OK)
        request.send_header("Content-Type", content_type)
        request.send_header("Content-Length", str(len(content)))
        request.send_header("Cache-Control", "no-store")
        request.end_headers()
        request.wfile.write(content)

    @staticmethod
    def _read_json_object(
        request: BaseHTTPRequestHandler,
        *,
        must_be_empty: bool = False,
    ) -> dict[str, object]:
        try:
            length = int(request.headers.get("Content-Length", "0"))
        except ValueError as error:
            raise HttpApiError(
                HTTPStatus.BAD_REQUEST, "INVALID_JSON", "요청 길이가 올바르지 않습니다."
            ) from error
        if length < 0 or length > _MAX_BODY_SIZE:
            raise HttpApiError(
                HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                "BODY_TOO_LARGE",
                "요청 본문이 너무 큽니다.",
            )
        try:
            value = json.loads(request.rfile.read(length) or b"{}")
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            raise HttpApiError(
                HTTPStatus.BAD_REQUEST, "INVALID_JSON", "JSON 본문이 올바르지 않습니다."
            ) from error
        if not isinstance(value, dict) or (must_be_empty and value):
            raise HttpApiError(
                HTTPStatus.BAD_REQUEST, "INVALID_JSON", "JSON 객체 형식이 필요합니다."
            )
        return value

    @staticmethod
    def _validate_item(item: str) -> None:
        if _ITEM_PATTERN.fullmatch(item) is None:
            raise HttpApiError(
                HTTPStatus.BAD_REQUEST, "INVALID_ITEM", "물건 ID가 올바르지 않습니다."
            )

    @staticmethod
    def _send_json(
        request: BaseHTTPRequestHandler,
        status: int,
        value: object,
    ) -> None:
        content = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode(
            "utf-8"
        )
        request.send_response(status)
        request.send_header("Content-Type", "application/json; charset=utf-8")
        request.send_header("Content-Length", str(len(content)))
        request.send_header("Cache-Control", "no-store")
        request.end_headers()
        request.wfile.write(content)

    @classmethod
    def _send_error(cls, request: BaseHTTPRequestHandler, error: HttpApiError) -> None:
        cls._send_json(
            request,
            error.status,
            {"error": {"code": error.code, "message": str(error)}},
        )
