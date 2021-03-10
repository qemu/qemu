/*
 * ARM CMSDK APB watchdog emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "APB watchdog" which is part of the Cortex-M
 * System Design Kit (CMSDK) and documented in the Cortex-M System
 * Design Kit Technical Reference Manual (ARM DDI0479C):
 * https://developer.arm.com/products/system-design/system-design-kits/cortex-m-system-design-kit
 *
 * We also support the variant of this device found in the TI
 * Stellaris/Luminary boards and documented in:
 * http://www.ti.com/lit/ds/symlink/lm3s6965.pdf
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "sysemu/watchdog.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/qdev-clock.h"
#include "hw/watchdog/cmsdk-apb-watchdog.h"
#include "migration/vmstate.h"

REG32(WDOGLOAD, 0x0)
REG32(WDOGVALUE, 0x4)
REG32(WDOGCONTROL, 0x8)
    FIELD(WDOGCONTROL, INTEN, 0, 1)
    FIELD(WDOGCONTROL, RESEN, 1, 1)
#define R_WDOGCONTROL_VALID_MASK (R_WDOGCONTROL_INTEN_MASK | \
                                  R_WDOGCONTROL_RESEN_MASK)
REG32(WDOGINTCLR, 0xc)
REG32(WDOGRIS, 0x10)
    FIELD(WDOGRIS, INT, 0, 1)
REG32(WDOGMIS, 0x14)
REG32(WDOGTEST, 0x418) /* only in Stellaris/Luminary version of the device */
REG32(WDOGLOCK, 0xc00)
#define WDOG_UNLOCK_VALUE 0x1ACCE551
REG32(WDOGITCR, 0xf00)
    FIELD(WDOGITCR, ENABLE, 0, 1)
#define R_WDOGITCR_VALID_MASK R_WDOGITCR_ENABLE_MASK
REG32(WDOGITOP, 0xf04)
    FIELD(WDOGITOP, WDOGRES, 0, 1)
    FIELD(WDOGITOP, WDOGINT, 1, 1)
#define R_WDOGITOP_VALID_MASK (R_WDOGITOP_WDOGRES_MASK | \
                               R_WDOGITOP_WDOGINT_MASK)
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
static const uint32_t cmsdk_apb_watchdog_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x24, 0xb8, 0x1b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static const uint32_t luminary_watchdog_id[] = {
    0x00, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x05, 0x18, 0x18, 0x01, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static bool cmsdk_apb_watchdog_intstatus(CMSDKAPBWatchdog *s)
{
    /* Return masked interrupt status */
    return s->intstatus && (s->control & R_WDOGCONTROL_INTEN_MASK);
}

static bool cmsdk_apb_watchdog_resetstatus(CMSDKAPBWatchdog *s)
{
    /* Return masked reset status */
    return s->resetstatus && (s->control & R_WDOGCONTROL_RESEN_MASK);
}

static void cmsdk_apb_watchdog_update(CMSDKAPBWatchdog *s)
{
    bool wdogint;
    bool wdogres;

    if (s->itcr) {
        /*
         * Not checking that !s->is_luminary since s->itcr can't be written
         * when s->is_luminary in the first place.
         */
        wdogint = s->itop & R_WDOGITOP_WDOGINT_MASK;
        wdogres = s->itop & R_WDOGITOP_WDOGRES_MASK;
    } else {
        wdogint = cmsdk_apb_watchdog_intstatus(s);
        wdogres = cmsdk_apb_watchdog_resetstatus(s);
    }

    qemu_set_irq(s->wdogint, wdogint);
    if (wdogres) {
        watchdog_perform_action();
    }
}

static uint64_t cmsdk_apb_watchdog_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    CMSDKAPBWatchdog *s = CMSDK_APB_WATCHDOG(opaque);
    uint64_t r;

    switch (offset) {
    case A_WDOGLOAD:
        r = ptimer_get_limit(s->timer);
        break;
    case A_WDOGVALUE:
        r = ptimer_get_count(s->timer);
        break;
    case A_WDOGCONTROL:
        r = s->control;
        break;
    case A_WDOGRIS:
        r = s->intstatus;
        break;
    case A_WDOGMIS:
        r = cmsdk_apb_watchdog_intstatus(s);
        break;
    case A_WDOGLOCK:
        r = s->lock;
        break;
    case A_WDOGITCR:
        if (s->is_luminary) {
            goto bad_offset;
        }
        r = s->itcr;
        break;
    case A_PID4 ... A_CID3:
        r = s->id[(offset - A_PID4) / 4];
        break;
    case A_WDOGINTCLR:
    case A_WDOGITOP:
        if (s->is_luminary) {
            goto bad_offset;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB watchdog read: read of WO offset %x\n",
                      (int)offset);
        r = 0;
        break;
    case A_WDOGTEST:
        if (!s->is_luminary) {
            goto bad_offset;
        }
        qemu_log_mask(LOG_UNIMP,
                      "Luminary watchdog read: stall not implemented\n");
        r = 0;
        break;
    default:
bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB watchdog read: bad offset %x\n", (int)offset);
        r = 0;
        break;
    }
    trace_cmsdk_apb_watchdog_read(offset, r, size);
    return r;
}

