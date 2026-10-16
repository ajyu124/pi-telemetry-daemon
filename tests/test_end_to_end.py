#!/usr/bin/env python3
"""
test_end_to_end.py

PURPOSE
    An end-to-end test: it launches the REAL compiled daemon as a separate
    program, listens for the UDP frames it sends, and checks them from the
    outside, the same way a real ground station would see them.

    The C++ unit tests check each part in isolation. This test checks that
    the parts work together as a running program: command line, sensor,
    timing loop, health state machine, encoder, UDP socket and database.

WHAT IT VERIFIES
    1. The daemon exits cleanly (exit code 0).
    2. Every frame arrives, in order, with a valid CRC (checked by the
       independent Python decoder in tools/telemetry_frame.py).
    3. A simulated sensor fault produces exactly the expected sequence of
       health states: NOMINAL, DEGRADED, FAULT, then back to NOMINAL only
       after 10 good reads.
    4. The SQLite database contains one row for every frame.
    5. Bad command line options are rejected with exit code 1.
    6. The viewer's frame-loss arithmetic, including the daemon being
       restarted and the sequence number wrapping around.

HOW TO RUN IT
    It runs automatically as part of:   bash scripts/build_and_test.sh
    To run it alone:                    python3 tests/test_end_to_end.py build/telemetry_daemon

This script uses only Python's standard library (unittest, socket, sqlite3,
subprocess). Nothing needs to be installed.
"""

import os
import socket
import sqlite3
import subprocess
import sys
import tempfile
import unittest

# Make tools/telemetry_frame.py importable from this tests/ folder.
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "tools"))

from telemetry_frame import FRAME_SIZE_BYTES, decode_frame  # noqa: E402
from telemetry_viewer import count_lost_frames, sequence_went_backwards  # noqa: E402

# Path to the compiled daemon. Filled in from the command line at the bottom.
DAEMON_PATH = ""

# ---- The scenario this test runs ---------------------------------------------
#
#   Frames   0 -  19   sensor healthy            (20 frames)
#   Frames  20 -  29   simulated sensor fault    (10 frames)
#   Frames  30 -  59   sensor healthy again      (30 frames)
#
# Run at 200 Hz so the whole test takes about a third of a second.
RATE_HZ = 200
TOTAL_FRAMES = 60
FAULT_START_FRAME = 20
FAULT_LENGTH_FRAMES = 10

# From the rules in src/health_monitor.hpp:
#   1 failure -> DEGRADED, 5 failures in a row -> FAULT,
#   10 successes in a row -> NOMINAL again.
FAILURES_UNTIL_FAULT = 5
SUCCESSES_UNTIL_RECOVERED = 10

SECONDS_TO_WAIT_FOR_A_FRAME = 5.0
SECONDS_TO_WAIT_FOR_DAEMON_EXIT = 10.0


def expected_health_state_name(frame_number: int) -> str:
    """Works out what health state each frame of the scenario should carry."""
    fault_end_frame = FAULT_START_FRAME + FAULT_LENGTH_FRAMES

    if frame_number < FAULT_START_FRAME:
        return "NOMINAL"

    if frame_number < fault_end_frame:
        failures_so_far = frame_number - FAULT_START_FRAME + 1
        return "FAULT" if failures_so_far >= FAILURES_UNTIL_FAULT else "DEGRADED"

    successes_since_fault = frame_number - fault_end_frame + 1
    return "NOMINAL" if successes_since_fault >= SUCCESSES_UNTIL_RECOVERED else "FAULT"


