/*
 * QEMU lowRISC Ibex Timer device
 *
 * Copyright (c) 2021 Western Digital
 *
 * For details check the documentation here:
 *    https://docs.opentitan.org/hw/ip/rv_timer/doc/
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/timer/ibex_timer.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "target/riscv/cpu.h"
#include "migration/vmstate.h"

REG32(CTRL, 0x00)
    FIELD(CTRL, ACTIVE, 0, 1)
REG32(CFG0, 0x100)
    FIELD(CFG0, PRESCALE, 0, 12)
    FIELD(CFG0, STEP, 16, 8)
REG32(LOWER0, 0x104)
REG32(UPPER0, 0x108)
REG32(COMPARE_LOWER0, 0x10C)
REG32(COMPARE_UPPER0, 0x110)
REG32(INTR_ENABLE, 0x114)
    FIELD(INTR_ENABLE, IE_0, 0, 1)
REG32(INTR_STATE, 0x118)
    FIELD(INTR_STATE, IS_0, 0, 1)
REG32(INTR_TEST, 0x11C)
    FIELD(INTR_TEST, T_0, 0, 1)

static uint64_t cpu_riscv_read_rtc(uint32_t timebase_freq)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                    timebase_freq, NANOSECONDS_PER_SECOND);
}

static void ibex_timer_update_irqs(IbexTimerState *s)
{
    CPUState *cs = qemu_get_cpu(0);
    RISCVCPU *cpu = RISCV_CPU(cs);
    uint64_t value = s->timer_compare_lower0 |
                         ((uint64_t)s->timer_compare_upper0 << 32);
    uint64_t next, diff;
    uint64_t now = cpu_riscv_read_rtc(s->timebase_freq);

    if (!(s->timer_ctrl & R_CTRL_ACTIVE_MASK)) {
        /* Timer isn't active */
        return;
    }

    /* Update the CPUs mtimecmp */
    cpu->env.timecmp = value;

    if (cpu->env.timecmp <= now) {
        /*
         * If the mtimecmp was in the past raise the interrupt now.
         */
        qemu_irq_raise(s->m_timer_irq);
        if (s->timer_intr_enable & R_INTR_ENABLE_IE_0_MASK) {
            s->timer_intr_state |= R_INTR_STATE_IS_0_MASK;
            qemu_set_irq(s->irq, true);
        }
        return;
    }

    /* Setup a timer to trigger the interrupt in the future */
    qemu_irq_lower(s->m_timer_irq);
    qemu_set_irq(s->irq, false);

    diff = cpu->env.timecmp - now;
    next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                 muldiv64(diff,
                                          NANOSECONDS_PER_SECOND,
                                          s->timebase_freq);

    if (next < qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
        /* We overflowed the timer, just set it as large as we can */
        timer_mod(cpu->env.timer, 0x7FFFFFFFFFFFFFFF);
    } else {
        timer_mod(cpu->env.timer, next);
    }
}

static void ibex_timer_cb(void *opaque)
{
    IbexTimerState *s = opaque;

    qemu_irq_raise(s->m_timer_irq);
    if (s->timer_intr_enable & R_INTR_ENABLE_IE_0_MASK) {
        s->timer_intr_state |= R_INTR_STATE_IS_0_MASK;
        qemu_set_irq(s->irq, true);
    }
}

static void ibex_timer_reset(DeviceState *dev)
{
    IbexTimerState *s = IBEX_TIMER(dev);

    CPUState *cpu = qemu_get_cpu(0);
    CPURISCVState *env = cpu->env_ptr;
    env->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                              &ibex_timer_cb, s);
    env->timecmp = 0;

    s->timer_ctrl = 0x00000000;
    s->timer_cfg0 = 0x00010000;
    s->timer_compare_lower0 = 0xFFFFFFFF;
    s->timer_compare_upper0 = 0xFFFFFFFF;
    s->timer_intr_enable = 0x00000000;
    s->timer_intr_state = 0x00000000;
    s->timer_intr_test = 0x00000000;

    ibex_timer_update_irqs(s);
}

