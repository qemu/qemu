.. SPDX-License-Identifier: GPL-2.0-or-later

VMLAUNCHUPDATE Interface Specification
######################################

Introduction
************

``VmLaunchUpdate`` is an extension to ``fw-cfg`` that allows guests to replace
boot state in their virtual machine using IGVM file container. Through a combination
of this ``fw-cfg`` hypervisor interface, an IGVM file containing specific directives
and with hypervisor stack knowledge, guests can deterministically replace the launch
state for guests. This is useful for environments like SEV-SNP where the
launch payload becomes the launch digest. Guests can use vm-launch-update device to
provide a measured, full guest payload (BIOS image, kernel, initramfs, kernel
command line) to the virtual machine which enables them to easily reason about
integrity of the resulting system.
It is also to be noted that this mechanism currently works only when the guest was
already started with an IGVM file defining its initial launch state. Subsequent
guest resets will use the launch state as defined in the guest provided IGVM file,
not the file with which the guest was initially started. If the guest was not started
with IGVM, writing a new bundle through the ``fw-cfg`` interface has no effect.

For more information, please see the `KVM Forum 2024 presentation <KVMFORUM_>`__
about this work.


.. _KVMFORUM: https://www.youtube.com/watch?v=VCMBxU6tAto

Base Requirements
*****************

#. **fw-cfg**:
     The target system must provide a ``fw-cfg`` interface. For x86 based
     environments, this ``fw-cfg`` interface must be accessible through PIO ports
     0x510 and 0x511. The ``fw-cfg`` interface does not need to be announced as part
     of system device tables such as DSDT. The ``fw-cfg`` interface must support the
     DMA interface. It may only support the DMA interface for write operations.

#. **IGVM support**:
     The hypervisor must provide support for parsing and executing the IGVM file bundle.

#. **Confidential guests**:
     For confidential guests, the hypervisor must support guest reset. Otherwise, the new
     boot state provided through IGVM will not be applied.

The Fw-cfg File
***************

Guests drive vmlaunchupdate through special ``fw-cfg`` files that control its flow
followed by a standard system reset operation. When the ``vm-launch-update`` device
is available, it provides the following ``fw-cfg`` file:

* ``etc/vmlaunchupdate`` - It exposes a structure of the following type, all in
  little-endian format:

.. code-block:: c
 :linenos:

 typedef struct {
        uint16_t version;
        uint16_t status;

        uint32_t _padding;

        uint64_t capabilities;
        uint64_t control;

        uint64_t fw_image_addr;
        uint64_t fw_image_size;

        uint64_t opaque_addr;
        uint64_t opaque_size;

 } VMLaunchUpdate;


Currently, the ``version`` number (line 2 above) is initialized to the value ``1``.
Only IGVM files are supported at present. The ``capabilities`` (line 7) and ``control`` (line 8) both support
the following single value:

* ``VM_LAUNCHUPDATE_FORMAT_IGVM``

  This value is used by the hypervisor to indicate that only IGVM container files are supported.
  This is set  as a part of ``capabilities`` parameter (line 7) in the above structure. This same value
  is passed by the guest to the hypervisor in the ``control`` parameter (line 8) in the above structure
  to indicate that the guest passed IGVM file in memory to the hypervisor. The starting guest physical
  address of the IGVM file in memory is specified in ``fw_image_addr`` and it's length is specified in
  ``fw_image_size`` by the guest. If any other value is passed by the guest in the ``control`` parameter,
  the write is ignored by the hypervisor.

Following  ``control`` parameters are supported:

* ``VM_LAUNCHUPDATE_CTL_DISABLE``

  This value is set in the ``control`` parameter by the guest in order to disable this ``fw-cfg``
  hypervisor interface from further updating the guest launch state with a new IGVM file.

