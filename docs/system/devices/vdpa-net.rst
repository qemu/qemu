vdpa net
============

This document explains the setup and usage of the vdpa network device.
The vdpa network device is a paravirtualized vdpa emulate device.

Description
-----------

VDPA net devices support dirty page bitmap mark and vring state saving and recovery.

Users can use this VDPA device for live migration simulation testing in a nested virtualization environment.

Registers layout
----------------

The vdpa device add live migrate registers layout as follow::

  Offset       Register Name	        Bitwidth     Associated vq
  0x0          LM_LOGGING_CTRL          4bits
  0x10         LM_BASE_ADDR_LOW         32bits
  0x14         LM_BASE_ADDR_HIGH        32bits
  0x18         LM_END_ADDR_LOW          32bits
  0x1c         LM_END_ADDR_HIGH         32bits
  0x20         LM_RING_STATE_OFFSET	32bits       vq0
  0x24         LM_RING_STATE_OFFSET	32bits       vq1
  0x28         LM_RING_STATE_OFFSET	32bits       vq2
  ......
  0x20+1023*4  LM_RING_STATE_OFFSET     32bits       vq1023

These registers are extended at the end of the notify bar space.

Architecture diagram
--------------------
::

  |------------------------------------------------------------------------|
  | guest-L1-user-space                                                    |
  |                                                                        |
  |                               |----------------------------------------|
  |                               |       [virtio-net driver]              |
  |                               |              ^  guest-L2-src(iommu=on) |
  |                               |--------------|-------------------------|
  |                               |              |  qemu-L2-src(viommu)    |
  | [dpdk-vdpa]<->[vhost socket]<-+->[vhost-user backend(iommu=on)]        |
  --------------------------------------------------------------------------
  --------------------------------------------------------------------------
  |       ^                             guest-L1-kernel-space              |
  |       |                                                                |
  |    [VFIO]                                                              |
  |       ^                                                                |
  |       |                             guest-L1-src(iommu=on)             |
  --------|-----------------------------------------------------------------
  --------|-----------------------------------------------------------------
  | [vdpa net device(iommu=on)]        [manager nic device]                |
  |          |                                    |                        |
  |          |                                    |                        |
  |     [tap device]     qemu-L1-src(viommu)      |                        |
  ------------------------------------------------+-------------------------
                                                  |
                                                  |
                        ---------------------     |
                        | kernel net bridge |<-----
                        |     virbr0        |<----------------------------------
                        ---------------------                                  |
                                                                               |
                                                                               |
  --------------------------------------------------------------------------   |
  | guest-L1-user-space                                                    |   |
  |                                                                        |   |
  |                               |----------------------------------------|   |
  |                               |       [virtio-net driver]              |   |
  |                               |              ^  guest-L2-dst(iommu=on) |   |
  |                               |--------------|-------------------------|   |
  |                               |              |  qemu-L2-dst(viommu)    |   |
  | [dpdk-vdpa]<->[vhost socket]<-+->[vhost-user backend(iommu=on)]        |   |
  --------------------------------------------------------------------------   |
  --------------------------------------------------------------------------   |
  |       ^                             guest-L1-kernel-space              |   |
  |       |                                                                |   |
  |    [VFIO]                                                              |   |
  |       ^                                                                |   |
  |       |                             guest-L1-dst(iommu=on)             |   |
  --------|-----------------------------------------------------------------   |
  --------|-----------------------------------------------------------------   |
  | [vdpa net device(iommu=on)]        [manager nic device]----------------+----
  |          |                                                             |
  |          |                                                             |
  |     [tap device]     qemu-L1-dst(viommu)                               |
  --------------------------------------------------------------------------


Device properties
-----------------

The Virtio vdpa device can be configured with the following properties:

 * ``vdpa=on`` open vdpa device emulated.

Usages
--------
This patch add virtio sriov support and vdpa live migrate support.
You can open vdpa by set xml file as follow::

  <qemu:commandline  xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>
  <qemu:arg value='-device'/>
  <qemu:arg value='intel-iommu,intremap=on,device-iotlb=on,aw-bits=48'/>
  <qemu:arg value='-netdev'/>
  <qemu:arg value='tap,id=hostnet1,script=no,downscript=no,vhost=off'/>
  <qemu:arg value='-device'/>
  <qemu:arg value='virtio-net-pci,netdev=hostnet1,id=net1,mac=56:4a:b7:4f:4d:a9,bus=pci.6,addr=0x0,iommu_platform=on,ats=on,vdpa=on'/>
  </qemu:commandline>

Limitations
-----------
1. Dependent on tap device with param ``vhost=off``.
2. Nested virtualization environment only supports ``q35`` machines.
3. Current only support split vring live migrate.



