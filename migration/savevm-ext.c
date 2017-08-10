/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009-2015 Red Hat Inc
 *
 * Authors:
 *  Klim Kireev <klim.s.kireev@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qerror.h"
#include "block/block_int.h"
#include "io/channel-command.h"

#include "hw/boards.h"
#include "hw/hw.h"
#include "hw/qdev.h"
#include "hw/xen/xen.h"
#include "net/net.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"
#include "qemu/timer.h"
#include "audio/audio.h"
#include "migration/migration.h"
#include "migration/postcopy-ram.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "qemu/queue.h"
#include "sysemu/cpus.h"
#include "exec/memory.h"
#include "qmp-commands.h"
#include "trace.h"
#include "qemu/bitops.h"
#include "qemu/iov.h"
#include "block/snapshot.h"
#include "block/qapi.h"
#include "qemu/cutils.h"
#include "io/channel-buffer.h"
#include "io/channel-file.h"

const char *input_command = "gunzip -c";
const char *output_command = "gzip -c";

static int qemu_savevm_state(QEMUFile *f, Error **errp)
{
    int ret;
    MigrationParams params = {
        .blk = 0,
        .shared = 0
    };
    MigrationState *ms = migrate_init(&params);
    MigrationStatus status;
    ms->to_dst_file = f;

    if (migration_is_blocked(errp)) {
        ret = -EINVAL;
        goto done;
    }

    qemu_mutex_unlock_iothread();
    qemu_savevm_state_header(f);
    qemu_savevm_state_begin(f, &params);
    qemu_mutex_lock_iothread();

    while (qemu_file_get_error(f) == 0) {
        if (qemu_savevm_state_iterate(f, false) > 0) {
            break;
        }
    }

    ret = qemu_file_get_error(f);
    if (ret == 0) {
        qemu_savevm_state_complete_precopy(f, false);
        ret = qemu_file_get_error(f);
    }
    qemu_savevm_state_cleanup();
    if (ret != 0) {
        error_setg_errno(errp, -ret, "Error while writing VM state");
    }

done:
    if (ret != 0) {
        status = MIGRATION_STATUS_FAILED;
    } else {
        status = MIGRATION_STATUS_COMPLETED;
    }
    migrate_set_state(&ms->state, MIGRATION_STATUS_SETUP, status);

    /* f is outer parameter, it should not stay in global migration state after
     * this function finished */
    ms->to_dst_file = NULL;

    return ret;
}

static void qlist_push(QList *qlist, QObject *value)
{
    QListEntry *entry;

    entry = g_malloc(sizeof(*entry));
    entry->value = value;

    QTAILQ_INSERT_HEAD(&qlist->head, entry, next);
}

static bool isNumber(const char* name)
{
    while (*name) {
        if (!isdigit(*name)) {
                return false;
        }
        name++;
    }
    return true;
}

static BlockDriverState* find_active(void)
{
    BdrvNextIterator bi;
    BlockDriverState *bs = bdrv_first(&bi);
    while (bs && bs->read_only) {
        bs = bdrv_next(&bi);
    }
    return bs;
}

static BlockDriverState* find_base(void)
{
    BdrvNextIterator bi;
    BlockDriverState *bs = bdrv_first(&bi);
    while (bs && bs->backing) {
        bs = bs->backing->bs;
    }
    return bs;
}

static QString *get_dir_path (void) {
    BlockDriverState *bs = find_base();
    if (bs == NULL) {
        return NULL;
    }

    char* end = strrchr(bs->filename, '/');
    if (end == NULL) {
        return qstring_from_str("");
    }
    size_t size = end - bs->filename;
    return qstring_from_substr(bs->filename, 0, size - 1);
}

static QString *get_snap_path (const char *path) {
    char* end = strrchr(path, '/');
    if (end == NULL) {
        return NULL;
    }
    size_t size = end - path;
    return qstring_from_substr(path, 0, size - 1);
}

static const char* get_snap_name (const char *path) {
    char* end = strrchr(path, '/');
    return end + 1;
}

static QList *get_snap_chain (BlockDriverState *bs) {
    QList* lst = qlist_new();

    bs = bs->backing->bs;

    while (bs->backing != NULL) {
        char path[PATH_MAX];
        if (realpath(bs->filename, path) == NULL) {
            return NULL;
        }
        QString *snap = get_snap_path(path);
        qlist_push(lst, QOBJECT(snap));
        bs = bs->backing->bs;
    }

    return lst;
}

static const char *get_base_name(void) {
    BlockDriverState *bs = find_base();
    const char *ret = strrchr(bs->filename, '/');
    if (ret == NULL) {
        return bs->filename;
    }
    return ret;
}

static void generate_name(char *name) {
    BlockDriverState *bs = find_base();
    srand(time(NULL));
    long unsigned int rand = random();
    sprintf(name, "%s-%08lX-tmp", bs->filename, rand);
}

