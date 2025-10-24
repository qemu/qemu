/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch  direct interrupt controller definitions
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/loongarch/virt.h"
#include "system/memory.h"

#define NR_VECTORS     256

#define TYPE_LOONGARCH_DINTC "loongarch_dintc"
OBJECT_DECLARE_TYPE(LoongArchDINTCState, LoongArchDINTCClass, LOONGARCH_DINTC)

typedef struct DINTCCore {
    CPUState *cpu;
    qemu_irq parent_irq;
    uint64_t arch_id;
} DINTCCore;

struct LoongArchDINTCState {
    SysBusDevice parent_obj;
    MemoryRegion dintc_mmio;
    DINTCCore *cpu;
    uint32_t num_cpu;
};

struct LoongArchDINTCClass {
    SysBusDeviceClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
};
