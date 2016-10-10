/*
 * ITS support for ARM GICv3
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Pavel Fedin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_ARM_GICV3_ITS_COMMON_H
#define QEMU_ARM_GICV3_ITS_COMMON_H

#include "hw/sysbus.h"
#include "hw/intc/arm_gicv3_common.h"

#define ITS_CONTROL_SIZE 0x10000
#define ITS_TRANS_SIZE   0x10000
#define ITS_SIZE         (ITS_CONTROL_SIZE + ITS_TRANS_SIZE)

struct GICv3ITSState {
    SysBusDevice parent_obj;

    MemoryRegion iomem_main;
    MemoryRegion iomem_its_cntrl;
    MemoryRegion iomem_its_translation;

    GICv3State *gicv3;

    int dev_fd; /* kvm device fd if backed by kvm vgic support */
    uint64_t gits_translater_gpa;
    bool translater_gpa_known;

    /* Registers */
    uint32_t ctlr;
    uint64_t cbaser;
    uint64_t cwriter;
    uint64_t creadr;
    uint64_t baser[8];

    Error *migration_blocker;
};

typedef struct GICv3ITSState GICv3ITSState;

void gicv3_its_init_mmio(GICv3ITSState *s, const MemoryRegionOps *ops);

#define TYPE_ARM_GICV3_ITS_COMMON "arm-gicv3-its-common"
#define ARM_GICV3_ITS_COMMON(obj) \
     OBJECT_CHECK(GICv3ITSState, (obj), TYPE_ARM_GICV3_ITS_COMMON)
#define ARM_GICV3_ITS_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(GICv3ITSCommonClass, (klass), TYPE_ARM_GICV3_ITS_COMMON)
#define ARM_GICV3_ITS_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(GICv3ITSCommonClass, (obj), TYPE_ARM_GICV3_ITS_COMMON)

struct GICv3ITSCommonClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    int (*send_msi)(GICv3ITSState *s, uint32_t data, uint16_t devid);
    void (*pre_save)(GICv3ITSState *s);
    void (*post_load)(GICv3ITSState *s);
};

typedef struct GICv3ITSCommonClass GICv3ITSCommonClass;

#endif
