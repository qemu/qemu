/*
 * Nuvoton NPCM7xx General Purpose Input / Output (GPIO)
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"

#include "hw/gpio/npcm7xx_gpio.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "trace.h"

/* 32-bit register indices. */
enum NPCM7xxGPIORegister {
    NPCM7XX_GPIO_TLOCK1,
    NPCM7XX_GPIO_DIN,
    NPCM7XX_GPIO_POL,
    NPCM7XX_GPIO_DOUT,
    NPCM7XX_GPIO_OE,
    NPCM7XX_GPIO_OTYP,
    NPCM7XX_GPIO_MP,
    NPCM7XX_GPIO_PU,
    NPCM7XX_GPIO_PD,
    NPCM7XX_GPIO_DBNC,
    NPCM7XX_GPIO_EVTYP,
    NPCM7XX_GPIO_EVBE,
    NPCM7XX_GPIO_OBL0,
    NPCM7XX_GPIO_OBL1,
    NPCM7XX_GPIO_OBL2,
    NPCM7XX_GPIO_OBL3,
    NPCM7XX_GPIO_EVEN,
    NPCM7XX_GPIO_EVENS,
    NPCM7XX_GPIO_EVENC,
    NPCM7XX_GPIO_EVST,
    NPCM7XX_GPIO_SPLCK,
    NPCM7XX_GPIO_MPLCK,
    NPCM7XX_GPIO_IEM,
    NPCM7XX_GPIO_OSRC,
    NPCM7XX_GPIO_ODSC,
    NPCM7XX_GPIO_DOS = 0x68 / sizeof(uint32_t),
    NPCM7XX_GPIO_DOC,
    NPCM7XX_GPIO_OES,
    NPCM7XX_GPIO_OEC,
    NPCM7XX_GPIO_TLOCK2 = 0x7c / sizeof(uint32_t),
    NPCM7XX_GPIO_REGS_END,
};

#define NPCM7XX_GPIO_REGS_SIZE (4 * KiB)

#define NPCM7XX_GPIO_LOCK_MAGIC1 (0xc0defa73)
#define NPCM7XX_GPIO_LOCK_MAGIC2 (0xc0de1248)

static void npcm7xx_gpio_update_events(NPCM7xxGPIOState *s, uint32_t din_diff)
{
    uint32_t din_new = s->regs[NPCM7XX_GPIO_DIN];

    /* Trigger on high level */
    s->regs[NPCM7XX_GPIO_EVST] |= din_new & ~s->regs[NPCM7XX_GPIO_EVTYP];
    /* Trigger on both edges */
    s->regs[NPCM7XX_GPIO_EVST] |= (din_diff & s->regs[NPCM7XX_GPIO_EVTYP]
                                   & s->regs[NPCM7XX_GPIO_EVBE]);
    /* Trigger on rising edge */
    s->regs[NPCM7XX_GPIO_EVST] |= (din_diff & din_new
                                   & s->regs[NPCM7XX_GPIO_EVTYP]);

    trace_npcm7xx_gpio_update_events(DEVICE(s)->canonical_path,
                                     s->regs[NPCM7XX_GPIO_EVST],
                                     s->regs[NPCM7XX_GPIO_EVEN]);
    qemu_set_irq(s->irq, !!(s->regs[NPCM7XX_GPIO_EVST]
                            & s->regs[NPCM7XX_GPIO_EVEN]));
}

