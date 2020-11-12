
===========
virtio pmem
===========

This document explains the setup and usage of the virtio pmem device.
The virtio pmem device is a paravirtualized persistent memory device
on regular (i.e non-NVDIMM) storage.

Usecase
-------

Virtio pmem allows to bypass the guest page cache and directly use
host page cache. This reduces guest memory footprint as the host can
make efficient memory reclaim decisions under memory pressure.

How does virtio-pmem compare to the nvdimm emulation?
-----------------------------------------------------

NVDIMM emulation on regular (i.e. non-NVDIMM) host storage does not
persist the guest writes as there are no defined semantics in the device
specification. The virtio pmem device provides guest write persistence
on non-NVDIMM host storage.

virtio pmem usage
-----------------

A virtio pmem device backed by a memory-backend-file can be created on
the QEMU command line as in the following example::

    -object memory-backend-file,id=mem1,share,mem-path=./virtio_pmem.img,size=4G
    -device virtio-pmem-pci,memdev=mem1,id=nv1

where:

  - "object memory-backend-file,id=mem1,share,mem-path=<image>, size=<image size>"
    creates a backend file with the specified size.

  - "device virtio-pmem-pci,id=nvdimm1,memdev=mem1" creates a virtio pmem
    pci device whose storage is provided by above memory backend device.

Multiple virtio pmem devices can be created if multiple pairs of "-object"
and "-device" are provided.

Hotplug
-------

Virtio pmem devices can be hotplugged via the QEMU monitor. First, the
memory backing has to be added via 'object_add'; afterwards, the virtio
pmem device can be added via 'device_add'.

For example, the following commands add another 4GB virtio pmem device to
the guest::

 (qemu) object_add memory-backend-file,id=mem2,share=on,mem-path=virtio_pmem2.img,size=4G
 (qemu) device_add virtio-pmem-pci,id=virtio_pmem2,memdev=mem2

Guest Data Persistence
----------------------

Guest data persistence on non-NVDIMM requires guest userspace applications
to perform fsync/msync. This is different from a real nvdimm backend where
no additional fsync/msync is required. This is to persist guest writes in
host backing file which otherwise remains in host page cache and there is
risk of losing the data in case of power failure.

With virtio pmem device, MAP_SYNC mmap flag is not supported. This provides
a hint to application to perform fsync for write persistence.

Limitations
-----------

- Real nvdimm device backend is not supported.
- virtio pmem hotunplug is not supported.
- ACPI NVDIMM features like regions/namespaces are not supported.
- ndctl command is not supported.
