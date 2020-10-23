/*
 * QEMU file monitor Linux inotify impl
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/filemonitor.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "trace.h"

#include <sys/inotify.h>

struct QFileMonitor {
    int fd;
    QemuMutex lock; /* protects dirs & idmap */
    GHashTable *dirs; /* dirname => QFileMonitorDir */
    GHashTable *idmap; /* inotify ID => dirname */
};


typedef struct {
    int64_t id; /* watch ID */
    char *filename; /* optional filter */
    QFileMonitorHandler cb;
    void *opaque;
} QFileMonitorWatch;


typedef struct {
    char *path;
    int inotify_id; /* inotify ID */
    int next_file_id; /* file ID counter */
    GArray *watches; /* QFileMonitorWatch elements */
} QFileMonitorDir;


static void qemu_file_monitor_watch(void *arg)
{
    QFileMonitor *mon = arg;
    char buf[4096]
        __attribute__ ((aligned(__alignof__(struct inotify_event))));
    int used = 0;
    int len;

    qemu_mutex_lock(&mon->lock);

    if (mon->fd == -1) {
        qemu_mutex_unlock(&mon->lock);
        return;
    }

    len = read(mon->fd, buf, sizeof(buf));

    if (len < 0) {
        if (errno != EAGAIN) {
            error_report("Failure monitoring inotify FD '%s',"
                         "disabling events", strerror(errno));
            goto cleanup;
        }

        /* no more events right now */
        goto cleanup;
    }

    /* Loop over all events in the buffer */
    while (used < len) {
        struct inotify_event *ev =
            (struct inotify_event *)(buf + used);
        const char *name = ev->len ? ev->name : "";
        QFileMonitorDir *dir = g_hash_table_lookup(mon->idmap,
                                                   GINT_TO_POINTER(ev->wd));
        uint32_t iev = ev->mask &
            (IN_CREATE | IN_MODIFY | IN_DELETE | IN_IGNORED |
             IN_MOVED_TO | IN_MOVED_FROM | IN_ATTRIB);
        int qev;
        gsize i;

        used += sizeof(struct inotify_event) + ev->len;

        if (!dir) {
            continue;
        }

        /*
         * During a rename operation, the old name gets
         * IN_MOVED_FROM and the new name gets IN_MOVED_TO.
         * To simplify life for callers, we turn these into
         * DELETED and CREATED events
         */
        switch (iev) {
        case IN_CREATE:
        case IN_MOVED_TO:
            qev = QFILE_MONITOR_EVENT_CREATED;
            break;
        case IN_MODIFY:
            qev = QFILE_MONITOR_EVENT_MODIFIED;
            break;
        case IN_DELETE:
        case IN_MOVED_FROM:
            qev = QFILE_MONITOR_EVENT_DELETED;
            break;
        case IN_ATTRIB:
            qev = QFILE_MONITOR_EVENT_ATTRIBUTES;
            break;
        case IN_IGNORED:
            qev = QFILE_MONITOR_EVENT_IGNORED;
            break;
        default:
            g_assert_not_reached();
        }

        trace_qemu_file_monitor_event(mon, dir->path, name, ev->mask,
                                      dir->inotify_id);
        for (i = 0; i < dir->watches->len; i++) {
            QFileMonitorWatch *watch = &g_array_index(dir->watches,
                                                      QFileMonitorWatch,
                                                      i);

            if (watch->filename == NULL ||
                (name && g_str_equal(watch->filename, name))) {
                trace_qemu_file_monitor_dispatch(mon, dir->path, name,
                                                 qev, watch->cb,
                                                 watch->opaque, watch->id);
                watch->cb(watch->id, qev, name, watch->opaque);
            }
        }
    }

 cleanup:
    qemu_mutex_unlock(&mon->lock);
}


static void
qemu_file_monitor_dir_free(void *data)
{
    QFileMonitorDir *dir = data;
    gsize i;

    for (i = 0; i < dir->watches->len; i++) {
        QFileMonitorWatch *watch = &g_array_index(dir->watches,
                                                  QFileMonitorWatch, i);
        g_free(watch->filename);
    }
    g_array_unref(dir->watches);
    g_free(dir->path);
    g_free(dir);
}


QFileMonitor *
qemu_file_monitor_new(Error **errp)
{
    int fd;
    QFileMonitor *mon;

    fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        error_setg_errno(errp, errno,
                         "Unable to initialize inotify");
        return NULL;
    }

    mon = g_new0(QFileMonitor, 1);
    qemu_mutex_init(&mon->lock);
    mon->fd = fd;

    mon->dirs = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                      qemu_file_monitor_dir_free);
    mon->idmap = g_hash_table_new(g_direct_hash, g_direct_equal);

    trace_qemu_file_monitor_new(mon, mon->fd);

    return mon;
}

