/*
 * Syborg pointing device (mouse/touchscreen)
 *
 * Copyright (c) 2008 CodeSourcery
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

#include "sysbus.h"
#include "console.h"
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
    int int_enabled;
    int fifo_size;
    event_data *event_fifo;
    int read_pos, read_count;
    qemu_irq irq;
    int absolute;
} SyborgPointerState;

static void syborg_pointer_update(SyborgPointerState *s)
{
    qemu_set_irq(s->irq, s->read_count && s->int_enabled);
}

static uint32_t syborg_pointer_read(void *opaque, target_phys_addr_t offset)
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

static void syborg_pointer_write(void *opaque, target_phys_addr_t offset,
                                 uint32_t value)
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

static CPUReadMemoryFunc *syborg_pointer_readfn[] = {
   syborg_pointer_read,
   syborg_pointer_read,
   syborg_pointer_read
};

static CPUWriteMemoryFunc *syborg_pointer_writefn[] = {
   syborg_pointer_write,
   syborg_pointer_write,
   syborg_pointer_write
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

static void syborg_pointer_save(QEMUFile *f, void *opaque)
{
    SyborgPointerState *s = (SyborgPointerState *)opaque;
    int i;

    qemu_put_be32(f, s->fifo_size);
    qemu_put_be32(f, s->absolute);
    qemu_put_be32(f, s->int_enabled);
    qemu_put_be32(f, s->read_pos);
    qemu_put_be32(f, s->read_count);
    for (i = 0; i < s->fifo_size; i++) {
        qemu_put_be32(f, s->event_fifo[i].x);
        qemu_put_be32(f, s->event_fifo[i].y);
        qemu_put_be32(f, s->event_fifo[i].z);
        qemu_put_be32(f, s->event_fifo[i].pointer_buttons);
    }
}

static int syborg_pointer_load(QEMUFile *f, void *opaque, int version_id)
{
    SyborgPointerState *s = (SyborgPointerState *)opaque;
    uint32_t val;
    int i;

    if (version_id != 1)
        return -EINVAL;

    val = qemu_get_be32(f);
    if (val != s->fifo_size)
        return -EINVAL;

    val = qemu_get_be32(f);
    if (val != s->absolute)
        return -EINVAL;

    s->int_enabled = qemu_get_be32(f);
    s->read_pos = qemu_get_be32(f);
    s->read_count = qemu_get_be32(f);
    for (i = 0; i < s->fifo_size; i++) {
        s->event_fifo[i].x = qemu_get_be32(f);
        s->event_fifo[i].y = qemu_get_be32(f);
        s->event_fifo[i].z = qemu_get_be32(f);
        s->event_fifo[i].pointer_buttons = qemu_get_be32(f);
    }
    return 0;
}

static void syborg_pointer_init(SysBusDevice *dev)
{
    SyborgPointerState *s = FROM_SYSBUS(SyborgPointerState, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->irq);
    iomemtype = cpu_register_io_memory(0, syborg_pointer_readfn,
				       syborg_pointer_writefn, s);
    sysbus_init_mmio(dev, 0x1000, iomemtype);

    s->absolute = qdev_get_prop_int(&dev->qdev, "absolute", 1);
    s->fifo_size = qdev_get_prop_int(&dev->qdev, "fifo-size", 16);
    if (s->fifo_size <= 0) {
        fprintf(stderr, "syborg_pointer: fifo too small\n");
        s->fifo_size = 16;
    }
    s->event_fifo = qemu_mallocz(s->fifo_size * sizeof(s->event_fifo[0]));

    qemu_add_mouse_event_handler(syborg_pointer_event, s, s->absolute,
                                 "Syborg Pointer");

    register_savevm("syborg_pointer", -1, 1,
                    syborg_pointer_save, syborg_pointer_load, s);
}

static void syborg_pointer_register_devices(void)
{
    sysbus_register_dev("syborg,pointer", sizeof(SyborgPointerState),
                        syborg_pointer_init);
}

device_init(syborg_pointer_register_devices)
