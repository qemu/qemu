/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * TODO:
 *
 *  - Enable SSL
 *  - Manage SetOptions/ResetOptions commands
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "io/channel-socket.h"
#include "ui/input.h"
#include "qom/object.h"
#include "ui/vnc_keysym.h" /* use name2keysym from VNC as we use X11 values */
#include "qemu/cutils.h"
#include "qapi/qmp/qerror.h"
#include "input-barrier.h"

#define TYPE_INPUT_BARRIER "input-barrier"
OBJECT_DECLARE_SIMPLE_TYPE(InputBarrier,
                           INPUT_BARRIER)


#define MAX_HELLO_LENGTH 1024

struct InputBarrier {
    Object parent;

    QIOChannelSocket *sioc;
    guint ioc_tag;

    /* display properties */
    gchar *name;
    int16_t x_origin, y_origin;
    int16_t width, height;

    /* keyboard/mouse server */

    SocketAddress saddr;

    char buffer[MAX_HELLO_LENGTH];
};


static const char *cmd_names[] = {
    [barrierCmdCNoop]          = "CNOP",
    [barrierCmdCClose]         = "CBYE",
    [barrierCmdCEnter]         = "CINN",
    [barrierCmdCLeave]         = "COUT",
    [barrierCmdCClipboard]     = "CCLP",
    [barrierCmdCScreenSaver]   = "CSEC",
    [barrierCmdCResetOptions]  = "CROP",
    [barrierCmdCInfoAck]       = "CIAK",
    [barrierCmdCKeepAlive]     = "CALV",
    [barrierCmdDKeyDown]       = "DKDN",
    [barrierCmdDKeyRepeat]     = "DKRP",
    [barrierCmdDKeyUp]         = "DKUP",
    [barrierCmdDMouseDown]     = "DMDN",
    [barrierCmdDMouseUp]       = "DMUP",
    [barrierCmdDMouseMove]     = "DMMV",
    [barrierCmdDMouseRelMove]  = "DMRM",
    [barrierCmdDMouseWheel]    = "DMWM",
    [barrierCmdDClipboard]     = "DCLP",
    [barrierCmdDInfo]          = "DINF",
    [barrierCmdDSetOptions]    = "DSOP",
    [barrierCmdDFileTransfer]  = "DFTR",
    [barrierCmdDDragInfo]      = "DDRG",
    [barrierCmdQInfo]          = "QINF",
    [barrierCmdEIncompatible]  = "EICV",
    [barrierCmdEBusy]          = "EBSY",
    [barrierCmdEUnknown]       = "EUNK",
    [barrierCmdEBad]           = "EBAD",
    [barrierCmdHello]          = "Barrier",
    [barrierCmdHelloBack]      = "Barrier",
};

static kbd_layout_t *kbd_layout;

static int input_barrier_to_qcode(uint16_t keyid, uint16_t keycode)
{
    /* keycode is optional, if it is not provided use keyid */
    if (keycode && keycode <= qemu_input_map_xorgkbd_to_qcode_len) {
        return qemu_input_map_xorgkbd_to_qcode[keycode];
    }

    if (keyid >= 0xE000 && keyid <= 0xEFFF) {
        keyid += 0x1000;
    }

    /* keyid is the X11 key id */
    if (kbd_layout) {
        keycode = keysym2scancode(kbd_layout, keyid, NULL, false);

        return qemu_input_key_number_to_qcode(keycode);
    }

    return qemu_input_map_x11_to_qcode[keyid];
}

static int input_barrier_to_mouse(uint8_t buttonid)
{
    switch (buttonid) {
    case barrierButtonLeft:
        return INPUT_BUTTON_LEFT;
    case barrierButtonMiddle:
        return INPUT_BUTTON_MIDDLE;
    case barrierButtonRight:
        return INPUT_BUTTON_RIGHT;
    case barrierButtonExtra0:
        return INPUT_BUTTON_SIDE;
    }
    return buttonid;
}

#define read_char(x, p, l)           \
do {                                 \
    int size = sizeof(char);         \
    if (l < size) {                  \
        return G_SOURCE_REMOVE;      \
    }                                \
    x = *(char *)p;                  \
    p += size;                       \
    l -= size;                       \
} while (0)

#define read_short(x, p, l)          \
do {                                 \
    int size = sizeof(short);        \
    if (l < size) {                  \
        return G_SOURCE_REMOVE;      \
    }                                \
    x = ntohs(*(short *)p);          \
    p += size;                       \
    l -= size;                       \
} while (0)

