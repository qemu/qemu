.. _vhost_user_input:

QEMU vhost-user-input - Input emulation
=======================================

This document describes the setup and usage of the Virtio input device.
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
