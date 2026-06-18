/*
 * TCG IOMMU translations.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef ACCEL_TCG_IOMMU_H
#define ACCEL_TCG_IOMMU_H

#ifndef CONFIG_TCG
#error Can only include this header with TCG
#endif

#ifdef CONFIG_USER_ONLY
#error Cannot include accel/tcg/iommu.h from user emulation
#endif

#include "exec/hwaddr.h"
#include "exec/memattrs.h"

void tcg_iommu_init_notifier_list(CPUState *cpu);
void tcg_iommu_free_notifier_list(CPUState *cpu);

MemoryRegionSection *address_space_translate_for_iotlb(CPUState *cpu,
                                                       int asidx,
                                                       hwaddr addr,
                                                       hwaddr *xlat,
                                                       hwaddr *plen,
                                                       MemTxAttrs attrs,
                                                       int *prot);

#endif

