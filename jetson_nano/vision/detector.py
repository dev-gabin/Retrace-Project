from pathlib import Path
import re

from ultralytics import YOLO


class YoloDetector:
    """Loads the YOLO model once and runs object detection on camera frames."""

    def __init__(self, model_path: Path, confidence: float, image_size: int) -> None:
        self.model = YOLO(str(model_path))
        self.confidence = confidence
        self.image_size = image_size

    def detect(self, frame):
        result = self.model.predict(
            source=frame,
            conf=self.confidence,
            imgsz=self.image_size,
            device=0,
            verbose=False,
        )[0]

        detections = []
        for box in result.boxes:
            class_id = int(box.cls[0])
            name = self.model.names[class_id]
            x1, y1, x2, y2 = (int(value) for value in box.xyxy[0].tolist())
            detections.append(
                {
                    "class_id": class_id,
                    "item_id": self._item_id(name, class_id),
                    "name": name,
                    "confidence": float(box.conf[0]),
                    "bbox": (x1, y1, x2, y2),
                    "center": ((x1 + x2) // 2, (y1 + y2) // 2),
                }
            )

        return detections

    @staticmethod
    def _item_id(name: str, class_id: int) -> str:
        item_id = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
        if not item_id or not item_id[0].isalpha():
            item_id = f"class_{class_id}"
        return item_id[:32].rstrip("_")
