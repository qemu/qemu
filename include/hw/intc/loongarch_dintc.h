/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch  direct interrupt controller definitions
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "hw/loongarch/virt.h"
#include "system/memory.h"
#include "hw/pci-host/ls7a.h"

#define NR_VECTORS     256
#define IRQ_BIT_BASE    5
#define IRQ_BIT_LEN     8
#define CPU_BIT_BASE   13
#define CPU_BIT_LEN     8

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
    int dev_fd;
    uint32_t num_cpu;
    uint64_t msg_addr_base;
    uint64_t msg_addr_size;
};

struct LoongArchDINTCClass {
    SysBusDeviceClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
};

void kvm_dintc_realize(DeviceState *dev, Error **errp);
