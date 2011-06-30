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

#include "hw.h"
#include "sysbus.h"
#include "trace.h"
#include "qemu-timer.h"
#include "qemu-error.h"

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

struct LM32TimerState {
    SysBusDevice busdev;

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

static uint32_t timer_read(void *opaque, target_phys_addr_t addr)
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

static void timer_write(void *opaque, target_phys_addr_t addr, uint32_t value)
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

static CPUReadMemoryFunc * const timer_read_fn[] = {
    NULL,
    NULL,
    &timer_read,
};

static CPUWriteMemoryFunc * const timer_write_fn[] = {
    NULL,
    NULL,
    &timer_write,
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
    LM32TimerState *s = container_of(d, LM32TimerState, busdev.qdev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }
    ptimer_stop(s->ptimer);
}

static int lm32_timer_init(SysBusDevice *dev)
{
    LM32TimerState *s = FROM_SYSBUS(typeof(*s), dev);
    int timer_regs;

    sysbus_init_irq(dev, &s->irq);

    s->bh = qemu_bh_new(timer_hit, s);
    s->ptimer = ptimer_init(s->bh);
    ptimer_set_freq(s->ptimer, s->freq_hz);

    timer_regs = cpu_register_io_memory(timer_read_fn, timer_write_fn, s,
            DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, R_MAX * 4, timer_regs);

    return 0;
}

static const VMStateDescription vmstate_lm32_timer = {
    .name = "lm32-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_PTIMER(ptimer, LM32TimerState),
        VMSTATE_UINT32(freq_hz, LM32TimerState),
        VMSTATE_UINT32_ARRAY(regs, LM32TimerState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo lm32_timer_info = {
    .init = lm32_timer_init,
    .qdev.name  = "lm32-timer",
    .qdev.size  = sizeof(LM32TimerState),
    .qdev.vmsd  = &vmstate_lm32_timer,
    .qdev.reset = timer_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32(
                "frequency", LM32TimerState, freq_hz, DEFAULT_FREQUENCY
        ),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void lm32_timer_register(void)
{
    sysbus_register_withprop(&lm32_timer_info);
}

device_init(lm32_timer_register)
