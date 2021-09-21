'virt' Generic Virtual Platform (``virt``)
==========================================

The ``virt`` board is a platform which does not correspond to any real hardware;
it is designed for use in virtual machines. It is the recommended board type
if you simply want to run a guest such as Linux and do not care about
reproducing the idiosyncrasies and limitations of a particular bit of
real-world hardware.

Supported devices
-----------------

The ``virt`` machine supports the following devices:

* Up to 8 generic RV32GC/RV64GC cores, with optional extensions
* Core Local Interruptor (CLINT)
* Platform-Level Interrupt Controller (PLIC)
* CFI parallel NOR flash memory
* 1 NS16550 compatible UART
* 1 Google Goldfish RTC
* 1 SiFive Test device
* 8 virtio-mmio transport devices
* 1 generic PCIe host bridge
* The fw_cfg device that allows a guest to obtain data from QEMU

Note that the default CPU is a generic RV32GC/RV64GC. Optional extensions
can be enabled via command line parameters, e.g.: ``-cpu rv64,x-h=true``
enables the hypervisor extension for RV64.

Hardware configuration information
----------------------------------

The ``virt`` machine automatically generates a device tree blob ("dtb")
which it passes to the guest, if there is no ``-dtb`` option. This provides
information about the addresses, interrupt lines and other configuration of
the various devices in the system. Guest software should discover the devices
that are present in the generated DTB.

If users want to provide their own DTB, they can use the ``-dtb`` option.
These DTBs should have the following requirements:

* The number of subnodes of the /cpus node should match QEMU's ``-smp`` option
* The /memory reg size should match QEMU’s selected ram_size via ``-m``
* Should contain a node for the CLINT device with a compatible string
  "riscv,clint0" if using with OpenSBI BIOS images

Boot options
------------

The ``virt`` machine can start using the standard -kernel functionality
for loading a Linux kernel, a VxWorks kernel, an S-mode U-Boot bootloader
with the default OpenSBI firmware image as the -bios. It also supports
the recommended RISC-V bootflow: U-Boot SPL (M-mode) loads OpenSBI fw_dynamic
firmware and U-Boot proper (S-mode), using the standard -bios functionality.

Machine-specific options
------------------------

The following machine-specific options are supported:

- aclint=[on|off]

  When this option is "on", ACLINT devices will be emulated instead of
  SiFive CLINT. When not specified, this option is assumed to be "off".

Running Linux kernel
--------------------

Linux mainline v5.12 release is tested at the time of writing. To build a
Linux mainline kernel that can be booted by the ``virt`` machine in
64-bit mode, simply configure the kernel using the defconfig configuration:

.. code-block:: bash

  $ export ARCH=riscv
  $ export CROSS_COMPILE=riscv64-linux-
  $ make defconfig
  $ make

To boot the newly built Linux kernel in QEMU with the ``virt`` machine:

.. code-block:: bash

  $ qemu-system-riscv64 -M virt -smp 4 -m 2G \
      -display none -serial stdio \
      -kernel arch/riscv/boot/Image \
      -initrd /path/to/rootfs.cpio \
      -append "root=/dev/ram"

To build a Linux mainline kernel that can be booted by the ``virt`` machine
in 32-bit mode, use the rv32_defconfig configuration. A patch is required to
fix the 32-bit boot issue for Linux kernel v5.12.

.. code-block:: bash

  $ export ARCH=riscv
  $ export CROSS_COMPILE=riscv64-linux-
  $ curl https://patchwork.kernel.org/project/linux-riscv/patch/20210627135117.28641-1-bmeng.cn@gmail.com/mbox/ > riscv.patch
  $ git am riscv.patch
  $ make rv32_defconfig
  $ make

Replace ``qemu-system-riscv64`` with ``qemu-system-riscv32`` in the command
line above to boot the 32-bit Linux kernel. A rootfs image containing 32-bit
applications shall be used in order for kernel to boot to user space.

Running U-Boot
--------------

U-Boot mainline v2021.04 release is tested at the time of writing. To build an
S-mode U-Boot bootloader that can be booted by the ``virt`` machine, use
the qemu-riscv64_smode_defconfig with similar commands as described above for Linux:

.. code-block:: bash

  $ export CROSS_COMPILE=riscv64-linux-
  $ make qemu-riscv64_smode_defconfig

Boot the 64-bit U-Boot S-mode image directly:

.. code-block:: bash

  $ qemu-system-riscv64 -M virt -smp 4 -m 2G \
      -display none -serial stdio \
      -kernel /path/to/u-boot.bin

To test booting U-Boot SPL which in M-mode, which in turn loads a FIT image
that bundles OpenSBI fw_dynamic firmware and U-Boot proper (S-mode) together,
build the U-Boot images using riscv64_spl_defconfig:

.. code-block:: bash

  $ export CROSS_COMPILE=riscv64-linux-
  $ export OPENSBI=/path/to/opensbi-riscv64-generic-fw_dynamic.bin
  $ make qemu-riscv64_spl_defconfig

The minimal QEMU commands to run U-Boot SPL are:

.. code-block:: bash

  $ qemu-system-riscv64 -M virt -smp 4 -m 2G \
      -display none -serial stdio \
      -bios /path/to/u-boot-spl \
      -device loader,file=/path/to/u-boot.itb,addr=0x80200000

To test 32-bit U-Boot images, switch to use qemu-riscv32_smode_defconfig and
riscv32_spl_defconfig builds, and replace ``qemu-system-riscv64`` with
``qemu-system-riscv32`` in the command lines above to boot the 32-bit U-Boot.
