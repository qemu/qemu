/*
 *  High Precision Event Timer emulation
 *
 *  Copyright (c) 2007 Alexander Graf
 *  Copyright (c) 2008 IBM Corporation
 *
 *  Authors: Beth Kon <bkon@us.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************
 *
 * This driver attempts to emulate an HPET device in software.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/qdev-properties.h"
#include "hw/timer/hpet.h"
#include "hw/sysbus.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/rtc/mc146818rtc_regs.h"
#include "migration/vmstate.h"
#include "hw/timer/i8254.h"
#include "system/address-spaces.h"
#include "qom/object.h"
#include "trace.h"

struct hpet_fw_config hpet_fw_cfg = {.count = UINT8_MAX};

#define HPET_MSI_SUPPORT        0

OBJECT_DECLARE_SIMPLE_TYPE(HPETState, HPET)

struct HPETState;
typedef struct HPETTimer {  /* timers */
    uint8_t tn;             /*timer number*/
    QEMUTimer *qemu_timer;
    struct HPETState *state;
    /* Memory-mapped, software visible timer registers */
    uint64_t config;        /* configuration/cap */
    uint64_t cmp;           /* comparator */
    uint64_t fsb;           /* FSB route */
    /* Hidden register state */
    uint64_t cmp64;         /* comparator (extended to counter width) */
    uint64_t period;        /* Last value written to comparator */
    uint8_t wrap_flag;      /* timer pop will indicate wrap for one-shot 32-bit
                             * mode. Next pop will be actual timer expiration.
                             */
    uint64_t last;          /* last value armed, to avoid timer storms */
} HPETTimer;

struct HPETState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint64_t hpet_offset;
    bool hpet_offset_saved;
    qemu_irq irqs[HPET_NUM_IRQ_ROUTES];
    uint32_t flags;
    uint8_t rtc_irq_level;
    qemu_irq pit_enabled;
    uint8_t num_timers;
    uint8_t num_timers_save;
    uint32_t intcap;
    HPETTimer timer[HPET_MAX_TIMERS];

    /* Memory-mapped, software visible registers */
    uint64_t capability;        /* capabilities */
    uint64_t config;            /* configuration */
    uint64_t isr;               /* interrupt status reg */
    uint64_t hpet_counter;      /* main counter */
    uint8_t  hpet_id;           /* instance id */
};

static uint32_t hpet_in_legacy_mode(HPETState *s)
{
    return s->config & HPET_CFG_LEGACY;
}

static uint32_t timer_int_route(struct HPETTimer *timer)
{
    return (timer->config & HPET_TN_INT_ROUTE_MASK) >> HPET_TN_INT_ROUTE_SHIFT;
}

static uint32_t timer_fsb_route(HPETTimer *t)
{
    return t->config & HPET_TN_FSB_ENABLE;
}

static uint32_t hpet_enabled(HPETState *s)
{
    return s->config & HPET_CFG_ENABLE;
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
    return ((int64_t)(b - a) < 0);
}

static uint64_t ticks_to_ns(uint64_t value)
{
    return value * HPET_CLK_PERIOD;
}

