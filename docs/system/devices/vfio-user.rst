.. SPDX-License-Identifier: GPL-2.0-or-later

=========
vfio-user
=========

QEMU includes a ``vfio-user`` client. The ``vfio-user`` specification allows for
implementing (PCI) devices in userspace outside of QEMU; it is similar to
``vhost-user`` in this respect (see :doc:`virtio/vhost-user`), but can emulate arbitrary
PCI devices, not just ``virtio``. Whereas ``vfio`` is handled by the host
kernel, ``vfio-user``, while similar in implementation, is handled entirely in
userspace.

For example, SPDK includes a virtual PCI NVMe controller implementation; by
setting up a ``vfio-user`` UNIX socket between QEMU and SPDK, a VM can send NVMe
I/O to the SPDK process.

Presuming a suitable ``vfio-user`` server has opened a socket at
``/tmp/vfio-user.sock``, a device can be configured with for example:

.. code-block:: console

  --device '{"driver": "vfio-user-pci","socket": {"path": "/tmp/vfio-user.sock", "type": "unix"}}'

See `libvfio-user <https://github.com/nutanix/libvfio-user/>`_ for further
information.
