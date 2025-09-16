/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors:
 *  Magnus Kulke      <magnuskulke@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "system/mshv.h"
#include "system/mshv_int.h"

void mshv_set_phys_mem(MshvMemoryListener *mml, MemoryRegionSection *section,
                       bool add)
{
    error_report("unimplemented");
    abort();
}

