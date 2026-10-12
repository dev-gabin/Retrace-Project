"""React to forwarded device events: entrance PIR leave check and drawer SOS."""

from __future__ import annotations

import os
import threading
import time
from typing import Callable

from communication.phone_notifier import (
    PhoneNotifier,
    leave_notification,
    sos_notification,
)
from communication.server_events import DeviceEvent
from record.last_seen import LastSeenError, LastSeenStore
from record.leave_items import LeaveItems


# Same display names as web/app.js; other items use their ID.
ITEM_LABELS = {
    "car_key": "차키",
    "carkey": "차키",
    "airpods": "에어팟",
    "earphones": "이어폰",
    "wallet": "지갑",
}


class LeaveVerifier:
    """Confirm registered items in fresh camera frames before a leave alert."""

    def __init__(
        self,
        *,
        seconds: float = 3.0,
        confidence: float = 0.50,
        required_hits: int = 3,
    ) -> None:
        if seconds <= 0:
            raise ValueError("RETRACE_LEAVE_CHECK_SECONDS must be positive")
        if not 0 < confidence <= 1:
            raise ValueError("RETRACE_LEAVE_CHECK_CONFIDENCE must be in (0, 1]")
        if required_hits < 1:
            raise ValueError("RETRACE_LEAVE_CHECK_HITS must be at least 1")
        self.seconds = seconds
        self.confidence = confidence
        self.required_hits = required_hits
        self._lock = threading.Lock()
        self._finished = threading.Event()
        self._active_items: set[str] = set()
        self._hits: dict[str, int] = {}
        self._positions: dict[str, tuple[int, int]] = {}

    @classmethod
    def from_env(cls) -> "LeaveVerifier":
        return cls(
            seconds=float(os.environ.get("RETRACE_LEAVE_CHECK_SECONDS", "3.0")),
            confidence=float(
                os.environ.get("RETRACE_LEAVE_CHECK_CONFIDENCE", "0.60")
            ),
            required_hits=int(os.environ.get("RETRACE_LEAVE_CHECK_HITS", "3")),
        )

    def active(self) -> bool:
        with self._lock:
            return bool(self._active_items)

    def check(self, items: list[str]) -> list[tuple[str, int, int]]:
        """Wait for fresh detections and return only repeatedly confirmed items."""
        with self._lock:
            if self._active_items:
                return []
            self._active_items = set(items)
            self._hits = {item: 0 for item in self._active_items}
            self._positions = {}
            self._finished.clear()

        self._finished.wait(timeout=self.seconds)

        with self._lock:
            confirmed = [
                (item, *self._positions[item])
                for item in items
                if self._hits.get(item, 0) >= self.required_hits
                and item in self._positions
            ]
            self._active_items.clear()
            self._hits = {}
            self._positions = {}
            return confirmed

    def observe(self, detections: list[dict]) -> None:
        """Record at most one high-confidence hit per registered item per frame."""
        with self._lock:
            if not self._active_items:
                return
            best: dict[str, dict] = {}
            for detection in detections:
                item = detection["item_id"]
                if (item not in self._active_items
                        or detection["confidence"] < self.confidence):
                    continue
                if (item not in best
                        or detection["confidence"] > best[item]["confidence"]):
                    best[item] = detection
            for item, detection in best.items():
                self._hits[item] += 1
                self._positions[item] = detection["center"]


class DeviceEventHandler:
    """Runs each accepted event on its own worker so the camera loop never waits."""

    def __init__(
        self,
        storage: LastSeenStore,
        notifier: PhoneNotifier | None,
        leave_items: LeaveItems,
        leave_verifier: LeaveVerifier,
        *,
        entrance_id: str = "ENTRANCE",
        cooldown_seconds: float = 15.0,
        clock: Callable[[], float] = time.monotonic,
        run_async: bool = True,
    ) -> None:
        self.storage = storage
        self.notifier = notifier
        self.entrance_id = entrance_id
        self.leave_items = leave_items
        self.leave_verifier = leave_verifier
        self.cooldown_seconds = cooldown_seconds
        self._clock = clock
        self._run_async = run_async
        self._last_leave_alert: float | None = None
        self._last_sos: dict[str, float] = {}

    @classmethod
    def from_env(
        cls,
        storage: LastSeenStore,
        notifier: PhoneNotifier | None,
        leave_items: LeaveItems,
        leave_verifier: LeaveVerifier,
    ) -> "DeviceEventHandler":
        return cls(
            storage,
            notifier,
            leave_items,
            leave_verifier,
            entrance_id=os.environ.get("RETRACE_ENTRANCE_CLIENT_ID", "ENTRANCE"),
            cooldown_seconds=float(
                os.environ.get("RETRACE_LEAVE_ALERT_COOLDOWN", "15")
            ),
        )

    def handle(self, event: DeviceEvent) -> None:
        now = self._clock()
        if event.sender == self.entrance_id and event.payload.endswith(":PIRON"):
            # Repeated PIR edges while someone stands at the door are one visit.
            if (self._last_leave_alert is not None and
                    now - self._last_leave_alert < self.cooldown_seconds):
                return
            self._last_leave_alert = now
            self._dispatch(self._check_leave)
        elif event.payload.endswith(":SOS"):
            last = self._last_sos.get(event.sender)
            if last is not None and now - last < self.cooldown_seconds:
                return
            self._last_sos[event.sender] = now
            if self.notifier is not None:
                self.notifier.send(sos_notification(event.sender))
            print(f"[EVENT] SOS from {event.sender}")

    def _dispatch(self, action: Callable[[], None]) -> None:
        if self._run_async:
            threading.Thread(target=action, name="retrace-leave", daemon=True).start()
        else:
            action()

    def _check_leave(self) -> None:
        registered = self.leave_items.get()
        if not registered:
            print("[LEAVE] 외출 알림 등록 물건 없음; 알림 생략")
            return

        print("[LEAVE] 등록 물건 3초 재확인 시작")
        confirmed = self.leave_verifier.check(registered)
        if not confirmed:
            print("[LEAVE] 재확인된 등록 물건 없음; 알림 생략")
            return

        names = [ITEM_LABELS.get(item, item) for item, _, _ in confirmed]
        print(f"[LEAVE] 책상에 남은 물건: {', '.join(names)}; 현관등 경고·폰 알림")
        if self.notifier is not None:
            self.notifier.send(leave_notification(names))
        try:
            self.storage.send_device_command(self.entrance_id, "LIGHT:ALERT")
        except LastSeenError as error:
            print(f"[LEAVE] 현관등 ALERT 실패: {error}")
