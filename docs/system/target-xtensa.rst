.. _Xtensa-System-emulator:

Xtensa System emulator
----------------------

Two executables cover simulation of both Xtensa endian options,
``qemu-system-xtensa`` and ``qemu-system-xtensaeb``. Two different
machine types are emulated:

-  Xtensa emulator pseudo board \"sim\"

-  Avnet LX60/LX110/LX200 board

The sim pseudo board emulation provides an environment similar to one
provided by the proprietary Tensilica ISS. It supports:

-  A range of Xtensa CPUs, default is the DC232B

-  Console and filesystem access via semihosting calls

The Avnet LX60/LX110/LX200 emulation supports:

-  A range of Xtensa CPUs, default is the DC232B

-  16550 UART

-  OpenCores 10/100 Mbps Ethernet MAC

The following options are specific to the Xtensa emulation:

``-semihosting``
   Enable semihosting syscall emulation.

   Xtensa semihosting provides basic file IO calls, such as
   open/read/write/seek/select. Tensilica baremetal libc for ISS and
   linux platform \"sim\" use this interface.

   Note that this allows guest direct access to the host filesystem, so
   should only be used with trusted guest OS.
