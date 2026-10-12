"""Persist manual drawer assignments independently of camera observations."""
from __future__ import annotations

from dataclasses import replace
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import threading
from uuid import uuid4


class DrawerItems:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.lock = threading.RLock()
        try:
            self.entries = json.loads(path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            self.entries = {}
        if not isinstance(self.entries, dict):
            raise ValueError("Invalid drawer storage file")

    def _save(self, entries) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(".tmp")
        temporary.write_text(json.dumps(entries), encoding="utf-8")
        os.replace(temporary, self.path)
        self.entries = entries

    def assign(self, item: str, drawer: int) -> None:
        with self.lock:
            entries = dict(self.entries)
            entries[item] = {**entries.get(item, {}), "drawer": drawer, "at": datetime.now(timezone.utc).isoformat()}
            self._save(entries)

    def add_custom(self, name: str, drawer: int) -> str:
        if not isinstance(name, str):
            raise ValueError("물건 이름은 1~40자로 입력해 주세요.")
        name = name.strip()
        if not 1 <= len(name) <= 40 or any(ord(char) < 32 or 127 <= ord(char) <= 159 for char in name):
            raise ValueError("물건 이름은 제어 문자를 제외한 1~40자로 입력해 주세요.")
        if type(drawer) is not int or not 1 <= drawer <= 6:
            raise ValueError("drawer_id는 1~6이어야 합니다.")
        with self.lock:
            item = next((item for item, entry in self.entries.items()
                         if item.startswith("custom_") and entry.get("display_name") == name),
                        "custom_" + uuid4().hex[:24])
            entries = dict(self.entries)
            entries[item] = {"drawer": drawer, "at": datetime.now(timezone.utc).isoformat(), "display_name": name}
            self._save(entries)
            return item

    def display_name(self, item: str) -> str | None:
        with self.lock:
            return self.entries.get(item, {}).get("display_name")

    def release(self, drawer: int) -> None:
        with self.lock:
            entries = dict(self.entries)
            for item, entry in entries.items():
                if entry["drawer"] == drawer:
                    entries[item] = {**entry, "drawer": None, "at": datetime.now(timezone.utc).isoformat()}
            self._save(entries)

    def apply(self, record):
        with self.lock:
            entry = self.entries.get(record.item)
            if entry is None:
                return record
            if entry["drawer"] is not None:
                return replace(record, drawer_id=entry["drawer"], pos_x=None,
                               pos_y=None, snapshot=None, seen_at=entry["at"], state="occluded")
            # A released item requires a new observation before laser guidance.
            if record.seen_at and datetime.fromisoformat(record.seen_at.replace("Z", "+00:00")) > datetime.fromisoformat(entry["at"]):
                return record
            return replace(record, drawer_id=None, pos_x=None, pos_y=None,
                           snapshot=None, seen_at=None, state="uncertain")
