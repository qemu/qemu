/*
 * QEMU Emulated HPET support
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Beth Kon   <bkon@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#ifndef QEMU_HPET_EMUL_H
#define QEMU_HPET_EMUL_H

#define HPET_BASE               0xfed00000
#define HPET_CLK_PERIOD         10000000ULL /* 10000000 femtoseconds == 10ns*/

#define FS_PER_NS 1000000
#define HPET_NUM_TIMERS 3
#define HPET_TIMER_TYPE_LEVEL 1
#define HPET_TIMER_TYPE_EDGE 0
#define HPET_TIMER_DELIVERY_APIC 0
#define HPET_TIMER_DELIVERY_FSB 1
#define HPET_TIMER_CAP_FSB_INT_DEL (1 << 15)
#define HPET_TIMER_CAP_PER_INT (1 << 4)

#define HPET_CFG_ENABLE 0x001
#define HPET_CFG_LEGACY 0x002

#define HPET_ID         0x000
#define HPET_PERIOD     0x004
#define HPET_CFG        0x010
#define HPET_STATUS     0x020
#define HPET_COUNTER    0x0f0
#define HPET_TN_CFG     0x000
#define HPET_TN_CMP     0x008
#define HPET_TN_ROUTE   0x010


#define HPET_TN_ENABLE           0x004
#define HPET_TN_PERIODIC         0x008
#define HPET_TN_PERIODIC_CAP     0x010
#define HPET_TN_SIZE_CAP         0x020
#define HPET_TN_SETVAL           0x040
#define HPET_TN_32BIT            0x100
#define HPET_TN_INT_ROUTE_MASK  0x3e00
#define HPET_TN_INT_ROUTE_SHIFT      9
#define HPET_TN_INT_ROUTE_CAP_SHIFT 32
#define HPET_TN_CFG_BITS_READONLY_OR_RESERVED 0xffff80b1U

struct HPETState;
typedef struct HPETTimer {  /* timers */
    uint8_t tn;             /*timer number*/
    QEMUTimer *qemu_timer;
    struct HPETState *state;
    /* Memory-mapped, software visible timer registers */
    uint64_t config;        /* configuration/cap */
    uint64_t cmp;           /* comparator */
    uint64_t fsb;           /* FSB route, not supported now */
    /* Hidden register state */
    uint64_t period;        /* Last value written to comparator */
    uint8_t wrap_flag;      /* timer pop will indicate wrap for one-shot 32-bit 
                             * mode. Next pop will be actual timer expiration.
                             */ 
} HPETTimer;

typedef struct HPETState {
    uint64_t hpet_offset;
    qemu_irq *irqs;
    HPETTimer timer[HPET_NUM_TIMERS];

    /* Memory-mapped, software visible registers */
    uint64_t capability;        /* capabilities */
    uint64_t config;            /* configuration */
    uint64_t isr;               /* interrupt status reg */
    uint64_t hpet_counter;      /* main counter */
} HPETState;

#if defined TARGET_I386 || defined TARGET_X86_64
extern uint32_t hpet_in_legacy_mode(void);
extern void hpet_init(qemu_irq *irq);
#endif

#endif
