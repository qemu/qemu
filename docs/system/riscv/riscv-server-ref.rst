.. SPDX-License-Identifier: GPL-2.0-or-later

RISC-V Server Platform Reference board (``riscv-server-ref``)
=============================================================

The RISC-V Server Platform specification `spec`_ defines a standardized
set of hardware and software capabilities that portable system software,
such as OS and hypervisors, can rely on being present in a RISC-V server
platform.  This machine aims to emulate this specification, providing
an environment for firmware/OS development and testing.

`spec`_ is version 1.0 at the introduction of this board.  For each new
supported spec version a new versioned board will be added, keeping
"riscv-server-ref" as an alias for the newest version supported.  E.g.
for spec version 2.0:

* riscv-server-ref-1.0 - board that complies with spec version 1.0
* riscv-server-ref-2.0 - board that complies with spec version 2.0
* riscv-server-ref - alias for riscv-server-ref-2.0

The main features included in the riscv-server-ref board are:

* IOMMU platform device (riscv-iommu-sys)
* AIA
* No AHCI (can be added via command line)
* No NIC (can be added via command line)
* No virtio mmio bus
* No fw_cfg device
* No ACPI table
* Minimal device tree nodes

There are multiple ways of using this reference board.  The spec compliant way
is using an EDK2 image and a TPM device.  The board was tested with the TPM
device ``tpm-tis`` that uses the external ``swtpm`` emulator.  More info on how
to use this device can be found in `tpm`_.

To use this board coupled with the tpm-tis device, first start the ``swtpm``
process in a shell (the ``log`` parameter is optional):

.. code-block:: bash

    $ mkdir /tmp/mytpm1
    $ swtpm socket --tpmstate dir=/tmp/mytpm1 \
            --ctrl type=unixio,path=/tmp/mytpm1/swtpm-sock \
            --tpm2 \
            --log level=20

And then start QEMU with:

.. code-block:: bash

 qemu-system-riscv64 -M riscv-server-ref \
     -bios fw_dynamic.bin \
     -kernel EDK2.fd \
     -drive file=nvme_disk.ext2,format=raw,id=hd0,if=none \
     -device nvme,drive=hd0,serial=ddaaddaa \
     -chardev socket,id=chrtpm,path=/tmp/mytpm1/swtpm-sock \
     -tpmdev emulator,id=tpm0,chardev=chrtpm \
     -device tpm-tis-device,tpmdev=tpm0 \
     -netdev user,id=net0 -device e1000e,netdev=net0 \
     -nographic


.. _spec: https://github.com/riscv-non-isa/riscv-server-platform
.. _tpm: https://qemu-project.gitlab.io/qemu/specs/tpm.html
