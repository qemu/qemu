/*
 * Xilinx Zynq cadence TTC model
 *
 * Copyright (c) 2011 Xilinx Inc.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 * Written By Haibing Ma
 *            M. Habib
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sysbus.h"
#include "qemu-timer.h"

#ifdef CADENCE_TTC_ERR_DEBUG
#define DB_PRINT(...) do { \
    fprintf(stderr,  ": %s: ", __func__); \
    fprintf(stderr, ## __VA_ARGS__); \
    } while (0);
#else
    #define DB_PRINT(...)
#endif

#define COUNTER_INTR_IV     0x00000001
#define COUNTER_INTR_M1     0x00000002
#define COUNTER_INTR_M2     0x00000004
#define COUNTER_INTR_M3     0x00000008
#define COUNTER_INTR_OV     0x00000010
#define COUNTER_INTR_EV     0x00000020

#define COUNTER_CTRL_DIS    0x00000001
#define COUNTER_CTRL_INT    0x00000002
#define COUNTER_CTRL_DEC    0x00000004
#define COUNTER_CTRL_MATCH  0x00000008
#define COUNTER_CTRL_RST    0x00000010

#define CLOCK_CTRL_PS_EN    0x00000001
#define CLOCK_CTRL_PS_V     0x0000001e

typedef struct {
    QEMUTimer *timer;
    int freq;

    uint32_t reg_clock;
    uint32_t reg_count;
    uint32_t reg_value;
    uint16_t reg_interval;
    uint16_t reg_match[3];
    uint32_t reg_intr;
    uint32_t reg_intr_en;
    uint32_t reg_event_ctrl;
    uint32_t reg_event;

    uint64_t cpu_time;
    unsigned int cpu_time_valid;

    qemu_irq irq;
} CadenceTimerState;

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    CadenceTimerState timer[3];
} CadenceTTCState;

static void cadence_timer_update(CadenceTimerState *s)
{
    qemu_set_irq(s->irq, !!(s->reg_intr & s->reg_intr_en));
}

static CadenceTimerState *cadence_timer_from_addr(void *opaque,
                                        target_phys_addr_t offset)
{
    unsigned int index;
    CadenceTTCState *s = (CadenceTTCState *)opaque;

    index = (offset >> 2) % 3;

    return &s->timer[index];
}

static uint64_t cadence_timer_get_ns(CadenceTimerState *s, uint64_t timer_steps)
{
    /* timer_steps has max value of 0x100000000. double check it
     * (or overflow can happen below) */
    assert(timer_steps <= 1ULL << 32);

    uint64_t r = timer_steps * 1000000000ULL;
    if (s->reg_clock & CLOCK_CTRL_PS_EN) {
        r >>= 16 - (((s->reg_clock & CLOCK_CTRL_PS_V) >> 1) + 1);
    } else {
        r >>= 16;
    }
    r /= (uint64_t)s->freq;
    return r;
}

static uint64_t cadence_timer_get_steps(CadenceTimerState *s, uint64_t ns)
{
    uint64_t to_divide = 1000000000ULL;

    uint64_t r = ns;
     /* for very large intervals (> 8s) do some division first to stop
      * overflow (costs some prescision) */
    while (r >= 8ULL << 30 && to_divide > 1) {
        r /= 1000;
        to_divide /= 1000;
    }
    r <<= 16;
    /* keep early-dividing as needed */
    while (r >= 8ULL << 30 && to_divide > 1) {
        r /= 1000;
        to_divide /= 1000;
    }
    r *= (uint64_t)s->freq;
    if (s->reg_clock & CLOCK_CTRL_PS_EN) {
        r /= 1 << (((s->reg_clock & CLOCK_CTRL_PS_V) >> 1) + 1);
    }

    r /= to_divide;
    return r;
}

/* determine if x is in between a and b, exclusive of a, inclusive of b */

static inline int64_t is_between(int64_t x, int64_t a, int64_t b)
{
    if (a < b) {
        return x > a && x <= b;
    }
    return x < a && x >= b;
}

