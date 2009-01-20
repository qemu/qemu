/*
 *  High Precisition Event Timer emulation
 *
 *  Copyright (c) 2007 Alexander Graf
 *  Copyright (c) 2008 IBM Corporation
 *
 *  Authors: Beth Kon <bkon@us.ibm.com>
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 *
 * *****************************************************************
 *
 * This driver attempts to emulate an HPET device in software.
 */

#include "hw.h"
#include "pc.h"
#include "console.h"
#include "qemu-timer.h"
#include "hpet_emul.h"

//#define HPET_DEBUG
#ifdef HPET_DEBUG
#define dprintf printf
#else
#define dprintf(...)
#endif

static HPETState *hpet_statep;

uint32_t hpet_in_legacy_mode(void)
{
    if (hpet_statep)
        return hpet_statep->config & HPET_CFG_LEGACY;
    else
        return 0;
}

static uint32_t timer_int_route(struct HPETTimer *timer)
{
    uint32_t route;
    route = (timer->config & HPET_TN_INT_ROUTE_MASK) >> HPET_TN_INT_ROUTE_SHIFT;
    return route;
}

static uint32_t hpet_enabled(void)
{
    return hpet_statep->config & HPET_CFG_ENABLE;
}

static uint32_t timer_is_periodic(HPETTimer *t)
{
    return t->config & HPET_TN_PERIODIC;
}

static uint32_t timer_enabled(HPETTimer *t)
{
    return t->config & HPET_TN_ENABLE;
}

static uint32_t hpet_time_after(uint64_t a, uint64_t b)
{
    return ((int32_t)(b) - (int32_t)(a) < 0);
}

static uint32_t hpet_time_after64(uint64_t a, uint64_t b)
{
    return ((int64_t)(b) - (int64_t)(a) < 0);
}

static uint64_t ticks_to_ns(uint64_t value)
{
    return (muldiv64(value, HPET_CLK_PERIOD, FS_PER_NS));
}

static uint64_t ns_to_ticks(uint64_t value)
{
    return (muldiv64(value, FS_PER_NS, HPET_CLK_PERIOD));
}

static uint64_t hpet_fixup_reg(uint64_t new, uint64_t old, uint64_t mask)
{
    new &= mask;
    new |= old & ~mask;
    return new;
}

static int activating_bit(uint64_t old, uint64_t new, uint64_t mask)
{
    return (!(old & mask) && (new & mask));
}

static int deactivating_bit(uint64_t old, uint64_t new, uint64_t mask)
{
    return ((old & mask) && !(new & mask));
}

static uint64_t hpet_get_ticks(void)
{
    uint64_t ticks;
    ticks = ns_to_ticks(qemu_get_clock(vm_clock) + hpet_statep->hpet_offset);
    return ticks;
}

/*
 * calculate diff between comparator value and current ticks
 */
static inline uint64_t hpet_calculate_diff(HPETTimer *t, uint64_t current)
{

    if (t->config & HPET_TN_32BIT) {
        uint32_t diff, cmp;
        cmp = (uint32_t)t->cmp;
        diff = cmp - (uint32_t)current;
        diff = (int32_t)diff > 0 ? diff : (uint32_t)0;
        return (uint64_t)diff;
    } else {
        uint64_t diff, cmp;
        cmp = t->cmp;
        diff = cmp - current;
        diff = (int64_t)diff > 0 ? diff : (uint64_t)0;
        return diff;
    }
}

static void update_irq(struct HPETTimer *timer)
{
    qemu_irq irq;
    int route;

    if (timer->tn <= 1 && hpet_in_legacy_mode()) {
        /* if LegacyReplacementRoute bit is set, HPET specification requires
         * timer0 be routed to IRQ0 in NON-APIC or IRQ2 in the I/O APIC,
         * timer1 be routed to IRQ8 in NON-APIC or IRQ8 in the I/O APIC.
         */
        if (timer->tn == 0) {
            irq=timer->state->irqs[0];
        } else
            irq=timer->state->irqs[8];
    } else {
        route=timer_int_route(timer);
        irq=timer->state->irqs[route];
    }
    if (timer_enabled(timer) && hpet_enabled()) {
        qemu_irq_pulse(irq);
    }
}

