/*
 *  QEMU model of the Milkymist System Controller.
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
 *   http://www.milkymist.org/socdoc/sysctl.pdf
 */

#include "hw.h"
#include "sysbus.h"
#include "sysemu.h"
#include "trace.h"
#include "qemu-timer.h"
#include "qemu-error.h"

enum {
    CTRL_ENABLE      = (1<<0),
    CTRL_AUTORESTART = (1<<1),
};

enum {
    ICAP_READY       = (1<<0),
};

enum {
    R_GPIO_IN = 0,
    R_GPIO_OUT,
    R_GPIO_INTEN,
    R_RESERVED0,
    R_TIMER0_CONTROL,
    R_TIMER0_COMPARE,
    R_TIMER0_COUNTER,
    R_RESERVED1,
    R_TIMER1_CONTROL,
    R_TIMER1_COMPARE,
    R_TIMER1_COUNTER,
    R_RESERVED2,
    R_RESERVED3,
    R_ICAP,
    R_CAPABILITIES,
    R_SYSTEM_ID,
    R_MAX
};

struct MilkymistSysctlState {
    SysBusDevice busdev;

    QEMUBH *bh0;
    QEMUBH *bh1;
    ptimer_state *ptimer0;
    ptimer_state *ptimer1;

    uint32_t freq_hz;
    uint32_t capabilities;
    uint32_t systemid;
    uint32_t strappings;

    uint32_t regs[R_MAX];

    qemu_irq gpio_irq;
    qemu_irq timer0_irq;
    qemu_irq timer1_irq;
};
typedef struct MilkymistSysctlState MilkymistSysctlState;

static void sysctl_icap_write(MilkymistSysctlState *s, uint32_t value)
{
    trace_milkymist_sysctl_icap_write(value);
    switch (value & 0xffff) {
    case 0x000e:
        qemu_system_shutdown_request();
        break;
    }
}

