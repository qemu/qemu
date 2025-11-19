
Removed features
================

What follows is a record of recently removed, formerly deprecated
features that serves as a record for users who have encountered
trouble after a recent upgrade.

System emulator command line arguments
--------------------------------------

``-hdachs`` (removed in 2.12)
'''''''''''''''''''''''''''''

The geometry defined by ``-hdachs c,h,s,t`` should now be specified via
``-device ide-hd,drive=dr,cyls=c,heads=h,secs=s,bios-chs-trans=t``
(together with ``-drive if=none,id=dr,...``).

``-net channel`` (removed in 2.12)
''''''''''''''''''''''''''''''''''

This option has been replaced by ``-net user,guestfwd=...``.

``-net dump`` (removed in 2.12)
'''''''''''''''''''''''''''''''

``-net dump[,vlan=n][,file=filename][,len=maxlen]`` has been replaced by
``-object filter-dump,id=id,netdev=dev[,file=filename][,maxlen=maxlen]``.
Note that the new syntax works with netdev IDs instead of the old "vlan" hubs.

``-no-kvm-pit`` (removed in 2.12)
'''''''''''''''''''''''''''''''''

This was just a dummy option that has been ignored, since the in-kernel PIT
cannot be disabled separately from the irqchip anymore. A similar effect
(which also disables the KVM IOAPIC) can be obtained with
``-M kernel_irqchip=split``.

``-tdf`` (removed in 2.12)
''''''''''''''''''''''''''

There is no replacement, the ``-tdf`` option has just been ignored since the
behaviour that could be changed by this option in qemu-kvm is now the default
when using the KVM PIT. It still can be requested explicitly using
``-global kvm-pit.lost_tick_policy=delay``.

``-drive secs=s``, ``-drive heads=h`` & ``-drive cyls=c`` (removed in 3.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The drive geometry should now be specified via
``-device ...,drive=dr,cyls=c,heads=h,secs=s`` (together with
``-drive if=none,id=dr,...``).

``-drive serial=``, ``-drive trans=`` & ``-drive addr=`` (removed in 3.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``-device ...,drive=dr,serial=r,bios-chs-trans=t,addr=a`` instead
(together with ``-drive if=none,id=dr,...``).

``-net ...,vlan=x`` (removed in 3.0)
''''''''''''''''''''''''''''''''''''

The term "vlan" was very confusing for most users in this context (it's about
specifying a hub ID, not about IEEE 802.1Q or something similar), so this
has been removed. To connect one NIC frontend with a network backend, either
use ``-nic ...`` (e.g. for on-board NICs) or use ``-netdev ...,id=n`` together
with ``-device ...,netdev=n`` (for full control over pluggable NICs). To
connect multiple NICs or network backends via a hub device (which is what
vlan did), use ``-nic hubport,hubid=x,...`` or
``-netdev hubport,id=n,hubid=x,...`` (with ``-device ...,netdev=n``) instead.

``-no-kvm-irqchip`` (removed in 3.0)
''''''''''''''''''''''''''''''''''''

Use ``-machine kernel_irqchip=off`` instead.

``-no-kvm-pit-reinjection`` (removed in 3.0)
''''''''''''''''''''''''''''''''''''''''''''

Use ``-global kvm-pit.lost_tick_policy=discard`` instead.

``-balloon`` (removed in 3.1)
'''''''''''''''''''''''''''''

The ``-balloon virtio`` option has been replaced by ``-device virtio-balloon``.
The ``-balloon none`` option was a no-op and has no replacement.

``-bootp`` (removed in 3.1)
'''''''''''''''''''''''''''

The ``-bootp /some/file`` argument is replaced by either
``-netdev user,id=x,bootp=/some/file`` (for pluggable NICs, accompanied with
``-device ...,netdev=x``), or ``-nic user,bootp=/some/file`` (for on-board NICs).
The new syntax allows different settings to be provided per NIC.

``-redir`` (removed in 3.1)
'''''''''''''''''''''''''''

The ``-redir [tcp|udp]:hostport:[guestaddr]:guestport`` option is replaced
by either ``-netdev
user,id=x,hostfwd=[tcp|udp]:[hostaddr]:hostport-[guestaddr]:guestport``
(for pluggable NICs, accompanied with ``-device ...,netdev=x``) or by the option
``-nic user,hostfwd=[tcp|udp]:[hostaddr]:hostport-[guestaddr]:guestport``
(for on-board NICs). The new syntax allows different settings to be provided
per NIC.

``-smb`` (removed in 3.1)
'''''''''''''''''''''''''

The ``-smb /some/dir`` argument is replaced by either
``-netdev user,id=x,smb=/some/dir`` (for pluggable NICs, accompanied with
``-device ...,netdev=x``), or ``-nic user,smb=/some/dir`` (for on-board NICs).
The new syntax allows different settings to be provided per NIC.

