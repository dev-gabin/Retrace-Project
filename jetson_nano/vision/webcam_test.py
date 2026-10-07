from pathlib import Path

import cv2
from ultralytics import YOLO


# webcam_test.py는 jetson_nano/vision/에 있으므로 상위 폴더가 jetson_nano다.
JETSON_DIR = Path(__file__).resolve().parents[1]
MODEL_PATH = JETSON_DIR / "models" / "yolo" / "coco83_yolo26n_v4_earphones_mouse.pt"

CAMERA_INDEX = 0
CONFIDENCE = 0.6
IMAGE_SIZE = 640
SAVE_PATH = JETSON_DIR / "webcam_test_result.jpg"


def main():
    if not MODEL_PATH.exists():
        print(f"[ERROR] 모델을 찾을 수 없음: {MODEL_PATH}")
        return

    print(f"[MODEL] {MODEL_PATH}")

    model = YOLO(str(MODEL_PATH))

    print("[CLASSES]")
    for class_id, class_name in model.names.items():
        print(f"  {class_id}: {class_name}")

    # 현재 Windows PC 웹캠 테스트용
    cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)

    if not cap.isOpened():
        print(f"[ERROR] 카메라 {CAMERA_INDEX}번을 열 수 없음")
        print("다른 카메라 프로그램을 닫고, CAMERA_INDEX를 1로도 확인해봐.")
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    print("\n[START] Webcam YOLO Test")
    print("Q 또는 ESC: 종료")
    print("S: 현재 화면 저장\n")

    try:
        while True:
            ret, frame = cap.read()

            if not ret:
                print("[ERROR] 카메라 영상을 읽지 못함")
                break

            results = model.predict(
                source=frame,
                conf=CONFIDENCE,
                imgsz=IMAGE_SIZE,
                device=0,
                verbose=False,
            )

            result = results[0]
            annotated_frame = result.plot()

            if result.boxes is not None and len(result.boxes) > 0:
                detected_objects = []

                for box in result.boxes:
                    class_id = int(box.cls[0])
                    confidence = float(box.conf[0])
                    class_name = model.names[class_id]
                    detected_objects.append(f"{class_name}: {confidence:.2f}")

                print("\r" + " | ".join(detected_objects) + " " * 20, end="")

            cv2.imshow("Retrace - YOLO Webcam Test", annotated_frame)

            key = cv2.waitKey(1) & 0xFF

            if key == ord("q") or key == 27:
                break

            if key == ord("s"):
                cv2.imwrite(str(SAVE_PATH), annotated_frame)
                print(f"\n[SAVED] {SAVE_PATH}")

    finally:
        cap.release()
        cv2.destroyAllWindows()
        print("\n[END] Webcam Test")


if __name__ == "__main__":
    main()
