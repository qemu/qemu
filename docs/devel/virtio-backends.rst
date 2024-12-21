..
   Copyright (c) 2022, Linaro Limited
   Written by Alex Bennée

Writing VirtIO backends for QEMU
================================

This document attempts to outline the information a developer needs to
know to write device emulations in QEMU. It is specifically focused on
implementing VirtIO devices. For VirtIO the frontend is the driver
running on the guest. The backend is the everything that QEMU needs to
do to handle the emulation of the VirtIO device. This can be done
entirely in QEMU, divided between QEMU and the kernel (vhost) or
handled by a separate process which is configured by QEMU
(vhost-user).

VirtIO Transports
-----------------

VirtIO supports a number of different transports. While the details of
the configuration and operation of the device will generally be the
same QEMU represents them as different devices depending on the
transport they use. For example -device virtio-foo represents the foo
device using mmio and -device virtio-foo-pci is the same class of
device using the PCI transport.

Using the QEMU Object Model (QOM)
---------------------------------

Generally all devices in QEMU are super classes of ``TYPE_DEVICE``
however VirtIO devices should be based on ``TYPE_VIRTIO_DEVICE`` which
itself is derived from the base class. For example:

.. code:: c

  static const TypeInfo virtio_blk_info = {
      .name = TYPE_VIRTIO_BLK,
      .parent = TYPE_VIRTIO_DEVICE,
      .instance_size = sizeof(VirtIOBlock),
      .instance_init = virtio_blk_instance_init,
      .class_init = virtio_blk_class_init,
  };

The author may decide to have a more expansive class hierarchy to
support multiple device types. For example the Virtio GPU device:

.. code:: c

  static const TypeInfo virtio_gpu_base_info = {
      .name = TYPE_VIRTIO_GPU_BASE,
      .parent = TYPE_VIRTIO_DEVICE,
      .instance_size = sizeof(VirtIOGPUBase),
      .class_size = sizeof(VirtIOGPUBaseClass),
      .class_init = virtio_gpu_base_class_init,
      .abstract = true
  };

  static const TypeInfo vhost_user_gpu_info = {
      .name = TYPE_VHOST_USER_GPU,
      .parent = TYPE_VIRTIO_GPU_BASE,
      .instance_size = sizeof(VhostUserGPU),
      .instance_init = vhost_user_gpu_instance_init,
      .instance_finalize = vhost_user_gpu_instance_finalize,
      .class_init = vhost_user_gpu_class_init,
  };

  static const TypeInfo virtio_gpu_info = {
      .name = TYPE_VIRTIO_GPU,
      .parent = TYPE_VIRTIO_GPU_BASE,
      .instance_size = sizeof(VirtIOGPU),
      .class_size = sizeof(VirtIOGPUClass),
      .class_init = virtio_gpu_class_init,
  };

defines a base class for the VirtIO GPU and then specialises two
versions, one for the internal implementation and the other for the
vhost-user version.

VirtIOPCIProxy
^^^^^^^^^^^^^^

[AJB: the following is supposition and welcomes more informed
opinions]

Probably due to legacy from the pre-QOM days PCI VirtIO devices don't
follow the normal hierarchy. Instead the a standalone object is based
on the VirtIOPCIProxy class and the specific VirtIO instance is
manually instantiated:

.. code:: c

  /*
   * virtio-blk-pci: This extends VirtioPCIProxy.
   */
  #define TYPE_VIRTIO_BLK_PCI "virtio-blk-pci-base"
  DECLARE_INSTANCE_CHECKER(VirtIOBlkPCI, VIRTIO_BLK_PCI,
                           TYPE_VIRTIO_BLK_PCI)

  struct VirtIOBlkPCI {
      VirtIOPCIProxy parent_obj;
      VirtIOBlock vdev;
  };

  static const Property virtio_blk_pci_properties[] = {
      DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
      DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                      VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
      DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                         DEV_NVECTORS_UNSPECIFIED),
  };

  static void virtio_blk_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
  {
      VirtIOBlkPCI *dev = VIRTIO_BLK_PCI(vpci_dev);
      DeviceState *vdev = DEVICE(&dev->vdev);

      ...

      qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
  }

  static void virtio_blk_pci_class_init(ObjectClass *klass, void *data)
  {
      DeviceClass *dc = DEVICE_CLASS(klass);
      VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
      PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

      set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
      device_class_set_props(dc, virtio_blk_pci_properties);
      k->realize = virtio_blk_pci_realize;
      pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
      pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_BLOCK;
      pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
      pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
  }

  static void virtio_blk_pci_instance_init(Object *obj)
  {
      VirtIOBlkPCI *dev = VIRTIO_BLK_PCI(obj);

      virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                  TYPE_VIRTIO_BLK);
      object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                                "bootindex");
  }

  static const VirtioPCIDeviceTypeInfo virtio_blk_pci_info = {
      .base_name              = TYPE_VIRTIO_BLK_PCI,
      .generic_name           = "virtio-blk-pci",
      .transitional_name      = "virtio-blk-pci-transitional",
      .non_transitional_name  = "virtio-blk-pci-non-transitional",
      .instance_size = sizeof(VirtIOBlkPCI),
      .instance_init = virtio_blk_pci_instance_init,
      .class_init    = virtio_blk_pci_class_init,
  };

Here you can see the instance_init has to manually instantiate the
underlying ``TYPE_VIRTIO_BLOCK`` object and link an alias for one of
it's properties to the PCI device.

  
Back End Implementations
------------------------

There are a number of places where the implementation of the backend
can be done:

* in QEMU itself
* in the host kernel (a.k.a vhost)
* in a separate process (a.k.a. vhost-user)

vhost_ops vs TYPE_VHOST_USER_BACKEND
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

There are two choices to how to implement vhost code. Most of the code
which has to work with either vhost or vhost-user uses
``vhost_dev_init()`` to instantiate the appropriate backend. This
means including a ``struct vhost_dev`` in the main object structure.

For vhost-user devices you also need to add code to track the
initialisation of the ``chardev`` device used for the control socket
between QEMU and the external vhost-user process.

If you only need to implement a vhost-user backed the other option is
a use a QOM-ified version of vhost-user.

.. code:: c

  static void
  vhost_user_gpu_instance_init(Object *obj)
  {
      VhostUserGPU *g = VHOST_USER_GPU(obj);

      g->vhost = VHOST_USER_BACKEND(object_new(TYPE_VHOST_USER_BACKEND));
      object_property_add_alias(obj, "chardev",
                                OBJECT(g->vhost), "chardev");
  }

  static const TypeInfo vhost_user_gpu_info = {
      .name = TYPE_VHOST_USER_GPU,
      .parent = TYPE_VIRTIO_GPU_BASE,
      .instance_size = sizeof(VhostUserGPU),
      .instance_init = vhost_user_gpu_instance_init,
      .instance_finalize = vhost_user_gpu_instance_finalize,
      .class_init = vhost_user_gpu_class_init,
  };

Using it this way entails adding a ``struct VhostUserBackend`` to your
core object structure and manually instantiating the backend. This
sub-structure tracks both the ``vhost_dev`` and ``CharDev`` types
needed for the connection. Instead of calling ``vhost_dev_init`` you
would call ``vhost_user_backend_dev_init`` which does what is needed
on your behalf.
