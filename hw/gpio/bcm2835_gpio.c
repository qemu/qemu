/*
 * Raspberry Pi (BCM2835) GPIO Controller
 *
 * Copyright (c) 2017 Antfield SAS
 *
 * Authors:
 *  Clement Deschamps <clement.deschamps@antfield.fr>
 *  Luc Michel <luc.michel@antfield.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/sd/sd.h"
#include "hw/gpio/bcm2835_gpio.h"
#include "hw/irq.h"

#define GPFSEL0   0x00
#define GPFSEL1   0x04
#define GPFSEL2   0x08
#define GPFSEL3   0x0C
#define GPFSEL4   0x10
#define GPFSEL5   0x14
#define GPSET0    0x1C
#define GPSET1    0x20
#define GPCLR0    0x28
#define GPCLR1    0x2C
#define GPLEV0    0x34
#define GPLEV1    0x38
#define GPEDS0    0x40
#define GPEDS1    0x44
#define GPREN0    0x4C
#define GPREN1    0x50
#define GPFEN0    0x58
#define GPFEN1    0x5C
#define GPHEN0    0x64
#define GPHEN1    0x68
#define GPLEN0    0x70
#define GPLEN1    0x74
#define GPAREN0   0x7C
#define GPAREN1   0x80
#define GPAFEN0   0x88
#define GPAFEN1   0x8C
#define GPPUD     0x94
#define GPPUDCLK0 0x98
#define GPPUDCLK1 0x9C

static uint32_t gpfsel_get(BCM2835GpioState *s, uint8_t reg)
{
    int i;
    uint32_t value = 0;
    for (i = 0; i < 10; i++) {
        uint32_t index = 10 * reg + i;
        if (index < sizeof(s->fsel)) {
            value |= (s->fsel[index] & 0x7) << (3 * i);
        }
    }
    return value;
}

static void gpfsel_set(BCM2835GpioState *s, uint8_t reg, uint32_t value)
{
    int i;
    for (i = 0; i < 10; i++) {
        uint32_t index = 10 * reg + i;
        if (index < sizeof(s->fsel)) {
            int fsel = (value >> (3 * i)) & 0x7;
            s->fsel[index] = fsel;
        }
    }

    /* SD controller selection (48-53) */
    if (s->sd_fsel != 0
            && (s->fsel[48] == 0) /* SD_CLK_R */
            && (s->fsel[49] == 0) /* SD_CMD_R */
            && (s->fsel[50] == 0) /* SD_DATA0_R */
            && (s->fsel[51] == 0) /* SD_DATA1_R */
            && (s->fsel[52] == 0) /* SD_DATA2_R */
            && (s->fsel[53] == 0) /* SD_DATA3_R */
            ) {
        /* SDHCI controller selected */
        sdbus_reparent_card(s->sdbus_sdhost, s->sdbus_sdhci);
        s->sd_fsel = 0;
    } else if (s->sd_fsel != 4
            && (s->fsel[48] == 4) /* SD_CLK_R */
            && (s->fsel[49] == 4) /* SD_CMD_R */
            && (s->fsel[50] == 4) /* SD_DATA0_R */
            && (s->fsel[51] == 4) /* SD_DATA1_R */
            && (s->fsel[52] == 4) /* SD_DATA2_R */
            && (s->fsel[53] == 4) /* SD_DATA3_R */
            ) {
        /* SDHost controller selected */
        sdbus_reparent_card(s->sdbus_sdhci, s->sdbus_sdhost);
        s->sd_fsel = 4;
    }
}

static int gpfsel_is_out(BCM2835GpioState *s, int index)
{
    if (index >= 0 && index < 54) {
        return s->fsel[index] == 1;
    }
    return 0;
}

static void gpset(BCM2835GpioState *s,
        uint32_t val, uint8_t start, uint8_t count, uint32_t *lev)
{
    uint32_t changes = val & ~*lev;
    uint32_t cur = 1;

    int i;
    for (i = 0; i < count; i++) {
        if ((changes & cur) && (gpfsel_is_out(s, start + i))) {
            qemu_set_irq(s->out[start + i], 1);
        }
        cur <<= 1;
    }

    *lev |= val;
}

static void gpclr(BCM2835GpioState *s,
        uint32_t val, uint8_t start, uint8_t count, uint32_t *lev)
{
    uint32_t changes = val & *lev;
    uint32_t cur = 1;

    int i;
    for (i = 0; i < count; i++) {
        if ((changes & cur) && (gpfsel_is_out(s, start + i))) {
            qemu_set_irq(s->out[start + i], 0);
        }
        cur <<= 1;
    }

    *lev &= ~val;
}

