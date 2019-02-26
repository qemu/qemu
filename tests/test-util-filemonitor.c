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
    QFILE_MONITOR_TEST_OP_CREATE,
    QFILE_MONITOR_TEST_OP_APPEND,
    QFILE_MONITOR_TEST_OP_TRUNC,
    QFILE_MONITOR_TEST_OP_RENAME,
    QFILE_MONITOR_TEST_OP_TOUCH,
    QFILE_MONITOR_TEST_OP_UNLINK,
};

typedef struct {
    int type;
    const char *filesrc;
    const char *filedst;
} QFileMonitorTestOp;

typedef struct {
    const char *file;
} QFileMonitorTestWatch;

typedef struct {
    gsize nwatches;
    const QFileMonitorTestWatch *watches;

    gsize nops;
    const QFileMonitorTestOp *ops;
} QFileMonitorTestPlan;

typedef struct {
    int id;
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
qemu_file_monitor_test_handler(int id,
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
                              int id,
                              QFileMonitorEvent event,
                              const char *filename)
{
    QFileMonitorTestRecord *rec;
    bool ret = false;

    rec = qemu_file_monitor_test_next_record(data);

    if (!rec) {
        g_printerr("Missing event watch id %d event %d file %s\n",
                   id, event, filename);
        return false;
    }

    if (id != rec->id) {
        g_printerr("Expected watch id %d but got %d\n", id, rec->id);
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
test_file_monitor_events(const void *opaque)
{
    const QFileMonitorTestPlan *plan = opaque;
    Error *local_err = NULL;
    GError *gerr = NULL;
    QFileMonitor *mon = qemu_file_monitor_new(&local_err);
    QemuThread th;
    GTimer *timer;
    gchar *dir = NULL;
    int err = -1;
    gsize i, j;
    char *pathsrc = NULL;
    char *pathdst = NULL;
    QFileMonitorTestData data;

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
     * First register all the directory / file watches
     * we're interested in seeing events against
     */
    for (i = 0; i < plan->nwatches; i++) {
        int watchid;
        watchid = qemu_file_monitor_add_watch(mon,
                                              dir,
                                              plan->watches[i].file,
                                              qemu_file_monitor_test_handler,
                                              &data,
                                              &local_err);
        if (watchid < 0) {
            g_printerr("Unable to add watch %s",
                       error_get_pretty(local_err));
            goto cleanup;
        }
    }


    /*
     * Now invoke all the file operations (create,
     * delete, rename, chmod, etc). These operations
     * will trigger the various file monitor events
     */
    for (i = 0; i < plan->nops; i++) {
        const QFileMonitorTestOp *op = &(plan->ops[i]);
        int fd;
        struct utimbuf ubuf;

        pathsrc = g_strdup_printf("%s/%s", dir, op->filesrc);
        if (op->filedst) {
            pathdst = g_strdup_printf("%s/%s", dir, op->filedst);
        }

        switch (op->type) {
        case QFILE_MONITOR_TEST_OP_CREATE:
            fd = open(pathsrc, O_WRONLY | O_CREAT, 0700);
            if (fd < 0) {
                g_printerr("Unable to create %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }
            close(fd);
            break;

        case QFILE_MONITOR_TEST_OP_APPEND:
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
            if (truncate(pathsrc, 4) < 0) {
                g_printerr("Unable to truncate %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_RENAME:
            if (rename(pathsrc, pathdst) < 0) {
                g_printerr("Unable to rename %s to %s: %s",
                           pathsrc, pathdst, strerror(errno));
                goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_UNLINK:
            if (unlink(pathsrc) < 0) {
                g_printerr("Unable to unlink %s: %s",
                           pathsrc, strerror(errno));
                goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_TOUCH:
            ubuf.actime = 1024;
            ubuf.modtime = 1025;
            if (utime(pathsrc, &ubuf) < 0) {
                g_printerr("Unable to touch %s: %s",
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


    /*
     * Finally validate that we have received all the events
     * we expect to see for the combination of watches and
     * file operations
     */
    for (i = 0; i < plan->nops; i++) {
        const QFileMonitorTestOp *op = &(plan->ops[i]);

        switch (op->type) {
        case QFILE_MONITOR_TEST_OP_CREATE:
            for (j = 0; j < plan->nwatches; j++) {
                if (plan->watches[j].file &&
                    !g_str_equal(plan->watches[j].file, op->filesrc))
                    continue;

                if (!qemu_file_monitor_test_expect(
                        &data, j, QFILE_MONITOR_EVENT_CREATED, op->filesrc))
                    goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_APPEND:
        case QFILE_MONITOR_TEST_OP_TRUNC:
            for (j = 0; j < plan->nwatches; j++) {
                if (plan->watches[j].file &&
                    !g_str_equal(plan->watches[j].file, op->filesrc))
                    continue;

                if (!qemu_file_monitor_test_expect(
                        &data, j, QFILE_MONITOR_EVENT_MODIFIED, op->filesrc))
                    goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_RENAME:
            for (j = 0; j < plan->nwatches; j++) {
                if (plan->watches[j].file &&
                    !g_str_equal(plan->watches[j].file, op->filesrc))
                    continue;

                if (!qemu_file_monitor_test_expect(
                        &data, j, QFILE_MONITOR_EVENT_DELETED, op->filesrc))
                    goto cleanup;
            }

            for (j = 0; j < plan->nwatches; j++) {
                if (plan->watches[j].file &&
                    !g_str_equal(plan->watches[j].file, op->filedst))
                    continue;

                if (!qemu_file_monitor_test_expect(
                        &data, j, QFILE_MONITOR_EVENT_CREATED, op->filedst))
                    goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_TOUCH:
            for (j = 0; j < plan->nwatches; j++) {
                if (plan->watches[j].file &&
                    !g_str_equal(plan->watches[j].file, op->filesrc))
                    continue;

                if (!qemu_file_monitor_test_expect(
                        &data, j, QFILE_MONITOR_EVENT_ATTRIBUTES, op->filesrc))
                    goto cleanup;
            }
            break;

        case QFILE_MONITOR_TEST_OP_UNLINK:
            for (j = 0; j < plan->nwatches; j++) {
                if (plan->watches[j].file &&
                    !g_str_equal(plan->watches[j].file, op->filesrc))
                    continue;

                if (!qemu_file_monitor_test_expect(
                        &data, j, QFILE_MONITOR_EVENT_DELETED, op->filesrc))
                    goto cleanup;
            }
            break;

        default:
            g_assert_not_reached();
        }
    }

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

    for (i = 0; i < plan->nops; i++) {
        const QFileMonitorTestOp *op = &(plan->ops[i]);
        pathsrc = g_strdup_printf("%s/%s", dir, op->filesrc);
        unlink(pathsrc);
        g_free(pathsrc);
        if (op->filedst) {
            pathdst = g_strdup_printf("%s/%s", dir, op->filedst);
            unlink(pathdst);
            g_free(pathdst);
        }
    }

    qemu_file_monitor_free(mon);
    g_list_foreach(data.records,
                   (GFunc)qemu_file_monitor_test_record_free, NULL);
    g_list_free(data.records);
    qemu_mutex_destroy(&data.lock);
    if (dir) {
        rmdir(dir);
    }
    g_free(dir);
    g_assert(err == 0);
}


/*
 * Set of structs which define which file name patterns
 * we're trying to watch against. NULL, means all files
 * in the directory
 */
static const QFileMonitorTestWatch watches_any[] = {
    { NULL },
};

static const QFileMonitorTestWatch watches_one[] = {
    { "one.txt" },
};

static const QFileMonitorTestWatch watches_two[] = {
    { "two.txt" },
};

static const QFileMonitorTestWatch watches_many[] = {
    { NULL },
    { "one.txt" },
    { "two.txt" },
};


/*
 * Various sets of file operations we're going to
 * trigger and validate events for
 */
static const QFileMonitorTestOp ops_create_one[] = {
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "one.txt", }
};

static const QFileMonitorTestOp ops_delete_one[] = {
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "one.txt", },
    { .type = QFILE_MONITOR_TEST_OP_UNLINK,
      .filesrc = "one.txt", }
};

static const QFileMonitorTestOp ops_create_many[] = {
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "one.txt", },
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "two.txt", },
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "three.txt", }
};

static const QFileMonitorTestOp ops_rename_one[] = {
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "one.txt", },
    { .type = QFILE_MONITOR_TEST_OP_RENAME,
      .filesrc = "one.txt", .filedst = "two.txt" }
};

static const QFileMonitorTestOp ops_rename_many[] = {
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "one.txt", },
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "two.txt", },
    { .type = QFILE_MONITOR_TEST_OP_RENAME,
      .filesrc = "one.txt", .filedst = "two.txt" }
};

static const QFileMonitorTestOp ops_append_one[] = {
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "one.txt", },
    { .type = QFILE_MONITOR_TEST_OP_APPEND,
      .filesrc = "one.txt", },
};

static const QFileMonitorTestOp ops_trunc_one[] = {
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "one.txt", },
    { .type = QFILE_MONITOR_TEST_OP_TRUNC,
      .filesrc = "one.txt", },
};

static const QFileMonitorTestOp ops_touch_one[] = {
    { .type = QFILE_MONITOR_TEST_OP_CREATE,
      .filesrc = "one.txt", },
    { .type = QFILE_MONITOR_TEST_OP_TOUCH,
      .filesrc = "one.txt", },
};


/*
 * No we define data sets for the combinatorial
 * expansion of file watches and operation sets
 */
#define PLAN_DATA(o, w) \
    static const QFileMonitorTestPlan plan_ ## o ## _ ## w = { \
        .nops = G_N_ELEMENTS(ops_ ##o), \
        .ops = ops_ ##o, \
        .nwatches = G_N_ELEMENTS(watches_ ##w), \
        .watches = watches_ ## w, \
    }

PLAN_DATA(create_one, any);
PLAN_DATA(create_one, one);
PLAN_DATA(create_one, two);
PLAN_DATA(create_one, many);

PLAN_DATA(delete_one, any);
PLAN_DATA(delete_one, one);
PLAN_DATA(delete_one, two);
PLAN_DATA(delete_one, many);

PLAN_DATA(create_many, any);
PLAN_DATA(create_many, one);
PLAN_DATA(create_many, two);
PLAN_DATA(create_many, many);

PLAN_DATA(rename_one, any);
PLAN_DATA(rename_one, one);
PLAN_DATA(rename_one, two);
PLAN_DATA(rename_one, many);

PLAN_DATA(rename_many, any);
PLAN_DATA(rename_many, one);
PLAN_DATA(rename_many, two);
PLAN_DATA(rename_many, many);

PLAN_DATA(append_one, any);
PLAN_DATA(append_one, one);
PLAN_DATA(append_one, two);
PLAN_DATA(append_one, many);

PLAN_DATA(trunc_one, any);
PLAN_DATA(trunc_one, one);
PLAN_DATA(trunc_one, two);
PLAN_DATA(trunc_one, many);

PLAN_DATA(touch_one, any);
PLAN_DATA(touch_one, one);
PLAN_DATA(touch_one, two);
PLAN_DATA(touch_one, many);


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qemu_init_main_loop(&error_abort);

    qemu_mutex_init(&evlock);

    /*
     * Register test cases for the combinatorial
     * expansion of file watches and operation sets
     */
    #define PLAN_REGISTER(o, w)                                         \
        g_test_add_data_func("/util/filemonitor/" # o "/" # w,          \
                             &plan_ ## o ## _ ## w, test_file_monitor_events)

    PLAN_REGISTER(create_one, any);
    PLAN_REGISTER(create_one, one);
    PLAN_REGISTER(create_one, two);
    PLAN_REGISTER(create_one, many);

    PLAN_REGISTER(delete_one, any);
    PLAN_REGISTER(delete_one, one);
    PLAN_REGISTER(delete_one, two);
    PLAN_REGISTER(delete_one, many);

    PLAN_REGISTER(create_many, any);
    PLAN_REGISTER(create_many, one);
    PLAN_REGISTER(create_many, two);
    PLAN_REGISTER(create_many, many);

    PLAN_REGISTER(rename_one, any);
    PLAN_REGISTER(rename_one, one);
    PLAN_REGISTER(rename_one, two);
    PLAN_REGISTER(rename_one, many);

    PLAN_REGISTER(rename_many, any);
    PLAN_REGISTER(rename_many, one);
    PLAN_REGISTER(rename_many, two);
    PLAN_REGISTER(rename_many, many);

    PLAN_REGISTER(append_one, any);
    PLAN_REGISTER(append_one, one);
    PLAN_REGISTER(append_one, two);
    PLAN_REGISTER(append_one, many);

    PLAN_REGISTER(trunc_one, any);
    PLAN_REGISTER(trunc_one, one);
    PLAN_REGISTER(trunc_one, two);
    PLAN_REGISTER(trunc_one, many);

    PLAN_REGISTER(touch_one, any);
    PLAN_REGISTER(touch_one, one);
    PLAN_REGISTER(touch_one, two);
    PLAN_REGISTER(touch_one, many);

    return g_test_run();
}