#define write_short(p, x, l)         \
do {                                 \
    int size = sizeof(short);        \
    if (l < size) {                  \
        return G_SOURCE_REMOVE;      \
    }                                \
    *(short *)p = htons(x);          \
    p += size;                       \
    l -= size;                       \
} while (0)

#define read_int(x, p, l)            \
do {                                 \
    int size = sizeof(int);          \
    if (l < size) {                  \
        return G_SOURCE_REMOVE;      \
    }                                \
    x = ntohl(*(int *)p);            \
    p += size;                       \
    l -= size;                       \
} while (0)

#define write_int(p, x, l)           \
do {                                 \
    int size = sizeof(int);          \
    if (l < size) {                  \
        return G_SOURCE_REMOVE;      \
    }                                \
    *(int *)p = htonl(x);            \
    p += size;                       \
    l -= size;                       \
} while (0)

#define write_cmd(p, c, l)           \
do {                                 \
    int size = strlen(cmd_names[c]); \
    if (l < size) {                  \
        return G_SOURCE_REMOVE;      \
    }                                \
    memcpy(p, cmd_names[c], size);   \
    p += size;                       \
    l -= size;                       \
} while (0)

#define write_string(p, s, l)        \
do {                                 \
    int size = strlen(s);            \
    if (l < size + sizeof(int)) {    \
        return G_SOURCE_REMOVE;      \
    }                                \
    *(int *)p = htonl(size);         \
    p += sizeof(size);               \
    l -= sizeof(size);               \
    memcpy(p, s, size);              \
    p += size;                       \
    l -= size;                       \
} while (0)

