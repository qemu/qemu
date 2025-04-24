/*
 * Generic watchdog device model for SBSA
 *
 * The watchdog device has been implemented as revision 1 variant of
 * the ARM SBSA specification v6.0
 * (https://developer.arm.com/documentation/den0029/d?lang=en)
 *
 * Copyright Linaro.org 2020
 *
 * Authors:
 *  Shashi Mallela <shashi.mallela@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "system/reset.h"
#include "system/watchdog.h"
#include "hw/qdev-properties.h"
#include "hw/watchdog/sbsa_gwdt.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

static const VMStateDescription vmstate_sbsa_gwdt = {
    .name = "sbsa-gwdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, SBSA_GWDTState),
        VMSTATE_UINT32(wcs, SBSA_GWDTState),
        VMSTATE_UINT32(worl, SBSA_GWDTState),
        VMSTATE_UINT32(woru, SBSA_GWDTState),
        VMSTATE_UINT32(wcvl, SBSA_GWDTState),
        VMSTATE_UINT32(wcvu, SBSA_GWDTState),
        VMSTATE_END_OF_LIST()
    }
};

typedef enum WdtRefreshType {
    EXPLICIT_REFRESH = 0,
    TIMEOUT_REFRESH = 1,
} WdtRefreshType;

static uint64_t sbsa_gwdt_rread(void *opaque, hwaddr addr, unsigned int size)
{
    SBSA_GWDTState *s = SBSA_GWDT(opaque);
    uint32_t ret = 0;

    switch (addr) {
    case SBSA_GWDT_WRR:
        /* watch refresh read has no effect and returns 0 */
        ret = 0;
        break;
    case SBSA_GWDT_W_IIDR:
        ret = s->id;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bad address in refresh frame read :"
                        " 0x%x\n", (int)addr);
    }
    return ret;
}

static uint64_t sbsa_gwdt_read(void *opaque, hwaddr addr, unsigned int size)
{
    SBSA_GWDTState *s = SBSA_GWDT(opaque);
    uint32_t ret = 0;

    switch (addr) {
    case SBSA_GWDT_WCS:
        ret = s->wcs;
        break;
    case SBSA_GWDT_WOR:
        ret = s->worl;
        break;
    case SBSA_GWDT_WORU:
         ret = s->woru;
         break;
    case SBSA_GWDT_WCV:
        ret = s->wcvl;
        break;
    case SBSA_GWDT_WCVU:
        ret = s->wcvu;
        break;
    case SBSA_GWDT_W_IIDR:
        ret = s->id;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bad address in control frame read :"
                        " 0x%x\n", (int)addr);
    }
    return ret;
}

static void sbsa_gwdt_update_timer(SBSA_GWDTState *s, WdtRefreshType rtype)
{
    uint64_t timeout = 0;

    timer_del(s->timer);

    if (s->wcs & SBSA_GWDT_WCS_EN) {
        /*
         * Extract the upper 16 bits from woru & 32 bits from worl
         * registers to construct the 48 bit offset value
         */
        timeout = s->woru;
        timeout <<= 32;
        timeout |= s->worl;
        timeout = muldiv64(timeout, NANOSECONDS_PER_SECOND, s->freq);
        timeout += qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

        if ((rtype == EXPLICIT_REFRESH) || ((rtype == TIMEOUT_REFRESH) &&
                (!(s->wcs & SBSA_GWDT_WCS_WS0)))) {
            /* store the current timeout value into compare registers */
            s->wcvu = timeout >> 32;
            s->wcvl = timeout;
        }
        timer_mod(s->timer, timeout);
    }
}

static void sbsa_gwdt_rwrite(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size) {
    SBSA_GWDTState *s = SBSA_GWDT(opaque);

    if (offset == SBSA_GWDT_WRR) {
        s->wcs &= ~(SBSA_GWDT_WCS_WS0 | SBSA_GWDT_WCS_WS1);

        sbsa_gwdt_update_timer(s, EXPLICIT_REFRESH);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "bad address in refresh frame write :"
                        " 0x%x\n", (int)offset);
    }
}

