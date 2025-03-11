=========================================================
AmigaNG boards (``amigaone``, ``pegasos2``, ``sam460ex``)
=========================================================

These PowerPC machines emulate boards that are primarily used for
running Amiga like OSes (AmigaOS 4, MorphOS and AROS) but these can
also run Linux which is what this section documents.

Eyetech AmigaOne/Mai Logic Teron (``amigaone``)
===============================================

The ``amigaone`` machine emulates an AmigaOne XE mainboard by Eyetech
which is a rebranded Mai Logic Teron board with modified U-Boot
firmware to support AmigaOS 4.

Emulated devices
----------------

 * PowerPC 7457 CPU (can also use ``-cpu g3, 750cxe, 750fx`` or ``750gx``)
 * Articia S north bridge
 * VIA VT82C686B south bridge
 * PCI VGA compatible card (guests may need other card instead)
 * PS/2 keyboard and mouse
 * 4 KiB NVRAM (use ``-drive if=mtd,format=raw,file=nvram.bin`` to keep contents persistent)

Firmware
--------

A firmware binary is necessary for the boot process. It is a modified
U-Boot under GPL but its source is lost so it cannot be included in
QEMU. A binary is available at
https://www.hyperion-entertainment.com/index.php/downloads?view=files&parent=28.
The ROM image is in the last 512kB which can be extracted with the
following command:

.. code-block:: bash

  $ tail -c 524288 updater.image > u-boot-amigaone.bin

The BIOS emulator in the firmware is unable to run QEMU‘s standard
vgabios so ``VGABIOS-lgpl-latest.bin`` is needed instead which can be
downloaded from http://www.nongnu.org/vgabios.

Running Linux
-------------

There are some Linux images under the following link that work on the
``amigaone`` machine:
https://sourceforge.net/projects/amigaone-linux/files/debian-installer/.
To boot the system run:

.. code-block:: bash

  $ qemu-system-ppc -machine amigaone -bios u-boot-amigaone.bin \
                    -cdrom "A1 Linux Net Installer.iso" \
                    -device ati-vga,model=rv100,romfile=VGABIOS-lgpl-latest.bin

If a firmware menu appears, select ``Boot sequence`` → ``Amiga Multiboot Options``
and set ``Boot device 1`` to ``Onboard VIA IDE CDROM``. Then hit escape until
the main screen appears again, hit escape once more and from the exit menu that
appears select either ``Save settings and exit`` or ``Use settings for this
session only``. It may take a long time loading the kernel into memory but
eventually it boots and the installer becomes visible. The ``ati-vga`` RV100
emulation is not complete yet so only frame buffer works, DRM and 3D is not
available.

Genesi/bPlan Pegasos II (``pegasos2``)
======================================

The ``pegasos2`` machine emulates the Pegasos II sold by Genesi and
designed by bPlan. Its schematics are available at
https://www.powerdeveloper.org/platforms/pegasos/schematics.

Emulated devices
----------------

 * PowerPC 7457 CPU (can also use ``-cpu g3`` or ``750cxe``)
 * Marvell MV64361 Discovery II north bridge
 * VIA VT8231 south bridge
 * PCI VGA compatible card (guests may need other card instead)
 * PS/2 keyboard and mouse

Firmware
--------

The Pegasos II board has an Open Firmware compliant ROM based on
SmartFirmware with some changes that are not open-sourced therefore
the ROM binary cannot be included in QEMU. An updater was available
from bPlan, it can be found in the `Internet Archive
<http://web.archive.org/web/20071021223056/http://www.bplan-gmbh.de/up050404/up050404>`_.
The ROM image can be extracted from it with the following command:

.. code-block:: bash

  $ tail -c +85581 up050404 | head -c 524288 > pegasos2.rom

Running Linux
-------------

The PowerPC version of Debian 8.11 supported Pegasos II. The BIOS
emulator in the firmware binary is unable to run QEMU‘s standard
vgabios so it needs to be disabled. To boot the system run:

.. code-block:: bash

  $ qemu-system-ppc -machine pegasos2 -bios pegasos2.rom \
                    -cdrom debian-8.11.0-powerpc-netinst.iso \
                    -device VGA,romfile="" -serial stdio

At the firmware ``ok`` prompt enter ``boot cd install/pegasos``.

Alternatively, it is possible to boot the kernel directly without
firmware ROM using the QEMU built-in minimal Virtual Open Firmware
(VOF) emulation which is also supported on ``pegasos2``. For this,
extract the kernel ``install/powerpc/vmlinuz-chrp.initrd`` from the CD
image, then run:

.. code-block:: bash

  $ qemu-system-ppc -machine pegasos2 -serial stdio \
                    -kernel vmlinuz-chrp.initrd -append "---" \
                    -cdrom debian-8.11.0-powerpc-netinst.iso

aCube Sam460ex (``sam460ex``)
=============================

The ``sam460ex`` machine emulates the Sam460ex board by aCube which is
based on the AMCC PowerPC 460EX SoC (that despite its name has a
PPC440 CPU core).

Firmware
--------

The board has a firmware based on an older U-Boot version with
modifications to support booting AmigaOS 4. The firmware ROM is
included with QEMU.

Emulated devices
----------------

 * PowerPC 460EX SoC
 * M41T80 serial RTC chip
 * Silicon Motion SM501 display parts (identical to SM502 on real board)
 * Silicon Image SiI3112 2 port SATA controller
 * USB keyboard and mouse

Running Linux
-------------

The only Linux distro that supported Sam460ex out of box was CruxPPC
2.x. It can be booted by running:

.. code-block:: bash

  $ qemu-system-ppc -machine sam460ex -serial stdio \
                    -drive if=none,id=cd,format=raw,file=crux-ppc-2.7a.iso \
                    -device ide-cd,drive=cd,bus=ide.1

There are some other kernels and instructions for booting other
distros on aCube's product page at
https://www.acube-systems.biz/index.php?page=hardware&pid=5
but those are untested.