static void cmsdk_apb_watchdog_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    CMSDKAPBWatchdog *s = CMSDK_APB_WATCHDOG(opaque);

    trace_cmsdk_apb_watchdog_write(offset, value, size);

    if (s->lock && offset != A_WDOGLOCK) {
        /* Write access is disabled via WDOGLOCK */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB watchdog write: write to locked watchdog\n");
        return;
    }

    switch (offset) {
    case A_WDOGLOAD:
        /*
         * Reset the load value and the current count, and make sure
         * we're counting.
         */
        ptimer_transaction_begin(s->timer);
        ptimer_set_limit(s->timer, value, 1);
        ptimer_run(s->timer, 0);
        ptimer_transaction_commit(s->timer);
        break;
    case A_WDOGCONTROL:
        if (s->is_luminary && 0 != (R_WDOGCONTROL_INTEN_MASK & s->control)) {
            /*
             * The Luminary version of this device ignores writes to
             * this register after the guest has enabled interrupts
             * (so they can only be disabled again via reset).
             */
            break;
        }
        s->control = value & R_WDOGCONTROL_VALID_MASK;
        cmsdk_apb_watchdog_update(s);
        break;
    case A_WDOGINTCLR:
        s->intstatus = 0;
        ptimer_transaction_begin(s->timer);
        ptimer_set_count(s->timer, ptimer_get_limit(s->timer));
        ptimer_transaction_commit(s->timer);
        cmsdk_apb_watchdog_update(s);
        break;
    case A_WDOGLOCK:
        s->lock = (value != WDOG_UNLOCK_VALUE);
        trace_cmsdk_apb_watchdog_lock(s->lock);
        break;
    case A_WDOGITCR:
        if (s->is_luminary) {
            goto bad_offset;
        }
        s->itcr = value & R_WDOGITCR_VALID_MASK;
        cmsdk_apb_watchdog_update(s);
        break;
    case A_WDOGITOP:
        if (s->is_luminary) {
            goto bad_offset;
        }
        s->itop = value & R_WDOGITOP_VALID_MASK;
        cmsdk_apb_watchdog_update(s);
        break;
    case A_WDOGVALUE:
    case A_WDOGRIS:
    case A_WDOGMIS:
    case A_PID4 ... A_CID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB watchdog write: write to RO offset 0x%x\n",
                      (int)offset);
        break;
    case A_WDOGTEST:
        if (!s->is_luminary) {
            goto bad_offset;
        }
        qemu_log_mask(LOG_UNIMP,
                      "Luminary watchdog write: stall not implemented\n");
        break;
    default:
bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "CMSDK APB watchdog write: bad offset 0x%x\n",
                      (int)offset);
        break;
    }
}