static void hpet_save(QEMUFile *f, void *opaque)
{
    HPETState *s = opaque;
    int i;
    qemu_put_be64s(f, &s->config);
    qemu_put_be64s(f, &s->isr);
    /* save current counter value */
    s->hpet_counter = hpet_get_ticks();
    qemu_put_be64s(f, &s->hpet_counter);

    for (i = 0; i < HPET_NUM_TIMERS; i++) {
        qemu_put_8s(f, &s->timer[i].tn);
        qemu_put_be64s(f, &s->timer[i].config);
        qemu_put_be64s(f, &s->timer[i].cmp);
        qemu_put_be64s(f, &s->timer[i].fsb);
        qemu_put_be64s(f, &s->timer[i].period);
        qemu_put_8s(f, &s->timer[i].wrap_flag);
        if (s->timer[i].qemu_timer) {
            qemu_put_timer(f, s->timer[i].qemu_timer);
        }
    }
}

static int hpet_load(QEMUFile *f, void *opaque, int version_id)
{
    HPETState *s = opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    qemu_get_be64s(f, &s->config);
    qemu_get_be64s(f, &s->isr);
    qemu_get_be64s(f, &s->hpet_counter);
    /* Recalculate the offset between the main counter and guest time */
    s->hpet_offset = ticks_to_ns(s->hpet_counter) - qemu_get_clock(vm_clock);

    for (i = 0; i < HPET_NUM_TIMERS; i++) {
        qemu_get_8s(f, &s->timer[i].tn);
        qemu_get_be64s(f, &s->timer[i].config);
        qemu_get_be64s(f, &s->timer[i].cmp);
        qemu_get_be64s(f, &s->timer[i].fsb);
        qemu_get_be64s(f, &s->timer[i].period);
        qemu_get_8s(f, &s->timer[i].wrap_flag);
        if (s->timer[i].qemu_timer) {
            qemu_get_timer(f, s->timer[i].qemu_timer);
        }
    }
    return 0;
}

/*
 * timer expiration callback
 */
static void hpet_timer(void *opaque)
{
    HPETTimer *t = (HPETTimer*)opaque;
    uint64_t diff;

    uint64_t period = t->period;
    uint64_t cur_tick = hpet_get_ticks();

    if (timer_is_periodic(t) && period != 0) {
        if (t->config & HPET_TN_32BIT) {
            while (hpet_time_after(cur_tick, t->cmp))
                t->cmp = (uint32_t)(t->cmp + t->period);
        } else
            while (hpet_time_after64(cur_tick, t->cmp))
                t->cmp += period;

        diff = hpet_calculate_diff(t, cur_tick);
        qemu_mod_timer(t->qemu_timer, qemu_get_clock(vm_clock)
                       + (int64_t)ticks_to_ns(diff));
    } else if (t->config & HPET_TN_32BIT && !timer_is_periodic(t)) {
        if (t->wrap_flag) {
            diff = hpet_calculate_diff(t, cur_tick);
            qemu_mod_timer(t->qemu_timer, qemu_get_clock(vm_clock)
                           + (int64_t)ticks_to_ns(diff));
            t->wrap_flag = 0;
        }
    }
    update_irq(t);
}

static void hpet_set_timer(HPETTimer *t)
{
    uint64_t diff;
    uint32_t wrap_diff;  /* how many ticks until we wrap? */
    uint64_t cur_tick = hpet_get_ticks();

    /* whenever new timer is being set up, make sure wrap_flag is 0 */
    t->wrap_flag = 0;
    diff = hpet_calculate_diff(t, cur_tick);

    /* hpet spec says in one-shot 32-bit mode, generate an interrupt when
     * counter wraps in addition to an interrupt with comparator match.
     */
    if (t->config & HPET_TN_32BIT && !timer_is_periodic(t)) {
        wrap_diff = 0xffffffff - (uint32_t)cur_tick;
        if (wrap_diff < (uint32_t)diff) {
            diff = wrap_diff;
            t->wrap_flag = 1;
        }
    }
    qemu_mod_timer(t->qemu_timer, qemu_get_clock(vm_clock)
                   + (int64_t)ticks_to_ns(diff));
}

