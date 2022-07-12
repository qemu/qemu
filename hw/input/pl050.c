/*
 * Arm PrimeCell PL050 Keyboard / Mouse Interface
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

/*
 * QEMU interface:
 * + sysbus MMIO region 0: MemoryRegion defining the PL050 registers
 * + Named GPIO input "ps2-input-irq": set to 1 if the downstream PS2 device
 *   has asserted its irq
 * + sysbus IRQ 0: PL050 output irq
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/input/ps2.h"
#include "hw/input/pl050.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"


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

static const unsigned char pl050_id[] = {
    0x50, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1
};

static void pl050_update_irq(PL050State *s)
{
    int level = (s->pending && (s->cr & 0x10) != 0)
                 || (s->cr & 0x08) != 0;

    qemu_set_irq(s->irq, level);
}

static void pl050_set_irq(void *opaque, int n, int level)
{
    PL050State *s = (PL050State *)opaque;

    s->pending = level;
    pl050_update_irq(s);
}

static uint64_t pl050_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL050State *s = (PL050State *)opaque;

    if (offset >= 0xfe0 && offset < 0x1000) {
        return pl050_id[(offset - 0xfe0) >> 2];
    }

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
            if (val) {
                stat |= PL050_RXPARITY;
            }
            if (s->pending) {
                stat |= PL050_RXFULL;
            }

            return stat;
        }
    case 2: /* KMIDATA */
        if (s->pending) {
            s->last = ps2_read_data(s->ps2dev);
        }
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
        pl050_update_irq(s);
        /* ??? Need to implement the enable/disable bit.  */
        break;
    case 2: /* KMIDATA */
        /* ??? This should toggle the TX interrupt line.  */
        /* ??? This means kbd/mouse can block each other.  */
        if (s->is_mouse) {
            ps2_write_mouse(PS2_MOUSE_DEVICE(s->ps2dev), value);
        } else {
            ps2_write_keyboard(PS2_KBD_DEVICE(s->ps2dev), value);
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

    qdev_connect_gpio_out(DEVICE(s->ps2dev), PS2_DEVICE_IRQ,
                          qdev_get_gpio_in_named(dev, "ps2-input-irq", 0));
}

static void pl050_kbd_realize(DeviceState *dev, Error **errp)
{
    PL050DeviceClass *pdc = PL050_GET_CLASS(dev);
    PL050KbdState *s = PL050_KBD_DEVICE(dev);
    PL050State *ps = PL050(dev);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->kbd), errp)) {
        return;
    }

    ps->ps2dev = PS2_DEVICE(&s->kbd);
    pdc->parent_realize(dev, errp);
}

static void pl050_kbd_init(Object *obj)
{
    PL050KbdState *s = PL050_KBD_DEVICE(obj);
    PL050State *ps = PL050(obj);

    ps->is_mouse = false;
    object_initialize_child(obj, "kbd", &s->kbd, TYPE_PS2_KBD_DEVICE);
}

static void pl050_mouse_realize(DeviceState *dev, Error **errp)
{
    PL050DeviceClass *pdc = PL050_GET_CLASS(dev);
    PL050MouseState *s = PL050_MOUSE_DEVICE(dev);
    PL050State *ps = PL050(dev);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mouse), errp)) {
        return;
    }

    ps->ps2dev = PS2_DEVICE(&s->mouse);
    pdc->parent_realize(dev, errp);
}

static void pl050_mouse_init(Object *obj)
{
    PL050MouseState *s = PL050_MOUSE_DEVICE(obj);
    PL050State *ps = PL050(obj);

    ps->is_mouse = true;
    object_initialize_child(obj, "mouse", &s->mouse, TYPE_PS2_MOUSE_DEVICE);
}

static void pl050_kbd_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PL050DeviceClass *pdc = PL050_CLASS(oc);

    device_class_set_parent_realize(dc, pl050_kbd_realize,
                                    &pdc->parent_realize);
}

static const TypeInfo pl050_kbd_info = {
    .name          = TYPE_PL050_KBD_DEVICE,
    .parent        = TYPE_PL050,
    .instance_init = pl050_kbd_init,
    .instance_size = sizeof(PL050KbdState),
    .class_init    = pl050_kbd_class_init,
};

static void pl050_mouse_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PL050DeviceClass *pdc = PL050_CLASS(oc);

    device_class_set_parent_realize(dc, pl050_mouse_realize,
                                    &pdc->parent_realize);
}

static const TypeInfo pl050_mouse_info = {
    .name          = TYPE_PL050_MOUSE_DEVICE,
    .parent        = TYPE_PL050,
    .instance_init = pl050_mouse_init,
    .instance_size = sizeof(PL050MouseState),
    .class_init    = pl050_mouse_class_init,
};

static void pl050_init(Object *obj)
{
    PL050State *s = PL050(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &pl050_ops, s, "pl050", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qdev_init_gpio_in_named(DEVICE(obj), pl050_set_irq, "ps2-input-irq", 1);
}

static void pl050_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pl050_realize;
    dc->vmsd = &vmstate_pl050;
}

static const TypeInfo pl050_type_info = {
    .name          = TYPE_PL050,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = pl050_init,
    .instance_size = sizeof(PL050State),
    .class_init    = pl050_class_init,
    .class_size    = sizeof(PL050DeviceClass),
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
