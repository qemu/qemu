===============================
IOMMUFD BACKEND usage with VFIO
===============================

(Same meaning for backend/container/BE)

With the introduction of iommufd, the Linux kernel provides a generic
interface for user space drivers to propagate their DMA mappings to kernel
for assigned devices. While the legacy kernel interface is group-centric,
the new iommufd interface is device-centric, relying on device fd and iommufd.

To support both interfaces in the QEMU VFIO device, introduce a base container
to abstract the common part of VFIO legacy and iommufd container. So that the
generic VFIO code can use either container.

The base container implements generic functions such as memory_listener and
address space management whereas the derived container implements callbacks
specific to either legacy or iommufd. Each container has its own way to setup
secure context and dma management interface. The below diagram shows how it
looks like with both containers.

::

                      VFIO                           AddressSpace/Memory
      +-------+  +----------+  +-----+  +-----+
      |  pci  |  | platform |  |  ap |  | ccw |
      +---+---+  +----+-----+  +--+--+  +--+--+     +----------------------+
          |           |           |        |        |   AddressSpace       |
          |           |           |        |        +------------+---------+
      +---V-----------V-----------V--------V----+               /
      |           VFIOAddressSpace              | <------------+
      |                  |                      |  MemoryListener
      |        VFIOContainerBase list           |
      +-------+----------------------------+----+
              |                            |
              |                            |
      +-------V------+            +--------V----------+
      |   iommufd    |            |    vfio legacy    |
      |  container   |            |     container     |
      +-------+------+            +--------+----------+
              |                            |
              | /dev/iommu                 | /dev/vfio/vfio
              | /dev/vfio/devices/vfioX    | /dev/vfio/$group_id
  Userspace   |                            |
  ============+============================+===========================
  Kernel      |  device fd                 |
              +---------------+            | group/container fd
              | (BIND_IOMMUFD |            | (SET_CONTAINER/SET_IOMMU)
              |  ATTACH_IOAS) |            | device fd
              |               |            |
              |       +-------V------------V-----------------+
      iommufd |       |                vfio                  |
  (map/unmap  |       +---------+--------------------+-------+
  ioas_copy)  |                 |                    | map/unmap
              |                 |                    |
       +------V------+    +-----V------+      +------V--------+
       | iommfd core |    |  device    |      |  vfio iommu   |
       +-------------+    +------------+      +---------------+

* Secure Context setup

  - iommufd BE: uses device fd and iommufd to setup secure context
    (bind_iommufd, attach_ioas)
  - vfio legacy BE: uses group fd and container fd to setup secure context
    (set_container, set_iommu)

* Device access

  - iommufd BE: device fd is opened through ``/dev/vfio/devices/vfioX``
  - vfio legacy BE: device fd is retrieved from group fd ioctl

* DMA Mapping flow

  1. VFIOAddressSpace receives MemoryRegion add/del via MemoryListener
  2. VFIO populates DMA map/unmap via the container BEs
     * iommufd BE: uses iommufd
     * vfio legacy BE: uses container fd

Example configuration
=====================

Step 1: configure the host device
---------------------------------

It's exactly same as the VFIO device with legacy VFIO container.

Step 2: configure QEMU
----------------------

Interactions with the ``/dev/iommu`` are abstracted by a new iommufd
object (compiled in with the ``CONFIG_IOMMUFD`` option).

Any QEMU device (e.g. VFIO device) wishing to use ``/dev/iommu`` must
be linked with an iommufd object. It gets a new optional property
named iommufd which allows to pass an iommufd object. Take ``vfio-pci``
device for example:

.. code-block:: bash

    -object iommufd,id=iommufd0
    -device vfio-pci,host=0000:02:00.0,iommufd=iommufd0

Note the ``/dev/iommu`` and VFIO cdev can be externally opened by a
management layer. In such a case the fd is passed, the fd supports a
string naming the fd or a number, for example:

.. code-block:: bash

    -object iommufd,id=iommufd0,fd=22
    -device vfio-pci,iommufd=iommufd0,fd=23

If the ``fd`` property is not passed, the fd is opened by QEMU.

If no ``iommufd`` object is passed to the ``vfio-pci`` device, iommufd
is not used and the user gets the behavior based on the legacy VFIO
container:

.. code-block:: bash

    -device vfio-pci,host=0000:02:00.0

Supported platform
==================

Supports x86, ARM and s390x currently.

Caveats
=======

Dirty page sync
---------------

Dirty page sync with iommufd backend is unsupported yet, live migration is
disabled by default. But it can be force enabled like below, low efficient
though.

.. code-block:: bash

    -object iommufd,id=iommufd0
    -device vfio-pci,host=0000:02:00.0,iommufd=iommufd0,enable-migration=on

P2P DMA
-------

PCI p2p DMA is unsupported as IOMMUFD doesn't support mapping hardware PCI
BAR region yet. Below warning shows for assigned PCI device, it's not a bug.

.. code-block:: none

    qemu-system-x86_64: warning: IOMMU_IOAS_MAP failed: Bad address, PCI BAR?
    qemu-system-x86_64: vfio_container_dma_map(0x560cb6cb1620, 0xe000000021000, 0x3000, 0x7f32ed55c000) = -14 (Bad address)

FD passing with mdev
--------------------

``vfio-pci`` device checks sysfsdev property to decide if backend is a mdev.
If FD passing is used, there is no way to know that and the mdev is treated
like a real PCI device. There is an error as below if user wants to enable
RAM discarding for mdev.

.. code-block:: none

    qemu-system-x86_64: -device vfio-pci,iommufd=iommufd0,x-balloon-allowed=on,fd=9: vfio VFIO_FD9: x-balloon-allowed only potentially compatible with mdev devices

``vfio-ap`` and ``vfio-ccw`` devices don't have same issue as their backend
devices are always mdev and RAM discarding is force enabled.
