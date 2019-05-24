===================
Vhost-user Protocol
===================
:Copyright: 2014 Virtual Open Systems Sarl.
:Licence: This work is licensed under the terms of the GNU GPL,
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

The protocol defines 2 sides of the communication, *master* and
*slave*. *Master* is the application that shares its virtqueues, in
our case QEMU. *Slave* is the consumer of the virtqueues.

In the current implementation QEMU is the *master*, and the *slave* is
the external process consuming the virtio queues, for example a
software Ethernet switch running in user space, such as Snabbswitch,
or a block device backend processing read & write to a virtual
disk. In order to facilitate interoperability between various backend
implementations, it is recommended to follow the :ref:`Backend program
conventions <backend_conventions>`.

*Master* and *slave* can be either a client (i.e. connecting) or
server (listening) in the socket communication.

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
- Bit 2 is the reply flag - needs to be sent on each reply from the slave
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

Memory regions description
^^^^^^^^^^^^^^^^^^^^^^^^^^

+-------------+---------+---------+-----+---------+
| num regions | padding | region0 | ... | region7 |
+-------------+---------+---------+-----+---------+

:num regions: a 32-bit number of regions

:padding: 32-bit

A region is:

+---------------+------+--------------+-------------+
| guest address | size | user address | mmap offset |
+---------------+------+--------------+-------------+

:guest address: a 64-bit guest address of the region

:size: a 64-bit size

:user address: a 64-bit user address

:mmap offset: 64-bit offset where region starts in the mapped memory

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
  - 0: Vhost master messages used for writeable fields
  - 1: Vhost master messages used for live migration

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

The communication consists of *master* sending message requests and
*slave* sending message replies. Most of the requests don't require
replies. Here is a list of the ones that do:

* ``VHOST_USER_GET_FEATURES``
* ``VHOST_USER_GET_PROTOCOL_FEATURES``
* ``VHOST_USER_GET_VRING_BASE``
* ``VHOST_USER_SET_LOG_BASE`` (if ``VHOST_USER_PROTOCOL_F_LOG_SHMFD``)
* ``VHOST_USER_GET_INFLIGHT_FD`` (if ``VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD``)

.. seealso::

   :ref:`REPLY_ACK <reply_ack>`
       The section on ``REPLY_ACK`` protocol extension.

There are several messages that the master sends with file descriptors passed
in the ancillary data:

* ``VHOST_USER_SET_MEM_TABLE``
* ``VHOST_USER_SET_LOG_BASE`` (if ``VHOST_USER_PROTOCOL_F_LOG_SHMFD``)
* ``VHOST_USER_SET_LOG_FD``
* ``VHOST_USER_SET_VRING_KICK``
* ``VHOST_USER_SET_VRING_CALL``
* ``VHOST_USER_SET_VRING_ERR``
* ``VHOST_USER_SET_SLAVE_REQ_FD``
* ``VHOST_USER_SET_INFLIGHT_FD`` (if ``VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD``)

If *master* is unable to send the full message or receives a wrong
reply it will close the connection. An optional reconnection mechanism
can be implemented.

Any protocol extensions are gated by protocol feature bits, which
allows full backwards compatibility on both master and slave.  As
older slaves don't support negotiating protocol features, a feature
bit was dedicated for this purpose::

  #define VHOST_USER_F_PROTOCOL_FEATURES 30

Starting and stopping rings
---------------------------

Client must only process each ring when it is started.

Client must only pass data between the ring and the backend, when the
ring is enabled.

If ring is started but disabled, client must process the ring without
talking to the backend.

For example, for a networking device, in the disabled state client
must not supply any new RX packets, but must process and discard any
TX packets.

If ``VHOST_USER_F_PROTOCOL_FEATURES`` has not been negotiated, the
ring is initialized in an enabled state.

If ``VHOST_USER_F_PROTOCOL_FEATURES`` has been negotiated, the ring is
initialized in a disabled state. Client must not pass data to/from the
backend until ring is enabled by ``VHOST_USER_SET_VRING_ENABLE`` with
parameter 1, or after it has been disabled by
``VHOST_USER_SET_VRING_ENABLE`` with parameter 0.

