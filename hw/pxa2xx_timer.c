/*
 * Intel XScale PXA255/270 OS Timers.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Copyright (c) 2006 Thorsten Zitterell
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "pxa.h"

#define OSMR0	0x00
#define OSMR1	0x04
#define OSMR2	0x08
#define OSMR3	0x0c
#define OSMR4	0x80
#define OSMR5	0x84
#define OSMR6	0x88
#define OSMR7	0x8c
#define OSMR8	0x90
#define OSMR9	0x94
#define OSMR10	0x98
#define OSMR11	0x9c
#define OSCR	0x10	/* OS Timer Count */
#define OSCR4	0x40
#define OSCR5	0x44
#define OSCR6	0x48
#define OSCR7	0x4c
#define OSCR8	0x50
#define OSCR9	0x54
#define OSCR10	0x58
#define OSCR11	0x5c
#define OSSR	0x14	/* Timer status register */
#define OWER	0x18
#define OIER	0x1c	/* Interrupt enable register  3-0 to E3-E0 */
#define OMCR4	0xc0	/* OS Match Control registers */
#define OMCR5	0xc4
#define OMCR6	0xc8
#define OMCR7	0xcc
#define OMCR8	0xd0
#define OMCR9	0xd4
#define OMCR10	0xd8
#define OMCR11	0xdc
#define OSNR	0x20

#define PXA25X_FREQ	3686400	/* 3.6864 MHz */
#define PXA27X_FREQ	3250000	/* 3.25 MHz */

static int pxa2xx_timer4_freq[8] = {
    [0] = 0,
    [1] = 32768,
    [2] = 1000,
    [3] = 1,
    [4] = 1000000,
    /* [5] is the "Externally supplied clock".  Assign if necessary.  */
    [5 ... 7] = 0,
};

struct pxa2xx_timer0_s {
    uint32_t value;
    int level;
    qemu_irq irq;
    QEMUTimer *qtimer;
    int num;
    void *info;
};

struct pxa2xx_timer4_s {
    struct pxa2xx_timer0_s tm;
    int32_t oldclock;
    int32_t clock;
    uint64_t lastload;
    uint32_t freq;
    uint32_t control;
};

typedef struct {
    int32_t clock;
    int32_t oldclock;
    uint64_t lastload;
    uint32_t freq;
    struct pxa2xx_timer0_s timer[4];
    struct pxa2xx_timer4_s *tm4;
    uint32_t events;
    uint32_t irq_enabled;
    uint32_t reset3;
    uint32_t snapshot;
} pxa2xx_timer_info;

static void pxa2xx_timer_update(void *opaque, uint64_t now_qemu)
{
    pxa2xx_timer_info *s = (pxa2xx_timer_info *) opaque;
    int i;
    uint32_t now_vm;
    uint64_t new_qemu;

    now_vm = s->clock +
            muldiv64(now_qemu - s->lastload, s->freq, ticks_per_sec);

    for (i = 0; i < 4; i ++) {
        new_qemu = now_qemu + muldiv64((uint32_t) (s->timer[i].value - now_vm),
                        ticks_per_sec, s->freq);
        qemu_mod_timer(s->timer[i].qtimer, new_qemu);
    }
}

static void pxa2xx_timer_update4(void *opaque, uint64_t now_qemu, int n)
{
    pxa2xx_timer_info *s = (pxa2xx_timer_info *) opaque;
    uint32_t now_vm;
    uint64_t new_qemu;
    static const int counters[8] = { 0, 0, 0, 0, 4, 4, 6, 6 };
    int counter;

    if (s->tm4[n].control & (1 << 7))
        counter = n;
    else
        counter = counters[n];

    if (!s->tm4[counter].freq) {
        qemu_del_timer(s->tm4[n].tm.qtimer);
        return;
    }

    now_vm = s->tm4[counter].clock + muldiv64(now_qemu -
                    s->tm4[counter].lastload,
                    s->tm4[counter].freq, ticks_per_sec);

    new_qemu = now_qemu + muldiv64((uint32_t) (s->tm4[n].tm.value - now_vm),
                    ticks_per_sec, s->tm4[counter].freq);
    qemu_mod_timer(s->tm4[n].tm.qtimer, new_qemu);
}

