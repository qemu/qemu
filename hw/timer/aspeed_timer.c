/*
 * ASPEED AST2400 Timer
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/timer/aspeed_timer.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

#define TIMER_NR_REGS 4

#define TIMER_CTRL_BITS 4
#define TIMER_CTRL_MASK ((1 << TIMER_CTRL_BITS) - 1)

#define TIMER_CLOCK_USE_EXT true
#define TIMER_CLOCK_EXT_HZ 1000000
#define TIMER_CLOCK_USE_APB false

#define TIMER_REG_STATUS 0
#define TIMER_REG_RELOAD 1
#define TIMER_REG_MATCH_FIRST 2
#define TIMER_REG_MATCH_SECOND 3

#define TIMER_FIRST_CAP_PULSE 4

enum timer_ctrl_op {
    op_enable = 0,
    op_external_clock,
    op_overflow_interrupt,
    op_pulse_enable
};

/**
 * Avoid mutual references between AspeedTimerCtrlState and AspeedTimer
 * structs, as it's a waste of memory. The ptimer BH callback needs to know
 * whether a specific AspeedTimer is enabled, but this information is held in
 * AspeedTimerCtrlState. So, provide a helper to hoist ourselves from an
 * arbitrary AspeedTimer to AspeedTimerCtrlState.
 */
static inline AspeedTimerCtrlState *timer_to_ctrl(AspeedTimer *t)
{
    const AspeedTimer (*timers)[] = (void *)t - (t->id * sizeof(*t));
    return container_of(timers, AspeedTimerCtrlState, timers);
}

static inline bool timer_ctrl_status(AspeedTimer *t, enum timer_ctrl_op op)
{
    return !!(timer_to_ctrl(t)->ctrl & BIT(t->id * TIMER_CTRL_BITS + op));
}

static inline bool timer_enabled(AspeedTimer *t)
{
    return timer_ctrl_status(t, op_enable);
}

static inline bool timer_overflow_interrupt(AspeedTimer *t)
{
    return timer_ctrl_status(t, op_overflow_interrupt);
}

static inline bool timer_can_pulse(AspeedTimer *t)
{
    return t->id >= TIMER_FIRST_CAP_PULSE;
}

static inline bool timer_external_clock(AspeedTimer *t)
{
    return timer_ctrl_status(t, op_external_clock);
}

static inline uint32_t calculate_rate(struct AspeedTimer *t)
{
    AspeedTimerCtrlState *s = timer_to_ctrl(t);

    return timer_external_clock(t) ? TIMER_CLOCK_EXT_HZ : s->scu->apb_freq;
}

static inline uint32_t calculate_ticks(struct AspeedTimer *t, uint64_t now_ns)
{
    uint64_t delta_ns = now_ns - MIN(now_ns, t->start);
    uint32_t rate = calculate_rate(t);
    uint64_t ticks = muldiv64(delta_ns, rate, NANOSECONDS_PER_SECOND);

    return t->reload - MIN(t->reload, ticks);
}

static inline uint64_t calculate_time(struct AspeedTimer *t, uint32_t ticks)
{
    uint64_t delta_ns;
    uint64_t delta_ticks;

    delta_ticks = t->reload - MIN(t->reload, ticks);
    delta_ns = muldiv64(delta_ticks, NANOSECONDS_PER_SECOND, calculate_rate(t));

    return t->start + delta_ns;
}

static inline uint32_t calculate_match(struct AspeedTimer *t, int i)
{
    return t->match[i] < t->reload ? t->match[i] : 0;
}

static uint64_t calculate_next(struct AspeedTimer *t)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t next;

    /*
     * We don't know the relationship between the values in the match
     * registers, so sort using MAX/MIN/zero. We sort in that order as
     * the timer counts down to zero.
     */

    next = calculate_time(t, MAX(calculate_match(t, 0), calculate_match(t, 1)));
    if (now < next) {
        return next;
    }

    next = calculate_time(t, MIN(calculate_match(t, 0), calculate_match(t, 1)));
    if (now < next) {
        return next;
    }

    next = calculate_time(t, 0);
    if (now < next) {
        return next;
    }

    /* We've missed all deadlines, fire interrupt and try again */
    timer_del(&t->timer);

    if (timer_overflow_interrupt(t)) {
        t->level = !t->level;
        qemu_set_irq(t->irq, t->level);
    }

    next = MAX(MAX(calculate_match(t, 0), calculate_match(t, 1)), 0);
    t->start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    return calculate_time(t, next);
}

