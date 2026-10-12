"""Measured pixel-to-servo mapping on one fixed plane (no hardware access)."""
from __future__ import annotations

from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import tempfile
from typing import Union

import numpy as np


def calibration_path() -> Path:
    return Path(os.environ.get(
        "RETRACE_AIM_CALIBRATION",
        str(Path(__file__).resolve().parents[1] / ".local" / "aim_calibration.json"),
    ))


def _number(value: object) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("보정값은 유한한 숫자여야 합니다.")
    return float(value)


def _hull(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    def cross(a, b, c):
        return (b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0])
    ordered = sorted(set(points))
    lower, upper = [], []
    for chain, values in ((lower, ordered), (upper, reversed(ordered))):
        for point in values:
            while len(chain) >= 2 and cross(chain[-2], chain[-1], point) <= 0:
                chain.pop()
            chain.append(point)
    return lower[:-1] + upper[:-1]


def _features(xy: np.ndarray) -> np.ndarray:
    x, y = (2 * xy - 1).T
    return np.column_stack((np.ones_like(x), x, y, x*x, x*y, y*y))


def _solve(design: np.ndarray, angles: np.ndarray) -> np.ndarray:
    coefficients, _, rank, singular = np.linalg.lstsq(design, angles, rcond=None)
    if rank < 6 or singular[0] / singular[-1] > 1000:
        raise ValueError("보정점 분포가 불안정합니다. 모서리·변·중앙에 3×3 이상으로 배치하세요.")
    return coefficients


@dataclass(frozen=True)
class PlaneCalibration:
    width: int
    height: int
    samples: tuple[tuple[float, float, float, float], ...]
    coefficients: np.ndarray
    hull: tuple[tuple[float, float], ...]
    rmse: tuple[float, float]
    loo_max: tuple[float, float]

    @classmethod
    def fit(cls, width: int, height: int, points: list[dict]) -> "PlaneCalibration":
        if type(width) is not int or type(height) is not int or min(width, height) < 2:
            raise ValueError("유효한 원본 영상 크기가 필요합니다.")
        if not isinstance(points, list) or not 9 <= len(points) <= 100:
            raise ValueError("보정점은 9~100개가 필요합니다. 4×4(16점)를 권장합니다.")
        rows = []
        for point in points:
            if not isinstance(point, dict) or not all(k in point for k in ("x", "y", "pan", "tilt")):
                raise ValueError("각 점에 x, y, pan, tilt가 필요합니다.")
            x, y, pan, tilt = (_number(point[k]) for k in ("x", "y", "pan", "tilt"))
            if not (0 <= x < width and 0 <= y < height and 0 <= pan <= 180 and 0 <= tilt <= 180):
                raise ValueError("화면 좌표 또는 서보 각도가 범위를 벗어났습니다.")
            if any(math.hypot(x-r[0], y-r[1]) < 2 for r in rows):
                raise ValueError("같거나 너무 가까운 보정점이 있습니다(2px 미만).")
            rows.append((x, y, pan, tilt))
        values = np.asarray(rows, dtype=float)
        xy = values[:, :2] / (width-1, height-1)
        hull = _hull([tuple(p) for p in xy])
        area = abs(sum(a[0]*b[1]-a[1]*b[0] for a, b in zip(hull, hull[1:]+hull[:1]))) / 2
        if np.min(np.ptp(xy, axis=0)) < .2 or area < .04:
            raise ValueError("보정 범위가 너무 좁습니다. 조준할 책상 영역 전체에 점을 기록하세요.")
        design = _features(xy)
        coefficients = _solve(design, values[:, 2:])
        errors = design @ coefficients - values[:, 2:]
        rmse = np.sqrt(np.mean(errors**2, axis=0))
        # Leave-one-out prevents a small set of contradictory points from looking good.
        loo = []
        for index in range(len(rows)):
            mask = np.arange(len(rows)) != index
            estimate = design[index] @ _solve(design[mask], values[mask, 2:])
            loo.append(np.abs(estimate - values[index, 2:]))
        loo_max = np.max(loo, axis=0)
        if np.max(rmse) > 1.0 or np.max(loo_max) > 3.0:
            raise ValueError(
                f"보정 오차가 큽니다: RMS={rmse.round(2).tolist()}°, "
                f"교차검증 최대={loo_max.round(2).tolist()}°. 잘못 클릭한 점을 다시 측정하세요."
            )
        return cls(width, height, tuple(rows), coefficients, tuple(hull),
                   tuple(float(v) for v in rmse), tuple(float(v) for v in loo_max))

    def map(self, x: float, y: float, width: int, height: int) -> tuple[int, int]:
        if (width, height) != (self.width, self.height):
            raise ValueError("보정 영상과 현재 해상도가 다릅니다. 같은 해상도로 다시 보정하세요.")
        x, y = _number(x), _number(y)
        if not (0 <= x < width and 0 <= y < height):
            raise ValueError("좌표가 영상 밖에 있습니다.")
        point = (x/(width-1), y/(height-1))
        for a, b in zip(self.hull, self.hull[1:]+self.hull[:1]):
            if (b[0]-a[0])*(point[1]-a[1]) - (b[1]-a[1])*(point[0]-a[0]) < -1e-9:
                raise ValueError("보정한 영역 밖입니다. 해당 위치까지 보정점을 추가하세요.")
        angles = _features(np.array([point]))[0] @ self.coefficients
        if not np.all(np.isfinite(angles)) or np.any(angles < 0) or np.any(angles > 180):
            raise ValueError("계산한 각도가 서보 범위를 벗어났습니다.")
        return tuple(math.floor(float(a)+.5) for a in angles)

    def document(self) -> dict:
        return {
            "version": 1, "model": "quadratic_plane", "width": self.width, "height": self.height,
            "points": [dict(zip(("x", "y", "pan", "tilt"), row)) for row in self.samples],
            "metrics": {"rmse_degrees": self.rmse, "loo_max_degrees": self.loo_max},
            "hull": [[x*(self.width-1), y*(self.height-1)] for x, y in self.hull],
        }

    @classmethod
    def from_document(cls, document: dict) -> "PlaneCalibration":
        if not isinstance(document, dict) or document.get("version") != 1 or document.get("model") != "quadratic_plane":
            raise ValueError("지원하지 않는 보정 파일입니다.")
        return cls.fit(document.get("width"), document.get("height"), document.get("points"))

    @classmethod
    def load(cls, path: Path) -> "PlaneCalibration":
        try:
            return cls.from_document(json.loads(path.read_text(encoding="utf-8")))
        except OSError as error:
            raise ValueError(f"보정 파일을 읽을 수 없습니다: {path}") from error

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        name = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                             prefix=path.name, suffix=".tmp", delete=False) as stream:
                name = stream.name
                json.dump(self.document(), stream, ensure_ascii=False, indent=2, allow_nan=False)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(name, path)
        finally:
            if name is not None and os.path.exists(name):
                os.unlink(name)


