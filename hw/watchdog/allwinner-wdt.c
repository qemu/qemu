/*
 * Allwinner Watchdog emulation
 *
 * Copyright (C) 2023 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 *  This file is derived from Allwinner RTC,
 *  by Niek Linnenbank.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/watchdog/allwinner-wdt.h"
#include "sysemu/watchdog.h"
#include "migration/vmstate.h"

/* WDT registers */
enum {
    REG_IRQ_EN = 0,     /* Watchdog interrupt enable */
    REG_IRQ_STA,        /* Watchdog interrupt status */
    REG_CTRL,           /* Watchdog control register */
    REG_CFG,            /* Watchdog configuration register */
    REG_MODE,           /* Watchdog mode register */
};

/* Universal WDT register flags */
#define WDT_RESTART_MASK    (1 << 0)
#define WDT_EN_MASK         (1 << 0)

/* sun4i specific WDT register flags */
#define RST_EN_SUN4I_MASK       (1 << 1)
#define INTV_VALUE_SUN4I_SHIFT  (3)
#define INTV_VALUE_SUN4I_MASK   (0xfu << INTV_VALUE_SUN4I_SHIFT)

/* sun6i specific WDT register flags */
#define RST_EN_SUN6I_MASK       (1 << 0)
#define KEY_FIELD_SUN6I_SHIFT   (1)
#define KEY_FIELD_SUN6I_MASK    (0xfffu << KEY_FIELD_SUN6I_SHIFT)
#define KEY_FIELD_SUN6I         (0xA57u)
#define INTV_VALUE_SUN6I_SHIFT  (4)
#define INTV_VALUE_SUN6I_MASK   (0xfu << INTV_VALUE_SUN6I_SHIFT)

/* Map of INTV_VALUE to 0.5s units. */
static const uint8_t allwinner_wdt_count_map[] = {
    1,
    2,
    4,
    6,
    8,
    10,
    12,
    16,
    20,
    24,
    28,
    32
};

/* WDT sun4i register map (offset to name) */
const uint8_t allwinner_wdt_sun4i_regmap[] = {
    [0x0000] = REG_CTRL,
    [0x0004] = REG_MODE,
};

/* WDT sun6i register map (offset to name) */
const uint8_t allwinner_wdt_sun6i_regmap[] = {
    [0x0000] = REG_IRQ_EN,
    [0x0004] = REG_IRQ_STA,
    [0x0010] = REG_CTRL,
    [0x0014] = REG_CFG,
    [0x0018] = REG_MODE,
};

static bool allwinner_wdt_sun4i_read(AwWdtState *s, uint32_t offset)
{
    /* no sun4i specific registers currently implemented */
    return false;
}

static bool allwinner_wdt_sun4i_write(AwWdtState *s, uint32_t offset,
                                      uint32_t data)
{
    /* no sun4i specific registers currently implemented */
    return false;
}

static bool allwinner_wdt_sun4i_can_reset_system(AwWdtState *s)
{
    if (s->regs[REG_MODE] & RST_EN_SUN4I_MASK) {
        return true;
    } else {
        return false;
    }
}

static bool allwinner_wdt_sun4i_is_key_valid(AwWdtState *s, uint32_t val)
{
    /* sun4i has no key */
    return true;
}

static uint8_t allwinner_wdt_sun4i_get_intv_value(AwWdtState *s)
{
    return ((s->regs[REG_MODE] & INTV_VALUE_SUN4I_MASK) >>
            INTV_VALUE_SUN4I_SHIFT);
}

static bool allwinner_wdt_sun6i_read(AwWdtState *s, uint32_t offset)
{
    const AwWdtClass *c = AW_WDT_GET_CLASS(s);

    switch (c->regmap[offset]) {
    case REG_IRQ_EN:
    case REG_IRQ_STA:
    case REG_CFG:
        return true;
    default:
        break;
    }
    return false;
}

