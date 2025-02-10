/*
 * nRF51 Random Number Generator
 *
 * Reference Manual: http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.1.pdf
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/arm/nrf51.h"
#include "hw/irq.h"
#include "hw/misc/nrf51_rng.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/guest-random.h"

static void update_irq(NRF51RNGState *s)
{
    bool irq = s->interrupt_enabled && s->event_valrdy;
    qemu_set_irq(s->irq, irq);
}

static uint64_t rng_read(void *opaque, hwaddr offset, unsigned int size)
{
    NRF51RNGState *s = NRF51_RNG(opaque);
    uint64_t r = 0;

    switch (offset) {
    case NRF51_RNG_EVENT_VALRDY:
        r = s->event_valrdy;
        break;
    case NRF51_RNG_REG_SHORTS:
        r = s->shortcut_stop_on_valrdy;
        break;
    case NRF51_RNG_REG_INTEN:
    case NRF51_RNG_REG_INTENSET:
    case NRF51_RNG_REG_INTENCLR:
        r = s->interrupt_enabled;
        break;
    case NRF51_RNG_REG_CONFIG:
        r = s->filter_enabled;
        break;
    case NRF51_RNG_REG_VALUE:
        r = s->value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    return r;
}

static int64_t calc_next_timeout(NRF51RNGState *s)
{
    int64_t timeout = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    if (s->filter_enabled) {
        timeout += s->period_filtered_us;
    } else {
        timeout += s->period_unfiltered_us;
    }

    return timeout;
}


static void rng_update_timer(NRF51RNGState *s)
{
    if (s->active) {
        timer_mod(&s->timer, calc_next_timeout(s));
    } else {
        timer_del(&s->timer);
    }
}


static void rng_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned int size)
{
    NRF51RNGState *s = NRF51_RNG(opaque);

    switch (offset) {
    case NRF51_RNG_TASK_START:
        if (value == NRF51_TRIGGER_TASK) {
            s->active = 1;
            rng_update_timer(s);
        }
        break;
    case NRF51_RNG_TASK_STOP:
        if (value == NRF51_TRIGGER_TASK) {
            s->active = 0;
            rng_update_timer(s);
        }
        break;
    case NRF51_RNG_EVENT_VALRDY:
        if (value == NRF51_EVENT_CLEAR) {
            s->event_valrdy = 0;
        }
        break;
    case NRF51_RNG_REG_SHORTS:
        s->shortcut_stop_on_valrdy =
                (value & BIT(NRF51_RNG_REG_SHORTS_VALRDY_STOP)) ? 1 : 0;
        break;
    case NRF51_RNG_REG_INTEN:
        s->interrupt_enabled =
                (value & BIT(NRF51_RNG_REG_INTEN_VALRDY)) ? 1 : 0;
        break;
    case NRF51_RNG_REG_INTENSET:
        if (value & BIT(NRF51_RNG_REG_INTEN_VALRDY)) {
            s->interrupt_enabled = 1;
        }
        break;
    case NRF51_RNG_REG_INTENCLR:
        if (value & BIT(NRF51_RNG_REG_INTEN_VALRDY)) {
            s->interrupt_enabled = 0;
        }
        break;
    case NRF51_RNG_REG_CONFIG:
        s->filter_enabled =
                      (value & BIT(NRF51_RNG_REG_CONFIG_DECEN)) ? 1 : 0;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    update_irq(s);
}

static const MemoryRegionOps rng_ops = {
    .read =  rng_read,
    .write = rng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4
};

static void nrf51_rng_timer_expire(void *opaque)
{
    NRF51RNGState *s = NRF51_RNG(opaque);

    qemu_guest_getrandom_nofail(&s->value, 1);

    s->event_valrdy = 1;
    qemu_set_irq(s->eep_valrdy, 1);

    if (s->shortcut_stop_on_valrdy) {
        s->active = 0;
    }

    rng_update_timer(s);
    update_irq(s);
}

static void nrf51_rng_tep_start(void *opaque, int n, int level)
{
    NRF51RNGState *s = NRF51_RNG(opaque);

    if (level) {
        s->active = 1;
        rng_update_timer(s);
    }
}

static void nrf51_rng_tep_stop(void *opaque, int n, int level)
{
    NRF51RNGState *s = NRF51_RNG(opaque);

    if (level) {
        s->active = 0;
        rng_update_timer(s);
    }
}


static void nrf51_rng_init(Object *obj)
{
    NRF51RNGState *s = NRF51_RNG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &rng_ops, s,
            TYPE_NRF51_RNG, NRF51_RNG_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);

    timer_init_us(&s->timer, QEMU_CLOCK_VIRTUAL, nrf51_rng_timer_expire, s);

    sysbus_init_irq(sbd, &s->irq);

    /* Tasks */
    qdev_init_gpio_in_named(DEVICE(s), nrf51_rng_tep_start, "tep_start", 1);
    qdev_init_gpio_in_named(DEVICE(s), nrf51_rng_tep_stop, "tep_stop", 1);

    /* Events */
    qdev_init_gpio_out_named(DEVICE(s), &s->eep_valrdy, "eep_valrdy", 1);
}

static void nrf51_rng_reset(DeviceState *dev)
{
    NRF51RNGState *s = NRF51_RNG(dev);

    s->value = 0;
    s->active = 0;
    s->event_valrdy = 0;
    s->shortcut_stop_on_valrdy = 0;
    s->interrupt_enabled = 0;
    s->filter_enabled = 0;

    rng_update_timer(s);
}


static const Property nrf51_rng_properties[] = {
    DEFINE_PROP_UINT16("period_unfiltered_us", NRF51RNGState,
            period_unfiltered_us, 167),
    DEFINE_PROP_UINT16("period_filtered_us", NRF51RNGState,
            period_filtered_us, 660),
};

static const VMStateDescription vmstate_rng = {
    .name = "nrf51_soc.rng",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(active, NRF51RNGState),
        VMSTATE_UINT32(event_valrdy, NRF51RNGState),
        VMSTATE_UINT32(shortcut_stop_on_valrdy, NRF51RNGState),
        VMSTATE_UINT32(interrupt_enabled, NRF51RNGState),
        VMSTATE_UINT32(filter_enabled, NRF51RNGState),
        VMSTATE_END_OF_LIST()
    }
};

static void nrf51_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nrf51_rng_properties);
    dc->vmsd = &vmstate_rng;
    device_class_set_legacy_reset(dc, nrf51_rng_reset);
}

static const TypeInfo nrf51_rng_info = {
    .name = TYPE_NRF51_RNG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF51RNGState),
    .instance_init = nrf51_rng_init,
    .class_init = nrf51_rng_class_init
};

static void nrf51_rng_register_types(void)
{
    type_register_static(&nrf51_rng_info);
}

type_init(nrf51_rng_register_types)
