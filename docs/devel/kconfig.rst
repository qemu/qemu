.. _kconfig:

================
QEMU and Kconfig
================

QEMU is a very versatile emulator; it can be built for a variety of
targets, where each target can emulate various boards and at the same
time different targets can share large amounts of code.  For example,
a POWER and an x86 board can run the same code to emulate a PCI network
card, even though the boards use different PCI host bridges, and they
can run the same code to emulate a SCSI disk while using different
SCSI adapters.  Arm, s390 and x86 boards can all present a virtio-blk
disk to their guests, but with three different virtio guest interfaces.

Each QEMU target enables a subset of the boards, devices and buses that
are included in QEMU's source code.  As a result, each QEMU executable
only links a small subset of the files that form QEMU's source code;
anything that is not needed to support a particular target is culled.

QEMU uses a simple domain-specific language to describe the dependencies
between components.  This is useful for two reasons:

* new targets and boards can be added without knowing in detail the
  architecture of the hardware emulation subsystems.  Boards only have
  to list the components they need, and the compiled executable will
  include all the required dependencies and all the devices that the
  user can add to that board;

* users can easily build reduced versions of QEMU that support only a subset
  of boards or devices.  For example, by default most targets will include
  all emulated PCI devices that QEMU supports, but the build process is
  configurable and it is easy to drop unnecessary (or otherwise unwanted)
  code to make a leaner binary.

This domain-specific language is based on the Kconfig language that
originated in the Linux kernel, though it was heavily simplified and
the handling of dependencies is stricter in QEMU.

Unlike Linux, there is no user interface to edit the configuration, which
is instead specified in per-target files under the ``default-configs/``
directory of the QEMU source tree.  This is because, unlike Linux,
configuration and dependencies can be treated as a black box when building
QEMU; the default configuration that QEMU ships with should be okay in
almost all cases.

The Kconfig language
--------------------

