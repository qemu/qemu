/*
 * Syborg keyboard controller.
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

static uint32_t syborg_keyboard_read(void *opaque, target_phys_addr_t offset)
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

static void syborg_keyboard_write(void *opaque, target_phys_addr_t offset,
                                  uint32_t value)
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

static CPUReadMemoryFunc * const syborg_keyboard_readfn[] = {
     syborg_keyboard_read,
     syborg_keyboard_read,
     syborg_keyboard_read
};

static CPUWriteMemoryFunc * const syborg_keyboard_writefn[] = {
     syborg_keyboard_write,
     syborg_keyboard_write,
     syborg_keyboard_write
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

static int syborg_keyboard_init(SysBusDevice *dev)
{
    SyborgKeyboardState *s = FROM_SYSBUS(SyborgKeyboardState, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->irq);
    iomemtype = cpu_register_io_memory(syborg_keyboard_readfn,
                                       syborg_keyboard_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    if (s->fifo_size <= 0) {
        fprintf(stderr, "syborg_keyboard: fifo too small\n");
        s->fifo_size = 16;
    }
    s->key_fifo = qemu_mallocz(s->fifo_size * sizeof(s->key_fifo[0]));

    qemu_add_kbd_event_handler(syborg_keyboard_event, s);

    vmstate_register(&dev->qdev, -1, &vmstate_syborg_keyboard, s);
    return 0;
}

static SysBusDeviceInfo syborg_keyboard_info = {
    .init = syborg_keyboard_init,
    .qdev.name  = "syborg,keyboard",
    .qdev.size  = sizeof(SyborgKeyboardState),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("fifo-size", SyborgKeyboardState, fifo_size, 16),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void syborg_keyboard_register_devices(void)
{
    sysbus_register_withprop(&syborg_keyboard_info);
}

device_init(syborg_keyboard_register_devices)
