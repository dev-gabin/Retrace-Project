from pathlib import Path
import time

import cv2

from communication.phone_notifier import PhoneNotifier, PhoneNotifierError
from communication.server_events import ServerEventError, ServerEventListener
from communication.stm32_link import Stm32Link, Stm32LinkError
from record.last_seen import LastSeenError, LastSeenStore
from record.leave_items import LeaveItems
from vision.aim_mapper import AimMapper
from vision.detector import YoloDetector
from vision.http_api import RetraceHttpServer
from vision.leave_alert import DeviceEventHandler, LeaveVerifier
from vision.object_tracker import StableObjectTracker


JETSON_DIR = Path(__file__).resolve().parents[1]
REPOSITORY_DIR = JETSON_DIR.parent
MODEL_PATH = JETSON_DIR / "models" / "yolo" / "coco83_yolo26n_v6_demo_desk.pt"

CAMERA_INDEX = 0
CONFIDENCE = 0.50
IMAGE_SIZE = 640
PIR_GRACE_SECONDS = 10.0


def main() -> None:
    if not MODEL_PATH.exists():
        print(f"[ERROR] 모델을 찾을 수 없음: {MODEL_PATH}")
        return

    print(f"[MODEL] {MODEL_PATH}")
    detector = YoloDetector(MODEL_PATH, CONFIDENCE, IMAGE_SIZE)
    tracker = StableObjectTracker(stable_frames=2)

    storage = None
    stm32 = None
    http_server = None
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
        storage.close()
        return

    # camera.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    # camera.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    # camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
    # camera.set(cv2.CAP_PROP_FPS, 30)
    # camera.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    # 카메라 자동 노출 안정화 및 초기 불완전 프레임 제거
    time.sleep(1.0)
    warmup_frame = None
    for _ in range(10):
        ok, candidate = camera.read()
        if ok:
            warmup_frame = candidate
    if warmup_frame is None:
        print("[ERROR] 카메라 초기 프레임을 읽지 못함")
        camera.release()
        stm32.close()
        storage.close()
        return

    leave_items = LeaveItems(storage.data_dir / "leave_items.json")
    frame_height, frame_width = warmup_frame.shape[:2]
    mapper = None
    try:
        mapper = AimMapper.from_env(frame_width, frame_height)
    except ValueError as error:
        print(f"[LASER] 좌표 보정 오류로 웹 레이저 안내 끔: {error}")
    try:
        http_server = RetraceHttpServer.from_env(
            storage,
            REPOSITORY_DIR / "web",
            leave_items,
            stm32=stm32,
            mapper=mapper,
        )
        http_server.start()
    except (OSError, ValueError) as error:
        print(f"[ERROR] HTTP API 시작 실패: {error}")
        camera.release()
        stm32.close()
        storage.close()
        return

    http_host, http_port = http_server.address
    print(f"[HTTP] http://{http_host}:{http_port}")

    # 현관 PIR·SOS 이벤트와 ntfy 알림은 설정이 없으면 끄고 나머지 기능은 계속 동작한다.
    notifier = None
    event_listener = None
    event_handler = None
    leave_verifier = None
    try:
        notifier = PhoneNotifier.from_env()
        if notifier is None:
            print("[NTFY] RETRACE_NTFY_TOPIC 없음; 폰 알림 끔")
        else:
            notifier.start()
        event_listener = ServerEventListener.from_env()
        event_listener.start()
        leave_verifier = LeaveVerifier.from_env()
        event_handler = DeviceEventHandler.from_env(
            storage, notifier, leave_items, leave_verifier
        )
    except (PhoneNotifierError, ServerEventError, ValueError) as error:
        print(f"[EVENT] 현관·SOS 이벤트 처리 끔: {error}")
        event_listener = None

    state = "IDLE"
    pir_high = False
    pir_low_started: float | None = None

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
                if event == "EVT@PIR:1":
                    pir_high = True
                    pir_low_started = None
                    if state != "ACTIVE":
                        state = "ACTIVE"
                        print("[STATE] ACTIVE (PIR)")
                elif event == "EVT@PIR:0":
                    if pir_high:
                        pir_low_started = time.monotonic()
                    pir_high = False
                    if state == "ACTIVE":
                        state = "GRACE"
                        print("[STATE] GRACE (PIR OFF)")

            while event_listener is not None and event_handler is not None:
                device_event = event_listener.get_event(timeout=0)
                if device_event is None:
                    break
                event_handler.handle(device_event)

            should_verify_leave = leave_verifier is not None and leave_verifier.active()
            if state == "IDLE" and not should_verify_leave:
                continue

            now = time.monotonic()
            detections = detector.detect(frame)
            if leave_verifier is not None:
                leave_verifier.observe(detections)

            if state == "IDLE":
                continue
            if (not pir_high and pir_low_started is not None
                    and now - pir_low_started >= PIR_GRACE_SECONDS):
                state = "IDLE"
                pir_low_started = None
                tracker.reset_stability()
                print("[STATE] IDLE")
                continue

            for detection in tracker.update(detections):
                center = detection["center"]
                x1, y1, x2, y2 = detection["bbox"]

                # 추론용 원본은 유지하고 저장용 복사본에만 표시한다.
                snapshot_frame = frame.copy()
                cv2.rectangle(
                    snapshot_frame,
                    (x1, y1),
                    (x2, y2),
                    (0, 255, 0),
                    2,
                )
                label = f"{detection['name']} {detection['confidence']:.2f}"
                cv2.putText(
                    snapshot_frame,
                    label,
                    (x1, max(y1 - 10, 20)),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.6,
                    (0, 255, 0),
                    2,
                    cv2.LINE_AA,
                )

                try:
                    saved = storage.save(
                        detection["item_id"],
                        snapshot_frame,
                        center[0],
                        center[1],
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
        if http_server is not None:
            http_server.close()
        if event_listener is not None:
            event_listener.close()
        if notifier is not None:
            notifier.close()
        try:
            stm32.laser(False)
        except Stm32LinkError:
            pass
        stm32.close()
        storage.close()
        print("[END] Jetson YOLO mode")
        
if __name__ == "__main__":
    main()
