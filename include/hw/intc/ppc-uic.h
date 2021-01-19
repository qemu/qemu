/*
 * "Universal" Interrupt Controller for PowerPPC 4xx embedded processors
 *
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_INTC_PPC_UIC_H
#define HW_INTC_PPC_UIC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_PPC_UIC "ppc-uic"
OBJECT_DECLARE_SIMPLE_TYPE(PPCUIC, PPC_UIC)

/*
 * QEMU interface:
 * QOM property "cpu": link to the PPC CPU
 *    (no default, must be set)
 * QOM property "dcr-base": base of the bank of DCR registers for the UIC
 *    (default 0x30)
 * QOM property "use-vectors": true if the UIC has vector registers
 *    (default true)
 * unnamed GPIO inputs 0..UIC_MAX_IRQ: input IRQ lines
 * sysbus IRQs:
 *  0 (PPCUIC_OUTPUT_INT): output INT line to the CPU
 *  1 (PPCUIC_OUTPUT_CINT): output CINT line to the CPU
 */

#define UIC_MAX_IRQ 32

/* Symbolic constants for the sysbus IRQ outputs */
enum {
    PPCUIC_OUTPUT_INT = 0,
    PPCUIC_OUTPUT_CINT = 1,
    PPCUIC_OUTPUT_NB,
};

struct PPCUIC {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    qemu_irq output_int;
    qemu_irq output_cint;

    /* properties */
    CPUState *cpu;
    uint32_t dcr_base;
    bool use_vectors;

    uint32_t level;  /* Remembers the state of level-triggered interrupts. */
    uint32_t uicsr;  /* Status register */
    uint32_t uicer;  /* Enable register */
    uint32_t uiccr;  /* Critical register */
    uint32_t uicpr;  /* Polarity register */
    uint32_t uictr;  /* Triggering register */
    uint32_t uicvcr; /* Vector configuration register */
    uint32_t uicvr;
};

#endif