static uint64_t ibex_timer_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    IbexTimerState *s = opaque;
    uint64_t now = cpu_riscv_read_rtc(s->timebase_freq);
    uint64_t retvalue = 0;

    switch (addr >> 2) {
    case R_CTRL:
        retvalue = s->timer_ctrl;
        break;
    case R_CFG0:
        retvalue = s->timer_cfg0;
        break;
    case R_LOWER0:
        retvalue = now;
        break;
    case R_UPPER0:
        retvalue = now >> 32;
        break;
    case R_COMPARE_LOWER0:
        retvalue = s->timer_compare_lower0;
        break;
    case R_COMPARE_UPPER0:
        retvalue = s->timer_compare_upper0;
        break;
    case R_INTR_ENABLE:
        retvalue = s->timer_intr_enable;
        break;
    case R_INTR_STATE:
        retvalue = s->timer_intr_state;
        break;
    case R_INTR_TEST:
        retvalue = s->timer_intr_test;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    return retvalue;
}

static void ibex_timer_write(void *opaque, hwaddr addr,
                             uint64_t val64, unsigned int size)
{
    IbexTimerState *s = opaque;
    uint32_t val = val64;

    switch (addr >> 2) {
    case R_CTRL:
        s->timer_ctrl = val;
        break;
    case R_CFG0:
        qemu_log_mask(LOG_UNIMP, "Changing prescale or step not supported");
        s->timer_cfg0 = val;
        break;
    case R_LOWER0:
        qemu_log_mask(LOG_UNIMP, "Changing timer value is not supported");
        break;
    case R_UPPER0:
        qemu_log_mask(LOG_UNIMP, "Changing timer value is not supported");
        break;
    case R_COMPARE_LOWER0:
        s->timer_compare_lower0 = val;
        ibex_timer_update_irqs(s);
        break;
    case R_COMPARE_UPPER0:
        s->timer_compare_upper0 = val;
        ibex_timer_update_irqs(s);
        break;
    case R_INTR_ENABLE:
        s->timer_intr_enable = val;
        break;
    case R_INTR_STATE:
        /* Write 1 to clear */
        s->timer_intr_state &= ~val;
        break;
    case R_INTR_TEST:
        s->timer_intr_test = val;
        if (s->timer_intr_enable &
            s->timer_intr_test &
            R_INTR_ENABLE_IE_0_MASK) {
            s->timer_intr_state |= R_INTR_STATE_IS_0_MASK;
            qemu_set_irq(s->irq, true);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps ibex_timer_ops = {
    .read = ibex_timer_read,
    .write = ibex_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static int ibex_timer_post_load(void *opaque, int version_id)
{
    IbexTimerState *s = opaque;

    ibex_timer_update_irqs(s);
    return 0;
}

static const VMStateDescription vmstate_ibex_timer = {
    .name = TYPE_IBEX_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ibex_timer_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(timer_ctrl, IbexTimerState),
        VMSTATE_UINT32(timer_cfg0, IbexTimerState),
        VMSTATE_UINT32(timer_compare_lower0, IbexTimerState),
        VMSTATE_UINT32(timer_compare_upper0, IbexTimerState),
        VMSTATE_UINT32(timer_intr_enable, IbexTimerState),
        VMSTATE_UINT32(timer_intr_state, IbexTimerState),
        VMSTATE_UINT32(timer_intr_test, IbexTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property ibex_timer_properties[] = {
    DEFINE_PROP_UINT32("timebase-freq", IbexTimerState, timebase_freq, 10000),
    DEFINE_PROP_END_OF_LIST(),
};

static void ibex_timer_init(Object *obj)
{
    IbexTimerState *s = IBEX_TIMER(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &ibex_timer_ops, s,
                          TYPE_IBEX_TIMER, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void ibex_timer_realize(DeviceState *dev, Error **errp)
{
    IbexTimerState *s = IBEX_TIMER(dev);

    qdev_init_gpio_out(dev, &s->m_timer_irq, 1);
}


static void ibex_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = ibex_timer_reset;
    dc->vmsd = &vmstate_ibex_timer;
    dc->realize = ibex_timer_realize;
    device_class_set_props(dc, ibex_timer_properties);
}

static const TypeInfo ibex_timer_info = {
    .name          = TYPE_IBEX_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IbexTimerState),
    .instance_init = ibex_timer_init,
    .class_init    = ibex_timer_class_init,
};

static void ibex_timer_register_types(void)
{
    type_register_static(&ibex_timer_info);
}

type_init(ibex_timer_register_types)
