.. include:: <isonum.txt>
.. SPDX-License-Identifier: GPL-2.0-or-later

================================
vfio-user Protocol Specification
================================

.. contents:: Table of Contents

Introduction
============
vfio-user is a protocol that allows a device to be emulated in a separate
process outside of a Virtual Machine Monitor (VMM). vfio-user devices consist
of a generic VFIO device type, living inside the VMM, which we call the client,
and the core device implementation, living outside the VMM, which we call the
server.

The vfio-user specification is partly based on the
`Linux VFIO ioctl interface <https://www.kernel.org/doc/html/latest/driver-api/vfio.html>`_.

VFIO is a mature and stable API, backed by an extensively used framework. The
existing VFIO client implementation in QEMU (``qemu/hw/vfio/``) can be largely
re-used, though there is nothing in this specification that requires that
particular implementation. None of the VFIO kernel modules are required for
supporting the protocol, on either the client or server side. Some source
definitions in VFIO are re-used for vfio-user.

The main idea is to allow a virtual device to function in a separate process in
the same host over a UNIX domain socket. A UNIX domain socket (``AF_UNIX``) is
chosen because file descriptors can be trivially sent over it, which in turn
allows:

* Sharing of client memory for DMA with the server.
* Sharing of server memory with the client for fast MMIO.
* Efficient sharing of eventfd's for triggering interrupts.

Other socket types could be used which allow the server to run in a separate
guest in the same host (``AF_VSOCK``) or remotely (``AF_INET``). Theoretically
the underlying transport does not necessarily have to be a socket, however we do
not examine such alternatives. In this protocol version we focus on using a UNIX
domain socket and introduce basic support for the other two types of sockets
without considering performance implications.

While passing of file descriptors is desirable for performance reasons, support
is not necessary for either the client or the server in order to implement the
protocol. There is always an in-band, message-passing fall back mechanism.

Overview
========

VFIO is a framework that allows a physical device to be securely passed through
to a user space process; the device-specific kernel driver does not drive the
device at all.  Typically, the user space process is a VMM and the device is
passed through to it in order to achieve high performance. VFIO provides an API
and the required functionality in the kernel. QEMU has adopted VFIO to allow a
guest to directly access physical devices, instead of emulating them in
software.

vfio-user reuses the core VFIO concepts defined in its API, but implements them
as messages to be sent over a socket. It does not change the kernel-based VFIO
in any way, in fact none of the VFIO kernel modules need to be loaded to use
vfio-user. It is also possible for the client to concurrently use the current
kernel-based VFIO for one device, and vfio-user for another device.

VFIO Device Model
-----------------

A device under VFIO presents a standard interface to the user process. Many of
the VFIO operations in the existing interface use the ``ioctl()`` system call, and
references to the existing interface are called the ``ioctl()`` implementation in
this document.

The following sections describe the set of messages that implement the vfio-user
interface over a socket. In many cases, the messages are analogous to data
structures used in the ``ioctl()`` implementation. Messages derived from the
``ioctl()`` will have a name derived from the ``ioctl()`` command name.  E.g., the
``VFIO_DEVICE_GET_INFO`` ``ioctl()`` command becomes a
``VFIO_USER_DEVICE_GET_INFO`` message.  The purpose of this reuse is to share as
much code as feasible with the ``ioctl()`` implementation.

Connection Initiation
^^^^^^^^^^^^^^^^^^^^^

After the client connects to the server, the initial client message is
``VFIO_USER_VERSION`` to propose a protocol version and set of capabilities to
apply to the session. The server replies with a compatible version and set of
capabilities it supports, or closes the connection if it cannot support the
advertised version.

Device Information
^^^^^^^^^^^^^^^^^^

The client uses a ``VFIO_USER_DEVICE_GET_INFO`` message to query the server for
information about the device. This information includes:

* The device type and whether it supports reset (``VFIO_DEVICE_FLAGS_``),
* the number of device regions, and
* the device presents to the client the number of interrupt types the device
  supports.

Region Information
^^^^^^^^^^^^^^^^^^

The client uses ``VFIO_USER_DEVICE_GET_REGION_INFO`` messages to query the
server for information about the device's regions. This information describes:

* Read and write permissions, whether it can be memory mapped, and whether it
  supports additional capabilities (``VFIO_REGION_INFO_CAP_``).
* Region index, size, and offset.

When a device region can be mapped by the client, the server provides a file
descriptor which the client can ``mmap()``. The server is responsible for
polling for client updates to memory mapped regions.

Region Capabilities
"""""""""""""""""""

Some regions have additional capabilities that cannot be described adequately
by the region info data structure. These capabilities are returned in the
region info reply in a list similar to PCI capabilities in a PCI device's
configuration space.

Sparse Regions
""""""""""""""
A region can be memory-mappable in whole or in part. When only a subset of a
region can be mapped by the client, a ``VFIO_REGION_INFO_CAP_SPARSE_MMAP``
capability is included in the region info reply. This capability describes
which portions can be mapped by the client.

.. Note::
   For example, in a virtual NVMe controller, sparse regions can be used so
   that accesses to the NVMe registers (found in the beginning of BAR0) are
   trapped (an infrequent event), while allowing direct access to the doorbells
   (an extremely frequent event as every I/O submission requires a write to
   BAR0), found in the next page after the NVMe registers in BAR0.

