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
#include "monitor.h"
#include "block_int.h"
#include "module.h"
#include "qjson.h"
#include "qemu-coroutine.h"
#include "qmp-commands.h"
#include "qemu-timer.h"

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

#define NOT_DONE 0x7fffffff /* used while emulated sync operation in progress */

typedef enum {
    BDRV_REQ_COPY_ON_READ = 0x1,
    BDRV_REQ_ZERO_WRITE   = 0x2,
} BdrvRequestFlags;

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
static int coroutine_fn bdrv_co_do_readv(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov,
    BdrvRequestFlags flags);
static int coroutine_fn bdrv_co_do_writev(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov,
    BdrvRequestFlags flags);
static BlockDriverAIOCB *bdrv_co_aio_rw_vector(BlockDriverState *bs,
                                               int64_t sector_num,
                                               QEMUIOVector *qiov,
                                               int nb_sectors,
                                               BlockDriverCompletionFunc *cb,
                                               void *opaque,
                                               bool is_write);
static void coroutine_fn bdrv_co_do_rw(void *opaque);

static bool bdrv_exceed_bps_limits(BlockDriverState *bs, int nb_sectors,
        bool is_write, double elapsed_time, uint64_t *wait);
static bool bdrv_exceed_iops_limits(BlockDriverState *bs, bool is_write,
        double elapsed_time, uint64_t *wait);
static bool bdrv_exceed_io_limits(BlockDriverState *bs, int nb_sectors,
        bool is_write, int64_t *wait);

static QTAILQ_HEAD(, BlockDriverState) bdrv_states =
    QTAILQ_HEAD_INITIALIZER(bdrv_states);

static QLIST_HEAD(, BlockDriver) bdrv_drivers =
    QLIST_HEAD_INITIALIZER(bdrv_drivers);

/* The device to use for VM snapshots */
static BlockDriverState *bs_snapshots;

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
void bdrv_io_limits_disable(BlockDriverState *bs)
{
    bs->io_limits_enabled = false;

    while (qemu_co_queue_next(&bs->throttled_reqs));

    if (bs->block_timer) {
        qemu_del_timer(bs->block_timer);
        qemu_free_timer(bs->block_timer);
        bs->block_timer = NULL;
    }

    bs->slice_start = 0;
    bs->slice_end   = 0;
    bs->slice_time  = 0;
    memset(&bs->io_base, 0, sizeof(bs->io_base));
}

static void bdrv_block_timer(void *opaque)
{
    BlockDriverState *bs = opaque;

    qemu_co_queue_next(&bs->throttled_reqs);
}

void bdrv_io_limits_enable(BlockDriverState *bs)
{
    qemu_co_queue_init(&bs->throttled_reqs);
    bs->block_timer = qemu_new_timer_ns(vm_clock, bdrv_block_timer, bs);
    bs->slice_time  = 5 * BLOCK_IO_SLICE_TIME;
    bs->slice_start = qemu_get_clock_ns(vm_clock);
    bs->slice_end   = bs->slice_start + bs->slice_time;
    memset(&bs->io_base, 0, sizeof(bs->io_base));
    bs->io_limits_enabled = true;
}

bool bdrv_io_limits_enabled(BlockDriverState *bs)
{
    BlockIOLimit *io_limits = &bs->io_limits;
    return io_limits->bps[BLOCK_IO_LIMIT_READ]
         || io_limits->bps[BLOCK_IO_LIMIT_WRITE]
         || io_limits->bps[BLOCK_IO_LIMIT_TOTAL]
         || io_limits->iops[BLOCK_IO_LIMIT_READ]
         || io_limits->iops[BLOCK_IO_LIMIT_WRITE]
         || io_limits->iops[BLOCK_IO_LIMIT_TOTAL];
}

static void bdrv_io_limits_intercept(BlockDriverState *bs,
                                     bool is_write, int nb_sectors)
{
    int64_t wait_time = -1;

    if (!qemu_co_queue_empty(&bs->throttled_reqs)) {
        qemu_co_queue_wait(&bs->throttled_reqs);
    }

    /* In fact, we hope to keep each request's timing, in FIFO mode. The next
     * throttled requests will not be dequeued until the current request is
     * allowed to be serviced. So if the current request still exceeds the
     * limits, it will be inserted to the head. All requests followed it will
     * be still in throttled_reqs queue.
     */

    while (bdrv_exceed_io_limits(bs, nb_sectors, is_write, &wait_time)) {
        qemu_mod_timer(bs->block_timer,
                       wait_time + qemu_get_clock_ns(vm_clock));
        qemu_co_queue_wait_insert_head(&bs->throttled_reqs);
    }

    qemu_co_queue_next(&bs->throttled_reqs);
}

/* check if the path starts with "<protocol>:" */
static int path_has_protocol(const char *path)
{
#ifdef _WIN32
    if (is_windows_drive(path) ||
        is_windows_drive_prefix(path)) {
        return 0;
    }
#endif

    return strchr(path, ':') != NULL;
}

