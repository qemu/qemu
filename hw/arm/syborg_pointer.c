/*
 * Syborg pointing device (mouse/touchscreen)
 *
 * Copyright (c) 2008 CodeSourcery
 * Copyright (c) 2010, 2013 Stefan Weil
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

#include "hw/sysbus.h"
#include "ui/console.h"
#include "syborg.h"

enum {
    POINTER_ID          = 0,
    POINTER_LATCH       = 1,
    POINTER_FIFO_COUNT  = 2,
    POINTER_X           = 3,
    POINTER_Y           = 4,
    POINTER_Z           = 5,
    POINTER_BUTTONS     = 6,
    POINTER_INT_ENABLE  = 7,
    POINTER_FIFO_SIZE   = 8
};

typedef struct {
    int x, y, z, pointer_buttons;
} event_data;

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    int int_enabled;
    uint32_t fifo_size;
    event_data *event_fifo;
    int read_pos, read_count;
    qemu_irq irq;
    uint32_t absolute;
} SyborgPointerState;

static void syborg_pointer_update(SyborgPointerState *s)
{
    qemu_set_irq(s->irq, s->read_count && s->int_enabled);
}

static uint64_t syborg_pointer_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    SyborgPointerState *s = (SyborgPointerState *)opaque;

    offset &= 0xfff;
    switch (offset >> 2) {
    case POINTER_ID:
        return s->absolute ? SYBORG_ID_TOUCHSCREEN : SYBORG_ID_MOUSE;
    case POINTER_FIFO_COUNT:
        return s->read_count;
    case POINTER_X:
        return s->event_fifo[s->read_pos].x;
    case POINTER_Y:
        return s->event_fifo[s->read_pos].y;
    case POINTER_Z:
        return s->event_fifo[s->read_pos].z;
    case POINTER_BUTTONS:
        return s->event_fifo[s->read_pos].pointer_buttons;
    case POINTER_INT_ENABLE:
        return s->int_enabled;
    case POINTER_FIFO_SIZE:
        return s->fifo_size;
    default:
        cpu_abort(cpu_single_env, "syborg_pointer_read: Bad offset %x\n",
                  (int)offset);
        return 0;
    }
}

static void syborg_pointer_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    SyborgPointerState *s = (SyborgPointerState *)opaque;

    offset &= 0xfff;
    switch (offset >> 2) {
    case POINTER_LATCH:
        if (s->read_count > 0) {
            s->read_count--;
            if (++s->read_pos == s->fifo_size)
                s->read_pos = 0;
        }
        break;
    case POINTER_INT_ENABLE:
        s->int_enabled = value;
        break;
    default:
        cpu_abort(cpu_single_env, "syborg_pointer_write: Bad offset %x\n",
                  (int)offset);
    }
    syborg_pointer_update(s);
}

static const MemoryRegionOps syborg_pointer_ops = {
    .read = syborg_pointer_read,
    .write = syborg_pointer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void syborg_pointer_event(void *opaque, int dx, int dy, int dz,
                                 int buttons_state)
{
    SyborgPointerState *s = (SyborgPointerState *)opaque;
    int slot = s->read_pos + s->read_count;

    /* This first FIFO entry is used to store current register state.  */
    if (s->read_count < s->fifo_size - 1) {
        s->read_count++;
        slot++;
    }

    if (slot >= s->fifo_size)
          slot -= s->fifo_size;

    if (s->read_count == s->fifo_size && !s->absolute) {
        /* Merge existing entries.  */
        s->event_fifo[slot].x += dx;
        s->event_fifo[slot].y += dy;
        s->event_fifo[slot].z += dz;
    } else {
        s->event_fifo[slot].x = dx;
        s->event_fifo[slot].y = dy;
        s->event_fifo[slot].z = dz;
    }
    s->event_fifo[slot].pointer_buttons = buttons_state;

    syborg_pointer_update(s);
}

static const VMStateDescription vmstate_event_data = {
    .name = "dbma_channel",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField[]) {
        VMSTATE_INT32(x, event_data),
        VMSTATE_INT32(y, event_data),
        VMSTATE_INT32(z, event_data),
        VMSTATE_INT32(pointer_buttons, event_data),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_syborg_pointer = {
    .name = "syborg_pointer",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_EQUAL(fifo_size, SyborgPointerState),
        VMSTATE_UINT32_EQUAL(absolute, SyborgPointerState),
        VMSTATE_INT32(int_enabled, SyborgPointerState),
        VMSTATE_INT32(read_pos, SyborgPointerState),
        VMSTATE_INT32(read_count, SyborgPointerState),
        VMSTATE_STRUCT_VARRAY_UINT32(event_fifo, SyborgPointerState, fifo_size,
                                     1, vmstate_event_data, event_data),
        VMSTATE_END_OF_LIST()
    }
};

static Property syborg_pointer_properties[] = {
    DEFINE_PROP_UINT32("fifo-size", SyborgPointerState, fifo_size, 16),
    DEFINE_PROP_UINT32("absolute",  SyborgPointerState, absolute,   1),
    DEFINE_PROP_END_OF_LIST(),
};

static int syborg_pointer_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    SyborgPointerState *s = SYBORG_POINTER(dev);

    sysbus_init_irq(dev, &s->irq);
    memory_region_init_io(&s->iomem, &syborg_pointer_ops, s,
                          "pointer", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    if (s->fifo_size <= 0) {
        fprintf(stderr, "syborg_pointer: fifo too small\n");
        s->fifo_size = 16;
    }
    s->event_fifo = g_malloc0(s->fifo_size * sizeof(s->event_fifo[0]));

    qemu_add_mouse_event_handler(syborg_pointer_event, s, s->absolute,
                                 "Syborg Pointer");

    vmstate_register(&dev->qdev, -1, &vmstate_syborg_pointer, s);
    return 0;
}

static void syborg_pointer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    dc->props = syborg_pointer_properties;
    k->init = syborg_pointer_init;
}

static const TypeInfo syborg_pointer_info = {
    .name  = "syborg,pointer",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SyborgPointerState),
    .class_init = syborg_pointer_class_init
};

static void syborg_pointer_register_types(void)
{
    type_register_static(&syborg_pointer_info);
}

type_init(syborg_pointer_register_types)
