.. _ARM-System-emulator:

Arm System emulator
-------------------

QEMU can emulate both 32-bit and 64-bit Arm CPUs. Use the
``qemu-system-aarch64`` executable to simulate a 64-bit Arm machine.
You can use either ``qemu-system-arm`` or ``qemu-system-aarch64``
to simulate a 32-bit Arm machine: in general, command lines that
work for ``qemu-system-arm`` will behave the same when used with
``qemu-system-aarch64``.

QEMU has generally good support for Arm guests. It has support for
nearly fifty different machines. The reason we support so many is that
Arm hardware is much more widely varying than x86 hardware. Arm CPUs
are generally built into "system-on-chip" (SoC) designs created by
many different companies with different devices, and these SoCs are
then built into machines which can vary still further even if they use
the same SoC. Even with fifty boards QEMU does not cover more than a
small fraction of the Arm hardware ecosystem.

The situation for 64-bit Arm is fairly similar, except that we don't
implement so many different machines.

As well as the more common "A-profile" CPUs (which have MMUs and will
run Linux) QEMU also supports "M-profile" CPUs such as the Cortex-M0,
Cortex-M4 and Cortex-M33 (which are microcontrollers used in very
embedded boards). For most boards the CPU type is fixed (matching what
the hardware has), so typically you don't need to specify the CPU type
by hand, except for special cases like the ``virt`` board.

Choosing a board model
======================

For QEMU's Arm system emulation, you must specify which board
model you want to use with the ``-M`` or ``--machine`` option;
there is no default.

Because Arm systems differ so much and in fundamental ways, typically
operating system or firmware images intended to run on one machine
will not run at all on any other. This is often surprising for new
users who are used to the x86 world where every system looks like a
standard PC. (Once the kernel has booted, most userspace software
cares much less about the detail of the hardware.)

If you already have a system image or a kernel that works on hardware
and you want to boot with QEMU, check whether QEMU lists that machine
in its ``-machine help`` output. If it is listed, then you can probably
use that board model. If it is not listed, then unfortunately your image
will almost certainly not boot on QEMU. (You might be able to
extract the filesystem and use that with a different kernel which
boots on a system that QEMU does emulate.)

If you don't care about reproducing the idiosyncrasies of a particular
bit of hardware, such as small amount of RAM, no PCI or other hard
disk, etc., and just want to run Linux, the best option is to use the
``virt`` board. This is a platform which doesn't correspond to any
real hardware and is designed for use in virtual machines. You'll
need to compile Linux with a suitable configuration for running on
the ``virt`` board. ``virt`` supports PCI, virtio, recent CPUs and
large amounts of RAM. It also supports 64-bit CPUs.

Board-specific documentation
============================

..
   This table of contents should be kept sorted alphabetically
   by the title text of each file, which isn't the same ordering
   as an alphabetical sort by filename.

.. toctree::
   :maxdepth: 1

   arm/integratorcp
   arm/mps2
   arm/musca
   arm/realview
   arm/sbsa
   arm/versatile
   arm/vexpress
   arm/aspeed
   arm/bananapi_m2u.rst
   arm/b-l475e-iot01a.rst
   arm/sabrelite
   arm/highbank
   arm/digic
   arm/cubieboard
   arm/emcraft-sf2
   arm/exynos
   arm/fby35
   arm/musicpal
   arm/kzm
   arm/nrf
   arm/nuvoton
   arm/imx25-pdk
   arm/mcimx6ul-evk
   arm/mcimx7d-sabre
   arm/imx8mp-evk
   arm/orangepi
   arm/raspi
   arm/collie
   arm/sx1
   arm/stellaris
   arm/stm32
   arm/virt
   arm/vmapple
   arm/xenpvh
   arm/xlnx-versal-virt
   arm/xlnx-zynq
   arm/xlnx-zcu102

Emulated CPU architecture support
=================================

.. toctree::
   arm/emulation

Arm CPU features
================

.. toctree::
   arm/cpu-features
