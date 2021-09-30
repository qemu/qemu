Xilinx Versal Virt (``xlnx-versal-virt``)
=========================================

Xilinx Versal is a family of heterogeneous multi-core SoCs
(System on Chip) that combine traditional hardened CPUs and I/O
peripherals in a Processing System (PS) with runtime programmable
FPGA logic (PL) and an Artificial Intelligence Engine (AIE).

More details here:
https://www.xilinx.com/products/silicon-devices/acap/versal.html

The family of Versal SoCs share a single architecture but come in
different parts with different speed grades, amounts of PL and
other differences.

The Xilinx Versal Virt board in QEMU is a model of a virtual board
(does not exist in reality) with a virtual Versal SoC without I/O
limitations. Currently, we support the following cores and devices:

Implemented CPU cores:

- 2 ACPUs (ARM Cortex-A72)

Implemented devices:

- Interrupt controller (ARM GICv3)
- 2 UARTs (ARM PL011)
- An RTC (Versal built-in)
- 2 GEMs (Cadence MACB Ethernet MACs)
- 8 ADMA (Xilinx zDMA) channels
- 2 SD Controllers
- OCM (256KB of On Chip Memory)
- XRAM (4MB of on chip Accelerator RAM)
- DDR memory
- BBRAM (36 bytes of Battery-backed RAM)
- eFUSE (3072 bytes of one-time field-programmable bit array)

QEMU does not yet model any other devices, including the PL and the AI Engine.

Other differences between the hardware and the QEMU model:

- QEMU allows the amount of DDR memory provided to be specified with the
  ``-m`` argument. If a DTB is provided on the command line then QEMU will
  edit it to include suitable entries describing the Versal DDR memory ranges.

- QEMU provides 8 virtio-mmio virtio transports; these start at
  address ``0xa0000000`` and have IRQs from 111 and upwards.

Running
"""""""
If the user provides an Operating System to be loaded, we expect users
to use the ``-kernel`` command line option.

Users can load firmware or boot-loaders with the ``-device loader`` options.

When loading an OS, QEMU generates a DTB and selects an appropriate address
where it gets loaded. This DTB will be passed to the kernel in register x0.

If there's no ``-kernel`` option, we generate a DTB and place it at 0x1000
for boot-loaders or firmware to pick it up.

If users want to provide their own DTB, they can use the ``-dtb`` option.
These DTBs will have their memory nodes modified to match QEMU's
selected ram_size option before they get passed to the kernel or FW.

When loading an OS, we turn on QEMU's PSCI implementation with SMC
as the PSCI conduit. When there's no ``-kernel`` option, we assume the user
provides EL3 firmware to handle PSCI.

A few examples:

Direct Linux boot of a generic ARM64 upstream Linux kernel:

.. code-block:: bash

  $ qemu-system-aarch64 -M xlnx-versal-virt -m 2G \
      -serial mon:stdio -display none \
      -kernel arch/arm64/boot/Image \
      -nic user -nic user \
      -device virtio-rng-device,bus=virtio-mmio-bus.0 \
      -drive if=none,index=0,file=hd0.qcow2,id=hd0,snapshot \
      -drive file=qemu_sd.qcow2,if=sd,index=0,snapshot \
      -device virtio-blk-device,drive=hd0 -append root=/dev/vda

Direct Linux boot of PetaLinux 2019.2:

.. code-block:: bash

  $ qemu-system-aarch64  -M xlnx-versal-virt -m 2G \
      -serial mon:stdio -display none \
      -kernel petalinux-v2019.2/Image \
      -append "rdinit=/sbin/init console=ttyAMA0,115200n8 earlycon=pl011,mmio,0xFF000000,115200n8" \
      -net nic,model=cadence_gem,netdev=net0 -netdev user,id=net0 \
      -device virtio-rng-device,bus=virtio-mmio-bus.0,rng=rng0 \
      -object rng-random,filename=/dev/urandom,id=rng0

