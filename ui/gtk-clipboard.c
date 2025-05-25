/*
 * GTK UI -- clipboard support
 *
 * Copyright (C) 2021 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

#include "ui/gtk.h"

static QemuClipboardSelection gd_find_selection(GtkDisplayState *gd,
                                                GtkClipboard *clipboard)
{
    QemuClipboardSelection s;

    for (s = 0; s < QEMU_CLIPBOARD_SELECTION__COUNT; s++) {
        if (gd->gtkcb[s] == clipboard) {
            return s;
        }
    }
    return QEMU_CLIPBOARD_SELECTION_CLIPBOARD;
}

static void gd_clipboard_get_data(GtkClipboard     *clipboard,
                                  GtkSelectionData *selection_data,
                                  guint             selection_info,
                                  gpointer          data)
{
    GtkDisplayState *gd = data;
    QemuClipboardSelection s = gd_find_selection(gd, clipboard);
    QemuClipboardType type = QEMU_CLIPBOARD_TYPE_TEXT;
    g_autoptr(QemuClipboardInfo) info = NULL;

    info = qemu_clipboard_info_ref(qemu_clipboard_info(s));

    qemu_clipboard_request(info, type);
    while (info == qemu_clipboard_info(s) &&
           info->types[type].available &&
           info->types[type].data == NULL) {
        main_loop_wait(false);
    }

    if (info == qemu_clipboard_info(s) && gd->cbowner[s]) {
        gtk_selection_data_set_text(selection_data,
                                    info->types[type].data,
                                    info->types[type].size);
    } else {
        /* clipboard owner changed while waiting for the data */
    }
}

static void gd_clipboard_clear(GtkClipboard *clipboard,
                               gpointer data)
{
    GtkDisplayState *gd = data;
    QemuClipboardSelection s = gd_find_selection(gd, clipboard);

    gd->cbowner[s] = false;
}

static void gd_clipboard_update_info(GtkDisplayState *gd,
                                     QemuClipboardInfo *info)
{
    QemuClipboardSelection s = info->selection;
    bool self_update = info->owner == &gd->cbpeer;

    if (info != qemu_clipboard_info(s)) {
        gd->cbpending[s] = 0;
        if (!self_update) {
            g_autoptr(GtkTargetList) list = NULL;
            GtkTargetEntry *targets;
            gint n_targets;

            list = gtk_target_list_new(NULL, 0);
            if (info->types[QEMU_CLIPBOARD_TYPE_TEXT].available) {
                gtk_target_list_add_text_targets(list, 0);
            }
            targets = gtk_target_table_new_from_list(list, &n_targets);

            gtk_clipboard_clear(gd->gtkcb[s]);
            if (targets) {
                gd->cbowner[s] = true;
                if (!gtk_clipboard_set_with_data(gd->gtkcb[s],
                                                 targets, n_targets,
                                                 gd_clipboard_get_data,
                                                 gd_clipboard_clear,
                                                 gd)) {
                    warn_report("Failed to set GTK clipboard");
                }

                gtk_target_table_free(targets, n_targets);
            }
        }
        return;
    }

    if (self_update) {
        return;
    }

    /*
     * Clipboard got updated, with data probably.  No action here, we
     * are waiting for updates in gd_clipboard_get_data().
     */
}

static void gd_clipboard_notify(Notifier *notifier, void *data)
{
    GtkDisplayState *gd =
        container_of(notifier, GtkDisplayState, cbpeer.notifier);
    QemuClipboardNotify *notify = data;

    switch (notify->type) {
    case QEMU_CLIPBOARD_UPDATE_INFO:
        gd_clipboard_update_info(gd, notify->info);
        return;
    case QEMU_CLIPBOARD_RESET_SERIAL:
        /* ignore */
        return;
    }
}

static void gd_clipboard_request(QemuClipboardInfo *info,
                                 QemuClipboardType type)
{
    GtkDisplayState *gd = container_of(info->owner, GtkDisplayState, cbpeer);
    char *text;

    switch (type) {
    case QEMU_CLIPBOARD_TYPE_TEXT:
        text = gtk_clipboard_wait_for_text(gd->gtkcb[info->selection]);
        if (text) {
            qemu_clipboard_set_data(&gd->cbpeer, info, type,
                                    strlen(text), text, true);
            g_free(text);
        }
        break;
    default:
        break;
    }
}

static void gd_owner_change(GtkClipboard *clipboard,
                            GdkEvent *event,
                            gpointer data)
{
    GtkDisplayState *gd = data;
    QemuClipboardSelection s = gd_find_selection(gd, clipboard);
    QemuClipboardInfo *info;

    if (gd->cbowner[s]) {
        /* ignore notifications about our own grabs */
        return;
    }


    switch (event->owner_change.reason) {
    case GDK_OWNER_CHANGE_NEW_OWNER:
        info = qemu_clipboard_info_new(&gd->cbpeer, s);
        if (gtk_clipboard_wait_is_text_available(clipboard)) {
            info->types[QEMU_CLIPBOARD_TYPE_TEXT].available = true;
        }

        qemu_clipboard_update(info);
        qemu_clipboard_info_unref(info);
        break;
    default:
        qemu_clipboard_peer_release(&gd->cbpeer, s);
        gd->cbowner[s] = false;
        break;
    }
}

void gd_clipboard_init(GtkDisplayState *gd)
{
    gd->cbpeer.name = "gtk";
    gd->cbpeer.notifier.notify = gd_clipboard_notify;
    gd->cbpeer.request = gd_clipboard_request;
    qemu_clipboard_peer_register(&gd->cbpeer);

    gd->gtkcb[QEMU_CLIPBOARD_SELECTION_CLIPBOARD] =
        gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gd->gtkcb[QEMU_CLIPBOARD_SELECTION_PRIMARY] =
        gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    gd->gtkcb[QEMU_CLIPBOARD_SELECTION_SECONDARY] =
        gtk_clipboard_get(GDK_SELECTION_SECONDARY);

    g_signal_connect(gd->gtkcb[QEMU_CLIPBOARD_SELECTION_CLIPBOARD],
                     "owner-change", G_CALLBACK(gd_owner_change), gd);
    g_signal_connect(gd->gtkcb[QEMU_CLIPBOARD_SELECTION_PRIMARY],
                     "owner-change", G_CALLBACK(gd_owner_change), gd);
    g_signal_connect(gd->gtkcb[QEMU_CLIPBOARD_SELECTION_SECONDARY],
                     "owner-change", G_CALLBACK(gd_owner_change), gd);
}
