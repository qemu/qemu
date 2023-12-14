/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU Motorola 680x0 IRQ Controller
 *
 * (c) 2020 Laurent Vivier <laurent@vivier.eu>
 *
 */

#ifndef M68K_IRQC_H
#define M68K_IRQC_H

#include "hw/sysbus.h"

#define TYPE_M68K_IRQC "m68k-irq-controller"
#define M68K_IRQC(obj) OBJECT_CHECK(M68KIRQCState, (obj), \
                                    TYPE_M68K_IRQC)

#define M68K_IRQC_AUTOVECTOR_BASE 25

enum {
    M68K_IRQC_LEVEL_1 = 0,
    M68K_IRQC_LEVEL_2,
    M68K_IRQC_LEVEL_3,
    M68K_IRQC_LEVEL_4,
    M68K_IRQC_LEVEL_5,
    M68K_IRQC_LEVEL_6,
    M68K_IRQC_LEVEL_7,
};
#define M68K_IRQC_LEVEL_NUM (M68K_IRQC_LEVEL_7 - M68K_IRQC_LEVEL_1 + 1)

typedef struct M68KIRQCState {
    SysBusDevice parent_obj;

    uint8_t ipr;
    ArchCPU *cpu;

    /* statistics */
    uint64_t stats_irq_count[M68K_IRQC_LEVEL_NUM];
} M68KIRQCState;

#endif
