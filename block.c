/*
 * QEMU System Emulator block driver
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#include "config-host.h"
#include "qemu-common.h"
#include "trace.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "qemu/module.h"
#include "qapi/qmp/qjson.h"
#include "sysemu/sysemu.h"
#include "qemu/notify.h"
#include "block/coroutine.h"
#include "block/qapi.h"
#include "qmp-commands.h"
#include "qemu/timer.h"
#include "qapi-event.h"

#ifdef CONFIG_BSD
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#ifndef __DragonFly__
#include <sys/disk.h>
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#endif

struct BdrvDirtyBitmap {
    HBitmap *bitmap;
    QLIST_ENTRY(BdrvDirtyBitmap) list;
};

#define NOT_DONE 0x7fffffff /* used while emulated sync operation in progress */

#define COROUTINE_POOL_RESERVATION 64 /* number of coroutines to reserve */

static void bdrv_dev_change_media_cb(BlockDriverState *bs, bool load);
static BlockDriverAIOCB *bdrv_aio_readv_em(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);
static BlockDriverAIOCB *bdrv_aio_writev_em(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);
static int coroutine_fn bdrv_co_readv_em(BlockDriverState *bs,
                                         int64_t sector_num, int nb_sectors,
                                         QEMUIOVector *iov);
static int coroutine_fn bdrv_co_writev_em(BlockDriverState *bs,
                                         int64_t sector_num, int nb_sectors,
                                         QEMUIOVector *iov);
static int coroutine_fn bdrv_co_do_preadv(BlockDriverState *bs,
    int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags);
static int coroutine_fn bdrv_co_do_pwritev(BlockDriverState *bs,
    int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags);
static BlockDriverAIOCB *bdrv_co_aio_rw_vector(BlockDriverState *bs,
                                               int64_t sector_num,
                                               QEMUIOVector *qiov,
                                               int nb_sectors,
                                               BdrvRequestFlags flags,
                                               BlockDriverCompletionFunc *cb,
                                               void *opaque,
                                               bool is_write);
static void coroutine_fn bdrv_co_do_rw(void *opaque);
static int coroutine_fn bdrv_co_do_write_zeroes(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, BdrvRequestFlags flags);

static QTAILQ_HEAD(, BlockDriverState) bdrv_states =
    QTAILQ_HEAD_INITIALIZER(bdrv_states);

static QTAILQ_HEAD(, BlockDriverState) graph_bdrv_states =
    QTAILQ_HEAD_INITIALIZER(graph_bdrv_states);

static QLIST_HEAD(, BlockDriver) bdrv_drivers =
    QLIST_HEAD_INITIALIZER(bdrv_drivers);

/* If non-zero, use only whitelisted block drivers */
static int use_bdrv_whitelist;

#ifdef _WIN32
static int is_windows_drive_prefix(const char *filename)
{
    return (((filename[0] >= 'a' && filename[0] <= 'z') ||
             (filename[0] >= 'A' && filename[0] <= 'Z')) &&
            filename[1] == ':');
}

int is_windows_drive(const char *filename)
{
    if (is_windows_drive_prefix(filename) &&
        filename[2] == '\0')
        return 1;
    if (strstart(filename, "\\\\.\\", NULL) ||
        strstart(filename, "//./", NULL))
        return 1;
    return 0;
}
#endif

/* throttling disk I/O limits */
void bdrv_set_io_limits(BlockDriverState *bs,
                        ThrottleConfig *cfg)
{
    int i;

    throttle_config(&bs->throttle_state, cfg);

    for (i = 0; i < 2; i++) {
        qemu_co_enter_next(&bs->throttled_reqs[i]);
    }
}

/* this function drain all the throttled IOs */
static bool bdrv_start_throttled_reqs(BlockDriverState *bs)
{
    bool drained = false;
    bool enabled = bs->io_limits_enabled;
    int i;

    bs->io_limits_enabled = false;

    for (i = 0; i < 2; i++) {
        while (qemu_co_enter_next(&bs->throttled_reqs[i])) {
            drained = true;
        }
    }

    bs->io_limits_enabled = enabled;

    return drained;
}

void bdrv_io_limits_disable(BlockDriverState *bs)
{
    bs->io_limits_enabled = false;

    bdrv_start_throttled_reqs(bs);

    throttle_destroy(&bs->throttle_state);
}

static void bdrv_throttle_read_timer_cb(void *opaque)
{
    BlockDriverState *bs = opaque;
    qemu_co_enter_next(&bs->throttled_reqs[0]);
}

static void bdrv_throttle_write_timer_cb(void *opaque)
{
    BlockDriverState *bs = opaque;
    qemu_co_enter_next(&bs->throttled_reqs[1]);
}

/* should be called before bdrv_set_io_limits if a limit is set */
void bdrv_io_limits_enable(BlockDriverState *bs)
{
    assert(!bs->io_limits_enabled);
    throttle_init(&bs->throttle_state,
                  bdrv_get_aio_context(bs),
                  QEMU_CLOCK_VIRTUAL,
                  bdrv_throttle_read_timer_cb,
                  bdrv_throttle_write_timer_cb,
                  bs);
    bs->io_limits_enabled = true;
}

/* This function makes an IO wait if needed
 *
 * @nb_sectors: the number of sectors of the IO
 * @is_write:   is the IO a write
 */
static void bdrv_io_limits_intercept(BlockDriverState *bs,
                                     unsigned int bytes,
                                     bool is_write)
{
    /* does this io must wait */
    bool must_wait = throttle_schedule_timer(&bs->throttle_state, is_write);

    /* if must wait or any request of this type throttled queue the IO */
    if (must_wait ||
        !qemu_co_queue_empty(&bs->throttled_reqs[is_write])) {
        qemu_co_queue_wait(&bs->throttled_reqs[is_write]);
    }

    /* the IO will be executed, do the accounting */
    throttle_account(&bs->throttle_state, is_write, bytes);


    /* if the next request must wait -> do nothing */
    if (throttle_schedule_timer(&bs->throttle_state, is_write)) {
        return;
    }

    /* else queue next request for execution */
    qemu_co_queue_next(&bs->throttled_reqs[is_write]);
}

size_t bdrv_opt_mem_align(BlockDriverState *bs)
{
    if (!bs || !bs->drv) {
        /* 4k should be on the safe side */
        return 4096;
    }

    return bs->bl.opt_mem_alignment;
}

/* check if the path starts with "<protocol>:" */
static int path_has_protocol(const char *path)
{
    const char *p;

#ifdef _WIN32
    if (is_windows_drive(path) ||
        is_windows_drive_prefix(path)) {
        return 0;
    }
    p = path + strcspn(path, ":/\\");
#else
    p = path + strcspn(path, ":/");
#endif

    return *p == ':';
}

int path_is_absolute(const char *path)
{
#ifdef _WIN32
    /* specific case for names like: "\\.\d:" */
    if (is_windows_drive(path) || is_windows_drive_prefix(path)) {
        return 1;
    }
    return (*path == '/' || *path == '\\');
#else
    return (*path == '/');
#endif
}

/* if filename is absolute, just copy it to dest. Otherwise, build a
   path to it by considering it is relative to base_path. URL are
   supported. */
void path_combine(char *dest, int dest_size,
                  const char *base_path,
                  const char *filename)
{
    const char *p, *p1;
    int len;

    if (dest_size <= 0)
        return;
    if (path_is_absolute(filename)) {
        pstrcpy(dest, dest_size, filename);
    } else {
        p = strchr(base_path, ':');
        if (p)
            p++;
        else
            p = base_path;
        p1 = strrchr(base_path, '/');
#ifdef _WIN32
        {
            const char *p2;
            p2 = strrchr(base_path, '\\');
            if (!p1 || p2 > p1)
                p1 = p2;
        }
#endif
        if (p1)
            p1++;
        else
            p1 = base_path;
        if (p1 > p)
            p = p1;
        len = p - base_path;
        if (len > dest_size - 1)
            len = dest_size - 1;
        memcpy(dest, base_path, len);
        dest[len] = '\0';
        pstrcat(dest, dest_size, filename);
    }
}

void bdrv_get_full_backing_filename(BlockDriverState *bs, char *dest, size_t sz)
{
    if (bs->backing_file[0] == '\0' || path_has_protocol(bs->backing_file)) {
        pstrcpy(dest, sz, bs->backing_file);
    } else {
        path_combine(dest, sz, bs->filename, bs->backing_file);
    }
}

void bdrv_register(BlockDriver *bdrv)
{
    /* Block drivers without coroutine functions need emulation */
    if (!bdrv->bdrv_co_readv) {
        bdrv->bdrv_co_readv = bdrv_co_readv_em;
        bdrv->bdrv_co_writev = bdrv_co_writev_em;

        /* bdrv_co_readv_em()/brdv_co_writev_em() work in terms of aio, so if
         * the block driver lacks aio we need to emulate that too.
         */
        if (!bdrv->bdrv_aio_readv) {
            /* add AIO emulation layer */
            bdrv->bdrv_aio_readv = bdrv_aio_readv_em;
            bdrv->bdrv_aio_writev = bdrv_aio_writev_em;
        }
    }

    QLIST_INSERT_HEAD(&bdrv_drivers, bdrv, list);
}

/* create a new block device (by default it is empty) */
BlockDriverState *bdrv_new(const char *device_name, Error **errp)
{
    BlockDriverState *bs;
    int i;

    if (bdrv_find(device_name)) {
        error_setg(errp, "Device with id '%s' already exists",
                   device_name);
        return NULL;
    }
    if (bdrv_find_node(device_name)) {
        error_setg(errp, "Device with node-name '%s' already exists",
                   device_name);
        return NULL;
    }

    bs = g_malloc0(sizeof(BlockDriverState));
    QLIST_INIT(&bs->dirty_bitmaps);
    pstrcpy(bs->device_name, sizeof(bs->device_name), device_name);
    if (device_name[0] != '\0') {
        QTAILQ_INSERT_TAIL(&bdrv_states, bs, device_list);
    }
    for (i = 0; i < BLOCK_OP_TYPE_MAX; i++) {
        QLIST_INIT(&bs->op_blockers[i]);
    }
    bdrv_iostatus_disable(bs);
    notifier_list_init(&bs->close_notifiers);
    notifier_with_return_list_init(&bs->before_write_notifiers);
    qemu_co_queue_init(&bs->throttled_reqs[0]);
    qemu_co_queue_init(&bs->throttled_reqs[1]);
    bs->refcnt = 1;
    bs->aio_context = qemu_get_aio_context();

    return bs;
}

void bdrv_add_close_notifier(BlockDriverState *bs, Notifier *notify)
{
    notifier_list_add(&bs->close_notifiers, notify);
}

BlockDriver *bdrv_find_format(const char *format_name)
{
    BlockDriver *drv1;
    QLIST_FOREACH(drv1, &bdrv_drivers, list) {
        if (!strcmp(drv1->format_name, format_name)) {
            return drv1;
        }
    }
    return NULL;
}

static int bdrv_is_whitelisted(BlockDriver *drv, bool read_only)
{
    static const char *whitelist_rw[] = {
        CONFIG_BDRV_RW_WHITELIST
    };
    static const char *whitelist_ro[] = {
        CONFIG_BDRV_RO_WHITELIST
    };
    const char **p;

    if (!whitelist_rw[0] && !whitelist_ro[0]) {
        return 1;               /* no whitelist, anything goes */
    }

    for (p = whitelist_rw; *p; p++) {
        if (!strcmp(drv->format_name, *p)) {
            return 1;
        }
    }
    if (read_only) {
        for (p = whitelist_ro; *p; p++) {
            if (!strcmp(drv->format_name, *p)) {
                return 1;
            }
        }
    }
    return 0;
}

BlockDriver *bdrv_find_whitelisted_format(const char *format_name,
                                          bool read_only)
{
    BlockDriver *drv = bdrv_find_format(format_name);
    return drv && bdrv_is_whitelisted(drv, read_only) ? drv : NULL;
}

typedef struct CreateCo {
    BlockDriver *drv;
    char *filename;
    QemuOpts *opts;
    int ret;
    Error *err;
} CreateCo;

static void coroutine_fn bdrv_create_co_entry(void *opaque)
{
    Error *local_err = NULL;
    int ret;

    CreateCo *cco = opaque;
    assert(cco->drv);

    ret = cco->drv->bdrv_create(cco->filename, cco->opts, &local_err);
    if (local_err) {
        error_propagate(&cco->err, local_err);
    }
    cco->ret = ret;
}

int bdrv_create(BlockDriver *drv, const char* filename,
                QemuOpts *opts, Error **errp)
{
    int ret;

    Coroutine *co;
    CreateCo cco = {
        .drv = drv,
        .filename = g_strdup(filename),
        .opts = opts,
        .ret = NOT_DONE,
        .err = NULL,
    };

    if (!drv->bdrv_create) {
        error_setg(errp, "Driver '%s' does not support image creation", drv->format_name);
        ret = -ENOTSUP;
        goto out;
    }

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_create_co_entry(&cco);
    } else {
        co = qemu_coroutine_create(bdrv_create_co_entry);
        qemu_coroutine_enter(co, &cco);
        while (cco.ret == NOT_DONE) {
            aio_poll(qemu_get_aio_context(), true);
        }
    }

    ret = cco.ret;
    if (ret < 0) {
        if (cco.err) {
            error_propagate(errp, cco.err);
        } else {
            error_setg_errno(errp, -ret, "Could not create image");
        }
    }

out:
    g_free(cco.filename);
    return ret;
}

int bdrv_create_file(const char *filename, QemuOpts *opts, Error **errp)
{
    BlockDriver *drv;
    Error *local_err = NULL;
    int ret;

    drv = bdrv_find_protocol(filename, true);
    if (drv == NULL) {
        error_setg(errp, "Could not find protocol for file '%s'", filename);
        return -ENOENT;
    }

    ret = bdrv_create(drv, filename, opts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return ret;
}

void bdrv_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BlockDriver *drv = bs->drv;
    Error *local_err = NULL;

    memset(&bs->bl, 0, sizeof(bs->bl));

    if (!drv) {
        return;
    }

    /* Take some limits from the children as a default */
    if (bs->file) {
        bdrv_refresh_limits(bs->file, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        bs->bl.opt_transfer_length = bs->file->bl.opt_transfer_length;
        bs->bl.opt_mem_alignment = bs->file->bl.opt_mem_alignment;
    } else {
        bs->bl.opt_mem_alignment = 512;
    }

    if (bs->backing_hd) {
        bdrv_refresh_limits(bs->backing_hd, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        bs->bl.opt_transfer_length =
            MAX(bs->bl.opt_transfer_length,
                bs->backing_hd->bl.opt_transfer_length);
        bs->bl.opt_mem_alignment =
            MAX(bs->bl.opt_mem_alignment,
                bs->backing_hd->bl.opt_mem_alignment);
    }

    /* Then let the driver override it */
    if (drv->bdrv_refresh_limits) {
        drv->bdrv_refresh_limits(bs, errp);
    }
}

/*
 * Create a uniquely-named empty temporary file.
 * Return 0 upon success, otherwise a negative errno value.
 */
int get_tmp_filename(char *filename, int size)
{
#ifdef _WIN32
    char temp_dir[MAX_PATH];
    /* GetTempFileName requires that its output buffer (4th param)
       have length MAX_PATH or greater.  */
    assert(size >= MAX_PATH);
    return (GetTempPath(MAX_PATH, temp_dir)
            && GetTempFileName(temp_dir, "qem", 0, filename)
            ? 0 : -GetLastError());
#else
    int fd;
    const char *tmpdir;
    tmpdir = getenv("TMPDIR");
    if (!tmpdir) {
        tmpdir = "/var/tmp";
    }
    if (snprintf(filename, size, "%s/vl.XXXXXX", tmpdir) >= size) {
        return -EOVERFLOW;
    }
    fd = mkstemp(filename);
    if (fd < 0) {
        return -errno;
    }
    if (close(fd) != 0) {
        unlink(filename);
        return -errno;
    }
    return 0;
#endif
}

/*
 * Detect host devices. By convention, /dev/cdrom[N] is always
 * recognized as a host CDROM.
 */
static BlockDriver *find_hdev_driver(const char *filename)
{
    int score_max = 0, score;
    BlockDriver *drv = NULL, *d;

    QLIST_FOREACH(d, &bdrv_drivers, list) {
        if (d->bdrv_probe_device) {
            score = d->bdrv_probe_device(filename);
            if (score > score_max) {
                score_max = score;
                drv = d;
            }
        }
    }

    return drv;
}

BlockDriver *bdrv_find_protocol(const char *filename,
                                bool allow_protocol_prefix)
{
    BlockDriver *drv1;
    char protocol[128];
    int len;
    const char *p;

    /* TODO Drivers without bdrv_file_open must be specified explicitly */

    /*
     * XXX(hch): we really should not let host device detection
     * override an explicit protocol specification, but moving this
     * later breaks access to device names with colons in them.
     * Thanks to the brain-dead persistent naming schemes on udev-
     * based Linux systems those actually are quite common.
     */
    drv1 = find_hdev_driver(filename);
    if (drv1) {
        return drv1;
    }

    if (!path_has_protocol(filename) || !allow_protocol_prefix) {
        return bdrv_find_format("file");
    }

    p = strchr(filename, ':');
    assert(p != NULL);
    len = p - filename;
    if (len > sizeof(protocol) - 1)
        len = sizeof(protocol) - 1;
    memcpy(protocol, filename, len);
    protocol[len] = '\0';
    QLIST_FOREACH(drv1, &bdrv_drivers, list) {
        if (drv1->protocol_name &&
            !strcmp(drv1->protocol_name, protocol)) {
            return drv1;
        }
    }
    return NULL;
}

static int find_image_format(BlockDriverState *bs, const char *filename,
                             BlockDriver **pdrv, Error **errp)
{
    int score, score_max;
    BlockDriver *drv1, *drv;
    uint8_t buf[2048];
    int ret = 0;

    /* Return the raw BlockDriver * to scsi-generic devices or empty drives */
    if (bs->sg || !bdrv_is_inserted(bs) || bdrv_getlength(bs) == 0) {
        drv = bdrv_find_format("raw");
        if (!drv) {
            error_setg(errp, "Could not find raw image format");
            ret = -ENOENT;
        }
        *pdrv = drv;
        return ret;
    }

    ret = bdrv_pread(bs, 0, buf, sizeof(buf));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read image for determining its "
                         "format");
        *pdrv = NULL;
        return ret;
    }

    score_max = 0;
    drv = NULL;
    QLIST_FOREACH(drv1, &bdrv_drivers, list) {
        if (drv1->bdrv_probe) {
            score = drv1->bdrv_probe(buf, ret, filename);
            if (score > score_max) {
                score_max = score;
                drv = drv1;
            }
        }
    }
    if (!drv) {
        error_setg(errp, "Could not determine image format: No compatible "
                   "driver found");
        ret = -ENOENT;
    }
    *pdrv = drv;
    return ret;
}

/**
 * Set the current 'total_sectors' value
 * Return 0 on success, -errno on error.
 */
static int refresh_total_sectors(BlockDriverState *bs, int64_t hint)
{
    BlockDriver *drv = bs->drv;

    /* Do not attempt drv->bdrv_getlength() on scsi-generic devices */
    if (bs->sg)
        return 0;

    /* query actual device if possible, otherwise just trust the hint */
    if (drv->bdrv_getlength) {
        int64_t length = drv->bdrv_getlength(bs);
        if (length < 0) {
            return length;
        }
        hint = DIV_ROUND_UP(length, BDRV_SECTOR_SIZE);
    }

    bs->total_sectors = hint;
    return 0;
}

/**
 * Set open flags for a given discard mode
 *
 * Return 0 on success, -1 if the discard mode was invalid.
 */
int bdrv_parse_discard_flags(const char *mode, int *flags)
{
    *flags &= ~BDRV_O_UNMAP;

    if (!strcmp(mode, "off") || !strcmp(mode, "ignore")) {
        /* do nothing */
    } else if (!strcmp(mode, "on") || !strcmp(mode, "unmap")) {
        *flags |= BDRV_O_UNMAP;
    } else {
        return -1;
    }

    return 0;
}

/**
 * Set open flags for a given cache mode
 *
 * Return 0 on success, -1 if the cache mode was invalid.
 */
int bdrv_parse_cache_flags(const char *mode, int *flags)
{
    *flags &= ~BDRV_O_CACHE_MASK;

    if (!strcmp(mode, "off") || !strcmp(mode, "none")) {
        *flags |= BDRV_O_NOCACHE | BDRV_O_CACHE_WB;
    } else if (!strcmp(mode, "directsync")) {
        *flags |= BDRV_O_NOCACHE;
    } else if (!strcmp(mode, "writeback")) {
        *flags |= BDRV_O_CACHE_WB;
    } else if (!strcmp(mode, "unsafe")) {
        *flags |= BDRV_O_CACHE_WB;
        *flags |= BDRV_O_NO_FLUSH;
    } else if (!strcmp(mode, "writethrough")) {
        /* this is the default */
    } else {
        return -1;
    }

    return 0;
}

/**
 * The copy-on-read flag is actually a reference count so multiple users may
 * use the feature without worrying about clobbering its previous state.
 * Copy-on-read stays enabled until all users have called to disable it.
 */
void bdrv_enable_copy_on_read(BlockDriverState *bs)
{
    bs->copy_on_read++;
}

void bdrv_disable_copy_on_read(BlockDriverState *bs)
{
    assert(bs->copy_on_read > 0);
    bs->copy_on_read--;
}

/*
 * Returns the flags that a temporary snapshot should get, based on the
 * originally requested flags (the originally requested image will have flags
 * like a backing file)
 */
static int bdrv_temp_snapshot_flags(int flags)
{
    return (flags & ~BDRV_O_SNAPSHOT) | BDRV_O_TEMPORARY;
}

