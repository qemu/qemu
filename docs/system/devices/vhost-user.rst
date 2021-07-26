.. _vhost_user:

vhost-user back ends
--------------------

vhost-user back ends are way to service the request of VirtIO devices
outside of QEMU itself. To do this there are a number of things
required.

vhost-user device
===================

These are simple stub devices that ensure the VirtIO device is visible
to the guest. The code is mostly boilerplate although each device has
a ``chardev`` option which specifies the ID of the ``--chardev``
device that connects via a socket to the vhost-user *daemon*.

vhost-user daemon
=================

This is a separate process that is connected to by QEMU via a socket
following the :ref:`vhost_user_proto`. There are a number of daemons
that can be built when enabled by the project although any daemon that
meets the specification for a given device can be used.

Shared memory object
====================

In order for the daemon to access the VirtIO queues to process the
requests it needs access to the guest's address space. This is
achieved via the ``memory-backend-file`` or ``memory-backend-memfd``
objects. A reference to a file-descriptor which can access this object
will be passed via the socket as part of the protocol negotiation.

Currently the shared memory object needs to match the size of the main
system memory as defined by the ``-m`` argument.

Example
=======

First start you daemon.

.. parsed-literal::

  $ virtio-foo --socket-path=/var/run/foo.sock $OTHER_ARGS

The you start your QEMU instance specifying the device, chardev and
memory objects.

.. parsed-literal::

  $ |qemu_system| \\
      -m 4096 \\
      -chardev socket,id=ba1,path=/var/run/foo.sock \\
      -device vhost-user-foo,chardev=ba1,$OTHER_ARGS \\
      -object memory-backend-memfd,id=mem,size=4G,share=on \\
      -numa node,memdev=mem \\
        ...

