/*
 * Renesas Compare-match timer Object
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_RENESAS_CMT_H
#define HW_TIMER_RENESAS_CMT_H

#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_CMT "renesas-cmt"
typedef struct RCMTState RCMTState;
DECLARE_INSTANCE_CHECKER(RCMTState, RCMT,
                         TYPE_RENESAS_CMT)

enum {
    CMT_CH = 2,
    CMT_NR_IRQ = 1 * CMT_CH
};

struct RCMTState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    uint64_t input_freq;
    MemoryRegion memory;

    uint16_t cmstr;
    uint16_t cmcr[CMT_CH];
    uint16_t cmcnt[CMT_CH];
    uint16_t cmcor[CMT_CH];
    int64_t tick[CMT_CH];
    qemu_irq cmi[CMT_CH];
    QEMUTimer timer[CMT_CH];
};

#endif
