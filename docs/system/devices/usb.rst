USB emulation
-------------

QEMU can emulate a PCI UHCI, OHCI, EHCI or XHCI USB controller. You can
plug virtual USB devices or real host USB devices (only works with
certain host operating systems). QEMU will automatically create and
connect virtual USB hubs as necessary to connect multiple USB devices.

USB controllers
~~~~~~~~~~~~~~~

XHCI controller support
^^^^^^^^^^^^^^^^^^^^^^^

QEMU has XHCI host adapter support.  The XHCI hardware design is much
more virtualization-friendly when compared to EHCI and UHCI, thus XHCI
emulation uses less resources (especially CPU).  So if your guest
supports XHCI (which should be the case for any operating system
released around 2010 or later) we recommend using it:

    qemu -device qemu-xhci

XHCI supports USB 1.1, USB 2.0 and USB 3.0 devices, so this is the
only controller you need.  With only a single USB controller (and
therefore only a single USB bus) present in the system there is no
need to use the bus= parameter when adding USB devices.


EHCI controller support
^^^^^^^^^^^^^^^^^^^^^^^

The QEMU EHCI Adapter supports USB 2.0 devices.  It can be used either
standalone or with companion controllers (UHCI, OHCI) for USB 1.1
devices.  The companion controller setup is more convenient to use
because it provides a single USB bus supporting both USB 2.0 and USB
1.1 devices.  See next section for details.

When running EHCI in standalone mode you can add UHCI or OHCI
controllers for USB 1.1 devices too.  Each controller creates its own
bus though, so there are two completely separate USB buses: One USB
1.1 bus driven by the UHCI controller and one USB 2.0 bus driven by
the EHCI controller.  Devices must be attached to the correct
controller manually.

The easiest way to add a UHCI controller to a ``pc`` machine is the
``-usb`` switch.  QEMU will create the UHCI controller as function of
the PIIX3 chipset.  The USB 1.1 bus will carry the name ``usb-bus.0``.

You can use the standard ``-device`` switch to add a EHCI controller to
your virtual machine.  It is strongly recommended to specify an ID for
the controller so the USB 2.0 bus gets an individual name, for example
``-device usb-ehci,id=ehci``.  This will give you a USB 2.0 bus named
``ehci.0``.

When adding USB devices using the ``-device`` switch you can specify the
bus they should be attached to.  Here is a complete example:

.. parsed-literal::

    |qemu_system| -M pc ${otheroptions}                        \\
        -drive if=none,id=usbstick,format=raw,file=/path/to/image   \\
        -usb                                                        \\
        -device usb-ehci,id=ehci                                    \\
        -device usb-tablet,bus=usb-bus.0                            \\
        -device usb-storage,bus=ehci.0,drive=usbstick

This attaches a USB tablet to the UHCI adapter and a USB mass storage
device to the EHCI adapter.


Companion controller support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The UHCI and OHCI controllers can attach to a USB bus created by EHCI
as companion controllers.  This is done by specifying the ``masterbus``
and ``firstport`` properties.  ``masterbus`` specifies the bus name the
controller should attach to.  ``firstport`` specifies the first port the
controller should attach to, which is needed as usually one EHCI
controller with six ports has three UHCI companion controllers with
two ports each.

There is a config file in docs which will do all this for
you, which you can use like this:

.. parsed-literal::

   |qemu_system| -readconfig docs/config/ich9-ehci-uhci.cfg

Then use ``bus=ehci.0`` to assign your USB devices to that bus.

Using the ``-usb`` switch for ``q35`` machines will create a similar
USB controller configuration.


.. _Connecting USB devices:

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
   Mass storage device backed by drive_id (see the :ref:`disk images`
   chapter in the System Emulation Users Guide). This is the classic
   bulk-only transport protocol used by 99% of USB sticks. This
   example shows it connected to an XHCI USB controller and with
   a drive backed by a raw format disk image:

   .. parsed-literal::

       |qemu_system| [...]                                   \\
        -drive if=none,id=stick,format=raw,file=/path/to/file.img \\
        -device nec-usb-xhci,id=xhci                              \\
        -device usb-storage,bus=xhci.0,drive=stick

``usb-uas``
   USB attached SCSI device. This does not create a SCSI disk, so
   you need to explicitly create a ``scsi-hd`` or ``scsi-cd`` device
   on the command line, as well as using the ``-drive`` option to
   specify what those disks are backed by. One ``usb-uas`` device can
   handle multiple logical units (disks). This example creates three
   logical units: two disks and one cdrom drive:

   .. parsed-literal::

      |qemu_system| [...]                                         \\
       -drive if=none,id=uas-disk1,format=raw,file=/path/to/file1.img  \\
       -drive if=none,id=uas-disk2,format=raw,file=/path/to/file2.img  \\
       -drive if=none,id=uas-cdrom,media=cdrom,format=raw,file=/path/to/image.iso \\
       -device nec-usb-xhci,id=xhci                                    \\
       -device usb-uas,id=uas,bus=xhci.0                               \\
       -device scsi-hd,bus=uas.0,scsi-id=0,lun=0,drive=uas-disk1       \\
       -device scsi-hd,bus=uas.0,scsi-id=0,lun=1,drive=uas-disk2       \\
       -device scsi-cd,bus=uas.0,scsi-id=0,lun=5,drive=uas-cdrom

