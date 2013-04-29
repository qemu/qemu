/*
 *  APIC support - internal interfaces
 *
 *  Copyright (c) 2004-2005 Fabrice Bellard
 *  Copyright (c) 2011      Jan Kiszka, Siemens AG
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */
#ifndef QEMU_APIC_INTERNAL_H
#define QEMU_APIC_INTERNAL_H

#include "exec/memory.h"
#include "hw/cpu/icc_bus.h"
#include "qemu/timer.h"

/* APIC Local Vector Table */
#define APIC_LVT_TIMER                  0
#define APIC_LVT_THERMAL                1
#define APIC_LVT_PERFORM                2
#define APIC_LVT_LINT0                  3
#define APIC_LVT_LINT1                  4
#define APIC_LVT_ERROR                  5
#define APIC_LVT_NB                     6

/* APIC delivery modes */
#define APIC_DM_FIXED                   0
#define APIC_DM_LOWPRI                  1
#define APIC_DM_SMI                     2
#define APIC_DM_NMI                     4
#define APIC_DM_INIT                    5
#define APIC_DM_SIPI                    6
#define APIC_DM_EXTINT                  7

/* APIC destination mode */
#define APIC_DESTMODE_FLAT              0xf
#define APIC_DESTMODE_CLUSTER           1

#define APIC_TRIGGER_EDGE               0
#define APIC_TRIGGER_LEVEL              1

#define APIC_LVT_TIMER_PERIODIC         (1<<17)
#define APIC_LVT_MASKED                 (1<<16)
#define APIC_LVT_LEVEL_TRIGGER          (1<<15)
#define APIC_LVT_REMOTE_IRR             (1<<14)
#define APIC_INPUT_POLARITY             (1<<13)
#define APIC_SEND_PENDING               (1<<12)

#define ESR_ILLEGAL_ADDRESS (1 << 7)

#define APIC_SV_DIRECTED_IO             (1<<12)
#define APIC_SV_ENABLE                  (1<<8)

#define VAPIC_ENABLE_BIT                0
#define VAPIC_ENABLE_MASK               (1 << VAPIC_ENABLE_BIT)

#define MAX_APICS 255

typedef struct APICCommonState APICCommonState;

#define TYPE_APIC_COMMON "apic-common"
#define APIC_COMMON(obj) \
     OBJECT_CHECK(APICCommonState, (obj), TYPE_APIC_COMMON)
#define APIC_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(APICCommonClass, (klass), TYPE_APIC_COMMON)
#define APIC_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(APICCommonClass, (obj), TYPE_APIC_COMMON)

typedef struct APICCommonClass
{
    ICCDeviceClass parent_class;

    void (*init)(APICCommonState *s);
    void (*set_base)(APICCommonState *s, uint64_t val);
    void (*set_tpr)(APICCommonState *s, uint8_t val);
    uint8_t (*get_tpr)(APICCommonState *s);
    void (*enable_tpr_reporting)(APICCommonState *s, bool enable);
    void (*vapic_base_update)(APICCommonState *s);
    void (*external_nmi)(APICCommonState *s);
    void (*pre_save)(APICCommonState *s);
    void (*post_load)(APICCommonState *s);
} APICCommonClass;

struct APICCommonState {
    ICCDevice busdev;

    MemoryRegion io_memory;
    X86CPU *cpu;
    uint32_t apicbase;
    uint8_t id;
    uint8_t arb_id;
    uint8_t tpr;
    uint32_t spurious_vec;
    uint8_t log_dest;
    uint8_t dest_mode;
    uint32_t isr[8];  /* in service register */
    uint32_t tmr[8];  /* trigger mode register */
    uint32_t irr[8]; /* interrupt request register */
    uint32_t lvt[APIC_LVT_NB];
    uint32_t esr; /* error register */
    uint32_t icr[2];

    uint32_t divide_conf;
    int count_shift;
    uint32_t initial_count;
    int64_t initial_count_load_time;
    int64_t next_time;
    int idx;
    QEMUTimer *timer;
    int64_t timer_expiry;
    int sipi_vector;
    int wait_for_sipi;

    uint32_t vapic_control;
    DeviceState *vapic;
    hwaddr vapic_paddr; /* note: persistence via kvmvapic */
};

typedef struct VAPICState {
    uint8_t tpr;
    uint8_t isr;
    uint8_t zero;
    uint8_t irr;
    uint8_t enabled;
} QEMU_PACKED VAPICState;

extern bool apic_report_tpr_access;

void apic_report_irq_delivered(int delivered);
bool apic_next_timer(APICCommonState *s, int64_t current_time);
void apic_enable_tpr_access_reporting(DeviceState *d, bool enable);
void apic_enable_vapic(DeviceState *d, hwaddr paddr);

void vapic_report_tpr_access(DeviceState *dev, CPUState *cpu, target_ulong ip,
                             TPRAccess access);

#endif /* !QEMU_APIC_INTERNAL_H */