static void npcm7xx_gpio_update_pins(NPCM7xxGPIOState *s, uint32_t diff)
{
    uint32_t drive_en;
    uint32_t drive_lvl;
    uint32_t not_driven;
    uint32_t undefined;
    uint32_t pin_diff;
    uint32_t din_old;

    /* Calculate level of each pin driven by GPIO controller. */
    drive_lvl = s->regs[NPCM7XX_GPIO_DOUT] ^ s->regs[NPCM7XX_GPIO_POL];
    /* If OTYP=1, only drive low (open drain) */
    drive_en = s->regs[NPCM7XX_GPIO_OE] & ~(s->regs[NPCM7XX_GPIO_OTYP]
                                            & drive_lvl);
    /*
     * If a pin is driven to opposite levels by the GPIO controller and the
     * external driver, the result is undefined.
     */
    undefined = drive_en & s->ext_driven & (drive_lvl ^ s->ext_level);
    if (undefined) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: pins have multiple drivers: 0x%" PRIx32 "\n",
                      DEVICE(s)->canonical_path, undefined);
    }

    not_driven = ~(drive_en | s->ext_driven);
    pin_diff = s->pin_level;

    /* Set pins to externally driven level. */
    s->pin_level = s->ext_level & s->ext_driven;
    /* Set internally driven pins, ignoring any conflicts. */
    s->pin_level |= drive_lvl & drive_en;
    /* Pull up undriven pins with internal pull-up enabled. */
    s->pin_level |= not_driven & s->regs[NPCM7XX_GPIO_PU];
    /* Pins not driven, pulled up or pulled down are undefined */
    undefined |= not_driven & ~(s->regs[NPCM7XX_GPIO_PU]
                                | s->regs[NPCM7XX_GPIO_PD]);

    /* If any pins changed state, update the outgoing GPIOs. */
    pin_diff ^= s->pin_level;
    pin_diff |= undefined & diff;
    if (pin_diff) {
        int i;

        for (i = 0; i < NPCM7XX_GPIO_NR_PINS; i++) {
            uint32_t mask = BIT(i);
            if (pin_diff & mask) {
                int level = (undefined & mask) ? -1 : !!(s->pin_level & mask);
                trace_npcm7xx_gpio_set_output(DEVICE(s)->canonical_path,
                                              i, level);
                qemu_set_irq(s->output[i], level);
            }
        }
    }

    /* Calculate new value of DIN after masking and polarity setting. */
    din_old = s->regs[NPCM7XX_GPIO_DIN];
    s->regs[NPCM7XX_GPIO_DIN] = ((s->pin_level & s->regs[NPCM7XX_GPIO_IEM])
                                 ^ s->regs[NPCM7XX_GPIO_POL]);

    /* See if any new events triggered because of all this. */
    npcm7xx_gpio_update_events(s, din_old ^ s->regs[NPCM7XX_GPIO_DIN]);
}

static bool npcm7xx_gpio_is_locked(NPCM7xxGPIOState *s)
{
    return s->regs[NPCM7XX_GPIO_TLOCK1] == 1;
}

static uint64_t npcm7xx_gpio_regs_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    hwaddr reg = addr / sizeof(uint32_t);
    NPCM7xxGPIOState *s = opaque;
    uint64_t value = 0;

    switch (reg) {
    case NPCM7XX_GPIO_TLOCK1 ... NPCM7XX_GPIO_EVEN:
    case NPCM7XX_GPIO_EVST ... NPCM7XX_GPIO_ODSC:
        value = s->regs[reg];
        break;

    case NPCM7XX_GPIO_EVENS ... NPCM7XX_GPIO_EVENC:
    case NPCM7XX_GPIO_DOS ... NPCM7XX_GPIO_TLOCK2:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from write-only register 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, addr);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, addr);
        break;
    }

    trace_npcm7xx_gpio_read(DEVICE(s)->canonical_path, addr, value);

    return value;
}

