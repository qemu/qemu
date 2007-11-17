/*
 * ARM AMBA PrimeCell PL031 RTC
 *
 * Copyright (c) 2007 CodeSourcery
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "hw.h"
#include "primecell.h"
#include "qemu-timer.h"
#include "sysemu.h"

//#define DEBUG_PL031

#ifdef DEBUG_PL031
#define DPRINTF(fmt, args...) \
do { printf("pl031: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

#define RTC_DR      0x00    /* Data read register */
#define RTC_MR      0x04    /* Match register */
#define RTC_LR      0x08    /* Data load register */
#define RTC_CR      0x0c    /* Control register */
#define RTC_IMSC    0x10    /* Interrupt mask and set register */
#define RTC_RIS     0x14    /* Raw interrupt status register */
#define RTC_MIS     0x18    /* Masked interrupt status register */
#define RTC_ICR     0x1c    /* Interrupt clear register */

typedef struct {
    QEMUTimer *timer;
    qemu_irq irq;
    uint32_t base;

    uint64_t start_time;
    uint32_t tick_offset;

    uint32_t mr;
    uint32_t lr;
    uint32_t cr;
    uint32_t im;
    uint32_t is;
} pl031_state;

static const unsigned char pl031_id[] = {
    0x31, 0x10, 0x14, 0x00,         /* Device ID        */
    0x0d, 0xf0, 0x05, 0xb1          /* Cell ID      */
};

static void pl031_update(pl031_state *s)
{
    qemu_set_irq(s->irq, s->is & s->im);
}

static void pl031_interrupt(void * opaque)
{
    pl031_state *s = (pl031_state *)opaque;

    s->im = 1;
    DPRINTF("Alarm raised\n");
    pl031_update(s);
}

static uint32_t pl031_get_count(pl031_state *s)
{
    /* This assumes qemu_get_clock returns the time since the machine was
       created.  */
    return s->tick_offset + qemu_get_clock(vm_clock) / ticks_per_sec;
}

static void pl031_set_alarm(pl031_state *s)
{
    int64_t now;
    uint32_t ticks;

    now = qemu_get_clock(vm_clock);
    ticks = s->tick_offset + now / ticks_per_sec;

    /* The timer wraps around.  This subtraction also wraps in the same way,
       and gives correct results when alarm < now_ticks.  */
    ticks = s->mr - ticks;
    DPRINTF("Alarm set in %ud ticks\n", ticks);
    if (ticks == 0) {
        qemu_del_timer(s->timer);
        pl031_interrupt(s);
    } else {
        qemu_mod_timer(s->timer, now + (int64_t)ticks * ticks_per_sec);
    }
}

static uint32_t pl031_read(void *opaque, target_phys_addr_t offset)
{
    pl031_state *s = (pl031_state *)opaque;

    offset -= s->base;

    if (offset >= 0xfe0  &&  offset < 0x1000)
        return pl031_id[(offset - 0xfe0) >> 2];

    switch (offset) {
    case RTC_DR:
        return pl031_get_count(s);
    case RTC_MR:
        return s->mr;
    case RTC_IMSC:
        return s->im;
    case RTC_RIS:
        return s->is;
    case RTC_LR:
        return s->lr;
    case RTC_CR:
        /* RTC is permanently enabled.  */
        return 1;
    case RTC_MIS:
        return s->is & s->im;
    case RTC_ICR:
        fprintf(stderr, "qemu: pl031_read: Unexpected offset 0x%x\n",
                (int)offset);
        break;
    default:
        cpu_abort(cpu_single_env, "pl031_read: Bad offset 0x%x\n",
                  (int)offset);
        break;
    }

    return 0;
}

static void pl031_write(void * opaque, target_phys_addr_t offset,
                        uint32_t value)
{
    pl031_state *s = (pl031_state *)opaque;

    offset -= s->base;

    switch (offset) {
    case RTC_LR:
        s->tick_offset += value - pl031_get_count(s);
        pl031_set_alarm(s);
        break;
    case RTC_MR:
        s->mr = value;
        pl031_set_alarm(s);
        break;
    case RTC_IMSC:
        s->im = value & 1;
        DPRINTF("Interrupt mask %d\n", s->im);
        pl031_update(s);
        break;
    case RTC_ICR:
        /* The PL031 documentation (DDI0224B) states that the interupt is
           cleared when bit 0 of the written value is set.  However the
           arm926e documentation (DDI0287B) states that the interrupt is
           cleared when any value is written.  */
        DPRINTF("Interrupt cleared");
        s->is = 0;
        pl031_update(s);
        break;
    case RTC_CR:
        /* Written value is ignored.  */
        break;

    case RTC_DR:
    case RTC_MIS:
    case RTC_RIS:
        fprintf(stderr, "qemu: pl031_write: Unexpected offset 0x%x\n",
                (int)offset);
        break;

    default:
        cpu_abort(cpu_single_env, "pl031_write: Bad offset 0x%x\n",
                  (int)offset);
        break;
    }
}

static CPUWriteMemoryFunc * pl031_writefn[] = {
    pl031_write,
    pl031_write,
    pl031_write
};

static CPUReadMemoryFunc * pl031_readfn[] = {
    pl031_read,
    pl031_read,
    pl031_read
};

void pl031_init(uint32_t base, qemu_irq irq)
{
    int iomemtype;
    pl031_state *s;
    time_t ti;
    struct tm *tm;

    s = qemu_mallocz(sizeof(pl031_state));
    if (!s)
        cpu_abort(cpu_single_env, "pl031_init: Out of memory\n");

    iomemtype = cpu_register_io_memory(0, pl031_readfn, pl031_writefn, s);
    if (iomemtype == -1)
        cpu_abort(cpu_single_env, "pl031_init: Can't register I/O memory\n");

    cpu_register_physical_memory(base, 0x00001000, iomemtype);

    s->base = base;
    s->irq  = irq;
    /* ??? We assume vm_clock is zero at this point.  */
    time(&ti);
    if (rtc_utc)
        tm = gmtime(&ti);
    else
        tm = localtime(&ti);
    s->tick_offset = mktime(tm);

    s->timer = qemu_new_timer(vm_clock, pl031_interrupt, s);
}
