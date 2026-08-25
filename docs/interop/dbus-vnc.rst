D-Bus VNC
=========

The ``qemu-vnc`` standalone VNC server exposes a D-Bus interface for management
and monitoring of VNC connections.

The service is available on the bus under the well-known name ``org.qemu.vnc``.
Objects are exported under ``/org/qemu/Vnc1/``.

.. contents::
   :local:
   :depth: 1

.. dbus-doc:: tools/qemu-vnc/qemu-vnc1.xml
