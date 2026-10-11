"""Browser-based manual pan/tilt laser calibration for the Jetson."""

from __future__ import annotations

import argparse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading
import time

import cv2

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
  </main>
  <script>
    const status = document.querySelector('#status');
    async function send(path, body) {
      status.textContent = '명령 전송 중...';
      const response = await fetch(path, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)});
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || '명령 실패');
      status.textContent = data.message;
    }
    document.querySelector('#point').onclick = () => send('/api/point', {
      pan: Number(document.querySelector('#pan').value),
      tilt: Number(document.querySelector('#tilt').value),
    }).catch(error => status.textContent = `오류: ${error.message}`);
    document.querySelector('#home').onclick = () => send('/api/home', {})
      .catch(error => status.textContent = `오류: ${error.message}`);
  </script>
</body>
</html>
"""


class CalibrationSession:
    def __init__(self, camera_index: int, serial_port: str | None) -> None:
        self._command_lock = threading.Lock()
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
        ok, frame = self._camera.read()
        if not ok:
            return None
        encoded, jpeg = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 75])
        return jpeg.tobytes() if encoded else None

    def point(self, pan: int, tilt: int) -> None:
        if not 0 <= pan <= 180 or not 0 <= tilt <= 180:
            raise ValueError("pan과 tilt는 0~180 사이여야 합니다.")
        with self._command_lock:
            self._stm32.point(pan, tilt, on_seconds=1.0)

    def home(self) -> None:
        with self._command_lock:
            self._stm32.home()

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
            self._send_json(HTTPStatus.NOT_FOUND, {"error": "없는 경로입니다."})
        except (ValueError, TypeError, Stm32LinkError) as error:
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
