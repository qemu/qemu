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

``-smp`` (Unsupported "parameter=1" SMP configurations) (since 9.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Specified CPU topology parameters must be supported by the machine.

In the SMP configuration, users should provide the CPU topology parameters that
are supported by the target machine.

However, historically it was allowed for users to specify the unsupported
topology parameter as "1", which is meaningless. So support for this kind of
configurations (e.g. -smp drawers=1,books=1,clusters=1 for x86 PC machine) is
marked deprecated since 9.0, users have to ensure that all the topology members
described with -smp are supported by the target machine.

``-old-param`` option for booting Arm kernels via param_struct (since 10.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``-old-param`` command line option is specific to Arm targets:
it is used when directly booting a guest kernel to pass it the
command line and other information via the old ``param_struct`` ABI,
rather than the newer ATAGS or DTB mechanisms. This option was only
ever needed to support ancient kernels on some old board types
like the ``akita`` or ``terrier``; it has been deprecated in the
kernel since 2001. None of the board types QEMU supports need
``param_struct`` support, so this option has been deprecated and will
be removed in a future QEMU version.

User-mode emulator command line arguments
-----------------------------------------

``-p`` (since 9.0)
''''''''''''''''''

The ``-p`` option pretends to control the host page size.  However,
it is not possible to change the host page size, and using the
option only causes failures.

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

``query-migrationthreads`` (since 9.2)
''''''''''''''''''''''''''''''''''''''

To be removed with no replacement, as it reports only a limited set of
threads (for example, it only reports source side of multifd threads,
without reporting any destination threads, or non-multifd source threads).
For debugging purpose, please use ``-name $VM,debug-threads=on`` instead.

Incorrectly typed ``device_add`` arguments (since 6.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''

Due to shortcomings in the internal implementation of ``device_add``, QEMU
incorrectly accepts certain invalid arguments: Any object or list arguments are
silently ignored. Other argument types are not checked, but an implicit
conversion happens, so that e.g. string values can be assigned to integer
device properties or vice versa.

This is a bug in QEMU that will be fixed in the future so that previously
accepted incorrect commands will return an error. Users should make sure that
all arguments passed to ``device_add`` are consistent with the documented
property types.

Host Architectures
------------------

Big endian MIPS since 7.2; 32-bit little endian MIPS since 9.2
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

As Debian 10 ("Buster") moved into LTS the big endian 32 bit version of
MIPS moved out of support making it hard to maintain our
cross-compilation CI tests of the architecture. As we no longer have
CI coverage support may bitrot away before the deprecation process
completes.

Likewise, the little endian variant of 32 bit MIPS is not supported by
Debian 13 ("Trixie") and newer.

64 bit little endian MIPS is still a supported host architecture.

System emulation on 32-bit x86 hosts (since 8.0)
''''''''''''''''''''''''''''''''''''''''''''''''

Support for 32-bit x86 host deployments is increasingly uncommon in mainstream
OS distributions given the widespread availability of 64-bit x86 hardware.
The QEMU project no longer considers 32-bit x86 support for system emulation to
be an effective use of its limited resources, and thus intends to discontinue
it. Since all recent x86 hardware from the past >10 years is capable of the
64-bit x86 extensions, a corresponding 64-bit OS should be used instead.

TCG Plugin support not enabled by default on 32-bit hosts (since 9.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

While it is still possible to enable TCG plugin support for 32-bit
hosts there are a number of potential pitfalls when instrumenting
64-bit guests. The plugin APIs typically pass most addresses as
uint64_t but practices like encoding that address in a host pointer
for passing as user-data will lose data. As most software analysis
benefits from having plenty of host memory it seems reasonable to
encourage users to use 64 bit builds of QEMU for analysis work
whatever targets they are instrumenting.

TCG Plugin support not enabled by default with TCI (since 9.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

While the TCG interpreter can interpret the TCG ops used by plugins it
is going to be so much slower it wouldn't make sense for any serious
instrumentation. Due to implementation differences there will also be
anomalies in things like memory instrumentation.

32-bit host operating systems (since 10.0)
''''''''''''''''''''''''''''''''''''''''''

Keeping 32-bit host support alive is a substantial burden for the
QEMU project.  Thus QEMU will in future drop the support for all
32-bit host systems.

linux-user mode CPUs
--------------------

iwMMXt emulation and the ``pxa`` CPUs (since 10.0)
''''''''''''''''''''''''''''''''''''''''''''''''''

The ``pxa`` CPU family (``pxa250``, ``pxa255``, ``pxa260``,
``pxa261``, ``pxa262``, ``pxa270-a0``, ``pxa270-a1``, ``pxa270``,
``pxa270-b0``, ``pxa270-b1``, ``pxa270-c0``, ``pxa270-c5``) are no
longer used in system emulation, because all the machine types which
used these CPUs were removed in the QEMU 9.2 release. These CPUs can
now only be used in linux-user mode, and to do that you would have to
explicitly select one of these CPUs with the ``-cpu`` command line
option or the ``QEMU_CPU`` environment variable.

We don't believe that anybody is using the iwMMXt emulation, and we do
not have any tests to validate it or any real hardware or similar
known-good implementation to test against. GCC is in the process of
dropping their support for iwMMXt codegen. These CPU types are
therefore deprecated in QEMU, and will be removed in a future release.

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

System emulator machines
------------------------

Arm ``virt`` machine ``dtb-kaslr-seed`` property (since 7.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``dtb-kaslr-seed`` property on the ``virt`` board has been
deprecated; use the new name ``dtb-randomness`` instead. The new name
better reflects the way this property affects all random data within
the device tree blob, not just the ``kaslr-seed`` node.

Big-Endian variants of MicroBlaze ``petalogix-ml605`` and ``xlnx-zynqmp-pmu`` machines (since 9.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Both ``petalogix-ml605`` and ``xlnx-zynqmp-pmu`` were added for little endian
CPUs. Big endian support is not tested.

Mips ``mipssim`` machine (since 10.0)
'''''''''''''''''''''''''''''''''''''

Linux dropped support for this virtual machine type in kernel v3.7, and
there does not seem to be anybody around who is still using this board
in QEMU: Most former MIPS-related people are working on other architectures
in their everyday job nowadays, and we are also not aware of anybody still
using old binaries with this board (i.e. there is also no binary available
online to check that this board did not completely bitrot yet). It is
recommended to use another MIPS machine for future MIPS code development
instead.

RISC-V default machine option (since 10.0)
''''''''''''''''''''''''''''''''''''''''''

RISC-V defines ``spike`` as the default machine if no machine option is
given in the command line.  This happens because ``spike`` is the first
RISC-V machine implemented in QEMU and setting it as default was
convenient at that time.  Now we have 7 riscv64 and 6 riscv32 machines
and having ``spike`` as a default is no longer justified.  This default
will also promote situations where users think they're running ``virt``
(the most used RISC-V machine type in 10.0) when in fact they're
running ``spike``.

Removing the default machine option forces users to always set the machine
they want to use and avoids confusion.  Existing users of the ``spike``
machine must ensure that they're setting the ``spike`` machine in the
command line (``-M spike``).


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

``-device sd-card,spec_version=1`` (since 9.1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

SD physical layer specification v2.00 supersedes the v1.10 one.
v2.00 is the default since QEMU 3.0.0.

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

``gluster`` backend (since 9.2)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

According to https://marc.info/?l=fedora-devel-list&m=171934833215726
the GlusterFS development effectively ended. Unless the development
gains momentum again, the QEMU project will remove the gluster backend
in a future release.


Character device options
''''''''''''''''''''''''

Backend ``memory`` (since 9.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``memory`` is a deprecated synonym for ``ringbuf``.

``reconnect`` (since 9.2)
^^^^^^^^^^^^^^^^^^^^^^^^^

The ``reconnect`` option only allows specifying second granularity timeouts,
which is not enough for all types of use cases, use ``reconnect-ms`` instead.


Net device options
''''''''''''''''''

Stream ``reconnect`` (since 9.2)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``reconnect`` option only allows specifying second granularity timeouts,
which is not enough for all types of use cases, use ``reconnect-ms`` instead.

VFIO device options
'''''''''''''''''''

``-device vfio-calxeda-xgmac`` (since 10.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The vfio-calxeda-xgmac device allows to assign a host Calxeda Highbank
10Gb XGMAC Ethernet controller device ("calxeda,hb-xgmac" compatibility
string) to a guest. Calxeda HW has been ewasted now and there is no point
keeping that device.

``-device vfio-amd-xgbe`` (since 10.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The vfio-amd-xgbe device allows to assign a host AMD 10GbE controller
to a guest ("amd,xgbe-seattle-v1a" compatibility string). AMD "Seattle"
is not supported anymore and there is no point keeping that device.

``-device vfio-platform`` (since 10.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The vfio-platform device allows to assign a host platform device
to a guest in a generic manner. Integrating a new device into
the vfio-platform infrastructure requires some adaptation at
both kernel and qemu level. No such attempt has been done for years
and the conclusion is that vfio-platform has not got any traction.
PCIe passthrough shall be the mainline solution.

CPU device properties
'''''''''''''''''''''

``pcommit`` on x86 (since 9.1)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The PCOMMIT instruction was never included in any physical processor.
It was implemented as a no-op instruction in TCG up to QEMU 9.0, but
only with ``-cpu max`` (which does not guarantee migration compatibility
across versions).

``pmu-num=n`` on RISC-V CPUs (since 8.2)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In order to support more flexible counter configurations this has been replaced
by a ``pmu-mask`` property. If set of counters is continuous then the mask can
be calculated with ``((2 ^ n) - 1) << 3``. The least significant three bits
must be left clear.


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

RISC-V "virt" board "riscv,delegate" DT property (since 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The "riscv,delegate" DT property was added in QEMU 7.0 as part of
the AIA APLIC support.  The property changed name during the review
process in Linux and the correct name ended up being
"riscv,delegation".  Changing the DT property name will break all
available firmwares that are using the current (wrong) name.  The
property is kept as is in 9.1, together with "riscv,delegation", to
give more time for firmware developers to change their code.

Migration
---------

``fd:`` URI when used for file migration (since 9.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``fd:`` URI can currently provide a file descriptor that
references either a socket or a plain file. These are two different
types of migration. In order to reduce ambiguity, the ``fd:`` URI
usage of providing a file descriptor to a plain file has been
deprecated in favor of explicitly using the ``file:`` URI with the
file descriptor being passed as an ``fdset``. Refer to the ``add-fd``
command documentation for details on the ``fdset`` usage.

``zero-blocks`` capability (since 9.2)
''''''''''''''''''''''''''''''''''''''

The ``zero-blocks`` capability was part of the block migration which
doesn't exist anymore since it was removed in QEMU v9.1.