Boot PetaLinux 2019.2 via ARM Trusted Firmware (2018.3 because the 2019.2
version of ATF tries to configure the CCI which we don't model) and U-boot:

.. code-block:: bash

  $ qemu-system-aarch64 -M xlnx-versal-virt -m 2G \
      -serial stdio -display none \
      -device loader,file=petalinux-v2018.3/bl31.elf,cpu-num=0 \
      -device loader,file=petalinux-v2019.2/u-boot.elf \
      -device loader,addr=0x20000000,file=petalinux-v2019.2/Image \
      -nic user -nic user \
      -device virtio-rng-device,bus=virtio-mmio-bus.0,rng=rng0 \
      -object rng-random,filename=/dev/urandom,id=rng0

Run the following at the U-Boot prompt:

.. code-block:: bash

  Versal>
  fdt addr $fdtcontroladdr
  fdt move $fdtcontroladdr 0x40000000
  fdt set /timer clock-frequency <0x3dfd240>
  setenv bootargs "rdinit=/sbin/init maxcpus=1 console=ttyAMA0,115200n8 earlycon=pl011,mmio,0xFF000000,115200n8"
  booti 20000000 - 40000000
  fdt addr $fdtcontroladdr

Boot Linux as DOM0 on Xen via U-Boot:

.. code-block:: bash

  $ qemu-system-aarch64 -M xlnx-versal-virt -m 4G \
      -serial stdio -display none \
      -device loader,file=petalinux-v2019.2/u-boot.elf,cpu-num=0 \
      -device loader,addr=0x30000000,file=linux/2018-04-24/xen \
      -device loader,addr=0x40000000,file=petalinux-v2019.2/Image \
      -nic user -nic user \
      -device virtio-rng-device,bus=virtio-mmio-bus.0,rng=rng0 \
      -object rng-random,filename=/dev/urandom,id=rng0

Run the following at the U-Boot prompt:

.. code-block:: bash

  Versal>
  fdt addr $fdtcontroladdr
  fdt move $fdtcontroladdr 0x20000000
  fdt set /timer clock-frequency <0x3dfd240>
  fdt set /chosen xen,xen-bootargs "console=dtuart dtuart=/uart@ff000000 dom0_mem=640M bootscrub=0 maxcpus=1 timer_slop=0"
  fdt set /chosen xen,dom0-bootargs "rdinit=/sbin/init clk_ignore_unused console=hvc0 maxcpus=1"
  fdt mknode /chosen dom0
  fdt set /chosen/dom0 compatible "xen,multiboot-module"
  fdt set /chosen/dom0 reg <0x00000000 0x40000000 0x0 0x03100000>
  booti 30000000 - 20000000

Boot Linux as Dom0 on Xen via ARM Trusted Firmware and U-Boot:

.. code-block:: bash

  $ qemu-system-aarch64 -M xlnx-versal-virt -m 4G \
      -serial stdio -display none \
      -device loader,file=petalinux-v2018.3/bl31.elf,cpu-num=0 \
      -device loader,file=petalinux-v2019.2/u-boot.elf \
      -device loader,addr=0x30000000,file=linux/2018-04-24/xen \
      -device loader,addr=0x40000000,file=petalinux-v2019.2/Image \
      -nic user -nic user \
      -device virtio-rng-device,bus=virtio-mmio-bus.0,rng=rng0 \
      -object rng-random,filename=/dev/urandom,id=rng0

Run the following at the U-Boot prompt:

.. code-block:: bash

  Versal>
  fdt addr $fdtcontroladdr
  fdt move $fdtcontroladdr 0x20000000
  fdt set /timer clock-frequency <0x3dfd240>
  fdt set /chosen xen,xen-bootargs "console=dtuart dtuart=/uart@ff000000 dom0_mem=640M bootscrub=0 maxcpus=1 timer_slop=0"
  fdt set /chosen xen,dom0-bootargs "rdinit=/sbin/init clk_ignore_unused console=hvc0 maxcpus=1"
  fdt mknode /chosen dom0
  fdt set /chosen/dom0 compatible "xen,multiboot-module"
  fdt set /chosen/dom0 reg <0x00000000 0x40000000 0x0 0x03100000>
  booti 30000000 - 20000000

BBRAM File Backend
""""""""""""""""""
BBRAM can have an optional file backend, which must be a seekable
binary file with a size of 36 bytes or larger. A file with all
binary 0s is a 'blank'.

To add a file-backend for the BBRAM:

.. code-block:: bash

  -drive if=pflash,index=0,file=versal-bbram.bin,format=raw

To use a different index value, N, from default of 0, add:

.. code-block:: bash

  -global xlnx,bbram-ctrl.drive-index=N

eFUSE File Backend
""""""""""""""""""
eFUSE can have an optional file backend, which must be a seekable
binary file with a size of 3072 bytes or larger. A file with all
binary 0s is a 'blank'.

To add a file-backend for the eFUSE:

.. code-block:: bash

  -drive if=pflash,index=1,file=versal-efuse.bin,format=raw

To use a different index value, N, from default of 1, add:

.. code-block:: bash

  -global xlnx,efuse.drive-index=N

.. warning::
  In actual physical Versal, BBRAM and eFUSE contain sensitive data.
  The QEMU device models do **not** encrypt nor obfuscate any data
  when holding them in models' memory or when writing them to their
  file backends.

  Thus, a file backend should be used with caution, and 'format=luks'
  is highly recommended (albeit with usage complexity).

  Better yet, do not use actual product data when running guest image
  on this Xilinx Versal Virt board.