Each ring is initialized in a stopped state, client must not process
it until ring is started, or after it has been stopped.

Client must start ring upon receiving a kick (that is, detecting that
file descriptor is readable) on the descriptor specified by
``VHOST_USER_SET_VRING_KICK``, and stop ring upon receiving
``VHOST_USER_GET_VRING_BASE``.

While processing the rings (whether they are enabled or not), client
must support changing some configuration aspects on the fly.

Multiple queue support
----------------------

Multiple queue is treated as a protocol extension, hence the slave has
to implement protocol features first. The multiple queues feature is
supported only when the protocol feature ``VHOST_USER_PROTOCOL_F_MQ``
(bit 0) is set.

The max number of queue pairs the slave supports can be queried with
message ``VHOST_USER_GET_QUEUE_NUM``. Master should stop when the
number of requested queues is bigger than that.

As all queues share one connection, the master uses a unique index for each
queue in the sent message to identify a specified queue. One queue pair
is enabled initially. More queues are enabled dynamically, by sending
message ``VHOST_USER_SET_VRING_ENABLE``.

Migration
---------

During live migration, the master may need to track the modifications
the slave makes to the memory mapped regions. The client should mark
the dirty pages in a log. Once it complies to this logging, it may
declare the ``VHOST_F_LOG_ALL`` vhost feature.

To start/stop logging of data/used ring writes, server may send
messages ``VHOST_USER_SET_FEATURES`` with ``VHOST_F_LOG_ALL`` and
``VHOST_USER_SET_VRING_ADDR`` with ``VHOST_VRING_F_LOG`` in ring's
flags set to 1/0, respectively.

All the modifications to memory pointed by vring "descriptor" should
be marked. Modifications to "used" vring should be marked if
``VHOST_VRING_F_LOG`` is part of ring's flags.

Dirty pages are of size::

  #define VHOST_LOG_PAGE 0x1000

The log memory fd is provided in the ancillary data of
``VHOST_USER_SET_LOG_BASE`` message when the slave has
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
ancillary data, it may be used to inform the master that the log has
been modified.

Once the source has finished migration, rings will be stopped by the
source. No further update must be done before rings are restarted.

In postcopy migration the slave is started before all the memory has
been received from the source host, and care must be taken to avoid
accessing pages that have yet to be received.  The slave opens a
'userfault'-fd and registers the memory with it; this fd is then
passed back over to the master.  The master services requests on the
userfaultfd for pages that are accessed and when the page is available
it performs WAKE ioctl's on the userfaultfd to wake the stalled
slave.  The client indicates support for this via the
``VHOST_USER_PROTOCOL_F_PAGEFAULT`` feature.

Memory access
-------------

The master sends a list of vhost memory regions to the slave using the
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
master sends IOTLB entries update & invalidation by sending
``VHOST_USER_IOTLB_MSG`` requests to the slave with a ``struct
vhost_iotlb_msg`` as payload. For update events, the ``iotlb`` payload
has to be filled with the update message type (2), the I/O virtual
address, the size, the user virtual address, and the permissions
flags. Addresses and size must be within vhost memory regions set via
the ``VHOST_USER_SET_MEM_TABLE`` request. For invalidation events, the
``iotlb`` payload has to be filled with the invalidation message type
(3), the I/O virtual address and the size. On success, the slave is
expected to reply with a zero payload, non-zero otherwise.

The slave relies on the slave communcation channel (see :ref:`Slave
communication <slave_communication>` section below) to send IOTLB miss
and access failure events, by sending ``VHOST_USER_SLAVE_IOTLB_MSG``
requests to the master with a ``struct vhost_iotlb_msg`` as
payload. For miss events, the iotlb payload has to be filled with the
miss message type (1), the I/O virtual address and the permissions
flags. For access failure event, the iotlb payload has to be filled
with the access failure message type (4), the I/O virtual address and
the permissions flags.  For synchronization purpose, the slave may
rely on the reply-ack feature, so the master may send a reply when
operation is completed if the reply-ack feature is negotiated and
slaves requests a reply. For miss events, completed operation means
either master sent an update message containing the IOTLB entry
containing requested address and permission, or master sent nothing if
the IOTLB miss message is invalid (invalid IOVA or permission).

