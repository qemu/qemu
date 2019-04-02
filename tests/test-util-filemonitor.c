/*
 * Tests for util/filemonitor-*.c
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qemu/filemonitor.h"

#include <utime.h>

enum {
    QFILE_MONITOR_TEST_OP_ADD_WATCH,
    QFILE_MONITOR_TEST_OP_DEL_WATCH,
    QFILE_MONITOR_TEST_OP_EVENT,
    QFILE_MONITOR_TEST_OP_CREATE,
    QFILE_MONITOR_TEST_OP_APPEND,
    QFILE_MONITOR_TEST_OP_TRUNC,
    QFILE_MONITOR_TEST_OP_RENAME,
    QFILE_MONITOR_TEST_OP_TOUCH,
    QFILE_MONITOR_TEST_OP_UNLINK,
    QFILE_MONITOR_TEST_OP_MKDIR,
    QFILE_MONITOR_TEST_OP_RMDIR,
};

typedef struct {
    int type;
    const char *filesrc;
    const char *filedst;
    int64_t *watchid;
    int eventid;
} QFileMonitorTestOp;

typedef struct {
    int64_t id;
    QFileMonitorEvent event;
    char *filename;
} QFileMonitorTestRecord;


typedef struct {
    QemuMutex lock;
    GList *records;
} QFileMonitorTestData;

static QemuMutex evlock;
static bool evstopping;
static bool evrunning;
static bool debug;

/*
 * Main function for a background thread that is
 * running the event loop during the test
 */
static void *
qemu_file_monitor_test_event_loop(void *opaque G_GNUC_UNUSED)
{
    qemu_mutex_lock(&evlock);

    while (!evstopping) {
        qemu_mutex_unlock(&evlock);
        main_loop_wait(true);
        qemu_mutex_lock(&evlock);
    }

    evrunning = false;
    qemu_mutex_unlock(&evlock);
    return NULL;
}


/*
 * File monitor event handler which simply maintains
 * an ordered list of all events that it receives
 */
static void
qemu_file_monitor_test_handler(int64_t id,
                               QFileMonitorEvent event,
                               const char *filename,
                               void *opaque)
{
    QFileMonitorTestData *data = opaque;
    QFileMonitorTestRecord *rec = g_new0(QFileMonitorTestRecord, 1);

    rec->id = id;
    rec->event = event;
    rec->filename = g_strdup(filename);

    qemu_mutex_lock(&data->lock);
    data->records = g_list_append(data->records, rec);
    qemu_mutex_unlock(&data->lock);
}


static void
qemu_file_monitor_test_record_free(QFileMonitorTestRecord *rec)
{
    g_free(rec->filename);
    g_free(rec);
}


/*
 * Get the next event record that has been received by
 * the file monitor event handler. Since events are
 * emitted in the background thread running the event
 * loop, we can't assume there is a record available
 * immediately. Thus we will sleep for upto 5 seconds
 * to wait for the event to be queued for us.
 */
static QFileMonitorTestRecord *
qemu_file_monitor_test_next_record(QFileMonitorTestData *data)
{
    GTimer *timer = g_timer_new();
    QFileMonitorTestRecord *record = NULL;
    GList *tmp;

    qemu_mutex_lock(&data->lock);
    while (!data->records && g_timer_elapsed(timer, NULL) < 5) {
        qemu_mutex_unlock(&data->lock);
        usleep(10 * 1000);
        qemu_mutex_lock(&data->lock);
    }
    if (data->records) {
        record = data->records->data;
        tmp = data->records;
        data->records = g_list_remove_link(data->records, tmp);
        g_list_free(tmp);
    }
    qemu_mutex_unlock(&data->lock);

    g_timer_destroy(timer);
    return record;
}


/*
 * Check whether the event record we retrieved matches
 * data we were expecting to see for the event
 */
static bool
qemu_file_monitor_test_expect(QFileMonitorTestData *data,
                              int64_t id,
                              QFileMonitorEvent event,
                              const char *filename)
{
    QFileMonitorTestRecord *rec;
    bool ret = false;

    rec = qemu_file_monitor_test_next_record(data);

    if (!rec) {
        g_printerr("Missing event watch id %" PRIx64 " event %d file %s\n",
                   id, event, filename);
        return false;
    }

    if (id != rec->id) {
        g_printerr("Expected watch id %" PRIx64 " but got %" PRIx64 "\n",
                   id, rec->id);
        goto cleanup;
    }

    if (event != rec->event) {
        g_printerr("Expected event %d but got %d\n", event, rec->event);
        goto cleanup;
    }

    if (!g_str_equal(filename, rec->filename)) {
        g_printerr("Expected filename %s but got %s\n",
                   filename, rec->filename);
        goto cleanup;
    }

    ret = true;

 cleanup:
    qemu_file_monitor_test_record_free(rec);
    return ret;
}


