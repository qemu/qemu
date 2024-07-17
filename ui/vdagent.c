#include "qemu/osdep.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "qemu/buffer.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/units.h"
#include "hw/qdev-core.h"
#include "migration/blocker.h"
#include "ui/clipboard.h"
#include "ui/console.h"
#include "ui/input.h"
#include "trace.h"

#include "qapi/qapi-types-char.h"
#include "qapi/qapi-types-ui.h"

#include "spice/vd_agent.h"

#define CHECK_SPICE_PROTOCOL_VERSION(major, minor, micro) \
    (CONFIG_SPICE_PROTOCOL_MAJOR > (major) ||             \
     (CONFIG_SPICE_PROTOCOL_MAJOR == (major) &&           \
      CONFIG_SPICE_PROTOCOL_MINOR > (minor)) ||           \
     (CONFIG_SPICE_PROTOCOL_MAJOR == (major) &&           \
      CONFIG_SPICE_PROTOCOL_MINOR == (minor) &&           \
      CONFIG_SPICE_PROTOCOL_MICRO >= (micro)))

#define VDAGENT_BUFFER_LIMIT (1 * MiB)
#define VDAGENT_MOUSE_DEFAULT true
#define VDAGENT_CLIPBOARD_DEFAULT false

struct VDAgentChardev {
    Chardev parent;

    /* TODO: migration isn't yet supported */
    Error *migration_blocker;

    /* config */
    bool mouse;
    bool clipboard;

    /* guest vdagent */
    uint32_t caps;
    VDIChunkHeader chunk;
    uint32_t chunksize;
    uint8_t *msgbuf;
    uint32_t msgsize;
    uint8_t *xbuf;
    uint32_t xoff, xsize;
    Buffer outbuf;

    /* mouse */
    DeviceState mouse_dev;
    uint32_t mouse_x;
    uint32_t mouse_y;
    uint32_t mouse_btn;
    uint32_t mouse_display;
    QemuInputHandlerState *mouse_hs;

    /* clipboard */
    QemuClipboardPeer cbpeer;
    uint32_t last_serial[QEMU_CLIPBOARD_SELECTION__COUNT];
    uint32_t cbpending[QEMU_CLIPBOARD_SELECTION__COUNT];
};
typedef struct VDAgentChardev VDAgentChardev;

#define TYPE_CHARDEV_QEMU_VDAGENT "chardev-qemu-vdagent"

DECLARE_INSTANCE_CHECKER(VDAgentChardev, QEMU_VDAGENT_CHARDEV,
                         TYPE_CHARDEV_QEMU_VDAGENT);

/* ------------------------------------------------------------------ */
/* names, for debug logging                                           */

static const char *cap_name[] = {
    [VD_AGENT_CAP_MOUSE_STATE]                    = "mouse-state",
    [VD_AGENT_CAP_MONITORS_CONFIG]                = "monitors-config",
    [VD_AGENT_CAP_REPLY]                          = "reply",
    [VD_AGENT_CAP_CLIPBOARD]                      = "clipboard",
    [VD_AGENT_CAP_DISPLAY_CONFIG]                 = "display-config",
    [VD_AGENT_CAP_CLIPBOARD_BY_DEMAND]            = "clipboard-by-demand",
    [VD_AGENT_CAP_CLIPBOARD_SELECTION]            = "clipboard-selection",
    [VD_AGENT_CAP_SPARSE_MONITORS_CONFIG]         = "sparse-monitors-config",
    [VD_AGENT_CAP_GUEST_LINEEND_LF]               = "guest-lineend-lf",
    [VD_AGENT_CAP_GUEST_LINEEND_CRLF]             = "guest-lineend-crlf",
    [VD_AGENT_CAP_MAX_CLIPBOARD]                  = "max-clipboard",
    [VD_AGENT_CAP_AUDIO_VOLUME_SYNC]              = "audio-volume-sync",
    [VD_AGENT_CAP_MONITORS_CONFIG_POSITION]       = "monitors-config-position",
    [VD_AGENT_CAP_FILE_XFER_DISABLED]             = "file-xfer-disabled",
    [VD_AGENT_CAP_FILE_XFER_DETAILED_ERRORS]      = "file-xfer-detailed-errors",
    [VD_AGENT_CAP_GRAPHICS_DEVICE_INFO]           = "graphics-device-info",
#if CHECK_SPICE_PROTOCOL_VERSION(0, 14, 1)
    [VD_AGENT_CAP_CLIPBOARD_NO_RELEASE_ON_REGRAB] = "clipboard-no-release-on-regrab",
    [VD_AGENT_CAP_CLIPBOARD_GRAB_SERIAL]          = "clipboard-grab-serial",
#endif
};