/*
 * Returns the flags that bs->file should get, based on the given flags for
 * the parent BDS
 */
static int bdrv_inherited_flags(int flags)
{
    /* Enable protocol handling, disable format probing for bs->file */
    flags |= BDRV_O_PROTOCOL;

    /* Our block drivers take care to send flushes and respect unmap policy,
     * so we can enable both unconditionally on lower layers. */
    flags |= BDRV_O_CACHE_WB | BDRV_O_UNMAP;

    /* Clear flags that only apply to the top layer */
    flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING | BDRV_O_COPY_ON_READ);

    return flags;
}

/*
 * Returns the flags that bs->backing_hd should get, based on the given flags
 * for the parent BDS
 */
static int bdrv_backing_flags(int flags)
{
    /* backing files always opened read-only */
    flags &= ~(BDRV_O_RDWR | BDRV_O_COPY_ON_READ);

    /* snapshot=on is handled on the top layer */
    flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_TEMPORARY);

    return flags;
}

static int bdrv_open_flags(BlockDriverState *bs, int flags)
{
    int open_flags = flags | BDRV_O_CACHE_WB;

    /*
     * Clear flags that are internal to the block layer before opening the
     * image.
     */
    open_flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING | BDRV_O_PROTOCOL);

    /*
     * Snapshots should be writable.
     */
    if (flags & BDRV_O_TEMPORARY) {
        open_flags |= BDRV_O_RDWR;
    }

    return open_flags;
}

static void bdrv_assign_node_name(BlockDriverState *bs,
                                  const char *node_name,
                                  Error **errp)
{
    if (!node_name) {
        return;
    }

    /* empty string node name is invalid */
    if (node_name[0] == '\0') {
        error_setg(errp, "Empty node name");
        return;
    }

    /* takes care of avoiding namespaces collisions */
    if (bdrv_find(node_name)) {
        error_setg(errp, "node-name=%s is conflicting with a device id",
                   node_name);
        return;
    }

    /* takes care of avoiding duplicates node names */
    if (bdrv_find_node(node_name)) {
        error_setg(errp, "Duplicate node name");
        return;
    }

    /* copy node name into the bs and insert it into the graph list */
    pstrcpy(bs->node_name, sizeof(bs->node_name), node_name);
    QTAILQ_INSERT_TAIL(&graph_bdrv_states, bs, node_list);
}

/*
 * Common part for opening disk images and files
 *
 * Removes all processed options from *options.
 */
static int bdrv_open_common(BlockDriverState *bs, BlockDriverState *file,
    QDict *options, int flags, BlockDriver *drv, Error **errp)
{
    int ret, open_flags;
    const char *filename;
    const char *node_name = NULL;
    Error *local_err = NULL;

    assert(drv != NULL);
    assert(bs->file == NULL);
    assert(options != NULL && bs->options != options);

    if (file != NULL) {
        filename = file->filename;
    } else {
        filename = qdict_get_try_str(options, "filename");
    }

    if (drv->bdrv_needs_filename && !filename) {
        error_setg(errp, "The '%s' block driver requires a file name",
                   drv->format_name);
        return -EINVAL;
    }

    trace_bdrv_open_common(bs, filename ?: "", flags, drv->format_name);

    node_name = qdict_get_try_str(options, "node-name");
    bdrv_assign_node_name(bs, node_name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -EINVAL;
    }
    qdict_del(options, "node-name");

    /* bdrv_open() with directly using a protocol as drv. This layer is already
     * opened, so assign it to bs (while file becomes a closed BlockDriverState)
     * and return immediately. */
    if (file != NULL && drv->bdrv_file_open) {
        bdrv_swap(file, bs);
        return 0;
    }

    bs->open_flags = flags;
    bs->guest_block_size = 512;
    bs->request_alignment = 512;
    bs->zero_beyond_eof = true;
    open_flags = bdrv_open_flags(bs, flags);
    bs->read_only = !(open_flags & BDRV_O_RDWR);
    bs->growable = !!(flags & BDRV_O_PROTOCOL);

    if (use_bdrv_whitelist && !bdrv_is_whitelisted(drv, bs->read_only)) {
        error_setg(errp,
                   !bs->read_only && bdrv_is_whitelisted(drv, true)
                        ? "Driver '%s' can only be used for read-only devices"
                        : "Driver '%s' is not whitelisted",
                   drv->format_name);
        return -ENOTSUP;
    }

    assert(bs->copy_on_read == 0); /* bdrv_new() and bdrv_close() make it so */
    if (flags & BDRV_O_COPY_ON_READ) {
        if (!bs->read_only) {
            bdrv_enable_copy_on_read(bs);
        } else {
            error_setg(errp, "Can't use copy-on-read on read-only device");
            return -EINVAL;
        }
    }

    if (filename != NULL) {
        pstrcpy(bs->filename, sizeof(bs->filename), filename);
    } else {
        bs->filename[0] = '\0';
    }

    bs->drv = drv;
    bs->opaque = g_malloc0(drv->instance_size);

    bs->enable_write_cache = !!(flags & BDRV_O_CACHE_WB);

    /* Open the image, either directly or using a protocol */
    if (drv->bdrv_file_open) {
        assert(file == NULL);
        assert(!drv->bdrv_needs_filename || filename != NULL);
        ret = drv->bdrv_file_open(bs, options, open_flags, &local_err);
    } else {
        if (file == NULL) {
            error_setg(errp, "Can't use '%s' as a block driver for the "
                       "protocol level", drv->format_name);
            ret = -EINVAL;
            goto free_and_fail;
        }
        bs->file = file;
        ret = drv->bdrv_open(bs, options, open_flags, &local_err);
    }

    if (ret < 0) {
        if (local_err) {
            error_propagate(errp, local_err);
        } else if (bs->filename[0]) {
            error_setg_errno(errp, -ret, "Could not open '%s'", bs->filename);
        } else {
            error_setg_errno(errp, -ret, "Could not open image");
        }
        goto free_and_fail;
    }

    ret = refresh_total_sectors(bs, bs->total_sectors);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not refresh total sector count");
        goto free_and_fail;
    }

    bdrv_refresh_limits(bs, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto free_and_fail;
    }

    assert(bdrv_opt_mem_align(bs) != 0);
    assert((bs->request_alignment != 0) || bs->sg);
    return 0;

free_and_fail:
    bs->file = NULL;
    g_free(bs->opaque);
    bs->opaque = NULL;
    bs->drv = NULL;
    return ret;
}

static QDict *parse_json_filename(const char *filename, Error **errp)
{
    QObject *options_obj;
    QDict *options;
    int ret;

    ret = strstart(filename, "json:", &filename);
    assert(ret);

    options_obj = qobject_from_json(filename);
    if (!options_obj) {
        error_setg(errp, "Could not parse the JSON options");
        return NULL;
    }

    if (qobject_type(options_obj) != QTYPE_QDICT) {
        qobject_decref(options_obj);
        error_setg(errp, "Invalid JSON object given");
        return NULL;
    }

    options = qobject_to_qdict(options_obj);
    qdict_flatten(options);

    return options;
}

/*
 * Fills in default options for opening images and converts the legacy
 * filename/flags pair to option QDict entries.
 */
static int bdrv_fill_options(QDict **options, const char **pfilename, int flags,
                             BlockDriver *drv, Error **errp)
{
    const char *filename = *pfilename;
    const char *drvname;
    bool protocol = flags & BDRV_O_PROTOCOL;
    bool parse_filename = false;
    Error *local_err = NULL;

    /* Parse json: pseudo-protocol */
    if (filename && g_str_has_prefix(filename, "json:")) {
        QDict *json_options = parse_json_filename(filename, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return -EINVAL;
        }

        /* Options given in the filename have lower priority than options
         * specified directly */
        qdict_join(*options, json_options, false);
        QDECREF(json_options);
        *pfilename = filename = NULL;
    }

    /* Fetch the file name from the options QDict if necessary */
    if (protocol && filename) {
        if (!qdict_haskey(*options, "filename")) {
            qdict_put(*options, "filename", qstring_from_str(filename));
            parse_filename = true;
        } else {
            error_setg(errp, "Can't specify 'file' and 'filename' options at "
                             "the same time");
            return -EINVAL;
        }
    }

    /* Find the right block driver */
    filename = qdict_get_try_str(*options, "filename");
    drvname = qdict_get_try_str(*options, "driver");

    if (drv) {
        if (drvname) {
            error_setg(errp, "Driver specified twice");
            return -EINVAL;
        }
        drvname = drv->format_name;
        qdict_put(*options, "driver", qstring_from_str(drvname));
    } else {
        if (!drvname && protocol) {
            if (filename) {
                drv = bdrv_find_protocol(filename, parse_filename);
                if (!drv) {
                    error_setg(errp, "Unknown protocol");
                    return -EINVAL;
                }

                drvname = drv->format_name;
                qdict_put(*options, "driver", qstring_from_str(drvname));
            } else {
                error_setg(errp, "Must specify either driver or file");
                return -EINVAL;
            }
        } else if (drvname) {
            drv = bdrv_find_format(drvname);
            if (!drv) {
                error_setg(errp, "Unknown driver '%s'", drvname);
                return -ENOENT;
            }
        }
    }

    assert(drv || !protocol);

    /* Driver-specific filename parsing */
    if (drv && drv->bdrv_parse_filename && parse_filename) {
        drv->bdrv_parse_filename(filename, *options, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return -EINVAL;
        }

        if (!drv->bdrv_needs_filename) {
            qdict_del(*options, "filename");
        }
    }

    return 0;
}

void bdrv_set_backing_hd(BlockDriverState *bs, BlockDriverState *backing_hd)
{

    if (bs->backing_hd) {
        assert(bs->backing_blocker);
        bdrv_op_unblock_all(bs->backing_hd, bs->backing_blocker);
    } else if (backing_hd) {
        error_setg(&bs->backing_blocker,
                   "device is used as backing hd of '%s'",
                   bs->device_name);
    }

    bs->backing_hd = backing_hd;
    if (!backing_hd) {
        error_free(bs->backing_blocker);
        bs->backing_blocker = NULL;
        goto out;
    }
    bs->open_flags &= ~BDRV_O_NO_BACKING;
    pstrcpy(bs->backing_file, sizeof(bs->backing_file), backing_hd->filename);
    pstrcpy(bs->backing_format, sizeof(bs->backing_format),
            backing_hd->drv ? backing_hd->drv->format_name : "");

    bdrv_op_block_all(bs->backing_hd, bs->backing_blocker);
    /* Otherwise we won't be able to commit due to check in bdrv_commit */
    bdrv_op_unblock(bs->backing_hd, BLOCK_OP_TYPE_COMMIT,
                    bs->backing_blocker);
out:
    bdrv_refresh_limits(bs, NULL);
}

/*
 * Opens the backing file for a BlockDriverState if not yet open
 *
 * options is a QDict of options to pass to the block drivers, or NULL for an
 * empty set of options. The reference to the QDict is transferred to this
 * function (even on failure), so if the caller intends to reuse the dictionary,
 * it needs to use QINCREF() before calling bdrv_file_open.
 */
int bdrv_open_backing_file(BlockDriverState *bs, QDict *options, Error **errp)
{
    char *backing_filename = g_malloc0(PATH_MAX);
    int ret = 0;
    BlockDriver *back_drv = NULL;
    BlockDriverState *backing_hd;
    Error *local_err = NULL;

    if (bs->backing_hd != NULL) {
        QDECREF(options);
        goto free_exit;
    }

    /* NULL means an empty set of options */
    if (options == NULL) {
        options = qdict_new();
    }

    bs->open_flags &= ~BDRV_O_NO_BACKING;
    if (qdict_haskey(options, "file.filename")) {
        backing_filename[0] = '\0';
    } else if (bs->backing_file[0] == '\0' && qdict_size(options) == 0) {
        QDECREF(options);
        goto free_exit;
    } else {
        bdrv_get_full_backing_filename(bs, backing_filename, PATH_MAX);
    }

    if (!bs->drv || !bs->drv->supports_backing) {
        ret = -EINVAL;
        error_setg(errp, "Driver doesn't support backing files");
        QDECREF(options);
        goto free_exit;
    }

    backing_hd = bdrv_new("", errp);

    if (bs->backing_format[0] != '\0') {
        back_drv = bdrv_find_format(bs->backing_format);
    }

    assert(bs->backing_hd == NULL);
    ret = bdrv_open(&backing_hd,
                    *backing_filename ? backing_filename : NULL, NULL, options,
                    bdrv_backing_flags(bs->open_flags), back_drv, &local_err);
    if (ret < 0) {
        bdrv_unref(backing_hd);
        backing_hd = NULL;
        bs->open_flags |= BDRV_O_NO_BACKING;
        error_setg(errp, "Could not open backing file: %s",
                   error_get_pretty(local_err));
        error_free(local_err);
        goto free_exit;
    }
    bdrv_set_backing_hd(bs, backing_hd);

free_exit:
    g_free(backing_filename);
    return ret;
}

/*
 * Opens a disk image whose options are given as BlockdevRef in another block
 * device's options.
 *
 * If allow_none is true, no image will be opened if filename is false and no
 * BlockdevRef is given. *pbs will remain unchanged and 0 will be returned.
 *
 * bdrev_key specifies the key for the image's BlockdevRef in the options QDict.
 * That QDict has to be flattened; therefore, if the BlockdevRef is a QDict
 * itself, all options starting with "${bdref_key}." are considered part of the
 * BlockdevRef.
 *
 * The BlockdevRef will be removed from the options QDict.
 *
 * To conform with the behavior of bdrv_open(), *pbs has to be NULL.
 */
int bdrv_open_image(BlockDriverState **pbs, const char *filename,
                    QDict *options, const char *bdref_key, int flags,
                    bool allow_none, Error **errp)
{
    QDict *image_options;
    int ret;
    char *bdref_key_dot;
    const char *reference;

    assert(pbs);
    assert(*pbs == NULL);

    bdref_key_dot = g_strdup_printf("%s.", bdref_key);
    qdict_extract_subqdict(options, &image_options, bdref_key_dot);
    g_free(bdref_key_dot);

    reference = qdict_get_try_str(options, bdref_key);
    if (!filename && !reference && !qdict_size(image_options)) {
        if (allow_none) {
            ret = 0;
        } else {
            error_setg(errp, "A block device must be specified for \"%s\"",
                       bdref_key);
            ret = -EINVAL;
        }
        QDECREF(image_options);
        goto done;
    }

    ret = bdrv_open(pbs, filename, reference, image_options, flags, NULL, errp);

done:
    qdict_del(options, bdref_key);
    return ret;
}

int bdrv_append_temp_snapshot(BlockDriverState *bs, int flags, Error **errp)
{
    /* TODO: extra byte is a hack to ensure MAX_PATH space on Windows. */
    char *tmp_filename = g_malloc0(PATH_MAX + 1);
    int64_t total_size;
    BlockDriver *bdrv_qcow2;
    QemuOpts *opts = NULL;
    QDict *snapshot_options;
    BlockDriverState *bs_snapshot;
    Error *local_err;
    int ret;

    /* if snapshot, we create a temporary backing file and open it
       instead of opening 'filename' directly */

    /* Get the required size from the image */
    total_size = bdrv_getlength(bs);
    if (total_size < 0) {
        ret = total_size;
        error_setg_errno(errp, -total_size, "Could not get image size");
        goto out;
    }

    /* Create the temporary image */
    ret = get_tmp_filename(tmp_filename, PATH_MAX + 1);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not get temporary filename");
        goto out;
    }

    bdrv_qcow2 = bdrv_find_format("qcow2");
    opts = qemu_opts_create(bdrv_qcow2->create_opts, NULL, 0,
                            &error_abort);
    qemu_opt_set_number(opts, BLOCK_OPT_SIZE, total_size);
    ret = bdrv_create(bdrv_qcow2, tmp_filename, opts, &local_err);
    qemu_opts_del(opts);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not create temporary overlay "
                         "'%s': %s", tmp_filename,
                         error_get_pretty(local_err));
        error_free(local_err);
        goto out;
    }

    /* Prepare a new options QDict for the temporary file */
    snapshot_options = qdict_new();
    qdict_put(snapshot_options, "file.driver",
              qstring_from_str("file"));
    qdict_put(snapshot_options, "file.filename",
              qstring_from_str(tmp_filename));

    bs_snapshot = bdrv_new("", &error_abort);

    ret = bdrv_open(&bs_snapshot, NULL, NULL, snapshot_options,
                    flags, bdrv_qcow2, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto out;
    }

    bdrv_append(bs_snapshot, bs);

out:
    g_free(tmp_filename);
    return ret;
}

/*
 * Opens a disk image (raw, qcow2, vmdk, ...)
 *
 * options is a QDict of options to pass to the block drivers, or NULL for an
 * empty set of options. The reference to the QDict belongs to the block layer
 * after the call (even on failure), so if the caller intends to reuse the
 * dictionary, it needs to use QINCREF() before calling bdrv_open.
 *
 * If *pbs is NULL, a new BDS will be created with a pointer to it stored there.
 * If it is not NULL, the referenced BDS will be reused.
 *
 * The reference parameter may be used to specify an existing block device which
 * should be opened. If specified, neither options nor a filename may be given,
 * nor can an existing BDS be reused (that is, *pbs has to be NULL).
 */
int bdrv_open(BlockDriverState **pbs, const char *filename,
              const char *reference, QDict *options, int flags,
              BlockDriver *drv, Error **errp)
{
    int ret;
    BlockDriverState *file = NULL, *bs;
    const char *drvname;
    Error *local_err = NULL;
    int snapshot_flags = 0;

    assert(pbs);

    if (reference) {
        bool options_non_empty = options ? qdict_size(options) : false;
        QDECREF(options);

        if (*pbs) {
            error_setg(errp, "Cannot reuse an existing BDS when referencing "
                       "another block device");
            return -EINVAL;
        }

        if (filename || options_non_empty) {
            error_setg(errp, "Cannot reference an existing block device with "
                       "additional options or a new filename");
            return -EINVAL;
        }

        bs = bdrv_lookup_bs(reference, reference, errp);
        if (!bs) {
            return -ENODEV;
        }
        bdrv_ref(bs);
        *pbs = bs;
        return 0;
    }

    if (*pbs) {
        bs = *pbs;
    } else {
        bs = bdrv_new("", &error_abort);
    }

    /* NULL means an empty set of options */
    if (options == NULL) {
        options = qdict_new();
    }

    ret = bdrv_fill_options(&options, &filename, flags, drv, &local_err);
    if (local_err) {
        goto fail;
    }

    /* Find the right image format driver */
    drv = NULL;
    drvname = qdict_get_try_str(options, "driver");
    if (drvname) {
        drv = bdrv_find_format(drvname);
        qdict_del(options, "driver");
        if (!drv) {
            error_setg(errp, "Unknown driver: '%s'", drvname);
            ret = -EINVAL;
            goto fail;
        }
    }

    assert(drvname || !(flags & BDRV_O_PROTOCOL));
    if (drv && !drv->bdrv_file_open) {
        /* If the user explicitly wants a format driver here, we'll need to add
         * another layer for the protocol in bs->file */
        flags &= ~BDRV_O_PROTOCOL;
    }

    bs->options = options;
    options = qdict_clone_shallow(options);

    /* Open image file without format layer */
    if ((flags & BDRV_O_PROTOCOL) == 0) {
        if (flags & BDRV_O_RDWR) {
            flags |= BDRV_O_ALLOW_RDWR;
        }
        if (flags & BDRV_O_SNAPSHOT) {
            snapshot_flags = bdrv_temp_snapshot_flags(flags);
            flags = bdrv_backing_flags(flags);
        }

        assert(file == NULL);
        ret = bdrv_open_image(&file, filename, options, "file",
                              bdrv_inherited_flags(flags),
                              true, &local_err);
        if (ret < 0) {
            goto fail;
        }
    }

    /* Image format probing */
    if (!drv && file) {
        ret = find_image_format(file, filename, &drv, &local_err);
        if (ret < 0) {
            goto fail;
        }
    } else if (!drv) {
        error_setg(errp, "Must specify either driver or file");
        ret = -EINVAL;
        goto fail;
    }

    /* Open the image */
    ret = bdrv_open_common(bs, file, options, flags, drv, &local_err);
    if (ret < 0) {
        goto fail;
    }

    if (file && (bs->file != file)) {
        bdrv_unref(file);
        file = NULL;
    }

    /* If there is a backing file, use it */
    if ((flags & BDRV_O_NO_BACKING) == 0) {
        QDict *backing_options;

        qdict_extract_subqdict(options, &backing_options, "backing.");
        ret = bdrv_open_backing_file(bs, backing_options, &local_err);
        if (ret < 0) {
            goto close_and_fail;
        }
    }

    /* For snapshot=on, create a temporary qcow2 overlay. bs points to the
     * temporary snapshot afterwards. */
    if (snapshot_flags) {
        ret = bdrv_append_temp_snapshot(bs, snapshot_flags, &local_err);
        if (local_err) {
            goto close_and_fail;
        }
    }

    /* Check if any unknown options were used */
    if (options && (qdict_size(options) != 0)) {
        const QDictEntry *entry = qdict_first(options);
        if (flags & BDRV_O_PROTOCOL) {
            error_setg(errp, "Block protocol '%s' doesn't support the option "
                       "'%s'", drv->format_name, entry->key);
        } else {
            error_setg(errp, "Block format '%s' used by device '%s' doesn't "
                       "support the option '%s'", drv->format_name,
                       bs->device_name, entry->key);
        }

        ret = -EINVAL;
        goto close_and_fail;
    }

    if (!bdrv_key_required(bs)) {
        bdrv_dev_change_media_cb(bs, true);
    } else if (!runstate_check(RUN_STATE_PRELAUNCH)
               && !runstate_check(RUN_STATE_INMIGRATE)
               && !runstate_check(RUN_STATE_PAUSED)) { /* HACK */
        error_setg(errp,
                   "Guest must be stopped for opening of encrypted image");
        ret = -EBUSY;
        goto close_and_fail;
    }

    QDECREF(options);
    *pbs = bs;
    return 0;

fail:
    if (file != NULL) {
        bdrv_unref(file);
    }
    QDECREF(bs->options);
    QDECREF(options);
    bs->options = NULL;
    if (!*pbs) {
        /* If *pbs is NULL, a new BDS has been created in this function and
           needs to be freed now. Otherwise, it does not need to be closed,
           since it has not really been opened yet. */
        bdrv_unref(bs);
    }
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return ret;

close_and_fail:
    /* See fail path, but now the BDS has to be always closed */
    if (*pbs) {
        bdrv_close(bs);
    } else {
        bdrv_unref(bs);
    }
    QDECREF(options);
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return ret;
}

