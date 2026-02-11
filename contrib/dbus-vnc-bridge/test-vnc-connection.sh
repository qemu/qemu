#!/usr/bin/env bash
# Quick test that the D-Bus VNC bridge accepts VNC connections and performs
# the RFB handshake. Does not require QEMU to be running (tests VNC server only).
# For full end-to-end (display from QEMU), run QEMU with -display dbus in the
# same D-Bus session, then run the bridge and connect with a VNC viewer.

set -e
BRIDGE="${1:-./contrib/dbus-vnc-bridge/qemu-dbus-vnc-bridge}"
PORT="${2:-5901}"
if [[ ! -x "$BRIDGE" ]]; then
  echo "Usage: $0 [path-to-bridge] [port]"
  echo "Example: $0 build/contrib/dbus-vnc-bridge/qemu-dbus-vnc-bridge 5901"
  exit 1
fi

# Start bridge in background (will block in vnc_server_accept until client connects)
"$BRIDGE" --address 127.0.0.1 --port "$PORT" &
PID=$!
sleep 1

# Minimal RFB 3.8 client: send version, then receive server version and ServerInit
python3 << PY
import socket, struct
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(5)
s.connect(("127.0.0.1", $PORT))
def recv_exact(n):
    b = b""
    while len(b) < n:
        got = s.recv(n - len(b))
        if not got: raise BrokenPipeError
        b += got
    return b
s.sendall(b"RFB 003.008\n")
ver = recv_exact(12)
assert ver == b"RFB 003.008\n", repr(ver)
s.sendall(bytes([1, 1]))
recv_exact(1)
s.sendall(bytes([0]))
init = recv_exact(24)
w, h = struct.unpack(">HH", init[:4])
namelen = struct.unpack(">I", init[20:24])[0]
if namelen:
    recv_exact(namelen)
print("VNC handshake OK: server version RFB 003.008, screen %dx%d" % (w, h))
s.close()
PY

kill $PID 2>/dev/null
wait $PID 2>/dev/null
echo "Bridge VNC server: connection and handshake work as expected."
