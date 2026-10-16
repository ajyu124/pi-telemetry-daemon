#!/usr/bin/env python3
"""
telemetry_viewer.py

PURPOSE
    A small "ground station": it listens for the UDP frames the daemon sends,
    checks each one, and prints the live values. It plays the part of the
    operator's display in a real telemetry system.

HOW TO RUN IT
    Open a second terminal, go to the project folder, and run:

        python3 tools/telemetry_viewer.py

    Then start the daemon in the first terminal. Press Ctrl+C to stop.
    Run with --help for options.

WHAT IT CHECKS ON EVERY FRAME
    1. Size, sync bytes, CRC, version   (done by tools/telemetry_frame.py)
    2. Sequence number                   if a number is skipped, a frame was
                                         lost in transit, and it is counted.
                                         If the count jumps backwards, the
                                         daemon was restarted
    3. Health state changes              printed the moment they happen

    It also notices silence: with --idle-timeout-seconds it reports when
    frames stop arriving, which is how a receiver detects a dead link.

This script uses only Python's standard library. Nothing needs to be installed.
"""

import argparse
import socket
import sys
from typing import Optional

from telemetry_frame import (
    HEALTH_STATE_NAMES,
    FrameDecodeError,
    TelemetryFrame,
    decode_frame,
)

# Sequence numbers are unsigned 32-bit, so they wrap from 4294967295 back to 0.
SEQUENCE_NUMBER_MODULUS = 2**32
HALF_SEQUENCE_NUMBER_RANGE = 2**31

# Larger than any frame, so an oversized packet is seen whole and rejected
# for its size instead of being silently cut short.
RECEIVE_BUFFER_BYTES = 1024


class ViewerStatistics:
    """Running totals shown in the final summary."""

    def __init__(self) -> None:
        self.frames_accepted = 0
        self.frames_rejected = 0
        self.frames_lost = 0
        self.stale_frames = 0
        self.sequence_restarts = 0
        self.health_state_changes = 0


def sequence_gap(previous_sequence: int, current_sequence: int) -> int:
    """
    Returns how far `current_sequence` is ahead of the frame that was
    expected next. 0 means it is exactly the expected frame.

    The modulo arithmetic keeps this correct when the sequence number wraps
    around from its maximum (4294967295) back to 0.
    """
    expected_sequence = (previous_sequence + 1) % SEQUENCE_NUMBER_MODULUS
    return (current_sequence - expected_sequence) % SEQUENCE_NUMBER_MODULUS


def sequence_went_backwards(previous_sequence: Optional[int], current_sequence: int) -> bool:
    """
    Returns True if the sequence number jumped backwards.

    That happens when the daemon is restarted (its count starts again at 0),
    or when the network delivers two frames out of order. It must not be
    counted as loss: going from 500 back to 0 is not "4 billion lost frames".

    A backwards jump shows up as a gap of more than half the number range,
    which is the standard way to compare sequence numbers that wrap around.
    """
    if previous_sequence is None:
        return False  # first frame seen, nothing to compare against
    return sequence_gap(previous_sequence, current_sequence) >= HALF_SEQUENCE_NUMBER_RANGE


def count_lost_frames(previous_sequence: Optional[int], current_sequence: int) -> int:
    """
    Returns how many frames went missing between two received frames.

    If frame 7 arrives right after frame 4, frames 5 and 6 were lost, so the
    answer is 2. A backwards jump counts as 0 lost (see
    sequence_went_backwards).
    """
    if previous_sequence is None:
        return 0  # first frame seen, nothing to compare against
    if sequence_went_backwards(previous_sequence, current_sequence):
        return 0
    return sequence_gap(previous_sequence, current_sequence)


