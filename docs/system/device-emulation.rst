.. _device-emulation:

Device Emulation
----------------

QEMU supports the emulation of a large number of devices from
peripherals such network cards and USB devices to integrated systems
on a chip (SoCs). Configuration of these is often a source of
confusion so it helps to have an understanding of some of the terms
used to describes devices within QEMU.

Common Terms
~~~~~~~~~~~~

Device Front End
================

A device front end is how a device is presented to the guest. The type
of device presented should match the hardware that the guest operating
system is expecting to see. All devices can be specified with the
``--device`` command line option. Running QEMU with the command line
options ``--device help`` will list all devices it is aware of. Using
the command line ``--device foo,help`` will list the additional
configuration options available for that device.

A front end is often paired with a back end, which describes how the
host's resources are used in the emulation.

Device Buses
============

Most devices will exist on a BUS of some sort. Depending on the
machine model you choose (``-M foo``) a number of buses will have been
automatically created. In most cases the BUS a device is attached to
can be inferred, for example PCI devices are generally automatically
allocated to the next free address of first PCI bus found. However in
complicated configurations you can explicitly specify what bus
(``bus=ID``) a device is attached to along with its address
(``addr=N``).

Some devices, for example a PCI SCSI host controller, will add an
additional buses to the system that other devices can be attached to.
A hypothetical chain of devices might look like:

  --device foo,bus=pci.0,addr=0,id=foo
  --device bar,bus=foo.0,addr=1,id=baz

which would be a bar device (with the ID of baz) which is attached to
the first foo bus (foo.0) at address 1. The foo device which provides
that bus is itself is attached to the first PCI bus (pci.0).


Device Back End
===============

The back end describes how the data from the emulated device will be
processed by QEMU. The configuration of the back end is usually
specific to the class of device being emulated. For example serial
devices will be backed by a ``--chardev`` which can redirect the data
to a file or socket or some other system. Storage devices are handled
by ``--blockdev`` which will specify how blocks are handled, for
example being stored in a qcow2 file or accessing a raw host disk
partition. Back ends can sometimes be stacked to implement features
like snapshots.

While the choice of back end is generally transparent to the guest,
there are cases where features will not be reported to the guest if
the back end is unable to support it.

Device Pass Through
===================

Device pass through is where the device is actually given access to
the underlying hardware. This can be as simple as exposing a single
USB device on the host system to the guest or dedicating a video card
in a PCI slot to the exclusive use of the guest.


Emulated Devices
~~~~~~~~~~~~~~~~

.. toctree::
   :maxdepth: 1

   devices/ivshmem.rst
   devices/net.rst
   devices/nvme.rst
   devices/usb.rst
   devices/vhost-user.rst
   devices/virtio-pmem.rst
   devices/vhost-user-rng.rst
