/*
 * SuperH Timer modules.
 *
 * Copyright (c) 2007 Magnus Damm
 * Based on arm_timer.c by Paul Brook
 * Copyright (c) 2005-2006 CodeSourcery.
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "sh.h"
#include "qemu-timer.h"

//#define DEBUG_TIMER

#define TIMER_TCR_TPSC          (7 << 0)
#define TIMER_TCR_CKEG          (3 << 3)
#define TIMER_TCR_UNIE          (1 << 5)
#define TIMER_TCR_ICPE          (3 << 6)
#define TIMER_TCR_UNF           (1 << 8)
#define TIMER_TCR_ICPF          (1 << 9)
#define TIMER_TCR_RESERVED      (0x3f << 10)

#define TIMER_FEAT_CAPT   (1 << 0)
#define TIMER_FEAT_EXTCLK (1 << 1)

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
    struct intc_source *irq;
} sh_timer_state;

/* Check all active timers, and schedule the next timer interrupt. */

static void sh_timer_update(sh_timer_state *s)
{
    int new_level = s->int_level && (s->tcr & TIMER_TCR_UNIE);

    if (new_level != s->old_level)
      sh_intc_toggle_source(s->irq, 0, new_level ? 1 : -1);

    s->old_level = s->int_level;
    s->int_level = new_level;
}

static uint32_t sh_timer_read(void *opaque, target_phys_addr_t offset)
{
    sh_timer_state *s = (sh_timer_state *)opaque;

    switch (offset >> 2) {
    case 0:
        return s->tcor;
    case 1:
        return ptimer_get_count(s->timer);
    case 2:
        return s->tcr | (s->int_level ? TIMER_TCR_UNF : 0);
    case 3:
        if (s->feat & TIMER_FEAT_CAPT)
            return s->tcpr;
    default:
        cpu_abort (cpu_single_env, "sh_timer_read: Bad offset %x\n",
                   (int)offset);
        return 0;
    }
}

static void sh_timer_write(void *opaque, target_phys_addr_t offset,
                            uint32_t value)
{
    sh_timer_state *s = (sh_timer_state *)opaque;
    int freq;

    switch (offset >> 2) {
    case 0:
        s->tcor = value;
        ptimer_set_limit(s->timer, s->tcor, 0);
        break;
    case 1:
        s->tcnt = value;
        ptimer_set_count(s->timer, s->tcnt);
        break;
    case 2:
        if (s->enabled) {
            /* Pause the timer if it is running.  This may cause some
               inaccuracy dure to rounding, but avoids a whole lot of other
               messyness.  */
            ptimer_stop(s->timer);
        }
        freq = s->freq;
        /* ??? Need to recalculate expiry time after changing divisor.  */
        switch (value & TIMER_TCR_TPSC) {
        case 0: freq >>= 2; break;
        case 1: freq >>= 4; break;
        case 2: freq >>= 6; break;
        case 3: freq >>= 8; break;
        case 4: freq >>= 10; break;
	case 6:
	case 7: if (s->feat & TIMER_FEAT_EXTCLK) break;
	default: cpu_abort (cpu_single_env,
			   "sh_timer_write: Reserved TPSC value\n"); break;
        }
        switch ((value & TIMER_TCR_CKEG) >> 3) {
	case 0: break;
        case 1:
        case 2:
        case 3: if (s->feat & TIMER_FEAT_EXTCLK) break;
	default: cpu_abort (cpu_single_env,
			   "sh_timer_write: Reserved CKEG value\n"); break;
        }
        switch ((value & TIMER_TCR_ICPE) >> 6) {
	case 0: break;
        case 2:
        case 3: if (s->feat & TIMER_FEAT_CAPT) break;
	default: cpu_abort (cpu_single_env,
			   "sh_timer_write: Reserved ICPE value\n"); break;
        }
	if ((value & TIMER_TCR_UNF) == 0)
            s->int_level = 0;

	value &= ~TIMER_TCR_UNF;

	if ((value & TIMER_TCR_ICPF) && (!(s->feat & TIMER_FEAT_CAPT)))
            cpu_abort (cpu_single_env,
		       "sh_timer_write: Reserved ICPF value\n");

	value &= ~TIMER_TCR_ICPF; /* capture not supported */

	if (value & TIMER_TCR_RESERVED)
            cpu_abort (cpu_single_env,
		       "sh_timer_write: Reserved TCR bits set\n");
        s->tcr = value;
        ptimer_set_limit(s->timer, s->tcor, 0);
        ptimer_set_freq(s->timer, freq);
        if (s->enabled) {
            /* Restart the timer if still enabled.  */
            ptimer_run(s->timer, 0);
        }
        break;
    case 3:
        if (s->feat & TIMER_FEAT_CAPT) {
            s->tcpr = value;
	    break;
	}
    default:
        cpu_abort (cpu_single_env, "sh_timer_write: Bad offset %x\n",
                   (int)offset);
    }
    sh_timer_update(s);
}

