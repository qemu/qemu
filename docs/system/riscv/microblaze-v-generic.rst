Microblaze-V generic board (``amd-microblaze-v-generic``)
=========================================================
The AMD MicroBlaze™ V processor is a soft-core RISC-V processor IP for AMD
adaptive SoCs and FPGAs. The MicroBlaze™ V processor is based on the 32-bit (or
64-bit) RISC-V instruction set architecture (ISA) and contains interfaces
compatible with the classic MicroBlaze™ V processor (i.e it is a drop in
replacement for the classic MicroBlaze™ processor in existing RTL designs).
More information can be found in below document.

https://docs.amd.com/r/en-US/ug1629-microblaze-v-user-guide/MicroBlaze-V-Architecture

The MicroBlaze™ V generic board in QEMU has following supported devices:

    - timer
    - uartlite
    - uart16550
    - emaclite
    - timer2
    - axi emac
    - axi dma

The MicroBlaze™ V core in QEMU has the following configuration:

    - RV32I base integer instruction set
    - "Zicsr" Control and Status register instructions
    - "Zifencei" instruction-fetch
    - Extensions: m, a, f, c

Running
"""""""
Below is an example command line for launching mainline U-boot
(xilinx_mbv32_defconfig) on the Microblaze-V generic board.

.. code-block:: bash

   $ qemu-system-riscv32 -M amd-microblaze-v-generic \
     -display none \
     -device loader,addr=0x80000000,file=u-boot-spl.bin,cpu-num=0 \
     -device loader,addr=0x80200000,file=u-boot.img \
     -serial mon:stdio \
     -device loader,addr=0x83000000,file=system.dtb \
     -m 2g