The master isn't expected to take the initiative to send IOTLB update
messages, as the slave sends IOTLB miss messages for the guest virtual
memory areas it needs to access.

.. _slave_communication:

Slave communication
-------------------

An optional communication channel is provided if the slave declares
``VHOST_USER_PROTOCOL_F_SLAVE_REQ`` protocol feature, to allow the
slave to make requests to the master.

The fd is provided via ``VHOST_USER_SET_SLAVE_REQ_FD`` ancillary data.

A slave may then send ``VHOST_USER_SLAVE_*`` messages to the master
using this fd communication channel.

If ``VHOST_USER_PROTOCOL_F_SLAVE_SEND_FD`` protocol feature is
negotiated, slave can send file descriptors (at most 8 descriptors in
each message) to master via ancillary data using this fd communication
channel.

Inflight I/O tracking
---------------------

To support reconnecting after restart or crash, slave may need to
resubmit inflight I/Os. If virtqueue is processed in order, we can
easily achieve that by getting the inflight descriptors from
descriptor table (split virtqueue) or descriptor ring (packed
virtqueue). However, it can't work when we process descriptors
out-of-order because some entries which store the information of
inflight descriptors in available ring (split virtqueue) or descriptor
ring (packed virtqueue) might be overrided by new entries. To solve
this problem, slave need to allocate an extra buffer to store this
information of inflight descriptors and share it with master for
persistent. ``VHOST_USER_GET_INFLIGHT_FD`` and
``VHOST_USER_SET_INFLIGHT_FD`` are used to transfer this buffer
between master and slave. And the format of this buffer is described
below:

+---------------+---------------+-----+---------------+
| queue0 region | queue1 region | ... | queueN region |
+---------------+---------------+-----+---------------+

N is the number of available virtqueues. Slave could get it from num
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

      /* The size of DescStateSplit array. It's equal to the virtqueue
       * size. Slave could get it from queue size field of VhostUserInflight. */
      uint16_t desc_num;

      /* The head of list that track the last batch of used descriptors. */
      uint16_t last_batch_head;

      /* Store the idx value of used ring */
      uint16_t used_idx;

      /* Used to track the state of each descriptor in descriptor table */
      DescStateSplit desc[0];
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

      /* The size of DescStatePacked array. It's equal to the virtqueue
       * size. Slave could get it from queue size field of VhostUserInflight. */
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
      DescStatePacked desc[0];
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
      slave has submitted the buffer to guest driver before crash, so
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

Protocol features
-----------------

.. code:: c

  #define VHOST_USER_PROTOCOL_F_MQ             0
  #define VHOST_USER_PROTOCOL_F_LOG_SHMFD      1
  #define VHOST_USER_PROTOCOL_F_RARP           2
  #define VHOST_USER_PROTOCOL_F_REPLY_ACK      3
  #define VHOST_USER_PROTOCOL_F_MTU            4
  #define VHOST_USER_PROTOCOL_F_SLAVE_REQ      5
  #define VHOST_USER_PROTOCOL_F_CROSS_ENDIAN   6
  #define VHOST_USER_PROTOCOL_F_CRYPTO_SESSION 7
  #define VHOST_USER_PROTOCOL_F_PAGEFAULT      8
  #define VHOST_USER_PROTOCOL_F_CONFIG         9
  #define VHOST_USER_PROTOCOL_F_SLAVE_SEND_FD  10
  #define VHOST_USER_PROTOCOL_F_HOST_NOTIFIER  11
  #define VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD 12

Master message types
--------------------

