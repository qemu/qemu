Arm CPU Features
================

CPU features are optional features that a CPU of supporting type may
choose to implement or not.  In QEMU, optional CPU features have
corresponding boolean CPU proprieties that, when enabled, indicate
that the feature is implemented, and, conversely, when disabled,
indicate that it is not implemented. An example of an Arm CPU feature
is the Performance Monitoring Unit (PMU).  CPU types such as the
Cortex-A15 and the Cortex-A57, which respectively implement Arm
architecture reference manuals ARMv7-A and ARMv8-A, may both optionally
implement PMUs.  For example, if a user wants to use a Cortex-A15 without
a PMU, then the ``-cpu`` parameter should contain ``pmu=off`` on the QEMU
command line, i.e. ``-cpu cortex-a15,pmu=off``.

As not all CPU types support all optional CPU features, then whether or
not a CPU property exists depends on the CPU type.  For example, CPUs
that implement the ARMv8-A architecture reference manual may optionally
support the AArch32 CPU feature, which may be enabled by disabling the
``aarch64`` CPU property.  A CPU type such as the Cortex-A15, which does
not implement ARMv8-A, will not have the ``aarch64`` CPU property.

QEMU's support may be limited for some CPU features, only partially
supporting the feature or only supporting the feature under certain
configurations.  For example, the ``aarch64`` CPU feature, which, when
disabled, enables the optional AArch32 CPU feature, is only supported
when using the KVM accelerator and when running on a host CPU type that
supports the feature.  While ``aarch64`` currently only works with KVM,
it could work with TCG.  CPU features that are specific to KVM are
prefixed with "kvm-" and are described in "KVM VCPU Features".

CPU Feature Probing
===================

Determining which CPU features are available and functional for a given
CPU type is possible with the ``query-cpu-model-expansion`` QMP command.
Below are some examples where ``scripts/qmp/qmp-shell`` (see the top comment
block in the script for usage) is used to issue the QMP commands.

1. Determine which CPU features are available for the ``max`` CPU type
   (Note, we started QEMU with qemu-system-aarch64, so ``max`` is
   implementing the ARMv8-A reference manual in this case)::

      (QEMU) query-cpu-model-expansion type=full model={"name":"max"}
      { "return": {
        "model": { "name": "max", "props": {
        "sve1664": true, "pmu": true, "sve1792": true, "sve1920": true,
        "sve128": true, "aarch64": true, "sve1024": true, "sve": true,
        "sve640": true, "sve768": true, "sve1408": true, "sve256": true,
        "sve1152": true, "sve512": true, "sve384": true, "sve1536": true,
        "sve896": true, "sve1280": true, "sve2048": true
      }}}}

We see that the ``max`` CPU type has the ``pmu``, ``aarch64``, ``sve``, and many
``sve<N>`` CPU features.  We also see that all the CPU features are
enabled, as they are all ``true``.  (The ``sve<N>`` CPU features are all
optional SVE vector lengths (see "SVE CPU Properties").  While with TCG
all SVE vector lengths can be supported, when KVM is in use it's more
likely that only a few lengths will be supported, if SVE is supported at
all.)

(2) Let's try to disable the PMU::

      (QEMU) query-cpu-model-expansion type=full model={"name":"max","props":{"pmu":false}}
      { "return": {
        "model": { "name": "max", "props": {
        "sve1664": true, "pmu": false, "sve1792": true, "sve1920": true,
        "sve128": true, "aarch64": true, "sve1024": true, "sve": true,
        "sve640": true, "sve768": true, "sve1408": true, "sve256": true,
        "sve1152": true, "sve512": true, "sve384": true, "sve1536": true,
        "sve896": true, "sve1280": true, "sve2048": true
      }}}}

We see it worked, as ``pmu`` is now ``false``.

(3) Let's try to disable ``aarch64``, which enables the AArch32 CPU feature::

      (QEMU) query-cpu-model-expansion type=full model={"name":"max","props":{"aarch64":false}}
      {"error": {
       "class": "GenericError", "desc":
       "'aarch64' feature cannot be disabled unless KVM is enabled and 32-bit EL1 is supported"
      }}

