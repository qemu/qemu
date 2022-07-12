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
        VMSTATE_UINT8(kbd_port.parent_obj.control, LASIPS2State),
        VMSTATE_UINT8(kbd_port.parent_obj.id, LASIPS2State),
        VMSTATE_BOOL(kbd_port.parent_obj.birq, LASIPS2State),
        VMSTATE_UINT8(mouse_port.parent_obj.control, LASIPS2State),
        VMSTATE_UINT8(mouse_port.parent_obj.id, LASIPS2State),
        VMSTATE_BOOL(mouse_port.parent_obj.birq, LASIPS2State),
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
    trace_lasips2_intr(s->kbd_port.parent_obj.birq |
                       s->mouse_port.parent_obj.birq);
    qemu_set_irq(s->irq, s->kbd_port.parent_obj.birq |
                         s->mouse_port.parent_obj.birq);
}

static void lasips2_set_irq(void *opaque, int n, int level)
{
    LASIPS2State *s = LASIPS2(opaque);

    if (level) {
        s->int_status |= BIT(n);
    } else {
        s->int_status &= ~BIT(n);
    }

    lasips2_update_irq(s);
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
            port->birq = true;
            port->loopback_rbne = true;
            lasips2_update_irq(port->parent);
            break;
        }

        if (port->id) {
            ps2_write_mouse(PS2_MOUSE_DEVICE(port->ps2dev), val);
        } else {
            ps2_write_keyboard(PS2_KBD_DEVICE(port->ps2dev), val);
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
            port->birq = false;
            port->loopback_rbne = false;
            lasips2_update_irq(port->parent);
            ret = port->buf;
            break;
        }

        ret = ps2_read_data(port->ps2dev);
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
            if (!ps2_queue_empty(port->ps2dev)) {
                ret |= LASIPS2_STATUS_RBNE;
            }
        }

        if (port->parent->kbd_port.parent_obj.birq ||
            port->parent->mouse_port.parent_obj.birq) {
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
    LASIPS2Port *port = LASIPS2_PORT(&s->kbd_port);

    port->birq = level;
    lasips2_update_irq(port->parent);
}

static void lasips2_set_mouse_irq(void *opaque, int n, int level)
{
    LASIPS2State *s = LASIPS2(opaque);
    LASIPS2Port *port = LASIPS2_PORT(&s->mouse_port);

    port->birq = level;
    lasips2_update_irq(port->parent);
}

static void lasips2_realize(DeviceState *dev, Error **errp)
{
    LASIPS2State *s = LASIPS2(dev);
    LASIPS2Port *lp;

    lp = LASIPS2_PORT(&s->kbd_port);
    if (!(qdev_realize(DEVICE(lp), NULL, errp))) {
        return;
    }

    qdev_connect_gpio_out(DEVICE(lp->ps2dev), PS2_DEVICE_IRQ,
                          qdev_get_gpio_in_named(dev, "ps2-kbd-input-irq",
                                                 0));

    lp = LASIPS2_PORT(&s->mouse_port);
    if (!(qdev_realize(DEVICE(lp), NULL, errp))) {
        return;
    }

    qdev_connect_gpio_out(DEVICE(lp->ps2dev), PS2_DEVICE_IRQ,
                          qdev_get_gpio_in_named(dev, "ps2-mouse-input-irq",
                                                 0));
}

static void lasips2_init(Object *obj)
{
    LASIPS2State *s = LASIPS2(obj);
    LASIPS2Port *lp;

    object_initialize_child(obj, "lasips2-kbd-port", &s->kbd_port,
                            TYPE_LASIPS2_KBD_PORT);
    object_initialize_child(obj, "lasips2-mouse-port", &s->mouse_port,
                            TYPE_LASIPS2_MOUSE_PORT);

    lp = LASIPS2_PORT(&s->kbd_port);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &lp->reg);
    lp = LASIPS2_PORT(&s->mouse_port);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &lp->reg);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    qdev_init_gpio_in_named(DEVICE(obj), lasips2_set_kbd_irq,
                            "ps2-kbd-input-irq", 1);
    qdev_init_gpio_in_named(DEVICE(obj), lasips2_set_mouse_irq,
                            "ps2-mouse-input-irq", 1);
    qdev_init_gpio_in_named(DEVICE(obj), lasips2_set_irq,
                            "lasips2-port-input-irq", 2);
}

