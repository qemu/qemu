Boards for RISC-V Processors by MIPS
====================================

RISC-V processors developed by MIPS support Boston-aia board model. The board
model supports up to 64 harts with MIPS CPS, MIPS GCR, MIPS CPC, AIA plic,
and AIA clint devices. The model can create boot code, if there is no
```-bios``` parameter. Also, we can specify ```-smp x,cores=y,thread=z```.

Running Linux kernel
--------------------

For example, to use 4 cores and 2 threads with each core to have 8 smp cpus,
that runs on the ```mips-p8700``` CPU, run qemu as follows:

.. code-block:: bash

   qemu-system-riscv64 -cpu mips-p8700 \
         -m 2G -M boston-aia \
         -smp 8,cores=4,threads=2 -kernel fw_payload.bin \
         -drive file=rootfs.ext2,format=raw -serial stdio