static uint32_t pxa2xx_timer_read(void *opaque, target_phys_addr_t offset)
{
    pxa2xx_timer_info *s = (pxa2xx_timer_info *) opaque;
    int tm = 0;

    switch (offset) {
    case OSMR3:  tm ++;
    case OSMR2:  tm ++;
    case OSMR1:  tm ++;
    case OSMR0:
        return s->timer[tm].value;
    case OSMR11: tm ++;
    case OSMR10: tm ++;
    case OSMR9:  tm ++;
    case OSMR8:  tm ++;
    case OSMR7:  tm ++;
    case OSMR6:  tm ++;
    case OSMR5:  tm ++;
    case OSMR4:
        if (!s->tm4)
            goto badreg;
        return s->tm4[tm].tm.value;
    case OSCR:
        return s->clock + muldiv64(qemu_get_clock(vm_clock) -
                        s->lastload, s->freq, ticks_per_sec);
    case OSCR11: tm ++;
    case OSCR10: tm ++;
    case OSCR9:  tm ++;
    case OSCR8:  tm ++;
    case OSCR7:  tm ++;
    case OSCR6:  tm ++;
    case OSCR5:  tm ++;
    case OSCR4:
        if (!s->tm4)
            goto badreg;

        if ((tm == 9 - 4 || tm == 11 - 4) && (s->tm4[tm].control & (1 << 9))) {
            if (s->tm4[tm - 1].freq)
                s->snapshot = s->tm4[tm - 1].clock + muldiv64(
                                qemu_get_clock(vm_clock) -
                                s->tm4[tm - 1].lastload,
                                s->tm4[tm - 1].freq, ticks_per_sec);
            else
                s->snapshot = s->tm4[tm - 1].clock;
        }

        if (!s->tm4[tm].freq)
            return s->tm4[tm].clock;
        return s->tm4[tm].clock + muldiv64(qemu_get_clock(vm_clock) -
                        s->tm4[tm].lastload, s->tm4[tm].freq, ticks_per_sec);
    case OIER:
        return s->irq_enabled;
    case OSSR:	/* Status register */
        return s->events;
    case OWER:
        return s->reset3;
    case OMCR11: tm ++;
    case OMCR10: tm ++;
    case OMCR9:  tm ++;
    case OMCR8:  tm ++;
    case OMCR7:  tm ++;
    case OMCR6:  tm ++;
    case OMCR5:  tm ++;
    case OMCR4:
        if (!s->tm4)
            goto badreg;
        return s->tm4[tm].control;
    case OSNR:
        return s->snapshot;
    default:
    badreg:
        cpu_abort(cpu_single_env, "pxa2xx_timer_read: Bad offset "
                        REG_FMT "\n", offset);
    }

    return 0;
}

