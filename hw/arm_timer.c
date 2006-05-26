/* 
 * ARM PrimeCell Timer modules.
 *
 * Copyright (c) 2005-2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "vl.h"
#include "arm_pic.h"

/* Common timer implementation.  */

#define TIMER_CTRL_ONESHOT      (1 << 0)
#define TIMER_CTRL_32BIT        (1 << 1)
#define TIMER_CTRL_DIV1         (0 << 2)
#define TIMER_CTRL_DIV16        (1 << 2)
#define TIMER_CTRL_DIV256       (2 << 2)
#define TIMER_CTRL_IE           (1 << 5)
#define TIMER_CTRL_PERIODIC     (1 << 6)
#define TIMER_CTRL_ENABLE       (1 << 7)

typedef struct {
    int64_t next_time;
    int64_t expires;
    int64_t loaded;
    QEMUTimer *timer;
    uint32_t control;
    uint32_t count;
    uint32_t limit;
    int raw_freq;
    int freq;
    int int_level;
    void *pic;
    int irq;
} arm_timer_state;

/* Calculate the new expiry time of the given timer.  */

static void arm_timer_reload(arm_timer_state *s)
{
    int64_t delay;

    s->loaded = s->expires;
    delay = muldiv64(s->count, ticks_per_sec, s->freq);
    if (delay == 0)
        delay = 1;
    s->expires += delay;
}

/* Check all active timers, and schedule the next timer interrupt.  */

static void arm_timer_update(arm_timer_state *s, int64_t now)
{
    int64_t next;

    /* Ignore disabled timers.  */
    if ((s->control & TIMER_CTRL_ENABLE) == 0)
        return;
    /* Ignore expired one-shot timers.  */
    if (s->count == 0 && (s->control & TIMER_CTRL_ONESHOT))
        return;
    if (s->expires - now <= 0) {
        /* Timer has expired.  */
        s->int_level = 1;
        if (s->control & TIMER_CTRL_ONESHOT) {
            /* One-shot.  */
            s->count = 0;
        } else {
            if ((s->control & TIMER_CTRL_PERIODIC) == 0) {
                /* Free running.  */
                if (s->control & TIMER_CTRL_32BIT)
                    s->count = 0xffffffff;
                else
                    s->count = 0xffff;
            } else {
                  /* Periodic.  */
                  s->count = s->limit;
            }
        }
    }
    while (s->expires - now <= 0) {
        arm_timer_reload(s);
    }
    /* Update interrupts.  */
    if (s->int_level && (s->control & TIMER_CTRL_IE)) {
        pic_set_irq_new(s->pic, s->irq, 1);
    } else {
        pic_set_irq_new(s->pic, s->irq, 0);
    }

    next = now;
    if (next - s->expires < 0)
        next = s->expires;

    /* Schedule the next timer interrupt.  */
    if (next == now) {
        qemu_del_timer(s->timer);
        s->next_time = 0;
    } else if (next != s->next_time) {
        qemu_mod_timer(s->timer, next);
        s->next_time = next;
    }
}

/* Return the current value of the timer.  */
static uint32_t arm_timer_getcount(arm_timer_state *s, int64_t now)
{
    int64_t elapsed;
    int64_t period;

    if (s->count == 0)
        return 0;
    if ((s->control & TIMER_CTRL_ENABLE) == 0)
        return s->count;
    elapsed = now - s->loaded;
    period = s->expires - s->loaded;
    /* If the timer should have expired then return 0.  This can happen
       when the host timer signal doesnt occur immediately.  It's better to
       have a timer appear to sit at zero for a while than have it wrap
       around before the guest interrupt is raised.  */
    /* ??? Could we trigger the interrupt here?  */
    if (elapsed > period)
        return 0;
    /* We need to calculate count * elapsed / period without overfowing.
       Scale both elapsed and period so they fit in a 32-bit int.  */
    while (period != (int32_t)period) {
        period >>= 1;
        elapsed >>= 1;
    }
    return ((uint64_t)s->count * (uint64_t)(int32_t)elapsed)
            / (int32_t)period;
}

