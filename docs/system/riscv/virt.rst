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

* Up to 512 generic RV32GC/RV64GC cores, with optional extensions
* Core Local Interruptor (CLINT)
* Platform-Level Interrupt Controller (PLIC)
* CFI parallel NOR flash memory
* 1 NS16550 compatible UART
* 1 Google Goldfish RTC
* 1 SiFive Test device
* 8 virtio-mmio transport devices
* 1 generic PCIe host bridge
* The fw_cfg device that allows a guest to obtain data from QEMU

The hypervisor extension has been enabled for the default CPU, so virtual
machines with hypervisor extension can simply be used without explicitly
declaring.

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

Using flash devices
-------------------

By default, the first flash device (pflash0) is expected to contain
S-mode firmware code. It can be configured as read-only, with the
second flash device (pflash1) available to store configuration data.

For example, booting edk2 looks like

.. code-block:: bash

  $ qemu-system-riscv64 \
     -blockdev node-name=pflash0,driver=file,read-only=on,filename=<edk2_code> \
     -blockdev node-name=pflash1,driver=file,filename=<edk2_vars> \
     -M virt,pflash0=pflash0,pflash1=pflash1 \
     ... other args ....

For TCG guests only, it is also possible to boot M-mode firmware from
the first flash device (pflash0) by additionally passing ``-bios
none``, as in

.. code-block:: bash

  $ qemu-system-riscv64 \
     -bios none \
     -blockdev node-name=pflash0,driver=file,read-only=on,filename=<m_mode_code> \
     -M virt,pflash0=pflash0 \
     ... other args ....

Firmware images used for pflash must be exactly 32 MiB in size.

riscv-iommu support
-------------------

The board has support for the riscv-iommu-pci device by using the following
command line:

.. code-block:: bash

  $ qemu-system-riscv64 -M virt -device riscv-iommu-pci (...)

It also has support for the riscv-iommu-sys platform device:

.. code-block:: bash

  $ qemu-system-riscv64 -M virt,iommu-sys=on (...)

Refer to :ref:`riscv-iommu` for more information on how the RISC-V IOMMU support
works.

Machine-specific options
------------------------

The following machine-specific options are supported:

- aclint=[on|off]

  When this option is "on", ACLINT devices will be emulated instead of
  SiFive CLINT. When not specified, this option is assumed to be "off".
  This option is restricted to the TCG accelerator.

- acpi=[on|off|auto]

  When this option is "on" (which is the default), ACPI tables are generated and
  exposed as firmware tables etc/acpi/rsdp and etc/acpi/tables.

- aia=[none|aplic|aplic-imsic]

  This option allows selecting interrupt controller defined by the AIA
  (advanced interrupt architecture) specification. The "aia=aplic" selects
  APLIC (advanced platform level interrupt controller) to handle wired
  interrupts whereas the "aia=aplic-imsic" selects APLIC and IMSIC (incoming
  message signaled interrupt controller) to handle both wired interrupts and
  MSIs. When not specified, this option is assumed to be "none" which selects
  SiFive PLIC to handle wired interrupts.

  This option also interacts with '-accel kvm'.  When using "aia=aplic-imsic"
  with KVM, it is possible to set the use of the kernel irqchip in split mode
  by using "-accel kvm,kernel-irqchip=split".  In this case the ``virt`` machine
  will emulate the APLIC controller instead of using the APLIC controller from
  the irqchip.  See :ref:`riscv-aia` for more details on all available AIA
  modes.

- aia-guests=nnn

  The number of per-HART VS-level AIA IMSIC pages to be emulated for a guest
  having AIA IMSIC (i.e. "aia=aplic-imsic" selected). When not specified,
  the default number of per-HART VS-level AIA IMSIC pages is 0.

- iommu-sys=[on|off]

  Enables the riscv-iommu-sys platform device. Defaults to 'off'.

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

Enabling TPM
------------

A TPM device can be connected to the virt board by following the steps below.

First launch the TPM emulator:

.. code-block:: bash

  $ swtpm socket --tpm2 -t -d --tpmstate dir=/tmp/tpm \
        --ctrl type=unixio,path=swtpm-sock

Then launch QEMU with some additional arguments to link a TPM device to the backend:

.. code-block:: bash

  $ qemu-system-riscv64 \
    ... other args .... \
    -chardev socket,id=chrtpm,path=swtpm-sock \
    -tpmdev emulator,id=tpm0,chardev=chrtpm \
    -device tpm-tis-device,tpmdev=tpm0

The TPM device can be seen in the memory tree and the generated device
tree and should be accessible from the guest software.