static uint64_t ns_to_ticks(uint64_t value)
{
    return value / HPET_CLK_PERIOD;
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

static uint64_t hpet_get_ticks(HPETState *s)
{
    return ns_to_ticks(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->hpet_offset);
}

static uint64_t hpet_get_ns(HPETState *s, uint64_t tick)
{
    return ticks_to_ns(tick) - s->hpet_offset;
}

/*
 * calculate next value of the general counter that matches the
 * target (either entirely, or the low 32-bit only depending on
 * the timer mode).
 */
static uint64_t hpet_calculate_cmp64(HPETTimer *t, uint64_t cur_tick, uint64_t target)
{
    if (t->config & HPET_TN_32BIT) {
        uint64_t result = deposit64(cur_tick, 0, 32, target);
        if (result < cur_tick) {
            result += 0x100000000ULL;
        }
        return result;
    } else {
        return target;
    }
}

static uint64_t hpet_next_wrap(uint64_t cur_tick)
{
    return (cur_tick | 0xffffffffU) + 1;
}

static void update_irq(struct HPETTimer *timer, int set)
{
    uint64_t mask;
    HPETState *s;
    int route;

    if (timer->tn <= 1 && hpet_in_legacy_mode(timer->state)) {
        /* if LegacyReplacementRoute bit is set, HPET specification requires
         * timer0 be routed to IRQ0 in NON-APIC or IRQ2 in the I/O APIC,
         * timer1 be routed to IRQ8 in NON-APIC or IRQ8 in the I/O APIC.
         */
        route = (timer->tn == 0) ? 0 : RTC_ISA_IRQ;
    } else {
        route = timer_int_route(timer);
    }
    s = timer->state;
    mask = 1 << timer->tn;

    if (set && (timer->config & HPET_TN_TYPE_LEVEL)) {
        /*
         * If HPET_TN_ENABLE bit is 0, "the timer will still operate and
         * generate appropriate status bits, but will not cause an interrupt"
         */
        s->isr |= mask;
    } else {
        s->isr &= ~mask;
    }

    if (set && timer_enabled(timer) && hpet_enabled(s)) {
        if (timer_fsb_route(timer)) {
            address_space_stl_le(&address_space_memory, timer->fsb >> 32,
                                 timer->fsb & 0xffffffff, MEMTXATTRS_UNSPECIFIED,
                                 NULL);
        } else if (timer->config & HPET_TN_TYPE_LEVEL) {
            qemu_irq_raise(s->irqs[route]);
        } else {
            qemu_irq_pulse(s->irqs[route]);
        }
    } else {
        if (!timer_fsb_route(timer)) {
            qemu_irq_lower(s->irqs[route]);
        }
    }
}

static int hpet_pre_save(void *opaque)
{
    HPETState *s = opaque;

    /* save current counter value */
    if (hpet_enabled(s)) {
        s->hpet_counter = hpet_get_ticks(s);
    }

    /*
     * The number of timers must match on source and destination, but it was
     * also added to the migration stream.  Check that it matches the value
     * that was configured.
     */
    s->num_timers_save = s->num_timers;
    return 0;
}

static bool hpet_validate_num_timers(void *opaque, int version_id)
{
    HPETState *s = opaque;

    return s->num_timers == s->num_timers_save;
}

static int hpet_post_load(void *opaque, int version_id)
{
    HPETState *s = opaque;
    int i;

    for (i = 0; i < s->num_timers; i++) {
        HPETTimer *t = &s->timer[i];
        t->cmp64 = hpet_calculate_cmp64(t, s->hpet_counter, t->cmp);
        t->last = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - NANOSECONDS_PER_SECOND;
    }
    /* Recalculate the offset between the main counter and guest time */
    if (!s->hpet_offset_saved) {
        s->hpet_offset = ticks_to_ns(s->hpet_counter)
                        - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }

    return 0;
}

static bool hpet_offset_needed(void *opaque)
{
    HPETState *s = opaque;

    return hpet_enabled(s) && s->hpet_offset_saved;
}

static bool hpet_rtc_irq_level_needed(void *opaque)
{
    HPETState *s = opaque;

    return s->rtc_irq_level != 0;
}

static const VMStateDescription vmstate_hpet_rtc_irq_level = {
    .name = "hpet/rtc_irq_level",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hpet_rtc_irq_level_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(rtc_irq_level, HPETState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_hpet_offset = {
    .name = "hpet/offset",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hpet_offset_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(hpet_offset, HPETState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_hpet_timer = {
    .name = "hpet_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(tn, HPETTimer),
        VMSTATE_UINT64(config, HPETTimer),
        VMSTATE_UINT64(cmp, HPETTimer),
        VMSTATE_UINT64(fsb, HPETTimer),
        VMSTATE_UINT64(period, HPETTimer),
        VMSTATE_UINT8(wrap_flag, HPETTimer),
        VMSTATE_TIMER_PTR(qemu_timer, HPETTimer),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_hpet = {
    .name = "hpet",
    .version_id = 2,
    .minimum_version_id = 1,
    .pre_save = hpet_pre_save,
    .post_load = hpet_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(config, HPETState),
        VMSTATE_UINT64(isr, HPETState),
        VMSTATE_UINT64(hpet_counter, HPETState),
        VMSTATE_UINT8_V(num_timers_save, HPETState, 2),
        VMSTATE_VALIDATE("num_timers must match", hpet_validate_num_timers),
        VMSTATE_STRUCT_VARRAY_UINT8(timer, HPETState, num_timers, 0,
                                    vmstate_hpet_timer, HPETTimer),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_hpet_rtc_irq_level,
        &vmstate_hpet_offset,
        NULL
    }
};

static void hpet_arm(HPETTimer *t, uint64_t tick)
{
    uint64_t ns = hpet_get_ns(t->state, tick);

    /* Clamp period to reasonable min value (1 us) */
    if (timer_is_periodic(t) && ns - t->last < 1000) {
        ns = t->last + 1000;
    }

    t->last = ns;
    timer_mod(t->qemu_timer, ns);
}

/*
 * timer expiration callback
 */
static void hpet_timer(void *opaque)
{
    HPETTimer *t = opaque;
    uint64_t period = t->period;
    uint64_t cur_tick = hpet_get_ticks(t->state);

    if (timer_is_periodic(t) && period != 0) {
        while (hpet_time_after(cur_tick, t->cmp64)) {
            t->cmp64 += period;
        }
        if (t->config & HPET_TN_32BIT) {
            t->cmp = (uint32_t)t->cmp64;
        } else {
            t->cmp = t->cmp64;
        }
        hpet_arm(t, t->cmp64);
    } else if (t->wrap_flag) {
        t->wrap_flag = 0;
        hpet_arm(t, t->cmp64);
    }
    update_irq(t, 1);
}

static void hpet_set_timer(HPETTimer *t)
{
    uint64_t cur_tick = hpet_get_ticks(t->state);

    t->wrap_flag = 0;
    t->cmp64 = hpet_calculate_cmp64(t, cur_tick, t->cmp);
    if (t->config & HPET_TN_32BIT) {

        /* hpet spec says in one-shot 32-bit mode, generate an interrupt when
         * counter wraps in addition to an interrupt with comparator match.
         */
        if (!timer_is_periodic(t) && t->cmp64 > hpet_next_wrap(cur_tick)) {
            t->wrap_flag = 1;
            hpet_arm(t, hpet_next_wrap(cur_tick));
            return;
        }
    }
    hpet_arm(t, t->cmp64);
}

static void hpet_del_timer(HPETTimer *t)
{
    HPETState *s = t->state;
    timer_del(t->qemu_timer);

    if (s->isr & (1 << t->tn)) {
        /* For level-triggered interrupt, this leaves ISR set but lowers irq.  */
        update_irq(t, 1);
    }
}

static uint64_t hpet_ram_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    HPETState *s = opaque;
    int shift = (addr & 4) * 8;
    uint64_t cur_tick;

    trace_hpet_ram_read(addr);

    /*address range of all TN regs*/
    if (addr >= 0x100 && addr <= 0x3ff) {
        uint8_t timer_id = (addr - 0x100) / 0x20;
        HPETTimer *timer = &s->timer[timer_id];

        if (timer_id > s->num_timers) {
            trace_hpet_timer_id_out_of_range(timer_id);
            return 0;
        }

        switch (addr & 0x18) {
        case HPET_TN_CFG: // including interrupt capabilities
            return timer->config >> shift;
        case HPET_TN_CMP: // comparator register
            return timer->cmp >> shift;
        case HPET_TN_ROUTE:
            return timer->fsb >> shift;
        default:
            trace_hpet_ram_read_invalid();
            break;
        }
    } else {
        switch (addr & ~4) {
        case HPET_ID: // including HPET_PERIOD
            return s->capability >> shift;
        case HPET_CFG:
            return s->config >> shift;
        case HPET_COUNTER:
            if (hpet_enabled(s)) {
                cur_tick = hpet_get_ticks(s);
            } else {
                cur_tick = s->hpet_counter;
            }
            trace_hpet_ram_read_reading_counter(addr & 4, cur_tick);
            return cur_tick >> shift;
        case HPET_STATUS:
            return s->isr >> shift;
        default:
            trace_hpet_ram_read_invalid();
            break;
        }
    }
    return 0;
}

static void hpet_ram_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    int i;
    HPETState *s = opaque;
    int shift = (addr & 4) * 8;
    int len = MIN(size * 8, 64 - shift);
    uint64_t old_val, new_val, cleared;

    trace_hpet_ram_write(addr, value);

    /*address range of all TN regs*/
    if (addr >= 0x100 && addr <= 0x3ff) {
        uint8_t timer_id = (addr - 0x100) / 0x20;
        HPETTimer *timer = &s->timer[timer_id];

        trace_hpet_ram_write_timer_id(timer_id);
        if (timer_id > s->num_timers) {
            trace_hpet_timer_id_out_of_range(timer_id);
            return;
        }
        switch (addr & 0x18) {
        case HPET_TN_CFG:
            trace_hpet_ram_write_tn_cfg(addr & 4);
            old_val = timer->config;
            new_val = deposit64(old_val, shift, len, value);
            new_val = hpet_fixup_reg(new_val, old_val, HPET_TN_CFG_WRITE_MASK);
            if (deactivating_bit(old_val, new_val, HPET_TN_TYPE_LEVEL)) {
                /*
                 * Do this before changing timer->config; otherwise, if
                 * HPET_TN_FSB is set, update_irq will not lower the qemu_irq.
                 */
                update_irq(timer, 0);
            }
            timer->config = new_val;
            if (activating_bit(old_val, new_val, HPET_TN_ENABLE)
                && (s->isr & (1 << timer_id))) {
                update_irq(timer, 1);
            }
            if (new_val & HPET_TN_32BIT) {
                timer->cmp = (uint32_t)timer->cmp;
                timer->period = (uint32_t)timer->period;
            }
            if (hpet_enabled(s)) {
                hpet_set_timer(timer);
            }
            break;
        case HPET_TN_CMP: // comparator register
            if (timer->config & HPET_TN_32BIT) {
                /* High 32-bits are zero, leave them untouched.  */
                if (shift) {
                    trace_hpet_ram_write_invalid_tn_cmp();
                    break;
                }
                len = 64;
                value = (uint32_t) value;
            }
            trace_hpet_ram_write_tn_cmp(addr & 4);
            if (!timer_is_periodic(timer)
                || (timer->config & HPET_TN_SETVAL)) {
                timer->cmp = deposit64(timer->cmp, shift, len, value);
            }
            if (timer_is_periodic(timer)) {
                timer->period = deposit64(timer->period, shift, len, value);
            }
            timer->config &= ~HPET_TN_SETVAL;
            if (hpet_enabled(s)) {
                hpet_set_timer(timer);
            }
            break;
        case HPET_TN_ROUTE:
            timer->fsb = deposit64(timer->fsb, shift, len, value);
            break;
        default:
            trace_hpet_ram_write_invalid();
            break;
        }
        return;
    } else {
        switch (addr & ~4) {
        case HPET_ID:
            return;
        case HPET_CFG:
            old_val = s->config;
            new_val = deposit64(old_val, shift, len, value);
            new_val = hpet_fixup_reg(new_val, old_val, HPET_CFG_WRITE_MASK);
            s->config = new_val;
            if (activating_bit(old_val, new_val, HPET_CFG_ENABLE)) {
                /* Enable main counter and interrupt generation. */
                s->hpet_offset =
                    ticks_to_ns(s->hpet_counter) - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                for (i = 0; i < s->num_timers; i++) {
                    if (timer_enabled(&s->timer[i]) && (s->isr & (1 << i))) {
                        update_irq(&s->timer[i], 1);
                    }
                    hpet_set_timer(&s->timer[i]);
                }
            } else if (deactivating_bit(old_val, new_val, HPET_CFG_ENABLE)) {
                /* Halt main counter and disable interrupt generation. */
                s->hpet_counter = hpet_get_ticks(s);
                for (i = 0; i < s->num_timers; i++) {
                    hpet_del_timer(&s->timer[i]);
                }
            }
            /* i8254 and RTC output pins are disabled
             * when HPET is in legacy mode */
            if (activating_bit(old_val, new_val, HPET_CFG_LEGACY)) {
                qemu_set_irq(s->pit_enabled, 0);
                qemu_irq_lower(s->irqs[0]);
                qemu_irq_lower(s->irqs[RTC_ISA_IRQ]);
            } else if (deactivating_bit(old_val, new_val, HPET_CFG_LEGACY)) {
                qemu_irq_lower(s->irqs[0]);
                qemu_set_irq(s->pit_enabled, 1);
                qemu_set_irq(s->irqs[RTC_ISA_IRQ], s->rtc_irq_level);
            }
            break;
        case HPET_STATUS:
            new_val = value << shift;
            cleared = new_val & s->isr;
            for (i = 0; i < s->num_timers; i++) {
                if (cleared & (1 << i)) {
                    update_irq(&s->timer[i], 0);
                }
            }
            break;
        case HPET_COUNTER:
            if (hpet_enabled(s)) {
                trace_hpet_ram_write_counter_write_while_enabled();
            }
            s->hpet_counter = deposit64(s->hpet_counter, shift, len, value);
            break;
        default:
            trace_hpet_ram_write_invalid();
            break;
        }
    }
}

static const MemoryRegionOps hpet_ram_ops = {
    .read = hpet_ram_read,
    .write = hpet_ram_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void hpet_reset(DeviceState *d)
{
    HPETState *s = HPET(d);
    SysBusDevice *sbd = SYS_BUS_DEVICE(d);
    int i;

    for (i = 0; i < s->num_timers; i++) {
        HPETTimer *timer = &s->timer[i];

        hpet_del_timer(timer);
        timer->cmp = ~0ULL;
        timer->config = HPET_TN_PERIODIC_CAP | HPET_TN_SIZE_CAP;
        if (s->flags & (1 << HPET_MSI_SUPPORT)) {
            timer->config |= HPET_TN_FSB_CAP;
        }
        /* advertise availability of ioapic int */
        timer->config |=  (uint64_t)s->intcap << 32;
        timer->period = 0ULL;
        timer->wrap_flag = 0;
    }

    qemu_set_irq(s->pit_enabled, 1);
    s->hpet_counter = 0ULL;
    s->hpet_offset = 0ULL;
    s->config = 0ULL;
    hpet_fw_cfg.hpet[s->hpet_id].event_timer_block_id = (uint32_t)s->capability;
    hpet_fw_cfg.hpet[s->hpet_id].address = sbd->mmio[0].addr;

    /* to document that the RTC lowers its output on reset as well */
    s->rtc_irq_level = 0;
}

static void hpet_handle_legacy_irq(void *opaque, int n, int level)
{
    HPETState *s = HPET(opaque);

    if (n == HPET_LEGACY_PIT_INT) {
        if (!hpet_in_legacy_mode(s)) {
            qemu_set_irq(s->irqs[0], level);
        }
    } else {
        s->rtc_irq_level = level;
        if (!hpet_in_legacy_mode(s)) {
            qemu_set_irq(s->irqs[RTC_ISA_IRQ], level);
        }
    }
}

static void hpet_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    HPETState *s = HPET(obj);

    /* HPET Area */
    memory_region_init_io(&s->iomem, obj, &hpet_ram_ops, s, "hpet", HPET_LEN);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void hpet_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    HPETState *s = HPET(dev);
    int i;
    HPETTimer *timer;

    if (!s->intcap) {
        warn_report("Hpet's intcap not initialized");
    }
    if (hpet_fw_cfg.count == UINT8_MAX) {
        /* first instance */
        hpet_fw_cfg.count = 0;
    }

    if (hpet_fw_cfg.count == 8) {
        error_setg(errp, "Only 8 instances of HPET is allowed");
        return;
    }

    s->hpet_id = hpet_fw_cfg.count++;

    for (i = 0; i < HPET_NUM_IRQ_ROUTES; i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }

    if (s->num_timers < HPET_MIN_TIMERS) {
        s->num_timers = HPET_MIN_TIMERS;
    } else if (s->num_timers > HPET_MAX_TIMERS) {
        s->num_timers = HPET_MAX_TIMERS;
    }
    for (i = 0; i < HPET_MAX_TIMERS; i++) {
        timer = &s->timer[i];
        timer->qemu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, hpet_timer, timer);
        timer->tn = i;
        timer->state = s;
    }

    /* 64-bit General Capabilities and ID Register; LegacyReplacementRoute. */
    s->capability = 0x8086a001ULL;
    s->capability |= (s->num_timers - 1) << HPET_ID_NUM_TIM_SHIFT;
    s->capability |= ((uint64_t)(HPET_CLK_PERIOD * FS_PER_NS) << 32);

    qdev_init_gpio_in(dev, hpet_handle_legacy_irq, 2);
    qdev_init_gpio_out(dev, &s->pit_enabled, 1);
}

static const Property hpet_device_properties[] = {
    DEFINE_PROP_UINT8("timers", HPETState, num_timers, HPET_MIN_TIMERS),
    DEFINE_PROP_BIT("msi", HPETState, flags, HPET_MSI_SUPPORT, false),
    DEFINE_PROP_UINT32(HPET_INTCAP, HPETState, intcap, 0),
    DEFINE_PROP_BOOL("hpet-offset-saved", HPETState, hpet_offset_saved, true),
};

static void hpet_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = hpet_realize;
    device_class_set_legacy_reset(dc, hpet_reset);
    dc->vmsd = &vmstate_hpet;
    device_class_set_props(dc, hpet_device_properties);
}

static const TypeInfo hpet_device_info = {
    .name          = TYPE_HPET,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HPETState),
    .instance_init = hpet_init,
    .class_init    = hpet_device_class_init,
};

static void hpet_register_types(void)
{
    type_register_static(&hpet_device_info);
}

type_init(hpet_register_types)
