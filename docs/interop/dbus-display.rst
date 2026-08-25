.. _dbus-display:

D-Bus display
=============

QEMU can export the VM display through D-Bus (when started with ``-display
dbus``), to allow out-of-process UIs, remote protocol servers or other
interactive display usages.

Various specialized D-Bus interfaces are available on different object paths
under ``/org/qemu/Display1/``, depending on the VM configuration.

QEMU also implements the standard interfaces, such as
`org.freedesktop.DBus.Introspectable
<https://dbus.freedesktop.org/doc/dbus-specification.html#standard-interfaces>`_.

.. contents::
   :local:
   :depth: 1

.. dbus-doc:: ui/dbus-display1.xml
