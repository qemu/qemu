.. _vhost_user_proto:

===================
Vhost-user Protocol
===================

..
  Copyright 2014 Virtual Open Systems Sarl.
  Copyright 2019 Intel Corporation
  Licence: This work is licensed under the terms of the GNU GPL,
           version 2 or later. See the COPYING file in the top-level
           directory.

.. contents:: Table of Contents

Introduction
============

This protocol is aiming to complement the ``ioctl`` interface used to
control the vhost implementation in the Linux kernel. It implements
the control plane needed to establish virtqueue sharing with a user
space process on the same host. It uses communication over a Unix
domain socket to share file descriptors in the ancillary data of the
message.

The protocol defines 2 sides of the communication, *front-end* and
*back-end*. The *front-end* is the application that shares its virtqueues, in
our case QEMU. The *back-end* is the consumer of the virtqueues.

In the current implementation QEMU is the *front-end*, and the *back-end*
is the external process consuming the virtio queues, for example a
software Ethernet switch running in user space, such as Snabbswitch,
or a block device back-end processing read & write to a virtual
disk. In order to facilitate interoperability between various back-end
implementations, it is recommended to follow the :ref:`Backend program
conventions <backend_conventions>`.

The *front-end* and *back-end* can be either a client (i.e. connecting) or
server (listening) in the socket communication.

Support for platforms other than Linux
--------------------------------------

While vhost-user was initially developed targeting Linux, nowadays it
is supported on any platform that provides the following features:

- A way for requesting shared memory represented by a file descriptor
  so it can be passed over a UNIX domain socket and then mapped by the
  other process.

- AF_UNIX sockets with SCM_RIGHTS, so QEMU and the other process can
  exchange messages through it, including ancillary data when needed.

- Either eventfd or pipe/pipe2. On platforms where eventfd is not
  available, QEMU will automatically fall back to pipe2 or, as a last
  resort, pipe. Each file descriptor will be used for receiving or
  sending events by reading or writing (respectively) an 8-byte value
  to the corresponding it. The 8-value itself has no meaning and
  should not be interpreted.

Message Specification
=====================

.. Note:: All numbers are in the machine native byte order.

A vhost-user message consists of 3 header fields and a payload.

+---------+-------+------+---------+
| request | flags | size | payload |
+---------+-------+------+---------+

Header
------

:request: 32-bit type of the request

:flags: 32-bit bit field

- Lower 2 bits are the version (currently 0x01)
- Bit 2 is the reply flag - needs to be sent on each reply from the back-end
- Bit 3 is the need_reply flag - see :ref:`REPLY_ACK <reply_ack>` for
  details.

:size: 32-bit size of the payload

Payload
-------

Depending on the request type, **payload** can be:

A single 64-bit integer
^^^^^^^^^^^^^^^^^^^^^^^

+-----+
| u64 |
+-----+

:u64: a 64-bit unsigned integer

A vring state description
^^^^^^^^^^^^^^^^^^^^^^^^^

+-------+-----+
| index | num |
+-------+-----+

:index: a 32-bit index

:num: a 32-bit number

A vring descriptor index for split virtqueues
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

+-------------+---------------------+
| vring index | index in avail ring |
+-------------+---------------------+

:vring index: 32-bit index of the respective virtqueue

:index in avail ring: 32-bit value, of which currently only the lower 16
  bits are used:

  - Bits 0–15: Index of the next *Available Ring* descriptor that the
    back-end will process.  This is a free-running index that is not
    wrapped by the ring size.
  - Bits 16–31: Reserved (set to zero)

Vring descriptor indices for packed virtqueues
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

+-------------+--------------------+
| vring index | descriptor indices |
+-------------+--------------------+

:vring index: 32-bit index of the respective virtqueue

:descriptor indices: 32-bit value:

  - Bits 0–14: Index of the next *Available Ring* descriptor that the
    back-end will process.  This is a free-running index that is not
    wrapped by the ring size.
  - Bit 15: Driver (Available) Ring Wrap Counter
  - Bits 16–30: Index of the entry in the *Used Ring* where the back-end
    will place the next descriptor.  This is a free-running index that
    is not wrapped by the ring size.
  - Bit 31: Device (Used) Ring Wrap Counter

A vring address description
^^^^^^^^^^^^^^^^^^^^^^^^^^^

+-------+-------+------+------------+------+-----------+-----+
| index | flags | size | descriptor | used | available | log |
+-------+-------+------+------------+------+-----------+-----+

:index: a 32-bit vring index

:flags: a 32-bit vring flags

:descriptor: a 64-bit ring address of the vring descriptor table

:used: a 64-bit ring address of the vring used ring

:available: a 64-bit ring address of the vring available ring

:log: a 64-bit guest address for logging

Note that a ring address is an IOVA if ``VIRTIO_F_IOMMU_PLATFORM`` has
been negotiated. Otherwise it is a user address.

Memory region description
^^^^^^^^^^^^^^^^^^^^^^^^^

+---------------+------+--------------+-------------+
| guest address | size | user address | mmap offset |
+---------------+------+--------------+-------------+

:guest address: a 64-bit guest address of the region

:size: a 64-bit size

:user address: a 64-bit user address

:mmap offset: 64-bit offset where region starts in the mapped memory

When the ``VHOST_USER_PROTOCOL_F_XEN_MMAP`` protocol feature has been
successfully negotiated, the memory region description contains two extra
fields at the end.

+---------------+------+--------------+-------------+----------------+-------+
| guest address | size | user address | mmap offset | xen mmap flags | domid |
+---------------+------+--------------+-------------+----------------+-------+

:xen mmap flags: 32-bit bit field

- Bit 0 is set for Xen foreign memory mapping.
- Bit 1 is set for Xen grant memory mapping.
- Bit 8 is set if the memory region can not be mapped in advance, and memory
  areas within this region must be mapped / unmapped only when required by the
  back-end. The back-end shouldn't try to map the entire region at once, as the
  front-end may not allow it. The back-end should rather map only the required
  amount of memory at once and unmap it after it is used.

:domid: a 32-bit Xen hypervisor specific domain id.

Single memory region description
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

+---------+--------+
| padding | region |
+---------+--------+

:padding: 64-bit

A region is represented by Memory region description.

Multiple Memory regions description
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

+-------------+---------+---------+-----+---------+
| num regions | padding | region0 | ... | region7 |
+-------------+---------+---------+-----+---------+

:num regions: a 32-bit number of regions

:padding: 32-bit

A region is represented by Memory region description.

Log description
^^^^^^^^^^^^^^^

+----------+------------+
| log size | log offset |
+----------+------------+

:log size: size of area used for logging

:log offset: offset from start of supplied file descriptor where
             logging starts (i.e. where guest address 0 would be
             logged)

An IOTLB message
^^^^^^^^^^^^^^^^

+------+------+--------------+-------------------+------+
| iova | size | user address | permissions flags | type |
+------+------+--------------+-------------------+------+

:iova: a 64-bit I/O virtual address programmed by the guest

:size: a 64-bit size

:user address: a 64-bit user address

:permissions flags: an 8-bit value:
  - 0: No access
  - 1: Read access
  - 2: Write access
  - 3: Read/Write access

:type: an 8-bit IOTLB message type:
  - 1: IOTLB miss
  - 2: IOTLB update
  - 3: IOTLB invalidate
  - 4: IOTLB access fail

Virtio device config space
^^^^^^^^^^^^^^^^^^^^^^^^^^

+--------+------+-------+---------+
| offset | size | flags | payload |
+--------+------+-------+---------+

:offset: a 32-bit offset of virtio device's configuration space

:size: a 32-bit configuration space access size in bytes

:flags: a 32-bit value:
  - 0: Vhost front-end messages used for writable fields
  - 1: Vhost front-end messages used for live migration

:payload: Size bytes array holding the contents of the virtio
          device's configuration space

Vring area description
^^^^^^^^^^^^^^^^^^^^^^

+-----+------+--------+
| u64 | size | offset |
+-----+------+--------+

:u64: a 64-bit integer contains vring index and flags

:size: a 64-bit size of this area

:offset: a 64-bit offset of this area from the start of the
         supplied file descriptor

Inflight description
^^^^^^^^^^^^^^^^^^^^

+-----------+-------------+------------+------------+
| mmap size | mmap offset | num queues | queue size |
+-----------+-------------+------------+------------+

:mmap size: a 64-bit size of area to track inflight I/O

:mmap offset: a 64-bit offset of this area from the start
              of the supplied file descriptor

:num queues: a 16-bit number of virtqueues

:queue size: a 16-bit size of virtqueues

VhostUserShared
^^^^^^^^^^^^^^^

+------+
| UUID |
+------+

:UUID: 16 bytes UUID, whose first three components (a 32-bit value, then
  two 16-bit values) are stored in big endian.

Device state transfer parameters
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

+--------------------+-----------------+
| transfer direction | migration phase |
+--------------------+-----------------+

