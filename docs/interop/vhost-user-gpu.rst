=======================
Vhost-user-gpu Protocol
=======================

:Licence: This work is licensed under the terms of the GNU GPL,
          version 2 or later. See the COPYING file in the top-level
          directory.

.. contents:: Table of Contents

Introduction
============

The vhost-user-gpu protocol is aiming at sharing the rendering result
of a virtio-gpu, done from a vhost-user slave process to a vhost-user
master process (such as QEMU). It bears a resemblance to a display
server protocol, if you consider QEMU as the display server and the
slave as the client, but in a very limited way. Typically, it will
work by setting a scanout/display configuration, before sending flush
events for the display updates. It will also update the cursor shape
and position.

The protocol is sent over a UNIX domain stream socket, since it uses
socket ancillary data to share opened file descriptors (DMABUF fds or
shared memory). The socket is usually obtained via
``VHOST_USER_GPU_SET_SOCKET``.

Requests are sent by the *slave*, and the optional replies by the
*master*.

Wire format
===========

Unless specified differently, numbers are in the machine native byte
order.

A vhost-user-gpu message (request and reply) consists of 3 header
fields and a payload.

+---------+-------+------+---------+
| request | flags | size | payload |
+---------+-------+------+---------+

Header
------

:request: ``u32``, type of the request

:flags: ``u32``, 32-bit bit field:

 - Bit 2 is the reply flag - needs to be set on each reply

:size: ``u32``, size of the payload

Payload types
-------------

Depending on the request type, **payload** can be:

VhostUserGpuCursorPos
^^^^^^^^^^^^^^^^^^^^^

+------------+---+---+
| scanout-id | x | y |
+------------+---+---+

:scanout-id: ``u32``, the scanout where the cursor is located

:x/y: ``u32``, the cursor position

VhostUserGpuCursorUpdate
^^^^^^^^^^^^^^^^^^^^^^^^

+-----+-------+-------+--------+
| pos | hot_x | hot_y | cursor |
+-----+-------+-------+--------+

:pos: a ``VhostUserGpuCursorPos``, the cursor location

:hot_x/hot_y: ``u32``, the cursor hot location

:cursor: ``[u32; 64 * 64]``, 64x64 RGBA cursor data (PIXMAN_a8r8g8b8 format)

VhostUserGpuScanout
^^^^^^^^^^^^^^^^^^^

+------------+---+---+
| scanout-id | w | h |
+------------+---+---+

:scanout-id: ``u32``, the scanout configuration to set

:w/h: ``u32``, the scanout width/height size

VhostUserGpuUpdate
^^^^^^^^^^^^^^^^^^

+------------+---+---+---+---+------+
| scanout-id | x | y | w | h | data |
+------------+---+---+---+---+------+

:scanout-id: ``u32``, the scanout content to update

:x/y/w/h: ``u32``, region of the update

:data: RGB data (PIXMAN_x8r8g8b8 format)

VhostUserGpuDMABUFScanout
^^^^^^^^^^^^^^^^^^^^^^^^^

+------------+---+---+---+---+-----+-----+--------+-------+--------+
| scanout-id | x | y | w | h | fdw | fwh | stride | flags | fourcc |
+------------+---+---+---+---+-----+-----+--------+-------+--------+

:scanout-id: ``u32``, the scanout configuration to set

:x/y: ``u32``, the location of the scanout within the DMABUF

:w/h: ``u32``, the scanout width/height size

:fdw/fdh/stride/flags: ``u32``, the DMABUF width/height/stride/flags

:fourcc: ``i32``, the DMABUF fourcc


C structure
-----------

In QEMU the vhost-user-gpu message is implemented with the following struct:

.. code:: c

  typedef struct VhostUserGpuMsg {
      uint32_t request; /* VhostUserGpuRequest */
      uint32_t flags;
      uint32_t size; /* the following payload size */
      union {
          VhostUserGpuCursorPos cursor_pos;
          VhostUserGpuCursorUpdate cursor_update;
          VhostUserGpuScanout scanout;
          VhostUserGpuUpdate update;
          VhostUserGpuDMABUFScanout dmabuf_scanout;
          struct virtio_gpu_resp_display_info display_info;
          uint64_t u64;
      } payload;
  } QEMU_PACKED VhostUserGpuMsg;

Protocol features
-----------------

None yet.

As the protocol may need to evolve, new messages and communication
changes are negotiated thanks to preliminary
``VHOST_USER_GPU_GET_PROTOCOL_FEATURES`` and
``VHOST_USER_GPU_SET_PROTOCOL_FEATURES`` requests.

Communication
=============

Message types
-------------

``VHOST_USER_GPU_GET_PROTOCOL_FEATURES``
  :id: 1
  :request payload: N/A
  :reply payload: ``u64``

  Get the supported protocol features bitmask.

``VHOST_USER_GPU_SET_PROTOCOL_FEATURES``
  :id: 2
  :request payload: ``u64``
  :reply payload: N/A

  Enable protocol features using a bitmask.

``VHOST_USER_GPU_GET_DISPLAY_INFO``
  :id: 3
  :request payload: N/A
  :reply payload: ``struct virtio_gpu_resp_display_info`` (from virtio specification)

  Get the preferred display configuration.

``VHOST_USER_GPU_CURSOR_POS``
  :id: 4
  :request payload: ``VhostUserGpuCursorPos``
  :reply payload: N/A

  Set/show the cursor position.

``VHOST_USER_GPU_CURSOR_POS_HIDE``
  :id: 5
  :request payload: ``VhostUserGpuCursorPos``
  :reply payload: N/A

  Set/hide the cursor.

``VHOST_USER_GPU_CURSOR_UPDATE``
  :id: 6
  :request payload: ``VhostUserGpuCursorUpdate``
  :reply payload: N/A

  Update the cursor shape and location.

``VHOST_USER_GPU_SCANOUT``
  :id: 7
  :request payload: ``VhostUserGpuScanout``
  :reply payload: N/A

  Set the scanout resolution. To disable a scanout, the dimensions
  width/height are set to 0.

``VHOST_USER_GPU_UPDATE``
  :id: 8
  :request payload: ``VhostUserGpuUpdate``
  :reply payload: N/A

  Update the scanout content. The data payload contains the graphical bits.
  The display should be flushed and presented.

``VHOST_USER_GPU_DMABUF_SCANOUT``
  :id: 9
  :request payload: ``VhostUserGpuDMABUFScanout``
  :reply payload: N/A

  Set the scanout resolution/configuration, and share a DMABUF file
  descriptor for the scanout content, which is passed as ancillary
  data. To disable a scanout, the dimensions width/height are set
  to 0, there is no file descriptor passed.

``VHOST_USER_GPU_DMABUF_UPDATE``
  :id: 10
  :request payload: ``VhostUserGpuUpdate``
  :reply payload: empty payload

  The display should be flushed and presented according to updated
  region from ``VhostUserGpuUpdate``.

  Note: there is no data payload, since the scanout is shared thanks
  to DMABUF, that must have been set previously with
  ``VHOST_USER_GPU_DMABUF_SCANOUT``.