It looks like this feature is limited to a configuration we do not
currently have.

(4) Let's disable ``sve`` and see what happens to all the optional SVE
    vector lengths::

      (QEMU) query-cpu-model-expansion type=full model={"name":"max","props":{"sve":false}}
      { "return": {
        "model": { "name": "max", "props": {
        "sve1664": false, "pmu": true, "sve1792": false, "sve1920": false,
        "sve128": false, "aarch64": true, "sve1024": false, "sve": false,
        "sve640": false, "sve768": false, "sve1408": false, "sve256": false,
        "sve1152": false, "sve512": false, "sve384": false, "sve1536": false,
        "sve896": false, "sve1280": false, "sve2048": false
      }}}}

As expected they are now all ``false``.

(5) Let's try probing CPU features for the Cortex-A15 CPU type::

      (QEMU) query-cpu-model-expansion type=full model={"name":"cortex-a15"}
      {"return": {"model": {"name": "cortex-a15", "props": {"pmu": true}}}}

Only the ``pmu`` CPU feature is available.

A note about CPU feature dependencies
-------------------------------------

It's possible for features to have dependencies on other features. I.e.
it may be possible to change one feature at a time without error, but
when attempting to change all features at once an error could occur
depending on the order they are processed.  It's also possible changing
all at once doesn't generate an error, because a feature's dependencies
are satisfied with other features, but the same feature cannot be changed
independently without error.  For these reasons callers should always
attempt to make their desired changes all at once in order to ensure the
collection is valid.

A note about CPU models and KVM
-------------------------------

Named CPU models generally do not work with KVM.  There are a few cases
that do work, e.g. using the named CPU model ``cortex-a57`` with KVM on a
seattle host, but mostly if KVM is enabled the ``host`` CPU type must be
used.  This means the guest is provided all the same CPU features as the
host CPU type has.  And, for this reason, the ``host`` CPU type should
enable all CPU features that the host has by default.  Indeed it's even
a bit strange to allow disabling CPU features that the host has when using
the ``host`` CPU type, but in the absence of CPU models it's the best we can
do if we want to launch guests without all the host's CPU features enabled.

Enabling KVM also affects the ``query-cpu-model-expansion`` QMP command.  The
affect is not only limited to specific features, as pointed out in example
(3) of "CPU Feature Probing", but also to which CPU types may be expanded.
When KVM is enabled, only the ``max``, ``host``, and current CPU type may be
expanded.  This restriction is necessary as it's not possible to know all
CPU types that may work with KVM, but it does impose a small risk of users
experiencing unexpected errors.  For example on a seattle, as mentioned
above, the ``cortex-a57`` CPU type is also valid when KVM is enabled.
Therefore a user could use the ``host`` CPU type for the current type, but
then attempt to query ``cortex-a57``, however that query will fail with our
restrictions.  This shouldn't be an issue though as management layers and
users have been preferring the ``host`` CPU type for use with KVM for quite
some time.  Additionally, if the KVM-enabled QEMU instance running on a
seattle host is using the ``cortex-a57`` CPU type, then querying ``cortex-a57``
will work.

Using CPU Features
==================

After determining which CPU features are available and supported for a
given CPU type, then they may be selectively enabled or disabled on the
QEMU command line with that CPU type::

  $ qemu-system-aarch64 -M virt -cpu max,pmu=off,sve=on,sve128=on,sve256=on

