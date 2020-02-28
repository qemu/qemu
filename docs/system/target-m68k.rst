.. _ColdFire-System-emulator:

ColdFire System emulator
------------------------

Use the executable ``qemu-system-m68k`` to simulate a ColdFire machine.
The emulator is able to boot a uClinux kernel.

The M5208EVB emulation includes the following devices:

-  MCF5208 ColdFire V2 Microprocessor (ISA A+ with EMAC).

-  Three Two on-chip UARTs.

-  Fast Ethernet Controller (FEC)

The AN5206 emulation includes the following devices:

-  MCF5206 ColdFire V2 Microprocessor.

-  Two on-chip UARTs.

The following options are specific to the ColdFire emulation:

``-semihosting``
   Enable semihosting syscall emulation.

   On M68K this implements the \"ColdFire GDB\" interface used by
   libgloss.

   Note that this allows guest direct access to the host filesystem, so
   should only be used with trusted guest OS.