typedef struct BlockReopenQueueEntry {
     bool prepared;
     BDRVReopenState state;
     QSIMPLEQ_ENTRY(BlockReopenQueueEntry) entry;
} BlockReopenQueueEntry;

/*
 * Adds a BlockDriverState to a simple queue for an atomic, transactional
 * reopen of multiple devices.
 *
 * bs_queue can either be an existing BlockReopenQueue that has had QSIMPLE_INIT
 * already performed, or alternatively may be NULL a new BlockReopenQueue will
 * be created and initialized. This newly created BlockReopenQueue should be
 * passed back in for subsequent calls that are intended to be of the same
 * atomic 'set'.
 *
 * bs is the BlockDriverState to add to the reopen queue.
 *
 * flags contains the open flags for the associated bs
 *
 * returns a pointer to bs_queue, which is either the newly allocated
 * bs_queue, or the existing bs_queue being used.
 *
 */
BlockReopenQueue *bdrv_reopen_queue(BlockReopenQueue *bs_queue,
                                    BlockDriverState *bs, int flags)
{
    assert(bs != NULL);

    BlockReopenQueueEntry *bs_entry;
    if (bs_queue == NULL) {
        bs_queue = g_new0(BlockReopenQueue, 1);
        QSIMPLEQ_INIT(bs_queue);
    }

    /* bdrv_open() masks this flag out */
    flags &= ~BDRV_O_PROTOCOL;

    if (bs->file) {
        bdrv_reopen_queue(bs_queue, bs->file, bdrv_inherited_flags(flags));
    }

    bs_entry = g_new0(BlockReopenQueueEntry, 1);
    QSIMPLEQ_INSERT_TAIL(bs_queue, bs_entry, entry);

    bs_entry->state.bs = bs;
    bs_entry->state.flags = flags;

    return bs_queue;
}

/*
 * Reopen multiple BlockDriverStates atomically & transactionally.
 *
 * The queue passed in (bs_queue) must have been built up previous
 * via bdrv_reopen_queue().
 *
 * Reopens all BDS specified in the queue, with the appropriate
 * flags.  All devices are prepared for reopen, and failure of any
 * device will cause all device changes to be abandonded, and intermediate
 * data cleaned up.
 *
 * If all devices prepare successfully, then the changes are committed
 * to all devices.
 *
 */
int bdrv_reopen_multiple(BlockReopenQueue *bs_queue, Error **errp)
{
    int ret = -1;
    BlockReopenQueueEntry *bs_entry, *next;
    Error *local_err = NULL;

    assert(bs_queue != NULL);

    bdrv_drain_all();

    QSIMPLEQ_FOREACH(bs_entry, bs_queue, entry) {
        if (bdrv_reopen_prepare(&bs_entry->state, bs_queue, &local_err)) {
            error_propagate(errp, local_err);
            goto cleanup;
        }
        bs_entry->prepared = true;
    }

    /* If we reach this point, we have success and just need to apply the
     * changes
     */
    QSIMPLEQ_FOREACH(bs_entry, bs_queue, entry) {
        bdrv_reopen_commit(&bs_entry->state);
    }

    ret = 0;

cleanup:
    QSIMPLEQ_FOREACH_SAFE(bs_entry, bs_queue, entry, next) {
        if (ret && bs_entry->prepared) {
            bdrv_reopen_abort(&bs_entry->state);
        }
        g_free(bs_entry);
    }
    g_free(bs_queue);
    return ret;
}


/* Reopen a single BlockDriverState with the specified flags. */
int bdrv_reopen(BlockDriverState *bs, int bdrv_flags, Error **errp)
{
    int ret = -1;
    Error *local_err = NULL;
    BlockReopenQueue *queue = bdrv_reopen_queue(NULL, bs, bdrv_flags);

    ret = bdrv_reopen_multiple(queue, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
    }
    return ret;
}


/*
 * Prepares a BlockDriverState for reopen. All changes are staged in the
 * 'opaque' field of the BDRVReopenState, which is used and allocated by
 * the block driver layer .bdrv_reopen_prepare()
 *
 * bs is the BlockDriverState to reopen
 * flags are the new open flags
 * queue is the reopen queue
 *
 * Returns 0 on success, non-zero on error.  On error errp will be set
 * as well.
 *
 * On failure, bdrv_reopen_abort() will be called to clean up any data.
 * It is the responsibility of the caller to then call the abort() or
 * commit() for any other BDS that have been left in a prepare() state
 *
 */
int bdrv_reopen_prepare(BDRVReopenState *reopen_state, BlockReopenQueue *queue,
                        Error **errp)
{
    int ret = -1;
    Error *local_err = NULL;
    BlockDriver *drv;

    assert(reopen_state != NULL);
    assert(reopen_state->bs->drv != NULL);
    drv = reopen_state->bs->drv;

    /* if we are to stay read-only, do not allow permission change
     * to r/w */
    if (!(reopen_state->bs->open_flags & BDRV_O_ALLOW_RDWR) &&
        reopen_state->flags & BDRV_O_RDWR) {
        error_set(errp, QERR_DEVICE_IS_READ_ONLY,
                  reopen_state->bs->device_name);
        goto error;
    }


    ret = bdrv_flush(reopen_state->bs);
    if (ret) {
        error_set(errp, ERROR_CLASS_GENERIC_ERROR, "Error (%s) flushing drive",
                  strerror(-ret));
        goto error;
    }

    if (drv->bdrv_reopen_prepare) {
        ret = drv->bdrv_reopen_prepare(reopen_state, queue, &local_err);
        if (ret) {
            if (local_err != NULL) {
                error_propagate(errp, local_err);
            } else {
                error_setg(errp, "failed while preparing to reopen image '%s'",
                           reopen_state->bs->filename);
            }
            goto error;
        }
    } else {
        /* It is currently mandatory to have a bdrv_reopen_prepare()
         * handler for each supported drv. */
        error_set(errp, QERR_BLOCK_FORMAT_FEATURE_NOT_SUPPORTED,
                  drv->format_name, reopen_state->bs->device_name,
                 "reopening of file");
        ret = -1;
        goto error;
    }

    ret = 0;

error:
    return ret;
}

/*
 * Takes the staged changes for the reopen from bdrv_reopen_prepare(), and
 * makes them final by swapping the staging BlockDriverState contents into
 * the active BlockDriverState contents.
 */
void bdrv_reopen_commit(BDRVReopenState *reopen_state)
{
    BlockDriver *drv;

    assert(reopen_state != NULL);
    drv = reopen_state->bs->drv;
    assert(drv != NULL);

    /* If there are any driver level actions to take */
    if (drv->bdrv_reopen_commit) {
        drv->bdrv_reopen_commit(reopen_state);
    }

    /* set BDS specific flags now */
    reopen_state->bs->open_flags         = reopen_state->flags;
    reopen_state->bs->enable_write_cache = !!(reopen_state->flags &
                                              BDRV_O_CACHE_WB);
    reopen_state->bs->read_only = !(reopen_state->flags & BDRV_O_RDWR);

    bdrv_refresh_limits(reopen_state->bs, NULL);
}

/*
 * Abort the reopen, and delete and free the staged changes in
 * reopen_state
 */
void bdrv_reopen_abort(BDRVReopenState *reopen_state)
{
    BlockDriver *drv;

    assert(reopen_state != NULL);
    drv = reopen_state->bs->drv;
    assert(drv != NULL);

    if (drv->bdrv_reopen_abort) {
        drv->bdrv_reopen_abort(reopen_state);
    }
}


void bdrv_close(BlockDriverState *bs)
{
    if (bs->job) {
        block_job_cancel_sync(bs->job);
    }
    bdrv_drain_all(); /* complete I/O */
    bdrv_flush(bs);
    bdrv_drain_all(); /* in case flush left pending I/O */
    notifier_list_notify(&bs->close_notifiers, bs);

    if (bs->drv) {
        if (bs->backing_hd) {
            BlockDriverState *backing_hd = bs->backing_hd;
            bdrv_set_backing_hd(bs, NULL);
            bdrv_unref(backing_hd);
        }
        bs->drv->bdrv_close(bs);
        g_free(bs->opaque);
        bs->opaque = NULL;
        bs->drv = NULL;
        bs->copy_on_read = 0;
        bs->backing_file[0] = '\0';
        bs->backing_format[0] = '\0';
        bs->total_sectors = 0;
        bs->encrypted = 0;
        bs->valid_key = 0;
        bs->sg = 0;
        bs->growable = 0;
        bs->zero_beyond_eof = false;
        QDECREF(bs->options);
        bs->options = NULL;

        if (bs->file != NULL) {
            bdrv_unref(bs->file);
            bs->file = NULL;
        }
    }

    bdrv_dev_change_media_cb(bs, false);

    /*throttling disk I/O limits*/
    if (bs->io_limits_enabled) {
        bdrv_io_limits_disable(bs);
    }
}

void bdrv_close_all(void)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, device_list) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        aio_context_acquire(aio_context);
        bdrv_close(bs);
        aio_context_release(aio_context);
    }
}

/* Check if any requests are in-flight (including throttled requests) */
static bool bdrv_requests_pending(BlockDriverState *bs)
{
    if (!QLIST_EMPTY(&bs->tracked_requests)) {
        return true;
    }
    if (!qemu_co_queue_empty(&bs->throttled_reqs[0])) {
        return true;
    }
    if (!qemu_co_queue_empty(&bs->throttled_reqs[1])) {
        return true;
    }
    if (bs->file && bdrv_requests_pending(bs->file)) {
        return true;
    }
    if (bs->backing_hd && bdrv_requests_pending(bs->backing_hd)) {
        return true;
    }
    return false;
}

/*
 * Wait for pending requests to complete across all BlockDriverStates
 *
 * This function does not flush data to disk, use bdrv_flush_all() for that
 * after calling this function.
 *
 * Note that completion of an asynchronous I/O operation can trigger any
 * number of other I/O operations on other devices---for example a coroutine
 * can be arbitrarily complex and a constant flow of I/O can come until the
 * coroutine is complete.  Because of this, it is not possible to have a
 * function to drain a single device's I/O queue.
 */
void bdrv_drain_all(void)
{
    /* Always run first iteration so any pending completion BHs run */
    bool busy = true;
    BlockDriverState *bs;

    while (busy) {
        busy = false;

        QTAILQ_FOREACH(bs, &bdrv_states, device_list) {
            AioContext *aio_context = bdrv_get_aio_context(bs);
            bool bs_busy;

            aio_context_acquire(aio_context);
            bdrv_flush_io_queue(bs);
            bdrv_start_throttled_reqs(bs);
            bs_busy = bdrv_requests_pending(bs);
            bs_busy |= aio_poll(aio_context, bs_busy);
            aio_context_release(aio_context);

            busy |= bs_busy;
        }
    }
}

/* make a BlockDriverState anonymous by removing from bdrv_state and
 * graph_bdrv_state list.
   Also, NULL terminate the device_name to prevent double remove */
void bdrv_make_anon(BlockDriverState *bs)
{
    if (bs->device_name[0] != '\0') {
        QTAILQ_REMOVE(&bdrv_states, bs, device_list);
    }
    bs->device_name[0] = '\0';
    if (bs->node_name[0] != '\0') {
        QTAILQ_REMOVE(&graph_bdrv_states, bs, node_list);
    }
    bs->node_name[0] = '\0';
}

static void bdrv_rebind(BlockDriverState *bs)
{
    if (bs->drv && bs->drv->bdrv_rebind) {
        bs->drv->bdrv_rebind(bs);
    }
}

static void bdrv_move_feature_fields(BlockDriverState *bs_dest,
                                     BlockDriverState *bs_src)
{
    /* move some fields that need to stay attached to the device */

    /* dev info */
    bs_dest->dev_ops            = bs_src->dev_ops;
    bs_dest->dev_opaque         = bs_src->dev_opaque;
    bs_dest->dev                = bs_src->dev;
    bs_dest->guest_block_size   = bs_src->guest_block_size;
    bs_dest->copy_on_read       = bs_src->copy_on_read;

    bs_dest->enable_write_cache = bs_src->enable_write_cache;

    /* i/o throttled req */
    memcpy(&bs_dest->throttle_state,
           &bs_src->throttle_state,
           sizeof(ThrottleState));
    bs_dest->throttled_reqs[0]  = bs_src->throttled_reqs[0];
    bs_dest->throttled_reqs[1]  = bs_src->throttled_reqs[1];
    bs_dest->io_limits_enabled  = bs_src->io_limits_enabled;

    /* r/w error */
    bs_dest->on_read_error      = bs_src->on_read_error;
    bs_dest->on_write_error     = bs_src->on_write_error;

    /* i/o status */
    bs_dest->iostatus_enabled   = bs_src->iostatus_enabled;
    bs_dest->iostatus           = bs_src->iostatus;

    /* dirty bitmap */
    bs_dest->dirty_bitmaps      = bs_src->dirty_bitmaps;

    /* reference count */
    bs_dest->refcnt             = bs_src->refcnt;

    /* job */
    bs_dest->job                = bs_src->job;

    /* keep the same entry in bdrv_states */
    pstrcpy(bs_dest->device_name, sizeof(bs_dest->device_name),
            bs_src->device_name);
    bs_dest->device_list = bs_src->device_list;
    memcpy(bs_dest->op_blockers, bs_src->op_blockers,
           sizeof(bs_dest->op_blockers));
}

/*
 * Swap bs contents for two image chains while they are live,
 * while keeping required fields on the BlockDriverState that is
 * actually attached to a device.
 *
 * This will modify the BlockDriverState fields, and swap contents
 * between bs_new and bs_old. Both bs_new and bs_old are modified.
 *
 * bs_new is required to be anonymous.
 *
 * This function does not create any image files.
 */
void bdrv_swap(BlockDriverState *bs_new, BlockDriverState *bs_old)
{
    BlockDriverState tmp;

    /* The code needs to swap the node_name but simply swapping node_list won't
     * work so first remove the nodes from the graph list, do the swap then
     * insert them back if needed.
     */
    if (bs_new->node_name[0] != '\0') {
        QTAILQ_REMOVE(&graph_bdrv_states, bs_new, node_list);
    }
    if (bs_old->node_name[0] != '\0') {
        QTAILQ_REMOVE(&graph_bdrv_states, bs_old, node_list);
    }

    /* bs_new must be anonymous and shouldn't have anything fancy enabled */
    assert(bs_new->device_name[0] == '\0');
    assert(QLIST_EMPTY(&bs_new->dirty_bitmaps));
    assert(bs_new->job == NULL);
    assert(bs_new->dev == NULL);
    assert(bs_new->io_limits_enabled == false);
    assert(!throttle_have_timer(&bs_new->throttle_state));

    tmp = *bs_new;
    *bs_new = *bs_old;
    *bs_old = tmp;

    /* there are some fields that should not be swapped, move them back */
    bdrv_move_feature_fields(&tmp, bs_old);
    bdrv_move_feature_fields(bs_old, bs_new);
    bdrv_move_feature_fields(bs_new, &tmp);

    /* bs_new shouldn't be in bdrv_states even after the swap!  */
    assert(bs_new->device_name[0] == '\0');

    /* Check a few fields that should remain attached to the device */
    assert(bs_new->dev == NULL);
    assert(bs_new->job == NULL);
    assert(bs_new->io_limits_enabled == false);
    assert(!throttle_have_timer(&bs_new->throttle_state));

    /* insert the nodes back into the graph node list if needed */
    if (bs_new->node_name[0] != '\0') {
        QTAILQ_INSERT_TAIL(&graph_bdrv_states, bs_new, node_list);
    }
    if (bs_old->node_name[0] != '\0') {
        QTAILQ_INSERT_TAIL(&graph_bdrv_states, bs_old, node_list);
    }

    bdrv_rebind(bs_new);
    bdrv_rebind(bs_old);
}

/*
 * Add new bs contents at the top of an image chain while the chain is
 * live, while keeping required fields on the top layer.
 *
 * This will modify the BlockDriverState fields, and swap contents
 * between bs_new and bs_top. Both bs_new and bs_top are modified.
 *
 * bs_new is required to be anonymous.
 *
 * This function does not create any image files.
 */
void bdrv_append(BlockDriverState *bs_new, BlockDriverState *bs_top)
{
    bdrv_swap(bs_new, bs_top);

    /* The contents of 'tmp' will become bs_top, as we are
     * swapping bs_new and bs_top contents. */
    bdrv_set_backing_hd(bs_top, bs_new);
}

static void bdrv_delete(BlockDriverState *bs)
{
    assert(!bs->dev);
    assert(!bs->job);
    assert(bdrv_op_blocker_is_empty(bs));
    assert(!bs->refcnt);
    assert(QLIST_EMPTY(&bs->dirty_bitmaps));

    bdrv_close(bs);

    /* remove from list, if necessary */
    bdrv_make_anon(bs);

    g_free(bs);
}

int bdrv_attach_dev(BlockDriverState *bs, void *dev)
/* TODO change to DeviceState *dev when all users are qdevified */
{
    if (bs->dev) {
        return -EBUSY;
    }
    bs->dev = dev;
    bdrv_iostatus_reset(bs);

    /* We're expecting I/O from the device so bump up coroutine pool size */
    qemu_coroutine_adjust_pool_size(COROUTINE_POOL_RESERVATION);
    return 0;
}

/* TODO qdevified devices don't use this, remove when devices are qdevified */
void bdrv_attach_dev_nofail(BlockDriverState *bs, void *dev)
{
    if (bdrv_attach_dev(bs, dev) < 0) {
        abort();
    }
}

void bdrv_detach_dev(BlockDriverState *bs, void *dev)
/* TODO change to DeviceState *dev when all users are qdevified */
{
    assert(bs->dev == dev);
    bs->dev = NULL;
    bs->dev_ops = NULL;
    bs->dev_opaque = NULL;
    bs->guest_block_size = 512;
    qemu_coroutine_adjust_pool_size(-COROUTINE_POOL_RESERVATION);
}

/* TODO change to return DeviceState * when all users are qdevified */
void *bdrv_get_attached_dev(BlockDriverState *bs)
{
    return bs->dev;
}

void bdrv_set_dev_ops(BlockDriverState *bs, const BlockDevOps *ops,
                      void *opaque)
{
    bs->dev_ops = ops;
    bs->dev_opaque = opaque;
}

static void bdrv_dev_change_media_cb(BlockDriverState *bs, bool load)
{
    if (bs->dev_ops && bs->dev_ops->change_media_cb) {
        bool tray_was_closed = !bdrv_dev_is_tray_open(bs);
        bs->dev_ops->change_media_cb(bs->dev_opaque, load);
        if (tray_was_closed) {
            /* tray open */
            qapi_event_send_device_tray_moved(bdrv_get_device_name(bs),
                                              true, &error_abort);
        }
        if (load) {
            /* tray close */
            qapi_event_send_device_tray_moved(bdrv_get_device_name(bs),
                                              false, &error_abort);
        }
    }
}

bool bdrv_dev_has_removable_media(BlockDriverState *bs)
{
    return !bs->dev || (bs->dev_ops && bs->dev_ops->change_media_cb);
}

void bdrv_dev_eject_request(BlockDriverState *bs, bool force)
{
    if (bs->dev_ops && bs->dev_ops->eject_request_cb) {
        bs->dev_ops->eject_request_cb(bs->dev_opaque, force);
    }
}

bool bdrv_dev_is_tray_open(BlockDriverState *bs)
{
    if (bs->dev_ops && bs->dev_ops->is_tray_open) {
        return bs->dev_ops->is_tray_open(bs->dev_opaque);
    }
    return false;
}

static void bdrv_dev_resize_cb(BlockDriverState *bs)
{
    if (bs->dev_ops && bs->dev_ops->resize_cb) {
        bs->dev_ops->resize_cb(bs->dev_opaque);
    }
}

bool bdrv_dev_is_medium_locked(BlockDriverState *bs)
{
    if (bs->dev_ops && bs->dev_ops->is_medium_locked) {
        return bs->dev_ops->is_medium_locked(bs->dev_opaque);
    }
    return false;
}

/*
 * Run consistency checks on an image
 *
 * Returns 0 if the check could be completed (it doesn't mean that the image is
 * free of errors) or -errno when an internal error occurred. The results of the
 * check are stored in res.
 */
int bdrv_check(BlockDriverState *bs, BdrvCheckResult *res, BdrvCheckMode fix)
{
    if (bs->drv->bdrv_check == NULL) {
        return -ENOTSUP;
    }

    memset(res, 0, sizeof(*res));
    return bs->drv->bdrv_check(bs, res, fix);
}

#define COMMIT_BUF_SECTORS 2048

