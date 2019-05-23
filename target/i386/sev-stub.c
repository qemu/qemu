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
#include "sev_i386.h"

SevInfo *sev_get_info(void)
{
    return NULL;
}

bool sev_enabled(void)
{
    return false;
}

uint64_t sev_get_me_mask(void)
{
    return ~0;
}

uint32_t sev_get_cbit_position(void)
{
    return 0;
}

uint32_t sev_get_reduced_phys_bits(void)
{
    return 0;
}

char *sev_get_launch_measurement(void)
{
    return NULL;
}

SevCapability *sev_get_capabilities(void)
{
    return NULL;
}
