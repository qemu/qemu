.. _QEMU-PC-System-emulator:

x86 (PC) System emulator
------------------------

.. _pcsys_005fdevices:

Peripherals
~~~~~~~~~~~

.. include:: target-i386-desc.rst.inc

.. include:: cpu-models-x86.rst.inc

.. _pcsys_005freq:

OS requirements
~~~~~~~~~~~~~~~

On x86_64 hosts, the default set of CPU features enabled by the KVM
accelerator require the host to be running Linux v4.5 or newer. Red Hat
Enterprise Linux 7 is also supported, since the required
functionality was backported.