int delete_tmp_overlay(void) {
    BlockDriverState *bs = find_active();
    if (bs == NULL) {
        return -EINVAL;
    }
    return unlink(bs->filename);
}

int create_tmp_overlay(void) {
    BlockDriverState *bs = find_active();
    char tmp_name[PATH_MAX];
    const char *dev_name = bdrv_get_device_name(bs);

    generate_name(tmp_name);
    while (!access(tmp_name, F_OK))
        generate_name(tmp_name);

    Error *local_err = NULL;

    qmp_blockdev_snapshot_sync(true, dev_name, false, NULL,
                                     tmp_name,
                                     false,
                                     NULL,
                                     true, "qcow2",
                                     true, 1, &local_err);
    if (local_err != NULL) {
        error_report_err(local_err);
        return -EINVAL;
    }
    return 0;
}

static int create_new_snap_dir(const char *name) {
    int ret = -EINVAL;
    char snap_dir_path[PATH_MAX] = {};
    QString *dir_path = get_dir_path();

    sprintf(snap_dir_path, "%s/%s", qstring_get_str(dir_path),
                                 name);
    ret = mkdir(snap_dir_path, 0777);
    QDECREF(dir_path);
    return ret;
}

static int gen_snap_path(const char *name, char *path) {
    QString *dir_path = get_dir_path();
    if (dir_path == NULL) {
        return -EINVAL;
    }
    const char *base_name = get_base_name();
    if (base_name == NULL) {
        return -EINVAL;
    }
    sprintf(path, "%s/%s/%s-sn", dir_path->string, name, base_name);
    QDECREF(dir_path);
    return 0;
}

int save_vmstate_ext(Monitor *mon, const char *name)
{
    BlockDriverState *bs;
    int ret = -EINVAL;
    QEMUFile *f = NULL;
    QString *snap_dir = NULL;
    int saved_vm_running = 0;
    Error *local_err = NULL;
    char snapshot_file[PATH_MAX] = {};
    char command[PATH_MAX] = {};

    if(isNumber(name)){
	monitor_printf(mon, "Error: Please don't save snapshot with numeric name\n"); // Why?
        ret = -EINVAL;
        return ret;
    }

    bs = find_active();
    if (bs == NULL) {
        monitor_printf(mon, "There are not block devices on current VM\n");
        ret = -ENODEV;
        goto end;
    }


    ret = gen_snap_path(name, snapshot_file);
    if (ret < 0) {
        monitor_printf(mon, "Cannot save snapshot %s\n", name);
        goto end;
    }

    ret = create_new_snap_dir(name);
    if (ret < 0) {
        monitor_printf(mon, "Cannot create directory for snapshot %s\n", name);
        goto end;
    }

    ret = link(bs->filename, snapshot_file);
    if (ret < 0) {
        monitor_printf(mon, "Cannot save snapshot %s\n", name);
        goto end;
    }
    ret = unlink(bs->filename);
    if (ret < 0) {
        monitor_printf(mon, "Cannot save snapshot %s\n", name);
        goto end;
    }

    memset(bs->filename, 0, PATH_MAX);
    memset(bs->exact_filename, 0, PATH_MAX);
    strcpy(bs->filename, snapshot_file);
    strcpy(bs->exact_filename, snapshot_file);

    ret = create_tmp_overlay();
    if (ret < 0) {
        monitor_printf(mon, "Cannot create temporary overlay %s\n", name);
        goto end;
    }

    saved_vm_running = runstate_is_running();

    ret = global_state_store();
    if (ret) {
        monitor_printf(mon, "Error saving global state\n");
        goto end;
    }
    vm_stop(RUN_STATE_SAVE_VM);

    snap_dir = get_snap_path(snapshot_file);
    if (snap_dir == NULL) {
        monitor_printf(mon, "Cannot get snap path %s\n", name);
        goto end;
    }

    sprintf(command, "%s > %s/mem", output_command, snap_dir->string);
    const char *argv[] = { "/bin/sh", "-c", command, NULL };

    QIOChannel *ioc;
    ioc = QIO_CHANNEL(qio_channel_command_new_spawn(argv,
                                                    O_WRONLY,
                                                    &local_err));
    qio_channel_set_name(ioc, "migration-exec-outgoing");

    if (!ioc) {
        monitor_printf(mon, "Could not open VM state file's channel\n");
        goto end;
    }

    f = qemu_fopen_channel_output(ioc);
    if (!f) {
        monitor_printf(mon, "Could not open VM state file\n");
        goto end;
    }

    ret = qemu_savevm_state(f, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        goto end;
    }

    qemu_fclose(f);

end:
    if (saved_vm_running) {
        vm_start();
    }
    if (snap_dir != NULL) {
        QDECREF(snap_dir);
    }
    return ret;
}

