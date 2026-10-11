"""Browser-based manual pan/tilt laser calibration for the Jetson."""

from __future__ import annotations

import argparse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
import time

import cv2
import numpy as np


AUTO_CENTER_ANGLE = 90
AUTO_PAN_STEP = 8
AUTO_TILT_STEP = 6
AUTO_SETTLE_SECONDS = 0.5


def _detect_laser_dot(off_frame: np.ndarray, on_frame: np.ndarray) -> tuple[int, int]:
    if off_frame.shape != on_frame.shape:
        raise RuntimeError("레이저 OFF/ON 프레임 크기가 다릅니다.")

    difference = cv2.absdiff(on_frame, off_frame)
    score = np.max(difference, axis=2).astype(np.uint8)
    score = cv2.GaussianBlur(score, (5, 5), 0)
    _minimum, maximum, _minimum_location, maximum_location = cv2.minMaxLoc(score)
    if maximum < 18:
        raise RuntimeError("레이저 점을 찾지 못했습니다. A4 위에 점이 보이는지 확인하세요.")

    threshold = max(12, int(maximum * 0.55))
    _unused, mask = cv2.threshold(score, threshold, 255, cv2.THRESH_BINARY)
    count, labels, stats, centroids = cv2.connectedComponentsWithStats(mask)
    if count < 2:
        raise RuntimeError("레이저 점 영역을 계산하지 못했습니다.")

    label = int(labels[maximum_location[1], maximum_location[0]])
    area = int(stats[label, cv2.CC_STAT_AREA])
    frame_area = off_frame.shape[0] * off_frame.shape[1]
    if label == 0 or area > frame_area * 0.02:
        raise RuntimeError("반사광이 너무 큽니다. 무광 A4와 주변 조명을 확인하세요.")

    x, y = centroids[label]
    return round(float(x)), round(float(y))


def _fit_axis(samples: list[tuple[float, float]], minimum_span: float) -> tuple[float, float]:
    coordinates = [sample[0] for sample in samples]
    angles = [sample[1] for sample in samples]
    if max(coordinates) - min(coordinates) < minimum_span:
        raise RuntimeError("레이저 점 이동 거리가 너무 짧습니다. 자동 보정 간격을 늘려주세요.")

    coordinate_mean = sum(coordinates) / len(coordinates)
    angle_mean = sum(angles) / len(angles)
    denominator = sum((coordinate - coordinate_mean) ** 2 for coordinate in coordinates)
    if denominator == 0:
        raise RuntimeError("보정 직선을 계산할 수 없습니다.")
    slope = sum(
        (coordinate - coordinate_mean) * (angle - angle_mean)
        for coordinate, angle in samples
    ) / denominator
    intercept = angle_mean - slope * coordinate_mean
    return slope, intercept

from communication.stm32_link import Stm32Link, Stm32LinkError


