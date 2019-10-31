================
ARM CPU Features
================

Examples of probing and using ARM CPU features

Introduction
============

CPU features are optional features that a CPU of supporting type may
choose to implement or not.  In QEMU, optional CPU features have
corresponding boolean CPU proprieties that, when enabled, indicate
that the feature is implemented, and, conversely, when disabled,
indicate that it is not implemented. An example of an ARM CPU feature
is the Performance Monitoring Unit (PMU).  CPU types such as the
Cortex-A15 and the Cortex-A57, which respectively implement ARM
architecture reference manuals ARMv7-A and ARMv8-A, may both optionally
implement PMUs.  For example, if a user wants to use a Cortex-A15 without
a PMU, then the `-cpu` parameter should contain `pmu=off` on the QEMU
command line, i.e. `-cpu cortex-a15,pmu=off`.

As not all CPU types support all optional CPU features, then whether or
not a CPU property exists depends on the CPU type.  For example, CPUs
that implement the ARMv8-A architecture reference manual may optionally
support the AArch32 CPU feature, which may be enabled by disabling the
`aarch64` CPU property.  A CPU type such as the Cortex-A15, which does
not implement ARMv8-A, will not have the `aarch64` CPU property.

QEMU's support may be limited for some CPU features, only partially
supporting the feature or only supporting the feature under certain
configurations.  For example, the `aarch64` CPU feature, which, when
disabled, enables the optional AArch32 CPU feature, is only supported
when using the KVM accelerator and when running on a host CPU type that
supports the feature.

CPU Feature Probing
===================

Determining which CPU features are available and functional for a given
CPU type is possible with the `query-cpu-model-expansion` QMP command.
Below are some examples where `scripts/qmp/qmp-shell` (see the top comment
block in the script for usage) is used to issue the QMP commands.

(1) Determine which CPU features are available for the `max` CPU type
    (Note, we started QEMU with qemu-system-aarch64, so `max` is
     implementing the ARMv8-A reference manual in this case)::

      (QEMU) query-cpu-model-expansion type=full model={"name":"max"}
      { "return": {
        "model": { "name": "max", "props": {
        "pmu": true, "aarch64": true
      }}}}

We see that the `max` CPU type has the `pmu` and `aarch64` CPU features.
We also see that the CPU features are enabled, as they are all `true`.

(2) Let's try to disable the PMU::

      (QEMU) query-cpu-model-expansion type=full model={"name":"max","props":{"pmu":false}}
      { "return": {
        "model": { "name": "max", "props": {
        "pmu": false, "aarch64": true
      }}}}

We see it worked, as `pmu` is now `false`.

(3) Let's try to disable `aarch64`, which enables the AArch32 CPU feature::

      (QEMU) query-cpu-model-expansion type=full model={"name":"max","props":{"aarch64":false}}
      {"error": {
       "class": "GenericError", "desc":
       "'aarch64' feature cannot be disabled unless KVM is enabled and 32-bit EL1 is supported"
      }}

It looks like this feature is limited to a configuration we do not
currently have.

(4) Let's try probing CPU features for the Cortex-A15 CPU type::

      (QEMU) query-cpu-model-expansion type=full model={"name":"cortex-a15"}
      {"return": {"model": {"name": "cortex-a15", "props": {"pmu": true}}}}

Only the `pmu` CPU feature is available.

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
that do work, e.g. using the named CPU model `cortex-a57` with KVM on a
seattle host, but mostly if KVM is enabled the `host` CPU type must be
used.  This means the guest is provided all the same CPU features as the
host CPU type has.  And, for this reason, the `host` CPU type should
enable all CPU features that the host has by default.  Indeed it's even
a bit strange to allow disabling CPU features that the host has when using
the `host` CPU type, but in the absence of CPU models it's the best we can
do if we want to launch guests without all the host's CPU features enabled.

Enabling KVM also affects the `query-cpu-model-expansion` QMP command.  The
affect is not only limited to specific features, as pointed out in example
(3) of "CPU Feature Probing", but also to which CPU types may be expanded.
When KVM is enabled, only the `max`, `host`, and current CPU type may be
expanded.  This restriction is necessary as it's not possible to know all
CPU types that may work with KVM, but it does impose a small risk of users
experiencing unexpected errors.  For example on a seattle, as mentioned
above, the `cortex-a57` CPU type is also valid when KVM is enabled.
Therefore a user could use the `host` CPU type for the current type, but
then attempt to query `cortex-a57`, however that query will fail with our
restrictions.  This shouldn't be an issue though as management layers and
users have been preferring the `host` CPU type for use with KVM for quite
some time.  Additionally, if the KVM-enabled QEMU instance running on a
seattle host is using the `cortex-a57` CPU type, then querying `cortex-a57`
will work.

Using CPU Features
==================

After determining which CPU features are available and supported for a
given CPU type, then they may be selectively enabled or disabled on the
QEMU command line with that CPU type::

  $ qemu-system-aarch64 -M virt -cpu max,pmu=off

The example above disables the PMU for the `max` CPU type.