Device-Specific Regions
"""""""""""""""""""""""

A device can define regions additional to the standard ones (e.g. PCI indexes
0-8). This is achieved by including a ``VFIO_REGION_INFO_CAP_TYPE`` capability
in the region info reply of a device-specific region. Such regions are reflected
in ``struct vfio_user_device_info.num_regions``. Thus, for PCI devices this
value can be equal to, or higher than, ``VFIO_PCI_NUM_REGIONS``.

Region I/O via file descriptors
-------------------------------

For unmapped regions, region I/O from the client is done via
``VFIO_USER_REGION_READ/WRITE``.  As an optimization, ioeventfds or ioregionfds
may be configured for sub-regions of some regions. A client may request
information on these sub-regions via ``VFIO_USER_DEVICE_GET_REGION_IO_FDS``; by
configuring the returned file descriptors as ioeventfds or ioregionfds, the
server can be directly notified of I/O (for example, by KVM) without taking a
trip through the client.

Interrupts
^^^^^^^^^^

The client uses ``VFIO_USER_DEVICE_GET_IRQ_INFO`` messages to query the server
for the device's interrupt types. The interrupt types are specific to the bus
the device is attached to, and the client is expected to know the capabilities
of each interrupt type. The server can signal an interrupt by directly injecting
interrupts into the guest via an event file descriptor. The client configures
how the server signals an interrupt with ``VFIO_USER_SET_IRQS`` messages.

Device Read and Write
^^^^^^^^^^^^^^^^^^^^^

When the guest executes load or store operations to an unmapped device region,
the client forwards these operations to the server with
``VFIO_USER_REGION_READ`` or ``VFIO_USER_REGION_WRITE`` messages. The server
will reply with data from the device on read operations or an acknowledgement on
write operations. See `Read and Write Operations`_.

Client memory access
--------------------

The client uses ``VFIO_USER_DMA_MAP`` and ``VFIO_USER_DMA_UNMAP`` messages to
inform the server of the valid DMA ranges that the server can access on behalf
of a device (typically, VM guest memory). DMA memory may be accessed by the
server via ``VFIO_USER_DMA_READ`` and ``VFIO_USER_DMA_WRITE`` messages over the
socket. In this case, the "DMA" part of the naming is a misnomer.

Actual direct memory access of client memory from the server is possible if the
client provides file descriptors the server can ``mmap()``. Note that ``mmap()``
privileges cannot be revoked by the client, therefore file descriptors should
only be exported in environments where the client trusts the server not to
corrupt guest memory.

See `Read and Write Operations`_.

Client/server interactions
==========================

Socket
------

A server can serve:

1) one or more clients, and/or
2) one or more virtual devices, belonging to one or more clients.

The current protocol specification requires dedicated sockets per
client/server connection. Commands in the client-to-server direction are
handled on the main communication socket which the client connects to, and
replies to these commands are passed on the same socket. Commands sent in the
other direction from the server to the client as well as their corresponding
replies can optionally be passed across a separate socket, which is set up
during negotiation (AF_UNIX servers just pass the file descriptor).

Using separate sockets for each command channel avoids introducing an
artificial point of synchronization between the channels. This simplifies
implementations since it obviates the need to demultiplex incoming messages
into commands and replies and interleave command handling and reply processing.
Note that it is still illegal for implementations to stall command or reply
processing indefinitely while waiting for replies on the other channel, as this
may lead to deadlocks. However, since incoming commands and requests arrive on
different sockets, it's possible to meet this requirement e.g. by running two
independent request processing threads that can internally operate
synchronously. It is expected that this is simpler to implement than fully
asynchronous message handling code. Implementations may still choose a fully
asynchronous, event-based design for other reasons, and the protocol fully
supports it.

It is a server-side implementation detail whether a single server handles
multiple virtual devices from the same or multiple clients. The location of the
socket is implementation-specific. Multiplexing clients, devices, and servers
over the same socket is not supported in this version of the protocol.

Authentication
--------------

For ``AF_UNIX``, we rely on OS mandatory access controls on the socket files,
therefore it is up to the management layer to set up the socket as required.
Socket types that span guests or hosts will require a proper authentication
mechanism. Defining that mechanism is deferred to a future version of the
protocol.

Command Concurrency
-------------------

A client may pipeline multiple commands without waiting for previous command
replies.  The server will process commands in the order they are received.  A
consequence of this is if a client issues a command with the *No_reply* bit,
then subsequently issues a command without *No_reply*, the older command will
have been processed before the reply to the younger command is sent by the
server.  The client must be aware of the device's capability to process
concurrent commands if pipelining is used.  For example, pipelining allows
multiple client threads to concurrently access device regions; the client must
ensure these accesses obey device semantics.

An example is a frame buffer device, where the device may allow concurrent
access to different areas of video memory, but may have indeterminate behavior
if concurrent accesses are performed to command or status registers.

Note that unrelated messages sent from the server to the client can appear in
between a client to server request/reply and vice versa.

Implementers should be prepared for certain commands to exhibit potentially
unbounded latencies.  For example, ``VFIO_USER_DEVICE_RESET`` may take an
arbitrarily long time to complete; clients should take care not to block
unnecessarily.

Socket Disconnection Behavior
-----------------------------
The server and the client can disconnect from each other, either intentionally
or unexpectedly. Both the client and the server need to know how to handle such
events.

Server Disconnection
^^^^^^^^^^^^^^^^^^^^
A server disconnecting from the client may indicate that:

1) A virtual device has been restarted, either intentionally (e.g. because of a
   device update) or unintentionally (e.g. because of a crash).
2) A virtual device has been shut down with no intention to be restarted.

It is impossible for the client to know whether or not a failure is
intermittent or innocuous and should be retried, therefore the client should
reset the VFIO device when it detects the socket has been disconnected.
Error recovery will be driven by the guest's device error handling
behavior.

Client Disconnection
^^^^^^^^^^^^^^^^^^^^
The client disconnecting from the server primarily means that the client
has exited. Currently, this means that the guest is shut down so the device is
no longer needed therefore the server can automatically exit. However, there
can be cases where a client disconnection should not result in a server exit:

1) A single server serving multiple clients.
2) A multi-process QEMU upgrading itself step by step, which is not yet
   implemented.

Therefore in order for the protocol to be forward compatible, the server should
respond to a client disconnection as follows:

 - all client memory regions are unmapped and cleaned up (including closing any
   passed file descriptors)
 - all IRQ file descriptors passed from the old client are closed
 - the device state should otherwise be retained

The expectation is that when a client reconnects, it will re-establish IRQ and
client memory mappings.

If anything happens to the client (such as qemu really did exit), the control
stack will know about it and can clean up resources accordingly.

Security Considerations
-----------------------

Speaking generally, vfio-user clients should not trust servers, and vice versa.
Standard tools and mechanisms should be used on both sides to validate input and
prevent against denial of service scenarios, buffer overflow, etc.

Request Retry and Response Timeout
----------------------------------
A failed command is a command that has been successfully sent and has been
responded to with an error code. Failure to send the command in the first place
(e.g. because the socket is disconnected) is a different type of error examined
earlier in the disconnect section.

.. Note::
   QEMU's VFIO retries certain operations if they fail. While this makes sense
   for real HW, we don't know for sure whether it makes sense for virtual
   devices.

Defining a retry and timeout scheme is deferred to a future version of the
protocol.

Message sizes
-------------

Some requests have an ``argsz`` field. In a request, it defines the maximum
expected reply payload size, which should be at least the size of the fixed
reply payload headers defined here. The *request* payload size is defined by the
usual ``msg_size`` field in the header, not the ``argsz`` field.

In a reply, the server sets ``argsz`` field to the size needed for a full
payload size. This may be less than the requested maximum size. This may be
larger than the requested maximum size: in that case, the full payload is not
included in the reply, but the ``argsz`` field in the reply indicates the needed
size, allowing a client to allocate a larger buffer for holding the reply before
trying again.

In addition, during negotiation (see  `Version`_), the client and server may
each specify a ``max_data_xfer_size`` value; this defines the maximum data that
may be read or written via one of the ``VFIO_USER_DMA/REGION_READ/WRITE``
messages; see `Read and Write Operations`_.

Protocol Specification
======================

To distinguish from the base VFIO symbols, all vfio-user symbols are prefixed
with ``vfio_user`` or ``VFIO_USER``. In this revision, all data is in the
endianness of the host system, although this may be relaxed in future
revisions in cases where the client and server run on different hosts
with different endianness.

Unless otherwise specified, all sizes should be presumed to be in bytes.

.. _Commands:

Commands
--------
The following table lists the VFIO message command IDs, and whether the
message command is sent from the client or the server.

======================================  =========  =================
Name                                    Command    Request Direction
======================================  =========  =================
``VFIO_USER_VERSION``                   1          client -> server
``VFIO_USER_DMA_MAP``                   2          client -> server
``VFIO_USER_DMA_UNMAP``                 3          client -> server
``VFIO_USER_DEVICE_GET_INFO``           4          client -> server
``VFIO_USER_DEVICE_GET_REGION_INFO``    5          client -> server
``VFIO_USER_DEVICE_GET_REGION_IO_FDS``  6          client -> server
``VFIO_USER_DEVICE_GET_IRQ_INFO``       7          client -> server
``VFIO_USER_DEVICE_SET_IRQS``           8          client -> server
``VFIO_USER_REGION_READ``               9          client -> server
``VFIO_USER_REGION_WRITE``              10         client -> server
``VFIO_USER_DMA_READ``                  11         server -> client
``VFIO_USER_DMA_WRITE``                 12         server -> client
``VFIO_USER_DEVICE_RESET``              13         client -> server
``VFIO_USER_REGION_WRITE_MULTI``        15         client -> server
``VFIO_USER_DEVICE_FEATURE``            16         client -> server
``VFIO_USER_MIG_DATA_READ``             17         client -> server
``VFIO_USER_MIG_DATA_WRITE``            18         client -> server
======================================  =========  =================

Header
------

All messages, both command messages and reply messages, are preceded by a
16-byte header that contains basic information about the message. The header is
followed by message-specific data described in the sections below.

+----------------+--------+-------------+
| Name           | Offset | Size        |
+================+========+=============+
| Message ID     | 0      | 2           |
+----------------+--------+-------------+
| Command        | 2      | 2           |
+----------------+--------+-------------+
| Message size   | 4      | 4           |
+----------------+--------+-------------+
| Flags          | 8      | 4           |
+----------------+--------+-------------+
|                | +-----+------------+ |
|                | | Bit | Definition | |
|                | +=====+============+ |
|                | | 0-3 | Type       | |
|                | +-----+------------+ |
|                | | 4   | No_reply   | |
|                | +-----+------------+ |
|                | | 5   | Error      | |
|                | +-----+------------+ |
+----------------+--------+-------------+
| Error          | 12     | 4           |
+----------------+--------+-------------+
| <message data> | 16     | variable    |
+----------------+--------+-------------+

* *Message ID* identifies the message, and is echoed in the command's reply
  message. Message IDs belong entirely to the sender, can be re-used (even
  concurrently) and the receiver must not make any assumptions about their
  uniqueness.
* *Command* specifies the command to be executed, listed in Commands_. It is
  also set in the reply header.
* *Message size* contains the size of the entire message, including the header.
* *Flags* contains attributes of the message:

  * The *Type* bits indicate the message type.

    *  *Command* (value 0x0) indicates a command message.
    *  *Reply* (value 0x1) indicates a reply message acknowledging a previous
       command with the same message ID.
  * *No_reply* in a command message indicates that no reply is needed for this
    command.  This is commonly used when multiple commands are sent, and only
    the last needs acknowledgement.
  * *Error* in a reply message indicates the command being acknowledged had
    an error. In this case, the *Error* field will be valid.

* *Error* in a reply message is an optional UNIX errno value. It may be zero
  even if the Error bit is set in Flags. It is reserved in a command message.

Each command message in Commands_ must be replied to with a reply message,
unless the message sets the *No_Reply* bit.  The reply consists of the header
with the *Reply* bit set, plus any additional data.

If an error occurs, the reply message must only include the reply header.

As the header is standard in both requests and replies, it is not included in
the command-specific specifications below; each message definition should be
appended to the standard header, and the offsets are given from the end of the
standard header.

``VFIO_USER_VERSION``
---------------------

.. _Version:

This is the initial message sent by the client after the socket connection is
established; the same format is used for the server's reply.

Upon establishing a connection, the client must send a ``VFIO_USER_VERSION``
message proposing a protocol version and a set of capabilities. The server
compares these with the versions and capabilities it supports and sends a
``VFIO_USER_VERSION`` reply according to the following rules.

* The major version in the reply must be the same as proposed. If the client
  does not support the proposed major, it closes the connection.
* The minor version in the reply must be equal to or less than the minor
  version proposed.
* The capability list must be a subset of those proposed. If the server
  requires a capability the client did not include, it closes the connection.

The protocol major version will only change when incompatible protocol changes
are made, such as changing the message format. The minor version may change
when compatible changes are made, such as adding new messages or capabilities,
Both the client and server must support all minor versions less than the
maximum minor version it supports. E.g., an implementation that supports
version 1.3 must also support 1.0 through 1.2.

When making a change to this specification, the protocol version number must
be included in the form "added in version X.Y"

Request
^^^^^^^

==============  ======  ====
Name            Offset  Size
==============  ======  ====
version major   0       2
version minor   2       2
version data    4       variable (including terminating NUL). Optional.
==============  ======  ====

The version data is an optional UTF-8 encoded JSON byte array with the following
format:

+--------------+--------+-----------------------------------+
| Name         | Type   | Description                       |
+==============+========+===================================+
| capabilities | object | Contains common capabilities that |
|              |        | the sender supports. Optional.    |
+--------------+--------+-----------------------------------+

Capabilities:

+--------------------+---------+-----------------------------------------------+
| Name               | Type    | Description                                   |
+====================+=========+===============================================+
| max_msg_fds        | number  | Maximum number of file descriptors that can   |
|                    |         | be received by the sender in one message.     |
|                    |         | Optional. If not specified then the receiver  |
|                    |         | must assume a value of ``1``.                 |
+--------------------+---------+-----------------------------------------------+
| max_data_xfer_size | number  | Maximum ``count`` for data transfer messages; |
|                    |         | see `Read and Write Operations`_. Optional,   |
|                    |         | with a default value of 1048576 bytes.        |
+--------------------+---------+-----------------------------------------------+
| max_dma_maps       | number  | Maximum number DMA map windows that can be    |
|                    |         | valid simultaneously.  Optional, with a       |
|                    |         | value of 65535 (64k-1).                       |
+--------------------+---------+-----------------------------------------------+
| pgsizes            | number  | Page sizes supported in DMA map operations    |
|                    |         | or'ed together. Optional, with a default      |
|                    |         | value of supporting only 4k pages.            |
+--------------------+---------+-----------------------------------------------+
| twin_socket        | object  | Parameters for twin-socket mode, which        |
|                    |         | handles server-to-client commands and their   |
|                    |         | replies on a separate socket. Optional.       |
+--------------------+---------+-----------------------------------------------+
| write_multiple     | boolean | ``VFIO_USER_REGION_WRITE_MULTI`` messages     |
|                    |         | are supported if the value is ``true``.       |
+--------------------+---------+-----------------------------------------------+

The ``twin_socket`` capability object holds these name/value pairs:

+-----------+---------+--------------------------------------------------------+
| Name      | Type    | Description                                            |
+===========+=========+========================================================+
| supported | boolean | Indicates whether the sender supports twin-socket      |
|           |         | mode. Optional, defaults to false.                     |
+-----------+---------+--------------------------------------------------------+
| fd_index  | number  | Specifies an index in the file descriptor array        |
|           |         | included with the message. The designated file         |
|           |         | descriptor is a socket which is to be used for the     |
|           |         | server-to-client command channel. Optional, only valid |
|           |         | in the reply message.                                  |
+-----------+---------+--------------------------------------------------------+

Reply
^^^^^

The same message format is used in the server's reply with the semantics
described above.

If and only if the client has indicated support for twin-socket mode by setting
``twin_socket.supported`` to true in its capabilities, the server may optionally
set up a separate command channel for server-to-client commands and their
replies. The server enables twin-socket mode as follows:

* Create a fresh socket pair.
* Keep the server end of the socket pair and pass the client end in the file
  descriptor array included with the reply message.
* Set ``twin_socket.supported`` to true in the reply.
* Indicate the index in the file descriptor array by the
  ``twin_socket.fd_index`` capability field in the reply, so the client can
  identify the correct file descriptor to use.

A client requesting twin-socket mode must examine the ``twin_socket`` capability
in the reply:

* If ``twin_socket.supported`` is false, the field is missing, or the entire
  ``twin_socket`` object is absent, the server does not support twin-socket mode
  or decided not to enable it. The client can choose whether it wants to proceed
  without twin-socket mode, or close the connection if not.
* If ``twin_socket.supported`` is true and ``twin_socket.fd_index`` is present
  and refers to a valid file descriptor, twin-socket mode negotiation has
  succeeded. The client monitors the provided file descriptor for commands from
  the server.
* Otherwise, the reply from the server is inconsistent. The client must abort
  and close the connection since it is potentially unable to receive commands
  from the server.

The twin-socket feature is optional, so some servers may not support it.
However, for server implementations that do send server-to-client commands it is
strongly recommended to implement twin-socket support.

``VFIO_USER_DMA_MAP``
---------------------

This command message is sent by the client to the server to inform it of the
memory regions the server can access. It must be sent before the server can
perform any DMA to the client. It is normally sent directly after the version
handshake is completed, but may also occur when memory is added to the client,
or if the client uses a vIOMMU.

Request
^^^^^^^

The request payload for this message is a structure of the following format:

+-------------+--------+-------------+
| Name        | Offset | Size        |
+=============+========+=============+
| argsz       | 0      | 4           |
+-------------+--------+-------------+
| flags       | 4      | 4           |
+-------------+--------+-------------+
|             | +-----+------------+ |
|             | | Bit | Definition | |
|             | +=====+============+ |
|             | | 0   | readable   | |
|             | +-----+------------+ |
|             | | 1   | writeable  | |
|             | +-----+------------+ |
+-------------+--------+-------------+
| offset      | 8      | 8           |
+-------------+--------+-------------+
| address     | 16     | 8           |
+-------------+--------+-------------+
| size        | 24     | 8           |
+-------------+--------+-------------+

* *argsz* is the size of the above structure. Note there is no reply payload,
  so this field differs from other message types.
* *flags* contains the following region attributes:

  * *readable* indicates that the region can be read from.

  * *writeable* indicates that the region can be written to.

* *offset* is the file offset of the region with respect to the associated file
  descriptor, or zero if the region is not mappable
* *address* is the base DMA address of the region.
* *size* is the size of the region.

This structure is 32 bytes in size, so the message size is 16 + 32 bytes.

If the DMA region being added can be directly mapped by the server, a file
descriptor must be sent as part of the message meta-data. The region can be
mapped via the mmap() system call. On ``AF_UNIX`` sockets, the file descriptor
must be passed as ``SCM_RIGHTS`` type ancillary data.  Otherwise, if the DMA
region cannot be directly mapped by the server, no file descriptor must be sent
as part of the message meta-data and the DMA region can be accessed by the
server using ``VFIO_USER_DMA_READ`` and ``VFIO_USER_DMA_WRITE`` messages,
explained in `Read and Write Operations`_. A command to map over an existing
region must be failed by the server with ``EEXIST`` set in error field in the
reply.

Reply
^^^^^

There is no payload in the reply message.

``VFIO_USER_DMA_UNMAP``
-----------------------

This command message is sent by the client to the server to inform it that a
DMA region, previously made available via a ``VFIO_USER_DMA_MAP`` command
message, is no longer available for DMA. It typically occurs when memory is
subtracted from the client or if the client uses a vIOMMU. The DMA region is
described by the following structure:

Request
^^^^^^^

The request payload for this message is a structure of the following format:

+--------------+--------+------------------------+
| Name         | Offset | Size                   |
+==============+========+========================+
| argsz        | 0      | 4                      |
+--------------+--------+------------------------+
| flags        | 4      | 4                      |
+--------------+--------+------------------------+
| address      | 8      | 8                      |
+--------------+--------+------------------------+
| size         | 16     | 8                      |
+--------------+--------+------------------------+

* *argsz* is the maximum size of the reply payload.
* *flags* is unused in this version.
* *address* is the base DMA address of the DMA region.
* *size* is the size of the DMA region.

The address and size of the DMA region being unmapped must match exactly a
previous mapping.

Reply
^^^^^

Upon receiving a ``VFIO_USER_DMA_UNMAP`` command, if the file descriptor is
mapped then the server must release all references to that DMA region before
replying, which potentially includes in-flight DMA transactions.

The server responds with the original DMA entry in the request.


``VFIO_USER_DEVICE_GET_INFO``
-----------------------------

This command message is sent by the client to the server to query for basic
information about the device.

Request
^^^^^^^

+-------------+--------+--------------------------+
| Name        | Offset | Size                     |
+=============+========+==========================+
| argsz       | 0      | 4                        |
+-------------+--------+--------------------------+
| flags       | 4      | 4                        |
+-------------+--------+--------------------------+
|             | +-----+-------------------------+ |
|             | | Bit | Definition              | |
|             | +=====+=========================+ |
|             | | 0   | VFIO_DEVICE_FLAGS_RESET | |
|             | +-----+-------------------------+ |
|             | | 1   | VFIO_DEVICE_FLAGS_PCI   | |
|             | +-----+-------------------------+ |
+-------------+--------+--------------------------+
| num_regions | 8      | 4                        |
+-------------+--------+--------------------------+
| num_irqs    | 12     | 4                        |
+-------------+--------+--------------------------+

* *argsz* is the maximum size of the reply payload
* all other fields must be zero.

Reply
^^^^^

+-------------+--------+--------------------------+
| Name        | Offset | Size                     |
+=============+========+==========================+
| argsz       | 0      | 4                        |
+-------------+--------+--------------------------+
| flags       | 4      | 4                        |
+-------------+--------+--------------------------+
|             | +-----+-------------------------+ |
|             | | Bit | Definition              | |
|             | +=====+=========================+ |
|             | | 0   | VFIO_DEVICE_FLAGS_RESET | |
|             | +-----+-------------------------+ |
|             | | 1   | VFIO_DEVICE_FLAGS_PCI   | |
|             | +-----+-------------------------+ |
+-------------+--------+--------------------------+
| num_regions | 8      | 4                        |
+-------------+--------+--------------------------+
| num_irqs    | 12     | 4                        |
+-------------+--------+--------------------------+

* *argsz* is the size required for the full reply payload (16 bytes today)
* *flags* contains the following device attributes.

  * ``VFIO_DEVICE_FLAGS_RESET`` indicates that the device supports the
    ``VFIO_USER_DEVICE_RESET`` message.
  * ``VFIO_DEVICE_FLAGS_PCI`` indicates that the device is a PCI device.

* *num_regions* is the number of memory regions that the device exposes.
* *num_irqs* is the number of distinct interrupt types that the device supports.

This version of the protocol only supports PCI devices. Additional devices may
be supported in future versions.

``VFIO_USER_DEVICE_GET_REGION_INFO``
------------------------------------

This command message is sent by the client to the server to query for
information about device regions. The VFIO region info structure is defined in
``<linux/vfio.h>`` (``struct vfio_region_info``).

Request
^^^^^^^

+------------+--------+------------------------------+
| Name       | Offset | Size                         |
+============+========+==============================+
| argsz      | 0      | 4                            |
+------------+--------+------------------------------+
| flags      | 4      | 4                            |
+------------+--------+------------------------------+
| index      | 8      | 4                            |
+------------+--------+------------------------------+
| cap_offset | 12     | 4                            |
+------------+--------+------------------------------+
| size       | 16     | 8                            |
+------------+--------+------------------------------+
| offset     | 24     | 8                            |
+------------+--------+------------------------------+

* *argsz* the maximum size of the reply payload
* *index* is the index of memory region being queried, it is the only field
  that is required to be set in the command message.
* all other fields must be zero.

Reply
^^^^^

+------------+--------+------------------------------+
| Name       | Offset | Size                         |
+============+========+==============================+
| argsz      | 0      | 4                            |
+------------+--------+------------------------------+
| flags      | 4      | 4                            |
+------------+--------+------------------------------+
|            | +-----+-----------------------------+ |
|            | | Bit | Definition                  | |
|            | +=====+=============================+ |
|            | | 0   | VFIO_REGION_INFO_FLAG_READ  | |
|            | +-----+-----------------------------+ |
|            | | 1   | VFIO_REGION_INFO_FLAG_WRITE | |
|            | +-----+-----------------------------+ |
|            | | 2   | VFIO_REGION_INFO_FLAG_MMAP  | |
|            | +-----+-----------------------------+ |
|            | | 3   | VFIO_REGION_INFO_FLAG_CAPS  | |
|            | +-----+-----------------------------+ |
+------------+--------+------------------------------+
+------------+--------+------------------------------+
| index      | 8      | 4                            |
+------------+--------+------------------------------+
| cap_offset | 12     | 4                            |
+------------+--------+------------------------------+
| size       | 16     | 8                            |
+------------+--------+------------------------------+
| offset     | 24     | 8                            |
+------------+--------+------------------------------+

* *argsz* is the size required for the full reply payload (region info structure
  plus the size of any region capabilities)
* *flags* are attributes of the region:

  * ``VFIO_REGION_INFO_FLAG_READ`` allows client read access to the region.
  * ``VFIO_REGION_INFO_FLAG_WRITE`` allows client write access to the region.
  * ``VFIO_REGION_INFO_FLAG_MMAP`` specifies the client can mmap() the region.
    When this flag is set, the reply will include a file descriptor in its
    meta-data. On ``AF_UNIX`` sockets, the file descriptors will be passed as
    ``SCM_RIGHTS`` type ancillary data.
  * ``VFIO_REGION_INFO_FLAG_CAPS`` indicates additional capabilities found in the
    reply.

* *index* is the index of memory region being queried, it is the only field
  that is required to be set in the command message.
* *cap_offset* describes where additional region capabilities can be found.
  cap_offset is relative to the beginning of the VFIO region info structure.
  The data structure it points is a VFIO cap header defined in
  ``<linux/vfio.h>``.
* *size* is the size of the region.
* *offset* is the offset that should be given to the mmap() system call for
  regions with the MMAP attribute. It is also used as the base offset when
  mapping a VFIO sparse mmap area, described below.

VFIO region capabilities
""""""""""""""""""""""""

The VFIO region information can also include a capabilities list. This list is
similar to a PCI capability list - each entry has a common header that
identifies a capability and where the next capability in the list can be found.
The VFIO capability header format is defined in ``<linux/vfio.h>`` (``struct
vfio_info_cap_header``).

VFIO cap header format
""""""""""""""""""""""

