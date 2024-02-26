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
#include "hw/gpio/bcm2838_gpio.h"

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

#define BYTES_IN_WORD        4

static uint64_t bcm2838_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t value = 0;

    qemu_log_mask(LOG_UNIMP, "%s: %s: not implemented for %"HWADDR_PRIx"\n",
                  TYPE_BCM2838_GPIO, __func__, offset);

    return value;
}

static void bcm2838_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: %s: not implemented for %"HWADDR_PRIx"\n",
                  TYPE_BCM2838_GPIO, __func__, offset);
}

static void bcm2838_gpio_reset(DeviceState *dev)
{
    BCM2838GpioState *s = BCM2838_GPIO(dev);

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

    memory_region_init_io(&s->iomem, obj, &bcm2838_gpio_ops, s,
                          "bcm2838_gpio", BCM2838_GPIO_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_out(dev, s->out, BCM2838_GPIO_NUM);
}

static void bcm2838_gpio_realize(DeviceState *dev, Error **errp)
{
    /* Temporary stub. Do nothing */
}

static void bcm2838_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_bcm2838_gpio;
    dc->realize = &bcm2838_gpio_realize;
    dc->reset = &bcm2838_gpio_reset;
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