:transfer direction: a 32-bit enum, describing the direction in which
  the state is transferred:

  - 0: Save: Transfer the state from the back-end to the front-end,
    which happens on the source side of migration
  - 1: Load: Transfer the state from the front-end to the back-end,
    which happens on the destination side of migration

:migration phase: a 32-bit enum, describing the state in which the VM
  guest and devices are:

  - 0: Stopped (in the period after the transfer of memory-mapped
    regions before switch-over to the destination): The VM guest is
    stopped, and the vhost-user device is suspended (see
    :ref:`Suspended device state <suspended_device_state>`).

  In the future, additional phases might be added e.g. to allow
  iterative migration while the device is running.

C structure
-----------

In QEMU the vhost-user message is implemented with the following struct:

.. code:: c

  typedef struct VhostUserMsg {
      VhostUserRequest request;
      uint32_t flags;
      uint32_t size;
      union {
          uint64_t u64;
          struct vhost_vring_state state;
          struct vhost_vring_addr addr;
          VhostUserMemory memory;
          VhostUserLog log;
          struct vhost_iotlb_msg iotlb;
          VhostUserConfig config;
          VhostUserVringArea area;
          VhostUserInflight inflight;
      };
  } QEMU_PACKED VhostUserMsg;

Communication
=============

The protocol for vhost-user is based on the existing implementation of
vhost for the Linux Kernel. Most messages that can be sent via the
Unix domain socket implementing vhost-user have an equivalent ioctl to
the kernel implementation.

The communication consists of the *front-end* sending message requests and
the *back-end* sending message replies. Most of the requests don't require
replies. Here is a list of the ones that do:

* ``VHOST_USER_GET_FEATURES``
* ``VHOST_USER_GET_PROTOCOL_FEATURES``
* ``VHOST_USER_GET_VRING_BASE``
* ``VHOST_USER_SET_LOG_BASE`` (if ``VHOST_USER_PROTOCOL_F_LOG_SHMFD``)
* ``VHOST_USER_GET_INFLIGHT_FD`` (if ``VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD``)

.. seealso::

   :ref:`REPLY_ACK <reply_ack>`
       The section on ``REPLY_ACK`` protocol extension.

There are several messages that the front-end sends with file descriptors passed
in the ancillary data:

* ``VHOST_USER_ADD_MEM_REG``
* ``VHOST_USER_SET_MEM_TABLE``
* ``VHOST_USER_SET_LOG_BASE`` (if ``VHOST_USER_PROTOCOL_F_LOG_SHMFD``)
* ``VHOST_USER_SET_LOG_FD``
* ``VHOST_USER_SET_VRING_KICK``
* ``VHOST_USER_SET_VRING_CALL``
* ``VHOST_USER_SET_VRING_ERR``
* ``VHOST_USER_SET_BACKEND_REQ_FD`` (previous name ``VHOST_USER_SET_SLAVE_REQ_FD``)
* ``VHOST_USER_SET_INFLIGHT_FD`` (if ``VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD``)
* ``VHOST_USER_SET_DEVICE_STATE_FD``

If *front-end* is unable to send the full message or receives a wrong
reply it will close the connection. An optional reconnection mechanism
can be implemented.

If *back-end* detects some error such as incompatible features, it may also
close the connection. This should only happen in exceptional circumstances.

Any protocol extensions are gated by protocol feature bits, which
allows full backwards compatibility on both front-end and back-end.  As
older back-ends don't support negotiating protocol features, a feature
bit was dedicated for this purpose::

  #define VHOST_USER_F_PROTOCOL_FEATURES 30

Note that VHOST_USER_F_PROTOCOL_FEATURES is the UNUSED (30) feature
bit defined in `VIRTIO 1.1 6.3 Legacy Interface: Reserved Feature Bits
<https://docs.oasis-open.org/virtio/virtio/v1.1/cs01/virtio-v1.1-cs01.html#x1-4130003>`_.
VIRTIO devices do not advertise this feature bit and therefore VIRTIO
drivers cannot negotiate it.

This reserved feature bit was reused by the vhost-user protocol to add
vhost-user protocol feature negotiation in a backwards compatible
fashion. Old vhost-user front-end and back-end implementations continue to
work even though they are not aware of vhost-user protocol feature
negotiation.

Ring states
-----------

Rings have two independent states: started/stopped, and enabled/disabled.

* While a ring is stopped, the back-end must not process the ring at
  all, regardless of whether it is enabled or disabled.  The
  enabled/disabled state should still be tracked, though, so it can come
  into effect once the ring is started.

* started and disabled: The back-end must process the ring without
  causing any side effects.  For example, for a networking device,
  in the disabled state the back-end must not supply any new RX packets,
  but must process and discard any TX packets.

* started and enabled: The back-end must process the ring normally, i.e.
  process all requests and execute them.

Each ring is initialized in a stopped and disabled state.  The back-end
must start a ring upon receiving a kick (that is, detecting that file
descriptor is readable) on the descriptor specified by
``VHOST_USER_SET_VRING_KICK`` or receiving the in-band message
``VHOST_USER_VRING_KICK`` if negotiated, and stop a ring upon receiving
``VHOST_USER_GET_VRING_BASE``.

Rings can be enabled or disabled by ``VHOST_USER_SET_VRING_ENABLE``.

In addition, upon receiving a ``VHOST_USER_SET_FEATURES`` message from
the front-end without ``VHOST_USER_F_PROTOCOL_FEATURES`` set, the
back-end must enable all rings immediately.

While processing the rings (whether they are enabled or not), the back-end
must support changing some configuration aspects on the fly.

.. _suspended_device_state:

Suspended device state
^^^^^^^^^^^^^^^^^^^^^^

While all vrings are stopped, the device is *suspended*.  In addition to
not processing any vring (because they are stopped), the device must:

* not write to any guest memory regions,
* not send any notifications to the guest,
* not send any messages to the front-end,
* still process and reply to messages from the front-end.

Multiple queue support
----------------------

Many devices have a fixed number of virtqueues.  In this case the front-end
already knows the number of available virtqueues without communicating with the
back-end.

Some devices do not have a fixed number of virtqueues.  Instead the maximum
number of virtqueues is chosen by the back-end.  The number can depend on host
resource availability or back-end implementation details.  Such devices are called
multiple queue devices.

Multiple queue support allows the back-end to advertise the maximum number of
queues.  This is treated as a protocol extension, hence the back-end has to
implement protocol features first. The multiple queues feature is supported
only when the protocol feature ``VHOST_USER_PROTOCOL_F_MQ`` (bit 0) is set.

The max number of queues the back-end supports can be queried with message
``VHOST_USER_GET_QUEUE_NUM``. Front-end should stop when the number of requested
queues is bigger than that.

As all queues share one connection, the front-end uses a unique index for each
queue in the sent message to identify a specified queue.

The front-end enables queues by sending message ``VHOST_USER_SET_VRING_ENABLE``.
vhost-user-net has historically automatically enabled the first queue pair.

Back-ends should always implement the ``VHOST_USER_PROTOCOL_F_MQ`` protocol
feature, even for devices with a fixed number of virtqueues, since it is simple
to implement and offers a degree of introspection.

Front-ends must not rely on the ``VHOST_USER_PROTOCOL_F_MQ`` protocol feature for
devices with a fixed number of virtqueues.  Only true multiqueue devices
require this protocol feature.

Migration
---------

During live migration, the front-end may need to track the modifications
the back-end makes to the memory mapped regions. The front-end should mark
the dirty pages in a log. Once it complies to this logging, it may
declare the ``VHOST_F_LOG_ALL`` vhost feature.

To start/stop logging of data/used ring writes, the front-end may send
messages ``VHOST_USER_SET_FEATURES`` with ``VHOST_F_LOG_ALL`` and
``VHOST_USER_SET_VRING_ADDR`` with ``VHOST_VRING_F_LOG`` in ring's
flags set to 1/0, respectively.

All the modifications to memory pointed by vring "descriptor" should
be marked. Modifications to "used" vring should be marked if
``VHOST_VRING_F_LOG`` is part of ring's flags.

Dirty pages are of size::

  #define VHOST_LOG_PAGE 0x1000

The log memory fd is provided in the ancillary data of
``VHOST_USER_SET_LOG_BASE`` message when the back-end has
``VHOST_USER_PROTOCOL_F_LOG_SHMFD`` protocol feature.

The size of the log is supplied as part of ``VhostUserMsg`` which
should be large enough to cover all known guest addresses. Log starts
at the supplied offset in the supplied file descriptor.  The log
covers from address 0 to the maximum of guest regions. In pseudo-code,
to mark page at ``addr`` as dirty::

  page = addr / VHOST_LOG_PAGE
  log[page / 8] |= 1 << page % 8

Where ``addr`` is the guest physical address.

Use atomic operations, as the log may be concurrently manipulated.

