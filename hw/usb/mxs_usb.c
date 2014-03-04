/*
 * mxs_usb.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

/*
 * Implements the USB block of the mxs. This is just a case of
 * instantiating a ehci block, and have a few read only registers
 * for mxs specific bits
 */
#include "hw/sysbus.h"
#include "hw/arm/mxs.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/qdev.h"

#define D(w)

enum {
    USB_MAX = 256 / 4,
};

typedef struct mxs_usb_state {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t r[USB_MAX];
    qemu_irq irq_dma, irq_error;

    EHCIState ehci;
} mxs_usb_state;

static uint64_t mxs_usb_read(
        void *opaque, hwaddr offset, unsigned size)
{
    mxs_usb_state *s = (mxs_usb_state *) opaque;
    uint32_t res = 0;

    D(printf("%s %04x (%d) = ", __func__, (int)offset, size);)
    switch (offset >> 2) {
        case 0 ... USB_MAX:
            res = s->r[offset >> 2];
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
    D(printf("%08x\n", res);)

    return res;
}

static void mxs_usb_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    mxs_usb_state *s = (mxs_usb_state *) opaque;

    D(printf("%s %04x %08x(%d)\n", __func__, (int)offset, (int)value, size);)
    switch (offset) {
        case 0 ... USB_MAX:
            s->r[offset] = value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
}

static const MemoryRegionOps mxs_usb_ops = {
    .read = mxs_usb_read,
    .write = mxs_usb_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int mxs_usb_init(SysBusDevice *dev)
{
    mxs_usb_state *s = OBJECT_CHECK(mxs_usb_state, dev, "mxs_usb");
    EHCIState *u = &s->ehci;

    memory_region_init_io(&s->iomem, OBJECT(s), &mxs_usb_ops, s,
            "mxs_usb", 0x100);

    s->r[0] = 0xe241fa05;
    s->r[0x04 >> 2] = 0x00000015;
    s->r[0x08 >> 2] = 0x10020001;
    s->r[0x0c >> 2] = 0x0000000b;
    s->r[0x10 >> 2] = 0x40060910;
    s->r[0x14 >> 2] = 0x00000710;

    u->capsbase = 0x100;
    u->opregbase = 0x140;
    // FIXME ?!?!?
//    u->dma = &dma_context_memory;

    usb_ehci_init(u, DEVICE(dev));
    sysbus_init_irq(dev, &u->irq);

    memory_region_add_subregion(&u->mem, 0x0, &s->iomem);
    sysbus_init_mmio(dev, &u->mem);

    D(printf("%s created bus %s\n", __func__, u->bus.qbus.name);)
#if 0
    /*
     * This is suposed to make companion ports that will support
     * slower speed devices (mouse/keyboard etc). It's inspired
     * from ehci/pci however it doesn't work, right now...
     */
    int i;
    for (i = 0; i < NB_PORTS; i += 2) {
        DeviceState * d = qdev_create(NULL, "sysbus-ohci");
        qdev_prop_set_string(d, "masterbus", u->bus.qbus.name);
        qdev_prop_set_uint32(d, "firstport", i);
        qdev_prop_set_uint32(d, "num-ports", 2);
        qdev_init_nofail(d);
        sysbus_connect_irq(SYS_BUS_DEVICE(d), 0, u->irq);
    }
#endif
    return 0;
}

static void mxs_usb_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = mxs_usb_init;
}

static TypeInfo mxs_usb_info = {
    .name          = "mxs_usb",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mxs_usb_state),
    .class_init    = mxs_usb_class_init,
};

static void mxs_usb_register(void)
{
    type_register_static(&mxs_usb_info);
}

type_init(mxs_usb_register)

#undef D
#define D(w)

enum {
    USBPHY_PWD = 0x0,
    USBPHY_TX = 0x1,
    USBPHY_RX = 0x2,
    USBPHY_CTRL = 0x3,
    USBPHY_MAX = 10,
};
typedef struct mxs_usbphy_state {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t r[USBPHY_MAX];
} mxs_usbphy_state;

static uint64_t mxs_usbphy_read(void *opaque, hwaddr offset,
        unsigned size)
{
    mxs_usbphy_state *s = (mxs_usbphy_state *) opaque;
    uint32_t res = 0;

    D(printf("%s %04x (%d) = ", __func__, (int)offset, size);)
    switch (offset >> 4) {
        case 0 ... USBPHY_MAX:
            res = s->r[offset >> 4];
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
    D(printf("%08x\n", res);)

    return res;
}

static void mxs_usbphy_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    mxs_usbphy_state *s = (mxs_usbphy_state *) opaque;
    uint32_t oldvalue = 0;

    D(printf("%s %04x %08x(%d) = ", __func__, (int)offset, (int)value, size);)
    switch (offset >> 4) {
        case 0 ... USBPHY_MAX:
            oldvalue = mxs_write(&s->r[offset >> 4], offset, value, size);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            return;
    }
    switch (offset >> 4) {
        case USBPHY_CTRL:
            if ((oldvalue ^ s->r[USBPHY_CTRL]) == 0x80000000
                    && !(oldvalue & 0x80000000)) {
                D(printf("%s reseting, anding clockgate\n", __func__);)
                s->r[USBPHY_CTRL] |= 0x40000000;
            }
            break;
    }
    D(printf("%08x\n", s->r[offset >> 4]);)
}


static const MemoryRegionOps mxs_usbphy_ops = {
    .read = mxs_usbphy_read,
    .write = mxs_usbphy_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int mxs_usbphy_init(SysBusDevice *dev)
{
    mxs_usbphy_state *s = OBJECT_CHECK(mxs_usbphy_state, dev, "mxs_usbphy");

    memory_region_init_io(&s->iomem, OBJECT(s), &mxs_usbphy_ops, s,
            "mxs_usbphy", 0x2000);
    sysbus_init_mmio(dev, &s->iomem);

    s->r[USBPHY_PWD] = 0x00860607;
    s->r[USBPHY_PWD] = 0x00860607;
    s->r[USBPHY_CTRL] = 0xc0000000;
    return 0;
}


static void mxs_usbphy_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = mxs_usbphy_init;
}

static TypeInfo usbphy_info = {
    .name          = "mxs_usbphy",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mxs_usbphy_state),
    .class_init    = mxs_usbphy_class_init,
};

static void mxs_usbphy_register(void)
{
    type_register_static(&usbphy_info);
}

type_init(mxs_usbphy_register)

