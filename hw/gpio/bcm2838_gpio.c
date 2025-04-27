/*
 * Raspberry Pi (BCM2838) GPIO Controller
 * This implementation is based on bcm2835_gpio (hw/gpio/bcm2835_gpio.c)
 *
 * Copyright (c) 2022 Auriga LLC
 *
 * Authors:
 *  Lotosh, Aleksey <aleksey.lotosh@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/sd/sd.h"
#include "hw/gpio/bcm2838_gpio.h"
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

#define GPIO_PUP_PDN_CNTRL_REG0 0xE4
#define GPIO_PUP_PDN_CNTRL_REG1 0xE8
#define GPIO_PUP_PDN_CNTRL_REG2 0xEC
#define GPIO_PUP_PDN_CNTRL_REG3 0xF0

#define RESET_VAL_CNTRL_REG0 0xAAA95555
#define RESET_VAL_CNTRL_REG1 0xA0AAAAAA
#define RESET_VAL_CNTRL_REG2 0x50AAA95A
#define RESET_VAL_CNTRL_REG3 0x00055555

#define NUM_FSELN_IN_GPFSELN 10
#define NUM_BITS_FSELN       3
#define MASK_FSELN           0x7

#define BYTES_IN_WORD        4

/* bcm,function property */
#define BCM2838_FSEL_GPIO_IN    0
#define BCM2838_FSEL_GPIO_OUT   1
#define BCM2838_FSEL_ALT5       2
#define BCM2838_FSEL_ALT4       3
#define BCM2838_FSEL_ALT0       4
#define BCM2838_FSEL_ALT1       5
#define BCM2838_FSEL_ALT2       6
#define BCM2838_FSEL_ALT3       7

static uint32_t gpfsel_get(BCM2838GpioState *s, uint8_t reg)
{
    int i;
    uint32_t value = 0;
    for (i = 0; i < NUM_FSELN_IN_GPFSELN; i++) {
        uint32_t index = NUM_FSELN_IN_GPFSELN * reg + i;
        if (index < sizeof(s->fsel)) {
            value |= (s->fsel[index] & MASK_FSELN) << (NUM_BITS_FSELN * i);
        }
    }
    return value;
}

static void gpfsel_set(BCM2838GpioState *s, uint8_t reg, uint32_t value)
{
    int i;
    for (i = 0; i < NUM_FSELN_IN_GPFSELN; i++) {
        uint32_t index = NUM_FSELN_IN_GPFSELN * reg + i;
        if (index < sizeof(s->fsel)) {
            int fsel = (value >> (NUM_BITS_FSELN * i)) & MASK_FSELN;
            s->fsel[index] = fsel;
        }
    }

    /* SD controller selection (48-53) */
    if (s->sd_fsel != BCM2838_FSEL_GPIO_IN
        && (s->fsel[48] == BCM2838_FSEL_GPIO_IN)
        && (s->fsel[49] == BCM2838_FSEL_GPIO_IN)
        && (s->fsel[50] == BCM2838_FSEL_GPIO_IN)
        && (s->fsel[51] == BCM2838_FSEL_GPIO_IN)
        && (s->fsel[52] == BCM2838_FSEL_GPIO_IN)
        && (s->fsel[53] == BCM2838_FSEL_GPIO_IN)
       ) {
        /* SDHCI controller selected */
        sdbus_reparent_card(s->sdbus_sdhost, s->sdbus_sdhci);
        s->sd_fsel = BCM2838_FSEL_GPIO_IN;
    } else if (s->sd_fsel != BCM2838_FSEL_ALT0
               && (s->fsel[48] == BCM2838_FSEL_ALT0) /* SD_CLK_R */
               && (s->fsel[49] == BCM2838_FSEL_ALT0) /* SD_CMD_R */
               && (s->fsel[50] == BCM2838_FSEL_ALT0) /* SD_DATA0_R */
               && (s->fsel[51] == BCM2838_FSEL_ALT0) /* SD_DATA1_R */
               && (s->fsel[52] == BCM2838_FSEL_ALT0) /* SD_DATA2_R */
               && (s->fsel[53] == BCM2838_FSEL_ALT0) /* SD_DATA3_R */
              ) {
        /* SDHost controller selected */
        sdbus_reparent_card(s->sdbus_sdhci, s->sdbus_sdhost);
        s->sd_fsel = BCM2838_FSEL_ALT0;
    }
}

static int gpfsel_is_out(BCM2838GpioState *s, int index)
{
    if (index >= 0 && index < BCM2838_GPIO_NUM) {
        return s->fsel[index] == 1;
    }
    return 0;
}

static void gpset(BCM2838GpioState *s, uint32_t val, uint8_t start,
                  uint8_t count, uint32_t *lev)
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

static void gpclr(BCM2838GpioState *s, uint32_t val, uint8_t start,
                  uint8_t count, uint32_t *lev)
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

