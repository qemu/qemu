/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2016 Imagination Technologies
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/timer/mips_gictimer.h"

#define TIMER_PERIOD 10 /* 10 ns period for 100 Mhz frequency */

uint32_t mips_gictimer_get_freq(MIPSGICTimerState *gic)
{
    return NANOSECONDS_PER_SECOND / TIMER_PERIOD;
}

static void gic_vptimer_update(MIPSGICTimerState *gictimer,
                                   uint32_t vp_index, uint64_t now)
{
    uint64_t next;
    uint32_t wait;

    wait = gictimer->vptimers[vp_index].comparelo - gictimer->sh_counterlo -
           (uint32_t)(now / TIMER_PERIOD);
    next = now + (uint64_t)wait * TIMER_PERIOD;

    timer_mod(gictimer->vptimers[vp_index].qtimer, next);
}

static void gic_vptimer_expire(MIPSGICTimerState *gictimer, uint32_t vp_index,
                               uint64_t now)
{
    if (gictimer->countstop) {
        /* timer stopped */
        return;
    }
    gictimer->cb(gictimer->opaque, vp_index);
    gic_vptimer_update(gictimer, vp_index, now);
}

static void gic_vptimer_cb(void *opaque)
{
    MIPSGICTimerVPState *vptimer = opaque;
    MIPSGICTimerState *gictimer = vptimer->gictimer;
    gic_vptimer_expire(gictimer, vptimer->vp_index,
                       qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
}

uint32_t mips_gictimer_get_sh_count(MIPSGICTimerState *gictimer)
{
    int i;
    if (gictimer->countstop) {
        return gictimer->sh_counterlo;
    } else {
        uint64_t now;
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        for (i = 0; i < gictimer->num_vps; i++) {
            if (timer_pending(gictimer->vptimers[i].qtimer)
                && timer_expired(gictimer->vptimers[i].qtimer, now)) {
                /* The timer has already expired.  */
                gic_vptimer_expire(gictimer, i, now);
            }
        }
        return gictimer->sh_counterlo + (uint32_t)(now / TIMER_PERIOD);
    }
}

void mips_gictimer_store_sh_count(MIPSGICTimerState *gictimer, uint64_t count)
{
    int i;
    uint64_t now;

    if (gictimer->countstop || !gictimer->vptimers[0].qtimer) {
        gictimer->sh_counterlo = count;
    } else {
        /* Store new count register */
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        gictimer->sh_counterlo = count - (uint32_t)(now / TIMER_PERIOD);
        /* Update timer timer */
        for (i = 0; i < gictimer->num_vps; i++) {
            gic_vptimer_update(gictimer, i, now);
        }
    }
}

uint32_t mips_gictimer_get_vp_compare(MIPSGICTimerState *gictimer,
                                      uint32_t vp_index)
{
    return gictimer->vptimers[vp_index].comparelo;
}

void mips_gictimer_store_vp_compare(MIPSGICTimerState *gictimer,
                                    uint32_t vp_index, uint64_t compare)
{
    gictimer->vptimers[vp_index].comparelo = (uint32_t) compare;
    gic_vptimer_update(gictimer, vp_index,
                       qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
}

uint8_t mips_gictimer_get_countstop(MIPSGICTimerState *gictimer)
{
    return gictimer->countstop;
}

void mips_gictimer_start_count(MIPSGICTimerState *gictimer)
{
    gictimer->countstop = 0;
    mips_gictimer_store_sh_count(gictimer, gictimer->sh_counterlo);
}

void mips_gictimer_stop_count(MIPSGICTimerState *gictimer)
{
    int i;

    gictimer->countstop = 1;
    /* Store the current value */
    gictimer->sh_counterlo +=
        (uint32_t)(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / TIMER_PERIOD);
    for (i = 0; i < gictimer->num_vps; i++) {
        timer_del(gictimer->vptimers[i].qtimer);
    }
}

MIPSGICTimerState *mips_gictimer_init(void *opaque, uint32_t nvps,
                                      MIPSGICTimerCB *cb)
{
    int i;
    MIPSGICTimerState *gictimer = g_new(MIPSGICTimerState, 1);
    gictimer->vptimers = g_new(MIPSGICTimerVPState, nvps);
    gictimer->countstop = 1;
    gictimer->num_vps = nvps;
    gictimer->opaque = opaque;
    gictimer->cb = cb;
    for (i = 0; i < nvps; i++) {
        gictimer->vptimers[i].gictimer = gictimer;
        gictimer->vptimers[i].vp_index = i;
        gictimer->vptimers[i].qtimer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                            &gic_vptimer_cb,
                                            &gictimer->vptimers[i]);
    }
    return gictimer;
}
