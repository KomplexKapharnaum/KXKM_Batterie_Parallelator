"""Serial device wrapper for HIL (Hardware-In-the-Loop) testing."""

from __future__ import annotations

import re
import time
from typing import Optional

import serial


class HilDevice:
    """Wraps a pyserial connection to the BMU device under test."""

    def __init__(self, port: str = "/dev/cu.usbmodem1101", baud: int = 115_200) -> None:
        self.port = port
        self.baud = baud
        self._ser: Optional[serial.Serial] = None
        self.log_lines: list[str] = []

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def open(self) -> None:
        """Open the serial port (non-blocking reads via timeout)."""
        self._ser = serial.Serial(
            port=self.port,
            baudrate=self.baud,
            timeout=1,
            dsrdtr=False,
            rtscts=False,
        )
        self.log_lines.clear()

    def close(self) -> None:
        """Close the serial port if open."""
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    # ------------------------------------------------------------------
    # Reading
    # ------------------------------------------------------------------

    def _readline(self) -> Optional[str]:
        """Read one line (stripped), return None on timeout / empty."""
        if self._ser is None:
            return None
        raw = self._ser.readline()
        if not raw:
            return None
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            self.log_lines.append(line)
        return line if line else None

    def capture_for(self, seconds: float) -> list[str]:
        """Capture serial output for *seconds* and return collected lines."""
        captured: list[str] = []
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            line = self._readline()
            if line is not None:
                captured.append(line)
        return captured

    def expect(self, pattern: str, timeout: float = 10.0) -> bool:
        """Read lines until *pattern* (regex) is found or *timeout* expires.

        Returns True if the pattern was matched, False otherwise.
        """
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self._readline()
            if line is not None and regex.search(line):
                return True
        return False

    # ------------------------------------------------------------------
    # Control
    # ------------------------------------------------------------------

    def reboot(self) -> None:
        """Reboot the device by pulsing DTR (triggers ESP32 reset)."""
        if self._ser is None:
            raise RuntimeError("Serial port not open — call open() first")
        self._ser.dtr = True
        time.sleep(0.1)
        self._ser.dtr = False
        time.sleep(0.5)
        self.log_lines.clear()
