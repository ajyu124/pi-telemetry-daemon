#!/usr/bin/env bash
# =============================================================================
# run_demo.sh
#
# PURPOSE
#   Shows the whole system working with one command. It starts the viewer
#   (the "ground station") and the daemon (the "device") together, and
#   deliberately breaks the simulated sensor for one second in the middle so
#   you can watch the fault handling.
#
# HOW TO RUN
#   bash scripts/run_demo.sh
#
# WHAT YOU WILL SEE (10 seconds at 50 frames per second = 500 frames)
#   seconds 0 to 3    state=NOMINAL, values drifting slowly
#   second  3         the sensor "fails": NOMINAL -> DEGRADED -> FAULT,
#                     frames keep arriving but are marked fresh=no
#   second  4         the sensor "recovers": FAULT -> NOMINAL after 10 good reads
#   seconds 4 to 10   state=NOMINAL again
#   then a summary from the daemon and from the viewer
#
# Lines come from the viewer, which is what an operator would see. To watch
# the two programs separately, see "Run it by hand" in README.md.
# =============================================================================

set -euo pipefail

cd "$(dirname "$0")/.."

daemon_path="build/telemetry_daemon"
demo_port=5005

if [ ! -x "$daemon_path" ]; then
    echo "Error: $daemon_path not found. Build first with:  bash scripts/build_and_test.sh"
    exit 1
fi

# Start the viewer in the background (the trailing & does that) so this
# script can go on to start the daemon. It exits by itself two seconds after
# the frames stop.
python3 tools/telemetry_viewer.py --port "$demo_port" --idle-timeout-seconds 2 &
viewer_process_id=$!

# If this script is interrupted, do not leave the viewer running.
trap 'kill "$viewer_process_id" 2>/dev/null || true' EXIT

# Give the viewer a moment to open its socket, then confirm it is running.
sleep 1
if ! kill -0 "$viewer_process_id" 2>/dev/null; then
    echo "Error: the viewer could not start. See the message above."
    exit 1
fi

# 500 frames at 50 Hz is 10 seconds. The fault covers frames 150 to 199,
# which is second 3 to second 4.
"$daemon_path" \
    --port "$demo_port" \
    --frame-limit 500 \
    --fault-start-frame 150 \
    --fault-length-frames 50 \
    --quiet

# Wait for the viewer to notice the silence and print its summary.
wait "$viewer_process_id"
trap - EXIT

echo
echo "The frames were also recorded in telemetry.db. To look inside:"
echo "  sqlite3 telemetry.db \"SELECT health_state, COUNT(*) FROM telemetry_frames GROUP BY health_state;\""
