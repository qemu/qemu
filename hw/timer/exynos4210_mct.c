/*
 * Samsung exynos4210 Multi Core timer
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd.
 * All rights reserved.
 *
 * Evgeny Voevodin <e.voevodin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Global Timer:
 *
 * Consists of two timers. First represents Free Running Counter and second
 * is used to measure interval from FRC to nearest comparator.
 *
 *        0                                                           UINT64_MAX
 *        |                              timer0                             |
 *        | <-------------------------------------------------------------- |
 *        | --------------------------------------------frc---------------> |
 *        |______________________________________________|__________________|
 *                CMP0          CMP1             CMP2    |           CMP3
 *                                                     __|            |_
 *                                                     |     timer1     |
 *                                                     | -------------> |
 *                                                    frc              CMPx
 *
 * Problem: when implementing global timer as is, overflow arises.
 * next_time = cur_time + period * count;
 * period and count are 64 bits width.
 * Lets arm timer for MCT_GT_COUNTER_STEP count and update internal G_CNT
 * register during each event.
 *
 * Problem: both timers need to be implemented using MCT_XT_COUNTER_STEP because
 * local timer contains two counters: TCNT and ICNT. TCNT == 0 -> ICNT--.
 * IRQ is generated when ICNT riches zero. Implementation where TCNT == 0
 * generates IRQs suffers from too frequently events. Better to have one
 * uint64_t counter equal to TCNT*ICNT and arm ptimer.c for a minimum(TCNT*ICNT,
 * MCT_GT_COUNTER_STEP); (yes, if target tunes ICNT * TCNT to be too low values,
 * there is no way to avoid frequently events).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "qemu/module.h"
#include "hw/ptimer.h"

#include "hw/arm/exynos4210.h"
#include "hw/irq.h"
#include "qom/object.h"

//#define DEBUG_MCT

#ifdef DEBUG_MCT
#define DPRINTF(fmt, ...) \
        do { fprintf(stdout, "MCT: [%24s:%5d] " fmt, __func__, __LINE__, \
                     ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define    MCT_CFG          0x000
#define    G_CNT_L          0x100
#define    G_CNT_U          0x104
#define    G_CNT_WSTAT      0x110
#define    G_COMP0_L        0x200
#define    G_COMP0_U        0x204
#define    G_COMP0_ADD_INCR 0x208
#define    G_COMP1_L        0x210
#define    G_COMP1_U        0x214
#define    G_COMP1_ADD_INCR 0x218
#define    G_COMP2_L        0x220
#define    G_COMP2_U        0x224
#define    G_COMP2_ADD_INCR 0x228
#define    G_COMP3_L        0x230
#define    G_COMP3_U        0x234
#define    G_COMP3_ADD_INCR 0x238
#define    G_TCON           0x240
#define    G_INT_CSTAT      0x244
#define    G_INT_ENB        0x248
#define    G_WSTAT          0x24C
#define    L0_TCNTB         0x300
#define    L0_TCNTO         0x304
#define    L0_ICNTB         0x308
#define    L0_ICNTO         0x30C
#define    L0_FRCNTB        0x310
#define    L0_FRCNTO        0x314
#define    L0_TCON          0x320
#define    L0_INT_CSTAT     0x330
#define    L0_INT_ENB       0x334
#define    L0_WSTAT         0x340
#define    L1_TCNTB         0x400
#define    L1_TCNTO         0x404
#define    L1_ICNTB         0x408
#define    L1_ICNTO         0x40C
#define    L1_FRCNTB        0x410
#define    L1_FRCNTO        0x414
#define    L1_TCON          0x420
#define    L1_INT_CSTAT     0x430
#define    L1_INT_ENB       0x434
#define    L1_WSTAT         0x440

#define MCT_CFG_GET_PRESCALER(x)    ((x) & 0xFF)
#define MCT_CFG_GET_DIVIDER(x)      (1 << ((x) >> 8 & 7))

#define GET_G_COMP_IDX(offset)          (((offset) - G_COMP0_L) / 0x10)
#define GET_G_COMP_ADD_INCR_IDX(offset) (((offset) - G_COMP0_ADD_INCR) / 0x10)

#define G_COMP_L(x) (G_COMP0_L + (x) * 0x10)
#define G_COMP_U(x) (G_COMP0_U + (x) * 0x10)

#define G_COMP_ADD_INCR(x)  (G_COMP0_ADD_INCR + (x) * 0x10)

/* MCT bits */
#define G_TCON_COMP_ENABLE(x)   (1 << 2 * (x))
#define G_TCON_AUTO_ICREMENT(x) (1 << (2 * (x) + 1))
#define G_TCON_TIMER_ENABLE     (1 << 8)

#define G_INT_ENABLE(x)         (1 << (x))
#define G_INT_CSTAT_COMP(x)     (1 << (x))

#define G_CNT_WSTAT_L           1
#define G_CNT_WSTAT_U           2

#define G_WSTAT_COMP_L(x)       (1 << 4 * (x))
#define G_WSTAT_COMP_U(x)       (1 << ((4 * (x)) + 1))
#define G_WSTAT_COMP_ADDINCR(x) (1 << ((4 * (x)) + 2))
#define G_WSTAT_TCON_WRITE      (1 << 16)

#define GET_L_TIMER_IDX(offset) ((((offset) & 0xF00) - L0_TCNTB) / 0x100)
#define GET_L_TIMER_CNT_REG_IDX(offset, lt_i) \
        (((offset) - (L0_TCNTB + 0x100 * (lt_i))) >> 2)

#define L_ICNTB_MANUAL_UPDATE   (1 << 31)

#define L_TCON_TICK_START       (1)
#define L_TCON_INT_START        (1 << 1)
#define L_TCON_INTERVAL_MODE    (1 << 2)
#define L_TCON_FRC_START        (1 << 3)

#define L_INT_CSTAT_INTCNT      (1 << 0)
#define L_INT_CSTAT_FRCCNT      (1 << 1)

#define L_INT_INTENB_ICNTEIE    (1 << 0)
#define L_INT_INTENB_FRCEIE     (1 << 1)

#define L_WSTAT_TCNTB_WRITE     (1 << 0)
#define L_WSTAT_ICNTB_WRITE     (1 << 1)
#define L_WSTAT_FRCCNTB_WRITE   (1 << 2)
#define L_WSTAT_TCON_WRITE      (1 << 3)

enum LocalTimerRegCntIndexes {
    L_REG_CNT_TCNTB,
    L_REG_CNT_TCNTO,
    L_REG_CNT_ICNTB,
    L_REG_CNT_ICNTO,
    L_REG_CNT_FRCCNTB,
    L_REG_CNT_FRCCNTO,

    L_REG_CNT_AMOUNT
};

#define MCT_SFR_SIZE            0x444