static const char *msg_name[] = {
    [VD_AGENT_MOUSE_STATE]           = "mouse-state",
    [VD_AGENT_MONITORS_CONFIG]       = "monitors-config",
    [VD_AGENT_REPLY]                 = "reply",
    [VD_AGENT_CLIPBOARD]             = "clipboard",
    [VD_AGENT_DISPLAY_CONFIG]        = "display-config",
    [VD_AGENT_ANNOUNCE_CAPABILITIES] = "announce-capabilities",
    [VD_AGENT_CLIPBOARD_GRAB]        = "clipboard-grab",
    [VD_AGENT_CLIPBOARD_REQUEST]     = "clipboard-request",
    [VD_AGENT_CLIPBOARD_RELEASE]     = "clipboard-release",
    [VD_AGENT_FILE_XFER_START]       = "file-xfer-start",
    [VD_AGENT_FILE_XFER_STATUS]      = "file-xfer-status",
    [VD_AGENT_FILE_XFER_DATA]        = "file-xfer-data",
    [VD_AGENT_CLIENT_DISCONNECTED]   = "client-disconnected",
    [VD_AGENT_MAX_CLIPBOARD]         = "max-clipboard",
    [VD_AGENT_AUDIO_VOLUME_SYNC]     = "audio-volume-sync",
    [VD_AGENT_GRAPHICS_DEVICE_INFO]  = "graphics-device-info",
};

static const char *sel_name[] = {
    [VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD] = "clipboard",
    [VD_AGENT_CLIPBOARD_SELECTION_PRIMARY]   = "primary",
    [VD_AGENT_CLIPBOARD_SELECTION_SECONDARY] = "secondary",
};

static const char *type_name[] = {
    [VD_AGENT_CLIPBOARD_NONE]       = "none",
    [VD_AGENT_CLIPBOARD_UTF8_TEXT]  = "text",
    [VD_AGENT_CLIPBOARD_IMAGE_PNG]  = "png",
    [VD_AGENT_CLIPBOARD_IMAGE_BMP]  = "bmp",
    [VD_AGENT_CLIPBOARD_IMAGE_TIFF] = "tiff",
    [VD_AGENT_CLIPBOARD_IMAGE_JPG]  = "jpg",
#if CHECK_SPICE_PROTOCOL_VERSION(0, 14, 3)
    [VD_AGENT_CLIPBOARD_FILE_LIST]  = "files",
#endif
};

#define GET_NAME(_m, _v) \
    (((_v) < ARRAY_SIZE(_m) && (_m[_v])) ? (_m[_v]) : "???")

/* ------------------------------------------------------------------ */
/* send messages                                                      */

static void vdagent_send_buf(VDAgentChardev *vd)
{
    uint32_t len;

    while (!buffer_empty(&vd->outbuf)) {
        len = qemu_chr_be_can_write(CHARDEV(vd));
        if (len == 0) {
            return;
        }
        if (len > vd->outbuf.offset) {
            len = vd->outbuf.offset;
        }
        qemu_chr_be_write(CHARDEV(vd), vd->outbuf.buffer, len);
        buffer_advance(&vd->outbuf, len);
    }
}

static void vdagent_send_msg(VDAgentChardev *vd, VDAgentMessage *msg)
{
    uint8_t *msgbuf = (void *)msg;
    uint32_t msgsize = sizeof(VDAgentMessage) + msg->size;
    uint32_t msgoff = 0;
    VDIChunkHeader chunk;

    trace_vdagent_send(GET_NAME(msg_name, msg->type));

    msg->protocol = VD_AGENT_PROTOCOL;

    if (vd->outbuf.offset + msgsize > VDAGENT_BUFFER_LIMIT) {
        error_report("buffer full, dropping message");
        return;
    }

    while (msgoff < msgsize) {
        chunk.port = VDP_CLIENT_PORT;
        chunk.size = msgsize - msgoff;
        if (chunk.size > 1024) {
            chunk.size = 1024;
        }
        buffer_reserve(&vd->outbuf, sizeof(chunk) + chunk.size);
        buffer_append(&vd->outbuf, &chunk, sizeof(chunk));
        buffer_append(&vd->outbuf, msgbuf + msgoff, chunk.size);
        msgoff += chunk.size;
    }
    vdagent_send_buf(vd);
}

