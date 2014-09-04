/*
 * Private peripheral timer/watchdog blocks for ARM 11MPCore and A9MP
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited
 * Written by Paul Brook, Peter Maydell
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/timer/arm_mptimer.h"
#include "qemu/timer.h"
#include "qom/cpu.h"

/* This device implements the per-cpu private timer and watchdog block
 * which is used in both the ARM11MPCore and Cortex-A9MP.
 */

static inline int get_current_cpu(ARMMPTimerState *s)
{
    if (current_cpu->cpu_index >= s->num_cpu) {
        hw_error("arm_mptimer: num-cpu %d but this cpu is %d!\n",
                 s->num_cpu, current_cpu->cpu_index);
    }
    return current_cpu->cpu_index;
}

static inline void timerblock_update_irq(TimerBlock *tb)
{
    qemu_set_irq(tb->irq, tb->status);
}

/* Return conversion factor from mpcore timer ticks to qemu timer ticks.  */
static inline uint32_t timerblock_scale(TimerBlock *tb)
{
    return (((tb->control >> 8) & 0xff) + 1) * 10;
}

static void timerblock_reload(TimerBlock *tb, int restart)
{
    if (tb->count == 0) {
        return;
    }
    if (restart) {
        tb->tick = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
    tb->tick += (int64_t)tb->count * timerblock_scale(tb);
    timer_mod(tb->timer, tb->tick);
}

static void timerblock_tick(void *opaque)
{
    TimerBlock *tb = (TimerBlock *)opaque;
    tb->status = 1;
    if (tb->control & 2) {
        tb->count = tb->load;
        timerblock_reload(tb, 0);
    } else {
        tb->count = 0;
    }
    timerblock_update_irq(tb);
}

static uint64_t timerblock_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    TimerBlock *tb = (TimerBlock *)opaque;
    int64_t val;
    switch (addr) {
    case 0: /* Load */
        return tb->load;
    case 4: /* Counter.  */
        if (((tb->control & 1) == 0) || (tb->count == 0)) {
            return 0;
        }
        /* Slow and ugly, but hopefully won't happen too often.  */
        val = tb->tick - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        val /= timerblock_scale(tb);
        if (val < 0) {
            val = 0;
        }
        return val;
    case 8: /* Control.  */
        return tb->control;
    case 12: /* Interrupt status.  */
        return tb->status;
    default:
        return 0;
    }
}

static void timerblock_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    TimerBlock *tb = (TimerBlock *)opaque;
    int64_t old;
    switch (addr) {
    case 0: /* Load */
        tb->load = value;
        /* Fall through.  */
    case 4: /* Counter.  */
        if ((tb->control & 1) && tb->count) {
            /* Cancel the previous timer.  */
            timer_del(tb->timer);
        }
        tb->count = value;
        if (tb->control & 1) {
            timerblock_reload(tb, 1);
        }
        break;
    case 8: /* Control.  */
        old = tb->control;
        tb->control = value;
        if (((old & 1) == 0) && (value & 1)) {
            if (tb->count == 0 && (tb->control & 2)) {
                tb->count = tb->load;
            }
            timerblock_reload(tb, 1);
        }
        break;
    case 12: /* Interrupt status.  */
        tb->status &= ~value;
        timerblock_update_irq(tb);
        break;
    }
}

/* Wrapper functions to implement the "read timer/watchdog for
 * the current CPU" memory regions.
 */
static uint64_t arm_thistimer_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    ARMMPTimerState *s = (ARMMPTimerState *)opaque;
    int id = get_current_cpu(s);
    return timerblock_read(&s->timerblock[id], addr, size);
}

static void arm_thistimer_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    ARMMPTimerState *s = (ARMMPTimerState *)opaque;
    int id = get_current_cpu(s);
    timerblock_write(&s->timerblock[id], addr, value, size);
}

static const MemoryRegionOps arm_thistimer_ops = {
    .read = arm_thistimer_read,
    .write = arm_thistimer_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps timerblock_ops = {
    .read = timerblock_read,
    .write = timerblock_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void timerblock_reset(TimerBlock *tb)
{
    tb->count = 0;
    tb->load = 0;
    tb->control = 0;
    tb->status = 0;
    tb->tick = 0;
    if (tb->timer) {
        timer_del(tb->timer);
    }
}

static void arm_mptimer_reset(DeviceState *dev)
{
    ARMMPTimerState *s = ARM_MPTIMER(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->timerblock); i++) {
        timerblock_reset(&s->timerblock[i]);
    }
}

static void arm_mptimer_init(Object *obj)
{
    ARMMPTimerState *s = ARM_MPTIMER(obj);

    memory_region_init_io(&s->iomem, obj, &arm_thistimer_ops, s,
                          "arm_mptimer_timer", 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void arm_mptimer_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    ARMMPTimerState *s = ARM_MPTIMER(dev);
    int i;

    if (s->num_cpu < 1 || s->num_cpu > ARM_MPTIMER_MAX_CPUS) {
        hw_error("%s: num-cpu must be between 1 and %d\n",
                 __func__, ARM_MPTIMER_MAX_CPUS);
    }
    /* We implement one timer block per CPU, and expose multiple MMIO regions:
     *  * region 0 is "timer for this core"
     *  * region 1 is "timer for core 0"
     *  * region 2 is "timer for core 1"
     * and so on.
     * The outgoing interrupt lines are
     *  * timer for core 0
     *  * timer for core 1
     * and so on.
     */
    for (i = 0; i < s->num_cpu; i++) {
        TimerBlock *tb = &s->timerblock[i];
        tb->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, timerblock_tick, tb);
        sysbus_init_irq(sbd, &tb->irq);
        memory_region_init_io(&tb->iomem, OBJECT(s), &timerblock_ops, tb,
                              "arm_mptimer_timerblock", 0x20);
        sysbus_init_mmio(sbd, &tb->iomem);
    }
}

static const VMStateDescription vmstate_timerblock = {
    .name = "arm_mptimer_timerblock",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(count, TimerBlock),
        VMSTATE_UINT32(load, TimerBlock),
        VMSTATE_UINT32(control, TimerBlock),
        VMSTATE_UINT32(status, TimerBlock),
        VMSTATE_INT64(tick, TimerBlock),
        VMSTATE_TIMER(timer, TimerBlock),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_arm_mptimer = {
    .name = "arm_mptimer",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_UINT32(timerblock, ARMMPTimerState, num_cpu,
                                     2, vmstate_timerblock, TimerBlock),
        VMSTATE_END_OF_LIST()
    }
};

static Property arm_mptimer_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", ARMMPTimerState, num_cpu, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void arm_mptimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = arm_mptimer_realize;
    dc->vmsd = &vmstate_arm_mptimer;
    dc->reset = arm_mptimer_reset;
    dc->props = arm_mptimer_properties;
}

static const TypeInfo arm_mptimer_info = {
    .name          = TYPE_ARM_MPTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMMPTimerState),
    .instance_init = arm_mptimer_init,
    .class_init    = arm_mptimer_class_init,
};

static void arm_mptimer_register_types(void)
{
    type_register_static(&arm_mptimer_info);
}

type_init(arm_mptimer_register_types)