PAGE = """<!doctype html>
<html lang="ko">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Retrace 레이저 보정</title>
  <style>
    body { margin: 0; background: #101828; color: #f8fafc; font: 16px system-ui, sans-serif; }
    main { max-width: 980px; margin: auto; padding: 24px; }
    img { width: 100%; max-width: 900px; background: #020617; border-radius: 12px; }
    .panel { display: flex; flex-wrap: wrap; gap: 12px; align-items: end; margin-top: 18px; }
    label { display: grid; gap: 6px; font-weight: 700; }
    input { width: 100px; padding: 10px; font: inherit; }
    button { padding: 11px 15px; border: 0; border-radius: 8px; font: inherit; font-weight: 700; cursor: pointer; }
    .point { background: #22c55e; color: #052e16; }
    .home { background: #cbd5e1; color: #0f172a; }
    .auto { background: #60a5fa; color: #172554; }
    pre { padding: 14px; overflow-x: auto; background: #020617; border-radius: 8px; }
    #status { min-height: 24px; color: #93c5fd; }
  </style>
</head>
<body>
  <main>
    <h1>Retrace 레이저 보정</h1>
    <p>카메라 화면의 목표 지점에 레이저가 오도록 pan/tilt를 조절하세요. 점등은 1초 후 자동으로 꺼집니다.</p>
    <img src="/stream.mjpg" alt="카메라 실시간 화면">
    <div class="panel">
      <label>Pan (0~180)<input id="pan" type="number" min="0" max="180" value="90"></label>
      <label>Tilt (0~180)<input id="tilt" type="number" min="0" max="180" value="90"></label>
      <button class="point" id="point">레이저 1초 비추기</button>
      <button class="home" id="home">HOME (레이저 끔)</button>
    </div>
    <p id="status">준비됨</p>
    <p>기록할 값: 화면 <b>왼쪽 / 오른쪽 / 위 / 아래</b> 끝에 레이저가 닿는 각각의 pan 또는 tilt 값.</p>
    <h2>A4 자동 5점 보정</h2>
    <p>A4를 가로로 놓고 90,90 레이저 점이 A4 중앙 근처에 오도록 배치하세요.</p>
    <div class="panel">
      <label>Pan 간격<input id="pan-step" type="number" min="2" max="30" value="8"></label>
      <label>Tilt 간격<input id="tilt-step" type="number" min="2" max="30" value="6"></label>
      <button class="auto" id="auto-calibrate">A4 자동 보정</button>
    </div>
    <pre id="result">자동 보정 결과가 여기에 표시됩니다.</pre>
  </main>
  <script>
    const status = document.querySelector('#status');
    async function send(path, body) {
      status.textContent = '명령 전송 중...';
      const response = await fetch(path, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)});
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || '명령 실패');
      status.textContent = data.message;
      return data;
    }
    document.querySelector('#point').onclick = () => send('/api/point', {
      pan: Number(document.querySelector('#pan').value),
      tilt: Number(document.querySelector('#tilt').value),
    }).catch(error => status.textContent = `오류: ${error.message}`);
    document.querySelector('#home').onclick = () => send('/api/home', {})
      .catch(error => status.textContent = `오류: ${error.message}`);
    document.querySelector('#auto-calibrate').onclick = async () => {
      const button = document.querySelector('#auto-calibrate');
      button.disabled = true;
      document.querySelector('#result').textContent = '레이저 자동 보정 중...';
      try {
        const data = await send('/api/auto-calibrate', {
          pan_step: Number(document.querySelector('#pan-step').value),
          tilt_step: Number(document.querySelector('#tilt-step').value),
        });
        document.querySelector('#result').textContent = [
          ...data.commands,
          '',
          ...data.points.map(point => `${point.name}: pixel=(${point.x}, ${point.y}) angle=(${point.pan}, ${point.tilt})`),
        ].join('\n');
      } catch (error) {
        status.textContent = `오류: ${error.message}`;
        document.querySelector('#result').textContent = '자동 보정 실패';
      } finally {
        button.disabled = false;
      }
    };
  </script>
</body>
</html>
"""


