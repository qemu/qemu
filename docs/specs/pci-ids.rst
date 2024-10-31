================
PCI IDs for QEMU
================

Red Hat, Inc. donates a part of its device ID range to QEMU, to be used for
virtual devices.  The vendor IDs are 1af4 (formerly Qumranet ID) and 1b36.

Contact Gerd Hoffmann <kraxel@redhat.com> to get a device ID assigned
for your devices.

1af4 vendor ID
--------------

The 1000 -> 10ff device ID range is used as follows for virtio-pci devices.
Note that this allocation is separate from the virtio device IDs, which are
maintained as part of the virtio specification.

1af4:1000
  network device (legacy)
1af4:1001
  block device (legacy)
1af4:1002
  balloon device (legacy)
1af4:1003
  console device (legacy)
1af4:1004
  SCSI host bus adapter device (legacy)
1af4:1005
  entropy generator device (legacy)
1af4:1009
  9p filesystem device (legacy)
1af4:1012
  vsock device (bug compatibility)

1af4:1040 to 1af4:10ef
  ID range for modern virtio devices.  The PCI device
  ID is calculated from the virtio device ID by adding the
  0x1040 offset.  The virtio IDs are defined in the virtio
  specification.  The Linux kernel has a header file with
  defines for all virtio IDs (``linux/virtio_ids.h``); QEMU has a
  copy in ``include/standard-headers/``.

1af4:10f0 to 1a4f:10ff
  Available for experimental usage without registration.  Must get
  official ID when the code leaves the test lab (i.e. when seeking
  upstream merge or shipping a distro/product) to avoid conflicts.

1af4:1100
  Used as PCI Subsystem ID for existing hardware devices emulated
  by QEMU.

1af4:1110
  ivshmem device (:doc:`ivshmem-spec`)

All other device IDs are reserved.

1b36 vendor ID
--------------

The 0000 -> 00ff device ID range is used as follows for QEMU-specific
PCI devices (other than virtio):

1b36:0001
  PCI-PCI bridge
1b36:0002
  PCI serial port (16550A) adapter (:doc:`pci-serial`)
1b36:0003
  PCI Dual-port 16550A adapter (:doc:`pci-serial`)
1b36:0004
  PCI Quad-port 16550A adapter (:doc:`pci-serial`)
1b36:0005
  PCI test device (:doc:`pci-testdev`)
1b36:0006
  PCI Rocker Ethernet switch device
1b36:0007
  PCI SD Card Host Controller Interface (SDHCI)
1b36:0008
  PCIe host bridge
1b36:0009
  PCI Expander Bridge (``-device pxb``)
1b36:000a
  PCI-PCI bridge (multiseat)
1b36:000b
  PCIe Expander Bridge (``-device pxb-pcie``)
1b36:000c
  PCIe Root Port (``-device pcie-root-port``)
1b36:000d
  PCI xhci usb host adapter
1b36:000e
  PCIe-to-PCI bridge (``-device pcie-pci-bridge``)
1b36:000f
  mdpy (mdev sample device), ``linux/samples/vfio-mdev/mdpy.c``
1b36:0010
  PCIe NVMe device (``-device nvme``)
1b36:0011
  PCI PVPanic device (``-device pvpanic-pci``)
1b36:0012
  PCI ACPI ERST device (``-device acpi-erst``)
1b36:0013
  PCI UFS device (``-device ufs``)
1b36:0014
  PCI RISC-V IOMMU device

All these devices are documented in :doc:`index`.

The 0100 device ID is used for the QXL video card device.
