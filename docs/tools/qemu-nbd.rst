=====================================
QEMU Disk Network Block Device Server
=====================================

Synopsis
--------

**qemu-nbd** [*OPTION*]... *filename*

**qemu-nbd** -L [*OPTION*]...

**qemu-nbd** -d *dev*

Description
-----------

Export a QEMU disk image using the NBD protocol.

Other uses:

- Bind a /dev/nbdX block device to a QEMU server (on Linux).
- As a client to query exports of a remote NBD server.

Options
-------

.. program:: qemu-nbd

*filename* is a disk image filename, or a set of block
driver options if :option:`--image-opts` is specified.

*dev* is an NBD device.

.. option:: --object type,id=ID,...

  Define a new instance of the *type* object class identified by *ID*.
  See the :manpage:`qemu(1)` manual page for full details of the properties
  supported. The common object types that it makes sense to define are the
  ``secret`` object, which is used to supply passwords and/or encryption
  keys, and the ``tls-creds`` object, which is used to supply TLS
  credentials for the ``qemu-nbd`` server or client.

.. option:: -p, --port=PORT

  TCP port to listen on as a server, or connect to as a client
  (default ``10809``).

.. option:: -o, --offset=OFFSET

  The offset into the image.

.. option:: -b, --bind=IFACE

  The interface to bind to as a server, or connect to as a client
  (default ``0.0.0.0``).

.. option:: -k, --socket=PATH

  Use a unix socket with path *PATH*.

.. option:: --image-opts

  Treat *filename* as a set of image options, instead of a plain
  filename. If this flag is specified, the ``-f`` flag should
  not be used, instead the :option:`format=` option should be set.

.. option:: -f, --format=FMT

  Force the use of the block driver for format *FMT* instead of
  auto-detecting.

.. option:: -r, --read-only

  Export the disk as read-only.

.. option:: -A, --allocation-depth

  Expose allocation depth information via the
  ``qemu:allocation-depth`` metadata context accessible through
  NBD_OPT_SET_META_CONTEXT.

.. option:: -B, --bitmap=NAME

  If *filename* has a qcow2 persistent bitmap *NAME*, expose
  that bitmap via the ``qemu:dirty-bitmap:NAME`` metadata context
  accessible through NBD_OPT_SET_META_CONTEXT.

.. option:: -s, --snapshot

  Use *filename* as an external snapshot, create a temporary
  file with ``backing_file=``\ *filename*, redirect the write to
  the temporary one.

.. option:: -l, --load-snapshot=SNAPSHOT_PARAM

  Load an internal snapshot inside *filename* and export it
  as an read-only device, SNAPSHOT_PARAM format is
  ``snapshot.id=[ID],snapshot.name=[NAME]`` or ``[ID_OR_NAME]``

.. option:: --cache=CACHE

  The cache mode to be used with the file. Valid values are:
  ``none``, ``writeback`` (the default), ``writethrough``,
  ``directsync`` and ``unsafe``. See the documentation of
  the emulator's ``-drive cache=...`` option for more info.

.. option:: -n, --nocache

  Equivalent to :option:`--cache=none`.

.. option:: --aio=AIO

  Set the asynchronous I/O mode between ``threads`` (the default),
  ``native`` (Linux only), and ``io_uring`` (Linux 5.1+).

.. option:: --discard=DISCARD

  Control whether ``discard`` (also known as ``trim`` or ``unmap``)
  requests are ignored or passed to the filesystem. *DISCARD* is one of
  ``ignore`` (or ``off``), ``unmap`` (or ``on``).  The default is
  ``ignore``.

.. option:: --detect-zeroes=DETECT_ZEROES

  Control the automatic conversion of plain zero writes by the OS to
  driver-specific optimized zero write commands.  *DETECT_ZEROES* is one of
  ``off``, ``on``, or ``unmap``.  ``unmap``
  converts a zero write to an unmap operation and can only be used if
  *DISCARD* is set to ``unmap``.  The default is ``off``.

.. option:: -c, --connect=DEV

  Connect *filename* to NBD device *DEV* (Linux only).

.. option:: -d, --disconnect

  Disconnect the device *DEV* (Linux only).

.. option:: -e, --shared=NUM

  Allow up to *NUM* clients to share the device (default
  ``1``), 0 for unlimited.

