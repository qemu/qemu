===============================
Persistent reservation managers
===============================

SCSI persistent reservations allow restricting access to block devices
to specific initiators in a shared storage setup.  When implementing
clustering of virtual machines, it is a common requirement for virtual
machines to send persistent reservation SCSI commands.  However,
the operating system restricts sending these commands to unprivileged
programs because incorrect usage can disrupt regular operation of the
storage fabric.

For this reason, QEMU's SCSI passthrough devices, ``scsi-block``
and ``scsi-generic`` (both are only available on Linux) can delegate
implementation of persistent reservations to a separate object,
the "persistent reservation manager".  Only PERSISTENT RESERVE OUT and
PERSISTENT RESERVE IN commands are passed to the persistent reservation
manager object; other commands are processed by QEMU as usual.

-----------------------------------------
Defining a persistent reservation manager
-----------------------------------------

A persistent reservation manager is an instance of a subclass of the
"pr-manager" QOM class.

Right now only one subclass is defined, ``pr-manager-helper``, which
forwards the commands to an external privileged helper program
over Unix sockets.  The helper program only allows sending persistent
reservation commands to devices for which QEMU has a file descriptor,
so that QEMU will not be able to effect persistent reservations
unless it has access to both the socket and the device.

``pr-manager-helper`` has a single string property, ``path``, which
accepts the path to the helper program's Unix socket.  For example,
the following command line defines a ``pr-manager-helper`` object and
attaches it to a SCSI passthrough device::

      $ qemu-system-x86_64
          -device virtio-scsi \
          -object pr-manager-helper,id=helper0,path=/var/run/qemu-pr-helper.sock
          -drive if=none,id=hd,driver=raw,file.filename=/dev/sdb,file.pr-manager=helper0
          -device scsi-block,drive=hd

Alternatively, using ``-blockdev``::

      $ qemu-system-x86_64
          -device virtio-scsi \
          -object pr-manager-helper,id=helper0,path=/var/run/qemu-pr-helper.sock
          -blockdev node-name=hd,driver=raw,file.driver=host_device,file.filename=/dev/sdb,file.pr-manager=helper0
          -device scsi-block,drive=hd

You will also need to ensure that the helper program
:command:`qemu-pr-helper` is running, and that it has been
set up to use the same socket filename as your QEMU commandline
specifies. See the qemu-pr-helper documentation or manpage for
further details.

---------------------------------------------
Multipath devices and persistent reservations
---------------------------------------------

Proper support of persistent reservation for multipath devices requires
communication with the multipath daemon, so that the reservation is
registered and applied when a path is newly discovered or becomes online
again.  :command:`qemu-pr-helper` can do this if the ``libmpathpersist``
library was available on the system at build time.

As of August 2017, a reservation key must be specified in ``multipath.conf``
for ``multipathd`` to check for persistent reservation for newly
discovered paths or reinstated paths.  The attribute can be added
to the ``defaults`` section or the ``multipaths`` section; for example::

    multipaths {
        multipath {
            wwid   XXXXXXXXXXXXXXXX
            alias      yellow
            reservation_key  0x123abc
        }
    }

Linking :program:`qemu-pr-helper` to ``libmpathpersist`` does not impede
its usage on regular SCSI devices.
