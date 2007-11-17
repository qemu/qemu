/*
 * QEMU VMMouse emulation
 *
 * Copyright (C) 2007 Anthony Liguori <anthony@codemonkey.ws>
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
#include "hw.h"
#include "console.h"
#include "ps2.h"
#include "pc.h"

/* debug only vmmouse */
//#define DEBUG_VMMOUSE

/* VMMouse Commands */
#define VMMOUSE_GETVERSION	10
#define VMMOUSE_DATA		39
#define VMMOUSE_STATUS		40
#define VMMOUSE_COMMAND		41

#define VMMOUSE_READ_ID			0x45414552
#define VMMOUSE_DISABLE			0x000000f5
#define VMMOUSE_REQUEST_RELATIVE	0x4c455252
#define VMMOUSE_REQUEST_ABSOLUTE	0x53424152

#define VMMOUSE_QUEUE_SIZE	1024

#define VMMOUSE_VERSION		0x3442554a

#ifdef DEBUG_VMMOUSE
#define DPRINTF(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

typedef struct _VMMouseState
{
    uint32_t queue[VMMOUSE_QUEUE_SIZE];
    uint16_t nb_queue;
    uint16_t status;
    uint8_t absolute;
    QEMUPutMouseEntry *entry;
    void *ps2_mouse;
} VMMouseState;

static uint32_t vmmouse_get_status(VMMouseState *s)
{
    DPRINTF("vmmouse_get_status()\n");
    return (s->status << 16) | s->nb_queue;
}

static void vmmouse_mouse_event(void *opaque, int x, int y, int dz, int buttons_state)
{
    VMMouseState *s = opaque;
    int buttons = 0;

    if (s->nb_queue > (VMMOUSE_QUEUE_SIZE - 4))
        return;

    DPRINTF("vmmouse_mouse_event(%d, %d, %d, %d)\n",
            x, y, dz, buttons_state);

    if ((buttons_state & MOUSE_EVENT_LBUTTON))
        buttons |= 0x20;
    if ((buttons_state & MOUSE_EVENT_RBUTTON))
        buttons |= 0x10;
    if ((buttons_state & MOUSE_EVENT_MBUTTON))
        buttons |= 0x08;

    if (s->absolute) {
        x <<= 1;
        y <<= 1;
    }

    s->queue[s->nb_queue++] = buttons;
    s->queue[s->nb_queue++] = x;
    s->queue[s->nb_queue++] = y;
    s->queue[s->nb_queue++] = dz;

    /* need to still generate PS2 events to notify driver to
       read from queue */
    ps2_mouse_fake_event(s->ps2_mouse);
}

static void vmmouse_update_handler(VMMouseState *s)
{
    if (s->entry) {
        qemu_remove_mouse_event_handler(s->entry);
        s->entry = NULL;
    }
    if (s->status == 0)
        s->entry = qemu_add_mouse_event_handler(vmmouse_mouse_event,
                                                s, s->absolute,
                                                "vmmouse");
}

static void vmmouse_read_id(VMMouseState *s)
{
    DPRINTF("vmmouse_read_id()\n");

    if (s->nb_queue == VMMOUSE_QUEUE_SIZE)
        return;

    s->queue[s->nb_queue++] = VMMOUSE_VERSION;
    s->status = 0;
    vmmouse_update_handler(s);
}

static void vmmouse_request_relative(VMMouseState *s)
{
    DPRINTF("vmmouse_request_relative()\n");
    s->absolute = 0;
    vmmouse_update_handler(s);
}

static void vmmouse_request_absolute(VMMouseState *s)
{
    DPRINTF("vmmouse_request_absolute()\n");
    s->absolute = 1;
    vmmouse_update_handler(s);
}

static void vmmouse_disable(VMMouseState *s)
{
    DPRINTF("vmmouse_disable()\n");
    s->status = 0xffff;
    vmmouse_update_handler(s);
}

static void vmmouse_data(VMMouseState *s, uint32_t *data, uint32_t size)
{
    int i;

    DPRINTF("vmmouse_data(%d)\n", size);

    if (size == 0 || size > 6 || size > s->nb_queue) {
        printf("vmmouse: driver requested too much data %d\n", size);
        s->status = 0xffff;
        vmmouse_update_handler(s);
        return;
    }

    for (i = 0; i < size; i++)
        data[i] = s->queue[i];

    s->nb_queue -= size;
    if (s->nb_queue)
        memmove(s->queue, &s->queue[size], sizeof(s->queue[0]) * s->nb_queue);
}

