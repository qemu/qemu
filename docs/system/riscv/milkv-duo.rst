Milk-V Duo board (``milkv-duo``)
================================

The ``milkv-duo`` machine is compatible with the Milk-V Duo development board.

The Milk-V Duo is an ultra-compact embedded development platform based on the
Sophgo CV1800B SoC. The CV1800B features T-Head C906 RISC-V processing cores
along with integrated peripherals.

For more information, see <https://milkv.io/duo>

Supported devices
-----------------
The ``milkv-duo`` machine supports the following devices:

* T-Head C906 cores
* Core Local Interruptor (CLINT)
* Platform-Level Interrupt Controller (PLIC)
* DesignWare APB UART (dw8250)
* SDHCI controller
* CV1800B clock controller

Boot options
------------
The ``milkv-duo`` machine supports direct boot using a generic RISC-V OpenSBI
firmware and a Linux kernel. The root filesystem can be provided via the
emulated SD card.

Running
-------
To boot the machine, you need a Linux kernel (built with CV1800B support), a
device tree blob (DTB), and a root filesystem image (e.g., built via BusyBox
or Buildroot).

Use the following command to boot the system directly to the Linux shell:

.. code-block:: bash

   $ qemu-system-riscv64 -M milkv-duo -nographic \
      -bios pc-bios/opensbi-riscv64-generic-fw_dynamic.bin \
      -dtb /path/to/linux/arch/riscv/boot/dts/sophgo/cv1800b-milkv-duo.dtb \
      -kernel /path/to/linux/arch/riscv/boot/Image \
      -drive file=/path/to/rootfs.ext4,format=raw,id=sd-card \
      -device sd-card,drive=sd-card \
      -append "console=ttyS0,115200 root=/dev/mmcblk0 rootwait earlycon"

This command boots the generic OpenSBI firmware, loads the Linux Image and the
specified DTB, and mounts the ext4 root filesystem from the emulated SD card.
The console output is routed to the ``ttyS0`` serial port.
