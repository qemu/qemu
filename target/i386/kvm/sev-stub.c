/*
 * QEMU SEV stub
 *
 * Copyright Advanced Micro Devices 2018
 *
 * Authors:
 *      Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sev.h"

int sev_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    /* If we get here, cgs must be some non-SEV thing */
    return 0;
}