static void cadence_timer_run(CadenceTimerState *s)
{
    int i;
    int64_t event_interval, next_value;

    assert(s->cpu_time_valid); /* cadence_timer_sync must be called first */

    if (s->reg_count & COUNTER_CTRL_DIS) {
        s->cpu_time_valid = 0;
        return;
    }

    { /* figure out what's going to happen next (rollover or match) */
        int64_t interval = (uint64_t)((s->reg_count & COUNTER_CTRL_INT) ?
                (int64_t)s->reg_interval + 1 : 0x10000ULL) << 16;
        next_value = (s->reg_count & COUNTER_CTRL_DEC) ? -1ULL : interval;
        for (i = 0; i < 3; ++i) {
            int64_t cand = (uint64_t)s->reg_match[i] << 16;
            if (is_between(cand, (uint64_t)s->reg_value, next_value)) {
                next_value = cand;
            }
        }
    }
    DB_PRINT("next timer event value: %09llx\n",
            (unsigned long long)next_value);

    event_interval = next_value - (int64_t)s->reg_value;
    event_interval = (event_interval < 0) ? -event_interval : event_interval;

    qemu_mod_timer(s->timer, s->cpu_time +
                cadence_timer_get_ns(s, event_interval));
}

static void cadence_timer_sync(CadenceTimerState *s)
{
    int i;
    int64_t r, x;
    int64_t interval = ((s->reg_count & COUNTER_CTRL_INT) ?
            (int64_t)s->reg_interval + 1 : 0x10000ULL) << 16;
    uint64_t old_time = s->cpu_time;

    s->cpu_time = qemu_get_clock_ns(vm_clock);
    DB_PRINT("cpu time: %lld ns\n", (long long)old_time);

    if (!s->cpu_time_valid || old_time == s->cpu_time) {
        s->cpu_time_valid = 1;
        return;
    }

    r = (int64_t)cadence_timer_get_steps(s, s->cpu_time - old_time);
    x = (int64_t)s->reg_value + ((s->reg_count & COUNTER_CTRL_DEC) ? -r : r);

    for (i = 0; i < 3; ++i) {
        int64_t m = (int64_t)s->reg_match[i] << 16;
        if (m > interval) {
            continue;
        }
        /* check to see if match event has occurred. check m +/- interval
         * to account for match events in wrap around cases */
        if (is_between(m, s->reg_value, x) ||
            is_between(m + interval, s->reg_value, x) ||
            is_between(m - interval, s->reg_value, x)) {
            s->reg_intr |= (2 << i);
        }
    }
    while (x < 0) {
        x += interval;
    }
    s->reg_value = (uint32_t)(x % interval);

    if (s->reg_value != x) {
        s->reg_intr |= (s->reg_count & COUNTER_CTRL_INT) ?
            COUNTER_INTR_IV : COUNTER_INTR_OV;
    }
    cadence_timer_update(s);
}

static void cadence_timer_tick(void *opaque)
{
    CadenceTimerState *s = opaque;

    DB_PRINT("\n");
    cadence_timer_sync(s);
    cadence_timer_run(s);
}

static uint32_t cadence_ttc_read_imp(void *opaque, target_phys_addr_t offset)
{
    CadenceTimerState *s = cadence_timer_from_addr(opaque, offset);
    uint32_t value;

    cadence_timer_sync(s);
    cadence_timer_run(s);

    switch (offset) {
    case 0x00: /* clock control */
    case 0x04:
    case 0x08:
        return s->reg_clock;

    case 0x0c: /* counter control */
    case 0x10:
    case 0x14:
        return s->reg_count;

    case 0x18: /* counter value */
    case 0x1c:
    case 0x20:
        return (uint16_t)(s->reg_value >> 16);

    case 0x24: /* reg_interval counter */
    case 0x28:
    case 0x2c:
        return s->reg_interval;

    case 0x30: /* match 1 counter */
    case 0x34:
    case 0x38:
        return s->reg_match[0];

    case 0x3c: /* match 2 counter */
    case 0x40:
    case 0x44:
        return s->reg_match[1];

    case 0x48: /* match 3 counter */
    case 0x4c:
    case 0x50:
        return s->reg_match[2];

    case 0x54: /* interrupt register */
    case 0x58:
    case 0x5c:
        /* cleared after read */
        value = s->reg_intr;
        s->reg_intr = 0;
        return value;

    case 0x60: /* interrupt enable */
    case 0x64:
    case 0x68:
        return s->reg_intr_en;

    case 0x6c:
    case 0x70:
    case 0x74:
        return s->reg_event_ctrl;

    case 0x78:
    case 0x7c:
    case 0x80:
        return s->reg_event;

    default:
        return 0;
    }
}

static uint64_t cadence_ttc_read(void *opaque, target_phys_addr_t offset,
    unsigned size)
{
    uint32_t ret = cadence_ttc_read_imp(opaque, offset);

    DB_PRINT("addr: %08x data: %08x\n", offset, ret);
    return ret;
}

