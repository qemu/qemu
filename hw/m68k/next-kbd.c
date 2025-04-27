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

/* following definitions from next68k netbsd */
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
    .endianness = DEVICE_BIG_ENDIAN,
};

static const int qcode_to_nextkbd_keycode[] = {
    [Q_KEY_CODE_ESC]           = 0x49,
    [Q_KEY_CODE_1]             = 0x4a,
    [Q_KEY_CODE_2]             = 0x4b,
    [Q_KEY_CODE_3]             = 0x4c,
    [Q_KEY_CODE_4]             = 0x4d,
    [Q_KEY_CODE_5]             = 0x50,
    [Q_KEY_CODE_6]             = 0x4f,
    [Q_KEY_CODE_7]             = 0x4e,
    [Q_KEY_CODE_8]             = 0x1e,
    [Q_KEY_CODE_9]             = 0x1f,
    [Q_KEY_CODE_0]             = 0x20,
    [Q_KEY_CODE_MINUS]         = 0x1d,
    [Q_KEY_CODE_EQUAL]         = 0x1c,
    [Q_KEY_CODE_BACKSPACE]     = 0x1b,

    [Q_KEY_CODE_Q]             = 0x42,
    [Q_KEY_CODE_W]             = 0x43,
    [Q_KEY_CODE_E]             = 0x44,
    [Q_KEY_CODE_R]             = 0x45,
    [Q_KEY_CODE_T]             = 0x48,
    [Q_KEY_CODE_Y]             = 0x47,
    [Q_KEY_CODE_U]             = 0x46,
    [Q_KEY_CODE_I]             = 0x06,
    [Q_KEY_CODE_O]             = 0x07,
    [Q_KEY_CODE_P]             = 0x08,
    [Q_KEY_CODE_RET]           = 0x2a,
    [Q_KEY_CODE_A]             = 0x39,
    [Q_KEY_CODE_S]             = 0x3a,

    [Q_KEY_CODE_D]             = 0x3b,
    [Q_KEY_CODE_F]             = 0x3c,
    [Q_KEY_CODE_G]             = 0x3d,
    [Q_KEY_CODE_H]             = 0x40,
    [Q_KEY_CODE_J]             = 0x3f,
    [Q_KEY_CODE_K]             = 0x3e,
    [Q_KEY_CODE_L]             = 0x2d,
    [Q_KEY_CODE_SEMICOLON]     = 0x2c,
    [Q_KEY_CODE_APOSTROPHE]    = 0x2b,
    [Q_KEY_CODE_GRAVE_ACCENT]  = 0x26,
    [Q_KEY_CODE_Z]             = 0x31,
    [Q_KEY_CODE_X]             = 0x32,
    [Q_KEY_CODE_C]             = 0x33,
    [Q_KEY_CODE_V]             = 0x34,

    [Q_KEY_CODE_B]             = 0x35,
    [Q_KEY_CODE_N]             = 0x37,
    [Q_KEY_CODE_M]             = 0x36,
    [Q_KEY_CODE_COMMA]         = 0x2e,
    [Q_KEY_CODE_DOT]           = 0x2f,
    [Q_KEY_CODE_SLASH]         = 0x30,

    [Q_KEY_CODE_SPC]           = 0x38,
};

static void nextkbd_put_keycode(NextKBDState *s, int keycode)
{
    KBDQueue *q = &s->queue;

    if (q->count >= KBD_QUEUE_SIZE) {
        return;
    }

    q->data[q->wptr] = keycode;
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

static void nextkbd_event(DeviceState *dev, QemuConsole *src, InputEvent *evt)
{
    NextKBDState *s = NEXTKBD(dev);
    int qcode, keycode;
    bool key_down = evt->u.key.data->down;

    qcode = qemu_input_key_value_to_qcode(evt->u.key.data->key);
    if (qcode >= ARRAY_SIZE(qcode_to_nextkbd_keycode)) {
        return;
    }

    /* Shift key currently has no keycode, so handle separately */
    if (qcode == Q_KEY_CODE_SHIFT) {
        if (key_down) {
            s->shift |= KD_LSHIFT;
        } else {
            s->shift &= ~KD_LSHIFT;
        }
    }

    if (qcode == Q_KEY_CODE_SHIFT_R) {
        if (key_down) {
            s->shift |= KD_RSHIFT;
        } else {
            s->shift &= ~KD_RSHIFT;
        }
    }

    keycode = qcode_to_nextkbd_keycode[qcode];
    if (!keycode) {
        return;
    }

    /* If key release event, create keyboard break code */
    if (!key_down) {
        keycode |= 0x80;
    }

    nextkbd_put_keycode(s, keycode);
}

static const QemuInputHandler nextkbd_handler = {
    .name  = "QEMU NeXT Keyboard",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = nextkbd_event,
};

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

    qemu_input_handler_register(dev, &nextkbd_handler);
}

static const VMStateDescription nextkbd_vmstate = {
    .name = TYPE_NEXTKBD,
    .unmigratable = 1,    /* TODO: Implement this when m68k CPU is migratable */
};

static void nextkbd_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->vmsd = &nextkbd_vmstate;
    dc->realize = nextkbd_realize;
    device_class_set_legacy_reset(dc, nextkbd_reset);
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