static gboolean readcmd(InputBarrier *ib, struct barrierMsg *msg)
{
    int ret, len, i;
    enum barrierCmd cmd;
    char *p;

    ret = qio_channel_read(QIO_CHANNEL(ib->sioc), (char *)&len, sizeof(len),
                           NULL);
    if (ret < 0) {
        return G_SOURCE_REMOVE;
    }

    len = ntohl(len);
    if (len > MAX_HELLO_LENGTH) {
        return G_SOURCE_REMOVE;
    }

    ret = qio_channel_read(QIO_CHANNEL(ib->sioc), ib->buffer, len, NULL);
    if (ret < 0) {
        return G_SOURCE_REMOVE;
    }

    p = ib->buffer;
    if (len >= strlen(cmd_names[barrierCmdHello]) &&
        memcmp(p, cmd_names[barrierCmdHello],
               strlen(cmd_names[barrierCmdHello])) == 0) {
        cmd = barrierCmdHello;
        p += strlen(cmd_names[barrierCmdHello]);
        len -= strlen(cmd_names[barrierCmdHello]);
    } else {
        for (cmd = 0; cmd < barrierCmdHello; cmd++) {
            if (memcmp(ib->buffer, cmd_names[cmd], 4) == 0) {
                break;
            }
        }

        if (cmd == barrierCmdHello) {
            return G_SOURCE_REMOVE;
        }
        p += 4;
        len -= 4;
    }

    msg->cmd = cmd;
    switch (cmd) {
    /* connection */
    case barrierCmdHello:
        read_short(msg->version.major, p, len);
        read_short(msg->version.minor, p, len);
        break;
    case barrierCmdDSetOptions:
        read_int(msg->set.nb, p, len);
        msg->set.nb /= 2;
        if (msg->set.nb > BARRIER_MAX_OPTIONS) {
            msg->set.nb = BARRIER_MAX_OPTIONS;
        }
        i = 0;
        while (len && i < msg->set.nb) {
            read_int(msg->set.option[i].id, p, len);
            /* it's a string, restore endianness */
            msg->set.option[i].id = htonl(msg->set.option[i].id);
            msg->set.option[i].nul = 0;
            read_int(msg->set.option[i].value, p, len);
            i++;
        }
        break;
    case barrierCmdQInfo:
        break;

    /* mouse */
    case barrierCmdDMouseMove:
    case barrierCmdDMouseRelMove:
        read_short(msg->mousepos.x, p, len);
        read_short(msg->mousepos.y, p, len);
        break;
    case barrierCmdDMouseDown:
    case barrierCmdDMouseUp:
        read_char(msg->mousebutton.buttonid, p, len);
        break;
    case barrierCmdDMouseWheel:
        read_short(msg->mousepos.y, p, len);
        msg->mousepos.x = 0;
        if (len) {
            msg->mousepos.x = msg->mousepos.y;
            read_short(msg->mousepos.y, p, len);
        }
        break;

    /* keyboard */
    case barrierCmdDKeyDown:
    case barrierCmdDKeyUp:
        read_short(msg->key.keyid, p, len);
        read_short(msg->key.modifier, p, len);
        msg->key.button = 0;
        if (len) {
            read_short(msg->key.button, p, len);
        }
        break;
    case barrierCmdDKeyRepeat:
        read_short(msg->repeat.keyid, p, len);
        read_short(msg->repeat.modifier, p, len);
        read_short(msg->repeat.repeat, p, len);
        msg->repeat.button = 0;
        if (len) {
            read_short(msg->repeat.button, p, len);
        }
        break;
    case barrierCmdCInfoAck:
    case barrierCmdCResetOptions:
    case barrierCmdCEnter:
    case barrierCmdDClipboard:
    case barrierCmdCKeepAlive:
    case barrierCmdCLeave:
    case barrierCmdCClose:
        break;

    /* Invalid from the server */
    case barrierCmdHelloBack:
    case barrierCmdCNoop:
    case barrierCmdDInfo:
        break;

    /* Error codes */
    case barrierCmdEIncompatible:
        read_short(msg->version.major, p, len);
        read_short(msg->version.minor, p, len);
        break;
    case barrierCmdEBusy:
    case barrierCmdEUnknown:
    case barrierCmdEBad:
        break;
    default:
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static gboolean writecmd(InputBarrier *ib, struct barrierMsg *msg)
{
    char *p;
    int ret, i;
    int avail, len;

    p = ib->buffer;
    avail = MAX_HELLO_LENGTH;

    /* reserve space to store the length */
    p += sizeof(int);
    avail -= sizeof(int);

    switch (msg->cmd) {
    case barrierCmdHello:
        if (msg->version.major < BARRIER_VERSION_MAJOR ||
            (msg->version.major == BARRIER_VERSION_MAJOR &&
             msg->version.minor < BARRIER_VERSION_MINOR)) {
            ib->ioc_tag = 0;
            return G_SOURCE_REMOVE;
        }
        write_cmd(p, barrierCmdHelloBack, avail);
        write_short(p, BARRIER_VERSION_MAJOR, avail);
        write_short(p, BARRIER_VERSION_MINOR, avail);
        write_string(p, ib->name, avail);
        break;
    case barrierCmdCClose:
        ib->ioc_tag = 0;
        return G_SOURCE_REMOVE;
    case barrierCmdQInfo:
        write_cmd(p, barrierCmdDInfo, avail);
        write_short(p, ib->x_origin, avail);
        write_short(p, ib->y_origin, avail);
        write_short(p, ib->width, avail);
        write_short(p, ib->height, avail);
        write_short(p, 0, avail);    /* warpsize (obsolete) */
        write_short(p, 0, avail);    /* mouse x */
        write_short(p, 0, avail);    /* mouse y */
        break;
    case barrierCmdCInfoAck:
        break;
    case barrierCmdCResetOptions:
        /* TODO: reset options */
        break;
    case barrierCmdDSetOptions:
        /* TODO: set options */
        break;
    case barrierCmdCEnter:
        break;
    case barrierCmdDClipboard:
        break;
    case barrierCmdCKeepAlive:
        write_cmd(p, barrierCmdCKeepAlive, avail);
        break;
    case barrierCmdCLeave:
        break;

    /* mouse */
    case barrierCmdDMouseMove:
        qemu_input_queue_abs(NULL, INPUT_AXIS_X, msg->mousepos.x,
                             ib->x_origin, ib->width);
        qemu_input_queue_abs(NULL, INPUT_AXIS_Y, msg->mousepos.y,
                             ib->y_origin, ib->height);
        qemu_input_event_sync();
        break;
    case barrierCmdDMouseRelMove:
        qemu_input_queue_rel(NULL, INPUT_AXIS_X, msg->mousepos.x);
        qemu_input_queue_rel(NULL, INPUT_AXIS_Y, msg->mousepos.y);
        qemu_input_event_sync();
        break;
    case barrierCmdDMouseDown:
        qemu_input_queue_btn(NULL,
                             input_barrier_to_mouse(msg->mousebutton.buttonid),
                             true);
        qemu_input_event_sync();
        break;
    case barrierCmdDMouseUp:
        qemu_input_queue_btn(NULL,
                             input_barrier_to_mouse(msg->mousebutton.buttonid),
                             false);
        qemu_input_event_sync();
        break;
    case barrierCmdDMouseWheel:
        qemu_input_queue_btn(NULL, (msg->mousepos.y > 0) ? INPUT_BUTTON_WHEEL_UP
                             : INPUT_BUTTON_WHEEL_DOWN, true);
        qemu_input_event_sync();
        qemu_input_queue_btn(NULL, (msg->mousepos.y > 0) ? INPUT_BUTTON_WHEEL_UP
                             : INPUT_BUTTON_WHEEL_DOWN, false);
        qemu_input_event_sync();
        break;

    /* keyboard */
    case barrierCmdDKeyDown:
        qemu_input_event_send_key_qcode(NULL,
                        input_barrier_to_qcode(msg->key.keyid, msg->key.button),
                                        true);
        break;
    case barrierCmdDKeyRepeat:
        for (i = 0; i < msg->repeat.repeat; i++) {
            qemu_input_event_send_key_qcode(NULL,
                  input_barrier_to_qcode(msg->repeat.keyid, msg->repeat.button),
                                            false);
            qemu_input_event_send_key_qcode(NULL,
                  input_barrier_to_qcode(msg->repeat.keyid, msg->repeat.button),
                                            true);
        }
        break;
    case barrierCmdDKeyUp:
        qemu_input_event_send_key_qcode(NULL,
                        input_barrier_to_qcode(msg->key.keyid, msg->key.button),
                                        false);
        break;
    default:
        write_cmd(p, barrierCmdEUnknown, avail);
        break;
    }

    len = MAX_HELLO_LENGTH - avail - sizeof(int);
    if (len) {
        p = ib->buffer;
        avail = sizeof(len);
        write_int(p, len, avail);
        ret = qio_channel_write(QIO_CHANNEL(ib->sioc), ib->buffer,
                                len + sizeof(len), NULL);
        if (ret < 0) {
            ib->ioc_tag = 0;
            return G_SOURCE_REMOVE;
        }
    }

    return G_SOURCE_CONTINUE;
}

static gboolean input_barrier_event(QIOChannel *ioc G_GNUC_UNUSED,
                                    GIOCondition condition, void *opaque)
{
    InputBarrier *ib = opaque;
    int ret;
    struct barrierMsg msg;

    ret = readcmd(ib, &msg);
    if (ret == G_SOURCE_REMOVE) {
        ib->ioc_tag = 0;
        return G_SOURCE_REMOVE;
    }

    return writecmd(ib, &msg);
}

static void input_barrier_complete(UserCreatable *uc, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(uc);
    Error *local_err = NULL;

    if (!ib->name) {
        error_setg(errp, QERR_MISSING_PARAMETER, "name");
        return;
    }

    /*
     * Connect to the primary
     * Primary is the server where the keyboard and the mouse
     * are connected and forwarded to the secondary (the client)
     */

    ib->sioc = qio_channel_socket_new();
    qio_channel_set_name(QIO_CHANNEL(ib->sioc), "barrier-client");

    qio_channel_socket_connect_sync(ib->sioc, &ib->saddr, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    qio_channel_set_delay(QIO_CHANNEL(ib->sioc), false);

    ib->ioc_tag = qio_channel_add_watch(QIO_CHANNEL(ib->sioc), G_IO_IN,
                                        input_barrier_event, ib, NULL);
}

static void input_barrier_instance_finalize(Object *obj)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    if (ib->ioc_tag) {
        g_source_remove(ib->ioc_tag);
        ib->ioc_tag = 0;
    }

    if (ib->sioc) {
        qio_channel_close(QIO_CHANNEL(ib->sioc), NULL);
        object_unref(OBJECT(ib->sioc));
    }
    g_free(ib->name);
    g_free(ib->saddr.u.inet.host);
    g_free(ib->saddr.u.inet.port);
}

static char *input_barrier_get_name(Object *obj, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    return g_strdup(ib->name);
}

static void input_barrier_set_name(Object *obj, const char *value,
                                  Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    if (ib->name) {
        error_setg(errp, "name property already set");
        return;
    }
    ib->name = g_strdup(value);
}

static char *input_barrier_get_server(Object *obj, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    return g_strdup(ib->saddr.u.inet.host);
}

static void input_barrier_set_server(Object *obj, const char *value,
                                     Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    g_free(ib->saddr.u.inet.host);
    ib->saddr.u.inet.host = g_strdup(value);
}

static char *input_barrier_get_port(Object *obj, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    return g_strdup(ib->saddr.u.inet.port);
}

static void input_barrier_set_port(Object *obj, const char *value,
                                     Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    g_free(ib->saddr.u.inet.port);
    ib->saddr.u.inet.port = g_strdup(value);
}

static void input_barrier_set_x_origin(Object *obj, const char *value,
                                       Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);
    int result, err;

    err = qemu_strtoi(value, NULL, 0, &result);
    if (err < 0 || result < 0 || result > SHRT_MAX) {
        error_setg(errp,
                   "x-origin property must be in the range [0..%d]", SHRT_MAX);
        return;
    }
    ib->x_origin = result;
}

static char *input_barrier_get_x_origin(Object *obj, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    return g_strdup_printf("%d", ib->x_origin);
}

static void input_barrier_set_y_origin(Object *obj, const char *value,
                                       Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);
    int result, err;

    err = qemu_strtoi(value, NULL, 0, &result);
    if (err < 0 || result < 0 || result > SHRT_MAX) {
        error_setg(errp,
                   "y-origin property must be in the range [0..%d]", SHRT_MAX);
        return;
    }
    ib->y_origin = result;
}

