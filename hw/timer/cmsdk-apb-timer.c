/*
 * ARM CMSDK APB timer emulation
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/* This is a model of the "APB timer" which is part of the Cortex-M
 * System Design Kit (CMSDK) and documented in the Cortex-M System
 * Design Kit Technical Reference Manual (ARM DDI0479C):
 * https://developer.arm.com/products/system-design/system-design-kits/cortex-m-system-design-kit
 *
 * The hardware has an EXTIN input wire, which can be configured
 * by the guest to act either as a 'timer enable' (timer does not run
 * when EXTIN is low), or as a 'timer clock' (timer runs at frequency
 * of EXTIN clock, not PCLK frequency). We don't model this.
 *
 * The documentation is not very clear about the exact behaviour;
 * we choose to implement that the interrupt is triggered when
 * the counter goes from 1 to 0, that the counter then holds at 0
 * for one clock cycle before reloading from the RELOAD register,
 * and that if the RELOAD register is 0 this does not cause an
 * interrupt (as there is no further 1->0 transition).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/timer/cmsdk-apb-timer.h"
#include "migration/vmstate.h"

REG32(CTRL, 0)
    FIELD(CTRL, EN, 0, 1)
    FIELD(CTRL, SELEXTEN, 1, 1)
    FIELD(CTRL, SELEXTCLK, 2, 1)
    FIELD(CTRL, IRQEN, 3, 1)
REG32(VALUE, 4)
REG32(RELOAD, 8)
REG32(INTSTATUS, 0xc)
    FIELD(INTSTATUS, IRQ, 0, 1)
REG32(PID4, 0xFD0)
REG32(PID5, 0xFD4)
REG32(PID6, 0xFD8)
REG32(PID7, 0xFDC)
REG32(PID0, 0xFE0)
REG32(PID1, 0xFE4)
REG32(PID2, 0xFE8)
REG32(PID3, 0xFEC)
REG32(CID0, 0xFF0)
REG32(CID1, 0xFF4)
REG32(CID2, 0xFF8)
REG32(CID3, 0xFFC)

/* PID/CID values */
static const int timer_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x22, 0xb8, 0x1b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static void cmsdk_apb_timer_update(CMSDKAPBTIMER *s)
{
    qemu_set_irq(s->timerint, !!(s->intstatus & R_INTSTATUS_IRQ_MASK));
}

static uint64_t cmsdk_apb_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    CMSDKAPBTIMER *s = CMSDK_APB_TIMER(opaque);
    uint64_t r;

    switch (offset) {
    case A_CTRL:
        r = s->ctrl;
        break;
    case A_VALUE:
        r = ptimer_get_count(s->timer);
        break;
    case A_RELOAD:
        r = ptimer_get_limit(s->timer);
        break;
    case A_INTSTATUS:
        r = s->intstatus;
        break;
    case A_PID4 ... A_CID3:
        r = timer_id[(offset - A_PID4) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB timer read: bad offset %x\n", (int) offset);
        r = 0;
        break;
    }
    trace_cmsdk_apb_timer_read(offset, r, size);
    return r;
}

