/*
 * imx23_digctl.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

/*
 * This module implements a very basic IO block for the digctl of the imx23
 * Basically there is no real logic, just constant registers return, the most
 * used one bing the "chip id" that is used by the various linux drivers
 * to differentiate between imx23 and 28.
 *
 * The module consists mostly of read/write registers that the bootloader and
 * kernel are quite happy to 'set' to whatever value they believe they set...
 */

#include "hw/sysbus.h"
#include "hw/arm/mxs.h"

enum {
    HW_DIGCTL_RAMCTL = 0x3,
    HW_DIGCTL_CHIPID = 0x31,
};

typedef struct imx23_digctl_state {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t	reg[0x2000 / 4];
} imx23_digctl_state;

static uint64_t imx23_digctl_read(
        void *opaque, hwaddr offset, unsigned size)
{
    imx23_digctl_state *s = (imx23_digctl_state *)opaque;
    uint32_t res = 0;

    switch (offset >> 4) {
    	case 0 ... 0x2000/4:
    		res = s->reg[offset >> 4];
    		break;
        default:
	        qemu_log_mask(LOG_GUEST_ERROR,
	        		"%s: bad offset 0x%x\n", __func__, (int)offset);
        	return 0;
    }
    return res;
}

static void imx23_digctl_write(
        void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    imx23_digctl_state *s = (imx23_digctl_state *) opaque;
    uint32_t * dst = NULL;

    switch (offset >> 4) {
        case 0 ... 0x2000 / 4:
            dst = &s->reg[(offset >> 4)];
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            return;
    }
    if (!dst) {
        return;
    }
    mxs_write(dst, offset, value, size);
}

static const MemoryRegionOps imx23_digctl_ops = {
    .read = imx23_digctl_read,
    .write = imx23_digctl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int imx23_digctl_init(SysBusDevice *dev)
{
    imx23_digctl_state *s = OBJECT_CHECK(imx23_digctl_state, dev, "imx23_digctl");

    memory_region_init_io(&s->iomem, OBJECT(s), &imx23_digctl_ops, s,
            "imx23_digctl", 0x2000);
    sysbus_init_mmio(dev, &s->iomem);
    s->reg[HW_DIGCTL_RAMCTL] = 0x6d676953;  /* default reset value */
    s->reg[HW_DIGCTL_CHIPID] = 0x37800000;  /* i.mX233 */
    return 0;
}

static void imx23_digctl_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = imx23_digctl_init;
}

static TypeInfo digctl_info = {
    .name          = "imx23_digctl",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(imx23_digctl_state),
    .class_init    = imx23_digctl_class_init,
};

static void imx23_digctl_register(void)
{
    type_register_static(&digctl_info);
}

type_init(imx23_digctl_register)
