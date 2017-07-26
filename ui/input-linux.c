/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "qemu/sockets.h"
#include "sysemu/sysemu.h"
#include "ui/input.h"
#include "qom/object_interfaces.h"

#include <sys/ioctl.h>
#include "standard-headers/linux/input.h"

static bool linux_is_button(unsigned int lnx)
{
    if (lnx < 0x100) {
        return false;
    }
    if (lnx >= 0x160 && lnx < 0x2c0) {
        return false;
    }
    return true;
}

#define TYPE_INPUT_LINUX "input-linux"
#define INPUT_LINUX(obj) \
    OBJECT_CHECK(InputLinux, (obj), TYPE_INPUT_LINUX)
#define INPUT_LINUX_GET_CLASS(obj) \
    OBJECT_GET_CLASS(InputLinuxClass, (obj), TYPE_INPUT_LINUX)
#define INPUT_LINUX_CLASS(klass) \
    OBJECT_CLASS_CHECK(InputLinuxClass, (klass), TYPE_INPUT_LINUX)

typedef struct InputLinux InputLinux;
typedef struct InputLinuxClass InputLinuxClass;

struct InputLinux {
    Object parent;

    char        *evdev;
    int         fd;
    bool        repeat;
    bool        grab_request;
    bool        grab_active;
    bool        grab_all;
    bool        keydown[KEY_CNT];
    int         keycount;
    int         wheel;
    bool        initialized;

    bool        has_rel_x;
    bool        has_abs_x;
    int         num_keys;
    int         num_btns;
    int         abs_x_min;
    int         abs_x_max;
    int         abs_y_min;
    int         abs_y_max;
    struct input_event event;
    int         read_offset;

    QTAILQ_ENTRY(InputLinux) next;
};

struct InputLinuxClass {
    ObjectClass parent_class;
};

static QTAILQ_HEAD(, InputLinux) inputs = QTAILQ_HEAD_INITIALIZER(inputs);

static void input_linux_toggle_grab(InputLinux *il)
{
    intptr_t request = !il->grab_active;
    InputLinux *item;
    int rc;

    rc = ioctl(il->fd, EVIOCGRAB, request);
    if (rc < 0) {
        return;
    }
    il->grab_active = !il->grab_active;

    if (!il->grab_all) {
        return;
    }
    QTAILQ_FOREACH(item, &inputs, next) {
        if (item == il || item->grab_all) {
            /* avoid endless loops */
            continue;
        }
        if (item->grab_active != il->grab_active) {
            input_linux_toggle_grab(item);
        }
    }
}

static void input_linux_handle_keyboard(InputLinux *il,
                                        struct input_event *event)
{
    if (event->type == EV_KEY) {
        if (event->value > 2 || (event->value > 1 && !il->repeat)) {
            /*
             * ignore autorepeat + unknown key events
             * 0 == up, 1 == down, 2 == autorepeat, other == undefined
             */
            return;
        }
        if (event->code >= KEY_CNT) {
            /*
             * Should not happen.  But better safe than sorry,
             * and we make Coverity happy too.
             */
            return;
        }

        /* keep track of key state */
        if (!il->keydown[event->code] && event->value) {
            il->keydown[event->code] = true;
            il->keycount++;
        }
        if (il->keydown[event->code] && !event->value) {
            il->keydown[event->code] = false;
            il->keycount--;
        }

        /* send event to guest when grab is active */
        if (il->grab_active) {
            int qcode = qemu_input_linux_to_qcode(event->code);
            qemu_input_event_send_key_qcode(NULL, qcode, event->value);
        }

        /* hotkey -> record switch request ... */
        if (il->keydown[KEY_LEFTCTRL] &&
            il->keydown[KEY_RIGHTCTRL]) {
            il->grab_request = true;
        }

        /*
         * ... and do the switch when all keys are lifted, so we
         * confuse neither guest nor host with keys which seem to
         * be stuck due to missing key-up events.
         */
        if (il->grab_request && !il->keycount) {
            il->grab_request = false;
            input_linux_toggle_grab(il);
        }
    }
}

static void input_linux_event_mouse_button(int button)
{
    qemu_input_queue_btn(NULL, button, true);
    qemu_input_event_sync();
    qemu_input_queue_btn(NULL, button, false);
    qemu_input_event_sync();
}

