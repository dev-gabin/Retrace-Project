"""Receive device EVT@ messages that the C server forwards to a listener client."""

from __future__ import annotations

from dataclasses import dataclass
import os
import queue
import socket
import threading


_MAX_LINE_SIZE = 4096
_RETRY_SECONDS = 5.0


class ServerEventError(RuntimeError):
    """Raised when the event listener configuration is invalid."""


@dataclass(frozen=True)
class DeviceEvent:
    sender: str
    payload: str


def parse_event_line(line: str) -> DeviceEvent | None:
    """Return an event for one '[sender]EVT@...' line, or None for other lines."""
    if not line.startswith("["):
        return None
    closing = line.find("]")
    if closing <= 1:
        return None
    payload = line[closing + 1:]
    if not payload.startswith("EVT@"):
        return None
    return DeviceEvent(sender=line[1:closing], payload=payload)


class ServerEventListener:
    """Keep a separate server login so events never mix with request replies."""

    def __init__(
        self,
        server_host: str,
        server_port: int,
        client_id: str,
        password: str,
        timeout: float = 3.0,
    ) -> None:
        self.server_host = server_host
        self.server_port = server_port
        self.client_id = client_id
        self.password = password
        self.timeout = timeout
        self._events: queue.Queue[DeviceEvent] = queue.Queue(maxsize=64)
        self._stop = threading.Event()
        self._connection: socket.socket | None = None
        self._thread = threading.Thread(
            target=self._run, name="retrace-events", daemon=True
        )

    @classmethod
    def from_env(cls) -> "ServerEventListener":
        try:
            client_id = os.environ.get("RETRACE_EVENT_CLIENT_ID", "EVENT")
            password = os.environ["RETRACE_EVENT_CLIENT_PASSWORD"]
        except KeyError as error:
            raise ServerEventError(
                f"Missing environment variable: {error.args[0]}"
            ) from error
        return cls(
            server_host=os.environ.get("RETRACE_SERVER_HOST", "127.0.0.1"),
            server_port=int(os.environ.get("RETRACE_SERVER_PORT", "5000")),
            client_id=client_id,
            password=password,
        )

    def start(self) -> None:
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        connection = self._connection
        if connection is not None:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self._thread.join(timeout=2.0)

    def get_event(self, timeout: float | None = None) -> DeviceEvent | None:
        try:
            return self._events.get(timeout=timeout) if timeout else self._events.get_nowait()
        except queue.Empty:
            return None

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                self._listen_once()
            except (OSError, UnicodeError, ServerEventError) as error:
                if not self._stop.is_set():
                    print(f"[EVENT] 서버 이벤트 연결 끊김: {error}; 재시도")
            finally:
                if self._connection is not None:
                    self._connection.close()
                    self._connection = None
            self._stop.wait(_RETRY_SECONDS)

    def _listen_once(self) -> None:
        connection = socket.create_connection(
            (self.server_host, self.server_port), timeout=self.timeout
        )
        self._connection = connection
        connection.sendall(f"[{self.client_id}:{self.password}]".encode("ascii"))
        reader = connection.makefile("rb")
        first = reader.readline(_MAX_LINE_SIZE + 1)
        if b"New connected!" not in first:
            raise ServerEventError(f"login failed: {first!r}")
        print(f"[EVENT] 서버 이벤트 수신 시작 ({self.client_id})")

        # Events may be minutes apart; only the login handshake uses a timeout.
        connection.settimeout(None)
        while not self._stop.is_set():
            raw = reader.readline(_MAX_LINE_SIZE + 1)
            if not raw:
                raise ServerEventError("server closed the connection")
            if len(raw) > _MAX_LINE_SIZE or not raw.endswith(b"\n"):
                raise ServerEventError("event line too long")
            line = raw.decode("utf-8").rstrip("\r\n")
            if line.endswith("]PING"):
                # Keep the listener reachable by the server's PING@clientID check.
                connection.sendall(b"[SERVER]OK@PING\n")
                continue
            event = parse_event_line(line)
            if event is None:
                continue
            try:
                self._events.put_nowait(event)
            except queue.Full:
                print(f"[EVENT] 이벤트 큐 가득 참; 버림: {line}")
