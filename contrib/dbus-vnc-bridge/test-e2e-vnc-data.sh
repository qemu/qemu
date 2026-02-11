#!/usr/bin/env bash
# Full automated end-to-end test: QEMU (-display dbus) + D-Bus VNC bridge + VNC
# client verification that display data with non-black pixels is received.
# Uses a dedicated D-Bus session (dbus-launch) so QEMU and the bridge share
# the same bus. Can repeat the full test multiple times (--runs N).
#
# Usage: ./test-e2e-vnc-data.sh [build-dir] [port] [--runs N] [--min-non-black N]
# Example: ./test-e2e-vnc-data.sh /tmp/qemu/build 5901
#          ./test-e2e-vnc-data.sh /tmp/qemu/build 5901 --runs 2

set -e
BUILD_DIR="${1:-.}"
PORT="${2:-5901}"
RUNS=1
MIN_NON_BLACK=100
shift 2 2>/dev/null || true
while [[ $# -gt 0 ]]; do
  case "$1" in
    --runs) RUNS="${2:?--runs requires N}"; shift 2 ;;
    --min-non-black) MIN_NON_BLACK="${2:?--min-non-black requires N}"; shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done
[[ "$RUNS" =~ ^[0-9]+$ ]] || { echo "ERROR: --runs must be a number" >&2; exit 1; }
[[ "$MIN_NON_BLACK" =~ ^[0-9]+$ ]] || { echo "ERROR: --min-non-black must be a number" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$BUILD_DIR"

QEMU="./qemu-system-x86_64"
BRIDGE="./contrib/dbus-vnc-bridge/qemu-dbus-vnc-bridge"
VERIFY="$SCRIPT_DIR/verify-vnc-data.py"

if [[ ! -x "$QEMU" ]]; then
  echo "ERROR: Not found or not executable: $QEMU" >&2
  exit 1
fi
if [[ ! -x "$BRIDGE" ]]; then
  echo "ERROR: Not found or not executable: $BRIDGE" >&2
  exit 1
fi
if [[ ! -f "$VERIFY" ]]; then
  echo "ERROR: Not found: $VERIFY" >&2
  exit 1
fi

if ! command -v dbus-launch &>/dev/null; then
  echo "ERROR: dbus-launch not found. Install dbus-x11 or dbus." >&2
  exit 1
fi

echo "Starting dedicated D-Bus session..."
eval "$(dbus-launch --sh-syntax)"
export DBUS_SESSION_BUS_ADDRESS
unset QEMU_DBUS_VNC_BRIDGE_DEBUG
if [[ -z "$DBUS_SESSION_BUS_ADDRESS" ]]; then
  echo "ERROR: dbus-launch did not set DBUS_SESSION_BUS_ADDRESS" >&2
  exit 1
fi

BRIDGE_PID=""
QEMU_PID=""
cleanup() {
  local status=$?
  [[ -n "$BRIDGE_PID" ]] && kill $BRIDGE_PID 2>/dev/null || true
  [[ -n "$QEMU_PID" ]] && kill $QEMU_PID 2>/dev/null || true
  wait $BRIDGE_PID $QEMU_PID 2>/dev/null || true
  [[ -n "$DBUS_SESSION_BUS_PID" ]] && kill "$DBUS_SESSION_BUS_PID" 2>/dev/null || true
  exit "$status"
}
trap cleanup EXIT INT TERM

run_one() {
  local run_num=$1
  if [[ $RUNS -gt 1 ]]; then
    echo "========== Run $run_num / $RUNS =========="
  fi

  echo "Starting QEMU with -display dbus..."
  "$QEMU" -display dbus -m 256 -no-reboot -name testvm 2>&1 &
  QEMU_PID=$!
  # Wait for QEMU to initialize display and for firmware/BIOS to draw (non-black pixels).
  echo "Waiting for QEMU to initialize and draw (12s)..."
  for i in $(seq 1 12); do
    sleep 1
    if ! kill -0 $QEMU_PID 2>/dev/null; then
      echo "ERROR: QEMU exited early" >&2
      return 1
    fi
  done
  sleep 1

  echo "Starting D-Bus VNC bridge on port $PORT..."
  "$BRIDGE" --address 127.0.0.1 --port "$PORT" 2>&1 &
  BRIDGE_PID=$!
  sleep 2
  if ! kill -0 $BRIDGE_PID 2>/dev/null; then
    echo "ERROR: Bridge exited early" >&2
    kill $QEMU_PID 2>/dev/null || true
    return 1
  fi

  echo "Running VNC verification (require >= $MIN_NON_BLACK non-black pixels)..."
  if python3 "$VERIFY" "$PORT" --min-non-black "$MIN_NON_BLACK"; then
    kill $BRIDGE_PID $QEMU_PID 2>/dev/null || true
    wait $BRIDGE_PID $QEMU_PID 2>/dev/null || true
    BRIDGE_PID=""
    QEMU_PID=""
    return 0
  fi
  kill $BRIDGE_PID $QEMU_PID 2>/dev/null || true
  wait $BRIDGE_PID $QEMU_PID 2>/dev/null || true
  BRIDGE_PID=""
  QEMU_PID=""
  return 1
}

for r in $(seq 1 "$RUNS"); do
  if ! run_one "$r"; then
    echo "E2E test FAILED (run $r/$RUNS)." >&2
    exit 1
  fi
  # Brief pause between runs so ports and D-Bus settle
  [[ $r -lt $RUNS ]] && sleep 2
done

echo "E2E test PASSED: VNC receives updated display data with non-black pixels from QEMU (all $RUNS run(s))."
exit 0
