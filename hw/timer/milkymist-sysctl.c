/*
 *  QEMU model of the Milkymist System Controller.
 *
 *  Copyright (c) 2010-2012 Michael Walle <michael@walle.cc>
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

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "trace.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "qemu/error-report.h"

enum {
    CTRL_ENABLE      = (1<<0),
    CTRL_AUTORESTART = (1<<1),
};

enum {
    ICAP_READY       = (1<<0),
};

enum {
    R_GPIO_IN         = 0,
    R_GPIO_OUT,
    R_GPIO_INTEN,
    R_TIMER0_CONTROL  = 4,
    R_TIMER0_COMPARE,
    R_TIMER0_COUNTER,
    R_TIMER1_CONTROL  = 8,
    R_TIMER1_COMPARE,
    R_TIMER1_COUNTER,
    R_ICAP = 16,
    R_DBG_SCRATCHPAD  = 20,
    R_DBG_WRITE_LOCK,
    R_CLK_FREQUENCY   = 29,
    R_CAPABILITIES,
    R_SYSTEM_ID,
    R_MAX
};

#define TYPE_MILKYMIST_SYSCTL "milkymist-sysctl"
#define MILKYMIST_SYSCTL(obj) \
    OBJECT_CHECK(MilkymistSysctlState, (obj), TYPE_MILKYMIST_SYSCTL)

struct MilkymistSysctlState {
    SysBusDevice parent_obj;

    MemoryRegion regs_region;

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

static uint64_t sysctl_read(void *opaque, hwaddr addr,
                            unsigned size)
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
    case R_DBG_SCRATCHPAD:
    case R_DBG_WRITE_LOCK:
    case R_CLK_FREQUENCY:
    case R_CAPABILITIES:
    case R_SYSTEM_ID:
        r = s->regs[addr];
        break;

    default:
        error_report("milkymist_sysctl: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_sysctl_memory_read(addr << 2, r);

    return r;
}

static void sysctl_write(void *opaque, hwaddr addr, uint64_t value,
                         unsigned size)
{
    MilkymistSysctlState *s = opaque;

    trace_milkymist_sysctl_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_GPIO_OUT:
    case R_GPIO_INTEN:
    case R_TIMER0_COUNTER:
    case R_TIMER1_COUNTER:
    case R_DBG_SCRATCHPAD:
        s->regs[addr] = value;
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
            trace_milkymist_sysctl_start_timer0();
            ptimer_set_count(s->ptimer0,
                    s->regs[R_TIMER0_COMPARE] - s->regs[R_TIMER0_COUNTER]);
            ptimer_run(s->ptimer0, 0);
        } else {
            trace_milkymist_sysctl_stop_timer0();
            ptimer_stop(s->ptimer0);
        }
        break;
    case R_TIMER1_CONTROL:
        s->regs[addr] = value;
        if (s->regs[R_TIMER1_CONTROL] & CTRL_ENABLE) {
            trace_milkymist_sysctl_start_timer1();
            ptimer_set_count(s->ptimer1,
                    s->regs[R_TIMER1_COMPARE] - s->regs[R_TIMER1_COUNTER]);
            ptimer_run(s->ptimer1, 0);
        } else {
            trace_milkymist_sysctl_stop_timer1();
            ptimer_stop(s->ptimer1);
        }
        break;
    case R_ICAP:
        sysctl_icap_write(s, value);
        break;
    case R_DBG_WRITE_LOCK:
        s->regs[addr] = 1;
        break;
    case R_SYSTEM_ID:
        qemu_system_reset_request();
        break;

    case R_GPIO_IN:
    case R_CLK_FREQUENCY:
    case R_CAPABILITIES:
        error_report("milkymist_sysctl: write to read-only register 0x"
                TARGET_FMT_plx, addr << 2);
        break;

    default:
        error_report("milkymist_sysctl: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
}

static const MemoryRegionOps sysctl_mmio_ops = {
    .read = sysctl_read,
    .write = sysctl_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
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
    MilkymistSysctlState *s = MILKYMIST_SYSCTL(d);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }

    ptimer_stop(s->ptimer0);
    ptimer_stop(s->ptimer1);

    /* defaults */
    s->regs[R_ICAP] = ICAP_READY;
    s->regs[R_SYSTEM_ID] = s->systemid;
    s->regs[R_CLK_FREQUENCY] = s->freq_hz;
    s->regs[R_CAPABILITIES] = s->capabilities;
    s->regs[R_GPIO_IN] = s->strappings;
}

static int milkymist_sysctl_init(SysBusDevice *dev)
{
    MilkymistSysctlState *s = MILKYMIST_SYSCTL(dev);

    sysbus_init_irq(dev, &s->gpio_irq);
    sysbus_init_irq(dev, &s->timer0_irq);
    sysbus_init_irq(dev, &s->timer1_irq);

    s->bh0 = qemu_bh_new(timer0_hit, s);
    s->bh1 = qemu_bh_new(timer1_hit, s);
    s->ptimer0 = ptimer_init(s->bh0);
    s->ptimer1 = ptimer_init(s->bh1);
    ptimer_set_freq(s->ptimer0, s->freq_hz);
    ptimer_set_freq(s->ptimer1, s->freq_hz);

    memory_region_init_io(&s->regs_region, OBJECT(s), &sysctl_mmio_ops, s,
            "milkymist-sysctl", R_MAX * 4);
    sysbus_init_mmio(dev, &s->regs_region);

    return 0;
}

static const VMStateDescription vmstate_milkymist_sysctl = {
    .name = "milkymist-sysctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistSysctlState, R_MAX),
        VMSTATE_PTIMER(ptimer0, MilkymistSysctlState),
        VMSTATE_PTIMER(ptimer1, MilkymistSysctlState),
        VMSTATE_END_OF_LIST()
    }
};

static Property milkymist_sysctl_properties[] = {
    DEFINE_PROP_UINT32("frequency", MilkymistSysctlState,
    freq_hz, 80000000),
    DEFINE_PROP_UINT32("capabilities", MilkymistSysctlState,
    capabilities, 0x00000000),
    DEFINE_PROP_UINT32("systemid", MilkymistSysctlState,
    systemid, 0x10014d31),
    DEFINE_PROP_UINT32("gpio_strappings", MilkymistSysctlState,
    strappings, 0x00000001),
    DEFINE_PROP_END_OF_LIST(),
};

static void milkymist_sysctl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = milkymist_sysctl_init;
    dc->reset = milkymist_sysctl_reset;
    dc->vmsd = &vmstate_milkymist_sysctl;
    dc->props = milkymist_sysctl_properties;
}

static const TypeInfo milkymist_sysctl_info = {
    .name          = TYPE_MILKYMIST_SYSCTL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MilkymistSysctlState),
    .class_init    = milkymist_sysctl_class_init,
};

static void milkymist_sysctl_register_types(void)
{
    type_register_static(&milkymist_sysctl_info);
}

type_init(milkymist_sysctl_register_types)
