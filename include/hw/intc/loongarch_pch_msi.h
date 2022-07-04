/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 7A1000 I/O interrupt controller definitions
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#define TYPE_LOONGARCH_PCH_MSI "loongarch_pch_msi"
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchPCHMSI, LOONGARCH_PCH_MSI)

/* Msi irq start start from 64 to 255 */
#define PCH_MSI_IRQ_START   64
#define PCH_MSI_IRQ_END     255
#define PCH_MSI_IRQ_NUM     192

struct LoongArchPCHMSI {
    SysBusDevice parent_obj;
    qemu_irq pch_msi_irq[PCH_MSI_IRQ_NUM];
    MemoryRegion msi_mmio;
    /* irq base passed to upper extioi intc */
    unsigned int irq_base;
};