int path_is_absolute(const char *path)
{
    const char *p;
#ifdef _WIN32
    /* specific case for names like: "\\.\d:" */
    if (*path == '/' || *path == '\\')
        return 1;
#endif
    p = strchr(path, ':');
    if (p)
        p++;
    else
        p = path;
#ifdef _WIN32
    return (*p == '/' || *p == '\\');
#else
    return (*p == '/');
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
BlockDriverState *bdrv_new(const char *device_name)
{
    BlockDriverState *bs;

    bs = g_malloc0(sizeof(BlockDriverState));
    pstrcpy(bs->device_name, sizeof(bs->device_name), device_name);
    if (device_name[0] != '\0') {
        QTAILQ_INSERT_TAIL(&bdrv_states, bs, list);
    }
    bdrv_iostatus_disable(bs);
    return bs;
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

static int bdrv_is_whitelisted(BlockDriver *drv)
{
    static const char *whitelist[] = {
        CONFIG_BDRV_WHITELIST
    };
    const char **p;

    if (!whitelist[0])
        return 1;               /* no whitelist, anything goes */

    for (p = whitelist; *p; p++) {
        if (!strcmp(drv->format_name, *p)) {
            return 1;
        }
    }
    return 0;
}

BlockDriver *bdrv_find_whitelisted_format(const char *format_name)
{
    BlockDriver *drv = bdrv_find_format(format_name);
    return drv && bdrv_is_whitelisted(drv) ? drv : NULL;
}

int bdrv_create(BlockDriver *drv, const char* filename,
    QEMUOptionParameter *options)
{
    if (!drv->bdrv_create)
        return -ENOTSUP;

    return drv->bdrv_create(filename, options);
}

int bdrv_create_file(const char* filename, QEMUOptionParameter *options)
{
    BlockDriver *drv;

    drv = bdrv_find_protocol(filename);
    if (drv == NULL) {
        return -ENOENT;
    }

    return bdrv_create(drv, filename, options);
}

#ifdef _WIN32
void get_tmp_filename(char *filename, int size)
{
    char temp_dir[MAX_PATH];

    GetTempPath(MAX_PATH, temp_dir);
    GetTempFileName(temp_dir, "qem", 0, filename);
}
#else
void get_tmp_filename(char *filename, int size)
{
    int fd;
    const char *tmpdir;
    /* XXX: race condition possible */
    tmpdir = getenv("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";
    snprintf(filename, size, "%s/vl.XXXXXX", tmpdir);
    fd = mkstemp(filename);
    close(fd);
}
#endif

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

BlockDriver *bdrv_find_protocol(const char *filename)
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

    if (!path_has_protocol(filename)) {
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

static int find_image_format(const char *filename, BlockDriver **pdrv)
{
    int ret, score, score_max;
    BlockDriver *drv1, *drv;
    uint8_t buf[2048];
    BlockDriverState *bs;

    ret = bdrv_file_open(&bs, filename, 0);
    if (ret < 0) {
        *pdrv = NULL;
        return ret;
    }

    /* Return the raw BlockDriver * to scsi-generic devices or empty drives */
    if (bs->sg || !bdrv_is_inserted(bs)) {
        bdrv_delete(bs);
        drv = bdrv_find_format("raw");
        if (!drv) {
            ret = -ENOENT;
        }
        *pdrv = drv;
        return ret;
    }

    ret = bdrv_pread(bs, 0, buf, sizeof(buf));
    bdrv_delete(bs);
    if (ret < 0) {
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
        ret = -ENOENT;
    }
    *pdrv = drv;
    return ret;
}

/**
 * Set the current 'total_sectors' value
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
        hint = length >> BDRV_SECTOR_BITS;
    }

    bs->total_sectors = hint;
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
 * Common part for opening disk images and files
 */
static int bdrv_open_common(BlockDriverState *bs, const char *filename,
    int flags, BlockDriver *drv)
{
    int ret, open_flags;

    assert(drv != NULL);

    trace_bdrv_open_common(bs, filename, flags, drv->format_name);

    bs->file = NULL;
    bs->total_sectors = 0;
    bs->encrypted = 0;
    bs->valid_key = 0;
    bs->sg = 0;
    bs->open_flags = flags;
    bs->growable = 0;
    bs->buffer_alignment = 512;

    assert(bs->copy_on_read == 0); /* bdrv_new() and bdrv_close() make it so */
    if ((flags & BDRV_O_RDWR) && (flags & BDRV_O_COPY_ON_READ)) {
        bdrv_enable_copy_on_read(bs);
    }

    pstrcpy(bs->filename, sizeof(bs->filename), filename);
    bs->backing_file[0] = '\0';

    if (use_bdrv_whitelist && !bdrv_is_whitelisted(drv)) {
        return -ENOTSUP;
    }

    bs->drv = drv;
    bs->opaque = g_malloc0(drv->instance_size);

    bs->enable_write_cache = !!(flags & BDRV_O_CACHE_WB);

    /*
     * Clear flags that are internal to the block layer before opening the
     * image.
     */
    open_flags = flags & ~(BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING);

    /*
     * Snapshots should be writable.
     */
    if (bs->is_temporary) {
        open_flags |= BDRV_O_RDWR;
    }

    bs->keep_read_only = bs->read_only = !(open_flags & BDRV_O_RDWR);

    /* Open the image, either directly or using a protocol */
    if (drv->bdrv_file_open) {
        ret = drv->bdrv_file_open(bs, filename, open_flags);
    } else {
        ret = bdrv_file_open(&bs->file, filename, open_flags);
        if (ret >= 0) {
            ret = drv->bdrv_open(bs, open_flags);
        }
    }

    if (ret < 0) {
        goto free_and_fail;
    }

    ret = refresh_total_sectors(bs, bs->total_sectors);
    if (ret < 0) {
        goto free_and_fail;
    }

#ifndef _WIN32
    if (bs->is_temporary) {
        unlink(filename);
    }
#endif
    return 0;

free_and_fail:
    if (bs->file) {
        bdrv_delete(bs->file);
        bs->file = NULL;
    }
    g_free(bs->opaque);
    bs->opaque = NULL;
    bs->drv = NULL;
    return ret;
}

/*
 * Opens a file using a protocol (file, host_device, nbd, ...)
 */
int bdrv_file_open(BlockDriverState **pbs, const char *filename, int flags)
{
    BlockDriverState *bs;
    BlockDriver *drv;
    int ret;

    drv = bdrv_find_protocol(filename);
    if (!drv) {
        return -ENOENT;
    }

    bs = bdrv_new("");
    ret = bdrv_open_common(bs, filename, flags, drv);
    if (ret < 0) {
        bdrv_delete(bs);
        return ret;
    }
    bs->growable = 1;
    *pbs = bs;
    return 0;
}

/*
 * Opens a disk image (raw, qcow2, vmdk, ...)
 */
int bdrv_open(BlockDriverState *bs, const char *filename, int flags,
              BlockDriver *drv)
{
    int ret;
    char tmp_filename[PATH_MAX];

    if (flags & BDRV_O_SNAPSHOT) {
        BlockDriverState *bs1;
        int64_t total_size;
        int is_protocol = 0;
        BlockDriver *bdrv_qcow2;
        QEMUOptionParameter *options;
        char backing_filename[PATH_MAX];

        /* if snapshot, we create a temporary backing file and open it
           instead of opening 'filename' directly */

        /* if there is a backing file, use it */
        bs1 = bdrv_new("");
        ret = bdrv_open(bs1, filename, 0, drv);
        if (ret < 0) {
            bdrv_delete(bs1);
            return ret;
        }
        total_size = bdrv_getlength(bs1) & BDRV_SECTOR_MASK;

        if (bs1->drv && bs1->drv->protocol_name)
            is_protocol = 1;

        bdrv_delete(bs1);

        get_tmp_filename(tmp_filename, sizeof(tmp_filename));

        /* Real path is meaningless for protocols */
        if (is_protocol)
            snprintf(backing_filename, sizeof(backing_filename),
                     "%s", filename);
        else if (!realpath(filename, backing_filename))
            return -errno;

        bdrv_qcow2 = bdrv_find_format("qcow2");
        options = parse_option_parameters("", bdrv_qcow2->create_options, NULL);

        set_option_parameter_int(options, BLOCK_OPT_SIZE, total_size);
        set_option_parameter(options, BLOCK_OPT_BACKING_FILE, backing_filename);
        if (drv) {
            set_option_parameter(options, BLOCK_OPT_BACKING_FMT,
                drv->format_name);
        }

        ret = bdrv_create(bdrv_qcow2, tmp_filename, options);
        free_option_parameters(options);
        if (ret < 0) {
            return ret;
        }

        filename = tmp_filename;
        drv = bdrv_qcow2;
        bs->is_temporary = 1;
    }

    /* Find the right image format driver */
    if (!drv) {
        ret = find_image_format(filename, &drv);
    }

    if (!drv) {
        goto unlink_and_fail;
    }

    /* Open the image */
    ret = bdrv_open_common(bs, filename, flags, drv);
    if (ret < 0) {
        goto unlink_and_fail;
    }

    /* If there is a backing file, use it */
    if ((flags & BDRV_O_NO_BACKING) == 0 && bs->backing_file[0] != '\0') {
        char backing_filename[PATH_MAX];
        int back_flags;
        BlockDriver *back_drv = NULL;

        bs->backing_hd = bdrv_new("");

        if (path_has_protocol(bs->backing_file)) {
            pstrcpy(backing_filename, sizeof(backing_filename),
                    bs->backing_file);
        } else {
            path_combine(backing_filename, sizeof(backing_filename),
                         filename, bs->backing_file);
        }

        if (bs->backing_format[0] != '\0') {
            back_drv = bdrv_find_format(bs->backing_format);
        }

        /* backing files always opened read-only */
        back_flags =
            flags & ~(BDRV_O_RDWR | BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING);

        ret = bdrv_open(bs->backing_hd, backing_filename, back_flags, back_drv);
        if (ret < 0) {
            bdrv_close(bs);
            return ret;
        }
        if (bs->is_temporary) {
            bs->backing_hd->keep_read_only = !(flags & BDRV_O_RDWR);
        } else {
            /* base image inherits from "parent" */
            bs->backing_hd->keep_read_only = bs->keep_read_only;
        }
    }

    if (!bdrv_key_required(bs)) {
        bdrv_dev_change_media_cb(bs, true);
    }

    /* throttling disk I/O limits */
    if (bs->io_limits_enabled) {
        bdrv_io_limits_enable(bs);
    }

    return 0;

unlink_and_fail:
    if (bs->is_temporary) {
        unlink(filename);
    }
    return ret;
}

void bdrv_close(BlockDriverState *bs)
{
    if (bs->drv) {
        if (bs == bs_snapshots) {
            bs_snapshots = NULL;
        }
        if (bs->backing_hd) {
            bdrv_delete(bs->backing_hd);
            bs->backing_hd = NULL;
        }
        bs->drv->bdrv_close(bs);
        g_free(bs->opaque);
#ifdef _WIN32
        if (bs->is_temporary) {
            unlink(bs->filename);
        }
#endif
        bs->opaque = NULL;
        bs->drv = NULL;
        bs->copy_on_read = 0;

        if (bs->file != NULL) {
            bdrv_close(bs->file);
        }

        bdrv_dev_change_media_cb(bs, false);
    }

    /*throttling disk I/O limits*/
    if (bs->io_limits_enabled) {
        bdrv_io_limits_disable(bs);
    }
}

void bdrv_close_all(void)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, list) {
        bdrv_close(bs);
    }
}

/*
 * Wait for pending requests to complete across all BlockDriverStates
 *
 * This function does not flush data to disk, use bdrv_flush_all() for that
 * after calling this function.
 */
void bdrv_drain_all(void)
{
    BlockDriverState *bs;

    qemu_aio_flush();

    /* If requests are still pending there is a bug somewhere */
    QTAILQ_FOREACH(bs, &bdrv_states, list) {
        assert(QLIST_EMPTY(&bs->tracked_requests));
        assert(qemu_co_queue_empty(&bs->throttled_reqs));
    }
}

/* make a BlockDriverState anonymous by removing from bdrv_state list.
   Also, NULL terminate the device_name to prevent double remove */
void bdrv_make_anon(BlockDriverState *bs)
{
    if (bs->device_name[0] != '\0') {
        QTAILQ_REMOVE(&bdrv_states, bs, list);
    }
    bs->device_name[0] = '\0';
}

/*
 * Add new bs contents at the top of an image chain while the chain is
 * live, while keeping required fields on the top layer.
 *
 * This will modify the BlockDriverState fields, and swap contents
 * between bs_new and bs_top. Both bs_new and bs_top are modified.
 *
 * This function does not create any image files.
 */
void bdrv_append(BlockDriverState *bs_new, BlockDriverState *bs_top)
{
    BlockDriverState tmp;

    /* the new bs must not be in bdrv_states */
    bdrv_make_anon(bs_new);

    tmp = *bs_new;

    /* there are some fields that need to stay on the top layer: */

    /* dev info */
    tmp.dev_ops           = bs_top->dev_ops;
    tmp.dev_opaque        = bs_top->dev_opaque;
    tmp.dev               = bs_top->dev;
    tmp.buffer_alignment  = bs_top->buffer_alignment;
    tmp.copy_on_read      = bs_top->copy_on_read;

    /* i/o timing parameters */
    tmp.slice_time        = bs_top->slice_time;
    tmp.slice_start       = bs_top->slice_start;
    tmp.slice_end         = bs_top->slice_end;
    tmp.io_limits         = bs_top->io_limits;
    tmp.io_base           = bs_top->io_base;
    tmp.throttled_reqs    = bs_top->throttled_reqs;
    tmp.block_timer       = bs_top->block_timer;
    tmp.io_limits_enabled = bs_top->io_limits_enabled;

    /* geometry */
    tmp.cyls              = bs_top->cyls;
    tmp.heads             = bs_top->heads;
    tmp.secs              = bs_top->secs;
    tmp.translation       = bs_top->translation;

    /* r/w error */
    tmp.on_read_error     = bs_top->on_read_error;
    tmp.on_write_error    = bs_top->on_write_error;

    /* i/o status */
    tmp.iostatus_enabled  = bs_top->iostatus_enabled;
    tmp.iostatus          = bs_top->iostatus;

    /* keep the same entry in bdrv_states */
    pstrcpy(tmp.device_name, sizeof(tmp.device_name), bs_top->device_name);
    tmp.list = bs_top->list;

    /* The contents of 'tmp' will become bs_top, as we are
     * swapping bs_new and bs_top contents. */
    tmp.backing_hd = bs_new;
    pstrcpy(tmp.backing_file, sizeof(tmp.backing_file), bs_top->filename);

    /* swap contents of the fixed new bs and the current top */
    *bs_new = *bs_top;
    *bs_top = tmp;

    /* clear the copied fields in the new backing file */
    bdrv_detach_dev(bs_new, bs_new->dev);

    qemu_co_queue_init(&bs_new->throttled_reqs);
    memset(&bs_new->io_base,   0, sizeof(bs_new->io_base));
    memset(&bs_new->io_limits, 0, sizeof(bs_new->io_limits));
    bdrv_iostatus_disable(bs_new);

    /* we don't use bdrv_io_limits_disable() for this, because we don't want
     * to affect or delete the block_timer, as it has been moved to bs_top */
    bs_new->io_limits_enabled = false;
    bs_new->block_timer       = NULL;
    bs_new->slice_time        = 0;
    bs_new->slice_start       = 0;
    bs_new->slice_end         = 0;
}

void bdrv_delete(BlockDriverState *bs)
{
    assert(!bs->dev);

    /* remove from list, if necessary */
    bdrv_make_anon(bs);

    bdrv_close(bs);
    if (bs->file != NULL) {
        bdrv_delete(bs->file);
    }

    assert(bs != bs_snapshots);
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
    bs->buffer_alignment = 512;
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
    if (bdrv_dev_has_removable_media(bs) && bs == bs_snapshots) {
        bs_snapshots = NULL;
    }
}

void bdrv_emit_qmp_error_event(const BlockDriverState *bdrv,
                               BlockQMPEventAction action, int is_read)
{
    QObject *data;
    const char *action_str;

    switch (action) {
    case BDRV_ACTION_REPORT:
        action_str = "report";
        break;
    case BDRV_ACTION_IGNORE:
        action_str = "ignore";
        break;
    case BDRV_ACTION_STOP:
        action_str = "stop";
        break;
    default:
        abort();
    }

    data = qobject_from_jsonf("{ 'device': %s, 'action': %s, 'operation': %s }",
                              bdrv->device_name,
                              action_str,
                              is_read ? "read" : "write");
    monitor_protocol_event(QEVENT_BLOCK_IO_ERROR, data);

    qobject_decref(data);
}

static void bdrv_emit_qmp_eject_event(BlockDriverState *bs, bool ejected)
{
    QObject *data;

    data = qobject_from_jsonf("{ 'device': %s, 'tray-open': %i }",
                              bdrv_get_device_name(bs), ejected);
    monitor_protocol_event(QEVENT_DEVICE_TRAY_MOVED, data);

    qobject_decref(data);
}

static void bdrv_dev_change_media_cb(BlockDriverState *bs, bool load)
{
    if (bs->dev_ops && bs->dev_ops->change_media_cb) {
        bool tray_was_closed = !bdrv_dev_is_tray_open(bs);
        bs->dev_ops->change_media_cb(bs->dev_opaque, load);
        if (tray_was_closed) {
            /* tray open */
            bdrv_emit_qmp_eject_event(bs, true);
        }
        if (load) {
            /* tray close */
            bdrv_emit_qmp_eject_event(bs, false);
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
int bdrv_check(BlockDriverState *bs, BdrvCheckResult *res)
{
    if (bs->drv->bdrv_check == NULL) {
        return -ENOTSUP;
    }

    memset(res, 0, sizeof(*res));
    return bs->drv->bdrv_check(bs, res);
}

#define COMMIT_BUF_SECTORS 2048

/* commit COW file into the raw image */
int bdrv_commit(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    BlockDriver *backing_drv;
    int64_t sector, total_sectors;
    int n, ro, open_flags;
    int ret = 0, rw_ret = 0;
    uint8_t *buf;
    char filename[1024];
    BlockDriverState *bs_rw, *bs_ro;

    if (!drv)
        return -ENOMEDIUM;
    
    if (!bs->backing_hd) {
        return -ENOTSUP;
    }

    if (bs->backing_hd->keep_read_only) {
        return -EACCES;
    }

    if (bdrv_in_use(bs) || bdrv_in_use(bs->backing_hd)) {
        return -EBUSY;
    }

    backing_drv = bs->backing_hd->drv;
    ro = bs->backing_hd->read_only;
    strncpy(filename, bs->backing_hd->filename, sizeof(filename));
    open_flags =  bs->backing_hd->open_flags;

    if (ro) {
        /* re-open as RW */
        bdrv_delete(bs->backing_hd);
        bs->backing_hd = NULL;
        bs_rw = bdrv_new("");
        rw_ret = bdrv_open(bs_rw, filename, open_flags | BDRV_O_RDWR,
            backing_drv);
        if (rw_ret < 0) {
            bdrv_delete(bs_rw);
            /* try to re-open read-only */
            bs_ro = bdrv_new("");
            ret = bdrv_open(bs_ro, filename, open_flags & ~BDRV_O_RDWR,
                backing_drv);
            if (ret < 0) {
                bdrv_delete(bs_ro);
                /* drive not functional anymore */
                bs->drv = NULL;
                return ret;
            }
            bs->backing_hd = bs_ro;
            return rw_ret;
        }
        bs->backing_hd = bs_rw;
    }

    total_sectors = bdrv_getlength(bs) >> BDRV_SECTOR_BITS;
    buf = g_malloc(COMMIT_BUF_SECTORS * BDRV_SECTOR_SIZE);

    for (sector = 0; sector < total_sectors; sector += n) {
        if (bdrv_is_allocated(bs, sector, COMMIT_BUF_SECTORS, &n)) {

            if (bdrv_read(bs, sector, buf, n) != 0) {
                ret = -EIO;
                goto ro_cleanup;
            }

            if (bdrv_write(bs->backing_hd, sector, buf, n) != 0) {
                ret = -EIO;
                goto ro_cleanup;
            }
        }
    }

    if (drv->bdrv_make_empty) {
        ret = drv->bdrv_make_empty(bs);
        bdrv_flush(bs);
    }

    /*
     * Make sure all data we wrote to the backing device is actually
     * stable on disk.
     */
    if (bs->backing_hd)
        bdrv_flush(bs->backing_hd);

ro_cleanup:
    g_free(buf);

    if (ro) {
        /* re-open as RO */
        bdrv_delete(bs->backing_hd);
        bs->backing_hd = NULL;
        bs_ro = bdrv_new("");
        ret = bdrv_open(bs_ro, filename, open_flags & ~BDRV_O_RDWR,
            backing_drv);
        if (ret < 0) {
            bdrv_delete(bs_ro);
            /* drive not functional anymore */
            bs->drv = NULL;
            return ret;
        }
        bs->backing_hd = bs_ro;
        bs->backing_hd->keep_read_only = 0;
    }

    return ret;
}

int bdrv_commit_all(void)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, list) {
        int ret = bdrv_commit(bs);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

struct BdrvTrackedRequest {
    BlockDriverState *bs;
    int64_t sector_num;
    int nb_sectors;
    bool is_write;
    QLIST_ENTRY(BdrvTrackedRequest) list;
    Coroutine *co; /* owner, used for deadlock detection */
    CoQueue wait_queue; /* coroutines blocked on this request */
};

/**
 * Remove an active request from the tracked requests list
 *
 * This function should be called when a tracked request is completing.
 */
static void tracked_request_end(BdrvTrackedRequest *req)
{
    QLIST_REMOVE(req, list);
    qemu_co_queue_restart_all(&req->wait_queue);
}

/**
 * Add an active request to the tracked requests list
 */
static void tracked_request_begin(BdrvTrackedRequest *req,
                                  BlockDriverState *bs,
                                  int64_t sector_num,
                                  int nb_sectors, bool is_write)
{
    *req = (BdrvTrackedRequest){
        .bs = bs,
        .sector_num = sector_num,
        .nb_sectors = nb_sectors,
        .is_write = is_write,
        .co = qemu_coroutine_self(),
    };

    qemu_co_queue_init(&req->wait_queue);

    QLIST_INSERT_HEAD(&bs->tracked_requests, req, list);
}

/**
 * Round a region to cluster boundaries
 */
static void round_to_clusters(BlockDriverState *bs,
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

static bool tracked_request_overlaps(BdrvTrackedRequest *req,
                                     int64_t sector_num, int nb_sectors) {
    /*        aaaa   bbbb */
    if (sector_num >= req->sector_num + req->nb_sectors) {
        return false;
    }
    /* bbbb   aaaa        */
    if (req->sector_num >= sector_num + nb_sectors) {
        return false;
    }
    return true;
}

static void coroutine_fn wait_for_overlapping_requests(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors)
{
    BdrvTrackedRequest *req;
    int64_t cluster_sector_num;
    int cluster_nb_sectors;
    bool retry;

    /* If we touch the same cluster it counts as an overlap.  This guarantees
     * that allocating writes will be serialized and not race with each other
     * for the same cluster.  For example, in copy-on-read it ensures that the
     * CoR read and write operations are atomic and guest writes cannot
     * interleave between them.
     */
    round_to_clusters(bs, sector_num, nb_sectors,
                      &cluster_sector_num, &cluster_nb_sectors);

    do {
        retry = false;
        QLIST_FOREACH(req, &bs->tracked_requests, list) {
            if (tracked_request_overlaps(req, cluster_sector_num,
                                         cluster_nb_sectors)) {
                /* Hitting this means there was a reentrant request, for
                 * example, a block driver issuing nested requests.  This must
                 * never happen since it means deadlock.
                 */
                assert(qemu_coroutine_self() != req->co);

                qemu_co_queue_wait(&req->wait_queue);
                retry = true;
                break;
            }
        }
    } while (retry);
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

    if (drv->bdrv_change_backing_file != NULL) {
        return drv->bdrv_change_backing_file(bs, backing_file, backing_fmt);
    } else {
        return -ENOTSUP;
    }
}

static int bdrv_check_byte_request(BlockDriverState *bs, int64_t offset,
                                   size_t size)
{
    int64_t len;

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
    return bdrv_check_byte_request(bs, sector_num * BDRV_SECTOR_SIZE,
                                   nb_sectors * BDRV_SECTOR_SIZE);
}

typedef struct RwCo {
    BlockDriverState *bs;
    int64_t sector_num;
    int nb_sectors;
    QEMUIOVector *qiov;
    bool is_write;
    int ret;
} RwCo;

static void coroutine_fn bdrv_rw_co_entry(void *opaque)
{
    RwCo *rwco = opaque;

    if (!rwco->is_write) {
        rwco->ret = bdrv_co_do_readv(rwco->bs, rwco->sector_num,
                                     rwco->nb_sectors, rwco->qiov, 0);
    } else {
        rwco->ret = bdrv_co_do_writev(rwco->bs, rwco->sector_num,
                                      rwco->nb_sectors, rwco->qiov, 0);
    }
}

/*
 * Process a synchronous request using coroutines
 */
static int bdrv_rw_co(BlockDriverState *bs, int64_t sector_num, uint8_t *buf,
                      int nb_sectors, bool is_write)
{
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len = nb_sectors * BDRV_SECTOR_SIZE,
    };
    Coroutine *co;
    RwCo rwco = {
        .bs = bs,
        .sector_num = sector_num,
        .nb_sectors = nb_sectors,
        .qiov = &qiov,
        .is_write = is_write,
        .ret = NOT_DONE,
    };

    qemu_iovec_init_external(&qiov, &iov, 1);

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_rw_co_entry(&rwco);
    } else {
        co = qemu_coroutine_create(bdrv_rw_co_entry);
        qemu_coroutine_enter(co, &rwco);
        while (rwco.ret == NOT_DONE) {
            qemu_aio_wait();
        }
    }
    return rwco.ret;
}

/* return < 0 if error. See bdrv_write() for the return codes */
int bdrv_read(BlockDriverState *bs, int64_t sector_num,
              uint8_t *buf, int nb_sectors)
{
    return bdrv_rw_co(bs, sector_num, buf, nb_sectors, false);
}

static void set_dirty_bitmap(BlockDriverState *bs, int64_t sector_num,
                             int nb_sectors, int dirty)
{
    int64_t start, end;
    unsigned long val, idx, bit;

    start = sector_num / BDRV_SECTORS_PER_DIRTY_CHUNK;
    end = (sector_num + nb_sectors - 1) / BDRV_SECTORS_PER_DIRTY_CHUNK;

    for (; start <= end; start++) {
        idx = start / (sizeof(unsigned long) * 8);
        bit = start % (sizeof(unsigned long) * 8);
        val = bs->dirty_bitmap[idx];
        if (dirty) {
            if (!(val & (1UL << bit))) {
                bs->dirty_count++;
                val |= 1UL << bit;
            }
        } else {
            if (val & (1UL << bit)) {
                bs->dirty_count--;
                val &= ~(1UL << bit);
            }
        }
        bs->dirty_bitmap[idx] = val;
    }
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
    return bdrv_rw_co(bs, sector_num, (uint8_t *)buf, nb_sectors, true);
}

int bdrv_pread(BlockDriverState *bs, int64_t offset,
               void *buf, int count1)
{
    uint8_t tmp_buf[BDRV_SECTOR_SIZE];
    int len, nb_sectors, count;
    int64_t sector_num;
    int ret;

    count = count1;
    /* first read to align to sector start */
    len = (BDRV_SECTOR_SIZE - offset) & (BDRV_SECTOR_SIZE - 1);
    if (len > count)
        len = count;
    sector_num = offset >> BDRV_SECTOR_BITS;
    if (len > 0) {
        if ((ret = bdrv_read(bs, sector_num, tmp_buf, 1)) < 0)
            return ret;
        memcpy(buf, tmp_buf + (offset & (BDRV_SECTOR_SIZE - 1)), len);
        count -= len;
        if (count == 0)
            return count1;
        sector_num++;
        buf += len;
    }

    /* read the sectors "in place" */
    nb_sectors = count >> BDRV_SECTOR_BITS;
    if (nb_sectors > 0) {
        if ((ret = bdrv_read(bs, sector_num, buf, nb_sectors)) < 0)
            return ret;
        sector_num += nb_sectors;
        len = nb_sectors << BDRV_SECTOR_BITS;
        buf += len;
        count -= len;
    }

    /* add data from the last sector */
    if (count > 0) {
        if ((ret = bdrv_read(bs, sector_num, tmp_buf, 1)) < 0)
            return ret;
        memcpy(buf, tmp_buf, count);
    }
    return count1;
}

int bdrv_pwrite(BlockDriverState *bs, int64_t offset,
                const void *buf, int count1)
{
    uint8_t tmp_buf[BDRV_SECTOR_SIZE];
    int len, nb_sectors, count;
    int64_t sector_num;
    int ret;

    count = count1;
    /* first write to align to sector start */
    len = (BDRV_SECTOR_SIZE - offset) & (BDRV_SECTOR_SIZE - 1);
    if (len > count)
        len = count;
    sector_num = offset >> BDRV_SECTOR_BITS;
    if (len > 0) {
        if ((ret = bdrv_read(bs, sector_num, tmp_buf, 1)) < 0)
            return ret;
        memcpy(tmp_buf + (offset & (BDRV_SECTOR_SIZE - 1)), buf, len);
        if ((ret = bdrv_write(bs, sector_num, tmp_buf, 1)) < 0)
            return ret;
        count -= len;
        if (count == 0)
            return count1;
        sector_num++;
        buf += len;
    }

    /* write the sectors "in place" */
    nb_sectors = count >> BDRV_SECTOR_BITS;
    if (nb_sectors > 0) {
        if ((ret = bdrv_write(bs, sector_num, buf, nb_sectors)) < 0)
            return ret;
        sector_num += nb_sectors;
        len = nb_sectors << BDRV_SECTOR_BITS;
        buf += len;
        count -= len;
    }

    /* add data from the last sector */
    if (count > 0) {
        if ((ret = bdrv_read(bs, sector_num, tmp_buf, 1)) < 0)
            return ret;
        memcpy(tmp_buf, buf, count);
        if ((ret = bdrv_write(bs, sector_num, tmp_buf, 1)) < 0)
            return ret;
    }
    return count1;
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

    /* No flush needed for cache modes that use O_DSYNC */
    if ((bs->open_flags & BDRV_O_CACHE_WB) != 0) {
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
    round_to_clusters(bs, sector_num, nb_sectors,
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
        ret = drv->bdrv_co_write_zeroes(bs, cluster_sector_num,
                                        cluster_nb_sectors);
    } else {
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
    qemu_iovec_from_buffer(qiov, bounce_buffer + skip_bytes,
                           nb_sectors * BDRV_SECTOR_SIZE);

err:
    qemu_vfree(bounce_buffer);
    return ret;
}

/*
 * Handle a read request in coroutine context
 */
static int coroutine_fn bdrv_co_do_readv(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    BlockDriver *drv = bs->drv;
    BdrvTrackedRequest req;
    int ret;

    if (!drv) {
        return -ENOMEDIUM;
    }
    if (bdrv_check_request(bs, sector_num, nb_sectors)) {
        return -EIO;
    }

    /* throttling disk read I/O */
    if (bs->io_limits_enabled) {
        bdrv_io_limits_intercept(bs, false, nb_sectors);
    }

    if (bs->copy_on_read) {
        flags |= BDRV_REQ_COPY_ON_READ;
    }
    if (flags & BDRV_REQ_COPY_ON_READ) {
        bs->copy_on_read_in_flight++;
    }

    if (bs->copy_on_read_in_flight) {
        wait_for_overlapping_requests(bs, sector_num, nb_sectors);
    }

    tracked_request_begin(&req, bs, sector_num, nb_sectors, false);

    if (flags & BDRV_REQ_COPY_ON_READ) {
        int pnum;

        ret = bdrv_co_is_allocated(bs, sector_num, nb_sectors, &pnum);
        if (ret < 0) {
            goto out;
        }

        if (!ret || pnum != nb_sectors) {
            ret = bdrv_co_do_copy_on_readv(bs, sector_num, nb_sectors, qiov);
            goto out;
        }
    }

    ret = drv->bdrv_co_readv(bs, sector_num, nb_sectors, qiov);

out:
    tracked_request_end(&req);

    if (flags & BDRV_REQ_COPY_ON_READ) {
        bs->copy_on_read_in_flight--;
    }

    return ret;
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

static int coroutine_fn bdrv_co_do_write_zeroes(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors)
{
    BlockDriver *drv = bs->drv;
    QEMUIOVector qiov;
    struct iovec iov;
    int ret;

    /* First try the efficient write zeroes operation */
    if (drv->bdrv_co_write_zeroes) {
        return drv->bdrv_co_write_zeroes(bs, sector_num, nb_sectors);
    }

    /* Fall back to bounce buffer if write zeroes is unsupported */
    iov.iov_len  = nb_sectors * BDRV_SECTOR_SIZE;
    iov.iov_base = qemu_blockalign(bs, iov.iov_len);
    memset(iov.iov_base, 0, iov.iov_len);
    qemu_iovec_init_external(&qiov, &iov, 1);

    ret = drv->bdrv_co_writev(bs, sector_num, nb_sectors, &qiov);

    qemu_vfree(iov.iov_base);
    return ret;
}

/*
 * Handle a write request in coroutine context
 */
static int coroutine_fn bdrv_co_do_writev(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, QEMUIOVector *qiov,
    BdrvRequestFlags flags)
{
    BlockDriver *drv = bs->drv;
    BdrvTrackedRequest req;
    int ret;

    if (!bs->drv) {
        return -ENOMEDIUM;
    }
    if (bs->read_only) {
        return -EACCES;
    }
    if (bdrv_check_request(bs, sector_num, nb_sectors)) {
        return -EIO;
    }

    /* throttling disk write I/O */
    if (bs->io_limits_enabled) {
        bdrv_io_limits_intercept(bs, true, nb_sectors);
    }

    if (bs->copy_on_read_in_flight) {
        wait_for_overlapping_requests(bs, sector_num, nb_sectors);
    }

    tracked_request_begin(&req, bs, sector_num, nb_sectors, true);

    if (flags & BDRV_REQ_ZERO_WRITE) {
        ret = bdrv_co_do_write_zeroes(bs, sector_num, nb_sectors);
    } else {
        ret = drv->bdrv_co_writev(bs, sector_num, nb_sectors, qiov);
    }

    if (bs->dirty_bitmap) {
        set_dirty_bitmap(bs, sector_num, nb_sectors, 1);
    }

    if (bs->wr_highest_sector < sector_num + nb_sectors - 1) {
        bs->wr_highest_sector = sector_num + nb_sectors - 1;
    }

    tracked_request_end(&req);

    return ret;
}

int coroutine_fn bdrv_co_writev(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, QEMUIOVector *qiov)
{
    trace_bdrv_co_writev(bs, sector_num, nb_sectors);

    return bdrv_co_do_writev(bs, sector_num, nb_sectors, qiov, 0);
}

int coroutine_fn bdrv_co_write_zeroes(BlockDriverState *bs,
                                      int64_t sector_num, int nb_sectors)
{
    trace_bdrv_co_write_zeroes(bs, sector_num, nb_sectors);

    return bdrv_co_do_writev(bs, sector_num, nb_sectors, NULL,
                             BDRV_REQ_ZERO_WRITE);
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
    if (bdrv_in_use(bs))
        return -EBUSY;
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
 * Length of a file in bytes. Return < 0 if error or unknown.
 */
int64_t bdrv_getlength(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;

    if (bs->growable || bdrv_dev_has_removable_media(bs)) {
        if (drv->bdrv_getlength) {
            return drv->bdrv_getlength(bs);
        }
    }
    return bs->total_sectors * BDRV_SECTOR_SIZE;
}

/* return 0 as number of sectors if no device present or error */
void bdrv_get_geometry(BlockDriverState *bs, uint64_t *nb_sectors_ptr)
{
    int64_t length;
    length = bdrv_getlength(bs);
    if (length < 0)
        length = 0;
    else
        length = length >> BDRV_SECTOR_BITS;
    *nb_sectors_ptr = length;
}

struct partition {
        uint8_t boot_ind;           /* 0x80 - active */
        uint8_t head;               /* starting head */
        uint8_t sector;             /* starting sector */
        uint8_t cyl;                /* starting cylinder */
        uint8_t sys_ind;            /* What partition type */
        uint8_t end_head;           /* end head */
        uint8_t end_sector;         /* end sector */
        uint8_t end_cyl;            /* end cylinder */
        uint32_t start_sect;        /* starting sector counting from 0 */
        uint32_t nr_sects;          /* nr of sectors in partition */
} QEMU_PACKED;

/* try to guess the disk logical geometry from the MSDOS partition table. Return 0 if OK, -1 if could not guess */
static int guess_disk_lchs(BlockDriverState *bs,
                           int *pcylinders, int *pheads, int *psectors)
{
    uint8_t buf[BDRV_SECTOR_SIZE];
    int ret, i, heads, sectors, cylinders;
    struct partition *p;
    uint32_t nr_sects;
    uint64_t nb_sectors;

    bdrv_get_geometry(bs, &nb_sectors);

    ret = bdrv_read(bs, 0, buf, 1);
    if (ret < 0)
        return -1;
    /* test msdos magic */
    if (buf[510] != 0x55 || buf[511] != 0xaa)
        return -1;
    for(i = 0; i < 4; i++) {
        p = ((struct partition *)(buf + 0x1be)) + i;
        nr_sects = le32_to_cpu(p->nr_sects);
        if (nr_sects && p->end_head) {
            /* We make the assumption that the partition terminates on
               a cylinder boundary */
            heads = p->end_head + 1;
            sectors = p->end_sector & 63;
            if (sectors == 0)
                continue;
            cylinders = nb_sectors / (heads * sectors);
            if (cylinders < 1 || cylinders > 16383)
                continue;
            *pheads = heads;
            *psectors = sectors;
            *pcylinders = cylinders;
#if 0
            printf("guessed geometry: LCHS=%d %d %d\n",
                   cylinders, heads, sectors);
#endif
            return 0;
        }
    }
    return -1;
}

void bdrv_guess_geometry(BlockDriverState *bs, int *pcyls, int *pheads, int *psecs)
{
    int translation, lba_detected = 0;
    int cylinders, heads, secs;
    uint64_t nb_sectors;

    /* if a geometry hint is available, use it */
    bdrv_get_geometry(bs, &nb_sectors);
    bdrv_get_geometry_hint(bs, &cylinders, &heads, &secs);
    translation = bdrv_get_translation_hint(bs);
    if (cylinders != 0) {
        *pcyls = cylinders;
        *pheads = heads;
        *psecs = secs;
    } else {
        if (guess_disk_lchs(bs, &cylinders, &heads, &secs) == 0) {
            if (heads > 16) {
                /* if heads > 16, it means that a BIOS LBA
                   translation was active, so the default
                   hardware geometry is OK */
                lba_detected = 1;
                goto default_geometry;
            } else {
                *pcyls = cylinders;
                *pheads = heads;
                *psecs = secs;
                /* disable any translation to be in sync with
                   the logical geometry */
                if (translation == BIOS_ATA_TRANSLATION_AUTO) {
                    bdrv_set_translation_hint(bs,
                                              BIOS_ATA_TRANSLATION_NONE);
                }
            }
        } else {
        default_geometry:
            /* if no geometry, use a standard physical disk geometry */
            cylinders = nb_sectors / (16 * 63);

            if (cylinders > 16383)
                cylinders = 16383;
            else if (cylinders < 2)
                cylinders = 2;
            *pcyls = cylinders;
            *pheads = 16;
            *psecs = 63;
            if ((lba_detected == 1) && (translation == BIOS_ATA_TRANSLATION_AUTO)) {
                if ((*pcyls * *pheads) <= 131072) {
                    bdrv_set_translation_hint(bs,
                                              BIOS_ATA_TRANSLATION_LARGE);
                } else {
                    bdrv_set_translation_hint(bs,
                                              BIOS_ATA_TRANSLATION_LBA);
                }
            }
        }
        bdrv_set_geometry_hint(bs, *pcyls, *pheads, *psecs);
    }
}

void bdrv_set_geometry_hint(BlockDriverState *bs,
                            int cyls, int heads, int secs)
{
    bs->cyls = cyls;
    bs->heads = heads;
    bs->secs = secs;
}

void bdrv_set_translation_hint(BlockDriverState *bs, int translation)
{
    bs->translation = translation;
}

void bdrv_get_geometry_hint(BlockDriverState *bs,
                            int *pcyls, int *pheads, int *psecs)
{
    *pcyls = bs->cyls;
    *pheads = bs->heads;
    *psecs = bs->secs;
}

/* throttling disk io limits */
void bdrv_set_io_limits(BlockDriverState *bs,
                        BlockIOLimit *io_limits)
{
    bs->io_limits = *io_limits;
    bs->io_limits_enabled = bdrv_io_limits_enabled(bs);
}

/* Recognize floppy formats */
typedef struct FDFormat {
    FDriveType drive;
    uint8_t last_sect;
    uint8_t max_track;
    uint8_t max_head;
    FDriveRate rate;
} FDFormat;

static const FDFormat fd_formats[] = {
    /* First entry is default format */
    /* 1.44 MB 3"1/2 floppy disks */
    { FDRIVE_DRV_144, 18, 80, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_144, 20, 80, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_144, 21, 80, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_144, 21, 82, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_144, 21, 83, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_144, 22, 80, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_144, 23, 80, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_144, 24, 80, 1, FDRIVE_RATE_500K, },
    /* 2.88 MB 3"1/2 floppy disks */
    { FDRIVE_DRV_288, 36, 80, 1, FDRIVE_RATE_1M, },
    { FDRIVE_DRV_288, 39, 80, 1, FDRIVE_RATE_1M, },
    { FDRIVE_DRV_288, 40, 80, 1, FDRIVE_RATE_1M, },
    { FDRIVE_DRV_288, 44, 80, 1, FDRIVE_RATE_1M, },
    { FDRIVE_DRV_288, 48, 80, 1, FDRIVE_RATE_1M, },
    /* 720 kB 3"1/2 floppy disks */
    { FDRIVE_DRV_144,  9, 80, 1, FDRIVE_RATE_250K, },
    { FDRIVE_DRV_144, 10, 80, 1, FDRIVE_RATE_250K, },
    { FDRIVE_DRV_144, 10, 82, 1, FDRIVE_RATE_250K, },
    { FDRIVE_DRV_144, 10, 83, 1, FDRIVE_RATE_250K, },
    { FDRIVE_DRV_144, 13, 80, 1, FDRIVE_RATE_250K, },
    { FDRIVE_DRV_144, 14, 80, 1, FDRIVE_RATE_250K, },
    /* 1.2 MB 5"1/4 floppy disks */
    { FDRIVE_DRV_120, 15, 80, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_120, 18, 80, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_120, 18, 82, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_120, 18, 83, 1, FDRIVE_RATE_500K, },
    { FDRIVE_DRV_120, 20, 80, 1, FDRIVE_RATE_500K, },
    /* 720 kB 5"1/4 floppy disks */
    { FDRIVE_DRV_120,  9, 80, 1, FDRIVE_RATE_250K, },
    { FDRIVE_DRV_120, 11, 80, 1, FDRIVE_RATE_250K, },
    /* 360 kB 5"1/4 floppy disks */
    { FDRIVE_DRV_120,  9, 40, 1, FDRIVE_RATE_300K, },
    { FDRIVE_DRV_120,  9, 40, 0, FDRIVE_RATE_300K, },
    { FDRIVE_DRV_120, 10, 41, 1, FDRIVE_RATE_300K, },
    { FDRIVE_DRV_120, 10, 42, 1, FDRIVE_RATE_300K, },
    /* 320 kB 5"1/4 floppy disks */
    { FDRIVE_DRV_120,  8, 40, 1, FDRIVE_RATE_250K, },
    { FDRIVE_DRV_120,  8, 40, 0, FDRIVE_RATE_250K, },
    /* 360 kB must match 5"1/4 better than 3"1/2... */
    { FDRIVE_DRV_144,  9, 80, 0, FDRIVE_RATE_250K, },
    /* end */
    { FDRIVE_DRV_NONE, -1, -1, 0, 0, },
};

void bdrv_get_floppy_geometry_hint(BlockDriverState *bs, int *nb_heads,
                                   int *max_track, int *last_sect,
                                   FDriveType drive_in, FDriveType *drive,
                                   FDriveRate *rate)
{
    const FDFormat *parse;
    uint64_t nb_sectors, size;
    int i, first_match, match;

    bdrv_get_geometry_hint(bs, nb_heads, max_track, last_sect);
    if (*nb_heads != 0 && *max_track != 0 && *last_sect != 0) {
        /* User defined disk */
        *rate = FDRIVE_RATE_500K;
    } else {
        bdrv_get_geometry(bs, &nb_sectors);
        match = -1;
        first_match = -1;
        for (i = 0; ; i++) {
            parse = &fd_formats[i];
            if (parse->drive == FDRIVE_DRV_NONE) {
                break;
            }
            if (drive_in == parse->drive ||
                drive_in == FDRIVE_DRV_NONE) {
                size = (parse->max_head + 1) * parse->max_track *
                    parse->last_sect;
                if (nb_sectors == size) {
                    match = i;
                    break;
                }
                if (first_match == -1) {
                    first_match = i;
                }
            }
        }
        if (match == -1) {
            if (first_match == -1) {
                match = 1;
            } else {
                match = first_match;
            }
            parse = &fd_formats[match];
        }
        *nb_heads = parse->max_head + 1;
        *max_track = parse->max_track;
        *last_sect = parse->last_sect;
        *drive = parse->drive;
        *rate = parse->rate;
    }
}

int bdrv_get_translation_hint(BlockDriverState *bs)
{
    return bs->translation;
}

void bdrv_set_on_error(BlockDriverState *bs, BlockErrorAction on_read_error,
                       BlockErrorAction on_write_error)
{
    bs->on_read_error = on_read_error;
    bs->on_write_error = on_write_error;
}

BlockErrorAction bdrv_get_on_error(BlockDriverState *bs, int is_read)
{
    return is_read ? bs->on_read_error : bs->on_write_error;
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

void bdrv_get_format(BlockDriverState *bs, char *buf, int buf_size)
{
    if (!bs->drv) {
        buf[0] = '\0';
    } else {
        pstrcpy(buf, buf_size, bs->drv->format_name);
    }
}

void bdrv_iterate_format(void (*it)(void *opaque, const char *name),
                         void *opaque)
{
    BlockDriver *drv;

    QLIST_FOREACH(drv, &bdrv_drivers, list) {
        it(opaque, drv->format_name);
    }
}

BlockDriverState *bdrv_find(const char *name)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, list) {
        if (!strcmp(name, bs->device_name)) {
            return bs;
        }
    }
    return NULL;
}

BlockDriverState *bdrv_next(BlockDriverState *bs)
{
    if (!bs) {
        return QTAILQ_FIRST(&bdrv_states);
    }
    return QTAILQ_NEXT(bs, list);
}

void bdrv_iterate(void (*it)(void *opaque, BlockDriverState *bs), void *opaque)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, list) {
        it(opaque, bs);
    }
}

const char *bdrv_get_device_name(BlockDriverState *bs)
{
    return bs->device_name;
}

void bdrv_flush_all(void)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, list) {
        if (!bdrv_is_read_only(bs) && bdrv_is_inserted(bs)) {
            bdrv_flush(bs);
        }
    }
}

int bdrv_has_zero_init(BlockDriverState *bs)
{
    assert(bs->drv);

    if (bs->drv->bdrv_has_zero_init) {
        return bs->drv->bdrv_has_zero_init(bs);
    }

    return 1;
}

typedef struct BdrvCoIsAllocatedData {
    BlockDriverState *bs;
    int64_t sector_num;
    int nb_sectors;
    int *pnum;
    int ret;
    bool done;
} BdrvCoIsAllocatedData;

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
int coroutine_fn bdrv_co_is_allocated(BlockDriverState *bs, int64_t sector_num,
                                      int nb_sectors, int *pnum)
{
    int64_t n;

    if (sector_num >= bs->total_sectors) {
        *pnum = 0;
        return 0;
    }

    n = bs->total_sectors - sector_num;
    if (n < nb_sectors) {
        nb_sectors = n;
    }

    if (!bs->drv->bdrv_co_is_allocated) {
        *pnum = nb_sectors;
        return 1;
    }

    return bs->drv->bdrv_co_is_allocated(bs, sector_num, nb_sectors, pnum);
}

/* Coroutine wrapper for bdrv_is_allocated() */
static void coroutine_fn bdrv_is_allocated_co_entry(void *opaque)
{
    BdrvCoIsAllocatedData *data = opaque;
    BlockDriverState *bs = data->bs;

    data->ret = bdrv_co_is_allocated(bs, data->sector_num, data->nb_sectors,
                                     data->pnum);
    data->done = true;
}

/*
 * Synchronous wrapper around bdrv_co_is_allocated().
 *
 * See bdrv_co_is_allocated() for details.
 */
int bdrv_is_allocated(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                      int *pnum)
{
    Coroutine *co;
    BdrvCoIsAllocatedData data = {
        .bs = bs,
        .sector_num = sector_num,
        .nb_sectors = nb_sectors,
        .pnum = pnum,
        .done = false,
    };

    co = qemu_coroutine_create(bdrv_is_allocated_co_entry);
    qemu_coroutine_enter(co, &data);
    while (!data.done) {
        qemu_aio_wait();
    }
    return data.ret;
}

BlockInfoList *qmp_query_block(Error **errp)
{
    BlockInfoList *head = NULL, *cur_item = NULL;
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, list) {
        BlockInfoList *info = g_malloc0(sizeof(*info));

        info->value = g_malloc0(sizeof(*info->value));
        info->value->device = g_strdup(bs->device_name);
        info->value->type = g_strdup("unknown");
        info->value->locked = bdrv_dev_is_medium_locked(bs);
        info->value->removable = bdrv_dev_has_removable_media(bs);

        if (bdrv_dev_has_removable_media(bs)) {
            info->value->has_tray_open = true;
            info->value->tray_open = bdrv_dev_is_tray_open(bs);
        }

        if (bdrv_iostatus_is_enabled(bs)) {
            info->value->has_io_status = true;
            info->value->io_status = bs->iostatus;
        }

        if (bs->drv) {
            info->value->has_inserted = true;
            info->value->inserted = g_malloc0(sizeof(*info->value->inserted));
            info->value->inserted->file = g_strdup(bs->filename);
            info->value->inserted->ro = bs->read_only;
            info->value->inserted->drv = g_strdup(bs->drv->format_name);
            info->value->inserted->encrypted = bs->encrypted;
            if (bs->backing_file[0]) {
                info->value->inserted->has_backing_file = true;
                info->value->inserted->backing_file = g_strdup(bs->backing_file);
            }

            if (bs->io_limits_enabled) {
                info->value->inserted->bps =
                               bs->io_limits.bps[BLOCK_IO_LIMIT_TOTAL];
                info->value->inserted->bps_rd =
                               bs->io_limits.bps[BLOCK_IO_LIMIT_READ];
                info->value->inserted->bps_wr =
                               bs->io_limits.bps[BLOCK_IO_LIMIT_WRITE];
                info->value->inserted->iops =
                               bs->io_limits.iops[BLOCK_IO_LIMIT_TOTAL];
                info->value->inserted->iops_rd =
                               bs->io_limits.iops[BLOCK_IO_LIMIT_READ];
                info->value->inserted->iops_wr =
                               bs->io_limits.iops[BLOCK_IO_LIMIT_WRITE];
            }
        }

        /* XXX: waiting for the qapi to support GSList */
        if (!cur_item) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }
    }

    return head;
}

/* Consider exposing this as a full fledged QMP command */
static BlockStats *qmp_query_blockstat(const BlockDriverState *bs, Error **errp)
{
    BlockStats *s;

    s = g_malloc0(sizeof(*s));

    if (bs->device_name[0]) {
        s->has_device = true;
        s->device = g_strdup(bs->device_name);
    }

    s->stats = g_malloc0(sizeof(*s->stats));
    s->stats->rd_bytes = bs->nr_bytes[BDRV_ACCT_READ];
    s->stats->wr_bytes = bs->nr_bytes[BDRV_ACCT_WRITE];
    s->stats->rd_operations = bs->nr_ops[BDRV_ACCT_READ];
    s->stats->wr_operations = bs->nr_ops[BDRV_ACCT_WRITE];
    s->stats->wr_highest_offset = bs->wr_highest_sector * BDRV_SECTOR_SIZE;
    s->stats->flush_operations = bs->nr_ops[BDRV_ACCT_FLUSH];
    s->stats->wr_total_time_ns = bs->total_time_ns[BDRV_ACCT_WRITE];
    s->stats->rd_total_time_ns = bs->total_time_ns[BDRV_ACCT_READ];
    s->stats->flush_total_time_ns = bs->total_time_ns[BDRV_ACCT_FLUSH];

    if (bs->file) {
        s->has_parent = true;
        s->parent = qmp_query_blockstat(bs->file, NULL);
    }

    return s;
}

BlockStatsList *qmp_query_blockstats(Error **errp)
{
    BlockStatsList *head = NULL, *cur_item = NULL;
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, list) {
        BlockStatsList *info = g_malloc0(sizeof(*info));
        info->value = qmp_query_blockstat(bs, NULL);

        /* XXX: waiting for the qapi to support GSList */
        if (!cur_item) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }
    }

    return head;
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

    if (bs->dirty_bitmap) {
        set_dirty_bitmap(bs, sector_num, nb_sectors, 1);
    }

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