static void npcm7xx_gpio_regs_write(void *opaque, hwaddr addr, uint64_t v,
                                    unsigned int size)
{
    hwaddr reg = addr / sizeof(uint32_t);
    NPCM7xxGPIOState *s = opaque;
    uint32_t value = v;
    uint32_t diff;

    trace_npcm7xx_gpio_write(DEVICE(s)->canonical_path, addr, v);

    if (npcm7xx_gpio_is_locked(s)) {
        switch (reg) {
        case NPCM7XX_GPIO_TLOCK1:
            if (s->regs[NPCM7XX_GPIO_TLOCK2] == NPCM7XX_GPIO_LOCK_MAGIC2 &&
                value == NPCM7XX_GPIO_LOCK_MAGIC1) {
                s->regs[NPCM7XX_GPIO_TLOCK1] = 0;
                s->regs[NPCM7XX_GPIO_TLOCK2] = 0;
            }
            break;

        case NPCM7XX_GPIO_TLOCK2:
            s->regs[reg] = value;
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to locked register @ 0x%" HWADDR_PRIx "\n",
                          DEVICE(s)->canonical_path, addr);
            break;
        }

        return;
    }

    diff = s->regs[reg] ^ value;

    switch (reg) {
    case NPCM7XX_GPIO_TLOCK1:
    case NPCM7XX_GPIO_TLOCK2:
        s->regs[NPCM7XX_GPIO_TLOCK1] = 1;
        s->regs[NPCM7XX_GPIO_TLOCK2] = 0;
        break;

    case NPCM7XX_GPIO_DIN:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register @ 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, addr);
        break;

    case NPCM7XX_GPIO_POL:
    case NPCM7XX_GPIO_DOUT:
    case NPCM7XX_GPIO_OE:
    case NPCM7XX_GPIO_OTYP:
    case NPCM7XX_GPIO_PU:
    case NPCM7XX_GPIO_PD:
    case NPCM7XX_GPIO_IEM:
        s->regs[reg] = value;
        npcm7xx_gpio_update_pins(s, diff);
        break;

    case NPCM7XX_GPIO_DOS:
        s->regs[NPCM7XX_GPIO_DOUT] |= value;
        npcm7xx_gpio_update_pins(s, value);
        break;
    case NPCM7XX_GPIO_DOC:
        s->regs[NPCM7XX_GPIO_DOUT] &= ~value;
        npcm7xx_gpio_update_pins(s, value);
        break;
    case NPCM7XX_GPIO_OES:
        s->regs[NPCM7XX_GPIO_OE] |= value;
        npcm7xx_gpio_update_pins(s, value);
        break;
    case NPCM7XX_GPIO_OEC:
        s->regs[NPCM7XX_GPIO_OE] &= ~value;
        npcm7xx_gpio_update_pins(s, value);
        break;

    case NPCM7XX_GPIO_EVTYP:
    case NPCM7XX_GPIO_EVBE:
    case NPCM7XX_GPIO_EVEN:
        s->regs[reg] = value;
        npcm7xx_gpio_update_events(s, 0);
        break;

    case NPCM7XX_GPIO_EVENS:
        s->regs[NPCM7XX_GPIO_EVEN] |= value;
        npcm7xx_gpio_update_events(s, 0);
        break;
    case NPCM7XX_GPIO_EVENC:
        s->regs[NPCM7XX_GPIO_EVEN] &= ~value;
        npcm7xx_gpio_update_events(s, 0);
        break;

    case NPCM7XX_GPIO_EVST:
        s->regs[reg] &= ~value;
        npcm7xx_gpio_update_events(s, 0);
        break;

    case NPCM7XX_GPIO_MP:
    case NPCM7XX_GPIO_DBNC:
    case NPCM7XX_GPIO_OSRC:
    case NPCM7XX_GPIO_ODSC:
        /* Nothing to do; just store the value. */
        s->regs[reg] = value;
        break;

    case NPCM7XX_GPIO_OBL0:
    case NPCM7XX_GPIO_OBL1:
    case NPCM7XX_GPIO_OBL2:
    case NPCM7XX_GPIO_OBL3:
        s->regs[reg] = value;
        qemu_log_mask(LOG_UNIMP, "%s: Blinking is not implemented\n",
                      __func__);
        break;

    case NPCM7XX_GPIO_SPLCK:
    case NPCM7XX_GPIO_MPLCK:
        qemu_log_mask(LOG_UNIMP, "%s: Per-pin lock is not implemented\n",
                      __func__);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, addr);
        break;
    }
}