uint32_t arm_timer_read(void *opaque, target_phys_addr_t offset)
{
    arm_timer_state *s = (arm_timer_state *)opaque;

    switch (offset >> 2) {
    case 0: /* TimerLoad */
    case 6: /* TimerBGLoad */
        return s->limit;
    case 1: /* TimerValue */
        return arm_timer_getcount(s, qemu_get_clock(vm_clock));
    case 2: /* TimerControl */
        return s->control;
    case 4: /* TimerRIS */
        return s->int_level;
    case 5: /* TimerMIS */
        if ((s->control & TIMER_CTRL_IE) == 0)
            return 0;
        return s->int_level;
    default:
        cpu_abort (cpu_single_env, "arm_timer_read: Bad offset %x\n", offset);
        return 0;
    }
}

static void arm_timer_write(void *opaque, target_phys_addr_t offset,
                            uint32_t value)
{
    arm_timer_state *s = (arm_timer_state *)opaque;
    int64_t now;

    now = qemu_get_clock(vm_clock);
    switch (offset >> 2) {
    case 0: /* TimerLoad */
        s->limit = value;
        s->count = value;
        s->expires = now;
        arm_timer_reload(s);
        break;
    case 1: /* TimerValue */
        /* ??? Linux seems to want to write to this readonly register.
           Ignore it.  */
        break;
    case 2: /* TimerControl */
        if (s->control & TIMER_CTRL_ENABLE) {
            /* Pause the timer if it is running.  This may cause some
               inaccuracy dure to rounding, but avoids a whole lot of other
               messyness.  */
            s->count = arm_timer_getcount(s, now);
        }
        s->control = value;
        s->freq = s->raw_freq;
        /* ??? Need to recalculate expiry time after changing divisor.  */
        switch ((value >> 2) & 3) {
        case 1: s->freq >>= 4; break;
        case 2: s->freq >>= 8; break;
        }
        if (s->control & TIMER_CTRL_ENABLE) {
            /* Restart the timer if still enabled.  */
            s->expires = now;
            arm_timer_reload(s);
        }
        break;
    case 3: /* TimerIntClr */
        s->int_level = 0;
        break;
    case 6: /* TimerBGLoad */
        s->limit = value;
        break;
    default:
        cpu_abort (cpu_single_env, "arm_timer_write: Bad offset %x\n", offset);
    }
    arm_timer_update(s, now);
}

static void arm_timer_tick(void *opaque)
{
    int64_t now;

    now = qemu_get_clock(vm_clock);
    arm_timer_update((arm_timer_state *)opaque, now);
}

static void *arm_timer_init(uint32_t freq, void *pic, int irq)
{
    arm_timer_state *s;

    s = (arm_timer_state *)qemu_mallocz(sizeof(arm_timer_state));
    s->pic = pic;
    s->irq = irq;
    s->raw_freq = s->freq = 1000000;
    s->control = TIMER_CTRL_IE;
    s->count = 0xffffffff;

    s->timer = qemu_new_timer(vm_clock, arm_timer_tick, s);
    /* ??? Save/restore.  */
    return s;
}

/* ARM PrimeCell SP804 dual timer module.
   Docs for this device don't seem to be publicly available.  This
   implementation is based on gueswork, the linux kernel sources and the
   Integrator/CP timer modules.  */

typedef struct {
    /* Include a pseudo-PIC device to merge the two interrupt sources.  */
    arm_pic_handler handler;
    void *timer[2];
    int level[2];
    uint32_t base;
    /* The output PIC device.  */
    void *pic;
    int irq;
} sp804_state;

static void sp804_set_irq(void *opaque, int irq, int level)
{
    sp804_state *s = (sp804_state *)opaque;

    s->level[irq] = level;
    pic_set_irq_new(s->pic, s->irq, s->level[0] || s->level[1]);
}

