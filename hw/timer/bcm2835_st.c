/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

/* Based on several timers code found in various QEMU source files. */

#include "qemu/timer.h"
#include "hw/sysbus.h"

#define TYPE_BCM2835_ST "bcm2835_st"
#define BCM2835_ST(obj) OBJECT_CHECK(bcm2835_st_state, (obj), TYPE_BCM2835_ST)

typedef struct bcm2835_st_state {
    SysBusDevice busdev;
    MemoryRegion iomem;
    QEMUTimer *timer;
    uint32_t compare[4];
    uint32_t match;
    uint32_t next;
    qemu_irq irq[4];
} bcm2835_st_state;

static void bcm2835_st_update(bcm2835_st_state *s)
{
    int64_t now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    uint32_t clo = (uint32_t)now;
    uint32_t delta = -1;
    int i;

    /* Calculate new "next" value and reschedule */
    for (i = 0; i < 4; i++) {
        if (!(s->match & (1 << i))) {
            if (s->compare[i] - clo < delta) {
                s->next = s->compare[i];
                delta = s->next - clo;
            }
        }
    }
    timer_mod(s->timer, now + delta);
}

static void bcm2835_st_tick(void *opaque)
{
    bcm2835_st_state *s = (bcm2835_st_state *)opaque;
    int i;

    /* Trigger irqs for current "next" value */
    for (i = 0; i < 4; i++) {
        if (!(s->match & (1 << i)) && (s->next == s->compare[i])) {
            s->match |= (1 << i);
            qemu_set_irq(s->irq[i], 1);
        }
    }

    bcm2835_st_update(s);
}

static uint64_t bcm2835_st_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    bcm2835_st_state *s = (bcm2835_st_state *)opaque;
    uint32_t res = 0;
    int64_t now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);

    assert(size == 4);

    switch (offset) {
    case 0x00:
        res = s->match;
        break;
    case 0x04:
        res = (uint32_t)now;
        /* Ugly temporary hack to get Plan9 to boot... */
        /* see http://plan9.bell-labs.com/sources/contrib/ \
         * miller/rpi/sys/src/9/bcm/clock.c */
        /* res = (now / 10000) * 10000; */
        break;
    case 0x08:
        res = (now >> 32);
        break;
    case 0x0c:
        res = s->compare[0];
        break;
    case 0x10:
        res = s->compare[1];
        break;
    case 0x14:
        res = s->compare[2];
        break;
    case 0x18:
        res = s->compare[3];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_st_read: Bad offset %x\n", (int)offset);
        return 0;
    }

    return res;
}

static void bcm2835_st_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    bcm2835_st_state *s = (bcm2835_st_state *)opaque;
    int i;

    assert(size == 4);

    switch (offset) {
    case 0x00:
        s->match &= ~value & 0x0f;
        for (i = 0; i < 4; i++) {
            if (!(s->match & (1 << i))) {
                qemu_set_irq(s->irq[i], 0);
            }
        }
        break;
    case 0x0c:
        s->compare[0] = value;
        break;
    case 0x10:
        s->compare[1] = value;
        break;
    case 0x14:
        s->compare[2] = value;
        break;
    case 0x18:
        s->compare[3] = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_st_write: Bad offset %x\n", (int)offset);
        return;
    }
    bcm2835_st_update(s);
}

static const MemoryRegionOps bcm2835_st_ops = {
    .read = bcm2835_st_read,
    .write = bcm2835_st_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_st = {
    .name = TYPE_BCM2835_ST,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(compare, bcm2835_st_state, 4),
        VMSTATE_UINT32(match, bcm2835_st_state),
        VMSTATE_END_OF_LIST()
    }
};

static int bcm2835_st_init(SysBusDevice *sbd)
{
    int i;
    DeviceState *dev = DEVICE(sbd);
    bcm2835_st_state *s = BCM2835_ST(dev);

    for (i = 0; i < 4; i++) {
        s->compare[i] = 0;
        sysbus_init_irq(sbd, &s->irq[i]);
    }
    s->match = 0;

    s->timer = timer_new_us(QEMU_CLOCK_VIRTUAL, bcm2835_st_tick, s);

    bcm2835_st_update(s);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_st_ops, s,
        TYPE_BCM2835_ST, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    vmstate_register(dev, -1, &vmstate_bcm2835_st, s);

    return 0;
}

static void bcm2835_st_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = bcm2835_st_init;
}

static TypeInfo bcm2835_st_info = {
    .name          = TYPE_BCM2835_ST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(bcm2835_st_state),
    .class_init    = bcm2835_st_class_init,
};

static void bcm2835_st_register_types(void)
{
    type_register_static(&bcm2835_st_info);
}

type_init(bcm2835_st_register_types)