static void
test_file_monitor_events(void)
{
    int64_t watch0 = 0;
    int64_t watch1 = 0;
    int64_t watch2 = 0;
    int64_t watch3 = 0;
    int64_t watch4 = 0;
    int64_t watch5 = 0;
    QFileMonitorTestOp ops[] = {
        { .type = QFILE_MONITOR_TEST_OP_ADD_WATCH,
          .filesrc = NULL, .watchid = &watch0 },
        { .type = QFILE_MONITOR_TEST_OP_ADD_WATCH,
          .filesrc = "one.txt", .watchid = &watch1 },
        { .type = QFILE_MONITOR_TEST_OP_ADD_WATCH,
          .filesrc = "two.txt", .watchid = &watch2 },


        { .type = QFILE_MONITOR_TEST_OP_CREATE,
          .filesrc = "one.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_CREATED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch1,
          .eventid = QFILE_MONITOR_EVENT_CREATED },


        { .type = QFILE_MONITOR_TEST_OP_CREATE,
          .filesrc = "two.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_CREATED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch2,
          .eventid = QFILE_MONITOR_EVENT_CREATED },


        { .type = QFILE_MONITOR_TEST_OP_CREATE,
          .filesrc = "three.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "three.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_CREATED },


        { .type = QFILE_MONITOR_TEST_OP_UNLINK,
          .filesrc = "three.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "three.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_DELETED },


        { .type = QFILE_MONITOR_TEST_OP_RENAME,
          .filesrc = "one.txt", .filedst = "two.txt" },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_DELETED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch1,
          .eventid = QFILE_MONITOR_EVENT_DELETED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_CREATED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch2,
          .eventid = QFILE_MONITOR_EVENT_CREATED },


        { .type = QFILE_MONITOR_TEST_OP_APPEND,
          .filesrc = "two.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_MODIFIED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch2,
          .eventid = QFILE_MONITOR_EVENT_MODIFIED },


        { .type = QFILE_MONITOR_TEST_OP_TOUCH,
          .filesrc = "two.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_ATTRIBUTES },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch2,
          .eventid = QFILE_MONITOR_EVENT_ATTRIBUTES },


        { .type = QFILE_MONITOR_TEST_OP_DEL_WATCH,
          .filesrc = "one.txt", .watchid = &watch1 },
        { .type = QFILE_MONITOR_TEST_OP_ADD_WATCH,
          .filesrc = "one.txt", .watchid = &watch3 },
        { .type = QFILE_MONITOR_TEST_OP_CREATE,
          .filesrc = "one.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_CREATED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch3,
          .eventid = QFILE_MONITOR_EVENT_CREATED },


        { .type = QFILE_MONITOR_TEST_OP_DEL_WATCH,
          .filesrc = "one.txt", .watchid = &watch3 },
        { .type = QFILE_MONITOR_TEST_OP_UNLINK,
          .filesrc = "one.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_DELETED },


        { .type = QFILE_MONITOR_TEST_OP_MKDIR,
          .filesrc = "fish", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "fish", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_CREATED },


        { .type = QFILE_MONITOR_TEST_OP_ADD_WATCH,
          .filesrc = "fish/", .watchid = &watch4 },
        { .type = QFILE_MONITOR_TEST_OP_ADD_WATCH,
          .filesrc = "fish/one.txt", .watchid = &watch5 },
        { .type = QFILE_MONITOR_TEST_OP_CREATE,
          .filesrc = "fish/one.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch4,
          .eventid = QFILE_MONITOR_EVENT_CREATED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch5,
          .eventid = QFILE_MONITOR_EVENT_CREATED },


        { .type = QFILE_MONITOR_TEST_OP_DEL_WATCH,
          .filesrc = "fish/one.txt", .watchid = &watch5 },
        { .type = QFILE_MONITOR_TEST_OP_RENAME,
          .filesrc = "fish/one.txt", .filedst = "two.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "one.txt", .watchid = &watch4,
          .eventid = QFILE_MONITOR_EVENT_DELETED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_CREATED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch2,
          .eventid = QFILE_MONITOR_EVENT_CREATED },


        { .type = QFILE_MONITOR_TEST_OP_RMDIR,
          .filesrc = "fish", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "", .watchid = &watch4,
          .eventid = QFILE_MONITOR_EVENT_IGNORED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "fish", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_DELETED },
        { .type = QFILE_MONITOR_TEST_OP_DEL_WATCH,
          .filesrc = "fish", .watchid = &watch4 },


        { .type = QFILE_MONITOR_TEST_OP_UNLINK,
          .filesrc = "two.txt", },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch0,
          .eventid = QFILE_MONITOR_EVENT_DELETED },
        { .type = QFILE_MONITOR_TEST_OP_EVENT,
          .filesrc = "two.txt", .watchid = &watch2,
          .eventid = QFILE_MONITOR_EVENT_DELETED },


        { .type = QFILE_MONITOR_TEST_OP_DEL_WATCH,
          .filesrc = "two.txt", .watchid = &watch2 },
        { .type = QFILE_MONITOR_TEST_OP_DEL_WATCH,
          .filesrc = NULL, .watchid = &watch0 },
    };
    Error *local_err = NULL;
    GError *gerr = NULL;
    QFileMonitor *mon = qemu_file_monitor_new(&local_err);
    QemuThread th;
    GTimer *timer;
    gchar *dir = NULL;
    int err = -1;
    gsize i;
    char *pathsrc = NULL;
    char *pathdst = NULL;
    QFileMonitorTestData data;
    GHashTable *ids = g_hash_table_new(g_int64_hash, g_int64_equal);

    qemu_mutex_init(&data.lock);
    data.records = NULL;

    /*
     * The file monitor needs the main loop running in
     * order to receive events from inotify. We must
     * thus spawn a background thread to run an event
     * loop impl, while this thread triggers the
     * actual file operations we're testing
     */
    evrunning = 1;
    evstopping = 0;
    qemu_thread_create(&th, "event-loop",
                       qemu_file_monitor_test_event_loop, NULL,
                       QEMU_THREAD_JOINABLE);

    if (local_err) {
        g_printerr("File monitoring not available: %s",
                   error_get_pretty(local_err));
        error_free(local_err);
        return;
    }

    dir = g_dir_make_tmp("test-util-filemonitor-XXXXXX",
                         &gerr);
    if (!dir) {
        g_printerr("Unable to create tmp dir %s",
                   gerr->message);
        g_error_free(gerr);
        abort();
    }

    /*
     * Run through the operation sequence validating events
     * as we go
     */
    for (i = 0; i < G_N_ELEMENTS(ops); i++) {
        const QFileMonitorTestOp *op = &(ops[i]);
        int fd;
        struct utimbuf ubuf;
        char *watchdir;
        const char *watchfile;

        pathsrc = g_strdup_printf("%s/%s", dir, op->filesrc);
        if (op->filedst) {
            pathdst = g_strdup_printf("%s/%s", dir, op->filedst);
        }

        switch (op->type) {
        case QFILE_MONITOR_TEST_OP_ADD_WATCH:
            if (debug) {
                g_printerr("Add watch %s %s\n",
                           dir, op->filesrc);
            }
            if (op->filesrc && strchr(op->filesrc, '/')) {
                watchdir = g_strdup_printf("%s/%s", dir, op->filesrc);
                watchfile = strrchr(watchdir, '/');
                *(char *)watchfile = '\0';
                watchfile++;
                if (*watchfile == '\0') {
                    watchfile = NULL;
                }
            } else {
                watchdir = g_strdup(dir);
                watchfile = op->filesrc;
            }
            *op->watchid =
                qemu_file_monitor_add_watch(mon,
                                            watchdir,
                                            watchfile,
                                            qemu_file_monitor_test_handler,
                                            &data,
                                            &local_err);
            g_free(watchdir);
            if (*op->watchid < 0) {
                g_printerr("Unable to add watch %s",
                           error_get_pretty(local_err));
                goto cleanup;
            }
            if (debug) {
                g_printerr("Watch ID %" PRIx64 "\n", *op->watchid);
            }
            if (g_hash_table_contains(ids, op->watchid)) {
                g_printerr("Watch ID %" PRIx64 "already exists", *op->watchid);
                goto cleanup;
            }
            g_hash_table_add(ids, op->watchid);
            break;
        case QFILE_MONITOR_TEST_OP_DEL_WATCH:
            if (debug) {
                g_printerr("Del watch %s %" PRIx64 "\n", dir, *op->watchid);
            }
            if (op->filesrc && strchr(op->filesrc, '/')) {
                watchdir = g_strdup_printf("%s/%s", dir, op->filesrc);
                watchfile = strrchr(watchdir, '/');
                *(char *)watchfile = '\0';
            } else {
                watchdir = g_strdup(dir);
            }
            g_hash_table_remove(ids, op->watchid);
            qemu_file_monitor_remove_watch(mon,
                                           watchdir,
                                           *op->watchid);
            g_free(watchdir);
            break;
        case QFILE_MONITOR_TEST_OP_EVENT:
            if (debug) {
                g_printerr("Event id=%" PRIx64 " event=%d file=%s\n",
                           *op->watchid, op->eventid, op->filesrc);
            }
            if (!qemu_file_monitor_test_expect(
                    &data, *op->watchid, op->eventid, op->filesrc))
                goto cleanup;
            break;
        case QFILE_MONITOR_TEST_OP_CREATE:
            if (debug) {
                g_printerr("Create %s\n", pathsrc);
            }
            fd = open(pathsrc, O_WRONLY | O_CREAT, 0700);
            if (fd < 0) {
                g_printerr("Unable to create %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }
            close(fd);
            break;

        case QFILE_MONITOR_TEST_OP_APPEND:
            if (debug) {
                g_printerr("Append %s\n", pathsrc);
            }
            fd = open(pathsrc, O_WRONLY | O_APPEND, 0700);
            if (fd < 0) {
                g_printerr("Unable to open %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }

            if (write(fd, "Hello World", 10) != 10) {
                g_printerr("Unable to write %s: %s",
                           pathsrc, strerror(errno));
                close(fd);
                goto cleanup;
            }
            close(fd);
            break;

        case QFILE_MONITOR_TEST_OP_TRUNC:
            if (debug) {
                g_printerr("Truncate %s\n", pathsrc);
            }
            if (truncate(pathsrc, 4) < 0) {
                g_printerr("Unable to truncate %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_RENAME:
            if (debug) {
                g_printerr("Rename %s -> %s\n", pathsrc, pathdst);
            }
            if (rename(pathsrc, pathdst) < 0) {
                g_printerr("Unable to rename %s to %s: %s",
                           pathsrc, pathdst, strerror(errno));
                goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_UNLINK:
            if (debug) {
                g_printerr("Unlink %s\n", pathsrc);
            }
            if (unlink(pathsrc) < 0) {
                g_printerr("Unable to unlink %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_TOUCH:
            if (debug) {
                g_printerr("Touch %s\n", pathsrc);
            }
            ubuf.actime = 1024;
            ubuf.modtime = 1025;
            if (utime(pathsrc, &ubuf) < 0) {
                g_printerr("Unable to touch %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_MKDIR:
            if (debug) {
                g_printerr("Mkdir %s\n", pathsrc);
            }
            if (mkdir(pathsrc, 0700) < 0) {
                g_printerr("Unable to mkdir %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_RMDIR:
            if (debug) {
                g_printerr("Rmdir %s\n", pathsrc);
            }
            if (rmdir(pathsrc) < 0) {
                g_printerr("Unable to rmdir %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }
            break;

        default:
            g_assert_not_reached();
        }

        g_free(pathsrc);
        g_free(pathdst);
        pathsrc = pathdst = NULL;
    }

    g_assert_cmpint(g_hash_table_size(ids), ==, 0);

    err = 0;

 cleanup:
    g_free(pathsrc);
    g_free(pathdst);

    qemu_mutex_lock(&evlock);
    evstopping = 1;
    timer = g_timer_new();
    while (evrunning && g_timer_elapsed(timer, NULL) < 5) {
        qemu_mutex_unlock(&evlock);
        usleep(10 * 1000);
        qemu_mutex_lock(&evlock);
    }
    qemu_mutex_unlock(&evlock);

    if (g_timer_elapsed(timer, NULL) >= 5) {
        g_printerr("Event loop failed to quit after 5 seconds\n");
    }
    g_timer_destroy(timer);

    qemu_file_monitor_free(mon);
    g_list_foreach(data.records,
                   (GFunc)qemu_file_monitor_test_record_free, NULL);
    g_list_free(data.records);
    qemu_mutex_destroy(&data.lock);
    if (dir) {
        for (i = 0; i < G_N_ELEMENTS(ops); i++) {
            const QFileMonitorTestOp *op = &(ops[i]);
            char *path = g_strdup_printf("%s/%s",
                                         dir, op->filesrc);
            if (op->type == QFILE_MONITOR_TEST_OP_MKDIR) {
                rmdir(path);
                g_free(path);
            } else {
                unlink(path);
                g_free(path);
                if (op->filedst) {
                    path = g_strdup_printf("%s/%s",
                                           dir, op->filedst);
                    unlink(path);
                    g_free(path);
                }
            }
        }
        if (rmdir(dir) < 0) {
            g_printerr("Failed to remove %s: %s\n",
                       dir, strerror(errno));
            abort();
        }
    }
    g_hash_table_unref(ids);
    g_free(dir);
    g_assert(err == 0);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qemu_init_main_loop(&error_abort);

    qemu_mutex_init(&evlock);

    debug = getenv("FILEMONITOR_DEBUG") != NULL;
    g_test_add_func("/util/filemonitor", test_file_monitor_events);

    return g_test_run();
}
