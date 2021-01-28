/*
 * ARM CMSDK APB dual-timer emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "APB dual-input timer" which is part of the Cortex-M
 * System Design Kit (CMSDK) and documented in the Cortex-M System
 * Design Kit Technical Reference Manual (ARM DDI0479C):
 * https://developer.arm.com/products/system-design/system-design-kits/cortex-m-system-design-kit
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/qdev-clock.h"
#include "hw/timer/cmsdk-apb-dualtimer.h"
#include "migration/vmstate.h"

REG32(TIMER1LOAD, 0x0)
REG32(TIMER1VALUE, 0x4)
REG32(TIMER1CONTROL, 0x8)
    FIELD(CONTROL, ONESHOT, 0, 1)
    FIELD(CONTROL, SIZE, 1, 1)
    FIELD(CONTROL, PRESCALE, 2, 2)
    FIELD(CONTROL, INTEN, 5, 1)
    FIELD(CONTROL, MODE, 6, 1)
    FIELD(CONTROL, ENABLE, 7, 1)
#define R_CONTROL_VALID_MASK (R_CONTROL_ONESHOT_MASK | R_CONTROL_SIZE_MASK | \
                              R_CONTROL_PRESCALE_MASK | R_CONTROL_INTEN_MASK | \
                              R_CONTROL_MODE_MASK | R_CONTROL_ENABLE_MASK)
REG32(TIMER1INTCLR, 0xc)
REG32(TIMER1RIS, 0x10)
REG32(TIMER1MIS, 0x14)
REG32(TIMER1BGLOAD, 0x18)
REG32(TIMER2LOAD, 0x20)
REG32(TIMER2VALUE, 0x24)
REG32(TIMER2CONTROL, 0x28)
REG32(TIMER2INTCLR, 0x2c)
REG32(TIMER2RIS, 0x30)
REG32(TIMER2MIS, 0x34)
REG32(TIMER2BGLOAD, 0x38)
REG32(TIMERITCR, 0xf00)
    FIELD(TIMERITCR, ENABLE, 0, 1)
#define R_TIMERITCR_VALID_MASK R_TIMERITCR_ENABLE_MASK
REG32(TIMERITOP, 0xf04)
    FIELD(TIMERITOP, TIMINT1, 0, 1)
    FIELD(TIMERITOP, TIMINT2, 1, 1)
#define R_TIMERITOP_VALID_MASK (R_TIMERITOP_TIMINT1_MASK | \
                                R_TIMERITOP_TIMINT2_MASK)
REG32(PID4, 0xfd0)
REG32(PID5, 0xfd4)
REG32(PID6, 0xfd8)
REG32(PID7, 0xfdc)
REG32(PID0, 0xfe0)
REG32(PID1, 0xfe4)
REG32(PID2, 0xfe8)
REG32(PID3, 0xfec)
REG32(CID0, 0xff0)
REG32(CID1, 0xff4)
REG32(CID2, 0xff8)
REG32(CID3, 0xffc)

/* PID/CID values */
static const int timer_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x23, 0xb8, 0x1b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static bool cmsdk_dualtimermod_intstatus(CMSDKAPBDualTimerModule *m)
{
    /* Return masked interrupt status for the timer module */
    return m->intstatus && (m->control & R_CONTROL_INTEN_MASK);
}

static void cmsdk_apb_dualtimer_update(CMSDKAPBDualTimer *s)
{
    bool timint1, timint2, timintc;

    if (s->timeritcr) {
        /* Integration test mode: outputs driven directly from TIMERITOP bits */
        timint1 = s->timeritop & R_TIMERITOP_TIMINT1_MASK;
        timint2 = s->timeritop & R_TIMERITOP_TIMINT2_MASK;
    } else {
        timint1 = cmsdk_dualtimermod_intstatus(&s->timermod[0]);
        timint2 = cmsdk_dualtimermod_intstatus(&s->timermod[1]);
    }

    timintc = timint1 || timint2;

    qemu_set_irq(s->timermod[0].timerint, timint1);
    qemu_set_irq(s->timermod[1].timerint, timint2);
    qemu_set_irq(s->timerintc, timintc);
}

static int cmsdk_dualtimermod_divisor(CMSDKAPBDualTimerModule *m)
{
    /* Return the divisor set by the current CONTROL.PRESCALE value */
    switch (FIELD_EX32(m->control, CONTROL, PRESCALE)) {
    case 0:
        return 1;
    case 1:
        return 16;
    case 2:
    case 3: /* UNDEFINED, we treat like 2 (and complained when it was set) */
        return 256;
    default:
        g_assert_not_reached();
    }
}