Note that when logging modifications to the used ring (when
``VHOST_VRING_F_LOG`` is set for this ring), ``log_guest_addr`` should
be used to calculate the log offset: the write to first byte of the
used ring is logged at this offset from log start. Also note that this
value might be outside the legal guest physical address range
(i.e. does not have to be covered by the ``VhostUserMemory`` table), but
the bit offset of the last byte of the ring must fall within the size
supplied by ``VhostUserLog``.

``VHOST_USER_SET_LOG_FD`` is an optional message with an eventfd in
ancillary data, it may be used to inform the front-end that the log has
been modified.

Once the source has finished migration, rings will be stopped by the
source (:ref:`Suspended device state <suspended_device_state>`). No
further update must be done before rings are restarted.

In postcopy migration the back-end is started before all the memory has
been received from the source host, and care must be taken to avoid
accessing pages that have yet to be received.  The back-end opens a
'userfault'-fd and registers the memory with it; this fd is then
passed back over to the front-end.  The front-end services requests on the
userfaultfd for pages that are accessed and when the page is available
it performs WAKE ioctl's on the userfaultfd to wake the stalled
back-end.  The front-end indicates support for this via the
``VHOST_USER_PROTOCOL_F_PAGEFAULT`` feature.

.. _migrating_backend_state:

Migrating back-end state
^^^^^^^^^^^^^^^^^^^^^^^^

Migrating device state involves transferring the state from one
back-end, called the source, to another back-end, called the
destination.  After migration, the destination transparently resumes
operation without requiring the driver to re-initialize the device at
the VIRTIO level.  If the migration fails, then the source can
transparently resume operation until another migration attempt is made.

Generally, the front-end is connected to a virtual machine guest (which
contains the driver), which has its own state to transfer between source
and destination, and therefore will have an implementation-specific
mechanism to do so.  The ``VHOST_USER_PROTOCOL_F_DEVICE_STATE`` feature
provides functionality to have the front-end include the back-end's
state in this transfer operation so the back-end does not need to
implement its own mechanism, and so the virtual machine may have its
complete state, including vhost-user devices' states, contained within a
single stream of data.

To do this, the back-end state is transferred from back-end to front-end
on the source side, and vice versa on the destination side.  This
transfer happens over a channel that is negotiated using the
``VHOST_USER_SET_DEVICE_STATE_FD`` message.  This message has two
parameters:

* Direction of transfer: On the source, the data is saved, transferring
  it from the back-end to the front-end.  On the destination, the data
  is loaded, transferring it from the front-end to the back-end.

* Migration phase: Currently, the only supported phase is the period
  after the transfer of memory-mapped regions before switch-over to the
  destination, when both the source and destination devices are
  suspended (:ref:`Suspended device state <suspended_device_state>`).
  In the future, additional phases might be supported to allow iterative
  migration while the device is running.

The nature of the channel is implementation-defined, but it must
generally behave like a pipe: The writing end will write all the data it
has into it, signalling the end of data by closing its end.  The reading
end must read all of this data (until encountering the end of file) and
process it.

* When saving, the writing end is the source back-end, and the reading
  end is the source front-end.  After reading the state data from the
  channel, the source front-end must transfer it to the destination
  front-end through an implementation-defined mechanism.

* When loading, the writing end is the destination front-end, and the
  reading end is the destination back-end.  After reading the state data
  from the channel, the destination back-end must deserialize its
  internal state from that data and set itself up to allow the driver to
  seamlessly resume operation on the VIRTIO level.

Seamlessly resuming operation means that the migration must be
transparent to the guest driver, which operates on the VIRTIO level.
This driver will not perform any re-initialization steps, but continue
to use the device as if no migration had occurred.  The vhost-user
front-end, however, will re-initialize the vhost state on the
destination, following the usual protocol for establishing a connection
to a vhost-user back-end: This includes, for example, setting up memory
mappings and kick and call FDs as necessary, negotiating protocol
features, or setting the initial vring base indices (to the same value
as on the source side, so that operation can resume).

Both on the source and on the destination side, after the respective
front-end has seen all data transferred (when the transfer FD has been
closed), it sends the ``VHOST_USER_CHECK_DEVICE_STATE`` message to
verify that data transfer was successful in the back-end, too.  The
back-end responds once it knows whether the transfer and processing was
successful or not.

Memory access
-------------

The front-end sends a list of vhost memory regions to the back-end using the
``VHOST_USER_SET_MEM_TABLE`` message.  Each region has two base
addresses: a guest address and a user address.

Messages contain guest addresses and/or user addresses to reference locations
within the shared memory.  The mapping of these addresses works as follows.

User addresses map to the vhost memory region containing that user address.

When the ``VIRTIO_F_IOMMU_PLATFORM`` feature has not been negotiated:

* Guest addresses map to the vhost memory region containing that guest
  address.

When the ``VIRTIO_F_IOMMU_PLATFORM`` feature has been negotiated:

* Guest addresses are also called I/O virtual addresses (IOVAs).  They are
  translated to user addresses via the IOTLB.

* The vhost memory region guest address is not used.

IOMMU support
-------------

When the ``VIRTIO_F_IOMMU_PLATFORM`` feature has been negotiated, the
front-end sends IOTLB entries update & invalidation by sending
``VHOST_USER_IOTLB_MSG`` requests to the back-end with a ``struct
vhost_iotlb_msg`` as payload. For update events, the ``iotlb`` payload
has to be filled with the update message type (2), the I/O virtual
address, the size, the user virtual address, and the permissions
flags. Addresses and size must be within vhost memory regions set via
the ``VHOST_USER_SET_MEM_TABLE`` request. For invalidation events, the
``iotlb`` payload has to be filled with the invalidation message type
(3), the I/O virtual address and the size. On success, the back-end is
expected to reply with a zero payload, non-zero otherwise.

The back-end relies on the back-end communication channel (see :ref:`Back-end
communication <backend_communication>` section below) to send IOTLB miss
and access failure events, by sending ``VHOST_USER_BACKEND_IOTLB_MSG``
requests to the front-end with a ``struct vhost_iotlb_msg`` as
payload. For miss events, the iotlb payload has to be filled with the
miss message type (1), the I/O virtual address and the permissions
flags. For access failure event, the iotlb payload has to be filled
with the access failure message type (4), the I/O virtual address and
the permissions flags.  For synchronization purpose, the back-end may
rely on the reply-ack feature, so the front-end may send a reply when
operation is completed if the reply-ack feature is negotiated and
back-ends requests a reply. For miss events, completed operation means
either front-end sent an update message containing the IOTLB entry
containing requested address and permission, or front-end sent nothing if
the IOTLB miss message is invalid (invalid IOVA or permission).

The front-end isn't expected to take the initiative to send IOTLB update
messages, as the back-end sends IOTLB miss messages for the guest virtual
memory areas it needs to access.

.. _backend_communication:

Back-end communication
----------------------

An optional communication channel is provided if the back-end declares
``VHOST_USER_PROTOCOL_F_BACKEND_REQ`` protocol feature, to allow the
back-end to make requests to the front-end.

The fd is provided via ``VHOST_USER_SET_BACKEND_REQ_FD`` ancillary data.

A back-end may then send ``VHOST_USER_BACKEND_*`` messages to the front-end
using this fd communication channel.

If ``VHOST_USER_PROTOCOL_F_BACKEND_SEND_FD`` protocol feature is
negotiated, back-end can send file descriptors (at most 8 descriptors in
each message) to front-end via ancillary data using this fd communication
channel.

Inflight I/O tracking
---------------------

To support reconnecting after restart or crash, back-end may need to
resubmit inflight I/Os. If virtqueue is processed in order, we can
easily achieve that by getting the inflight descriptors from
descriptor table (split virtqueue) or descriptor ring (packed
virtqueue). However, it can't work when we process descriptors
out-of-order because some entries which store the information of
inflight descriptors in available ring (split virtqueue) or descriptor
ring (packed virtqueue) might be overridden by new entries. To solve
this problem, the back-end need to allocate an extra buffer to store this
information of inflight descriptors and share it with front-end for
persistent. ``VHOST_USER_GET_INFLIGHT_FD`` and
``VHOST_USER_SET_INFLIGHT_FD`` are used to transfer this buffer
between front-end and back-end. And the format of this buffer is described
below:

+---------------+---------------+-----+---------------+
| queue0 region | queue1 region | ... | queueN region |
+---------------+---------------+-----+---------------+

N is the number of available virtqueues. The back-end could get it from num
queues field of ``VhostUserInflight``.

For split virtqueue, queue region can be implemented as:

