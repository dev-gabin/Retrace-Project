"""Convert full-frame pixel coordinates to calibrated pan/tilt angles."""

from __future__ import annotations

from dataclasses import dataclass
import os


@dataclass(frozen=True)
class AimMapper:
    frame_width: int
    frame_height: int
    pan_left: float
    pan_right: float
    tilt_top: float
    tilt_bottom: float

    @classmethod
    def from_env(cls, frame_width: int, frame_height: int) -> "AimMapper":
        names = (
            "RETRACE_PAN_LEFT",
            "RETRACE_PAN_RIGHT",
            "RETRACE_TILT_TOP",
            "RETRACE_TILT_BOTTOM",
        )
        missing = [name for name in names if name not in os.environ]
        if missing:
            raise ValueError(f"Missing laser calibration: {', '.join(missing)}")
        return cls(
            frame_width=frame_width,
            frame_height=frame_height,
            pan_left=float(os.environ["RETRACE_PAN_LEFT"]),
            pan_right=float(os.environ["RETRACE_PAN_RIGHT"]),
            tilt_top=float(os.environ["RETRACE_TILT_TOP"]),
            tilt_bottom=float(os.environ["RETRACE_TILT_BOTTOM"]),
        )

    def map(self, x: int, y: int) -> tuple[int, int]:
        if self.frame_width < 2 or self.frame_height < 2:
            raise ValueError("frame dimensions must be at least 2x2")
        if not 0 <= x < self.frame_width or not 0 <= y < self.frame_height:
            raise ValueError("pixel coordinates are outside the frame")

        x_ratio = x / (self.frame_width - 1)
        y_ratio = y / (self.frame_height - 1)
        pan = self.pan_left + x_ratio * (self.pan_right - self.pan_left)
        tilt = self.tilt_top + y_ratio * (self.tilt_bottom - self.tilt_top)
        pan_angle = round(pan)
        tilt_angle = round(tilt)
        if not 0 <= pan_angle <= 180 or not 0 <= tilt_angle <= 180:
            raise ValueError("calibrated angle is outside 0..180")
        return pan_angle, tilt_angle
