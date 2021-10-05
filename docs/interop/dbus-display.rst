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

.. only:: sphinx4

   .. dbus-doc:: ui/dbus-display1.xml

.. only:: not sphinx4

   .. warning::
      Sphinx 4 is required to build D-Bus documentation.

      This is the content of ``ui/dbus-display1.xml``:

   .. literalinclude:: ../../ui/dbus-display1.xml
      :language: xml