static void vdagent_send_caps(VDAgentChardev *vd, bool request)
{
    g_autofree VDAgentMessage *msg = g_malloc0(sizeof(VDAgentMessage) +
                                               sizeof(VDAgentAnnounceCapabilities) +
                                               sizeof(uint32_t));
    VDAgentAnnounceCapabilities *caps = (void *)msg->data;

    msg->type = VD_AGENT_ANNOUNCE_CAPABILITIES;
    msg->size = sizeof(VDAgentAnnounceCapabilities) + sizeof(uint32_t);
    if (vd->mouse) {
        caps->caps[0] |= (1 << VD_AGENT_CAP_MOUSE_STATE);
    }
    if (vd->clipboard) {
        caps->caps[0] |= (1 << VD_AGENT_CAP_CLIPBOARD_BY_DEMAND);
        caps->caps[0] |= (1 << VD_AGENT_CAP_CLIPBOARD_SELECTION);
#if CHECK_SPICE_PROTOCOL_VERSION(0, 14, 1)
        caps->caps[0] |= (1 << VD_AGENT_CAP_CLIPBOARD_GRAB_SERIAL);
#endif
    }

    caps->request = request;
    vdagent_send_msg(vd, msg);
}

/* ------------------------------------------------------------------ */
/* mouse events                                                       */

static bool have_mouse(VDAgentChardev *vd)
{
    return vd->mouse &&
        (vd->caps & (1 << VD_AGENT_CAP_MOUSE_STATE));
}

static void vdagent_send_mouse(VDAgentChardev *vd)
{
    g_autofree VDAgentMessage *msg = g_malloc0(sizeof(VDAgentMessage) +
                                               sizeof(VDAgentMouseState));
    VDAgentMouseState *mouse = (void *)msg->data;

    msg->type = VD_AGENT_MOUSE_STATE;
    msg->size = sizeof(VDAgentMouseState);

    mouse->x          = vd->mouse_x;
    mouse->y          = vd->mouse_y;
    mouse->buttons    = vd->mouse_btn;
    mouse->display_id = vd->mouse_display;

    vdagent_send_msg(vd, msg);
}

static void vdagent_pointer_event(DeviceState *dev, QemuConsole *src,
                                  InputEvent *evt)
{
    static const int bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]        = VD_AGENT_LBUTTON_MASK,
        [INPUT_BUTTON_RIGHT]       = VD_AGENT_RBUTTON_MASK,
        [INPUT_BUTTON_MIDDLE]      = VD_AGENT_MBUTTON_MASK,
        [INPUT_BUTTON_WHEEL_UP]    = VD_AGENT_UBUTTON_MASK,
        [INPUT_BUTTON_WHEEL_DOWN]  = VD_AGENT_DBUTTON_MASK,
#ifdef VD_AGENT_EBUTTON_MASK
        [INPUT_BUTTON_SIDE]        = VD_AGENT_SBUTTON_MASK,
        [INPUT_BUTTON_EXTRA]       = VD_AGENT_EBUTTON_MASK,
#endif
    };

    VDAgentChardev *vd = container_of(dev, struct VDAgentChardev, mouse_dev);
    InputMoveEvent *move;
    InputBtnEvent *btn;
    uint32_t xres, yres;

    switch (evt->type) {
    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        xres = qemu_console_get_width(src, 1024);
        yres = qemu_console_get_height(src, 768);
        if (move->axis == INPUT_AXIS_X) {
            vd->mouse_x = qemu_input_scale_axis(move->value,
                                                INPUT_EVENT_ABS_MIN,
                                                INPUT_EVENT_ABS_MAX,
                                                0, xres);
        } else if (move->axis == INPUT_AXIS_Y) {
            vd->mouse_y = qemu_input_scale_axis(move->value,
                                                INPUT_EVENT_ABS_MIN,
                                                INPUT_EVENT_ABS_MAX,
                                                0, yres);
        }
        vd->mouse_display = qemu_console_get_index(src);
        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (btn->down) {
            vd->mouse_btn |= bmap[btn->button];
        } else {
            vd->mouse_btn &= ~bmap[btn->button];
        }
        break;

    default:
        /* keep gcc happy */
        break;
    }
}