* ``VM_LAUNCHUPDATE_CTL_HOST_IGVM``

  This value is set in the ``control`` parameter by the guest in order to send request to the
  hypervisor to initialize the guest using the original host provided IGVM file.
  It is useful if the guest wanted to update the UKIs present in the ESP and upon
  reset, use one of the updated UKIs present there. If the guest passed addresses in memory
  where its own IGVM file is loaded (see below) while also setting this control value, the next
  reset will load the guest provided IGVM file and a subsequent second reset will restore the original
  host IGVM. If the guest did not provide any addresses of its own IGVM (the address values are
  cleared) while setting this control parameter, the immediate next guest reset will load the
  original host provided IGVM file.

  The combination of the above two ctl interfaces work as
  follows:

  A) ``CTL_HOST_IGVM`` = off ``CTL_DISABLE`` = off

    Supplied IGVM file replaces the firmware permanently.  Updating the
    firmware again is possible.

  B) ``CTL_HOST_IGVM`` = off ``CTL_DISABLE`` = on

    Supplied IGVM file replaces the firmware permanently.  Updating the
    firmware again is not possible.

  C) ``CTL_HOST_IGVM`` = on ``CTL_DISABLE`` = off

    Supplied IGVM file replaces the firmware for one reset.  Resetting
    again will switch back to the original firmware.  Updating the
    firmware again is possible.

  D) ``CTL_HOST_IGVM`` = on ``CTL_DISABLE`` = on

    Supplied IGVM file replaces the firmware for one reset.  Resetting
    again will switch back to the original firmware.  Updating the
    firmware again is NOT possible.

``fw_image_addr`` (line 10) is the base guest physical address of the guest memory where the IGVM file of size
``fw_image_size`` (line 11) is loaded. ``opaque_addr`` (line 13) and ``opaque_size`` (line 14) are used by
the guest for passing data across resets. The contents of this guest memory are preserved across the
reset. For confidential guests, this memory region must come from guest shared unencrypted memory.

``status`` (line 3) is written by the hypervisor and it indicates the result of the IGVM loading operation.
A success indicates status code 0. Otherwise a non-zero status code indicates failure. The nature of the
failure is indicated by the value of the code.

Triggering the Launch State Update using IGVM
*********************************************

To initiate the launch update process, the guest issues a standard system reset
operation through any of the means implemented by the machine model.

On a write to the ``etc/vmlaunchupdate`` interface, the hypervisor evaluates whether this
hypervisor interface is disabled. If it is, it ignores any writes to this ``fw-cfg`` file
by the guest. No updates to initial launch state is performed.

If the hypervisor interface is enabled, upon write to the ``etc/vmlaunchupdate`` interface,
the hypervisor parses the IGVM file bundle passed to it in memory, with starting guest physical
address at ``fw_image_addr`` and length ``fw_image_size``. If parsing is successful, it creates
a context handle to the IGVM file. If parsing and context loading is successful and there are no
errors, ``fw_image_addr`` and ``fw_image_size`` are cleared. The guest can check this in order
to determine if the IGVM was successfully parsed and the new context was loaded. If not, the
guest can throw error and abort rebooting to new IGVM boot state. Alternatively, the guest can
also check the ``status`` code from the ``fw-cfg`` file. A status code of 0 indicates success
of the operation. Non-zero status code indicates failure. Exact nature of the failure is
indicated by the value of the code. Currently, only two error values are supported:

* ``VM_LAUNCHUPDATE_LOAD_FAIL`` - defined as value 1 and is set when loading of the IGVM file failed.
* ``VM_LAUNCHUPDATE_NOT_IGVM_INIT`` - defined as value 2 and is set when the guest was not started with
  IGVM file.

Upon guest reset, the hypervisor executes the IGVM bundle using
the context handle, setting the initial launch state of the guest accordingly.
If an invalid IGVM file is passed, parsing the file fails and the hypervisor ignores it
when ``fw-cfg`` files are written. In this case, the initial launch state
is not modified. If invalid addresses are passed, the hypervisor ignores them as well and no
new launch state is set.

The launch state update mechanism works both for confidential and non-confidential
guests. In confidential guests, as a part of the reset operation, all existing
guest shared memory (shared with the hypervisor) as well as the guest memory region
starting with ``opaque_addr`` and length ``opaque_size`` are preserved.
The reset causes recreation of the VM context which triggers a fresh
measurement of the replaced BIOS region and reset CPU state.

For non-confidential guests, there is no concept of guest private memory and all the existing
guest memory is preserved (this is the default behaviour today - QEMU does not reset/clear
guest memory upon reset).

In both confidential and non-confidential cases, CPU and device state are reset to
the reset states specified in IGVM. In confidential environments, the guest
always resumes operation in the highest privileged mode available to it (VMPL0 in SEV-SNP).

Closing Remarks
***************
The exact content of the memory region specified by starting address ``opaque_addr``
and length ``opaque_size`` is guest specific and is hypervisor agnostic. The hypervisor does
not care about the contents of this memory region. Therefore, it is not included in this
specification. As of writing this document, TDX guests on QEMU does not support IGVM.
Therefore, this mechanism cannot be used to change launch state of TDX guests.