static bool allwinner_wdt_sun6i_write(AwWdtState *s, uint32_t offset,
                                      uint32_t data)
{
    const AwWdtClass *c = AW_WDT_GET_CLASS(s);

    switch (c->regmap[offset]) {
    case REG_IRQ_EN:
    case REG_IRQ_STA:
    case REG_CFG:
        return true;
    default:
        break;
    }
    return false;
}

static bool allwinner_wdt_sun6i_can_reset_system(AwWdtState *s)
{
    if (s->regs[REG_CFG] & RST_EN_SUN6I_MASK) {
        return true;
    } else {
        return false;
    }
}

static bool allwinner_wdt_sun6i_is_key_valid(AwWdtState *s, uint32_t val)
{
    uint16_t key = (val & KEY_FIELD_SUN6I_MASK) >> KEY_FIELD_SUN6I_SHIFT;
    return (key == KEY_FIELD_SUN6I);
}

static uint8_t allwinner_wdt_sun6i_get_intv_value(AwWdtState *s)
{
    return ((s->regs[REG_MODE] & INTV_VALUE_SUN6I_MASK) >>
            INTV_VALUE_SUN6I_SHIFT);
}

static void allwinner_wdt_update_timer(AwWdtState *s)
{
    const AwWdtClass *c = AW_WDT_GET_CLASS(s);
    uint8_t count = c->get_intv_value(s);

    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);

    /* Use map to convert. */
    if (count < sizeof(allwinner_wdt_count_map)) {
        ptimer_set_count(s->timer, allwinner_wdt_count_map[count]);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: incorrect INTV_VALUE 0x%02x\n",
                __func__, count);
    }

    ptimer_run(s->timer, 1);
    ptimer_transaction_commit(s->timer);

    trace_allwinner_wdt_update_timer(count);
}

static uint64_t allwinner_wdt_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    AwWdtState *s = AW_WDT(opaque);
    const AwWdtClass *c = AW_WDT_GET_CLASS(s);
    uint64_t r;

    if (offset >= c->regmap_size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    switch (c->regmap[offset]) {
    case REG_CTRL:
    case REG_MODE:
        r = s->regs[c->regmap[offset]];
        break;
    default:
        if (!c->read(s, offset)) {
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%04x\n",
                            __func__, (uint32_t)offset);
            return 0;
        }
        r = s->regs[c->regmap[offset]];
        break;
    }

    trace_allwinner_wdt_read(offset, r, size);

    return r;
}

static void allwinner_wdt_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwWdtState *s = AW_WDT(opaque);
    const AwWdtClass *c = AW_WDT_GET_CLASS(s);
    uint32_t old_val;

    if (offset >= c->regmap_size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

   trace_allwinner_wdt_write(offset, val, size);

    switch (c->regmap[offset]) {
    case REG_CTRL:
        if (c->is_key_valid(s, val)) {
            if (val & WDT_RESTART_MASK) {
                /* Kick timer */
                allwinner_wdt_update_timer(s);
            }
        }
        break;
    case REG_MODE:
        old_val = s->regs[REG_MODE];
        s->regs[REG_MODE] = (uint32_t)val;

        /* Check for rising edge on WDOG_MODE_EN */
        if ((s->regs[REG_MODE] & ~old_val) & WDT_EN_MASK) {
            allwinner_wdt_update_timer(s);
        }
        break;
    default:
        if (!c->write(s, offset, val)) {
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented register 0x%04x\n",
                          __func__, (uint32_t)offset);
        }
        s->regs[c->regmap[offset]] = (uint32_t)val;
        break;
    }
}

