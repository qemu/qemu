.. _Sparc64-System-emulator:

Sparc64 System emulator
-----------------------

Use the executable ``qemu-system-sparc64`` to simulate a Sun4u
(UltraSPARC PC-like machine), Sun4v (T1 PC-like machine), or generic
Niagara (T1) machine. The Sun4u emulator is mostly complete, being able
to run Linux, NetBSD and OpenBSD in headless (-nographic) mode. The
Sun4v emulator is still a work in progress.

The Niagara T1 emulator makes use of firmware and OS binaries supplied
in the S10image/ directory of the OpenSPARC T1 project
http://download.oracle.com/technetwork/systems/opensparc/OpenSPARCT1_Arch.1.5.tar.bz2
and is able to boot the disk.s10hw2 Solaris image.

::

   qemu-system-sparc64 -M niagara -L /path-to/S10image/ \
                       -nographic -m 256 \
                       -drive if=pflash,readonly=on,file=/S10image/disk.s10hw2

QEMU emulates the following peripherals:

-  UltraSparc IIi APB PCI Bridge

-  PCI VGA compatible card with VESA Bochs Extensions

-  PS/2 mouse and keyboard

-  Non Volatile RAM M48T59

-  PC-compatible serial ports

-  2 PCI IDE interfaces with hard disk and CD-ROM support

-  Floppy disk
