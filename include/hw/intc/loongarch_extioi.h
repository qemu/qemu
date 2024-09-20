/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 3A5000 ext interrupt controller definitions
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_EXTIOI_H
#define LOONGARCH_EXTIOI_H

#include "hw/intc/loongarch_extioi_common.h"

typedef struct ExtIOICore {
    uint32_t coreisr[EXTIOI_IRQS_GROUP_COUNT];
    DECLARE_BITMAP(sw_isr[LS3A_INTC_IP], EXTIOI_IRQS);
    qemu_irq parent_irq[LS3A_INTC_IP];
} ExtIOICore;

#define TYPE_LOONGARCH_EXTIOI "loongarch.extioi"
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchExtIOI, LOONGARCH_EXTIOI)
struct LoongArchExtIOI {
    SysBusDevice parent_obj;
    uint32_t num_cpu;
    uint32_t features;
    uint32_t status;
    /* hardware state */
    uint32_t nodetype[EXTIOI_IRQS_NODETYPE_COUNT / 2];
    uint32_t bounce[EXTIOI_IRQS_GROUP_COUNT];
    uint32_t isr[EXTIOI_IRQS / 32];
    uint32_t enable[EXTIOI_IRQS / 32];
    uint32_t ipmap[EXTIOI_IRQS_IPMAP_SIZE / 4];
    uint32_t coremap[EXTIOI_IRQS / 4];
    uint32_t sw_pending[EXTIOI_IRQS / 32];
    uint8_t  sw_ipmap[EXTIOI_IRQS_IPMAP_SIZE];
    uint8_t  sw_coremap[EXTIOI_IRQS];
    qemu_irq irq[EXTIOI_IRQS];
    ExtIOICore *cpu;
    MemoryRegion extioi_system_mem;
    MemoryRegion virt_extend;
};
#endif /* LOONGARCH_EXTIOI_H */