static void hpet_del_timer(HPETTimer *t)
{
    qemu_del_timer(t->qemu_timer);
}

#ifdef HPET_DEBUG
static uint32_t hpet_ram_readb(void *opaque, target_phys_addr_t addr)
{
    printf("qemu: hpet_read b at %" PRIx64 "\n", addr);
    return 0;
}

static uint32_t hpet_ram_readw(void *opaque, target_phys_addr_t addr)
{
    printf("qemu: hpet_read w at %" PRIx64 "\n", addr);
    return 0;
}
#endif

static uint32_t hpet_ram_readl(void *opaque, target_phys_addr_t addr)
{
    HPETState *s = (HPETState *)opaque;
    uint64_t cur_tick, index;

    dprintf("qemu: Enter hpet_ram_readl at %" PRIx64 "\n", addr);
    index = addr;
    /*address range of all TN regs*/
    if (index >= 0x100 && index <= 0x3ff) {
        uint8_t timer_id = (addr - 0x100) / 0x20;
        if (timer_id > HPET_NUM_TIMERS - 1) {
            printf("qemu: timer id out of range\n");
            return 0;
        }
        HPETTimer *timer = &s->timer[timer_id];

        switch ((addr - 0x100) % 0x20) {
            case HPET_TN_CFG:
                return timer->config;
            case HPET_TN_CFG + 4: // Interrupt capabilities
                return timer->config >> 32;
            case HPET_TN_CMP: // comparator register
                return timer->cmp;
            case HPET_TN_CMP + 4:
                return timer->cmp >> 32;
            case HPET_TN_ROUTE:
                return timer->fsb >> 32;
            default:
                dprintf("qemu: invalid hpet_ram_readl\n");
                break;
        }
    } else {
        switch (index) {
            case HPET_ID:
                return s->capability;
            case HPET_PERIOD:
                return s->capability >> 32;
            case HPET_CFG:
                return s->config;
            case HPET_CFG + 4:
                dprintf("qemu: invalid HPET_CFG + 4 hpet_ram_readl \n");
                return 0;
            case HPET_COUNTER:
                if (hpet_enabled())
                    cur_tick = hpet_get_ticks();
                else
                    cur_tick = s->hpet_counter;
                dprintf("qemu: reading counter  = %" PRIx64 "\n", cur_tick);
                return cur_tick;
            case HPET_COUNTER + 4:
                if (hpet_enabled())
                    cur_tick = hpet_get_ticks();
                else
                    cur_tick = s->hpet_counter;
                dprintf("qemu: reading counter + 4  = %" PRIx64 "\n", cur_tick);
                return cur_tick >> 32;
            case HPET_STATUS:
                return s->isr;
            default:
                dprintf("qemu: invalid hpet_ram_readl\n");
                break;
        }
    }
    return 0;
}

#ifdef HPET_DEBUG
static void hpet_ram_writeb(void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    printf("qemu: invalid hpet_write b at %" PRIx64 " = %#x\n",
           addr, value);
}

static void hpet_ram_writew(void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    printf("qemu: invalid hpet_write w at %" PRIx64 " = %#x\n",
           addr, value);
}
#endif