static void sh_timer_start_stop(void *opaque, int enable)
{
    sh_timer_state *s = (sh_timer_state *)opaque;

#ifdef DEBUG_TIMER
    printf("sh_timer_start_stop %d (%d)\n", enable, s->enabled);
#endif

    if (s->enabled && !enable) {
        ptimer_stop(s->timer);
    }
    if (!s->enabled && enable) {
        ptimer_run(s->timer, 0);
    }
    s->enabled = !!enable;

#ifdef DEBUG_TIMER
    printf("sh_timer_start_stop done %d\n", s->enabled);
#endif
}

static void sh_timer_tick(void *opaque)
{
    sh_timer_state *s = (sh_timer_state *)opaque;
    s->int_level = s->enabled;
    sh_timer_update(s);
}

static void *sh_timer_init(uint32_t freq, int feat, struct intc_source *irq)
{
    sh_timer_state *s;
    QEMUBH *bh;

    s = (sh_timer_state *)qemu_mallocz(sizeof(sh_timer_state));
    s->freq = freq;
    s->feat = feat;
    s->tcor = 0xffffffff;
    s->tcnt = 0xffffffff;
    s->tcpr = 0xdeadbeef;
    s->tcor = 0;
    s->enabled = 0;
    s->irq = irq;

    bh = qemu_bh_new(sh_timer_tick, s);
    s->timer = ptimer_init(bh);
    /* ??? Save/restore.  */
    return s;
}

typedef struct {
    void *timer[3];
    int level[3];
    uint32_t tocr;
    uint32_t tstr;
    target_phys_addr_t base;
    int feat;
} tmu012_state;

static uint32_t tmu012_read(void *opaque, target_phys_addr_t offset)
{
    tmu012_state *s = (tmu012_state *)opaque;

#ifdef DEBUG_TIMER
    printf("tmu012_read 0x%lx\n", (unsigned long) offset);
#endif
    offset -= s->base;

    if (offset >= 0x20) {
        if (!(s->feat & TMU012_FEAT_3CHAN))
	    cpu_abort (cpu_single_env, "tmu012_write: Bad channel offset %x\n",
		       (int)offset);
        return sh_timer_read(s->timer[2], offset - 0x20);
    }

    if (offset >= 0x14)
        return sh_timer_read(s->timer[1], offset - 0x14);

    if (offset >= 0x08)
        return sh_timer_read(s->timer[0], offset - 0x08);

    if (offset == 4)
        return s->tstr;

    if ((s->feat & TMU012_FEAT_TOCR) && offset == 0)
        return s->tocr;

    cpu_abort (cpu_single_env, "tmu012_write: Bad offset %x\n",
	       (int)offset);
    return 0;
}

static void tmu012_write(void *opaque, target_phys_addr_t offset,
                        uint32_t value)
{
    tmu012_state *s = (tmu012_state *)opaque;

#ifdef DEBUG_TIMER
    printf("tmu012_write 0x%lx 0x%08x\n", (unsigned long) offset, value);
#endif
    offset -= s->base;

    if (offset >= 0x20) {
        if (!(s->feat & TMU012_FEAT_3CHAN))
	    cpu_abort (cpu_single_env, "tmu012_write: Bad channel offset %x\n",
		       (int)offset);
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
        if (s->feat & TMU012_FEAT_3CHAN)
            sh_timer_start_stop(s->timer[2], value & (1 << 2));
	else
            if (value & (1 << 2))
                cpu_abort (cpu_single_env, "tmu012_write: Bad channel\n");

	s->tstr = value;
	return;
    }

    if ((s->feat & TMU012_FEAT_TOCR) && offset == 0) {
        s->tocr = value & (1 << 0);
    }
}

static CPUReadMemoryFunc *tmu012_readfn[] = {
    tmu012_read,
    tmu012_read,
    tmu012_read
};

static CPUWriteMemoryFunc *tmu012_writefn[] = {
    tmu012_write,
    tmu012_write,
    tmu012_write
};

void tmu012_init(target_phys_addr_t base, int feat, uint32_t freq,
		 struct intc_source *ch0_irq, struct intc_source *ch1_irq,
		 struct intc_source *ch2_irq0, struct intc_source *ch2_irq1)
{
    int iomemtype;
    tmu012_state *s;
    int timer_feat = (feat & TMU012_FEAT_EXTCLK) ? TIMER_FEAT_EXTCLK : 0;

    s = (tmu012_state *)qemu_mallocz(sizeof(tmu012_state));
    s->base = base;
    s->feat = feat;
    s->timer[0] = sh_timer_init(freq, timer_feat, ch0_irq);
    s->timer[1] = sh_timer_init(freq, timer_feat, ch1_irq);
    if (feat & TMU012_FEAT_3CHAN)
        s->timer[2] = sh_timer_init(freq, timer_feat | TIMER_FEAT_CAPT,
				    ch2_irq0); /* ch2_irq1 not supported */
    iomemtype = cpu_register_io_memory(0, tmu012_readfn,
                                       tmu012_writefn, s);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);
    /* ??? Save/restore.  */
}
