/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "qemu/sockets.h"
#include "sysemu/sysemu.h"
#include "ui/input.h"

#include <sys/ioctl.h>
#include "standard-headers/linux/input.h"

static int linux_to_qcode[KEY_CNT] = {
    [KEY_ESC]            = Q_KEY_CODE_ESC,
    [KEY_1]              = Q_KEY_CODE_1,
    [KEY_2]              = Q_KEY_CODE_2,
    [KEY_3]              = Q_KEY_CODE_3,
    [KEY_4]              = Q_KEY_CODE_4,
    [KEY_5]              = Q_KEY_CODE_5,
    [KEY_6]              = Q_KEY_CODE_6,
    [KEY_7]              = Q_KEY_CODE_7,
    [KEY_8]              = Q_KEY_CODE_8,
    [KEY_9]              = Q_KEY_CODE_9,
    [KEY_0]              = Q_KEY_CODE_0,
    [KEY_MINUS]          = Q_KEY_CODE_MINUS,
    [KEY_EQUAL]          = Q_KEY_CODE_EQUAL,
    [KEY_BACKSPACE]      = Q_KEY_CODE_BACKSPACE,
    [KEY_TAB]            = Q_KEY_CODE_TAB,
    [KEY_Q]              = Q_KEY_CODE_Q,
    [KEY_W]              = Q_KEY_CODE_W,
    [KEY_E]              = Q_KEY_CODE_E,
    [KEY_R]              = Q_KEY_CODE_R,
    [KEY_T]              = Q_KEY_CODE_T,
    [KEY_Y]              = Q_KEY_CODE_Y,
    [KEY_U]              = Q_KEY_CODE_U,
    [KEY_I]              = Q_KEY_CODE_I,
    [KEY_O]              = Q_KEY_CODE_O,
    [KEY_P]              = Q_KEY_CODE_P,
    [KEY_LEFTBRACE]      = Q_KEY_CODE_BRACKET_LEFT,
    [KEY_RIGHTBRACE]     = Q_KEY_CODE_BRACKET_RIGHT,
    [KEY_ENTER]          = Q_KEY_CODE_RET,
    [KEY_LEFTCTRL]       = Q_KEY_CODE_CTRL,
    [KEY_A]              = Q_KEY_CODE_A,
    [KEY_S]              = Q_KEY_CODE_S,
    [KEY_D]              = Q_KEY_CODE_D,
    [KEY_F]              = Q_KEY_CODE_F,
    [KEY_G]              = Q_KEY_CODE_G,
    [KEY_H]              = Q_KEY_CODE_H,
    [KEY_J]              = Q_KEY_CODE_J,
    [KEY_K]              = Q_KEY_CODE_K,
    [KEY_L]              = Q_KEY_CODE_L,
    [KEY_SEMICOLON]      = Q_KEY_CODE_SEMICOLON,
    [KEY_APOSTROPHE]     = Q_KEY_CODE_APOSTROPHE,
    [KEY_GRAVE]          = Q_KEY_CODE_GRAVE_ACCENT,
    [KEY_LEFTSHIFT]      = Q_KEY_CODE_SHIFT,
    [KEY_BACKSLASH]      = Q_KEY_CODE_BACKSLASH,
    [KEY_102ND]          = Q_KEY_CODE_LESS,
    [KEY_Z]              = Q_KEY_CODE_Z,
    [KEY_X]              = Q_KEY_CODE_X,
    [KEY_C]              = Q_KEY_CODE_C,
    [KEY_V]              = Q_KEY_CODE_V,
    [KEY_B]              = Q_KEY_CODE_B,
    [KEY_N]              = Q_KEY_CODE_N,
    [KEY_M]              = Q_KEY_CODE_M,
    [KEY_COMMA]          = Q_KEY_CODE_COMMA,
    [KEY_DOT]            = Q_KEY_CODE_DOT,
    [KEY_SLASH]          = Q_KEY_CODE_SLASH,
    [KEY_RIGHTSHIFT]     = Q_KEY_CODE_SHIFT_R,
    [KEY_LEFTALT]        = Q_KEY_CODE_ALT,
    [KEY_SPACE]          = Q_KEY_CODE_SPC,
    [KEY_CAPSLOCK]       = Q_KEY_CODE_CAPS_LOCK,
    [KEY_F1]             = Q_KEY_CODE_F1,
    [KEY_F2]             = Q_KEY_CODE_F2,
    [KEY_F3]             = Q_KEY_CODE_F3,
    [KEY_F4]             = Q_KEY_CODE_F4,
    [KEY_F5]             = Q_KEY_CODE_F5,
    [KEY_F6]             = Q_KEY_CODE_F6,
    [KEY_F7]             = Q_KEY_CODE_F7,
    [KEY_F8]             = Q_KEY_CODE_F8,
    [KEY_F9]             = Q_KEY_CODE_F9,
    [KEY_F10]            = Q_KEY_CODE_F10,
    [KEY_NUMLOCK]        = Q_KEY_CODE_NUM_LOCK,
    [KEY_SCROLLLOCK]     = Q_KEY_CODE_SCROLL_LOCK,
    [KEY_KP0]            = Q_KEY_CODE_KP_0,
    [KEY_KP1]            = Q_KEY_CODE_KP_1,
    [KEY_KP2]            = Q_KEY_CODE_KP_2,
    [KEY_KP3]            = Q_KEY_CODE_KP_3,
    [KEY_KP4]            = Q_KEY_CODE_KP_4,
    [KEY_KP5]            = Q_KEY_CODE_KP_5,
    [KEY_KP6]            = Q_KEY_CODE_KP_6,
    [KEY_KP7]            = Q_KEY_CODE_KP_7,
    [KEY_KP8]            = Q_KEY_CODE_KP_8,
    [KEY_KP9]            = Q_KEY_CODE_KP_9,
    [KEY_KPMINUS]        = Q_KEY_CODE_KP_SUBTRACT,
    [KEY_KPPLUS]         = Q_KEY_CODE_KP_ADD,
    [KEY_KPDOT]          = Q_KEY_CODE_KP_DECIMAL,
    [KEY_KPENTER]        = Q_KEY_CODE_KP_ENTER,
    [KEY_KPSLASH]        = Q_KEY_CODE_KP_DIVIDE,
    [KEY_KPASTERISK]     = Q_KEY_CODE_KP_MULTIPLY,
    [KEY_F11]            = Q_KEY_CODE_F11,
    [KEY_F12]            = Q_KEY_CODE_F12,
    [KEY_RIGHTCTRL]      = Q_KEY_CODE_CTRL_R,
    [KEY_SYSRQ]          = Q_KEY_CODE_SYSRQ,
    [KEY_RIGHTALT]       = Q_KEY_CODE_ALT_R,
    [KEY_HOME]           = Q_KEY_CODE_HOME,
    [KEY_UP]             = Q_KEY_CODE_UP,
    [KEY_PAGEUP]         = Q_KEY_CODE_PGUP,
    [KEY_LEFT]           = Q_KEY_CODE_LEFT,
    [KEY_RIGHT]          = Q_KEY_CODE_RIGHT,
    [KEY_END]            = Q_KEY_CODE_END,
    [KEY_DOWN]           = Q_KEY_CODE_DOWN,
    [KEY_PAGEDOWN]       = Q_KEY_CODE_PGDN,
    [KEY_INSERT]         = Q_KEY_CODE_INSERT,
    [KEY_DELETE]         = Q_KEY_CODE_DELETE,
    [KEY_LEFTMETA]       = Q_KEY_CODE_META_L,
    [KEY_RIGHTMETA]      = Q_KEY_CODE_META_R,
    [KEY_MENU]           = Q_KEY_CODE_MENU,
};