static const MemoryRegionOps npcm7xx_gpio_regs_ops = {
    .read = npcm7xx_gpio_regs_read,
    .write = npcm7xx_gpio_regs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void npcm7xx_gpio_set_input(void *opaque, int line, int level)
{
    NPCM7xxGPIOState *s = opaque;

    trace_npcm7xx_gpio_set_input(DEVICE(s)->canonical_path, line, level);

    g_assert(line >= 0 && line < NPCM7XX_GPIO_NR_PINS);

    s->ext_driven = deposit32(s->ext_driven, line, 1, level >= 0);
    s->ext_level = deposit32(s->ext_level, line, 1, level > 0);

    npcm7xx_gpio_update_pins(s, BIT(line));
}

static void npcm7xx_gpio_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxGPIOState *s = NPCM7XX_GPIO(obj);

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[NPCM7XX_GPIO_PU] = s->reset_pu;
    s->regs[NPCM7XX_GPIO_PD] = s->reset_pd;
    s->regs[NPCM7XX_GPIO_OSRC] = s->reset_osrc;
    s->regs[NPCM7XX_GPIO_ODSC] = s->reset_odsc;
}

static void npcm7xx_gpio_hold_reset(Object *obj)
{
    NPCM7xxGPIOState *s = NPCM7XX_GPIO(obj);

    npcm7xx_gpio_update_pins(s, -1);
}

static void npcm7xx_gpio_init(Object *obj)
{
    NPCM7xxGPIOState *s = NPCM7XX_GPIO(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &npcm7xx_gpio_regs_ops, s,
                          "regs", NPCM7XX_GPIO_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    qdev_init_gpio_in(dev, npcm7xx_gpio_set_input, NPCM7XX_GPIO_NR_PINS);
    qdev_init_gpio_out(dev, s->output, NPCM7XX_GPIO_NR_PINS);
}

static const VMStateDescription vmstate_npcm7xx_gpio = {
    .name = "npcm7xx-gpio",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(pin_level, NPCM7xxGPIOState),
        VMSTATE_UINT32(ext_level, NPCM7xxGPIOState),
        VMSTATE_UINT32(ext_driven, NPCM7xxGPIOState),
        VMSTATE_UINT32_ARRAY(regs, NPCM7xxGPIOState, NPCM7XX_GPIO_NR_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static Property npcm7xx_gpio_properties[] = {
    /* Bit n set => pin n has pullup enabled by default. */
    DEFINE_PROP_UINT32("reset-pullup", NPCM7xxGPIOState, reset_pu, 0),
    /* Bit n set => pin n has pulldown enabled by default. */
    DEFINE_PROP_UINT32("reset-pulldown", NPCM7xxGPIOState, reset_pd, 0),
    /* Bit n set => pin n has high slew rate by default. */
    DEFINE_PROP_UINT32("reset-osrc", NPCM7xxGPIOState, reset_osrc, 0),
    /* Bit n set => pin n has high drive strength by default. */
    DEFINE_PROP_UINT32("reset-odsc", NPCM7xxGPIOState, reset_odsc, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void npcm7xx_gpio_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *reset = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    QEMU_BUILD_BUG_ON(NPCM7XX_GPIO_REGS_END > NPCM7XX_GPIO_NR_REGS);

    dc->desc = "NPCM7xx GPIO Controller";
    dc->vmsd = &vmstate_npcm7xx_gpio;
    reset->phases.enter = npcm7xx_gpio_enter_reset;
    reset->phases.hold = npcm7xx_gpio_hold_reset;
    device_class_set_props(dc, npcm7xx_gpio_properties);
}

static const TypeInfo npcm7xx_gpio_types[] = {
    {
        .name = TYPE_NPCM7XX_GPIO,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NPCM7xxGPIOState),
        .class_init = npcm7xx_gpio_class_init,
        .instance_init = npcm7xx_gpio_init,
    },
};
DEFINE_TYPES(npcm7xx_gpio_types);
