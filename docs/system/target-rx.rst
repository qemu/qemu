.. _RX-System-emulator:

RX System emulator
--------------------

Use the executable ``qemu-system-rx`` to simulate RX target (GDB simulator).
This target emulated following devices.

-  R5F562N8 MCU

   -  On-chip memory (ROM 512KB, RAM 96KB)
   -  Interrupt Control Unit (ICUa)
   -  8Bit Timer x 1CH (TMR0,1)
   -  Compare Match Timer x 2CH (CMT0,1)
   -  Serial Communication Interface x 1CH (SCI0)

-  External memory 16MByte

Example of ``qemu-system-rx`` usage for RX is shown below:

Download ``<u-boot_image_file>`` from
https://osdn.net/users/ysato/pf/qemu/dl/u-boot.bin.gz

Start emulation of rx-virt::
  qemu-system-rx -M gdbsim-r5f562n8 -bios <u-boot_image_file>

Download ``kernel_image_file`` from
https://osdn.net/users/ysato/pf/qemu/dl/zImage

Download ``device_tree_blob`` from
https://osdn.net/users/ysato/pf/qemu/dl/rx-virt.dtb

Start emulation of rx-virt::
  qemu-system-rx -M gdbsim-r5f562n8 \
      -kernel <kernel_image_file> -dtb <device_tree_blob> \
      -append "earlycon"