#define MCT_GT_CMP_NUM          4

#define MCT_GT_COUNTER_STEP     0x100000000ULL
#define MCT_LT_COUNTER_STEP     0x100000000ULL
#define MCT_LT_CNT_LOW_LIMIT    0x100

/* global timer */
typedef struct {
    qemu_irq  irq[MCT_GT_CMP_NUM];

    struct gregs {
        uint64_t cnt;
        uint32_t cnt_wstat;
        uint32_t tcon;
        uint32_t int_cstat;
        uint32_t int_enb;
        uint32_t wstat;
        uint64_t comp[MCT_GT_CMP_NUM];
        uint32_t comp_add_incr[MCT_GT_CMP_NUM];
    } reg;

    uint64_t count;            /* Value FRC was armed with */
    int32_t curr_comp;             /* Current comparator FRC is running to */

    ptimer_state *ptimer_frc;                   /* FRC timer */

} Exynos4210MCTGT;

/* local timer */
typedef struct {
    int         id;             /* timer id */
    qemu_irq    irq;            /* local timer irq */

    struct tick_timer {
        uint32_t cnt_run;           /* cnt timer is running */
        uint32_t int_run;           /* int timer is running */

        uint32_t last_icnto;
        uint32_t last_tcnto;
        uint32_t tcntb;             /* initial value for TCNTB */
        uint32_t icntb;             /* initial value for ICNTB */

        /* for step mode */
        uint64_t    distance;       /* distance to count to the next event */
        uint64_t    progress;       /* progress when counting by steps */
        uint64_t    count;          /* count to arm timer with */

        ptimer_state *ptimer_tick;  /* timer for tick counter */
    } tick_timer;

    /* use ptimer.c to represent count down timer */

    ptimer_state *ptimer_frc;   /* timer for free running counter */

    /* registers */
    struct lregs {
        uint32_t    cnt[L_REG_CNT_AMOUNT];
        uint32_t    tcon;
        uint32_t    int_cstat;
        uint32_t    int_enb;
        uint32_t    wstat;
    } reg;

} Exynos4210MCTLT;

#define TYPE_EXYNOS4210_MCT "exynos4210.mct"
OBJECT_DECLARE_SIMPLE_TYPE(Exynos4210MCTState, EXYNOS4210_MCT)

struct Exynos4210MCTState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* Registers */
    uint32_t    reg_mct_cfg;

    Exynos4210MCTLT l_timer[2];
    Exynos4210MCTGT g_timer;

    uint32_t    freq;                   /* all timers tick frequency, TCLK */
};

