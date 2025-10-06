==============
UEFI variables
==============

Guest UEFI variable management
==============================

The traditional approach for UEFI Variable storage in qemu guests is
to work as close as possible to physical hardware.  That means
providing pflash as storage and leaving the management of variables
and flash to the guest.

Secure boot support comes with the requirement that the UEFI variable
storage must be protected against direct access by the OS.  All update
requests must pass the sanity checks.  (Parts of) the firmware must
run with a higher privilege level than the OS so this can be enforced
by the firmware.  On x86 this has been implemented using System
Management Mode (SMM) in qemu and kvm, which again is the same
approach taken by physical hardware.  Only privileged code running in
SMM mode is allowed to access flash storage.

Communication with the firmware code running in SMM mode works by
serializing the requests to a shared buffer, then trapping into SMM
mode via SMI.  The SMM code processes the request, stores the reply in
the same buffer and returns.

Host UEFI variable service
==========================

Instead of running the privileged code inside the guest we can run it
on the host.  The serialization protocol can be reused.  The
communication with the host uses a virtual device, which essentially
configures the shared buffer location and size, and traps to the host
to process the requests.

The ``uefi-vars`` device implements the UEFI virtual device.  It comes
in ``uefi-vars-x64`` and ``uefi-vars-sysbus`` flavours.  The device
reimplements the handlers needed, specifically
``EfiSmmVariableProtocol`` and ``VarCheckPolicyLibMmiHandler``.  It
also consumes events (``EfiEndOfDxeEventGroup``,
``EfiEventReadyToBoot`` and ``EfiEventExitBootServices``).

The advantage of the approach is that we do not need a special
privilege level for the firmware to protect itself, i.e. it does not
depend on SMM emulation on x64, which allows the removal of a bunch of
complex code for SMM emulation from the linux kernel
(CONFIG_KVM_SMM=n).  It also allows support for secure boot on arm
without implementing secure world (el3) emulation in kvm.

Of course there are also downsides.  The added device increases the
attack surface of the host, and we are adding some code duplication
because we have to reimplement some edk2 functionality in qemu.

usage on x86_64
---------------

.. code::

   qemu-system-x86_64 \
      -device uefi-vars-x64,jsonfile=/path/to/vars.json

usage on aarch64
----------------

.. code::

   qemu-system-aarch64 -M virt \
      -device uefi-vars-sysbus,jsonfile=/path/to/vars.json
