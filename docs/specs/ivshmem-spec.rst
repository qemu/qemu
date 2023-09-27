======================================================
Device Specification for Inter-VM shared memory device
======================================================

The Inter-VM shared memory device (ivshmem) is designed to share a
memory region between multiple QEMU processes running different guests
and the host.  In order for all guests to be able to pick up the
shared memory area, it is modeled by QEMU as a PCI device exposing
said memory to the guest as a PCI BAR.

The device can use a shared memory object on the host directly, or it
can obtain one from an ivshmem server.

In the latter case, the device can additionally interrupt its peers, and
get interrupted by its peers.

For information on configuring the ivshmem device on the QEMU
command line, see :doc:`../system/devices/ivshmem`.

The ivshmem PCI device's guest interface
========================================

The device has vendor ID 1af4, device ID 1110, revision 1.  Before
QEMU 2.6.0, it had revision 0.

PCI BARs
--------

The ivshmem PCI device has two or three BARs:

- BAR0 holds device registers (256 Byte MMIO)
- BAR1 holds MSI-X table and PBA (only ivshmem-doorbell)
- BAR2 maps the shared memory object

There are two ways to use this device:

- If you only need the shared memory part, BAR2 suffices.  This way,
  you have access to the shared memory in the guest and can use it as
  you see fit.

- If you additionally need the capability for peers to interrupt each
  other, you need BAR0 and BAR1.  You will most likely want to write a
  kernel driver to handle interrupts.  Requires the device to be
  configured for interrupts, obviously.

Before QEMU 2.6.0, BAR2 can initially be invalid if the device is
configured for interrupts.  It becomes safely accessible only after
the ivshmem server provided the shared memory.  These devices have PCI
revision 0 rather than 1.  Guest software should wait for the
IVPosition register (described below) to become non-negative before
accessing BAR2.

Revision 0 of the device is not capable to tell guest software whether
it is configured for interrupts.

PCI device registers
--------------------

BAR 0 contains the following registers:

::

    Offset  Size  Access      On reset  Function
        0     4   read/write        0   Interrupt Mask
                                        bit 0: peer interrupt (rev 0)
                                               reserved       (rev 1)
                                        bit 1..31: reserved
        4     4   read/write        0   Interrupt Status
                                        bit 0: peer interrupt (rev 0)
                                               reserved       (rev 1)
                                        bit 1..31: reserved
        8     4   read-only   0 or ID   IVPosition
       12     4   write-only      N/A   Doorbell
                                        bit 0..15: vector
                                        bit 16..31: peer ID
       16   240   none            N/A   reserved

Software should only access the registers as specified in column
"Access".  Reserved bits should be ignored on read, and preserved on
write.

In revision 0 of the device, Interrupt Status and Mask Register
together control the legacy INTx interrupt when the device has no
MSI-X capability: INTx is asserted when the bit-wise AND of Status and
Mask is non-zero and the device has no MSI-X capability.  Interrupt
Status Register bit 0 becomes 1 when an interrupt request from a peer
is received.  Reading the register clears it.

IVPosition Register: if the device is not configured for interrupts,
this is zero.  Else, it is the device's ID (between 0 and 65535).

Before QEMU 2.6.0, the register may read -1 for a short while after
reset.  These devices have PCI revision 0 rather than 1.

There is no good way for software to find out whether the device is
configured for interrupts.  A positive IVPosition means interrupts,
but zero could be either.

Doorbell Register: writing this register requests to interrupt a peer.
The written value's high 16 bits are the ID of the peer to interrupt,
and its low 16 bits select an interrupt vector.

If the device is not configured for interrupts, the write is ignored.

If the interrupt hasn't completed setup, the write is ignored.  The
device is not capable to tell guest software whether setup is
complete.  Interrupts can regress to this state on migration.

If the peer with the requested ID isn't connected, or it has fewer
interrupt vectors connected, the write is ignored.  The device is not
capable to tell guest software what peers are connected, or how many
interrupt vectors are connected.

The peer's interrupt for this vector then becomes pending.  There is
no way for software to clear the pending bit, and a polling mode of
operation is therefore impossible.