static void vdagent_pointer_sync(DeviceState *dev)
{
    VDAgentChardev *vd = container_of(dev, struct VDAgentChardev, mouse_dev);

    if (vd->caps & (1 << VD_AGENT_CAP_MOUSE_STATE)) {
        vdagent_send_mouse(vd);
    }
}

static const QemuInputHandler vdagent_mouse_handler = {
    .name  = "vdagent mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = vdagent_pointer_event,
    .sync  = vdagent_pointer_sync,
};

/* ------------------------------------------------------------------ */
/* clipboard                                                          */

static bool have_clipboard(VDAgentChardev *vd)
{
    return vd->clipboard &&
        (vd->caps & (1 << VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));
}

static bool have_selection(VDAgentChardev *vd)
{
    return vd->caps & (1 << VD_AGENT_CAP_CLIPBOARD_SELECTION);
}

static uint32_t type_qemu_to_vdagent(enum QemuClipboardType type)
{
    switch (type) {
    case QEMU_CLIPBOARD_TYPE_TEXT:
        return VD_AGENT_CLIPBOARD_UTF8_TEXT;
    default:
        return VD_AGENT_CLIPBOARD_NONE;
    }
}

static void vdagent_send_clipboard_grab(VDAgentChardev *vd,
                                        QemuClipboardInfo *info)
{
    g_autofree VDAgentMessage *msg =
        g_malloc0(sizeof(VDAgentMessage) +
                  sizeof(uint32_t) * (QEMU_CLIPBOARD_TYPE__COUNT + 1) +
                  sizeof(uint32_t));
    uint8_t *s = msg->data;
    uint32_t *data = (uint32_t *)msg->data;
    uint32_t q, type;

    if (have_selection(vd)) {
        *s = info->selection;
        data++;
        msg->size += sizeof(uint32_t);
    } else if (info->selection != QEMU_CLIPBOARD_SELECTION_CLIPBOARD) {
        return;
    }

#if CHECK_SPICE_PROTOCOL_VERSION(0, 14, 1)
    if (vd->caps & (1 << VD_AGENT_CAP_CLIPBOARD_GRAB_SERIAL)) {
        if (!info->has_serial) {
            /* client should win */
            info->serial = vd->last_serial[info->selection]++;
            info->has_serial = true;
        }
        *data = info->serial;
        data++;
        msg->size += sizeof(uint32_t);
    }
#endif

    for (q = 0; q < QEMU_CLIPBOARD_TYPE__COUNT; q++) {
        type = type_qemu_to_vdagent(q);
        if (type != VD_AGENT_CLIPBOARD_NONE && info->types[q].available) {
            *data = type;
            data++;
            msg->size += sizeof(uint32_t);
        }
    }

    msg->type = VD_AGENT_CLIPBOARD_GRAB;
    vdagent_send_msg(vd, msg);
}

static void vdagent_send_clipboard_release(VDAgentChardev *vd,
                                           QemuClipboardInfo *info)
{
    g_autofree VDAgentMessage *msg = g_malloc0(sizeof(VDAgentMessage) +
                                               sizeof(uint32_t));

    if (have_selection(vd)) {
        uint8_t *s = msg->data;
        *s = info->selection;
        msg->size += sizeof(uint32_t);
    } else if (info->selection != QEMU_CLIPBOARD_SELECTION_CLIPBOARD) {
        return;
    }

    msg->type = VD_AGENT_CLIPBOARD_RELEASE;
    vdagent_send_msg(vd, msg);
}

static void vdagent_send_clipboard_data(VDAgentChardev *vd,
                                        QemuClipboardInfo *info,
                                        QemuClipboardType type)
{
    g_autofree VDAgentMessage *msg = g_malloc0(sizeof(VDAgentMessage) +
                                               sizeof(uint32_t) * 2 +
                                               info->types[type].size);

    uint8_t *s = msg->data;
    uint32_t *data = (uint32_t *)msg->data;

    if (have_selection(vd)) {
        *s = info->selection;
        data++;
        msg->size += sizeof(uint32_t);
    } else if (info->selection != QEMU_CLIPBOARD_SELECTION_CLIPBOARD) {
        return;
    }

    *data = type_qemu_to_vdagent(type);
    data++;
    msg->size += sizeof(uint32_t);

    memcpy(data, info->types[type].data, info->types[type].size);
    msg->size += info->types[type].size;

    msg->type = VD_AGENT_CLIPBOARD;
    vdagent_send_msg(vd, msg);
}