The example above disables the PMU and enables the first two SVE vector
lengths for the ``max`` CPU type.  Note, the ``sve=on`` isn't actually
necessary, because, as we observed above with our probe of the ``max`` CPU
type, ``sve`` is already on by default.  Also, based on our probe of
defaults, it would seem we need to disable many SVE vector lengths, rather
than only enabling the two we want.  This isn't the case, because, as
disabling many SVE vector lengths would be quite verbose, the ``sve<N>`` CPU
properties have special semantics (see "SVE CPU Property Parsing
Semantics").

KVM VCPU Features
=================

KVM VCPU features are CPU features that are specific to KVM, such as
paravirt features or features that enable CPU virtualization extensions.
The features' CPU properties are only available when KVM is enabled and
are named with the prefix "kvm-".  KVM VCPU features may be probed,
enabled, and disabled in the same way as other CPU features.  Below is
the list of KVM VCPU features and their descriptions.

``kvm-no-adjvtime``
  By default kvm-no-adjvtime is disabled.  This means that by default
  the virtual time adjustment is enabled (vtime is not *not* adjusted).

  When virtual time adjustment is enabled each time the VM transitions
  back to running state the VCPU's virtual counter is updated to
  ensure stopped time is not counted.  This avoids time jumps
  surprising guest OSes and applications, as long as they use the
  virtual counter for timekeeping.  However it has the side effect of
  the virtual and physical counters diverging.  All timekeeping based
  on the virtual counter will appear to lag behind any timekeeping
  that does not subtract VM stopped time.  The guest may resynchronize
  its virtual counter with other time sources as needed.

  Enable kvm-no-adjvtime to disable virtual time adjustment, also
  restoring the legacy (pre-5.0) behavior.

``kvm-steal-time``
  Since v5.2, kvm-steal-time is enabled by default when KVM is
  enabled, the feature is supported, and the guest is 64-bit.

  When kvm-steal-time is enabled a 64-bit guest can account for time
  its CPUs were not running due to the host not scheduling the
  corresponding VCPU threads.  The accounting statistics may influence
  the guest scheduler behavior and/or be exposed to the guest
  userspace.

TCG VCPU Features
=================

TCG VCPU features are CPU features that are specific to TCG.
Below is the list of TCG VCPU features and their descriptions.

``pauth-impdef``
  When ``FEAT_Pauth`` is enabled, either the *impdef* (Implementation
  Defined) algorithm is enabled or the *architected* QARMA algorithm
  is enabled.  By default the impdef algorithm is disabled, and QARMA
  is enabled.

  The architected QARMA algorithm has good cryptographic properties,
  but can be quite slow to emulate.  The impdef algorithm used by QEMU
  is non-cryptographic but significantly faster.

SVE CPU Properties
==================

There are two types of SVE CPU properties: ``sve`` and ``sve<N>``.  The first
is used to enable or disable the entire SVE feature, just as the ``pmu``
CPU property completely enables or disables the PMU.  The second type
is used to enable or disable specific vector lengths, where ``N`` is the
number of bits of the length.  The ``sve<N>`` CPU properties have special
dependencies and constraints, see "SVE CPU Property Dependencies and
Constraints" below.  Additionally, as we want all supported vector lengths
to be enabled by default, then, in order to avoid overly verbose command
lines (command lines full of ``sve<N>=off``, for all ``N`` not wanted), we
provide the parsing semantics listed in "SVE CPU Property Parsing
Semantics".

SVE CPU Property Dependencies and Constraints
---------------------------------------------

  1) At least one vector length must be enabled when ``sve`` is enabled.

  2) If a vector length ``N`` is enabled, then, when KVM is enabled, all
     smaller, host supported vector lengths must also be enabled.  If
     KVM is not enabled, then only all the smaller, power-of-two vector
     lengths must be enabled.  E.g. with KVM if the host supports all
     vector lengths up to 512-bits (128, 256, 384, 512), then if ``sve512``
     is enabled, the 128-bit vector length, 256-bit vector length, and
     384-bit vector length must also be enabled. Without KVM, the 384-bit
     vector length would not be required.

  3) If KVM is enabled then only vector lengths that the host CPU type
     support may be enabled.  If SVE is not supported by the host, then
     no ``sve*`` properties may be enabled.