static void cmsdk_dualtimermod_write_control(CMSDKAPBDualTimerModule *m,
                                             uint32_t newctrl)
{
    /* Handle a write to the CONTROL register */
    uint32_t changed;

    ptimer_transaction_begin(m->timer);

    newctrl &= R_CONTROL_VALID_MASK;

    changed = m->control ^ newctrl;

    if (changed & ~newctrl & R_CONTROL_ENABLE_MASK) {
        /* ENABLE cleared, stop timer before any further changes */
        ptimer_stop(m->timer);
    }

    if (changed & R_CONTROL_PRESCALE_MASK) {
        int divisor;

        switch (FIELD_EX32(newctrl, CONTROL, PRESCALE)) {
        case 0:
            divisor = 1;
            break;
        case 1:
            divisor = 16;
            break;
        case 2:
            divisor = 256;
            break;
        case 3:
            /* UNDEFINED; complain, and arbitrarily treat like 2 */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "CMSDK APB dual-timer: CONTROL.PRESCALE==0b11"
                          " is undefined behaviour\n");
            divisor = 256;
            break;
        default:
            g_assert_not_reached();
        }
        ptimer_set_period_from_clock(m->timer, m->parent->timclk, divisor);
    }

    if (changed & R_CONTROL_MODE_MASK) {
        uint32_t load;
        if (newctrl & R_CONTROL_MODE_MASK) {
            /* Periodic: the limit is the LOAD register value */
            load = m->load;
        } else {
            /* Free-running: counter wraps around */
            load = ptimer_get_limit(m->timer);
            if (!(m->control & R_CONTROL_SIZE_MASK)) {
                load = deposit32(m->load, 0, 16, load);
            }
            m->load = load;
            load = 0xffffffff;
        }
        if (!(m->control & R_CONTROL_SIZE_MASK)) {
            load &= 0xffff;
        }
        ptimer_set_limit(m->timer, load, 0);
    }

    if (changed & R_CONTROL_SIZE_MASK) {
        /* Timer switched between 16 and 32 bit count */
        uint32_t value, load;

        value = ptimer_get_count(m->timer);
        load = ptimer_get_limit(m->timer);
        if (newctrl & R_CONTROL_SIZE_MASK) {
            /* 16 -> 32, top half of VALUE is in struct field */
            value = deposit32(m->value, 0, 16, value);
        } else {
            /* 32 -> 16: save top half to struct field and truncate */
            m->value = value;
            value &= 0xffff;
        }

        if (newctrl & R_CONTROL_MODE_MASK) {
            /* Periodic, timer limit has LOAD value */
            if (newctrl & R_CONTROL_SIZE_MASK) {
                load = deposit32(m->load, 0, 16, load);
            } else {
                m->load = load;
                load &= 0xffff;
            }
        } else {
            /* Free-running, timer limit is set to give wraparound */
            if (newctrl & R_CONTROL_SIZE_MASK) {
                load = 0xffffffff;
            } else {
                load = 0xffff;
            }
        }
        ptimer_set_count(m->timer, value);
        ptimer_set_limit(m->timer, load, 0);
    }

    if (newctrl & R_CONTROL_ENABLE_MASK) {
        /*
         * ENABLE is set; start the timer after all other changes.
         * We start it even if the ENABLE bit didn't actually change,
         * in case the timer was an expired one-shot timer that has
         * now been changed into a free-running or periodic timer.
         */
        ptimer_run(m->timer, !!(newctrl & R_CONTROL_ONESHOT_MASK));
    }

    m->control = newctrl;

    ptimer_transaction_commit(m->timer);
}

static uint64_t cmsdk_apb_dualtimer_read(void *opaque, hwaddr offset,
                                          unsigned size)
{
    CMSDKAPBDualTimer *s = CMSDK_APB_DUALTIMER(opaque);
    uint64_t r;

    if (offset >= A_TIMERITCR) {
        switch (offset) {
        case A_TIMERITCR:
            r = s->timeritcr;
            break;
        case A_PID4 ... A_CID3:
            r = timer_id[(offset - A_PID4) / 4];
            break;
        default:
        bad_offset:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "CMSDK APB dual-timer read: bad offset %x\n",
                          (int) offset);
            r = 0;
            break;
        }
    } else {
        int timer = offset >> 5;
        CMSDKAPBDualTimerModule *m;

        if (timer >= ARRAY_SIZE(s->timermod)) {
            goto bad_offset;
        }

        m = &s->timermod[timer];

        switch (offset & 0x1F) {
        case A_TIMER1LOAD:
        case A_TIMER1BGLOAD:
            if (m->control & R_CONTROL_MODE_MASK) {
                /*
                 * Periodic: the ptimer limit is the LOAD register value, (or
                 * just the low 16 bits of it if the timer is in 16-bit mode)
                 */
                r = ptimer_get_limit(m->timer);
                if (!(m->control & R_CONTROL_SIZE_MASK)) {
                    r = deposit32(m->load, 0, 16, r);
                }
            } else {
                /* Free-running: LOAD register value is just in m->load */
                r = m->load;
            }
            break;
        case A_TIMER1VALUE:
            r = ptimer_get_count(m->timer);
            if (!(m->control & R_CONTROL_SIZE_MASK)) {
                r = deposit32(m->value, 0, 16, r);
            }
            break;
        case A_TIMER1CONTROL:
            r = m->control;
            break;
        case A_TIMER1RIS:
            r = m->intstatus;
            break;
        case A_TIMER1MIS:
            r = cmsdk_dualtimermod_intstatus(m);
            break;
        default:
            goto bad_offset;
        }
    }

    trace_cmsdk_apb_dualtimer_read(offset, r, size);
    return r;
}