.. code:: c

  typedef struct DescStateSplit {
      /* Indicate whether this descriptor is inflight or not.
       * Only available for head-descriptor. */
      uint8_t inflight;

      /* Padding */
      uint8_t padding[5];

      /* Maintain a list for the last batch of used descriptors.
       * Only available when batching is used for submitting */
      uint16_t next;

      /* Used to preserve the order of fetching available descriptors.
       * Only available for head-descriptor. */
      uint64_t counter;
  } DescStateSplit;

  typedef struct QueueRegionSplit {
      /* The feature flags of this region. Now it's initialized to 0. */
      uint64_t features;

      /* The version of this region. It's 1 currently.
       * Zero value indicates an uninitialized buffer */
      uint16_t version;

      /* The size of DescStateSplit array. It's equal to the virtqueue size.
       * The back-end could get it from queue size field of VhostUserInflight. */
      uint16_t desc_num;

      /* The head of list that track the last batch of used descriptors. */
      uint16_t last_batch_head;

      /* Store the idx value of used ring */
      uint16_t used_idx;

      /* Used to track the state of each descriptor in descriptor table */
      DescStateSplit desc[];
  } QueueRegionSplit;

To track inflight I/O, the queue region should be processed as follows:

When receiving available buffers from the driver:

#. Get the next available head-descriptor index from available ring, ``i``

#. Set ``desc[i].counter`` to the value of global counter

#. Increase global counter by 1

#. Set ``desc[i].inflight`` to 1

When supplying used buffers to the driver:

1. Get corresponding used head-descriptor index, i

2. Set ``desc[i].next`` to ``last_batch_head``

3. Set ``last_batch_head`` to ``i``

#. Steps 1,2,3 may be performed repeatedly if batching is possible

#. Increase the ``idx`` value of used ring by the size of the batch

#. Set the ``inflight`` field of each ``DescStateSplit`` entry in the batch to 0

#. Set ``used_idx`` to the ``idx`` value of used ring

When reconnecting:

#. If the value of ``used_idx`` does not match the ``idx`` value of
   used ring (means the inflight field of ``DescStateSplit`` entries in
   last batch may be incorrect),

   a. Subtract the value of ``used_idx`` from the ``idx`` value of
      used ring to get last batch size of ``DescStateSplit`` entries

   #. Set the ``inflight`` field of each ``DescStateSplit`` entry to 0 in last batch
      list which starts from ``last_batch_head``

   #. Set ``used_idx`` to the ``idx`` value of used ring

#. Resubmit inflight ``DescStateSplit`` entries in order of their
   counter value

For packed virtqueue, queue region can be implemented as:

.. code:: c

  typedef struct DescStatePacked {
      /* Indicate whether this descriptor is inflight or not.
       * Only available for head-descriptor. */
      uint8_t inflight;

      /* Padding */
      uint8_t padding;

      /* Link to the next free entry */
      uint16_t next;

      /* Link to the last entry of descriptor list.
       * Only available for head-descriptor. */
      uint16_t last;

      /* The length of descriptor list.
       * Only available for head-descriptor. */
      uint16_t num;

      /* Used to preserve the order of fetching available descriptors.
       * Only available for head-descriptor. */
      uint64_t counter;

      /* The buffer id */
      uint16_t id;

      /* The descriptor flags */
      uint16_t flags;

      /* The buffer length */
      uint32_t len;

      /* The buffer address */
      uint64_t addr;
  } DescStatePacked;

  typedef struct QueueRegionPacked {
      /* The feature flags of this region. Now it's initialized to 0. */
      uint64_t features;

      /* The version of this region. It's 1 currently.
       * Zero value indicates an uninitialized buffer */
      uint16_t version;

      /* The size of DescStatePacked array. It's equal to the virtqueue size.
       * The back-end could get it from queue size field of VhostUserInflight. */
      uint16_t desc_num;

      /* The head of free DescStatePacked entry list */
      uint16_t free_head;

      /* The old head of free DescStatePacked entry list */
      uint16_t old_free_head;

      /* The used index of descriptor ring */
      uint16_t used_idx;

      /* The old used index of descriptor ring */
      uint16_t old_used_idx;

      /* Device ring wrap counter */
      uint8_t used_wrap_counter;

      /* The old device ring wrap counter */
      uint8_t old_used_wrap_counter;

      /* Padding */
      uint8_t padding[7];

      /* Used to track the state of each descriptor fetched from descriptor ring */
      DescStatePacked desc[];
  } QueueRegionPacked;

To track inflight I/O, the queue region should be processed as follows:

When receiving available buffers from the driver:

#. Get the next available descriptor entry from descriptor ring, ``d``

#. If ``d`` is head descriptor,

   a. Set ``desc[old_free_head].num`` to 0

   #. Set ``desc[old_free_head].counter`` to the value of global counter

   #. Increase global counter by 1

   #. Set ``desc[old_free_head].inflight`` to 1

#. If ``d`` is last descriptor, set ``desc[old_free_head].last`` to
   ``free_head``

#. Increase ``desc[old_free_head].num`` by 1

#. Set ``desc[free_head].addr``, ``desc[free_head].len``,
   ``desc[free_head].flags``, ``desc[free_head].id`` to ``d.addr``,
   ``d.len``, ``d.flags``, ``d.id``

#. Set ``free_head`` to ``desc[free_head].next``

#. If ``d`` is last descriptor, set ``old_free_head`` to ``free_head``

When supplying used buffers to the driver:

1. Get corresponding used head-descriptor entry from descriptor ring,
   ``d``

2. Get corresponding ``DescStatePacked`` entry, ``e``

3. Set ``desc[e.last].next`` to ``free_head``

4. Set ``free_head`` to the index of ``e``

#. Steps 1,2,3,4 may be performed repeatedly if batching is possible

#. Increase ``used_idx`` by the size of the batch and update
   ``used_wrap_counter`` if needed

#. Update ``d.flags``

#. Set the ``inflight`` field of each head ``DescStatePacked`` entry
   in the batch to 0

#. Set ``old_free_head``,  ``old_used_idx``, ``old_used_wrap_counter``
   to ``free_head``, ``used_idx``, ``used_wrap_counter``

When reconnecting:

#. If ``used_idx`` does not match ``old_used_idx`` (means the
   ``inflight`` field of ``DescStatePacked`` entries in last batch may
   be incorrect),

   a. Get the next descriptor ring entry through ``old_used_idx``, ``d``

   #. Use ``old_used_wrap_counter`` to calculate the available flags

   #. If ``d.flags`` is not equal to the calculated flags value (means
      back-end has submitted the buffer to guest driver before crash, so
      it has to commit the in-progres update), set ``old_free_head``,
      ``old_used_idx``, ``old_used_wrap_counter`` to ``free_head``,
      ``used_idx``, ``used_wrap_counter``

#. Set ``free_head``, ``used_idx``, ``used_wrap_counter`` to
   ``old_free_head``, ``old_used_idx``, ``old_used_wrap_counter``
   (roll back any in-progress update)

#. Set the ``inflight`` field of each ``DescStatePacked`` entry in
   free list to 0

#. Resubmit inflight ``DescStatePacked`` entries in order of their
   counter value

In-band notifications
---------------------

In some limited situations (e.g. for simulation) it is desirable to
have the kick, call and error (if used) signals done via in-band
messages instead of asynchronous eventfd notifications. This can be
done by negotiating the ``VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS``
protocol feature.

Note that due to the fact that too many messages on the sockets can
cause the sending application(s) to block, it is not advised to use
this feature unless absolutely necessary. It is also considered an
error to negotiate this feature without also negotiating
``VHOST_USER_PROTOCOL_F_BACKEND_REQ`` and ``VHOST_USER_PROTOCOL_F_REPLY_ACK``,
the former is necessary for getting a message channel from the back-end
to the front-end, while the latter needs to be used with the in-band
notification messages to block until they are processed, both to avoid
blocking later and for proper processing (at least in the simulation
use case.) As it has no other way of signalling this error, the back-end
should close the connection as a response to a
``VHOST_USER_SET_PROTOCOL_FEATURES`` message that sets the in-band
notifications feature flag without the other two.

Protocol features
-----------------

.. code:: c

  #define VHOST_USER_PROTOCOL_F_MQ                    0
  #define VHOST_USER_PROTOCOL_F_LOG_SHMFD             1
  #define VHOST_USER_PROTOCOL_F_RARP                  2
  #define VHOST_USER_PROTOCOL_F_REPLY_ACK             3
  #define VHOST_USER_PROTOCOL_F_MTU                   4
  #define VHOST_USER_PROTOCOL_F_BACKEND_REQ           5
  #define VHOST_USER_PROTOCOL_F_CROSS_ENDIAN          6
  #define VHOST_USER_PROTOCOL_F_CRYPTO_SESSION        7
  #define VHOST_USER_PROTOCOL_F_PAGEFAULT             8
  #define VHOST_USER_PROTOCOL_F_CONFIG                9
  #define VHOST_USER_PROTOCOL_F_BACKEND_SEND_FD      10
  #define VHOST_USER_PROTOCOL_F_HOST_NOTIFIER        11
  #define VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD       12
  #define VHOST_USER_PROTOCOL_F_RESET_DEVICE         13
  #define VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS 14
  #define VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS  15
  #define VHOST_USER_PROTOCOL_F_STATUS               16
  #define VHOST_USER_PROTOCOL_F_XEN_MMAP             17
  #define VHOST_USER_PROTOCOL_F_SHARED_OBJECT        18
  #define VHOST_USER_PROTOCOL_F_DEVICE_STATE         19