SVE CPU Property Parsing Semantics
----------------------------------

  1) If SVE is disabled (``sve=off``), then which SVE vector lengths
     are enabled or disabled is irrelevant to the guest, as the entire
     SVE feature is disabled and that disables all vector lengths for
     the guest.  However QEMU will still track any ``sve<N>`` CPU
     properties provided by the user.  If later an ``sve=on`` is provided,
     then the guest will get only the enabled lengths.  If no ``sve=on``
     is provided and there are explicitly enabled vector lengths, then
     an error is generated.

  2) If SVE is enabled (``sve=on``), but no ``sve<N>`` CPU properties are
     provided, then all supported vector lengths are enabled, which when
     KVM is not in use means including the non-power-of-two lengths, and,
     when KVM is in use, it means all vector lengths supported by the host
     processor.

  3) If SVE is enabled, then an error is generated when attempting to
     disable the last enabled vector length (see constraint (1) of "SVE
     CPU Property Dependencies and Constraints").

  4) If one or more vector lengths have been explicitly enabled and at
     least one of the dependency lengths of the maximum enabled length
     has been explicitly disabled, then an error is generated (see
     constraint (2) of "SVE CPU Property Dependencies and Constraints").

  5) When KVM is enabled, if the host does not support SVE, then an error
     is generated when attempting to enable any ``sve*`` properties (see
     constraint (3) of "SVE CPU Property Dependencies and Constraints").

  6) When KVM is enabled, if the host does support SVE, then an error is
     generated when attempting to enable any vector lengths not supported
     by the host (see constraint (3) of "SVE CPU Property Dependencies and
     Constraints").

  7) If one or more ``sve<N>`` CPU properties are set ``off``, but no ``sve<N>``,
     CPU properties are set ``on``, then the specified vector lengths are
     disabled but the default for any unspecified lengths remains enabled.
     When KVM is not enabled, disabling a power-of-two vector length also
     disables all vector lengths larger than the power-of-two length.
     When KVM is enabled, then disabling any supported vector length also
     disables all larger vector lengths (see constraint (2) of "SVE CPU
     Property Dependencies and Constraints").

  8) If one or more ``sve<N>`` CPU properties are set to ``on``, then they
     are enabled and all unspecified lengths default to disabled, except
     for the required lengths per constraint (2) of "SVE CPU Property
     Dependencies and Constraints", which will even be auto-enabled if
     they were not explicitly enabled.

  9) If SVE was disabled (``sve=off``), allowing all vector lengths to be
     explicitly disabled (i.e. avoiding the error specified in (3) of
     "SVE CPU Property Parsing Semantics"), then if later an ``sve=on`` is
     provided an error will be generated.  To avoid this error, one must
     enable at least one vector length prior to enabling SVE.

SVE CPU Property Examples
-------------------------

  1) Disable SVE::

     $ qemu-system-aarch64 -M virt -cpu max,sve=off

  2) Implicitly enable all vector lengths for the ``max`` CPU type::

     $ qemu-system-aarch64 -M virt -cpu max

  3) When KVM is enabled, implicitly enable all host CPU supported vector
     lengths with the ``host`` CPU type::

     $ qemu-system-aarch64 -M virt,accel=kvm -cpu host

  4) Only enable the 128-bit vector length::

     $ qemu-system-aarch64 -M virt -cpu max,sve128=on

  5) Disable the 512-bit vector length and all larger vector lengths,
     since 512 is a power-of-two.  This results in all the smaller,
     uninitialized lengths (128, 256, and 384) defaulting to enabled::

     $ qemu-system-aarch64 -M virt -cpu max,sve512=off

  6) Enable the 128-bit, 256-bit, and 512-bit vector lengths::

     $ qemu-system-aarch64 -M virt -cpu max,sve128=on,sve256=on,sve512=on

  7) The same as (6), but since the 128-bit and 256-bit vector
     lengths are required for the 512-bit vector length to be enabled,
     then allow them to be auto-enabled::

     $ qemu-system-aarch64 -M virt -cpu max,sve512=on

  8) Do the same as (7), but by first disabling SVE and then re-enabling it::

     $ qemu-system-aarch64 -M virt -cpu max,sve=off,sve512=on,sve=on

  9) Force errors regarding the last vector length::

     $ qemu-system-aarch64 -M virt -cpu max,sve128=off
     $ qemu-system-aarch64 -M virt -cpu max,sve=off,sve128=off,sve=on