/* commit COW file into the raw image */
int bdrv_commit(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    int64_t sector, total_sectors, length, backing_length;
    int n, ro, open_flags;
    int ret = 0;
    uint8_t *buf = NULL;
    char filename[PATH_MAX];

    if (!drv)
        return -ENOMEDIUM;
    
    if (!bs->backing_hd) {
        return -ENOTSUP;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_COMMIT, NULL) ||
        bdrv_op_is_blocked(bs->backing_hd, BLOCK_OP_TYPE_COMMIT, NULL)) {
        return -EBUSY;
    }

    ro = bs->backing_hd->read_only;
    /* Use pstrcpy (not strncpy): filename must be NUL-terminated. */
    pstrcpy(filename, sizeof(filename), bs->backing_hd->filename);
    open_flags =  bs->backing_hd->open_flags;

    if (ro) {
        if (bdrv_reopen(bs->backing_hd, open_flags | BDRV_O_RDWR, NULL)) {
            return -EACCES;
        }
    }

    length = bdrv_getlength(bs);
    if (length < 0) {
        ret = length;
        goto ro_cleanup;
    }

    backing_length = bdrv_getlength(bs->backing_hd);
    if (backing_length < 0) {
        ret = backing_length;
        goto ro_cleanup;
    }

    /* If our top snapshot is larger than the backing file image,
     * grow the backing file image if possible.  If not possible,
     * we must return an error */
    if (length > backing_length) {
        ret = bdrv_truncate(bs->backing_hd, length);
        if (ret < 0) {
            goto ro_cleanup;
        }
    }

    total_sectors = length >> BDRV_SECTOR_BITS;
    buf = g_malloc(COMMIT_BUF_SECTORS * BDRV_SECTOR_SIZE);

    for (sector = 0; sector < total_sectors; sector += n) {
        ret = bdrv_is_allocated(bs, sector, COMMIT_BUF_SECTORS, &n);
        if (ret < 0) {
            goto ro_cleanup;
        }
        if (ret) {
            ret = bdrv_read(bs, sector, buf, n);
            if (ret < 0) {
                goto ro_cleanup;
            }

            ret = bdrv_write(bs->backing_hd, sector, buf, n);
            if (ret < 0) {
                goto ro_cleanup;
            }
        }
    }

    if (drv->bdrv_make_empty) {
        ret = drv->bdrv_make_empty(bs);
        if (ret < 0) {
            goto ro_cleanup;
        }
        bdrv_flush(bs);
    }

    /*
     * Make sure all data we wrote to the backing device is actually
     * stable on disk.
     */
    if (bs->backing_hd) {
        bdrv_flush(bs->backing_hd);
    }

    ret = 0;
ro_cleanup:
    g_free(buf);

    if (ro) {
        /* ignoring error return here */
        bdrv_reopen(bs->backing_hd, open_flags & ~BDRV_O_RDWR, NULL);
    }

    return ret;
}

int bdrv_commit_all(void)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, device_list) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        aio_context_acquire(aio_context);
        if (bs->drv && bs->backing_hd) {
            int ret = bdrv_commit(bs);
            if (ret < 0) {
                aio_context_release(aio_context);
                return ret;
            }
        }
        aio_context_release(aio_context);
    }
    return 0;
}

/**
 * Remove an active request from the tracked requests list
 *
 * This function should be called when a tracked request is completing.
 */
static void tracked_request_end(BdrvTrackedRequest *req)
{
    if (req->serialising) {
        req->bs->serialising_in_flight--;
    }

    QLIST_REMOVE(req, list);
    qemu_co_queue_restart_all(&req->wait_queue);
}

/**
 * Add an active request to the tracked requests list
 */
static void tracked_request_begin(BdrvTrackedRequest *req,
                                  BlockDriverState *bs,
                                  int64_t offset,
                                  unsigned int bytes, bool is_write)
{
    *req = (BdrvTrackedRequest){
        .bs = bs,
        .offset         = offset,
        .bytes          = bytes,
        .is_write       = is_write,
        .co             = qemu_coroutine_self(),
        .serialising    = false,
        .overlap_offset = offset,
        .overlap_bytes  = bytes,
    };

    qemu_co_queue_init(&req->wait_queue);

    QLIST_INSERT_HEAD(&bs->tracked_requests, req, list);
}

static void mark_request_serialising(BdrvTrackedRequest *req, uint64_t align)
{
    int64_t overlap_offset = req->offset & ~(align - 1);
    unsigned int overlap_bytes = ROUND_UP(req->offset + req->bytes, align)
                               - overlap_offset;

    if (!req->serialising) {
        req->bs->serialising_in_flight++;
        req->serialising = true;
    }

    req->overlap_offset = MIN(req->overlap_offset, overlap_offset);
    req->overlap_bytes = MAX(req->overlap_bytes, overlap_bytes);
}

/**
 * Round a region to cluster boundaries
 */
void bdrv_round_to_clusters(BlockDriverState *bs,
                            int64_t sector_num, int nb_sectors,
                            int64_t *cluster_sector_num,
                            int *cluster_nb_sectors)
{
    BlockDriverInfo bdi;

    if (bdrv_get_info(bs, &bdi) < 0 || bdi.cluster_size == 0) {
        *cluster_sector_num = sector_num;
        *cluster_nb_sectors = nb_sectors;
    } else {
        int64_t c = bdi.cluster_size / BDRV_SECTOR_SIZE;
        *cluster_sector_num = QEMU_ALIGN_DOWN(sector_num, c);
        *cluster_nb_sectors = QEMU_ALIGN_UP(sector_num - *cluster_sector_num +
                                            nb_sectors, c);
    }
}

static int bdrv_get_cluster_size(BlockDriverState *bs)
{
    BlockDriverInfo bdi;
    int ret;

    ret = bdrv_get_info(bs, &bdi);
    if (ret < 0 || bdi.cluster_size == 0) {
        return bs->request_alignment;
    } else {
        return bdi.cluster_size;
    }
}

static bool tracked_request_overlaps(BdrvTrackedRequest *req,
                                     int64_t offset, unsigned int bytes)
{
    /*        aaaa   bbbb */
    if (offset >= req->overlap_offset + req->overlap_bytes) {
        return false;
    }
    /* bbbb   aaaa        */
    if (req->overlap_offset >= offset + bytes) {
        return false;
    }
    return true;
}

static bool coroutine_fn wait_serialising_requests(BdrvTrackedRequest *self)
{
    BlockDriverState *bs = self->bs;
    BdrvTrackedRequest *req;
    bool retry;
    bool waited = false;

    if (!bs->serialising_in_flight) {
        return false;
    }

    do {
        retry = false;
        QLIST_FOREACH(req, &bs->tracked_requests, list) {
            if (req == self || (!req->serialising && !self->serialising)) {
                continue;
            }
            if (tracked_request_overlaps(req, self->overlap_offset,
                                         self->overlap_bytes))
            {
                /* Hitting this means there was a reentrant request, for
                 * example, a block driver issuing nested requests.  This must
                 * never happen since it means deadlock.
                 */
                assert(qemu_coroutine_self() != req->co);

                /* If the request is already (indirectly) waiting for us, or
                 * will wait for us as soon as it wakes up, then just go on
                 * (instead of producing a deadlock in the former case). */
                if (!req->waiting_for) {
                    self->waiting_for = req;
                    qemu_co_queue_wait(&req->wait_queue);
                    self->waiting_for = NULL;
                    retry = true;
                    waited = true;
                    break;
                }
            }
        }
    } while (retry);

    return waited;
}

/*
 * Return values:
 * 0        - success
 * -EINVAL  - backing format specified, but no file
 * -ENOSPC  - can't update the backing file because no space is left in the
 *            image file header
 * -ENOTSUP - format driver doesn't support changing the backing file
 */
int bdrv_change_backing_file(BlockDriverState *bs,
    const char *backing_file, const char *backing_fmt)
{
    BlockDriver *drv = bs->drv;
    int ret;

    /* Backing file format doesn't make sense without a backing file */
    if (backing_fmt && !backing_file) {
        return -EINVAL;
    }

    if (drv->bdrv_change_backing_file != NULL) {
        ret = drv->bdrv_change_backing_file(bs, backing_file, backing_fmt);
    } else {
        ret = -ENOTSUP;
    }

    if (ret == 0) {
        pstrcpy(bs->backing_file, sizeof(bs->backing_file), backing_file ?: "");
        pstrcpy(bs->backing_format, sizeof(bs->backing_format), backing_fmt ?: "");
    }
    return ret;
}

/*
 * Finds the image layer in the chain that has 'bs' as its backing file.
 *
 * active is the current topmost image.
 *
 * Returns NULL if bs is not found in active's image chain,
 * or if active == bs.
 *
 * Returns the bottommost base image if bs == NULL.
 */
BlockDriverState *bdrv_find_overlay(BlockDriverState *active,
                                    BlockDriverState *bs)
{
    while (active && bs != active->backing_hd) {
        active = active->backing_hd;
    }

    return active;
}

/* Given a BDS, searches for the base layer. */
BlockDriverState *bdrv_find_base(BlockDriverState *bs)
{
    return bdrv_find_overlay(bs, NULL);
}

typedef struct BlkIntermediateStates {
    BlockDriverState *bs;
    QSIMPLEQ_ENTRY(BlkIntermediateStates) entry;
} BlkIntermediateStates;


/*
 * Drops images above 'base' up to and including 'top', and sets the image
 * above 'top' to have base as its backing file.
 *
 * Requires that the overlay to 'top' is opened r/w, so that the backing file
 * information in 'bs' can be properly updated.
 *
 * E.g., this will convert the following chain:
 * bottom <- base <- intermediate <- top <- active
 *
 * to
 *
 * bottom <- base <- active
 *
 * It is allowed for bottom==base, in which case it converts:
 *
 * base <- intermediate <- top <- active
 *
 * to
 *
 * base <- active
 *
 * If backing_file_str is non-NULL, it will be used when modifying top's
 * overlay image metadata.
 *
 * Error conditions:
 *  if active == top, that is considered an error
 *
 */
int bdrv_drop_intermediate(BlockDriverState *active, BlockDriverState *top,
                           BlockDriverState *base, const char *backing_file_str)
{
    BlockDriverState *intermediate;
    BlockDriverState *base_bs = NULL;
    BlockDriverState *new_top_bs = NULL;
    BlkIntermediateStates *intermediate_state, *next;
    int ret = -EIO;

    QSIMPLEQ_HEAD(states_to_delete, BlkIntermediateStates) states_to_delete;
    QSIMPLEQ_INIT(&states_to_delete);

    if (!top->drv || !base->drv) {
        goto exit;
    }

    new_top_bs = bdrv_find_overlay(active, top);

    if (new_top_bs == NULL) {
        /* we could not find the image above 'top', this is an error */
        goto exit;
    }

    /* special case of new_top_bs->backing_hd already pointing to base - nothing
     * to do, no intermediate images */
    if (new_top_bs->backing_hd == base) {
        ret = 0;
        goto exit;
    }

    intermediate = top;

    /* now we will go down through the list, and add each BDS we find
     * into our deletion queue, until we hit the 'base'
     */
    while (intermediate) {
        intermediate_state = g_malloc0(sizeof(BlkIntermediateStates));
        intermediate_state->bs = intermediate;
        QSIMPLEQ_INSERT_TAIL(&states_to_delete, intermediate_state, entry);

        if (intermediate->backing_hd == base) {
            base_bs = intermediate->backing_hd;
            break;
        }
        intermediate = intermediate->backing_hd;
    }
    if (base_bs == NULL) {
        /* something went wrong, we did not end at the base. safely
         * unravel everything, and exit with error */
        goto exit;
    }

    /* success - we can delete the intermediate states, and link top->base */
    backing_file_str = backing_file_str ? backing_file_str : base_bs->filename;
    ret = bdrv_change_backing_file(new_top_bs, backing_file_str,
                                   base_bs->drv ? base_bs->drv->format_name : "");
    if (ret) {
        goto exit;
    }
    bdrv_set_backing_hd(new_top_bs, base_bs);

    QSIMPLEQ_FOREACH_SAFE(intermediate_state, &states_to_delete, entry, next) {
        /* so that bdrv_close() does not recursively close the chain */
        bdrv_set_backing_hd(intermediate_state->bs, NULL);
        bdrv_unref(intermediate_state->bs);
    }
    ret = 0;

exit:
    QSIMPLEQ_FOREACH_SAFE(intermediate_state, &states_to_delete, entry, next) {
        g_free(intermediate_state);
    }
    return ret;
}


static int bdrv_check_byte_request(BlockDriverState *bs, int64_t offset,
                                   size_t size)
{
    int64_t len;

    if (size > INT_MAX) {
        return -EIO;
    }

    if (!bdrv_is_inserted(bs))
        return -ENOMEDIUM;

    if (bs->growable)
        return 0;

    len = bdrv_getlength(bs);

    if (offset < 0)
        return -EIO;

    if ((offset > len) || (len - offset < size))
        return -EIO;

    return 0;
}

static int bdrv_check_request(BlockDriverState *bs, int64_t sector_num,
                              int nb_sectors)
{
    if (nb_sectors < 0 || nb_sectors > INT_MAX / BDRV_SECTOR_SIZE) {
        return -EIO;
    }

    return bdrv_check_byte_request(bs, sector_num * BDRV_SECTOR_SIZE,
                                   nb_sectors * BDRV_SECTOR_SIZE);
}

typedef struct RwCo {
    BlockDriverState *bs;
    int64_t offset;
    QEMUIOVector *qiov;
    bool is_write;
    int ret;
    BdrvRequestFlags flags;
} RwCo;

static void coroutine_fn bdrv_rw_co_entry(void *opaque)
{
    RwCo *rwco = opaque;

    if (!rwco->is_write) {
        rwco->ret = bdrv_co_do_preadv(rwco->bs, rwco->offset,
                                      rwco->qiov->size, rwco->qiov,
                                      rwco->flags);
    } else {
        rwco->ret = bdrv_co_do_pwritev(rwco->bs, rwco->offset,
                                       rwco->qiov->size, rwco->qiov,
                                       rwco->flags);
    }
}

/*
 * Process a vectored synchronous request using coroutines
 */
static int bdrv_prwv_co(BlockDriverState *bs, int64_t offset,
                        QEMUIOVector *qiov, bool is_write,
                        BdrvRequestFlags flags)
{
    Coroutine *co;
    RwCo rwco = {
        .bs = bs,
        .offset = offset,
        .qiov = qiov,
        .is_write = is_write,
        .ret = NOT_DONE,
        .flags = flags,
    };

    /**
     * In sync call context, when the vcpu is blocked, this throttling timer
     * will not fire; so the I/O throttling function has to be disabled here
     * if it has been enabled.
     */
    if (bs->io_limits_enabled) {
        fprintf(stderr, "Disabling I/O throttling on '%s' due "
                        "to synchronous I/O.\n", bdrv_get_device_name(bs));
        bdrv_io_limits_disable(bs);
    }

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_rw_co_entry(&rwco);
    } else {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        co = qemu_coroutine_create(bdrv_rw_co_entry);
        qemu_coroutine_enter(co, &rwco);
        while (rwco.ret == NOT_DONE) {
            aio_poll(aio_context, true);
        }
    }
    return rwco.ret;
}

/*
 * Process a synchronous request using coroutines
 */
static int bdrv_rw_co(BlockDriverState *bs, int64_t sector_num, uint8_t *buf,
                      int nb_sectors, bool is_write, BdrvRequestFlags flags)
{
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len = nb_sectors * BDRV_SECTOR_SIZE,
    };

    if (nb_sectors < 0 || nb_sectors > INT_MAX / BDRV_SECTOR_SIZE) {
        return -EINVAL;
    }

    qemu_iovec_init_external(&qiov, &iov, 1);
    return bdrv_prwv_co(bs, sector_num << BDRV_SECTOR_BITS,
                        &qiov, is_write, flags);
}

/* return < 0 if error. See bdrv_write() for the return codes */
int bdrv_read(BlockDriverState *bs, int64_t sector_num,
              uint8_t *buf, int nb_sectors)
{
    return bdrv_rw_co(bs, sector_num, buf, nb_sectors, false, 0);
}

/* Just like bdrv_read(), but with I/O throttling temporarily disabled */
int bdrv_read_unthrottled(BlockDriverState *bs, int64_t sector_num,
                          uint8_t *buf, int nb_sectors)
{
    bool enabled;
    int ret;

    enabled = bs->io_limits_enabled;
    bs->io_limits_enabled = false;
    ret = bdrv_read(bs, sector_num, buf, nb_sectors);
    bs->io_limits_enabled = enabled;
    return ret;
}

/* Return < 0 if error. Important errors are:
  -EIO         generic I/O error (may happen for all errors)
  -ENOMEDIUM   No media inserted.
  -EINVAL      Invalid sector number or nb_sectors
  -EACCES      Trying to write a read-only device
*/
int bdrv_write(BlockDriverState *bs, int64_t sector_num,
               const uint8_t *buf, int nb_sectors)
{
    return bdrv_rw_co(bs, sector_num, (uint8_t *)buf, nb_sectors, true, 0);
}

int bdrv_write_zeroes(BlockDriverState *bs, int64_t sector_num,
                      int nb_sectors, BdrvRequestFlags flags)
{
    return bdrv_rw_co(bs, sector_num, NULL, nb_sectors, true,
                      BDRV_REQ_ZERO_WRITE | flags);
}

/*
 * Completely zero out a block device with the help of bdrv_write_zeroes.
 * The operation is sped up by checking the block status and only writing
 * zeroes to the device if they currently do not return zeroes. Optional
 * flags are passed through to bdrv_write_zeroes (e.g. BDRV_REQ_MAY_UNMAP).
 *
 * Returns < 0 on error, 0 on success. For error codes see bdrv_write().
 */
int bdrv_make_zero(BlockDriverState *bs, BdrvRequestFlags flags)
{
    int64_t target_sectors, ret, nb_sectors, sector_num = 0;
    int n;

    target_sectors = bdrv_nb_sectors(bs);
    if (target_sectors < 0) {
        return target_sectors;
    }

    for (;;) {
        nb_sectors = target_sectors - sector_num;
        if (nb_sectors <= 0) {
            return 0;
        }
        if (nb_sectors > INT_MAX) {
            nb_sectors = INT_MAX;
        }
        ret = bdrv_get_block_status(bs, sector_num, nb_sectors, &n);
        if (ret < 0) {
            error_report("error getting block status at sector %" PRId64 ": %s",
                         sector_num, strerror(-ret));
            return ret;
        }
        if (ret & BDRV_BLOCK_ZERO) {
            sector_num += n;
            continue;
        }
        ret = bdrv_write_zeroes(bs, sector_num, n, flags);
        if (ret < 0) {
            error_report("error writing zeroes at sector %" PRId64 ": %s",
                         sector_num, strerror(-ret));
            return ret;
        }
        sector_num += n;
    }
}

int bdrv_pread(BlockDriverState *bs, int64_t offset, void *buf, int bytes)
{
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len = bytes,
    };
    int ret;

    if (bytes < 0) {
        return -EINVAL;
    }

    qemu_iovec_init_external(&qiov, &iov, 1);
    ret = bdrv_prwv_co(bs, offset, &qiov, false, 0);
    if (ret < 0) {
        return ret;
    }

    return bytes;
}

int bdrv_pwritev(BlockDriverState *bs, int64_t offset, QEMUIOVector *qiov)
{
    int ret;

    ret = bdrv_prwv_co(bs, offset, qiov, true, 0);
    if (ret < 0) {
        return ret;
    }

    return qiov->size;
}

int bdrv_pwrite(BlockDriverState *bs, int64_t offset,
                const void *buf, int bytes)
{
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base   = (void *) buf,
        .iov_len    = bytes,
    };

    if (bytes < 0) {
        return -EINVAL;
    }

    qemu_iovec_init_external(&qiov, &iov, 1);
    return bdrv_pwritev(bs, offset, &qiov);
}

/*
 * Writes to the file and ensures that no writes are reordered across this
 * request (acts as a barrier)
 *
 * Returns 0 on success, -errno in error cases.
 */
int bdrv_pwrite_sync(BlockDriverState *bs, int64_t offset,
    const void *buf, int count)
{
    int ret;

    ret = bdrv_pwrite(bs, offset, buf, count);
    if (ret < 0) {
        return ret;
    }

    /* No flush needed for cache modes that already do it */
    if (bs->enable_write_cache) {
        bdrv_flush(bs);
    }

    return 0;
}

static int coroutine_fn bdrv_co_do_copy_on_readv(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov)
{
    /* Perform I/O through a temporary buffer so that users who scribble over
     * their read buffer while the operation is in progress do not end up
     * modifying the image file.  This is critical for zero-copy guest I/O
     * where anything might happen inside guest memory.
     */
    void *bounce_buffer;

    BlockDriver *drv = bs->drv;
    struct iovec iov;
    QEMUIOVector bounce_qiov;
    int64_t cluster_sector_num;
    int cluster_nb_sectors;
    size_t skip_bytes;
    int ret;

    /* Cover entire cluster so no additional backing file I/O is required when
     * allocating cluster in the image file.
     */
    bdrv_round_to_clusters(bs, sector_num, nb_sectors,
                           &cluster_sector_num, &cluster_nb_sectors);

    trace_bdrv_co_do_copy_on_readv(bs, sector_num, nb_sectors,
                                   cluster_sector_num, cluster_nb_sectors);

    iov.iov_len = cluster_nb_sectors * BDRV_SECTOR_SIZE;
    iov.iov_base = bounce_buffer = qemu_blockalign(bs, iov.iov_len);
    qemu_iovec_init_external(&bounce_qiov, &iov, 1);

    ret = drv->bdrv_co_readv(bs, cluster_sector_num, cluster_nb_sectors,
                             &bounce_qiov);
    if (ret < 0) {
        goto err;
    }

    if (drv->bdrv_co_write_zeroes &&
        buffer_is_zero(bounce_buffer, iov.iov_len)) {
        ret = bdrv_co_do_write_zeroes(bs, cluster_sector_num,
                                      cluster_nb_sectors, 0);
    } else {
        /* This does not change the data on the disk, it is not necessary
         * to flush even in cache=writethrough mode.
         */
        ret = drv->bdrv_co_writev(bs, cluster_sector_num, cluster_nb_sectors,
                                  &bounce_qiov);
    }

    if (ret < 0) {
        /* It might be okay to ignore write errors for guest requests.  If this
         * is a deliberate copy-on-read then we don't want to ignore the error.
         * Simply report it in all cases.
         */
        goto err;
    }

    skip_bytes = (sector_num - cluster_sector_num) * BDRV_SECTOR_SIZE;
    qemu_iovec_from_buf(qiov, 0, bounce_buffer + skip_bytes,
                        nb_sectors * BDRV_SECTOR_SIZE);

err:
    qemu_vfree(bounce_buffer);
    return ret;
}