``VHOST_USER_GET_FEATURES``
  :id: 1
  :equivalent ioctl: ``VHOST_GET_FEATURES``
  :master payload: N/A
  :slave payload: ``u64``

  Get from the underlying vhost implementation the features bitmask.
  Feature bit ``VHOST_USER_F_PROTOCOL_FEATURES`` signals slave support
  for ``VHOST_USER_GET_PROTOCOL_FEATURES`` and
  ``VHOST_USER_SET_PROTOCOL_FEATURES``.

``VHOST_USER_SET_FEATURES``
  :id: 2
  :equivalent ioctl: ``VHOST_SET_FEATURES``
  :master payload: ``u64``

  Enable features in the underlying vhost implementation using a
  bitmask.  Feature bit ``VHOST_USER_F_PROTOCOL_FEATURES`` signals
  slave support for ``VHOST_USER_GET_PROTOCOL_FEATURES`` and
  ``VHOST_USER_SET_PROTOCOL_FEATURES``.

``VHOST_USER_GET_PROTOCOL_FEATURES``
  :id: 15
  :equivalent ioctl: ``VHOST_GET_FEATURES``
  :master payload: N/A
  :slave payload: ``u64``

  Get the protocol feature bitmask from the underlying vhost
  implementation.  Only legal if feature bit
  ``VHOST_USER_F_PROTOCOL_FEATURES`` is present in
  ``VHOST_USER_GET_FEATURES``.

.. Note::
   Slave that reported ``VHOST_USER_F_PROTOCOL_FEATURES`` must
   support this message even before ``VHOST_USER_SET_FEATURES`` was
   called.

``VHOST_USER_SET_PROTOCOL_FEATURES``
  :id: 16
  :equivalent ioctl: ``VHOST_SET_FEATURES``
  :master payload: ``u64``

  Enable protocol features in the underlying vhost implementation.

  Only legal if feature bit ``VHOST_USER_F_PROTOCOL_FEATURES`` is present in
  ``VHOST_USER_GET_FEATURES``.

.. Note::
   Slave that reported ``VHOST_USER_F_PROTOCOL_FEATURES`` must support
   this message even before ``VHOST_USER_SET_FEATURES`` was called.

``VHOST_USER_SET_OWNER``
  :id: 3
  :equivalent ioctl: ``VHOST_SET_OWNER``
  :master payload: N/A

  Issued when a new connection is established. It sets the current
  *master* as an owner of the session. This can be used on the *slave*
  as a "session start" flag.

``VHOST_USER_RESET_OWNER``
  :id: 4
  :master payload: N/A

.. admonition:: Deprecated

   This is no longer used. Used to be sent to request disabling all
   rings, but some clients interpreted it to also discard connection
   state (this interpretation would lead to bugs).  It is recommended
   that clients either ignore this message, or use it to disable all
   rings.

``VHOST_USER_SET_MEM_TABLE``
  :id: 5
  :equivalent ioctl: ``VHOST_SET_MEM_TABLE``
  :master payload: memory regions description
  :slave payload: (postcopy only) memory regions description

  Sets the memory map regions on the slave so it can translate the
  vring addresses. In the ancillary data there is an array of file
  descriptors for each memory mapped region. The size and ordering of
  the fds matches the number and ordering of memory regions.

  When ``VHOST_USER_POSTCOPY_LISTEN`` has been received,
  ``SET_MEM_TABLE`` replies with the bases of the memory mapped
  regions to the master.  The slave must have mmap'd the regions but
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
  :master payload: u64
  :slave payload: N/A

  Sets logging shared memory space.

  When slave has ``VHOST_USER_PROTOCOL_F_LOG_SHMFD`` protocol feature,
  the log memory fd is provided in the ancillary data of
  ``VHOST_USER_SET_LOG_BASE`` message, the size and offset of shared
  memory area provided in the message.

``VHOST_USER_SET_LOG_FD``
  :id: 7
  :equivalent ioctl: ``VHOST_SET_LOG_FD``
  :master payload: N/A

  Sets the logging file descriptor, which is passed as ancillary data.

