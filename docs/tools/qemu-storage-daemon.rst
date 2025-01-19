.. _storage-daemon:

===================
QEMU Storage Daemon
===================

Synopsis
--------

**qemu-storage-daemon** [options]

Description
-----------

``qemu-storage-daemon`` provides disk image functionality from QEMU,
``qemu-img``, and ``qemu-nbd`` in a long-running process controlled via QMP
commands without running a virtual machine.
It can export disk images, run block job operations, and
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

  --chardev socket,id=char1,path=/var/run/qsd-qmp.sock,server=on,wait=off

.. option:: --export [type=]nbd,id=<id>,node-name=<node-name>[,name=<export-name>][,writable=on|off][,bitmap=<name>]
  --export [type=]vhost-user-blk,id=<id>,node-name=<node-name>,addr.type=unix,addr.path=<socket-path>[,writable=on|off][,logical-block-size=<block-size>][,num-queues=<num-queues>]
  --export [type=]vhost-user-blk,id=<id>,node-name=<node-name>,addr.type=fd,addr.str=<fd>[,writable=on|off][,logical-block-size=<block-size>][,num-queues=<num-queues>]
  --export [type=]fuse,id=<id>,node-name=<node-name>,mountpoint=<file>[,growable=on|off][,writable=on|off][,allow-other=on|off|auto]
  --export [type=]vduse-blk,id=<id>,node-name=<node-name>,name=<vduse-name>[,writable=on|off][,num-queues=<num-queues>][,queue-size=<queue-size>][,logical-block-size=<block-size>][,serial=<serial-number>]

  is a block export definition. ``node-name`` is the block node that should be
  exported. ``writable`` determines whether or not the export allows write
  requests for modifying data (the default is off).

  The ``nbd`` export type requires ``--nbd-server`` (see below). ``name`` is
  the NBD export name (if not specified, it defaults to the given
  ``node-name``). ``bitmap`` is the name of a dirty bitmap reachable from the
  block node, so the NBD client can use NBD_OPT_SET_META_CONTEXT with the
  metadata context name "qemu:dirty-bitmap:BITMAP" to inspect the bitmap.

  The ``vhost-user-blk`` export type takes a vhost-user socket address on which
  it accept incoming connections. Both
  ``addr.type=unix,addr.path=<socket-path>`` for UNIX domain sockets and
  ``addr.type=fd,addr.str=<fd>`` for file descriptor passing are supported.
  ``logical-block-size`` sets the logical block size in bytes (the default is
  512). ``num-queues`` sets the number of virtqueues (the default is 1).

  The ``fuse`` export type takes a mount point, which must be a regular file,
  on which to export the given block node. That file will not be changed, it
  will just appear to have the block node's content while the export is active
  (very much like mounting a filesystem on a directory does not change what the
  directory contains, it only shows a different content while the filesystem is
  mounted). Consequently, applications that have opened the given file before
  the export became active will continue to see its original content. If
  ``growable`` is set, writes after the end of the exported file will grow the
  block node to fit.  The ``allow-other`` option controls whether users other
  than the user running the process will be allowed to access the export.  Note
  that enabling this option as a non-root user requires enabling the
  user_allow_other option in the global fuse.conf configuration file.  Setting
  ``allow-other`` to auto (the default) will try enabling this option, and on
  error fall back to disabling it.

  The ``vduse-blk`` export type takes a ``name`` (must be unique across the host)
  to create the VDUSE device.
  ``num-queues`` sets the number of virtqueues (the default is 1).
  ``queue-size`` sets the virtqueue descriptor table size (the default is 256).

  The instantiated VDUSE device must then be added to the vDPA bus using the
  vdpa(8) command from the iproute2 project::

  # vdpa dev add name <id> mgmtdev vduse

  The device can be removed from the vDPA bus later as follows::

  # vdpa dev del <id>

  For more information about attaching vDPA devices to the host with
  virtio_vdpa.ko or attaching them to guests with vhost_vdpa.ko, see
  https://vdpa-dev.gitlab.io/.

  For more information about VDUSE, see
  https://docs.kernel.org/userspace-api/vduse.html.

.. option:: --monitor MONITORDEF

  is a QMP monitor definition. See the :manpage:`qemu(1)` manual page for
  a description of QMP monitor properties. A common QMP monitor definition
  configures a monitor on character device ``char1``::

  --monitor chardev=char1