/*
 * Forwards an already correctly aligned request to the BlockDriver. This
 * handles copy on read and zeroing after EOF; any other features must be
 * implemented by the caller.
 */
static int coroutine_fn bdrv_aligned_preadv(BlockDriverState *bs,
    BdrvTrackedRequest *req, int64_t offset, unsigned int bytes,
    int64_t align, QEMUIOVector *qiov, int flags)
{
    BlockDriver *drv = bs->drv;
    int ret;

    int64_t sector_num = offset >> BDRV_SECTOR_BITS;
    unsigned int nb_sectors = bytes >> BDRV_SECTOR_BITS;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert(!qiov || bytes == qiov->size);

    /* Handle Copy on Read and associated serialisation */
    if (flags & BDRV_REQ_COPY_ON_READ) {
        /* If we touch the same cluster it counts as an overlap.  This
         * guarantees that allocating writes will be serialized and not race
         * with each other for the same cluster.  For example, in copy-on-read
         * it ensures that the CoR read and write operations are atomic and
         * guest writes cannot interleave between them. */
        mark_request_serialising(req, bdrv_get_cluster_size(bs));
    }

    wait_serialising_requests(req);

    if (flags & BDRV_REQ_COPY_ON_READ) {
        int pnum;

        ret = bdrv_is_allocated(bs, sector_num, nb_sectors, &pnum);
        if (ret < 0) {
            goto out;
        }

        if (!ret || pnum != nb_sectors) {
            ret = bdrv_co_do_copy_on_readv(bs, sector_num, nb_sectors, qiov);
            goto out;
        }
    }

    /* Forward the request to the BlockDriver */
    if (!(bs->zero_beyond_eof && bs->growable)) {
        ret = drv->bdrv_co_readv(bs, sector_num, nb_sectors, qiov);
    } else {
        /* Read zeros after EOF of growable BDSes */
        int64_t total_sectors, max_nb_sectors;

        total_sectors = bdrv_nb_sectors(bs);
        if (total_sectors < 0) {
            ret = total_sectors;
            goto out;
        }

        max_nb_sectors = ROUND_UP(MAX(0, total_sectors - sector_num),
                                  align >> BDRV_SECTOR_BITS);
        if (max_nb_sectors > 0) {
            QEMUIOVector local_qiov;
            size_t local_sectors;

            max_nb_sectors = MIN(max_nb_sectors, SIZE_MAX / BDRV_SECTOR_BITS);
            local_sectors = MIN(max_nb_sectors, nb_sectors);

            qemu_iovec_init(&local_qiov, qiov->niov);
            qemu_iovec_concat(&local_qiov, qiov, 0,
                              local_sectors * BDRV_SECTOR_SIZE);

            ret = drv->bdrv_co_readv(bs, sector_num, local_sectors,
                                     &local_qiov);

            qemu_iovec_destroy(&local_qiov);
        } else {
            ret = 0;
        }

        /* Reading beyond end of file is supposed to produce zeroes */
        if (ret == 0 && total_sectors < sector_num + nb_sectors) {
            uint64_t offset = MAX(0, total_sectors - sector_num);
            uint64_t bytes = (sector_num + nb_sectors - offset) *
                              BDRV_SECTOR_SIZE;
            qemu_iovec_memset(qiov, offset * BDRV_SECTOR_SIZE, 0, bytes);
        }
    }

out:
    return ret;
}

/*
 * Handle a read request in coroutine context
 */
static int coroutine_fn bdrv_co_do_preadv(BlockDriverState *bs,
    int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    BlockDriver *drv = bs->drv;
    BdrvTrackedRequest req;

    /* TODO Lift BDRV_SECTOR_SIZE restriction in BlockDriver interface */
    uint64_t align = MAX(BDRV_SECTOR_SIZE, bs->request_alignment);
    uint8_t *head_buf = NULL;
    uint8_t *tail_buf = NULL;
    QEMUIOVector local_qiov;
    bool use_local_qiov = false;
    int ret;

    if (!drv) {
        return -ENOMEDIUM;
    }
    if (bdrv_check_byte_request(bs, offset, bytes)) {
        return -EIO;
    }

    if (bs->copy_on_read) {
        flags |= BDRV_REQ_COPY_ON_READ;
    }

    /* throttling disk I/O */
    if (bs->io_limits_enabled) {
        bdrv_io_limits_intercept(bs, bytes, false);
    }

    /* Align read if necessary by padding qiov */
    if (offset & (align - 1)) {
        head_buf = qemu_blockalign(bs, align);
        qemu_iovec_init(&local_qiov, qiov->niov + 2);
        qemu_iovec_add(&local_qiov, head_buf, offset & (align - 1));
        qemu_iovec_concat(&local_qiov, qiov, 0, qiov->size);
        use_local_qiov = true;

        bytes += offset & (align - 1);
        offset = offset & ~(align - 1);
    }

    if ((offset + bytes) & (align - 1)) {
        if (!use_local_qiov) {
            qemu_iovec_init(&local_qiov, qiov->niov + 1);
            qemu_iovec_concat(&local_qiov, qiov, 0, qiov->size);
            use_local_qiov = true;
        }
        tail_buf = qemu_blockalign(bs, align);
        qemu_iovec_add(&local_qiov, tail_buf,
                       align - ((offset + bytes) & (align - 1)));

        bytes = ROUND_UP(bytes, align);
    }

    tracked_request_begin(&req, bs, offset, bytes, false);
    ret = bdrv_aligned_preadv(bs, &req, offset, bytes, align,
                              use_local_qiov ? &local_qiov : qiov,
                              flags);
    tracked_request_end(&req);

    if (use_local_qiov) {
        qemu_iovec_destroy(&local_qiov);
        qemu_vfree(head_buf);
        qemu_vfree(tail_buf);
    }

    return ret;
}

static int coroutine_fn bdrv_co_do_readv(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    if (nb_sectors < 0 || nb_sectors > (UINT_MAX >> BDRV_SECTOR_BITS)) {
        return -EINVAL;
    }

    return bdrv_co_do_preadv(bs, sector_num << BDRV_SECTOR_BITS,
                             nb_sectors << BDRV_SECTOR_BITS, qiov, flags);
}

int coroutine_fn bdrv_co_readv(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, QEMUIOVector *qiov)
{
    trace_bdrv_co_readv(bs, sector_num, nb_sectors);

    return bdrv_co_do_readv(bs, sector_num, nb_sectors, qiov, 0);
}

int coroutine_fn bdrv_co_copy_on_readv(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov)
{
    trace_bdrv_co_copy_on_readv(bs, sector_num, nb_sectors);

    return bdrv_co_do_readv(bs, sector_num, nb_sectors, qiov,
                            BDRV_REQ_COPY_ON_READ);
}

/* if no limit is specified in the BlockLimits use a default
 * of 32768 512-byte sectors (16 MiB) per request.
 */
#define MAX_WRITE_ZEROES_DEFAULT 32768

static int coroutine_fn bdrv_co_do_write_zeroes(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, BdrvRequestFlags flags)
{
    BlockDriver *drv = bs->drv;
    QEMUIOVector qiov;
    struct iovec iov = {0};
    int ret = 0;

    int max_write_zeroes = bs->bl.max_write_zeroes ?
                           bs->bl.max_write_zeroes : MAX_WRITE_ZEROES_DEFAULT;

    while (nb_sectors > 0 && !ret) {
        int num = nb_sectors;

        /* Align request.  Block drivers can expect the "bulk" of the request
         * to be aligned.
         */
        if (bs->bl.write_zeroes_alignment
            && num > bs->bl.write_zeroes_alignment) {
            if (sector_num % bs->bl.write_zeroes_alignment != 0) {
                /* Make a small request up to the first aligned sector.  */
                num = bs->bl.write_zeroes_alignment;
                num -= sector_num % bs->bl.write_zeroes_alignment;
            } else if ((sector_num + num) % bs->bl.write_zeroes_alignment != 0) {
                /* Shorten the request to the last aligned sector.  num cannot
                 * underflow because num > bs->bl.write_zeroes_alignment.
                 */
                num -= (sector_num + num) % bs->bl.write_zeroes_alignment;
            }
        }

        /* limit request size */
        if (num > max_write_zeroes) {
            num = max_write_zeroes;
        }

        ret = -ENOTSUP;
        /* First try the efficient write zeroes operation */
        if (drv->bdrv_co_write_zeroes) {
            ret = drv->bdrv_co_write_zeroes(bs, sector_num, num, flags);
        }

        if (ret == -ENOTSUP) {
            /* Fall back to bounce buffer if write zeroes is unsupported */
            iov.iov_len = num * BDRV_SECTOR_SIZE;
            if (iov.iov_base == NULL) {
                iov.iov_base = qemu_blockalign(bs, num * BDRV_SECTOR_SIZE);
                memset(iov.iov_base, 0, num * BDRV_SECTOR_SIZE);
            }
            qemu_iovec_init_external(&qiov, &iov, 1);

            ret = drv->bdrv_co_writev(bs, sector_num, num, &qiov);

            /* Keep bounce buffer around if it is big enough for all
             * all future requests.
             */
            if (num < max_write_zeroes) {
                qemu_vfree(iov.iov_base);
                iov.iov_base = NULL;
            }
        }

        sector_num += num;
        nb_sectors -= num;
    }

    qemu_vfree(iov.iov_base);
    return ret;
}

/*
 * Forwards an already correctly aligned write request to the BlockDriver.
 */
static int coroutine_fn bdrv_aligned_pwritev(BlockDriverState *bs,
    BdrvTrackedRequest *req, int64_t offset, unsigned int bytes,
    QEMUIOVector *qiov, int flags)
{
    BlockDriver *drv = bs->drv;
    bool waited;
    int ret;

    int64_t sector_num = offset >> BDRV_SECTOR_BITS;
    unsigned int nb_sectors = bytes >> BDRV_SECTOR_BITS;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert(!qiov || bytes == qiov->size);

    waited = wait_serialising_requests(req);
    assert(!waited || !req->serialising);
    assert(req->overlap_offset <= offset);
    assert(offset + bytes <= req->overlap_offset + req->overlap_bytes);

    ret = notifier_with_return_list_notify(&bs->before_write_notifiers, req);

    if (!ret && bs->detect_zeroes != BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF &&
        !(flags & BDRV_REQ_ZERO_WRITE) && drv->bdrv_co_write_zeroes &&
        qemu_iovec_is_zero(qiov)) {
        flags |= BDRV_REQ_ZERO_WRITE;
        if (bs->detect_zeroes == BLOCKDEV_DETECT_ZEROES_OPTIONS_UNMAP) {
            flags |= BDRV_REQ_MAY_UNMAP;
        }
    }

    if (ret < 0) {
        /* Do nothing, write notifier decided to fail this request */
    } else if (flags & BDRV_REQ_ZERO_WRITE) {
        BLKDBG_EVENT(bs, BLKDBG_PWRITEV_ZERO);
        ret = bdrv_co_do_write_zeroes(bs, sector_num, nb_sectors, flags);
    } else {
        BLKDBG_EVENT(bs, BLKDBG_PWRITEV);
        ret = drv->bdrv_co_writev(bs, sector_num, nb_sectors, qiov);
    }
    BLKDBG_EVENT(bs, BLKDBG_PWRITEV_DONE);

    if (ret == 0 && !bs->enable_write_cache) {
        ret = bdrv_co_flush(bs);
    }

    bdrv_set_dirty(bs, sector_num, nb_sectors);

    if (bs->wr_highest_sector < sector_num + nb_sectors - 1) {
        bs->wr_highest_sector = sector_num + nb_sectors - 1;
    }
    if (bs->growable && ret >= 0) {
        bs->total_sectors = MAX(bs->total_sectors, sector_num + nb_sectors);
    }

    return ret;
}

/*
 * Handle a write request in coroutine context
 */
static int coroutine_fn bdrv_co_do_pwritev(BlockDriverState *bs,
    int64_t offset, unsigned int bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    BdrvTrackedRequest req;
    /* TODO Lift BDRV_SECTOR_SIZE restriction in BlockDriver interface */
    uint64_t align = MAX(BDRV_SECTOR_SIZE, bs->request_alignment);
    uint8_t *head_buf = NULL;
    uint8_t *tail_buf = NULL;
    QEMUIOVector local_qiov;
    bool use_local_qiov = false;
    int ret;

    if (!bs->drv) {
        return -ENOMEDIUM;
    }
    if (bs->read_only) {
        return -EACCES;
    }
    if (bdrv_check_byte_request(bs, offset, bytes)) {
        return -EIO;
    }

    /* throttling disk I/O */
    if (bs->io_limits_enabled) {
        bdrv_io_limits_intercept(bs, bytes, true);
    }

    /*
     * Align write if necessary by performing a read-modify-write cycle.
     * Pad qiov with the read parts and be sure to have a tracked request not
     * only for bdrv_aligned_pwritev, but also for the reads of the RMW cycle.
     */
    tracked_request_begin(&req, bs, offset, bytes, true);

    if (offset & (align - 1)) {
        QEMUIOVector head_qiov;
        struct iovec head_iov;

        mark_request_serialising(&req, align);
        wait_serialising_requests(&req);

        head_buf = qemu_blockalign(bs, align);
        head_iov = (struct iovec) {
            .iov_base   = head_buf,
            .iov_len    = align,
        };
        qemu_iovec_init_external(&head_qiov, &head_iov, 1);

        BLKDBG_EVENT(bs, BLKDBG_PWRITEV_RMW_HEAD);
        ret = bdrv_aligned_preadv(bs, &req, offset & ~(align - 1), align,
                                  align, &head_qiov, 0);
        if (ret < 0) {
            goto fail;
        }
        BLKDBG_EVENT(bs, BLKDBG_PWRITEV_RMW_AFTER_HEAD);

        qemu_iovec_init(&local_qiov, qiov->niov + 2);
        qemu_iovec_add(&local_qiov, head_buf, offset & (align - 1));
        qemu_iovec_concat(&local_qiov, qiov, 0, qiov->size);
        use_local_qiov = true;

        bytes += offset & (align - 1);
        offset = offset & ~(align - 1);
    }

    if ((offset + bytes) & (align - 1)) {
        QEMUIOVector tail_qiov;
        struct iovec tail_iov;
        size_t tail_bytes;
        bool waited;

        mark_request_serialising(&req, align);
        waited = wait_serialising_requests(&req);
        assert(!waited || !use_local_qiov);

        tail_buf = qemu_blockalign(bs, align);
        tail_iov = (struct iovec) {
            .iov_base   = tail_buf,
            .iov_len    = align,
        };
        qemu_iovec_init_external(&tail_qiov, &tail_iov, 1);

        BLKDBG_EVENT(bs, BLKDBG_PWRITEV_RMW_TAIL);
        ret = bdrv_aligned_preadv(bs, &req, (offset + bytes) & ~(align - 1), align,
                                  align, &tail_qiov, 0);
        if (ret < 0) {
            goto fail;
        }
        BLKDBG_EVENT(bs, BLKDBG_PWRITEV_RMW_AFTER_TAIL);

        if (!use_local_qiov) {
            qemu_iovec_init(&local_qiov, qiov->niov + 1);
            qemu_iovec_concat(&local_qiov, qiov, 0, qiov->size);
            use_local_qiov = true;
        }

        tail_bytes = (offset + bytes) & (align - 1);
        qemu_iovec_add(&local_qiov, tail_buf + tail_bytes, align - tail_bytes);

        bytes = ROUND_UP(bytes, align);
    }

    ret = bdrv_aligned_pwritev(bs, &req, offset, bytes,
                               use_local_qiov ? &local_qiov : qiov,
                               flags);

fail:
    tracked_request_end(&req);

    if (use_local_qiov) {
        qemu_iovec_destroy(&local_qiov);
    }
    qemu_vfree(head_buf);
    qemu_vfree(tail_buf);

    return ret;
}

static int coroutine_fn bdrv_co_do_writev(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    if (nb_sectors < 0 || nb_sectors > (INT_MAX >> BDRV_SECTOR_BITS)) {
        return -EINVAL;
    }

    return bdrv_co_do_pwritev(bs, sector_num << BDRV_SECTOR_BITS,
                              nb_sectors << BDRV_SECTOR_BITS, qiov, flags);
}

int coroutine_fn bdrv_co_writev(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, QEMUIOVector *qiov)
{
    trace_bdrv_co_writev(bs, sector_num, nb_sectors);

    return bdrv_co_do_writev(bs, sector_num, nb_sectors, qiov, 0);
}

int coroutine_fn bdrv_co_write_zeroes(BlockDriverState *bs,
                                      int64_t sector_num, int nb_sectors,
                                      BdrvRequestFlags flags)
{
    trace_bdrv_co_write_zeroes(bs, sector_num, nb_sectors, flags);

    if (!(bs->open_flags & BDRV_O_UNMAP)) {
        flags &= ~BDRV_REQ_MAY_UNMAP;
    }

    return bdrv_co_do_writev(bs, sector_num, nb_sectors, NULL,
                             BDRV_REQ_ZERO_WRITE | flags);
}

/**
 * Truncate file to 'offset' bytes (needed only for file protocols)
 */
int bdrv_truncate(BlockDriverState *bs, int64_t offset)
{
    BlockDriver *drv = bs->drv;
    int ret;
    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_truncate)
        return -ENOTSUP;
    if (bs->read_only)
        return -EACCES;

    ret = drv->bdrv_truncate(bs, offset);
    if (ret == 0) {
        ret = refresh_total_sectors(bs, offset >> BDRV_SECTOR_BITS);
        bdrv_dev_resize_cb(bs);
    }
    return ret;
}

/**
 * Length of a allocated file in bytes. Sparse files are counted by actual
 * allocated space. Return < 0 if error or unknown.
 */
int64_t bdrv_get_allocated_file_size(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    if (!drv) {
        return -ENOMEDIUM;
    }
    if (drv->bdrv_get_allocated_file_size) {
        return drv->bdrv_get_allocated_file_size(bs);
    }
    if (bs->file) {
        return bdrv_get_allocated_file_size(bs->file);
    }
    return -ENOTSUP;
}

/**
 * Return number of sectors on success, -errno on error.
 */
int64_t bdrv_nb_sectors(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;

    if (!drv)
        return -ENOMEDIUM;

    if (drv->has_variable_length) {
        int ret = refresh_total_sectors(bs, bs->total_sectors);
        if (ret < 0) {
            return ret;
        }
    }
    return bs->total_sectors;
}

/**
 * Return length in bytes on success, -errno on error.
 * The length is always a multiple of BDRV_SECTOR_SIZE.
 */
int64_t bdrv_getlength(BlockDriverState *bs)
{
    int64_t ret = bdrv_nb_sectors(bs);

    return ret < 0 ? ret : ret * BDRV_SECTOR_SIZE;
}

/* return 0 as number of sectors if no device present or error */
void bdrv_get_geometry(BlockDriverState *bs, uint64_t *nb_sectors_ptr)
{
    int64_t nb_sectors = bdrv_nb_sectors(bs);

    *nb_sectors_ptr = nb_sectors < 0 ? 0 : nb_sectors;
}

void bdrv_set_on_error(BlockDriverState *bs, BlockdevOnError on_read_error,
                       BlockdevOnError on_write_error)
{
    bs->on_read_error = on_read_error;
    bs->on_write_error = on_write_error;
}

BlockdevOnError bdrv_get_on_error(BlockDriverState *bs, bool is_read)
{
    return is_read ? bs->on_read_error : bs->on_write_error;
}

BlockErrorAction bdrv_get_error_action(BlockDriverState *bs, bool is_read, int error)
{
    BlockdevOnError on_err = is_read ? bs->on_read_error : bs->on_write_error;

    switch (on_err) {
    case BLOCKDEV_ON_ERROR_ENOSPC:
        return (error == ENOSPC) ?
               BLOCK_ERROR_ACTION_STOP : BLOCK_ERROR_ACTION_REPORT;
    case BLOCKDEV_ON_ERROR_STOP:
        return BLOCK_ERROR_ACTION_STOP;
    case BLOCKDEV_ON_ERROR_REPORT:
        return BLOCK_ERROR_ACTION_REPORT;
    case BLOCKDEV_ON_ERROR_IGNORE:
        return BLOCK_ERROR_ACTION_IGNORE;
    default:
        abort();
    }
}

/* This is done by device models because, while the block layer knows
 * about the error, it does not know whether an operation comes from
 * the device or the block layer (from a job, for example).
 */