static void aspeed_timer_mod(AspeedTimer *t)
{
    uint64_t next = calculate_next(t);
    if (next) {
        timer_mod(&t->timer, next);
    }
}

static void aspeed_timer_expire(void *opaque)
{
    AspeedTimer *t = opaque;
    bool interrupt = false;
    uint32_t ticks;

    if (!timer_enabled(t)) {
        return;
    }

    ticks = calculate_ticks(t, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));

    if (!ticks) {
        interrupt = timer_overflow_interrupt(t) || !t->match[0] || !t->match[1];
    } else if (ticks <= MIN(t->match[0], t->match[1])) {
        interrupt = true;
    } else if (ticks <= MAX(t->match[0], t->match[1])) {
        interrupt = true;
    }

    if (interrupt) {
        t->level = !t->level;
        qemu_set_irq(t->irq, t->level);
    }

    aspeed_timer_mod(t);
}

static uint64_t aspeed_timer_get_value(AspeedTimer *t, int reg)
{
    uint64_t value;

    switch (reg) {
    case TIMER_REG_STATUS:
        if (timer_enabled(t)) {
            value = calculate_ticks(t, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        } else {
            value = t->reload;
        }
        break;
    case TIMER_REG_RELOAD:
        value = t->reload;
        break;
    case TIMER_REG_MATCH_FIRST:
    case TIMER_REG_MATCH_SECOND:
        value = t->match[reg - 2];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Programming error: unexpected reg: %d\n",
                      __func__, reg);
        value = 0;
        break;
    }
    return value;
}

static uint64_t aspeed_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedTimerCtrlState *s = opaque;
    const int reg = (offset & 0xf) / 4;
    uint64_t value;

    switch (offset) {
    case 0x30: /* Control Register */
        value = s->ctrl;
        break;
    case 0x34: /* Control Register 2 */
        value = s->ctrl2;
        break;
    case 0x00 ... 0x2c: /* Timers 1 - 4 */
        value = aspeed_timer_get_value(&s->timers[(offset >> 4)], reg);
        break;
    case 0x40 ... 0x8c: /* Timers 5 - 8 */
        value = aspeed_timer_get_value(&s->timers[(offset >> 4) - 1], reg);
        break;
    /* Illegal */
    case 0x38:
    case 0x3C:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                __func__, offset);
        value = 0;
        break;
    }
    trace_aspeed_timer_read(offset, size, value);
    return value;
}

static void aspeed_timer_set_value(AspeedTimerCtrlState *s, int timer, int reg,
                                   uint32_t value)
{
    AspeedTimer *t;
    uint32_t old_reload;

    trace_aspeed_timer_set_value(timer, reg, value);
    t = &s->timers[timer];
    switch (reg) {
    case TIMER_REG_RELOAD:
        old_reload = t->reload;
        t->reload = value;

        /* If the reload value was not previously set, or zero, and
         * the current value is valid, try to start the timer if it is
         * enabled.
         */
        if (old_reload || !t->reload) {
            break;
        }

    case TIMER_REG_STATUS:
        if (timer_enabled(t)) {
            uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            int64_t delta = (int64_t) value - (int64_t) calculate_ticks(t, now);
            uint32_t rate = calculate_rate(t);

            if (delta >= 0) {
                t->start += muldiv64(delta, NANOSECONDS_PER_SECOND, rate);
            } else {
                t->start -= muldiv64(-delta, NANOSECONDS_PER_SECOND, rate);
            }
            aspeed_timer_mod(t);
        }
        break;
    case TIMER_REG_MATCH_FIRST:
    case TIMER_REG_MATCH_SECOND:
        t->match[reg - 2] = value;
        if (timer_enabled(t)) {
            aspeed_timer_mod(t);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Programming error: unexpected reg: %d\n",
                      __func__, reg);
        break;
    }
}

/* Control register operations are broken out into helpers that can be
 * explicitly called on aspeed_timer_reset(), but also from
 * aspeed_timer_ctrl_op().
 */

