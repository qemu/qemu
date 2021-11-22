Shakti C Reference Platform (``shakti_c``)
==========================================

Shakti C Reference Platform is a reference platform based on arty a7 100t
for the Shakti SoC.

Shakti SoC is a SoC based on the Shakti C-class processor core. Shakti C
is a 64bit RV64GCSUN processor core.

For more details on Shakti SoC, please see:
https://gitlab.com/shaktiproject/cores/shakti-soc/-/blob/master/fpga/boards/artya7-100t/c-class/README.rst

For more info on the Shakti C-class core, please see:
https://c-class.readthedocs.io/en/latest/

Supported devices
-----------------

The ``shakti_c`` machine supports the following devices:

 * 1 C-class core
 * Core Level Interruptor (CLINT)
 * Platform-Level Interrupt Controller (PLIC)
 * 1 UART

Boot options
------------

The ``shakti_c`` machine can start using the standard -bios
functionality for loading the baremetal application or opensbi.

Boot the machine
----------------

Shakti SDK
~~~~~~~~~~
Shakti SDK can be used to generate the baremetal example UART applications.

.. code-block:: bash

   $ git clone https://gitlab.com/behindbytes/shakti-sdk.git
   $ cd shakti-sdk
   $ make software PROGRAM=loopback TARGET=artix7_100t

Binary would be generated in:
  software/examples/uart_applns/loopback/output/loopback.shakti

You could also download the precompiled example applications using below
commands.

.. code-block:: bash

   $ wget -c https://gitlab.com/behindbytes/shakti-binaries/-/raw/master/sdk/shakti_sdk_qemu.zip
   $ unzip shakti_sdk_qemu.zip

Then we can run the UART example using:

.. code-block:: bash

   $ qemu-system-riscv64 -M shakti_c -nographic \
      -bios path/to/shakti_sdk_qemu/loopback.shakti

OpenSBI
~~~~~~~
We can also run OpenSBI with Test Payload.

.. code-block:: bash

   $ git clone https://github.com/riscv/opensbi.git -b v0.9
   $ cd opensbi
   $ wget -c https://gitlab.com/behindbytes/shakti-binaries/-/raw/master/dts/shakti.dtb
   $ export CROSS_COMPILE=riscv64-unknown-elf-
   $ export FW_FDT_PATH=./shakti.dtb
   $ make PLATFORM=generic

fw_payload.elf would be generated in build/platform/generic/firmware/fw_payload.elf.
Boot it using the below qemu command.

.. code-block:: bash

   $ qemu-system-riscv64 -M shakti_c -nographic \
      -bios path/to/fw_payload.elf
