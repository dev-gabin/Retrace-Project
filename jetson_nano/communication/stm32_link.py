"""USB serial link to the STM32 main unit."""

from __future__ import annotations

from glob import glob
import os
from queue import Empty, Queue
import threading
import time

import serial


class Stm32LinkError(RuntimeError):
    """Raised when the STM32 link or command exchange fails."""


def find_stm32_port() -> str:
    configured = os.environ.get("RETRACE_STM32_PORT")
    if configured:
        return configured

    by_id = sorted(glob("/dev/serial/by-id/*STMicroelectronics*"))
    if by_id:
        return by_id[0]
    candidates = sorted(glob("/dev/ttyACM*"))
    if candidates:
        return candidates[0]
    raise Stm32LinkError("STM32 serial port not found (/dev/ttyACM*)")


class Stm32Link:
    """Send one command at a time while collecting asynchronous EVT lines."""

    def __init__(
        self,
        port: str | None = None,
        baudrate: int = 115200,
        response_timeout: float = 3.0,
        home_pan: int | None = None,
        home_tilt: int | None = None,
    ) -> None:
        self.port = port or find_stm32_port()
        self.response_timeout = response_timeout
        # Keep legacy constructor arguments for old tools. PIR reception no
        # longer depends on laser calibration or home-angle environment values.
        try:
            self._serial = serial.Serial(
                self.port,
                baudrate=baudrate,
                timeout=0.2,
                write_timeout=response_timeout,
            )
        except serial.SerialException as error:
            raise Stm32LinkError(f"Cannot open STM32 port {self.port}: {error}") from error

        self._responses: Queue[str] = Queue()
        self._events: Queue[str] = Queue()
        self._command_lock = threading.Lock()
        self._stop = threading.Event()
        self._reader_error: Exception | None = None
        self._reader = threading.Thread(
            target=self._read_loop, name="stm32-serial-reader", daemon=True
        )
        self._reader.start()

    def __enter__(self) -> "Stm32Link":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()

    def close(self) -> None:
        self._stop.set()
        if self._serial.is_open:
            self._serial.close()
        self._reader.join(timeout=1.0)

    def ping(self) -> None:
        self._command("PING", "OK@PING")

    def aim(self, pan: int, tilt: int) -> None:
        raise Stm32LinkError("LASER_DISABLED: use the last-seen photo and time")

    def laser(self, enabled: bool) -> None:
        if enabled:
            raise Stm32LinkError("LASER_DISABLED: use the last-seen photo and time")
        self._command("LASER:OFF", "OK@LASER")

    def home(self) -> None:
        self.laser(False)

    def point(
        self,
        pan: int,
        tilt: int,
        *,
        settle_seconds: float = 0.5,
        on_seconds: float = 1.0,
    ) -> None:
        raise Stm32LinkError("LASER_DISABLED: use the last-seen photo and time")

    def get_event(self, timeout: float | None = None) -> str | None:
        try:
            return self._events.get(timeout=timeout)
        except Empty:
            return None

    def _command(self, command: str, expected: str) -> None:
        with self._command_lock:
            self._raise_reader_error()
            self._discard_stale_responses()
            try:
                self._serial.write(f"{command}\n".encode("ascii"))
                self._serial.flush()
            except serial.SerialException as error:
                raise Stm32LinkError(f"STM32 write failed: {error}") from error

            deadline = time.monotonic() + self.response_timeout
            while True:
                self._raise_reader_error()
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise Stm32LinkError(f"STM32 response timeout: {command}")
                try:
                    response = self._responses.get(timeout=min(remaining, 0.2))
                except Empty:
                    continue
                if response == expected:
                    return
                if response.startswith("ERR@"):
                    raise Stm32LinkError(response)
                raise Stm32LinkError(f"Unexpected STM32 response: {response}")

    def _read_loop(self) -> None:
        try:
            while not self._stop.is_set():
                raw = self._serial.readline()
                if not raw:
                    continue
                try:
                    line = raw.decode("ascii").strip("\r\n")
                except UnicodeDecodeError:
                    continue
                if not line:
                    continue
                if line.startswith("EVT@"):
                    self._events.put(line)
                elif line.startswith(("OK@", "ERR@")):
                    self._responses.put(line)    
                               
        except (OSError, serial.SerialException) as error:
            if not self._stop.is_set():
                self._reader_error = error

    def _discard_stale_responses(self) -> None:
        while True:
            try:
                self._responses.get_nowait()
            except Empty:
                return

    def _raise_reader_error(self) -> None:
        if self._reader_error is not None:
            raise Stm32LinkError(f"STM32 read failed: {self._reader_error}")
