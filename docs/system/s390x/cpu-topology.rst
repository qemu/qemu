.. _cpu-topology-s390x:

CPU topology on s390x
=====================

Since QEMU 8.2, CPU topology on s390x provides up to 3 levels of
topology containers: drawers, books and sockets. They define a
tree-shaped hierarchy.

The socket container has one or more CPU entries.
Each of these CPU entries consists of a bitmap and three CPU attributes:

- CPU type
- entitlement
- dedication

Each bit set in the bitmap correspond to a core-id of a vCPU with matching
attributes.

This documentation provides general information on S390 CPU topology,
how to enable it and explains the new CPU attributes.
For information on how to modify the S390 CPU topology and how to
monitor polarization changes, see ``docs/devel/s390-cpu-topology.rst``.

Prerequisites
-------------

To use the CPU topology, you need to run with KVM on a s390x host that
uses the Linux kernel v6.0 or newer (which provide the so-called
``KVM_CAP_S390_CPU_TOPOLOGY`` capability that allows QEMU to signal the
CPU topology facility via the so-called STFLE bit 11 to the VM).

Enabling CPU topology
---------------------

Currently, CPU topology is only enabled in the host model by default.

Enabling CPU topology in a CPU model is done by setting the CPU flag
``ctop`` to ``on`` as in:

.. code-block:: bash

   -cpu gen16b,ctop=on

Having the topology disabled by default allows migration between
old and new QEMU without adding new flags.

Default topology usage
----------------------

The CPU topology can be specified on the QEMU command line
with the ``-smp`` or the ``-device`` QEMU command arguments.

Note also that since 7.2 threads are no longer supported in the topology
and the ``-smp`` command line argument accepts only ``threads=1``.

If none of the containers attributes (drawers, books, sockets) are
specified for the ``-smp`` flag, the number of these containers
is 1.

Thus the following two options will result in the same topology:

.. code-block:: bash

    -smp cpus=5,drawer=1,books=1,sockets=8,cores=4,maxcpus=32

and

.. code-block:: bash

    -smp cpus=5,sockets=8,cores=4,maxcpus=32

When a CPU is defined by the ``-smp`` command argument, its position
inside the topology is calculated by adding the CPUs to the topology
based on the core-id starting with core-0 at position 0 of socket-0,
book-0, drawer-0 and filling all CPUs of socket-0 before filling socket-1
of book-0 and so on up to the last socket of the last book of the last
drawer.

When a CPU is defined by the ``-device`` command argument, the
tree topology attributes must all be defined or all not defined.

.. code-block:: bash

    -device gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=1

or

.. code-block:: bash

    -device gen16b-s390x-cpu,core-id=1,dedicated=true

If none of the tree attributes (drawer, book, sockets), are specified
for the ``-device`` argument, like for all CPUs defined with the ``-smp``
command argument the topology tree attributes will be set by simply
adding the CPUs to the topology based on the core-id.

QEMU will not try to resolve collisions and will report an error if the
CPU topology defined explicitly or implicitly on a ``-device``
argument collides with the definition of a CPU implicitly defined
on the ``-smp`` argument.

When the topology modifier attributes are not defined for the
``-device`` command argument they takes following default values:

- dedicated: ``false``
- entitlement: ``medium``


Hot plug
++++++++

New CPUs can be plugged using the device_add hmp command as in:

.. code-block:: bash

  (qemu) device_add gen16b-s390x-cpu,core-id=9

The placement of the CPU is derived from the core-id as described above.

The topology can of course also be fully defined:

.. code-block:: bash

    (qemu) device_add gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=1


Examples
++++++++

In the following machine we define 8 sockets with 4 cores each.

.. code-block:: bash

  $ qemu-system-s390x -m 2G \
    -cpu gen16b,ctop=on \
    -smp cpus=5,sockets=8,cores=4,maxcpus=32 \
    -device host-s390x-cpu,core-id=14 \

A new CPUs can be plugged using the device_add hmp command as before:

.. code-block:: bash

  (qemu) device_add gen16b-s390x-cpu,core-id=9

The core-id defines the placement of the core in the topology by
starting with core 0 in socket 0 up to maxcpus.

In the example above:

* There are 5 CPUs provided to the guest with the ``-smp`` command line
  They will take the core-ids 0,1,2,3,4
  As we have 4 cores in a socket, we have 4 CPUs provided
  to the guest in socket 0, with core-ids 0,1,2,3.
  The last CPU, with core-id 4, will be on socket 1.

* the core with ID 14 provided by the ``-device`` command line will
  be placed in socket 3, with core-id 14

* the core with ID 9 provided by the ``device_add`` qmp command will
  be placed in socket 2, with core-id 9


Polarization, entitlement and dedication
----------------------------------------

Polarization
++++++++++++

The polarization affects how the CPUs of a shared host are utilized/distributed
among guests.
The guest determines the polarization by using the PTF instruction.

Polarization defines two models of CPU provisioning: horizontal
and vertical.

The horizontal polarization is the default model on boot and after
subsystem reset. When horizontal polarization is in effect all vCPUs should
have about equal resource provisioning.

In the vertical polarization model vCPUs are unequal, but overall more resources
might be available.
The guest can make use of the vCPU entitlement information provided by the host
to optimize kernel thread scheduling.

A subsystem reset puts all vCPU of the configuration into the
horizontal polarization.

Entitlement
+++++++++++

The vertical polarization specifies that the guest's vCPU can get
different real CPU provisioning:

- a vCPU with vertical high entitlement specifies that this
  vCPU gets 100% of the real CPU provisioning.

- a vCPU with vertical medium entitlement specifies that this
  vCPU shares the real CPU with other vCPUs.

- a vCPU with vertical low entitlement specifies that this
  vCPU only gets real CPU provisioning when no other vCPUs needs it.

In the case a vCPU with vertical high entitlement does not use
the real CPU, the unused "slack" can be dispatched to other vCPU
with medium or low entitlement.

A vCPU can be "dedicated" in which case the vCPU is fully dedicated to a single
real CPU.

The dedicated bit is an indication of affinity of a vCPU for a real CPU
while the entitlement indicates the sharing or exclusivity of use.

Defining the topology on the command line
-----------------------------------------

The topology can entirely be defined using -device cpu statements,
with the exception of CPU 0 which must be defined with the -smp
argument.

For example, here we set the position of the cores 1,2,3 to
drawer 1, book 1, socket 2 and cores 0,9 and 14 to drawer 0,
book 0, socket 0 without defining entitlement or dedication.
Core 4 will be set on its default position on socket 1
(since we have 4 core per socket) and we define it as dedicated and
with vertical high entitlement.

.. code-block:: bash

  $ qemu-system-s390x -m 2G \
    -cpu gen16b,ctop=on \
    -smp cpus=1,sockets=8,cores=4,maxcpus=32 \
    \
    -device gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=1 \
    -device gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=2 \
    -device gen16b-s390x-cpu,drawer-id=1,book-id=1,socket-id=2,core-id=3 \
    \
    -device gen16b-s390x-cpu,drawer-id=0,book-id=0,socket-id=0,core-id=9 \
    -device gen16b-s390x-cpu,drawer-id=0,book-id=0,socket-id=0,core-id=14 \
    \
    -device gen16b-s390x-cpu,core-id=4,dedicated=on,entitlement=high

The entitlement defined for the CPU 4 will only be used after the guest
successfully enables vertical polarization by using the PTF instruction.
