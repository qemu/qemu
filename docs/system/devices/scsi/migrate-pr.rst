..
    SPDX-License-Identifier: GPL-2.0-or-later

.. _scsi_migrate_pr:

SCSI Persistent Reservation Live Migration
==========================================

This document explains how to live migrate SCSI Persistent Reservations.

The ``scsi-block`` device migrates SCSI Persistent Reservations when the
``migrate-pr=on`` parameter is given. Migration is enabled by default in
versioned machine types since QEMU 11.0. It is disabled by default on older
machine types and needs to be explicitly enabled with ``--device
scsi-block,migrate-pr=on,...``.

When migration is enabled, QEMU snoops PERSISTENT RESERVATION OUT commands and
tracks the reservation key registered by the guest as well as reservations that
the guest acquires. This information is migrated along with the guest and the
destination QEMU submits a PERSISTENT RESERVATION OUT command with the PREEMPT
service action to atomically transfer the reservation to the destination before
the guest starts running on the destination.

The following persistent reservation capabilities reported by the PERSISTENT
RESERVATION IN command with the REPORT CAPABILITIES service action are masked
from the guest by QEMU when migration is enabled:

 * Specify Initiator Ports Capable (SIP_C)
 * All Target Ports Capable (ATC_C)

When migration is disabled, the ``scsi-block`` device is live migrated but
reservations remain in place on the source. Usually this is not the intended
behavior unless there is another mechanism to update reservations during
migration. The PERSISTENT RESERVATION IN command also does not mask
capabilities reported to the guest when migration is disabled.

Limitations
-----------

QEMU does not remember snooped reservation details across restart, so software
inside the guest must acquire the reservation after boot in order for live
migration to work. Similarly, if the reservation is acquired outside the guest
then it will not live migrate along with the guest.

Snooping only considers the PERSISTENT RESERVATION OUT commands from the guest
and does not track reservation changes made by other SCSI initiators. QEMU's
snooped reservation details can become stale if another SCSI initiator
makes changes to the reservation.

Guests running on the same host share a single SCSI initiator identity unless
Fibre Channel N_Port ID Virtualization is configured. As a consequence,
multiple guests on the same hosts may observe unexpected behavior if they use
the same physical LUN. From the LUN's perspective all guests are the same
initiator and there is no way to distinguish between guests.
