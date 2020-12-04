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
#include "qemu-common.h"
#include "sysemu/sev.h"

int sev_kvm_init(ConfidentialGuestSupport *cgs)
{
    /* SEV can't be selected if it's not compiled */
    g_assert_not_reached();
}