static void cmsdk_apb_dualtimer_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size)
{
    CMSDKAPBDualTimer *s = CMSDK_APB_DUALTIMER(opaque);

    trace_cmsdk_apb_dualtimer_write(offset, value, size);

    if (offset >= A_TIMERITCR) {
        switch (offset) {
        case A_TIMERITCR:
            s->timeritcr = value & R_TIMERITCR_VALID_MASK;
            cmsdk_apb_dualtimer_update(s);
            break;
        case A_TIMERITOP:
            s->timeritop = value & R_TIMERITOP_VALID_MASK;
            cmsdk_apb_dualtimer_update(s);
            break;
        default:
        bad_offset:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "CMSDK APB dual-timer write: bad offset %x\n",
                          (int) offset);
            break;
        }
    } else {
        int timer = offset >> 5;
        CMSDKAPBDualTimerModule *m;

        if (timer >= ARRAY_SIZE(s->timermod)) {
            goto bad_offset;
        }

        m = &s->timermod[timer];

        switch (offset & 0x1F) {
        case A_TIMER1LOAD:
            /* Set the limit, and immediately reload the count from it */
            m->load = value;
            m->value = value;
            if (!(m->control & R_CONTROL_SIZE_MASK)) {
                value &= 0xffff;
            }
            ptimer_transaction_begin(m->timer);
            if (!(m->control & R_CONTROL_MODE_MASK)) {
                /*
                 * In free-running mode this won't set the limit but will
                 * still change the current count value.
                 */
                ptimer_set_count(m->timer, value);
            } else {
                if (!value) {
                    ptimer_stop(m->timer);
                }
                ptimer_set_limit(m->timer, value, 1);
                if (value && (m->control & R_CONTROL_ENABLE_MASK)) {
                    /* Force possibly-expired oneshot timer to restart */
                    ptimer_run(m->timer, 1);
                }
            }
            ptimer_transaction_commit(m->timer);
            break;
        case A_TIMER1BGLOAD:
            /* Set the limit, but not the current count */
            m->load = value;
            if (!(m->control & R_CONTROL_MODE_MASK)) {
                /* In free-running mode there is no limit */
                break;
            }
            if (!(m->control & R_CONTROL_SIZE_MASK)) {
                value &= 0xffff;
            }
            ptimer_transaction_begin(m->timer);
            ptimer_set_limit(m->timer, value, 0);
            ptimer_transaction_commit(m->timer);
            break;
        case A_TIMER1CONTROL:
            cmsdk_dualtimermod_write_control(m, value);
            cmsdk_apb_dualtimer_update(s);
            break;
        case A_TIMER1INTCLR:
            m->intstatus = 0;
            cmsdk_apb_dualtimer_update(s);
            break;
        default:
            goto bad_offset;
        }
    }
}

