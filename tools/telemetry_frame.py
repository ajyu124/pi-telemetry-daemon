"""
telemetry_frame.py

PURPOSE
    The Python side of the telemetry protocol: turns the 34 bytes the C++
    daemon sends back into named values, and rejects anything malformed.

    This is the "twin" of src/telemetry_frame.cpp. The frame layout table
    lives in src/telemetry_frame.hpp. If the layout changes there, it must
    change here too.

WHY A SECOND IMPLEMENTATION IS VALUABLE
    The CRC and the decoder here were written separately from the C++ ones.
    When C++ encodes a frame and this Python code decodes it and the CRC
    matches, two independent implementations have agreed on every byte. That
    is much stronger evidence than C++ decoding its own output.

USED BY
    tools/telemetry_viewer.py   the live viewer
    tests/test_end_to_end.py    the automated end-to-end test

This file uses only Python's standard library. Nothing needs to be installed.
"""

import struct
from dataclasses import dataclass

# ---- Protocol constants (must match src/telemetry_frame.hpp) -----------------

SYNC_BYTES = b"\xAA\x55"
PROTOCOL_VERSION = 1
FRAME_SIZE_BYTES = 34
CRC_COVERED_BYTES = 32

FLAG_READING_IS_FRESH = 0x01

HEALTH_STATE_NAMES = {0: "NOMINAL", 1: "DEGRADED", 2: "FAULT"}

# The `struct` module converts between bytes and Python values using a format
# string. Each character describes one field:
#
#   <    little-endian, no padding
#   2s   2 raw bytes              sync bytes
#   B    unsigned 8-bit           protocol version
#   B    unsigned 8-bit           health state
#   B    unsigned 8-bit           flags
#   B    unsigned 8-bit           reserved
#   H    unsigned 16-bit          consecutive read failures
#   I    unsigned 32-bit          sequence number
#   Q    unsigned 64-bit          timestamp (milliseconds)
#   i    SIGNED 32-bit            temperature (0.01 C)
#   I    unsigned 32-bit          pressure (Pa)
#   I    unsigned 32-bit          humidity (0.01 %)
#   H    unsigned 16-bit          CRC-16
FRAME_FORMAT = "<2sBBBBHIQiIIH"

# Guard against the format string and the documented size drifting apart.
assert struct.calcsize(FRAME_FORMAT) == FRAME_SIZE_BYTES


class FrameDecodeError(Exception):
    """Raised when a block of bytes is not a valid telemetry frame."""


@dataclass
class TelemetryFrame:
    """One decoded telemetry frame. Field names match the C++ struct."""

    health_state: int
    reading_is_fresh: bool
    consecutive_read_failures: int
    sequence_number: int
    timestamp_milliseconds: int
    temperature_centi_celsius: int
    pressure_pascals: int
    humidity_centi_percent: int

    @property
    def health_state_name(self) -> str:
        return HEALTH_STATE_NAMES[self.health_state]

    @property
    def temperature_celsius(self) -> float:
        return self.temperature_centi_celsius / 100.0

    @property
    def humidity_percent(self) -> float:
        return self.humidity_centi_percent / 100.0


def compute_crc16(data: bytes) -> int:
    """
    CRC-16/CCITT-FALSE: polynomial 0x1021, starting value 0xFFFF.

    Same algorithm as src/crc16.cpp. The standard self-test is that
    compute_crc16(b"123456789") returns 0x29B1.
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def decode_frame(data: bytes) -> TelemetryFrame:
    """
    Validates `data` and returns a TelemetryFrame.

    Raises FrameDecodeError with a specific reason if the bytes are not a
    valid frame. The checks run in the same order as the C++ decoder:
    size, sync bytes, CRC, version, health state.
    """
    if len(data) != FRAME_SIZE_BYTES:
        raise FrameDecodeError(f"wrong size: {len(data)} bytes, expected {FRAME_SIZE_BYTES}")

    (
        sync,
        version,
        health_state,
        flags,
        _reserved,
        consecutive_read_failures,
        sequence_number,
        timestamp_milliseconds,
        temperature_centi_celsius,
        pressure_pascals,
        humidity_centi_percent,
        received_crc,
    ) = struct.unpack(FRAME_FORMAT, data)

    if sync != SYNC_BYTES:
        raise FrameDecodeError("bad sync bytes")

    expected_crc = compute_crc16(data[:CRC_COVERED_BYTES])
    if received_crc != expected_crc:
        raise FrameDecodeError(
            f"CRC mismatch: frame says 0x{received_crc:04X}, calculated 0x{expected_crc:04X}"
        )

    if version != PROTOCOL_VERSION:
        raise FrameDecodeError(f"unsupported protocol version {version}")

    if health_state not in HEALTH_STATE_NAMES:
        raise FrameDecodeError(f"invalid health state {health_state}")

    return TelemetryFrame(
        health_state=health_state,
        reading_is_fresh=bool(flags & FLAG_READING_IS_FRESH),
        consecutive_read_failures=consecutive_read_failures,
        sequence_number=sequence_number,
        timestamp_milliseconds=timestamp_milliseconds,
        temperature_centi_celsius=temperature_centi_celsius,
        pressure_pascals=pressure_pascals,
        humidity_centi_percent=humidity_centi_percent,
    )
