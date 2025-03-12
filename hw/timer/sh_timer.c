/*
 * SuperH Timer modules.
 *
 * Copyright (c) 2007 Magnus Damm
 * Based on arm_timer.c by Paul Brook
 * Copyright (c) 2005-2006 CodeSourcery.
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "system/memory.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/sh4/sh.h"
#include "hw/timer/tmu012.h"
#include "hw/ptimer.h"
#include "trace.h"

#define TIMER_TCR_TPSC          (7 << 0)
#define TIMER_TCR_CKEG          (3 << 3)
#define TIMER_TCR_UNIE          (1 << 5)
#define TIMER_TCR_ICPE          (3 << 6)
#define TIMER_TCR_UNF           (1 << 8)
#define TIMER_TCR_ICPF          (1 << 9)
#define TIMER_TCR_RESERVED      (0x3f << 10)

#define TIMER_FEAT_CAPT   (1 << 0)
#define TIMER_FEAT_EXTCLK (1 << 1)

#define OFFSET_TCOR   0
#define OFFSET_TCNT   1
#define OFFSET_TCR    2
#define OFFSET_TCPR   3

typedef struct {
    ptimer_state *timer;
    uint32_t tcnt;
    uint32_t tcor;
    uint32_t tcr;
    uint32_t tcpr;
    int freq;
    int int_level;
    int old_level;
    int feat;
    int enabled;
    qemu_irq irq;
} SHTimerState;

/* Check all active timers, and schedule the next timer interrupt. */

static void sh_timer_update(SHTimerState *s)
{
    int new_level = s->int_level && (s->tcr & TIMER_TCR_UNIE);

    if (new_level != s->old_level) {
        qemu_set_irq(s->irq, new_level);
    }
    s->old_level = s->int_level;
    s->int_level = new_level;
}

static uint32_t sh_timer_read(void *opaque, hwaddr offset)
{
    SHTimerState *s = opaque;

    switch (offset >> 2) {
    case OFFSET_TCOR:
        return s->tcor;
    case OFFSET_TCNT:
        return ptimer_get_count(s->timer);
    case OFFSET_TCR:
        return s->tcr | (s->int_level ? TIMER_TCR_UNF : 0);
    case OFFSET_TCPR:
        if (s->feat & TIMER_FEAT_CAPT) {
            return s->tcpr;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                  __func__, offset);
    return 0;
}

static void sh_timer_write(void *opaque, hwaddr offset, uint32_t value)
{
    SHTimerState *s = opaque;
    int freq;

    switch (offset >> 2) {
    case OFFSET_TCOR:
        s->tcor = value;
        ptimer_transaction_begin(s->timer);
        ptimer_set_limit(s->timer, s->tcor, 0);
        ptimer_transaction_commit(s->timer);
        break;
    case OFFSET_TCNT:
        s->tcnt = value;
        ptimer_transaction_begin(s->timer);
        ptimer_set_count(s->timer, s->tcnt);
        ptimer_transaction_commit(s->timer);
        break;
    case OFFSET_TCR:
        ptimer_transaction_begin(s->timer);
        if (s->enabled) {
            /*
             * Pause the timer if it is running. This may cause some inaccuracy
             * due to rounding, but avoids a whole lot of other messiness
             */
            ptimer_stop(s->timer);
        }
        freq = s->freq;
        /* ??? Need to recalculate expiry time after changing divisor.  */
        switch (value & TIMER_TCR_TPSC) {
        case 0:
            freq >>= 2;
            break;
        case 1:
            freq >>= 4;
            break;
        case 2:
            freq >>= 6;
            break;
        case 3:
            freq >>= 8;
            break;
        case 4:
            freq >>= 10;
            break;
        case 6:
        case 7:
            if (s->feat & TIMER_FEAT_EXTCLK) {
                break;
            }
            /* fallthrough */
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Reserved TPSC value\n", __func__);
        }
        switch ((value & TIMER_TCR_CKEG) >> 3) {
        case 0:
            break;
        case 1:
        case 2:
        case 3:
            if (s->feat & TIMER_FEAT_EXTCLK) {
                break;
            }
            /* fallthrough */
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Reserved CKEG value\n", __func__);
        }
        switch ((value & TIMER_TCR_ICPE) >> 6) {
        case 0:
            break;
        case 2:
        case 3:
            if (s->feat & TIMER_FEAT_CAPT) {
                break;
            }
            /* fallthrough */
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Reserved ICPE value\n", __func__);
        }
        if ((value & TIMER_TCR_UNF) == 0) {
            s->int_level = 0;
        }

        value &= ~TIMER_TCR_UNF;

        if ((value & TIMER_TCR_ICPF) && (!(s->feat & TIMER_FEAT_CAPT))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Reserved ICPF value\n", __func__);
        }

        value &= ~TIMER_TCR_ICPF; /* capture not supported */

        if (value & TIMER_TCR_RESERVED) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Reserved TCR bits set\n", __func__);
        }
        s->tcr = value;
        ptimer_set_limit(s->timer, s->tcor, 0);
        ptimer_set_freq(s->timer, freq);
        if (s->enabled) {
            /* Restart the timer if still enabled.  */
            ptimer_run(s->timer, 0);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case OFFSET_TCPR:
        if (s->feat & TIMER_FEAT_CAPT) {
            s->tcpr = value;
            break;
        }
        /* fallthrough */
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, offset);
    }
    sh_timer_update(s);
}

