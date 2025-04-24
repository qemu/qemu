/*
 * TCG IOMMU translations.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef ACCEL_TCG_IOMMU_H
#define ACCEL_TCG_IOMMU_H

#ifdef CONFIG_USER_ONLY
#error Cannot include accel/tcg/iommu.h from user emulation
#endif

#include "exec/hwaddr.h"
#include "exec/memattrs.h"

/**
 * iotlb_to_section:
 * @cpu: CPU performing the access
 * @index: TCG CPU IOTLB entry
 *
 * Given a TCG CPU IOTLB entry, return the MemoryRegionSection that
 * it refers to. @index will have been initially created and returned
 * by memory_region_section_get_iotlb().
 */
MemoryRegionSection *iotlb_to_section(CPUState *cpu,
                                      hwaddr index, MemTxAttrs attrs);

MemoryRegionSection *address_space_translate_for_iotlb(CPUState *cpu,
                                                       int asidx,
                                                       hwaddr addr,
                                                       hwaddr *xlat,
                                                       hwaddr *plen,
                                                       MemTxAttrs attrs,
                                                       int *prot);

hwaddr memory_region_section_get_iotlb(CPUState *cpu,
                                       MemoryRegionSection *section);

#endif