``-tftp`` (removed in 3.1)
''''''''''''''''''''''''''

The ``-tftp /some/dir`` argument is replaced by either
``-netdev user,id=x,tftp=/some/dir`` (for pluggable NICs, accompanied with
``-device ...,netdev=x``), or ``-nic user,tftp=/some/dir`` (for embedded NICs).
The new syntax allows different settings to be provided per NIC.

``-localtime`` (removed in 3.1)
'''''''''''''''''''''''''''''''

Replaced by ``-rtc base=localtime``.

``-nodefconfig`` (removed in 3.1)
'''''''''''''''''''''''''''''''''

Use ``-no-user-config`` instead.

``-rtc-td-hack`` (removed in 3.1)
'''''''''''''''''''''''''''''''''

Use ``-rtc driftfix=slew`` instead.

``-startdate`` (removed in 3.1)
'''''''''''''''''''''''''''''''

Replaced by ``-rtc base=date``.

``-vnc ...,tls=...``, ``-vnc ...,x509=...`` & ``-vnc ...,x509verify=...`` (removed in 3.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The "tls-creds" option should be used instead to point to a "tls-creds-x509"
object created using "-object".

``-mem-path`` fallback to RAM (removed in 5.0)
''''''''''''''''''''''''''''''''''''''''''''''

If guest RAM allocation from file pointed by ``mem-path`` failed,
QEMU was falling back to allocating from RAM, which might have resulted
in unpredictable behavior since the backing file specified by the user
as ignored. Currently, users are responsible for making sure the backing storage
specified with ``-mem-path`` can actually provide the guest RAM configured with
``-m`` and QEMU fails to start up if RAM allocation is unsuccessful.

``-net ...,name=...`` (removed in 5.1)
''''''''''''''''''''''''''''''''''''''

The ``name`` parameter of the ``-net`` option was a synonym
for the ``id`` parameter, which should now be used instead.

RISC-V firmware not booted by default (removed in 5.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''

QEMU 5.1 changes the default behaviour from ``-bios none`` to ``-bios default``
for the RISC-V ``virt`` machine and ``sifive_u`` machine.

``-numa node,mem=...`` (removed in 5.1)
'''''''''''''''''''''''''''''''''''''''

The parameter ``mem`` of ``-numa node`` was used to assign a part of guest RAM
to a NUMA node. But when using it, it's impossible to manage a specified RAM
chunk on the host side (like bind it to a host node, setting bind policy, ...),
so the guest ends up with the fake NUMA configuration with suboptiomal
performance.
However since 2014 there is an alternative way to assign RAM to a NUMA node
using parameter ``memdev``, which does the same as ``mem`` and adds
means to actually manage node RAM on the host side. Use parameter ``memdev``
with *memory-backend-ram* backend as replacement for parameter ``mem``
to achieve the same fake NUMA effect or a properly configured
*memory-backend-file* backend to actually benefit from NUMA configuration.
New machine versions (since 5.1) will not accept the option but it will still
work with old machine types. User can check the QAPI schema to see if the legacy
option is supported by looking at MachineInfo::numa-mem-supported property.

``-numa`` node (without memory specified) (removed in 5.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

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

``-smp`` (invalid topologies) (removed in 5.2)
''''''''''''''''''''''''''''''''''''''''''''''

CPU topology properties should describe whole machine topology including
possible CPUs.

However, historically it was possible to start QEMU with an incorrect topology
where *n* <= *sockets* * *cores* * *threads* < *maxcpus*,
which could lead to an incorrect topology enumeration by the guest.
Support for invalid topologies is removed, the user must ensure
topologies described with -smp include all possible cpus, i.e.
*sockets* * *cores* * *threads* = *maxcpus*.

``-machine enforce-config-section=on|off`` (removed in 5.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``enforce-config-section`` property was replaced by the
``-global migration.send-configuration={on|off}`` option.

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

``-drive file=json:{...{'driver':'file'}}`` (removed in 6.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

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
Use ``-device usb-ccid`` instead.

``-no-quit`` (removed in 7.0)
'''''''''''''''''''''''''''''

The ``-no-quit`` was a synonym for ``-display ...,window-close=off`` which
should be used instead.

``--enable-fips`` (removed in 7.1)
''''''''''''''''''''''''''''''''''

This option restricted usage of certain cryptographic algorithms when
the host is operating in FIPS mode.

If FIPS compliance is required, QEMU should be built with the ``libgcrypt``
or ``gnutls`` library enabled as a cryptography provider.

Neither the ``nettle`` library, or the built-in cryptography provider are
supported on FIPS enabled hosts.

``-writeconfig`` (removed in 7.1)
'''''''''''''''''''''''''''''''''

The ``-writeconfig`` option was not able to serialize the entire contents
of the QEMU command line.  It is thus considered a failed experiment
and removed without a replacement.

``loaded`` property of secret and TLS credential objects (removed in 9.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``loaded=on`` option in the command line or QMP ``object-add`` either had
no effect (if ``loaded`` was the last option) or caused options to be
effectively ignored as if they were not given.  The property is therefore
useless and has been removed.

``opened`` property of ``rng-*`` objects (removed in 7.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``opened=on`` option in the command line or QMP ``object-add`` either had
no effect (if ``opened`` was the last option) or caused errors.  The property
is therefore useless and should simply be removed.

``-display sdl,window_close=...`` (removed in 7.1)
''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``-display sdl,window-close=...`` instead (i.e. with a minus instead of
an underscore between "window" and "close").

``-alt-grab`` and ``-display sdl,alt_grab=on`` (removed in 7.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``-display sdl,grab-mod=lshift-lctrl-lalt`` instead.

``-ctrl-grab`` and ``-display sdl,ctrl_grab=on`` (removed in 7.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``-display sdl,grab-mod=rctrl`` instead.

``-sdl`` (removed in 7.1)
'''''''''''''''''''''''''

Use ``-display sdl`` instead.

``-curses`` (removed in 7.1)
''''''''''''''''''''''''''''

Use ``-display curses`` instead.

Creating sound card devices using ``-soundhw`` (removed in 7.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Sound card devices should be created using ``-device`` or ``-audio``.
The exception is ``pcspk`` which can be activated using ``-machine
pcspk-audiodev=<name>``.

``-watchdog`` (removed in 7.2)
''''''''''''''''''''''''''''''

Use ``-device`` instead.

Hexadecimal sizes with scaling multipliers (removed in 8.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Input parameters that take a size value should only use a size suffix
(such as 'k' or 'M') when the base is written in decimal, and not when
the value is hexadecimal.  That is, '0x20M' should be written either as
'32M' or as '0x2000000'.

``-chardev`` backend aliases ``tty`` and ``parport`` (removed in 8.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

``tty`` and ``parport`` used to be aliases for ``serial`` and ``parallel``
respectively. The actual backend names should be used instead.

``-drive if=none`` for the sifive_u OTP device (removed in 8.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``-drive if=pflash`` to configure the OTP device of the sifive_u
RISC-V machine instead.

``-spice password=string`` (removed in 8.0)
'''''''''''''''''''''''''''''''''''''''''''

This option was insecure because the SPICE password remained visible in
the process listing. This was replaced by the new ``password-secret``
option which lets the password be securely provided on the command
line using a ``secret`` object instance.

``QEMU_AUDIO_`` environment variables and ``-audio-help`` (removed in 8.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``-audiodev`` and ``-audio`` command line options are now the only
way to specify audio backend settings.

Using ``-audiodev`` to define the default audio backend (removed in 8.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

If no audiodev property is specified, previous versions would use the
first ``-audiodev`` command line option as a fallback.  Starting with
version 8.2, audio backends created with ``-audiodev`` will only be
used by clients (sound cards, machines with embedded sound hardware, VNC)
that refer to it in an ``audiodev=`` property.

In order to configure a default audio backend, use the ``-audio``
command line option without specifying a ``model``; while previous
versions of QEMU required a model, starting with version 8.2
QEMU does not require a model and will not create any sound card
in this case.

Note that the default audio backend must be configured on the command
line if the ``-nodefaults`` options is used.

``-no-hpet`` (removed in 9.0)
'''''''''''''''''''''''''''''

The HPET setting has been turned into a machine property.
Use ``-machine hpet=off`` instead.

``-no-acpi`` (removed in 9.0)
'''''''''''''''''''''''''''''

The ``-no-acpi`` setting has been turned into a machine property.
Use ``-machine acpi=off`` instead.

``-async-teardown`` (removed in 9.0)
''''''''''''''''''''''''''''''''''''

Use ``-run-with async-teardown=on`` instead.

``-chroot`` (removed in 9.0)
''''''''''''''''''''''''''''

Use ``-run-with chroot=dir`` instead.

``-singlestep`` (removed in 9.0)
''''''''''''''''''''''''''''''''

The ``-singlestep`` option has been turned into an accelerator property,
and given a name that better reflects what it actually does.
Use ``-accel tcg,one-insn-per-tb=on`` instead.

``-smp`` ("parameter=0" SMP configurations) (removed in 9.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Specified CPU topology parameters must be greater than zero.

In the SMP configuration, users should either provide a CPU topology
parameter with a reasonable value (greater than zero) or just omit it
and QEMU will compute the missing value.

However, historically it was implicitly allowed for users to provide
a parameter with zero value, which is meaningless and could also possibly
cause unexpected results in the -smp parsing. So support for this kind of
configurations (e.g. -smp 8,sockets=0) is removed since 9.0, users have
to ensure that all the topology members described with -smp are greater
than zero.

``-global migration.decompress-error-check`` (removed in 9.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Removed along with the ``compression`` migration capability.

``-device virtio-blk,scsi=on|off`` (removed in 9.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''

The virtio-blk SCSI passthrough feature is a legacy VIRTIO feature.  VIRTIO 1.0
and later do not support it because the virtio-scsi device was introduced for
full SCSI support.  Use virtio-scsi instead when SCSI passthrough is required.

``-fsdev proxy`` and ``-virtfs proxy`` (removed in 9.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''

The 9p ``proxy`` filesystem backend driver was originally developed to
enhance security by dispatching low level filesystem operations from 9p
server (QEMU process) over to a separate process (the virtfs-proxy-helper
binary). However the proxy backend was much slower than the local backend,
didn't see any development in years, and showed to be less secure,
especially due to the fact that its helper daemon must be run as root.

Use ``local``, possibly mapping permissions et al by using its 'mapped'
security model option, or switch to ``virtiofs``.   The virtiofs daemon
``virtiofsd`` uses vhost to eliminate the high latency costs of the 9p
``proxy`` backend.

``-portrait`` and ``-rotate`` (removed in 9.2)
''''''''''''''''''''''''''''''''''''''''''''''

The ``-portrait`` and ``-rotate`` options were documented as only
working with the PXA LCD device, and all the machine types using
that display device were removed in 9.2, so these options also
have been dropped.

These options were intended to simulate a mobile device being
rotated by the user, and had three effects:

* the display output was rotated by 90, 180 or 270 degrees
* the mouse/trackpad input was rotated the opposite way
* the machine model would signal to the guest about its
  orientation

Of these three things, the input-rotation was coded without being
restricted to boards which supported the full set of device-rotation
handling, so in theory the options were usable on other machine models
to produce an odd effect (rotating input but not display output). But
this was never intended or documented behaviour, so we have dropped
the options along with the machine models they were intended for.

``-runas`` (removed in 10.0)
''''''''''''''''''''''''''''

Use ``-run-with user=..`` instead.

``-old-param`` option for booting Arm kernels via param_struct (removed in 10.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``-old-param`` command line option was specific to Arm targets:
it was used when directly booting a guest kernel to pass it the
command line and other information via the old ``param_struct`` ABI,
rather than the newer ATAGS or DTB mechanisms. This option was only
ever needed to support ancient kernels on some old board types
like the ``akita`` or ``terrier``; it has been deprecated in the
kernel since 2001. None of the board types QEMU supports need
``param_struct`` support, so this option has been removed.


User-mode emulator command line arguments
-----------------------------------------

``-singlestep`` (removed in 9.0)
''''''''''''''''''''''''''''''''

The ``-singlestep`` option has been given a name that better reflects
what it actually does. For both linux-user and bsd-user, use the
``-one-insn-per-tb`` option instead.

``-p`` (removed in 10.2)
''''''''''''''''''''''''

The ``-p`` option pretends to control the host page size.  However,
it is not possible to change the host page size; we stopped trying
to do anything with the option except print a warning from 9.0,
and now the option is removed entirely.


QEMU Machine Protocol (QMP) commands
------------------------------------

``block-dirty-bitmap-add`` "autoload" parameter (removed in 4.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The "autoload" parameter has been ignored since 2.12.0. All bitmaps
are automatically loaded from qcow2 images.

``cpu-add`` (removed in 5.2)
''''''''''''''''''''''''''''

Use ``device_add`` for hotplugging vCPUs instead of ``cpu-add``.  See
documentation of ``query-hotpluggable-cpus`` for additional details.

``change`` (removed in 6.0)
'''''''''''''''''''''''''''

Use ``blockdev-change-medium`` or ``change-vnc-password`` or
``display-update`` instead.

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

``query-sgx`` return value member ``section-size`` (removed in 8.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Member ``section-size`` in the return value of ``query-sgx``
was superseded by ``sections``.


``query-sgx-capabilities`` return value member ``section-size`` (removed in 8.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Member ``section-size`` in the return value of ``query-sgx-capabilities``
was superseded by ``sections``.

``query-migrate`` return value member ``skipped`` (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Member ``skipped`` of the ``MigrationStats`` struct hasn't been used
for more than 10 years. Removed with no replacement.

``migrate`` command option ``inc`` (removed in 9.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''

Use blockdev-mirror with NBD instead. See "QMP invocation for live
storage migration with ``blockdev-mirror`` + NBD" in
docs/interop/live-block-operations.rst for a detailed explanation.

``migrate`` command option ``blk`` (removed in 9.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''

Use blockdev-mirror with NBD instead. See "QMP invocation for live
storage migration with ``blockdev-mirror`` + NBD" in
docs/interop/live-block-operations.rst for a detailed explanation.

``migrate-set-capabilities`` ``block`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Block migration has been removed. For a replacement, see "QMP
invocation for live storage migration with ``blockdev-mirror`` + NBD"
in docs/interop/live-block-operations.rst.

``migrate-set-parameter`` ``compress-level`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``multifd-zlib-level`` or ``multifd-zstd-level`` instead.

``migrate-set-parameter`` ``compress-threads`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``multifd-channels`` instead.

``migrate-set-parameter`` ``compress-wait-thread`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Removed with no replacement.

``migrate-set-parameter`` ``decompress-threads`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``multifd-channels`` instead.

``migrate-set-capability`` ``compress`` option (removed in 9.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``multifd-compression`` instead.

Incorrectly typed ``device_add`` arguments (removed in 9.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Due to shortcomings in the internal implementation of ``device_add``,
QEMU used to incorrectly accept certain invalid arguments. Any object
or list arguments were silently ignored. Other argument types were not
checked, but an implicit conversion happened, so that e.g. string
values could be assigned to integer device properties or vice versa.

QEMU Machine Protocol (QMP) events
----------------------------------

``MEM_UNPLUG_ERROR`` (removed in 9.1)
'''''''''''''''''''''''''''''''''''''

MEM_UNPLUG_ERROR has been replaced by the more generic ``DEVICE_UNPLUG_GUEST_ERROR`` event.

``vcpu`` trace events (removed in 9.1)
''''''''''''''''''''''''''''''''''''''

The ability to instrument QEMU helper functions with vCPU-aware trace
points was removed in 7.0.


Human Monitor Protocol (HMP) commands
-------------------------------------

``usb_add`` and ``usb_remove`` (removed in 2.12)
''''''''''''''''''''''''''''''''''''''''''''''''

Replaced by ``device_add`` and ``device_del`` (use ``device_add help`` for a
list of available devices).

``host_net_add`` and ``host_net_remove`` (removed in 2.12)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Replaced by ``netdev_add`` and ``netdev_del``.

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

``info cpustats`` (removed in 6.1)
''''''''''''''''''''''''''''''''''

This command didn't produce any output already. Removed with no replacement.

``singlestep`` (removed in 9.0)
'''''''''''''''''''''''''''''''

The ``singlestep`` command has been replaced by the ``one-insn-per-tb``
command, which has the same behaviour but a less misleading name.

``migrate`` command ``-i`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''

Use blockdev-mirror with NBD instead. See "QMP invocation for live
storage migration with ``blockdev-mirror`` + NBD" in
docs/interop/live-block-operations.rst for a detailed explanation.

``migrate`` command ``-b`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''

Use blockdev-mirror with NBD instead. See "QMP invocation for live
storage migration with ``blockdev-mirror`` + NBD" in
docs/interop/live-block-operations.rst for a detailed explanation.

``migrate_set_capability`` ``block`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Block migration has been removed. For a replacement, see "QMP
invocation for live storage migration with ``blockdev-mirror`` + NBD"
in docs/interop/live-block-operations.rst.

``migrate_set_parameter`` ``compress-level`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``multifd-zlib-level`` or ``multifd-zstd-level`` instead.

``migrate_set_parameter`` ``compress-threads`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``multifd-channels`` instead.

``migrate_set_parameter`` ``compress-wait-thread`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Removed with no replacement.

``migrate_set_parameter`` ``decompress-threads`` option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``multifd-channels`` instead.

``migrate_set_capability`` ``compress`` option (removed in 9.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use ``multifd-compression`` instead.

Host Architectures
------------------

System emulation on 32-bit Windows hosts (removed in 9.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Windows 11 has no support for 32-bit host installs, and Windows 10 did
not support new 32-bit installs, only upgrades. 32-bit Windows support
has now been dropped by the MSYS2 project. QEMU also is deprecating
and dropping support for 32-bit x86 host deployments in
general. 32-bit Windows is therefore no longer a supported host for
QEMU.  Since all recent x86 hardware from the past >10 years is
capable of the 64-bit x86 extensions, a corresponding 64-bit OS should
be used instead.

32-bit hosts for 64-bit guests (removed in 10.0)
''''''''''''''''''''''''''''''''''''''''''''''''

In general, 32-bit hosts cannot support the memory space or atomicity
requirements of 64-bit guests.  Prior to 10.0, QEMU attempted to
work around the atomicity issues in system mode by running all vCPUs
in a single thread context; in user mode atomicity was simply broken.
From 10.0, QEMU has disabled configuration of 64-bit guests on 32-bit hosts.

32-bit MIPS (since 10.2)
''''''''''''''''''''''''

Debian 12 "Bookworm" removed support for 32-bit MIPS, making it hard to
maintain our cross-compilation CI tests of the architecture.

32-bit PPC (since 10.2)
'''''''''''''''''''''''

The QEMU project no longer supports 32-bit host builds.

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
to host KVM guests as of the 5.7 kernel, and was thus removed from QEMU
as well.  Running 32-bit guests on a 64-bit Arm host remains supported.

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

``compat`` property of server class POWER CPUs (removed in 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``max-cpu-compat`` property of the ``pseries`` machine type should be used
instead.

``moxie`` CPU (removed in 6.1)
''''''''''''''''''''''''''''''

Nobody was using this CPU emulation in QEMU, and there were no test images
available to make sure that the code is still working, so it has been removed
without replacement.

``lm32`` CPUs (removed in 6.1)
''''''''''''''''''''''''''''''

The only public user of this architecture was the milkymist project,
which has been dead for years; there was never an upstream Linux
port.  Removed without replacement.

``unicore32`` CPUs (removed in 6.1)
'''''''''''''''''''''''''''''''''''

Support for this CPU was removed from the upstream Linux kernel, and
there is no available upstream toolchain to build binaries for it.
Removed without replacement.

x86 ``Icelake-Client`` CPU (removed in 7.1)
'''''''''''''''''''''''''''''''''''''''''''

There isn't ever Icelake Client CPU, it is some wrong and imaginary one.
Use ``Icelake-Server`` instead.

Nios II CPU (removed in 9.1)
''''''''''''''''''''''''''''

QEMU Nios II architecture was orphan; Intel has EOL'ed the Nios II
processor IP (see `Intel discontinuance notification`_).

CRIS CPU architecture (removed in 9.2)
''''''''''''''''''''''''''''''''''''''

The CRIS architecture was pulled from Linux in 4.17 and the compiler
was no longer packaged in any distro making it harder to run the
``check-tcg`` tests.

RISC-V 'any' CPU type ``-cpu any`` (removed in 9.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''

The 'any' CPU type was introduced back in 2018 and was around since the
initial RISC-V QEMU port. Its usage was always been unclear: users don't know
what to expect from a CPU called 'any', and in fact the CPU does not do anything
special that isn't already done by the default CPUs rv32/rv64.

System accelerators
-------------------

Userspace local APIC with KVM (x86, removed in 8.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''

``-M kernel-irqchip=off`` cannot be used on KVM if the CPU model includes
a local APIC.  The ``split`` setting is supported, as is using ``-M
kernel-irqchip=off`` when the CPU does not have a local APIC.

MIPS "Trap-and-Emulate" KVM support (removed in 8.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''

The MIPS "Trap-and-Emulate" KVM host and guest support was removed
from Linux in 2021, and is not supported anymore by QEMU either.

HAXM (``-accel hax``) (removed in 8.2)
''''''''''''''''''''''''''''''''''''''

The HAXM project has been retired (see https://github.com/intel/haxm#status).
Use "whpx" (on Windows) or "hvf" (on macOS) instead.

System emulator machines
------------------------

Versioned machine types (aarch64, arm, i386, m68k, ppc64, s390x, x86_64)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

In accordance with our versioned machine type deprecation policy, all machine
types with version |VER_MACHINE_DELETION_VERSION|, or older, have been
removed.

``s390-virtio`` (removed in 2.6)
''''''''''''''''''''''''''''''''

Use the ``s390-ccw-virtio`` machine instead.

The m68k ``dummy`` machine (removed in 2.9)
'''''''''''''''''''''''''''''''''''''''''''

Use the ``none`` machine with the ``loader`` device instead.

``xlnx-ep108`` (removed in 3.0)
'''''''''''''''''''''''''''''''

The EP108 was an early access development board that is no longer used.
Use the ``xlnx-zcu102`` machine instead.

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

Raspberry Pi ``raspi2`` and ``raspi3`` machines (removed in 6.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The Raspberry Pi machines come in various models (A, A+, B, B+). To be able
to distinguish which model QEMU is implementing, the ``raspi2`` and ``raspi3``
machines have been renamed ``raspi2b`` and ``raspi3b``.

Aspeed ``swift-bmc`` machine (removed in 7.0)
'''''''''''''''''''''''''''''''''''''''''''''

This machine was removed because it was unused. Alternative AST2500 based
OpenPOWER machines are ``witherspoon-bmc`` and ``romulus-bmc``.

ppc ``taihu`` machine (removed in 7.2)
'''''''''''''''''''''''''''''''''''''''''''''

This machine was removed because it was partially emulated and 405
machines are very similar. Use the ``ref405ep`` machine instead.

Nios II ``10m50-ghrd`` and ``nios2-generic-nommu`` machines (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The Nios II architecture was orphan.

``shix`` (removed in 9.2)
'''''''''''''''''''''''''

The machine was unmaintained.

Arm machines ``akita``, ``borzoi``, ``cheetah``, ``connex``, ``mainstone``, ``n800``, ``n810``, ``spitz``, ``terrier``, ``tosa``, ``verdex``, ``z2`` (removed in 9.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

QEMU included models of some machine types where the QEMU code that
emulates their SoCs was very old and unmaintained. This code was
blocking our ability to move forward with various changes across
the codebase, and over many years nobody has been interested in
trying to modernise it. We don't expect any of these machines to have
a large number of users, because they're all modelling hardware that
has now passed away into history. We are therefore dropping support
for all machine types using the PXA2xx and OMAP2 SoCs. We are also
dropping the ``cheetah`` OMAP1 board, because we don't have any
test images for it and don't know of anybody who does.

Aspeed ``tacoma-bmc`` machine (removed in 10.0)
'''''''''''''''''''''''''''''''''''''''''''''''

The ``tacoma-bmc`` machine was removed because it didn't bring much
compared to the ``rainier-bmc`` machine. Also, the ``tacoma-bmc`` was
a board used for bring up of the AST2600 SoC that never left the
labs. It can be easily replaced by the ``rainier-bmc`` machine, which
was the actual final product, or by the ``ast2600-evb`` with some
tweaks.

ppc ``ref405ep`` machine (removed in 10.0)
''''''''''''''''''''''''''''''''''''''''''

This machine was removed because PPC 405 CPU have no known users,
firmware images are not available, OpenWRT dropped support in 2019,
U-Boot in 2017, and Linux in 2024.

Big-Endian variants of ``petalogix-ml605`` and ``xlnx-zynqmp-pmu`` machines (removed in 10.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Both the MicroBlaze ``petalogix-ml605`` and ``xlnx-zynqmp-pmu`` machines
were added for little endian CPUs. Big endian support was never tested
and likely never worked. Starting with QEMU v10.1, the machines are now
only available as little-endian machines.

Mips ``mipssim`` machine (removed in 10.2)
''''''''''''''''''''''''''''''''''''''''''

Linux dropped support for this virtual machine type in kernel v3.7, and
there was also no binary available online to use with that board.

linux-user mode CPUs
--------------------

``tilegx`` CPUs (removed in 6.0)
''''''''''''''''''''''''''''''''

The ``tilegx`` guest CPU support has been removed without replacement. It was
only implemented in linux-user mode, but support for this CPU was removed from
the upstream Linux kernel in 2018, and it has also been dropped from glibc, so
there is no new Linux development taking place with this architecture. For
running the old binaries, you can use older versions of QEMU.

``ppc64abi32`` CPUs (removed in 7.0)
''''''''''''''''''''''''''''''''''''

The ``ppc64abi32`` architecture has a number of issues which regularly
tripped up the CI testing and was suspected to be quite broken. For that
reason the maintainers strongly suspected no one actually used it.

``nios2`` CPU (removed in 9.1)
''''''''''''''''''''''''''''''

QEMU Nios II architecture was orphan; Intel has EOL'ed the Nios II
processor IP (see `Intel discontinuance notification`_).

iwMMXt emulation and the ``pxa`` CPUs (removed in 10.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``pxa`` CPU family (``pxa250``, ``pxa255``, ``pxa260``,
``pxa261``, ``pxa262``, ``pxa270-a0``, ``pxa270-a1``, ``pxa270``,
``pxa270-b0``, ``pxa270-b1``, ``pxa270-c0``, ``pxa270-c5``) were
not available in system emulation, because all the machine types which
used these CPUs were removed in the QEMU 9.2 release. We don't
believe that anybody was using the iwMMXt emulation (which you
would have to explicitly enable on the command line), and we did
not have any tests to validate it or any real hardware or similar
known-good implementation to test against. These CPUs have
therefore been removed in linux-user mode as well.

TCG introspection features
--------------------------

TCG trace-events (removed in 7.0)
'''''''''''''''''''''''''''''''''

The ability to add new TCG trace points had bit rotted and as the
feature can be replicated with TCG plugins it was removed. If
any user is currently using this feature and needs help with
converting to using TCG plugins they should contact the qemu-devel
mailing list.


System emulator devices
-----------------------

``spapr-pci-vfio-host-bridge`` (removed in 2.12)
'''''''''''''''''''''''''''''''''''''''''''''''''

The ``spapr-pci-vfio-host-bridge`` device type has been replaced by the
``spapr-pci-host-bridge`` device type.

``ivshmem`` (removed in 4.0)
''''''''''''''''''''''''''''

Replaced by either the ``ivshmem-plain`` or ``ivshmem-doorbell``.

``ide-drive`` (removed in 6.0)
''''''''''''''''''''''''''''''

The 'ide-drive' device has been removed. Users should use 'ide-hd' or
'ide-cd' as appropriate to get an IDE hard disk or CD-ROM as needed.

``scsi-disk`` (removed in 6.0)
''''''''''''''''''''''''''''''

The 'scsi-disk' device has been removed. Users should use 'scsi-hd' or
'scsi-cd' as appropriate to get a SCSI hard disk or CD-ROM as needed.

``sga`` (removed in 8.0)
''''''''''''''''''''''''

The ``sga`` device loaded an option ROM for x86 targets which enabled
SeaBIOS to send messages to the serial console. SeaBIOS 1.11.0 onwards
contains native support for this feature and thus use of the option
ROM approach was obsolete. The native SeaBIOS support can be activated
by using ``-machine graphics=off``.

``pvrdma`` and the RDMA subsystem (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''

The 'pvrdma' device and the whole RDMA subsystem have been removed.

``-device sd-card,spec_version=1`` (since 10.2)
'''''''''''''''''''''''''''''''''''''''''''''''

SD physical layer specification v2.00 supersedes the v1.10 one.

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

``qemu-img amend`` to adjust backing file (removed in 6.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The use of ``qemu-img amend`` to modify the name or format of a qcow2
backing image was never fully documented or tested, and interferes
with other amend operations that need access to the original backing
image (such as deciding whether a v3 zero cluster may be left
unallocated when converting to a v2 image).  Any changes to the
backing chain should be performed with ``qemu-img rebase -u`` either
before or after the remaining changes being performed by amend, as
appropriate.

``qemu-img`` backing file without format (removed in 6.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The use of ``qemu-img create``, ``qemu-img rebase``, or ``qemu-img
convert`` to create or modify an image that depends on a backing file
now requires that an explicit backing format be provided.  This is
for safety: if QEMU probes a different format than what you thought,
the data presented to the guest will be corrupt; similarly, presenting
a raw image to a guest allows a potential security exploit if a future
probe sees a non-raw image based on guest writes.

To avoid creating unsafe backing chains, you must pass ``-o
backing_fmt=`` (or the shorthand ``-F`` during create) to specify the
intended backing format.  You may use ``qemu-img rebase -u`` to
retroactively add a backing format to an existing image.  However, be
aware that there are already potential security risks to blindly using
``qemu-img info`` to probe the format of an untrusted backing image,
when deciding what format to add into an existing image.

Block devices
-------------

VXHS backend (removed in 5.1)
'''''''''''''''''''''''''''''

The VXHS code did not compile since v2.12.0. It was removed in 5.1.

``sheepdog`` driver (removed in 6.0)
''''''''''''''''''''''''''''''''''''

The corresponding upstream server project is no longer maintained.
Users are recommended to switch to an alternative distributed block
device driver such as RBD.

VFIO devices
------------

``-device vfio-calxeda-xgmac`` (since 10.2)
'''''''''''''''''''''''''''''''''''''''''''
The vfio-calxeda-xgmac device allows to assign a host Calxeda Highbank
10Gb XGMAC Ethernet controller device ("calxeda,hb-xgmac" compatibility
string) to a guest. Calxeda HW has been ewasted now and there is no point
keeping that device.

``-device vfio-amd-xgbe`` (since 10.2)
''''''''''''''''''''''''''''''''''''''
The vfio-amd-xgbe device allows to assign a host AMD 10GbE controller
to a guest ("amd,xgbe-seattle-v1a" compatibility string). AMD "Seattle"
is not supported anymore and there is no point keeping that device.

``-device vfio-platform`` (since 10.2)
''''''''''''''''''''''''''''''''''''''
The vfio-platform device allows to assign a host platform device
to a guest in a generic manner. Integrating a new device into
the vfio-platform infrastructure requires some adaptation at
both kernel and qemu level. No such attempt has been done for years
and the conclusion is that vfio-platform has not got any traction.
PCIe passthrough shall be the mainline solution.

Tools
-----

virtiofsd (removed in 8.0)
''''''''''''''''''''''''''

There is a newer Rust implementation of ``virtiofsd`` at
``https://gitlab.com/virtio-fs/virtiofsd``; this has been
stable for some time and is now widely used.
The command line and feature set is very close to the removed
C implementation.

QEMU guest agent
----------------

``--blacklist`` command line option (removed in 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''

``--blacklist`` has been replaced by ``--block-rpcs`` (which is a better
wording for what this option does). The short form ``-b`` still stays
the same and thus is the preferred way for scripts that should run with
both, older and future versions of QEMU.

``blacklist`` config file option (removed in 9.1)
'''''''''''''''''''''''''''''''''''''''''''''''''

The ``blacklist`` config file option has been renamed to ``block-rpcs``
(to be in sync with the renaming of the corresponding command line
option).

Device options
--------------

Character device options
''''''''''''''''''''''''

``reconnect`` (removed in 10.2)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``reconnect`` has been replaced by ``reconnect-ms``, which provides
better precision.

Net device options
''''''''''''''''''

Stream ``reconnect`` (removed in 10.2)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``reconnect`` has been replaced by ``reconnect-ms``, which provides
better precision.


.. _Intel discontinuance notification: https://www.intel.com/content/www/us/en/content-details/781327/intel-is-discontinuing-ip-ordering-codes-listed-in-pdn2312-for-nios-ii-ip.html