Front-end message types
-----------------------

``VHOST_USER_GET_FEATURES``
  :id: 1
  :equivalent ioctl: ``VHOST_GET_FEATURES``
  :request payload: N/A
  :reply payload: ``u64``

  Get from the underlying vhost implementation the features bitmask.
  Feature bit ``VHOST_USER_F_PROTOCOL_FEATURES`` signals back-end support
  for ``VHOST_USER_GET_PROTOCOL_FEATURES`` and
  ``VHOST_USER_SET_PROTOCOL_FEATURES``.

``VHOST_USER_SET_FEATURES``
  :id: 2
  :equivalent ioctl: ``VHOST_SET_FEATURES``
  :request payload: ``u64``
  :reply payload: N/A

  Enable features in the underlying vhost implementation using a
  bitmask.  Feature bit ``VHOST_USER_F_PROTOCOL_FEATURES`` signals
  back-end support for ``VHOST_USER_GET_PROTOCOL_FEATURES`` and
  ``VHOST_USER_SET_PROTOCOL_FEATURES``.

``VHOST_USER_GET_PROTOCOL_FEATURES``
  :id: 15
  :equivalent ioctl: ``VHOST_GET_FEATURES``
  :request payload: N/A
  :reply payload: ``u64``

  Get the protocol feature bitmask from the underlying vhost
  implementation.  Only legal if feature bit
  ``VHOST_USER_F_PROTOCOL_FEATURES`` is present in
  ``VHOST_USER_GET_FEATURES``.  It does not need to be acknowledged by
  ``VHOST_USER_SET_FEATURES``.

.. Note::
   Back-ends that report ``VHOST_USER_F_PROTOCOL_FEATURES`` must
   support this message even before ``VHOST_USER_SET_FEATURES`` was
   called.

``VHOST_USER_SET_PROTOCOL_FEATURES``
  :id: 16
  :equivalent ioctl: ``VHOST_SET_FEATURES``
  :request payload: ``u64``
  :reply payload: N/A

  Enable protocol features in the underlying vhost implementation.

  Only legal if feature bit ``VHOST_USER_F_PROTOCOL_FEATURES`` is present in
  ``VHOST_USER_GET_FEATURES``.  It does not need to be acknowledged by
  ``VHOST_USER_SET_FEATURES``.

.. Note::
   Back-ends that report ``VHOST_USER_F_PROTOCOL_FEATURES`` must support
   this message even before ``VHOST_USER_SET_FEATURES`` was called.

``VHOST_USER_SET_OWNER``
  :id: 3
  :equivalent ioctl: ``VHOST_SET_OWNER``
  :request payload: N/A
  :reply payload: N/A

  Issued when a new connection is established. It marks the sender
  as the front-end that owns of the session. This can be used on the *back-end*
  as a "session start" flag.

``VHOST_USER_RESET_OWNER``
  :id: 4
  :request payload: N/A
  :reply payload: N/A

.. admonition:: Deprecated

   This is no longer used. Used to be sent to request disabling all
   rings, but some back-ends interpreted it to also discard connection
   state (this interpretation would lead to bugs).  It is recommended
   that back-ends either ignore this message, or use it to disable all
   rings.

``VHOST_USER_SET_MEM_TABLE``
  :id: 5
  :equivalent ioctl: ``VHOST_SET_MEM_TABLE``
  :request payload: multiple memory regions description
  :reply payload: (postcopy only) multiple memory regions description

  Sets the memory map regions on the back-end so it can translate the
  vring addresses. In the ancillary data there is an array of file
  descriptors for each memory mapped region. The size and ordering of
  the fds matches the number and ordering of memory regions.

  When ``VHOST_USER_POSTCOPY_LISTEN`` has been received,
  ``SET_MEM_TABLE`` replies with the bases of the memory mapped
  regions to the front-end.  The back-end must have mmap'd the regions but
  not yet accessed them and should not yet generate a userfault
  event.

.. Note::
   ``NEED_REPLY_MASK`` is not set in this case.  QEMU will then
   reply back to the list of mappings with an empty
   ``VHOST_USER_SET_MEM_TABLE`` as an acknowledgement; only upon
   reception of this message may the guest start accessing the memory
   and generating faults.

``VHOST_USER_SET_LOG_BASE``
  :id: 6
  :equivalent ioctl: ``VHOST_SET_LOG_BASE``
  :request payload: u64
  :reply payload: N/A

  Sets logging shared memory space.

  When the back-end has ``VHOST_USER_PROTOCOL_F_LOG_SHMFD`` protocol feature,
  the log memory fd is provided in the ancillary data of
  ``VHOST_USER_SET_LOG_BASE`` message, the size and offset of shared
  memory area provided in the message.

``VHOST_USER_SET_LOG_FD``
  :id: 7
  :equivalent ioctl: ``VHOST_SET_LOG_FD``
  :request payload: N/A
  :reply payload: N/A

  Sets the logging file descriptor, which is passed as ancillary data.

``VHOST_USER_SET_VRING_NUM``
  :id: 8
  :equivalent ioctl: ``VHOST_SET_VRING_NUM``
  :request payload: vring state description
  :reply payload: N/A

  Set the size of the queue.

``VHOST_USER_SET_VRING_ADDR``
  :id: 9
  :equivalent ioctl: ``VHOST_SET_VRING_ADDR``
  :request payload: vring address description
  :reply payload: N/A

  Sets the addresses of the different aspects of the vring.

``VHOST_USER_SET_VRING_BASE``
  :id: 10
  :equivalent ioctl: ``VHOST_SET_VRING_BASE``
  :request payload: vring descriptor index/indices
  :reply payload: N/A

  Sets the next index to use for descriptors in this vring:

  * For a split virtqueue, sets only the next descriptor index to
    process in the *Available Ring*.  The device is supposed to read the
    next index in the *Used Ring* from the respective vring structure in
    guest memory.

  * For a packed virtqueue, both indices are supplied, as they are not
    explicitly available in memory.

  Consequently, the payload type is specific to the type of virt queue
  (*a vring descriptor index for split virtqueues* vs. *vring descriptor
  indices for packed virtqueues*).

``VHOST_USER_GET_VRING_BASE``
  :id: 11
  :equivalent ioctl: ``VHOST_USER_GET_VRING_BASE``
  :request payload: vring state description
  :reply payload: vring descriptor index/indices

  Stops the vring and returns the current descriptor index or indices:

    * For a split virtqueue, returns only the 16-bit next descriptor
      index to process in the *Available Ring*.  Note that this may
      differ from the available ring index in the vring structure in
      memory, which points to where the driver will put new available
      descriptors.  For the *Used Ring*, the device only needs the next
      descriptor index at which to put new descriptors, which is the
      value in the vring structure in memory, so this value is not
      covered by this message.

    * For a packed virtqueue, neither index is explicitly available to
      read from memory, so both indices (as maintained by the device) are
      returned.

  Consequently, the payload type is specific to the type of virt queue
  (*a vring descriptor index for split virtqueues* vs. *vring descriptor
  indices for packed virtqueues*).

  When and as long as all of a device’s vrings are stopped, it is
  *suspended*, see :ref:`Suspended device state
  <suspended_device_state>`.

  The request payload’s *num* field is currently reserved and must be
  set to 0.

``VHOST_USER_SET_VRING_KICK``
  :id: 12
  :equivalent ioctl: ``VHOST_SET_VRING_KICK``
  :request payload: ``u64``
  :reply payload: N/A

  Set the event file descriptor for adding buffers to the vring. It is
  passed in the ancillary data.

  Bits (0-7) of the payload contain the vring index. Bit 8 is the
  invalid FD flag. This flag is set when there is no file descriptor
  in the ancillary data. This signals that polling should be used
  instead of waiting for the kick. Note that if the protocol feature
  ``VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS`` has been negotiated
  this message isn't necessary as the ring is also started on the
  ``VHOST_USER_VRING_KICK`` message, it may however still be used to
  set an event file descriptor (which will be preferred over the
  message) or to enable polling.

``VHOST_USER_SET_VRING_CALL``
  :id: 13
  :equivalent ioctl: ``VHOST_SET_VRING_CALL``
  :request payload: ``u64``
  :reply payload: N/A

  Set the event file descriptor to signal when buffers are used. It is
  passed in the ancillary data.

  Bits (0-7) of the payload contain the vring index. Bit 8 is the
  invalid FD flag. This flag is set when there is no file descriptor
  in the ancillary data. This signals that polling will be used
  instead of waiting for the call. Note that if the protocol features
  ``VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS`` and
  ``VHOST_USER_PROTOCOL_F_BACKEND_REQ`` have been negotiated this message
  isn't necessary as the ``VHOST_USER_BACKEND_VRING_CALL`` message can be
  used, it may however still be used to set an event file descriptor
  or to enable polling.

