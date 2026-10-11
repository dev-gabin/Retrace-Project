"""Browser-based manual pan/tilt laser calibration for the Jetson."""

from __future__ import annotations

import argparse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
import time

import cv2
import numpy as np

cv2.setNumThreads(1)


AUTO_CENTER_ANGLE = 90
DEFAULT_HOME_PAN = 85
DEFAULT_HOME_TILT = 42
AUTO_PAN_STEP = 8
AUTO_TILT_STEP = 6
AUTO_SETTLE_SECONDS = 0.5
AUTO_LASER_ON_SECONDS = 5.0
AUTO_LASER_CAPTURE_DELAY_SECONDS = 4.0
AUTO_DEBUG_DIR = Path("/tmp/retrace-laser-calibration")
AUTO_SEARCH_SECONDS = 5.0
AUTO_SEARCH_SETTLE_SECONDS = 0.20
SERVO_STEP_DELAY_SECONDS = 0.05
AUTO_SCAN_PAN_RADIUS = 24
AUTO_SCAN_TILT_RADIUS = 18


def _detect_laser_dot(
    off_frame: np.ndarray,
    on_frame: np.ndarray,
) -> tuple[int, int, np.ndarray]:
    if off_frame.shape != on_frame.shape:
        raise RuntimeError("레이저 OFF/ON 프레임 크기가 다릅니다.")

    difference = cv2.absdiff(on_frame, off_frame)
    score = np.max(difference, axis=2).astype(np.uint8)
    score = cv2.GaussianBlur(score, (5, 5), 0)

    # A moving hand or camera auto exposure can produce a stronger difference
    # than the laser.  Require both a changed pixel and a red laser halo.
    hsv = cv2.cvtColor(on_frame, cv2.COLOR_BGR2HSV)
    red_low = cv2.inRange(hsv, (0, 70, 100), (15, 255, 255))
    red_high = cv2.inRange(hsv, (165, 70, 100), (179, 255, 255))
    red_mask = cv2.bitwise_or(red_low, red_high)
    changed_mask = cv2.inRange(score, 18, 255)
    kernel = np.ones((3, 3), dtype=np.uint8)
    # Restrict candidates to the white A4 area seen while the laser is off.
    # This removes the fixed indicator LEDs and the wooden shelf from search.
    off_hsv = cv2.cvtColor(off_frame, cv2.COLOR_BGR2HSV)
    white_regions = cv2.inRange(off_hsv, (0, 0, 170), (179, 65, 255))
    white_regions = cv2.morphologyEx(
        white_regions, cv2.MORPH_CLOSE, kernel, iterations=2
    )
    contours, _hierarchy = cv2.findContours(
        white_regions, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
    )
    if not contours:
        raise RuntimeError("A4 흰 영역을 찾지 못했습니다.")
    paper_contour = max(contours, key=cv2.contourArea)
    if cv2.contourArea(paper_contour) < off_frame.shape[0] * off_frame.shape[1] * 0.05:
        raise RuntimeError("A4 영역이 너무 작거나 보이지 않습니다.")
    paper_mask = np.zeros(white_regions.shape, dtype=np.uint8)
    cv2.drawContours(paper_mask, [paper_contour], -1, 255, thickness=cv2.FILLED)
    mask = cv2.bitwise_and(red_mask, changed_mask)
    mask = cv2.bitwise_and(mask, paper_mask)
    # Preserve even a tiny laser core; closing joins its red halo without
    # erasing one- or two-pixel highlights.
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
    mask = cv2.dilate(mask, kernel, iterations=1)

    count, labels, stats, centroids = cv2.connectedComponentsWithStats(mask)
    if count < 2:
        raise RuntimeError("빨간 레이저 점을 찾지 못했습니다. A4와 주변 조명을 확인하세요.")

    height, width = off_frame.shape[:2]
    frame_area = height * width
    margin = max(10, round(min(width, height) * 0.02))
    candidates: list[tuple[float, int]] = []
    for label in range(1, count):
        x = int(stats[label, cv2.CC_STAT_LEFT])
        y = int(stats[label, cv2.CC_STAT_TOP])
        w = int(stats[label, cv2.CC_STAT_WIDTH])
        h = int(stats[label, cv2.CC_STAT_HEIGHT])
        area = int(stats[label, cv2.CC_STAT_AREA])
        center_x, center_y = centroids[label]
        if not 3 <= area <= frame_area * 0.01:
            continue
        if not (
            margin <= center_x < width - margin
            and margin <= center_y < height - margin
        ):
            continue
        component = labels[y:y + h, x:x + w] == label
        strength = float(np.mean(score[y:y + h, x:x + w][component]))
        candidates.append((strength, label))

    if not candidates:
        raise RuntimeError("레이저 후보가 없거나 영상 가장자리에 있습니다.")

    _strength, best_label = max(candidates)
    component_mask = labels == best_label
    weights = score.astype(np.float32) * component_mask
    total = float(weights.sum())
    if total <= 0:
        center_x, center_y = centroids[best_label]
    else:
        ys, xs = np.indices(score.shape)
        center_x = float((xs * weights).sum() / total)
        center_y = float((ys * weights).sum() / total)

    x = round(center_x)
    y = round(center_y)
    overlay = on_frame.copy()
    cv2.circle(overlay, (x, y), 14, (0, 255, 0), 2)
    cv2.drawMarker(overlay, (x, y), (255, 255, 255), cv2.MARKER_CROSS, 24, 2)
    return x, y, overlay


