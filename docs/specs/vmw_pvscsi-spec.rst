==============================
VMWare PVSCSI Device Interface
==============================

..
   Created by Dmitry Fleytman (dmitry@daynix.com), Daynix Computing LTD.

This document describes the VMWare PVSCSI device interface specification,
based on the source code of the PVSCSI Linux driver from kernel 3.0.4.

Overview
========

The interface is based on a memory area shared between hypervisor and VM.
The memory area is obtained by driver as a device IO memory resource of
``PVSCSI_MEM_SPACE_SIZE`` length.
The shared memory consists of a registers area and a rings area.
The registers area is used to raise hypervisor interrupts and issue device
commands. The rings area is used to transfer data descriptors and SCSI
commands from VM to hypervisor and to transfer messages produced by
hypervisor to VM. Data itself is transferred via virtual scatter-gather DMA.

PVSCSI Device Registers
=======================

The length of the registers area is 1 page
(``PVSCSI_MEM_SPACE_COMMAND_NUM_PAGES``).  The structure of the
registers area is described by the ``PVSCSIRegOffset`` enum.  There
are registers to issue device commands (with optional short data),
issue device interrupts, and control interrupt masking.

PVSCSI Device Rings
===================

There are three rings in shared memory:

Request ring (``struct PVSCSIRingReqDesc *req_ring``)
    ring for OS to device requests

Completion ring (``struct PVSCSIRingCmpDesc *cmp_ring``)
    ring for device request completions

Message ring (``struct PVSCSIRingMsgDesc *msg_ring``)
    ring for messages from device. This ring is optional and the
    guest might not configure it.

There is a control area (``struct PVSCSIRingsState *rings_state``)
used to control rings operation.

PVSCSI Device to Host Interrupts
================================

The following interrupt types are supported by the PVSCSI device:

Completion interrupts (completion ring notifications):

- ``PVSCSI_INTR_CMPL_0``
- ``PVSCSI_INTR_CMPL_1``

Message interrupts (message ring notifications):

- ``PVSCSI_INTR_MSG_0``
- ``PVSCSI_INTR_MSG_1``

Interrupts are controlled via the ``PVSCSI_REG_OFFSET_INTR_MASK``
register.  If a bit is set it means the interrupt is enabled, and if
it is clear then the interrupt is disabled.

The interrupt modes supported are legacy, MSI and MSI-X.
In the case of legacy interrupts, the ``PVSCSI_REG_OFFSET_INTR_STATUS``
register is used to check which interrupt has arrived.  Interrupts are
acknowledged when the corresponding bit is written to the interrupt
status register.

PVSCSI Device Operation Sequences
=================================

Startup sequence
----------------

a. Issue ``PVSCSI_CMD_ADAPTER_RESET`` command
b. Windows driver reads interrupt status register here
c. Issue ``PVSCSI_CMD_SETUP_MSG_RING`` command with no additional data,
   check status and disable device messages if error returned
   (Omitted if device messages disabled by driver configuration)
d. Issue ``PVSCSI_CMD_SETUP_RINGS`` command, provide rings configuration
   as ``struct PVSCSICmdDescSetupRings``
e. Issue ``PVSCSI_CMD_SETUP_MSG_RING`` command again, provide
   rings configuration as ``struct PVSCSICmdDescSetupMsgRing``
f. Unmask completion and message (if device messages enabled) interrupts

Shutdown sequence
-----------------

a. Mask interrupts
b. Flush request ring using ``PVSCSI_REG_OFFSET_KICK_NON_RW_IO``
c. Issue ``PVSCSI_CMD_ADAPTER_RESET`` command

Send request
------------

a. Fill next free request ring descriptor
b. Issue ``PVSCSI_REG_OFFSET_KICK_RW_IO`` for R/W operations
   or ``PVSCSI_REG_OFFSET_KICK_NON_RW_IO`` for other operations

Abort command
-------------

a. Issue ``PVSCSI_CMD_ABORT_CMD`` command

Request completion processing
-----------------------------

a. Upon completion interrupt arrival process completion
   and message (if enabled) rings
