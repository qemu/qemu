/*
 * i.MX processors GPIO emulation.
 *
 * Copyright (C) 2015 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/gpio/imx_gpio.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#ifndef DEBUG_IMX_GPIO
#define DEBUG_IMX_GPIO 0
#endif

typedef enum IMXGPIOLevel {
    IMX_GPIO_LEVEL_LOW = 0,
    IMX_GPIO_LEVEL_HIGH = 1,
} IMXGPIOLevel;

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_GPIO) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_GPIO, \
                                             __func__, ##args); \
        } \
    } while (0)

static const char *imx_gpio_reg_name(uint32_t reg)
{
    switch (reg) {
    case DR_ADDR:
        return "DR";
    case GDIR_ADDR:
        return "GDIR";
    case PSR_ADDR:
        return "PSR";
    case ICR1_ADDR:
        return "ICR1";
    case ICR2_ADDR:
        return "ICR2";
    case IMR_ADDR:
        return "IMR";
    case ISR_ADDR:
        return "ISR";
    case EDGE_SEL_ADDR:
        return "EDGE_SEL";
    default:
        return "[?]";
    }
}

static void imx_gpio_update_int(IMXGPIOState *s)
{
    if (s->has_upper_pin_irq) {
        qemu_set_irq(s->irq[0], (s->isr & s->imr & 0x0000FFFF) ? 1 : 0);
        qemu_set_irq(s->irq[1], (s->isr & s->imr & 0xFFFF0000) ? 1 : 0);
    } else {
        qemu_set_irq(s->irq[0], (s->isr & s->imr) ? 1 : 0);
    }
}

static void imx_gpio_set_int_line(IMXGPIOState *s, int line, IMXGPIOLevel level)
{
    /* if this signal isn't configured as an input signal, nothing to do */
    if (!extract32(s->gdir, line, 1)) {
        return;
    }

    /* When set, EDGE_SEL overrides the ICR config */
    if (extract32(s->edge_sel, line, 1)) {
        /* we detect interrupt on rising and falling edge */
        if (extract32(s->psr, line, 1) != level) {
            /* level changed */
            s->isr = deposit32(s->isr, line, 1, 1);
        }
    } else if (extract64(s->icr, 2*line + 1, 1)) {
        /* interrupt is edge sensitive */
        if (extract32(s->psr, line, 1) != level) {
            /* level changed */
            if (extract64(s->icr, 2*line, 1) != level) {
                s->isr = deposit32(s->isr, line, 1, 1);
            }
        }
    } else {
        /* interrupt is level sensitive */
        if (extract64(s->icr, 2*line, 1) == level) {
            s->isr = deposit32(s->isr, line, 1, 1);
        }
    }
}

static void imx_gpio_set(void *opaque, int line, int level)
{
    IMXGPIOState *s = IMX_GPIO(opaque);
    IMXGPIOLevel imx_level = level ? IMX_GPIO_LEVEL_HIGH : IMX_GPIO_LEVEL_LOW;

    imx_gpio_set_int_line(s, line, imx_level);

    /* this is an input signal, so set PSR */
    s->psr = deposit32(s->psr, line, 1, imx_level);

    imx_gpio_update_int(s);
}

static void imx_gpio_set_all_int_lines(IMXGPIOState *s)
{
    int i;

    for (i = 0; i < IMX_GPIO_PIN_COUNT; i++) {
        IMXGPIOLevel imx_level = extract32(s->psr, i, 1);
        imx_gpio_set_int_line(s, i, imx_level);
    }

    imx_gpio_update_int(s);
}

static inline void imx_gpio_set_all_output_lines(IMXGPIOState *s)
{
    int i;

    for (i = 0; i < IMX_GPIO_PIN_COUNT; i++) {
        /*
         * if the line is set as output, then forward the line
         * level to its user.
         */
        if (extract32(s->gdir, i, 1) && s->output[i]) {
            qemu_set_irq(s->output[i], extract32(s->dr, i, 1));
        }
    }
}

