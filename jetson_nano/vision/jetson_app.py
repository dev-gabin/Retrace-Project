from pathlib import Path

import cv2

from vision.detector import YoloDetector


JETSON_DIR = Path(__file__).resolve().parents[1]
MODEL_PATH = JETSON_DIR / "models" / "yolo" / "coco83_yolo26n_v6_demo_desk.pt"

CAMERA_INDEX = 0
CONFIDENCE = 0.6
IMAGE_SIZE = 640


def main() -> None:
    if not MODEL_PATH.exists():
        print(f"[ERROR] 모델을 찾을 수 없음: {MODEL_PATH}")
        return

    print(f"[MODEL] {MODEL_PATH}")
    detector = YoloDetector(MODEL_PATH, CONFIDENCE, IMAGE_SIZE)

    # Jetson Linux USB webcam: OpenCV chooses its default V4L2 camera backend.
    camera = cv2.VideoCapture(CAMERA_INDEX)
    if not camera.isOpened():
        print(f"[ERROR] 카메라 {CAMERA_INDEX}번을 열 수 없음")
        print("USB 웹캠 연결과 CAMERA_INDEX를 확인하세요.")
        return

    camera.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    print("[START] Jetson YOLO mode (Ctrl+C: 종료)")

    try:
        while True:
            ok, frame = camera.read()
            if not ok:
                print("[ERROR] 카메라 영상을 읽지 못함")
                break

            detections = detector.detect(frame)
            if detections:
                summary = " | ".join(
                    f"{item['name']}: {item['confidence']:.2f}"
                    for item in detections
                )
                print(f"\r{summary}                    ", end="", flush=True)

    except KeyboardInterrupt:
        print("\n[STOP] Ctrl+C 종료")
    finally:
        camera.release()
        print("[END] Jetson YOLO mode")