.. option:: --nbd-server addr.type=inet,addr.host=<host>,addr.port=<port>[,tls-creds=<id>][,tls-authz=<id>][,max-connections=<n>]
  --nbd-server addr.type=unix,addr.path=<path>[,tls-creds=<id>][,tls-authz=<id>][,max-connections=<n>]
  --nbd-server addr.type=fd,addr.str=<fd>[,tls-creds=<id>][,tls-authz=<id>][,max-connections=<n>]

  is a server for NBD exports. Both TCP and UNIX domain sockets are supported.
  A listen socket can be provided via file descriptor passing (see Examples
  below). TLS encryption can be configured using ``--object`` tls-creds-* and
  authz-* secrets (see below).

  To configure an NBD server on UNIX domain socket path
  ``/var/run/qsd-nbd.sock``::

  --nbd-server addr.type=unix,addr.path=/var/run/qsd-nbd.sock

.. option:: --object help
  --object <type>,help
  --object <type>[,<property>=<value>...]

  is a QEMU user creatable object definition. List object types with ``help``.
  List object properties with ``<type>,help``. See the :manpage:`qemu(1)`
  manual page for a description of the object properties.

.. option:: --pidfile PATH

  is the path to a file where the daemon writes its pid. This allows scripts to
  stop the daemon by sending a signal::

    $ kill -SIGTERM $(<path/to/qsd.pid)

  A file lock is applied to the file so only one instance of the daemon can run
  with a given pid file path. The daemon unlinks its pid file when terminating.

  The pid file is written after chardevs, exports, and NBD servers have been
  created but before accepting connections. The daemon has started successfully
  when the pid file is written and clients may begin connecting.

.. option:: --daemonize

  Daemonize the process. The parent process will exit once startup is complete
  (i.e., after the pid file has been or would have been written) or failure
  occurs. Its exit code reflects whether the child has started up successfully
  or failed to do so.

Examples
--------
Launch the daemon with QMP monitor socket ``qmp.sock`` so clients can execute
QMP commands::

  $ qemu-storage-daemon \
      --chardev socket,path=qmp.sock,server=on,wait=off,id=char1 \
      --monitor chardev=char1

Launch the daemon from Python with a QMP monitor socket using file descriptor
passing so there is no need to busy wait for the QMP monitor to become
available::

  #!/usr/bin/env python3
  import subprocess
  import socket

  sock_path = '/var/run/qmp.sock'

  with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listen_sock:
      listen_sock.bind(sock_path)
      listen_sock.listen()

      fd = listen_sock.fileno()

      subprocess.Popen(
          ['qemu-storage-daemon',
           '--chardev', f'socket,fd={fd},server=on,id=char1',
           '--monitor', 'chardev=char1'],
          pass_fds=[fd],
      )

  # listen_sock was automatically closed when leaving the 'with' statement
  # body. If the daemon process terminated early then the following connect()
  # will fail with "Connection refused" because no process has the listen
  # socket open anymore. Launch errors can be detected this way.

  qmp_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  qmp_sock.connect(sock_path)
  ...QMP interaction...

The same socket spawning approach also works with the ``--nbd-server
addr.type=fd,addr.str=<fd>`` and ``--export
type=vhost-user-blk,addr.type=fd,addr.str=<fd>`` options.

Export raw image file ``disk.img`` over NBD UNIX domain socket ``nbd.sock``::

  $ qemu-storage-daemon \
      --blockdev driver=file,node-name=disk,filename=disk.img \
      --nbd-server addr.type=unix,addr.path=nbd.sock \
      --export type=nbd,id=export,node-name=disk,writable=on

Export a qcow2 image file ``disk.qcow2`` as a vhost-user-blk device over UNIX
domain socket ``vhost-user-blk.sock``::

  $ qemu-storage-daemon \
      --blockdev driver=file,node-name=file,filename=disk.qcow2 \
      --blockdev driver=qcow2,node-name=qcow2,file=file \
      --export type=vhost-user-blk,id=export,addr.type=unix,addr.path=vhost-user-blk.sock,node-name=qcow2

Export a qcow2 image file ``disk.qcow2`` via FUSE on itself, so the disk image
file will then appear as a raw image::

  $ qemu-storage-daemon \
      --blockdev driver=file,node-name=file,filename=disk.qcow2 \
      --blockdev driver=qcow2,node-name=qcow2,file=file \
      --export type=fuse,id=export,node-name=qcow2,mountpoint=disk.qcow2,writable=on

See also
--------

:manpage:`qemu(1)`, :manpage:`qemu-block-drivers(7)`, :manpage:`qemu-storage-daemon-qmp-ref(7)`