``VHOST_USER_SET_VRING_ERR``
  :id: 14
  :equivalent ioctl: ``VHOST_SET_VRING_ERR``
  :request payload: ``u64``
  :reply payload: N/A

  Set the event file descriptor to signal when error occurs. It is
  passed in the ancillary data.

  Bits (0-7) of the payload contain the vring index. Bit 8 is the
  invalid FD flag. This flag is set when there is no file descriptor
  in the ancillary data. Note that if the protocol features
  ``VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS`` and
  ``VHOST_USER_PROTOCOL_F_BACKEND_REQ`` have been negotiated this message
  isn't necessary as the ``VHOST_USER_BACKEND_VRING_ERR`` message can be
  used, it may however still be used to set an event file descriptor
  (which will be preferred over the message).

``VHOST_USER_GET_QUEUE_NUM``
  :id: 17
  :equivalent ioctl: N/A
  :request payload: N/A
  :reply payload: u64

  Query how many queues the back-end supports.

  This request should be sent only when ``VHOST_USER_PROTOCOL_F_MQ``
  is set in queried protocol features by
  ``VHOST_USER_GET_PROTOCOL_FEATURES``.

``VHOST_USER_SET_VRING_ENABLE``
  :id: 18
  :equivalent ioctl: N/A
  :request payload: vring state description
  :reply payload: N/A

  Signal the back-end to enable or disable corresponding vring.

  This request should be sent only when
  ``VHOST_USER_F_PROTOCOL_FEATURES`` has been negotiated.

``VHOST_USER_SEND_RARP``
  :id: 19
  :equivalent ioctl: N/A
  :request payload: ``u64``
  :reply payload: N/A

  Ask vhost user back-end to broadcast a fake RARP to notify the migration
  is terminated for guest that does not support GUEST_ANNOUNCE.

  Only legal if feature bit ``VHOST_USER_F_PROTOCOL_FEATURES`` is
  present in ``VHOST_USER_GET_FEATURES`` and protocol feature bit
  ``VHOST_USER_PROTOCOL_F_RARP`` is present in
  ``VHOST_USER_GET_PROTOCOL_FEATURES``.  The first 6 bytes of the
  payload contain the mac address of the guest to allow the vhost user
  back-end to construct and broadcast the fake RARP.

``VHOST_USER_NET_SET_MTU``
  :id: 20
  :equivalent ioctl: N/A
  :request payload: ``u64``
  :reply payload: N/A

  Set host MTU value exposed to the guest.

  This request should be sent only when ``VIRTIO_NET_F_MTU`` feature
  has been successfully negotiated, ``VHOST_USER_F_PROTOCOL_FEATURES``
  is present in ``VHOST_USER_GET_FEATURES`` and protocol feature bit
  ``VHOST_USER_PROTOCOL_F_NET_MTU`` is present in
  ``VHOST_USER_GET_PROTOCOL_FEATURES``.

  If ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is negotiated, the back-end must
  respond with zero in case the specified MTU is valid, or non-zero
  otherwise.

``VHOST_USER_SET_BACKEND_REQ_FD`` (previous name ``VHOST_USER_SET_SLAVE_REQ_FD``)
  :id: 21
  :equivalent ioctl: N/A
  :request payload: N/A
  :reply payload: N/A

  Set the socket file descriptor for back-end initiated requests. It is passed
  in the ancillary data.

  This request should be sent only when
  ``VHOST_USER_F_PROTOCOL_FEATURES`` has been negotiated, and protocol
  feature bit ``VHOST_USER_PROTOCOL_F_BACKEND_REQ`` bit is present in
  ``VHOST_USER_GET_PROTOCOL_FEATURES``.  If
  ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is negotiated, the back-end must
  respond with zero for success, non-zero otherwise.

``VHOST_USER_IOTLB_MSG``
  :id: 22
  :equivalent ioctl: N/A (equivalent to ``VHOST_IOTLB_MSG`` message type)
  :request payload: ``struct vhost_iotlb_msg``
  :reply payload: ``u64``

  Send IOTLB messages with ``struct vhost_iotlb_msg`` as payload.

  The front-end sends such requests to update and invalidate entries in the
  device IOTLB. The back-end has to acknowledge the request with sending
  zero as ``u64`` payload for success, non-zero otherwise.

  This request should be send only when ``VIRTIO_F_IOMMU_PLATFORM``
  feature has been successfully negotiated.

``VHOST_USER_SET_VRING_ENDIAN``
  :id: 23
  :equivalent ioctl: ``VHOST_SET_VRING_ENDIAN``
  :request payload: vring state description
  :reply payload: N/A

  Set the endianness of a VQ for legacy devices. Little-endian is
  indicated with state.num set to 0 and big-endian is indicated with
  state.num set to 1. Other values are invalid.

  This request should be sent only when
  ``VHOST_USER_PROTOCOL_F_CROSS_ENDIAN`` has been negotiated.
  Backends that negotiated this feature should handle both
  endiannesses and expect this message once (per VQ) during device
  configuration (ie. before the front-end starts the VQ).

``VHOST_USER_GET_CONFIG``
  :id: 24
  :equivalent ioctl: N/A
  :request payload: virtio device config space
  :reply payload: virtio device config space

  When ``VHOST_USER_PROTOCOL_F_CONFIG`` is negotiated, this message is
  submitted by the vhost-user front-end to fetch the contents of the
  virtio device configuration space, vhost-user back-end's payload size
  MUST match the front-end's request, vhost-user back-end uses zero length of
  payload to indicate an error to the vhost-user front-end. The vhost-user
  front-end may cache the contents to avoid repeated
  ``VHOST_USER_GET_CONFIG`` calls.

``VHOST_USER_SET_CONFIG``
  :id: 25
  :equivalent ioctl: N/A
  :request payload: virtio device config space
  :reply payload: N/A

  When ``VHOST_USER_PROTOCOL_F_CONFIG`` is negotiated, this message is
  submitted by the vhost-user front-end when the Guest changes the virtio
  device configuration space and also can be used for live migration
  on the destination host. The vhost-user back-end must check the flags
  field, and back-ends MUST NOT accept SET_CONFIG for read-only
  configuration space fields unless the live migration bit is set.

``VHOST_USER_CREATE_CRYPTO_SESSION``
  :id: 26
  :equivalent ioctl: N/A
  :request payload: crypto session description
  :reply payload: crypto session description

  Create a session for crypto operation. The back-end must return
  the session id, 0 or positive for success, negative for failure.
  This request should be sent only when
  ``VHOST_USER_PROTOCOL_F_CRYPTO_SESSION`` feature has been
  successfully negotiated.  It's a required feature for crypto
  devices.

``VHOST_USER_CLOSE_CRYPTO_SESSION``
  :id: 27
  :equivalent ioctl: N/A
  :request payload: ``u64``
  :reply payload: N/A

  Close a session for crypto operation which was previously
  created by ``VHOST_USER_CREATE_CRYPTO_SESSION``.

  This request should be sent only when
  ``VHOST_USER_PROTOCOL_F_CRYPTO_SESSION`` feature has been
  successfully negotiated.  It's a required feature for crypto
  devices.

``VHOST_USER_POSTCOPY_ADVISE``
  :id: 28
  :request payload: N/A
  :reply payload: userfault fd

  When ``VHOST_USER_PROTOCOL_F_PAGEFAULT`` is supported, the front-end
  advises back-end that a migration with postcopy enabled is underway,
  the back-end must open a userfaultfd for later use.  Note that at this
  stage the migration is still in precopy mode.

``VHOST_USER_POSTCOPY_LISTEN``
  :id: 29
  :request payload: N/A
  :reply payload: N/A

  The front-end advises back-end that a transition to postcopy mode has
  happened.  The back-end must ensure that shared memory is registered
  with userfaultfd to cause faulting of non-present pages.

  This is always sent sometime after a ``VHOST_USER_POSTCOPY_ADVISE``,
  and thus only when ``VHOST_USER_PROTOCOL_F_PAGEFAULT`` is supported.

``VHOST_USER_POSTCOPY_END``
  :id: 30
  :request payload: N/A
  :reply payload: ``u64``

  The front-end advises that postcopy migration has now completed.  The back-end
  must disable the userfaultfd. The reply is an acknowledgement
  only.

  When ``VHOST_USER_PROTOCOL_F_PAGEFAULT`` is supported, this message
  is sent at the end of the migration, after
  ``VHOST_USER_POSTCOPY_LISTEN`` was previously sent.

  The value returned is an error indication; 0 is success.

``VHOST_USER_GET_INFLIGHT_FD``
  :id: 31
  :equivalent ioctl: N/A
  :request payload: inflight description
  :reply payload: N/A

  When ``VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD`` protocol feature has
  been successfully negotiated, this message is submitted by the front-end to
  get a shared buffer from back-end. The shared buffer will be used to
  track inflight I/O by back-end. QEMU should retrieve a new one when vm
  reset.