.. option:: -t, --persistent

  Don't exit on the last connection.

.. option:: -x, --export-name=NAME

  Set the NBD volume export name (default of a zero-length string).

.. option:: -D, --description=DESCRIPTION

  Set the NBD volume export description, as a human-readable
  string.

.. option:: -L, --list

  Connect as a client and list all details about the exports exposed by
  a remote NBD server.  This enables list mode, and is incompatible
  with options that change behavior related to a specific export (such as
  :option:`--export-name`, :option:`--offset`, ...).

.. option:: --tls-creds=ID

  Enable mandatory TLS encryption for the server by setting the ID
  of the TLS credentials object previously created with the
  :option:`--object` option; or provide the credentials needed for
  connecting as a client in list mode.

.. option:: --tls-hostname=hostname

  When validating an x509 certificate received over a TLS connection,
  the hostname that the NBD client used to connect will be checked
  against information in the server provided certificate. Sometimes
  it might be required to override the hostname used to perform this
  check. For example, if the NBD client is using a tunnel from localhost
  to connect to the remote server, the :option:`--tls-hostname` option should
  be used to set the officially expected hostname of the remote NBD
  server. This can also be used if accessing NBD over a UNIX socket
  where there is no inherent hostname available. This is only permitted
  when acting as a NBD client with the :option:`--list` option.

.. option:: --fork

  Fork off the server process and exit the parent once the server is running.

.. option:: --pid-file=PATH

  Store the server's process ID in the given file.

.. option:: --tls-authz=ID

  Specify the ID of a qauthz object previously created with the
  :option:`--object` option. This will be used to authorize connecting users
  against their x509 distinguished name.

.. option:: -v, --verbose

  Display extra debugging information.

.. option:: -h, --help

  Display this help and exit.

.. option:: -V, --version

  Display version information and exit.

.. option:: -T, --trace [[enable=]PATTERN][,events=FILE][,file=FILE]

  .. include:: ../qemu-option-trace.rst.inc

Examples
--------

Start a server listening on port 10809 that exposes only the
guest-visible contents of a qcow2 file, with no TLS encryption, and
with the default export name (an empty string). The command is
one-shot, and will block until the first successful client
disconnects:

::

  qemu-nbd -f qcow2 file.qcow2

Start a long-running server listening with encryption on port 10810,
and allow clients with a specific X.509 certificate to connect to
a 1 megabyte subset of a raw file, using the export name 'subset':

::

  qemu-nbd \
    --object tls-creds-x509,id=tls0,endpoint=server,dir=/path/to/qemutls \
    --object 'authz-simple,id=auth0,identity=CN=laptop.example.com,,\
              O=Example Org,,L=London,,ST=London,,C=GB' \
    --tls-creds tls0 --tls-authz auth0 \
    -t -x subset -p 10810 \
    --image-opts driver=raw,offset=1M,size=1M,file.driver=file,file.filename=file.raw

Serve a read-only copy of a guest image over a Unix socket with as
many as 5 simultaneous readers, with a persistent process forked as a
daemon:

::

  qemu-nbd --fork --persistent --shared=5 --socket=/path/to/sock \
    --read-only --format=qcow2 file.qcow2

Expose the guest-visible contents of a qcow2 file via a block device
/dev/nbd0 (and possibly creating /dev/nbd0p1 and friends for
partitions found within), then disconnect the device when done.
Access to bind ``qemu-nbd`` to a /dev/nbd device generally requires root
privileges, and may also require the execution of ``modprobe nbd``
to enable the kernel NBD client module.  *CAUTION*: Do not use
this method to mount filesystems from an untrusted guest image - a
malicious guest may have prepared the image to attempt to trigger
kernel bugs in partition probing or file system mounting.

::

  qemu-nbd -c /dev/nbd0 -f qcow2 file.qcow2
  qemu-nbd -d /dev/nbd0

Query a remote server to see details about what export(s) it is
serving on port 10809, and authenticating via PSK:

::

  qemu-nbd \
    --object tls-creds-psk,id=tls0,dir=/tmp/keys,username=eblake,endpoint=client \
    --tls-creds tls0 -L -b remote.example.com

See also
--------

:manpage:`qemu(1)`, :manpage:`qemu-img(1)`
