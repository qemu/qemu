======================================
Persistent reservation managers
======================================

SCSI persistent Reservations allow restricting access to block devices
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