static void input_linux_handle_mouse(InputLinux *il, struct input_event *event)
{
    if (!il->grab_active) {
        return;
    }

    switch (event->type) {
    case EV_KEY:
        switch (event->code) {
        case BTN_LEFT:
            qemu_input_queue_btn(NULL, INPUT_BUTTON_LEFT, event->value);
            break;
        case BTN_RIGHT:
            qemu_input_queue_btn(NULL, INPUT_BUTTON_RIGHT, event->value);
            break;
        case BTN_MIDDLE:
            qemu_input_queue_btn(NULL, INPUT_BUTTON_MIDDLE, event->value);
            break;
        case BTN_GEAR_UP:
            qemu_input_queue_btn(NULL, INPUT_BUTTON_WHEEL_UP, event->value);
            break;
        case BTN_GEAR_DOWN:
            qemu_input_queue_btn(NULL, INPUT_BUTTON_WHEEL_DOWN,
                                 event->value);
            break;
        case BTN_SIDE:
            qemu_input_queue_btn(NULL, INPUT_BUTTON_SIDE, event->value);
            break;
        case BTN_EXTRA:
            qemu_input_queue_btn(NULL, INPUT_BUTTON_EXTRA, event->value);
            break;
        };
        break;
    case EV_REL:
        switch (event->code) {
        case REL_X:
            qemu_input_queue_rel(NULL, INPUT_AXIS_X, event->value);
            break;
        case REL_Y:
            qemu_input_queue_rel(NULL, INPUT_AXIS_Y, event->value);
            break;
        case REL_WHEEL:
            il->wheel = event->value;
            break;
        }
        break;
    case EV_ABS:
        switch (event->code) {
        case ABS_X:
            qemu_input_queue_abs(NULL, INPUT_AXIS_X, event->value,
                                 il->abs_x_min, il->abs_x_max);
            break;
        case ABS_Y:
            qemu_input_queue_abs(NULL, INPUT_AXIS_Y, event->value,
                                 il->abs_y_min, il->abs_y_max);
            break;
        }
        break;
    case EV_SYN:
        qemu_input_event_sync();
        if (il->wheel != 0) {
            input_linux_event_mouse_button((il->wheel > 0)
                                           ? INPUT_BUTTON_WHEEL_UP
                                           : INPUT_BUTTON_WHEEL_DOWN);
            il->wheel = 0;
        }
        break;
    }
}

static void input_linux_event(void *opaque)
{
    InputLinux *il = opaque;
    int rc;
    int read_size;
    uint8_t *p = (uint8_t *)&il->event;

    for (;;) {
        read_size = sizeof(il->event) - il->read_offset;
        rc = read(il->fd, &p[il->read_offset], read_size);
        if (rc != read_size) {
            if (rc < 0 && errno != EAGAIN) {
                fprintf(stderr, "%s: read: %s\n", __func__, strerror(errno));
                qemu_set_fd_handler(il->fd, NULL, NULL, NULL);
                close(il->fd);
            } else if (rc > 0) {
                il->read_offset += rc;
            }
            break;
        }
        il->read_offset = 0;

        if (il->num_keys) {
            input_linux_handle_keyboard(il, &il->event);
        }
        if ((il->has_rel_x || il->has_abs_x) && il->num_btns) {
            input_linux_handle_mouse(il, &il->event);
        }
    }
}