/*** VMState ***/
static const VMStateDescription vmstate_tick_timer = {
    .name = "exynos4210.mct.tick_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cnt_run, struct tick_timer),
        VMSTATE_UINT32(int_run, struct tick_timer),
        VMSTATE_UINT32(last_icnto, struct tick_timer),
        VMSTATE_UINT32(last_tcnto, struct tick_timer),
        VMSTATE_UINT32(tcntb, struct tick_timer),
        VMSTATE_UINT32(icntb, struct tick_timer),
        VMSTATE_UINT64(distance, struct tick_timer),
        VMSTATE_UINT64(progress, struct tick_timer),
        VMSTATE_UINT64(count, struct tick_timer),
        VMSTATE_PTIMER(ptimer_tick, struct tick_timer),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_lregs = {
    .name = "exynos4210.mct.lregs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(cnt, struct lregs, L_REG_CNT_AMOUNT),
        VMSTATE_UINT32(tcon, struct lregs),
        VMSTATE_UINT32(int_cstat, struct lregs),
        VMSTATE_UINT32(int_enb, struct lregs),
        VMSTATE_UINT32(wstat, struct lregs),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_exynos4210_mct_lt = {
    .name = "exynos4210.mct.lt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(id, Exynos4210MCTLT),
        VMSTATE_STRUCT(tick_timer, Exynos4210MCTLT, 0,
                vmstate_tick_timer,
                struct tick_timer),
        VMSTATE_PTIMER(ptimer_frc, Exynos4210MCTLT),
        VMSTATE_STRUCT(reg, Exynos4210MCTLT, 0,
                vmstate_lregs,
                struct lregs),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_gregs = {
    .name = "exynos4210.mct.lregs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(cnt, struct gregs),
        VMSTATE_UINT32(cnt_wstat, struct gregs),
        VMSTATE_UINT32(tcon, struct gregs),
        VMSTATE_UINT32(int_cstat, struct gregs),
        VMSTATE_UINT32(int_enb, struct gregs),
        VMSTATE_UINT32(wstat, struct gregs),
        VMSTATE_UINT64_ARRAY(comp, struct gregs, MCT_GT_CMP_NUM),
        VMSTATE_UINT32_ARRAY(comp_add_incr, struct gregs,
                MCT_GT_CMP_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_exynos4210_mct_gt = {
    .name = "exynos4210.mct.lt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(reg, Exynos4210MCTGT, 0, vmstate_gregs,
                struct gregs),
        VMSTATE_UINT64(count, Exynos4210MCTGT),
        VMSTATE_INT32(curr_comp, Exynos4210MCTGT),
        VMSTATE_PTIMER(ptimer_frc, Exynos4210MCTGT),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_exynos4210_mct_state = {
    .name = "exynos4210.mct",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(reg_mct_cfg, Exynos4210MCTState),
        VMSTATE_STRUCT_ARRAY(l_timer, Exynos4210MCTState, 2, 0,
            vmstate_exynos4210_mct_lt, Exynos4210MCTLT),
        VMSTATE_STRUCT(g_timer, Exynos4210MCTState, 0,
            vmstate_exynos4210_mct_gt, Exynos4210MCTGT),
        VMSTATE_UINT32(freq, Exynos4210MCTState),
        VMSTATE_END_OF_LIST()
    }
};

static void exynos4210_mct_update_freq(Exynos4210MCTState *s);

/*
 * Set counter of FRC global timer.
 * Must be called within exynos4210_gfrc_tx_begin/commit block.
 */
static void exynos4210_gfrc_set_count(Exynos4210MCTGT *s, uint64_t count)
{
    s->count = count;
    DPRINTF("global timer frc set count 0x%llx\n", count);
    ptimer_set_count(s->ptimer_frc, count);
}

/*
 * Get counter of FRC global timer.
 */
static uint64_t exynos4210_gfrc_get_count(Exynos4210MCTGT *s)
{
    uint64_t count = 0;
    count = ptimer_get_count(s->ptimer_frc);
    count = s->count - count;
    return s->reg.cnt + count;
}

/*
 * Stop global FRC timer
 * Must be called within exynos4210_gfrc_tx_begin/commit block.
 */
static void exynos4210_gfrc_stop(Exynos4210MCTGT *s)
{
    DPRINTF("global timer frc stop\n");

    ptimer_stop(s->ptimer_frc);
}

/*
 * Start global FRC timer
 * Must be called within exynos4210_gfrc_tx_begin/commit block.
 */
static void exynos4210_gfrc_start(Exynos4210MCTGT *s)
{
    DPRINTF("global timer frc start\n");

    ptimer_run(s->ptimer_frc, 1);
}

/*
 * Start ptimer transaction for global FRC timer; this is just for
 * consistency with the way we wrap operations like stop and run.
 */
static void exynos4210_gfrc_tx_begin(Exynos4210MCTGT *s)
{
    ptimer_transaction_begin(s->ptimer_frc);
}

/* Commit ptimer transaction for global FRC timer. */
static void exynos4210_gfrc_tx_commit(Exynos4210MCTGT *s)
{
    ptimer_transaction_commit(s->ptimer_frc);
}

/*
 * Find next nearest Comparator. If current Comparator value equals to other
 * Comparator value, skip them both
 */
static int32_t exynos4210_gcomp_find(Exynos4210MCTState *s)
{
    int res;
    int i;
    int enabled;
    uint64_t min;
    int min_comp_i;
    uint64_t gfrc;
    uint64_t distance;
    uint64_t distance_min;
    int comp_i;

    /* get gfrc count */
    gfrc = exynos4210_gfrc_get_count(&s->g_timer);

    min = UINT64_MAX;
    distance_min = UINT64_MAX;
    comp_i = MCT_GT_CMP_NUM;
    min_comp_i = MCT_GT_CMP_NUM;
    enabled = 0;

    /* lookup for nearest comparator */
    for (i = 0; i < MCT_GT_CMP_NUM; i++) {

        if (s->g_timer.reg.tcon & G_TCON_COMP_ENABLE(i)) {

            enabled = 1;

            if (s->g_timer.reg.comp[i] > gfrc) {
                /* Comparator is upper then FRC */
                distance = s->g_timer.reg.comp[i] - gfrc;

                if (distance <= distance_min) {
                    distance_min = distance;
                    comp_i = i;
                }
            } else {
                /* Comparator is below FRC, find the smallest */

                if (s->g_timer.reg.comp[i] <= min) {
                    min = s->g_timer.reg.comp[i];
                    min_comp_i = i;
                }
            }
        }
    }

    if (!enabled) {
        /* All Comparators disabled */
        res = -1;
    } else if (comp_i < MCT_GT_CMP_NUM) {
        /* Found upper Comparator */
        res = comp_i;
    } else {
        /* All Comparators are below or equal to FRC  */
        res = min_comp_i;
    }

    if (res >= 0) {
        DPRINTF("found comparator %d: "
                "comp 0x%llx distance 0x%llx, gfrc 0x%llx\n",
                res,
                s->g_timer.reg.comp[res],
                distance_min,
                gfrc);
    }

    return res;
}

/*
 * Get distance to nearest Comparator
 */
static uint64_t exynos4210_gcomp_get_distance(Exynos4210MCTState *s, int32_t id)
{
    if (id == -1) {
        /* no enabled Comparators, choose max distance */
        return MCT_GT_COUNTER_STEP;
    }
    if (s->g_timer.reg.comp[id] - s->g_timer.reg.cnt < MCT_GT_COUNTER_STEP) {
        return s->g_timer.reg.comp[id] - s->g_timer.reg.cnt;
    } else {
        return MCT_GT_COUNTER_STEP;
    }
}

/*
 * Restart global FRC timer
 * Must be called within exynos4210_gfrc_tx_begin/commit block.
 */
static void exynos4210_gfrc_restart(Exynos4210MCTState *s)
{
    uint64_t distance;

    exynos4210_gfrc_stop(&s->g_timer);

    s->g_timer.curr_comp = exynos4210_gcomp_find(s);

    distance = exynos4210_gcomp_get_distance(s, s->g_timer.curr_comp);

    if (distance > MCT_GT_COUNTER_STEP || !distance) {
        distance = MCT_GT_COUNTER_STEP;
    }

    exynos4210_gfrc_set_count(&s->g_timer, distance);
    exynos4210_gfrc_start(&s->g_timer);
}

/*
 * Raise global timer CMP IRQ
 */
static void exynos4210_gcomp_raise_irq(void *opaque, uint32_t id)
{
    Exynos4210MCTGT *s = opaque;

    /* If CSTAT is pending and IRQ is enabled */
    if ((s->reg.int_cstat & G_INT_CSTAT_COMP(id)) &&
            (s->reg.int_enb & G_INT_ENABLE(id))) {
        DPRINTF("gcmp timer[%u] IRQ\n", id);
        qemu_irq_raise(s->irq[id]);
    }
}

/*
 * Lower global timer CMP IRQ
 */
static void exynos4210_gcomp_lower_irq(void *opaque, uint32_t id)
{
    Exynos4210MCTGT *s = opaque;
    qemu_irq_lower(s->irq[id]);
}

/*
 * Global timer FRC event handler.
 * Each event occurs when internal counter reaches counter + MCT_GT_COUNTER_STEP
 * Every time we arm global FRC timer to count for MCT_GT_COUNTER_STEP value
 */
static void exynos4210_gfrc_event(void *opaque)
{
    Exynos4210MCTState *s = (Exynos4210MCTState *)opaque;
    int i;
    uint64_t distance;

    DPRINTF("\n");

    s->g_timer.reg.cnt += s->g_timer.count;

    /* Process all comparators */
    for (i = 0; i < MCT_GT_CMP_NUM; i++) {

        if (s->g_timer.reg.cnt == s->g_timer.reg.comp[i]) {
            /* reached nearest comparator */

            s->g_timer.reg.int_cstat |= G_INT_CSTAT_COMP(i);

            /* Auto increment */
            if (s->g_timer.reg.tcon & G_TCON_AUTO_ICREMENT(i)) {
                s->g_timer.reg.comp[i] += s->g_timer.reg.comp_add_incr[i];
            }

            /* IRQ */
            exynos4210_gcomp_raise_irq(&s->g_timer, i);
        }
    }

    /* Reload FRC to reach nearest comparator */
    s->g_timer.curr_comp = exynos4210_gcomp_find(s);
    distance = exynos4210_gcomp_get_distance(s, s->g_timer.curr_comp);
    if (distance > MCT_GT_COUNTER_STEP || !distance) {
        distance = MCT_GT_COUNTER_STEP;
    }
    exynos4210_gfrc_set_count(&s->g_timer, distance);

    exynos4210_gfrc_start(&s->g_timer);
}

/*
 * Get counter of FRC local timer.
 */
static uint64_t exynos4210_lfrc_get_count(Exynos4210MCTLT *s)
{
    return ptimer_get_count(s->ptimer_frc);
}

/*
 * Set counter of FRC local timer.
 * Must be called from within exynos4210_lfrc_tx_begin/commit block.
 */
static void exynos4210_lfrc_update_count(Exynos4210MCTLT *s)
{
    if (!s->reg.cnt[L_REG_CNT_FRCCNTB]) {
        ptimer_set_count(s->ptimer_frc, MCT_LT_COUNTER_STEP);
    } else {
        ptimer_set_count(s->ptimer_frc, s->reg.cnt[L_REG_CNT_FRCCNTB]);
    }
}

/*
 * Start local FRC timer
 * Must be called from within exynos4210_lfrc_tx_begin/commit block.
 */
static void exynos4210_lfrc_start(Exynos4210MCTLT *s)
{
    ptimer_run(s->ptimer_frc, 1);
}

/*
 * Stop local FRC timer
 * Must be called from within exynos4210_lfrc_tx_begin/commit block.
 */
static void exynos4210_lfrc_stop(Exynos4210MCTLT *s)
{
    ptimer_stop(s->ptimer_frc);
}

/* Start ptimer transaction for local FRC timer */
static void exynos4210_lfrc_tx_begin(Exynos4210MCTLT *s)
{
    ptimer_transaction_begin(s->ptimer_frc);
}

/* Commit ptimer transaction for local FRC timer */
static void exynos4210_lfrc_tx_commit(Exynos4210MCTLT *s)
{
    ptimer_transaction_commit(s->ptimer_frc);
}

/*
 * Local timer free running counter tick handler
 */
static void exynos4210_lfrc_event(void *opaque)
{
    Exynos4210MCTLT * s = (Exynos4210MCTLT *)opaque;

    /* local frc expired */

    DPRINTF("\n");

    s->reg.int_cstat |= L_INT_CSTAT_FRCCNT;

    /* update frc counter */
    exynos4210_lfrc_update_count(s);

    /* raise irq */
    if (s->reg.int_enb & L_INT_INTENB_FRCEIE) {
        qemu_irq_raise(s->irq);
    }

    /*  we reached here, this means that timer is enabled */
    exynos4210_lfrc_start(s);
}

static uint32_t exynos4210_ltick_int_get_cnto(struct tick_timer *s);
static uint32_t exynos4210_ltick_cnt_get_cnto(struct tick_timer *s);
static void exynos4210_ltick_recalc_count(struct tick_timer *s);

/*
 * Action on enabling local tick int timer
 */
static void exynos4210_ltick_int_start(struct tick_timer *s)
{
    if (!s->int_run) {
        s->int_run = 1;
    }
}

/*
 * Action on disabling local tick int timer
 */
static void exynos4210_ltick_int_stop(struct tick_timer *s)
{
    if (s->int_run) {
        s->last_icnto = exynos4210_ltick_int_get_cnto(s);
        s->int_run = 0;
    }
}

/*
 * Get count for INT timer
 */
static uint32_t exynos4210_ltick_int_get_cnto(struct tick_timer *s)
{
    uint32_t icnto;
    uint64_t remain;
    uint64_t count;
    uint64_t counted;
    uint64_t cur_progress;

    count = ptimer_get_count(s->ptimer_tick);
    if (count) {
        /* timer is still counting, called not from event */
        counted = s->count - ptimer_get_count(s->ptimer_tick);
        cur_progress = s->progress + counted;
    } else {
        /* timer expired earlier */
        cur_progress = s->progress;
    }

    remain = s->distance - cur_progress;

    if (!s->int_run) {
        /* INT is stopped. */
        icnto = s->last_icnto;
    } else {
        /* Both are counting */
        icnto = remain / s->tcntb;
    }

    return icnto;
}

/*
 * Start local tick cnt timer.
 * Must be called within exynos4210_ltick_tx_begin/commit block.
 */
static void exynos4210_ltick_cnt_start(struct tick_timer *s)
{
    if (!s->cnt_run) {

        exynos4210_ltick_recalc_count(s);
        ptimer_set_count(s->ptimer_tick, s->count);
        ptimer_run(s->ptimer_tick, 1);

        s->cnt_run = 1;
    }
}

/*
 * Stop local tick cnt timer.
 * Must be called within exynos4210_ltick_tx_begin/commit block.
 */
static void exynos4210_ltick_cnt_stop(struct tick_timer *s)
{
    if (s->cnt_run) {

        s->last_tcnto = exynos4210_ltick_cnt_get_cnto(s);

        if (s->int_run) {
            exynos4210_ltick_int_stop(s);
        }

        ptimer_stop(s->ptimer_tick);

        s->cnt_run = 0;
    }
}

/* Start ptimer transaction for local tick timer */
static void exynos4210_ltick_tx_begin(struct tick_timer *s)
{
    ptimer_transaction_begin(s->ptimer_tick);
}

/* Commit ptimer transaction for local tick timer */
static void exynos4210_ltick_tx_commit(struct tick_timer *s)
{
    ptimer_transaction_commit(s->ptimer_tick);
}

/*
 * Get counter for CNT timer
 */
static uint32_t exynos4210_ltick_cnt_get_cnto(struct tick_timer *s)
{
    uint32_t tcnto;
    uint32_t icnto;
    uint64_t remain;
    uint64_t counted;
    uint64_t count;
    uint64_t cur_progress;

    count = ptimer_get_count(s->ptimer_tick);
    if (count) {
        /* timer is still counting, called not from event */
        counted = s->count - ptimer_get_count(s->ptimer_tick);
        cur_progress = s->progress + counted;
    } else {
        /* timer expired earlier */
        cur_progress = s->progress;
    }

    remain = s->distance - cur_progress;

    if (!s->cnt_run) {
        /* Both are stopped. */
        tcnto = s->last_tcnto;
    } else if (!s->int_run) {
        /* INT counter is stopped, progress is by CNT timer */
        tcnto = remain % s->tcntb;
    } else {
        /* Both are counting */
        icnto = remain / s->tcntb;
        if (icnto) {
            tcnto = remain % (icnto * s->tcntb);
        } else {
            tcnto = remain % s->tcntb;
        }
    }

    return tcnto;
}

/*
 * Set new values of counters for CNT and INT timers
 * Must be called within exynos4210_ltick_tx_begin/commit block.
 */
static void exynos4210_ltick_set_cntb(struct tick_timer *s, uint32_t new_cnt,
        uint32_t new_int)
{
    uint32_t cnt_stopped = 0;
    uint32_t int_stopped = 0;

    if (s->cnt_run) {
        exynos4210_ltick_cnt_stop(s);
        cnt_stopped = 1;
    }

    if (s->int_run) {
        exynos4210_ltick_int_stop(s);
        int_stopped = 1;
    }

    s->tcntb = new_cnt + 1;
    s->icntb = new_int + 1;

    if (cnt_stopped) {
        exynos4210_ltick_cnt_start(s);
    }
    if (int_stopped) {
        exynos4210_ltick_int_start(s);
    }

}

/*
 * Calculate new counter value for tick timer
 */
static void exynos4210_ltick_recalc_count(struct tick_timer *s)
{
    uint64_t to_count;

    if ((s->cnt_run && s->last_tcnto) || (s->int_run && s->last_icnto)) {
        /*
         * one or both timers run and not counted to the end;
         * distance is not passed, recalculate with last_tcnto * last_icnto
         */

        if (s->last_tcnto) {
            to_count = (uint64_t)s->last_tcnto * s->last_icnto;
        } else {
            to_count = s->last_icnto;
        }
    } else {
        /* distance is passed, recalculate with tcnto * icnto */
        if (s->icntb) {
            s->distance = (uint64_t)s->tcntb * s->icntb;
        } else {
            s->distance = s->tcntb;
        }

        to_count = s->distance;
        s->progress = 0;
    }

    if (to_count > MCT_LT_COUNTER_STEP) {
        /* count by step */
        s->count = MCT_LT_COUNTER_STEP;
    } else {
        s->count = to_count;
    }
}

/*
 * Initialize tick_timer
 */
static void exynos4210_ltick_timer_init(struct tick_timer *s)
{
    exynos4210_ltick_int_stop(s);
    exynos4210_ltick_tx_begin(s);
    exynos4210_ltick_cnt_stop(s);
    exynos4210_ltick_tx_commit(s);

    s->count = 0;
    s->distance = 0;
    s->progress = 0;
    s->icntb = 0;
    s->tcntb = 0;
}

/*
 * tick_timer event.
 * Raises when abstract tick_timer expires.
 */
static void exynos4210_ltick_timer_event(struct tick_timer *s)
{
    s->progress += s->count;
}

/*
 * Local timer tick counter handler.
 * Don't use reloaded timers. If timer counter = zero
 * then handler called but after handler finished no
 * timer reload occurs.
 */
static void exynos4210_ltick_event(void *opaque)
{
    Exynos4210MCTLT * s = (Exynos4210MCTLT *)opaque;
    uint32_t tcnto;
    uint32_t icnto;
#ifdef DEBUG_MCT
    static uint64_t time1[2] = {0};
    static uint64_t time2[2] = {0};
#endif

    /* Call tick_timer event handler, it will update its tcntb and icntb. */
    exynos4210_ltick_timer_event(&s->tick_timer);

    /* get tick_timer cnt */
    tcnto = exynos4210_ltick_cnt_get_cnto(&s->tick_timer);

    /* get tick_timer int */
    icnto = exynos4210_ltick_int_get_cnto(&s->tick_timer);

    /* raise IRQ if needed */
    if (!icnto && s->reg.tcon & L_TCON_INT_START) {
        /* INT counter enabled and expired */

        s->reg.int_cstat |= L_INT_CSTAT_INTCNT;

        /* raise interrupt if enabled */
        if (s->reg.int_enb & L_INT_INTENB_ICNTEIE) {
#ifdef DEBUG_MCT
            time2[s->id] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            DPRINTF("local timer[%d] IRQ: %llx\n", s->id,
                    time2[s->id] - time1[s->id]);
            time1[s->id] = time2[s->id];
#endif
            qemu_irq_raise(s->irq);
        }

        /* reload ICNTB */
        if (s->reg.tcon & L_TCON_INTERVAL_MODE) {
            exynos4210_ltick_set_cntb(&s->tick_timer,
                    s->reg.cnt[L_REG_CNT_TCNTB],
                    s->reg.cnt[L_REG_CNT_ICNTB]);
        }
    } else {
        /* reload TCNTB */
        if (!tcnto) {
            exynos4210_ltick_set_cntb(&s->tick_timer,
                    s->reg.cnt[L_REG_CNT_TCNTB],
                    icnto);
        }
    }

    /* start tick_timer cnt */
    exynos4210_ltick_cnt_start(&s->tick_timer);

    /* start tick_timer int */
    exynos4210_ltick_int_start(&s->tick_timer);
}

static void tx_ptimer_set_freq(ptimer_state *s, uint32_t freq)
{
    /*
     * callers of exynos4210_mct_update_freq() never do anything
     * else that needs to be in the same ptimer transaction, so
     * to avoid a lot of repetition we have a convenience function
     * for begin/set_freq/commit.
     */
    ptimer_transaction_begin(s);
    ptimer_set_freq(s, freq);
    ptimer_transaction_commit(s);
}

/* update timer frequency */
static void exynos4210_mct_update_freq(Exynos4210MCTState *s)
{
    uint32_t freq = s->freq;
    s->freq = 24000000 /
            ((MCT_CFG_GET_PRESCALER(s->reg_mct_cfg) + 1) *
                    MCT_CFG_GET_DIVIDER(s->reg_mct_cfg));

    if (freq != s->freq) {
        DPRINTF("freq=%uHz\n", s->freq);

        /* global timer */
        tx_ptimer_set_freq(s->g_timer.ptimer_frc, s->freq);

        /* local timer */
        tx_ptimer_set_freq(s->l_timer[0].tick_timer.ptimer_tick, s->freq);
        tx_ptimer_set_freq(s->l_timer[0].ptimer_frc, s->freq);
        tx_ptimer_set_freq(s->l_timer[1].tick_timer.ptimer_tick, s->freq);
        tx_ptimer_set_freq(s->l_timer[1].ptimer_frc, s->freq);
    }
}

/* set defaul_timer values for all fields */
static void exynos4210_mct_reset(DeviceState *d)
{
    Exynos4210MCTState *s = EXYNOS4210_MCT(d);
    uint32_t i;

    s->reg_mct_cfg = 0;

    /* global timer */
    memset(&s->g_timer.reg, 0, sizeof(s->g_timer.reg));
    exynos4210_gfrc_tx_begin(&s->g_timer);
    exynos4210_gfrc_stop(&s->g_timer);
    exynos4210_gfrc_tx_commit(&s->g_timer);

    /* local timer */
    memset(s->l_timer[0].reg.cnt, 0, sizeof(s->l_timer[0].reg.cnt));
    memset(s->l_timer[1].reg.cnt, 0, sizeof(s->l_timer[1].reg.cnt));
    for (i = 0; i < 2; i++) {
        s->l_timer[i].reg.int_cstat = 0;
        s->l_timer[i].reg.int_enb = 0;
        s->l_timer[i].reg.tcon = 0;
        s->l_timer[i].reg.wstat = 0;
        s->l_timer[i].tick_timer.count = 0;
        s->l_timer[i].tick_timer.distance = 0;
        s->l_timer[i].tick_timer.progress = 0;
        exynos4210_lfrc_tx_begin(&s->l_timer[i]);
        ptimer_stop(s->l_timer[i].ptimer_frc);
        exynos4210_lfrc_tx_commit(&s->l_timer[i]);

        exynos4210_ltick_timer_init(&s->l_timer[i].tick_timer);
    }

    exynos4210_mct_update_freq(s);

}

/* Multi Core Timer read */
static uint64_t exynos4210_mct_read(void *opaque, hwaddr offset,
        unsigned size)
{
    Exynos4210MCTState *s = (Exynos4210MCTState *)opaque;
    int index;
    int shift;
    uint64_t count;
    uint32_t value = 0;
    int lt_i;

    switch (offset) {

    case MCT_CFG:
        value = s->reg_mct_cfg;
        break;

    case G_CNT_L: case G_CNT_U:
        shift = 8 * (offset & 0x4);
        count = exynos4210_gfrc_get_count(&s->g_timer);
        value = UINT32_MAX & (count >> shift);
        DPRINTF("read FRC=0x%llx\n", count);
        break;

    case G_CNT_WSTAT:
        value = s->g_timer.reg.cnt_wstat;
        break;

    case G_COMP_L(0): case G_COMP_L(1): case G_COMP_L(2): case G_COMP_L(3):
    case G_COMP_U(0): case G_COMP_U(1): case G_COMP_U(2): case G_COMP_U(3):
        index = GET_G_COMP_IDX(offset);
        shift = 8 * (offset & 0x4);
        value = UINT32_MAX & (s->g_timer.reg.comp[index] >> shift);
    break;

    case G_TCON:
        value = s->g_timer.reg.tcon;
        break;

    case G_INT_CSTAT:
        value = s->g_timer.reg.int_cstat;
        break;

    case G_INT_ENB:
        value = s->g_timer.reg.int_enb;
        break;
    case G_WSTAT:
        value = s->g_timer.reg.wstat;
        break;

    case G_COMP0_ADD_INCR: case G_COMP1_ADD_INCR:
    case G_COMP2_ADD_INCR: case G_COMP3_ADD_INCR:
        value = s->g_timer.reg.comp_add_incr[GET_G_COMP_ADD_INCR_IDX(offset)];
        break;

        /* Local timers */
    case L0_TCNTB: case L0_ICNTB: case L0_FRCNTB:
    case L1_TCNTB: case L1_ICNTB: case L1_FRCNTB:
        lt_i = GET_L_TIMER_IDX(offset);
        index = GET_L_TIMER_CNT_REG_IDX(offset, lt_i);
        value = s->l_timer[lt_i].reg.cnt[index];
        break;

    case L0_TCNTO: case L1_TCNTO:
        lt_i = GET_L_TIMER_IDX(offset);

        value = exynos4210_ltick_cnt_get_cnto(&s->l_timer[lt_i].tick_timer);
        DPRINTF("local timer[%d] read TCNTO %x\n", lt_i, value);
        break;

    case L0_ICNTO: case L1_ICNTO:
        lt_i = GET_L_TIMER_IDX(offset);

        value = exynos4210_ltick_int_get_cnto(&s->l_timer[lt_i].tick_timer);
        DPRINTF("local timer[%d] read ICNTO %x\n", lt_i, value);
        break;

    case L0_FRCNTO: case L1_FRCNTO:
        lt_i = GET_L_TIMER_IDX(offset);

        value = exynos4210_lfrc_get_count(&s->l_timer[lt_i]);
        break;

    case L0_TCON: case L1_TCON:
        lt_i = ((offset & 0xF00) - L0_TCNTB) / 0x100;
        value = s->l_timer[lt_i].reg.tcon;
        break;

    case L0_INT_CSTAT: case L1_INT_CSTAT:
        lt_i = ((offset & 0xF00) - L0_TCNTB) / 0x100;
        value = s->l_timer[lt_i].reg.int_cstat;
        break;

    case L0_INT_ENB: case L1_INT_ENB:
        lt_i = ((offset & 0xF00) - L0_TCNTB) / 0x100;
        value = s->l_timer[lt_i].reg.int_enb;
        break;

    case L0_WSTAT: case L1_WSTAT:
        lt_i = ((offset & 0xF00) - L0_TCNTB) / 0x100;
        value = s->l_timer[lt_i].reg.wstat;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        break;
    }
    return value;
}

/* MCT write */
static void exynos4210_mct_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    Exynos4210MCTState *s = (Exynos4210MCTState *)opaque;
    int index;  /* index in buffer which represents register set */
    int shift;
    int lt_i;
    uint64_t new_frc;
    uint32_t i;
    uint32_t old_val;
#ifdef DEBUG_MCT
    static uint32_t icntb_max[2] = {0};
    static uint32_t icntb_min[2] = {UINT32_MAX, UINT32_MAX};
    static uint32_t tcntb_max[2] = {0};
    static uint32_t tcntb_min[2] = {UINT32_MAX, UINT32_MAX};
#endif

    new_frc = s->g_timer.reg.cnt;

    switch (offset) {

    case MCT_CFG:
        s->reg_mct_cfg = value;
        exynos4210_mct_update_freq(s);
        break;

    case G_CNT_L:
    case G_CNT_U:
        if (offset == G_CNT_L) {

            DPRINTF("global timer write to reg.cntl %llx\n", value);

            new_frc = (s->g_timer.reg.cnt & (uint64_t)UINT32_MAX << 32) + value;
            s->g_timer.reg.cnt_wstat |= G_CNT_WSTAT_L;
        }
        if (offset == G_CNT_U) {

            DPRINTF("global timer write to reg.cntu %llx\n", value);

            new_frc = (s->g_timer.reg.cnt & UINT32_MAX) +
                    ((uint64_t)value << 32);
            s->g_timer.reg.cnt_wstat |= G_CNT_WSTAT_U;
        }

        s->g_timer.reg.cnt = new_frc;
        exynos4210_gfrc_tx_begin(&s->g_timer);
        exynos4210_gfrc_restart(s);
        exynos4210_gfrc_tx_commit(&s->g_timer);
        break;

    case G_CNT_WSTAT:
        s->g_timer.reg.cnt_wstat &= ~(value);
        break;

    case G_COMP_L(0): case G_COMP_L(1): case G_COMP_L(2): case G_COMP_L(3):
    case G_COMP_U(0): case G_COMP_U(1): case G_COMP_U(2): case G_COMP_U(3):
        index = GET_G_COMP_IDX(offset);
        shift = 8 * (offset & 0x4);
        s->g_timer.reg.comp[index] =
                (s->g_timer.reg.comp[index] &
                (((uint64_t)UINT32_MAX << 32) >> shift)) +
                (value << shift);

        DPRINTF("comparator %d write 0x%llx val << %d\n", index, value, shift);

        if (offset & 0x4) {
            s->g_timer.reg.wstat |= G_WSTAT_COMP_U(index);
        } else {
            s->g_timer.reg.wstat |= G_WSTAT_COMP_L(index);
        }

        exynos4210_gfrc_tx_begin(&s->g_timer);
        exynos4210_gfrc_restart(s);
        exynos4210_gfrc_tx_commit(&s->g_timer);
        break;

    case G_TCON:
        old_val = s->g_timer.reg.tcon;
        s->g_timer.reg.tcon = value;
        s->g_timer.reg.wstat |= G_WSTAT_TCON_WRITE;

        DPRINTF("global timer write to reg.g_tcon %llx\n", value);

        exynos4210_gfrc_tx_begin(&s->g_timer);

        /* Start FRC if transition from disabled to enabled */
        if ((value & G_TCON_TIMER_ENABLE) > (old_val &
                G_TCON_TIMER_ENABLE)) {
            exynos4210_gfrc_restart(s);
        }
        if ((value & G_TCON_TIMER_ENABLE) < (old_val &
                G_TCON_TIMER_ENABLE)) {
            exynos4210_gfrc_stop(&s->g_timer);
        }

        /* Start CMP if transition from disabled to enabled */
        for (i = 0; i < MCT_GT_CMP_NUM; i++) {
            if ((value & G_TCON_COMP_ENABLE(i)) != (old_val &
                    G_TCON_COMP_ENABLE(i))) {
                exynos4210_gfrc_restart(s);
            }
        }

        exynos4210_gfrc_tx_commit(&s->g_timer);
        break;

    case G_INT_CSTAT:
        s->g_timer.reg.int_cstat &= ~(value);
        for (i = 0; i < MCT_GT_CMP_NUM; i++) {
            if (value & G_INT_CSTAT_COMP(i)) {
                exynos4210_gcomp_lower_irq(&s->g_timer, i);
            }
        }
        break;

    case G_INT_ENB:
        /* Raise IRQ if transition from disabled to enabled and CSTAT pending */
        for (i = 0; i < MCT_GT_CMP_NUM; i++) {
            if ((value & G_INT_ENABLE(i)) > (s->g_timer.reg.tcon &
                    G_INT_ENABLE(i))) {
                if (s->g_timer.reg.int_cstat & G_INT_CSTAT_COMP(i)) {
                    exynos4210_gcomp_raise_irq(&s->g_timer, i);
                }
            }

            if ((value & G_INT_ENABLE(i)) < (s->g_timer.reg.tcon &
                    G_INT_ENABLE(i))) {
                exynos4210_gcomp_lower_irq(&s->g_timer, i);
            }
        }

        DPRINTF("global timer INT enable %llx\n", value);
        s->g_timer.reg.int_enb = value;
        break;

    case G_WSTAT:
        s->g_timer.reg.wstat &= ~(value);
        break;

    case G_COMP0_ADD_INCR: case G_COMP1_ADD_INCR:
    case G_COMP2_ADD_INCR: case G_COMP3_ADD_INCR:
        index = GET_G_COMP_ADD_INCR_IDX(offset);
        s->g_timer.reg.comp_add_incr[index] = value;
        s->g_timer.reg.wstat |= G_WSTAT_COMP_ADDINCR(index);
        break;

        /* Local timers */
    case L0_TCON: case L1_TCON:
        lt_i = GET_L_TIMER_IDX(offset);
        old_val = s->l_timer[lt_i].reg.tcon;

        s->l_timer[lt_i].reg.wstat |= L_WSTAT_TCON_WRITE;
        s->l_timer[lt_i].reg.tcon = value;

        exynos4210_ltick_tx_begin(&s->l_timer[lt_i].tick_timer);
        /* Stop local CNT */
        if ((value & L_TCON_TICK_START) <
                (old_val & L_TCON_TICK_START)) {
            DPRINTF("local timer[%d] stop cnt\n", lt_i);
            exynos4210_ltick_cnt_stop(&s->l_timer[lt_i].tick_timer);
        }

        /* Stop local INT */
        if ((value & L_TCON_INT_START) <
                (old_val & L_TCON_INT_START)) {
            DPRINTF("local timer[%d] stop int\n", lt_i);
            exynos4210_ltick_int_stop(&s->l_timer[lt_i].tick_timer);
        }

        /* Start local CNT */
        if ((value & L_TCON_TICK_START) >
        (old_val & L_TCON_TICK_START)) {
            DPRINTF("local timer[%d] start cnt\n", lt_i);
            exynos4210_ltick_cnt_start(&s->l_timer[lt_i].tick_timer);
        }

        /* Start local INT */
        if ((value & L_TCON_INT_START) >
        (old_val & L_TCON_INT_START)) {
            DPRINTF("local timer[%d] start int\n", lt_i);
            exynos4210_ltick_int_start(&s->l_timer[lt_i].tick_timer);
        }
        exynos4210_ltick_tx_commit(&s->l_timer[lt_i].tick_timer);

        /* Start or Stop local FRC if TCON changed */
        exynos4210_lfrc_tx_begin(&s->l_timer[lt_i]);
        if ((value & L_TCON_FRC_START) >
        (s->l_timer[lt_i].reg.tcon & L_TCON_FRC_START)) {
            DPRINTF("local timer[%d] start frc\n", lt_i);
            exynos4210_lfrc_start(&s->l_timer[lt_i]);
        }
        if ((value & L_TCON_FRC_START) <
                (s->l_timer[lt_i].reg.tcon & L_TCON_FRC_START)) {
            DPRINTF("local timer[%d] stop frc\n", lt_i);
            exynos4210_lfrc_stop(&s->l_timer[lt_i]);
        }
        exynos4210_lfrc_tx_commit(&s->l_timer[lt_i]);
        break;

    case L0_TCNTB: case L1_TCNTB:
        lt_i = GET_L_TIMER_IDX(offset);

        /*
         * TCNTB is updated to internal register only after CNT expired.
         * Due to this we should reload timer to nearest moment when CNT is
         * expired and then in event handler update tcntb to new TCNTB value.
         */
        exynos4210_ltick_tx_begin(&s->l_timer[lt_i].tick_timer);
        exynos4210_ltick_set_cntb(&s->l_timer[lt_i].tick_timer, value,
                s->l_timer[lt_i].tick_timer.icntb);
        exynos4210_ltick_tx_commit(&s->l_timer[lt_i].tick_timer);

        s->l_timer[lt_i].reg.wstat |= L_WSTAT_TCNTB_WRITE;
        s->l_timer[lt_i].reg.cnt[L_REG_CNT_TCNTB] = value;

#ifdef DEBUG_MCT
        if (tcntb_min[lt_i] > value) {
            tcntb_min[lt_i] = value;
        }
        if (tcntb_max[lt_i] < value) {
            tcntb_max[lt_i] = value;
        }
        DPRINTF("local timer[%d] TCNTB write %llx; max=%x, min=%x\n",
                lt_i, value, tcntb_max[lt_i], tcntb_min[lt_i]);
#endif
        break;

    case L0_ICNTB: case L1_ICNTB:
        lt_i = GET_L_TIMER_IDX(offset);

        s->l_timer[lt_i].reg.wstat |= L_WSTAT_ICNTB_WRITE;
        s->l_timer[lt_i].reg.cnt[L_REG_CNT_ICNTB] = value &
                ~L_ICNTB_MANUAL_UPDATE;

        /*
         * We need to avoid too small values for TCNTB*ICNTB. If not, IRQ event
         * could raise too fast disallowing QEMU to execute target code.
         */
        if (s->l_timer[lt_i].reg.cnt[L_REG_CNT_ICNTB] *
            s->l_timer[lt_i].reg.cnt[L_REG_CNT_TCNTB] < MCT_LT_CNT_LOW_LIMIT) {
            if (!s->l_timer[lt_i].reg.cnt[L_REG_CNT_TCNTB]) {
                s->l_timer[lt_i].reg.cnt[L_REG_CNT_ICNTB] =
                        MCT_LT_CNT_LOW_LIMIT;
            } else {
                s->l_timer[lt_i].reg.cnt[L_REG_CNT_ICNTB] =
                        MCT_LT_CNT_LOW_LIMIT /
                        s->l_timer[lt_i].reg.cnt[L_REG_CNT_TCNTB];
            }
        }

        if (value & L_ICNTB_MANUAL_UPDATE) {
            exynos4210_ltick_set_cntb(&s->l_timer[lt_i].tick_timer,
                    s->l_timer[lt_i].tick_timer.tcntb,
                    s->l_timer[lt_i].reg.cnt[L_REG_CNT_ICNTB]);
        }

#ifdef DEBUG_MCT
        if (icntb_min[lt_i] > value) {
            icntb_min[lt_i] = value;
        }
        if (icntb_max[lt_i] < value) {
            icntb_max[lt_i] = value;
        }
        DPRINTF("local timer[%d] ICNTB write %llx; max=%x, min=%x\n\n",
                lt_i, value, icntb_max[lt_i], icntb_min[lt_i]);
#endif
        break;

    case L0_FRCNTB: case L1_FRCNTB:
        lt_i = GET_L_TIMER_IDX(offset);
        DPRINTF("local timer[%d] FRCNTB write %llx\n", lt_i, value);

        s->l_timer[lt_i].reg.wstat |= L_WSTAT_FRCCNTB_WRITE;
        s->l_timer[lt_i].reg.cnt[L_REG_CNT_FRCCNTB] = value;

        break;

    case L0_TCNTO: case L1_TCNTO:
    case L0_ICNTO: case L1_ICNTO:
    case L0_FRCNTO: case L1_FRCNTO:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "exynos4210.mct: write to RO register " HWADDR_FMT_plx,
                      offset);
        break;

    case L0_INT_CSTAT: case L1_INT_CSTAT:
        lt_i = GET_L_TIMER_IDX(offset);

        DPRINTF("local timer[%d] CSTAT write %llx\n", lt_i, value);

        s->l_timer[lt_i].reg.int_cstat &= ~value;
        if (!s->l_timer[lt_i].reg.int_cstat) {
            qemu_irq_lower(s->l_timer[lt_i].irq);
        }
        break;

    case L0_INT_ENB: case L1_INT_ENB:
        lt_i = GET_L_TIMER_IDX(offset);
        old_val = s->l_timer[lt_i].reg.int_enb;

        /* Raise Local timer IRQ if cstat is pending */
        if ((value & L_INT_INTENB_ICNTEIE) > (old_val & L_INT_INTENB_ICNTEIE)) {
            if (s->l_timer[lt_i].reg.int_cstat & L_INT_CSTAT_INTCNT) {
                qemu_irq_raise(s->l_timer[lt_i].irq);
            }
        }

        s->l_timer[lt_i].reg.int_enb = value;

        break;

    case L0_WSTAT: case L1_WSTAT:
        lt_i = GET_L_TIMER_IDX(offset);

        s->l_timer[lt_i].reg.wstat &= ~value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps exynos4210_mct_ops = {
    .read = exynos4210_mct_read,
    .write = exynos4210_mct_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* MCT init */
static void exynos4210_mct_init(Object *obj)
{
    int i;
    Exynos4210MCTState *s = EXYNOS4210_MCT(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    /* Global timer */
    s->g_timer.ptimer_frc = ptimer_init(exynos4210_gfrc_event, s,
                                        PTIMER_POLICY_LEGACY);
    memset(&s->g_timer.reg, 0, sizeof(struct gregs));

    /* Local timers */
    for (i = 0; i < 2; i++) {
        s->l_timer[i].tick_timer.ptimer_tick =
            ptimer_init(exynos4210_ltick_event, &s->l_timer[i],
                        PTIMER_POLICY_LEGACY);
        s->l_timer[i].ptimer_frc =
            ptimer_init(exynos4210_lfrc_event, &s->l_timer[i],
                        PTIMER_POLICY_LEGACY);
        s->l_timer[i].id = i;
    }

    /* IRQs */
    for (i = 0; i < MCT_GT_CMP_NUM; i++) {
        sysbus_init_irq(dev, &s->g_timer.irq[i]);
    }
    for (i = 0; i < 2; i++) {
        sysbus_init_irq(dev, &s->l_timer[i].irq);
    }

    memory_region_init_io(&s->iomem, obj, &exynos4210_mct_ops, s,
                          "exynos4210-mct", MCT_SFR_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static void exynos4210_mct_finalize(Object *obj)
{
    int i;
    Exynos4210MCTState *s = EXYNOS4210_MCT(obj);

    ptimer_free(s->g_timer.ptimer_frc);

    for (i = 0; i < 2; i++) {
        ptimer_free(s->l_timer[i].tick_timer.ptimer_tick);
        ptimer_free(s->l_timer[i].ptimer_frc);
    }
}

static void exynos4210_mct_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4210_mct_reset;
    dc->vmsd = &vmstate_exynos4210_mct_state;
}

static const TypeInfo exynos4210_mct_info = {
    .name          = TYPE_EXYNOS4210_MCT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210MCTState),
    .instance_init = exynos4210_mct_init,
    .instance_finalize = exynos4210_mct_finalize,
    .class_init    = exynos4210_mct_class_init,
};

static void exynos4210_mct_register_types(void)
{
    type_register_static(&exynos4210_mct_info);
}

type_init(exynos4210_mct_register_types)
