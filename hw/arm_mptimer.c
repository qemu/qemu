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

#include "sysbus.h"
#include "qemu-timer.h"

/* This device implements the per-cpu private timer and watchdog block
 * which is used in both the ARM11MPCore and Cortex-A9MP.
 */

#define MAX_CPUS 4

/* State of a single timer or watchdog block */
typedef struct {
    uint32_t count;
    uint32_t load;
    uint32_t control;
    uint32_t status;
    int64_t tick;
    QEMUTimer *timer;
    qemu_irq irq;
    MemoryRegion iomem;
} timerblock;

typedef struct {
    SysBusDevice busdev;
    uint32_t num_cpu;
    timerblock timerblock[MAX_CPUS * 2];
    MemoryRegion iomem[2];
} arm_mptimer_state;

static inline int get_current_cpu(arm_mptimer_state *s)
{
    if (cpu_single_env->cpu_index >= s->num_cpu) {
        hw_error("arm_mptimer: num-cpu %d but this cpu is %d!\n",
                 s->num_cpu, cpu_single_env->cpu_index);
    }
    return cpu_single_env->cpu_index;
}

static inline void timerblock_update_irq(timerblock *tb)
{
    qemu_set_irq(tb->irq, tb->status);
}

/* Return conversion factor from mpcore timer ticks to qemu timer ticks.  */
static inline uint32_t timerblock_scale(timerblock *tb)
{
    return (((tb->control >> 8) & 0xff) + 1) * 10;
}

static void timerblock_reload(timerblock *tb, int restart)
{
    if (tb->count == 0) {
        return;
    }
    if (restart) {
        tb->tick = qemu_get_clock_ns(vm_clock);
    }
    tb->tick += (int64_t)tb->count * timerblock_scale(tb);
    qemu_mod_timer(tb->timer, tb->tick);
}

static void timerblock_tick(void *opaque)
{
    timerblock *tb = (timerblock *)opaque;
    tb->status = 1;
    if (tb->control & 2) {
        tb->count = tb->load;
        timerblock_reload(tb, 0);
    } else {
        tb->count = 0;
    }
    timerblock_update_irq(tb);
}

