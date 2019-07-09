/*
 * Global peripheral timer block for ARM A9MP
 *
 * (C) 2013 Xilinx Inc.
 *
 * Written by François LEGAL
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/timer/a9gtimer.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/cpu.h"

#ifndef A9_GTIMER_ERR_DEBUG
#define A9_GTIMER_ERR_DEBUG 0
#endif

#define DB_PRINT_L(level, ...) do { \
    if (A9_GTIMER_ERR_DEBUG > (level)) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0)

#define DB_PRINT(...) DB_PRINT_L(0, ## __VA_ARGS__)

static inline int a9_gtimer_get_current_cpu(A9GTimerState *s)
{
    if (current_cpu->cpu_index >= s->num_cpu) {
        hw_error("a9gtimer: num-cpu %d but this cpu is %d!\n",
                 s->num_cpu, current_cpu->cpu_index);
    }
    return current_cpu->cpu_index;
}

static inline uint64_t a9_gtimer_get_conv(A9GTimerState *s)
{
    uint64_t prescale = extract32(s->control, R_CONTROL_PRESCALER_SHIFT,
                                  R_CONTROL_PRESCALER_LEN);

    return (prescale + 1) * 10;
}

static A9GTimerUpdate a9_gtimer_get_update(A9GTimerState *s)
{
    A9GTimerUpdate ret;

    ret.now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    ret.new = s->ref_counter +
              (ret.now - s->cpu_ref_time) / a9_gtimer_get_conv(s);
    return ret;
}

static void a9_gtimer_update(A9GTimerState *s, bool sync)
{

    A9GTimerUpdate update = a9_gtimer_get_update(s);
    int i;
    int64_t next_cdiff = 0;

    for (i = 0; i < s->num_cpu; ++i) {
        A9GTimerPerCPU *gtb = &s->per_cpu[i];
        int64_t cdiff = 0;

        if ((s->control & R_CONTROL_TIMER_ENABLE) &&
                (gtb->control & R_CONTROL_COMP_ENABLE)) {
            /* R2p0+, where the compare function is >= */
            if (gtb->compare < update.new) {
                DB_PRINT("Compare event happened for CPU %d\n", i);
                gtb->status = 1;
                if (gtb->control & R_CONTROL_AUTO_INCREMENT && gtb->inc) {
                    uint64_t inc =
                        QEMU_ALIGN_UP(update.new - gtb->compare, gtb->inc);
                    DB_PRINT("Auto incrementing timer compare by %"
                                                        PRId64 "\n", inc);
                    gtb->compare += inc;
                }
            }
            cdiff = (int64_t)gtb->compare - (int64_t)update.new + 1;
            if (cdiff > 0 && (cdiff < next_cdiff || !next_cdiff)) {
                next_cdiff = cdiff;
            }
        }

        qemu_set_irq(gtb->irq,
                     gtb->status && (gtb->control & R_CONTROL_IRQ_ENABLE));
    }

    timer_del(s->timer);
    if (next_cdiff) {
        DB_PRINT("scheduling qemu_timer to fire again in %"
                 PRIx64 " cycles\n", next_cdiff);
        timer_mod(s->timer, update.now + next_cdiff * a9_gtimer_get_conv(s));
    }

    if (s->control & R_CONTROL_TIMER_ENABLE) {
        s->counter = update.new;
    }

    if (sync) {
        s->cpu_ref_time = update.now;
        s->ref_counter = s->counter;
    }
}

static void a9_gtimer_update_no_sync(void *opaque)
{
    A9GTimerState *s = A9_GTIMER(opaque);

    a9_gtimer_update(s, false);
}

static uint64_t a9_gtimer_read(void *opaque, hwaddr addr, unsigned size)
{
    A9GTimerPerCPU *gtb = (A9GTimerPerCPU *)opaque;
    A9GTimerState *s = gtb->parent;
    A9GTimerUpdate update;
    uint64_t ret = 0;
    int shift = 0;

    switch (addr) {
    case R_COUNTER_HI:
        shift = 32;
        /* fallthrough */
    case R_COUNTER_LO:
        update = a9_gtimer_get_update(s);
        ret = extract64(update.new, shift, 32);
        break;
    case R_CONTROL:
        ret = s->control | gtb->control;
        break;
    case R_INTERRUPT_STATUS:
        ret = gtb->status;
        break;
    case R_COMPARATOR_HI:
        shift = 32;
        /* fallthrough */
    case R_COMPARATOR_LO:
        ret = extract64(gtb->compare, shift, 32);
        break;
    case R_AUTO_INCREMENT:
        ret =  gtb->inc;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bad a9gtimer register: %x\n",
                      (unsigned)addr);
        return 0;
    }

    DB_PRINT("addr:%#x data:%#08" PRIx64 "\n", (unsigned)addr, ret);
    return ret;
}