``VHOST_USER_SET_INFLIGHT_FD``
  :id: 32
  :equivalent ioctl: N/A
  :request payload: inflight description
  :reply payload: N/A

  When ``VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD`` protocol feature has
  been successfully negotiated, this message is submitted by the front-end to
  send the shared inflight buffer back to the back-end so that the back-end
  could get inflight I/O after a crash or restart.

``VHOST_USER_GPU_SET_SOCKET``
  :id: 33
  :equivalent ioctl: N/A
  :request payload: N/A
  :reply payload: N/A

  Sets the GPU protocol socket file descriptor, which is passed as
  ancillary data. The GPU protocol is used to inform the front-end of
  rendering state and updates. See vhost-user-gpu.rst for details.

``VHOST_USER_RESET_DEVICE``
  :id: 34
  :equivalent ioctl: N/A
  :request payload: N/A
  :reply payload: N/A

  Ask the vhost user back-end to disable all rings and reset all
  internal device state to the initial state, ready to be
  reinitialized. The back-end retains ownership of the device
  throughout the reset operation.

  Only valid if the ``VHOST_USER_PROTOCOL_F_RESET_DEVICE`` protocol
  feature is set by the back-end.

``VHOST_USER_VRING_KICK``
  :id: 35
  :equivalent ioctl: N/A
  :request payload: vring state description
  :reply payload: N/A

  When the ``VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS`` protocol
  feature has been successfully negotiated, this message may be
  submitted by the front-end to indicate that a buffer was added to
  the vring instead of signalling it using the vring's kick file
  descriptor or having the back-end rely on polling.

  The state.num field is currently reserved and must be set to 0.

``VHOST_USER_GET_MAX_MEM_SLOTS``
  :id: 36
  :equivalent ioctl: N/A
  :request payload: N/A
  :reply payload: u64

  When the ``VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS`` protocol
  feature has been successfully negotiated, this message is submitted
  by the front-end to the back-end. The back-end should return the message with a
  u64 payload containing the maximum number of memory slots for
  QEMU to expose to the guest. The value returned by the back-end
  will be capped at the maximum number of ram slots which can be
  supported by the target platform.

``VHOST_USER_ADD_MEM_REG``
  :id: 37
  :equivalent ioctl: N/A
  :request payload: N/A
  :reply payload: single memory region description

  When the ``VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS`` protocol
  feature has been successfully negotiated, this message is submitted
  by the front-end to the back-end. The message payload contains a memory
  region descriptor struct, describing a region of guest memory which
  the back-end device must map in. When the
  ``VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS`` protocol feature has
  been successfully negotiated, along with the
  ``VHOST_USER_REM_MEM_REG`` message, this message is used to set and
  update the memory tables of the back-end device.

  Exactly one file descriptor from which the memory is mapped is
  passed in the ancillary data.

  In postcopy mode (see ``VHOST_USER_POSTCOPY_LISTEN``), the back-end
  replies with the bases of the memory mapped region to the front-end.
  For further details on postcopy, see ``VHOST_USER_SET_MEM_TABLE``.
  They apply to ``VHOST_USER_ADD_MEM_REG`` accordingly.

``VHOST_USER_REM_MEM_REG``
  :id: 38
  :equivalent ioctl: N/A
  :request payload: N/A
  :reply payload: single memory region description

  When the ``VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS`` protocol
  feature has been successfully negotiated, this message is submitted
  by the front-end to the back-end. The message payload contains a memory
  region descriptor struct, describing a region of guest memory which
  the back-end device must unmap. When the
  ``VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS`` protocol feature has
  been successfully negotiated, along with the
  ``VHOST_USER_ADD_MEM_REG`` message, this message is used to set and
  update the memory tables of the back-end device.

  The memory region to be removed is identified by its guest address,
  user address and size. The mmap offset is ignored.

  No file descriptors SHOULD be passed in the ancillary data. For
  compatibility with existing incorrect implementations, the back-end MAY
  accept messages with one file descriptor. If a file descriptor is
  passed, the back-end MUST close it without using it otherwise.

``VHOST_USER_SET_STATUS``
  :id: 39
  :equivalent ioctl: VHOST_VDPA_SET_STATUS
  :request payload: ``u64``
  :reply payload: N/A

  When the ``VHOST_USER_PROTOCOL_F_STATUS`` protocol feature has been
  successfully negotiated, this message is submitted by the front-end to
  notify the back-end with updated device status as defined in the Virtio
  specification.

``VHOST_USER_GET_STATUS``
  :id: 40
  :equivalent ioctl: VHOST_VDPA_GET_STATUS
  :request payload: N/A
  :reply payload: ``u64``

  When the ``VHOST_USER_PROTOCOL_F_STATUS`` protocol feature has been
  successfully negotiated, this message is submitted by the front-end to
  query the back-end for its device status as defined in the Virtio
  specification.

``VHOST_USER_GET_SHARED_OBJECT``
  :id: 41
  :equivalent ioctl: N/A
  :request payload: ``struct VhostUserShared``
  :reply payload: dmabuf fd

  When the ``VHOST_USER_PROTOCOL_F_SHARED_OBJECT`` protocol
  feature has been successfully negotiated, and the UUID is found
  in the exporters cache, this message is submitted by the front-end
  to retrieve a given dma-buf fd from a given back-end, determined by
  the requested UUID. Back-end will reply passing the fd when the operation
  is successful, or no fd otherwise.

``VHOST_USER_SET_DEVICE_STATE_FD``
  :id: 42
  :equivalent ioctl: N/A
  :request payload: device state transfer parameters
  :reply payload: ``u64``

  Front-end and back-end negotiate a channel over which to transfer the
  back-end’s internal state during migration.  Either side (front-end or
  back-end) may create the channel.  The nature of this channel is not
  restricted or defined in this document, but whichever side creates it
  must create a file descriptor that is provided to the respectively
  other side, allowing access to the channel.  This FD must behave as
  follows:

  * For the writing end, it must allow writing the whole back-end state
    sequentially.  Closing the file descriptor signals the end of
    transfer.

  * For the reading end, it must allow reading the whole back-end state
    sequentially.  The end of file signals the end of the transfer.

  For example, the channel may be a pipe, in which case the two ends of
  the pipe fulfill these requirements respectively.

  Initially, the front-end creates a channel along with such an FD.  It
  passes the FD to the back-end as ancillary data of a
  ``VHOST_USER_SET_DEVICE_STATE_FD`` message.  The back-end may create a
  different transfer channel, passing the respective FD back to the
  front-end as ancillary data of the reply.  If so, the front-end must
  then discard its channel and use the one provided by the back-end.

  Whether the back-end should decide to use its own channel is decided
  based on efficiency: If the channel is a pipe, both ends will most
  likely need to copy data into and out of it.  Any channel that allows
  for more efficient processing on at least one end, e.g. through
  zero-copy, is considered more efficient and thus preferred.  If the
  back-end can provide such a channel, it should decide to use it.

  The request payload contains parameters for the subsequent data
  transfer, as described in the :ref:`Migrating back-end state
  <migrating_backend_state>` section.

  The value returned is both an indication for success, and whether a
  file descriptor for a back-end-provided channel is returned: Bits 0–7
  are 0 on success, and non-zero on error.  Bit 8 is the invalid FD
  flag; this flag is set when there is no file descriptor returned.
  When this flag is not set, the front-end must use the returned file
  descriptor as its end of the transfer channel.  The back-end must not
  both indicate an error and return a file descriptor.

  Using this function requires prior negotiation of the
  ``VHOST_USER_PROTOCOL_F_DEVICE_STATE`` feature.

``VHOST_USER_CHECK_DEVICE_STATE``
  :id: 43
  :equivalent ioctl: N/A
  :request payload: N/A
  :reply payload: ``u64``

  After transferring the back-end’s internal state during migration (see
  the :ref:`Migrating back-end state <migrating_backend_state>`
  section), check whether the back-end was able to successfully fully
  process the state.

  The value returned indicates success or error; 0 is success, any
  non-zero value is an error.

  Using this function requires prior negotiation of the
  ``VHOST_USER_PROTOCOL_F_DEVICE_STATE`` feature.

Back-end message types
----------------------

For this type of message, the request is sent by the back-end and the reply
is sent by the front-end.

``VHOST_USER_BACKEND_IOTLB_MSG`` (previous name ``VHOST_USER_SLAVE_IOTLB_MSG``)
  :id: 1
  :equivalent ioctl: N/A (equivalent to ``VHOST_IOTLB_MSG`` message type)
  :request payload: ``struct vhost_iotlb_msg``
  :reply payload: N/A

  Send IOTLB messages with ``struct vhost_iotlb_msg`` as payload.
  The back-end sends such requests to notify of an IOTLB miss, or an IOTLB
  access failure. If ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is
  negotiated, and back-end set the ``VHOST_USER_NEED_REPLY`` flag, the front-end
  must respond with zero when operation is successfully completed, or
  non-zero otherwise.  This request should be send only when
  ``VIRTIO_F_IOMMU_PLATFORM`` feature has been successfully
  negotiated.

