#include "qemu/osdep.h"
#include "ui/clipboard.h"

static NotifierList clipboard_notifiers =
    NOTIFIER_LIST_INITIALIZER(clipboard_notifiers);

void qemu_clipboard_peer_register(QemuClipboardPeer *peer)
{
    notifier_list_add(&clipboard_notifiers, &peer->update);
}

void qemu_clipboard_peer_unregister(QemuClipboardPeer *peer)
{
    notifier_remove(&peer->update);
}

void qemu_clipboard_update(QemuClipboardInfo *info)
{
    notifier_list_notify(&clipboard_notifiers, info);
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

    info->types[type].requested = true;
    info->owner->request(info, type);
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
    info->types[type].data = g_memdup(data, size);
    info->types[type].size = size;
    info->types[type].available = true;

    if (update) {
        qemu_clipboard_update(info);
    }
}