static const MemoryRegionOps allwinner_wdt_ops = {
    .read = allwinner_wdt_read,
    .write = allwinner_wdt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_wdt_expired(void *opaque)
{
    AwWdtState *s = AW_WDT(opaque);
    const AwWdtClass *c = AW_WDT_GET_CLASS(s);

    bool enabled = s->regs[REG_MODE] & WDT_EN_MASK;
    bool reset_enabled = c->can_reset_system(s);

    trace_allwinner_wdt_expired(enabled, reset_enabled);

    /* Perform watchdog action if watchdog is enabled and can trigger reset */
    if (enabled && reset_enabled) {
        watchdog_perform_action();
    }
}

static void allwinner_wdt_reset_enter(Object *obj, ResetType type)
{
    AwWdtState *s = AW_WDT(obj);

    trace_allwinner_wdt_reset_enter();

    /* Clear registers */
    memset(s->regs, 0, sizeof(s->regs));
}

static const VMStateDescription allwinner_wdt_vmstate = {
    .name = "allwinner-wdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PTIMER(timer, AwWdtState),
        VMSTATE_UINT32_ARRAY(regs, AwWdtState, AW_WDT_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_wdt_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwWdtState *s = AW_WDT(obj);
    const AwWdtClass *c = AW_WDT_GET_CLASS(s);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_wdt_ops, s,
                          TYPE_AW_WDT, c->regmap_size * 4);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void allwinner_wdt_realize(DeviceState *dev, Error **errp)
{
    AwWdtState *s = AW_WDT(dev);

    s->timer = ptimer_init(allwinner_wdt_expired, s,
                           PTIMER_POLICY_NO_IMMEDIATE_TRIGGER |
                           PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    ptimer_transaction_begin(s->timer);
    /* Set to 2Hz (0.5s period); other periods are multiples of 0.5s. */
    ptimer_set_freq(s->timer, 2);
    ptimer_set_limit(s->timer, 0xff, 1);
    ptimer_transaction_commit(s->timer);
}

static void allwinner_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = allwinner_wdt_reset_enter;
    dc->realize = allwinner_wdt_realize;
    dc->vmsd = &allwinner_wdt_vmstate;
}

static void allwinner_wdt_sun4i_class_init(ObjectClass *klass, void *data)
{
    AwWdtClass *awc = AW_WDT_CLASS(klass);

    awc->regmap = allwinner_wdt_sun4i_regmap;
    awc->regmap_size = sizeof(allwinner_wdt_sun4i_regmap);
    awc->read = allwinner_wdt_sun4i_read;
    awc->write = allwinner_wdt_sun4i_write;
    awc->can_reset_system = allwinner_wdt_sun4i_can_reset_system;
    awc->is_key_valid = allwinner_wdt_sun4i_is_key_valid;
    awc->get_intv_value = allwinner_wdt_sun4i_get_intv_value;
}

static void allwinner_wdt_sun6i_class_init(ObjectClass *klass, void *data)
{
    AwWdtClass *awc = AW_WDT_CLASS(klass);

    awc->regmap = allwinner_wdt_sun6i_regmap;
    awc->regmap_size = sizeof(allwinner_wdt_sun6i_regmap);
    awc->read = allwinner_wdt_sun6i_read;
    awc->write = allwinner_wdt_sun6i_write;
    awc->can_reset_system = allwinner_wdt_sun6i_can_reset_system;
    awc->is_key_valid = allwinner_wdt_sun6i_is_key_valid;
    awc->get_intv_value = allwinner_wdt_sun6i_get_intv_value;
}

static const TypeInfo allwinner_wdt_info = {
    .name          = TYPE_AW_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_wdt_init,
    .instance_size = sizeof(AwWdtState),
    .class_init    = allwinner_wdt_class_init,
    .class_size    = sizeof(AwWdtClass),
    .abstract      = true,
};

static const TypeInfo allwinner_wdt_sun4i_info = {
    .name          = TYPE_AW_WDT_SUN4I,
    .parent        = TYPE_AW_WDT,
    .class_init    = allwinner_wdt_sun4i_class_init,
};

static const TypeInfo allwinner_wdt_sun6i_info = {
    .name          = TYPE_AW_WDT_SUN6I,
    .parent        = TYPE_AW_WDT,
    .class_init    = allwinner_wdt_sun6i_class_init,
};

static void allwinner_wdt_register(void)
{
    type_register_static(&allwinner_wdt_info);
    type_register_static(&allwinner_wdt_sun4i_info);
    type_register_static(&allwinner_wdt_sun6i_info);
}

type_init(allwinner_wdt_register)
