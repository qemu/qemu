/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch 3A5000 ext interrupt controller definitions
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_EXTIOI_COMMON_H
#define LOONGARCH_EXTIOI_COMMON_H

#include "hw/sysbus.h"
#include "hw/loongarch/virt.h"

#define LS3A_INTC_IP                 8
#define EXTIOI_IRQS                  (256)
#define EXTIOI_IRQS_BITMAP_SIZE      (256 / 8)
/* irq from EXTIOI is routed to no more than 4 cpus */
#define EXTIOI_CPUS                  (4)
/* map to ipnum per 32 irqs */
#define EXTIOI_IRQS_IPMAP_SIZE       (256 / 32)
#define EXTIOI_IRQS_COREMAP_SIZE     256
#define EXTIOI_IRQS_NODETYPE_COUNT   16
#define EXTIOI_IRQS_GROUP_COUNT      8

#define APIC_OFFSET                  0x400
#define APIC_BASE                    (0x1000ULL + APIC_OFFSET)
#define EXTIOI_NODETYPE_START        (0x4a0 - APIC_OFFSET)
#define EXTIOI_NODETYPE_END          (0x4c0 - APIC_OFFSET)
#define EXTIOI_IPMAP_START           (0x4c0 - APIC_OFFSET)
#define EXTIOI_IPMAP_END             (0x4c8 - APIC_OFFSET)
#define EXTIOI_ENABLE_START          (0x600 - APIC_OFFSET)
#define EXTIOI_ENABLE_END            (0x620 - APIC_OFFSET)
#define EXTIOI_BOUNCE_START          (0x680 - APIC_OFFSET)
#define EXTIOI_BOUNCE_END            (0x6a0 - APIC_OFFSET)
#define EXTIOI_ISR_START             (0x700 - APIC_OFFSET)
#define EXTIOI_ISR_END               (0x720 - APIC_OFFSET)
#define EXTIOI_COREISR_START         (0x800 - APIC_OFFSET)
#define EXTIOI_COREISR_END           (0xB20 - APIC_OFFSET)
#define EXTIOI_COREMAP_START         (0xC00 - APIC_OFFSET)
#define EXTIOI_COREMAP_END           (0xD00 - APIC_OFFSET)
#define EXTIOI_SIZE                  0x800

#define EXTIOI_VIRT_BASE             (0x40000000)
#define EXTIOI_VIRT_SIZE             (0x1000)
#define EXTIOI_VIRT_FEATURES         (0x0)
#define  EXTIOI_HAS_VIRT_EXTENSION   (0)
#define  EXTIOI_HAS_ENABLE_OPTION    (1)
#define  EXTIOI_HAS_INT_ENCODE       (2)
#define  EXTIOI_HAS_CPU_ENCODE       (3)
#define  EXTIOI_VIRT_HAS_FEATURES    (BIT(EXTIOI_HAS_VIRT_EXTENSION)  \
                                      | BIT(EXTIOI_HAS_ENABLE_OPTION) \
                                      | BIT(EXTIOI_HAS_CPU_ENCODE))
#define EXTIOI_VIRT_CONFIG           (0x4)
#define  EXTIOI_ENABLE               (1)
#define  EXTIOI_ENABLE_INT_ENCODE    (2)
#define  EXTIOI_ENABLE_CPU_ENCODE    (3)
#define EXTIOI_VIRT_COREMAP_START    (0x40)
#define EXTIOI_VIRT_COREMAP_END      (0x240)
#endif /* LOONGARCH_EXTIOI_H */
