/*
 * Arm PrimeCell PL050 Keyboard / Mouse Interface
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/input/ps2.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TYPE_PL050 "pl050"
#define PL050(obj) OBJECT_CHECK(PL050State, (obj), TYPE_PL050)

typedef struct PL050State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    void *dev;
    uint32_t cr;
    uint32_t clk;
    uint32_t last;
    int pending;
    qemu_irq irq;
    bool is_mouse;
} PL050State;

static const VMStateDescription vmstate_pl050 = {
    .name = "pl050",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cr, PL050State),
        VMSTATE_UINT32(clk, PL050State),
        VMSTATE_UINT32(last, PL050State),
        VMSTATE_INT32(pending, PL050State),
        VMSTATE_END_OF_LIST()
    }
};

#define PL050_TXEMPTY         (1 << 6)
#define PL050_TXBUSY          (1 << 5)
#define PL050_RXFULL          (1 << 4)
#define PL050_RXBUSY          (1 << 3)
#define PL050_RXPARITY        (1 << 2)
#define PL050_KMIC            (1 << 1)
#define PL050_KMID            (1 << 0)

static const unsigned char pl050_id[] =
{ 0x50, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };

static void pl050_update(void *opaque, int level)
{
    PL050State *s = (PL050State *)opaque;
    int raise;

    s->pending = level;
    raise = (s->pending && (s->cr & 0x10) != 0)
            || (s->cr & 0x08) != 0;
    qemu_set_irq(s->irq, raise);
}

static uint64_t pl050_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL050State *s = (PL050State *)opaque;
    if (offset >= 0xfe0 && offset < 0x1000)
        return pl050_id[(offset - 0xfe0) >> 2];

    switch (offset >> 2) {
    case 0: /* KMICR */
        return s->cr;
    case 1: /* KMISTAT */
        {
            uint8_t val;
            uint32_t stat;

            val = s->last;
            val = val ^ (val >> 4);
            val = val ^ (val >> 2);
            val = (val ^ (val >> 1)) & 1;

            stat = PL050_TXEMPTY;
            if (val)
                stat |= PL050_RXPARITY;
            if (s->pending)
                stat |= PL050_RXFULL;

            return stat;
        }
    case 2: /* KMIDATA */
        if (s->pending)
            s->last = ps2_read_data(s->dev);
        return s->last;
    case 3: /* KMICLKDIV */
        return s->clk;
    case 4: /* KMIIR */
        return s->pending | 2;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl050_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void pl050_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PL050State *s = (PL050State *)opaque;
    switch (offset >> 2) {
    case 0: /* KMICR */
        s->cr = value;
        pl050_update(s, s->pending);
        /* ??? Need to implement the enable/disable bit.  */
        break;
    case 2: /* KMIDATA */
        /* ??? This should toggle the TX interrupt line.  */
        /* ??? This means kbd/mouse can block each other.  */
        if (s->is_mouse) {
            ps2_write_mouse(s->dev, value);
        } else {
            ps2_write_keyboard(s->dev, value);
        }
        break;
    case 3: /* KMICLKDIV */
        s->clk = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl050_write: Bad offset %x\n", (int)offset);
    }
}
static const MemoryRegionOps pl050_ops = {
    .read = pl050_read,
    .write = pl050_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pl050_realize(DeviceState *dev, Error **errp)
{
    PL050State *s = PL050(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &pl050_ops, s, "pl050", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    if (s->is_mouse) {
        s->dev = ps2_mouse_init(pl050_update, s);
    } else {
        s->dev = ps2_kbd_init(pl050_update, s);
    }
}

static void pl050_keyboard_init(Object *obj)
{
    PL050State *s = PL050(obj);

    s->is_mouse = false;
}

static void pl050_mouse_init(Object *obj)
{
    PL050State *s = PL050(obj);

    s->is_mouse = true;
}

static const TypeInfo pl050_kbd_info = {
    .name          = "pl050_keyboard",
    .parent        = TYPE_PL050,
    .instance_init = pl050_keyboard_init,
};

static const TypeInfo pl050_mouse_info = {
    .name          = "pl050_mouse",
    .parent        = TYPE_PL050,
    .instance_init = pl050_mouse_init,
};

static void pl050_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pl050_realize;
    dc->vmsd = &vmstate_pl050;
}

static const TypeInfo pl050_type_info = {
    .name          = TYPE_PL050,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PL050State),
    .abstract      = true,
    .class_init    = pl050_class_init,
};

static void pl050_register_types(void)
{
    type_register_static(&pl050_type_info);
    type_register_static(&pl050_kbd_info);
    type_register_static(&pl050_mouse_info);
}

type_init(pl050_register_types)
