#include "qemu/osdep.h"
#include "ui/clipboard.h"
#include "trace.h"

static NotifierList clipboard_notifiers =
    NOTIFIER_LIST_INITIALIZER(clipboard_notifiers);

static QemuClipboardInfo *cbinfo[QEMU_CLIPBOARD_SELECTION__COUNT];

void qemu_clipboard_peer_register(QemuClipboardPeer *peer)
{
    notifier_list_add(&clipboard_notifiers, &peer->notifier);
}

void qemu_clipboard_peer_unregister(QemuClipboardPeer *peer)
{
    int i;

    for (i = 0; i < QEMU_CLIPBOARD_SELECTION__COUNT; i++) {
        qemu_clipboard_peer_release(peer, i);
    }
    notifier_remove(&peer->notifier);
}

bool qemu_clipboard_peer_owns(QemuClipboardPeer *peer,
                              QemuClipboardSelection selection)
{
    QemuClipboardInfo *info = qemu_clipboard_info(selection);

    return info && info->owner == peer;
}

void qemu_clipboard_peer_release(QemuClipboardPeer *peer,
                                 QemuClipboardSelection selection)
{
    g_autoptr(QemuClipboardInfo) info = NULL;

    if (qemu_clipboard_peer_owns(peer, selection)) {
        /* set empty clipboard info */
        info = qemu_clipboard_info_new(NULL, selection);
        qemu_clipboard_update(info);
    }
}

bool qemu_clipboard_check_serial(QemuClipboardInfo *info, bool client)
{
    bool ok;

    if (!info->has_serial ||
        !cbinfo[info->selection] ||
        !cbinfo[info->selection]->has_serial) {
        trace_clipboard_check_serial(-1, -1, true);
        return true;
    }

    if (client) {
        ok = info->serial >= cbinfo[info->selection]->serial;
    } else {
        ok = info->serial > cbinfo[info->selection]->serial;
    }

    trace_clipboard_check_serial(cbinfo[info->selection]->serial, info->serial, ok);
    return ok;
}

void qemu_clipboard_update(QemuClipboardInfo *info)
{
    uint32_t type;
    QemuClipboardNotify notify = {
        .type = QEMU_CLIPBOARD_UPDATE_INFO,
        .info = info,
    };
    assert(info->selection < QEMU_CLIPBOARD_SELECTION__COUNT);

    for (type = 0; type < QEMU_CLIPBOARD_TYPE__COUNT; type++) {
        /*
         * If data is missing, the clipboard owner's 'request' callback needs to
         * be set. Otherwise, there is no way to get the clipboard data and
         * qemu_clipboard_request() cannot be called.
         */
        if (info->types[type].available && !info->types[type].data) {
            assert(info->owner && info->owner->request);
        }
    }

    notifier_list_notify(&clipboard_notifiers, &notify);

    if (cbinfo[info->selection] != info) {
        qemu_clipboard_info_unref(cbinfo[info->selection]);
        cbinfo[info->selection] = qemu_clipboard_info_ref(info);
    }
}

QemuClipboardInfo *qemu_clipboard_info(QemuClipboardSelection selection)
{
    assert(selection < QEMU_CLIPBOARD_SELECTION__COUNT);

    return cbinfo[selection];
}

QemuClipboardInfo *qemu_clipboard_info_new(QemuClipboardPeer *owner,
                                           QemuClipboardSelection selection)
{
    QemuClipboardInfo *info = g_new0(QemuClipboardInfo, 1);

    info->owner = owner;
    info->selection = selection;
    info->refcount = 1;

    return info;
}

QemuClipboardInfo *qemu_clipboard_info_ref(QemuClipboardInfo *info)
{
    info->refcount++;
    return info;
}

void qemu_clipboard_info_unref(QemuClipboardInfo *info)
{
    uint32_t type;

    if (!info) {
        return;
    }

    info->refcount--;
    if (info->refcount > 0) {
        return;
    }

    for (type = 0; type < QEMU_CLIPBOARD_TYPE__COUNT; type++) {
        g_free(info->types[type].data);
    }
    g_free(info);
}

void qemu_clipboard_request(QemuClipboardInfo *info,
                            QemuClipboardType type)
{
    if (info->types[type].data ||
        info->types[type].requested ||
        !info->types[type].available ||
        !info->owner)
        return;

    assert(info->owner->request);

    info->types[type].requested = true;
    info->owner->request(info, type);
}

void qemu_clipboard_reset_serial(void)
{
    QemuClipboardNotify notify = { .type = QEMU_CLIPBOARD_RESET_SERIAL };
    int i;

    for (i = 0; i < QEMU_CLIPBOARD_SELECTION__COUNT; i++) {
        QemuClipboardInfo *info = qemu_clipboard_info(i);
        if (info) {
            info->serial = 0;
        }
    }
    notifier_list_notify(&clipboard_notifiers, &notify);
}

void qemu_clipboard_set_data(QemuClipboardPeer *peer,
                             QemuClipboardInfo *info,
                             QemuClipboardType type,
                             uint32_t size,
                             const void *data,
                             bool update)
{
    if (!info ||
        info->owner != peer) {
        return;
    }

    g_free(info->types[type].data);
    if (size) {
        info->types[type].data = g_memdup2(data, size);
        info->types[type].size = size;
        info->types[type].available = true;
    } else {
        info->types[type].data = NULL;
        info->types[type].size = 0;
        info->types[type].available = false;
    }

    if (update) {
        qemu_clipboard_update(info);
    }
}