static char *input_barrier_get_y_origin(Object *obj, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    return g_strdup_printf("%d", ib->y_origin);
}

static void input_barrier_set_width(Object *obj, const char *value,
                                       Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);
    int result, err;

    err = qemu_strtoi(value, NULL, 0, &result);
    if (err < 0 || result < 0 || result > SHRT_MAX) {
        error_setg(errp,
                   "width property must be in the range [0..%d]", SHRT_MAX);
        return;
    }
    ib->width = result;
}

static char *input_barrier_get_width(Object *obj, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    return g_strdup_printf("%d", ib->width);
}

static void input_barrier_set_height(Object *obj, const char *value,
                                       Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);
    int result, err;

    err = qemu_strtoi(value, NULL, 0, &result);
    if (err < 0 || result < 0 || result > SHRT_MAX) {
        error_setg(errp,
                   "height property must be in the range [0..%d]", SHRT_MAX);
        return;
    }
    ib->height = result;
}

static char *input_barrier_get_height(Object *obj, Error **errp)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    return g_strdup_printf("%d", ib->height);
}

static void input_barrier_instance_init(Object *obj)
{
    InputBarrier *ib = INPUT_BARRIER(obj);

    /* always use generic keymaps */
    if (keyboard_layout && !kbd_layout) {
        /* We use X11 key id, so use VNC name2keysym */
        kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout,
                                          &error_fatal);
    }

    ib->saddr.type = SOCKET_ADDRESS_TYPE_INET;
    ib->saddr.u.inet.host = g_strdup("localhost");
    ib->saddr.u.inet.port = g_strdup("24800");

    ib->x_origin = 0;
    ib->y_origin = 0;
    ib->width = 1920;
    ib->height = 1080;
}

