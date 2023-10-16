.. _s390x-System-emulator:

s390x System emulator
---------------------

QEMU can emulate z/Architecture (in particular, 64 bit) s390x systems
via the ``qemu-system-s390x`` binary. Only one machine type,
``s390-ccw-virtio``, is supported (with versioning for compatibility
handling).

When using KVM as accelerator, QEMU can emulate CPUs up to the generation
of the host. When using the default cpu model with TCG as accelerator,
QEMU will emulate a subset of z13 cpu features that should be enough to run
distributions built for the z13.

Device support
==============

QEMU will not emulate most of the traditional devices found under LPAR or
z/VM; virtio devices (especially using virtio-ccw) make up the bulk of
the available devices. Passthrough of host devices via vfio-pci, vfio-ccw,
or vfio-ap is also available.

.. toctree::
   s390x/vfio-ap
   s390x/css
   s390x/3270
   s390x/vfio-ccw
   s390x/pcidevices

Architectural features
======================

.. toctree::
   s390x/bootdevices
   s390x/protvirt
   s390x/cpu-topology