static void vdagent_send_empty_clipboard_data(VDAgentChardev *vd,
                                              QemuClipboardSelection selection,
                                              QemuClipboardType type)
{
    g_autoptr(QemuClipboardInfo) info = qemu_clipboard_info_new(&vd->cbpeer, selection);

    trace_vdagent_send_empty_clipboard();
    vdagent_send_clipboard_data(vd, info, type);
}

static void vdagent_clipboard_update_info(VDAgentChardev *vd,
                                          QemuClipboardInfo *info)
{
    QemuClipboardSelection s = info->selection;
    QemuClipboardType type;
    bool self_update = info->owner == &vd->cbpeer;

    if (info != qemu_clipboard_info(s)) {
        vd->cbpending[s] = 0;
        if (!self_update) {
            if (info->owner) {
                vdagent_send_clipboard_grab(vd, info);
            } else {
                vdagent_send_clipboard_release(vd, info);
            }
        }
        return;
    }

    if (self_update) {
        return;
    }

    for (type = 0; type < QEMU_CLIPBOARD_TYPE__COUNT; type++) {
        if (vd->cbpending[s] & (1 << type)) {
            vd->cbpending[s] &= ~(1 << type);
            vdagent_send_clipboard_data(vd, info, type);
        }
    }
}

static void vdagent_clipboard_reset_serial(VDAgentChardev *vd)
{
    Chardev *chr = CHARDEV(vd);

    /* reopen the agent connection to reset the serial state */
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
    /* OPENED again after the guest disconnected, see set_fe_open */
}

static void vdagent_clipboard_notify(Notifier *notifier, void *data)
{
    VDAgentChardev *vd =
        container_of(notifier, VDAgentChardev, cbpeer.notifier);
    QemuClipboardNotify *notify = data;

    switch (notify->type) {
    case QEMU_CLIPBOARD_UPDATE_INFO:
        vdagent_clipboard_update_info(vd, notify->info);
        return;
    case QEMU_CLIPBOARD_RESET_SERIAL:
        vdagent_clipboard_reset_serial(vd);
        return;
    }
}

static void vdagent_clipboard_request(QemuClipboardInfo *info,
                                      QemuClipboardType qtype)
{
    VDAgentChardev *vd = container_of(info->owner, VDAgentChardev, cbpeer);
    g_autofree VDAgentMessage *msg = g_malloc0(sizeof(VDAgentMessage) +
                                               sizeof(uint32_t) * 2);
    uint32_t type = type_qemu_to_vdagent(qtype);
    uint8_t *s = msg->data;
    uint32_t *data = (uint32_t *)msg->data;

    if (type == VD_AGENT_CLIPBOARD_NONE) {
        return;
    }

    if (have_selection(vd)) {
        *s = info->selection;
        data++;
        msg->size += sizeof(uint32_t);
    }

    *data = type;
    msg->size += sizeof(uint32_t);

    msg->type = VD_AGENT_CLIPBOARD_REQUEST;
    vdagent_send_msg(vd, msg);
}

static void vdagent_clipboard_recv_grab(VDAgentChardev *vd, uint8_t s, uint32_t size, void *data)
{
    g_autoptr(QemuClipboardInfo) info = NULL;

    trace_vdagent_cb_grab_selection(GET_NAME(sel_name, s));
    info = qemu_clipboard_info_new(&vd->cbpeer, s);
#if CHECK_SPICE_PROTOCOL_VERSION(0, 14, 1)
    if (vd->caps & (1 << VD_AGENT_CAP_CLIPBOARD_GRAB_SERIAL)) {
        if (size < sizeof(uint32_t)) {
            /* this shouldn't happen! */
            return;
        }

        info->has_serial = true;
        info->serial = *(uint32_t *)data;
        if (info->serial < vd->last_serial[s]) {
            trace_vdagent_cb_grab_discard(GET_NAME(sel_name, s),
                                          vd->last_serial[s], info->serial);
            /* discard lower-ordering guest grab */
            return;
        }
        vd->last_serial[s] = info->serial;
        data += sizeof(uint32_t);
        size -= sizeof(uint32_t);
    }
#endif
    if (size > sizeof(uint32_t) * 10) {
        /*
         * spice has 6 types as of 2021. Limiting to 10 entries
         * so we have some wiggle room.
         */
        return;
    }
    while (size >= sizeof(uint32_t)) {
        trace_vdagent_cb_grab_type(GET_NAME(type_name, *(uint32_t *)data));
        switch (*(uint32_t *)data) {
        case VD_AGENT_CLIPBOARD_UTF8_TEXT:
            info->types[QEMU_CLIPBOARD_TYPE_TEXT].available = true;
            break;
        default:
            break;
        }
        data += sizeof(uint32_t);
        size -= sizeof(uint32_t);
    }
    qemu_clipboard_update(info);
}