int bdrv_save_vmstate(BlockDriverState *bs, const uint8_t *buf,
                      int64_t pos, int size)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (drv->bdrv_save_vmstate)
        return drv->bdrv_save_vmstate(bs, buf, pos, size);
    if (bs->file)
        return bdrv_save_vmstate(bs->file, buf, pos, size);
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
    BlockDriver *drv = bs->drv;

    if (!drv || !drv->bdrv_debug_event) {
        return;
    }

    return drv->bdrv_debug_event(bs, event);

}

/**************************************************************/
/* handling of snapshots */

int bdrv_can_snapshot(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    if (!drv || !bdrv_is_inserted(bs) || bdrv_is_read_only(bs)) {
        return 0;
    }

    if (!drv->bdrv_snapshot_create) {
        if (bs->file != NULL) {
            return bdrv_can_snapshot(bs->file);
        }
        return 0;
    }

    return 1;
}

int bdrv_is_snapshot(BlockDriverState *bs)
{
    return !!(bs->open_flags & BDRV_O_SNAPSHOT);
}

BlockDriverState *bdrv_snapshots(void)
{
    BlockDriverState *bs;

    if (bs_snapshots) {
        return bs_snapshots;
    }

    bs = NULL;
    while ((bs = bdrv_next(bs))) {
        if (bdrv_can_snapshot(bs)) {
            bs_snapshots = bs;
            return bs;
        }
    }
    return NULL;
}