static void a9_gtimer_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    A9GTimerPerCPU *gtb = (A9GTimerPerCPU *)opaque;
    A9GTimerState *s = gtb->parent;
    int shift = 0;

    DB_PRINT("addr:%#x data:%#08" PRIx64 "\n", (unsigned)addr, value);

    switch (addr) {
    case R_COUNTER_HI:
        shift = 32;
        /* fallthrough */
    case R_COUNTER_LO:
        /*
         * Keep it simple - ARM docco explicitly says to disable timer before
         * modding it, so don't bother trying to do all the difficult on the fly
         * timer modifications - (if they even work in real hardware??).
         */
        if (s->control & R_CONTROL_TIMER_ENABLE) {
            qemu_log_mask(LOG_GUEST_ERROR, "Cannot mod running ARM gtimer\n");
            return;
        }
        s->counter = deposit64(s->counter, shift, 32, value);
        return;
    case R_CONTROL:
        a9_gtimer_update(s, (value ^ s->control) & R_CONTROL_NEEDS_SYNC);
        gtb->control = value & R_CONTROL_BANKED;
        s->control = value & ~R_CONTROL_BANKED;
        break;
    case R_INTERRUPT_STATUS:
        a9_gtimer_update(s, false);
        gtb->status &= ~value;
        break;
    case R_COMPARATOR_HI:
        shift = 32;
        /* fallthrough */
    case R_COMPARATOR_LO:
        a9_gtimer_update(s, false);
        gtb->compare = deposit64(gtb->compare, shift, 32, value);
        break;
    case R_AUTO_INCREMENT:
        gtb->inc = value;
        return;
    default:
        return;
    }

    a9_gtimer_update(s, false);
}

/* Wrapper functions to implement the "read global timer for
 * the current CPU" memory regions.
 */
static uint64_t a9_gtimer_this_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    A9GTimerState *s = A9_GTIMER(opaque);
    int id = a9_gtimer_get_current_cpu(s);

    /* no \n so concatenates with message from read fn */
    DB_PRINT("CPU:%d:", id);

    return a9_gtimer_read(&s->per_cpu[id], addr, size);
}

static void a9_gtimer_this_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    A9GTimerState *s = A9_GTIMER(opaque);
    int id = a9_gtimer_get_current_cpu(s);

    /* no \n so concatenates with message from write fn */
    DB_PRINT("CPU:%d:", id);

    a9_gtimer_write(&s->per_cpu[id], addr, value, size);
}

static const MemoryRegionOps a9_gtimer_this_ops = {
    .read = a9_gtimer_this_read,
    .write = a9_gtimer_this_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps a9_gtimer_ops = {
    .read = a9_gtimer_read,
    .write = a9_gtimer_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void a9_gtimer_reset(DeviceState *dev)
{
    A9GTimerState *s = A9_GTIMER(dev);
    int i;

    s->counter = 0;
    s->control = 0;

    for (i = 0; i < s->num_cpu; i++) {
        A9GTimerPerCPU *gtb = &s->per_cpu[i];

        gtb->control = 0;
        gtb->status = 0;
        gtb->compare = 0;
        gtb->inc = 0;
    }
    a9_gtimer_update(s, false);
}

static void a9_gtimer_realize(DeviceState *dev, Error **errp)
{
    A9GTimerState *s = A9_GTIMER(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    if (s->num_cpu < 1 || s->num_cpu > A9_GTIMER_MAX_CPUS) {
        error_setg(errp, "%s: num-cpu must be between 1 and %d",
                   __func__, A9_GTIMER_MAX_CPUS);
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(dev), &a9_gtimer_this_ops, s,
                          "a9gtimer shared", 0x20);
    sysbus_init_mmio(sbd, &s->iomem);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, a9_gtimer_update_no_sync, s);

    for (i = 0; i < s->num_cpu; i++) {
        A9GTimerPerCPU *gtb = &s->per_cpu[i];

        gtb->parent = s;
        sysbus_init_irq(sbd, &gtb->irq);
        memory_region_init_io(&gtb->iomem, OBJECT(dev), &a9_gtimer_ops, gtb,
                              "a9gtimer per cpu", 0x20);
        sysbus_init_mmio(sbd, &gtb->iomem);
    }
}

static const VMStateDescription vmstate_a9_gtimer_per_cpu = {
    .name = "arm.cortex-a9-global-timer.percpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(control, A9GTimerPerCPU),
        VMSTATE_UINT64(compare, A9GTimerPerCPU),
        VMSTATE_UINT32(status, A9GTimerPerCPU),
        VMSTATE_UINT32(inc, A9GTimerPerCPU),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_a9_gtimer = {
    .name = "arm.cortex-a9-global-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, A9GTimerState),
        VMSTATE_UINT64(counter, A9GTimerState),
        VMSTATE_UINT64(ref_counter, A9GTimerState),
        VMSTATE_UINT64(cpu_ref_time, A9GTimerState),
        VMSTATE_STRUCT_VARRAY_UINT32(per_cpu, A9GTimerState, num_cpu,
                                     1, vmstate_a9_gtimer_per_cpu,
                                     A9GTimerPerCPU),
        VMSTATE_END_OF_LIST()
    }
};

static Property a9_gtimer_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", A9GTimerState, num_cpu, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void a9_gtimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = a9_gtimer_realize;
    dc->vmsd = &vmstate_a9_gtimer;
    dc->reset = a9_gtimer_reset;
    dc->props = a9_gtimer_properties;
}

static const TypeInfo a9_gtimer_info = {
    .name          = TYPE_A9_GTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(A9GTimerState),
    .class_init    = a9_gtimer_class_init,
};

static void a9_gtimer_register_types(void)
{
    type_register_static(&a9_gtimer_info);
}

type_init(a9_gtimer_register_types)