SVE CPU Property Recommendations
--------------------------------

The examples in "SVE CPU Property Examples" exhibit many ways to select
vector lengths which developers may find useful in order to avoid overly
verbose command lines.  However, the recommended way to select vector
lengths is to explicitly enable each desired length.  Therefore only
example's (1), (4), and (6) exhibit recommended uses of the properties.

SME CPU Property Examples
-------------------------

  1) Disable SME::

     $ qemu-system-aarch64 -M virt -cpu max,sme=off

  2) Implicitly enable all vector lengths for the ``max`` CPU type::

     $ qemu-system-aarch64 -M virt -cpu max

  3) Only enable the 256-bit vector length::

     $ qemu-system-aarch64 -M virt -cpu max,sme256=on

  3) Enable the 256-bit and 1024-bit vector lengths::

     $ qemu-system-aarch64 -M virt -cpu max,sme256=on,sme1024=on

  4) Disable the 512-bit vector length.  This results in all the other
     lengths supported by ``max`` defaulting to enabled
     (128, 256, 1024 and 2048)::

     $ qemu-system-aarch64 -M virt -cpu max,sve512=off

SVE User-mode Default Vector Length Property
--------------------------------------------

For qemu-aarch64, the cpu property ``sve-default-vector-length=N`` is
defined to mirror the Linux kernel parameter file
``/proc/sys/abi/sve_default_vector_length``.  The default length, ``N``,
is in units of bytes and must be between 16 and 8192.
If not specified, the default vector length is 64.

If the default length is larger than the maximum vector length enabled,
the actual vector length will be reduced.  Note that the maximum vector
length supported by QEMU is 256.

If this property is set to ``-1`` then the default vector length
is set to the maximum possible length.

SME CPU Properties
==================

The SME CPU properties are much like the SVE properties: ``sme`` is
used to enable or disable the entire SME feature, and ``sme<N>`` is
used to enable or disable specific vector lengths.  Finally,
``sme_fa64`` is used to enable or disable ``FEAT_SME_FA64``, which
allows execution of the "full a64" instruction set while Streaming
SVE mode is enabled.

SME is not supported by KVM at this time.

At least one vector length must be enabled when ``sme`` is enabled,
and all vector lengths must be powers of 2.  The maximum vector
length supported by qemu is 2048 bits.  Otherwise, there are no
additional constraints on the set of vector lengths supported by SME.

SME User-mode Default Vector Length Property
--------------------------------------------

For qemu-aarch64, the cpu property ``sme-default-vector-length=N`` is
defined to mirror the Linux kernel parameter file
``/proc/sys/abi/sme_default_vector_length``.  The default length, ``N``,
is in units of bytes and must be between 16 and 8192.
If not specified, the default vector length is 32.

As with ``sve-default-vector-length``, if the default length is larger
than the maximum vector length enabled, the actual vector length will
be reduced.  If this property is set to ``-1`` then the default vector
length is set to the maximum possible length.

RME CPU Properties
==================

The status of RME support with QEMU is experimental.  At this time we
only support RME within the CPU proper, not within the SMMU or GIC.
The feature is enabled by the CPU property ``x-rme``, with the ``x-``
prefix present as a reminder of the experimental status, and defaults off.

The method for enabling RME will change in some future QEMU release
without notice or backward compatibility.

RME Level 0 GPT Size Property
-----------------------------

To aid firmware developers in testing different possible CPU
configurations, ``x-l0gptsz=S`` may be used to specify the value
to encode into ``GPCCR_EL3.L0GPTSZ``, a read-only field that
specifies the size of the Level 0 Granule Protection Table.
Legal values for ``S`` are 30, 34, 36, and 39; the default is 30.

As with ``x-rme``, the ``x-l0gptsz`` property may be renamed or
removed in some future QEMU release.