``VHOST_USER_SET_VRING_NUM``
  :id: 8
  :equivalent ioctl: ``VHOST_SET_VRING_NUM``
  :master payload: vring state description

  Set the size of the queue.

``VHOST_USER_SET_VRING_ADDR``
  :id: 9
  :equivalent ioctl: ``VHOST_SET_VRING_ADDR``
  :master payload: vring address description
  :slave payload: N/A

  Sets the addresses of the different aspects of the vring.

``VHOST_USER_SET_VRING_BASE``
  :id: 10
  :equivalent ioctl: ``VHOST_SET_VRING_BASE``
  :master payload: vring state description

  Sets the base offset in the available vring.

``VHOST_USER_GET_VRING_BASE``
  :id: 11
  :equivalent ioctl: ``VHOST_USER_GET_VRING_BASE``
  :master payload: vring state description
  :slave payload: vring state description

  Get the available vring base offset.

``VHOST_USER_SET_VRING_KICK``
  :id: 12
  :equivalent ioctl: ``VHOST_SET_VRING_KICK``
  :master payload: ``u64``

  Set the event file descriptor for adding buffers to the vring. It is
  passed in the ancillary data.

  Bits (0-7) of the payload contain the vring index. Bit 8 is the
  invalid FD flag. This flag is set when there is no file descriptor
  in the ancillary data. This signals that polling should be used
  instead of waiting for a kick.

``VHOST_USER_SET_VRING_CALL``
  :id: 13
  :equivalent ioctl: ``VHOST_SET_VRING_CALL``
  :master payload: ``u64``

  Set the event file descriptor to signal when buffers are used. It is
  passed in the ancillary data.

  Bits (0-7) of the payload contain the vring index. Bit 8 is the
  invalid FD flag. This flag is set when there is no file descriptor
  in the ancillary data. This signals that polling will be used
  instead of waiting for the call.

``VHOST_USER_SET_VRING_ERR``
  :id: 14
  :equivalent ioctl: ``VHOST_SET_VRING_ERR``
  :master payload: ``u64``

  Set the event file descriptor to signal when error occurs. It is
  passed in the ancillary data.

  Bits (0-7) of the payload contain the vring index. Bit 8 is the
  invalid FD flag. This flag is set when there is no file descriptor
  in the ancillary data.

``VHOST_USER_GET_QUEUE_NUM``
  :id: 17
  :equivalent ioctl: N/A
  :master payload: N/A
  :slave payload: u64

  Query how many queues the backend supports.

  This request should be sent only when ``VHOST_USER_PROTOCOL_F_MQ``
  is set in queried protocol features by
  ``VHOST_USER_GET_PROTOCOL_FEATURES``.

``VHOST_USER_SET_VRING_ENABLE``
  :id: 18
  :equivalent ioctl: N/A
  :master payload: vring state description

  Signal slave to enable or disable corresponding vring.

  This request should be sent only when
  ``VHOST_USER_F_PROTOCOL_FEATURES`` has been negotiated.

``VHOST_USER_SEND_RARP``
  :id: 19
  :equivalent ioctl: N/A
  :master payload: ``u64``

  Ask vhost user backend to broadcast a fake RARP to notify the migration
  is terminated for guest that does not support GUEST_ANNOUNCE.

  Only legal if feature bit ``VHOST_USER_F_PROTOCOL_FEATURES`` is
  present in ``VHOST_USER_GET_FEATURES`` and protocol feature bit
  ``VHOST_USER_PROTOCOL_F_RARP`` is present in
  ``VHOST_USER_GET_PROTOCOL_FEATURES``.  The first 6 bytes of the
  payload contain the mac address of the guest to allow the vhost user
  backend to construct and broadcast the fake RARP.

