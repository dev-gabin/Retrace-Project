from pathlib import Path

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
            detections.append(
                {
                    "name": self.model.names[class_id],
                    "confidence": float(box.conf[0]),
                }
            )

        return detections