static void pxa2xx_timer_write(void *opaque, target_phys_addr_t offset,
                uint32_t value)
{
    int i, tm = 0;
    pxa2xx_timer_info *s = (pxa2xx_timer_info *) opaque;

    switch (offset) {
    case OSMR3:  tm ++;
    case OSMR2:  tm ++;
    case OSMR1:  tm ++;
    case OSMR0:
        s->timer[tm].value = value;
        pxa2xx_timer_update(s, qemu_get_clock(vm_clock));
        break;
    case OSMR11: tm ++;
    case OSMR10: tm ++;
    case OSMR9:  tm ++;
    case OSMR8:  tm ++;
    case OSMR7:  tm ++;
    case OSMR6:  tm ++;
    case OSMR5:  tm ++;
    case OSMR4:
        if (!s->tm4)
            goto badreg;
        s->tm4[tm].tm.value = value;
        pxa2xx_timer_update4(s, qemu_get_clock(vm_clock), tm);
        break;
    case OSCR:
        s->oldclock = s->clock;
        s->lastload = qemu_get_clock(vm_clock);
        s->clock = value;
        pxa2xx_timer_update(s, s->lastload);
        break;
    case OSCR11: tm ++;
    case OSCR10: tm ++;
    case OSCR9:  tm ++;
    case OSCR8:  tm ++;
    case OSCR7:  tm ++;
    case OSCR6:  tm ++;
    case OSCR5:  tm ++;
    case OSCR4:
        if (!s->tm4)
            goto badreg;
        s->tm4[tm].oldclock = s->tm4[tm].clock;
        s->tm4[tm].lastload = qemu_get_clock(vm_clock);
        s->tm4[tm].clock = value;
        pxa2xx_timer_update4(s, s->tm4[tm].lastload, tm);
        break;
    case OIER:
        s->irq_enabled = value & 0xfff;
        break;
    case OSSR:	/* Status register */
        s->events &= ~value;
        for (i = 0; i < 4; i ++, value >>= 1) {
            if (s->timer[i].level && (value & 1)) {
                s->timer[i].level = 0;
                qemu_irq_lower(s->timer[i].irq);
            }
        }
        if (s->tm4) {
            for (i = 0; i < 8; i ++, value >>= 1)
                if (s->tm4[i].tm.level && (value & 1))
                    s->tm4[i].tm.level = 0;
            if (!(s->events & 0xff0))
                qemu_irq_lower(s->tm4->tm.irq);
        }
        break;
    case OWER:	/* XXX: Reset on OSMR3 match? */
        s->reset3 = value;
        break;
    case OMCR7:  tm ++;
    case OMCR6:  tm ++;
    case OMCR5:  tm ++;
    case OMCR4:
        if (!s->tm4)
            goto badreg;
        s->tm4[tm].control = value & 0x0ff;
        /* XXX Stop if running (shouldn't happen) */
        if ((value & (1 << 7)) || tm == 0)
            s->tm4[tm].freq = pxa2xx_timer4_freq[value & 7];
        else {
            s->tm4[tm].freq = 0;
            pxa2xx_timer_update4(s, qemu_get_clock(vm_clock), tm);
        }
        break;
    case OMCR11: tm ++;
    case OMCR10: tm ++;
    case OMCR9:  tm ++;
    case OMCR8:  tm += 4;
        if (!s->tm4)
            goto badreg;
        s->tm4[tm].control = value & 0x3ff;
        /* XXX Stop if running (shouldn't happen) */
        if ((value & (1 << 7)) || !(tm & 1))
            s->tm4[tm].freq =
                    pxa2xx_timer4_freq[(value & (1 << 8)) ?  0 : (value & 7)];
        else {
            s->tm4[tm].freq = 0;
            pxa2xx_timer_update4(s, qemu_get_clock(vm_clock), tm);
        }
        break;
    default:
    badreg:
        cpu_abort(cpu_single_env, "pxa2xx_timer_write: Bad offset "
                        REG_FMT "\n", offset);
    }
}

static CPUReadMemoryFunc *pxa2xx_timer_readfn[] = {
    pxa2xx_timer_read,
    pxa2xx_timer_read,
    pxa2xx_timer_read,
};

static CPUWriteMemoryFunc *pxa2xx_timer_writefn[] = {
    pxa2xx_timer_write,
    pxa2xx_timer_write,
    pxa2xx_timer_write,
};

static void pxa2xx_timer_tick(void *opaque)
{
    struct pxa2xx_timer0_s *t = (struct pxa2xx_timer0_s *) opaque;
    pxa2xx_timer_info *i = (pxa2xx_timer_info *) t->info;

    if (i->irq_enabled & (1 << t->num)) {
        t->level = 1;
        i->events |= 1 << t->num;
        qemu_irq_raise(t->irq);
    }

    if (t->num == 3)
        if (i->reset3 & 1) {
            i->reset3 = 0;
            qemu_system_reset_request();
        }
}

static void pxa2xx_timer_tick4(void *opaque)
{
    struct pxa2xx_timer4_s *t = (struct pxa2xx_timer4_s *) opaque;
    pxa2xx_timer_info *i = (pxa2xx_timer_info *) t->tm.info;

    pxa2xx_timer_tick(&t->tm);
    if (t->control & (1 << 3))
        t->clock = 0;
    if (t->control & (1 << 6))
        pxa2xx_timer_update4(i, qemu_get_clock(vm_clock), t->tm.num - 4);
}

static void pxa2xx_timer_save(QEMUFile *f, void *opaque)
{
    pxa2xx_timer_info *s = (pxa2xx_timer_info *) opaque;
    int i;

    qemu_put_be32s(f, (uint32_t *) &s->clock);
    qemu_put_be32s(f, (uint32_t *) &s->oldclock);
    qemu_put_be64s(f, &s->lastload);

    for (i = 0; i < 4; i ++) {
        qemu_put_be32s(f, &s->timer[i].value);
        qemu_put_be32(f, s->timer[i].level);
    }
    if (s->tm4)
        for (i = 0; i < 8; i ++) {
            qemu_put_be32s(f, &s->tm4[i].tm.value);
            qemu_put_be32(f, s->tm4[i].tm.level);
            qemu_put_sbe32s(f, &s->tm4[i].oldclock);
            qemu_put_sbe32s(f, &s->tm4[i].clock);
            qemu_put_be64s(f, &s->tm4[i].lastload);
            qemu_put_be32s(f, &s->tm4[i].freq);
            qemu_put_be32s(f, &s->tm4[i].control);
        }

    qemu_put_be32s(f, &s->events);
    qemu_put_be32s(f, &s->irq_enabled);
    qemu_put_be32s(f, &s->reset3);
    qemu_put_be32s(f, &s->snapshot);
}

