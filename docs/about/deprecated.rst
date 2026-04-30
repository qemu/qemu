.. _Deprecated features:

Deprecated features
===================

In general features are intended to be supported indefinitely once
introduced into QEMU. In the event that a feature needs to be removed,
it will be listed in this section. The feature will remain functional for the
release in which it was deprecated and one further release. After these two
releases, the feature is liable to be removed. Deprecated features may also
generate warnings on the console when QEMU starts up, or if activated via a
monitor command, however, this is not a mandatory requirement.

As a special exception to this general timeframe, rather than have an
indefinite lifetime, versioned machine types are only intended to be
supported for a period of 6 years, equivalent to 18 QEMU releases. All
versioned machine types will be automatically marked deprecated after an
initial 3 years (9 QEMU releases) has passed, and will then be deleted after
a further 3 year period has passed. It is recommended that a deprecated
machine type is only used for incoming migrations and restore of saved state,
for pre-existing VM deployments. They should be scheduled for updating to a
newer machine type during an appropriate service window. Newly deployed VMs
should exclusively use a non-deprecated machine type, with use of the most
recent version highly recommended. Non-versioned machine types follow the
general feature deprecation policy.

What follows is a list of all features currently marked as
deprecated.

System emulator command line arguments
--------------------------------------

Short-form boolean options (since 6.0)
''''''''''''''''''''''''''''''''''''''

Boolean options such as ``share=on``/``share=off`` could be written
in short form as ``share`` and ``noshare``.  This is now deprecated
and will cause a warning.