static uint32_t sysctl_read(void *opaque, target_phys_addr_t addr)
{
    MilkymistSysctlState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_TIMER0_COUNTER:
        r = (uint32_t)ptimer_get_count(s->ptimer0);
        /* milkymist timer counts up */
        r = s->regs[R_TIMER0_COMPARE] - r;
        break;
    case R_TIMER1_COUNTER:
        r = (uint32_t)ptimer_get_count(s->ptimer1);
        /* milkymist timer counts up */
        r = s->regs[R_TIMER1_COMPARE] - r;
        break;
    case R_GPIO_IN:
    case R_GPIO_OUT:
    case R_GPIO_INTEN:
    case R_TIMER0_CONTROL:
    case R_TIMER0_COMPARE:
    case R_TIMER1_CONTROL:
    case R_TIMER1_COMPARE:
    case R_ICAP:
    case R_CAPABILITIES:
    case R_SYSTEM_ID:
        r = s->regs[addr];
        break;

    default:
        error_report("milkymist_sysctl: read access to unkown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_sysctl_memory_read(addr << 2, r);

    return r;
}

static void sysctl_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    MilkymistSysctlState *s = opaque;

    trace_milkymist_sysctl_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_GPIO_OUT:
    case R_GPIO_INTEN:
    case R_TIMER0_COUNTER:
        if (value > s->regs[R_TIMER0_COUNTER]) {
            value = s->regs[R_TIMER0_COUNTER];
            error_report("milkymist_sysctl: timer0: trying to write a "
                    "value greater than the limit. Clipping.");
        }
        /* milkymist timer counts up */
        value = s->regs[R_TIMER0_COUNTER] - value;
        ptimer_set_count(s->ptimer0, value);
        break;
    case R_TIMER1_COUNTER:
        if (value > s->regs[R_TIMER1_COUNTER]) {
            value = s->regs[R_TIMER1_COUNTER];
            error_report("milkymist_sysctl: timer1: trying to write a "
                    "value greater than the limit. Clipping.");
        }
        /* milkymist timer counts up */
        value = s->regs[R_TIMER1_COUNTER] - value;
        ptimer_set_count(s->ptimer1, value);
        break;
    case R_TIMER0_COMPARE:
        ptimer_set_limit(s->ptimer0, value, 0);
        s->regs[addr] = value;
        break;
    case R_TIMER1_COMPARE:
        ptimer_set_limit(s->ptimer1, value, 0);
        s->regs[addr] = value;
        break;
    case R_TIMER0_CONTROL:
        s->regs[addr] = value;
        if (s->regs[R_TIMER0_CONTROL] & CTRL_ENABLE) {
            trace_milkymist_sysctl_start_timer1();
            ptimer_run(s->ptimer0, 0);
        } else {
            trace_milkymist_sysctl_stop_timer1();
            ptimer_stop(s->ptimer0);
        }
        break;
    case R_TIMER1_CONTROL:
        s->regs[addr] = value;
        if (s->regs[R_TIMER1_CONTROL] & CTRL_ENABLE) {
            trace_milkymist_sysctl_start_timer1();
            ptimer_run(s->ptimer1, 0);
        } else {
            trace_milkymist_sysctl_stop_timer1();
            ptimer_stop(s->ptimer1);
        }
        break;
    case R_ICAP:
        sysctl_icap_write(s, value);
        break;
    case R_SYSTEM_ID:
        qemu_system_reset_request();
        break;

    case R_GPIO_IN:
    case R_CAPABILITIES:
        error_report("milkymist_sysctl: write to read-only register 0x"
                TARGET_FMT_plx, addr << 2);
        break;

    default:
        error_report("milkymist_sysctl: write access to unkown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
}

static CPUReadMemoryFunc * const sysctl_read_fn[] = {
    NULL,
    NULL,
    &sysctl_read,
};

static CPUWriteMemoryFunc * const sysctl_write_fn[] = {
    NULL,
    NULL,
    &sysctl_write,
};

static void timer0_hit(void *opaque)
{
    MilkymistSysctlState *s = opaque;

    if (!(s->regs[R_TIMER0_CONTROL] & CTRL_AUTORESTART)) {
        s->regs[R_TIMER0_CONTROL] &= ~CTRL_ENABLE;
        trace_milkymist_sysctl_stop_timer0();
        ptimer_stop(s->ptimer0);
    }

    trace_milkymist_sysctl_pulse_irq_timer0();
    qemu_irq_pulse(s->timer0_irq);
}

static void timer1_hit(void *opaque)
{
    MilkymistSysctlState *s = opaque;

    if (!(s->regs[R_TIMER1_CONTROL] & CTRL_AUTORESTART)) {
        s->regs[R_TIMER1_CONTROL] &= ~CTRL_ENABLE;
        trace_milkymist_sysctl_stop_timer1();
        ptimer_stop(s->ptimer1);
    }

    trace_milkymist_sysctl_pulse_irq_timer1();
    qemu_irq_pulse(s->timer1_irq);
}

static void milkymist_sysctl_reset(DeviceState *d)
{
    MilkymistSysctlState *s =
            container_of(d, MilkymistSysctlState, busdev.qdev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }

    ptimer_stop(s->ptimer0);
    ptimer_stop(s->ptimer1);

    /* defaults */
    s->regs[R_ICAP] = ICAP_READY;
    s->regs[R_SYSTEM_ID] = s->systemid;
    s->regs[R_CAPABILITIES] = s->capabilities;
    s->regs[R_GPIO_IN] = s->strappings;
}

static int milkymist_sysctl_init(SysBusDevice *dev)
{
    MilkymistSysctlState *s = FROM_SYSBUS(typeof(*s), dev);
    int sysctl_regs;

    sysbus_init_irq(dev, &s->gpio_irq);
    sysbus_init_irq(dev, &s->timer0_irq);
    sysbus_init_irq(dev, &s->timer1_irq);

    s->bh0 = qemu_bh_new(timer0_hit, s);
    s->bh1 = qemu_bh_new(timer1_hit, s);
    s->ptimer0 = ptimer_init(s->bh0);
    s->ptimer1 = ptimer_init(s->bh1);
    ptimer_set_freq(s->ptimer0, s->freq_hz);
    ptimer_set_freq(s->ptimer1, s->freq_hz);

    sysctl_regs = cpu_register_io_memory(sysctl_read_fn, sysctl_write_fn, s,
            DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, R_MAX * 4, sysctl_regs);

    return 0;
}

static const VMStateDescription vmstate_milkymist_sysctl = {
    .name = "milkymist-sysctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistSysctlState, R_MAX),
        VMSTATE_PTIMER(ptimer0, MilkymistSysctlState),
        VMSTATE_PTIMER(ptimer1, MilkymistSysctlState),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo milkymist_sysctl_info = {
    .init = milkymist_sysctl_init,
    .qdev.name  = "milkymist-sysctl",
    .qdev.size  = sizeof(MilkymistSysctlState),
    .qdev.vmsd  = &vmstate_milkymist_sysctl,
    .qdev.reset = milkymist_sysctl_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("frequency", MilkymistSysctlState,
                freq_hz, 80000000),
        DEFINE_PROP_UINT32("capabilities", MilkymistSysctlState,
                capabilities, 0x00000000),
        DEFINE_PROP_UINT32("systemid", MilkymistSysctlState,
                systemid, 0x10014d31),
        DEFINE_PROP_UINT32("gpio_strappings", MilkymistSysctlState,
                strappings, 0x00000001),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void milkymist_sysctl_register(void)
{
    sysbus_register_withprop(&milkymist_sysctl_info);
}

device_init(milkymist_sysctl_register)