static void cmsdk_apb_timer_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size)
{
    CMSDKAPBTIMER *s = CMSDK_APB_TIMER(opaque);

    trace_cmsdk_apb_timer_write(offset, value, size);

    switch (offset) {
    case A_CTRL:
        if (value & 6) {
            /* Bits [1] and [2] enable using EXTIN as either clock or
             * an enable line. We don't model this.
             */
            qemu_log_mask(LOG_UNIMP,
                          "CMSDK APB timer: EXTIN input not supported\n");
        }
        s->ctrl = value & 0xf;
        ptimer_transaction_begin(s->timer);
        if (s->ctrl & R_CTRL_EN_MASK) {
            ptimer_run(s->timer, ptimer_get_limit(s->timer) == 0);
        } else {
            ptimer_stop(s->timer);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case A_RELOAD:
        /* Writing to reload also sets the current timer value */
        ptimer_transaction_begin(s->timer);
        if (!value) {
            ptimer_stop(s->timer);
        }
        ptimer_set_limit(s->timer, value, 1);
        if (value && (s->ctrl & R_CTRL_EN_MASK)) {
            /*
             * Make sure timer is running (it might have stopped if this
             * was an expired one-shot timer)
             */
            ptimer_run(s->timer, 0);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case A_VALUE:
        ptimer_transaction_begin(s->timer);
        if (!value && !ptimer_get_limit(s->timer)) {
            ptimer_stop(s->timer);
        }
        ptimer_set_count(s->timer, value);
        if (value && (s->ctrl & R_CTRL_EN_MASK)) {
            ptimer_run(s->timer, ptimer_get_limit(s->timer) == 0);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case A_INTSTATUS:
        /* Just one bit, which is W1C. */
        value &= 1;
        s->intstatus &= ~value;
        cmsdk_apb_timer_update(s);
        break;
    case A_PID4 ... A_CID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB timer write: write to RO offset 0x%x\n",
                      (int)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB timer write: bad offset 0x%x\n", (int) offset);
        break;
    }
}

static const MemoryRegionOps cmsdk_apb_timer_ops = {
    .read = cmsdk_apb_timer_read,
    .write = cmsdk_apb_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void cmsdk_apb_timer_tick(void *opaque)
{
    CMSDKAPBTIMER *s = CMSDK_APB_TIMER(opaque);

    if (s->ctrl & R_CTRL_IRQEN_MASK) {
        s->intstatus |= R_INTSTATUS_IRQ_MASK;
        cmsdk_apb_timer_update(s);
    }
}

static void cmsdk_apb_timer_reset(DeviceState *dev)
{
    CMSDKAPBTIMER *s = CMSDK_APB_TIMER(dev);

    trace_cmsdk_apb_timer_reset();
    s->ctrl = 0;
    s->intstatus = 0;
    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    /* Set the limit and the count */
    ptimer_set_limit(s->timer, 0, 1);
    ptimer_transaction_commit(s->timer);
}

static void cmsdk_apb_timer_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    CMSDKAPBTIMER *s = CMSDK_APB_TIMER(obj);

    memory_region_init_io(&s->iomem, obj, &cmsdk_apb_timer_ops,
                          s, "cmsdk-apb-timer", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->timerint);
}

static void cmsdk_apb_timer_realize(DeviceState *dev, Error **errp)
{
    CMSDKAPBTIMER *s = CMSDK_APB_TIMER(dev);

    if (s->pclk_frq == 0) {
        error_setg(errp, "CMSDK APB timer: pclk-frq property must be set");
        return;
    }

    s->timer = ptimer_init(cmsdk_apb_timer_tick, s,
                           PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
                           PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                           PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, s->pclk_frq);
    ptimer_transaction_commit(s->timer);
}

static const VMStateDescription cmsdk_apb_timer_vmstate = {
    .name = "cmsdk-apb-timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(timer, CMSDKAPBTIMER),
        VMSTATE_UINT32(ctrl, CMSDKAPBTIMER),
        VMSTATE_UINT32(value, CMSDKAPBTIMER),
        VMSTATE_UINT32(reload, CMSDKAPBTIMER),
        VMSTATE_UINT32(intstatus, CMSDKAPBTIMER),
        VMSTATE_END_OF_LIST()
    }
};

static Property cmsdk_apb_timer_properties[] = {
    DEFINE_PROP_UINT32("pclk-frq", CMSDKAPBTIMER, pclk_frq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void cmsdk_apb_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cmsdk_apb_timer_realize;
    dc->vmsd = &cmsdk_apb_timer_vmstate;
    dc->reset = cmsdk_apb_timer_reset;
    dc->props = cmsdk_apb_timer_properties;
}

static const TypeInfo cmsdk_apb_timer_info = {
    .name = TYPE_CMSDK_APB_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CMSDKAPBTIMER),
    .instance_init = cmsdk_apb_timer_init,
    .class_init = cmsdk_apb_timer_class_init,
};

static void cmsdk_apb_timer_register_types(void)
{
    type_register_static(&cmsdk_apb_timer_info);
}

type_init(cmsdk_apb_timer_register_types);