static int goto_snap (const char* snap) {
    char image_path[PATH_MAX] = {};

    BlockDriverState *bs = find_active();
    if (bs == NULL) {
        return -EINVAL;
    }

    int ret = bs->drv->bdrv_make_empty(bs);
    QString *dir_path = get_dir_path();

    sprintf(image_path, "%s/%s/%s-sn", qstring_get_str(dir_path),
                                       snap,
                                       get_base_name());
    ret = bdrv_change_backing_file(bs, image_path, "qcow2");
    if (ret < 0) {
        goto end;
    }

    BlockDriverState *new_back = NULL;
    Error *local_err = NULL;
    new_back = bdrv_open(bs->backing_file, NULL, NULL, 0, &local_err);
    if (new_back == NULL) {
        ret = -EINVAL;
        goto end;
    }

    bdrv_set_backing_hd(bs, NULL, &local_err);
    if (local_err != NULL) {
        ret = -EINVAL;
        goto end;
    }
    bdrv_set_backing_hd(bs, new_back, &local_err);
    if (local_err != NULL) {
        ret = -EINVAL;
        goto end;
    }
    bdrv_unref(new_back);

end:
    QDECREF(dir_path);
    return ret;
}

static int load_state_ext(const char *name)
{
    QEMUFile *f;
    int ret = -EINVAL;
    char command[NAME_MAX] = {};
    QString *dir_path;
    Error *local_err = NULL;
    MigrationIncomingState *mis = migration_incoming_get_current();

    BlockDriverState *base = find_active();
    if (base == NULL) {
        error_report("There is no base image");
        return ret;
    }

    dir_path = get_dir_path();

    sprintf(command, "%s %s/%s/mem", input_command, dir_path->string, name);
    const char *argv[] = { "/bin/sh", "-c", command, NULL };

    QIOChannel *ioc;
    ioc = QIO_CHANNEL(qio_channel_command_new_spawn(argv,
                                                    O_RDONLY,
                                                    &local_err));
    qio_channel_set_name(ioc, "loadvm-exec-incoing");

    if (!ioc) {
        error_report("Could not open VM state file's channel\n");
        goto end;
    }

    f = qemu_fopen_channel_input(ioc);
    if (!f) {
        error_report("Cannot run a command %s\n", command);
        goto end;
    }

    mis->from_src_file = f;

    ret = qemu_loadvm_state(f);
    qemu_fclose(f);

    migration_incoming_state_destroy();
    if (ret < 0) {
        error_report("Error %d while loading vm state", ret);
        return ret;
    }
end:
    QDECREF(dir_path);
    return ret;
}

int incremental_load_vmstate_ext (const char *name, Monitor *mon) {
    int saved_vm_running  = runstate_is_running();
    int ret = -EINVAL;
    QList *snap_chain = NULL;
    QString *cur = NULL;
    const char *cur_name = NULL;

    if (saved_vm_running) {
        vm_stop(RUN_STATE_RESTORE_VM);
    }
    memory_global_dirty_log_start();

    QString *dir_path = get_dir_path();
    if (dir_path == NULL) {
        monitor_printf(mon, "There are not block devices on current VM\n");
        ret = -ENODEV;
        goto end;
    }

    ret = goto_snap(name);
    if (ret < 0) {
        monitor_printf(mon, "Cannot load snapshot %s\n", name);
        goto end;
    }

    BlockDriverState *bs = find_active();
    if (bs == NULL) {
        monitor_printf(mon, "There are not block devices on current VM\n");
        ret = -ENODEV;
        goto end;
    }

    snap_chain = get_snap_chain(bs);
    if (bs == NULL) {
        monitor_printf(mon, "Cannot build snapshot chain on current VM\n");
        ret = -EINVAL;
        goto end;
    }

    bdrv_drain_all();

    cur = qobject_to_qstring(qlist_pop(snap_chain));
    if (cur == NULL) {
        monitor_printf(mon, "The chain is empty\n");
        ret = -EINVAL;
        goto end;
    }

    while (cur) {
        cur_name = get_snap_name(cur->string);
        if (cur_name == NULL) {
            monitor_printf(mon, "Cannot load snapshot %s\n", name);
            ret = -EINVAL;
            goto end;
        }

        vm_start();
        vm_stop(RUN_STATE_RESTORE_VM);

        ret = load_state_ext(cur_name);
        if (ret < 0) {
            monitor_printf(mon, "Cannot load memory for snapshot %s\n", name);
            goto end;
        }
        QDECREF(cur);
        cur = qobject_to_qstring(qlist_pop(snap_chain));
    }

end:
    if (saved_vm_running) {
        vm_start();
    }
    if (snap_chain != NULL) {
        qlist_destroy_obj(QOBJECT(snap_chain));
    }
    QDECREF(dir_path);
    return ret;
}