static void hpet_ram_writel(void *opaque, target_phys_addr_t addr,
                            uint32_t value)
{
    int i;
    HPETState *s = (HPETState *)opaque;
    uint64_t old_val, new_val, index;

    dprintf("qemu: Enter hpet_ram_writel at %" PRIx64 " = %#x\n", addr, value);
    index = addr;
    old_val = hpet_ram_readl(opaque, addr);
    new_val = value;

    /*address range of all TN regs*/
    if (index >= 0x100 && index <= 0x3ff) {
        uint8_t timer_id = (addr - 0x100) / 0x20;
        dprintf("qemu: hpet_ram_writel timer_id = %#x \n", timer_id);
        HPETTimer *timer = &s->timer[timer_id];

        switch ((addr - 0x100) % 0x20) {
            case HPET_TN_CFG:
                dprintf("qemu: hpet_ram_writel HPET_TN_CFG\n");
                timer->config = hpet_fixup_reg(new_val, old_val, 
                                               HPET_TN_CFG_WRITE_MASK);
                if (new_val & HPET_TN_32BIT) {
                    timer->cmp = (uint32_t)timer->cmp;
                    timer->period = (uint32_t)timer->period;
                }
                if (new_val & HPET_TIMER_TYPE_LEVEL) {
                    printf("qemu: level-triggered hpet not supported\n");
                    exit (-1);
                }

                break;
            case HPET_TN_CFG + 4: // Interrupt capabilities
                dprintf("qemu: invalid HPET_TN_CFG+4 write\n");
                break;
            case HPET_TN_CMP: // comparator register
                dprintf("qemu: hpet_ram_writel HPET_TN_CMP \n");
                if (timer->config & HPET_TN_32BIT)
                    new_val = (uint32_t)new_val;
                if (!timer_is_periodic(timer) ||
                           (timer->config & HPET_TN_SETVAL))
                    timer->cmp = (timer->cmp & 0xffffffff00000000ULL)
                                  | new_val;
                else {
                    /*
                     * FIXME: Clamp period to reasonable min value?
                     * Clamp period to reasonable max value
                     */
                    new_val &= (timer->config & HPET_TN_32BIT ? ~0u : ~0ull) >> 1;
                    timer->period = (timer->period & 0xffffffff00000000ULL)
                                     | new_val;
                }
                timer->config &= ~HPET_TN_SETVAL;
                if (hpet_enabled())
                    hpet_set_timer(timer);
                break;
            case HPET_TN_CMP + 4: // comparator register high order
                dprintf("qemu: hpet_ram_writel HPET_TN_CMP + 4\n");
                if (!timer_is_periodic(timer) ||
                           (timer->config & HPET_TN_SETVAL))
                    timer->cmp = (timer->cmp & 0xffffffffULL)
                                  | new_val << 32;
                else {
                    /*
                     * FIXME: Clamp period to reasonable min value?
                     * Clamp period to reasonable max value
                     */
                    new_val &= (timer->config
                                & HPET_TN_32BIT ? ~0u : ~0ull) >> 1;
                    timer->period = (timer->period & 0xffffffffULL)
                                     | new_val << 32;
                }
                timer->config &= ~HPET_TN_SETVAL;
                if (hpet_enabled())
                    hpet_set_timer(timer);
                break;
            case HPET_TN_ROUTE + 4:
                dprintf("qemu: hpet_ram_writel HPET_TN_ROUTE + 4\n");
                break;
            default:
                dprintf("qemu: invalid hpet_ram_writel\n");
                break;
        }
        return;
    } else {
        switch (index) {
            case HPET_ID:
                return;
            case HPET_CFG:
                s->config = hpet_fixup_reg(new_val, old_val, 
                                           HPET_CFG_WRITE_MASK);
                if (activating_bit(old_val, new_val, HPET_CFG_ENABLE)) {
                    /* Enable main counter and interrupt generation. */
                    s->hpet_offset = ticks_to_ns(s->hpet_counter)
                                     - qemu_get_clock(vm_clock);
                    for (i = 0; i < HPET_NUM_TIMERS; i++)
                        if ((&s->timer[i])->cmp != ~0ULL)
                            hpet_set_timer(&s->timer[i]);
                }
                else if (deactivating_bit(old_val, new_val, HPET_CFG_ENABLE)) {
                    /* Halt main counter and disable interrupt generation. */
                    s->hpet_counter = hpet_get_ticks();
                    for (i = 0; i < HPET_NUM_TIMERS; i++)
                        hpet_del_timer(&s->timer[i]);
                }
                /* i8254 and RTC are disabled when HPET is in legacy mode */
                if (activating_bit(old_val, new_val, HPET_CFG_LEGACY)) {
                    hpet_pit_disable();
                } else if (deactivating_bit(old_val, new_val, HPET_CFG_LEGACY)) {
                    hpet_pit_enable();
                }
                break;
            case HPET_CFG + 4:
                dprintf("qemu: invalid HPET_CFG+4 write \n");
                break;
            case HPET_STATUS:
                /* FIXME: need to handle level-triggered interrupts */
                break;
            case HPET_COUNTER:
               if (hpet_enabled())
                   printf("qemu: Writing counter while HPET enabled!\n");
               s->hpet_counter = (s->hpet_counter & 0xffffffff00000000ULL)
                                  | value;
               dprintf("qemu: HPET counter written. ctr = %#x -> %" PRIx64 "\n",
                        value, s->hpet_counter);
               break;
            case HPET_COUNTER + 4:
               if (hpet_enabled())
                   printf("qemu: Writing counter while HPET enabled!\n");
               s->hpet_counter = (s->hpet_counter & 0xffffffffULL)
                                  | (((uint64_t)value) << 32);
               dprintf("qemu: HPET counter + 4 written. ctr = %#x -> %" PRIx64 "\n",
                        value, s->hpet_counter);
               break;
            default:
               dprintf("qemu: invalid hpet_ram_writel\n");
               break;
        }
    }
}