static gboolean
qemu_file_monitor_free_idle(void *opaque)
{
    QFileMonitor *mon = opaque;

    if (!mon) {
        return G_SOURCE_REMOVE;
    }

    qemu_mutex_lock(&mon->lock);

    g_hash_table_unref(mon->idmap);
    g_hash_table_unref(mon->dirs);

    qemu_mutex_unlock(&mon->lock);

    qemu_mutex_destroy(&mon->lock);
    g_free(mon);

    return G_SOURCE_REMOVE;
}

void
qemu_file_monitor_free(QFileMonitor *mon)
{
    if (!mon) {
        return;
    }

    qemu_mutex_lock(&mon->lock);
    if (mon->fd != -1) {
        qemu_set_fd_handler(mon->fd, NULL, NULL, NULL);
        close(mon->fd);
        mon->fd = -1;
    }
    qemu_mutex_unlock(&mon->lock);

    /*
     * Can't free it yet, because another thread
     * may be running event loop, so the inotify
     * callback might be pending. Using an idle
     * source ensures we'll only free after the
     * pending callback is done
     */
    g_idle_add((GSourceFunc)qemu_file_monitor_free_idle, mon);
}

int64_t
qemu_file_monitor_add_watch(QFileMonitor *mon,
                            const char *dirpath,
                            const char *filename,
                            QFileMonitorHandler cb,
                            void *opaque,
                            Error **errp)
{
    QFileMonitorDir *dir;
    QFileMonitorWatch watch;
    int64_t ret = -1;

    qemu_mutex_lock(&mon->lock);
    dir = g_hash_table_lookup(mon->dirs, dirpath);
    if (!dir) {
        int rv = inotify_add_watch(mon->fd, dirpath,
                                   IN_CREATE | IN_DELETE | IN_MODIFY |
                                   IN_MOVED_TO | IN_MOVED_FROM | IN_ATTRIB);

        if (rv < 0) {
            error_setg_errno(errp, errno, "Unable to watch '%s'", dirpath);
            goto cleanup;
        }

        trace_qemu_file_monitor_enable_watch(mon, dirpath, rv);

        dir = g_new0(QFileMonitorDir, 1);
        dir->path = g_strdup(dirpath);
        dir->inotify_id = rv;
        dir->watches = g_array_new(FALSE, TRUE, sizeof(QFileMonitorWatch));

        g_hash_table_insert(mon->dirs, dir->path, dir);
        g_hash_table_insert(mon->idmap, GINT_TO_POINTER(rv), dir);

        if (g_hash_table_size(mon->dirs) == 1) {
            qemu_set_fd_handler(mon->fd, qemu_file_monitor_watch, NULL, mon);
        }
    }

    watch.id = (((int64_t)dir->inotify_id) << 32) | dir->next_file_id++;
    watch.filename = g_strdup(filename);
    watch.cb = cb;
    watch.opaque = opaque;

    g_array_append_val(dir->watches, watch);

    trace_qemu_file_monitor_add_watch(mon, dirpath,
                                      filename ? filename : "<none>",
                                      cb, opaque, watch.id);

    ret = watch.id;

 cleanup:
    qemu_mutex_unlock(&mon->lock);
    return ret;
}


void qemu_file_monitor_remove_watch(QFileMonitor *mon,
                                    const char *dirpath,
                                    int64_t id)
{
    QFileMonitorDir *dir;
    gsize i;

    qemu_mutex_lock(&mon->lock);

    trace_qemu_file_monitor_remove_watch(mon, dirpath, id);

    dir = g_hash_table_lookup(mon->dirs, dirpath);
    if (!dir) {
        goto cleanup;
    }

    for (i = 0; i < dir->watches->len; i++) {
        QFileMonitorWatch *watch = &g_array_index(dir->watches,
                                                  QFileMonitorWatch, i);
        if (watch->id == id) {
            g_free(watch->filename);
            g_array_remove_index(dir->watches, i);
            break;
        }
    }

    if (dir->watches->len == 0) {
        inotify_rm_watch(mon->fd, dir->inotify_id);
        trace_qemu_file_monitor_disable_watch(mon, dir->path, dir->inotify_id);

        g_hash_table_remove(mon->idmap, GINT_TO_POINTER(dir->inotify_id));
        g_hash_table_remove(mon->dirs, dir->path);

        if (g_hash_table_size(mon->dirs) == 0) {
            qemu_set_fd_handler(mon->fd, NULL, NULL, NULL);
        }
    }

 cleanup:
    qemu_mutex_unlock(&mon->lock);
}