class CalibrationSession:
    def __init__(self, camera_index: int, serial_port: str | None) -> None:
        self._command_lock = threading.Lock()
        self._camera_lock = threading.Lock()
        self._calibrating = threading.Event()
        self._camera = cv2.VideoCapture(camera_index)
        if not self._camera.isOpened():
            raise RuntimeError(f"카메라 {camera_index}번을 열 수 없습니다.")
        self._stm32 = Stm32Link(port=serial_port)
        try:
            self._stm32.ping()
        except Exception:
            self._camera.release()
            self._stm32.close()
            raise

    def frame_jpeg(self) -> bytes | None:
        if self._calibrating.is_set():
            return None
        with self._camera_lock:
            ok, frame = self._camera.read()
        if not ok:
            return None
        encoded, jpeg = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 75])
        return jpeg.tobytes() if encoded else None

    def _average_frame(self, *, discard: int = 2, count: int = 3) -> np.ndarray:
        frames: list[np.ndarray] = []
        with self._camera_lock:
            for _index in range(discard):
                ok, _frame = self._camera.read()
                if not ok:
                    raise RuntimeError("카메라 프레임을 읽지 못했습니다.")
            for _index in range(count):
                ok, frame = self._camera.read()
                if not ok:
                    raise RuntimeError("카메라 프레임을 읽지 못했습니다.")
                frames.append(frame.astype(np.float32))
        return np.mean(frames, axis=0).astype(np.uint8)

    def point(self, pan: int, tilt: int) -> None:
        if not 0 <= pan <= 180 or not 0 <= tilt <= 180:
            raise ValueError("pan과 tilt는 0~180 사이여야 합니다.")
        with self._command_lock:
            self._stm32.point(pan, tilt, on_seconds=1.0)

    def home(self) -> None:
        with self._command_lock:
            self._stm32.home()

    def auto_calibrate(self, pan_step: int, tilt_step: int) -> dict[str, object]:
        if not 2 <= pan_step <= 30 or not 2 <= tilt_step <= 30:
            raise ValueError("pan/tilt 간격은 2~30 사이여야 합니다.")
        if self._calibrating.is_set():
            raise RuntimeError("자동 보정이 이미 진행 중입니다.")

        targets = (
            ("center", AUTO_CENTER_ANGLE, AUTO_CENTER_ANGLE),
            ("pan_minus", AUTO_CENTER_ANGLE - pan_step, AUTO_CENTER_ANGLE),
            ("pan_plus", AUTO_CENTER_ANGLE + pan_step, AUTO_CENTER_ANGLE),
            ("tilt_minus", AUTO_CENTER_ANGLE, AUTO_CENTER_ANGLE - tilt_step),
            ("tilt_plus", AUTO_CENTER_ANGLE, AUTO_CENTER_ANGLE + tilt_step),
        )
        measurements: list[dict[str, int | str]] = []
        frame_width = 0
        frame_height = 0

        self._calibrating.set()
        try:
            with self._command_lock:
                self._stm32.laser(False)
                for name, pan, tilt in targets:
                    self._stm32.aim(pan, tilt)
                    time.sleep(AUTO_SETTLE_SECONDS)
                    off_frame = self._average_frame()
                    self._stm32.laser(True)
                    try:
                        time.sleep(0.12)
                        on_frame = self._average_frame()
                    finally:
                        self._stm32.laser(False)

                    x, y = _detect_laser_dot(off_frame, on_frame)
                    frame_height, frame_width = off_frame.shape[:2]
                    measurements.append(
                        {"name": name, "x": x, "y": y, "pan": pan, "tilt": tilt}
                    )
        finally:
            try:
                with self._command_lock:
                    try:
                        self._stm32.laser(False)
                    finally:
                        self._stm32.home()
            finally:
                self._calibrating.clear()

        by_name = {str(point["name"]): point for point in measurements}
        center = by_name["center"]
        pan_minus = by_name["pan_minus"]
        pan_plus = by_name["pan_plus"]
        tilt_minus = by_name["tilt_minus"]
        tilt_plus = by_name["tilt_plus"]

        pan_slope, pan_intercept = _fit_axis(
            [
                (float(pan_minus["x"]), float(pan_minus["pan"])),
                (float(center["x"]), float(center["pan"])),
                (float(pan_plus["x"]), float(pan_plus["pan"])),
            ],
            minimum_span=max(20.0, frame_width * 0.05),
        )
        tilt_slope, tilt_intercept = _fit_axis(
            [
                (float(tilt_minus["y"]), float(tilt_minus["tilt"])),
                (float(center["y"]), float(center["tilt"])),
                (float(tilt_plus["y"]), float(tilt_plus["tilt"])),
            ],
            minimum_span=max(20.0, frame_height * 0.05),
        )

        calibration = {
            "RETRACE_PAN_LEFT": pan_intercept,
            "RETRACE_PAN_RIGHT": pan_slope * (frame_width - 1) + pan_intercept,
            "RETRACE_TILT_TOP": tilt_intercept,
            "RETRACE_TILT_BOTTOM": tilt_slope * (frame_height - 1) + tilt_intercept,
        }
        if any(not 0 <= value <= 180 for value in calibration.values()):
            raise RuntimeError("화면 끝 보정값이 서보 범위 0~180을 벗어났습니다.")

        rounded = {name: round(value, 1) for name, value in calibration.items()}
        commands = [f"export {name}={value}" for name, value in rounded.items()]
        return {
            "message": "A4 자동 5점 보정 완료",
            "calibration": rounded,
            "commands": commands,
            "points": measurements,
        }

    def close(self) -> None:
        self._camera.release()
        self._stm32.close()


