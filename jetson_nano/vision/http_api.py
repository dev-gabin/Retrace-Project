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

from communication.stm32_link import Stm32Link, Stm32LinkError
from record.last_seen import LastSeenError, LastSeenRecord, LastSeenStore
from vision.aim_mapper import AimMapper


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
        stm32: Stm32Link,
        mapper: AimMapper,
        *,
        web_root: Path,
        host: str = "0.0.0.0",
        port: int = 8080,
        drawer_target: str = "DRAWER",
        buzzer_target: str = "TOKEN",
        laser_seconds: float = 5.0,
    ) -> None:
        if not 0 < laser_seconds <= 5.0:
            raise ValueError("RETRACE_LASER_SECONDS must be in (0, 5]")
        self.storage = storage
        self.stm32 = stm32
        self.mapper = mapper
        self.web_root = Path(web_root).resolve()
        self.drawer_target = drawer_target
        self.buzzer_target = buzzer_target
        self.laser_seconds = laser_seconds
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
        stm32: Stm32Link,
        mapper: AimMapper,
        web_root: Path,
    ) -> "RetraceHttpServer":
        return cls(
            storage,
            stm32,
            mapper,
            web_root=web_root,
            host=os.environ.get("RETRACE_HTTP_HOST", "0.0.0.0"),
            port=int(os.environ.get("RETRACE_HTTP_PORT", "8080")),
            drawer_target=os.environ.get("RETRACE_DRAWER_CLIENT_ID", "DRAWER"),
            buzzer_target=os.environ.get("RETRACE_BUZZER_CLIENT_ID", "TOKEN"),
            laser_seconds=float(os.environ.get("RETRACE_LASER_SECONDS", "5.0")),
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
            self._send_json(
                request,
                HTTPStatus.OK,
                {"items": [asdict(record) for record in self.storage.list_items()]},
            )
            return
        if path.startswith("/api/items/"):
            item = path[len("/api/items/"):]
            self._validate_item(item)
            self._send_json(
                request,
                HTTPStatus.OK,
                asdict(self._get_item(item)),
            )
            return
        if path.startswith("/snapshots/"):
            self._send_snapshot(request, path[1:])
            return
        self._send_web_file(request, path)

    def _handle_post(self, request: BaseHTTPRequestHandler) -> None:
        path = unquote(urlsplit(request.path).path)
        if path.startswith("/api/items/") and path.endswith("/aim"):
            item = path[len("/api/items/"):-len("/aim")]
            self._validate_item(item)
            self._read_json_object(request, must_be_empty=True)
            record = self._get_item(item)
            if record.pos_x is None or record.pos_y is None:
                raise HttpApiError(
                    HTTPStatus.CONFLICT,
                    "NO_POSITION",
                    "저장된 물건 좌표가 없습니다.",
                )
            pan, tilt = self.mapper.map(record.pos_x, record.pos_y)
            self.stm32.point(pan, tilt, on_seconds=self.laser_seconds)
            self._send_json(request, HTTPStatus.OK, {"ok": True})
            return

        drawer_match = _DRAWER_PATH.fullmatch(path)
        if drawer_match is not None:
            self._read_json_object(request, must_be_empty=True)
            drawer = int(drawer_match.group(1))
            self.storage.send_device_command(
                self.drawer_target, f"OPEN:{drawer}"
            )
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

    def _get_item(self, item: str) -> LastSeenRecord:
        try:
            return self.storage.get_item(item)
        except LastSeenError as error:
            if "RT_NOT_FOUND" in str(error):
                raise HttpApiError(
                    HTTPStatus.NOT_FOUND,
                    "ITEM_NOT_FOUND",
                    "등록된 물건을 찾을 수 없습니다.",
                ) from error
            raise

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