+---------+--------+------+
| Name    | Offset | Size |
+=========+========+======+
| id      | 0      | 2    |
+---------+--------+------+
| version | 2      | 2    |
+---------+--------+------+
| next    | 4      | 4    |
+---------+--------+------+

* *id* is the capability identity.
* *version* is a capability-specific version number.
* *next* specifies the offset of the next capability in the capability list. It
  is relative to the beginning of the VFIO region info structure.

VFIO sparse mmap cap header
"""""""""""""""""""""""""""

+------------------+----------------------------------+
| Name             | Value                            |
+==================+==================================+
| id               | VFIO_REGION_INFO_CAP_SPARSE_MMAP |
+------------------+----------------------------------+
| version          | 0x1                              |
+------------------+----------------------------------+
| next             | <next>                           |
+------------------+----------------------------------+
| sparse mmap info | VFIO region info sparse mmap     |
+------------------+----------------------------------+

This capability is defined when only a subrange of the region supports
direct access by the client via mmap(). The VFIO sparse mmap area is defined in
``<linux/vfio.h>`` (``struct vfio_region_sparse_mmap_area`` and ``struct
vfio_region_info_cap_sparse_mmap``).

VFIO region info cap sparse mmap
""""""""""""""""""""""""""""""""

+----------+--------+------+
| Name     | Offset | Size |
+==========+========+======+
| nr_areas | 0      | 4    |
+----------+--------+------+
| reserved | 4      | 4    |
+----------+--------+------+
| offset   | 8      | 8    |
+----------+--------+------+
| size     | 16     | 8    |
+----------+--------+------+
| ...      |        |      |
+----------+--------+------+

* *nr_areas* is the number of sparse mmap areas in the region.
* *offset* and size describe a single area that can be mapped by the client.
  There will be *nr_areas* pairs of offset and size. The offset will be added to
  the base offset given in the ``VFIO_USER_DEVICE_GET_REGION_INFO`` to form the
  offset argument of the subsequent mmap() call.

The VFIO sparse mmap area is defined in ``<linux/vfio.h>`` (``struct
vfio_region_info_cap_sparse_mmap``).


``VFIO_USER_DEVICE_GET_REGION_IO_FDS``
--------------------------------------

Clients can access regions via ``VFIO_USER_REGION_READ/WRITE`` or, if provided, by
``mmap()`` of a file descriptor provided by the server.

``VFIO_USER_DEVICE_GET_REGION_IO_FDS`` provides an alternative access mechanism via
file descriptors. This is an optional feature intended for performance
improvements where an underlying sub-system (such as KVM) supports communication
across such file descriptors to the vfio-user server, without needing to
round-trip through the client.

The server returns an array of sub-regions for the requested region. Each
sub-region describes a span (offset and size) of a region, along with the
requested file descriptor notification mechanism to use.  Each sub-region in the
response message may choose to use a different method, as defined below.  The
two mechanisms supported in this specification are ioeventfds and ioregionfds.

The server in addition returns a file descriptor in the ancillary data; clients
are expected to configure each sub-region's file descriptor with the requested
notification method. For example, a client could configure KVM with the
requested ioeventfd via a ``KVM_IOEVENTFD`` ``ioctl()``.

Request
^^^^^^^

+-------------+--------+------+
| Name        | Offset | Size |
+=============+========+======+
| argsz       | 0      | 4    |
+-------------+--------+------+
| flags       | 4      | 4    |
+-------------+--------+------+
| index       | 8      | 4    |
+-------------+--------+------+
| count       | 12     | 4    |
+-------------+--------+------+

* *argsz* the maximum size of the reply payload
* *index* is the index of memory region being queried
* all other fields must be zero

The client must set ``flags`` to zero and specify the region being queried in
the ``index``.

Reply
^^^^^

+-------------+--------+------+
| Name        | Offset | Size |
+=============+========+======+
| argsz       | 0      | 4    |
+-------------+--------+------+
| flags       | 4      | 4    |
+-------------+--------+------+
| index       | 8      | 4    |
+-------------+--------+------+
| count       | 12     | 4    |
+-------------+--------+------+
| sub-regions | 16     | ...  |
+-------------+--------+------+

* *argsz* is the size of the region IO FD info structure plus the
  total size of the sub-region array. Thus, each array entry "i" is at offset
  i * ((argsz - 32) / count). Note that currently this is 40 bytes for both IO
  FD types, but this is not to be relied on. As elsewhere, this indicates the
  full reply payload size needed.
* *flags* must be zero
* *index* is the index of memory region being queried
* *count* is the number of sub-regions in the array
* *sub-regions* is the array of Sub-Region IO FD info structures

The reply message will additionally include at least one file descriptor in the
ancillary data. Note that more than one sub-region may share the same file
descriptor.

Note that it is the client's responsibility to verify the requested values (for
example, that the requested offset does not exceed the region's bounds).

Each sub-region given in the response has one of two possible structures,
depending whether *type* is ``VFIO_USER_IO_FD_TYPE_IOEVENTFD`` or
``VFIO_USER_IO_FD_TYPE_IOREGIONFD``:

Sub-Region IO FD info format (ioeventfd)
""""""""""""""""""""""""""""""""""""""""

