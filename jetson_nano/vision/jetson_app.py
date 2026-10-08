from pathlib import Path
from collections import deque
import time

import cv2

from communication.stm32_link import Stm32Link, Stm32LinkError
from record.last_seen import LastSeenError, LastSeenStore
from vision.detector import YoloDetector
from vision.object_tracker import StableObjectTracker


JETSON_DIR = Path(__file__).resolve().parents[1]
MODEL_PATH = JETSON_DIR / "models" / "yolo" / "coco83_yolo26n_v6_demo_desk.pt"

CAMERA_INDEX = 0
CONFIDENCE = 0.40
IMAGE_SIZE = 640
PERSON_ENTER_CONFIDENCE = 0.55
PERSON_KEEP_CONFIDENCE = 0.40
PERSON_GRACE_SECONDS = 10.0


def main() -> None:
    if not MODEL_PATH.exists():
        print(f"[ERROR] 모델을 찾을 수 없음: {MODEL_PATH}")
        return

    print(f"[MODEL] {MODEL_PATH}")
    detector = YoloDetector(MODEL_PATH, CONFIDENCE, IMAGE_SIZE)
    tracker = StableObjectTracker()

    storage = None
    stm32 = None
    try:
        storage = LastSeenStore.from_env()
        stm32 = Stm32Link()
        stm32.ping()
    except (LastSeenError, Stm32LinkError, ValueError) as error:
        if stm32 is not None:
            stm32.close()
        if storage is not None:
            storage.close()
        print(f"[ERROR] 초기 연결 실패: {error}")
        return

    # Jetson Linux USB webcam: OpenCV chooses its default V4L2 camera backend.
    camera = cv2.VideoCapture(CAMERA_INDEX)
    if not camera.isOpened():
        print(f"[ERROR] 카메라 {CAMERA_INDEX}번을 열 수 없음")
        print("USB 웹캠 연결과 CAMERA_INDEX를 확인하세요.")
        stm32.close()
        return

    camera.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    camera.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    state = "IDLE"
    pir_high = False
    state_started = time.monotonic()
    last_person_seen: float | None = None
    person_window: deque[bool] = deque(maxlen=5)

    print("[START] PIR-triggered Jetson YOLO mode (Ctrl+C: 종료)")

    try:
        while True:
            ok, frame = camera.read()
            if not ok:
                print("[ERROR] 카메라 영상을 읽지 못함")
                break

            while True:
                event = stm32.get_event(timeout=0)
                if event is None:
                    break
                if event == "EVT:PIR:1":
                    pir_high = True
                    if state in {"IDLE", "GRACE"}:
                        state = "SEARCHING"
                        state_started = time.monotonic()
                        person_window.clear()
                        print("[STATE] SEARCHING")
                elif event == "EVT:PIR:0":
                    pir_high = False

            if state == "IDLE":
                continue

            now = time.monotonic()
            detections = detector.detect(frame)
            person_confidence = max(
                (
                    detection["confidence"]
                    for detection in detections
                    if detection["name"] == "person"
                ),
                default=0.0,
            )

            if state == "SEARCHING":
                person_window.append(person_confidence >= PERSON_ENTER_CONFIDENCE)
                if len(person_window) == 5 and sum(person_window) >= 3:
                    state = "ACTIVE"
                    last_person_seen = now
                    print("[STATE] ACTIVE")
                elif not pir_high and now - state_started >= PERSON_GRACE_SECONDS:
                    state = "IDLE"
                    tracker.reset_stability()
                    print("[STATE] IDLE")
                    continue
            elif person_confidence >= PERSON_KEEP_CONFIDENCE:
                if state != "ACTIVE":
                    print("[STATE] ACTIVE")
                state = "ACTIVE"
                last_person_seen = now
            else:
                if last_person_seen is None:
                    last_person_seen = now
                if state != "GRACE":
                    print("[STATE] GRACE")
                state = "GRACE"
                if not pir_high and now - last_person_seen >= PERSON_GRACE_SECONDS:
                    state = "IDLE"
                    tracker.reset_stability()
                    print("[STATE] IDLE")
                    continue

            for detection in tracker.update(detections):
                center = detection["center"]
                try:
                    saved = storage.save(
                        detection["item_id"], frame, center[0], center[1]
                    )
                except LastSeenError as error:
                    print(f"[SAVE ERROR] {detection['name']}: {error}")
                    continue
                tracker.mark_saved(detection["item_id"], center)
                print(
                    f"[SAVED] {detection['name']} "
                    f"({saved.x}, {saved.y}) {saved.snapshot}"
                )

    except KeyboardInterrupt:
        print("\n[STOP] Ctrl+C 종료")
    finally:
        camera.release()
        try:
            stm32.laser(False)
        except Stm32LinkError:
            pass
        stm32.close()
        storage.close()
        print("[END] Jetson YOLO mode")