static void aspeed_timer_ctrl_enable(AspeedTimer *t, bool enable)
{
    trace_aspeed_timer_ctrl_enable(t->id, enable);
    if (enable) {
        t->start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        aspeed_timer_mod(t);
    } else {
        timer_del(&t->timer);
    }
}

static void aspeed_timer_ctrl_external_clock(AspeedTimer *t, bool enable)
{
    trace_aspeed_timer_ctrl_external_clock(t->id, enable);
}

static void aspeed_timer_ctrl_overflow_interrupt(AspeedTimer *t, bool enable)
{
    trace_aspeed_timer_ctrl_overflow_interrupt(t->id, enable);
}

static void aspeed_timer_ctrl_pulse_enable(AspeedTimer *t, bool enable)
{
    if (timer_can_pulse(t)) {
        trace_aspeed_timer_ctrl_pulse_enable(t->id, enable);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: Timer does not support pulse mode\n", __func__);
    }
}

/**
 * Given the actions are fixed in number and completely described in helper
 * functions, dispatch with a lookup table rather than manage control flow with
 * a switch statement.
 */
static void (*const ctrl_ops[])(AspeedTimer *, bool) = {
    [op_enable] = aspeed_timer_ctrl_enable,
    [op_external_clock] = aspeed_timer_ctrl_external_clock,
    [op_overflow_interrupt] = aspeed_timer_ctrl_overflow_interrupt,
    [op_pulse_enable] = aspeed_timer_ctrl_pulse_enable,
};

/**
 * Conditionally affect changes chosen by a timer's control bit.
 *
 * The aspeed_timer_ctrl_op() interface is convenient for the
 * aspeed_timer_set_ctrl() function as the "no change" early exit can be
 * calculated for all operations, which cleans up the caller code. However the
 * interface isn't convenient for the reset function where we want to enter a
 * specific state without artificially constructing old and new values that
 * will fall through the change guard (and motivates extracting the actions
 * out to helper functions).
 *
 * @t: The timer to manipulate
 * @op: The type of operation to be performed
 * @old: The old state of the timer's control bits
 * @new: The incoming state for the timer's control bits
 */
static void aspeed_timer_ctrl_op(AspeedTimer *t, enum timer_ctrl_op op,
                                 uint8_t old, uint8_t new)
{
    const uint8_t mask = BIT(op);
    const bool enable = !!(new & mask);
    const bool changed = ((old ^ new) & mask);
    if (!changed) {
        return;
    }
    ctrl_ops[op](t, enable);
}

static void aspeed_timer_set_ctrl(AspeedTimerCtrlState *s, uint32_t reg)
{
    int i;
    int shift;
    uint8_t t_old, t_new;
    AspeedTimer *t;
    const uint8_t enable_mask = BIT(op_enable);

    /* Handle a dependency between the 'enable' and remaining three
     * configuration bits - i.e. if more than one bit in the control set has
     * changed, including the 'enable' bit, then we want either disable the
     * timer and perform configuration, or perform configuration and then
     * enable the timer
     */
    for (i = 0; i < ASPEED_TIMER_NR_TIMERS; i++) {
        t = &s->timers[i];
        shift = (i * TIMER_CTRL_BITS);
        t_old = (s->ctrl >> shift) & TIMER_CTRL_MASK;
        t_new = (reg >> shift) & TIMER_CTRL_MASK;

        /* If we are disabling, do so first */
        if ((t_old & enable_mask) && !(t_new & enable_mask)) {
            aspeed_timer_ctrl_enable(t, false);
        }
        aspeed_timer_ctrl_op(t, op_external_clock, t_old, t_new);
        aspeed_timer_ctrl_op(t, op_overflow_interrupt, t_old, t_new);
        aspeed_timer_ctrl_op(t, op_pulse_enable, t_old, t_new);
        /* If we are enabling, do so last */
        if (!(t_old & enable_mask) && (t_new & enable_mask)) {
            aspeed_timer_ctrl_enable(t, true);
        }
    }
    s->ctrl = reg;
}

static void aspeed_timer_set_ctrl2(AspeedTimerCtrlState *s, uint32_t value)
{
    trace_aspeed_timer_set_ctrl2(value);
}

