/*
 * QEMU HP Lasi PS/2 interface emulation
 *
 * Copyright (c) 2019 Sven Schnelle
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/input/ps2.h"
#include "hw/input/lasips2.h"
#include "exec/hwaddr.h"
#include "trace.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "qapi/error.h"


static const VMStateDescription vmstate_lasips2 = {
    .name = "lasips2",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(kbd.control, LASIPS2State),
        VMSTATE_UINT8(kbd.id, LASIPS2State),
        VMSTATE_BOOL(kbd.irq, LASIPS2State),
        VMSTATE_UINT8(mouse.control, LASIPS2State),
        VMSTATE_UINT8(mouse.id, LASIPS2State),
        VMSTATE_BOOL(mouse.irq, LASIPS2State),
        VMSTATE_END_OF_LIST()
    }
};

typedef enum {
    REG_PS2_ID = 0,
    REG_PS2_RCVDATA = 4,
    REG_PS2_CONTROL = 8,
    REG_PS2_STATUS = 12,
} lasips2_read_reg_t;

typedef enum {
    REG_PS2_RESET = 0,
    REG_PS2_XMTDATA = 4,
} lasips2_write_reg_t;

typedef enum {
    LASIPS2_CONTROL_ENABLE = 0x01,
    LASIPS2_CONTROL_LOOPBACK = 0x02,
    LASIPS2_CONTROL_DIAG = 0x20,
    LASIPS2_CONTROL_DATDIR = 0x40,
    LASIPS2_CONTROL_CLKDIR = 0x80,
} lasips2_control_reg_t;

typedef enum {
    LASIPS2_STATUS_RBNE = 0x01,
    LASIPS2_STATUS_TBNE = 0x02,
    LASIPS2_STATUS_TERR = 0x04,
    LASIPS2_STATUS_PERR = 0x08,
    LASIPS2_STATUS_CMPINTR = 0x10,
    LASIPS2_STATUS_DATSHD = 0x40,
    LASIPS2_STATUS_CLKSHD = 0x80,
} lasips2_status_reg_t;

static const char *lasips2_read_reg_name(uint64_t addr)
{
    switch (addr & 0xc) {
    case REG_PS2_ID:
        return " PS2_ID";

    case REG_PS2_RCVDATA:
        return " PS2_RCVDATA";

    case REG_PS2_CONTROL:
        return " PS2_CONTROL";

    case REG_PS2_STATUS:
        return " PS2_STATUS";

    default:
        return "";
    }
}

static const char *lasips2_write_reg_name(uint64_t addr)
{
    switch (addr & 0x0c) {
    case REG_PS2_RESET:
        return " PS2_RESET";

    case REG_PS2_XMTDATA:
        return " PS2_XMTDATA";

    case REG_PS2_CONTROL:
        return " PS2_CONTROL";

    default:
        return "";
    }
}

static void lasips2_update_irq(LASIPS2State *s)
{
    trace_lasips2_intr(s->kbd.irq | s->mouse.irq);
    qemu_set_irq(s->irq, s->kbd.irq | s->mouse.irq);
}

static void lasips2_reg_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    LASIPS2Port *port = opaque;

    trace_lasips2_reg_write(size, port->id, addr,
                            lasips2_write_reg_name(addr), val);

    switch (addr & 0xc) {
    case REG_PS2_CONTROL:
        port->control = val;
        break;

    case REG_PS2_XMTDATA:
        if (port->control & LASIPS2_CONTROL_LOOPBACK) {
            port->buf = val;
            port->irq = true;
            port->loopback_rbne = true;
            lasips2_update_irq(port->parent);
            break;
        }

        if (port->id) {
            ps2_write_mouse(port->dev, val);
        } else {
            ps2_write_keyboard(port->dev, val);
        }
        break;

    case REG_PS2_RESET:
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register 0x%02" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static uint64_t lasips2_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    LASIPS2Port *port = opaque;
    uint64_t ret = 0;

    switch (addr & 0xc) {
    case REG_PS2_ID:
        ret = port->id;
        break;

    case REG_PS2_RCVDATA:
        if (port->control & LASIPS2_CONTROL_LOOPBACK) {
            port->irq = false;
            port->loopback_rbne = false;
            lasips2_update_irq(port->parent);
            ret = port->buf;
            break;
        }

        ret = ps2_read_data(port->dev);
        break;

    case REG_PS2_CONTROL:
        ret = port->control;
        break;

    case REG_PS2_STATUS:
        ret = LASIPS2_STATUS_DATSHD | LASIPS2_STATUS_CLKSHD;

        if (port->control & LASIPS2_CONTROL_DIAG) {
            if (!(port->control & LASIPS2_CONTROL_DATDIR)) {
                ret &= ~LASIPS2_STATUS_DATSHD;
            }

            if (!(port->control & LASIPS2_CONTROL_CLKDIR)) {
                ret &= ~LASIPS2_STATUS_CLKSHD;
            }
        }

        if (port->control & LASIPS2_CONTROL_LOOPBACK) {
            if (port->loopback_rbne) {
                ret |= LASIPS2_STATUS_RBNE;
            }
        } else {
            if (!ps2_queue_empty(port->dev)) {
                ret |= LASIPS2_STATUS_RBNE;
            }
        }

        if (port->parent->kbd.irq || port->parent->mouse.irq) {
            ret |= LASIPS2_STATUS_CMPINTR;
        }
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register 0x%02" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }

    trace_lasips2_reg_read(size, port->id, addr,
                           lasips2_read_reg_name(addr), ret);
    return ret;
}

static const MemoryRegionOps lasips2_reg_ops = {
    .read = lasips2_reg_read,
    .write = lasips2_reg_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void lasips2_set_kbd_irq(void *opaque, int n, int level)
{
    LASIPS2State *s = LASIPS2(opaque);
    LASIPS2Port *port = &s->kbd;

    port->irq = level;
    lasips2_update_irq(port->parent);
}

static void lasips2_set_mouse_irq(void *opaque, int n, int level)
{
    LASIPS2State *s = LASIPS2(opaque);
    LASIPS2Port *port = &s->mouse;

    port->irq = level;
    lasips2_update_irq(port->parent);
}

LASIPS2State *lasips2_initfn(hwaddr base, qemu_irq irq)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_LASIPS2);
    qdev_prop_set_uint64(dev, "base", base);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);

    return LASIPS2(dev);
}

static void lasips2_realize(DeviceState *dev, Error **errp)
{
    LASIPS2State *s = LASIPS2(dev);

    vmstate_register(NULL, s->base, &vmstate_lasips2, s);

    s->kbd.dev = ps2_kbd_init(NULL, NULL);
    qdev_connect_gpio_out(DEVICE(s->kbd.dev), PS2_DEVICE_IRQ,
                          qdev_get_gpio_in_named(dev, "ps2-kbd-input-irq",
                                                 0));
    s->mouse.dev = ps2_mouse_init(NULL, NULL);
    qdev_connect_gpio_out(DEVICE(s->mouse.dev), PS2_DEVICE_IRQ,
                          qdev_get_gpio_in_named(dev, "ps2-mouse-input-irq",
                                                 0));
}

static void lasips2_init(Object *obj)
{
    LASIPS2State *s = LASIPS2(obj);

    s->kbd.id = 0;
    s->mouse.id = 1;
    s->kbd.parent = s;
    s->mouse.parent = s;

    memory_region_init_io(&s->kbd.reg, obj, &lasips2_reg_ops, &s->kbd,
                          "lasips2-kbd", 0x100);
    memory_region_init_io(&s->mouse.reg, obj, &lasips2_reg_ops, &s->mouse,
                          "lasips2-mouse", 0x100);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->kbd.reg);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mouse.reg);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    qdev_init_gpio_in_named(DEVICE(obj), lasips2_set_kbd_irq,
                            "ps2-kbd-input-irq", 1);
    qdev_init_gpio_in_named(DEVICE(obj), lasips2_set_mouse_irq,
                            "ps2-mouse-input-irq", 1);
}

static Property lasips2_properties[] = {
    DEFINE_PROP_UINT64("base", LASIPS2State, base, UINT64_MAX),
    DEFINE_PROP_END_OF_LIST(),
};

static void lasips2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lasips2_realize;
    device_class_set_props(dc, lasips2_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo lasips2_info = {
    .name          = TYPE_LASIPS2,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = lasips2_init,
    .instance_size = sizeof(LASIPS2State),
    .class_init    = lasips2_class_init,
};

static void lasips2_register_types(void)
{
    type_register_static(&lasips2_info);
}

type_init(lasips2_register_types)
