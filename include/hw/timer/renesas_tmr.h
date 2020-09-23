/*
 * Renesas 8bit timer Object
 *
 * Copyright (c) 2018 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_RENESAS_TMR_H
#define HW_TIMER_RENESAS_TMR_H

#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_TMR "renesas-tmr"
typedef struct RTMRState RTMRState;
DECLARE_INSTANCE_CHECKER(RTMRState, RTMR,
                         TYPE_RENESAS_TMR)

enum timer_event {
    cmia = 0,
    cmib = 1,
    ovi = 2,
    none = 3,
    TMR_NR_EVENTS = 4
};

enum {
    TMR_CH = 2,
    TMR_NR_IRQ = 3 * TMR_CH
};

struct RTMRState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    uint64_t input_freq;
    MemoryRegion memory;

    int64_t tick;
    uint8_t tcnt[TMR_CH];
    uint8_t tcora[TMR_CH];
    uint8_t tcorb[TMR_CH];
    uint8_t tcr[TMR_CH];
    uint8_t tccr[TMR_CH];
    uint8_t tcor[TMR_CH];
    uint8_t tcsr[TMR_CH];
    int64_t div_round[TMR_CH];
    uint8_t next[TMR_CH];
    qemu_irq cmia[TMR_CH];
    qemu_irq cmib[TMR_CH];
    qemu_irq ovi[TMR_CH];
    QEMUTimer timer[TMR_CH];
};

#endif