static void aspeed_timer_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    const uint32_t tv = (uint32_t)(value & 0xFFFFFFFF);
    const int reg = (offset & 0xf) / 4;
    AspeedTimerCtrlState *s = opaque;

    switch (offset) {
    /* Control Registers */
    case 0x30:
        aspeed_timer_set_ctrl(s, tv);
        break;
    case 0x34:
        aspeed_timer_set_ctrl2(s, tv);
        break;
    /* Timer Registers */
    case 0x00 ... 0x2c:
        aspeed_timer_set_value(s, (offset >> TIMER_NR_REGS), reg, tv);
        break;
    case 0x40 ... 0x8c:
        aspeed_timer_set_value(s, (offset >> TIMER_NR_REGS) - 1, reg, tv);
        break;
    /* Illegal */
    case 0x38:
    case 0x3C:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                __func__, offset);
        break;
    }
}

static const MemoryRegionOps aspeed_timer_ops = {
    .read = aspeed_timer_read,
    .write = aspeed_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void aspeed_init_one_timer(AspeedTimerCtrlState *s, uint8_t id)
{
    AspeedTimer *t = &s->timers[id];

    t->id = id;
    timer_init_ns(&t->timer, QEMU_CLOCK_VIRTUAL, aspeed_timer_expire, t);
}

static void aspeed_timer_realize(DeviceState *dev, Error **errp)
{
    int i;
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedTimerCtrlState *s = ASPEED_TIMER(dev);
    Object *obj;
    Error *err = NULL;

    obj = object_property_get_link(OBJECT(dev), "scu", &err);
    if (!obj) {
        error_propagate_prepend(errp, err, "required link 'scu' not found: ");
        return;
    }
    s->scu = ASPEED_SCU(obj);

    for (i = 0; i < ASPEED_TIMER_NR_TIMERS; i++) {
        aspeed_init_one_timer(s, i);
        sysbus_init_irq(sbd, &s->timers[i].irq);
    }
    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_timer_ops, s,
                          TYPE_ASPEED_TIMER, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_timer_reset(DeviceState *dev)
{
    int i;
    AspeedTimerCtrlState *s = ASPEED_TIMER(dev);

    for (i = 0; i < ASPEED_TIMER_NR_TIMERS; i++) {
        AspeedTimer *t = &s->timers[i];
        /* Explicitly call helpers to avoid any conditional behaviour through
         * aspeed_timer_set_ctrl().
         */
        aspeed_timer_ctrl_enable(t, false);
        aspeed_timer_ctrl_external_clock(t, TIMER_CLOCK_USE_APB);
        aspeed_timer_ctrl_overflow_interrupt(t, false);
        aspeed_timer_ctrl_pulse_enable(t, false);
        t->level = 0;
        t->reload = 0;
        t->match[0] = 0;
        t->match[1] = 0;
    }
    s->ctrl = 0;
    s->ctrl2 = 0;
}

static const VMStateDescription vmstate_aspeed_timer = {
    .name = "aspeed.timer",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(id, AspeedTimer),
        VMSTATE_INT32(level, AspeedTimer),
        VMSTATE_TIMER(timer, AspeedTimer),
        VMSTATE_UINT32(reload, AspeedTimer),
        VMSTATE_UINT32_ARRAY(match, AspeedTimer, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_aspeed_timer_state = {
    .name = "aspeed.timerctrl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, AspeedTimerCtrlState),
        VMSTATE_UINT32(ctrl2, AspeedTimerCtrlState),
        VMSTATE_STRUCT_ARRAY(timers, AspeedTimerCtrlState,
                             ASPEED_TIMER_NR_TIMERS, 1, vmstate_aspeed_timer,
                             AspeedTimer),
        VMSTATE_END_OF_LIST()
    }
};

static void timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_timer_realize;
    dc->reset = aspeed_timer_reset;
    dc->desc = "ASPEED Timer";
    dc->vmsd = &vmstate_aspeed_timer_state;
}

static const TypeInfo aspeed_timer_info = {
    .name = TYPE_ASPEED_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedTimerCtrlState),
    .class_init = timer_class_init,
};

static void aspeed_timer_register_types(void)
{
    type_register_static(&aspeed_timer_info);
}

type_init(aspeed_timer_register_types)