void bdrv_error_action(BlockDriverState *bs, BlockErrorAction action,
                       bool is_read, int error)
{
    assert(error >= 0);

    if (action == BLOCK_ERROR_ACTION_STOP) {
        /* First set the iostatus, so that "info block" returns an iostatus
         * that matches the events raised so far (an additional error iostatus
         * is fine, but not a lost one).
         */
        bdrv_iostatus_set_err(bs, error);

        /* Then raise the request to stop the VM and the event.
         * qemu_system_vmstop_request_prepare has two effects.  First,
         * it ensures that the STOP event always comes after the
         * BLOCK_IO_ERROR event.  Second, it ensures that even if management
         * can observe the STOP event and do a "cont" before the STOP
         * event is issued, the VM will not stop.  In this case, vm_start()
         * also ensures that the STOP/RESUME pair of events is emitted.
         */
        qemu_system_vmstop_request_prepare();
        qapi_event_send_block_io_error(bdrv_get_device_name(bs),
                                       is_read ? IO_OPERATION_TYPE_READ :
                                       IO_OPERATION_TYPE_WRITE,
                                       action, &error_abort);
        qemu_system_vmstop_request(RUN_STATE_IO_ERROR);
    } else {
        qapi_event_send_block_io_error(bdrv_get_device_name(bs),
                                       is_read ? IO_OPERATION_TYPE_READ :
                                       IO_OPERATION_TYPE_WRITE,
                                       action, &error_abort);
    }
}

int bdrv_is_read_only(BlockDriverState *bs)
{
    return bs->read_only;
}

int bdrv_is_sg(BlockDriverState *bs)
{
    return bs->sg;
}

int bdrv_enable_write_cache(BlockDriverState *bs)
{
    return bs->enable_write_cache;
}

void bdrv_set_enable_write_cache(BlockDriverState *bs, bool wce)
{
    bs->enable_write_cache = wce;

    /* so a reopen() will preserve wce */
    if (wce) {
        bs->open_flags |= BDRV_O_CACHE_WB;
    } else {
        bs->open_flags &= ~BDRV_O_CACHE_WB;
    }
}

int bdrv_is_encrypted(BlockDriverState *bs)
{
    if (bs->backing_hd && bs->backing_hd->encrypted)
        return 1;
    return bs->encrypted;
}

int bdrv_key_required(BlockDriverState *bs)
{
    BlockDriverState *backing_hd = bs->backing_hd;

    if (backing_hd && backing_hd->encrypted && !backing_hd->valid_key)
        return 1;
    return (bs->encrypted && !bs->valid_key);
}

int bdrv_set_key(BlockDriverState *bs, const char *key)
{
    int ret;
    if (bs->backing_hd && bs->backing_hd->encrypted) {
        ret = bdrv_set_key(bs->backing_hd, key);
        if (ret < 0)
            return ret;
        if (!bs->encrypted)
            return 0;
    }
    if (!bs->encrypted) {
        return -EINVAL;
    } else if (!bs->drv || !bs->drv->bdrv_set_key) {
        return -ENOMEDIUM;
    }
    ret = bs->drv->bdrv_set_key(bs, key);
    if (ret < 0) {
        bs->valid_key = 0;
    } else if (!bs->valid_key) {
        bs->valid_key = 1;
        /* call the change callback now, we skipped it on open */
        bdrv_dev_change_media_cb(bs, true);
    }
    return ret;
}

const char *bdrv_get_format_name(BlockDriverState *bs)
{
    return bs->drv ? bs->drv->format_name : NULL;
}

void bdrv_iterate_format(void (*it)(void *opaque, const char *name),
                         void *opaque)
{
    BlockDriver *drv;
    int count = 0;
    const char **formats = NULL;

    QLIST_FOREACH(drv, &bdrv_drivers, list) {
        if (drv->format_name) {
            bool found = false;
            int i = count;
            while (formats && i && !found) {
                found = !strcmp(formats[--i], drv->format_name);
            }

            if (!found) {
                formats = g_realloc(formats, (count + 1) * sizeof(char *));
                formats[count++] = drv->format_name;
                it(opaque, drv->format_name);
            }
        }
    }
    g_free(formats);
}

/* This function is to find block backend bs */
BlockDriverState *bdrv_find(const char *name)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, device_list) {
        if (!strcmp(name, bs->device_name)) {
            return bs;
        }
    }
    return NULL;
}

/* This function is to find a node in the bs graph */
BlockDriverState *bdrv_find_node(const char *node_name)
{
    BlockDriverState *bs;

    assert(node_name);

    QTAILQ_FOREACH(bs, &graph_bdrv_states, node_list) {
        if (!strcmp(node_name, bs->node_name)) {
            return bs;
        }
    }
    return NULL;
}

/* Put this QMP function here so it can access the static graph_bdrv_states. */
BlockDeviceInfoList *bdrv_named_nodes_list(void)
{
    BlockDeviceInfoList *list, *entry;
    BlockDriverState *bs;

    list = NULL;
    QTAILQ_FOREACH(bs, &graph_bdrv_states, node_list) {
        entry = g_malloc0(sizeof(*entry));
        entry->value = bdrv_block_device_info(bs);
        entry->next = list;
        list = entry;
    }

    return list;
}

BlockDriverState *bdrv_lookup_bs(const char *device,
                                 const char *node_name,
                                 Error **errp)
{
    BlockDriverState *bs = NULL;

    if (device) {
        bs = bdrv_find(device);

        if (bs) {
            return bs;
        }
    }

    if (node_name) {
        bs = bdrv_find_node(node_name);

        if (bs) {
            return bs;
        }
    }

    error_setg(errp, "Cannot find device=%s nor node_name=%s",
                     device ? device : "",
                     node_name ? node_name : "");
    return NULL;
}

/* If 'base' is in the same chain as 'top', return true. Otherwise,
 * return false.  If either argument is NULL, return false. */
bool bdrv_chain_contains(BlockDriverState *top, BlockDriverState *base)
{
    while (top && top != base) {
        top = top->backing_hd;
    }

    return top != NULL;
}

BlockDriverState *bdrv_next(BlockDriverState *bs)
{
    if (!bs) {
        return QTAILQ_FIRST(&bdrv_states);
    }
    return QTAILQ_NEXT(bs, device_list);
}

void bdrv_iterate(void (*it)(void *opaque, BlockDriverState *bs), void *opaque)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, device_list) {
        it(opaque, bs);
    }
}

const char *bdrv_get_device_name(BlockDriverState *bs)
{
    return bs->device_name;
}

int bdrv_get_flags(BlockDriverState *bs)
{
    return bs->open_flags;
}

int bdrv_flush_all(void)
{
    BlockDriverState *bs;
    int result = 0;

    QTAILQ_FOREACH(bs, &bdrv_states, device_list) {
        AioContext *aio_context = bdrv_get_aio_context(bs);
        int ret;

        aio_context_acquire(aio_context);
        ret = bdrv_flush(bs);
        if (ret < 0 && !result) {
            result = ret;
        }
        aio_context_release(aio_context);
    }

    return result;
}

int bdrv_has_zero_init_1(BlockDriverState *bs)
{
    return 1;
}

int bdrv_has_zero_init(BlockDriverState *bs)
{
    assert(bs->drv);

    /* If BS is a copy on write image, it is initialized to
       the contents of the base image, which may not be zeroes.  */
    if (bs->backing_hd) {
        return 0;
    }
    if (bs->drv->bdrv_has_zero_init) {
        return bs->drv->bdrv_has_zero_init(bs);
    }

    /* safe default */
    return 0;
}

bool bdrv_unallocated_blocks_are_zero(BlockDriverState *bs)
{
    BlockDriverInfo bdi;

    if (bs->backing_hd) {
        return false;
    }

    if (bdrv_get_info(bs, &bdi) == 0) {
        return bdi.unallocated_blocks_are_zero;
    }

    return false;
}

bool bdrv_can_write_zeroes_with_unmap(BlockDriverState *bs)
{
    BlockDriverInfo bdi;

    if (bs->backing_hd || !(bs->open_flags & BDRV_O_UNMAP)) {
        return false;
    }

    if (bdrv_get_info(bs, &bdi) == 0) {
        return bdi.can_write_zeroes_with_unmap;
    }

    return false;
}

typedef struct BdrvCoGetBlockStatusData {
    BlockDriverState *bs;
    BlockDriverState *base;
    int64_t sector_num;
    int nb_sectors;
    int *pnum;
    int64_t ret;
    bool done;
} BdrvCoGetBlockStatusData;

/*
 * Returns true iff the specified sector is present in the disk image. Drivers
 * not implementing the functionality are assumed to not support backing files,
 * hence all their sectors are reported as allocated.
 *
 * If 'sector_num' is beyond the end of the disk image the return value is 0
 * and 'pnum' is set to 0.
 *
 * 'pnum' is set to the number of sectors (including and immediately following
 * the specified sector) that are known to be in the same
 * allocated/unallocated state.
 *
 * 'nb_sectors' is the max value 'pnum' should be set to.  If nb_sectors goes
 * beyond the end of the disk image it will be clamped.
 */
static int64_t coroutine_fn bdrv_co_get_block_status(BlockDriverState *bs,
                                                     int64_t sector_num,
                                                     int nb_sectors, int *pnum)
{
    int64_t total_sectors;
    int64_t n;
    int64_t ret, ret2;

    total_sectors = bdrv_nb_sectors(bs);
    if (total_sectors < 0) {
        return total_sectors;
    }

    if (sector_num >= total_sectors) {
        *pnum = 0;
        return 0;
    }

    n = total_sectors - sector_num;
    if (n < nb_sectors) {
        nb_sectors = n;
    }

    if (!bs->drv->bdrv_co_get_block_status) {
        *pnum = nb_sectors;
        ret = BDRV_BLOCK_DATA | BDRV_BLOCK_ALLOCATED;
        if (bs->drv->protocol_name) {
            ret |= BDRV_BLOCK_OFFSET_VALID | (sector_num * BDRV_SECTOR_SIZE);
        }
        return ret;
    }

    ret = bs->drv->bdrv_co_get_block_status(bs, sector_num, nb_sectors, pnum);
    if (ret < 0) {
        *pnum = 0;
        return ret;
    }

    if (ret & BDRV_BLOCK_RAW) {
        assert(ret & BDRV_BLOCK_OFFSET_VALID);
        return bdrv_get_block_status(bs->file, ret >> BDRV_SECTOR_BITS,
                                     *pnum, pnum);
    }

    if (ret & (BDRV_BLOCK_DATA | BDRV_BLOCK_ZERO)) {
        ret |= BDRV_BLOCK_ALLOCATED;
    }

    if (!(ret & BDRV_BLOCK_DATA) && !(ret & BDRV_BLOCK_ZERO)) {
        if (bdrv_unallocated_blocks_are_zero(bs)) {
            ret |= BDRV_BLOCK_ZERO;
        } else if (bs->backing_hd) {
            BlockDriverState *bs2 = bs->backing_hd;
            int64_t nb_sectors2 = bdrv_nb_sectors(bs2);
            if (nb_sectors2 >= 0 && sector_num >= nb_sectors2) {
                ret |= BDRV_BLOCK_ZERO;
            }
        }
    }

    if (bs->file &&
        (ret & BDRV_BLOCK_DATA) && !(ret & BDRV_BLOCK_ZERO) &&
        (ret & BDRV_BLOCK_OFFSET_VALID)) {
        ret2 = bdrv_co_get_block_status(bs->file, ret >> BDRV_SECTOR_BITS,
                                        *pnum, pnum);
        if (ret2 >= 0) {
            /* Ignore errors.  This is just providing extra information, it
             * is useful but not necessary.
             */
            ret |= (ret2 & BDRV_BLOCK_ZERO);
        }
    }

    return ret;
}

/* Coroutine wrapper for bdrv_get_block_status() */
static void coroutine_fn bdrv_get_block_status_co_entry(void *opaque)
{
    BdrvCoGetBlockStatusData *data = opaque;
    BlockDriverState *bs = data->bs;

    data->ret = bdrv_co_get_block_status(bs, data->sector_num, data->nb_sectors,
                                         data->pnum);
    data->done = true;
}

/*
 * Synchronous wrapper around bdrv_co_get_block_status().
 *
 * See bdrv_co_get_block_status() for details.
 */
int64_t bdrv_get_block_status(BlockDriverState *bs, int64_t sector_num,
                              int nb_sectors, int *pnum)
{
    Coroutine *co;
    BdrvCoGetBlockStatusData data = {
        .bs = bs,
        .sector_num = sector_num,
        .nb_sectors = nb_sectors,
        .pnum = pnum,
        .done = false,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_get_block_status_co_entry(&data);
    } else {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        co = qemu_coroutine_create(bdrv_get_block_status_co_entry);
        qemu_coroutine_enter(co, &data);
        while (!data.done) {
            aio_poll(aio_context, true);
        }
    }
    return data.ret;
}

int coroutine_fn bdrv_is_allocated(BlockDriverState *bs, int64_t sector_num,
                                   int nb_sectors, int *pnum)
{
    int64_t ret = bdrv_get_block_status(bs, sector_num, nb_sectors, pnum);
    if (ret < 0) {
        return ret;
    }
    return !!(ret & BDRV_BLOCK_ALLOCATED);
}

/*
 * Given an image chain: ... -> [BASE] -> [INTER1] -> [INTER2] -> [TOP]
 *
 * Return true if the given sector is allocated in any image between
 * BASE and TOP (inclusive).  BASE can be NULL to check if the given
 * sector is allocated in any image of the chain.  Return false otherwise.
 *
 * 'pnum' is set to the number of sectors (including and immediately following
 *  the specified sector) that are known to be in the same
 *  allocated/unallocated state.
 *
 */
int bdrv_is_allocated_above(BlockDriverState *top,
                            BlockDriverState *base,
                            int64_t sector_num,
                            int nb_sectors, int *pnum)
{
    BlockDriverState *intermediate;
    int ret, n = nb_sectors;

    intermediate = top;
    while (intermediate && intermediate != base) {
        int pnum_inter;
        ret = bdrv_is_allocated(intermediate, sector_num, nb_sectors,
                                &pnum_inter);
        if (ret < 0) {
            return ret;
        } else if (ret) {
            *pnum = pnum_inter;
            return 1;
        }

        /*
         * [sector_num, nb_sectors] is unallocated on top but intermediate
         * might have
         *
         * [sector_num+x, nr_sectors] allocated.
         */
        if (n > pnum_inter &&
            (intermediate == top ||
             sector_num + pnum_inter < intermediate->total_sectors)) {
            n = pnum_inter;
        }

        intermediate = intermediate->backing_hd;
    }

    *pnum = n;
    return 0;
}

const char *bdrv_get_encrypted_filename(BlockDriverState *bs)
{
    if (bs->backing_hd && bs->backing_hd->encrypted)
        return bs->backing_file;
    else if (bs->encrypted)
        return bs->filename;
    else
        return NULL;
}

void bdrv_get_backing_filename(BlockDriverState *bs,
                               char *filename, int filename_size)
{
    pstrcpy(filename, filename_size, bs->backing_file);
}

int bdrv_write_compressed(BlockDriverState *bs, int64_t sector_num,
                          const uint8_t *buf, int nb_sectors)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_write_compressed)
        return -ENOTSUP;
    if (bdrv_check_request(bs, sector_num, nb_sectors))
        return -EIO;

    assert(QLIST_EMPTY(&bs->dirty_bitmaps));

    return drv->bdrv_write_compressed(bs, sector_num, buf, nb_sectors);
}

int bdrv_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_get_info)
        return -ENOTSUP;
    memset(bdi, 0, sizeof(*bdi));
    return drv->bdrv_get_info(bs, bdi);
}

ImageInfoSpecific *bdrv_get_specific_info(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    if (drv && drv->bdrv_get_specific_info) {
        return drv->bdrv_get_specific_info(bs);
    }
    return NULL;
}

int bdrv_save_vmstate(BlockDriverState *bs, const uint8_t *buf,
                      int64_t pos, int size)
{
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base   = (void *) buf,
        .iov_len    = size,
    };

    qemu_iovec_init_external(&qiov, &iov, 1);
    return bdrv_writev_vmstate(bs, &qiov, pos);
}

int bdrv_writev_vmstate(BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos)
{
    BlockDriver *drv = bs->drv;

    if (!drv) {
        return -ENOMEDIUM;
    } else if (drv->bdrv_save_vmstate) {
        return drv->bdrv_save_vmstate(bs, qiov, pos);
    } else if (bs->file) {
        return bdrv_writev_vmstate(bs->file, qiov, pos);
    }

    return -ENOTSUP;
}

int bdrv_load_vmstate(BlockDriverState *bs, uint8_t *buf,
                      int64_t pos, int size)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (drv->bdrv_load_vmstate)
        return drv->bdrv_load_vmstate(bs, buf, pos, size);
    if (bs->file)
        return bdrv_load_vmstate(bs->file, buf, pos, size);
    return -ENOTSUP;
}

void bdrv_debug_event(BlockDriverState *bs, BlkDebugEvent event)
{
    if (!bs || !bs->drv || !bs->drv->bdrv_debug_event) {
        return;
    }

    bs->drv->bdrv_debug_event(bs, event);
}

int bdrv_debug_breakpoint(BlockDriverState *bs, const char *event,
                          const char *tag)
{
    while (bs && bs->drv && !bs->drv->bdrv_debug_breakpoint) {
        bs = bs->file;
    }

    if (bs && bs->drv && bs->drv->bdrv_debug_breakpoint) {
        return bs->drv->bdrv_debug_breakpoint(bs, event, tag);
    }

    return -ENOTSUP;
}

int bdrv_debug_remove_breakpoint(BlockDriverState *bs, const char *tag)
{
    while (bs && bs->drv && !bs->drv->bdrv_debug_remove_breakpoint) {
        bs = bs->file;
    }

    if (bs && bs->drv && bs->drv->bdrv_debug_remove_breakpoint) {
        return bs->drv->bdrv_debug_remove_breakpoint(bs, tag);
    }

    return -ENOTSUP;
}

int bdrv_debug_resume(BlockDriverState *bs, const char *tag)
{
    while (bs && (!bs->drv || !bs->drv->bdrv_debug_resume)) {
        bs = bs->file;
    }

    if (bs && bs->drv && bs->drv->bdrv_debug_resume) {
        return bs->drv->bdrv_debug_resume(bs, tag);
    }

    return -ENOTSUP;
}

bool bdrv_debug_is_suspended(BlockDriverState *bs, const char *tag)
{
    while (bs && bs->drv && !bs->drv->bdrv_debug_is_suspended) {
        bs = bs->file;
    }

    if (bs && bs->drv && bs->drv->bdrv_debug_is_suspended) {
        return bs->drv->bdrv_debug_is_suspended(bs, tag);
    }

    return false;
}

int bdrv_is_snapshot(BlockDriverState *bs)
{
    return !!(bs->open_flags & BDRV_O_SNAPSHOT);
}

/* backing_file can either be relative, or absolute, or a protocol.  If it is
 * relative, it must be relative to the chain.  So, passing in bs->filename
 * from a BDS as backing_file should not be done, as that may be relative to
 * the CWD rather than the chain. */
BlockDriverState *bdrv_find_backing_image(BlockDriverState *bs,
        const char *backing_file)
{
    char *filename_full = NULL;
    char *backing_file_full = NULL;
    char *filename_tmp = NULL;
    int is_protocol = 0;
    BlockDriverState *curr_bs = NULL;
    BlockDriverState *retval = NULL;

    if (!bs || !bs->drv || !backing_file) {
        return NULL;
    }

    filename_full     = g_malloc(PATH_MAX);
    backing_file_full = g_malloc(PATH_MAX);
    filename_tmp      = g_malloc(PATH_MAX);

    is_protocol = path_has_protocol(backing_file);

    for (curr_bs = bs; curr_bs->backing_hd; curr_bs = curr_bs->backing_hd) {

        /* If either of the filename paths is actually a protocol, then
         * compare unmodified paths; otherwise make paths relative */
        if (is_protocol || path_has_protocol(curr_bs->backing_file)) {
            if (strcmp(backing_file, curr_bs->backing_file) == 0) {
                retval = curr_bs->backing_hd;
                break;
            }
        } else {
            /* If not an absolute filename path, make it relative to the current
             * image's filename path */
            path_combine(filename_tmp, PATH_MAX, curr_bs->filename,
                         backing_file);

            /* We are going to compare absolute pathnames */
            if (!realpath(filename_tmp, filename_full)) {
                continue;
            }

            /* We need to make sure the backing filename we are comparing against
             * is relative to the current image filename (or absolute) */
            path_combine(filename_tmp, PATH_MAX, curr_bs->filename,
                         curr_bs->backing_file);

            if (!realpath(filename_tmp, backing_file_full)) {
                continue;
            }

            if (strcmp(backing_file_full, filename_full) == 0) {
                retval = curr_bs->backing_hd;
                break;
            }
        }
    }

    g_free(filename_full);
    g_free(backing_file_full);
    g_free(filename_tmp);
    return retval;
}

int bdrv_get_backing_file_depth(BlockDriverState *bs)
{
    if (!bs->drv) {
        return 0;
    }

    if (!bs->backing_hd) {
        return 0;
    }

    return 1 + bdrv_get_backing_file_depth(bs->backing_hd);
}

/**************************************************************/
/* async I/Os */

BlockDriverAIOCB *bdrv_aio_readv(BlockDriverState *bs, int64_t sector_num,
                                 QEMUIOVector *qiov, int nb_sectors,
                                 BlockDriverCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_readv(bs, sector_num, nb_sectors, opaque);

    return bdrv_co_aio_rw_vector(bs, sector_num, qiov, nb_sectors, 0,
                                 cb, opaque, false);
}

BlockDriverAIOCB *bdrv_aio_writev(BlockDriverState *bs, int64_t sector_num,
                                  QEMUIOVector *qiov, int nb_sectors,
                                  BlockDriverCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_writev(bs, sector_num, nb_sectors, opaque);

    return bdrv_co_aio_rw_vector(bs, sector_num, qiov, nb_sectors, 0,
                                 cb, opaque, true);
}