static uint64_t timerblock_read(void *opaque, target_phys_addr_t addr,
                                unsigned size)
{
    timerblock *tb = (timerblock *)opaque;
    int64_t val;
    switch (addr) {
    case 0: /* Load */
        return tb->load;
    case 4: /* Counter.  */
        if (((tb->control & 1) == 0) || (tb->count == 0)) {
            return 0;
        }
        /* Slow and ugly, but hopefully won't happen too often.  */
        val = tb->tick - qemu_get_clock_ns(vm_clock);
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

static void timerblock_write(void *opaque, target_phys_addr_t addr,
                             uint64_t value, unsigned size)
{
    timerblock *tb = (timerblock *)opaque;
    int64_t old;
    switch (addr) {
    case 0: /* Load */
        tb->load = value;
        /* Fall through.  */
    case 4: /* Counter.  */
        if ((tb->control & 1) && tb->count) {
            /* Cancel the previous timer.  */
            qemu_del_timer(tb->timer);
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
static uint64_t arm_thistimer_read(void *opaque, target_phys_addr_t addr,
                                   unsigned size)
{
    arm_mptimer_state *s = (arm_mptimer_state *)opaque;
    int id = get_current_cpu(s);
    return timerblock_read(&s->timerblock[id * 2], addr, size);
}

static void arm_thistimer_write(void *opaque, target_phys_addr_t addr,
                                uint64_t value, unsigned size)
{
    arm_mptimer_state *s = (arm_mptimer_state *)opaque;
    int id = get_current_cpu(s);
    timerblock_write(&s->timerblock[id * 2], addr, value, size);
}

static uint64_t arm_thiswdog_read(void *opaque, target_phys_addr_t addr,
                                  unsigned size)
{
    arm_mptimer_state *s = (arm_mptimer_state *)opaque;
    int id = get_current_cpu(s);
    return timerblock_read(&s->timerblock[id * 2 + 1], addr, size);
}

static void arm_thiswdog_write(void *opaque, target_phys_addr_t addr,
                               uint64_t value, unsigned size)
{
    arm_mptimer_state *s = (arm_mptimer_state *)opaque;
    int id = get_current_cpu(s);
    timerblock_write(&s->timerblock[id * 2 + 1], addr, value, size);
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

static const MemoryRegionOps arm_thiswdog_ops = {
    .read = arm_thiswdog_read,
    .write = arm_thiswdog_write,
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

static void timerblock_reset(timerblock *tb)
{
    tb->count = 0;
    tb->load = 0;
    tb->control = 0;
    tb->status = 0;
    tb->tick = 0;
}

static void arm_mptimer_reset(DeviceState *dev)
{
    arm_mptimer_state *s =
        FROM_SYSBUS(arm_mptimer_state, sysbus_from_qdev(dev));
    int i;
    /* We reset every timer in the array, not just the ones we're using,
     * because vmsave will look at every array element.
     */
    for (i = 0; i < ARRAY_SIZE(s->timerblock); i++) {
        timerblock_reset(&s->timerblock[i]);
    }
}

static int arm_mptimer_init(SysBusDevice *dev)
{
    arm_mptimer_state *s = FROM_SYSBUS(arm_mptimer_state, dev);
    int i;
    if (s->num_cpu < 1 || s->num_cpu > MAX_CPUS) {
        hw_error("%s: num-cpu must be between 1 and %d\n", __func__, MAX_CPUS);
    }
    /* We implement one timer and one watchdog block per CPU, and
     * expose multiple MMIO regions:
     *  * region 0 is "timer for this core"
     *  * region 1 is "watchdog for this core"
     *  * region 2 is "timer for core 0"
     *  * region 3 is "watchdog for core 0"
     *  * region 4 is "timer for core 1"
     *  * region 5 is "watchdog for core 1"
     * and so on.
     * The outgoing interrupt lines are
     *  * timer for core 0
     *  * watchdog for core 0
     *  * timer for core 1
     *  * watchdog for core 1
     * and so on.
     */
    memory_region_init_io(&s->iomem[0], &arm_thistimer_ops, s,
                          "arm_mptimer_timer", 0x20);
    sysbus_init_mmio(dev, &s->iomem[0]);
    memory_region_init_io(&s->iomem[1], &arm_thiswdog_ops, s,
                          "arm_mptimer_wdog", 0x20);
    sysbus_init_mmio(dev, &s->iomem[1]);
    for (i = 0; i < (s->num_cpu * 2); i++) {
        timerblock *tb = &s->timerblock[i];
        tb->timer = qemu_new_timer_ns(vm_clock, timerblock_tick, tb);
        sysbus_init_irq(dev, &tb->irq);
        memory_region_init_io(&tb->iomem, &timerblock_ops, tb,
                              "arm_mptimer_timerblock", 0x20);
        sysbus_init_mmio(dev, &tb->iomem);
    }

    return 0;
}

static const VMStateDescription vmstate_timerblock = {
    .name = "arm_mptimer_timerblock",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(count, timerblock),
        VMSTATE_UINT32(load, timerblock),
        VMSTATE_UINT32(control, timerblock),
        VMSTATE_UINT32(status, timerblock),
        VMSTATE_INT64(tick, timerblock),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_arm_mptimer = {
    .name = "arm_mptimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(timerblock, arm_mptimer_state, (MAX_CPUS * 2),
                             1, vmstate_timerblock, timerblock),
        VMSTATE_END_OF_LIST()
    }
};

static Property arm_mptimer_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", arm_mptimer_state, num_cpu, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void arm_mptimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);

    sbc->init = arm_mptimer_init;
    dc->vmsd = &vmstate_arm_mptimer;
    dc->reset = arm_mptimer_reset;
    dc->no_user = 1;
    dc->props = arm_mptimer_properties;
}

static TypeInfo arm_mptimer_info = {
    .name          = "arm_mptimer",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(arm_mptimer_state),
    .class_init    = arm_mptimer_class_init,
};

static void arm_mptimer_register_types(void)
{
    type_register_static(&arm_mptimer_info);
}

type_init(arm_mptimer_register_types)
