
Removed features
================

What follows is a record of recently removed, formerly deprecated
features that serves as a record for users who have encountered
trouble after a recent upgrade.

System emulator command line arguments
--------------------------------------

``-net ...,name=``\ *name* (removed in 5.1)
'''''''''''''''''''''''''''''''''''''''''''

The ``name`` parameter of the ``-net`` option was a synonym
for the ``id`` parameter, which should now be used instead.

``-no-kvm`` (removed in 5.2)
''''''''''''''''''''''''''''

The ``-no-kvm`` argument was a synonym for setting ``-machine accel=tcg``.

``-realtime`` (removed in 6.0)
''''''''''''''''''''''''''''''

The ``-realtime mlock=on|off`` argument has been replaced by the
``-overcommit mem-lock=on|off`` argument.

``-show-cursor`` option (removed in 6.0)
''''''''''''''''''''''''''''''''''''''''

Use ``-display sdl,show-cursor=on``, ``-display gtk,show-cursor=on``
or ``-display default,show-cursor=on`` instead.

``-tb-size`` option (removed in 6.0)
''''''''''''''''''''''''''''''''''''

QEMU 5.0 introduced an alternative syntax to specify the size of the translation
block cache, ``-accel tcg,tb-size=``.

``-usbdevice audio`` (removed in 6.0)
'''''''''''''''''''''''''''''''''''''

This option lacked the possibility to specify an audio backend device.
Use ``-device usb-audio`` now instead (and specify a corresponding USB
host controller or ``-usb`` if necessary).

``-vnc acl`` (removed in 6.0)
'''''''''''''''''''''''''''''

The ``acl`` option to the ``-vnc`` argument has been replaced
by the ``tls-authz`` and ``sasl-authz`` options.

``-mon ...,control=readline,pretty=on|off`` (removed in 6.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``pretty=on|off`` switch has no effect for HMP monitors and
its use is rejected.

``-drive file=json:{...{'driver':'file'}}`` (removed 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The 'file' driver for drives is no longer appropriate for character or host
devices and will only accept regular files (S_IFREG). The correct driver
for these file types is 'host_cdrom' or 'host_device' as appropriate.

Floppy controllers' drive properties (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``-device floppy,...`` instead.  When configuring onboard floppy
controllers
::

    -global isa-fdc.driveA=...
    -global sysbus-fdc.driveA=...
    -global SUNW,fdtwo.drive=...

become
::

    -device floppy,unit=0,drive=...

and
::

    -global isa-fdc.driveB=...
    -global sysbus-fdc.driveB=...

become
::

    -device floppy,unit=1,drive=...

When plugging in a floppy controller
::

    -device isa-fdc,...,driveA=...

becomes
::

    -device isa-fdc,...
    -device floppy,unit=0,drive=...

and
::

    -device isa-fdc,...,driveB=...

becomes
::

    -device isa-fdc,...
    -device floppy,unit=1,drive=...

``-drive`` with bogus interface type (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''

Drives with interface types other than ``if=none`` are for onboard
devices.  Drives the board doesn't pick up can no longer be used with
-device.  Use ``if=none`` instead.

``-usbdevice ccid`` (removed in 6.0)
'''''''''''''''''''''''''''''''''''''

This option was undocumented and not used in the field.
Use `-device usb-ccid`` instead.


QEMU Machine Protocol (QMP) commands
------------------------------------

``block-dirty-bitmap-add`` "autoload" parameter (removed in 4.2.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The "autoload" parameter has been ignored since 2.12.0. All bitmaps
are automatically loaded from qcow2 images.

``cpu-add`` (removed in 5.2)
''''''''''''''''''''''''''''

Use ``device_add`` for hotplugging vCPUs instead of ``cpu-add``.  See
documentation of ``query-hotpluggable-cpus`` for additional details.

``change`` (removed in 6.0)
'''''''''''''''''''''''''''

Use ``blockdev-change-medium`` or ``change-vnc-password`` instead.

``query-events`` (removed in 6.0)
'''''''''''''''''''''''''''''''''