static void lasips2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lasips2_realize;
    dc->vmsd = &vmstate_lasips2;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo lasips2_info = {
    .name          = TYPE_LASIPS2,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = lasips2_init,
    .instance_size = sizeof(LASIPS2State),
    .class_init    = lasips2_class_init,
};

static void lasips2_port_set_irq(void *opaque, int n, int level)
{
    LASIPS2Port *s = LASIPS2_PORT(opaque);

    qemu_set_irq(s->irq, level);
}

static void lasips2_port_realize(DeviceState *dev, Error **errp)
{
    LASIPS2Port *s = LASIPS2_PORT(dev);

    qdev_connect_gpio_out(DEVICE(s->ps2dev), PS2_DEVICE_IRQ,
                          qdev_get_gpio_in_named(dev, "ps2-input-irq", 0));
}

static void lasips2_port_init(Object *obj)
{
    LASIPS2Port *s = LASIPS2_PORT(obj);

    qdev_init_gpio_out(DEVICE(obj), &s->irq, 1);
    qdev_init_gpio_in_named(DEVICE(obj), lasips2_port_set_irq,
                            "ps2-input-irq", 1);
}

static void lasips2_port_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lasips2_port_realize;
}

static const TypeInfo lasips2_port_info = {
    .name          = TYPE_LASIPS2_PORT,
    .parent        = TYPE_DEVICE,
    .instance_init = lasips2_port_init,
    .instance_size = sizeof(LASIPS2Port),
    .class_init    = lasips2_port_class_init,
    .class_size    = sizeof(LASIPS2PortDeviceClass),
    .abstract      = true,
};

static void lasips2_kbd_port_realize(DeviceState *dev, Error **errp)
{
    LASIPS2Port *lp = LASIPS2_PORT(dev);

    lp->ps2dev = ps2_kbd_init();
}

static void lasips2_kbd_port_init(Object *obj)
{
    LASIPS2KbdPort *s = LASIPS2_KBD_PORT(obj);
    LASIPS2Port *lp = LASIPS2_PORT(obj);

    memory_region_init_io(&lp->reg, obj, &lasips2_reg_ops, lp, "lasips2-kbd",
                          0x100);
    lp->id = 0;
    lp->parent = container_of(s, LASIPS2State, kbd_port);
}

static void lasips2_kbd_port_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LASIPS2PortDeviceClass *lpdc = LASIPS2_PORT_CLASS(klass);

    device_class_set_parent_realize(dc, lasips2_kbd_port_realize,
                                    &lpdc->parent_realize);
}

static const TypeInfo lasips2_kbd_port_info = {
    .name          = TYPE_LASIPS2_KBD_PORT,
    .parent        = TYPE_LASIPS2_PORT,
    .instance_size = sizeof(LASIPS2KbdPort),
    .instance_init = lasips2_kbd_port_init,
    .class_init    = lasips2_kbd_port_class_init,
};

static void lasips2_mouse_port_realize(DeviceState *dev, Error **errp)
{
    LASIPS2Port *lp = LASIPS2_PORT(dev);

    lp->ps2dev = ps2_mouse_init();
}

static void lasips2_mouse_port_init(Object *obj)
{
    LASIPS2MousePort *s = LASIPS2_MOUSE_PORT(obj);
    LASIPS2Port *lp = LASIPS2_PORT(obj);

    memory_region_init_io(&lp->reg, obj, &lasips2_reg_ops, lp, "lasips2-mouse",
                          0x100);
    lp->id = 1;
    lp->parent = container_of(s, LASIPS2State, mouse_port);
}

static void lasips2_mouse_port_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LASIPS2PortDeviceClass *lpdc = LASIPS2_PORT_CLASS(klass);

    device_class_set_parent_realize(dc, lasips2_mouse_port_realize,
                                    &lpdc->parent_realize);
}

static const TypeInfo lasips2_mouse_port_info = {
    .name          = TYPE_LASIPS2_MOUSE_PORT,
    .parent        = TYPE_LASIPS2_PORT,
    .instance_size = sizeof(LASIPS2MousePort),
    .instance_init = lasips2_mouse_port_init,
    .class_init    = lasips2_mouse_port_class_init,
};

static void lasips2_register_types(void)
{
    type_register_static(&lasips2_info);
    type_register_static(&lasips2_port_info);
    type_register_static(&lasips2_kbd_port_info);
    type_register_static(&lasips2_mouse_port_info);
}

type_init(lasips2_register_types)