+-----------+--------+------+
| Name      | Offset | Size |
+===========+========+======+
| offset    | 0      | 8    |
+-----------+--------+------+
| size      | 8      | 8    |
+-----------+--------+------+
| fd_index  | 16     | 4    |
+-----------+--------+------+
| type      | 20     | 4    |
+-----------+--------+------+
| flags     | 24     | 4    |
+-----------+--------+------+
| padding   | 28     | 4    |
+-----------+--------+------+
| datamatch | 32     | 8    |
+-----------+--------+------+

* *offset* is the offset of the start of the sub-region within the region
  requested ("physical address offset" for the region)
* *size* is the length of the sub-region. This may be zero if the access size is
  not relevant, which may allow for optimizations
* *fd_index* is the index in the ancillary data of the FD to use for ioeventfd
  notification; it may be shared.
* *type* is ``VFIO_USER_IO_FD_TYPE_IOEVENTFD``
* *flags* is any of:

  * ``KVM_IOEVENTFD_FLAG_DATAMATCH``
  * ``KVM_IOEVENTFD_FLAG_PIO``
  * ``KVM_IOEVENTFD_FLAG_VIRTIO_CCW_NOTIFY`` (FIXME: makes sense?)

* *datamatch* is the datamatch value if needed

See https://www.kernel.org/doc/Documentation/virtual/kvm/api.txt, *4.59
KVM_IOEVENTFD* for further context on the ioeventfd-specific fields.

