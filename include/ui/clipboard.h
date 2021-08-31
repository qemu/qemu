#ifndef QEMU_CLIPBOARD_H
#define QEMU_CLIPBOARD_H

#include "qemu/notify.h"

/**
 * DOC: Introduction
 *
 * The header ``ui/clipboard.h`` declares the qemu clipboard interface.
 *
 * All qemu elements which want use the clipboard can register as
 * clipboard peer.  Subsequently they can set the clipboard content
 * and get notifications for clipboard updates.
 *
 * Typical users are user interfaces (gtk), remote access protocols
 * (vnc) and devices talking to the guest (vdagent).
 *
 * Even though the design allows different data types only plain text
 * is supported for now.
 */

typedef enum QemuClipboardType QemuClipboardType;
typedef enum QemuClipboardSelection QemuClipboardSelection;
typedef struct QemuClipboardPeer QemuClipboardPeer;
typedef struct QemuClipboardInfo QemuClipboardInfo;

/**
 * enum QemuClipboardType
 *
 * @QEMU_CLIPBOARD_TYPE_TEXT: text/plain; charset=utf-8
 * @QEMU_CLIPBOARD_TYPE__COUNT: type count.
 */
enum QemuClipboardType {
    QEMU_CLIPBOARD_TYPE_TEXT,
    QEMU_CLIPBOARD_TYPE__COUNT,
};

/* same as VD_AGENT_CLIPBOARD_SELECTION_* */
/**
 * enum QemuClipboardSelection
 *
 * @QEMU_CLIPBOARD_SELECTION_CLIPBOARD: clipboard (explitcit cut+paste).
 * @QEMU_CLIPBOARD_SELECTION_PRIMARY: primary selection (select + middle mouse button).
 * @QEMU_CLIPBOARD_SELECTION_SECONDARY: secondary selection (dunno).
 * @QEMU_CLIPBOARD_SELECTION__COUNT: selection count.
 */
enum QemuClipboardSelection {
    QEMU_CLIPBOARD_SELECTION_CLIPBOARD,
    QEMU_CLIPBOARD_SELECTION_PRIMARY,
    QEMU_CLIPBOARD_SELECTION_SECONDARY,
    QEMU_CLIPBOARD_SELECTION__COUNT,
};

/**
 * struct QemuClipboardPeer
 *
 * @name: peer name.
 * @update: notifier for clipboard updates.
 * @request: callback for clipboard data requests.
 *
 * Clipboard peer description.
 */
struct QemuClipboardPeer {
    const char *name;
    Notifier update;
    void (*request)(QemuClipboardInfo *info,
                    QemuClipboardType type);
};

/**
 * struct QemuClipboardInfo
 *
 * @refcount: reference counter.
 * @owner: clipboard owner.
 * @selection: clipboard selection.
 * @types: clipboard data array (one entry per type).
 *
 * Clipboard content data and metadata.
 */
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

/**
 * qemu_clipboard_peer_register
 *
 * @peer: peer information.
 *
 * Register clipboard peer.  Registering is needed for both active
 * (set+grab clipboard) and passive (watch clipboard for updates)
 * interaction with the qemu clipboard.
 */
void qemu_clipboard_peer_register(QemuClipboardPeer *peer);

/**
 * qemu_clipboard_peer_unregister
 *
 * @peer: peer information.
 *
 * Unregister clipboard peer.
 */
void qemu_clipboard_peer_unregister(QemuClipboardPeer *peer);

/**
 * qemu_clipboard_peer_owns
 *
 * @peer: peer information.
 * @selection: clipboard selection.
 *
 * Return TRUE if the peer owns the clipboard.
 */
bool qemu_clipboard_peer_owns(QemuClipboardPeer *peer,
                              QemuClipboardSelection selection);

/**
 * qemu_clipboard_peer_release
 *
 * @peer: peer information.
 * @selection: clipboard selection.
 *
 * If the peer owns the clipboard, release it.
 */
void qemu_clipboard_peer_release(QemuClipboardPeer *peer,
                                 QemuClipboardSelection selection);

/**
 * qemu_clipboard_info
 *
 * @selection: clipboard selection.
 *
 * Return the current clipboard data & owner informations.
 */
QemuClipboardInfo *qemu_clipboard_info(QemuClipboardSelection selection);

/**
 * qemu_clipboard_info_new
 *
 * @owner: clipboard owner.
 * @selection: clipboard selection.
 *
 * Allocate a new QemuClipboardInfo and initialize it with the given
 * @owner and @selection.
 *
 * QemuClipboardInfo is a reference-counted struct.  The new struct is
 * returned with a reference already taken (i.e. reference count is
 * one).
 */
QemuClipboardInfo *qemu_clipboard_info_new(QemuClipboardPeer *owner,
                                           QemuClipboardSelection selection);
/**
 * qemu_clipboard_info_ref
 *
 * @info: clipboard info.
 *
 * Increase @info reference count.
 */
QemuClipboardInfo *qemu_clipboard_info_ref(QemuClipboardInfo *info);

/**
 * qemu_clipboard_info_unref
 *
 * @info: clipboard info.
 *
 * Decrease @info reference count.  When the count goes down to zero
 * free the @info struct itself and all clipboard data.
 */
void qemu_clipboard_info_unref(QemuClipboardInfo *info);

/**
 * qemu_clipboard_update
 *
 * @info: clipboard info.
 *
 * Update the qemu clipboard.  Notify all registered peers (including
 * the clipboard owner) that the qemu clipboard has been updated.
 *
 * This is used for both new completely clipboard content and for
 * clipboard data updates in response to qemu_clipboard_request()
 * calls.
 */
void qemu_clipboard_update(QemuClipboardInfo *info);

/**
 * qemu_clipboard_request
 *
 * @info: clipboard info.
 * @type: clipboard data type.
 *
 * Request clipboard content.  Typically the clipboard owner only
 * advertises the available data types and provides the actual data
 * only on request.
 */
void qemu_clipboard_request(QemuClipboardInfo *info,
                            QemuClipboardType type);

/**
 * qemu_clipboard_set_data
 *
 * @peer: clipboard peer.
 * @info: clipboard info.
 * @type: clipboard data type.
 * @size: data size.
 * @data: data blob.
 * @update: notify peers about the update.
 *
 * Set clipboard content for the given @type.  This function will make
 * a copy of the content data and store that.
 */
void qemu_clipboard_set_data(QemuClipboardPeer *peer,
                             QemuClipboardInfo *info,
                             QemuClipboardType type,
                             uint32_t size,
                             const void *data,
                             bool update);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QemuClipboardInfo, qemu_clipboard_info_unref)

#endif /* QEMU_CLIPBOARD_H */
