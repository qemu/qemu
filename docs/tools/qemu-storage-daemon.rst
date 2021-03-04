QEMU Storage Daemon
===================

Synopsis
--------

**qemu-storage-daemon** [options]

Description
-----------

qemu-storage-daemon provides disk image functionality from QEMU, qemu-img, and
qemu-nbd in a long-running process controlled via QMP commands without running
a virtual machine. It can export disk images, run block job operations, and
perform other disk-related operations. The daemon is controlled via a QMP
monitor and initial configuration from the command-line.

The daemon offers the following subset of QEMU features:

* Block nodes
* Block jobs
* Block exports
* Throttle groups
* Character devices
* Crypto and secrets
* QMP
* IOThreads

Commands can be sent over a QEMU Monitor Protocol (QMP) connection. See the
:manpage:`qemu-storage-daemon-qmp-ref(7)` manual page for a description of the
commands.

The daemon runs until it is stopped using the ``quit`` QMP command or
SIGINT/SIGHUP/SIGTERM.

**Warning:** Never modify images in use by a running virtual machine or any
other process; this may destroy the image. Also, be aware that querying an
image that is being modified by another process may encounter inconsistent
state.

Options
-------

.. program:: qemu-storage-daemon

Standard options:

.. option:: -h, --help

  Display help and exit

.. option:: -V, --version

  Display version information and exit

.. option:: -T, --trace [[enable=]PATTERN][,events=FILE][,file=FILE]

  .. include:: ../qemu-option-trace.rst.inc

.. option:: --blockdev BLOCKDEVDEF

  is a block node definition. See the :manpage:`qemu(1)` manual page for a
  description of block node properties and the :manpage:`qemu-block-drivers(7)`
  manual page for a description of driver-specific parameters.

.. option:: --chardev CHARDEVDEF

  is a character device definition. See the :manpage:`qemu(1)` manual page for
  a description of character device properties. A common character device
  definition configures a UNIX domain socket::

  --chardev socket,id=char1,path=/tmp/qmp.sock,server=on,wait=off

.. option:: --export [type=]nbd,id=<id>,node-name=<node-name>[,name=<export-name>][,writable=on|off][,bitmap=<name>]
  --export [type=]vhost-user-blk,id=<id>,node-name=<node-name>,addr.type=unix,addr.path=<socket-path>[,writable=on|off][,logical-block-size=<block-size>][,num-queues=<num-queues>]
  --export [type=]vhost-user-blk,id=<id>,node-name=<node-name>,addr.type=fd,addr.str=<fd>[,writable=on|off][,logical-block-size=<block-size>][,num-queues=<num-queues>]

  is a block export definition. ``node-name`` is the block node that should be
  exported. ``writable`` determines whether or not the export allows write
  requests for modifying data (the default is off).

  The ``nbd`` export type requires ``--nbd-server`` (see below). ``name`` is
  the NBD export name. ``bitmap`` is the name of a dirty bitmap reachable from
  the block node, so the NBD client can use NBD_OPT_SET_META_CONTEXT with the
  metadata context name "qemu:dirty-bitmap:BITMAP" to inspect the bitmap.

  The ``vhost-user-blk`` export type takes a vhost-user socket address on which
  it accept incoming connections. Both
  ``addr.type=unix,addr.path=<socket-path>`` for UNIX domain sockets and
  ``addr.type=fd,addr.str=<fd>`` for file descriptor passing are supported.
  ``logical-block-size`` sets the logical block size in bytes (the default is
  512). ``num-queues`` sets the number of virtqueues (the default is 1).

.. option:: --monitor MONITORDEF

  is a QMP monitor definition. See the :manpage:`qemu(1)` manual page for
  a description of QMP monitor properties. A common QMP monitor definition
  configures a monitor on character device ``char1``::

  --monitor chardev=char1

.. option:: --nbd-server addr.type=inet,addr.host=<host>,addr.port=<port>[,tls-creds=<id>][,tls-authz=<id>][,max-connections=<n>]
  --nbd-server addr.type=unix,addr.path=<path>[,tls-creds=<id>][,tls-authz=<id>][,max-connections=<n>]

  is a server for NBD exports. Both TCP and UNIX domain sockets are supported.
  TLS encryption can be configured using ``--object`` tls-creds-* and authz-*
  secrets (see below).

  To configure an NBD server on UNIX domain socket path ``/tmp/nbd.sock``::

  --nbd-server addr.type=unix,addr.path=/tmp/nbd.sock

.. option:: --object help
  --object <type>,help
  --object <type>[,<property>=<value>...]

  is a QEMU user creatable object definition. List object types with ``help``.
  List object properties with ``<type>,help``. See the :manpage:`qemu(1)`
  manual page for a description of the object properties.

Examples
--------
Launch the daemon with QMP monitor socket ``qmp.sock`` so clients can execute
QMP commands::

  $ qemu-storage-daemon \
      --chardev socket,path=qmp.sock,server=on,wait=off,id=char1 \
      --monitor chardev=char1

Export raw image file ``disk.img`` over NBD UNIX domain socket ``nbd.sock``::

  $ qemu-storage-daemon \
      --blockdev driver=file,node-name=disk,filename=disk.img \
      --nbd-server addr.type=unix,addr.path=nbd.sock \
      --export type=nbd,id=export,node-name=disk,writable=on

Export a qcow2 image file ``disk.qcow2`` as a vhosts-user-blk device over UNIX
domain socket ``vhost-user-blk.sock``::

  $ qemu-storage-daemon \
      --blockdev driver=file,node-name=file,filename=disk.qcow2 \
      --blockdev driver=qcow2,node-name=qcow2,file=file \
      --export type=vhost-user-blk,id=export,addr.type=unix,addr.path=vhost-user-blk.sock,node-name=qcow2

See also
--------

:manpage:`qemu(1)`, :manpage:`qemu-block-drivers(7)`, :manpage:`qemu-storage-daemon-qmp-ref(7)`