static void cadence_ttc_write(void *opaque, target_phys_addr_t offset,
        uint64_t value, unsigned size)
{
    CadenceTimerState *s = cadence_timer_from_addr(opaque, offset);

    DB_PRINT("addr: %08x data %08x\n", offset, (unsigned)value);

    cadence_timer_sync(s);

    switch (offset) {
    case 0x00: /* clock control */
    case 0x04:
    case 0x08:
        s->reg_clock = value & 0x3F;
        break;

    case 0x0c: /* counter control */
    case 0x10:
    case 0x14:
        if (value & COUNTER_CTRL_RST) {
            s->reg_value = 0;
        }
        s->reg_count = value & 0x3f & ~COUNTER_CTRL_RST;
        break;

    case 0x24: /* interval register */
    case 0x28:
    case 0x2c:
        s->reg_interval = value & 0xffff;
        break;

    case 0x30: /* match register */
    case 0x34:
    case 0x38:
        s->reg_match[0] = value & 0xffff;

    case 0x3c: /* match register */
    case 0x40:
    case 0x44:
        s->reg_match[1] = value & 0xffff;

    case 0x48: /* match register */
    case 0x4c:
    case 0x50:
        s->reg_match[2] = value & 0xffff;
        break;

    case 0x54: /* interrupt register */
    case 0x58:
    case 0x5c:
        s->reg_intr &= (~value & 0xfff);
        break;

    case 0x60: /* interrupt enable */
    case 0x64:
    case 0x68:
        s->reg_intr_en = value & 0x3f;
        break;

    case 0x6c: /* event control */
    case 0x70:
    case 0x74:
        s->reg_event_ctrl = value & 0x07;
        break;

    default:
        return;
    }

    cadence_timer_run(s);
    cadence_timer_update(s);
}

static const MemoryRegionOps cadence_ttc_ops = {
    .read = cadence_ttc_read,
    .write = cadence_ttc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void cadence_timer_reset(CadenceTimerState *s)
{
   s->reg_count = 0x21;
}

static void cadence_timer_init(uint32_t freq, CadenceTimerState *s)
{
    memset(s, 0, sizeof(CadenceTimerState));
    s->freq = freq;

    cadence_timer_reset(s);

    s->timer = qemu_new_timer_ns(vm_clock, cadence_timer_tick, s);
}

static int cadence_ttc_init(SysBusDevice *dev)
{
    CadenceTTCState *s = FROM_SYSBUS(CadenceTTCState, dev);
    int i;

    for (i = 0; i < 3; ++i) {
        cadence_timer_init(2500000, &s->timer[i]);
        sysbus_init_irq(dev, &s->timer[i].irq);
    }

    memory_region_init_io(&s->iomem, &cadence_ttc_ops, s, "timer", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);

    return 0;
}

static void cadence_timer_pre_save(void *opaque)
{
    cadence_timer_sync((CadenceTimerState *)opaque);
}

static int cadence_timer_post_load(void *opaque, int version_id)
{
    CadenceTimerState *s = opaque;

    s->cpu_time_valid = 0;
    cadence_timer_sync(s);
    cadence_timer_run(s);
    cadence_timer_update(s);
    return 0;
}

static const VMStateDescription vmstate_cadence_timer = {
    .name = "cadence_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .pre_save = cadence_timer_pre_save,
    .post_load = cadence_timer_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(reg_clock, CadenceTimerState),
        VMSTATE_UINT32(reg_count, CadenceTimerState),
        VMSTATE_UINT32(reg_value, CadenceTimerState),
        VMSTATE_UINT16(reg_interval, CadenceTimerState),
        VMSTATE_UINT16_ARRAY(reg_match, CadenceTimerState, 3),
        VMSTATE_UINT32(reg_intr, CadenceTimerState),
        VMSTATE_UINT32(reg_intr_en, CadenceTimerState),
        VMSTATE_UINT32(reg_event_ctrl, CadenceTimerState),
        VMSTATE_UINT32(reg_event, CadenceTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_cadence_ttc = {
    .name = "cadence_TTC",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(timer, CadenceTTCState, 3, 0,
                            vmstate_cadence_timer,
                            CadenceTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void cadence_ttc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = cadence_ttc_init;
    dc->vmsd = &vmstate_cadence_ttc;
}

static TypeInfo cadence_ttc_info = {
    .name  = "cadence_ttc",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(CadenceTTCState),
    .class_init = cadence_ttc_class_init,
};

static void cadence_ttc_register_types(void)
{
    type_register_static(&cadence_ttc_info);
}

type_init(cadence_ttc_register_types)