int bdrv_snapshot_create(BlockDriverState *bs,
                         QEMUSnapshotInfo *sn_info)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (drv->bdrv_snapshot_create)
        return drv->bdrv_snapshot_create(bs, sn_info);
    if (bs->file)
        return bdrv_snapshot_create(bs->file, sn_info);
    return -ENOTSUP;
}

int bdrv_snapshot_goto(BlockDriverState *bs,
                       const char *snapshot_id)
{
    BlockDriver *drv = bs->drv;
    int ret, open_ret;

    if (!drv)
        return -ENOMEDIUM;
    if (drv->bdrv_snapshot_goto)
        return drv->bdrv_snapshot_goto(bs, snapshot_id);

    if (bs->file) {
        drv->bdrv_close(bs);
        ret = bdrv_snapshot_goto(bs->file, snapshot_id);
        open_ret = drv->bdrv_open(bs, bs->open_flags);
        if (open_ret < 0) {
            bdrv_delete(bs->file);
            bs->drv = NULL;
            return open_ret;
        }
        return ret;
    }

    return -ENOTSUP;
}

int bdrv_snapshot_delete(BlockDriverState *bs, const char *snapshot_id)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (drv->bdrv_snapshot_delete)
        return drv->bdrv_snapshot_delete(bs, snapshot_id);
    if (bs->file)
        return bdrv_snapshot_delete(bs->file, snapshot_id);
    return -ENOTSUP;
}