The ``query-events`` command has been superseded by the more powerful
and accurate ``query-qmp-schema`` command.

``migrate_set_cache_size`` and ``query-migrate-cache-size`` (removed in 6.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``migrate_set_parameter`` and ``info migrate_parameters`` instead.

``migrate_set_downtime`` and ``migrate_set_speed`` (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``migrate_set_parameter`` instead.

``query-cpus`` (removed in 6.0)
'''''''''''''''''''''''''''''''

The ``query-cpus`` command is replaced by the ``query-cpus-fast`` command.

``query-cpus-fast`` ``arch`` output member (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``arch`` output member of the ``query-cpus-fast`` command is
replaced by the ``target`` output member.

chardev client socket with ``wait`` option (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Character devices creating sockets in client mode should not specify
the 'wait' field, which is only applicable to sockets in server mode

``query-named-block-nodes`` result ``encryption_key_missing`` (removed in 6.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Removed with no replacement.

``query-block`` result ``inserted.encryption_key_missing`` (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Removed with no replacement.

``query-named-block-nodes`` and ``query-block`` result dirty-bitmaps[i].status (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``status`` field of the ``BlockDirtyInfo`` structure, returned by
these commands is removed. Two new boolean fields, ``recording`` and
``busy`` effectively replace it.

``query-block`` result field ``dirty-bitmaps`` (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``dirty-bitmaps`` field of the ``BlockInfo`` structure, returned by
the query-block command is itself now removed. The ``dirty-bitmaps``
field of the ``BlockDeviceInfo`` struct should be used instead, which is the
type of the ``inserted`` field in query-block replies, as well as the
type of array items in query-named-block-nodes.

``object-add`` option ``props`` (removed in 6.0)
''''''''''''''''''''''''''''''''''''''''''''''''

Specify the properties for the object as top-level arguments instead.

Human Monitor Protocol (HMP) commands
-------------------------------------

The ``hub_id`` parameter of ``hostfwd_add`` / ``hostfwd_remove`` (removed in 5.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``[hub_id name]`` parameter tuple of the 'hostfwd_add' and
'hostfwd_remove' HMP commands has been replaced by ``netdev_id``.

``cpu-add`` (removed in 5.2)
''''''''''''''''''''''''''''

Use ``device_add`` for hotplugging vCPUs instead of ``cpu-add``.  See
documentation of ``query-hotpluggable-cpus`` for additional details.

``change vnc TARGET`` (removed in 6.0)
''''''''''''''''''''''''''''''''''''''

No replacement.  The ``change vnc password`` and ``change DEVICE MEDIUM``
commands are not affected.

``acl_show``, ``acl_reset``, ``acl_policy``, ``acl_add``, ``acl_remove`` (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``acl_show``, ``acl_reset``, ``acl_policy``, ``acl_add``, and
``acl_remove`` commands were removed with no replacement. Authorization
for VNC should be performed using the pluggable QAuthZ objects.

``migrate-set-cache-size`` and ``info migrate-cache-size`` (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``migrate-set-parameters`` and ``info migrate-parameters`` instead.

``migrate_set_downtime`` and ``migrate_set_speed`` (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``migrate-set-parameters`` instead.

Guest Emulator ISAs
-------------------

RISC-V ISA privilege specification version 1.09.1 (removed in 5.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The RISC-V ISA privilege specification version 1.09.1 has been removed.
QEMU supports both the newer version 1.10.0 and the ratified version 1.11.0, these
should be used instead of the 1.09.1 version.

System emulator CPUS
--------------------

KVM guest support on 32-bit Arm hosts (removed in 5.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''

The Linux kernel has dropped support for allowing 32-bit Arm systems
to host KVM guests as of the 5.7 kernel. Accordingly, QEMU is deprecating
its support for this configuration and will remove it in a future version.
Running 32-bit guests on a 64-bit Arm host remains supported.

RISC-V ISA Specific CPUs (removed in 5.1)
'''''''''''''''''''''''''''''''''''''''''

The RISC-V cpus with the ISA version in the CPU name have been removed. The
four CPUs are: ``rv32gcsu-v1.9.1``, ``rv32gcsu-v1.10.0``, ``rv64gcsu-v1.9.1`` and
``rv64gcsu-v1.10.0``. Instead the version can be specified via the CPU ``priv_spec``
option when using the ``rv32`` or ``rv64`` CPUs.

RISC-V no MMU CPUs (removed in 5.1)
'''''''''''''''''''''''''''''''''''

The RISC-V no MMU cpus have been removed. The two CPUs: ``rv32imacu-nommu`` and
``rv64imacu-nommu`` can no longer be used. Instead the MMU status can be specified
via the CPU ``mmu`` option when using the ``rv32`` or ``rv64`` CPUs.

System emulator machines
------------------------

``spike_v1.9.1`` and ``spike_v1.10`` (removed in 5.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''

The version specific Spike machines have been removed in favour of the
generic ``spike`` machine. If you need to specify an older version of the RISC-V
spec you can use the ``-cpu rv64gcsu,priv_spec=v1.10.0`` command line argument.

mips ``r4k`` platform (removed in 5.2)
''''''''''''''''''''''''''''''''''''''

This machine type was very old and unmaintained. Users should use the ``malta``
machine type instead.

mips ``fulong2e`` machine alias (removed in 6.0)
''''''''''''''''''''''''''''''''''''''''''''''''

This machine has been renamed ``fuloong2e``.

``pc-1.0``, ``pc-1.1``, ``pc-1.2`` and ``pc-1.3`` (removed in 6.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

These machine types were very old and likely could not be used for live
migration from old QEMU versions anymore. Use a newer machine type instead.


linux-user mode CPUs
--------------------

``tilegx`` CPUs (removed in 6.0)
''''''''''''''''''''''''''''''''

The ``tilegx`` guest CPU support has been removed without replacement. It was
only implemented in linux-user mode, but support for this CPU was removed from
the upstream Linux kernel in 2018, and it has also been dropped from glibc, so
there is no new Linux development taking place with this architecture. For
running the old binaries, you can use older versions of QEMU.

System emulator devices
-----------------------

``ide-drive`` (removed in 6.0)
''''''''''''''''''''''''''''''

The 'ide-drive' device has been removed. Users should use 'ide-hd' or
'ide-cd' as appropriate to get an IDE hard disk or CD-ROM as needed.

``scsi-disk`` (removed in 6.0)
''''''''''''''''''''''''''''''

The 'scsi-disk' device has been removed. Users should use 'scsi-hd' or
'scsi-cd' as appropriate to get a SCSI hard disk or CD-ROM as needed.

Related binaries
----------------

``qemu-nbd --partition`` (removed in 5.0)
'''''''''''''''''''''''''''''''''''''''''

The ``qemu-nbd --partition $digit`` code (also spelled ``-P``)
could only handle MBR partitions, and never correctly handled logical
partitions beyond partition 5.  Exporting a partition can still be
done by utilizing the ``--image-opts`` option with a raw blockdev
using the ``offset`` and ``size`` parameters layered on top of
any other existing blockdev. For example, if partition 1 is 100MiB
long starting at 1MiB, the old command::

  qemu-nbd -t -P 1 -f qcow2 file.qcow2

can be rewritten as::

  qemu-nbd -t --image-opts driver=raw,offset=1M,size=100M,file.driver=qcow2,file.file.driver=file,file.file.filename=file.qcow2

``qemu-img convert -n -o`` (removed in 5.1)
'''''''''''''''''''''''''''''''''''''''''''

All options specified in ``-o`` are image creation options, so
they are now rejected when used with ``-n`` to skip image creation.


``qemu-img create -b bad file $size`` (removed in 5.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''

When creating an image with a backing file that could not be opened,
``qemu-img create`` used to issue a warning about the failure but
proceed with the image creation if an explicit size was provided.
However, as the ``-u`` option exists for this purpose, it is safer to
enforce that any failure to open the backing image (including if the
backing file is missing or an incorrect format was specified) is an
error when ``-u`` is not used.

Command line options
--------------------

``-smp`` (invalid topologies) (removed 5.2)
'''''''''''''''''''''''''''''''''''''''''''

CPU topology properties should describe whole machine topology including
possible CPUs.

However, historically it was possible to start QEMU with an incorrect topology
where *n* <= *sockets* * *cores* * *threads* < *maxcpus*,
which could lead to an incorrect topology enumeration by the guest.
Support for invalid topologies is removed, the user must ensure
topologies described with -smp include all possible cpus, i.e.
*sockets* * *cores* * *threads* = *maxcpus*.

``-numa`` node (without memory specified) (removed 5.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''

Splitting RAM by default between NUMA nodes had the same issues as ``mem``
parameter with the difference that the role of the user plays QEMU using
implicit generic or board specific splitting rule.
Use ``memdev`` with *memory-backend-ram* backend or ``mem`` (if
it's supported by used machine type) to define mapping explicitly instead.
Users of existing VMs, wishing to preserve the same RAM distribution, should
configure it explicitly using ``-numa node,memdev`` options. Current RAM
distribution can be retrieved using HMP command ``info numa`` and if separate
memory devices (pc|nv-dimm) are present use ``info memory-device`` and subtract
device memory from output of ``info numa``.

``-numa node,mem=``\ *size* (removed in 5.1)
''''''''''''''''''''''''''''''''''''''''''''

The parameter ``mem`` of ``-numa node`` was used to assign a part of
guest RAM to a NUMA node. But when using it, it's impossible to manage a specified
RAM chunk on the host side (like bind it to a host node, setting bind policy, ...),
so the guest ends up with the fake NUMA configuration with suboptiomal performance.
However since 2014 there is an alternative way to assign RAM to a NUMA node
using parameter ``memdev``, which does the same as ``mem`` and adds
means to actually manage node RAM on the host side. Use parameter ``memdev``
with *memory-backend-ram* backend as replacement for parameter ``mem``
to achieve the same fake NUMA effect or a properly configured
*memory-backend-file* backend to actually benefit from NUMA configuration.
New machine versions (since 5.1) will not accept the option but it will still
work with old machine types. User can check the QAPI schema to see if the legacy
option is supported by looking at MachineInfo::numa-mem-supported property.

``-mem-path`` fallback to RAM (removed in 5.0)
''''''''''''''''''''''''''''''''''''''''''''''

If guest RAM allocation from file pointed by ``mem-path`` failed,
QEMU was falling back to allocating from RAM, which might have resulted
in unpredictable behavior since the backing file specified by the user
as ignored. Currently, users are responsible for making sure the backing storage
specified with ``-mem-path`` can actually provide the guest RAM configured with
``-m`` and QEMU fails to start up if RAM allocation is unsuccessful.

``-smp`` (invalid topologies) (removed 5.2)
'''''''''''''''''''''''''''''''''''''''''''

CPU topology properties should describe whole machine topology including
possible CPUs.

However, historically it was possible to start QEMU with an incorrect topology
where *n* <= *sockets* * *cores* * *threads* < *maxcpus*,
which could lead to an incorrect topology enumeration by the guest.
Support for invalid topologies is removed, the user must ensure
topologies described with -smp include all possible cpus, i.e.
*sockets* * *cores* * *threads* = *maxcpus*.

``-machine enforce-config-section=on|off`` (removed 5.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``enforce-config-section`` property was replaced by the
``-global migration.send-configuration={on|off}`` option.

Block devices
-------------

VXHS backend (removed in 5.1)
'''''''''''''''''''''''''''''

The VXHS code did not compile since v2.12.0. It was removed in 5.1.
