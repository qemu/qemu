/*
 * i.MX31 Vectored Interrupt Controller
 *
 * Note this is NOT the PL192 provided by ARM, but
 * a custom implementation by Freescale.
 *
 * Copyright (c) 2008 OKL
 * Copyright (c) 2011 NICTA Pty Ltd
 * Originally written by Hans Jiang
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * TODO: implement vectors.
 */
#ifndef IMX_AVIC_H
#define IMX_AVIC_H

#include "hw/sysbus.h"

#define TYPE_IMX_AVIC "imx.avic"
#define IMX_AVIC(obj) OBJECT_CHECK(IMXAVICState, (obj), TYPE_IMX_AVIC)

#define IMX_AVIC_NUM_IRQS 64

/* Interrupt Control Bits */
#define ABFLAG (1<<25)
#define ABFEN  (1<<24)
#define NIDIS  (1<<22) /* Normal Interrupt disable */
#define FIDIS  (1<<21) /* Fast interrupt disable */
#define NIAD   (1<<20) /* Normal Interrupt Arbiter Rise ARM level */
#define FIAD   (1<<19) /* Fast Interrupt Arbiter Rise ARM level */
#define NM     (1<<18) /* Normal interrupt mode */

#define PRIO_PER_WORD (sizeof(uint32_t) * 8 / 4)
#define PRIO_WORDS (IMX_AVIC_NUM_IRQS/PRIO_PER_WORD)

typedef struct IMXAVICState{
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint64_t pending;
    uint64_t enabled;
    uint64_t is_fiq;
    uint32_t intcntl;
    uint32_t intmask;
    qemu_irq irq;
    qemu_irq fiq;
    uint32_t prio[PRIO_WORDS]; /* Priorities are 4-bits each */
} IMXAVICState;

#endif /* IMX_AVIC_H */
