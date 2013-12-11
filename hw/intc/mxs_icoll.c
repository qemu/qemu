/*
 * mxs_icoll.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

/*
 * This block implements the interrupt collector of the mxs
 * Currently no priority is handled, as linux doesn't use them anyway
 */

#include "hw/sysbus.h"
#include "hw/arm/mxs.h"

enum {
    ICOLL_VECTOR = 0,
    ICOLL_LEVELACK = 1,
    ICOLL_CTRL = 2,
    // 3, reserved?
    ICOLL_VBASE = 4,
    ICOLL_STAT = 7,

    ICOLL_REG_MAX,

    ICOLL_RAW0	= 0xa,
    ICOLL_RAW1,
    ICOLL_RAW2,
    ICOLL_RAW3,

    ICOLL_INT0 = 0x12,
    ICOLL_INT127 = 0x91,
};

typedef struct mxs_icoll_state {
    SysBusDevice busdev;
    MemoryRegion iomem;
    uint32_t	reg[ICOLL_REG_MAX];

    uint32_t	raised[4];
    uint32_t	fiq[4];
    uint32_t	irq[4];

    uint8_t	r[128];

    qemu_irq parent_irq;
    qemu_irq parent_fiq;
} mxs_icoll_state;

static void mxs_icoll_update(mxs_icoll_state *s)
{
    int fiq = 0, irq = 0;
    int i;

    for (i = 0; i < 4; i++) {
        int id = ffs(s->raised[i]);
        int vector = (i * 32) + id - 1;
        if (s->raised[i] & s->fiq[i]) {
            fiq++;
            s->reg[ICOLL_STAT] = vector;
            break;
        }
        if (s->raised[i] & s->irq[i]) {
            irq++;
            s->reg[ICOLL_STAT] = vector;
            break;
        }
    }
    qemu_set_irq(s->parent_irq, irq != 0);
    qemu_set_irq(s->parent_fiq, fiq != 0);
}

static void mxs_icoll_set_irq(void *opaque, int irq, int level)
{
    mxs_icoll_state *s = (mxs_icoll_state *) opaque;
    if (level)
        s->raised[(irq / 32)] |= 1 << (irq % 32);
    else
        s->raised[(irq / 32)] &= ~(1 << (irq % 32));
    mxs_icoll_update(s);
}

static uint64_t mxs_icoll_read(void *opaque, hwaddr offset, unsigned size)
{
    mxs_icoll_state *s = (mxs_icoll_state *) opaque;

    switch (offset >> 4) {
        case 0 ... ICOLL_REG_MAX:
            return s->reg[offset >> 4];
        case ICOLL_RAW0 ... ICOLL_RAW3:
            return s->raised[(offset >> 4) - ICOLL_RAW0];
        case ICOLL_INT0 ... ICOLL_INT127:
            return s->r[(offset >> 4) - ICOLL_INT0];
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
    return 0;
}

static void mxs_icoll_write(
        void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    mxs_icoll_state *s = (mxs_icoll_state *) opaque;
    uint32_t irqval, irqi = 0;
    uint32_t * dst = NULL;
    uint32_t oldvalue = 0;

    switch (offset >> 4) {
        case 0 ... ICOLL_REG_MAX:
            dst = s->reg + (offset >> 4);
            break;
        case ICOLL_INT0 ... ICOLL_INT127:
            irqi = (offset >> 4) - ICOLL_INT0;
            irqval = s->r[irqi];
            dst = &irqval;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: bad offset 0x%x\n", __func__, (int) offset);
            break;
    }
    if (!dst) {
        return;
    }
    oldvalue = mxs_write(dst, offset, value, size);

    switch (offset >> 4) {
        case ICOLL_CTRL:
            if ((oldvalue ^ s->r[ICOLL_CTRL]) == 0x80000000
                    && !(oldvalue & 0x80000000)) {
                //	printf("%s reseting, anding clockgate\n", __func__);
                s->r[ICOLL_CTRL] |= 0x40000000;
            }
            break;
        case ICOLL_LEVELACK:
            irqi = s->reg[ICOLL_STAT] & 0x7f;
            s->raised[(irqi / 32)] &= ~(1 << (irqi % 32));
            s->reg[ICOLL_STAT] = 0x7f;
            break;
        case ICOLL_INT0 ... ICOLL_INT127:
            s->r[irqi] = irqval & ~(0x40); // dont' set softirq bit
            if (irqval & 0x4) // ENABLE
                s->irq[irqi / 32] |= (1 << (irqi % 32));
            else
                s->irq[irqi / 32] &= ~(1 << (irqi % 32));
            if (irqval & 0x10) // ENFIQ
                s->fiq[irqi / 32] |= (1 << (irqi % 32));
            else
                s->fiq[irqi / 32] &= ~(1 << (irqi % 32));
            if (irqval & 0x8) // SOFTIRQ
                mxs_icoll_set_irq(s, irqi, 1);
            break;
    }

    mxs_icoll_update(s);
}

static const MemoryRegionOps mxs_icoll_ops = {
    .read = mxs_icoll_read,
    .write = mxs_icoll_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int mxs_icoll_init(SysBusDevice *dev)
{
    mxs_icoll_state *s = OBJECT_CHECK(mxs_icoll_state, dev, "mxs_icoll");
    DeviceState *qdev = DEVICE(dev);

    qdev_init_gpio_in(qdev, mxs_icoll_set_irq, 128);
    sysbus_init_irq(dev, &s->parent_irq);
    sysbus_init_irq(dev, &s->parent_fiq);
    memory_region_init_io(&s->iomem, OBJECT(s), &mxs_icoll_ops, s,
            "mxs_icoll", 0x2000);
    sysbus_init_mmio(dev, &s->iomem);
    return 0;
}

static void mxs_icoll_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = mxs_icoll_init;
}

static TypeInfo icoll_info = {
    .name          = "mxs_icoll",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mxs_icoll_state),
    .class_init    = mxs_icoll_class_init,
};

static void mxs_icoll_register(void)
{
    type_register_static(&icoll_info);
}

type_init(mxs_icoll_register)
