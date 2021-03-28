/*
 * i.MX USB PHY
 *
 * Copyright (c) 2020 Guenter Roeck <linux@roeck-us.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * We need to implement basic reset control in the PHY control register.
 * For everything else, it is sufficient to set whatever is written.
 */

#include "qemu/osdep.h"
#include "hw/usb/imx-usb-phy.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static const VMStateDescription vmstate_imx_usbphy = {
    .name = TYPE_IMX_USBPHY,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(usbphy, IMXUSBPHYState, USBPHY_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void imx_usbphy_softreset(IMXUSBPHYState *s)
{
    s->usbphy[USBPHY_PWD] = 0x001e1c00;
    s->usbphy[USBPHY_TX] = 0x10060607;
    s->usbphy[USBPHY_RX] = 0x00000000;
    s->usbphy[USBPHY_CTRL] = 0xc0200000;
}

static void imx_usbphy_reset(DeviceState *dev)
{
    IMXUSBPHYState *s = IMX_USBPHY(dev);

    s->usbphy[USBPHY_STATUS] = 0x00000000;
    s->usbphy[USBPHY_DEBUG] = 0x7f180000;
    s->usbphy[USBPHY_DEBUG0_STATUS] = 0x00000000;
    s->usbphy[USBPHY_DEBUG1] = 0x00001000;
    s->usbphy[USBPHY_VERSION] = 0x04020000;

    imx_usbphy_softreset(s);
}

static uint64_t imx_usbphy_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXUSBPHYState *s = (IMXUSBPHYState *)opaque;
    uint32_t index = offset >> 2;
    uint32_t value;

    switch (index) {
    case USBPHY_PWD_SET:
    case USBPHY_TX_SET:
    case USBPHY_RX_SET:
    case USBPHY_CTRL_SET:
    case USBPHY_DEBUG_SET:
    case USBPHY_DEBUG1_SET:
        /*
         * All REG_NAME_SET register access are in fact targeting the
         * REG_NAME register.
         */
        value = s->usbphy[index - 1];
        break;
    case USBPHY_PWD_CLR:
    case USBPHY_TX_CLR:
    case USBPHY_RX_CLR:
    case USBPHY_CTRL_CLR:
    case USBPHY_DEBUG_CLR:
    case USBPHY_DEBUG1_CLR:
        /*
         * All REG_NAME_CLR register access are in fact targeting the
         * REG_NAME register.
         */
        value = s->usbphy[index - 2];
        break;
    case USBPHY_PWD_TOG:
    case USBPHY_TX_TOG:
    case USBPHY_RX_TOG:
    case USBPHY_CTRL_TOG:
    case USBPHY_DEBUG_TOG:
    case USBPHY_DEBUG1_TOG:
        /*
         * All REG_NAME_TOG register access are in fact targeting the
         * REG_NAME register.
         */
        value = s->usbphy[index - 3];
        break;
    default:
        value = s->usbphy[index];
        break;
    }
    return (uint64_t)value;
}

static void imx_usbphy_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    IMXUSBPHYState *s = (IMXUSBPHYState *)opaque;
    uint32_t index = offset >> 2;

    switch (index) {
    case USBPHY_CTRL:
        s->usbphy[index] = value;
        if (value & USBPHY_CTRL_SFTRST) {
            imx_usbphy_softreset(s);
        }
        break;
    case USBPHY_PWD:
    case USBPHY_TX:
    case USBPHY_RX:
    case USBPHY_STATUS:
    case USBPHY_DEBUG:
    case USBPHY_DEBUG1:
        s->usbphy[index] = value;
        break;
    case USBPHY_CTRL_SET:
        s->usbphy[index - 1] |= value;
        if (value & USBPHY_CTRL_SFTRST) {
            imx_usbphy_softreset(s);
        }
        break;
    case USBPHY_PWD_SET:
    case USBPHY_TX_SET:
    case USBPHY_RX_SET:
    case USBPHY_DEBUG_SET:
    case USBPHY_DEBUG1_SET:
        /*
         * All REG_NAME_SET register access are in fact targeting the
         * REG_NAME register. So we change the value of the REG_NAME
         * register, setting bits passed in the value.
         */
        s->usbphy[index - 1] |= value;
        break;
    case USBPHY_PWD_CLR:
    case USBPHY_TX_CLR:
    case USBPHY_RX_CLR:
    case USBPHY_CTRL_CLR:
    case USBPHY_DEBUG_CLR:
    case USBPHY_DEBUG1_CLR:
        /*
         * All REG_NAME_CLR register access are in fact targeting the
         * REG_NAME register. So we change the value of the REG_NAME
         * register, unsetting bits passed in the value.
         */
        s->usbphy[index - 2] &= ~value;
        break;
    case USBPHY_CTRL_TOG:
        s->usbphy[index - 3] ^= value;
        if ((value & USBPHY_CTRL_SFTRST) &&
            (s->usbphy[index - 3] & USBPHY_CTRL_SFTRST)) {
            imx_usbphy_softreset(s);
        }
        break;
    case USBPHY_PWD_TOG:
    case USBPHY_TX_TOG:
    case USBPHY_RX_TOG:
    case USBPHY_DEBUG_TOG:
    case USBPHY_DEBUG1_TOG:
        /*
         * All REG_NAME_TOG register access are in fact targeting the
         * REG_NAME register. So we change the value of the REG_NAME
         * register, toggling bits passed in the value.
         */
        s->usbphy[index - 3] ^= value;
        break;
    default:
        /* Other registers are read-only */
        break;
    }
}

static const struct MemoryRegionOps imx_usbphy_ops = {
    .read = imx_usbphy_read,
    .write = imx_usbphy_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx_usbphy_realize(DeviceState *dev, Error **errp)
{
    IMXUSBPHYState *s = IMX_USBPHY(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &imx_usbphy_ops, s,
                          "imx-usbphy", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void imx_usbphy_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = imx_usbphy_reset;
    dc->vmsd = &vmstate_imx_usbphy;
    dc->desc = "i.MX USB PHY Module";
    dc->realize = imx_usbphy_realize;
}

static const TypeInfo imx_usbphy_info = {
    .name          = TYPE_IMX_USBPHY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXUSBPHYState),
    .class_init    = imx_usbphy_class_init,
};

static void imx_usbphy_register_types(void)
{
    type_register_static(&imx_usbphy_info);
}

type_init(imx_usbphy_register_types)