static void vdagent_clipboard_recv_request(VDAgentChardev *vd, uint8_t s, uint32_t size, void *data)
{
    QemuClipboardType type;
    QemuClipboardInfo *info;

    if (size < sizeof(uint32_t)) {
        return;
    }
    switch (*(uint32_t *)data) {
    case VD_AGENT_CLIPBOARD_UTF8_TEXT:
        type = QEMU_CLIPBOARD_TYPE_TEXT;
        break;
    default:
        return;
    }

    info = qemu_clipboard_info(s);
    if (info && info->types[type].available && info->owner != &vd->cbpeer) {
        if (info->types[type].data) {
            vdagent_send_clipboard_data(vd, info, type);
        } else {
            vd->cbpending[s] |= (1 << type);
            qemu_clipboard_request(info, type);
        }
    } else {
        vdagent_send_empty_clipboard_data(vd, s, type);
    }
}

static void vdagent_clipboard_recv_data(VDAgentChardev *vd, uint8_t s, uint32_t size, void *data)
{
    QemuClipboardType type;

    if (size < sizeof(uint32_t)) {
        return;
    }
    switch (*(uint32_t *)data) {
    case VD_AGENT_CLIPBOARD_UTF8_TEXT:
        type = QEMU_CLIPBOARD_TYPE_TEXT;
        break;
    default:
        return;
    }
    data += 4;
    size -= 4;

    if (qemu_clipboard_peer_owns(&vd->cbpeer, s)) {
        qemu_clipboard_set_data(&vd->cbpeer, qemu_clipboard_info(s),
                                type, size, data, true);
    }
}

static void vdagent_clipboard_recv_release(VDAgentChardev *vd, uint8_t s)
{
    qemu_clipboard_peer_release(&vd->cbpeer, s);
}

static void vdagent_chr_recv_clipboard(VDAgentChardev *vd, VDAgentMessage *msg)
{
    uint8_t s = VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD;
    uint32_t size = msg->size;
    void *data = msg->data;

    if (have_selection(vd)) {
        if (size < 4) {
            return;
        }
        s = *(uint8_t *)data;
        if (s >= QEMU_CLIPBOARD_SELECTION__COUNT) {
            return;
        }
        data += 4;
        size -= 4;
    }

    switch (msg->type) {
    case VD_AGENT_CLIPBOARD_GRAB:
        return vdagent_clipboard_recv_grab(vd, s, size, data);
    case VD_AGENT_CLIPBOARD_REQUEST:
        return vdagent_clipboard_recv_request(vd, s, size, data);
    case VD_AGENT_CLIPBOARD: /* data */
        return vdagent_clipboard_recv_data(vd, s, size, data);
    case VD_AGENT_CLIPBOARD_RELEASE:
        return vdagent_clipboard_recv_release(vd, s);
    default:
        g_assert_not_reached();
    }
}

/* ------------------------------------------------------------------ */
/* chardev backend                                                    */

static void vdagent_chr_open(Chardev *chr,
                             ChardevBackend *backend,
                             bool *be_opened,
                             Error **errp)
{
    VDAgentChardev *vd = QEMU_VDAGENT_CHARDEV(chr);
    ChardevQemuVDAgent *cfg = backend->u.qemu_vdagent.data;

#if HOST_BIG_ENDIAN
    /*
     * TODO: vdagent protocol is defined to be LE,
     * so we have to byteswap everything on BE hosts.
     */
    error_setg(errp, "vdagent is not supported on bigendian hosts");
    return;
#endif

    if (migrate_add_blocker(&vd->migration_blocker, errp) != 0) {
        return;
    }

    vd->mouse = VDAGENT_MOUSE_DEFAULT;
    if (cfg->has_mouse) {
        vd->mouse = cfg->mouse;
    }

    vd->clipboard = VDAGENT_CLIPBOARD_DEFAULT;
    if (cfg->has_clipboard) {
        vd->clipboard = cfg->clipboard;
    }

    if (vd->mouse) {
        vd->mouse_hs = qemu_input_handler_register(&vd->mouse_dev,
                                                   &vdagent_mouse_handler);
    }

    *be_opened = true;
}