static void input_barrier_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = input_barrier_complete;

    object_class_property_add_str(oc, "name",
                                  input_barrier_get_name,
                                  input_barrier_set_name);
    object_class_property_add_str(oc, "server",
                                  input_barrier_get_server,
                                  input_barrier_set_server);
    object_class_property_add_str(oc, "port",
                                  input_barrier_get_port,
                                  input_barrier_set_port);
    object_class_property_add_str(oc, "x-origin",
                                  input_barrier_get_x_origin,
                                  input_barrier_set_x_origin);
    object_class_property_add_str(oc, "y-origin",
                                  input_barrier_get_y_origin,
                                  input_barrier_set_y_origin);
    object_class_property_add_str(oc, "width",
                                  input_barrier_get_width,
                                  input_barrier_set_width);
    object_class_property_add_str(oc, "height",
                                  input_barrier_get_height,
                                  input_barrier_set_height);
}

static const TypeInfo input_barrier_info = {
    .name = TYPE_INPUT_BARRIER,
    .parent = TYPE_OBJECT,
    .class_init = input_barrier_class_init,
    .instance_size = sizeof(InputBarrier),
    .instance_init = input_barrier_instance_init,
    .instance_finalize = input_barrier_instance_finalize,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&input_barrier_info);
}

type_init(register_types);