static int qemu_input_linux_to_qcode(unsigned int lnx)
{
    assert(lnx < KEY_CNT);
    return linux_to_qcode[lnx];
}

typedef struct InputLinux InputLinux;

struct InputLinux {
    const char  *evdev;
    int         fd;
    bool        repeat;
    bool        grab_request;
    bool        grab_active;
    bool        grab_all;
    bool        keydown[KEY_CNT];
    int         keycount;
    int         wheel;
    QTAILQ_ENTRY(InputLinux) next;
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

static void input_linux_event_keyboard(void *opaque)
{
    InputLinux *il = opaque;
    struct input_event event;
    int rc;

    for (;;) {
        rc = read(il->fd, &event, sizeof(event));
        if (rc != sizeof(event)) {
            if (rc < 0 && errno != EAGAIN) {
                fprintf(stderr, "%s: read: %s\n", __func__, strerror(errno));
                qemu_set_fd_handler(il->fd, NULL, NULL, NULL);
                close(il->fd);
            }
            break;
        }

        switch (event.type) {
        case EV_KEY:
            if (event.value > 2 || (event.value > 1 && !il->repeat)) {
                /*
                 * ignore autorepeat + unknown key events
                 * 0 == up, 1 == down, 2 == autorepeat, other == undefined
                 */
                continue;
            }
            /* keep track of key state */
            if (!il->keydown[event.code] && event.value) {
                il->keydown[event.code] = true;
                il->keycount++;
            }
            if (il->keydown[event.code] && !event.value) {
                il->keydown[event.code] = false;
                il->keycount--;
            }

            /* send event to guest when grab is active */
            if (il->grab_active) {
                int qcode = qemu_input_linux_to_qcode(event.code);
                qemu_input_event_send_key_qcode(NULL, qcode, event.value);
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
            break;
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

static void input_linux_event_mouse(void *opaque)
{
    InputLinux *il = opaque;
    struct input_event event;
    int rc;

    for (;;) {
        rc = read(il->fd, &event, sizeof(event));
        if (rc != sizeof(event)) {
            if (rc < 0 && errno != EAGAIN) {
                fprintf(stderr, "%s: read: %s\n", __func__, strerror(errno));
                qemu_set_fd_handler(il->fd, NULL, NULL, NULL);
                close(il->fd);
            }
            break;
        }

        /* only send event to guest when grab is active */
        if (!il->grab_active) {
            continue;
        }

        switch (event.type) {
        case EV_KEY:
            switch (event.code) {
            case BTN_LEFT:
                qemu_input_queue_btn(NULL, INPUT_BUTTON_LEFT, event.value);
                break;
            case BTN_RIGHT:
                qemu_input_queue_btn(NULL, INPUT_BUTTON_RIGHT, event.value);
                break;
            case BTN_MIDDLE:
                qemu_input_queue_btn(NULL, INPUT_BUTTON_MIDDLE, event.value);
                break;
            case BTN_GEAR_UP:
                qemu_input_queue_btn(NULL, INPUT_BUTTON_WHEEL_UP, event.value);
                break;
            case BTN_GEAR_DOWN:
                qemu_input_queue_btn(NULL, INPUT_BUTTON_WHEEL_DOWN,
                                     event.value);
                break;
            };
            break;
        case EV_REL:
            switch (event.code) {
            case REL_X:
                qemu_input_queue_rel(NULL, INPUT_AXIS_X, event.value);
                break;
            case REL_Y:
                qemu_input_queue_rel(NULL, INPUT_AXIS_Y, event.value);
                break;
            case REL_WHEEL:
                il->wheel = event.value;
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
}

int input_linux_init(void *opaque, QemuOpts *opts, Error **errp)
{
    InputLinux *il = g_new0(InputLinux, 1);
    uint32_t evtmap;
    int rc, ver;

    il->evdev = qemu_opt_get(opts, "evdev");
    il->grab_all = qemu_opt_get_bool(opts, "grab-all", false);
    il->repeat = qemu_opt_get_bool(opts, "repeat", false);

    if (!il->evdev) {
        error_setg(errp, "no input device specified");
        goto err_free;
    }

    il->fd = open(il->evdev, O_RDWR);
    if (il->fd < 0)  {
        error_setg_file_open(errp, errno, il->evdev);
        goto err_free;
    }
    qemu_set_nonblock(il->fd);

    rc = ioctl(il->fd, EVIOCGVERSION, &ver);
    if (rc < 0) {
        error_setg(errp, "%s: is not an evdev device", il->evdev);
        goto err_close;
    }

    rc = ioctl(il->fd, EVIOCGBIT(0, sizeof(evtmap)), &evtmap);

    if (evtmap & (1 << EV_REL)) {
        /* has relative axis -> assume mouse */
        qemu_set_fd_handler(il->fd, input_linux_event_mouse, NULL, il);
    } else if (evtmap & (1 << EV_ABS)) {
        /* has absolute axis -> not supported */
        error_setg(errp, "tablet/touchscreen not supported");
        goto err_close;
    } else if (evtmap & (1 << EV_KEY)) {
        /* has keys/buttons (and no axis) -> assume keyboard */
        qemu_set_fd_handler(il->fd, input_linux_event_keyboard, NULL, il);
    } else {
        /* Huh? What is this? */
        error_setg(errp, "unknown kind of input device");
        goto err_close;
    }
    input_linux_toggle_grab(il);
    QTAILQ_INSERT_TAIL(&inputs, il, next);
    return 0;

err_close:
    close(il->fd);
err_free:
    g_free(il);
    return -1;
}

static QemuOptsList qemu_input_linux_opts = {
    .name = "input-linux",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_input_linux_opts.head),
    .implied_opt_name = "evdev",
    .desc = {
        {
            .name = "evdev",
            .type = QEMU_OPT_STRING,
        },{
            .name = "grab-all",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "repeat",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

static void input_linux_register_config(void)
{
    qemu_add_opts(&qemu_input_linux_opts);
}
machine_init(input_linux_register_config);
