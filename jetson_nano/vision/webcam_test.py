from pathlib import Path

import cv2
from ultralytics import YOLO


# --------------------------------------------------
# 모델 경로
# --------------------------------------------------

JETSON_DIR = Path(__file__).resolve().parents[1]
MODEL_PATH = JETSON_DIR / "models" / "yolo" / "best.pt"


# --------------------------------------------------
# 설정
# --------------------------------------------------

CAMERA_INDEX = 0     # 카메라가 안 맞으면 1로 변경
CONFIDENCE = 0.25
IMAGE_SIZE = 640


def main():
    # --------------------------------------------------
    # 1. 모델 확인
    # --------------------------------------------------

    if not MODEL_PATH.exists():
        print(f"[ERROR] 모델을 찾을 수 없음: {MODEL_PATH}")
        return

    print(f"[MODEL] {MODEL_PATH}")

    # --------------------------------------------------
    # 2. YOLO 모델 로드
    # --------------------------------------------------

    model = YOLO(str(MODEL_PATH))

    print("[CLASSES]")
    for class_id, class_name in model.names.items():
        print(f"  {class_id}: {class_name}")

    # --------------------------------------------------
    # 3. 웹캠 켜기
    # --------------------------------------------------

    cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)

    if not cap.isOpened():
        print(f"[ERROR] 카메라 {CAMERA_INDEX}번을 열 수 없음")
        print("CAMERA_INDEX를 1로 바꿔서 다시 실행해봐.")
        return

    # 웹캠 해상도
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

    print("\n[START] Webcam YOLO Test")
    print("Q 또는 ESC : 종료")
    print("S          : 현재 화면 저장\n")

    # --------------------------------------------------
    # 4. 실시간 카메라 반복
    # --------------------------------------------------

    while True:

        ret, frame = cap.read()

        if not ret:
            print("[ERROR] 카메라 영상을 읽지 못함")
            break

        # --------------------------------------------------
        # 5. YOLO 추론
        # --------------------------------------------------

        results = model.predict(
            source=frame,
            conf=CONFIDENCE,
            imgsz=IMAGE_SIZE,
            verbose=False
        )

        result = results[0]

        # --------------------------------------------------
        # 6. Bounding Box 그리기
        # --------------------------------------------------

        annotated_frame = result.plot()

        # --------------------------------------------------
        # 7. 검출된 객체 터미널 출력
        # --------------------------------------------------

        if result.boxes is not None and len(result.boxes) > 0:

            detected_objects = []

            for box in result.boxes:
                class_id = int(box.cls[0])
                confidence = float(box.conf[0])

                class_name = model.names[class_id]

                detected_objects.append(
                    f"{class_name}: {confidence:.2f}"
                )

            print(
                "\r" + " | ".join(detected_objects) + "          ",
                end=""
            )

        # --------------------------------------------------
        # 8. 웹캠 화면 출력
        # --------------------------------------------------

        cv2.imshow(
            "Retrace - YOLO Webcam Test",
            annotated_frame
        )

        key = cv2.waitKey(1) & 0xFF

        # Q 또는 ESC 누르면 종료
        if key == ord("q") or key == 27:
            break

        # S 누르면 사진 저장
        if key == ord("s"):
            cv2.imwrite(
                "webcam_test_result.jpg",
                annotated_frame
            )

            print("\n[SAVED] webcam_test_result.jpg")

    # --------------------------------------------------
    # 9. 웹캠 종료
    # --------------------------------------------------

    cap.release()
    cv2.destroyAllWindows()

    print("\n[END] Webcam Test")


if __name__ == "__main__":
    main()