``VHOST_USER_NET_SET_MTU``
  :id: 20
  :equivalent ioctl: N/A
  :master payload: ``u64``

  Set host MTU value exposed to the guest.

  This request should be sent only when ``VIRTIO_NET_F_MTU`` feature
  has been successfully negotiated, ``VHOST_USER_F_PROTOCOL_FEATURES``
  is present in ``VHOST_USER_GET_FEATURES`` and protocol feature bit
  ``VHOST_USER_PROTOCOL_F_NET_MTU`` is present in
  ``VHOST_USER_GET_PROTOCOL_FEATURES``.

  If ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is negotiated, slave must
  respond with zero in case the specified MTU is valid, or non-zero
  otherwise.

``VHOST_USER_SET_SLAVE_REQ_FD``
  :id: 21
  :equivalent ioctl: N/A
  :master payload: N/A

  Set the socket file descriptor for slave initiated requests. It is passed
  in the ancillary data.

  This request should be sent only when
  ``VHOST_USER_F_PROTOCOL_FEATURES`` has been negotiated, and protocol
  feature bit ``VHOST_USER_PROTOCOL_F_SLAVE_REQ`` bit is present in
  ``VHOST_USER_GET_PROTOCOL_FEATURES``.  If
  ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is negotiated, slave must
  respond with zero for success, non-zero otherwise.

``VHOST_USER_IOTLB_MSG``
  :id: 22
  :equivalent ioctl: N/A (equivalent to ``VHOST_IOTLB_MSG`` message type)
  :master payload: ``struct vhost_iotlb_msg``
  :slave payload: ``u64``

  Send IOTLB messages with ``struct vhost_iotlb_msg`` as payload.

  Master sends such requests to update and invalidate entries in the
  device IOTLB. The slave has to acknowledge the request with sending
  zero as ``u64`` payload for success, non-zero otherwise.

  This request should be send only when ``VIRTIO_F_IOMMU_PLATFORM``
  feature has been successfully negotiated.

``VHOST_USER_SET_VRING_ENDIAN``
  :id: 23
  :equivalent ioctl: ``VHOST_SET_VRING_ENDIAN``
  :master payload: vring state description

  Set the endianness of a VQ for legacy devices. Little-endian is
  indicated with state.num set to 0 and big-endian is indicated with
  state.num set to 1. Other values are invalid.

  This request should be sent only when
  ``VHOST_USER_PROTOCOL_F_CROSS_ENDIAN`` has been negotiated.
  Backends that negotiated this feature should handle both
  endiannesses and expect this message once (per VQ) during device
  configuration (ie. before the master starts the VQ).

``VHOST_USER_GET_CONFIG``
  :id: 24
  :equivalent ioctl: N/A
  :master payload: virtio device config space
  :slave payload: virtio device config space

  When ``VHOST_USER_PROTOCOL_F_CONFIG`` is negotiated, this message is
  submitted by the vhost-user master to fetch the contents of the
  virtio device configuration space, vhost-user slave's payload size
  MUST match master's request, vhost-user slave uses zero length of
  payload to indicate an error to vhost-user master. The vhost-user
  master may cache the contents to avoid repeated
  ``VHOST_USER_GET_CONFIG`` calls.

``VHOST_USER_SET_CONFIG``
  :id: 25
  :equivalent ioctl: N/A
  :master payload: virtio device config space
  :slave payload: N/A

  When ``VHOST_USER_PROTOCOL_F_CONFIG`` is negotiated, this message is
  submitted by the vhost-user master when the Guest changes the virtio
  device configuration space and also can be used for live migration
  on the destination host. The vhost-user slave must check the flags
  field, and slaves MUST NOT accept SET_CONFIG for read-only
  configuration space fields unless the live migration bit is set.

``VHOST_USER_CREATE_CRYPTO_SESSION``
  :id: 26
  :equivalent ioctl: N/A
  :master payload: crypto session description
  :slave payload: crypto session description

  Create a session for crypto operation. The server side must return
  the session id, 0 or positive for success, negative for failure.
  This request should be sent only when
  ``VHOST_USER_PROTOCOL_F_CRYPTO_SESSION`` feature has been
  successfully negotiated.  It's a required feature for crypto
  devices.

