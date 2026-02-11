#!/usr/bin/env python3
"""
Verify that the D-Bus VNC bridge sends real framebuffer data to the VNC client.
Connects to the bridge, completes RFB handshake, requests framebuffer updates
(repeating as needed), and checks that we receive at least a minimum number of
non-black pixels (to ensure the display is actually being updated from QEMU).
"""
import argparse
import socket
import struct
import sys
import time

def recv_exact(sock, n, timeout=10):
    buf = b""
    sock.settimeout(timeout)
    while len(buf) < n:
        got = sock.recv(n - len(buf))
        if not got:
            raise BrokenPipeError("connection closed")
        buf += got
    return buf

def count_non_black_pixels(pixels):
    """Count pixels (4 bytes each) that have at least one non-zero byte."""
    n = 0
    for i in range(0, len(pixels), 4):
        if i + 4 > len(pixels):
            break
        if pixels[i] or pixels[i+1] or pixels[i+2] or pixels[i+3]:
            n += 1
    return n

def request_and_receive_framebuffer(s, width, height, timeout=10):
    """
    Send FramebufferUpdateRequest and read FramebufferUpdate reply.
    Returns (total_pixels, total_bytes, non_black_pixel_count) or None on error.
    """
    s.sendall(bytes([3, 0]) + struct.pack(">HHHH", 0, 0, width, height))
    try:
        hdr = recv_exact(s, 4, timeout)
    except (BrokenPipeError, ConnectionResetError, OSError):
        return None
    msg_type, padding, num_rects = hdr[0], hdr[1], struct.unpack(">H", hdr[2:4])[0]
    if msg_type != 0 or num_rects == 0:
        return None
    total_pixels = 0
    total_bytes = 0
    non_black_pixels = 0
    for i in range(num_rects):
        rect_hdr = recv_exact(s, 12, timeout)
        rx, ry, rw, rh = struct.unpack(">HHHH", rect_hdr[:8])
        encoding = struct.unpack(">i", rect_hdr[8:12])[0]
        if encoding == 0:
            payload_len = rw * rh * 4
            pixels = recv_exact(s, payload_len, timeout)
            total_pixels += rw * rh
            total_bytes += payload_len
            non_black_pixels += count_non_black_pixels(pixels)
    return (total_pixels, total_bytes, non_black_pixels)

def main():
    ap = argparse.ArgumentParser(description="Verify VNC receives non-black framebuffer data")
    ap.add_argument("port", type=int, nargs="?", default=5901, help="VNC port (default: 5901)")
    ap.add_argument("--min-non-black", type=int, default=100,
                    help="Minimum number of non-black pixels required (default: 100)")
    ap.add_argument("--retries", type=int, default=25,
                    help="Max framebuffer update requests before giving up (default: 25)")
    ap.add_argument("--retry-delay", type=float, default=1.0,
                    help="Seconds between framebuffer requests (default: 1.0)")
    args = ap.parse_args()

    port = args.port
    min_non_black = args.min_non_black
    max_attempts = args.retries
    retry_delay = args.retry_delay

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(15)
    try:
        s.connect(("127.0.0.1", port))
    except (socket.error, OSError) as e:
        print(f"ERROR: Cannot connect to 127.0.0.1:{port}: {e}", file=sys.stderr)
        return 1

    try:
        # RFB 3.8 handshake (server reads client version first in this implementation)
        s.sendall(b"RFB 003.008\n")
        recv_exact(s, 12)
        s.sendall(bytes([1, 1]))
        recv_exact(s, 1)
        s.sendall(bytes([0]))
        init = recv_exact(s, 24)
        width, height = struct.unpack(">HH", init[:4])
        namelen = struct.unpack(">I", init[20:24])[0]
        if namelen:
            recv_exact(s, namelen)
    except (BrokenPipeError, ConnectionResetError, OSError) as e:
        print("ERROR: Handshake failed:", e, file=sys.stderr)
        return 1

    print(f"Screen: {width}x{height}, requiring >={min_non_black} non-black pixels (retries up to {max_attempts})")

    best_non_black = 0
    for attempt in range(1, max_attempts + 1):
        result = request_and_receive_framebuffer(s, width, height, timeout=8)
        if result is None:
            print(f"Attempt {attempt}/{max_attempts}: no valid FramebufferUpdate (connection closed?)", file=sys.stderr)
            break
        total_pixels, total_bytes, non_black = result
        if total_bytes == 0:
            print(f"Attempt {attempt}/{max_attempts}: no pixel data", file=sys.stderr)
            break
        best_non_black = max(best_non_black, non_black)
        if non_black >= min_non_black:
            print(f"Attempt {attempt}/{max_attempts}: {total_pixels} pixels, {non_black} non-black (>= {min_non_black}) OK")
            s.close()
            print("PASS: VNC is receiving updated display data with non-black pixels from the bridge.")
            return 0
        if attempt < max_attempts:
            time.sleep(retry_delay)

    s.close()
    print(f"FAIL: Only {best_non_black} non-black pixels (required >={min_non_black}) after {max_attempts} attempts.", file=sys.stderr)
    return 1

if __name__ == "__main__":
    sys.exit(main())