static int pxa2xx_timer_load(QEMUFile *f, void *opaque, int version_id)
{
    pxa2xx_timer_info *s = (pxa2xx_timer_info *) opaque;
    int64_t now;
    int i;

    qemu_get_be32s(f, (uint32_t *) &s->clock);
    qemu_get_be32s(f, (uint32_t *) &s->oldclock);
    qemu_get_be64s(f, &s->lastload);

    now = qemu_get_clock(vm_clock);
    for (i = 0; i < 4; i ++) {
        qemu_get_be32s(f, &s->timer[i].value);
        s->timer[i].level = qemu_get_be32(f);
    }
    pxa2xx_timer_update(s, now);

    if (s->tm4)
        for (i = 0; i < 8; i ++) {
            qemu_get_be32s(f, &s->tm4[i].tm.value);
            s->tm4[i].tm.level = qemu_get_be32(f);
            qemu_get_sbe32s(f, &s->tm4[i].oldclock);
            qemu_get_sbe32s(f, &s->tm4[i].clock);
            qemu_get_be64s(f, &s->tm4[i].lastload);
            qemu_get_be32s(f, &s->tm4[i].freq);
            qemu_get_be32s(f, &s->tm4[i].control);
            pxa2xx_timer_update4(s, now, i);
        }

    qemu_get_be32s(f, &s->events);
    qemu_get_be32s(f, &s->irq_enabled);
    qemu_get_be32s(f, &s->reset3);
    qemu_get_be32s(f, &s->snapshot);

    return 0;
}

static pxa2xx_timer_info *pxa2xx_timer_init(target_phys_addr_t base,
                qemu_irq *irqs)
{
    int i;
    int iomemtype;
    pxa2xx_timer_info *s;

    s = (pxa2xx_timer_info *) qemu_mallocz(sizeof(pxa2xx_timer_info));
    s->irq_enabled = 0;
    s->oldclock = 0;
    s->clock = 0;
    s->lastload = qemu_get_clock(vm_clock);
    s->reset3 = 0;

    for (i = 0; i < 4; i ++) {
        s->timer[i].value = 0;
        s->timer[i].irq = irqs[i];
        s->timer[i].info = s;
        s->timer[i].num = i;
        s->timer[i].level = 0;
        s->timer[i].qtimer = qemu_new_timer(vm_clock,
                        pxa2xx_timer_tick, &s->timer[i]);
    }

    iomemtype = cpu_register_io_memory(0, pxa2xx_timer_readfn,
                    pxa2xx_timer_writefn, s);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);

    register_savevm("pxa2xx_timer", 0, 0,
                    pxa2xx_timer_save, pxa2xx_timer_load, s);

    return s;
}

void pxa25x_timer_init(target_phys_addr_t base, qemu_irq *irqs)
{
    pxa2xx_timer_info *s = pxa2xx_timer_init(base, irqs);
    s->freq = PXA25X_FREQ;
    s->tm4 = 0;
}

void pxa27x_timer_init(target_phys_addr_t base,
                qemu_irq *irqs, qemu_irq irq4)
{
    pxa2xx_timer_info *s = pxa2xx_timer_init(base, irqs);
    int i;
    s->freq = PXA27X_FREQ;
    s->tm4 = (struct pxa2xx_timer4_s *) qemu_mallocz(8 *
                    sizeof(struct pxa2xx_timer4_s));
    for (i = 0; i < 8; i ++) {
        s->tm4[i].tm.value = 0;
        s->tm4[i].tm.irq = irq4;
        s->tm4[i].tm.info = s;
        s->tm4[i].tm.num = i + 4;
        s->tm4[i].tm.level = 0;
        s->tm4[i].freq = 0;
        s->tm4[i].control = 0x0;
        s->tm4[i].tm.qtimer = qemu_new_timer(vm_clock,
                        pxa2xx_timer_tick4, &s->tm4[i]);
    }
}