def _validate_measurements(
    points: dict[str, dict[str, int | str]],
    frame_width: int,
    frame_height: int,
) -> None:
    center = points["center"]
    pan_minus = points["pan_minus"]
    pan_plus = points["pan_plus"]
    tilt_minus = points["tilt_minus"]
    tilt_plus = points["tilt_plus"]

    cx, cy = int(center["x"]), int(center["y"])
    pmx, pmy = int(pan_minus["x"]), int(pan_minus["y"])
    ppx, ppy = int(pan_plus["x"]), int(pan_plus["y"])
    tmx, tmy = int(tilt_minus["x"]), int(tilt_minus["y"])
    tpx, tpy = int(tilt_plus["x"]), int(tilt_plus["y"])

    if not min(pmx, ppx) < cx < max(pmx, ppx):
        raise RuntimeError("Pan 좌우 점 사이에 중앙점이 없습니다. 레이저 검출을 확인하세요.")
    if not min(tmy, tpy) < cy < max(tmy, tpy):
        raise RuntimeError("Tilt 상하 점 사이에 중앙점이 없습니다. 레이저 검출을 확인하세요.")

    pan_cross_limit = max(24, round(frame_height * 0.10))
    tilt_cross_limit = max(32, round(frame_width * 0.10))
    if max(abs(pmy - cy), abs(ppy - cy)) > pan_cross_limit:
        raise RuntimeError("Pan 이동 중 Y 좌표 변화가 너무 큽니다. 오검출 가능성이 있습니다.")
    if max(abs(tmx - cx), abs(tpx - cx)) > tilt_cross_limit:
        raise RuntimeError("Tilt 이동 중 X 좌표 변화가 너무 큽니다. 오검출 가능성이 있습니다.")


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
    <p>웹에서 각도 이동과 레이저 ON/OFF를 따로 조작해 대략적인 중앙을 맞추세요.</p>
    <img src="/stream.mjpg" alt="카메라 실시간 화면">
    <div class="panel">
      <label>Pan (0~180)<input id="pan" type="number" min="0" max="180" value="85"></label>
      <label>Tilt (0~180)<input id="tilt" type="number" min="0" max="180" value="42"></label>
      <button class="auto" id="aim">각도만 이동</button>
      <button class="point" id="laser-on">레이저 ON</button>
      <button class="home" id="laser-off">레이저 OFF</button>
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
    document.querySelector('#aim').onclick = () => send('/api/aim', {
      pan: Number(document.querySelector('#pan').value),
      tilt: Number(document.querySelector('#tilt').value),
    }).catch(error => status.textContent = `오류: ${error.message}`);
    document.querySelector('#laser-on').onclick = () => send('/api/laser', {on: true})
      .catch(error => status.textContent = `오류: ${error.message}`);
    document.querySelector('#laser-off').onclick = () => send('/api/laser', {on: false})
      .catch(error => status.textContent = `오류: ${error.message}`);
    document.querySelector('#home').onclick = () => send('/api/home', {})
      .catch(error => status.textContent = `오류: ${error.message}`);
    document.querySelector('#auto-calibrate').onclick = async () => {
      const button = document.querySelector('#auto-calibrate');
      button.disabled = true;
      document.querySelector('#result').textContent = '레이저 자동 보정 중...';
      try {
        const data = await send('/api/auto-calibrate', {
          start_pan: Number(document.querySelector('#pan').value),
          start_tilt: Number(document.querySelector('#tilt').value),
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
        self._calibrating = threading.Event()
        self._home_pan = DEFAULT_HOME_PAN
        self._home_tilt = DEFAULT_HOME_TILT
        self._current_pan = DEFAULT_HOME_PAN
        self._current_tilt = DEFAULT_HOME_TILT
        self._camera = cv2.VideoCapture(camera_index)
        if not self._camera.isOpened():
            raise RuntimeError(f"카메라 {camera_index}번을 열 수 없습니다.")
        self._frame_condition = threading.Condition()
        self._latest_frame: np.ndarray | None = None
        self._frame_sequence = 0
        self._camera_stop = threading.Event()
        self._camera_thread = threading.Thread(
            target=self._capture_loop,
            name="calibration-camera",
            daemon=True,
        )
        self._camera_thread.start()
        self._stm32 = Stm32Link(port=serial_port)
        try:
            self._stm32.ping()
        except Exception:
            self._camera_stop.set()
            self._camera.release()
            self._stm32.close()
            raise

    def _capture_loop(self) -> None:
        while not self._camera_stop.is_set():
            ok, frame = self._camera.read()
            if not ok:
                time.sleep(0.05)
                continue
            with self._frame_condition:
                self._latest_frame = frame
                self._frame_sequence += 1
                self._frame_condition.notify_all()
            # Some V4L2 backends return frames as fast as CPU allows instead
            # of blocking at the webcam frame rate.  Cap this loop near 30 fps.
            time.sleep(0.03)

    def _next_frame(self, after_sequence: int, timeout: float = 2.0) -> tuple[np.ndarray, int]:
        deadline = time.monotonic() + timeout
        with self._frame_condition:
            while self._frame_sequence <= after_sequence:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError("카메라 새 프레임이 2초 동안 없습니다. 레이저를 끕니다.")
                self._frame_condition.wait(remaining)
            if self._latest_frame is None:
                raise RuntimeError("카메라 프레임이 없습니다.")
            return self._latest_frame.copy(), self._frame_sequence

    def frame_jpeg(self) -> bytes | None:
        if self._calibrating.is_set():
            return None
        with self._frame_condition:
            if self._latest_frame is None:
                return None
            frame = self._latest_frame.copy()
        if frame is None:
            return None
        encoded, jpeg = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, 75])
        return jpeg.tobytes() if encoded else None

    def _average_frame(self, *, discard: int = 2, count: int = 3) -> np.ndarray:
        frames: list[np.ndarray] = []
        with self._frame_condition:
            sequence = self._frame_sequence
        for _index in range(discard):
            _frame, sequence = self._next_frame(sequence)
        for _index in range(count):
            frame, sequence = self._next_frame(sequence)
            frames.append(frame.astype(np.float32))
        return np.mean(frames, axis=0).astype(np.uint8)

    def point(self, pan: int, tilt: int) -> None:
        if not 0 <= pan <= 180 or not 0 <= tilt <= 180:
            raise ValueError("pan과 tilt는 0~180 사이여야 합니다.")
        with self._command_lock:
            self._stm32.point(pan, tilt, on_seconds=1.0)

    def aim_only(self, pan: int, tilt: int) -> None:
        if not 0 <= pan <= 180 or not 0 <= tilt <= 180:
            raise ValueError("pan과 tilt는 0~180 사이여야 합니다.")
        with self._command_lock:
            self._aim_slow(pan, tilt)

    def laser(self, on: bool) -> None:
        with self._command_lock:
            self._stm32.laser(on)

    def _aim_slow(self, pan: int, tilt: int) -> None:
        """Move to a target one degree at a time to avoid abrupt servo motion."""
        start_pan = self._current_pan
        start_tilt = self._current_tilt
        steps = max(abs(pan - start_pan), abs(tilt - start_tilt))
        if steps == 0:
            self._stm32.aim(pan, tilt)
            return
        for step in range(1, steps + 1):
            ratio = step / steps
            next_pan = round(start_pan + (pan - start_pan) * ratio)
            next_tilt = round(start_tilt + (tilt - start_tilt) * ratio)
            self._stm32.aim(next_pan, next_tilt)
            self._current_pan = next_pan
            self._current_tilt = next_tilt
            time.sleep(SERVO_STEP_DELAY_SECONDS)

    def home(self) -> None:
        with self._command_lock:
            self._stm32.laser(False)
            self._aim_slow(self._home_pan, self._home_tilt)

    def _search_laser_center(
        self,
        start_pan: int,
        start_tilt: int,
        pan_step: int,
        tilt_step: int,
    ) -> tuple[int, int]:
        """Find the dot, drive it to image center, then verify it for five seconds."""
        self._stm32.laser(False)
        off_frame = self._average_frame(discard=1, count=3)
        # Serpentine raster scan over the whole safe front-facing range.
        # Alternate the pan direction on each tilt row so the mechanism does
        # not jump back to the same edge between rows.
        pan_min = max(0, start_pan - AUTO_SCAN_PAN_RADIUS)
        pan_max = min(180, start_pan + AUTO_SCAN_PAN_RADIUS)
        tilt_min = max(0, start_tilt - AUTO_SCAN_TILT_RADIUS)
        tilt_max = min(180, start_tilt + AUTO_SCAN_TILT_RADIUS)
        pan_values = list(range(pan_min, pan_max + 1, pan_step))
        if pan_values[-1] != pan_max:
            pan_values.append(pan_max)
        tilt_values = list(range(tilt_min, tilt_max + 1, tilt_step))
        if tilt_values[-1] != tilt_max:
            tilt_values.append(tilt_max)
        scan_positions: list[tuple[int, int]] = []
        for row, tilt_value in enumerate(tilt_values):
            row_pans = pan_values if row % 2 == 0 else list(reversed(pan_values))
            scan_positions.extend((pan_value, tilt_value) for pan_value in row_pans)
        def observe(pan: int, tilt: int) -> tuple[int, int, np.ndarray]:
            self._aim_slow(pan, tilt)
            time.sleep(AUTO_SEARCH_SETTLE_SECONDS)
            on_frame = self._average_frame(discard=1, count=2)
            return _detect_laser_dot(off_frame, on_frame)

        self._stm32.laser(True)
        try:
            found: tuple[int, int, int, int] | None = None
            for index, (pan, tilt) in enumerate(scan_positions):
                try:
                    x, y, overlay = observe(pan, tilt)
                except RuntimeError:
                    continue
                cv2.imwrite(str(AUTO_DEBUG_DIR / f"search_{index:02d}_found.jpg"), overlay)
                found = (pan, tilt, x, y)
                break

            if found is None:
                raise RuntimeError(
                    "전면 래스터 탐색에서 레이저 점을 찾지 못했습니다. "
                    "레이저 방향 또는 카메라 범위를 확인하세요."
                )

            pan, tilt, x, y = found
            height, width = off_frame.shape[:2]
            target = np.array([width / 2.0, height / 2.0], dtype=np.float64)

            # Measure how one pan step and one tilt step move the dot.  This
            # gives a local 2-D angle-to-pixel transform regardless of mounting
            # direction, then lets us drive the point to the camera center.
            probe_pan = max(0, min(180, pan + pan_step))
            probe_tilt = max(0, min(180, tilt + tilt_step))
            px, py, _ = observe(probe_pan, tilt)
            tx, ty, _ = observe(pan, probe_tilt)
            jacobian = np.array(
                [
                    [(px - x) / (probe_pan - pan), (tx - x) / (probe_tilt - tilt)],
                    [(py - y) / (probe_pan - pan), (ty - y) / (probe_tilt - tilt)],
                ],
                dtype=np.float64,
            )
            if abs(float(np.linalg.det(jacobian))) < 0.05:
                raise RuntimeError("pan/tilt 이동 방향을 계산할 수 없습니다.")

            for iteration in range(6):
                error = target - np.array([x, y], dtype=np.float64)
                if float(np.linalg.norm(error)) <= 15.0:
                    break
                correction = np.linalg.solve(jacobian, error)
                correction = np.clip(correction, -15.0, 15.0)
                pan = max(0, min(180, round(pan + float(correction[0]))))
                tilt = max(0, min(180, round(tilt + float(correction[1]))))
                x, y, overlay = observe(pan, tilt)
                cv2.imwrite(
                    str(AUTO_DEBUG_DIR / f"center_{iteration:02d}.jpg"), overlay
                )
            else:
                raise RuntimeError("레이저를 카메라 중앙으로 맞추지 못했습니다.")

            if float(np.linalg.norm(target - np.array([x, y]))) > 15.0:
                raise RuntimeError("레이저 중앙 오차가 너무 큽니다.")

            # The point must remain centered for five continuous seconds.
            hold_started = time.monotonic()
            while time.monotonic() - hold_started < 5.0:
                x, y, _overlay = observe(pan, tilt)
                if float(np.linalg.norm(target - np.array([x, y]))) > 15.0:
                    raise RuntimeError("5초 확인 중 레이저가 중앙에서 벗어났습니다.")

            self._home_pan = pan
            self._home_tilt = tilt
            return pan, tilt
        finally:
            self._stm32.laser(False)

    def auto_calibrate(
        self,
        start_pan: int,
        start_tilt: int,
        pan_step: int,
        tilt_step: int,
    ) -> dict[str, object]:
        if not 0 <= start_pan <= 180 or not 0 <= start_tilt <= 180:
            raise ValueError("시작 pan/tilt는 0~180 사이여야 합니다.")
        if not 2 <= pan_step <= 30 or not 2 <= tilt_step <= 30:
            raise ValueError("pan/tilt 간격은 2~30 사이여야 합니다.")
        if self._calibrating.is_set():
            raise RuntimeError("자동 보정이 이미 진행 중입니다.")

        measurements: list[dict[str, int | str]] = []
        frame_width = 0
        frame_height = 0

        self._calibrating.set()
        AUTO_DEBUG_DIR.mkdir(parents=True, exist_ok=True)
        try:
            with self._command_lock:
                center_pan, center_tilt = self._search_laser_center(
                    start_pan, start_tilt, pan_step, tilt_step
                )
                targets = (
                    ("center", center_pan, center_tilt),
                    ("pan_minus", max(0, center_pan - pan_step), center_tilt),
                    ("pan_plus", min(180, center_pan + pan_step), center_tilt),
                    ("tilt_minus", center_pan, max(0, center_tilt - tilt_step)),
                    ("tilt_plus", center_pan, min(180, center_tilt + tilt_step)),
                )
                for name, pan, tilt in targets:
                    self._aim_slow(pan, tilt)
                    time.sleep(AUTO_SETTLE_SECONDS)
                    off_frame = self._average_frame()
                    self._stm32.laser(True)
                    try:
                        laser_started = time.monotonic()
                        # Let camera auto exposure settle, then use several stable
                        # frames near the end of the five-second laser pulse.
                        time.sleep(AUTO_LASER_CAPTURE_DELAY_SECONDS)
                        on_frame = self._average_frame(discard=2, count=5)
                        remaining = AUTO_LASER_ON_SECONDS - (
                            time.monotonic() - laser_started
                        )
                        if remaining > 0:
                            time.sleep(remaining)
                    finally:
                        self._stm32.laser(False)

                    x, y, overlay = _detect_laser_dot(off_frame, on_frame)
                    cv2.imwrite(str(AUTO_DEBUG_DIR / f"{name}_off.jpg"), off_frame)
                    cv2.imwrite(str(AUTO_DEBUG_DIR / f"{name}_on.jpg"), on_frame)
                    cv2.imwrite(str(AUTO_DEBUG_DIR / f"{name}_detected.jpg"), overlay)
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
                        self._aim_slow(self._home_pan, self._home_tilt)
            finally:
                self._calibrating.clear()

        by_name = {str(point["name"]): point for point in measurements}
        center = by_name["center"]
        pan_minus = by_name["pan_minus"]
        pan_plus = by_name["pan_plus"]
        tilt_minus = by_name["tilt_minus"]
        tilt_plus = by_name["tilt_plus"]

        _validate_measurements(by_name, frame_width, frame_height)

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
            "debug_dir": str(AUTO_DEBUG_DIR),
        }

    def close(self) -> None:
        self._camera_stop.set()
        self._camera.release()
        self._camera_thread.join(timeout=2.0)
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
            if self.path == "/api/aim":
                pan = payload.get("pan")
                tilt = payload.get("tilt")
                if isinstance(pan, bool) or isinstance(tilt, bool):
                    raise ValueError("pan과 tilt는 정수여야 합니다.")
                self.server.session.aim_only(int(pan), int(tilt))
                self._send_json(
                    HTTPStatus.OK,
                    {"ok": True, "message": f"각도 이동 완료: {pan},{tilt}"},
                )
                return
            if self.path == "/api/laser":
                on = payload.get("on")
                if not isinstance(on, bool):
                    raise ValueError("레이저 상태는 true 또는 false여야 합니다.")
                self.server.session.laser(on)
                self._send_json(
                    HTTPStatus.OK,
                    {"ok": True, "message": "레이저 ON" if on else "레이저 OFF"},
                )
                return
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
                start_pan = payload.get("start_pan", DEFAULT_HOME_PAN)
                start_tilt = payload.get("start_tilt", DEFAULT_HOME_TILT)
                pan_step = payload.get("pan_step", AUTO_PAN_STEP)
                tilt_step = payload.get("tilt_step", AUTO_TILT_STEP)
                if any(
                    isinstance(value, bool)
                    for value in (start_pan, start_tilt, pan_step, tilt_step)
                ):
                    raise ValueError("pan/tilt 간격은 정수여야 합니다.")
                result = self.server.session.auto_calibrate(
                    int(start_pan), int(start_tilt), int(pan_step), int(tilt_step)
                )
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
                time.sleep(0.08)
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
