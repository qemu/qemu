Deprecated features
===================

In general features are intended to be supported indefinitely once
introduced into QEMU. In the event that a feature needs to be removed,
it will be listed in this section. The feature will remain functional
for 2 releases prior to actual removal. Deprecated features may also
generate warnings on the console when QEMU starts up, or if activated
via a monitor command, however, this is not a mandatory requirement.

Prior to the 2.10.0 release there was no official policy on how
long features would be deprecated prior to their removal, nor
any documented list of which features were deprecated. Thus
any features deprecated prior to 2.10.0 will be treated as if
they were first deprecated in the 2.10.0 release.

What follows is a list of all features currently marked as
deprecated.

System emulator command line arguments
--------------------------------------

``-machine enforce-config-section=on|off`` (since 3.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``enforce-config-section`` parameter is replaced by the
``-global migration.send-configuration={on|off}`` option.

``-no-kvm`` (since 1.3.0)
'''''''''''''''''''''''''

The ``-no-kvm`` argument is now a synonym for setting ``-accel tcg``.

``-usbdevice`` (since 2.10.0)
'''''''''''''''''''''''''''''

The ``-usbdevice DEV`` argument is now a synonym for setting
the ``-device usb-DEV`` argument instead. The deprecated syntax
would automatically enable USB support on the machine type.
If using the new syntax, USB support must be explicitly
enabled via the ``-machine usb=on`` argument.

``-drive file=json:{...{'driver':'file'}}`` (since 3.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''

The 'file' driver for drives is no longer appropriate for character or host
devices and will only accept regular files (S_IFREG). The correct driver
for these file types is 'host_cdrom' or 'host_device' as appropriate.

``-smp`` (invalid topologies) (since 3.1)
'''''''''''''''''''''''''''''''''''''''''

CPU topology properties should describe whole machine topology including
possible CPUs.

However, historically it was possible to start QEMU with an incorrect topology
where *n* <= *sockets* * *cores* * *threads* < *maxcpus*,
which could lead to an incorrect topology enumeration by the guest.
Support for invalid topologies will be removed, the user must ensure
topologies described with -smp include all possible cpus, i.e.
*sockets* * *cores* * *threads* = *maxcpus*.

``-vnc acl`` (since 4.0.0)
''''''''''''''''''''''''''

The ``acl`` option to the ``-vnc`` argument has been replaced
by the ``tls-authz`` and ``sasl-authz`` options.

``QEMU_AUDIO_`` environment variables and ``-audio-help`` (since 4.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``-audiodev`` argument is now the preferred way to specify audio
backend settings instead of environment variables.  To ease migration to
the new format, the ``-audiodev-help`` option can be used to convert
the current values of the environment variables to ``-audiodev`` options.

Creating sound card devices and vnc without ``audiodev=`` property (since 4.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

When not using the deprecated legacy audio config, each sound card
should specify an ``audiodev=`` property.  Additionally, when using
vnc, you should specify an ``audiodev=`` propery if you plan to
transmit audio through the VNC protocol.

Creating sound card devices using ``-soundhw`` (since 5.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Sound card devices should be created using ``-device`` instead.  The
names are the same for most devices.  The exceptions are ``hda`` which
needs two devices (``-device intel-hda -device hda-duplex``) and
``pcspk`` which can be activated using ``-machine
pcspk-audiodev=<name>``.

``-mon ...,control=readline,pretty=on|off`` (since 4.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``pretty=on|off`` switch has no effect for HMP monitors, but is
silently ignored. Using the switch with HMP monitors will become an
error in the future.

``-realtime`` (since 4.1)
'''''''''''''''''''''''''

The ``-realtime mlock=on|off`` argument has been replaced by the
``-overcommit mem-lock=on|off`` argument.

``-numa`` node (without memory specified) (since 4.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''

Splitting RAM by default between NUMA nodes has the same issues as ``mem``
parameter described above with the difference that the role of the user plays
QEMU using implicit generic or board specific splitting rule.
Use ``memdev`` with *memory-backend-ram* backend or ``mem`` (if
it's supported by used machine type) to define mapping explictly instead.

``-mem-path`` fallback to RAM (since 4.1)
'''''''''''''''''''''''''''''''''''''''''

Currently if guest RAM allocation from file pointed by ``mem-path``
fails, QEMU falls back to allocating from RAM, which might result
in unpredictable behavior since the backing file specified by the user
is ignored. In the future, users will be responsible for making sure
the backing storage specified with ``-mem-path`` can actually provide
the guest RAM configured with ``-m`` and QEMU will fail to start up if
RAM allocation is unsuccessful.

RISC-V ``-bios`` (since 5.1)
''''''''''''''''''''''''''''

QEMU 4.1 introduced support for the -bios option in QEMU for RISC-V for the
RISC-V virt machine and sifive_u machine. QEMU 4.1 had no changes to the
default behaviour to avoid breakages.

QEMU 5.1 changes the default behaviour from ``-bios none`` to ``-bios default``.

QEMU 5.1 has three options:
 1. ``-bios default`` - This is the current default behavior if no -bios option
      is included. This option will load the default OpenSBI firmware automatically.
      The firmware is included with the QEMU release and no user interaction is
      required. All a user needs to do is specify the kernel they want to boot
      with the -kernel option
 2. ``-bios none`` - QEMU will not automatically load any firmware. It is up
      to the user to load all the images they need.
 3. ``-bios <file>`` - Tells QEMU to load the specified file as the firmwrae.

``-tb-size`` option (since 5.0)
'''''''''''''''''''''''''''''''

QEMU 5.0 introduced an alternative syntax to specify the size of the translation
block cache, ``-accel tcg,tb-size=``.  The new syntax deprecates the
previously available ``-tb-size`` option.

``-show-cursor`` option (since 5.0)
'''''''''''''''''''''''''''''''''''

Use ``-display sdl,show-cursor=on`` or
 ``-display gtk,show-cursor=on`` instead.

``Configuring floppies with ``-global``
'''''''''''''''''''''''''''''''''''''''

Use ``-device floppy,...`` instead:
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

``-drive`` with bogus interface type
''''''''''''''''''''''''''''''''''''

Drives with interface types other than ``if=none`` are for onboard
devices.  It is possible to use drives the board doesn't pick up with
-device.  This usage is now deprecated.  Use ``if=none`` instead.


QEMU Machine Protocol (QMP) commands
------------------------------------

``change`` (since 2.5.0)
''''''''''''''''''''''''

Use ``blockdev-change-medium`` or ``change-vnc-password`` instead.

``blockdev-open-tray``, ``blockdev-close-tray`` argument ``device`` (since 2.8.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``eject`` argument ``device`` (since 2.8.0)
'''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``blockdev-change-medium`` argument ``device`` (since 2.8.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``block_set_io_throttle`` argument ``device`` (since 2.8.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``migrate_set_downtime`` and ``migrate_set_speed`` (since 2.8.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``migrate-set-parameters`` instead.

``query-named-block-nodes`` result ``encryption_key_missing`` (since 2.10.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Always false.

``query-block`` result ``inserted.encryption_key_missing`` (since 2.10.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Always false.

``blockdev-add`` empty string argument ``backing`` (since 2.10.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument value ``null`` instead.

``migrate-set-cache-size`` and ``query-migrate-cache-size`` (since 2.11.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``migrate-set-parameters`` and ``query-migrate-parameters`` instead.

``block-commit`` arguments ``base`` and ``top`` (since 3.1.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use arguments ``base-node`` and ``top-node`` instead.

``object-add`` option ``props`` (since 5.0)
'''''''''''''''''''''''''''''''''''''''''''

Specify the properties for the object as top-level arguments instead.

``query-named-block-nodes`` and ``query-block`` result dirty-bitmaps[i].status (since 4.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``status`` field of the ``BlockDirtyInfo`` structure, returned by
these commands is deprecated. Two new boolean fields, ``recording`` and
``busy`` effectively replace it.

``query-block`` result field ``dirty-bitmaps`` (Since 4.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``dirty-bitmaps`` field of the ``BlockInfo`` structure, returned by
the query-block command is itself now deprecated. The ``dirty-bitmaps``
field of the ``BlockDeviceInfo`` struct should be used instead, which is the
type of the ``inserted`` field in query-block replies, as well as the
type of array items in query-named-block-nodes.

Since the ``dirty-bitmaps`` field is optionally present in both the old and
new locations, clients must use introspection to learn where to anticipate
the field if/when it does appear in command output.

``query-cpus`` (since 2.12.0)
'''''''''''''''''''''''''''''

The ``query-cpus`` command is replaced by the ``query-cpus-fast`` command.

``query-cpus-fast`` ``arch`` output member (since 3.0.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``arch`` output member of the ``query-cpus-fast`` command is
replaced by the ``target`` output member.

``cpu-add`` (since 4.0)
'''''''''''''''''''''''

Use ``device_add`` for hotplugging vCPUs instead of ``cpu-add``.  See
documentation of ``query-hotpluggable-cpus`` for additional
details.

``query-events`` (since 4.0)
''''''''''''''''''''''''''''

The ``query-events`` command has been superseded by the more powerful
and accurate ``query-qmp-schema`` command.

chardev client socket with ``wait`` option (since 4.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''

Character devices creating sockets in client mode should not specify
the 'wait' field, which is only applicable to sockets in server mode

Human Monitor Protocol (HMP) commands
-------------------------------------

``cpu-add`` (since 4.0)
'''''''''''''''''''''''

Use ``device_add`` for hotplugging vCPUs instead of ``cpu-add``.  See
documentation of ``query-hotpluggable-cpus`` for additional details.

``acl_show``, ``acl_reset``, ``acl_policy``, ``acl_add``, ``acl_remove`` (since 4.0.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``acl_show``, ``acl_reset``, ``acl_policy``, ``acl_add``, and
``acl_remove`` commands are deprecated with no replacement. Authorization
for VNC should be performed using the pluggable QAuthZ objects.

System emulator CPUS
--------------------

``compat`` property of server class POWER CPUs (since 5.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``compat`` property used to set backwards compatibility modes for
the processor has been deprecated. The ``max-cpu-compat`` property of
the ``pseries`` machine type should be used instead.

KVM guest support on 32-bit Arm hosts (since 5.0)
'''''''''''''''''''''''''''''''''''''''''''''''''

The Linux kernel has dropped support for allowing 32-bit Arm systems
to host KVM guests as of the 5.7 kernel. Accordingly, QEMU is deprecating
its support for this configuration and will remove it in a future version.
Running 32-bit guests on a 64-bit Arm host remains supported.

System emulator devices
-----------------------

``ide-drive`` (since 4.2)
'''''''''''''''''''''''''

The 'ide-drive' device is deprecated. Users should use 'ide-hd' or
'ide-cd' as appropriate to get an IDE hard disk or CD-ROM as needed.

``scsi-disk`` (since 4.2)
'''''''''''''''''''''''''

The 'scsi-disk' device is deprecated. Users should use 'scsi-hd' or
'scsi-cd' as appropriate to get a SCSI hard disk or CD-ROM as needed.

System emulator machines
------------------------

mips ``r4k`` platform (since 5.0)
'''''''''''''''''''''''''''''''''

This machine type is very old and unmaintained. Users should use the ``malta``
machine type instead.

mips ``fulong2e`` machine (since 5.1)
'''''''''''''''''''''''''''''''''''''

This machine has been renamed ``fuloong2e``.

``pc-1.0``, ``pc-1.1``, ``pc-1.2`` and ``pc-1.3`` (since 5.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

These machine types are very old and likely can not be used for live migration
from old QEMU versions anymore. A newer machine type should be used instead.

Device options
--------------

Emulated device options
'''''''''''''''''''''''

``-device virtio-blk,scsi=on|off`` (since 5.0.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The virtio-blk SCSI passthrough feature is a legacy VIRTIO feature.  VIRTIO 1.0
and later do not support it because the virtio-scsi device was introduced for
full SCSI support.  Use virtio-scsi instead when SCSI passthrough is required.

Note this also applies to ``-device virtio-blk-pci,scsi=on|off``, which is an
alias.

Block device options
''''''''''''''''''''

``"backing": ""`` (since 2.12.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In order to prevent QEMU from automatically opening an image's backing
chain, use ``"backing": null`` instead.

``rbd`` keyvalue pair encoded filenames: ``""`` (since 3.1.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Options for ``rbd`` should be specified according to its runtime options,
like other block drivers.  Legacy parsing of keyvalue pair encoded
filenames is useful to open images with the old format for backing files;
These image files should be updated to use the current format.

Example of legacy encoding::

  json:{"file.driver":"rbd", "file.filename":"rbd:rbd/name"}

The above, converted to the current supported format::

  json:{"file.driver":"rbd", "file.pool":"rbd", "file.image":"name"}

linux-user mode CPUs
--------------------

``tilegx`` CPUs (since 5.1.0)
'''''''''''''''''''''''''''''

The ``tilegx`` guest CPU support (which was only implemented in
linux-user mode) is deprecated and will be removed in a future version
of QEMU. Support for this CPU was removed from the upstream Linux
kernel in 2018, and has also been dropped from glibc.

Related binaries
----------------

qemu-img amend to adjust backing file (since 5.1)
'''''''''''''''''''''''''''''''''''''''''''''''''

The use of ``qemu-img amend`` to modify the name or format of a qcow2
backing image is deprecated; this functionality was never fully
documented or tested, and interferes with other amend operations that
need access to the original backing image (such as deciding whether a
v3 zero cluster may be left unallocated when converting to a v2
image).  Rather, any changes to the backing chain should be performed
with ``qemu-img rebase -u`` either before or after the remaining
changes being performed by amend, as appropriate.

qemu-img backing file without format (since 5.1)
''''''''''''''''''''''''''''''''''''''''''''''''

The use of ``qemu-img create``, ``qemu-img rebase``, or ``qemu-img
convert`` to create or modify an image that depends on a backing file
now recommends that an explicit backing format be provided.  This is
for safety: if QEMU probes a different format than what you thought,
the data presented to the guest will be corrupt; similarly, presenting
a raw image to a guest allows a potential security exploit if a future
probe sees a non-raw image based on guest writes.

To avoid the warning message, or even future refusal to create an
unsafe image, you must pass ``-o backing_fmt=`` (or the shorthand
``-F`` during create) to specify the intended backing format.  You may
use ``qemu-img rebase -u`` to retroactively add a backing format to an
existing image.  However, be aware that there are already potential
security risks to blindly using ``qemu-img info`` to probe the format
of an untrusted backing image, when deciding what format to add into
an existing image.

Backwards compatibility
-----------------------

Runnability guarantee of CPU models (since 4.1.0)
'''''''''''''''''''''''''''''''''''''''''''''''''

Previous versions of QEMU never changed existing CPU models in
ways that introduced additional host software or hardware
requirements to the VM.  This allowed management software to
safely change the machine type of an existing VM without
introducing new requirements ("runnability guarantee").  This
prevented CPU models from being updated to include CPU
vulnerability mitigations, leaving guests vulnerable in the
default configuration.

The CPU model runnability guarantee won't apply anymore to
existing CPU models.  Management software that needs runnability
guarantees must resolve the CPU model aliases using te
``alias-of`` field returned by the ``query-cpu-definitions`` QMP
command.

While those guarantees are kept, the return value of
``query-cpu-definitions`` will have existing CPU model aliases
point to a version that doesn't break runnability guarantees
(specifically, version 1 of those CPU models).  In future QEMU
versions, aliases will point to newer CPU model versions
depending on the machine type, so management software must
resolve CPU model aliases before starting a virtual machine.


Recently removed features
=========================

What follows is a record of recently removed, formerly deprecated
features that serves as a record for users who have encountered
trouble after a recent upgrade.

System emulator command line arguments
--------------------------------------

``-net ...,name=``\ *name* (removed in 5.1)
'''''''''''''''''''''''''''''''''''''''''''

The ``name`` parameter of the ``-net`` option was a synonym
for the ``id`` parameter, which should now be used instead.

QEMU Machine Protocol (QMP) commands
------------------------------------

``block-dirty-bitmap-add`` "autoload" parameter (since 4.2.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The "autoload" parameter has been ignored since 2.12.0. All bitmaps
are automatically loaded from qcow2 images.

Human Monitor Protocol (HMP) commands
-------------------------------------

The ``hub_id`` parameter of ``hostfwd_add`` / ``hostfwd_remove`` (removed in 5.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``[hub_id name]`` parameter tuple of the 'hostfwd_add' and
'hostfwd_remove' HMP commands has been replaced by ``netdev_id``.

Guest Emulator ISAs
-------------------

RISC-V ISA privledge specification version 1.09.1 (removed in 5.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The RISC-V ISA privledge specification version 1.09.1 has been removed.
QEMU supports both the newer version 1.10.0 and the ratified version 1.11.0, these
should be used instead of the 1.09.1 version.

System emulator CPUS
--------------------

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

Block devices
-------------

VXHS backend (removed in 5.1)
'''''''''''''''''''''''''''''

The VXHS code does not compile since v2.12.0. It was removed in 5.1.
