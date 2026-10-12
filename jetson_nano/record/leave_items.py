"""Items the user registered in the web to be reminded about when leaving."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import threading


_ITEM_PATTERN = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
_MAX_ITEMS = 20


class LeaveItems:
    """A small JSON file next to the snapshots; the web edits it, the PIR check reads it."""

    def __init__(self, path: Path) -> None:
        self.path = Path(path)
        self._lock = threading.Lock()

    def get(self) -> list[str]:
        with self._lock:
            try:
                data = json.loads(self.path.read_text(encoding="utf-8"))
            except FileNotFoundError:
                return []
            except (OSError, ValueError) as error:
                print(f"[LEAVE] 등록 목록을 읽지 못함: {error}")
                return []
        items = data.get("items") if isinstance(data, dict) else None
        if not isinstance(items, list):
            return []
        return [item for item in items if isinstance(item, str) and _ITEM_PATTERN.fullmatch(item)]

    def set(self, items: list[str]) -> list[str]:
        if not isinstance(items, list) or len(items) > _MAX_ITEMS:
            raise ValueError("items는 20개 이하의 물건 ID 목록이어야 합니다.")
        cleaned: list[str] = []
        for item in items:
            if not isinstance(item, str) or _ITEM_PATTERN.fullmatch(item) is None:
                raise ValueError("물건 ID 형식을 확인해 주세요.")
            if item not in cleaned:
                cleaned.append(item)
        with self._lock:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.path.with_suffix(".tmp")
            temporary.write_text(json.dumps({"items": cleaned}), encoding="utf-8")
            os.replace(temporary, self.path)
        return cleaned