static void sh_timer_start_stop(void *opaque, int enable)
{
    SHTimerState *s = opaque;

    trace_sh_timer_start_stop(enable, s->enabled);
    ptimer_transaction_begin(s->timer);
    if (s->enabled && !enable) {
        ptimer_stop(s->timer);
    }
    if (!s->enabled && enable) {
        ptimer_run(s->timer, 0);
    }
    ptimer_transaction_commit(s->timer);
    s->enabled = !!enable;
}

static void sh_timer_tick(void *opaque)
{
    SHTimerState *s = opaque;
    s->int_level = s->enabled;
    sh_timer_update(s);
}

static void *sh_timer_init(uint32_t freq, int feat, qemu_irq irq)
{
    SHTimerState *s;

    s = g_malloc0(sizeof(*s));
    s->freq = freq;
    s->feat = feat;
    s->tcor = 0xffffffff;
    s->tcnt = 0xffffffff;
    s->tcpr = 0xdeadbeef;
    s->tcr = 0;
    s->enabled = 0;
    s->irq = irq;

    s->timer = ptimer_init(sh_timer_tick, s, PTIMER_POLICY_LEGACY);

    sh_timer_write(s, OFFSET_TCOR >> 2, s->tcor);
    sh_timer_write(s, OFFSET_TCNT >> 2, s->tcnt);
    sh_timer_write(s, OFFSET_TCPR >> 2, s->tcpr);
    sh_timer_write(s, OFFSET_TCR  >> 2, s->tcpr);
    /* ??? Save/restore.  */
    return s;
}

typedef struct {
    MemoryRegion iomem;
    MemoryRegion iomem_p4;
    MemoryRegion iomem_a7;
    void *timer[3];
    int level[3];
    uint32_t tocr;
    uint32_t tstr;
    int feat;
} tmu012_state;

static uint64_t tmu012_read(void *opaque, hwaddr offset, unsigned size)
{
    tmu012_state *s = opaque;

    trace_sh_timer_read(offset);
    if (offset >= 0x20) {
        if (!(s->feat & TMU012_FEAT_3CHAN)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad channel offset 0x%" HWADDR_PRIx "\n",
                          __func__, offset);
        }
        return sh_timer_read(s->timer[2], offset - 0x20);
    }

    if (offset >= 0x14) {
        return sh_timer_read(s->timer[1], offset - 0x14);
    }
    if (offset >= 0x08) {
        return sh_timer_read(s->timer[0], offset - 0x08);
    }
    if (offset == 4) {
        return s->tstr;
    }
    if ((s->feat & TMU012_FEAT_TOCR) && offset == 0) {
        return s->tocr;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, offset);
    return 0;
}

static void tmu012_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    tmu012_state *s = opaque;

    trace_sh_timer_write(offset, value);
    if (offset >= 0x20) {
        if (!(s->feat & TMU012_FEAT_3CHAN)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad channel offset 0x%" HWADDR_PRIx "\n",
                          __func__, offset);
        }
        sh_timer_write(s->timer[2], offset - 0x20, value);
        return;
    }

    if (offset >= 0x14) {
        sh_timer_write(s->timer[1], offset - 0x14, value);
        return;
    }

    if (offset >= 0x08) {
        sh_timer_write(s->timer[0], offset - 0x08, value);
        return;
    }

    if (offset == 4) {
        sh_timer_start_stop(s->timer[0], value & (1 << 0));
        sh_timer_start_stop(s->timer[1], value & (1 << 1));
        if (s->feat & TMU012_FEAT_3CHAN) {
            sh_timer_start_stop(s->timer[2], value & (1 << 2));
        } else {
            if (value & (1 << 2)) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad channel\n", __func__);
            }
        }

        s->tstr = value;
        return;
    }

    if ((s->feat & TMU012_FEAT_TOCR) && offset == 0) {
        s->tocr = value & (1 << 0);
    }
}

static const MemoryRegionOps tmu012_ops = {
    .read = tmu012_read,
    .write = tmu012_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void tmu012_init(MemoryRegion *sysmem, hwaddr base, int feat, uint32_t freq,
                 qemu_irq ch0_irq, qemu_irq ch1_irq,
                 qemu_irq ch2_irq0, qemu_irq ch2_irq1)
{
    tmu012_state *s;
    int timer_feat = (feat & TMU012_FEAT_EXTCLK) ? TIMER_FEAT_EXTCLK : 0;

    s = g_malloc0(sizeof(*s));
    s->feat = feat;
    s->timer[0] = sh_timer_init(freq, timer_feat, ch0_irq);
    s->timer[1] = sh_timer_init(freq, timer_feat, ch1_irq);
    if (feat & TMU012_FEAT_3CHAN) {
        s->timer[2] = sh_timer_init(freq, timer_feat | TIMER_FEAT_CAPT,
                                    ch2_irq0); /* ch2_irq1 not supported */
    }

    memory_region_init_io(&s->iomem, NULL, &tmu012_ops, s, "timer", 0x30);

    memory_region_init_alias(&s->iomem_p4, NULL, "timer-p4",
                             &s->iomem, 0, memory_region_size(&s->iomem));
    memory_region_add_subregion(sysmem, P4ADDR(base), &s->iomem_p4);

    memory_region_init_alias(&s->iomem_a7, NULL, "timer-a7",
                             &s->iomem, 0, memory_region_size(&s->iomem));
    memory_region_add_subregion(sysmem, A7ADDR(base), &s->iomem_a7);
    /* ??? Save/restore.  */
}