static uint64_t bcm2838_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2838GpioState *s = (BCM2838GpioState *)opaque;
    uint64_t value = 0;

    switch (offset) {
    case GPFSEL0:
    case GPFSEL1:
    case GPFSEL2:
    case GPFSEL3:
    case GPFSEL4:
    case GPFSEL5:
        value = gpfsel_get(s, offset / BYTES_IN_WORD);
        break;
    case GPSET0:
    case GPSET1:
    case GPCLR0:
    case GPCLR1:
        /* Write Only */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %s: Attempt reading from write only"
                      " register. 0x%"PRIx64" will be returned."
                      " Address 0x%"HWADDR_PRIx", size %u\n",
                      TYPE_BCM2838_GPIO, __func__, value, offset, size);
        break;
    case GPLEV0:
        value = s->lev0;
        break;
    case GPLEV1:
        value = s->lev1;
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
        /* Not implemented */
        qemu_log_mask(LOG_UNIMP, "%s: %s: not implemented for %"HWADDR_PRIx"\n",
                      TYPE_BCM2838_GPIO, __func__, offset);
        break;
    case GPIO_PUP_PDN_CNTRL_REG0:
    case GPIO_PUP_PDN_CNTRL_REG1:
    case GPIO_PUP_PDN_CNTRL_REG2:
    case GPIO_PUP_PDN_CNTRL_REG3:
        value = s->pup_cntrl_reg[(offset - GPIO_PUP_PDN_CNTRL_REG0)
                                 / sizeof(s->pup_cntrl_reg[0])];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %s: bad offset %"HWADDR_PRIx"\n",
                      TYPE_BCM2838_GPIO, __func__, offset);
        break;
    }

    return value;
}

static void bcm2838_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    BCM2838GpioState *s = (BCM2838GpioState *)opaque;

    switch (offset) {
    case GPFSEL0:
    case GPFSEL1:
    case GPFSEL2:
    case GPFSEL3:
    case GPFSEL4:
    case GPFSEL5:
        gpfsel_set(s, offset / BYTES_IN_WORD, value);
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
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %s: Attempt writing 0x%"PRIx64""
                      " to read only register. Ignored."
                      " Address 0x%"HWADDR_PRIx", size %u\n",
                      TYPE_BCM2838_GPIO, __func__, value, offset, size);
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
        /* Not implemented */
        qemu_log_mask(LOG_UNIMP, "%s: %s: not implemented for %"HWADDR_PRIx"\n",
                      TYPE_BCM2838_GPIO, __func__, offset);
        break;
    case GPIO_PUP_PDN_CNTRL_REG0:
    case GPIO_PUP_PDN_CNTRL_REG1:
    case GPIO_PUP_PDN_CNTRL_REG2:
    case GPIO_PUP_PDN_CNTRL_REG3:
        s->pup_cntrl_reg[(offset - GPIO_PUP_PDN_CNTRL_REG0)
                         / sizeof(s->pup_cntrl_reg[0])] = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: %s: bad offset %"HWADDR_PRIx"\n",
                  TYPE_BCM2838_GPIO, __func__, offset);
    }
}

static void bcm2838_gpio_reset(DeviceState *dev)
{
    BCM2838GpioState *s = BCM2838_GPIO(dev);

    memset(s->fsel, 0, sizeof(s->fsel));

    s->sd_fsel = 0;

    /* SDHCI is selected by default */
    sdbus_reparent_card(&s->sdbus, s->sdbus_sdhci);

    s->lev0 = 0;
    s->lev1 = 0;

    memset(s->fsel, 0, sizeof(s->fsel));

    s->pup_cntrl_reg[0] = RESET_VAL_CNTRL_REG0;
    s->pup_cntrl_reg[1] = RESET_VAL_CNTRL_REG1;
    s->pup_cntrl_reg[2] = RESET_VAL_CNTRL_REG2;
    s->pup_cntrl_reg[3] = RESET_VAL_CNTRL_REG3;
}

static const MemoryRegionOps bcm2838_gpio_ops = {
    .read = bcm2838_gpio_read,
    .write = bcm2838_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2838_gpio = {
    .name = "bcm2838_gpio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(fsel, BCM2838GpioState, BCM2838_GPIO_NUM),
        VMSTATE_UINT32(lev0, BCM2838GpioState),
        VMSTATE_UINT32(lev1, BCM2838GpioState),
        VMSTATE_UINT8(sd_fsel, BCM2838GpioState),
        VMSTATE_UINT32_ARRAY(pup_cntrl_reg, BCM2838GpioState,
                             GPIO_PUP_PDN_CNTRL_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2838_gpio_init(Object *obj)
{
    BCM2838GpioState *s = BCM2838_GPIO(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_SD_BUS, DEVICE(s), "sd-bus");

    memory_region_init_io(&s->iomem, obj, &bcm2838_gpio_ops, s,
                          "bcm2838_gpio", BCM2838_GPIO_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_out(dev, s->out, BCM2838_GPIO_NUM);
}

static void bcm2838_gpio_realize(DeviceState *dev, Error **errp)
{
    BCM2838GpioState *s = BCM2838_GPIO(dev);
    Object *obj;

    obj = object_property_get_link(OBJECT(dev), "sdbus-sdhci", &error_abort);
    s->sdbus_sdhci = SD_BUS(obj);

    obj = object_property_get_link(OBJECT(dev), "sdbus-sdhost", &error_abort);
    s->sdbus_sdhost = SD_BUS(obj);
}

static void bcm2838_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_bcm2838_gpio;
    dc->realize = &bcm2838_gpio_realize;
    device_class_set_legacy_reset(dc, bcm2838_gpio_reset);
}

static const TypeInfo bcm2838_gpio_info = {
    .name          = TYPE_BCM2838_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2838GpioState),
    .instance_init = bcm2838_gpio_init,
    .class_init    = bcm2838_gpio_class_init,
};

static void bcm2838_gpio_register_types(void)
{
    type_register_static(&bcm2838_gpio_info);
}

type_init(bcm2838_gpio_register_types)