BlockDriverAIOCB *bdrv_aio_write_zeroes(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, BdrvRequestFlags flags,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_write_zeroes(bs, sector_num, nb_sectors, flags, opaque);

    return bdrv_co_aio_rw_vector(bs, sector_num, NULL, nb_sectors,
                                 BDRV_REQ_ZERO_WRITE | flags,
                                 cb, opaque, true);
}


typedef struct MultiwriteCB {
    int error;
    int num_requests;
    int num_callbacks;
    struct {
        BlockDriverCompletionFunc *cb;
        void *opaque;
        QEMUIOVector *free_qiov;
    } callbacks[];
} MultiwriteCB;

static void multiwrite_user_cb(MultiwriteCB *mcb)
{
    int i;

    for (i = 0; i < mcb->num_callbacks; i++) {
        mcb->callbacks[i].cb(mcb->callbacks[i].opaque, mcb->error);
        if (mcb->callbacks[i].free_qiov) {
            qemu_iovec_destroy(mcb->callbacks[i].free_qiov);
        }
        g_free(mcb->callbacks[i].free_qiov);
    }
}

static void multiwrite_cb(void *opaque, int ret)
{
    MultiwriteCB *mcb = opaque;

    trace_multiwrite_cb(mcb, ret);

    if (ret < 0 && !mcb->error) {
        mcb->error = ret;
    }

    mcb->num_requests--;
    if (mcb->num_requests == 0) {
        multiwrite_user_cb(mcb);
        g_free(mcb);
    }
}

static int multiwrite_req_compare(const void *a, const void *b)
{
    const BlockRequest *req1 = a, *req2 = b;

    /*
     * Note that we can't simply subtract req2->sector from req1->sector
     * here as that could overflow the return value.
     */
    if (req1->sector > req2->sector) {
        return 1;
    } else if (req1->sector < req2->sector) {
        return -1;
    } else {
        return 0;
    }
}

/*
 * Takes a bunch of requests and tries to merge them. Returns the number of
 * requests that remain after merging.
 */
static int multiwrite_merge(BlockDriverState *bs, BlockRequest *reqs,
    int num_reqs, MultiwriteCB *mcb)
{
    int i, outidx;

    // Sort requests by start sector
    qsort(reqs, num_reqs, sizeof(*reqs), &multiwrite_req_compare);

    // Check if adjacent requests touch the same clusters. If so, combine them,
    // filling up gaps with zero sectors.
    outidx = 0;
    for (i = 1; i < num_reqs; i++) {
        int merge = 0;
        int64_t oldreq_last = reqs[outidx].sector + reqs[outidx].nb_sectors;

        // Handle exactly sequential writes and overlapping writes.
        if (reqs[i].sector <= oldreq_last) {
            merge = 1;
        }

        if (reqs[outidx].qiov->niov + reqs[i].qiov->niov + 1 > IOV_MAX) {
            merge = 0;
        }

        if (merge) {
            size_t size;
            QEMUIOVector *qiov = g_malloc0(sizeof(*qiov));
            qemu_iovec_init(qiov,
                reqs[outidx].qiov->niov + reqs[i].qiov->niov + 1);

            // Add the first request to the merged one. If the requests are
            // overlapping, drop the last sectors of the first request.
            size = (reqs[i].sector - reqs[outidx].sector) << 9;
            qemu_iovec_concat(qiov, reqs[outidx].qiov, 0, size);

            // We should need to add any zeros between the two requests
            assert (reqs[i].sector <= oldreq_last);

            // Add the second request
            qemu_iovec_concat(qiov, reqs[i].qiov, 0, reqs[i].qiov->size);

            reqs[outidx].nb_sectors = qiov->size >> 9;
            reqs[outidx].qiov = qiov;

            mcb->callbacks[i].free_qiov = reqs[outidx].qiov;
        } else {
            outidx++;
            reqs[outidx].sector     = reqs[i].sector;
            reqs[outidx].nb_sectors = reqs[i].nb_sectors;
            reqs[outidx].qiov       = reqs[i].qiov;
        }
    }

    return outidx + 1;
}

/*
 * Submit multiple AIO write requests at once.
 *
 * On success, the function returns 0 and all requests in the reqs array have
 * been submitted. In error case this function returns -1, and any of the
 * requests may or may not be submitted yet. In particular, this means that the
 * callback will be called for some of the requests, for others it won't. The
 * caller must check the error field of the BlockRequest to wait for the right
 * callbacks (if error != 0, no callback will be called).
 *
 * The implementation may modify the contents of the reqs array, e.g. to merge
 * requests. However, the fields opaque and error are left unmodified as they
 * are used to signal failure for a single request to the caller.
 */
int bdrv_aio_multiwrite(BlockDriverState *bs, BlockRequest *reqs, int num_reqs)
{
    MultiwriteCB *mcb;
    int i;

    /* don't submit writes if we don't have a medium */
    if (bs->drv == NULL) {
        for (i = 0; i < num_reqs; i++) {
            reqs[i].error = -ENOMEDIUM;
        }
        return -1;
    }

    if (num_reqs == 0) {
        return 0;
    }

    // Create MultiwriteCB structure
    mcb = g_malloc0(sizeof(*mcb) + num_reqs * sizeof(*mcb->callbacks));
    mcb->num_requests = 0;
    mcb->num_callbacks = num_reqs;

    for (i = 0; i < num_reqs; i++) {
        mcb->callbacks[i].cb = reqs[i].cb;
        mcb->callbacks[i].opaque = reqs[i].opaque;
    }

    // Check for mergable requests
    num_reqs = multiwrite_merge(bs, reqs, num_reqs, mcb);

    trace_bdrv_aio_multiwrite(mcb, mcb->num_callbacks, num_reqs);

    /* Run the aio requests. */
    mcb->num_requests = num_reqs;
    for (i = 0; i < num_reqs; i++) {
        bdrv_co_aio_rw_vector(bs, reqs[i].sector, reqs[i].qiov,
                              reqs[i].nb_sectors, reqs[i].flags,
                              multiwrite_cb, mcb,
                              true);
    }

    return 0;
}

void bdrv_aio_cancel(BlockDriverAIOCB *acb)
{
    acb->aiocb_info->cancel(acb);
}

/**************************************************************/
/* async block device emulation */

typedef struct BlockDriverAIOCBSync {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    int ret;
    /* vector translation state */
    QEMUIOVector *qiov;
    uint8_t *bounce;
    int is_write;
} BlockDriverAIOCBSync;

static void bdrv_aio_cancel_em(BlockDriverAIOCB *blockacb)
{
    BlockDriverAIOCBSync *acb =
        container_of(blockacb, BlockDriverAIOCBSync, common);
    qemu_bh_delete(acb->bh);
    acb->bh = NULL;
    qemu_aio_release(acb);
}

static const AIOCBInfo bdrv_em_aiocb_info = {
    .aiocb_size         = sizeof(BlockDriverAIOCBSync),
    .cancel             = bdrv_aio_cancel_em,
};

static void bdrv_aio_bh_cb(void *opaque)
{
    BlockDriverAIOCBSync *acb = opaque;

    if (!acb->is_write)
        qemu_iovec_from_buf(acb->qiov, 0, acb->bounce, acb->qiov->size);
    qemu_vfree(acb->bounce);
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_bh_delete(acb->bh);
    acb->bh = NULL;
    qemu_aio_release(acb);
}

static BlockDriverAIOCB *bdrv_aio_rw_vector(BlockDriverState *bs,
                                            int64_t sector_num,
                                            QEMUIOVector *qiov,
                                            int nb_sectors,
                                            BlockDriverCompletionFunc *cb,
                                            void *opaque,
                                            int is_write)

{
    BlockDriverAIOCBSync *acb;

    acb = qemu_aio_get(&bdrv_em_aiocb_info, bs, cb, opaque);
    acb->is_write = is_write;
    acb->qiov = qiov;
    acb->bounce = qemu_blockalign(bs, qiov->size);
    acb->bh = aio_bh_new(bdrv_get_aio_context(bs), bdrv_aio_bh_cb, acb);

    if (is_write) {
        qemu_iovec_to_buf(acb->qiov, 0, acb->bounce, qiov->size);
        acb->ret = bs->drv->bdrv_write(bs, sector_num, acb->bounce, nb_sectors);
    } else {
        acb->ret = bs->drv->bdrv_read(bs, sector_num, acb->bounce, nb_sectors);
    }

    qemu_bh_schedule(acb->bh);

    return &acb->common;
}

static BlockDriverAIOCB *bdrv_aio_readv_em(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_rw_vector(bs, sector_num, qiov, nb_sectors, cb, opaque, 0);
}

static BlockDriverAIOCB *bdrv_aio_writev_em(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_rw_vector(bs, sector_num, qiov, nb_sectors, cb, opaque, 1);
}


typedef struct BlockDriverAIOCBCoroutine {
    BlockDriverAIOCB common;
    BlockRequest req;
    bool is_write;
    bool *done;
    QEMUBH* bh;
} BlockDriverAIOCBCoroutine;

static void bdrv_aio_co_cancel_em(BlockDriverAIOCB *blockacb)
{
    AioContext *aio_context = bdrv_get_aio_context(blockacb->bs);
    BlockDriverAIOCBCoroutine *acb =
        container_of(blockacb, BlockDriverAIOCBCoroutine, common);
    bool done = false;

    acb->done = &done;
    while (!done) {
        aio_poll(aio_context, true);
    }
}

static const AIOCBInfo bdrv_em_co_aiocb_info = {
    .aiocb_size         = sizeof(BlockDriverAIOCBCoroutine),
    .cancel             = bdrv_aio_co_cancel_em,
};

static void bdrv_co_em_bh(void *opaque)
{
    BlockDriverAIOCBCoroutine *acb = opaque;

    acb->common.cb(acb->common.opaque, acb->req.error);

    if (acb->done) {
        *acb->done = true;
    }

    qemu_bh_delete(acb->bh);
    qemu_aio_release(acb);
}

/* Invoke bdrv_co_do_readv/bdrv_co_do_writev */
static void coroutine_fn bdrv_co_do_rw(void *opaque)
{
    BlockDriverAIOCBCoroutine *acb = opaque;
    BlockDriverState *bs = acb->common.bs;

    if (!acb->is_write) {
        acb->req.error = bdrv_co_do_readv(bs, acb->req.sector,
            acb->req.nb_sectors, acb->req.qiov, acb->req.flags);
    } else {
        acb->req.error = bdrv_co_do_writev(bs, acb->req.sector,
            acb->req.nb_sectors, acb->req.qiov, acb->req.flags);
    }

    acb->bh = aio_bh_new(bdrv_get_aio_context(bs), bdrv_co_em_bh, acb);
    qemu_bh_schedule(acb->bh);
}

static BlockDriverAIOCB *bdrv_co_aio_rw_vector(BlockDriverState *bs,
                                               int64_t sector_num,
                                               QEMUIOVector *qiov,
                                               int nb_sectors,
                                               BdrvRequestFlags flags,
                                               BlockDriverCompletionFunc *cb,
                                               void *opaque,
                                               bool is_write)
{
    Coroutine *co;
    BlockDriverAIOCBCoroutine *acb;

    acb = qemu_aio_get(&bdrv_em_co_aiocb_info, bs, cb, opaque);
    acb->req.sector = sector_num;
    acb->req.nb_sectors = nb_sectors;
    acb->req.qiov = qiov;
    acb->req.flags = flags;
    acb->is_write = is_write;
    acb->done = NULL;

    co = qemu_coroutine_create(bdrv_co_do_rw);
    qemu_coroutine_enter(co, acb);

    return &acb->common;
}

static void coroutine_fn bdrv_aio_flush_co_entry(void *opaque)
{
    BlockDriverAIOCBCoroutine *acb = opaque;
    BlockDriverState *bs = acb->common.bs;

    acb->req.error = bdrv_co_flush(bs);
    acb->bh = aio_bh_new(bdrv_get_aio_context(bs), bdrv_co_em_bh, acb);
    qemu_bh_schedule(acb->bh);
}

BlockDriverAIOCB *bdrv_aio_flush(BlockDriverState *bs,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_flush(bs, opaque);

    Coroutine *co;
    BlockDriverAIOCBCoroutine *acb;

    acb = qemu_aio_get(&bdrv_em_co_aiocb_info, bs, cb, opaque);
    acb->done = NULL;

    co = qemu_coroutine_create(bdrv_aio_flush_co_entry);
    qemu_coroutine_enter(co, acb);

    return &acb->common;
}

static void coroutine_fn bdrv_aio_discard_co_entry(void *opaque)
{
    BlockDriverAIOCBCoroutine *acb = opaque;
    BlockDriverState *bs = acb->common.bs;

    acb->req.error = bdrv_co_discard(bs, acb->req.sector, acb->req.nb_sectors);
    acb->bh = aio_bh_new(bdrv_get_aio_context(bs), bdrv_co_em_bh, acb);
    qemu_bh_schedule(acb->bh);
}

BlockDriverAIOCB *bdrv_aio_discard(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    Coroutine *co;
    BlockDriverAIOCBCoroutine *acb;

    trace_bdrv_aio_discard(bs, sector_num, nb_sectors, opaque);

    acb = qemu_aio_get(&bdrv_em_co_aiocb_info, bs, cb, opaque);
    acb->req.sector = sector_num;
    acb->req.nb_sectors = nb_sectors;
    acb->done = NULL;
    co = qemu_coroutine_create(bdrv_aio_discard_co_entry);
    qemu_coroutine_enter(co, acb);

    return &acb->common;
}

void bdrv_init(void)
{
    module_call_init(MODULE_INIT_BLOCK);
}

void bdrv_init_with_whitelist(void)
{
    use_bdrv_whitelist = 1;
    bdrv_init();
}

void *qemu_aio_get(const AIOCBInfo *aiocb_info, BlockDriverState *bs,
                   BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriverAIOCB *acb;

    acb = g_slice_alloc(aiocb_info->aiocb_size);
    acb->aiocb_info = aiocb_info;
    acb->bs = bs;
    acb->cb = cb;
    acb->opaque = opaque;
    return acb;
}

void qemu_aio_release(void *p)
{
    BlockDriverAIOCB *acb = p;
    g_slice_free1(acb->aiocb_info->aiocb_size, acb);
}

/**************************************************************/
/* Coroutine block device emulation */

typedef struct CoroutineIOCompletion {
    Coroutine *coroutine;
    int ret;
} CoroutineIOCompletion;

static void bdrv_co_io_em_complete(void *opaque, int ret)
{
    CoroutineIOCompletion *co = opaque;

    co->ret = ret;
    qemu_coroutine_enter(co->coroutine, NULL);
}

static int coroutine_fn bdrv_co_io_em(BlockDriverState *bs, int64_t sector_num,
                                      int nb_sectors, QEMUIOVector *iov,
                                      bool is_write)
{
    CoroutineIOCompletion co = {
        .coroutine = qemu_coroutine_self(),
    };
    BlockDriverAIOCB *acb;

    if (is_write) {
        acb = bs->drv->bdrv_aio_writev(bs, sector_num, iov, nb_sectors,
                                       bdrv_co_io_em_complete, &co);
    } else {
        acb = bs->drv->bdrv_aio_readv(bs, sector_num, iov, nb_sectors,
                                      bdrv_co_io_em_complete, &co);
    }

    trace_bdrv_co_io_em(bs, sector_num, nb_sectors, is_write, acb);
    if (!acb) {
        return -EIO;
    }
    qemu_coroutine_yield();

    return co.ret;
}

static int coroutine_fn bdrv_co_readv_em(BlockDriverState *bs,
                                         int64_t sector_num, int nb_sectors,
                                         QEMUIOVector *iov)
{
    return bdrv_co_io_em(bs, sector_num, nb_sectors, iov, false);
}

static int coroutine_fn bdrv_co_writev_em(BlockDriverState *bs,
                                         int64_t sector_num, int nb_sectors,
                                         QEMUIOVector *iov)
{
    return bdrv_co_io_em(bs, sector_num, nb_sectors, iov, true);
}

static void coroutine_fn bdrv_flush_co_entry(void *opaque)
{
    RwCo *rwco = opaque;

    rwco->ret = bdrv_co_flush(rwco->bs);
}

int coroutine_fn bdrv_co_flush(BlockDriverState *bs)
{
    int ret;

    if (!bs || !bdrv_is_inserted(bs) || bdrv_is_read_only(bs)) {
        return 0;
    }

    /* Write back cached data to the OS even with cache=unsafe */
    BLKDBG_EVENT(bs->file, BLKDBG_FLUSH_TO_OS);
    if (bs->drv->bdrv_co_flush_to_os) {
        ret = bs->drv->bdrv_co_flush_to_os(bs);
        if (ret < 0) {
            return ret;
        }
    }

    /* But don't actually force it to the disk with cache=unsafe */
    if (bs->open_flags & BDRV_O_NO_FLUSH) {
        goto flush_parent;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_FLUSH_TO_DISK);
    if (bs->drv->bdrv_co_flush_to_disk) {
        ret = bs->drv->bdrv_co_flush_to_disk(bs);
    } else if (bs->drv->bdrv_aio_flush) {
        BlockDriverAIOCB *acb;
        CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
        };

        acb = bs->drv->bdrv_aio_flush(bs, bdrv_co_io_em_complete, &co);
        if (acb == NULL) {
            ret = -EIO;
        } else {
            qemu_coroutine_yield();
            ret = co.ret;
        }
    } else {
        /*
         * Some block drivers always operate in either writethrough or unsafe
         * mode and don't support bdrv_flush therefore. Usually qemu doesn't
         * know how the server works (because the behaviour is hardcoded or
         * depends on server-side configuration), so we can't ensure that
         * everything is safe on disk. Returning an error doesn't work because
         * that would break guests even if the server operates in writethrough
         * mode.
         *
         * Let's hope the user knows what he's doing.
         */
        ret = 0;
    }
    if (ret < 0) {
        return ret;
    }

    /* Now flush the underlying protocol.  It will also have BDRV_O_NO_FLUSH
     * in the case of cache=unsafe, so there are no useless flushes.
     */
flush_parent:
    return bdrv_co_flush(bs->file);
}

void bdrv_invalidate_cache(BlockDriverState *bs, Error **errp)
{
    Error *local_err = NULL;
    int ret;

    if (!bs->drv)  {
        return;
    }

    if (bs->drv->bdrv_invalidate_cache) {
        bs->drv->bdrv_invalidate_cache(bs, &local_err);
    } else if (bs->file) {
        bdrv_invalidate_cache(bs->file, &local_err);
    }
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    ret = refresh_total_sectors(bs, bs->total_sectors);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not refresh total sector count");
        return;
    }
}

void bdrv_invalidate_cache_all(Error **errp)
{
    BlockDriverState *bs;
    Error *local_err = NULL;

    QTAILQ_FOREACH(bs, &bdrv_states, device_list) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        aio_context_acquire(aio_context);
        bdrv_invalidate_cache(bs, &local_err);
        aio_context_release(aio_context);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

void bdrv_clear_incoming_migration_all(void)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, device_list) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        aio_context_acquire(aio_context);
        bs->open_flags = bs->open_flags & ~(BDRV_O_INCOMING);
        aio_context_release(aio_context);
    }
}

int bdrv_flush(BlockDriverState *bs)
{
    Coroutine *co;
    RwCo rwco = {
        .bs = bs,
        .ret = NOT_DONE,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_flush_co_entry(&rwco);
    } else {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        co = qemu_coroutine_create(bdrv_flush_co_entry);
        qemu_coroutine_enter(co, &rwco);
        while (rwco.ret == NOT_DONE) {
            aio_poll(aio_context, true);
        }
    }

    return rwco.ret;
}

typedef struct DiscardCo {
    BlockDriverState *bs;
    int64_t sector_num;
    int nb_sectors;
    int ret;
} DiscardCo;
static void coroutine_fn bdrv_discard_co_entry(void *opaque)
{
    DiscardCo *rwco = opaque;

    rwco->ret = bdrv_co_discard(rwco->bs, rwco->sector_num, rwco->nb_sectors);
}

/* if no limit is specified in the BlockLimits use a default
 * of 32768 512-byte sectors (16 MiB) per request.
 */
#define MAX_DISCARD_DEFAULT 32768

int coroutine_fn bdrv_co_discard(BlockDriverState *bs, int64_t sector_num,
                                 int nb_sectors)
{
    int max_discard;

    if (!bs->drv) {
        return -ENOMEDIUM;
    } else if (bdrv_check_request(bs, sector_num, nb_sectors)) {
        return -EIO;
    } else if (bs->read_only) {
        return -EROFS;
    }

    bdrv_reset_dirty(bs, sector_num, nb_sectors);

    /* Do nothing if disabled.  */
    if (!(bs->open_flags & BDRV_O_UNMAP)) {
        return 0;
    }

    if (!bs->drv->bdrv_co_discard && !bs->drv->bdrv_aio_discard) {
        return 0;
    }

    max_discard = bs->bl.max_discard ?  bs->bl.max_discard : MAX_DISCARD_DEFAULT;
    while (nb_sectors > 0) {
        int ret;
        int num = nb_sectors;

        /* align request */
        if (bs->bl.discard_alignment &&
            num >= bs->bl.discard_alignment &&
            sector_num % bs->bl.discard_alignment) {
            if (num > bs->bl.discard_alignment) {
                num = bs->bl.discard_alignment;
            }
            num -= sector_num % bs->bl.discard_alignment;
        }

        /* limit request size */
        if (num > max_discard) {
            num = max_discard;
        }

        if (bs->drv->bdrv_co_discard) {
            ret = bs->drv->bdrv_co_discard(bs, sector_num, num);
        } else {
            BlockDriverAIOCB *acb;
            CoroutineIOCompletion co = {
                .coroutine = qemu_coroutine_self(),
            };

            acb = bs->drv->bdrv_aio_discard(bs, sector_num, nb_sectors,
                                            bdrv_co_io_em_complete, &co);
            if (acb == NULL) {
                return -EIO;
            } else {
                qemu_coroutine_yield();
                ret = co.ret;
            }
        }
        if (ret && ret != -ENOTSUP) {
            return ret;
        }

        sector_num += num;
        nb_sectors -= num;
    }
    return 0;
}