Sub-Region IO FD info format (ioregionfd)
"""""""""""""""""""""""""""""""""""""""""

+-----------+--------+------+
| Name      | Offset | Size |
+===========+========+======+
| offset    | 0      | 8    |
+-----------+--------+------+
| size      | 8      | 8    |
+-----------+--------+------+
| fd_index  | 16     | 4    |
+-----------+--------+------+
| type      | 20     | 4    |
+-----------+--------+------+
| flags     | 24     | 4    |
+-----------+--------+------+
| padding   | 28     | 4    |
+-----------+--------+------+
| user_data | 32     | 8    |
+-----------+--------+------+

* *offset* is the offset of the start of the sub-region within the region
  requested ("physical address offset" for the region)
* *size* is the length of the sub-region. This may be zero if the access size is
  not relevant, which may allow for optimizations; ``KVM_IOREGION_POSTED_WRITES``
  must be set in *flags* in this case
* *fd_index* is the index in the ancillary data of the FD to use for ioregionfd
  messages; it may be shared
* *type* is ``VFIO_USER_IO_FD_TYPE_IOREGIONFD``
* *flags* is any of:

  * ``KVM_IOREGION_PIO``
  * ``KVM_IOREGION_POSTED_WRITES``

* *user_data* is an opaque value passed back to the server via a message on the
  file descriptor

For further information on the ioregionfd-specific fields, see:
https://lore.kernel.org/kvm/cover.1613828726.git.eafanasova@gmail.com/

(FIXME: update with final API docs.)

``VFIO_USER_DEVICE_GET_IRQ_INFO``
---------------------------------

This command message is sent by the client to the server to query for
information about device interrupt types. The VFIO IRQ info structure is
defined in ``<linux/vfio.h>`` (``struct vfio_irq_info``).

Request
^^^^^^^

+-------+--------+---------------------------+
| Name  | Offset | Size                      |
+=======+========+===========================+
| argsz | 0      | 4                         |
+-------+--------+---------------------------+
| flags | 4      | 4                         |
+-------+--------+---------------------------+
|       | +-----+--------------------------+ |
|       | | Bit | Definition               | |
|       | +=====+==========================+ |
|       | | 0   | VFIO_IRQ_INFO_EVENTFD    | |
|       | +-----+--------------------------+ |
|       | | 1   | VFIO_IRQ_INFO_MASKABLE   | |
|       | +-----+--------------------------+ |
|       | | 2   | VFIO_IRQ_INFO_AUTOMASKED | |
|       | +-----+--------------------------+ |
|       | | 3   | VFIO_IRQ_INFO_NORESIZE   | |
|       | +-----+--------------------------+ |
+-------+--------+---------------------------+
| index | 8      | 4                         |
+-------+--------+---------------------------+
| count | 12     | 4                         |
+-------+--------+---------------------------+

* *argsz* is the maximum size of the reply payload (16 bytes today)
* index is the index of IRQ type being queried (e.g. ``VFIO_PCI_MSIX_IRQ_INDEX``)
* all other fields must be zero

Reply
^^^^^

+-------+--------+---------------------------+
| Name  | Offset | Size                      |
+=======+========+===========================+
| argsz | 0      | 4                         |
+-------+--------+---------------------------+
| flags | 4      | 4                         |
+-------+--------+---------------------------+
|       | +-----+--------------------------+ |
|       | | Bit | Definition               | |
|       | +=====+==========================+ |
|       | | 0   | VFIO_IRQ_INFO_EVENTFD    | |
|       | +-----+--------------------------+ |
|       | | 1   | VFIO_IRQ_INFO_MASKABLE   | |
|       | +-----+--------------------------+ |
|       | | 2   | VFIO_IRQ_INFO_AUTOMASKED | |
|       | +-----+--------------------------+ |
|       | | 3   | VFIO_IRQ_INFO_NORESIZE   | |
|       | +-----+--------------------------+ |
+-------+--------+---------------------------+
| index | 8      | 4                         |
+-------+--------+---------------------------+
| count | 12     | 4                         |
+-------+--------+---------------------------+

* *argsz* is the size required for the full reply payload (16 bytes today)
* *flags* defines IRQ attributes:

  * ``VFIO_IRQ_INFO_EVENTFD`` indicates the IRQ type can support server eventfd
    signalling.
  * ``VFIO_IRQ_INFO_MASKABLE`` indicates that the IRQ type supports the ``MASK``
    and ``UNMASK`` actions in a ``VFIO_USER_DEVICE_SET_IRQS`` message.
  * ``VFIO_IRQ_INFO_AUTOMASKED`` indicates the IRQ type masks itself after being
    triggered, and the client must send an ``UNMASK`` action to receive new
    interrupts.
  * ``VFIO_IRQ_INFO_NORESIZE`` indicates ``VFIO_USER_SET_IRQS`` operations setup
    interrupts as a set, and new sub-indexes cannot be enabled without disabling
    the entire type.
* index is the index of IRQ type being queried
* count describes the number of interrupts of the queried type.

``VFIO_USER_DEVICE_SET_IRQS``
-----------------------------

This command message is sent by the client to the server to set actions for
device interrupt types. The VFIO IRQ set structure is defined in
``<linux/vfio.h>`` (``struct vfio_irq_set``).

Request
^^^^^^^

+-------+--------+------------------------------+
| Name  | Offset | Size                         |
+=======+========+==============================+
| argsz | 0      | 4                            |
+-------+--------+------------------------------+
| flags | 4      | 4                            |
+-------+--------+------------------------------+
|       | +-----+-----------------------------+ |
|       | | Bit | Definition                  | |
|       | +=====+=============================+ |
|       | | 0   | VFIO_IRQ_SET_DATA_NONE      | |
|       | +-----+-----------------------------+ |
|       | | 1   | VFIO_IRQ_SET_DATA_BOOL      | |
|       | +-----+-----------------------------+ |
|       | | 2   | VFIO_IRQ_SET_DATA_EVENTFD   | |
|       | +-----+-----------------------------+ |
|       | | 3   | VFIO_IRQ_SET_ACTION_MASK    | |
|       | +-----+-----------------------------+ |
|       | | 4   | VFIO_IRQ_SET_ACTION_UNMASK  | |
|       | +-----+-----------------------------+ |
|       | | 5   | VFIO_IRQ_SET_ACTION_TRIGGER | |
|       | +-----+-----------------------------+ |
+-------+--------+------------------------------+
| index | 8      | 4                            |
+-------+--------+------------------------------+
| start | 12     | 4                            |
+-------+--------+------------------------------+
| count | 16     | 4                            |
+-------+--------+------------------------------+
| data  | 20     | variable                     |
+-------+--------+------------------------------+

* *argsz* is the size of the VFIO IRQ set request payload, including any *data*
  field. Note there is no reply payload, so this field differs from other
  message types.
* *flags* defines the action performed on the interrupt range. The ``DATA``
  flags describe the data field sent in the message; the ``ACTION`` flags
  describe the action to be performed. The flags are mutually exclusive for
  both sets.

  * ``VFIO_IRQ_SET_DATA_NONE`` indicates there is no data field in the command.
    The action is performed unconditionally.
  * ``VFIO_IRQ_SET_DATA_BOOL`` indicates the data field is an array of boolean
    bytes. The action is performed if the corresponding boolean is true.
  * ``VFIO_IRQ_SET_DATA_EVENTFD`` indicates an array of event file descriptors
    was sent in the message meta-data. These descriptors will be signalled when
    the action defined by the action flags occurs. In ``AF_UNIX`` sockets, the
    descriptors are sent as ``SCM_RIGHTS`` type ancillary data.
    If no file descriptors are provided, this de-assigns the specified
    previously configured interrupts.
  * ``VFIO_IRQ_SET_ACTION_MASK`` indicates a masking event. It can be used with
    ``VFIO_IRQ_SET_DATA_BOOL`` or ``VFIO_IRQ_SET_DATA_NONE`` to mask an interrupt,
    or with ``VFIO_IRQ_SET_DATA_EVENTFD`` to generate an event when the guest masks
    the interrupt.
  * ``VFIO_IRQ_SET_ACTION_UNMASK`` indicates an unmasking event. It can be used
    with ``VFIO_IRQ_SET_DATA_BOOL`` or ``VFIO_IRQ_SET_DATA_NONE`` to unmask an
    interrupt, or with ``VFIO_IRQ_SET_DATA_EVENTFD`` to generate an event when the
    guest unmasks the interrupt.
  * ``VFIO_IRQ_SET_ACTION_TRIGGER`` indicates a triggering event. It can be used
    with ``VFIO_IRQ_SET_DATA_BOOL`` or ``VFIO_IRQ_SET_DATA_NONE`` to trigger an
    interrupt, or with ``VFIO_IRQ_SET_DATA_EVENTFD`` to generate an event when the
    server triggers the interrupt.

* *index* is the index of IRQ type being setup.
* *start* is the start of the sub-index being set.
* *count* describes the number of sub-indexes being set. As a special case, a
  count (and start) of 0, with data flags of ``VFIO_IRQ_SET_DATA_NONE`` disables
  all interrupts of the index.
* *data* is an optional field included when the
  ``VFIO_IRQ_SET_DATA_BOOL`` flag is present. It contains an array of booleans
  that specify whether the action is to be performed on the corresponding
  index. It's used when the action is only performed on a subset of the range
  specified.

Not all interrupt types support every combination of data and action flags.
The client must know the capabilities of the device and IRQ index before it
sends a ``VFIO_USER_DEVICE_SET_IRQ`` message.

In typical operation, a specific IRQ may operate as follows:

1. The client sends a ``VFIO_USER_DEVICE_SET_IRQ`` message with
   ``flags=(VFIO_IRQ_SET_DATA_EVENTFD|VFIO_IRQ_SET_ACTION_TRIGGER)`` along
   with an eventfd. This associates the IRQ with a particular eventfd on the
   server side.

#. The client may send a ``VFIO_USER_DEVICE_SET_IRQ`` message with
   ``flags=(VFIO_IRQ_SET_DATA_EVENTFD|VFIO_IRQ_SET_ACTION_MASK/UNMASK)`` along
   with another eventfd. This associates the given eventfd with the
   mask/unmask state on the server side.

#. The server may trigger the IRQ by writing 1 to the eventfd.

#. The server may mask/unmask an IRQ which will write 1 to the corresponding
   mask/unmask eventfd, if there is one.

5. A client may trigger a device IRQ itself, by sending a
   ``VFIO_USER_DEVICE_SET_IRQ`` message with
   ``flags=(VFIO_IRQ_SET_DATA_NONE/BOOL|VFIO_IRQ_SET_ACTION_TRIGGER)``.

6. A client may mask or unmask the IRQ, by sending a
   ``VFIO_USER_DEVICE_SET_IRQ`` message with
   ``flags=(VFIO_IRQ_SET_DATA_NONE/BOOL|VFIO_IRQ_SET_ACTION_MASK/UNMASK)``.

Reply
^^^^^

There is no payload in the reply.

.. _Read and Write Operations:

Note that all of these operations must be supported by the client and/or server,
even if the corresponding memory or device region has been shared as mappable.

The ``count`` field must not exceed the value of ``max_data_xfer_size`` of the
peer, for both reads and writes.

``VFIO_USER_REGION_READ``
-------------------------

If a device region is not mappable, it's not directly accessible by the client
via ``mmap()`` of the underlying file descriptor. In this case, a client can
read from a device region with this message.

Request
^^^^^^^

+--------+--------+----------+
| Name   | Offset | Size     |
+========+========+==========+
| offset | 0      | 8        |
+--------+--------+----------+
| region | 8      | 4        |
+--------+--------+----------+
| count  | 12     | 4        |
+--------+--------+----------+

* *offset* into the region being accessed.
* *region* is the index of the region being accessed.
* *count* is the size of the data to be transferred.

Reply
^^^^^

+--------+--------+----------+
| Name   | Offset | Size     |
+========+========+==========+
| offset | 0      | 8        |
+--------+--------+----------+
| region | 8      | 4        |
+--------+--------+----------+
| count  | 12     | 4        |
+--------+--------+----------+
| data   | 16     | variable |
+--------+--------+----------+

* *offset* into the region accessed.
* *region* is the index of the region accessed.
* *count* is the size of the data transferred.
* *data* is the data that was read from the device region.

``VFIO_USER_REGION_WRITE``
--------------------------

If a device region is not mappable, it's not directly accessible by the client
via mmap() of the underlying fd. In this case, a client can write to a device
region with this message.

Request
^^^^^^^

+--------+--------+----------+
| Name   | Offset | Size     |
+========+========+==========+
| offset | 0      | 8        |
+--------+--------+----------+
| region | 8      | 4        |
+--------+--------+----------+
| count  | 12     | 4        |
+--------+--------+----------+
| data   | 16     | variable |
+--------+--------+----------+

* *offset* into the region being accessed.
* *region* is the index of the region being accessed.
* *count* is the size of the data to be transferred.
* *data* is the data to write

Reply
^^^^^

+--------+--------+----------+
| Name   | Offset | Size     |
+========+========+==========+
| offset | 0      | 8        |
+--------+--------+----------+
| region | 8      | 4        |
+--------+--------+----------+
| count  | 12     | 4        |
+--------+--------+----------+

* *offset* into the region accessed.
* *region* is the index of the region accessed.
* *count* is the size of the data transferred.

``VFIO_USER_DMA_READ``
-----------------------

If the client has not shared mappable memory, the server can use this message to
read from guest memory. This message and its reply are passed over the separate
server-to-client socket if twin-socket mode has been negotiated during
connection setup.

Request
^^^^^^^

+---------+--------+----------+
| Name    | Offset | Size     |
+=========+========+==========+
| address | 0      | 8        |
+---------+--------+----------+
| count   | 8      | 8        |
+---------+--------+----------+

* *address* is the client DMA memory address being accessed. This address must have
  been previously exported to the server with a ``VFIO_USER_DMA_MAP`` message.
* *count* is the size of the data to be transferred.

Reply
^^^^^

+---------+--------+----------+
| Name    | Offset | Size     |
+=========+========+==========+
| address | 0      | 8        |
+---------+--------+----------+
| count   | 8      | 8        |
+---------+--------+----------+
| data    | 16     | variable |
+---------+--------+----------+

* *address* is the client DMA memory address being accessed.
* *count* is the size of the data transferred.
* *data* is the data read.

``VFIO_USER_DMA_WRITE``
-----------------------

If the client has not shared mappable memory, the server can use this message to
write to guest memory. This message and its reply are passed over the separate
server-to-client socket if twin-socket mode has been negotiated during
connection setup.

Request
^^^^^^^

+---------+--------+----------+
| Name    | Offset | Size     |
+=========+========+==========+
| address | 0      | 8        |
+---------+--------+----------+
| count   | 8      | 8        |
+---------+--------+----------+
| data    | 16     | variable |
+---------+--------+----------+

* *address* is the client DMA memory address being accessed. This address must have
  been previously exported to the server with a ``VFIO_USER_DMA_MAP`` message.
* *count* is the size of the data to be transferred.
* *data* is the data to write

Reply
^^^^^

+---------+--------+----------+
| Name    | Offset | Size     |
+=========+========+==========+
| address | 0      | 8        |
+---------+--------+----------+
| count   | 8      | 4        |
+---------+--------+----------+

* *address* is the client DMA memory address being accessed.
* *count* is the size of the data transferred.

``VFIO_USER_DEVICE_RESET``
--------------------------

This command message is sent from the client to the server to reset the device.
Neither the request or reply have a payload.

``VFIO_USER_REGION_WRITE_MULTI``
--------------------------------

This message can be used to coalesce multiple device write operations
into a single messgage.  It is only used as an optimization when the
outgoing message queue is relatively full.

Request
^^^^^^^

+---------+--------+----------+
| Name    | Offset | Size     |
+=========+========+==========+
| wr_cnt  | 0      | 8        |
+---------+--------+----------+
| wrs     | 8      | variable |
+---------+--------+----------+

* *wr_cnt* is the number of device writes coalesced in the message
* *wrs* is an array of device writes defined below

Single Device Write Format
""""""""""""""""""""""""""

+--------+--------+----------+
| Name   | Offset | Size     |
+========+========+==========+
| offset | 0      | 8        |
+--------+--------+----------+
| region | 8      | 4        |
+--------+--------+----------+
| count  | 12     | 4        |
+--------+--------+----------+
| data   | 16     | 8        |
+--------+--------+----------+

* *offset* into the region being accessed.
* *region* is the index of the region being accessed.
* *count* is the size of the data to be transferred.  This format can
  only describe writes of 8 bytes or less.
* *data* is the data to write.

Reply
^^^^^

+---------+--------+----------+
| Name    | Offset | Size     |
+=========+========+==========+
| wr_cnt  | 0      | 8        |
+---------+--------+----------+

* *wr_cnt* is the number of device writes completed.

``VFIO_USER_DEVICE_FEATURE``
----------------------------

This command is analogous to ``VFIO_DEVICE_FEATURE``. It is used to get, set, or
probe feature data of the device.

Request
^^^^^^^

The request payload for this message is a structure of the following format.

+-------+--------+--------------------------------+
| Name  | Offset | Size                           |
+=======+========+================================+
| argsz | 0      | 4                              |
+-------+--------+--------------------------------+
| flags | 4      | 4                              |
+-------+--------+--------------------------------+
|       | +---------+---------------------------+ |
|       | | Bit     | Definition                | |
|       | +=========+===========================+ |
|       | | 0 to 15 | Feature index             | |
|       | +---------+---------------------------+ |
|       | | 16      | VFIO_DEVICE_FEATURE_GET   | |
|       | +---------+---------------------------+ |
|       | | 17      | VFIO_DEVICE_FEATURE_SET   | |
|       | +---------+---------------------------+ |
|       | | 18      | VFIO_DEVICE_FEATURE_PROBE | |
|       | +---------+---------------------------+ |
+-------+--------+--------------------------------+
| data  | 8      | variable                       |
+-------+--------+--------------------------------+

* *argsz* is the maximum size of the reply payload.

* *flags* defines the action to be performed by the server and upon which
  feature:

  * The feature index consists of the least significant 16 bits of the flags
    field, and can be accessed using the ``VFIO_DEVICE_FEATURE_MASK`` bit mask.

  * ``VFIO_DEVICE_FEATURE_GET`` instructs the server to get the data for the
    given feature.

  * ``VFIO_DEVICE_FEATURE_SET`` instructs the server to set the feature data to
    that given in the ``data`` field of the payload.

  * ``VFIO_DEVICE_FEATURE_PROBE`` instructs the server to probe for feature
    support. If ``VFIO_DEVICE_FEATURE_GET`` and/or ``VFIO_DEVICE_FEATURE_SET``
    are also set, the probe will only return success if all of the indicated
    methods are supported.

  ``VFIO_DEVICE_FEATURE_GET`` and ``VFIO_DEVICE_FEATURE_SET`` are mutually
  exclusive, except for use with ``VFIO_DEVICE_FEATURE_PROBE``.

* *data* is specific to the particular feature. It is not used for probing.

This part of the request is analogous to VFIO's ``struct vfio_device_feature``.

Reply
^^^^^

The reply payload must be the same as the request payload for setting or
probing a feature. For getting a feature's data, the data is added in the data
section and its length is added to ``argsz``.

Device Features
^^^^^^^^^^^^^^^

The only device features supported by vfio-user are those related to migration,
although this may change in the future. They are a subset of those supported in
the VFIO implementation of the Linux kernel.

+----------------------------------------+---------------+
| Name                                   | Feature Index |
+========================================+===============+
| VFIO_DEVICE_FEATURE_MIGRATION          | 1             |
+----------------------------------------+---------------+
| VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE   | 2             |
+----------------------------------------+---------------+
| VFIO_DEVICE_FEATURE_DMA_LOGGING_START  | 6             |
+----------------------------------------+---------------+
| VFIO_DEVICE_FEATURE_DMA_LOGGING_STOP   | 7             |
+----------------------------------------+---------------+
| VFIO_DEVICE_FEATURE_DMA_LOGGING_REPORT | 8             |
+----------------------------------------+---------------+

``VFIO_DEVICE_FEATURE_MIGRATION``
"""""""""""""""""""""""""""""""""