static uint64_t imx_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXGPIOState *s = IMX_GPIO(opaque);
    uint32_t reg_value = 0;

    switch (offset) {
    case DR_ADDR:
        /*
         * depending on the "line" configuration, the bit values
         * are coming either from DR or PSR
         */
        reg_value = (s->dr & s->gdir) | (s->psr & ~s->gdir);
        break;

    case GDIR_ADDR:
        reg_value = s->gdir;
        break;

    case PSR_ADDR:
        reg_value = s->psr & ~s->gdir;
        break;

    case ICR1_ADDR:
        reg_value = extract64(s->icr, 0, 32);
        break;

    case ICR2_ADDR:
        reg_value = extract64(s->icr, 32, 32);
        break;

    case IMR_ADDR:
        reg_value = s->imr;
        break;

    case ISR_ADDR:
        reg_value = s->isr;
        break;

    case EDGE_SEL_ADDR:
        if (s->has_edge_sel) {
            reg_value = s->edge_sel;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: EDGE_SEL register not "
                          "present on this version of GPIO device\n",
                          TYPE_IMX_GPIO, __func__);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_GPIO, __func__, offset);
        break;
    }

    DPRINTF("(%s) = 0x%" PRIx32 "\n", imx_gpio_reg_name(offset), reg_value);

    return reg_value;
}

static void imx_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    IMXGPIOState *s = IMX_GPIO(opaque);

    DPRINTF("(%s, value = 0x%" PRIx32 ")\n", imx_gpio_reg_name(offset),
            (uint32_t)value);

    switch (offset) {
    case DR_ADDR:
        s->dr = value;
        imx_gpio_set_all_output_lines(s);
        break;

    case GDIR_ADDR:
        s->gdir = value;
        imx_gpio_set_all_output_lines(s);
        imx_gpio_set_all_int_lines(s);
        break;

    case ICR1_ADDR:
        s->icr = deposit64(s->icr, 0, 32, value);
        imx_gpio_set_all_int_lines(s);
        break;

    case ICR2_ADDR:
        s->icr = deposit64(s->icr, 32, 32, value);
        imx_gpio_set_all_int_lines(s);
        break;

    case IMR_ADDR:
        s->imr = value;
        imx_gpio_update_int(s);
        break;

    case ISR_ADDR:
        s->isr &= ~value;
        imx_gpio_set_all_int_lines(s);
        break;

    case EDGE_SEL_ADDR:
        if (s->has_edge_sel) {
            s->edge_sel = value;
            imx_gpio_set_all_int_lines(s);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: EDGE_SEL register not "
                          "present on this version of GPIO device\n",
                          TYPE_IMX_GPIO, __func__);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_GPIO, __func__, offset);
        break;
    }

    return;
}

static const MemoryRegionOps imx_gpio_ops = {
    .read = imx_gpio_read,
    .write = imx_gpio_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_imx_gpio = {
    .name = TYPE_IMX_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(dr, IMXGPIOState),
        VMSTATE_UINT32(gdir, IMXGPIOState),
        VMSTATE_UINT32(psr, IMXGPIOState),
        VMSTATE_UINT64(icr, IMXGPIOState),
        VMSTATE_UINT32(imr, IMXGPIOState),
        VMSTATE_UINT32(isr, IMXGPIOState),
        VMSTATE_BOOL(has_edge_sel, IMXGPIOState),
        VMSTATE_UINT32(edge_sel, IMXGPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static Property imx_gpio_properties[] = {
    DEFINE_PROP_BOOL("has-edge-sel", IMXGPIOState, has_edge_sel, true),
    DEFINE_PROP_BOOL("has-upper-pin-irq", IMXGPIOState, has_upper_pin_irq,
                     false),
    DEFINE_PROP_END_OF_LIST(),
};

static void imx_gpio_reset(DeviceState *dev)
{
    IMXGPIOState *s = IMX_GPIO(dev);

    s->dr       = 0;
    s->gdir     = 0;
    s->psr      = 0;
    s->icr      = 0;
    s->imr      = 0;
    s->isr      = 0;
    s->edge_sel = 0;

    imx_gpio_set_all_output_lines(s);
    imx_gpio_update_int(s);
}

static void imx_gpio_realize(DeviceState *dev, Error **errp)
{
    IMXGPIOState *s = IMX_GPIO(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &imx_gpio_ops, s,
                          TYPE_IMX_GPIO, IMX_GPIO_MEM_SIZE);

    qdev_init_gpio_in(DEVICE(s), imx_gpio_set, IMX_GPIO_PIN_COUNT);
    qdev_init_gpio_out(DEVICE(s), s->output, IMX_GPIO_PIN_COUNT);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq[0]);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq[1]);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void imx_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx_gpio_realize;
    dc->reset = imx_gpio_reset;
    device_class_set_props(dc, imx_gpio_properties);
    dc->vmsd = &vmstate_imx_gpio;
    dc->desc = "i.MX GPIO controller";
}

static const TypeInfo imx_gpio_info = {
    .name = TYPE_IMX_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXGPIOState),
    .class_init = imx_gpio_class_init,
};

static void imx_gpio_register_types(void)
{
    type_register_static(&imx_gpio_info);
}

type_init(imx_gpio_register_types)
