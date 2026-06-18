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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "hw/i386/vmport.h"
#include "hw/input/i8042.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "target/i386/cpu.h"
#include "qom/object.h"

#include "trace.h"

/* debug only vmmouse */
//#define DEBUG_VMMOUSE

#define VMMOUSE_READ_ID			0x45414552
#define VMMOUSE_DISABLE			0x000000f5
#define VMMOUSE_REQUEST_RELATIVE	0x4c455252
#define VMMOUSE_REQUEST_ABSOLUTE	0x53424152

#define VMMOUSE_QUEUE_SIZE	1024

#define VMMOUSE_VERSION		0x3442554a

#define VMMOUSE_RELATIVE_PACKET    0x00010000

#define VMMOUSE_LEFT_BUTTON        0x20
#define VMMOUSE_RIGHT_BUTTON       0x10
#define VMMOUSE_MIDDLE_BUTTON      0x08

#define VMMOUSE_MIN_X 0
#define VMMOUSE_MIN_Y 0
#define VMMOUSE_MAX_X 0xFFFF
#define VMMOUSE_MAX_Y 0xFFFF

#define TYPE_VMMOUSE "vmmouse"
OBJECT_DECLARE_SIMPLE_TYPE(VMMouseState, VMMOUSE)

struct VMMouseState {
    ISADevice parent_obj;

    uint32_t queue[VMMOUSE_QUEUE_SIZE];
    int32_t queue_size;
    uint16_t nb_queue;
    uint16_t status;
    uint8_t absolute;
    QemuInputHandlerState *hs;
    int axis[INPUT_AXIS__MAX];
    int dz;
    bool btns[INPUT_BUTTON__MAX];
    ISAKBDState *i8042;
};

static void vmmouse_get_data(uint64_t *data)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    CPUX86State *env = &cpu->env;

    data[0] = env->regs[R_EAX]; data[1] = env->regs[R_EBX];
    data[2] = env->regs[R_ECX]; data[3] = env->regs[R_EDX];
    data[4] = env->regs[R_ESI]; data[5] = env->regs[R_EDI];
}

static void vmmouse_set_data(const uint64_t *data)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    CPUX86State *env = &cpu->env;

    env->regs[R_EAX] = data[0]; env->regs[R_EBX] = data[1];
    env->regs[R_ECX] = data[2]; env->regs[R_EDX] = data[3];
    env->regs[R_ESI] = data[4]; env->regs[R_EDI] = data[5];
}

static uint32_t vmmouse_get_status(VMMouseState *s)
{
    trace_vmmouse_get_status();

    return (s->status << 16) | s->nb_queue;
}

static void vmmouse_input_event(DeviceState *dev, QemuConsole *src,
                                QemuInputEvent *evt)
{
    VMMouseState *s = VMMOUSE(dev);

    switch (evt->type) {
    case INPUT_EVENT_KIND_BTN:
        if (evt->btn.down) {
            if (evt->btn.button == INPUT_BUTTON_WHEEL_UP) {
                s->dz--;
            } else if (evt->btn.button == INPUT_BUTTON_WHEEL_DOWN) {
                s->dz++;
            }
        }
        s->btns[evt->btn.button] = evt->btn.down;
        break;
    case INPUT_EVENT_KIND_ABS:
        if (evt->abs.axis == INPUT_AXIS_X) {
            s->axis[INPUT_AXIS_X] =
                qemu_input_scale_axis(evt->abs.value,
                                      INPUT_EVENT_ABS_MIN, INPUT_EVENT_ABS_MAX,
                                      VMMOUSE_MIN_X, VMMOUSE_MAX_X);
        } else if (evt->abs.axis == INPUT_AXIS_Y) {
            s->axis[INPUT_AXIS_Y] =
                qemu_input_scale_axis(evt->abs.value,
                                      INPUT_EVENT_ABS_MIN, INPUT_EVENT_ABS_MAX,
                                      VMMOUSE_MIN_Y, VMMOUSE_MAX_Y);
        }
        break;
    case INPUT_EVENT_KIND_REL:
        s->axis[evt->rel.axis] += evt->rel.value;
        break;
    default:
        break;
    }
}

static void vmmouse_input_sync(DeviceState *dev)
{
    VMMouseState *s = VMMOUSE(dev);
    int buttons = 0;

    if (s->nb_queue > (VMMOUSE_QUEUE_SIZE - 4)) {
        return;
    }

    if (s->btns[INPUT_BUTTON_LEFT]) {
        buttons |= VMMOUSE_LEFT_BUTTON;
    }
    if (s->btns[INPUT_BUTTON_RIGHT]) {
        buttons |= VMMOUSE_RIGHT_BUTTON;
    }
    if (s->btns[INPUT_BUTTON_MIDDLE]) {
        buttons |= VMMOUSE_MIDDLE_BUTTON;
    }
    if (!s->absolute) {
        buttons |= VMMOUSE_RELATIVE_PACKET;
    }

    trace_vmmouse_queue_event(s->axis[INPUT_AXIS_X], s->axis[INPUT_AXIS_Y],
                              s->dz, buttons);

    s->queue[s->nb_queue++] = buttons;
    s->queue[s->nb_queue++] = s->axis[INPUT_AXIS_X];
    s->queue[s->nb_queue++] = s->axis[INPUT_AXIS_Y];
    s->queue[s->nb_queue++] = s->dz;
    s->dz = 0;

    if (!s->absolute) {
        s->axis[INPUT_AXIS_X] = 0;
        s->axis[INPUT_AXIS_Y] = 0;
    }

    /* need to still generate PS2 events to notify driver to
       read from queue */
    i8042_isa_mouse_fake_event(s->i8042);
}