``delay`` option for socket character devices (since 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The replacement for the ``nodelay`` short-form boolean option is ``nodelay=on``
rather than ``delay=off``.

Plugin argument passing through ``arg=<string>`` (since 6.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Passing TCG plugins arguments through ``arg=`` is redundant is makes the
command-line less readable, especially when the argument itself consist of a
name and a value, e.g. ``-plugin plugin_name,arg="arg_name=arg_value"``.
Therefore, the usage of ``arg`` is redundant. Single-word arguments are treated
as short-form boolean values, and passed to plugins as ``arg_name=on``.
However, short-form booleans are deprecated and full explicit ``arg_name=on``
form is preferred.

``debug-threads`` option for ``-name`` (since 11.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''

The ``debug-threads`` option of the ``-name`` argument is now
ignored. Thread naming is unconditionally enabled for all platforms
where it is supported.

QEMU Machine Protocol (QMP) commands
------------------------------------

``blockdev-open-tray``, ``blockdev-close-tray`` argument ``device`` (since 2.8)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``eject`` argument ``device`` (since 2.8)
'''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``blockdev-change-medium`` argument ``device`` (since 2.8)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``block_set_io_throttle`` argument ``device`` (since 2.8)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``blockdev-add`` empty string argument ``backing`` (since 2.10)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument value ``null`` instead.

``block-commit`` arguments ``base`` and ``top`` (since 3.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use arguments ``base-node`` and ``top-node`` instead.

``nbd-server-add`` and ``nbd-server-remove`` (since 5.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use the more generic commands ``block-export-add`` and ``block-export-del``
instead.  As part of this deprecation, where ``nbd-server-add`` used a
single ``bitmap``, the new ``block-export-add`` uses a list of ``bitmaps``.

``query-qmp-schema`` return value member ``values`` (since 6.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Member ``values`` in return value elements with meta-type ``enum`` is
deprecated.  Use ``members`` instead.

``drive-backup`` (since 6.2)
''''''''''''''''''''''''''''

Use ``blockdev-backup`` in combination with ``blockdev-add`` instead.
This change primarily separates the creation/opening process of the backup
target with explicit, separate steps. ``blockdev-backup`` uses mostly the
same arguments as ``drive-backup``, except the ``format`` and ``mode``
options are removed in favor of using explicit ``blockdev-create`` and
``blockdev-add`` calls. See :doc:`/interop/live-block-operations` for
details.

``block-job-pause`` (since 10.1)
''''''''''''''''''''''''''''''''

Use ``job-pause`` instead. The only difference is that ``job-pause``
always reports GenericError on failure when ``block-job-pause`` reports
DeviceNotActive when block-job is not found.

``block-job-resume`` (since 10.1)
'''''''''''''''''''''''''''''''''

Use ``job-resume`` instead. The only difference is that ``job-resume``
always reports GenericError on failure when ``block-job-resume`` reports
DeviceNotActive when block-job is not found.

``block-job-complete`` (since 10.1)
'''''''''''''''''''''''''''''''''''

Use ``job-complete`` instead. The only difference is that ``job-complete``
always reports GenericError on failure when ``block-job-complete`` reports
DeviceNotActive when block-job is not found.

``block-job-dismiss`` (since 10.1)
''''''''''''''''''''''''''''''''''

Use ``job-dismiss`` instead.

``block-job-finalize`` (since 10.1)
'''''''''''''''''''''''''''''''''''

Use ``job-finalize`` instead.

``query-kvm`` (since 11.0)
''''''''''''''''''''''''''

Use ``query-accelerators`` instead.

Human Machine Protocol (HMP) commands
-------------------------------------

``wavcapture`` (since 10.2)
''''''''''''''''''''''''''''

The ``wavcapture`` command is deprecated and will be removed in a future release.

Use ``-audiodev wav`` or your host audio system to capture audio.

``stopcapture`` (since 10.2)
''''''''''''''''''''''''''''

The ``stopcapture`` command is deprecated and will be removed in a future release.

``info`` argument ``capture`` (since 10.2)
''''''''''''''''''''''''''''''''''''''''''

The ``info capture`` command is deprecated and will be removed in a future release.

Host Architectures
------------------

TCG Plugin support not enabled by default with TCI (since 9.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

While the TCG interpreter can interpret the TCG ops used by plugins it
is going to be so much slower it wouldn't make sense for any serious
instrumentation. Due to implementation differences there will also be
anomalies in things like memory instrumentation.

System emulator CPUs
--------------------

``power5+`` and ``power7+`` CPU names (since 9.0)
'''''''''''''''''''''''''''''''''''''''''''''''''

The character "+" in device (and thus also CPU) names is not allowed
in the QEMU object model anymore. ``power5+``, ``power5+_v2.1``,
``power7+`` and ``power7+_v2.1`` are currently still supported via
an alias, but for consistency these will get removed in a future
release, too. Use ``power5p_v2.1`` and ``power7p_v2.1`` instead.

``Sun-UltraSparc-IIIi+`` and ``Sun-UltraSparc-IV+`` CPU names (since 9.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The character "+" in device (and thus also CPU) names is not allowed
in the QEMU object model anymore. ``Sun-UltraSparc-IIIi+`` and
``Sun-UltraSparc-IV+`` are currently still supported via a workaround,
but for consistency these will get removed in a future release, too.
Use ``Sun-UltraSparc-IIIi-plus`` and ``Sun-UltraSparc-IV-plus`` instead.

PPC 405 CPUs (since 10.0)
'''''''''''''''''''''''''

The PPC 405 CPU has no known users and the ``ref405ep`` machine was
removed in QEMU 10.0. Since the IBM POWER [8-11] processors uses an
embedded 405 for power management (OCC) and other internal tasks, it
is theoretically possible to use QEMU to model them. Let's keep the
CPU implementation for a while before removing all support.

Power8E and Power8NVL CPUs and corresponding Pnv chips (since 10.1)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The Power8E and Power8NVL variants of Power8 are not really useful anymore
in qemu, and are old and unmaintained now.

The CPUs as well as corresponding Power8NVL and Power8E PnvChips will also
be considered deprecated.

System emulator machines
------------------------

Versioned machine types (aarch64, arm, i386, m68k, ppc64, s390x, x86_64)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

In accordance with our versioned machine type deprecation policy, all machine
types with version |VER_MACHINE_DEPRECATION_VERSION|, or older, have been
deprecated.

Arm ``virt`` machine ``dtb-kaslr-seed`` property (since 7.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``dtb-kaslr-seed`` property on the ``virt`` board has been
deprecated; use the new name ``dtb-randomness`` instead. The new name
better reflects the way this property affects all random data within
the device tree blob, not just the ``kaslr-seed`` node.

RISC-V Shakti machine (since 11.1)
''''''''''''''''''''''''''''''''''

The RISC-V ``shakti_c`` machine hasn't had meaningful contributions since 2021
and is currently unmaintained. The machine is scheduled to be removed as it
appears to have no users.


Backend options
---------------

Using non-persistent backing file with pmem=on (since 6.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

This option is used when ``memory-backend-file`` is consumed by emulated NVDIMM
device. However enabling ``memory-backend-file.pmem`` option, when backing file
is (a) not DAX capable or (b) not on a filesystem that support direct mapping
of persistent memory, is not safe and may lead to data loss or corruption in case
of host crash.
Options are:

    - modify VM configuration to set ``pmem=off`` to continue using fake NVDIMM
      (without persistence guaranties) with backing file on non DAX storage
    - move backing file to NVDIMM storage and keep ``pmem=on``
      (to have NVDIMM with persistence guaranties).

Using an external DH (Diffie-Hellman) parameters file (since 10.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Loading of external Diffie-Hellman parameters from a 'dh-params.pem'
file is deprecated and will be removed with no replacement in a
future release. Where no 'dh-params.pem' file is provided, the DH
parameters will be automatically negotiated in accordance with
RFC7919.

Device options
--------------

Emulated device options
'''''''''''''''''''''''

``-device nvme-ns,eui64-default=on|off`` (since 7.1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In QEMU versions 6.1, 6.2 and 7.0, the ``nvme-ns`` generates an EUI-64
identifier that is not globally unique. If an EUI-64 identifier is required, the
user must set it explicitly using the ``nvme-ns`` device parameter ``eui64``.

``-device nvme,use-intel-id=on|off`` (since 7.1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``nvme`` device originally used a PCI Vendor/Device Identifier combination
from Intel that was not properly allocated. Since version 5.2, the controller
has used a properly allocated identifier. Deprecate the ``use-intel-id``
machine compatibility parameter.

``-device cxl-type3,memdev=xxxx`` (since 8.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``cxl-type3`` device initially only used a single memory backend.  With
the addition of volatile memory support, it is now necessary to distinguish
between persistent and volatile memory backends.  As such, memdev is deprecated
in favor of persistent-memdev.


RISC-V CPU properties which start with capital 'Z' (since 8.2)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

All RISC-V CPU properties which start with capital 'Z' are being deprecated
starting in 8.2. The reason is that they were wrongly added with capital 'Z'
in the past. CPU properties were later added with lower-case names, which
is the format we want to use from now on.

Users which try to use these deprecated properties will receive a warning
recommending to switch to their stable counterparts:

- "Zifencei" should be replaced with "zifencei"
- "Zicsr" should be replaced with "zicsr"
- "Zihintntl" should be replaced with "zihintntl"
- "Zihintpause" should be replaced with "zihintpause"
- "Zawrs" should be replaced with "zawrs"
- "Zfa" should be replaced with "zfa"
- "Zfh" should be replaced with "zfh"
- "Zfhmin" should be replaced with "zfhmin"
- "Zve32f" should be replaced with "zve32f"
- "Zve64f" should be replaced with "zve64f"
- "Zve64d" should be replaced with "zve64d"

Block device options
''''''''''''''''''''

``"backing": ""`` (since 2.12)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In order to prevent QEMU from automatically opening an image's backing
chain, use ``"backing": null`` instead.

``rbd`` keyvalue pair encoded filenames: ``""`` (since 3.1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Options for ``rbd`` should be specified according to its runtime options,
like other block drivers.  Legacy parsing of keyvalue pair encoded
filenames is useful to open images with the old format for backing files;
These image files should be updated to use the current format.

Example of legacy encoding::

  json:{"file.driver":"rbd", "file.filename":"rbd:rbd/name"}

The above, converted to the current supported format::

  json:{"file.driver":"rbd", "file.pool":"rbd", "file.image":"name"}

``iscsi,password=xxx`` (since 8.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Specifying the iSCSI password in plain text on the command line using the
``password`` option is insecure. The ``password-secret`` option should be
used instead, to refer to a ``--object secret...`` instance that provides
a password via a file, or encrypted.


Character device options
''''''''''''''''''''''''

Backend ``memory`` (since 9.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``memory`` is a deprecated synonym for ``ringbuf``.


CPU device properties
'''''''''''''''''''''

``pmu-num=n`` on RISC-V CPUs (since 8.2)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In order to support more flexible counter configurations this has been replaced
by a ``pmu-mask`` property. If set of counters is continuous then the mask can
be calculated with ``((2 ^ n) - 1) << 3``. The least significant three bits
must be left clear.


``pcommit`` on x86 (since 9.1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The PCOMMIT instruction was never included in any physical processor.
It was implemented as a no-op instruction in TCG up to QEMU 9.0, but
only with ``-cpu max`` (which does not guarantee migration compatibility
across versions).

linux-user mode CPUs
--------------------

OABI and NWFPE support for Arm CPUs
'''''''''''''''''''''''''''''''''''

Linux for 32-bit Arm has had two major ABIs: the original OABI and the
more modern EABI. OABI support was marked as obsolete in GCC 4.7 and
dropped in GCC 4.8 (released in 2013). In the Linux kernel,
compatibility handling for OABI (OABI_COMPAT) is not generally enabled
by default and is not compatible with building a Thumb2
kernel. Distros dropped OABI support fifteen years or more ago.

The original floating-point coprocessor for 32-bit Arm was the
FPA11. This was not present in many CPUs but did get baked into the
OABI for how to pass floating point arguments, and so the Linux kernel
has support for emulating it via the config option FPE_NWFPE; QEMU
follows that. FPA11 support was also removed from GCC in GCC 4.8.

QEMU's NWFPE code is old and untested and not thread-safe; the OABI
ABI is long-obsolete. We are therefore deprecating both OABI support
and NWFPE emulation, and they will be removed in a future QEMU
release.


Backwards compatibility
-----------------------

Runnability guarantee of CPU models (since 4.1)
'''''''''''''''''''''''''''''''''''''''''''''''

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

x86 "isapc" board use of modern x86 CPUs (since 10.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''

The "isapc" board represents a historical x86 ISA PC and is intended for
older 32-bit x86 CPU models, defaulting to a 486 CPU model.  Previously it
was possible (but non-sensical) to specify a more modern x86 CPU, including
``-cpu host`` or ``-cpu max`` even if the features were incompatible with many
of the intended guest OSs.

If the user requests a modern x86 CPU model (i.e. not one of ``486``,
``athlon``, ``kvm32``, ``pentium``, ``pentium2``, ``pentium3``or ``qemu32``)
a warning will be displayed until a future QEMU version when such CPUs will
be rejected.