@dataclass(frozen=True)
class SphericalPlaneCalibration:
    """Pixel-to-servo mapping through a measured horizontal plane.

    Pixels are first projected into table millimetres. The target point is
    then expressed as azimuth/elevation from the pan/tilt rotation centre,
    which avoids treating image X/Y as if they were servo angles.
    """

    width: int
    height: int
    pixel_to_plane: np.ndarray
    axis_projection_mm: tuple[float, float]
    axis_height_mm: float
    center_azimuth_deg: float
    center_elevation_deg: float
    pan_scale: float
    tilt_scale: float
    pan_center_command: float
    tilt_center_command: float
    pan_correction_deg: float
    tilt_correction_deg: float
    uncertainty_mm: tuple[float, float]

    @staticmethod
    def _matrix(value: object) -> np.ndarray:
        matrix = np.asarray(value, dtype=float)
        if matrix.shape != (3, 3) or not np.all(np.isfinite(matrix)):
            raise ValueError("pixel_to_plane은 유한한 3x3 행렬이어야 합니다.")
        if abs(float(np.linalg.det(matrix))) < 1e-12:
            raise ValueError("pixel_to_plane 행렬이 역변환 불가능합니다.")
        return matrix

    @classmethod
    def from_document(cls, document: dict) -> "SphericalPlaneCalibration":
        if (not isinstance(document, dict) or document.get("version") != 2
                or document.get("model") != "spherical_plane"):
            raise ValueError("지원하지 않는 구면 평면 보정 파일입니다.")
        image = document.get("image_size", {})
        geometry = document.get("geometry", {})
        servo = document.get("servo", {})
        metrics = document.get("metrics", {})
        width, height = image.get("width"), image.get("height")
        if type(width) is not int or type(height) is not int or min(width, height) < 2:
            raise ValueError("유효한 보정 영상 크기가 필요합니다.")
        axis = geometry.get("axis_projection_mm")
        if not isinstance(axis, list) or len(axis) != 2:
            raise ValueError("axis_projection_mm에는 x, y가 필요합니다.")
        uncertainty = metrics.get("repeat_radius_mm", [0.0, 0.0])
        if not isinstance(uncertainty, list) or len(uncertainty) != 2:
            raise ValueError("repeat_radius_mm에는 평균과 최댓값이 필요합니다.")
        values = [*axis, geometry.get("axis_height_mm"),
                  geometry.get("center_azimuth_deg"),
                  geometry.get("center_elevation_deg"),
                  servo.get("pan_physical_deg_per_command_deg"),
                  servo.get("tilt_physical_deg_per_command_deg"),
                  servo.get("pan_center_command_deg"),
                  servo.get("tilt_center_command_deg"),
                  servo.get("pan_correction_deg", 0.0),
                  servo.get("tilt_correction_deg", 0.0), *uncertainty]
        numbers = tuple(_number(value) for value in values)
        if numbers[2] <= 0 or abs(numbers[5]) < 1e-9 or abs(numbers[6]) < 1e-9:
            raise ValueError("축 높이와 서보 각도 배율이 유효하지 않습니다.")
        return cls(width, height, cls._matrix(document.get("pixel_to_plane")),
                   (numbers[0], numbers[1]), numbers[2], numbers[3], numbers[4],
                   numbers[5], numbers[6], numbers[7], numbers[8], numbers[9],
                   numbers[10], (numbers[11], numbers[12]))

    @classmethod
    def load(cls, path: Path) -> "SphericalPlaneCalibration":
        try:
            return cls.from_document(json.loads(path.read_text(encoding="utf-8")))
        except OSError as error:
            raise ValueError(f"보정 파일을 읽을 수 없습니다: {path}") from error

    def plane_point(self, x: float, y: float, width: int, height: int) -> tuple[float, float]:
        if (width, height) != (self.width, self.height):
            raise ValueError("보정 영상과 현재 해상도가 다릅니다. 같은 해상도로 다시 보정하세요.")
        x, y = _number(x), _number(y)
        if not (0 <= x < width and 0 <= y < height):
            raise ValueError("좌표가 영상 밖에 있습니다.")
        projected = self.pixel_to_plane @ np.array([x, y, 1.0])
        if abs(float(projected[2])) < 1e-12:
            raise ValueError("좌표를 책상 평면으로 투영할 수 없습니다.")
        return float(projected[0] / projected[2]), float(projected[1] / projected[2])

    def map_float(self, x: float, y: float, width: int, height: int) -> tuple[float, float]:
        plane_x, plane_y = self.plane_point(x, y, width, height)
        dx = plane_x - self.axis_projection_mm[0]
        dy = plane_y - self.axis_projection_mm[1]
        azimuth = math.degrees(math.atan2(dy, dx))
        elevation = math.degrees(math.atan2(self.axis_height_mm, math.hypot(dx, dy)))
        # Wrap azimuth around the fitted centre to avoid a discontinuity at +/-180.
        azimuth_delta = (azimuth - self.center_azimuth_deg + 180.0) % 360.0 - 180.0
        pan = (self.pan_center_command + azimuth_delta / self.pan_scale
               + self.pan_correction_deg)
        tilt = (self.tilt_center_command
                + (elevation - self.center_elevation_deg) / self.tilt_scale
                + self.tilt_correction_deg)
        if not math.isfinite(pan) or not math.isfinite(tilt):
            raise ValueError("계산한 서보 각도가 유효하지 않습니다.")
        return pan, tilt

    def map(self, x: float, y: float, width: int, height: int) -> tuple[int, int]:
        pan, tilt = self.map_float(x, y, width, height)
        result = tuple(math.floor(angle + 0.5) for angle in (pan, tilt))
        if any(angle < 0 or angle > 180 for angle in result):
            raise ValueError("계산한 각도가 서보 범위를 벗어났습니다.")
        return result


Calibration = Union[PlaneCalibration, SphericalPlaneCalibration]


def load_calibration(path: Path) -> Calibration:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ValueError(f"보정 파일을 읽을 수 없습니다: {path}") from error
    if document.get("model") == "spherical_plane":
        return SphericalPlaneCalibration.from_document(document)
    return PlaneCalibration.from_document(document)