static uint64_t bcm2835_gpio_read(void *opaque, hwaddr offset,
        unsigned size)
{
    BCM2835GpioState *s = (BCM2835GpioState *)opaque;

    switch (offset) {
    case GPFSEL0:
    case GPFSEL1:
    case GPFSEL2:
    case GPFSEL3:
    case GPFSEL4:
    case GPFSEL5:
        return gpfsel_get(s, offset / 4);
    case GPSET0:
    case GPSET1:
        /* Write Only */
        return 0;
    case GPCLR0:
    case GPCLR1:
        /* Write Only */
        return 0;
    case GPLEV0:
        return s->lev0;
    case GPLEV1:
        return s->lev1;
    case GPEDS0:
    case GPEDS1:
    case GPREN0:
    case GPREN1:
    case GPFEN0:
    case GPFEN1:
    case GPHEN0:
    case GPHEN1:
    case GPLEN0:
    case GPLEN1:
    case GPAREN0:
    case GPAREN1:
    case GPAFEN0:
    case GPAFEN1:
    case GPPUD:
    case GPPUDCLK0:
    case GPPUDCLK1:
        /* Not implemented */
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                __func__, offset);
        break;
    }

    return 0;
}

static void bcm2835_gpio_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    BCM2835GpioState *s = (BCM2835GpioState *)opaque;

    switch (offset) {
    case GPFSEL0:
    case GPFSEL1:
    case GPFSEL2:
    case GPFSEL3:
    case GPFSEL4:
    case GPFSEL5:
        gpfsel_set(s, offset / 4, value);
        break;
    case GPSET0:
        gpset(s, value, 0, 32, &s->lev0);
        break;
    case GPSET1:
        gpset(s, value, 32, 22, &s->lev1);
        break;
    case GPCLR0:
        gpclr(s, value, 0, 32, &s->lev0);
        break;
    case GPCLR1:
        gpclr(s, value, 32, 22, &s->lev1);
        break;
    case GPLEV0:
    case GPLEV1:
        /* Read Only */
        break;
    case GPEDS0:
    case GPEDS1:
    case GPREN0:
    case GPREN1:
    case GPFEN0:
    case GPFEN1:
    case GPHEN0:
    case GPHEN1:
    case GPLEN0:
    case GPLEN1:
    case GPAREN0:
    case GPAREN1:
    case GPAFEN0:
    case GPAFEN1:
    case GPPUD:
    case GPPUDCLK0:
    case GPPUDCLK1:
        /* Not implemented */
        break;
    default:
        goto err_out;
    }
    return;

err_out:
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
            __func__, offset);
}

static void bcm2835_gpio_reset(DeviceState *dev)
{
    BCM2835GpioState *s = BCM2835_GPIO(dev);

    int i;
    for (i = 0; i < 6; i++) {
        gpfsel_set(s, i, 0);
    }

    s->sd_fsel = 0;

    /* SDHCI is selected by default */
    sdbus_reparent_card(&s->sdbus, s->sdbus_sdhci);

    s->lev0 = 0;
    s->lev1 = 0;
}

static const MemoryRegionOps bcm2835_gpio_ops = {
    .read = bcm2835_gpio_read,
    .write = bcm2835_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_gpio = {
    .name = "bcm2835_gpio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(fsel, BCM2835GpioState, 54),
        VMSTATE_UINT32(lev0, BCM2835GpioState),
        VMSTATE_UINT32(lev1, BCM2835GpioState),
        VMSTATE_UINT8(sd_fsel, BCM2835GpioState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_gpio_init(Object *obj)
{
    BCM2835GpioState *s = BCM2835_GPIO(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_SD_BUS, DEVICE(s), "sd-bus");

    memory_region_init_io(&s->iomem, obj,
            &bcm2835_gpio_ops, s, "bcm2835_gpio", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_out(dev, s->out, 54);
}

static void bcm2835_gpio_realize(DeviceState *dev, Error **errp)
{
    BCM2835GpioState *s = BCM2835_GPIO(dev);
    Object *obj;

    obj = object_property_get_link(OBJECT(dev), "sdbus-sdhci", &error_abort);
    s->sdbus_sdhci = SD_BUS(obj);

    obj = object_property_get_link(OBJECT(dev), "sdbus-sdhost", &error_abort);
    s->sdbus_sdhost = SD_BUS(obj);
}

static void bcm2835_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_bcm2835_gpio;
    dc->realize = &bcm2835_gpio_realize;
    dc->reset = &bcm2835_gpio_reset;
}

static const TypeInfo bcm2835_gpio_info = {
    .name          = TYPE_BCM2835_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835GpioState),
    .instance_init = bcm2835_gpio_init,
    .class_init    = bcm2835_gpio_class_init,
};

static void bcm2835_gpio_register_types(void)
{
    type_register_static(&bcm2835_gpio_info);
}

type_init(bcm2835_gpio_register_types)
