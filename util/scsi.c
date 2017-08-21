/*
 *  SCSI helpers
 *
 *  Copyright 2017 Red Hat, Inc.
 *
 *  Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "qemu/osdep.h"
#include "scsi/scsi.h"

int scsi_sense_to_errno(int key, int asc, int ascq)
{
    switch (key) {
    case 0x00: /* NO SENSE */
    case 0x01: /* RECOVERED ERROR */
    case 0x06: /* UNIT ATTENTION */
        /* These sense keys are not errors */
        return 0;
    case 0x0b: /* COMMAND ABORTED */
        return ECANCELED;
    case 0x02: /* NOT READY */
    case 0x05: /* ILLEGAL REQUEST */
    case 0x07: /* DATA PROTECTION */
        /* Parse ASCQ */
        break;
    default:
        return EIO;
    }
    switch ((asc << 8) | ascq) {
    case 0x1a00: /* PARAMETER LIST LENGTH ERROR */
    case 0x2000: /* INVALID OPERATION CODE */
    case 0x2400: /* INVALID FIELD IN CDB */
    case 0x2600: /* INVALID FIELD IN PARAMETER LIST */
        return EINVAL;
    case 0x2100: /* LBA OUT OF RANGE */
    case 0x2707: /* SPACE ALLOC FAILED */
        return ENOSPC;
    case 0x2500: /* LOGICAL UNIT NOT SUPPORTED */
        return ENOTSUP;
    case 0x3a00: /* MEDIUM NOT PRESENT */
    case 0x3a01: /* MEDIUM NOT PRESENT TRAY CLOSED */
    case 0x3a02: /* MEDIUM NOT PRESENT TRAY OPEN */
        return ENOMEDIUM;
    case 0x2700: /* WRITE PROTECTED */
        return EACCES;
    case 0x0401: /* NOT READY, IN PROGRESS OF BECOMING READY */
        return EAGAIN;
    case 0x0402: /* NOT READY, INITIALIZING COMMAND REQUIRED */
        return ENOTCONN;
    default:
        return EIO;
    }
}
