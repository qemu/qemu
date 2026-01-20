.. SPDX-License-Identifier: GPL-2.0-or-later

iommu-testdev — IOMMU test device for bare-metal testing
========================================================

Overview
--------
``iommu-testdev`` is a minimal, test-only PCI device designed to exercise
IOMMU translation (such as ARM SMMUv3) without requiring firmware or a guest
OS. Tests can populate IOMMU translation tables with known values and trigger
DMA operations that flow through the IOMMU translation path. It is **not** a
faithful PCIe endpoint and must be considered a QEMU-internal test vehicle.

Key Features
------------
* **Bare-metal IOMMU testing**: No guest kernel or firmware required
* **Configurable DMA attributes**: Supports address space configuration via
  MMIO registers
* **Deterministic verification**: Write-then-read DMA pattern with automatic
  result checking

Status
------
* Location: ``hw/misc/iommu-testdev.c``
* Header: ``include/hw/misc/iommu-testdev.h``
* Build guard: ``CONFIG_IOMMU_TESTDEV``

Device Interface
----------------
The device exposes a single PCI BAR0 with 32-bit MMIO registers:

* ``ITD_REG_DMA_TRIGGERING`` (0x00): Read triggers DMA and consumes
  the armed request
* ``ITD_REG_DMA_GVA_LO`` (0x04): DMA IOVA bits [31:0]
* ``ITD_REG_DMA_GVA_HI`` (0x08): DMA IOVA bits [63:32]
* ``ITD_REG_DMA_GPA_LO`` (0x1C): DMA GPA bits [31:0] for readback validation
* ``ITD_REG_DMA_GPA_HI`` (0x20): DMA GPA bits [63:32] for readback validation
* ``ITD_REG_DMA_LEN`` (0x0C): DMA transfer length
* ``ITD_REG_DMA_RESULT`` (0x10): DMA result
  (0=success, 0xffffffff=idle, 0xfffffffe=armed)
* ``ITD_REG_DMA_DBELL`` (0x14): Write 1 to arm DMA, write 0 to disarm.
  Arming only marks the request and sets BUSY (no latch/check), but it
  provides an explicit gate for qtests and leaves room for async/latching.
* ``ITD_REG_DMA_ATTRS`` (0x18): DMA attributes which shadow some fields in
  MemTxAttrs:

  - bit[0]: secure (1=Secure, 0=Non-Secure)
  - bits[2:1]: ArmSecuritySpace (0=Secure, 1=Non-Secure)
  - bit[3]: space_valid (1=space is valid, 0=ignore space and default to Non-Secure)
    ``space`` field in MemTxAttrs is consumed only when ``space_valid`` is set.
    For Secure/Non-Secure, ``secure`` and ``space`` must match; mismatches
    return ``ITD_DMA_ERR_BAD_ATTRS``. Other bits are reserved but can be wired
    up easily if future tests need to pass extra attributes.

Translation Setup Workflow
--------------------------
``iommu-testdev`` never builds SMMU/AMD-Vi/RISC-V IOMMU structures on its own.
Architecture-specific construction lives entirely in qtest/libqos helpers.
Those helpers populate guest memory with page tables/architecture-specific
structures and program the emulated IOMMU registers directly. See the
``qsmmu_setup_and_enable_translation()`` function in
``tests/qtest/libqos/qos-smmuv3.c`` for an example of how SMMUv3 translation
is set up for this device.

DMA Operation Flow
------------------
Arming semantics:

* Writing ``DMA_DBELL`` with bit0=1 marks the request armed and sets
  ``DMA_RESULT`` to BUSY. It does not latch GVA/LEN/ATTRS; values are sampled
  when ``DMA_TRIGGERING`` is read.
* Writing ``DMA_DBELL`` with bit0=0 disarms the request and sets
  ``DMA_RESULT`` to IDLE.
* Reading ``DMA_TRIGGERING`` consumes the armed request and clears the armed
  state, even on error.

The flow would be split into these steps, mainly for timing control and
debuggability: qtests can easily exercise and assert distinct paths
(NOT_ARMED, BAD_LEN, TX/RD failures, mismatch) instead of having all side
effects hidden behind a single step:
1. Test programs IOMMU translation tables
2. Test configures DMA IOVA (GVA_LO/HI), GPA for readback, length, and attributes
3. Test writes 1 to DMA_DBELL to arm the operation
4. Test reads DMA_TRIGGERING to execute DMA
5. Test polls DMA_RESULT:

   - 0x00000000: Success
   - 0xFFFFFFFE: Armed (waiting for trigger). DMA runs synchronously, so
     BUSY is not observed once the trigger read completes.
   - 0xDEAD0006: Bad attrs (secure/space mismatch for S/NS)
   - 0xDEAD000X: Various error codes

The device performs a write-then-read sequence using a known pattern
(0x12345678) and verifies data integrity automatically.

Running the qtest
-----------------
The SMMUv3 test suite uses this device and covers multiple translation modes::

    cd build
    QTEST_QEMU_BINARY=./qemu-system-aarch64 \\
        ./tests/qtest/iommu-smmuv3-test --tap -k

This test suite exercises:

* Stage 1 only translation
* Stage 2 only translation
* Nested (Stage 1 + Stage 2) translation

Instantiation
-------------
The device is not wired into any board by default. Tests instantiate it
via QEMU command line::

    -device iommu-testdev

For ARM platforms with SMMUv3::

    -M virt,iommu=smmuv3 -device iommu-testdev

When the IOMMU sits on the same PCI root complex (``pci.0``), the device is
placed behind it automatically. For other PCI topologies, specify the bus
explicitly.

Limitations
-----------
* No realistic PCIe enumeration, MSI/MSI-X, or interrupt handling
* No ATS/PRI support
* No actual device functionality beyond DMA test pattern
* Test-only; not suitable for production or machine realism
* Address space support (Secure/Root/Realm) is architecture-dependent and
  gated by ``space_valid``
* Readback uses the programmed GPA and reads via system memory, avoiding a
  second IOMMU access for the readback step

See also
--------
* ``tests/qtest/iommu-smmuv3-test.c`` — SMMUv3 test suite
* ``tests/qtest/libqos/qos-smmuv3.{c,h}`` — SMMUv3 test library
* SMMUv3 emulation: ``hw/arm/smmu*``
