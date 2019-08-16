/*
 * ARMv7M SysTick timer
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell
 *
 * This code is licensed under the GPL (version 2 or later).
 */

#include "qemu/osdep.h"
#include "hw/timer/armv7m_systick.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

/* qemu timers run at 1GHz.   We want something closer to 1MHz.  */
#define SYSTICK_SCALE 1000ULL

#define SYSTICK_ENABLE    (1 << 0)
#define SYSTICK_TICKINT   (1 << 1)
#define SYSTICK_CLKSOURCE (1 << 2)
#define SYSTICK_COUNTFLAG (1 << 16)

int system_clock_scale;

/* Conversion factor from qemu timer to SysTick frequencies.  */
static inline int64_t systick_scale(SysTickState *s)
{
    if (s->control & SYSTICK_CLKSOURCE) {
        return system_clock_scale;
    } else {
        return 1000;
    }
}

static void systick_reload(SysTickState *s, int reset)
{
    /* The Cortex-M3 Devices Generic User Guide says that "When the
     * ENABLE bit is set to 1, the counter loads the RELOAD value from the
     * SYST RVR register and then counts down". So, we need to check the
     * ENABLE bit before reloading the value.
     */
    trace_systick_reload();

    if ((s->control & SYSTICK_ENABLE) == 0) {
        return;
    }

    if (reset) {
        s->tick = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
    s->tick += (s->reload + 1) * systick_scale(s);
    timer_mod(s->timer, s->tick);
}

static void systick_timer_tick(void *opaque)
{
    SysTickState *s = (SysTickState *)opaque;

    trace_systick_timer_tick();

    s->control |= SYSTICK_COUNTFLAG;
    if (s->control & SYSTICK_TICKINT) {
        /* Tell the NVIC to pend the SysTick exception */
        qemu_irq_pulse(s->irq);
    }
    if (s->reload == 0) {
        s->control &= ~SYSTICK_ENABLE;
    } else {
        systick_reload(s, 0);
    }
}

static MemTxResult systick_read(void *opaque, hwaddr addr, uint64_t *data,
                                unsigned size, MemTxAttrs attrs)
{
    SysTickState *s = opaque;
    uint32_t val;

    if (attrs.user) {
        /* Generate BusFault for unprivileged accesses */
        return MEMTX_ERROR;
    }

    switch (addr) {
    case 0x0: /* SysTick Control and Status.  */
        val = s->control;
        s->control &= ~SYSTICK_COUNTFLAG;
        break;
    case 0x4: /* SysTick Reload Value.  */
        val = s->reload;
        break;
    case 0x8: /* SysTick Current Value.  */
    {
        int64_t t;

        if ((s->control & SYSTICK_ENABLE) == 0) {
            val = 0;
            break;
        }
        t = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        if (t >= s->tick) {
            val = 0;
            break;
        }
        val = ((s->tick - (t + 1)) / systick_scale(s)) + 1;
        /* The interrupt in triggered when the timer reaches zero.
           However the counter is not reloaded until the next clock
           tick.  This is a hack to return zero during the first tick.  */
        if (val > s->reload) {
            val = 0;
        }
        break;
    }
    case 0xc: /* SysTick Calibration Value.  */
        val = 10000;
        break;
    default:
        val = 0;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SysTick: Bad read offset 0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    trace_systick_read(addr, val, size);
    *data = val;
    return MEMTX_OK;
}

static MemTxResult systick_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    SysTickState *s = opaque;

    if (attrs.user) {
        /* Generate BusFault for unprivileged accesses */
        return MEMTX_ERROR;
    }

    trace_systick_write(addr, value, size);

    switch (addr) {
    case 0x0: /* SysTick Control and Status.  */
    {
        uint32_t oldval = s->control;

        s->control &= 0xfffffff8;
        s->control |= value & 7;
        if ((oldval ^ value) & SYSTICK_ENABLE) {
            int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            if (value & SYSTICK_ENABLE) {
                if (s->tick) {
                    s->tick += now;
                    timer_mod(s->timer, s->tick);
                } else {
                    systick_reload(s, 1);
                }
            } else {
                timer_del(s->timer);
                s->tick -= now;
                if (s->tick < 0) {
                    s->tick = 0;
                }
            }
        } else if ((oldval ^ value) & SYSTICK_CLKSOURCE) {
            /* This is a hack. Force the timer to be reloaded
               when the reference clock is changed.  */
            systick_reload(s, 1);
        }
        break;
    }
    case 0x4: /* SysTick Reload Value.  */
        s->reload = value;
        break;
    case 0x8: /* SysTick Current Value.  Writes reload the timer.  */
        systick_reload(s, 1);
        s->control &= ~SYSTICK_COUNTFLAG;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SysTick: Bad write offset 0x%" HWADDR_PRIx "\n", addr);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps systick_ops = {
    .read_with_attrs = systick_read,
    .write_with_attrs = systick_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void systick_reset(DeviceState *dev)
{
    SysTickState *s = SYSTICK(dev);

    s->control = 0;
    s->reload = 0;
    s->tick = 0;
    timer_del(s->timer);
}

static void systick_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SysTickState *s = SYSTICK(obj);

    memory_region_init_io(&s->iomem, obj, &systick_ops, s, "systick", 0xe0);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, systick_timer_tick, s);
}

static const VMStateDescription vmstate_systick = {
    .name = "armv7m_systick",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(control, SysTickState),
        VMSTATE_UINT32(reload, SysTickState),
        VMSTATE_INT64(tick, SysTickState),
        VMSTATE_TIMER_PTR(timer, SysTickState),
        VMSTATE_END_OF_LIST()
    }
};

static void systick_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_systick;
    dc->reset = systick_reset;
}

static const TypeInfo armv7m_systick_info = {
    .name = TYPE_SYSTICK,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = systick_instance_init,
    .instance_size = sizeof(SysTickState),
    .class_init = systick_class_init,
};

static void armv7m_systick_register_types(void)
{
    type_register_static(&armv7m_systick_info);
}

type_init(armv7m_systick_register_types)