static const MemoryRegionOps cmsdk_apb_dualtimer_ops = {
    .read = cmsdk_apb_dualtimer_read,
    .write = cmsdk_apb_dualtimer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* byte/halfword accesses are just zero-padded on reads and writes */
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void cmsdk_dualtimermod_tick(void *opaque)
{
    CMSDKAPBDualTimerModule *m = opaque;

    m->intstatus = 1;
    cmsdk_apb_dualtimer_update(m->parent);
}

static void cmsdk_dualtimermod_reset(CMSDKAPBDualTimerModule *m)
{
    m->control = R_CONTROL_INTEN_MASK;
    m->intstatus = 0;
    m->load = 0;
    m->value = 0xffffffff;
    ptimer_transaction_begin(m->timer);
    ptimer_stop(m->timer);
    /*
     * We start in free-running mode, with VALUE at 0xffffffff, and
     * in 16-bit counter mode. This means that the ptimer count and
     * limit must both be set to 0xffff, so we wrap at 16 bits.
     */
    ptimer_set_limit(m->timer, 0xffff, 1);
    ptimer_set_period_from_clock(m->timer, m->parent->timclk,
                                 cmsdk_dualtimermod_divisor(m));
    ptimer_transaction_commit(m->timer);
}

static void cmsdk_apb_dualtimer_reset(DeviceState *dev)
{
    CMSDKAPBDualTimer *s = CMSDK_APB_DUALTIMER(dev);
    int i;

    trace_cmsdk_apb_dualtimer_reset();

    for (i = 0; i < ARRAY_SIZE(s->timermod); i++) {
        cmsdk_dualtimermod_reset(&s->timermod[i]);
    }
    s->timeritcr = 0;
    s->timeritop = 0;
}

static void cmsdk_apb_dualtimer_clk_update(void *opaque)
{
    CMSDKAPBDualTimer *s = CMSDK_APB_DUALTIMER(opaque);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->timermod); i++) {
        CMSDKAPBDualTimerModule *m = &s->timermod[i];
        ptimer_transaction_begin(m->timer);
        ptimer_set_period_from_clock(m->timer, m->parent->timclk,
                                     cmsdk_dualtimermod_divisor(m));
        ptimer_transaction_commit(m->timer);
    }
}

static void cmsdk_apb_dualtimer_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    CMSDKAPBDualTimer *s = CMSDK_APB_DUALTIMER(obj);
    int i;

    memory_region_init_io(&s->iomem, obj, &cmsdk_apb_dualtimer_ops,
                          s, "cmsdk-apb-dualtimer", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->timerintc);

    for (i = 0; i < ARRAY_SIZE(s->timermod); i++) {
        sysbus_init_irq(sbd, &s->timermod[i].timerint);
    }
    s->timclk = qdev_init_clock_in(DEVICE(s), "TIMCLK",
                                   cmsdk_apb_dualtimer_clk_update, s);
}

static void cmsdk_apb_dualtimer_realize(DeviceState *dev, Error **errp)
{
    CMSDKAPBDualTimer *s = CMSDK_APB_DUALTIMER(dev);
    int i;

    if (!clock_has_source(s->timclk)) {
        error_setg(errp, "CMSDK APB dualtimer: TIMCLK clock must be connected");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(s->timermod); i++) {
        CMSDKAPBDualTimerModule *m = &s->timermod[i];

        m->parent = s;
        m->timer = ptimer_init(cmsdk_dualtimermod_tick, m,
                               PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
                               PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                               PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                               PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);
    }
}

static const VMStateDescription cmsdk_dualtimermod_vmstate = {
    .name = "cmsdk-apb-dualtimer-module",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(timer, CMSDKAPBDualTimerModule),
        VMSTATE_UINT32(load, CMSDKAPBDualTimerModule),
        VMSTATE_UINT32(value, CMSDKAPBDualTimerModule),
        VMSTATE_UINT32(control, CMSDKAPBDualTimerModule),
        VMSTATE_UINT32(intstatus, CMSDKAPBDualTimerModule),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription cmsdk_apb_dualtimer_vmstate = {
    .name = "cmsdk-apb-dualtimer",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_CLOCK(timclk, CMSDKAPBDualTimer),
        VMSTATE_STRUCT_ARRAY(timermod, CMSDKAPBDualTimer,
                             CMSDK_APB_DUALTIMER_NUM_MODULES,
                             1, cmsdk_dualtimermod_vmstate,
                             CMSDKAPBDualTimerModule),
        VMSTATE_UINT32(timeritcr, CMSDKAPBDualTimer),
        VMSTATE_UINT32(timeritop, CMSDKAPBDualTimer),
        VMSTATE_END_OF_LIST()
    }
};

static void cmsdk_apb_dualtimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cmsdk_apb_dualtimer_realize;
    dc->vmsd = &cmsdk_apb_dualtimer_vmstate;
    dc->reset = cmsdk_apb_dualtimer_reset;
}

static const TypeInfo cmsdk_apb_dualtimer_info = {
    .name = TYPE_CMSDK_APB_DUALTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CMSDKAPBDualTimer),
    .instance_init = cmsdk_apb_dualtimer_init,
    .class_init = cmsdk_apb_dualtimer_class_init,
};

static void cmsdk_apb_dualtimer_register_types(void)
{
    type_register_static(&cmsdk_apb_dualtimer_info);
}

type_init(cmsdk_apb_dualtimer_register_types);