static void sbsa_gwdt_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size) {
    SBSA_GWDTState *s = SBSA_GWDT(opaque);

    switch (offset) {
    case SBSA_GWDT_WCS:
        s->wcs = data & SBSA_GWDT_WCS_EN;
        qemu_set_irq(s->irq, 0);
        sbsa_gwdt_update_timer(s, EXPLICIT_REFRESH);
        break;

    case SBSA_GWDT_WOR:
        s->worl = data;
        s->wcs &= ~(SBSA_GWDT_WCS_WS0 | SBSA_GWDT_WCS_WS1);
        qemu_set_irq(s->irq, 0);
        sbsa_gwdt_update_timer(s, EXPLICIT_REFRESH);
        break;

    case SBSA_GWDT_WORU:
        s->woru = data & SBSA_GWDT_WOR_MASK;
        s->wcs &= ~(SBSA_GWDT_WCS_WS0 | SBSA_GWDT_WCS_WS1);
        qemu_set_irq(s->irq, 0);
        sbsa_gwdt_update_timer(s, EXPLICIT_REFRESH);
        break;

    case SBSA_GWDT_WCV:
        s->wcvl = data;
        break;

    case SBSA_GWDT_WCVU:
        s->wcvu = data;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bad address in control frame write :"
                " 0x%x\n", (int)offset);
    }
}

static void wdt_sbsa_gwdt_reset(DeviceState *dev)
{
    SBSA_GWDTState *s = SBSA_GWDT(dev);

    timer_del(s->timer);

    s->wcs  = 0;
    s->wcvl = 0;
    s->wcvu = 0;
    s->worl = 0;
    s->woru = 0;
    s->id = SBSA_GWDT_ID;
}

static void sbsa_gwdt_timer_sysinterrupt(void *opaque)
{
    SBSA_GWDTState *s = SBSA_GWDT(opaque);

    if (!(s->wcs & SBSA_GWDT_WCS_WS0)) {
        s->wcs |= SBSA_GWDT_WCS_WS0;
        sbsa_gwdt_update_timer(s, TIMEOUT_REFRESH);
        qemu_set_irq(s->irq, 1);
    } else {
        s->wcs |= SBSA_GWDT_WCS_WS1;
        qemu_log_mask(CPU_LOG_RESET, "Watchdog timer expired.\n");
        /*
         * Reset the watchdog only if the guest gets notified about
         * expiry. watchdog_perform_action() may temporarily relinquish
         * the BQL; reset before triggering the action to avoid races with
         * sbsa_gwdt instructions.
         */
        switch (get_watchdog_action()) {
        case WATCHDOG_ACTION_DEBUG:
        case WATCHDOG_ACTION_NONE:
        case WATCHDOG_ACTION_PAUSE:
            break;
        default:
            wdt_sbsa_gwdt_reset(DEVICE(s));
        }
        watchdog_perform_action();
    }
}

static const MemoryRegionOps sbsa_gwdt_rops = {
    .read = sbsa_gwdt_rread,
    .write = sbsa_gwdt_rwrite,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static const MemoryRegionOps sbsa_gwdt_ops = {
    .read = sbsa_gwdt_read,
    .write = sbsa_gwdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void wdt_sbsa_gwdt_realize(DeviceState *dev, Error **errp)
{
    SBSA_GWDTState *s = SBSA_GWDT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->rmmio, OBJECT(dev),
                          &sbsa_gwdt_rops, s,
                          "sbsa_gwdt.refresh",
                          SBSA_GWDT_RMMIO_SIZE);

    memory_region_init_io(&s->cmmio, OBJECT(dev),
                          &sbsa_gwdt_ops, s,
                          "sbsa_gwdt.control",
                          SBSA_GWDT_CMMIO_SIZE);

    sysbus_init_mmio(sbd, &s->rmmio);
    sysbus_init_mmio(sbd, &s->cmmio);

    sysbus_init_irq(sbd, &s->irq);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sbsa_gwdt_timer_sysinterrupt,
                dev);
}

static const Property wdt_sbsa_gwdt_props[] = {
    /*
     * Timer frequency in Hz. This must match the frequency used by
     * the CPU's generic timer. Default 62.5Hz matches QEMU's legacy
     * CPU timer frequency default.
     */
    DEFINE_PROP_UINT64("clock-frequency", struct SBSA_GWDTState, freq,
                       62500000),
};

static void wdt_sbsa_gwdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = wdt_sbsa_gwdt_realize;
    device_class_set_legacy_reset(dc, wdt_sbsa_gwdt_reset);
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_WATCHDOG, dc->categories);
    dc->vmsd = &vmstate_sbsa_gwdt;
    dc->desc = "SBSA-compliant generic watchdog device";
    device_class_set_props(dc, wdt_sbsa_gwdt_props);
}

static const TypeInfo wdt_sbsa_gwdt_info = {
    .class_init = wdt_sbsa_gwdt_class_init,
    .parent = TYPE_SYS_BUS_DEVICE,
    .name  = TYPE_WDT_SBSA,
    .instance_size  = sizeof(SBSA_GWDTState),
};

static void wdt_sbsa_gwdt_register_types(void)
{
    type_register_static(&wdt_sbsa_gwdt_info);
}

type_init(wdt_sbsa_gwdt_register_types)
