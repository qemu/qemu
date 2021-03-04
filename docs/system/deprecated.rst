Deprecated features
===================

In general features are intended to be supported indefinitely once
introduced into QEMU. In the event that a feature needs to be removed,
it will be listed in this section. The feature will remain functional for the
release in which it was deprecated and one further release. After these two
releases, the feature is liable to be removed. Deprecated features may also
generate warnings on the console when QEMU starts up, or if activated via a
monitor command, however, this is not a mandatory requirement.

Prior to the 2.10.0 release there was no official policy on how
long features would be deprecated prior to their removal, nor
any documented list of which features were deprecated. Thus
any features deprecated prior to 2.10.0 will be treated as if
they were first deprecated in the 2.10.0 release.

What follows is a list of all features currently marked as
deprecated.

System emulator command line arguments
--------------------------------------

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
vnc, you should specify an ``audiodev=`` property if you plan to
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

Short-form boolean options (since 6.0)
''''''''''''''''''''''''''''''''''''''

Boolean options such as ``share=on``/``share=off`` could be written
in short form as ``share`` and ``noshare``.  This is now deprecated
and will cause a warning.

``--enable-fips`` (since 6.0)
'''''''''''''''''''''''''''''

This option restricts usage of certain cryptographic algorithms when
the host is operating in FIPS mode.

If FIPS compliance is required, QEMU should be built with the ``libgcrypt``
library enabled as a cryptography provider.

Neither the ``nettle`` library, or the built-in cryptography provider are
supported on FIPS enabled hosts.

``-writeconfig`` (since 6.0)
'''''''''''''''''''''''''''''

The ``-writeconfig`` option is not able to serialize the entire contents
of the QEMU command line.  It is thus considered a failed experiment
and deprecated, with no current replacement.

QEMU Machine Protocol (QMP) commands
------------------------------------

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

``query-events`` (since 4.0)
''''''''''''''''''''''''''''

The ``query-events`` command has been superseded by the more powerful
and accurate ``query-qmp-schema`` command.

chardev client socket with ``wait`` option (since 4.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''

Character devices creating sockets in client mode should not specify
the 'wait' field, which is only applicable to sockets in server mode

``nbd-server-add`` and ``nbd-server-remove`` (since 5.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use the more generic commands ``block-export-add`` and ``block-export-del``
instead.  As part of this deprecation, where ``nbd-server-add`` used a
single ``bitmap``, the new ``block-export-add`` uses a list of ``bitmaps``.

Human Monitor Protocol (HMP) commands
-------------------------------------

``acl_show``, ``acl_reset``, ``acl_policy``, ``acl_add``, ``acl_remove`` (since 4.0.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``acl_show``, ``acl_reset``, ``acl_policy``, ``acl_add``, and
``acl_remove`` commands are deprecated with no replacement. Authorization
for VNC should be performed using the pluggable QAuthZ objects.

System emulator CPUS
--------------------

``moxie`` CPU (since 5.2.0)
'''''''''''''''''''''''''''

The ``moxie`` guest CPU support is deprecated and will be removed in
a future version of QEMU. It's unclear whether anybody is still using
CPU emulation in QEMU, and there are no test images available to make
sure that the code is still working.

``lm32`` CPUs (since 5.2.0)
'''''''''''''''''''''''''''

The ``lm32`` guest CPU support is deprecated and will be removed in
a future version of QEMU. The only public user of this architecture
was the milkymist project, which has been dead for years; there was
never an upstream Linux port.

``unicore32`` CPUs (since 5.2.0)
''''''''''''''''''''''''''''''''

The ``unicore32`` guest CPU support is deprecated and will be removed in
a future version of QEMU. Support for this CPU was removed from the
upstream Linux kernel, and there is no available upstream toolchain
to build binaries for it.

``Icelake-Client`` CPU Model (since 5.2.0)
''''''''''''''''''''''''''''''''''''''''''

``Icelake-Client`` CPU Models are deprecated. Use ``Icelake-Server`` CPU
Models instead.

MIPS ``I7200`` CPU Model (since 5.2)
''''''''''''''''''''''''''''''''''''

The ``I7200`` guest CPU relies on the nanoMIPS ISA, which is deprecated
(the ISA has never been upstreamed to a compiler toolchain). Therefore
this CPU is also deprecated.

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

Raspberry Pi ``raspi2`` and ``raspi3`` machines (since 5.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The Raspberry Pi machines come in various models (A, A+, B, B+). To be able
to distinguish which model QEMU is implementing, the ``raspi2`` and ``raspi3``
machines have been renamed ``raspi2b`` and ``raspi3b``.

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

``sheepdog`` driver (since 5.2.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``sheepdog`` block device driver is deprecated. The corresponding upstream
server project is no longer actively maintained. Users are recommended to switch
to an alternative distributed block device driver such as RBD. The
``qemu-img convert`` command can be used to liberate existing data by moving
it out of sheepdog volumes into an alternative storage backend.

linux-user mode CPUs
--------------------

``tilegx`` CPUs (since 5.1.0)
'''''''''''''''''''''''''''''

The ``tilegx`` guest CPU support (which was only implemented in
linux-user mode) is deprecated and will be removed in a future version
of QEMU. Support for this CPU was removed from the upstream Linux
kernel in 2018, and has also been dropped from glibc.

``ppc64abi32`` CPUs (since 5.2.0)
'''''''''''''''''''''''''''''''''

The ``ppc64abi32`` architecture has a number of issues which regularly
trip up our CI testing and is suspected to be quite broken. For that
reason the maintainers strongly suspect no one actually uses it.

MIPS ``I7200`` CPU (since 5.2)
''''''''''''''''''''''''''''''

The ``I7200`` guest CPU relies on the nanoMIPS ISA, which is deprecated
(the ISA has never been upstreamed to a compiler toolchain). Therefore
this CPU is also deprecated.

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
guarantees must resolve the CPU model aliases using the
``alias-of`` field returned by the ``query-cpu-definitions`` QMP
command.

While those guarantees are kept, the return value of
``query-cpu-definitions`` will have existing CPU model aliases
point to a version that doesn't break runnability guarantees
(specifically, version 1 of those CPU models).  In future QEMU
versions, aliases will point to newer CPU model versions
depending on the machine type, so management software must
resolve CPU model aliases before starting a virtual machine.

Guest Emulator ISAs
-------------------

nanoMIPS ISA
''''''''''''

The ``nanoMIPS`` ISA has never been upstreamed to any compiler toolchain.
As it is hard to generate binaries for it, declare it deprecated.
