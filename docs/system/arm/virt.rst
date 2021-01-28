'virt' generic virtual platform (``virt``)
==========================================

The `virt` board is a platform which does not correspond to any
real hardware; it is designed for use in virtual machines.
It is the recommended board type if you simply want to run
a guest such as Linux and do not care about reproducing the
idiosyncrasies and limitations of a particular bit of real-world
hardware.

This is a "versioned" board model, so as well as the ``virt`` machine
type itself (which may have improvements, bugfixes and other minor
changes between QEMU versions) a version is provided that guarantees
to have the same behaviour as that of previous QEMU releases, so
that VM migration will work between QEMU versions. For instance the
``virt-5.0`` machine type will behave like the ``virt`` machine from
the QEMU 5.0 release, and migration should work between ``virt-5.0``
of the 5.0 release and ``virt-5.0`` of the 5.1 release. Migration
is not guaranteed to work between different QEMU releases for
the non-versioned ``virt`` machine type.

Supported devices
"""""""""""""""""

The virt board supports:

- PCI/PCIe devices
- Flash memory
- One PL011 UART
- An RTC
- The fw_cfg device that allows a guest to obtain data from QEMU
- A PL061 GPIO controller
- An optional SMMUv3 IOMMU
- hotpluggable DIMMs
- hotpluggable NVDIMMs
- An MSI controller (GICv2M or ITS). GICv2M is selected by default along
  with GICv2. ITS is selected by default with GICv3 (>= virt-2.7). Note
  that ITS is not modeled in TCG mode.
- 32 virtio-mmio transport devices
- running guests using the KVM accelerator on aarch64 hardware
- large amounts of RAM (at least 255GB, and more if using highmem)
- many CPUs (up to 512 if using a GICv3 and highmem)
- Secure-World-only devices if the CPU has TrustZone:

  - A second PL011 UART
  - A second PL061 GPIO controller, with GPIO lines for triggering
    a system reset or system poweroff
  - A secure flash memory
  - 16MB of secure RAM

Supported guest CPU types:

- ``cortex-a7`` (32-bit)
- ``cortex-a15`` (32-bit; the default)
- ``cortex-a53`` (64-bit)
- ``cortex-a57`` (64-bit)
- ``cortex-a72`` (64-bit)
- ``host`` (with KVM only)
- ``max`` (same as ``host`` for KVM; best possible emulation with TCG)

Note that the default is ``cortex-a15``, so for an AArch64 guest you must
specify a CPU type.

Graphics output is available, but unlike the x86 PC machine types
there is no default display device enabled: you should select one from
the Display devices section of "-device help". The recommended option
is ``virtio-gpu-pci``; this is the only one which will work correctly
with KVM. You may also need to ensure your guest kernel is configured
with support for this; see below.

Machine-specific options
""""""""""""""""""""""""

The following machine-specific options are supported:

secure
  Set ``on``/``off`` to enable/disable emulating a guest CPU which implements the
  Arm Security Extensions (TrustZone). The default is ``off``.

virtualization
  Set ``on``/``off`` to enable/disable emulating a guest CPU which implements the
  Arm Virtualization Extensions. The default is ``off``.

mte
  Set ``on``/``off`` to enable/disable emulating a guest CPU which implements the
  Arm Memory Tagging Extensions. The default is ``off``.

highmem
  Set ``on``/``off`` to enable/disable placing devices and RAM in physical
  address space above 32 bits. The default is ``on`` for machine types
  later than ``virt-2.12``.

gic-version
  Specify the version of the Generic Interrupt Controller (GIC) to provide.
  Valid values are:

  ``2``
    GICv2
  ``3``
    GICv3
  ``host``
    Use the same GIC version the host provides, when using KVM
  ``max``
    Use the best GIC version possible (same as host when using KVM;
    currently same as ``3``` for TCG, but this may change in future)

its
  Set ``on``/``off`` to enable/disable ITS instantiation. The default is ``on``
  for machine types later than ``virt-2.7``.

iommu
  Set the IOMMU type to create for the guest. Valid values are:

  ``none``
    Don't create an IOMMU (the default)
  ``smmuv3``
    Create an SMMUv3

ras
  Set ``on``/``off`` to enable/disable reporting host memory errors to a guest
  using ACPI and guest external abort exceptions. The default is off.

Linux guest kernel configuration
""""""""""""""""""""""""""""""""

The 'defconfig' for Linux arm and arm64 kernels should include the
right device drivers for virtio and the PCI controller; however some older
kernel versions, especially for 32-bit Arm, did not have everything
enabled by default. If you're not seeing PCI devices that you expect,
then check that your guest config has::

  CONFIG_PCI=y
  CONFIG_VIRTIO_PCI=y
  CONFIG_PCI_HOST_GENERIC=y

If you want to use the ``virtio-gpu-pci`` graphics device you will also
need::

  CONFIG_DRM=y
  CONFIG_DRM_VIRTIO_GPU=y

Hardware configuration information for bare-metal programming
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

The ``virt`` board automatically generates a device tree blob ("dtb")
which it passes to the guest. This provides information about the
addresses, interrupt lines and other configuration of the various devices
in the system. Guest code can rely on and hard-code the following
addresses:

- Flash memory starts at address 0x0000_0000

- RAM starts at 0x4000_0000

All other information about device locations may change between
QEMU versions, so guest code must look in the DTB.

QEMU supports two types of guest image boot for ``virt``, and
the way for the guest code to locate the dtb binary differs:

- For guests using the Linux kernel boot protocol (this means any
  non-ELF file passed to the QEMU ``-kernel`` option) the address
  of the DTB is passed in a register (``r2`` for 32-bit guests,
  or ``x0`` for 64-bit guests)

- For guests booting as "bare-metal" (any other kind of boot),
  the DTB is at the start of RAM (0x4000_0000)