int bdrv_snapshot_list(BlockDriverState *bs,
                       QEMUSnapshotInfo **psn_info)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (drv->bdrv_snapshot_list)
        return drv->bdrv_snapshot_list(bs, psn_info);
    if (bs->file)
        return bdrv_snapshot_list(bs->file, psn_info);
    return -ENOTSUP;
}

int bdrv_snapshot_load_tmp(BlockDriverState *bs,
        const char *snapshot_name)
{
    BlockDriver *drv = bs->drv;
    if (!drv) {
        return -ENOMEDIUM;
    }
    if (!bs->read_only) {
        return -EINVAL;
    }
    if (drv->bdrv_snapshot_load_tmp) {
        return drv->bdrv_snapshot_load_tmp(bs, snapshot_name);
    }
    return -ENOTSUP;
}

BlockDriverState *bdrv_find_backing_image(BlockDriverState *bs,
        const char *backing_file)
{
    if (!bs->drv) {
        return NULL;
    }

    if (bs->backing_hd) {
        if (strcmp(bs->backing_file, backing_file) == 0) {
            return bs->backing_hd;
        } else {
            return bdrv_find_backing_image(bs->backing_hd, backing_file);
        }
    }

    return NULL;
}

#define NB_SUFFIXES 4

char *get_human_readable_size(char *buf, int buf_size, int64_t size)
{
    static const char suffixes[NB_SUFFIXES] = "KMGT";
    int64_t base;
    int i;

    if (size <= 999) {
        snprintf(buf, buf_size, "%" PRId64, size);
    } else {
        base = 1024;
        for(i = 0; i < NB_SUFFIXES; i++) {
            if (size < (10 * base)) {
                snprintf(buf, buf_size, "%0.1f%c",
                         (double)size / base,
                         suffixes[i]);
                break;
            } else if (size < (1000 * base) || i == (NB_SUFFIXES - 1)) {
                snprintf(buf, buf_size, "%" PRId64 "%c",
                         ((size + (base >> 1)) / base),
                         suffixes[i]);
                break;
            }
            base = base * 1024;
        }
    }
    return buf;
}

