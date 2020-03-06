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