``usb-bot``
   Bulk-only transport storage device. This presents the guest with the
   same USB bulk-only transport protocol interface as ``usb-storage``, but
   the QEMU command line option works like ``usb-uas`` and does not
   automatically create SCSI disks for you. ``usb-bot`` supports up to
   16 LUNs. Unlike ``usb-uas``, the LUN numbers must be continuous,
   i.e. for three devices you must use 0+1+2. The 0+1+5 numbering from the
   ``usb-uas`` example above won't work with ``usb-bot``.

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
   Braille device. This emulates a Baum Braille device USB port. id has to
   specify a character device defined with ``-chardev …,id=id``.  One will
   normally use BrlAPI to display the braille output on a BRLTTY-supported
   device with

   .. parsed-literal::

      |qemu_system| [...] -chardev braille,id=brl -device usb-braille,chardev=brl

   or alternatively, use the following equivalent shortcut:

   .. parsed-literal::

      |qemu_system| [...] -usbdevice braille

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

``u2f-{emulated,passthru}``
   :doc:`usb-u2f`

``canokey``
   An Open-source Secure Key implementing FIDO2, OpenPGP, PIV and more.
   For more information, see :ref:`canokey`.

Physical port addressing
^^^^^^^^^^^^^^^^^^^^^^^^

For all the above USB devices, by default QEMU will plug the device
into the next available port on the specified USB bus, or onto
some available USB bus if you didn't specify one explicitly.
If you need to, you can also specify the physical port where
the device will show up in the guest.  This can be done using the
``port`` property.  UHCI has two root ports (1,2).  EHCI has six root
ports (1-6), and the emulated (1.1) USB hub has eight ports.

Plugging a tablet into UHCI port 1 works like this::

        -device usb-tablet,bus=usb-bus.0,port=1

Plugging a hub into UHCI port 2 works like this::

        -device usb-hub,bus=usb-bus.0,port=2

Plugging a virtual USB stick into port 4 of the hub just plugged works
this way::

        -device usb-storage,bus=usb-bus.0,port=2.4,drive=...

In the monitor, the ``device_add` command also accepts a ``port``
property specification. If you want to unplug devices too you should
specify some unique id which you can use to refer to the device.
You can then use ``device_del`` to unplug the device later.
For example::

        (qemu) device_add usb-tablet,bus=usb-bus.0,port=1,id=my-tablet
        (qemu) device_del my-tablet

Hotplugging USB storage
~~~~~~~~~~~~~~~~~~~~~~~

The ``usb-bot`` and ``usb-uas`` devices can be hotplugged.  In the hotplug
case they are added with ``attached = false`` so the guest will not see
the device until the ``attached`` property is explicitly set to true.
That allows you to attach one or more scsi devices before making the
device visible to the guest. The workflow looks like this:

#. ``device-add usb-bot,id=foo``
#. ``device-add scsi-{hd,cd},bus=foo.0,lun=0``
#. optionally add more devices (luns 1 ... 15)
#. ``scripts/qmp/qom-set foo.attached = true``

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

``usb-host`` properties for specifying the host device
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The example above uses the ``vendorid`` and ``productid`` to
specify which host device to pass through, but this is not
the only way to specify the host device. ``usb-host`` supports
the following properties:

``hostbus=<nr>``
  Specifies the bus number the device must be attached to
``hostaddr=<nr>``
  Specifies the device address the device got assigned by the guest os
``hostport=<str>``
  Specifies the physical port the device is attached to
``vendorid=<hexnr>``
  Specifies the vendor ID of the device
``productid=<hexnr>``
  Specifies the product ID of the device.

In theory you can combine all these properties as you like.  In
practice only a few combinations are useful:

- ``vendorid`` and ``productid`` -- match for a specific device, pass it to
  the guest when it shows up somewhere in the host.

- ``hostbus`` and ``hostport`` -- match for a specific physical port in the
  host, any device which is plugged in there gets passed to the
  guest.

- ``hostbus`` and ``hostaddr`` -- most useful for ad-hoc pass through as the
  hostaddr isn't stable. The next time you plug the device into the host it
  will get a new hostaddr.

Note that on the host USB 1.1 devices are handled by UHCI/OHCI and USB
2.0 by EHCI.  That means different USB devices plugged into the very
same physical port on the host may show up on different host buses
depending on the speed. Supposing that devices plugged into a given
physical port appear as bus 1 + port 1 for 2.0 devices and bus 3 + port 1
for 1.1 devices, you can pass through any device plugged into that port
and also assign it to the correct USB bus in QEMU like this:

.. parsed-literal::

   |qemu_system| -M pc [...]                            \\
        -usb                                                 \\
        -device usb-ehci,id=ehci                             \\
        -device usb-host,bus=usb-bus.0,hostbus=3,hostport=1  \\
        -device usb-host,bus=ehci.0,hostbus=1,hostport=1

``usb-host`` properties for reset behavior
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``guest-reset`` and ``guest-reset-all`` properties control
whenever the guest is allowed to reset the physical usb device on the
host.  There are three cases:

``guest-reset=false``
  The guest is not allowed to reset the (physical) usb device.

``guest-reset=true,guest-resets-all=false``
  The guest is allowed to reset the device when it is not yet
  initialized (aka no usb bus address assigned).  Usually this results
  in one guest reset being allowed.  This is the default behavior.

``guest-reset=true,guest-resets-all=true``
  The guest is allowed to reset the device as it pleases.

The reason for this existing are broken usb devices.  In theory one
should be able to reset (and re-initialize) usb devices at any time.
In practice that may result in shitty usb device firmware crashing and
the device not responding any more until you power-cycle (aka un-plug
and re-plug) it.

What works best pretty much depends on the behavior of the specific
usb device at hand, so it's a trial-and-error game.  If the default
doesn't work, try another option and see whenever the situation
improves.

record usb transfers
^^^^^^^^^^^^^^^^^^^^

All usb devices have support for recording the usb traffic.  This can
be enabled using the ``pcap=<file>`` property, for example:

``-device usb-mouse,pcap=mouse.pcap``

The pcap files are compatible with the linux kernels usbmon.  Many
tools, including ``wireshark``, can decode and inspect these trace
files.
