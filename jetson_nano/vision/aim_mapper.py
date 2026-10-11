"""Convert full-frame pixel coordinates to calibrated pan/tilt angles."""

from __future__ import annotations

from dataclasses import dataclass
import math
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
        pan_angle = math.floor(pan + 0.5)
        tilt_angle = math.floor(tilt + 0.5)
        if not 0 <= pan_angle <= 180 or not 0 <= tilt_angle <= 180:
            raise ValueError("calibrated angle is outside 0..180")
        return pan_angle, tilt_angle
