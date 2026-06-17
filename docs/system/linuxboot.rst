.. _direct_005flinux_005fboot:

Direct Linux Boot
-----------------

This section explains how to launch a Linux kernel inside QEMU without
having to make a full bootable image. It is very useful for fast Linux
kernel testing.

The syntax is:

.. parsed-literal::

   |qemu_system| -kernel bzImage -drive file=rootdisk.img,format=raw -append "root=/dev/sda"

Use ``-kernel`` to provide the Linux kernel image and ``-append`` to
give the kernel command line arguments. The ``-initrd`` option can be
used to provide an INITRD image.

The ``-shim`` option specifies the ``shim.efi`` binary.  This is needed
when you are booting UEFI firmware and using the ``-kernel`` option to
tell UEFI to boot a specific kernel image, and the UEFI firmware you
are booting has UEFI secure boot enabled.

When this option is specified, the guest UEFI firmware will first
load, verify and run the shim binary, which is typically signed by
Microsoft so the firmware accepts it.  The shim binary in turn will
load and verify the Linux kernel.  The kernel is typically signed by
the distro and the certificates needed to verify them are compiled
into the shim binary, so shim and kernel must come from the same Linux
distribution.

Usually you can find shim.efi as ``EFI/BOOT/BOOT{X64,AA64}.EFI`` on
distro install media.  You might find a second shim copy in the
``EFI/$distro/`` directory.

If you do not need graphical output, you can disable it and redirect the
virtual serial port and the QEMU monitor to the console with the
``-nographic`` option. The typical command line is:

.. parsed-literal::

   |qemu_system| -kernel bzImage -drive file=rootdisk.img,format=raw \
                    -append "root=/dev/sda console=ttyS0" -nographic

Use :kbd:`Ctrl+a c` to switch between the serial console and the monitor (see
:ref:`GUI_keys`).
