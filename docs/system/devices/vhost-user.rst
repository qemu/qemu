.. _vhost_user:

vhost-user back ends
--------------------

vhost-user back ends are way to service the request of VirtIO devices
outside of QEMU itself. To do this there are a number of things
required.

vhost-user device
=================

These are simple stub devices that ensure the VirtIO device is visible
to the guest. The code is mostly boilerplate although each device has
a ``chardev`` option which specifies the ID of the ``--chardev``
device that connects via a socket to the vhost-user *daemon*.

Each device will have an virtio-mmio and virtio-pci variant. See your
platform details for what sort of virtio bus to use.

.. list-table:: vhost-user devices
  :widths: 20 20 60
  :header-rows: 1

  * - Device
    - Type
    - Notes
  * - vhost-user-blk
    - Block storage
    - See contrib/vhost-user-blk
  * - vhost-user-fs
    - File based storage driver
    - See https://gitlab.com/virtio-fs/virtiofsd
  * - vhost-user-gpio
    - Proxy gpio pins to host
    - See https://github.com/rust-vmm/vhost-device
  * - vhost-user-gpu
    - GPU driver
    - See contrib/vhost-user-gpu
  * - vhost-user-i2c
    - Proxy i2c devices to host
    - See https://github.com/rust-vmm/vhost-device
  * - vhost-user-input
    - Generic input driver
    - :ref:`vhost_user_input`
  * - vhost-user-rng
    - Entropy driver
    - :ref:`vhost_user_rng`
  * - vhost-user-scmi
    - System Control and Management Interface
    - See https://github.com/rust-vmm/vhost-device
  * - vhost-user-snd
    - Audio device
    - See https://github.com/rust-vmm/vhost-device/staging
  * - vhost-user-scsi
    - SCSI based storage
    - See contrib/vhost-user-scsi
  * - vhost-user-vsock
    - Socket based communication
    - See https://github.com/rust-vmm/vhost-device

The referenced *daemons* are not exhaustive, any conforming backend
implementing the device and using the vhost-user protocol should work.

vhost-user-device
^^^^^^^^^^^^^^^^^

The vhost-user-device is a generic development device intended for
expert use while developing new backends. The user needs to specify
all the required parameters including:

  - Device ``virtio-id``
  - The ``num_vqs`` it needs and their ``vq_size``
  - The ``config_size`` if needed

.. note::
  To prevent user confusion you cannot currently instantiate
  vhost-user-device without first patching out::

    /* Reason: stop inexperienced users confusing themselves */
    dc->user_creatable = false;

  in ``vhost-user-device.c`` and ``vhost-user-device-pci.c`` file and
  rebuilding.

vhost-user daemon
=================

This is a separate process that is connected to by QEMU via a socket
following the :ref:`vhost_user_proto`. There are a number of daemons
that can be built when enabled by the project although any daemon that
meets the specification for a given device can be used.

.. _shared_memory_object:

Shared memory object
====================

In order for the daemon to access the VirtIO queues to process the
requests it needs access to the guest's address space. This is
achieved via the ``memory-backend-file``, ``memory-backend-memfd``, or
``memory-backend-shm`` objects.
A reference to a file-descriptor which can access this object
will be passed via the socket as part of the protocol negotiation.

Currently the shared memory object needs to match the size of the main
system memory as defined by the ``-m`` argument.

Example
=======

First start your daemon.

.. parsed-literal::

  $ virtio-foo --socket-path=/var/run/foo.sock $OTHER_ARGS

Then you start your QEMU instance specifying the device, chardev and
memory objects.

.. parsed-literal::

  $ |qemu_system| \\
      -m 4096 \\
      -chardev socket,id=ba1,path=/var/run/foo.sock \\
      -device vhost-user-foo,chardev=ba1,$OTHER_ARGS \\
      -object memory-backend-memfd,id=mem,size=4G,share=on \\
      -numa node,memdev=mem \\
        ...

