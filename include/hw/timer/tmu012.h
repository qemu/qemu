/*
 * SuperH Timer
 *
 * Copyright (c) 2007 Magnus Damm
 *
 * This code is licensed under the GPL.
 */

#ifndef HW_TIMER_TMU012_H
#define HW_TIMER_TMU012_H

#include "exec/hwaddr.h"

#define TMU012_FEAT_TOCR   (1 << 0)
#define TMU012_FEAT_3CHAN  (1 << 1)
#define TMU012_FEAT_EXTCLK (1 << 2)

void tmu012_init(MemoryRegion *sysmem, hwaddr base,
                 int feat, uint32_t freq,
                 qemu_irq ch0_irq, qemu_irq ch1_irq,
                 qemu_irq ch2_irq0, qemu_irq ch2_irq1);

#endif
