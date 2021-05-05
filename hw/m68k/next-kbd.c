/*
 * QEMU NeXT Keyboard/Mouse emulation
 *
 * Copyright (c) 2011 Bryce Lanham
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

/*
 * This is admittedly hackish, but works well enough for basic input. Mouse
 * support will be added once we can boot something that needs the mouse.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "hw/m68k/next-cube.h"
#include "ui/console.h"
#include "migration/vmstate.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(NextKBDState, NEXTKBD)

/* following defintions from next68k netbsd */
#define CSR_INT 0x00800000
#define CSR_DATA 0x00400000

#define KD_KEYMASK    0x007f
#define KD_DIRECTION  0x0080 /* pressed or released */
#define KD_CNTL       0x0100
#define KD_LSHIFT     0x0200
#define KD_RSHIFT     0x0400
#define KD_LCOMM      0x0800
#define KD_RCOMM      0x1000
#define KD_LALT       0x2000
#define KD_RALT       0x4000
#define KD_VALID      0x8000 /* only set for scancode keys ? */
#define KD_MODS       0x4f00

#define KBD_QUEUE_SIZE 256

typedef struct {
    uint8_t data[KBD_QUEUE_SIZE];
    int rptr, wptr, count;
} KBDQueue;


struct NextKBDState {
    SysBusDevice sbd;
    MemoryRegion mr;
    KBDQueue queue;
    uint16_t shift;
};

static void queue_code(void *opaque, int code);

/* lots of magic numbers here */
static uint32_t kbd_read_byte(void *opaque, hwaddr addr)
{
    switch (addr & 0x3) {
    case 0x0:   /* 0xe000 */
        return 0x80 | 0x20;

    case 0x1:   /* 0xe001 */
        return 0x80 | 0x40 | 0x20 | 0x10;

    case 0x2:   /* 0xe002 */
        /* returning 0x40 caused mach to hang */
        return 0x10 | 0x2 | 0x1;

    default:
        qemu_log_mask(LOG_UNIMP, "NeXT kbd read byte %"HWADDR_PRIx"\n", addr);
    }

    return 0;
}

static uint32_t kbd_read_word(void *opaque, hwaddr addr)
{
    qemu_log_mask(LOG_UNIMP, "NeXT kbd read word %"HWADDR_PRIx"\n", addr);
    return 0;
}

/* even more magic numbers */
static uint32_t kbd_read_long(void *opaque, hwaddr addr)
{
    int key = 0;
    NextKBDState *s = NEXTKBD(opaque);
    KBDQueue *q = &s->queue;

    switch (addr & 0xf) {
    case 0x0:   /* 0xe000 */
        return 0xA0F09300;

    case 0x8:   /* 0xe008 */
        /* get keycode from buffer */
        if (q->count > 0) {
            key = q->data[q->rptr];
            if (++q->rptr == KBD_QUEUE_SIZE) {
                q->rptr = 0;
            }

            q->count--;

            if (s->shift) {
                key |= s->shift;
            }

            if (key & 0x80) {
                return 0;
            } else {
                return 0x10000000 | KD_VALID | key;
            }
        } else {
            return 0;
        }

    default:
        qemu_log_mask(LOG_UNIMP, "NeXT kbd read long %"HWADDR_PRIx"\n", addr);
        return 0;
    }
}

static uint64_t kbd_readfn(void *opaque, hwaddr addr, unsigned size)
{
    switch (size) {
    case 1:
        return kbd_read_byte(opaque, addr);
    case 2:
        return kbd_read_word(opaque, addr);
    case 4:
        return kbd_read_long(opaque, addr);
    default:
        g_assert_not_reached();
    }
}

static void kbd_writefn(void *opaque, hwaddr addr, uint64_t value,
                        unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "NeXT kbd write: size=%u addr=0x%"HWADDR_PRIx
                  "val=0x%"PRIx64"\n", size, addr, value);
}

static const MemoryRegionOps kbd_ops = {
    .read = kbd_readfn,
    .write = kbd_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void nextkbd_event(void *opaque, int ch)
{
    /*
     * Will want to set vars for caps/num lock
     * if (ch & 0x80) -> key release
     * there's also e0 escaped scancodes that might need to be handled
     */
    queue_code(opaque, ch);
}

static const unsigned char next_keycodes[128] = {
    0x00, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x50, 0x4F,
    0x4E, 0x1E, 0x1F, 0x20, 0x1D, 0x1C, 0x1B, 0x00,
    0x42, 0x43, 0x44, 0x45, 0x48, 0x47, 0x46, 0x06,
    0x07, 0x08, 0x00, 0x00, 0x2A, 0x00, 0x39, 0x3A,
    0x3B, 0x3C, 0x3D, 0x40, 0x3F, 0x3E, 0x2D, 0x2C,
    0x2B, 0x26, 0x00, 0x00, 0x31, 0x32, 0x33, 0x34,
    0x35, 0x37, 0x36, 0x2e, 0x2f, 0x30, 0x00, 0x00,
    0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void queue_code(void *opaque, int code)
{
    NextKBDState *s = NEXTKBD(opaque);
    KBDQueue *q = &s->queue;
    int key = code & KD_KEYMASK;
    int release = code & 0x80;
    static int ext;

    if (code == 0xE0) {
        ext = 1;
    }

    if (code == 0x2A || code == 0x1D || code == 0x36) {
        if (code == 0x2A) {
            s->shift = KD_LSHIFT;
        } else if (code == 0x36) {
            s->shift = KD_RSHIFT;
            ext = 0;
        } else if (code == 0x1D && !ext) {
            s->shift = KD_LCOMM;
        } else if (code == 0x1D && ext) {
            ext = 0;
            s->shift = KD_RCOMM;
        }
        return;
    } else if (code == (0x2A | 0x80) || code == (0x1D | 0x80) ||
               code == (0x36 | 0x80)) {
        s->shift = 0;
        return;
    }

    if (q->count >= KBD_QUEUE_SIZE) {
        return;
    }

    q->data[q->wptr] = next_keycodes[key] | release;

    if (++q->wptr == KBD_QUEUE_SIZE) {
        q->wptr = 0;
    }

    q->count++;

    /*
     * might need to actually trigger the NeXT irq, but as the keyboard works
     * at the moment, I'll worry about it later
     */
    /* s->update_irq(s->update_arg, 1); */
}

static void nextkbd_reset(DeviceState *dev)
{
    NextKBDState *nks = NEXTKBD(dev);

    memset(&nks->queue, 0, sizeof(KBDQueue));
    nks->shift = 0;
}

static void nextkbd_realize(DeviceState *dev, Error **errp)
{
    NextKBDState *s = NEXTKBD(dev);

    memory_region_init_io(&s->mr, OBJECT(dev), &kbd_ops, s, "next.kbd", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);

    qemu_add_kbd_event_handler(nextkbd_event, s);
}

static const VMStateDescription nextkbd_vmstate = {
    .name = TYPE_NEXTKBD,
    .unmigratable = 1,    /* TODO: Implement this when m68k CPU is migratable */
};

static void nextkbd_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->vmsd = &nextkbd_vmstate;
    dc->realize = nextkbd_realize;
    dc->reset = nextkbd_reset;
}

static const TypeInfo nextkbd_info = {
    .name          = TYPE_NEXTKBD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NextKBDState),
    .class_init    = nextkbd_class_init,
};

static void nextkbd_register_types(void)
{
    type_register_static(&nextkbd_info);
}

type_init(nextkbd_register_types)
