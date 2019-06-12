/*
 *  QEMU model of the LatticeMico32 timer block.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Specification available at:
 *   http://www.latticesemi.com/documents/mico32timer.pdf
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"

#define DEFAULT_FREQUENCY (50*1000000)

enum {
    R_SR = 0,
    R_CR,
    R_PERIOD,
    R_SNAPSHOT,
    R_MAX
};

enum {
    SR_TO    = (1 << 0),
    SR_RUN   = (1 << 1),
};

enum {
    CR_ITO   = (1 << 0),
    CR_CONT  = (1 << 1),
    CR_START = (1 << 2),
    CR_STOP  = (1 << 3),
};

#define TYPE_LM32_TIMER "lm32-timer"
#define LM32_TIMER(obj) OBJECT_CHECK(LM32TimerState, (obj), TYPE_LM32_TIMER)

struct LM32TimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    QEMUBH *bh;
    ptimer_state *ptimer;

    qemu_irq irq;
    uint32_t freq_hz;

    uint32_t regs[R_MAX];
};
typedef struct LM32TimerState LM32TimerState;

static void timer_update_irq(LM32TimerState *s)
{
    int state = (s->regs[R_SR] & SR_TO) && (s->regs[R_CR] & CR_ITO);

    trace_lm32_timer_irq_state(state);
    qemu_set_irq(s->irq, state);
}

static uint64_t timer_read(void *opaque, hwaddr addr, unsigned size)
{
    LM32TimerState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_SR:
    case R_CR:
    case R_PERIOD:
        r = s->regs[addr];
        break;
    case R_SNAPSHOT:
        r = (uint32_t)ptimer_get_count(s->ptimer);
        break;
    default:
        error_report("lm32_timer: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_lm32_timer_memory_read(addr << 2, r);
    return r;
}

static void timer_write(void *opaque, hwaddr addr,
                        uint64_t value, unsigned size)
{
    LM32TimerState *s = opaque;

    trace_lm32_timer_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_SR:
        s->regs[R_SR] &= ~SR_TO;
        break;
    case R_CR:
        s->regs[R_CR] = value;
        if (s->regs[R_CR] & CR_START) {
            ptimer_run(s->ptimer, 1);
        }
        if (s->regs[R_CR] & CR_STOP) {
            ptimer_stop(s->ptimer);
        }
        break;
    case R_PERIOD:
        s->regs[R_PERIOD] = value;
        ptimer_set_count(s->ptimer, value);
        break;
    case R_SNAPSHOT:
        error_report("lm32_timer: write access to read only register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    default:
        error_report("lm32_timer: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
    timer_update_irq(s);
}

static const MemoryRegionOps timer_ops = {
    .read = timer_read,
    .write = timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void timer_hit(void *opaque)
{
    LM32TimerState *s = opaque;

    trace_lm32_timer_hit();

    s->regs[R_SR] |= SR_TO;

    if (s->regs[R_CR] & CR_CONT) {
        ptimer_set_count(s->ptimer, s->regs[R_PERIOD]);
        ptimer_run(s->ptimer, 1);
    }
    timer_update_irq(s);
}

static void timer_reset(DeviceState *d)
{
    LM32TimerState *s = LM32_TIMER(d);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }
    ptimer_stop(s->ptimer);
}

static void lm32_timer_init(Object *obj)
{
    LM32TimerState *s = LM32_TIMER(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(dev, &s->irq);

    s->bh = qemu_bh_new(timer_hit, s);
    s->ptimer = ptimer_init(s->bh, PTIMER_POLICY_DEFAULT);

    memory_region_init_io(&s->iomem, obj, &timer_ops, s,
                          "timer", R_MAX * 4);
    sysbus_init_mmio(dev, &s->iomem);
}

static void lm32_timer_realize(DeviceState *dev, Error **errp)
{
    LM32TimerState *s = LM32_TIMER(dev);

    ptimer_set_freq(s->ptimer, s->freq_hz);
}

static const VMStateDescription vmstate_lm32_timer = {
    .name = "lm32-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(ptimer, LM32TimerState),
        VMSTATE_UINT32(freq_hz, LM32TimerState),
        VMSTATE_UINT32_ARRAY(regs, LM32TimerState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static Property lm32_timer_properties[] = {
    DEFINE_PROP_UINT32("frequency", LM32TimerState, freq_hz, DEFAULT_FREQUENCY),
    DEFINE_PROP_END_OF_LIST(),
};

static void lm32_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lm32_timer_realize;
    dc->reset = timer_reset;
    dc->vmsd = &vmstate_lm32_timer;
    dc->props = lm32_timer_properties;
}

static const TypeInfo lm32_timer_info = {
    .name          = TYPE_LM32_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LM32TimerState),
    .instance_init = lm32_timer_init,
    .class_init    = lm32_timer_class_init,
};

static void lm32_timer_register_types(void)
{
    type_register_static(&lm32_timer_info);
}

type_init(lm32_timer_register_types)