char *bdrv_snapshot_dump(char *buf, int buf_size, QEMUSnapshotInfo *sn)
{
    char buf1[128], date_buf[128], clock_buf[128];
#ifdef _WIN32
    struct tm *ptm;
#else
    struct tm tm;
#endif
    time_t ti;
    int64_t secs;

    if (!sn) {
        snprintf(buf, buf_size,
                 "%-10s%-20s%7s%20s%15s",
                 "ID", "TAG", "VM SIZE", "DATE", "VM CLOCK");
    } else {
        ti = sn->date_sec;
#ifdef _WIN32
        ptm = localtime(&ti);
        strftime(date_buf, sizeof(date_buf),
                 "%Y-%m-%d %H:%M:%S", ptm);
#else
        localtime_r(&ti, &tm);
        strftime(date_buf, sizeof(date_buf),
                 "%Y-%m-%d %H:%M:%S", &tm);
#endif
        secs = sn->vm_clock_nsec / 1000000000;
        snprintf(clock_buf, sizeof(clock_buf),
                 "%02d:%02d:%02d.%03d",
                 (int)(secs / 3600),
                 (int)((secs / 60) % 60),
                 (int)(secs % 60),
                 (int)((sn->vm_clock_nsec / 1000000) % 1000));
        snprintf(buf, buf_size,
                 "%-10s%-20s%7s%20s%15s",
                 sn->id_str, sn->name,
                 get_human_readable_size(buf1, sizeof(buf1), sn->vm_state_size),
                 date_buf,
                 clock_buf);
    }
    return buf;
}

/**************************************************************/
/* async I/Os */

BlockDriverAIOCB *bdrv_aio_readv(BlockDriverState *bs, int64_t sector_num,
                                 QEMUIOVector *qiov, int nb_sectors,
                                 BlockDriverCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_readv(bs, sector_num, nb_sectors, opaque);

    return bdrv_co_aio_rw_vector(bs, sector_num, qiov, nb_sectors,
                                 cb, opaque, false);
}

BlockDriverAIOCB *bdrv_aio_writev(BlockDriverState *bs, int64_t sector_num,
                                  QEMUIOVector *qiov, int nb_sectors,
                                  BlockDriverCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_writev(bs, sector_num, nb_sectors, opaque);

    return bdrv_co_aio_rw_vector(bs, sector_num, qiov, nb_sectors,
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
            qemu_iovec_concat(qiov, reqs[outidx].qiov, size);

            // We should need to add any zeros between the two requests
            assert (reqs[i].sector <= oldreq_last);

            // Add the second request
            qemu_iovec_concat(qiov, reqs[i].qiov, reqs[i].qiov->size);

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
        bdrv_aio_writev(bs, reqs[i].sector, reqs[i].qiov,
            reqs[i].nb_sectors, multiwrite_cb, mcb);
    }

    return 0;
}

void bdrv_aio_cancel(BlockDriverAIOCB *acb)
{
    acb->pool->cancel(acb);
}