Kconfig defines configurable components in files named ``hw/*/Kconfig``.
Note that configurable components are _not_ visible in C code as preprocessor
symbols; they are only visible in the Makefile.  Each configurable component
defines a Makefile variable whose name starts with ``CONFIG_``.

All elements have boolean (true/false) type; truth is written as ``y``, while
falsehood is written ``n``.  They are defined in a Kconfig
stanza like the following::

      config ARM_VIRT
         bool
         imply PCI_DEVICES
         imply VFIO_AMD_XGBE
         imply VFIO_XGMAC
         select A15MPCORE
         select ACPI
         select ARM_SMMUV3

The ``config`` keyword introduces a new configuration element.  In the example
above, Makefiles will have access to a variable named ``CONFIG_ARM_VIRT``,
with value ``y`` or ``n`` (respectively for boolean true and false).

Boolean expressions can be used within the language, whenever ``<expr>``
is written in the remainder of this section.  The ``&&``, ``||`` and
``!`` operators respectively denote conjunction (AND), disjunction (OR)
and negation (NOT).

The ``bool`` data type declaration is optional, but it is suggested to
include it for clarity and future-proofing.  After ``bool`` the following
directives can be included:

**dependencies**: ``depends on <expr>``

  This defines a dependency for this configurable element. Dependencies
  evaluate an expression and force the value of the variable to false
  if the expression is false.

**reverse dependencies**: ``select <symbol> [if <expr>]``

  While ``depends on`` can force a symbol to false, reverse dependencies can
  be used to force another symbol to true.  In the following example,
  ``CONFIG_BAZ`` will be true whenever ``CONFIG_FOO`` is true::

    config FOO
      select BAZ

  The optional expression will prevent ``select`` from having any effect
  unless it is true.

  Note that unlike Linux's Kconfig implementation, QEMU will detect
  contradictions between ``depends on`` and ``select`` statements and prevent
  you from building such a configuration.

**default value**: ``default <value> [if <expr>]``

  Default values are assigned to the config symbol if no other value was
  set by the user via ``default-configs/*.mak`` files, and only if
  ``select`` or ``depends on`` directives do not force the value to true
  or false respectively.  ``<value>`` can be ``y`` or ``n``; it cannot
  be an arbitrary Boolean expression.  However, a condition for applying
  the default value can be added with ``if``.

  A configuration element can have any number of default values (usually,
  if more than one default is present, they will have different
  conditions). If multiple default values satisfy their condition,
  only the first defined one is active.

**reverse default** (weak reverse dependency): ``imply <symbol> [if <expr>]``

  This is similar to ``select`` as it applies a lower limit of ``y``
  to another symbol.  However, the lower limit is only a default
  and the "implied" symbol's value may still be set to ``n`` from a
  ``default-configs/*.mak`` files.  The following two examples are
  equivalent::

    config FOO
      bool
      imply BAZ

    config BAZ
      bool
      default y if FOO

  The next section explains where to use ``imply`` or ``default y``.

Guidelines for writing Kconfig files
------------------------------------

Configurable elements in QEMU fall under five broad groups.  Each group
declares its dependencies in different ways:

**subsystems**, of which **buses** are a special case

  Example::

    config SCSI
      bool

  Subsystems always default to false (they have no ``default`` directive)
  and are never visible in ``default-configs/*.mak`` files.  It's
  up to other symbols to ``select`` whatever subsystems they require.

  They sometimes have ``select`` directives to bring in other required
  subsystems or buses.  For example, ``AUX`` (the DisplayPort auxiliary
  channel "bus") selects ``I2C`` because it can act as an I2C master too.

**devices**

  Example::

    config MEGASAS_SCSI_PCI
      bool
      default y if PCI_DEVICES
      depends on PCI
      select SCSI

  Devices are the most complex of the five.  They can have a variety
  of directives that cooperate so that a default configuration includes
  all the devices that can be accessed from QEMU.

  Devices *depend on* the bus that they lie on, for example a PCI
  device would specify ``depends on PCI``.  An MMIO device will likely
  have no ``depends on`` directive.  Devices also *select* the buses
  that the device provides, for example a SCSI adapter would specify
  ``select SCSI``.  Finally, devices are usually ``default y`` if and
  only if they have at least one ``depends on``; the default could be
  conditional on a device group.

  Devices also select any optional subsystem that they use; for example
  a video card might specify ``select EDID`` if it needs to build EDID
  information and publish it to the guest.

**device groups**

  Example::

    config PCI_DEVICES
      bool

  Device groups provide a convenient mechanism to enable/disable many
  devices in one go.  This is useful when a set of devices is likely to
  be enabled/disabled by several targets.  Device groups usually need
  no directive and are not used in the Makefile either; they only appear
  as conditions for ``default y`` directives.

  QEMU currently has two device groups, ``PCI_DEVICES`` and
  ``TEST_DEVICES``.  PCI devices usually have a ``default y if
  PCI_DEVICES`` directive rather than just ``default y``.  This lets
  some boards (notably s390) easily support a subset of PCI devices,
  for example only VFIO (passthrough) and virtio-pci devices.
  ``TEST_DEVICES`` instead is used for devices that are rarely used on
  production virtual machines, but provide useful hooks to test QEMU
  or KVM.

**boards**

  Example::

    config SUN4M
      bool
      imply TCX
      imply CG3
      select CS4231
      select ECCMEMCTL
      select EMPTY_SLOT
      select ESCC
      select ESP
      select FDC
      select SLAVIO
      select LANCE
      select M48T59
      select STP2000

  Boards specify their constituent devices using ``imply`` and ``select``
  directives.  A device should be listed under ``select`` if the board
  cannot be started at all without it.  It should be listed under
  ``imply`` if (depending on the QEMU command line) the board may or
  may not be started without it.  Boards also default to false; they are
  enabled by the ``default-configs/*.mak`` for the target they apply to.

**internal elements**

  Example::

    config ECCMEMCTL
      bool
      select ECC

  Internal elements group code that is useful in several boards or
  devices.  They are usually enabled with ``select`` and in turn select
  other elements; they are never visible in ``default-configs/*.mak``
  files, and often not even in the Makefile.

Writing and modifying default configurations
--------------------------------------------

In addition to the Kconfig files under hw/, each target also includes
a file called ``default-configs/TARGETNAME-softmmu.mak``.  These files
initialize some Kconfig variables to non-default values and provide the
starting point to turn on devices and subsystems.

A file in ``default-configs/`` looks like the following example::

    # Default configuration for alpha-softmmu

    # Uncomment the following lines to disable these optional devices:
    #
    #CONFIG_PCI_DEVICES=n
    #CONFIG_TEST_DEVICES=n

    # Boards:
    #
    CONFIG_DP264=y

The first part, consisting of commented-out ``=n`` assignments, tells
the user which devices or device groups are implied by the boards.
The second part, consisting of ``=y`` assignments, tells the user which
boards are supported by the target.  The user will typically modify
the default configuration by uncommenting lines in the first group,
or commenting out lines in the second group.

It is also possible to run QEMU's configure script with the
``--without-default-devices`` option.  When this is done, everything defaults
to ``n`` unless it is ``select``ed or explicitly switched on in the
``.mak`` files.  In other words, ``default`` and ``imply`` directives
are disabled.  When QEMU is built with this option, the user will probably
want to change some lines in the first group, for example like this::

   CONFIG_PCI_DEVICES=y
   #CONFIG_TEST_DEVICES=n

and/or pick a subset of the devices in those device groups.  Right now
there is no single place that lists all the optional devices for
``CONFIG_PCI_DEVICES`` and ``CONFIG_TEST_DEVICES``.  In the future,
we expect that ``.mak`` files will be automatically generated, so that
they will include all these symbols and some help text on what they do.

``Kconfig.host``
----------------

In some special cases, a configurable element depends on host features
that are detected by QEMU's configure or ``meson.build`` scripts; for
example some devices depend on the availability of KVM or on the presence
of a library on the host.

These symbols should be listed in ``Kconfig.host`` like this::

    config TPM
      bool

and also listed as follows in the top-level meson.build's host_kconfig
variable::

    host_kconfig = \
      ('CONFIG_TPM' in config_host ? ['CONFIG_TPM=y'] : []) + \
      ('CONFIG_SPICE' in config_host ? ['CONFIG_SPICE=y'] : []) + \
      ('CONFIG_IVSHMEM' in config_host ? ['CONFIG_IVSHMEM=y'] : []) + \
      ...
