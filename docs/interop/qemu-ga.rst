QEMU Guest Agent
================

Synopsis
--------

**qemu-ga** [*OPTIONS*]

Description
-----------

The QEMU Guest Agent is a daemon intended to be run within virtual
machines. It allows the hypervisor host to perform various operations
in the guest, such as:

- get information from the guest
- set the guest's system time
- read/write a file
- sync and freeze the filesystems
- suspend the guest
- reconfigure guest local processors
- set user's password
- ...

qemu-ga will read a system configuration file on startup (located at
|CONFDIR|\ ``/qemu-ga.conf`` by default), then parse remaining
configuration options on the command line. For the same key, the last
option wins, but the lists accumulate (see below for configuration
file format).

Options
-------

.. program:: qemu-ga

.. option:: -m, --method=METHOD

  Transport method: one of ``unix-listen``, ``virtio-serial``, or
  ``isa-serial``, or ``vsock-listen`` (``virtio-serial`` is the default).

.. option:: -p, --path=PATH

  Device/socket path (the default for virtio-serial is
  ``/dev/virtio-ports/org.qemu.guest_agent.0``,
  the default for isa-serial is ``/dev/ttyS0``). Socket addresses for
  vsock-listen are written as ``<cid>:<port>``.

.. option:: -l, --logfile=PATH

  Set log file path (default is stderr).

.. option:: -f, --pidfile=PATH

  Specify pid file (default is ``/var/run/qemu-ga.pid``).

.. option:: -F, --fsfreeze-hook=PATH

  Enable fsfreeze hook. Accepts an optional argument that specifies
  script to run on freeze/thaw. Script will be called with
  'freeze'/'thaw' arguments accordingly (default is
  |CONFDIR|\ ``/fsfreeze-hook``). If using -F with an argument, do
  not follow -F with a space (for example:
  ``-F/var/run/fsfreezehook.sh``).

.. option:: -t, --statedir=PATH

  Specify the directory to store state information (absolute paths only,
  default is ``/var/run``).

.. option:: -v, --verbose

  Log extra debugging information.

.. option:: -V, --version

  Print version information and exit.

.. option:: -d, --daemon

  Daemonize after startup (detach from terminal).

.. option:: -b, --block-rpcs=LIST

  Comma-separated list of RPCs to disable (no spaces, use ``--block-rpcs=help``
  to list available RPCs).

.. option:: -a, --allow-rpcs=LIST

  Comma-separated list of RPCs to enable (no spaces, use ``--allow-rpcs=help``
  to list available RPCs).

.. option:: -D, --dump-conf

  Dump the configuration in a format compatible with ``qemu-ga.conf``
  and exit.

.. option:: -h, --help

  Display this help and exit.

Files
-----


The syntax of the ``qemu-ga.conf`` configuration file follows the
Desktop Entry Specification, here is a quick summary: it consists of
groups of key-value pairs, interspersed with comments.

::

    # qemu-ga configuration sample
    [general]
    daemonize = 0
    pidfile = /var/run/qemu-ga.pid
    verbose = 0
    method = virtio-serial
    path = /dev/virtio-ports/org.qemu.guest_agent.0
    statedir = /var/run

The list of keys follows the command line options:

=============  ===========
Key             Key type
=============  ===========
daemon         boolean
method         string
path           string
logfile        string
pidfile        string
fsfreeze-hook  string
statedir       string
verbose        boolean
block-rpcs     string list
=============  ===========

See also
--------

:manpage:`qemu(1)`
