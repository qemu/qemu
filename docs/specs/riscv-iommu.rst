.. _riscv-iommu:

RISC-V IOMMU support for RISC-V machines
========================================

QEMU implements a RISC-V IOMMU emulation based on the RISC-V IOMMU spec
version 1.0 `iommu1.0.0`_.

The emulation includes a PCI reference device (riscv-iommu-pci) and a platform
bus device (riscv-iommu-sys) that QEMU RISC-V boards can use.  The 'virt'
RISC-V machine is compatible with both devices.

riscv-iommu-pci reference device
--------------------------------

This device implements the RISC-V IOMMU emulation as recommended by the section
"Integrating an IOMMU as a PCIe device" of `iommu1.0.0`_: a PCI device with base
class 08h, sub-class 06h and programming interface 00h.

As a reference device it doesn't implement anything outside of the specification,
so it uses a generic default PCI ID given by QEMU: 1b36:0014.

To include the device in the 'virt' machine:

.. code-block:: bash

  $ qemu-system-riscv64 -M virt -device riscv-iommu-pci,[optional_pci_opts] (...)

This will add a RISC-V IOMMU PCI device in the board following any additional
PCI parameters (like PCI bus address).  The behavior of the RISC-V IOMMU is
defined by the spec but its operation is OS dependent.

Linux kernel iommu support was merged in v6.13. QEMU IOMMU emulation can be
used with mainline kernels for simple IOMMU PCIe support.

As of v6.17, it does not have support for features like VFIO passthrough.
There is a `VFIO`_ RFC series that is not yet merged. The public Ventana Micro
Systems kernel repository in `ventana-linux`_ can be used for testing the VFIO
functions.

The v6.13+ Linux kernel support uses the IOMMU device to create IOMMU groups
with any eligible cards available in the system, regardless of factors such as the
order in which the devices are added in the command line.

This means that these command lines are equivalent as far as the current
IOMMU kernel driver behaves:

.. code-block:: bash

  $ qemu-system-riscv64 \
        -M virt,aia=aplic-imsic,aia-guests=5 \
        -device riscv-iommu-pci,addr=1.0 \
        -device e1000e,netdev=net1 -netdev user,id=net1,net=192.168.0.0/24 \
        -device e1000e,netdev=net2 -netdev user,id=net2,net=192.168.200.0/24 \
        (...)

  $ qemu-system-riscv64 \
        -M virt,aia=aplic-imsic,aia-guests=5 \
        -device e1000e,netdev=net1 -netdev user,id=net1,net=192.168.0.0/24 \
        -device e1000e,netdev=net2 -netdev user,id=net2,net=192.168.200.0/24 \
        -device riscv-iommu-pci,addr=3.0 \
        (...)

Both will create iommu groups for the two e1000e cards.

Several options are available to control the capabilities of the device, namely:

- "bus": the bus that the IOMMU device uses
- "ioatc-limit": size of the Address Translation Cache (default to 2Mb)
- "intremap": enable/disable MSI support
- "ats": enable ATS support
- "off" (Out-of-reset translation mode: 'on' for DMA disabled, 'off' for 'BARE' (passthrough))
- "s-stage": enable s-stage support
- "g-stage": enable g-stage support
- "hpm-counters": number of hardware performance counters available. Maximum value is 31.
  Default value is 31. Use 0 (zero) to disable HPM support
- "vendor-id"/"device-id": pci device ID. Defaults to 1b36:0014 (Redhat)

riscv-iommu-sys device
----------------------

This device implements the RISC-V IOMMU emulation as a platform bus device that
RISC-V boards can use.

For the 'virt' board the device is disabled by default.  To enable it use the
'iommu-sys' machine option:

.. code-block:: bash

  $ qemu-system-riscv64 -M virt,iommu-sys=on (...)

There is no options to configure the capabilities of this device in the 'virt'
board using the QEMU command line.  The device is configured with the following
riscv-iommu options:

- "ioatc-limit": default value (2Mb)
- "intremap": enabled
- "ats": enabled
- "off": on (DMA disabled)
- "s-stage": enabled
- "g-stage": enabled

.. _iommu1.0.0: https://github.com/riscv-non-isa/riscv-iommu/releases/download/v1.0.0/riscv-iommu.pdf

.. _VFIO: https://lore.kernel.org/linux-riscv/20241114161845.502027-17-ajones@ventanamicro.com/

.. _ventana-linux: https://github.com/ventanamicro/linux/tree/dev-upstream
