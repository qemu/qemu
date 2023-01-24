----------
About QEMU
----------

QEMU is a generic and open source machine emulator and virtualizer.

QEMU can be used in several different ways. The most common is for
:ref:`System Emulation`, where it provides a virtual model of an
entire machine (CPU, memory and emulated devices) to run a guest OS.
In this mode the CPU may be fully emulated, or it may work with a
hypervisor such as KVM, Xen, Hax or Hypervisor.Framework to allow the
guest to run directly on the host CPU.

The second supported way to use QEMU is :ref:`User Mode Emulation`,
where QEMU can launch processes compiled for one CPU on another CPU.
In this mode the CPU is always emulated.

QEMU also provides a number of standalone :ref:`command line
utilities<Tools>`, such as the ``qemu-img`` disk image utility that
allows you to create, convert and modify disk images.

.. toctree::
   :maxdepth: 2

   build-platforms
   emulation
   deprecated
   removed-features
   license