class EndToEndTest(unittest.TestCase):
    def setUp(self) -> None:
        """Runs before each test: open a listening socket and a temp folder."""
        self.receive_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # Port 0 asks the operating system for any free port, so this test
        # can never clash with a viewer you left running on port 5005.
        self.receive_socket.bind(("127.0.0.1", 0))
        self.receive_socket.settimeout(SECONDS_TO_WAIT_FOR_A_FRAME)
        self.port = self.receive_socket.getsockname()[1]

        self.temporary_directory = tempfile.TemporaryDirectory()
        self.database_path = os.path.join(self.temporary_directory.name, "telemetry.db")

    def tearDown(self) -> None:
        """Runs after each test: clean up."""
        self.receive_socket.close()
        self.temporary_directory.cleanup()

    def start_daemon(self, extra_arguments):
        """Launches the daemon as a separate process and returns its handle."""
        command = [
            DAEMON_PATH,
            "--port", str(self.port),
            "--database", self.database_path,
            "--quiet",
        ] + extra_arguments
        return subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

    def receive_frames(self, frame_count):
        """Receives and decodes `frame_count` frames. Fails the test on timeout."""
        frames = []
        for frame_number in range(frame_count):
            try:
                data, _sender = self.receive_socket.recvfrom(1024)
            except socket.timeout:
                self.fail(f"timed out waiting for frame {frame_number} of {frame_count}")
            self.assertEqual(len(data), FRAME_SIZE_BYTES)
            # decode_frame raises an exception if the CRC or layout is wrong,
            # which fails the test.
            frames.append(decode_frame(data))
        return frames

    def test_fault_scenario_from_the_outside(self) -> None:
        daemon = self.start_daemon([
            "--rate-hz", str(RATE_HZ),
            "--frame-limit", str(TOTAL_FRAMES),
            "--fault-start-frame", str(FAULT_START_FRAME),
            "--fault-length-frames", str(FAULT_LENGTH_FRAMES),
        ])

        frames = self.receive_frames(TOTAL_FRAMES)
        standard_output, standard_error = daemon.communicate(
            timeout=SECONDS_TO_WAIT_FOR_DAEMON_EXIT
        )

        # 1. Clean exit.
        self.assertEqual(daemon.returncode, 0, msg=standard_error)
        self.assertIn("frames built:         60", standard_output)

        # 2. Every frame, in order.
        sequence_numbers = [frame.sequence_number for frame in frames]
        self.assertEqual(sequence_numbers, list(range(TOTAL_FRAMES)))

        # 3. Health states and freshness, frame by frame.
        fault_end_frame = FAULT_START_FRAME + FAULT_LENGTH_FRAMES
        for frame_number, frame in enumerate(frames):
            inside_fault = FAULT_START_FRAME <= frame_number < fault_end_frame
            self.assertEqual(
                frame.health_state_name,
                expected_health_state_name(frame_number),
                msg=f"health state of frame {frame_number}",
            )
            self.assertEqual(
                frame.reading_is_fresh, not inside_fault, msg=f"fresh flag of frame {frame_number}"
            )

        # During the fault, frames repeat the last good reading.
        last_good_frame = frames[FAULT_START_FRAME - 1]
        for frame in frames[FAULT_START_FRAME:fault_end_frame]:
            self.assertEqual(
                frame.temperature_centi_celsius, last_good_frame.temperature_centi_celsius
            )

        # The failure counter climbs 1, 2, 3 ... during the fault, then resets.
        failure_counts = [f.consecutive_read_failures for f in frames[FAULT_START_FRAME:fault_end_frame]]
        self.assertEqual(failure_counts, list(range(1, FAULT_LENGTH_FRAMES + 1)))
        self.assertEqual(frames[fault_end_frame].consecutive_read_failures, 0)

        # Values look like a room, and timestamps never go backwards.
        for frame in frames:
            self.assertTrue(15.0 < frame.temperature_celsius < 30.0)
            self.assertTrue(95000 < frame.pressure_pascals < 105000)
            self.assertTrue(30.0 < frame.humidity_percent < 60.0)
        timestamps = [frame.timestamp_milliseconds for frame in frames]
        self.assertEqual(timestamps, sorted(timestamps))

        # 4. The database has every frame, including the stale ones.
        connection = sqlite3.connect(self.database_path)
        try:
            (row_count,) = connection.execute("SELECT COUNT(*) FROM telemetry_frames").fetchone()
            (stale_count,) = connection.execute(
                "SELECT COUNT(*) FROM telemetry_frames WHERE reading_is_fresh = 0"
            ).fetchone()
            (fault_count,) = connection.execute(
                "SELECT COUNT(*) FROM telemetry_frames WHERE health_state = 2"
            ).fetchone()
        finally:
            connection.close()

        self.assertEqual(row_count, TOTAL_FRAMES)
        self.assertEqual(stale_count, FAULT_LENGTH_FRAMES)
        expected_fault_frames = sum(
            1 for n in range(TOTAL_FRAMES) if expected_health_state_name(n) == "FAULT"
        )
        self.assertEqual(fault_count, expected_fault_frames)

    def test_frame_rate_is_close_to_requested(self) -> None:
        """100 frames at 100 Hz should take about one second on the wire."""
        daemon = self.start_daemon(["--rate-hz", "100", "--frame-limit", "100"])

        frames = self.receive_frames(100)
        daemon.communicate(timeout=SECONDS_TO_WAIT_FOR_DAEMON_EXIT)

        # 99 gaps of 10 ms each is 990 ms. The limits are wide on purpose so
        # a busy computer does not fail the test.
        elapsed_milliseconds = frames[-1].timestamp_milliseconds - frames[0].timestamp_milliseconds
        self.assertGreaterEqual(elapsed_milliseconds, 900)
        self.assertLessEqual(elapsed_milliseconds, 1500)

    def test_bad_option_is_rejected(self) -> None:
        daemon = self.start_daemon(["--rate-hz", "not-a-number"])
        _standard_output, standard_error = daemon.communicate(
            timeout=SECONDS_TO_WAIT_FOR_DAEMON_EXIT
        )

        self.assertEqual(daemon.returncode, 1)
        self.assertIn("Error:", standard_error)

    def test_help_exits_successfully(self) -> None:
        daemon = self.start_daemon(["--help"])
        standard_output, _standard_error = daemon.communicate(
            timeout=SECONDS_TO_WAIT_FOR_DAEMON_EXIT
        )

        self.assertEqual(daemon.returncode, 0)
        self.assertIn("--rate-hz", standard_output)