/* block I/O throttling */
static bool bdrv_exceed_bps_limits(BlockDriverState *bs, int nb_sectors,
                 bool is_write, double elapsed_time, uint64_t *wait)
{
    uint64_t bps_limit = 0;
    double   bytes_limit, bytes_base, bytes_res;
    double   slice_time, wait_time;

    if (bs->io_limits.bps[BLOCK_IO_LIMIT_TOTAL]) {
        bps_limit = bs->io_limits.bps[BLOCK_IO_LIMIT_TOTAL];
    } else if (bs->io_limits.bps[is_write]) {
        bps_limit = bs->io_limits.bps[is_write];
    } else {
        if (wait) {
            *wait = 0;
        }

        return false;
    }

    slice_time = bs->slice_end - bs->slice_start;
    slice_time /= (NANOSECONDS_PER_SECOND);
    bytes_limit = bps_limit * slice_time;
    bytes_base  = bs->nr_bytes[is_write] - bs->io_base.bytes[is_write];
    if (bs->io_limits.bps[BLOCK_IO_LIMIT_TOTAL]) {
        bytes_base += bs->nr_bytes[!is_write] - bs->io_base.bytes[!is_write];
    }

    /* bytes_base: the bytes of data which have been read/written; and
     *             it is obtained from the history statistic info.
     * bytes_res: the remaining bytes of data which need to be read/written.
     * (bytes_base + bytes_res) / bps_limit: used to calcuate
     *             the total time for completing reading/writting all data.
     */
    bytes_res   = (unsigned) nb_sectors * BDRV_SECTOR_SIZE;

    if (bytes_base + bytes_res <= bytes_limit) {
        if (wait) {
            *wait = 0;
        }

        return false;
    }

    /* Calc approx time to dispatch */
    wait_time = (bytes_base + bytes_res) / bps_limit - elapsed_time;

    /* When the I/O rate at runtime exceeds the limits,
     * bs->slice_end need to be extended in order that the current statistic
     * info can be kept until the timer fire, so it is increased and tuned
     * based on the result of experiment.
     */
    bs->slice_time = wait_time * BLOCK_IO_SLICE_TIME * 10;
    bs->slice_end += bs->slice_time - 3 * BLOCK_IO_SLICE_TIME;
    if (wait) {
        *wait = wait_time * BLOCK_IO_SLICE_TIME * 10;
    }

    return true;
}

static bool bdrv_exceed_iops_limits(BlockDriverState *bs, bool is_write,
                             double elapsed_time, uint64_t *wait)
{
    uint64_t iops_limit = 0;
    double   ios_limit, ios_base;
    double   slice_time, wait_time;

    if (bs->io_limits.iops[BLOCK_IO_LIMIT_TOTAL]) {
        iops_limit = bs->io_limits.iops[BLOCK_IO_LIMIT_TOTAL];
    } else if (bs->io_limits.iops[is_write]) {
        iops_limit = bs->io_limits.iops[is_write];
    } else {
        if (wait) {
            *wait = 0;
        }

        return false;
    }

    slice_time = bs->slice_end - bs->slice_start;
    slice_time /= (NANOSECONDS_PER_SECOND);
    ios_limit  = iops_limit * slice_time;
    ios_base   = bs->nr_ops[is_write] - bs->io_base.ios[is_write];
    if (bs->io_limits.iops[BLOCK_IO_LIMIT_TOTAL]) {
        ios_base += bs->nr_ops[!is_write] - bs->io_base.ios[!is_write];
    }

    if (ios_base + 1 <= ios_limit) {
        if (wait) {
            *wait = 0;
        }

        return false;
    }

    /* Calc approx time to dispatch */
    wait_time = (ios_base + 1) / iops_limit;
    if (wait_time > elapsed_time) {
        wait_time = wait_time - elapsed_time;
    } else {
        wait_time = 0;
    }

    bs->slice_time = wait_time * BLOCK_IO_SLICE_TIME * 10;
    bs->slice_end += bs->slice_time - 3 * BLOCK_IO_SLICE_TIME;
    if (wait) {
        *wait = wait_time * BLOCK_IO_SLICE_TIME * 10;
    }

    return true;
}

static bool bdrv_exceed_io_limits(BlockDriverState *bs, int nb_sectors,
                           bool is_write, int64_t *wait)
{
    int64_t  now, max_wait;
    uint64_t bps_wait = 0, iops_wait = 0;
    double   elapsed_time;
    int      bps_ret, iops_ret;

    now = qemu_get_clock_ns(vm_clock);
    if ((bs->slice_start < now)
        && (bs->slice_end > now)) {
        bs->slice_end = now + bs->slice_time;
    } else {
        bs->slice_time  =  5 * BLOCK_IO_SLICE_TIME;
        bs->slice_start = now;
        bs->slice_end   = now + bs->slice_time;

        bs->io_base.bytes[is_write]  = bs->nr_bytes[is_write];
        bs->io_base.bytes[!is_write] = bs->nr_bytes[!is_write];

        bs->io_base.ios[is_write]    = bs->nr_ops[is_write];
        bs->io_base.ios[!is_write]   = bs->nr_ops[!is_write];
    }

    elapsed_time  = now - bs->slice_start;
    elapsed_time  /= (NANOSECONDS_PER_SECOND);

    bps_ret  = bdrv_exceed_bps_limits(bs, nb_sectors,
                                      is_write, elapsed_time, &bps_wait);
    iops_ret = bdrv_exceed_iops_limits(bs, is_write,
                                      elapsed_time, &iops_wait);
    if (bps_ret || iops_ret) {
        max_wait = bps_wait > iops_wait ? bps_wait : iops_wait;
        if (wait) {
            *wait = max_wait;
        }

        now = qemu_get_clock_ns(vm_clock);
        if (bs->slice_end < now + max_wait) {
            bs->slice_end = now + max_wait;
        }

        return true;
    }

    if (wait) {
        *wait = 0;
    }

    return false;
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

static AIOPool bdrv_em_aio_pool = {
    .aiocb_size         = sizeof(BlockDriverAIOCBSync),
    .cancel             = bdrv_aio_cancel_em,
};

static void bdrv_aio_bh_cb(void *opaque)
{
    BlockDriverAIOCBSync *acb = opaque;

    if (!acb->is_write)
        qemu_iovec_from_buffer(acb->qiov, acb->bounce, acb->qiov->size);
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

    acb = qemu_aio_get(&bdrv_em_aio_pool, bs, cb, opaque);
    acb->is_write = is_write;
    acb->qiov = qiov;
    acb->bounce = qemu_blockalign(bs, qiov->size);
    acb->bh = qemu_bh_new(bdrv_aio_bh_cb, acb);

    if (is_write) {
        qemu_iovec_to_buffer(acb->qiov, acb->bounce);
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
    QEMUBH* bh;
} BlockDriverAIOCBCoroutine;

static void bdrv_aio_co_cancel_em(BlockDriverAIOCB *blockacb)
{
    qemu_aio_flush();
}

static AIOPool bdrv_em_co_aio_pool = {
    .aiocb_size         = sizeof(BlockDriverAIOCBCoroutine),
    .cancel             = bdrv_aio_co_cancel_em,
};

static void bdrv_co_em_bh(void *opaque)
{
    BlockDriverAIOCBCoroutine *acb = opaque;

    acb->common.cb(acb->common.opaque, acb->req.error);
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
            acb->req.nb_sectors, acb->req.qiov, 0);
    } else {
        acb->req.error = bdrv_co_do_writev(bs, acb->req.sector,
            acb->req.nb_sectors, acb->req.qiov, 0);
    }

    acb->bh = qemu_bh_new(bdrv_co_em_bh, acb);
    qemu_bh_schedule(acb->bh);
}

static BlockDriverAIOCB *bdrv_co_aio_rw_vector(BlockDriverState *bs,
                                               int64_t sector_num,
                                               QEMUIOVector *qiov,
                                               int nb_sectors,
                                               BlockDriverCompletionFunc *cb,
                                               void *opaque,
                                               bool is_write)
{
    Coroutine *co;
    BlockDriverAIOCBCoroutine *acb;

    acb = qemu_aio_get(&bdrv_em_co_aio_pool, bs, cb, opaque);
    acb->req.sector = sector_num;
    acb->req.nb_sectors = nb_sectors;
    acb->req.qiov = qiov;
    acb->is_write = is_write;

    co = qemu_coroutine_create(bdrv_co_do_rw);
    qemu_coroutine_enter(co, acb);

    return &acb->common;
}

static void coroutine_fn bdrv_aio_flush_co_entry(void *opaque)
{
    BlockDriverAIOCBCoroutine *acb = opaque;
    BlockDriverState *bs = acb->common.bs;

    acb->req.error = bdrv_co_flush(bs);
    acb->bh = qemu_bh_new(bdrv_co_em_bh, acb);
    qemu_bh_schedule(acb->bh);
}

BlockDriverAIOCB *bdrv_aio_flush(BlockDriverState *bs,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    trace_bdrv_aio_flush(bs, opaque);

    Coroutine *co;
    BlockDriverAIOCBCoroutine *acb;

    acb = qemu_aio_get(&bdrv_em_co_aio_pool, bs, cb, opaque);
    co = qemu_coroutine_create(bdrv_aio_flush_co_entry);
    qemu_coroutine_enter(co, acb);

    return &acb->common;
}

static void coroutine_fn bdrv_aio_discard_co_entry(void *opaque)
{
    BlockDriverAIOCBCoroutine *acb = opaque;
    BlockDriverState *bs = acb->common.bs;

    acb->req.error = bdrv_co_discard(bs, acb->req.sector, acb->req.nb_sectors);
    acb->bh = qemu_bh_new(bdrv_co_em_bh, acb);
    qemu_bh_schedule(acb->bh);
}

