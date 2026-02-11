=====================
QEMU D-Bus VNC bridge
=====================

This is a **standalone** VNC server that connects to a QEMU process only via
D-Bus. It is similar in spirit to the `qemu-vnc` Rust implementation from the
`qemu-display` project: QEMU runs with ``-display dbus`` and this bridge
registers as a display listener over D-Bus and exposes the same display as
VNC.

Build
=====

The bridge is built automatically when QEMU is configured with D-Bus display
support (``-display dbus``), and only on Unix (Linux, macOS, etc.)::

  meson setup build -Ddbus_display=enabled
  ninja -C build qemu-dbus-vnc-bridge

The binary is installed as ``qemu-dbus-vnc-bridge`` (in ``bindir``).

Usage
=====

1. Start QEMU with the D-Bus display::

     qemu-system-x86_64 -display dbus -name myvm ...

2. Start the bridge (default: listen on 127.0.0.1:5900)::

     qemu-dbus-vnc-bridge

   Or with options::

     qemu-dbus-vnc-bridge --address 0.0.0.0 --port 5900
     qemu-dbus-vnc-bridge --dbus-address unix:path=/run/user/1000/bus

3. Connect a VNC viewer to the bridge::

     vncviewer localhost:5900

**Important:** QEMU and the bridge must use the same D-Bus session (e.g. start
both from the same desktop terminal). Otherwise the bridge will report
"Object does not exist at path /org/qemu/Display1/Console_0".

Verifying that VNC receives framebuffer data
============================================

* To see when the bridge sends framebuffer updates to the VNC client, run the
  bridge with::

     QEMU_DBUS_VNC_BRIDGE_DEBUG=1 qemu-dbus-vnc-bridge

  Each time QEMU pushes a display update over D-Bus, the bridge will print
  e.g. ``VNC: sent framebuffer 640×480 (1228800 bytes)``.

* To verify that the VNC client receives non-black pixel data, run (with QEMU
  and the bridge already running in the same D-Bus session)::

     python3 contrib/dbus-vnc-bridge/verify-vnc-data.py 5900 --min-non-black 100

  This connects as an RFB client, repeatedly requests framebuffer updates
  (with retries), and requires at least ``--min-non-black`` non-black pixels
  (default 100). Use ``--retries N`` and ``--retry-delay SECS`` to tune
  waiting for the display to update.

Options
=======

* ``--address ADDR``  Bind address (default: 127.0.0.1)
* ``--port PORT``     Bind port (default: 5900)
* ``--dbus-address ADDR``  D-Bus address (default: session bus)
* ``--help``          Show help

Implementation notes
====================

* The bridge uses the same D-Bus interface as QEMU's ``-display dbus``
  (``org.qemu.Display1.*``). It connects to the session bus (or a given
  address), obtains the first console (Console_0), and calls
  ``RegisterListener`` with one end of a Unix socket pair. It then
  implements the ``org.qemu.Display1.Listener`` interface on the other
  end (Scanout, Update, etc.) and stores the framebuffer. A minimal RFB
  (VNC) server sends that framebuffer to VNC clients and forwards input
  (keyboard, pointer) back over D-Bus.

* Only one VNC client is served at a time. When the client disconnects, the
  bridge unregisters the listener and waits for the next connection.

* Pixel format is fixed to 32 bpp X8R8G8B8 (same as QEMU's internal format).
  The VNC server uses raw encoding only.

* This bridge does **not** reuse QEMU's in-tree VNC server code (``ui/vnc*.c``)
  because that code is tied to QEMU's display and main loop. The protocol
  and behavior are aligned so that a future refactor could share more code
  if the in-tree VNC were split into a library.
