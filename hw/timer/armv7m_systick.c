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
#include "hw/qdev-clock.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"

#define SYSTICK_ENABLE    (1 << 0)
#define SYSTICK_TICKINT   (1 << 1)
#define SYSTICK_CLKSOURCE (1 << 2)
#define SYSTICK_COUNTFLAG (1 << 16)

#define SYSCALIB_NOREF (1U << 31)
#define SYSCALIB_SKEW (1U << 30)
#define SYSCALIB_TENMS ((1U << 24) - 1)

static void systick_set_period_from_clock(SysTickState *s)
{
    /*
     * Set the ptimer period from whichever clock is selected.
     * Must be called from within a ptimer transaction block.
     */
    if (s->control & SYSTICK_CLKSOURCE) {
        ptimer_set_period_from_clock(s->ptimer, s->cpuclk, 1);
    } else {
        ptimer_set_period_from_clock(s->ptimer, s->refclk, 1);
    }
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
    if (ptimer_get_limit(s->ptimer) == 0) {
        /*
         * Timer expiry with SYST_RVR zero disables the timer
         * (but doesn't clear SYST_CSR.ENABLE)
         */
        ptimer_stop(s->ptimer);
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
        val = ptimer_get_limit(s->ptimer);
        break;
    case 0x8: /* SysTick Current Value.  */
        val = ptimer_get_count(s->ptimer);
        break;
    case 0xc: /* SysTick Calibration Value.  */
        /*
         * In real hardware it is possible to make this register report
         * a different value from what the reference clock is actually
         * running at. We don't model that (which usually happens due
         * to integration errors in the real hardware) and instead always
         * report the theoretical correct value as described in the
         * knowledgebase article at
         * https://developer.arm.com/documentation/ka001325/latest
         * If necessary, we could implement an extra QOM property on this
         * device to force the STCALIB value to something different from
         * the "correct" value.
         */
        if (!clock_has_source(s->refclk)) {
            val = SYSCALIB_NOREF;
            break;
        }
        val = clock_ns_to_ticks(s->refclk, 10 * SCALE_MS) - 1;
        val &= SYSCALIB_TENMS;
        if (clock_ticks_to_ns(s->refclk, val + 1) != 10 * SCALE_MS) {
            /* report that tick count does not yield exactly 10ms */
            val |= SYSCALIB_SKEW;
        }
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
        uint32_t oldval;

        if (!clock_has_source(s->refclk)) {
            /* This bit is always 1 if there is no external refclk */
            value |= SYSTICK_CLKSOURCE;
        }

        ptimer_transaction_begin(s->ptimer);
        oldval = s->control;
        s->control &= 0xfffffff8;
        s->control |= value & 7;

        if ((oldval ^ value) & SYSTICK_CLKSOURCE) {
            systick_set_period_from_clock(s);
        }

        if ((oldval ^ value) & SYSTICK_ENABLE) {
            if (value & SYSTICK_ENABLE) {
                ptimer_run(s->ptimer, 0);
            } else {
                ptimer_stop(s->ptimer);
            }
        }
        ptimer_transaction_commit(s->ptimer);
        break;
    }
    case 0x4: /* SysTick Reload Value.  */
        ptimer_transaction_begin(s->ptimer);
        ptimer_set_limit(s->ptimer, value & 0xffffff, 0);
        ptimer_transaction_commit(s->ptimer);
        break;
    case 0x8: /* SysTick Current Value. */
        /*
         * Writing any value clears SYST_CVR to zero and clears
         * SYST_CSR.COUNTFLAG. The counter will then reload from SYST_RVR
         * on the next clock edge unless SYST_RVR is zero.
         */
        ptimer_transaction_begin(s->ptimer);
        if (ptimer_get_limit(s->ptimer) == 0) {
            ptimer_stop(s->ptimer);
        }
        ptimer_set_count(s->ptimer, 0);
        s->control &= ~SYSTICK_COUNTFLAG;
        ptimer_transaction_commit(s->ptimer);
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

    ptimer_transaction_begin(s->ptimer);
    s->control = 0;
    if (!clock_has_source(s->refclk)) {
        /* This bit is always 1 if there is no external refclk */
        s->control |= SYSTICK_CLKSOURCE;
    }
    ptimer_stop(s->ptimer);
    ptimer_set_count(s->ptimer, 0);
    ptimer_set_limit(s->ptimer, 0, 0);
    systick_set_period_from_clock(s);
    ptimer_transaction_commit(s->ptimer);
}

static void systick_cpuclk_update(void *opaque, ClockEvent event)
{
    SysTickState *s = SYSTICK(opaque);

    if (!(s->control & SYSTICK_CLKSOURCE)) {
        /* currently using refclk, we can ignore cpuclk changes */
    }

    ptimer_transaction_begin(s->ptimer);
    ptimer_set_period_from_clock(s->ptimer, s->cpuclk, 1);
    ptimer_transaction_commit(s->ptimer);
}

static void systick_refclk_update(void *opaque, ClockEvent event)
{
    SysTickState *s = SYSTICK(opaque);

    if (s->control & SYSTICK_CLKSOURCE) {
        /* currently using cpuclk, we can ignore refclk changes */
    }

    ptimer_transaction_begin(s->ptimer);
    ptimer_set_period_from_clock(s->ptimer, s->refclk, 1);
    ptimer_transaction_commit(s->ptimer);
}

static void systick_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SysTickState *s = SYSTICK(obj);

    memory_region_init_io(&s->iomem, obj, &systick_ops, s, "systick", 0xe0);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->refclk = qdev_init_clock_in(DEVICE(obj), "refclk",
                                   systick_refclk_update, s, ClockUpdate);
    s->cpuclk = qdev_init_clock_in(DEVICE(obj), "cpuclk",
                                   systick_cpuclk_update, s, ClockUpdate);
}

static void systick_realize(DeviceState *dev, Error **errp)
{
    SysTickState *s = SYSTICK(dev);
    s->ptimer = ptimer_init(systick_timer_tick, s,
                            PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
                            PTIMER_POLICY_NO_COUNTER_ROUND_DOWN |
                            PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                            PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT);

    if (!clock_has_source(s->cpuclk)) {
        error_setg(errp, "systick: cpuclk must be connected");
        return;
    }
    /* It's OK not to connect the refclk */
}

static const VMStateDescription vmstate_systick = {
    .name = "armv7m_systick",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_CLOCK(refclk, SysTickState),
        VMSTATE_CLOCK(cpuclk, SysTickState),
        VMSTATE_UINT32(control, SysTickState),
        VMSTATE_INT64(tick, SysTickState),
        VMSTATE_PTIMER(ptimer, SysTickState),
        VMSTATE_END_OF_LIST()
    }
};

static void systick_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_systick;
    dc->reset = systick_reset;
    dc->realize = systick_realize;
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
