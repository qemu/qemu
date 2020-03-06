.. _pcsys_005fusb:

USB emulation
-------------

QEMU can emulate a PCI UHCI, OHCI, EHCI or XHCI USB controller. You can
plug virtual USB devices or real host USB devices (only works with
certain host operating systems). QEMU will automatically create and
connect virtual USB hubs as necessary to connect multiple USB devices.

.. _usb_005fdevices:

Connecting USB devices
~~~~~~~~~~~~~~~~~~~~~~

USB devices can be connected with the ``-device usb-...`` command line
option or the ``device_add`` monitor command. Available devices are:

``usb-mouse``
   Virtual Mouse. This will override the PS/2 mouse emulation when
   activated.

``usb-tablet``
   Pointer device that uses absolute coordinates (like a touchscreen).
   This means QEMU is able to report the mouse position without having
   to grab the mouse. Also overrides the PS/2 mouse emulation when
   activated.

``usb-storage,drive=drive_id``
   Mass storage device backed by drive_id (see
   :ref:`disk_005fimages`)

``usb-uas``
   USB attached SCSI device, see
   `usb-storage.txt <https://git.qemu.org/?p=qemu.git;a=blob_plain;f=docs/usb-storage.txt>`__
   for details

``usb-bot``
   Bulk-only transport storage device, see
   `usb-storage.txt <https://git.qemu.org/?p=qemu.git;a=blob_plain;f=docs/usb-storage.txt>`__
   for details here, too

``usb-mtp,rootdir=dir``
   Media transfer protocol device, using dir as root of the file tree
   that is presented to the guest.

``usb-host,hostbus=bus,hostaddr=addr``
   Pass through the host device identified by bus and addr

``usb-host,vendorid=vendor,productid=product``
   Pass through the host device identified by vendor and product ID

``usb-wacom-tablet``
   Virtual Wacom PenPartner tablet. This device is similar to the
   ``tablet`` above but it can be used with the tslib library because in
   addition to touch coordinates it reports touch pressure.

``usb-kbd``
   Standard USB keyboard. Will override the PS/2 keyboard (if present).

``usb-serial,chardev=id``
   Serial converter. This emulates an FTDI FT232BM chip connected to
   host character device id.

``usb-braille,chardev=id``
   Braille device. This will use BrlAPI to display the braille output on
   a real or fake device referenced by id.

``usb-net[,netdev=id]``
   Network adapter that supports CDC ethernet and RNDIS protocols. id
   specifies a netdev defined with ``-netdev …,id=id``. For instance,
   user-mode networking can be used with

   .. parsed-literal::

      |qemu_system| [...] -netdev user,id=net0 -device usb-net,netdev=net0

``usb-ccid``
   Smartcard reader device

``usb-audio``
   USB audio device

.. _host_005fusb_005fdevices:

Using host USB devices on a Linux host
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

WARNING: this is an experimental feature. QEMU will slow down when using
it. USB devices requiring real time streaming (i.e. USB Video Cameras)
are not supported yet.

1. If you use an early Linux 2.4 kernel, verify that no Linux driver is
   actually using the USB device. A simple way to do that is simply to
   disable the corresponding kernel module by renaming it from
   ``mydriver.o`` to ``mydriver.o.disabled``.

2. Verify that ``/proc/bus/usb`` is working (most Linux distributions
   should enable it by default). You should see something like that:

   ::

      ls /proc/bus/usb
      001  devices  drivers

3. Since only root can access to the USB devices directly, you can
   either launch QEMU as root or change the permissions of the USB
   devices you want to use. For testing, the following suffices:

   ::

      chown -R myuid /proc/bus/usb

4. Launch QEMU and do in the monitor:

   ::

      info usbhost
        Device 1.2, speed 480 Mb/s
          Class 00: USB device 1234:5678, USB DISK

   You should see the list of the devices you can use (Never try to use
   hubs, it won't work).

5. Add the device in QEMU by using:

   ::

      device_add usb-host,vendorid=0x1234,productid=0x5678

   Normally the guest OS should report that a new USB device is plugged.
   You can use the option ``-device usb-host,...`` to do the same.

6. Now you can try to use the host USB device in QEMU.

When relaunching QEMU, you may have to unplug and plug again the USB
device to make it work again (this is a bug).
