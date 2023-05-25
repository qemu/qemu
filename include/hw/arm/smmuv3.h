/*
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_SMMUV3_H
#define HW_ARM_SMMUV3_H

#include "hw/arm/smmu-common.h"
#include "qom/object.h"

#define TYPE_SMMUV3_IOMMU_MEMORY_REGION "smmuv3-iommu-memory-region"

typedef struct SMMUQueue {
     uint64_t base; /* base register */
     uint32_t prod;
     uint32_t cons;
     uint8_t entry_size;
     uint8_t log2size;
} SMMUQueue;

struct SMMUv3State {
    SMMUState     smmu_state;

    uint32_t features;
    uint8_t sid_size;
    uint8_t sid_split;

    uint32_t idr[6];
    uint32_t iidr;
    uint32_t aidr;
    uint32_t cr[3];
    uint32_t cr0ack;
    uint32_t statusr;
    uint32_t gbpa;
    uint32_t irq_ctrl;
    uint32_t gerror;
    uint32_t gerrorn;
    uint64_t gerror_irq_cfg0;
    uint32_t gerror_irq_cfg1;
    uint32_t gerror_irq_cfg2;
    uint64_t strtab_base;
    uint32_t strtab_base_cfg;
    uint64_t eventq_irq_cfg0;
    uint32_t eventq_irq_cfg1;
    uint32_t eventq_irq_cfg2;

    SMMUQueue eventq, cmdq;

    qemu_irq     irq[4];
    QemuMutex mutex;
    char *stage;
};

typedef enum {
    SMMU_IRQ_EVTQ,
    SMMU_IRQ_PRIQ,
    SMMU_IRQ_CMD_SYNC,
    SMMU_IRQ_GERROR,
} SMMUIrq;

struct SMMUv3Class {
    /*< private >*/
    SMMUBaseClass smmu_base_class;
    /*< public >*/

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

#define TYPE_ARM_SMMUV3   "arm-smmuv3"
OBJECT_DECLARE_TYPE(SMMUv3State, SMMUv3Class, ARM_SMMUV3)

#define STAGE1_SUPPORTED(s)      FIELD_EX32(s->idr[0], IDR0, S1P)
#define STAGE2_SUPPORTED(s)      FIELD_EX32(s->idr[0], IDR0, S2P)

#endif