static void vmmouse_get_data(uint32_t *data)
{
    CPUState *env = cpu_single_env;

    data[0] = env->regs[R_EAX]; data[1] = env->regs[R_EBX];
    data[2] = env->regs[R_ECX]; data[3] = env->regs[R_EDX];
    data[4] = env->regs[R_ESI]; data[5] = env->regs[R_EDI];

    DPRINTF("get_data = {%x, %x, %x, %x, %x, %x}\n",
            data[0], data[1], data[2], data[3], data[4], data[5]);
}

static void vmmouse_set_data(const uint32_t *data)
{
    CPUState *env = cpu_single_env;

    DPRINTF("set_data = {%x, %x, %x, %x, %x, %x}\n",
            data[0], data[1], data[2], data[3], data[4], data[5]);

    env->regs[R_EAX] = data[0]; env->regs[R_EBX] = data[1];
    env->regs[R_ECX] = data[2]; env->regs[R_EDX] = data[3];
    env->regs[R_ESI] = data[4]; env->regs[R_EDI] = data[5];
}

static uint32_t vmmouse_ioport_read(void *opaque, uint32_t addr)
{
    VMMouseState *s = opaque;
    uint32_t data[6];
    uint16_t command;

    vmmouse_get_data(data);

    command = data[2] & 0xFFFF;

    switch (command) {
    case VMMOUSE_STATUS:
        data[0] = vmmouse_get_status(s);
        break;
    case VMMOUSE_COMMAND:
        switch (data[1]) {
        case VMMOUSE_DISABLE:
            vmmouse_disable(s);
            break;
        case VMMOUSE_READ_ID:
            vmmouse_read_id(s);
            break;
        case VMMOUSE_REQUEST_RELATIVE:
            vmmouse_request_relative(s);
            break;
        case VMMOUSE_REQUEST_ABSOLUTE:
            vmmouse_request_absolute(s);
            break;
        default:
            printf("vmmouse: unknown command %x\n", data[1]);
            break;
        }
        break;
    case VMMOUSE_DATA:
        vmmouse_data(s, data, data[1]);
        break;
    default:
        printf("vmmouse: unknown command %x\n", command);
        break;
    }

    vmmouse_set_data(data);
    return data[0];
}

static void vmmouse_save(QEMUFile *f, void *opaque)
{
    VMMouseState *s = opaque;
    int i;

    qemu_put_be32(f, VMMOUSE_QUEUE_SIZE);
    for (i = 0; i < VMMOUSE_QUEUE_SIZE; i++)
        qemu_put_be32s(f, &s->queue[i]);
    qemu_put_be16s(f, &s->nb_queue);
    qemu_put_be16s(f, &s->status);
    qemu_put_8s(f, &s->absolute);
}

static int vmmouse_load(QEMUFile *f, void *opaque, int version_id)
{
    VMMouseState *s = opaque;
    int i;

    if (version_id != 0)
        return -EINVAL;

    if (qemu_get_be32(f) != VMMOUSE_QUEUE_SIZE)
        return -EINVAL;
    for (i = 0; i < VMMOUSE_QUEUE_SIZE; i++)
        qemu_get_be32s(f, &s->queue[i]);
    qemu_get_be16s(f, &s->nb_queue);
    qemu_get_be16s(f, &s->status);
    qemu_get_8s(f, &s->absolute);

    vmmouse_update_handler(s);

    return 0;
}

void *vmmouse_init(void *m)
{
    VMMouseState *s = NULL;

    DPRINTF("vmmouse_init\n");

    s = qemu_mallocz(sizeof(VMMouseState));
    if (!s)
        return NULL;

    s->status = 0xffff;
    s->ps2_mouse = m;

    vmport_register(VMMOUSE_STATUS, vmmouse_ioport_read, s);
    vmport_register(VMMOUSE_COMMAND, vmmouse_ioport_read, s);
    vmport_register(VMMOUSE_DATA, vmmouse_ioport_read, s);
    register_savevm("vmmouse", 0, 0, vmmouse_save, vmmouse_load, s);

    return s;
}

