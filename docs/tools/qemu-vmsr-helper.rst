==================================
QEMU virtual RAPL MSR helper
==================================

Synopsis
--------

**qemu-vmsr-helper** [*OPTION*]

Description
-----------

Implements the virtual RAPL MSR helper for QEMU.

Accessing the RAPL (Running Average Power Limit) MSR enables the RAPL powercap
driver to advertise and monitor the power consumption or accumulated energy
consumption of different power domains, such as CPU packages, DRAM, and other
components when available.

However those registers are accessible under privileged access (CAP_SYS_RAWIO).
QEMU can use an external helper to access those privileged registers.

:program:`qemu-vmsr-helper` is that external helper; it creates a listener
socket which will accept incoming connections for communication with QEMU.

If you want to run VMs in a setup like this, this helper should be started as a
system service, and you should read the QEMU manual section on "RAPL MSR
support" to find out how to configure QEMU to connect to the socket created by
:program:`qemu-vmsr-helper`.

After connecting to the socket, :program:`qemu-vmsr-helper` can
optionally drop root privileges, except for those capabilities that
are needed for its operation.

:program:`qemu-vmsr-helper` can also use the systemd socket activation
protocol.  In this case, the systemd socket unit should specify a
Unix stream socket, like this::

    [Socket]
    ListenStream=/var/run/qemu-vmsr-helper.sock

Options
-------

.. program:: qemu-vmsr-helper

.. option:: -d, --daemon

  run in the background (and create a PID file)

.. option:: -q, --quiet

  decrease verbosity

.. option:: -v, --verbose

  increase verbosity

.. option:: -f, --pidfile=PATH

  PID file when running as a daemon. By default the PID file
  is created in the system runtime state directory, for example
  :file:`/var/run/qemu-vmsr-helper.pid`.

.. option:: -k, --socket=PATH

  path to the socket. By default the socket is created in
  the system runtime state directory, for example
  :file:`/var/run/qemu-vmsr-helper.sock`.

.. option:: -T, --trace [[enable=]PATTERN][,events=FILE][,file=FILE]

  .. include:: ../qemu-option-trace.rst.inc

.. option:: -u, --user=USER

  user to drop privileges to

.. option:: -g, --group=GROUP

  group to drop privileges to

.. option:: -h, --help

  Display a help message and exit.

.. option:: -V, --version

  Display version information and exit.
