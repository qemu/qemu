#!/usr/bin/env bash
# Full VNC session test: QEMU (-display dbus) + bridge + data verification.
# Run from a terminal that has a D-Bus session (e.g. desktop session) so
# QEMU and the bridge share the same bus.
#
# Usage: ./run-full-vnc-test.sh [build-dir]
# Example: ./run-full-vnc-test.sh /tmp/qemu/build

set -e
BUILD_DIR="${1:-.}"
PORT="${2:-5901}"
cd "$BUILD_DIR"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/user/$(id -u)/bus}"
# Uncomment to see when the bridge sends framebuffer data to VNC:
# export QEMU_DBUS_VNC_BRIDGE_DEBUG=1

QEMU="./qemu-system-x86_64"
BRIDGE="./contrib/dbus-vnc-bridge/qemu-dbus-vnc-bridge"
VERIFY="$(dirname "$0")/verify-vnc-data.py"

if [[ ! -x "$QEMU" ]]; then
  echo "Not found or not executable: $QEMU"
  exit 1
fi
if [[ ! -x "$BRIDGE" ]]; then
  echo "Not found or not executable: $BRIDGE"
  exit 1
fi

echo "Starting QEMU with -display dbus..."
"$QEMU" -display dbus -m 256 -no-reboot -name testvm 2>&1 &
QEMU_PID=$!
sleep 5
echo "Starting D-Bus VNC bridge on port $PORT..."
"$BRIDGE" --address 127.0.0.1 --port "$PORT" 2>&1 &
BRIDGE_PID=$!
sleep 2
echo "Verifying VNC framebuffer data..."
if python3 "$VERIFY" "$PORT"; then
  echo "SUCCESS: VNC is receiving real framebuffer data."
else
  echo "Verification failed or bridge could not register (no QEMU on same D-Bus session?)."
fi
kill $BRIDGE_PID $QEMU_PID 2>/dev/null
wait $BRIDGE_PID $QEMU_PID 2>/dev/null
echo "Done."