``VHOST_USER_CLOSE_CRYPTO_SESSION``
  :id: 27
  :equivalent ioctl: N/A
  :master payload: ``u64``

  Close a session for crypto operation which was previously
  created by ``VHOST_USER_CREATE_CRYPTO_SESSION``.

  This request should be sent only when
  ``VHOST_USER_PROTOCOL_F_CRYPTO_SESSION`` feature has been
  successfully negotiated.  It's a required feature for crypto
  devices.

``VHOST_USER_POSTCOPY_ADVISE``
  :id: 28
  :master payload: N/A
  :slave payload: userfault fd

  When ``VHOST_USER_PROTOCOL_F_PAGEFAULT`` is supported, the master
  advises slave that a migration with postcopy enabled is underway,
  the slave must open a userfaultfd for later use.  Note that at this
  stage the migration is still in precopy mode.

``VHOST_USER_POSTCOPY_LISTEN``
  :id: 29
  :master payload: N/A

  Master advises slave that a transition to postcopy mode has
  happened.  The slave must ensure that shared memory is registered
  with userfaultfd to cause faulting of non-present pages.

  This is always sent sometime after a ``VHOST_USER_POSTCOPY_ADVISE``,
  and thus only when ``VHOST_USER_PROTOCOL_F_PAGEFAULT`` is supported.

``VHOST_USER_POSTCOPY_END``
  :id: 30
  :slave payload: ``u64``

  Master advises that postcopy migration has now completed.  The slave
  must disable the userfaultfd. The response is an acknowledgement
  only.

  When ``VHOST_USER_PROTOCOL_F_PAGEFAULT`` is supported, this message
  is sent at the end of the migration, after
  ``VHOST_USER_POSTCOPY_LISTEN`` was previously sent.

  The value returned is an error indication; 0 is success.

``VHOST_USER_GET_INFLIGHT_FD``
  :id: 31
  :equivalent ioctl: N/A
  :master payload: inflight description

  When ``VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD`` protocol feature has
  been successfully negotiated, this message is submitted by master to
  get a shared buffer from slave. The shared buffer will be used to
  track inflight I/O by slave. QEMU should retrieve a new one when vm
  reset.

``VHOST_USER_SET_INFLIGHT_FD``
  :id: 32
  :equivalent ioctl: N/A
  :master payload: inflight description

  When ``VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD`` protocol feature has
  been successfully negotiated, this message is submitted by master to
  send the shared inflight buffer back to slave so that slave could
  get inflight I/O after a crash or restart.

``VHOST_USER_GPU_SET_SOCKET``
  :id: 33
  :equivalent ioctl: N/A
  :master payload: N/A

  Sets the GPU protocol socket file descriptor, which is passed as
  ancillary data. The GPU protocol is used to inform the master of
  rendering state and updates. See vhost-user-gpu.rst for details.

Slave message types
-------------------

``VHOST_USER_SLAVE_IOTLB_MSG``
  :id: 1
  :equivalent ioctl: N/A (equivalent to ``VHOST_IOTLB_MSG`` message type)
  :slave payload: ``struct vhost_iotlb_msg``
  :master payload: N/A

  Send IOTLB messages with ``struct vhost_iotlb_msg`` as payload.
  Slave sends such requests to notify of an IOTLB miss, or an IOTLB
  access failure. If ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is
  negotiated, and slave set the ``VHOST_USER_NEED_REPLY`` flag, master
  must respond with zero when operation is successfully completed, or
  non-zero otherwise.  This request should be send only when
  ``VIRTIO_F_IOMMU_PLATFORM`` feature has been successfully
  negotiated.