class CalibrationHandler(BaseHTTPRequestHandler):
    server: "CalibrationServer"

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/":
            body = PAGE.encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path == "/stream.mjpg":
            self._stream_camera()
            return
        self.send_error(HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:  # noqa: N802
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(content_length) or b"{}")
            if self.path == "/api/point":
                pan = payload.get("pan")
                tilt = payload.get("tilt")
                if isinstance(pan, bool) or isinstance(tilt, bool):
                    raise ValueError("pan과 tilt는 정수여야 합니다.")
                self.server.session.point(int(pan), int(tilt))
                self._send_json(HTTPStatus.OK, {"ok": True, "message": f"AIM:{pan},{tilt} / 레이저 1초"})
                return
            if self.path == "/api/home":
                self.server.session.home()
                self._send_json(HTTPStatus.OK, {"ok": True, "message": "HOME / 레이저 꺼짐"})
                return
            if self.path == "/api/auto-calibrate":
                pan_step = payload.get("pan_step", AUTO_PAN_STEP)
                tilt_step = payload.get("tilt_step", AUTO_TILT_STEP)
                if isinstance(pan_step, bool) or isinstance(tilt_step, bool):
                    raise ValueError("pan/tilt 간격은 정수여야 합니다.")
                result = self.server.session.auto_calibrate(int(pan_step), int(tilt_step))
                self._send_json(HTTPStatus.OK, {"ok": True, **result})
                return
            self._send_json(HTTPStatus.NOT_FOUND, {"error": "없는 경로입니다."})
        except (ValueError, TypeError, RuntimeError, Stm32LinkError) as error:
            self._send_json(HTTPStatus.BAD_REQUEST, {"error": str(error)})

    def _stream_camera(self) -> None:
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            while True:
                jpeg = self.server.session.frame_jpeg()
                if jpeg is None:
                    time.sleep(0.1)
                    continue
                self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\n")
                self.wfile.write(f"Content-Length: {len(jpeg)}\r\n\r\n".encode())
                self.wfile.write(jpeg)
                self.wfile.write(b"\r\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _send_json(self, status: HTTPStatus, payload: dict[str, object]) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: object) -> None:
        return


class CalibrationServer(ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], session: CalibrationSession) -> None:
        super().__init__(address, CalibrationHandler)
        self.session = session


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Browser-based Retrace laser calibration")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--http-port", type=int, default=8081)
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--port", help="STM32 port, for example /dev/ttyACM0")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    session = None
    server = None
    try:
        session = CalibrationSession(args.camera, args.port)
        server = CalibrationServer((args.host, args.http_port), session)
        print(f"[CALIBRATION] http://0.0.0.0:{args.http_port}")
        print("[CALIBRATION] Ctrl+C: 종료")
        server.serve_forever()
    except (OSError, RuntimeError, Stm32LinkError, ValueError) as error:
        print(f"[ERROR] 보정 시작 실패: {error}")
    except KeyboardInterrupt:
        print("\n[END] 레이저 보정 종료")
    finally:
        if server is not None:
            server.server_close()
        if session is not None:
            session.close()


if __name__ == "__main__":
    main()