def format_frame_line(frame: TelemetryFrame) -> str:
    """Builds the one-line text shown for a frame."""
    fresh_text = "yes" if frame.reading_is_fresh else "no"
    return (
        f"seq={frame.sequence_number:<8d} "
        f"state={frame.health_state_name:<8s} "
        f"fresh={fresh_text:<3s} "
        f"temp={frame.temperature_celsius:6.2f} C  "
        f"pressure={frame.pressure_pascals:6d} Pa  "
        f"humidity={frame.humidity_percent:5.2f} %"
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Listen for telemetry frames over UDP and display them."
    )
    parser.add_argument(
        "--listen-ip",
        default="127.0.0.1",
        help="IP address to listen on. Use 0.0.0.0 to accept frames from "
        "other machines such as a Raspberry Pi (default: 127.0.0.1)",
    )
    parser.add_argument(
        "--port", type=int, default=5005, help="UDP port to listen on (default: 5005)"
    )
    parser.add_argument(
        "--print-every",
        type=int,
        default=50,
        help="Print one line for every N frames. At 50 Hz, 50 means once per "
        "second. Use 1 to print every frame (default: 50)",
    )
    parser.add_argument(
        "--frame-limit",
        type=int,
        default=0,
        help="Stop after receiving this many frames. 0 means run until Ctrl+C (default: 0)",
    )
    parser.add_argument(
        "--idle-timeout-seconds",
        type=float,
        default=0.0,
        help="Once frames have started arriving, stop if none arrive for this "
        "many seconds. 0 means never stop on silence (default: 0)",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.print_every < 1:
        print("Error: --print-every must be at least 1", file=sys.stderr)
        return 1

    # AF_INET = IPv4. SOCK_DGRAM = datagram socket, which means UDP.
    receive_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # "bind" claims the port so the operating system delivers packets
        # addressed to it to this program.
        receive_socket.bind((arguments.listen_ip, arguments.port))
    except OSError as error:
        print(
            f"Error: could not listen on {arguments.listen_ip}:{arguments.port} ({error}).\n"
            "Another viewer may already be running. Close it, or pick another "
            "port with --port (and give the daemon the same --port).",
            file=sys.stderr,
        )
        return 1

    if arguments.idle_timeout_seconds > 0:
        # Makes recvfrom() give up and raise socket.timeout after this long.
        receive_socket.settimeout(arguments.idle_timeout_seconds)

    print(f"Listening for telemetry on {arguments.listen_ip}:{arguments.port} (UDP).")
    print("Start the daemon in another terminal. Press Ctrl+C here to stop.\n")

    statistics = ViewerStatistics()
    previous_sequence: Optional[int] = None
    previous_health_state: Optional[int] = None

    try:
        while True:
            # Waits here until one datagram arrives (or the idle timeout passes).
            try:
                data, _sender_address = receive_socket.recvfrom(RECEIVE_BUFFER_BYTES)
            except socket.timeout:
                if statistics.frames_accepted == 0:
                    continue  # the daemon has not started yet, keep waiting
                print(
                    f"No frames for {arguments.idle_timeout_seconds:g} seconds: "
                    "the daemon stopped or the link was lost."
                )
                break

            try:
                frame = decode_frame(data)
            except FrameDecodeError as error:
                statistics.frames_rejected += 1
                print(f"REJECTED frame: {error}")
                continue

            statistics.frames_accepted += 1
            if not frame.reading_is_fresh:
                statistics.stale_frames += 1

            if sequence_went_backwards(previous_sequence, frame.sequence_number):
                statistics.sequence_restarts += 1
                print(
                    f"SEQUENCE RESTARTED at seq={frame.sequence_number} "
                    "(the daemon was restarted, or a frame arrived out of order)"
                )

            lost = count_lost_frames(previous_sequence, frame.sequence_number)
            if lost > 0:
                statistics.frames_lost += lost
                print(f"LOST {lost} frame(s) before seq={frame.sequence_number}")
            previous_sequence = frame.sequence_number

            state_changed = (
                previous_health_state is not None and frame.health_state != previous_health_state
            )
            if state_changed:
                statistics.health_state_changes += 1
                old_name = HEALTH_STATE_NAMES[previous_health_state]
                print(f"*** HEALTH STATE CHANGED: {old_name} -> {frame.health_state_name} ***")
            previous_health_state = frame.health_state

            is_scheduled_print = statistics.frames_accepted % arguments.print_every == 0
            if is_scheduled_print or state_changed:
                # flush=True makes the line appear immediately.
                print(format_frame_line(frame), flush=True)

            if arguments.frame_limit and statistics.frames_accepted >= arguments.frame_limit:
                break
    except KeyboardInterrupt:
        # Ctrl+C raises KeyboardInterrupt. Catch it so we can print a summary
        # instead of an error trace.
        pass
    finally:
        receive_socket.close()

    print("\nViewer stopped")
    print(f"  frames accepted:      {statistics.frames_accepted}")
    print(f"  frames rejected:      {statistics.frames_rejected}")
    print(f"  frames lost:          {statistics.frames_lost}")
    print(f"  stale frames:         {statistics.stale_frames}")
    print(f"  sequence restarts:    {statistics.sequence_restarts}")
    print(f"  health state changes: {statistics.health_state_changes}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
