"""Send phone notifications through an ntfy server without blocking the camera loop."""

from __future__ import annotations

from dataclasses import dataclass
import os
import queue
import threading
from urllib.error import URLError
from urllib.request import Request, urlopen


class PhoneNotifierError(RuntimeError):
    """Raised when the ntfy configuration is invalid."""


@dataclass(frozen=True)
class PhoneNotification:
    title: str
    message: str
    priority: int = 3
    tags: str = ""


# Tasker matches this exact title. The notification body names the items left behind.
LEAVE_TITLE = "Retrace Leave Check"


def leave_notification(names: list[str]) -> PhoneNotification:
    return PhoneNotification(
        title=LEAVE_TITLE,
        message=f"{', '.join(names)} 챙기셨나요?",
        priority=5,
        tags="key",
    )


def sos_notification(sender: str) -> PhoneNotification:
    return PhoneNotification(
        title="Retrace SOS",
        message=f"{sender} 비상 버튼이 눌렸습니다.",
        priority=4,
        tags="rotating_light",
    )


def build_request(base_url: str, topic: str, notification: PhoneNotification,
                  token: str = "") -> Request:
    headers = {
        "Title": notification.title,
        "Priority": str(notification.priority),
        "Content-Type": "text/plain; charset=utf-8",
    }
    if notification.tags:
        headers["Tags"] = notification.tags
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return Request(
        f"{base_url.rstrip('/')}/{topic}",
        data=notification.message.encode("utf-8"),
        headers=headers,
        method="POST",
    )


class PhoneNotifier:
    """Queue ntfy posts on a worker thread; failures are logged, never raised."""

    def __init__(self, base_url: str, topic: str, token: str = "",
                 timeout: float = 5.0) -> None:
        if not base_url.startswith(("http://", "https://")):
            raise PhoneNotifierError("RETRACE_NTFY_URL must start with http:// or https://")
        if not topic or "/" in topic:
            raise PhoneNotifierError("RETRACE_NTFY_TOPIC is empty or invalid")
        self.base_url = base_url
        self.topic = topic
        self.token = token
        self.timeout = timeout
        self._pending: queue.Queue[PhoneNotification | None] = queue.Queue(maxsize=16)
        self._thread = threading.Thread(
            target=self._run, name="retrace-ntfy", daemon=True
        )

    @classmethod
    def from_env(cls) -> "PhoneNotifier | None":
        topic = os.environ.get("RETRACE_NTFY_TOPIC", "")
        if not topic:
            return None
        return cls(
            base_url=os.environ.get("RETRACE_NTFY_URL", "http://127.0.0.1"),
            topic=topic,
            token=os.environ.get("RETRACE_NTFY_TOKEN", ""),
        )

    def start(self) -> None:
        self._thread.start()

    def close(self) -> None:
        try:
            self._pending.put_nowait(None)
        except queue.Full:
            pass
        self._thread.join(timeout=self.timeout + 1.0)

    def send(self, notification: PhoneNotification) -> None:
        try:
            self._pending.put_nowait(notification)
        except queue.Full:
            print(f"[NTFY] 전송 대기열 가득 참; 버림: {notification.title}")

    def _run(self) -> None:
        while True:
            notification = self._pending.get()
            if notification is None:
                return
            request = build_request(self.base_url, self.topic, notification, self.token)
            try:
                with urlopen(request, timeout=self.timeout) as response:
                    response.read()
                print(f"[NTFY] 전송: {notification.title}")
            except (URLError, OSError) as error:
                print(f"[NTFY] 전송 실패: {notification.title}: {error}")