This feature indicates that the device can support the migration API through
``VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE``. If ``GET`` succeeds, the ``RUNNING``
and ``ERROR`` states are always supported. Support for additional states is
indicated via the flags field; at least ``VFIO_MIGRATION_STOP_COPY`` must be
set.

There is no data field of the request message.

The data field of the reply message is structured as follows:

+-------+--------+---------------------------+
| Name  | Offset | Size                      |
+=======+========+===========================+
| flags | 0      | 8                         |
+-------+--------+---------------------------+
|       | +-----+--------------------------+ |
|       | | Bit | Definition               | |
|       | +=====+==========================+ |
|       | | 0   | VFIO_MIGRATION_STOP_COPY | |
|       | +-----+--------------------------+ |
|       | | 1   | VFIO_MIGRATION_P2P       | |
|       | +-----+--------------------------+ |
|       | | 2   | VFIO_MIGRATION_PRE_COPY  | |
|       | +-----+--------------------------+ |
+-------+--------+---------------------------+

These flags are interpreted in the same way as VFIO.

``VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE``
""""""""""""""""""""""""""""""""""""""""

Upon ``VFIO_DEVICE_FEATURE_SET``, execute a migration state change on the VFIO
device. The new state is supplied in ``device_state``. The state transition must
fully complete before the reply is sent.

The data field of the reply message, as well as the ``SET`` request message, is
structured as follows:

+--------------+--------+------+
| Name         | Offset | Size |
+==============+========+======+
| device_state | 0      | 4    |
+--------------+--------+------+
| data_fd      | 4      | 4    |
+--------------+--------+------+

* *device_state* is the current state of the device (for ``GET``) or the
  state to transition to (for ``SET``). It is defined by the
  ``vfio_device_mig_state`` enum as detailed below. These states are the states
  of the device migration Finite State Machine.

+--------------------------------+-------+---------------------------------------------------------------------+
| Name                           | State | Description                                                         |
+================================+=======+=====================================================================+
| VFIO_DEVICE_STATE_ERROR        | 0     | The device has failed and must be reset.                            |
+--------------------------------+-------+---------------------------------------------------------------------+
| VFIO_DEVICE_STATE_STOP         | 1     | The device does not change the internal or external state.          |
+--------------------------------+-------+---------------------------------------------------------------------+
| VFIO_DEVICE_STATE_RUNNING      | 2     | The device is running normally.                                     |
+--------------------------------+-------+---------------------------------------------------------------------+
| VFIO_DEVICE_STATE_STOP_COPY    | 3     | The device internal state can be read out.                          |
+--------------------------------+-------+---------------------------------------------------------------------+
| VFIO_DEVICE_STATE_RESUMING     | 4     | The device is stopped and is loading a new internal state.          |
+--------------------------------+-------+---------------------------------------------------------------------+
| VFIO_DEVICE_STATE_RUNNING_P2P  | 5     | (not used in vfio-user)                                             |
+--------------------------------+-------+---------------------------------------------------------------------+
| VFIO_DEVICE_STATE_PRE_COPY     | 6     | The device is running normally but tracking internal state changes. |
+--------------------------------+-------+---------------------------------------------------------------------+
| VFIO_DEVICE_STATE_PRE_COPY_P2P | 7     | (not used in vfio-user)                                             |
+--------------------------------+-------+---------------------------------------------------------------------+

* *data_fd* is unused in vfio-user, as the ``VFIO_USER_MIG_DATA_READ`` and
  ``VFIO_USER_MIG_DATA_WRITE`` messages are used instead for migration data
  transport.

Direct State Transitions
""""""""""""""""""""""""

The device migration FSM is a Mealy machine, so actions are taken upon the arcs
between FSM states. The following transitions need to be supported by the
server, a subset of those defined in ``<linux/vfio.h>``
(``enum vfio_device_mig_state``).

* ``RUNNING -> STOP``, ``STOP_COPY -> STOP``: Stop the operation of the device.
  The ``STOP_COPY`` arc terminates the data transfer session.

* ``RESUMING -> STOP``: Terminate the data transfer session. Complete processing
  of the migration data. Stop the operation of the device. If the delivered data
  is found to be incomplete, inconsistent, or otherwise invalid, fail the
  ``SET`` command and optionally transition to the ``ERROR`` state.

* ``PRE_COPY -> RUNNING``: Terminate the data transfer session. The device is
  now fully operational.

* ``STOP -> RUNNING``: Start the operation of the device.

* ``RUNNING -> PRE_COPY``, ``STOP -> STOP_COPY``: Begin the process of saving
  the device state. The device operation is unchanged, but data transfer begins.
  ``PRE_COPY`` and ``STOP_COPY`` are referred to as the "saving group" of
  states.

* ``PRE_COPY -> STOP_COPY``: Continue to transfer migration data, but stop
  device operation.

* ``STOP -> RESUMING``: Start the process of restoring the device state. The
  internal device state may be changed to prepare the device to receive the
  migration data.

The ``STOP_COPY -> PRE_COPY`` transition is explicitly not allowed and should
return an error if requested.

``ERROR`` cannot be specified as a device state, but any transition request can
be failed and then move the state into ``ERROR`` if the server was unable to
execute the requested arc AND was unable to restore the device into any valid
state. To recover from ``ERROR``, ``VFIO_USER_DEVICE_RESET`` must be used to
return back to ``RUNNING``.

If ``PRE_COPY`` is not supported, arcs touching it are removed.

Complex State Transitions
"""""""""""""""""""""""""

The remaining possible transitions are to be implemented as combinations of the
above FSM arcs. As there are multiple paths, the path should be selected based
on the following rules:

* Select the shortest path.

* The path cannot have saving group states as interior arcs, only start/end
  states.

``VFIO_DEVICE_FEATURE_DMA_LOGGING_START`` / ``VFIO_DEVICE_FEATURE_DMA_LOGGING_STOP``
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

Upon ``VFIO_DEVICE_FEATURE_SET``, start/stop DMA logging. These features can
also be probed to determine whether the device supports DMA logging.

When DMA logging is started, a range of IOVAs to monitor is provided and the
device can optimize its logging to cover only the IOVA range given. Only DMA
writes are logged.

The data field of the ``SET`` request is structured as follows:

+------------+--------+----------+
| Name       | Offset | Size     |
+============+========+==========+
| page_size  | 0      | 8        |
+------------+--------+----------+
| num_ranges | 8      | 4        |
+------------+--------+----------+
| reserved   | 12     | 4        |
+------------+--------+----------+
| ranges     | 16     | variable |
+------------+--------+----------+

* *page_size* hints what tracking granularity the device should try to achieve.
  If the device cannot do the hinted page size then it's the driver's choice
  which page size to pick based on its support. On output the device will return
  the page size it selected.

* *num_ranges* is the number of IOVA ranges to monitor. A value of zero
  indicates that all writes should be logged.

* *ranges* is an array of ``vfio_user_device_feature_dma_logging_range``
  entries:

+--------+--------+------+
| Name   | Offset | Size |
+========+========+======+
| iova   | 0      | 8    |
+--------+--------+------+
| length | 8      | 8    |
+--------+--------+------+

  * *iova* is the base IO virtual address
  * *length* is the length of the range to log

Upon success, the response data field will be the same as the request, unless
the page size was changed, in which case this will be reflected in the response.

``VFIO_DEVICE_FEATURE_DMA_LOGGING_REPORT``
""""""""""""""""""""""""""""""""""""""""""

Upon ``VFIO_DEVICE_FEATURE_GET``, returns the dirty bitmap for a specific IOVA
range. This operation is only valid if logging of dirty pages has been
previously started by setting ``VFIO_DEVICE_FEATURE_DMA_LOGGING_START``.

The data field of the request is structured as follows:

+-----------+--------+------+
| Name      | Offset | Size |
+===========+========+======+
| iova      | 0      | 8    |
+-----------+--------+------+
| length    | 8      | 8    |
+-----------+--------+------+
| page_size | 16     | 8    |
+-----------+--------+------+

* *iova* is the base IO virtual address

* *length* is the length of the range

* *page_size* is the unit of granularity of the bitmap, and must be a power of
  two. It doesn't have to match the value given to
  ``VFIO_DEVICE_FEATURE_DMA_LOGGING_START`` because the driver will format its
  internal logging to match the reporting page size possibly by replicating bits
  if the internal page size is lower than requested

The data field of the response is identical, except with the bitmap added on
the end at offset 24.

The bitmap is an array of u64s that holds the output bitmap, with 1 bit
reporting a *page_size* unit of IOVA. The bits outside of the requested range
must be zero.

The mapping of IOVA to bits is given by:

``bitmap[(addr - iova)/page_size] & (1ULL << (addr % 64))``

``VFIO_USER_MIG_DATA_READ``
---------------------------

This command is used to read data from the source migration server while it is
in a saving group state (``PRE_COPY`` or ``STOP_COPY``).

This command, and ``VFIO_USER_MIG_DATA_WRITE``, are used in place of the
``data_fd`` file descriptor in ``<linux/vfio.h>``
(``struct vfio_device_feature_mig_state``) to enable all data transport to use
the single already-established UNIX socket. Hence, the migration data is
treated like a stream, so the client must continue reading until no more
migration data remains.

Request
^^^^^^^

The request payload for this message is a structure of the following format.

+-------+--------+------+
| Name  | Offset | Size |
+=======+========+======+
| argsz | 0      | 4    |
+-------+--------+------+
| size  | 4      | 4    |
+-------+--------+------+

* *argsz* is the maximum size of the reply payload.

* *size* is the size of the migration data to read.

Reply
^^^^^

The reply payload for this message is a structure of the following format.

+-------+--------+----------+
| Name  | Offset | Size     |
+=======+========+==========+
| argsz | 0      | 4        |
+-------+--------+----------+
| size  | 4      | 4        |
+-------+--------+----------+
| data  | 8      | variable |
+-------+--------+----------+

* *argsz* is the size of the above structure, including the size of the data.

* *size* indicates the size of returned migration data. If this is less than the
  requested size, there is no more migration data to read.

* *data* contains the migration data.

``VFIO_USER_MIG_DATA_WRITE``
----------------------------

This command is used to write data to the destination migration server while it
is in the ``RESUMING`` state.

As above, this replaces the ``data_fd`` file descriptor for transport of
migration data, and as such, the migration data is treated like a stream.

Request
^^^^^^^

The request payload for this message is a structure of the following format.

+-------+--------+----------+
| Name  | Offset | Size     |
+=======+========+==========+
| argsz | 0      | 4        |
+-------+--------+----------+
| size  | 4      | 4        |
+-------+--------+----------+
| data  | 8      | variable |
+-------+--------+----------+

* *argsz* is the maximum size of the reply payload.

* *size* is the size of the migration data to be written.

* *data* contains the migration data.

Reply
^^^^^

There is no reply payload for this message.

Appendices
==========

Unused VFIO ``ioctl()`` commands
--------------------------------

The following VFIO commands do not have an equivalent vfio-user command:

* ``VFIO_GET_API_VERSION``
* ``VFIO_CHECK_EXTENSION``
* ``VFIO_SET_IOMMU``
* ``VFIO_GROUP_GET_STATUS``
* ``VFIO_GROUP_SET_CONTAINER``
* ``VFIO_GROUP_UNSET_CONTAINER``
* ``VFIO_GROUP_GET_DEVICE_FD``
* ``VFIO_IOMMU_GET_INFO``

However, once support for live migration for VFIO devices is finalized some
of the above commands may have to be handled by the client in their
corresponding vfio-user form. This will be addressed in a future protocol
version.

VFIO groups and containers
^^^^^^^^^^^^^^^^^^^^^^^^^^

The current VFIO implementation includes group and container idioms that
describe how a device relates to the host IOMMU. In the vfio-user
implementation, the IOMMU is implemented in SW by the client, and is not
visible to the server. The simplest idea would be that the client put each
device into its own group and container.

Backend Program Conventions
---------------------------

vfio-user backend program conventions are based on the vhost-user ones.

* The backend program must not daemonize itself.
* No assumptions must be made as to what access the backend program has on the
  system.
* File descriptors 0, 1 and 2 must exist, must have regular
  stdin/stdout/stderr semantics, and can be redirected.
* The backend program must honor the SIGTERM signal.
* The backend program must accept the following commands line options:

  * ``--socket-path=PATH``: path to UNIX domain socket,
  * ``--fd=FDNUM``: file descriptor for UNIX domain socket, incompatible with
    ``--socket-path``
* The backend program must be accompanied with a JSON file stored under
  ``/usr/share/vfio-user``.

TODO add schema similar to docs/interop/vhost-user.json.
