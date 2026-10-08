"""Store a detected object's full-frame snapshot through the existing C server."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import socket
import threading
from typing import Any
from uuid import uuid4

import cv2


_ITEM_PATTERN = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
_VALID_STATES = {"visible", "occluded", "uncertain"}
_MAX_LINE_SIZE = 4096


class LastSeenError(RuntimeError):
    """Raised when a snapshot or Last Seen record cannot be saved."""


@dataclass(frozen=True)
class SavedObservation:
    item: str
    snapshot: str
    x: int
    y: int
    seen_at: str
    drawer_id: int
    state: str


class LastSeenStore:
    """Atomically publish a JPEG and register it with the existing SAVE API."""

    def __init__(
        self,
        data_dir: Path,
        server_host: str,
        server_port: int,
        client_id: str,
        password: str,
        timeout: float = 3.0,
    ) -> None:
        self.data_dir = Path(data_dir)
        self.server_host = server_host
        self.server_port = server_port
        self.client_id = client_id
        self.password = password
        self.timeout = timeout
        self._connection: socket.socket | None = None
        self._connection_lock = threading.Lock()

    @classmethod
    def from_env(cls) -> "LastSeenStore":
        try:
            client_id = os.environ["RETRACE_VISION_CLIENT_ID"]
            password = os.environ["RETRACE_VISION_CLIENT_PASSWORD"]
        except KeyError as error:
            raise LastSeenError(f"Missing environment variable: {error.args[0]}") from error

        return cls(
            data_dir=Path(os.environ.get("RETRACE_DATA_DIR", "./data")),
            server_host=os.environ.get("RETRACE_SERVER_HOST", "127.0.0.1"),
            server_port=int(os.environ.get("RETRACE_SERVER_PORT", "5000")),
            client_id=client_id,
            password=password,
        )

    def save(
        self,
        item: str,
        frame: Any,
        x: int,
        y: int,
        *,
        drawer_id: int = 0,
        state: str = "visible",
        seen_at: datetime | None = None,
    ) -> SavedObservation:
        self._validate(item, frame, x, y, drawer_id, state)
        timestamp = self._utc_timestamp(seen_at)
        snapshot_path, absolute_path = self._publish_snapshot(frame)
        encoded_time = timestamp.replace(":", "%3A")
        command = (
            f"SAVE@{item}:{snapshot_path}:{x}:{y}:"
            f"{encoded_time}:{drawer_id}:{state}"
        )

        try:
            response = self._request(command)
        except Exception:
            # Delivery may have failed after the DB commit. Keep the image and
            # let rt_recover decide whether it is referenced.
            raise

        success = (
            response == f"SAVE@OK:{item}"
            or response == f"SAVE@OK_CLEANUP_PENDING:{item}"
        )
        if not success:
            if "RT_COMMIT_UNKNOWN" not in response:
                self._remove_snapshot(absolute_path)
            raise LastSeenError(response)

        return SavedObservation(
            item=item,
            snapshot=snapshot_path,
            x=x,
            y=y,
            seen_at=timestamp,
            drawer_id=drawer_id,
            state=state,
        )

    def get_coordinates(self, item: str) -> tuple[int, int]:
        if _ITEM_PATTERN.fullmatch(item) is None:
            raise LastSeenError("Invalid item ID")
        response = self._request(f"GET@{item}:XYXY")
        prefix = f"GET@{item}:"
        if not response.startswith(prefix):
            raise LastSeenError(response)
        fields = response[len(prefix):].split(":")
        if len(fields) != 2:
            raise LastSeenError(response)
        if fields[0] == "-" or fields[1] == "-":
            raise LastSeenError(f"No coordinates stored for {item}")
        try:
            return int(fields[0]), int(fields[1])
        except ValueError as error:
            raise LastSeenError(f"Invalid coordinates from server: {response}") from error

    def close(self) -> None:
        with self._connection_lock:
            if self._connection is not None:
                self._connection.close()
                self._connection = None

    @staticmethod
    def _validate(
        item: str,
        frame: Any,
        x: int,
        y: int,
        drawer_id: int,
        state: str,
    ) -> None:
        if _ITEM_PATTERN.fullmatch(item) is None:
            raise LastSeenError("Invalid item ID")
        if state not in _VALID_STATES:
            raise LastSeenError("Invalid state")
        if not 0 <= drawer_id <= 6:
            raise LastSeenError("drawer_id must be between 0 and 6")
        if frame is None or not hasattr(frame, "shape") or len(frame.shape) < 2:
            raise LastSeenError("Invalid camera frame")
        height, width = frame.shape[:2]
        if not 0 <= x < width or not 0 <= y < height:
            raise LastSeenError("Coordinates outside full camera frame")

    @staticmethod
    def _utc_timestamp(value: datetime | None) -> str:
        timestamp = value or datetime.now(timezone.utc)
        if timestamp.tzinfo is None:
            raise LastSeenError("seen_at must include a timezone")
        timestamp = timestamp.astimezone(timezone.utc)
        return timestamp.strftime("%Y-%m-%dT%H:%M:%S.%fZ")

    def _publish_snapshot(self, frame: Any) -> tuple[str, Path]:
        encoded_ok, encoded = cv2.imencode(".jpg", frame)
        if not encoded_ok:
            raise LastSeenError("JPEG encoding failed")

        snapshot_dir = self.data_dir / "snapshots"
        snapshot_dir.mkdir(parents=True, exist_ok=True)
        token = uuid4().hex
        temporary_path = snapshot_dir / f".rt_{token}.tmp"
        final_path = snapshot_dir / f"rt_{token}.jpg"

        try:
            with temporary_path.open("xb") as output:
                output.write(encoded.tobytes())
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary_path, final_path)
            self._sync_directory(snapshot_dir)
        finally:
            temporary_path.unlink(missing_ok=True)

        return f"snapshots/{final_path.name}", final_path

    def _request(self, command: str) -> str:
        self._validate_credential(self.client_id, "client ID")
        self._validate_credential(self.password, "password")

        with self._connection_lock:
            try:
                connection = self._connect()
                connection.sendall(f"[SQL]{command}\n".encode("ascii"))
                sender, response = self._read_message(connection)
                if sender != "SQL":
                    raise LastSeenError(f"Unexpected server response: [{sender}]{response}")
                return response
            except LastSeenError:
                if self._connection is not None:
                    self._connection.close()
                    self._connection = None
                raise
            except (OSError, UnicodeError) as error:
                if self._connection is not None:
                    self._connection.close()
                    self._connection = None
                raise LastSeenError(f"Storage server connection failed: {error}") from error

    def _connect(self) -> socket.socket:
        if self._connection is not None:
            return self._connection
        connection = socket.create_connection(
            (self.server_host, self.server_port), timeout=self.timeout
        )
        connection.settimeout(self.timeout)
        login = f"[{self.client_id}:{self.password}]".encode("ascii")
        connection.sendall(login)
        sender, response = self._read_message(connection)
        if sender != "SERVER" or "New connected!" not in response:
            connection.close()
            raise LastSeenError(f"Server login failed: [{sender}]{response}")
        self._connection = connection
        return connection

    @staticmethod
    def _validate_credential(value: str, label: str) -> None:
        if not value or any(character in value for character in "[]:\r\n"):
            raise LastSeenError(f"Invalid {label}")

    @staticmethod
    def _read_message(connection: socket.socket) -> tuple[str, str]:
        line = bytearray()
        while len(line) < _MAX_LINE_SIZE:
            byte = connection.recv(1)
            if not byte:
                raise LastSeenError("Storage server closed the connection")
            if byte == b"\n":
                break
            if byte != b"\r":
                line.extend(byte)
        else:
            raise LastSeenError("Storage server response is too long")

        try:
            text = line.decode("utf-8")
        except UnicodeDecodeError as error:
            raise LastSeenError("Storage server returned invalid UTF-8") from error
        if not text.startswith("[") or "]" not in text:
            raise LastSeenError(f"Malformed server response: {text}")
        sender, response = text[1:].split("]", 1)
        return sender, response

    @staticmethod
    def _remove_snapshot(path: Path) -> None:
        path.unlink(missing_ok=True)
        LastSeenStore._sync_directory(path.parent)

    @staticmethod
    def _sync_directory(path: Path) -> None:
        try:
            descriptor = os.open(path, os.O_RDONLY)
        except OSError:
            return
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