``VHOST_USER_SLAVE_CONFIG_CHANGE_MSG``
  :id: 2
  :equivalent ioctl: N/A
  :slave payload: N/A
  :master payload: N/A

  When ``VHOST_USER_PROTOCOL_F_CONFIG`` is negotiated, vhost-user
  slave sends such messages to notify that the virtio device's
  configuration space has changed, for those host devices which can
  support such feature, host driver can send ``VHOST_USER_GET_CONFIG``
  message to slave to get the latest content. If
  ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` is negotiated, and slave set the
  ``VHOST_USER_NEED_REPLY`` flag, master must respond with zero when
  operation is successfully completed, or non-zero otherwise.

``VHOST_USER_SLAVE_VRING_HOST_NOTIFIER_MSG``
  :id: 3
  :equivalent ioctl: N/A
  :slave payload: vring area description
  :master payload: N/A

  Sets host notifier for a specified queue. The queue index is
  contained in the ``u64`` field of the vring area description. The
  host notifier is described by the file descriptor (typically it's a
  VFIO device fd) which is passed as ancillary data and the size
  (which is mmap size and should be the same as host page size) and
  offset (which is mmap offset) carried in the vring area
  description. QEMU can mmap the file descriptor based on the size and
  offset to get a memory range. Registering a host notifier means
  mapping this memory range to the VM as the specified queue's notify
  MMIO region. Slave sends this request to tell QEMU to de-register
  the existing notifier if any and register the new notifier if the
  request is sent with a file descriptor.

  This request should be sent only when
  ``VHOST_USER_PROTOCOL_F_HOST_NOTIFIER`` protocol feature has been
  successfully negotiated.

.. _reply_ack:

VHOST_USER_PROTOCOL_F_REPLY_ACK
-------------------------------

The original vhost-user specification only demands replies for certain
commands. This differs from the vhost protocol implementation where
commands are sent over an ``ioctl()`` call and block until the client
has completed.

With this protocol extension negotiated, the sender (QEMU) can set the
``need_reply`` [Bit 3] flag to any command. This indicates that the
client MUST respond with a Payload ``VhostUserMsg`` indicating success
or failure. The payload should be set to zero on success or non-zero
on failure, unless the message already has an explicit reply body.

The response payload gives QEMU a deterministic indication of the result
of the command. Today, QEMU is expected to terminate the main vhost-user
loop upon receiving such errors. In future, qemu could be taught to be more
resilient for selective requests.

For the message types that already solicit a reply from the client,
the presence of ``VHOST_USER_PROTOCOL_F_REPLY_ACK`` or need_reply bit
being set brings no behavioural change. (See the Communication_
section for details.)

.. _backend_conventions:

Backend program conventions
===========================

vhost-user backends can provide various devices & services and may
need to be configured manually depending on the use case. However, it
is a good idea to follow the conventions listed here when
possible. Users, QEMU or libvirt, can then rely on some common
behaviour to avoid heterogenous configuration and management of the
backend programs and facilitate interoperability.

Each backend installed on a host system should come with at least one
JSON file that conforms to the vhost-user.json schema. Each file
informs the management applications about the backend type, and binary
location. In addition, it defines rules for management apps for
picking the highest priority backend when multiple match the search
criteria (see ``@VhostUserBackend`` documentation in the schema file).

If the backend is not capable of enabling a requested feature on the
host (such as 3D acceleration with virgl), or the initialization
failed, the backend should fail to start early and exit with a status
!= 0. It may also print a message to stderr for further details.

The backend program must not daemonize itself, but it may be
daemonized by the management layer. It may also have a restricted
access to the system.

File descriptors 0, 1 and 2 will exist, and have regular
stdin/stdout/stderr usage (they may have been redirected to /dev/null
by the management layer, or to a log handler).

The backend program must end (as quickly and cleanly as possible) when
the SIGTERM signal is received. Eventually, it may receive SIGKILL by
the management layer after a few seconds.

The following command line options have an expected behaviour. They
are mandatory, unless explicitly said differently:

--socket-path=PATH

  This option specify the location of the vhost-user Unix domain socket.
  It is incompatible with --fd.

--fd=FDNUM

  When this argument is given, the backend program is started with the
  vhost-user socket as file descriptor FDNUM. It is incompatible with
  --socket-path.

--print-capabilities

  Output to stdout the backend capabilities in JSON format, and then
  exit successfully. Other options and arguments should be ignored, and
  the backend program should not perform its normal function.  The
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
