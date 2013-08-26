/*
 * Syborg keyboard controller.
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

//#define DEBUG_SYBORG_KEYBOARD

#ifdef DEBUG_SYBORG_KEYBOARD
#define DPRINTF(fmt, ...) \
do { printf("syborg_keyboard: " fmt , ##args); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_keyboard: error: " fmt , ## __VA_ARGS__); \
    exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_keyboard: error: " fmt , ## __VA_ARGS__); \
} while (0)
#endif

enum {
    KBD_ID          = 0,
    KBD_DATA        = 1,
    KBD_FIFO_COUNT  = 2,
    KBD_INT_ENABLE  = 3,
    KBD_FIFO_SIZE   = 4
};

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    uint32_t int_enabled;
    int extension_bit;
    uint32_t fifo_size;
    uint32_t *key_fifo;
    uint32_t read_pos, read_count;
    qemu_irq irq;
} SyborgKeyboardState;

static void syborg_keyboard_update(SyborgKeyboardState *s)
{
    int level = s->read_count && s->int_enabled;
    DPRINTF("Update IRQ %d\n", level);
    qemu_set_irq(s->irq, level);
}

static uint64_t syborg_keyboard_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    SyborgKeyboardState *s = (SyborgKeyboardState *)opaque;
    int c;

    DPRINTF("reg read %d\n", (int)offset);
    offset &= 0xfff;
    switch (offset >> 2) {
    case KBD_ID:
        return SYBORG_ID_KEYBOARD;
    case KBD_FIFO_COUNT:
        return s->read_count;
    case KBD_DATA:
        if (s->read_count == 0) {
            c = -1;
            DPRINTF("FIFO underflow\n");
        } else {
            c = s->key_fifo[s->read_pos];
            DPRINTF("FIFO read 0x%x\n", c);
            s->read_count--;
            s->read_pos++;
            if (s->read_pos == s->fifo_size)
                s->read_pos = 0;
        }
        syborg_keyboard_update(s);
        return c;
    case KBD_INT_ENABLE:
        return s->int_enabled;
    case KBD_FIFO_SIZE:
        return s->fifo_size;
    default:
        cpu_abort(cpu_single_env, "syborg_keyboard_read: Bad offset %x\n",
                  (int)offset);
        return 0;
    }
}

static void syborg_keyboard_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    SyborgKeyboardState *s = (SyborgKeyboardState *)opaque;

    DPRINTF("reg write %d\n", (int)offset);
    offset &= 0xfff;
    switch (offset >> 2) {
    case KBD_INT_ENABLE:
        s->int_enabled = value;
        syborg_keyboard_update(s);
        break;
    default:
        cpu_abort(cpu_single_env, "syborg_keyboard_write: Bad offset %x\n",
                  (int)offset);
    }
}

static const MemoryRegionOps syborg_keyboard_ops = {
    .read = syborg_keyboard_read,
    .write = syborg_keyboard_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void syborg_keyboard_event(void *opaque, int keycode)
{
    SyborgKeyboardState *s = (SyborgKeyboardState *)opaque;
    int slot;
    uint32_t val;

    /* Strip off 0xe0 prefixes and reconstruct the full scancode.  */
    if (keycode == 0xe0 && !s->extension_bit) {
        DPRINTF("Extension bit\n");
        s->extension_bit = 0x80;
        return;
    }
    val = (keycode & 0x7f) | s->extension_bit;
    if (keycode & 0x80)
        val |= 0x80000000u;
    s->extension_bit = 0;

    DPRINTF("FIFO push 0x%x\n", val);
    slot = s->read_pos + s->read_count;
    if (slot >= s->fifo_size)
        slot -= s->fifo_size;

    if (s->read_count < s->fifo_size) {
        s->read_count++;
        s->key_fifo[slot] = val;
    } else {
        fprintf(stderr, "syborg_keyboard error! FIFO overflow\n");
    }

    syborg_keyboard_update(s);
}

static const VMStateDescription vmstate_syborg_keyboard = {
    .name = "syborg_keyboard",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_EQUAL(fifo_size, SyborgKeyboardState),
        VMSTATE_UINT32(int_enabled, SyborgKeyboardState),
        VMSTATE_UINT32(read_pos, SyborgKeyboardState),
        VMSTATE_UINT32(read_count, SyborgKeyboardState),
        VMSTATE_VARRAY_UINT32(key_fifo, SyborgKeyboardState, fifo_size, 1,
                              vmstate_info_uint32, uint32),
        VMSTATE_END_OF_LIST()
    }
};

static int syborg_keyboard_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    SyborgKeyboardState *s = SYBORG_KEYBOARD(dev);

    sysbus_init_irq(dev, &s->irq);
    memory_region_init_io(&s->iomem, &syborg_keyboard_ops, s,
                              "keyboard", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    if (s->fifo_size <= 0) {
        fprintf(stderr, "syborg_keyboard: fifo too small\n");
        s->fifo_size = 16;
    }
    s->key_fifo = g_malloc0(s->fifo_size * sizeof(s->key_fifo[0]));

    qemu_add_kbd_event_handler(syborg_keyboard_event, s);

    vmstate_register(&dev->qdev, -1, &vmstate_syborg_keyboard, s);
    return 0;
}

static Property syborg_keyboard_properties[] = {
    DEFINE_PROP_UINT32("fifo-size", SyborgKeyboardState, fifo_size, 16),
    DEFINE_PROP_END_OF_LIST()
};

static void syborg_keyboard_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    dc->props = syborg_keyboard_properties;
    k->init = syborg_keyboard_init;
}

static const TypeInfo syborg_keyboard_info = {
    .name  = "syborg,keyboard",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(SyborgKeyboardState),
    .class_init = syborg_keyboard_class_init
};

static void syborg_keyboard_register_types(void)
{
    type_register_static(&syborg_keyboard_info);
}

type_init(syborg_keyboard_register_types)
