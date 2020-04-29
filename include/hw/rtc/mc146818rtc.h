/*
 * QEMU MC146818 RTC emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_RTC_MC146818RTC_H
#define HW_RTC_MC146818RTC_H

#include "qapi/qapi-types-misc.h"
#include "qemu/queue.h"
#include "qemu/timer.h"
#include "hw/isa/isa.h"

#define TYPE_MC146818_RTC "mc146818rtc"
#define MC146818_RTC(obj) OBJECT_CHECK(RTCState, (obj), TYPE_MC146818_RTC)

typedef struct RTCState {
    ISADevice parent_obj;

    MemoryRegion io;
    MemoryRegion coalesced_io;
    uint8_t cmos_data[128];
    uint8_t cmos_index;
    int32_t base_year;
    uint64_t base_rtc;
    uint64_t last_update;
    int64_t offset;
    qemu_irq irq;
    int it_shift;
    /* periodic timer */
    QEMUTimer *periodic_timer;
    int64_t next_periodic_time;
    /* update-ended timer */
    QEMUTimer *update_timer;
    uint64_t next_alarm_time;
    uint16_t irq_reinject_on_ack_count;
    uint32_t irq_coalesced;
    uint32_t period;
    QEMUTimer *coalesced_timer;
    Notifier clock_reset_notifier;
    LostTickPolicy lost_tick_policy;
    Notifier suspend_notifier;
    QLIST_ENTRY(RTCState) link;
} RTCState;

#define RTC_ISA_IRQ 8
#define RTC_ISA_BASE 0x70

ISADevice *mc146818_rtc_init(ISABus *bus, int base_year,
                             qemu_irq intercept_irq);
void rtc_set_memory(ISADevice *dev, int addr, int val);
int rtc_get_memory(ISADevice *dev, int addr);

#endif /* MC146818RTC_H */
