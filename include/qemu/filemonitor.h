/*
 * QEMU file monitor helper
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#ifndef QEMU_FILEMONITOR_H
#define QEMU_FILEMONITOR_H



typedef struct QFileMonitor QFileMonitor;

typedef enum {
    /* File has been created in a dir */
    QFILE_MONITOR_EVENT_CREATED,
    /* File has been modified in a dir */
    QFILE_MONITOR_EVENT_MODIFIED,
    /* File has been deleted in a dir */
    QFILE_MONITOR_EVENT_DELETED,
    /* File has attributes changed */
    QFILE_MONITOR_EVENT_ATTRIBUTES,
    /* Dir is no longer being monitored (due to deletion) */
    QFILE_MONITOR_EVENT_IGNORED,
} QFileMonitorEvent;


/**
 * QFileMonitorHandler:
 * @id: id from qemu_file_monitor_add_watch()
 * @event: the file change that occurred
 * @filename: the name of the file affected
 * @opaque: opaque data provided to qemu_file_monitor_add_watch()
 *
 * Invoked whenever a file changes. If @event is
 * QFILE_MONITOR_EVENT_IGNORED, @filename will be
 * empty.
 *
 */
typedef void (*QFileMonitorHandler)(int64_t id,
                                    QFileMonitorEvent event,
                                    const char *filename,
                                    void *opaque);

/**
 * qemu_file_monitor_new:
 * @errp: pointer to a NULL-initialized error object
 *
 * Create a handle for a file monitoring object.
 *
 * This object does locking internally to enable it to be
 * safe to use from multiple threads
 *
 * If the platform does not support file monitoring, an
 * error will be reported. Likewise if file monitoring
 * is supported, but cannot be initialized
 *
 * Currently this is implemented on Linux platforms with
 * the inotify subsystem.
 *
 * Returns: the new monitoring object, or NULL on error
 */
QFileMonitor *qemu_file_monitor_new(Error **errp);

/**
 * qemu_file_monitor_free:
 * @mon: the file monitor context
 *
 * Free resources associated with the file monitor,
 * including any currently registered watches.
 */
void qemu_file_monitor_free(QFileMonitor *mon);

/**
 * qemu_file_monitor_add_watch:
 * @mon: the file monitor context
 * @dirpath: the directory whose contents to watch
 * @filename: optional filename to filter on
 * @cb: the function to invoke when @dirpath has changes
 * @opaque: data to pass to @cb
 * @errp: pointer to a NULL-initialized error object
 *
 * Register to receive notifications of changes
 * in the directory @dirpath. All files in the
 * directory will be monitored. If the caller is
 * only interested in one specific file, @filename
 * can be used to filter events.
 *
 * Returns: a positive integer watch ID, or -1 on error
 */
int64_t qemu_file_monitor_add_watch(QFileMonitor *mon,
                                    const char *dirpath,
                                    const char *filename,
                                    QFileMonitorHandler cb,
                                    void *opaque,
                                    Error **errp);

/**
 * qemu_file_monitor_remove_watch:
 * @mon: the file monitor context
 * @dirpath: the directory whose contents to unwatch
 * @id: id of the watch to remove
 *
 * Removes the file monitoring watch @id, associated
 * with the directory @dirpath. This must never be
 * called from a QFileMonitorHandler callback, or a
 * deadlock will result.
 */
void qemu_file_monitor_remove_watch(QFileMonitor *mon,
                                    const char *dirpath,
                                    int64_t id);

#endif /* QEMU_FILEMONITOR_H */