If the peer is a revision 0 device without MSI-X capability, its
Interrupt Status register is set to 1.  This asserts INTx unless
masked by the Interrupt Mask register.  The device is not capable to
communicate the interrupt vector to guest software then.

With multiple MSI-X vectors, different vectors can be used to indicate
different events have occurred.  The semantics of interrupt vectors
are left to the application.

Interrupt infrastructure
========================

When configured for interrupts, the peers share eventfd objects in
addition to shared memory.  The shared resources are managed by an
ivshmem server.

The ivshmem server
------------------

The server listens on a UNIX domain socket.

For each new client that connects to the server, the server

- picks an ID,
- creates eventfd file descriptors for the interrupt vectors,
- sends the ID and the file descriptor for the shared memory to the
  new client,
- sends connect notifications for the new client to the other clients
  (these contain file descriptors for sending interrupts),
- sends connect notifications for the other clients to the new client,
  and
- sends interrupt setup messages to the new client (these contain file
  descriptors for receiving interrupts).

The first client to connect to the server receives ID zero.

When a client disconnects from the server, the server sends disconnect
notifications to the other clients.

The next section describes the protocol in detail.

If the server terminates without sending disconnect notifications for
its connected clients, the clients can elect to continue.  They can
communicate with each other normally, but won't receive disconnect
notification on disconnect, and no new clients can connect.  There is
no way for the clients to connect to a restarted server.  The device
is not capable to tell guest software whether the server is still up.

Example server code is in contrib/ivshmem-server/.  Not to be used in
production.  It assumes all clients use the same number of interrupt
vectors.

A standalone client is in contrib/ivshmem-client/.  It can be useful
for debugging.

The ivshmem Client-Server Protocol
----------------------------------

An ivshmem device configured for interrupts connects to an ivshmem
server.  This section details the protocol between the two.

The connection is one-way: the server sends messages to the client.
Each message consists of a single 8 byte little-endian signed number,
and may be accompanied by a file descriptor via SCM_RIGHTS.  Both
client and server close the connection on error.

Note: QEMU currently doesn't close the connection right on error, but
only when the character device is destroyed.

On connect, the server sends the following messages in order:

1. The protocol version number, currently zero.  The client should
   close the connection on receipt of versions it can't handle.

2. The client's ID.  This is unique among all clients of this server.
   IDs must be between 0 and 65535, because the Doorbell register
   provides only 16 bits for them.

3. The number -1, accompanied by the file descriptor for the shared
   memory.

4. Connect notifications for existing other clients, if any.  This is
   a peer ID (number between 0 and 65535 other than the client's ID),
   repeated N times.  Each repetition is accompanied by one file
   descriptor.  These are for interrupting the peer with that ID using
   vector 0,..,N-1, in order.  If the client is configured for fewer
   vectors, it closes the extra file descriptors.  If it is configured
   for more, the extra vectors remain unconnected.

5. Interrupt setup.  This is the client's own ID, repeated N times.
   Each repetition is accompanied by one file descriptor.  These are
   for receiving interrupts from peers using vector 0,..,N-1, in
   order.  If the client is configured for fewer vectors, it closes
   the extra file descriptors.  If it is configured for more, the
   extra vectors remain unconnected.

From then on, the server sends these kinds of messages:

6. Connection / disconnection notification.  This is a peer ID.

  - If the number comes with a file descriptor, it's a connection
    notification, exactly like in step 4.

  - Else, it's a disconnection notification for the peer with that ID.

Known bugs:

* The protocol changed incompatibly in QEMU 2.5.  Before, messages
  were native endian long, and there was no version number.

* The protocol is poorly designed.

The ivshmem Client-Client Protocol
----------------------------------

An ivshmem device configured for interrupts receives eventfd file
descriptors for interrupting peers and getting interrupted by peers
from the server, as explained in the previous section.

To interrupt a peer, the device writes the 8-byte integer 1 in native
byte order to the respective file descriptor.

To receive an interrupt, the device reads and discards as many 8-byte
integers as it can.
