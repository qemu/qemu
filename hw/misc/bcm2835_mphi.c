/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "hw/sysbus.h"

#define TYPE_BCM2835_MPHI "bcm2835_mphi"
#define BCM2835_MPHI(obj) \
        OBJECT_CHECK(bcm2835_mphi_state, (obj), TYPE_BCM2835_MPHI)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t mphi_base;
    uint32_t mphi_ctrl;
    uint32_t mphi_outdda;
    uint32_t mphi_outddb;
    uint32_t mphi_intstat;

    qemu_irq irq;

} bcm2835_mphi_state;


static void bcm2835_mphi_update_irq(bcm2835_mphi_state *s)
{
    if (s->mphi_intstat) {
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, 0);
    }
}

static uint64_t bcm2835_mphi_read(void *opaque, hwaddr offset,
    unsigned size)
{
    bcm2835_mphi_state *s = (bcm2835_mphi_state *)opaque;
    uint32_t res = 0;

    assert(size == 4);

    switch (offset) {
    case 0x00:    /* mphi_base */
        res = s->mphi_base;
        break;
    case 0x28:    /* mphi_outdda */
        res = s->mphi_outdda;
        break;
    case 0x2c:    /* mphi_outddb */
        res = s->mphi_outddb;
        break;
    case 0x4c:    /* mphi_ctrl */
        res = s->mphi_ctrl;
        break;
    case 0x50:    /* mphi_intstat */
        res = s->mphi_intstat;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_mphi_read: Bad offset %x\n", (int)offset);
        res = 0;
        break;
    }

    return res;
}

static void bcm2835_mphi_write(void *opaque, hwaddr offset,
    uint64_t value, unsigned size)
{
    bcm2835_mphi_state *s = (bcm2835_mphi_state *)opaque;
    int set_irq = 0;

    assert(size == 4);

    switch (offset) {
    case 0x00:    /* mphi_base */
        s->mphi_base = value;
        break;
    case 0x28:    /* mphi_outdda */
        s->mphi_outdda = value;
        break;
    case 0x2c:    /* mphi_outddb */
        s->mphi_outddb = value;
        if (value & (1 << 29)) {
            /* Enable MPHI interrupt */
            s->mphi_intstat |= (1 << 16);
            set_irq = 1;
        }
        break;
    case 0x4c:    /* mphi_ctrl */
        s->mphi_ctrl &= ~(1 << 31);
        s->mphi_ctrl |= value & (1 << 31);

        s->mphi_ctrl &= ~(3 << 16);
        if (value & (1 << 16)) {
            s->mphi_ctrl |= (3 << 16);
        }

        break;
    case 0x50:    /* mphi_intstat */
        s->mphi_intstat &= ~value;
        set_irq = 1;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_mphi_write: Bad offset %x\n", (int)offset);
        break;
    }

    if (set_irq) {
        bcm2835_mphi_update_irq(s);
    }
}

static const MemoryRegionOps bcm2835_mphi_ops = {
    .read = bcm2835_mphi_read,
    .write = bcm2835_mphi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_mphi = {
    .name = TYPE_BCM2835_MPHI,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static int bcm2835_mphi_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    bcm2835_mphi_state *s = BCM2835_MPHI(dev);

    s->mphi_base = 0;
    s->mphi_ctrl = 0;
    s->mphi_outdda = 0;
    s->mphi_outddb = 0;
    s->mphi_intstat = 0;

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_mphi_ops, s,
        TYPE_BCM2835_MPHI, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    vmstate_register(dev, -1, &vmstate_bcm2835_mphi, s);

    sysbus_init_irq(sbd, &s->irq);

    return 0;
}

static void bcm2835_mphi_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = bcm2835_mphi_init;
}

static TypeInfo bcm2835_mphi_info = {
    .name          = TYPE_BCM2835_MPHI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(bcm2835_mphi_state),
    .class_init    = bcm2835_mphi_class_init,
};

static void bcm2835_mphi_register_types(void)
{
    type_register_static(&bcm2835_mphi_info);
}

type_init(bcm2835_mphi_register_types)
