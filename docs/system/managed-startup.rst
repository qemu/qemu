Managed start up options
========================

In system mode emulation, it's possible to create a VM in a paused
state using the ``-S`` command line option. In this state the machine
is completely initialized according to command line options and ready
to execute VM code but VCPU threads are not executing any code. The VM
state in this paused state depends on the way QEMU was started. It
could be in:

- initial state (after reset/power on state)
- with direct kernel loading, the initial state could be amended to execute
  code loaded by QEMU in the VM's RAM and with incoming migration
- with incoming migration, initial state will be amended with the migrated
  machine state after migration completes

This paused state is typically used by users to query machine state and/or
additionally configure the machine (by hotplugging devices) in runtime before
allowing VM code to run.

However, at the ``-S`` pause point, it's impossible to configure options
that affect initial VM creation (like: ``-smp``/``-m``/``-numa`` ...) or
cold plug devices. The experimental ``--preconfig`` command line option
allows pausing QEMU before the initial VM creation, in a "preconfig" state,
where additional queries and configuration can be performed via QMP
before moving on to the resulting configuration startup. In the
preconfig state, QEMU only allows a limited set of commands over the
QMP monitor, where the commands do not depend on an initialized
machine, including but not limited to:

- ``qmp_capabilities``
- ``query-qmp-schema``
- ``query-commands``
- ``query-status``
- ``x-exit-preconfig``
