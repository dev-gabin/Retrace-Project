"""Small stability filter for class-level Last Seen updates."""

from __future__ import annotations

from dataclasses import dataclass
import math
import time
from typing import Any


@dataclass
class _Track:
    center: tuple[int, int]
    streak: int = 1
    last_saved: tuple[int, int] | None = None
    last_attempt: float = 0.0


class StableObjectTracker:
    def __init__(
        self,
        *,
        confidence: float = 0.60,
        stable_frames: int = 3,
        stability_radius: float = 30.0,
        movement_threshold: float = 50.0,
        retry_seconds: float = 5.0,
    ) -> None:
        self.confidence = confidence
        self.stable_frames = stable_frames
        self.stability_radius = stability_radius
        self.movement_threshold = movement_threshold
        self.retry_seconds = retry_seconds
        self._tracks: dict[str, _Track] = {}

    def update(self, detections: list[dict[str, Any]]) -> list[dict[str, Any]]:
        now = time.monotonic()
        best: dict[str, dict[str, Any]] = {}
        for detection in detections:
            if detection["name"] == "person" or detection["confidence"] < self.confidence:
                continue
            item_id = detection["item_id"]
            if item_id not in best or detection["confidence"] > best[item_id]["confidence"]:
                best[item_id] = detection

        ready = []
        for item_id, detection in best.items():
            center = detection["center"]
            track = self._tracks.get(item_id)
            if track is None:
                track = _Track(center=center)
                self._tracks[item_id] = track
            elif self._distance(track.center, center) <= self.stability_radius:
                track.center = center
                track.streak += 1
            else:
                track.center = center
                track.streak = 1

            moved = (
                track.last_saved is None
                or self._distance(track.last_saved, center) >= self.movement_threshold
            )
            retry_ready = now - track.last_attempt >= self.retry_seconds
            if track.streak >= self.stable_frames and moved and retry_ready:
                track.last_attempt = now
                ready.append(detection)

        missing = set(self._tracks) - set(best)
        for item_id in missing:
            self._tracks[item_id].streak = 0
        return ready

    def mark_saved(self, item_id: str, center: tuple[int, int]) -> None:
        track = self._tracks.get(item_id)
        if track is not None:
            track.last_saved = center

    def reset_stability(self) -> None:
        for track in self._tracks.values():
            track.streak = 0

    @staticmethod
    def _distance(first: tuple[int, int], second: tuple[int, int]) -> float:
        return math.hypot(first[0] - second[0], first[1] - second[1])