BlockDriverAIOCB *bdrv_aio_discard(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    Coroutine *co;
    BlockDriverAIOCBCoroutine *acb;

    trace_bdrv_aio_discard(bs, sector_num, nb_sectors, opaque);

    acb = qemu_aio_get(&bdrv_em_co_aio_pool, bs, cb, opaque);
    acb->req.sector = sector_num;
    acb->req.nb_sectors = nb_sectors;
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

void *qemu_aio_get(AIOPool *pool, BlockDriverState *bs,
                   BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriverAIOCB *acb;

    if (pool->free_aiocb) {
        acb = pool->free_aiocb;
        pool->free_aiocb = acb->next;
    } else {
        acb = g_malloc0(pool->aiocb_size);
        acb->pool = pool;
    }
    acb->bs = bs;
    acb->cb = cb;
    acb->opaque = opaque;
    return acb;
}

void qemu_aio_release(void *p)
{
    BlockDriverAIOCB *acb = (BlockDriverAIOCB *)p;
    AIOPool *pool = acb->pool;
    acb->next = pool->free_aiocb;
    pool->free_aiocb = acb;
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

    if (!bs->drv) {
        return 0;
    }

    /* Write back cached data to the OS even with cache=unsafe */
    if (bs->drv->bdrv_co_flush_to_os) {
        ret = bs->drv->bdrv_co_flush_to_os(bs);
        if (ret < 0) {
            return ret;
        }
    }

    /* But don't actually force it to the disk with cache=unsafe */
    if (bs->open_flags & BDRV_O_NO_FLUSH) {
        return 0;
    }

    if (bs->drv->bdrv_co_flush_to_disk) {
        return bs->drv->bdrv_co_flush_to_disk(bs);
    } else if (bs->drv->bdrv_aio_flush) {
        BlockDriverAIOCB *acb;
        CoroutineIOCompletion co = {
            .coroutine = qemu_coroutine_self(),
        };

        acb = bs->drv->bdrv_aio_flush(bs, bdrv_co_io_em_complete, &co);
        if (acb == NULL) {
            return -EIO;
        } else {
            qemu_coroutine_yield();
            return co.ret;
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
        return 0;
    }
}

void bdrv_invalidate_cache(BlockDriverState *bs)
{
    if (bs->drv && bs->drv->bdrv_invalidate_cache) {
        bs->drv->bdrv_invalidate_cache(bs);
    }
}

void bdrv_invalidate_cache_all(void)
{
    BlockDriverState *bs;

    QTAILQ_FOREACH(bs, &bdrv_states, list) {
        bdrv_invalidate_cache(bs);
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
        co = qemu_coroutine_create(bdrv_flush_co_entry);
        qemu_coroutine_enter(co, &rwco);
        while (rwco.ret == NOT_DONE) {
            qemu_aio_wait();
        }
    }

    return rwco.ret;
}

static void coroutine_fn bdrv_discard_co_entry(void *opaque)
{
    RwCo *rwco = opaque;

    rwco->ret = bdrv_co_discard(rwco->bs, rwco->sector_num, rwco->nb_sectors);
}

int coroutine_fn bdrv_co_discard(BlockDriverState *bs, int64_t sector_num,
                                 int nb_sectors)
{
    if (!bs->drv) {
        return -ENOMEDIUM;
    } else if (bdrv_check_request(bs, sector_num, nb_sectors)) {
        return -EIO;
    } else if (bs->read_only) {
        return -EROFS;
    } else if (bs->drv->bdrv_co_discard) {
        return bs->drv->bdrv_co_discard(bs, sector_num, nb_sectors);
    } else if (bs->drv->bdrv_aio_discard) {
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
            return co.ret;
        }
    } else {
        return 0;
    }
}

int bdrv_discard(BlockDriverState *bs, int64_t sector_num, int nb_sectors)
{
    Coroutine *co;
    RwCo rwco = {
        .bs = bs,
        .sector_num = sector_num,
        .nb_sectors = nb_sectors,
        .ret = NOT_DONE,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_discard_co_entry(&rwco);
    } else {
        co = qemu_coroutine_create(bdrv_discard_co_entry);
        qemu_coroutine_enter(co, &rwco);
        while (rwco.ret == NOT_DONE) {
            qemu_aio_wait();
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
        bdrv_emit_qmp_eject_event(bs, eject_flag);
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

void bdrv_set_buffer_alignment(BlockDriverState *bs, int align)
{
    bs->buffer_alignment = align;
}

void *qemu_blockalign(BlockDriverState *bs, size_t size)
{
    return qemu_memalign((bs && bs->buffer_alignment) ? bs->buffer_alignment : 512, size);
}

void bdrv_set_dirty_tracking(BlockDriverState *bs, int enable)
{
    int64_t bitmap_size;

    bs->dirty_count = 0;
    if (enable) {
        if (!bs->dirty_bitmap) {
            bitmap_size = (bdrv_getlength(bs) >> BDRV_SECTOR_BITS) +
                    BDRV_SECTORS_PER_DIRTY_CHUNK * 8 - 1;
            bitmap_size /= BDRV_SECTORS_PER_DIRTY_CHUNK * 8;

            bs->dirty_bitmap = g_malloc0(bitmap_size);
        }
    } else {
        if (bs->dirty_bitmap) {
            g_free(bs->dirty_bitmap);
            bs->dirty_bitmap = NULL;
        }
    }
}

int bdrv_get_dirty(BlockDriverState *bs, int64_t sector)
{
    int64_t chunk = sector / (int64_t)BDRV_SECTORS_PER_DIRTY_CHUNK;

    if (bs->dirty_bitmap &&
        (sector << BDRV_SECTOR_BITS) < bdrv_getlength(bs)) {
        return !!(bs->dirty_bitmap[chunk / (sizeof(unsigned long) * 8)] &
            (1UL << (chunk % (sizeof(unsigned long) * 8))));
    } else {
        return 0;
    }
}

void bdrv_reset_dirty(BlockDriverState *bs, int64_t cur_sector,
                      int nr_sectors)
{
    set_dirty_bitmap(bs, cur_sector, nr_sectors, 0);
}

int64_t bdrv_get_dirty_count(BlockDriverState *bs)
{
    return bs->dirty_count;
}

void bdrv_set_in_use(BlockDriverState *bs, int in_use)
{
    assert(bs->in_use != in_use);
    bs->in_use = in_use;
}

int bdrv_in_use(BlockDriverState *bs)
{
    return bs->in_use;
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
           (bs->on_write_error == BLOCK_ERR_STOP_ENOSPC ||
            bs->on_write_error == BLOCK_ERR_STOP_ANY    ||
            bs->on_read_error == BLOCK_ERR_STOP_ANY));
}

void bdrv_iostatus_disable(BlockDriverState *bs)
{
    bs->iostatus_enabled = false;
}

void bdrv_iostatus_reset(BlockDriverState *bs)
{
    if (bdrv_iostatus_is_enabled(bs)) {
        bs->iostatus = BLOCK_DEVICE_IO_STATUS_OK;
    }
}

/* XXX: Today this is set by device models because it makes the implementation
   quite simple. However, the block layer knows about the error, so it's
   possible to implement this without device models being involved */
void bdrv_iostatus_set_err(BlockDriverState *bs, int error)
{
    if (bdrv_iostatus_is_enabled(bs) &&
        bs->iostatus == BLOCK_DEVICE_IO_STATUS_OK) {
        assert(error >= 0);
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

int bdrv_img_create(const char *filename, const char *fmt,
                    const char *base_filename, const char *base_fmt,
                    char *options, uint64_t img_size, int flags)
{
    QEMUOptionParameter *param = NULL, *create_options = NULL;
    QEMUOptionParameter *backing_fmt, *backing_file, *size;
    BlockDriverState *bs = NULL;
    BlockDriver *drv, *proto_drv;
    BlockDriver *backing_drv = NULL;
    int ret = 0;

    /* Find driver and parse its options */
    drv = bdrv_find_format(fmt);
    if (!drv) {
        error_report("Unknown file format '%s'", fmt);
        ret = -EINVAL;
        goto out;
    }

    proto_drv = bdrv_find_protocol(filename);
    if (!proto_drv) {
        error_report("Unknown protocol '%s'", filename);
        ret = -EINVAL;
        goto out;
    }

    create_options = append_option_parameters(create_options,
                                              drv->create_options);
    create_options = append_option_parameters(create_options,
                                              proto_drv->create_options);

    /* Create parameter list with default values */
    param = parse_option_parameters("", create_options, param);

    set_option_parameter_int(param, BLOCK_OPT_SIZE, img_size);

    /* Parse -o options */
    if (options) {
        param = parse_option_parameters(options, create_options, param);
        if (param == NULL) {
            error_report("Invalid options for file format '%s'.", fmt);
            ret = -EINVAL;
            goto out;
        }
    }

    if (base_filename) {
        if (set_option_parameter(param, BLOCK_OPT_BACKING_FILE,
                                 base_filename)) {
            error_report("Backing file not supported for file format '%s'",
                         fmt);
            ret = -EINVAL;
            goto out;
        }
    }

    if (base_fmt) {
        if (set_option_parameter(param, BLOCK_OPT_BACKING_FMT, base_fmt)) {
            error_report("Backing file format not supported for file "
                         "format '%s'", fmt);
            ret = -EINVAL;
            goto out;
        }
    }

    backing_file = get_option_parameter(param, BLOCK_OPT_BACKING_FILE);
    if (backing_file && backing_file->value.s) {
        if (!strcmp(filename, backing_file->value.s)) {
            error_report("Error: Trying to create an image with the "
                         "same filename as the backing file");
            ret = -EINVAL;
            goto out;
        }
    }

    backing_fmt = get_option_parameter(param, BLOCK_OPT_BACKING_FMT);
    if (backing_fmt && backing_fmt->value.s) {
        backing_drv = bdrv_find_format(backing_fmt->value.s);
        if (!backing_drv) {
            error_report("Unknown backing file format '%s'",
                         backing_fmt->value.s);
            ret = -EINVAL;
            goto out;
        }
    }

    // The size for the image must always be specified, with one exception:
    // If we are using a backing file, we can obtain the size from there
    size = get_option_parameter(param, BLOCK_OPT_SIZE);
    if (size && size->value.n == -1) {
        if (backing_file && backing_file->value.s) {
            uint64_t size;
            char buf[32];

            bs = bdrv_new("");

            ret = bdrv_open(bs, backing_file->value.s, flags, backing_drv);
            if (ret < 0) {
                error_report("Could not open '%s'", backing_file->value.s);
                goto out;
            }
            bdrv_get_geometry(bs, &size);
            size *= 512;

            snprintf(buf, sizeof(buf), "%" PRId64, size);
            set_option_parameter(param, BLOCK_OPT_SIZE, buf);
        } else {
            error_report("Image creation needs a size parameter");
            ret = -EINVAL;
            goto out;
        }
    }

    printf("Formatting '%s', fmt=%s ", filename, fmt);
    print_option_parameters(param);
    puts("");

    ret = bdrv_create(drv, filename, param);

    if (ret < 0) {
        if (ret == -ENOTSUP) {
            error_report("Formatting or formatting option not supported for "
                         "file format '%s'", fmt);
        } else if (ret == -EFBIG) {
            error_report("The image size is too large for file format '%s'",
                         fmt);
        } else {
            error_report("%s: error while creating %s: %s", filename, fmt,
                         strerror(-ret));
        }
    }

out:
    free_option_parameters(create_options);
    free_option_parameters(param);

    if (bs) {
        bdrv_delete(bs);
    }

    return ret;
}

void *block_job_create(const BlockJobType *job_type, BlockDriverState *bs,
                       BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockJob *job;

    if (bs->job || bdrv_in_use(bs)) {
        return NULL;
    }
    bdrv_set_in_use(bs, 1);

    job = g_malloc0(job_type->instance_size);
    job->job_type      = job_type;
    job->bs            = bs;
    job->cb            = cb;
    job->opaque        = opaque;
    bs->job = job;
    return job;
}

void block_job_complete(BlockJob *job, int ret)
{
    BlockDriverState *bs = job->bs;

    assert(bs->job == job);
    job->cb(job->opaque, ret);
    bs->job = NULL;
    g_free(job);
    bdrv_set_in_use(bs, 0);
}

int block_job_set_speed(BlockJob *job, int64_t value)
{
    if (!job->job_type->set_speed) {
        return -ENOTSUP;
    }
    return job->job_type->set_speed(job, value);
}

void block_job_cancel(BlockJob *job)
{
    job->cancelled = true;
}

bool block_job_is_cancelled(BlockJob *job)
{
    return job->cancelled;
}
