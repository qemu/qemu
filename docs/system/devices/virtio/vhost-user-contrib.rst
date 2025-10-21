vhost-user daemons in contrib
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

QEMU provides a number of :ref:`vhost_user` daemons in the contrib
directory. They were often written when vhost-user was initially added
to the code base. You should also consider if other vhost-user daemons
such as those from the rust-vmm `vhost-device repository`_ are better
suited for production use.

.. _vhost-device repository: https://github.com/rust-vmm/vhost-device

.. _vhost_user_block:

vhost-user-block - block device
===============================

vhost-user-block is a backend for exposing block devices. It can
present a flat file or block device as a simple block device to the
guest. You almost certainly want to use the :ref:`storage-daemon`
instead which supports a wide variety of storage modes and exports a
number of interfaces including vhost-user.

.. _vhost_user_gpu:

vhost-user-gpu - gpu device
===========================

vhost-user-gpu presents a paravirtualized GPU and display controller.
You probably want to use the internal :ref:`virtio_gpu` implementation
if you want the latest features. There is also a `vhost_device_gpu`_
daemon as part of the rust-vmm project.

.. _vhost_device_gpu: https://github.com/rust-vmm/vhost-device/tree/main/vhost-device-gpu

.. _vhost_user_input:

vhost-user-input - Input emulation
==================================

The Virtio input device is a paravirtualized device for input events.

Description
-----------

The vhost-user-input device implementation was designed to work with a daemon
polling on input devices and passes input events to the guest.

QEMU provides a backend implementation in contrib/vhost-user-input.

Linux kernel support
--------------------

Virtio input requires a guest Linux kernel built with the
``CONFIG_VIRTIO_INPUT`` option.

Examples
--------

The backend daemon should be started first:

::

  host# vhost-user-input --socket-path=input.sock	\
      --evdev-path=/dev/input/event17

The QEMU invocation needs to create a chardev socket to communicate with the
backend daemon and access the VirtIO queues with the guest over the
:ref:`shared memory <shared_memory_object>`.

::

  host# qemu-system								\
      -chardev socket,path=/tmp/input.sock,id=mouse0				\
      -device vhost-user-input-pci,chardev=mouse0				\
      -m 4096 									\
      -object memory-backend-file,id=mem,size=4G,mem-path=/dev/shm,share=on	\
      -numa node,memdev=mem							\
      ...


.. _vhost_user_scsi:

vhost-user-scsi - SCSI controller
=================================

The vhost-user-scsi daemon can proxy iSCSI devices onto a virtualized
SCSI controller.