static void vdagent_chr_recv_caps(VDAgentChardev *vd, VDAgentMessage *msg)
{
    VDAgentAnnounceCapabilities *caps = (void *)msg->data;
    int i;

    if (msg->size < (sizeof(VDAgentAnnounceCapabilities) +
                     sizeof(uint32_t))) {
        return;
    }

    for (i = 0; i < ARRAY_SIZE(cap_name); i++) {
        if (caps->caps[0] & (1 << i)) {
            trace_vdagent_peer_cap(GET_NAME(cap_name, i));
        }
    }

    vd->caps = caps->caps[0];
    if (caps->request) {
        vdagent_send_caps(vd, false);
    }
    if (have_mouse(vd) && vd->mouse_hs) {
        qemu_input_handler_activate(vd->mouse_hs);
    }

    memset(vd->last_serial, 0, sizeof(vd->last_serial));

    if (have_clipboard(vd) && vd->cbpeer.notifier.notify == NULL) {
        qemu_clipboard_reset_serial();

        vd->cbpeer.name = "vdagent";
        vd->cbpeer.notifier.notify = vdagent_clipboard_notify;
        vd->cbpeer.request = vdagent_clipboard_request;
        qemu_clipboard_peer_register(&vd->cbpeer);
    }
}

static void vdagent_chr_recv_msg(VDAgentChardev *vd, VDAgentMessage *msg)
{
    trace_vdagent_recv_msg(GET_NAME(msg_name, msg->type), msg->size);

    switch (msg->type) {
    case VD_AGENT_ANNOUNCE_CAPABILITIES:
        vdagent_chr_recv_caps(vd, msg);
        break;
    case VD_AGENT_CLIPBOARD:
    case VD_AGENT_CLIPBOARD_GRAB:
    case VD_AGENT_CLIPBOARD_REQUEST:
    case VD_AGENT_CLIPBOARD_RELEASE:
        if (have_clipboard(vd)) {
            vdagent_chr_recv_clipboard(vd, msg);
        }
        break;
    default:
        break;
    }
}

static void vdagent_reset_xbuf(VDAgentChardev *vd)
{
    g_clear_pointer(&vd->xbuf, g_free);
    vd->xoff = 0;
    vd->xsize = 0;
}

static void vdagent_chr_recv_chunk(VDAgentChardev *vd)
{
    VDAgentMessage *msg = (void *)vd->msgbuf;

    if (!vd->xsize) {
        if (vd->msgsize < sizeof(*msg)) {
            error_report("%s: message too small: %d < %zd", __func__,
                         vd->msgsize, sizeof(*msg));
            return;
        }
        if (vd->msgsize == msg->size + sizeof(*msg)) {
            vdagent_chr_recv_msg(vd, msg);
            return;
        }
    }

    if (!vd->xsize) {
        vd->xsize = msg->size + sizeof(*msg);
        vd->xbuf = g_malloc0(vd->xsize);
    }

    if (vd->xoff + vd->msgsize > vd->xsize) {
        error_report("%s: Oops: %d+%d > %d", __func__,
                     vd->xoff, vd->msgsize, vd->xsize);
        vdagent_reset_xbuf(vd);
        return;
    }

    memcpy(vd->xbuf + vd->xoff, vd->msgbuf, vd->msgsize);
    vd->xoff += vd->msgsize;
    if (vd->xoff < vd->xsize) {
        return;
    }

    msg = (void *)vd->xbuf;
    vdagent_chr_recv_msg(vd, msg);
    vdagent_reset_xbuf(vd);
}

static void vdagent_reset_bufs(VDAgentChardev *vd)
{
    memset(&vd->chunk, 0, sizeof(vd->chunk));
    vd->chunksize = 0;
    g_free(vd->msgbuf);
    vd->msgbuf = NULL;
    vd->msgsize = 0;
}