int bdrv_discard(BlockDriverState *bs, int64_t sector_num, int nb_sectors)
{
    Coroutine *co;
    DiscardCo rwco = {
        .bs = bs,
        .sector_num = sector_num,
        .nb_sectors = nb_sectors,
        .ret = NOT_DONE,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_discard_co_entry(&rwco);
    } else {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        co = qemu_coroutine_create(bdrv_discard_co_entry);
        qemu_coroutine_enter(co, &rwco);
        while (rwco.ret == NOT_DONE) {
            aio_poll(aio_context, true);
        }
    }

    return rwco.ret;
}

/**************************************************************/
/* removable device support */

/**
 * Return TRUE if the media is present
 */
int bdrv_is_inserted(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;

    if (!drv)
        return 0;
    if (!drv->bdrv_is_inserted)
        return 1;
    return drv->bdrv_is_inserted(bs);
}

/**
 * Return whether the media changed since the last call to this
 * function, or -ENOTSUP if we don't know.  Most drivers don't know.
 */
int bdrv_media_changed(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;

    if (drv && drv->bdrv_media_changed) {
        return drv->bdrv_media_changed(bs);
    }
    return -ENOTSUP;
}

/**
 * If eject_flag is TRUE, eject the media. Otherwise, close the tray
 */
void bdrv_eject(BlockDriverState *bs, bool eject_flag)
{
    BlockDriver *drv = bs->drv;

    if (drv && drv->bdrv_eject) {
        drv->bdrv_eject(bs, eject_flag);
    }

    if (bs->device_name[0] != '\0') {
        qapi_event_send_device_tray_moved(bdrv_get_device_name(bs),
                                          eject_flag, &error_abort);
    }
}

/**
 * Lock or unlock the media (if it is locked, the user won't be able
 * to eject it manually).
 */
void bdrv_lock_medium(BlockDriverState *bs, bool locked)
{
    BlockDriver *drv = bs->drv;

    trace_bdrv_lock_medium(bs, locked);

    if (drv && drv->bdrv_lock_medium) {
        drv->bdrv_lock_medium(bs, locked);
    }
}

/* needed for generic scsi interface */

int bdrv_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
    BlockDriver *drv = bs->drv;

    if (drv && drv->bdrv_ioctl)
        return drv->bdrv_ioctl(bs, req, buf);
    return -ENOTSUP;
}

BlockDriverAIOCB *bdrv_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriver *drv = bs->drv;

    if (drv && drv->bdrv_aio_ioctl)
        return drv->bdrv_aio_ioctl(bs, req, buf, cb, opaque);
    return NULL;
}

void bdrv_set_guest_block_size(BlockDriverState *bs, int align)
{
    bs->guest_block_size = align;
}

void *qemu_blockalign(BlockDriverState *bs, size_t size)
{
    return qemu_memalign(bdrv_opt_mem_align(bs), size);
}

/*
 * Check if all memory in this vector is sector aligned.
 */
bool bdrv_qiov_is_aligned(BlockDriverState *bs, QEMUIOVector *qiov)
{
    int i;
    size_t alignment = bdrv_opt_mem_align(bs);

    for (i = 0; i < qiov->niov; i++) {
        if ((uintptr_t) qiov->iov[i].iov_base % alignment) {
            return false;
        }
        if (qiov->iov[i].iov_len % alignment) {
            return false;
        }
    }

    return true;
}

BdrvDirtyBitmap *bdrv_create_dirty_bitmap(BlockDriverState *bs, int granularity,
                                          Error **errp)
{
    int64_t bitmap_size;
    BdrvDirtyBitmap *bitmap;

    assert((granularity & (granularity - 1)) == 0);

    granularity >>= BDRV_SECTOR_BITS;
    assert(granularity);
    bitmap_size = bdrv_nb_sectors(bs);
    if (bitmap_size < 0) {
        error_setg_errno(errp, -bitmap_size, "could not get length of device");
        errno = -bitmap_size;
        return NULL;
    }
    bitmap = g_malloc0(sizeof(BdrvDirtyBitmap));
    bitmap->bitmap = hbitmap_alloc(bitmap_size, ffs(granularity) - 1);
    QLIST_INSERT_HEAD(&bs->dirty_bitmaps, bitmap, list);
    return bitmap;
}

void bdrv_release_dirty_bitmap(BlockDriverState *bs, BdrvDirtyBitmap *bitmap)
{
    BdrvDirtyBitmap *bm, *next;
    QLIST_FOREACH_SAFE(bm, &bs->dirty_bitmaps, list, next) {
        if (bm == bitmap) {
            QLIST_REMOVE(bitmap, list);
            hbitmap_free(bitmap->bitmap);
            g_free(bitmap);
            return;
        }
    }
}

BlockDirtyInfoList *bdrv_query_dirty_bitmaps(BlockDriverState *bs)
{
    BdrvDirtyBitmap *bm;
    BlockDirtyInfoList *list = NULL;
    BlockDirtyInfoList **plist = &list;

    QLIST_FOREACH(bm, &bs->dirty_bitmaps, list) {
        BlockDirtyInfo *info = g_malloc0(sizeof(BlockDirtyInfo));
        BlockDirtyInfoList *entry = g_malloc0(sizeof(BlockDirtyInfoList));
        info->count = bdrv_get_dirty_count(bs, bm);
        info->granularity =
            ((int64_t) BDRV_SECTOR_SIZE << hbitmap_granularity(bm->bitmap));
        entry->value = info;
        *plist = entry;
        plist = &entry->next;
    }

    return list;
}

int bdrv_get_dirty(BlockDriverState *bs, BdrvDirtyBitmap *bitmap, int64_t sector)
{
    if (bitmap) {
        return hbitmap_get(bitmap->bitmap, sector);
    } else {
        return 0;
    }
}

void bdrv_dirty_iter_init(BlockDriverState *bs,
                          BdrvDirtyBitmap *bitmap, HBitmapIter *hbi)
{
    hbitmap_iter_init(hbi, bitmap->bitmap, 0);
}

void bdrv_set_dirty(BlockDriverState *bs, int64_t cur_sector,
                    int nr_sectors)
{
    BdrvDirtyBitmap *bitmap;
    QLIST_FOREACH(bitmap, &bs->dirty_bitmaps, list) {
        hbitmap_set(bitmap->bitmap, cur_sector, nr_sectors);
    }
}

void bdrv_reset_dirty(BlockDriverState *bs, int64_t cur_sector, int nr_sectors)
{
    BdrvDirtyBitmap *bitmap;
    QLIST_FOREACH(bitmap, &bs->dirty_bitmaps, list) {
        hbitmap_reset(bitmap->bitmap, cur_sector, nr_sectors);
    }
}

int64_t bdrv_get_dirty_count(BlockDriverState *bs, BdrvDirtyBitmap *bitmap)
{
    return hbitmap_count(bitmap->bitmap);
}

/* Get a reference to bs */
void bdrv_ref(BlockDriverState *bs)
{
    bs->refcnt++;
}

/* Release a previously grabbed reference to bs.
 * If after releasing, reference count is zero, the BlockDriverState is
 * deleted. */
void bdrv_unref(BlockDriverState *bs)
{
    assert(bs->refcnt > 0);
    if (--bs->refcnt == 0) {
        bdrv_delete(bs);
    }
}

struct BdrvOpBlocker {
    Error *reason;
    QLIST_ENTRY(BdrvOpBlocker) list;
};

bool bdrv_op_is_blocked(BlockDriverState *bs, BlockOpType op, Error **errp)
{
    BdrvOpBlocker *blocker;
    assert((int) op >= 0 && op < BLOCK_OP_TYPE_MAX);
    if (!QLIST_EMPTY(&bs->op_blockers[op])) {
        blocker = QLIST_FIRST(&bs->op_blockers[op]);
        if (errp) {
            error_setg(errp, "Device '%s' is busy: %s",
                       bs->device_name, error_get_pretty(blocker->reason));
        }
        return true;
    }
    return false;
}

void bdrv_op_block(BlockDriverState *bs, BlockOpType op, Error *reason)
{
    BdrvOpBlocker *blocker;
    assert((int) op >= 0 && op < BLOCK_OP_TYPE_MAX);

    blocker = g_malloc0(sizeof(BdrvOpBlocker));
    blocker->reason = reason;
    QLIST_INSERT_HEAD(&bs->op_blockers[op], blocker, list);
}

void bdrv_op_unblock(BlockDriverState *bs, BlockOpType op, Error *reason)
{
    BdrvOpBlocker *blocker, *next;
    assert((int) op >= 0 && op < BLOCK_OP_TYPE_MAX);
    QLIST_FOREACH_SAFE(blocker, &bs->op_blockers[op], list, next) {
        if (blocker->reason == reason) {
            QLIST_REMOVE(blocker, list);
            g_free(blocker);
        }
    }
}

void bdrv_op_block_all(BlockDriverState *bs, Error *reason)
{
    int i;
    for (i = 0; i < BLOCK_OP_TYPE_MAX; i++) {
        bdrv_op_block(bs, i, reason);
    }
}

void bdrv_op_unblock_all(BlockDriverState *bs, Error *reason)
{
    int i;
    for (i = 0; i < BLOCK_OP_TYPE_MAX; i++) {
        bdrv_op_unblock(bs, i, reason);
    }
}

bool bdrv_op_blocker_is_empty(BlockDriverState *bs)
{
    int i;

    for (i = 0; i < BLOCK_OP_TYPE_MAX; i++) {
        if (!QLIST_EMPTY(&bs->op_blockers[i])) {
            return false;
        }
    }
    return true;
}

void bdrv_iostatus_enable(BlockDriverState *bs)
{
    bs->iostatus_enabled = true;
    bs->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
}

/* The I/O status is only enabled if the drive explicitly
 * enables it _and_ the VM is configured to stop on errors */
bool bdrv_iostatus_is_enabled(const BlockDriverState *bs)
{
    return (bs->iostatus_enabled &&
           (bs->on_write_error == BLOCKDEV_ON_ERROR_ENOSPC ||
            bs->on_write_error == BLOCKDEV_ON_ERROR_STOP   ||
            bs->on_read_error == BLOCKDEV_ON_ERROR_STOP));
}

void bdrv_iostatus_disable(BlockDriverState *bs)
{
    bs->iostatus_enabled = false;
}

void bdrv_iostatus_reset(BlockDriverState *bs)
{
    if (bdrv_iostatus_is_enabled(bs)) {
        bs->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
        if (bs->job) {
            block_job_iostatus_reset(bs->job);
        }
    }
}

void bdrv_iostatus_set_err(BlockDriverState *bs, int error)
{
    assert(bdrv_iostatus_is_enabled(bs));
    if (bs->iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
        bs->iostatus = error == ENOSPC ? BLOCK_DEVICE_IO_STATUS_NOSPACE :
                                         BLOCK_DEVICE_IO_STATUS_FAILED;
    }
}

void
bdrv_acct_start(BlockDriverState *bs, BlockAcctCookie *cookie, int64_t bytes,
        enum BlockAcctType type)
{
    assert(type < BDRV_MAX_IOTYPE);

    cookie->bytes = bytes;
    cookie->start_time_ns = get_clock();
    cookie->type = type;
}

void
bdrv_acct_done(BlockDriverState *bs, BlockAcctCookie *cookie)
{
    assert(cookie->type < BDRV_MAX_IOTYPE);

    bs->nr_bytes[cookie->type] += cookie->bytes;
    bs->nr_ops[cookie->type]++;
    bs->total_time_ns[cookie->type] += get_clock() - cookie->start_time_ns;
}

void bdrv_img_create(const char *filename, const char *fmt,
                     const char *base_filename, const char *base_fmt,
                     char *options, uint64_t img_size, int flags,
                     Error **errp, bool quiet)
{
    QemuOptsList *create_opts = NULL;
    QemuOpts *opts = NULL;
    const char *backing_fmt, *backing_file;
    int64_t size;
    BlockDriver *drv, *proto_drv;
    BlockDriver *backing_drv = NULL;
    Error *local_err = NULL;
    int ret = 0;

    /* Find driver and parse its options */
    drv = bdrv_find_format(fmt);
    if (!drv) {
        error_setg(errp, "Unknown file format '%s'", fmt);
        return;
    }

    proto_drv = bdrv_find_protocol(filename, true);
    if (!proto_drv) {
        error_setg(errp, "Unknown protocol '%s'", filename);
        return;
    }

    create_opts = qemu_opts_append(create_opts, drv->create_opts);
    create_opts = qemu_opts_append(create_opts, proto_drv->create_opts);

    /* Create parameter list with default values */
    opts = qemu_opts_create(create_opts, NULL, 0, &error_abort);
    qemu_opt_set_number(opts, BLOCK_OPT_SIZE, img_size);

    /* Parse -o options */
    if (options) {
        if (qemu_opts_do_parse(opts, options, NULL) != 0) {
            error_setg(errp, "Invalid options for file format '%s'", fmt);
            goto out;
        }
    }

    if (base_filename) {
        if (qemu_opt_set(opts, BLOCK_OPT_BACKING_FILE, base_filename)) {
            error_setg(errp, "Backing file not supported for file format '%s'",
                       fmt);
            goto out;
        }
    }

    if (base_fmt) {
        if (qemu_opt_set(opts, BLOCK_OPT_BACKING_FMT, base_fmt)) {
            error_setg(errp, "Backing file format not supported for file "
                             "format '%s'", fmt);
            goto out;
        }
    }

    backing_file = qemu_opt_get(opts, BLOCK_OPT_BACKING_FILE);
    if (backing_file) {
        if (!strcmp(filename, backing_file)) {
            error_setg(errp, "Error: Trying to create an image with the "
                             "same filename as the backing file");
            goto out;
        }
    }

    backing_fmt = qemu_opt_get(opts, BLOCK_OPT_BACKING_FMT);
    if (backing_fmt) {
        backing_drv = bdrv_find_format(backing_fmt);
        if (!backing_drv) {
            error_setg(errp, "Unknown backing file format '%s'",
                       backing_fmt);
            goto out;
        }
    }

    // The size for the image must always be specified, with one exception:
    // If we are using a backing file, we can obtain the size from there
    size = qemu_opt_get_size(opts, BLOCK_OPT_SIZE, 0);
    if (size == -1) {
        if (backing_file) {
            BlockDriverState *bs;
            int64_t size;
            int back_flags;

            /* backing files always opened read-only */
            back_flags =
                flags & ~(BDRV_O_RDWR | BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING);

            bs = NULL;
            ret = bdrv_open(&bs, backing_file, NULL, NULL, back_flags,
                            backing_drv, &local_err);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "Could not open '%s': %s",
                                 backing_file,
                                 error_get_pretty(local_err));
                error_free(local_err);
                local_err = NULL;
                goto out;
            }
            size = bdrv_getlength(bs);
            if (size < 0) {
                error_setg_errno(errp, -size, "Could not get size of '%s'",
                                 backing_file);
                bdrv_unref(bs);
                goto out;
            }

            qemu_opt_set_number(opts, BLOCK_OPT_SIZE, size);

            bdrv_unref(bs);
        } else {
            error_setg(errp, "Image creation needs a size parameter");
            goto out;
        }
    }

    if (!quiet) {
        printf("Formatting '%s', fmt=%s ", filename, fmt);
        qemu_opts_print(opts);
        puts("");
    }

    ret = bdrv_create(drv, filename, opts, &local_err);

    if (ret == -EFBIG) {
        /* This is generally a better message than whatever the driver would
         * deliver (especially because of the cluster_size_hint), since that
         * is most probably not much different from "image too large". */
        const char *cluster_size_hint = "";
        if (qemu_opt_get_size(opts, BLOCK_OPT_CLUSTER_SIZE, 0)) {
            cluster_size_hint = " (try using a larger cluster size)";
        }
        error_setg(errp, "The image size is too large for file format '%s'"
                   "%s", fmt, cluster_size_hint);
        error_free(local_err);
        local_err = NULL;
    }

out:
    qemu_opts_del(opts);
    qemu_opts_free(create_opts);
    if (local_err) {
        error_propagate(errp, local_err);
    }
}

AioContext *bdrv_get_aio_context(BlockDriverState *bs)
{
    return bs->aio_context;
}

void bdrv_detach_aio_context(BlockDriverState *bs)
{
    if (!bs->drv) {
        return;
    }

    if (bs->io_limits_enabled) {
        throttle_detach_aio_context(&bs->throttle_state);
    }
    if (bs->drv->bdrv_detach_aio_context) {
        bs->drv->bdrv_detach_aio_context(bs);
    }
    if (bs->file) {
        bdrv_detach_aio_context(bs->file);
    }
    if (bs->backing_hd) {
        bdrv_detach_aio_context(bs->backing_hd);
    }

    bs->aio_context = NULL;
}

void bdrv_attach_aio_context(BlockDriverState *bs,
                             AioContext *new_context)
{
    if (!bs->drv) {
        return;
    }

    bs->aio_context = new_context;

    if (bs->backing_hd) {
        bdrv_attach_aio_context(bs->backing_hd, new_context);
    }
    if (bs->file) {
        bdrv_attach_aio_context(bs->file, new_context);
    }
    if (bs->drv->bdrv_attach_aio_context) {
        bs->drv->bdrv_attach_aio_context(bs, new_context);
    }
    if (bs->io_limits_enabled) {
        throttle_attach_aio_context(&bs->throttle_state, new_context);
    }
}

void bdrv_set_aio_context(BlockDriverState *bs, AioContext *new_context)
{
    bdrv_drain_all(); /* ensure there are no in-flight requests */

    bdrv_detach_aio_context(bs);

    /* This function executes in the old AioContext so acquire the new one in
     * case it runs in a different thread.
     */
    aio_context_acquire(new_context);
    bdrv_attach_aio_context(bs, new_context);
    aio_context_release(new_context);
}

void bdrv_add_before_write_notifier(BlockDriverState *bs,
                                    NotifierWithReturn *notifier)
{
    notifier_with_return_list_add(&bs->before_write_notifiers, notifier);
}

int bdrv_amend_options(BlockDriverState *bs, QemuOpts *opts)
{
    if (!bs->drv->bdrv_amend_options) {
        return -ENOTSUP;
    }
    return bs->drv->bdrv_amend_options(bs, opts);
}

/* This function will be called by the bdrv_recurse_is_first_non_filter method
 * of block filter and by bdrv_is_first_non_filter.
 * It is used to test if the given bs is the candidate or recurse more in the
 * node graph.
 */
bool bdrv_recurse_is_first_non_filter(BlockDriverState *bs,
                                      BlockDriverState *candidate)
{
    /* return false if basic checks fails */
    if (!bs || !bs->drv) {
        return false;
    }

    /* the code reached a non block filter driver -> check if the bs is
     * the same as the candidate. It's the recursion termination condition.
     */
    if (!bs->drv->is_filter) {
        return bs == candidate;
    }
    /* Down this path the driver is a block filter driver */

    /* If the block filter recursion method is defined use it to recurse down
     * the node graph.
     */
    if (bs->drv->bdrv_recurse_is_first_non_filter) {
        return bs->drv->bdrv_recurse_is_first_non_filter(bs, candidate);
    }

    /* the driver is a block filter but don't allow to recurse -> return false
     */
    return false;
}

/* This function checks if the candidate is the first non filter bs down it's
 * bs chain. Since we don't have pointers to parents it explore all bs chains
 * from the top. Some filters can choose not to pass down the recursion.
 */
bool bdrv_is_first_non_filter(BlockDriverState *candidate)
{
    BlockDriverState *bs;

    /* walk down the bs forest recursively */
    QTAILQ_FOREACH(bs, &bdrv_states, device_list) {
        bool perm;

        /* try to recurse in this top level bs */
        perm = bdrv_recurse_is_first_non_filter(bs, candidate);

        /* candidate is the first non filter */
        if (perm) {
            return true;
        }
    }

    return false;
}

BlockDriverState *check_to_replace_node(const char *node_name, Error **errp)
{
    BlockDriverState *to_replace_bs = bdrv_find_node(node_name);
    if (!to_replace_bs) {
        error_setg(errp, "Node name '%s' not found", node_name);
        return NULL;
    }

    if (bdrv_op_is_blocked(to_replace_bs, BLOCK_OP_TYPE_REPLACE, errp)) {
        return NULL;
    }

    /* We don't want arbitrary node of the BDS chain to be replaced only the top
     * most non filter in order to prevent data corruption.
     * Another benefit is that this tests exclude backing files which are
     * blocked by the backing blockers.
     */
    if (!bdrv_is_first_non_filter(to_replace_bs)) {
        error_setg(errp, "Only top most non filter can be replaced");
        return NULL;
    }

    return to_replace_bs;
}

void bdrv_io_plug(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    if (drv && drv->bdrv_io_plug) {
        drv->bdrv_io_plug(bs);
    } else if (bs->file) {
        bdrv_io_plug(bs->file);
    }
}

void bdrv_io_unplug(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    if (drv && drv->bdrv_io_unplug) {
        drv->bdrv_io_unplug(bs);
    } else if (bs->file) {
        bdrv_io_unplug(bs->file);
    }
}

void bdrv_flush_io_queue(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    if (drv && drv->bdrv_flush_io_queue) {
        drv->bdrv_flush_io_queue(bs);
    } else if (bs->file) {
        bdrv_flush_io_queue(bs->file);
    }
}