static uint32_t sp804_read(void *opaque, target_phys_addr_t offset)
{
    sp804_state *s = (sp804_state *)opaque;

    /* ??? Don't know the PrimeCell ID for this device.  */
    offset -= s->base;
    if (offset < 0x20) {
        return arm_timer_read(s->timer[0], offset);
    } else {
        return arm_timer_read(s->timer[1], offset - 0x20);
    }
}

static void sp804_write(void *opaque, target_phys_addr_t offset,
                        uint32_t value)
{
    sp804_state *s = (sp804_state *)opaque;

    offset -= s->base;
    if (offset < 0x20) {
        arm_timer_write(s->timer[0], offset, value);
    } else {
        arm_timer_write(s->timer[1], offset - 0x20, value);
    }
}

static CPUReadMemoryFunc *sp804_readfn[] = {
   sp804_read,
   sp804_read,
   sp804_read
};

static CPUWriteMemoryFunc *sp804_writefn[] = {
   sp804_write,
   sp804_write,
   sp804_write
};

void sp804_init(uint32_t base, void *pic, int irq)
{
    int iomemtype;
    sp804_state *s;

    s = (sp804_state *)qemu_mallocz(sizeof(sp804_state));
    s->handler = sp804_set_irq;
    s->base = base;
    s->pic = pic;
    s->irq = irq;
    /* ??? The timers are actually configurable between 32kHz and 1MHz, but
       we don't implement that.  */
    s->timer[0] = arm_timer_init(1000000, s, 0);
    s->timer[1] = arm_timer_init(1000000, s, 1);
    iomemtype = cpu_register_io_memory(0, sp804_readfn,
                                       sp804_writefn, s);
    cpu_register_physical_memory(base, 0x00000fff, iomemtype);
    /* ??? Save/restore.  */
}


/* Integrator/CP timer module.  */

typedef struct {
    void *timer[3];
    uint32_t base;
} icp_pit_state;

static uint32_t icp_pit_read(void *opaque, target_phys_addr_t offset)
{
    icp_pit_state *s = (icp_pit_state *)opaque;
    int n;

    /* ??? Don't know the PrimeCell ID for this device.  */
    offset -= s->base;
    n = offset >> 8;
    if (n > 3)
        cpu_abort(cpu_single_env, "sp804_read: Bad timer %d\n", n);

    return arm_timer_read(s->timer[n], offset & 0xff);
}

static void icp_pit_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    icp_pit_state *s = (icp_pit_state *)opaque;
    int n;

    offset -= s->base;
    n = offset >> 8;
    if (n > 3)
        cpu_abort(cpu_single_env, "sp804_write: Bad timer %d\n", n);

    arm_timer_write(s->timer[n], offset & 0xff, value);
}


static CPUReadMemoryFunc *icp_pit_readfn[] = {
   icp_pit_read,
   icp_pit_read,
   icp_pit_read
};

static CPUWriteMemoryFunc *icp_pit_writefn[] = {
   icp_pit_write,
   icp_pit_write,
   icp_pit_write
};

void icp_pit_init(uint32_t base, void *pic, int irq)
{
    int iomemtype;
    icp_pit_state *s;

    s = (icp_pit_state *)qemu_mallocz(sizeof(icp_pit_state));
    s->base = base;
    /* Timer 0 runs at the system clock speed (40MHz).  */
    s->timer[0] = arm_timer_init(40000000, pic, irq);
    /* The other two timers run at 1MHz.  */
    s->timer[1] = arm_timer_init(1000000, pic, irq + 1);
    s->timer[2] = arm_timer_init(1000000, pic, irq + 2);

    iomemtype = cpu_register_io_memory(0, icp_pit_readfn,
                                       icp_pit_writefn, s);
    cpu_register_physical_memory(base, 0x00000fff, iomemtype);
    /* ??? Save/restore.  */
}

