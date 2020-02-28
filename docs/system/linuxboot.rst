.. _direct_005flinux_005fboot:

Direct Linux Boot
-----------------

This section explains how to launch a Linux kernel inside QEMU without
having to make a full bootable image. It is very useful for fast Linux
kernel testing.

The syntax is:

.. parsed-literal::

   |qemu_system| -kernel bzImage -hda rootdisk.img -append "root=/dev/hda"

Use ``-kernel`` to provide the Linux kernel image and ``-append`` to
give the kernel command line arguments. The ``-initrd`` option can be
used to provide an INITRD image.

If you do not need graphical output, you can disable it and redirect the
virtual serial port and the QEMU monitor to the console with the
``-nographic`` option. The typical command line is:

.. parsed-literal::

   |qemu_system| -kernel bzImage -hda rootdisk.img \
                    -append "root=/dev/hda console=ttyS0" -nographic

Use Ctrl-a c to switch between the serial console and the monitor (see
:ref:`pcsys_005fkeys`).
