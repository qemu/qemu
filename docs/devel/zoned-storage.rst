=============
zoned-storage
=============

Zoned Block Devices (ZBDs) divide the LBA space into block regions called zones
that are larger than the LBA size. They can only allow sequential writes, which
can reduce write amplification in SSDs, and potentially lead to higher
throughput and increased capacity. More details about ZBDs can be found at:

https://zonedstorage.io/docs/introduction/zoned-storage

1. Block layer APIs for zoned storage
-------------------------------------
QEMU block layer supports three zoned storage models:
- BLK_Z_HM: The host-managed zoned model only allows sequential writes access
to zones. It supports ZBD-specific I/O commands that can be used by a host to
manage the zones of a device.
- BLK_Z_HA: The host-aware zoned model allows random write operations in
zones, making it backward compatible with regular block devices.
- BLK_Z_NONE: The non-zoned model has no zones support. It includes both
regular and drive-managed ZBD devices. ZBD-specific I/O commands are not
supported.

The block device information resides inside BlockDriverState. QEMU uses
BlockLimits struct(BlockDriverState::bl) that is continuously accessed by the
block layer while processing I/O requests. A BlockBackend has a root pointer to
a BlockDriverState graph(for example, raw format on top of file-posix). The
zoned storage information can be propagated from the leaf BlockDriverState all
the way up to the BlockBackend. If the zoned storage model in file-posix is
set to BLK_Z_HM, then block drivers will declare support for zoned host device.

The block layer APIs support commands needed for zoned storage devices,
including report zones, four zone operations, and zone append.

2. Emulating zoned storage controllers
--------------------------------------
When the BlockBackend's BlockLimits model reports a zoned storage device, users
like the virtio-blk emulation or the qemu-io-cmds.c utility can use block layer
APIs for zoned storage emulation or testing.

For example, to test zone_report on a null_blk device using qemu-io is::

  $ path/to/qemu-io --image-opts -n driver=host_device,filename=/dev/nullb0 -c "zrp offset nr_zones"

To expose the host's zoned block device through virtio-blk, the command line
can be (includes the -device parameter)::

  -blockdev node-name=drive0,driver=host_device,filename=/dev/nullb0,cache.direct=on \
  -device virtio-blk-pci,drive=drive0

Or only use the -drive parameter::

  -driver driver=host_device,file=/dev/nullb0,if=virtio,cache.direct=on

Additionally, QEMU has several ways of supporting zoned storage, including:
(1) Using virtio-scsi: --device scsi-block allows for the passing through of
SCSI ZBC devices, enabling the attachment of ZBC or ZAC HDDs to QEMU.
(2) PCI device pass-through: While NVMe ZNS emulation is available for testing
purposes, it cannot yet pass through a zoned device from the host. To pass on
the NVMe ZNS device to the guest, use VFIO PCI pass the entire NVMe PCI adapter
through to the guest. Likewise, an HDD HBA can be passed on to QEMU all HDDs
attached to the HBA.