``VHOST_USER_BACKEND_CONFIG_CHANGE_MSG`` (previous name ``VHOST_USER_SLAVE_CONFIG_CHANGE_MSG``)
  :id: 2
  :equivalent ioctl: N/A
  :request payload: N/A
  :reply payload: N/A

  When ``VHOST_USER_PROTOCOL_F_CONFIG`` is negotiated, vhost-user
  back-end sends such messages to notify that the virtio device's
  configuration space has changed, for those host devices which can
  support such feature, host driver can send ``VHOST_USER_GET_CONFIG``
  message to the back-end to get the latest content. If
  ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is negotiated, and the back-end sets the
  ``VHOST_USER_NEED_REPLY`` flag, the front-end must respond with zero when
  operation is successfully completed, or non-zero otherwise.

``VHOST_USER_BACKEND_VRING_HOST_NOTIFIER_MSG`` (previous name ``VHOST_USER_SLAVE_VRING_HOST_NOTIFIER_MSG``)
  :id: 3
  :equivalent ioctl: N/A
  :request payload: vring area description
  :reply payload: N/A

  Sets host notifier for a specified queue. The queue index is
  contained in the ``u64`` field of the vring area description. The
  host notifier is described by the file descriptor (typically it's a
  VFIO device fd) which is passed as ancillary data and the size
  (which is mmap size and should be the same as host page size) and
  offset (which is mmap offset) carried in the vring area
  description. QEMU can mmap the file descriptor based on the size and
  offset to get a memory range. Registering a host notifier means
  mapping this memory range to the VM as the specified queue's notify
  MMIO region. The back-end sends this request to tell QEMU to de-register
  the existing notifier if any and register the new notifier if the
  request is sent with a file descriptor.

  This request should be sent only when
  ``VHOST_USER_PROTOCOL_F_HOST_NOTIFIER`` protocol feature has been
  successfully negotiated.

``VHOST_USER_BACKEND_VRING_CALL`` (previous name ``VHOST_USER_SLAVE_VRING_CALL``)
  :id: 4
  :equivalent ioctl: N/A
  :request payload: vring state description
  :reply payload: N/A

  When the ``VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS`` protocol
  feature has been successfully negotiated, this message may be
  submitted by the back-end to indicate that a buffer was used from
  the vring instead of signalling this using the vring's call file
  descriptor or having the front-end relying on polling.

  The state.num field is currently reserved and must be set to 0.

``VHOST_USER_BACKEND_VRING_ERR`` (previous name ``VHOST_USER_SLAVE_VRING_ERR``)
  :id: 5
  :equivalent ioctl: N/A
  :request payload: vring state description
  :reply payload: N/A

  When the ``VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS`` protocol
  feature has been successfully negotiated, this message may be
  submitted by the back-end to indicate that an error occurred on the
  specific vring, instead of signalling the error file descriptor
  set by the front-end via ``VHOST_USER_SET_VRING_ERR``.

  The state.num field is currently reserved and must be set to 0.

``VHOST_USER_BACKEND_SHARED_OBJECT_ADD``
  :id: 6
  :equivalent ioctl: N/A
  :request payload: ``struct VhostUserShared``
  :reply payload: N/A

  When the ``VHOST_USER_PROTOCOL_F_SHARED_OBJECT`` protocol
  feature has been successfully negotiated, this message can be submitted
  by the backends to add themselves as exporters to the virtio shared lookup
  table. The back-end device gets associated with a UUID in the shared table.
  The back-end is responsible of keeping its own table with exported dma-buf fds.
  When another back-end tries to import the resource associated with the UUID,
  it will send a message to the front-end, which will act as a proxy to the
  exporter back-end. If ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is negotiated, and
  the back-end sets the ``VHOST_USER_NEED_REPLY`` flag, the front-end must
  respond with zero when operation is successfully completed, or non-zero
  otherwise.

``VHOST_USER_BACKEND_SHARED_OBJECT_REMOVE``
  :id: 7
  :equivalent ioctl: N/A
  :request payload: ``struct VhostUserShared``
  :reply payload: N/A

  When the ``VHOST_USER_PROTOCOL_F_SHARED_OBJECT`` protocol
  feature has been successfully negotiated, this message can be submitted
  by the backend to remove themselves from to the virtio-dmabuf shared
  table API. The shared table will remove the back-end device associated with
  the UUID. If ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is negotiated, and the
  back-end sets the ``VHOST_USER_NEED_REPLY`` flag, the front-end must respond
  with zero when operation is successfully completed, or non-zero otherwise.

``VHOST_USER_BACKEND_SHARED_OBJECT_LOOKUP``
  :id: 8
  :equivalent ioctl: N/A
  :request payload: ``struct VhostUserShared``
  :reply payload: dmabuf fd and ``u64``

  When the ``VHOST_USER_PROTOCOL_F_SHARED_OBJECT`` protocol
  feature has been successfully negotiated, this message can be submitted
  by the backends to retrieve a given dma-buf fd from the virtio-dmabuf
  shared table given a UUID. Frontend will reply passing the fd and a zero
  when the operation is successful, or non-zero otherwise. Note that if the
  operation fails, no fd is sent to the backend.

.. _reply_ack:

VHOST_USER_PROTOCOL_F_REPLY_ACK
-------------------------------

The original vhost-user specification only demands replies for certain
commands. This differs from the vhost protocol implementation where
commands are sent over an ``ioctl()`` call and block until the back-end
has completed.

With this protocol extension negotiated, the sender (QEMU) can set the
``need_reply`` [Bit 3] flag to any command. This indicates that the
back-end MUST respond with a Payload ``VhostUserMsg`` indicating success
or failure. The payload should be set to zero on success or non-zero
on failure, unless the message already has an explicit reply body.

The reply payload gives QEMU a deterministic indication of the result
of the command. Today, QEMU is expected to terminate the main vhost-user
loop upon receiving such errors. In future, qemu could be taught to be more
resilient for selective requests.

For the message types that already solicit a reply from the back-end,
the presence of ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` or need_reply bit
being set brings no behavioural change. (See the Communication_
section for details.)

.. _backend_conventions:

Backend program conventions
===========================

vhost-user back-ends can provide various devices & services and may
need to be configured manually depending on the use case. However, it
is a good idea to follow the conventions listed here when
possible. Users, QEMU or libvirt, can then rely on some common
behaviour to avoid heterogeneous configuration and management of the
back-end programs and facilitate interoperability.

Each back-end installed on a host system should come with at least one
JSON file that conforms to the vhost-user.json schema. Each file
informs the management applications about the back-end type, and binary
location. In addition, it defines rules for management apps for
picking the highest priority back-end when multiple match the search
criteria (see ``@VhostUserBackend`` documentation in the schema file).

If the back-end is not capable of enabling a requested feature on the
host (such as 3D acceleration with virgl), or the initialization
failed, the back-end should fail to start early and exit with a status
!= 0. It may also print a message to stderr for further details.

The back-end program must not daemonize itself, but it may be
daemonized by the management layer. It may also have a restricted
access to the system.

File descriptors 0, 1 and 2 will exist, and have regular
stdin/stdout/stderr usage (they may have been redirected to /dev/null
by the management layer, or to a log handler).

The back-end program must end (as quickly and cleanly as possible) when
the SIGTERM signal is received. Eventually, it may receive SIGKILL by
the management layer after a few seconds.

The following command line options have an expected behaviour. They
are mandatory, unless explicitly said differently:

--socket-path=PATH

  This option specify the location of the vhost-user Unix domain socket.
  It is incompatible with --fd.

--fd=FDNUM

  When this argument is given, the back-end program is started with the
  vhost-user socket as file descriptor FDNUM. It is incompatible with
  --socket-path.

--print-capabilities

  Output to stdout the back-end capabilities in JSON format, and then
  exit successfully. Other options and arguments should be ignored, and
  the back-end program should not perform its normal function.  The
  capabilities can be reported dynamically depending on the host
  capabilities.

The JSON output is described in the ``vhost-user.json`` schema, by
```@VHostUserBackendCapabilities``.  Example:

.. code:: json

  {
    "type": "foo",
    "features": [
      "feature-a",
      "feature-b"
    ]
  }

vhost-user-input
----------------

Command line options:

--evdev-path=PATH

  Specify the linux input device.

  (optional)

--no-grab

  Do no request exclusive access to the input device.

  (optional)

vhost-user-gpu
--------------

Command line options:

--render-node=PATH

  Specify the GPU DRM render node.

  (optional)

--virgl

  Enable virgl rendering support.

  (optional)

vhost-user-blk
--------------

Command line options:

--blk-file=PATH

  Specify block device or file path.

  (optional)

--read-only

  Enable read-only.

  (optional)
