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
#include "qapi/error.h"
#include "sev_i386.h"

bool sev_enabled(void)
{
    return false;
}

uint32_t sev_get_cbit_position(void)
{
    return 0;
}

uint32_t sev_get_reduced_phys_bits(void)
{
    return 0;
}

bool sev_es_enabled(void)
{
    return false;
}

bool sev_add_kernel_loader_hashes(SevKernelLoaderContext *ctx, Error **errp)
{
    g_assert_not_reached();
}