static const MemoryRegionOps cmsdk_apb_watchdog_ops = {
    .read = cmsdk_apb_watchdog_read,
    .write = cmsdk_apb_watchdog_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* byte/halfword accesses are just zero-padded on reads and writes */
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void cmsdk_apb_watchdog_tick(void *opaque)
{
    CMSDKAPBWatchdog *s = CMSDK_APB_WATCHDOG(opaque);

    if (!s->intstatus) {
        /* Count expired for the first time: raise interrupt */
        s->intstatus = R_WDOGRIS_INT_MASK;
    } else {
        /* Count expired for the second time: raise reset and stop clock */
        s->resetstatus = 1;
        ptimer_stop(s->timer);
    }
    cmsdk_apb_watchdog_update(s);
}

static void cmsdk_apb_watchdog_reset(DeviceState *dev)
{
    CMSDKAPBWatchdog *s = CMSDK_APB_WATCHDOG(dev);

    trace_cmsdk_apb_watchdog_reset();
    s->control = 0;
    s->intstatus = 0;
    s->lock = 0;
    s->itcr = 0;
    s->itop = 0;
    s->resetstatus = 0;
    /* Set the limit and the count */
    ptimer_transaction_begin(s->timer);
    ptimer_set_limit(s->timer, 0xffffffff, 1);
    ptimer_run(s->timer, 0);
    ptimer_transaction_commit(s->timer);
}

static void cmsdk_apb_watchdog_clk_update(void *opaque, ClockEvent event)
{
    CMSDKAPBWatchdog *s = CMSDK_APB_WATCHDOG(opaque);

    ptimer_transaction_begin(s->timer);
    ptimer_set_period_from_clock(s->timer, s->wdogclk, 1);
    ptimer_transaction_commit(s->timer);
}

static void cmsdk_apb_watchdog_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    CMSDKAPBWatchdog *s = CMSDK_APB_WATCHDOG(obj);

    memory_region_init_io(&s->iomem, obj, &cmsdk_apb_watchdog_ops,
                          s, "cmsdk-apb-watchdog", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->wdogint);
    s->wdogclk = qdev_init_clock_in(DEVICE(s), "WDOGCLK",
                                    cmsdk_apb_watchdog_clk_update, s,
                                    ClockUpdate);

    s->is_luminary = false;
    s->id = cmsdk_apb_watchdog_id;
}

static void cmsdk_apb_watchdog_realize(DeviceState *dev, Error **errp)
{
    CMSDKAPBWatchdog *s = CMSDK_APB_WATCHDOG(dev);

    if (!clock_has_source(s->wdogclk)) {
        error_setg(errp,
                   "CMSDK APB watchdog: WDOGCLK clock must be connected");
        return;
    }

    s->timer = ptimer_init(cmsdk_apb_watchdog_tick, s,
                           PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
                           PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                           PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    ptimer_transaction_begin(s->timer);
    ptimer_set_period_from_clock(s->timer, s->wdogclk, 1);
    ptimer_transaction_commit(s->timer);
}

static const VMStateDescription cmsdk_apb_watchdog_vmstate = {
    .name = "cmsdk-apb-watchdog",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_CLOCK(wdogclk, CMSDKAPBWatchdog),
        VMSTATE_PTIMER(timer, CMSDKAPBWatchdog),
        VMSTATE_UINT32(control, CMSDKAPBWatchdog),
        VMSTATE_UINT32(intstatus, CMSDKAPBWatchdog),
        VMSTATE_UINT32(lock, CMSDKAPBWatchdog),
        VMSTATE_UINT32(itcr, CMSDKAPBWatchdog),
        VMSTATE_UINT32(itop, CMSDKAPBWatchdog),
        VMSTATE_UINT32(resetstatus, CMSDKAPBWatchdog),
        VMSTATE_END_OF_LIST()
    }
};

static void cmsdk_apb_watchdog_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cmsdk_apb_watchdog_realize;
    dc->vmsd = &cmsdk_apb_watchdog_vmstate;
    dc->reset = cmsdk_apb_watchdog_reset;
}

static const TypeInfo cmsdk_apb_watchdog_info = {
    .name = TYPE_CMSDK_APB_WATCHDOG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CMSDKAPBWatchdog),
    .instance_init = cmsdk_apb_watchdog_init,
    .class_init = cmsdk_apb_watchdog_class_init,
};

static void luminary_watchdog_init(Object *obj)
{
    CMSDKAPBWatchdog *s = CMSDK_APB_WATCHDOG(obj);

    s->is_luminary = true;
    s->id = luminary_watchdog_id;
}

static const TypeInfo luminary_watchdog_info = {
    .name = TYPE_LUMINARY_WATCHDOG,
    .parent = TYPE_CMSDK_APB_WATCHDOG,
    .instance_init = luminary_watchdog_init
};

static void cmsdk_apb_watchdog_register_types(void)
{
    type_register_static(&cmsdk_apb_watchdog_info);
    type_register_static(&luminary_watchdog_info);
}

type_init(cmsdk_apb_watchdog_register_types);
