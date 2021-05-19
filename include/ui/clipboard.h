#ifndef QEMU_CLIPBOARD_H
#define QEMU_CLIPBOARD_H

#include "qemu/notify.h"

typedef enum QemuClipboardType QemuClipboardType;
typedef enum QemuClipboardSelection QemuClipboardSelection;
typedef struct QemuClipboardPeer QemuClipboardPeer;
typedef struct QemuClipboardInfo QemuClipboardInfo;

enum QemuClipboardType {
    QEMU_CLIPBOARD_TYPE_TEXT,  /* text/plain; charset=utf-8 */
    QEMU_CLIPBOARD_TYPE__COUNT,
};

/* same as VD_AGENT_CLIPBOARD_SELECTION_* */
enum QemuClipboardSelection {
    QEMU_CLIPBOARD_SELECTION_CLIPBOARD,
    QEMU_CLIPBOARD_SELECTION_PRIMARY,
    QEMU_CLIPBOARD_SELECTION_SECONDARY,
    QEMU_CLIPBOARD_SELECTION__COUNT,
};

struct QemuClipboardPeer {
    const char *name;
    Notifier update;
    void (*request)(QemuClipboardInfo *info,
                    QemuClipboardType type);
};

struct QemuClipboardInfo {
    uint32_t refcount;
    QemuClipboardPeer *owner;
    QemuClipboardSelection selection;
    struct {
        bool available;
        bool requested;
        size_t size;
        void *data;
    } types[QEMU_CLIPBOARD_TYPE__COUNT];
};

void qemu_clipboard_peer_register(QemuClipboardPeer *peer);
void qemu_clipboard_peer_unregister(QemuClipboardPeer *peer);

QemuClipboardInfo *qemu_clipboard_info_new(QemuClipboardPeer *owner,
                                           QemuClipboardSelection selection);
QemuClipboardInfo *qemu_clipboard_info_ref(QemuClipboardInfo *info);
void qemu_clipboard_info_unref(QemuClipboardInfo *info);

void qemu_clipboard_update(QemuClipboardInfo *info);
void qemu_clipboard_request(QemuClipboardInfo *info,
                            QemuClipboardType type);

void qemu_clipboard_set_data(QemuClipboardPeer *peer,
                             QemuClipboardInfo *info,
                             QemuClipboardType type,
                             uint32_t size,
                             void *data,
                             bool update);

#endif /* QEMU_CLIPBOARD_H */
