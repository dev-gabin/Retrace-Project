"""Convert full-frame pixel coordinates to calibrated pan/tilt angles."""

from __future__ import annotations

from dataclasses import dataclass
import math
import os

from vision.plane_calibration import Calibration, calibration_path, load_calibration


AFFINE_NAMES = (
    "RETRACE_PAN_X", "RETRACE_PAN_Y", "RETRACE_PAN_OFFSET",
    "RETRACE_TILT_X", "RETRACE_TILT_Y", "RETRACE_TILT_OFFSET",
)


def fit_affine(samples: list[tuple[float, float, float]]) -> tuple[float, float, float]:
    """Fit angle = a*x + b*y + c using normalized image coordinates."""
    if len(samples) < 3 or not all(math.isfinite(v) for row in samples for v in row):
        raise ValueError("at least three finite calibration samples are required")
    n = len(samples)
    mx, my, ma = (sum(row[i] for row in samples) / n for i in range(3))
    xx = sum((x - mx) ** 2 for x, y, angle in samples)
    yy = sum((y - my) ** 2 for x, y, angle in samples)
    xy = sum((x - mx) * (y - my) for x, y, angle in samples)
    xa = sum((x - mx) * (angle - ma) for x, y, angle in samples)
    ya = sum((y - my) * (angle - ma) for x, y, angle in samples)
    determinant = xx * yy - xy * xy
    if xx <= 0 or yy <= 0 or determinant <= 1e-6 * xx * yy:
        raise ValueError("calibration points are collinear or too close to collinear")
    a = (xa * yy - ya * xy) / determinant
    b = (ya * xx - xa * xy) / determinant
    return a, b, ma - a * mx - b * my


@dataclass(frozen=True)
class AimMapper:
    frame_width: int
    frame_height: int
    pan_left: float
    pan_right: float
    tilt_top: float
    tilt_bottom: float
    affine: tuple[float, float, float, float, float, float] | None = None
    plane: Calibration | None = None

    @classmethod
    def from_env(cls, frame_width: int, frame_height: int) -> "AimMapper":
        path = calibration_path()
        if "RETRACE_AIM_CALIBRATION" in os.environ or path.exists():
            plane = load_calibration(path)
            if (plane.width, plane.height) != (frame_width, frame_height):
                raise ValueError("보정 파일과 카메라 해상도가 다릅니다.")
            return cls(frame_width, frame_height, 0, 0, 0, 0, plane=plane)
        present = [name for name in AFFINE_NAMES if name in os.environ]
        if present:
            missing = [name for name in AFFINE_NAMES if name not in os.environ]
            if missing:
                raise ValueError(f"Missing laser calibration: {', '.join(missing)}")
            coefficients = tuple(float(os.environ[name]) for name in AFFINE_NAMES)
            if not all(math.isfinite(value) for value in coefficients):
                raise ValueError("laser calibration coefficients must be finite")
            return cls(frame_width, frame_height, 0, 0, 0, 0, coefficients)
        # Manually calibrated on the installed camera/laser assembly.
        # Environment variables remain available for future fine adjustment.
        defaults = {
            "RETRACE_PAN_LEFT": "98",
            "RETRACE_PAN_RIGHT": "71",
            "RETRACE_TILT_TOP": "54",
            "RETRACE_TILT_BOTTOM": "30",
        }
        return cls(
            frame_width=frame_width,
            frame_height=frame_height,
            pan_left=float(os.environ.get("RETRACE_PAN_LEFT", defaults["RETRACE_PAN_LEFT"])),
            pan_right=float(os.environ.get("RETRACE_PAN_RIGHT", defaults["RETRACE_PAN_RIGHT"])),
            tilt_top=float(os.environ.get("RETRACE_TILT_TOP", defaults["RETRACE_TILT_TOP"])),
            tilt_bottom=float(os.environ.get("RETRACE_TILT_BOTTOM", defaults["RETRACE_TILT_BOTTOM"])),
        )

    def map(self, x: int, y: int) -> tuple[int, int]:
        if self.plane is not None:
            return self.plane.map(x, y, self.frame_width, self.frame_height)
        if self.frame_width < 2 or self.frame_height < 2:
            raise ValueError("frame dimensions must be at least 2x2")
        if not 0 <= x < self.frame_width or not 0 <= y < self.frame_height:
            raise ValueError("pixel coordinates are outside the frame")

        # Pixel centers use 0..width-1, while the optical center of an even
        # sized frame is width/2. Using width here keeps (320,240) at exactly
        # 0.5 for a 640x480 camera.
        x_ratio = x / self.frame_width
        y_ratio = y / self.frame_height
        pan = self.pan_left + x_ratio * (self.pan_right - self.pan_left)
        tilt = self.tilt_top + y_ratio * (self.tilt_bottom - self.tilt_top)
        if self.affine is not None:
            a, b, c, d, e, f = self.affine
            pan = a * x_ratio + b * y_ratio + c
            tilt = d * x_ratio + e * y_ratio + f
        pan_angle = math.floor(pan + 0.5)
        tilt_angle = math.floor(tilt + 0.5)
        if not 0 <= pan_angle <= 180 or not 0 <= tilt_angle <= 180:
            raise ValueError("calibrated angle is outside 0..180")
        return pan_angle, tilt_angle