static void input_linux_complete(UserCreatable *uc, Error **errp)
{
    InputLinux *il = INPUT_LINUX(uc);
    uint8_t evtmap, relmap, absmap;
    uint8_t keymap[KEY_CNT / 8], keystate[KEY_CNT / 8];
    unsigned int i;
    int rc, ver;
    struct input_absinfo absinfo;

    if (!il->evdev) {
        error_setg(errp, "no input device specified");
        return;
    }

    il->fd = open(il->evdev, O_RDWR);
    if (il->fd < 0)  {
        error_setg_file_open(errp, errno, il->evdev);
        return;
    }
    qemu_set_nonblock(il->fd);

    rc = ioctl(il->fd, EVIOCGVERSION, &ver);
    if (rc < 0) {
        error_setg(errp, "%s: is not an evdev device", il->evdev);
        goto err_close;
    }

    rc = ioctl(il->fd, EVIOCGBIT(0, sizeof(evtmap)), &evtmap);
    if (rc < 0) {
        error_setg(errp, "%s: failed to read event bits", il->evdev);
        goto err_close;
    }

    if (evtmap & (1 << EV_REL)) {
        relmap = 0;
        rc = ioctl(il->fd, EVIOCGBIT(EV_REL, sizeof(relmap)), &relmap);
        if (relmap & (1 << REL_X)) {
            il->has_rel_x = true;
        }
    }

    if (evtmap & (1 << EV_ABS)) {
        absmap = 0;
        rc = ioctl(il->fd, EVIOCGBIT(EV_ABS, sizeof(absmap)), &absmap);
        if (absmap & (1 << ABS_X)) {
            il->has_abs_x = true;
            rc = ioctl(il->fd, EVIOCGABS(ABS_X), &absinfo);
            il->abs_x_min = absinfo.minimum;
            il->abs_x_max = absinfo.maximum;
            rc = ioctl(il->fd, EVIOCGABS(ABS_Y), &absinfo);
            il->abs_y_min = absinfo.minimum;
            il->abs_y_max = absinfo.maximum;
        }
    }

    if (evtmap & (1 << EV_KEY)) {
        memset(keymap, 0, sizeof(keymap));
        rc = ioctl(il->fd, EVIOCGBIT(EV_KEY, sizeof(keymap)), keymap);
        rc = ioctl(il->fd, EVIOCGKEY(sizeof(keystate)), keystate);
        for (i = 0; i < KEY_CNT; i++) {
            if (keymap[i / 8] & (1 << (i % 8))) {
                if (linux_is_button(i)) {
                    il->num_btns++;
                } else {
                    il->num_keys++;
                }
                if (keystate[i / 8] & (1 << (i % 8))) {
                    il->keydown[i] = true;
                    il->keycount++;
                }
            }
        }
    }

    qemu_set_fd_handler(il->fd, input_linux_event, NULL, il);
    if (il->keycount) {
        /* delay grab until all keys are released */
        il->grab_request = true;
    } else {
        input_linux_toggle_grab(il);
    }
    QTAILQ_INSERT_TAIL(&inputs, il, next);
    il->initialized = true;
    return;

err_close:
    close(il->fd);
    return;
}

static void input_linux_instance_finalize(Object *obj)
{
    InputLinux *il = INPUT_LINUX(obj);

    if (il->initialized) {
        QTAILQ_REMOVE(&inputs, il, next);
        close(il->fd);
    }
    g_free(il->evdev);
}

static char *input_linux_get_evdev(Object *obj, Error **errp)
{
    InputLinux *il = INPUT_LINUX(obj);

    return g_strdup(il->evdev);
}

static void input_linux_set_evdev(Object *obj, const char *value,
                                  Error **errp)
{
    InputLinux *il = INPUT_LINUX(obj);

    if (il->evdev) {
        error_setg(errp, "evdev property already set");
        return;
    }
    il->evdev = g_strdup(value);
}

static bool input_linux_get_grab_all(Object *obj, Error **errp)
{
    InputLinux *il = INPUT_LINUX(obj);

    return il->grab_all;
}

static void input_linux_set_grab_all(Object *obj, bool value,
                                   Error **errp)
{
    InputLinux *il = INPUT_LINUX(obj);

    il->grab_all = value;
}

static bool input_linux_get_repeat(Object *obj, Error **errp)
{
    InputLinux *il = INPUT_LINUX(obj);

    return il->repeat;
}

static void input_linux_set_repeat(Object *obj, bool value,
                                   Error **errp)
{
    InputLinux *il = INPUT_LINUX(obj);

    il->repeat = value;
}

static void input_linux_instance_init(Object *obj)
{
    object_property_add_str(obj, "evdev",
                            input_linux_get_evdev,
                            input_linux_set_evdev, NULL);
    object_property_add_bool(obj, "grab_all",
                             input_linux_get_grab_all,
                             input_linux_set_grab_all, NULL);
    object_property_add_bool(obj, "repeat",
                             input_linux_get_repeat,
                             input_linux_set_repeat, NULL);
}

static void input_linux_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = input_linux_complete;
}

static const TypeInfo input_linux_info = {
    .name = TYPE_INPUT_LINUX,
    .parent = TYPE_OBJECT,
    .class_size = sizeof(InputLinuxClass),
    .class_init = input_linux_class_init,
    .instance_size = sizeof(InputLinux),
    .instance_init = input_linux_instance_init,
    .instance_finalize = input_linux_instance_finalize,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&input_linux_info);
}

type_init(register_types);