static int vdagent_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    VDAgentChardev *vd = QEMU_VDAGENT_CHARDEV(chr);
    uint32_t copy, ret = len;

    while (len) {
        if (vd->chunksize < sizeof(vd->chunk)) {
            copy = sizeof(vd->chunk) - vd->chunksize;
            if (copy > len) {
                copy = len;
            }
            memcpy((void *)(&vd->chunk) + vd->chunksize, buf, copy);
            vd->chunksize += copy;
            buf += copy;
            len -= copy;
            if (vd->chunksize < sizeof(vd->chunk)) {
                break;
            }

            assert(vd->msgbuf == NULL);
            vd->msgbuf = g_malloc0(vd->chunk.size);
        }

        copy = vd->chunk.size - vd->msgsize;
        if (copy > len) {
            copy = len;
        }
        memcpy(vd->msgbuf + vd->msgsize, buf, copy);
        vd->msgsize += copy;
        buf += copy;
        len -= copy;

        if (vd->msgsize == vd->chunk.size) {
            trace_vdagent_recv_chunk(vd->chunk.size);
            vdagent_chr_recv_chunk(vd);
            vdagent_reset_bufs(vd);
        }
    }

    return ret;
}

static void vdagent_chr_accept_input(Chardev *chr)
{
    VDAgentChardev *vd = QEMU_VDAGENT_CHARDEV(chr);

    vdagent_send_buf(vd);
}

static void vdagent_disconnect(VDAgentChardev *vd)
{
    trace_vdagent_disconnect();

    buffer_reset(&vd->outbuf);
    vdagent_reset_bufs(vd);
    vd->caps = 0;
    if (vd->mouse_hs) {
        qemu_input_handler_deactivate(vd->mouse_hs);
    }
    if (vd->cbpeer.notifier.notify) {
        qemu_clipboard_peer_unregister(&vd->cbpeer);
        memset(&vd->cbpeer, 0, sizeof(vd->cbpeer));
    }
}

static void vdagent_chr_set_fe_open(struct Chardev *chr, int fe_open)
{
    VDAgentChardev *vd = QEMU_VDAGENT_CHARDEV(chr);

    trace_vdagent_fe_open(fe_open);

    if (!fe_open) {
        trace_vdagent_close();
        vdagent_disconnect(vd);
        /* To reset_serial, we CLOSED our side. Make sure the other end knows we
         * are ready again. */
        qemu_chr_be_event(chr, CHR_EVENT_OPENED);
        return;
    }

    vdagent_send_caps(vd, true);
}

static void vdagent_chr_parse(QemuOpts *opts, ChardevBackend *backend,
                              Error **errp)
{
    ChardevQemuVDAgent *cfg;

    backend->type = CHARDEV_BACKEND_KIND_QEMU_VDAGENT;
    cfg = backend->u.qemu_vdagent.data = g_new0(ChardevQemuVDAgent, 1);
    qemu_chr_parse_common(opts, qapi_ChardevQemuVDAgent_base(cfg));
    cfg->has_mouse = true;
    cfg->mouse = qemu_opt_get_bool(opts, "mouse", VDAGENT_MOUSE_DEFAULT);
    cfg->has_clipboard = true;
    cfg->clipboard = qemu_opt_get_bool(opts, "clipboard", VDAGENT_CLIPBOARD_DEFAULT);
}

/* ------------------------------------------------------------------ */

static void vdagent_chr_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse            = vdagent_chr_parse;
    cc->open             = vdagent_chr_open;
    cc->chr_write        = vdagent_chr_write;
    cc->chr_set_fe_open  = vdagent_chr_set_fe_open;
    cc->chr_accept_input = vdagent_chr_accept_input;
}

static void vdagent_chr_init(Object *obj)
{
    VDAgentChardev *vd = QEMU_VDAGENT_CHARDEV(obj);

    buffer_init(&vd->outbuf, "vdagent-outbuf");
    error_setg(&vd->migration_blocker,
               "The vdagent chardev doesn't yet support migration");
}

static void vdagent_chr_fini(Object *obj)
{
    VDAgentChardev *vd = QEMU_VDAGENT_CHARDEV(obj);

    migrate_del_blocker(&vd->migration_blocker);
    vdagent_disconnect(vd);
    if (vd->mouse_hs) {
        qemu_input_handler_unregister(vd->mouse_hs);
    }
    buffer_free(&vd->outbuf);
}

static const TypeInfo vdagent_chr_type_info = {
    .name = TYPE_CHARDEV_QEMU_VDAGENT,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(VDAgentChardev),
    .instance_init = vdagent_chr_init,
    .instance_finalize = vdagent_chr_fini,
    .class_init = vdagent_chr_class_init,
};

static void register_types(void)
{
    type_register_static(&vdagent_chr_type_info);
}

type_init(register_types);