static void vmmouse_remove_handler(VMMouseState *s)
{
    g_clear_pointer(&s->hs, qemu_input_handler_unregister);
}

static void vmmouse_update_handler(VMMouseState *s, int absolute)
{
    static const QemuInputHandler vmmouse_abs_handler = {
        .name  = "vmmouse",
        .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
        .event = vmmouse_input_event,
        .sync  = vmmouse_input_sync,
    };

    static const QemuInputHandler vmmouse_rel_handler = {
        .name  = "vmmouse",
        .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
        .event = vmmouse_input_event,
        .sync  = vmmouse_input_sync,
    };

    if (s->status != 0) {
        return;
    }
    if (s->absolute != absolute) {
        s->absolute = absolute;
        vmmouse_remove_handler(s);
    }
    if (s->hs == NULL) {
        const QemuInputHandler *h = s->absolute ?
            &vmmouse_abs_handler : &vmmouse_rel_handler;
        s->hs = qemu_input_handler_register(DEVICE(s), h);
        qemu_input_handler_activate(s->hs);
    }
}

static void vmmouse_read_id(VMMouseState *s)
{
    trace_vmmouse_read_id();

    if (s->nb_queue == VMMOUSE_QUEUE_SIZE)
        return;

    s->queue[s->nb_queue++] = VMMOUSE_VERSION;
    s->status = 0;
    vmmouse_update_handler(s, s->absolute);
}

static void vmmouse_request_relative(VMMouseState *s)
{
    trace_vmmouse_request_relative();

    vmmouse_update_handler(s, 0);
}

static void vmmouse_request_absolute(VMMouseState *s)
{
    trace_vmmouse_request_absolute();

    vmmouse_update_handler(s, 1);
}

static void vmmouse_disable(VMMouseState *s)
{
    trace_vmmouse_disable();

    s->status = 0xffff;
    vmmouse_remove_handler(s);
}

static void vmmouse_data(VMMouseState *s, uint64_t *data, uint32_t size)
{
    int i;

    trace_vmmouse_data(size);

    if (size == 0 || size > 6 || size > s->nb_queue) {
        printf("vmmouse: driver requested too much data %d\n", size);
        s->status = 0xffff;
        vmmouse_remove_handler(s);
        return;
    }

    for (i = 0; i < size; i++)
        data[i] = s->queue[i];

    s->nb_queue -= size;
    if (s->nb_queue)
        memmove(s->queue, &s->queue[size], sizeof(s->queue[0]) * s->nb_queue);
}

static uint32_t vmmouse_ioport_read(void *opaque, uint32_t addr)
{
    VMMouseState *s = opaque;
    uint64_t data[6];
    uint16_t command;

    vmmouse_get_data(data);

    command = data[2] & 0xFFFF;

    switch (command) {
    case VMPORT_CMD_VMMOUSE_STATUS:
        data[0] = vmmouse_get_status(s);
        break;
    case VMPORT_CMD_VMMOUSE_COMMAND:
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
            printf("vmmouse: unknown command %" PRIx64 "\n", data[1]);
            break;
        }
        break;
    case VMPORT_CMD_VMMOUSE_DATA:
        vmmouse_data(s, data, data[1]);
        break;
    default:
        printf("vmmouse: unknown command %x\n", command);
        break;
    }

    vmmouse_set_data(data);
    return data[0];
}

static int vmmouse_post_load(void *opaque, int version_id)
{
    VMMouseState *s = opaque;

    vmmouse_remove_handler(s);
    vmmouse_update_handler(s, s->absolute);
    return 0;
}

static const VMStateDescription vmstate_vmmouse = {
    .name = "vmmouse",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = vmmouse_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32_EQUAL(queue_size, VMMouseState),
        VMSTATE_UINT32_ARRAY(queue, VMMouseState, VMMOUSE_QUEUE_SIZE),
        VMSTATE_UINT16(nb_queue, VMMouseState),
        VMSTATE_UINT16(status, VMMouseState),
        VMSTATE_UINT8(absolute, VMMouseState),
        VMSTATE_END_OF_LIST()
    }
};

static void vmmouse_reset(DeviceState *d)
{
    VMMouseState *s = VMMOUSE(d);

    s->queue_size = VMMOUSE_QUEUE_SIZE;
    s->nb_queue = 0;

    vmmouse_disable(s);
}

static void vmmouse_realizefn(DeviceState *dev, Error **errp)
{
    VMMouseState *s = VMMOUSE(dev);

    trace_vmmouse_init();

    if (!s->i8042) {
        error_setg(errp, "'i8042' link is not set");
        return;
    }
    if (!object_resolve_path_type("", TYPE_VMPORT, NULL)) {
        error_setg(errp, "vmmouse needs a machine with vmport");
        return;
    }

    vmport_register(VMPORT_CMD_VMMOUSE_STATUS, vmmouse_ioport_read, s);
    vmport_register(VMPORT_CMD_VMMOUSE_COMMAND, vmmouse_ioport_read, s);
    vmport_register(VMPORT_CMD_VMMOUSE_DATA, vmmouse_ioport_read, s);
}

static const Property vmmouse_properties[] = {
    DEFINE_PROP_LINK("i8042", VMMouseState, i8042, TYPE_I8042, ISAKBDState *),
};

static void vmmouse_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vmmouse_realizefn;
    device_class_set_legacy_reset(dc, vmmouse_reset);
    dc->vmsd = &vmstate_vmmouse;
    device_class_set_props(dc, vmmouse_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo vmmouse_info = {
    .name          = TYPE_VMMOUSE,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(VMMouseState),
    .class_init    = vmmouse_class_initfn,
};

static void vmmouse_register_types(void)
{
    type_register_static(&vmmouse_info);
}

type_init(vmmouse_register_types)