static CPUReadMemoryFunc *hpet_ram_read[] = {
#ifdef HPET_DEBUG
    hpet_ram_readb,
    hpet_ram_readw,
#else
    NULL,
    NULL,
#endif
    hpet_ram_readl,
};

static CPUWriteMemoryFunc *hpet_ram_write[] = {
#ifdef HPET_DEBUG
    hpet_ram_writeb,
    hpet_ram_writew,
#else
    NULL,
    NULL,
#endif
    hpet_ram_writel,
};

static void hpet_reset(void *opaque) {
    HPETState *s = opaque;
    int i;
    static int count = 0;

    for (i=0; i<HPET_NUM_TIMERS; i++) {
        HPETTimer *timer = &s->timer[i];
        hpet_del_timer(timer);
        timer->tn = i;
        timer->cmp = ~0ULL;
        timer->config =  HPET_TN_PERIODIC_CAP | HPET_TN_SIZE_CAP;
        /* advertise availability of irqs 5,10,11 */
        timer->config |=  0x00000c20ULL << 32;
        timer->state = s;
        timer->period = 0ULL;
        timer->wrap_flag = 0;
    }

    s->hpet_counter = 0ULL;
    s->hpet_offset = 0ULL;
    /* 64-bit main counter; 3 timers supported; LegacyReplacementRoute. */
    s->capability = 0x8086a201ULL;
    s->capability |= ((HPET_CLK_PERIOD) << 32);
    if (count > 0)
        /* we don't enable pit when hpet_reset is first called (by hpet_init)
         * because hpet is taking over for pit here. On subsequent invocations,
         * hpet_reset is called due to system reset. At this point control must
         * be returned to pit until SW reenables hpet.
         */
        hpet_pit_enable();
    count = 1;
}


void hpet_init(qemu_irq *irq) {
    int i, iomemtype;
    HPETState *s;

    dprintf ("hpet_init\n");

    s = qemu_mallocz(sizeof(HPETState));
    hpet_statep = s;
    s->irqs = irq;
    for (i=0; i<HPET_NUM_TIMERS; i++) {
        HPETTimer *timer = &s->timer[i];
        timer->qemu_timer = qemu_new_timer(vm_clock, hpet_timer, timer);
    }
    hpet_reset(s);
    register_savevm("hpet", -1, 1, hpet_save, hpet_load, s);
    qemu_register_reset(hpet_reset, s);
    /* HPET Area */
    iomemtype = cpu_register_io_memory(0, hpet_ram_read,
                                       hpet_ram_write, s);
    cpu_register_physical_memory(HPET_BASE, 0x400, iomemtype);
}
