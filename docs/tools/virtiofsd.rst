QEMU virtio-fs shared file system daemon
========================================

Synopsis
--------

**virtiofsd** [*OPTIONS*]

Description
-----------

Share a host directory tree with a guest through a virtio-fs device.  This
program is a vhost-user backend that implements the virtio-fs device.  Each
virtio-fs device instance requires its own virtiofsd process.

This program is designed to work with QEMU's ``--device vhost-user-fs-pci``
but should work with any virtual machine monitor (VMM) that supports
vhost-user.  See the Examples section below.

This program must be run as the root user.  Upon startup the program will
switch into a new file system namespace with the shared directory tree as its
root.  This prevents "file system escapes" due to symlinks and other file
system objects that might lead to files outside the shared directory.  The
program also sandboxes itself using seccomp(2) to prevent ptrace(2) and other
vectors that could allow an attacker to compromise the system after gaining
control of the virtiofsd process.

Options
-------

.. program:: virtiofsd

.. option:: -h, --help

  Print help.

.. option:: -V, --version

  Print version.

.. option:: -d

  Enable debug output.

.. option:: --syslog

  Print log messages to syslog instead of stderr.

.. option:: -o OPTION

  * debug -
    Enable debug output.

  * flock|no_flock -
    Enable/disable flock.  The default is ``no_flock``.

  * modcaps=CAPLIST
    Modify the list of capabilities allowed; CAPLIST is a colon separated
    list of capabilities, each preceded by either + or -, e.g.
    ''+sys_admin:-chown''.

  * log_level=LEVEL -
    Print only log messages matching LEVEL or more severe.  LEVEL is one of
    ``err``, ``warn``, ``info``, or ``debug``.  The default is ``info``.

  * norace -
    Disable racy fallback.  The default is false.

  * posix_lock|no_posix_lock -
    Enable/disable remote POSIX locks.  The default is ``posix_lock``.

  * readdirplus|no_readdirplus -
    Enable/disable readdirplus.  The default is ``readdirplus``.

  * source=PATH -
    Share host directory tree located at PATH.  This option is required.

  * timeout=TIMEOUT -
    I/O timeout in seconds.  The default depends on cache= option.

  * writeback|no_writeback -
    Enable/disable writeback cache. The cache alows the FUSE client to buffer
    and merge write requests.  The default is ``no_writeback``.

  * xattr|no_xattr -
    Enable/disable extended attributes (xattr) on files and directories.  The
    default is ``no_xattr``.

.. option:: --socket-path=PATH

  Listen on vhost-user UNIX domain socket at PATH.

.. option:: --fd=FDNUM

  Accept connections from vhost-user UNIX domain socket file descriptor FDNUM.
  The file descriptor must already be listening for connections.

.. option:: --thread-pool-size=NUM

  Restrict the number of worker threads per request queue to NUM.  The default
  is 64.

.. option:: --cache=none|auto|always

  Select the desired trade-off between coherency and performance.  ``none``
  forbids the FUSE client from caching to achieve best coherency at the cost of
  performance.  ``auto`` acts similar to NFS with a 1 second metadata cache
  timeout.  ``always`` sets a long cache lifetime at the expense of coherency.

Examples
--------

Export ``/var/lib/fs/vm001/`` on vhost-user UNIX domain socket
``/var/run/vm001-vhost-fs.sock``:

::

  host# virtiofsd --socket-path=/var/run/vm001-vhost-fs.sock -o source=/var/lib/fs/vm001
  host# qemu-system-x86_64 \
      -chardev socket,id=char0,path=/var/run/vm001-vhost-fs.sock \
      -device vhost-user-fs-pci,chardev=char0,tag=myfs \
      -object memory-backend-memfd,id=mem,size=4G,share=on \
      -numa node,memdev=mem \
      ...
  guest# mount -t virtiofs myfs /mnt