class ViewerSequenceLogicTest(unittest.TestCase):
    """
    Tests for the viewer's frame-loss arithmetic in tools/telemetry_viewer.py.
    These need no daemon and no network. They are plain function calls.
    """

    MAXIMUM_SEQUENCE = 2**32 - 1

    def test_no_loss_when_frames_are_consecutive(self) -> None:
        self.assertEqual(count_lost_frames(None, 0), 0)  # very first frame
        self.assertEqual(count_lost_frames(4, 5), 0)

    def test_counts_skipped_frames(self) -> None:
        self.assertEqual(count_lost_frames(4, 7), 2)  # frames 5 and 6 are missing
        self.assertEqual(count_lost_frames(0, 100), 99)

    def test_wrap_around_is_not_loss(self) -> None:
        # The counter rolling over from its maximum to 0 is the normal next frame.
        self.assertEqual(count_lost_frames(self.MAXIMUM_SEQUENCE, 0), 0)
        self.assertFalse(sequence_went_backwards(self.MAXIMUM_SEQUENCE, 0))
        # Losing one frame across the roll-over is still counted correctly.
        self.assertEqual(count_lost_frames(self.MAXIMUM_SEQUENCE, 1), 1)

    def test_daemon_restart_is_not_counted_as_loss(self) -> None:
        # The daemon was at frame 500, was restarted, and is now at frame 0.
        self.assertTrue(sequence_went_backwards(500, 0))
        self.assertEqual(count_lost_frames(500, 0), 0)

    def test_forward_jump_is_not_a_restart(self) -> None:
        self.assertFalse(sequence_went_backwards(None, 0))
        self.assertFalse(sequence_went_backwards(4, 5))
        self.assertFalse(sequence_went_backwards(4, 7))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 tests/test_end_to_end.py PATH_TO_TELEMETRY_DAEMON")
        print("Example: python3 tests/test_end_to_end.py build/telemetry_daemon")
        sys.exit(2)

    DAEMON_PATH = os.path.abspath(sys.argv[1])
    if not os.path.isfile(DAEMON_PATH):
        print(f"Error: {DAEMON_PATH} does not exist. Build the project first with:")
        print("  bash scripts/build_and_test.sh")
        sys.exit(2)

    # Hand the remaining arguments to unittest (it must not see the path).
    unittest.main(argv=[sys.argv[0]] + sys.argv[2:], verbosity=2)
