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

#include "qemu/osdep.h"
#include "block/trace.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "block/nbd.h"
#include "block/qdict.h"
#include "qemu/error-report.h"
#include "module_block.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qapi-visit-block-core.h"
#include "sysemu/block-backend.h"
#include "sysemu/sysemu.h"
#include "qemu/notify.h"
#include "qemu/option.h"
#include "qemu/coroutine.h"
#include "block/qapi.h"
#include "qemu/timer.h"
#include "qemu/cutils.h"
#include "qemu/id.h"

#ifdef CONFIG_BSD
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

static QTAILQ_HEAD(, BlockDriverState) graph_bdrv_states =
    QTAILQ_HEAD_INITIALIZER(graph_bdrv_states);

static QTAILQ_HEAD(, BlockDriverState) all_bdrv_states =
    QTAILQ_HEAD_INITIALIZER(all_bdrv_states);

static QLIST_HEAD(, BlockDriver) bdrv_drivers =
    QLIST_HEAD_INITIALIZER(bdrv_drivers);

static BlockDriverState *bdrv_open_inherit(const char *filename,
                                           const char *reference,
                                           QDict *options, int flags,
                                           BlockDriverState *parent,
                                           const BdrvChildRole *child_role,
                                           Error **errp);

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

size_t bdrv_opt_mem_align(BlockDriverState *bs)
{
    if (!bs || !bs->drv) {
        /* page size or 4k (hdd sector size) should be on the safe side */
        return MAX(4096, qemu_real_host_page_size);
    }

    return bs->bl.opt_mem_alignment;
}

size_t bdrv_min_mem_align(BlockDriverState *bs)
{
    if (!bs || !bs->drv) {
        /* page size or 4k (hdd sector size) should be on the safe side */
        return MAX(4096, qemu_real_host_page_size);
    }

    return bs->bl.min_mem_alignment;
}

/* check if the path starts with "<protocol>:" */
int path_has_protocol(const char *path)
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

/* if filename is absolute, just return its duplicate. Otherwise, build a
   path to it by considering it is relative to base_path. URL are
   supported. */
char *path_combine(const char *base_path, const char *filename)
{
    const char *protocol_stripped = NULL;
    const char *p, *p1;
    char *result;
    int len;

    if (path_is_absolute(filename)) {
        return g_strdup(filename);
    }

    if (path_has_protocol(base_path)) {
        protocol_stripped = strchr(base_path, ':');
        if (protocol_stripped) {
            protocol_stripped++;
        }
    }
    p = protocol_stripped ?: base_path;

    p1 = strrchr(base_path, '/');
#ifdef _WIN32
    {
        const char *p2;
        p2 = strrchr(base_path, '\\');
        if (!p1 || p2 > p1) {
            p1 = p2;
        }
    }
#endif
    if (p1) {
        p1++;
    } else {
        p1 = base_path;
    }
    if (p1 > p) {
        p = p1;
    }
    len = p - base_path;

    result = g_malloc(len + strlen(filename) + 1);
    memcpy(result, base_path, len);
    strcpy(result + len, filename);

    return result;
}

/*
 * Helper function for bdrv_parse_filename() implementations to remove optional
 * protocol prefixes (especially "file:") from a filename and for putting the
 * stripped filename into the options QDict if there is such a prefix.
 */
void bdrv_parse_filename_strip_prefix(const char *filename, const char *prefix,
                                      QDict *options)
{
    if (strstart(filename, prefix, &filename)) {
        /* Stripping the explicit protocol prefix may result in a protocol
         * prefix being (wrongly) detected (if the filename contains a colon) */
        if (path_has_protocol(filename)) {
            QString *fat_filename;

            /* This means there is some colon before the first slash; therefore,
             * this cannot be an absolute path */
            assert(!path_is_absolute(filename));

            /* And we can thus fix the protocol detection issue by prefixing it
             * by "./" */
            fat_filename = qstring_from_str("./");
            qstring_append(fat_filename, filename);

            assert(!path_has_protocol(qstring_get_str(fat_filename)));

            qdict_put(options, "filename", fat_filename);
        } else {
            /* If no protocol prefix was detected, we can use the shortened
             * filename as-is */
            qdict_put_str(options, "filename", filename);
        }
    }
}


/* Returns whether the image file is opened as read-only. Note that this can
 * return false and writing to the image file is still not possible because the
 * image is inactivated. */
bool bdrv_is_read_only(BlockDriverState *bs)
{
    return bs->read_only;
}

int bdrv_can_set_read_only(BlockDriverState *bs, bool read_only,
                           bool ignore_allow_rdw, Error **errp)
{
    /* Do not set read_only if copy_on_read is enabled */
    if (bs->copy_on_read && read_only) {
        error_setg(errp, "Can't set node '%s' to r/o with copy-on-read enabled",
                   bdrv_get_device_or_node_name(bs));
        return -EINVAL;
    }

    /* Do not clear read_only if it is prohibited */
    if (!read_only && !(bs->open_flags & BDRV_O_ALLOW_RDWR) &&
        !ignore_allow_rdw)
    {
        error_setg(errp, "Node '%s' is read only",
                   bdrv_get_device_or_node_name(bs));
        return -EPERM;
    }

    return 0;
}

/*
 * Called by a driver that can only provide a read-only image.
 *
 * Returns 0 if the node is already read-only or it could switch the node to
 * read-only because BDRV_O_AUTO_RDONLY is set.
 *
 * Returns -EACCES if the node is read-write and BDRV_O_AUTO_RDONLY is not set
 * or bdrv_can_set_read_only() forbids making the node read-only. If @errmsg
 * is not NULL, it is used as the error message for the Error object.
 */
int bdrv_apply_auto_read_only(BlockDriverState *bs, const char *errmsg,
                              Error **errp)
{
    int ret = 0;

    if (!(bs->open_flags & BDRV_O_RDWR)) {
        return 0;
    }
    if (!(bs->open_flags & BDRV_O_AUTO_RDONLY)) {
        goto fail;
    }

    ret = bdrv_can_set_read_only(bs, true, false, NULL);
    if (ret < 0) {
        goto fail;
    }

    bs->read_only = true;
    bs->open_flags &= ~BDRV_O_RDWR;

    return 0;

fail:
    error_setg(errp, "%s", errmsg ?: "Image is read-only");
    return -EACCES;
}

/*
 * If @backing is empty, this function returns NULL without setting
 * @errp.  In all other cases, NULL will only be returned with @errp
 * set.
 *
 * Therefore, a return value of NULL without @errp set means that
 * there is no backing file; if @errp is set, there is one but its
 * absolute filename cannot be generated.
 */
char *bdrv_get_full_backing_filename_from_filename(const char *backed,
                                                   const char *backing,
                                                   Error **errp)
{
    if (backing[0] == '\0') {
        return NULL;
    } else if (path_has_protocol(backing) || path_is_absolute(backing)) {
        return g_strdup(backing);
    } else if (backed[0] == '\0' || strstart(backed, "json:", NULL)) {
        error_setg(errp, "Cannot use relative backing file names for '%s'",
                   backed);
        return NULL;
    } else {
        return path_combine(backed, backing);
    }
}

/*
 * If @filename is empty or NULL, this function returns NULL without
 * setting @errp.  In all other cases, NULL will only be returned with
 * @errp set.
 */
static char *bdrv_make_absolute_filename(BlockDriverState *relative_to,
                                         const char *filename, Error **errp)
{
    char *dir, *full_name;

    if (!filename || filename[0] == '\0') {
        return NULL;
    } else if (path_has_protocol(filename) || path_is_absolute(filename)) {
        return g_strdup(filename);
    }

    dir = bdrv_dirname(relative_to, errp);
    if (!dir) {
        return NULL;
    }

    full_name = g_strconcat(dir, filename, NULL);
    g_free(dir);
    return full_name;
}

char *bdrv_get_full_backing_filename(BlockDriverState *bs, Error **errp)
{
    return bdrv_make_absolute_filename(bs, bs->backing_file, errp);
}

void bdrv_register(BlockDriver *bdrv)
{
    QLIST_INSERT_HEAD(&bdrv_drivers, bdrv, list);
}

BlockDriverState *bdrv_new(void)
{
    BlockDriverState *bs;
    int i;

    bs = g_new0(BlockDriverState, 1);
    QLIST_INIT(&bs->dirty_bitmaps);
    for (i = 0; i < BLOCK_OP_TYPE_MAX; i++) {
        QLIST_INIT(&bs->op_blockers[i]);
    }
    notifier_with_return_list_init(&bs->before_write_notifiers);
    qemu_co_mutex_init(&bs->reqs_lock);
    qemu_mutex_init(&bs->dirty_bitmap_mutex);
    bs->refcnt = 1;
    bs->aio_context = qemu_get_aio_context();

    qemu_co_queue_init(&bs->flush_queue);

    for (i = 0; i < bdrv_drain_all_count; i++) {
        bdrv_drained_begin(bs);
    }

    QTAILQ_INSERT_TAIL(&all_bdrv_states, bs, bs_list);

    return bs;
}

static BlockDriver *bdrv_do_find_format(const char *format_name)
{
    BlockDriver *drv1;

    QLIST_FOREACH(drv1, &bdrv_drivers, list) {
        if (!strcmp(drv1->format_name, format_name)) {
            return drv1;
        }
    }

    return NULL;
}

BlockDriver *bdrv_find_format(const char *format_name)
{
    BlockDriver *drv1;
    int i;

    drv1 = bdrv_do_find_format(format_name);
    if (drv1) {
        return drv1;
    }

    /* The driver isn't registered, maybe we need to load a module */
    for (i = 0; i < (int)ARRAY_SIZE(block_driver_modules); ++i) {
        if (!strcmp(block_driver_modules[i].format_name, format_name)) {
            block_module_load_one(block_driver_modules[i].library_name);
            break;
        }
    }

    return bdrv_do_find_format(format_name);
}

static int bdrv_format_is_whitelisted(const char *format_name, bool read_only)
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
        if (!strcmp(format_name, *p)) {
            return 1;
        }
    }
    if (read_only) {
        for (p = whitelist_ro; *p; p++) {
            if (!strcmp(format_name, *p)) {
                return 1;
            }
        }
    }
    return 0;
}

int bdrv_is_whitelisted(BlockDriver *drv, bool read_only)
{
    return bdrv_format_is_whitelisted(drv->format_name, read_only);
}

bool bdrv_uses_whitelist(void)
{
    return use_bdrv_whitelist;
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

    ret = cco->drv->bdrv_co_create_opts(cco->filename, cco->opts, &local_err);
    error_propagate(&cco->err, local_err);
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

    if (!drv->bdrv_co_create_opts) {
        error_setg(errp, "Driver '%s' does not support image creation", drv->format_name);
        ret = -ENOTSUP;
        goto out;
    }

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_create_co_entry(&cco);
    } else {
        co = qemu_coroutine_create(bdrv_create_co_entry, &cco);
        qemu_coroutine_enter(co);
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

    drv = bdrv_find_protocol(filename, true, errp);
    if (drv == NULL) {
        return -ENOENT;
    }

    ret = bdrv_create(drv, filename, opts, &local_err);
    error_propagate(errp, local_err);
    return ret;
}

/**
 * Try to get @bs's logical and physical block size.
 * On success, store them in @bsz struct and return 0.
 * On failure return -errno.
 * @bs must not be empty.
 */
int bdrv_probe_blocksizes(BlockDriverState *bs, BlockSizes *bsz)
{
    BlockDriver *drv = bs->drv;

    if (drv && drv->bdrv_probe_blocksizes) {
        return drv->bdrv_probe_blocksizes(bs, bsz);
    } else if (drv && drv->is_filter && bs->file) {
        return bdrv_probe_blocksizes(bs->file->bs, bsz);
    }

    return -ENOTSUP;
}

/**
 * Try to get @bs's geometry (cyls, heads, sectors).
 * On success, store them in @geo struct and return 0.
 * On failure return -errno.
 * @bs must not be empty.
 */
int bdrv_probe_geometry(BlockDriverState *bs, HDGeometry *geo)
{
    BlockDriver *drv = bs->drv;

    if (drv && drv->bdrv_probe_geometry) {
        return drv->bdrv_probe_geometry(bs, geo);
    } else if (drv && drv->is_filter && bs->file) {
        return bdrv_probe_geometry(bs->file->bs, geo);
    }

    return -ENOTSUP;
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

static BlockDriver *bdrv_do_find_protocol(const char *protocol)
{
    BlockDriver *drv1;

    QLIST_FOREACH(drv1, &bdrv_drivers, list) {
        if (drv1->protocol_name && !strcmp(drv1->protocol_name, protocol)) {
            return drv1;
        }
    }

    return NULL;
}

BlockDriver *bdrv_find_protocol(const char *filename,
                                bool allow_protocol_prefix,
                                Error **errp)
{
    BlockDriver *drv1;
    char protocol[128];
    int len;
    const char *p;
    int i;

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
        return &bdrv_file;
    }

    p = strchr(filename, ':');
    assert(p != NULL);
    len = p - filename;
    if (len > sizeof(protocol) - 1)
        len = sizeof(protocol) - 1;
    memcpy(protocol, filename, len);
    protocol[len] = '\0';

    drv1 = bdrv_do_find_protocol(protocol);
    if (drv1) {
        return drv1;
    }

    for (i = 0; i < (int)ARRAY_SIZE(block_driver_modules); ++i) {
        if (block_driver_modules[i].protocol_name &&
            !strcmp(block_driver_modules[i].protocol_name, protocol)) {
            block_module_load_one(block_driver_modules[i].library_name);
            break;
        }
    }

    drv1 = bdrv_do_find_protocol(protocol);
    if (!drv1) {
        error_setg(errp, "Unknown protocol '%s'", protocol);
    }
    return drv1;
}

/*
 * Guess image format by probing its contents.
 * This is not a good idea when your image is raw (CVE-2008-2004), but
 * we do it anyway for backward compatibility.
 *
 * @buf         contains the image's first @buf_size bytes.
 * @buf_size    is the buffer size in bytes (generally BLOCK_PROBE_BUF_SIZE,
 *              but can be smaller if the image file is smaller)
 * @filename    is its filename.
 *
 * For all block drivers, call the bdrv_probe() method to get its
 * probing score.
 * Return the first block driver with the highest probing score.
 */
BlockDriver *bdrv_probe_all(const uint8_t *buf, int buf_size,
                            const char *filename)
{
    int score_max = 0, score;
    BlockDriver *drv = NULL, *d;

    QLIST_FOREACH(d, &bdrv_drivers, list) {
        if (d->bdrv_probe) {
            score = d->bdrv_probe(buf, buf_size, filename);
            if (score > score_max) {
                score_max = score;
                drv = d;
            }
        }
    }

    return drv;
}

static int find_image_format(BlockBackend *file, const char *filename,
                             BlockDriver **pdrv, Error **errp)
{
    BlockDriver *drv;
    uint8_t buf[BLOCK_PROBE_BUF_SIZE];
    int ret = 0;

    /* Return the raw BlockDriver * to scsi-generic devices or empty drives */
    if (blk_is_sg(file) || !blk_is_inserted(file) || blk_getlength(file) == 0) {
        *pdrv = &bdrv_raw;
        return ret;
    }

    ret = blk_pread(file, 0, buf, sizeof(buf));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read image for determining its "
                         "format");
        *pdrv = NULL;
        return ret;
    }

    drv = bdrv_probe_all(buf, ret, filename);
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
int refresh_total_sectors(BlockDriverState *bs, int64_t hint)
{
    BlockDriver *drv = bs->drv;

    if (!drv) {
        return -ENOMEDIUM;
    }

    /* Do not attempt drv->bdrv_getlength() on scsi-generic devices */
    if (bdrv_is_sg(bs))
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
 * Combines a QDict of new block driver @options with any missing options taken
 * from @old_options, so that leaving out an option defaults to its old value.
 */
static void bdrv_join_options(BlockDriverState *bs, QDict *options,
                              QDict *old_options)
{
    if (bs->drv && bs->drv->bdrv_join_options) {
        bs->drv->bdrv_join_options(options, old_options);
    } else {
        qdict_join(options, old_options, false);
    }
}

static BlockdevDetectZeroesOptions bdrv_parse_detect_zeroes(QemuOpts *opts,
                                                            int open_flags,
                                                            Error **errp)
{
    Error *local_err = NULL;
    char *value = qemu_opt_get_del(opts, "detect-zeroes");
    BlockdevDetectZeroesOptions detect_zeroes =
        qapi_enum_parse(&BlockdevDetectZeroesOptions_lookup, value,
                        BLOCKDEV_DETECT_ZEROES_OPTIONS_OFF, &local_err);
    g_free(value);
    if (local_err) {
        error_propagate(errp, local_err);
        return detect_zeroes;
    }

    if (detect_zeroes == BLOCKDEV_DETECT_ZEROES_OPTIONS_UNMAP &&
        !(open_flags & BDRV_O_UNMAP))
    {
        error_setg(errp, "setting detect-zeroes to unmap is not allowed "
                   "without setting discard operation to unmap");
    }

    return detect_zeroes;
}

/**
 * Set open flags for aio engine
 *
 * Return 0 on success, -1 if the engine specified is invalid
 */
int bdrv_parse_aio(const char *mode, int *flags)
{
    if (!strcmp(mode, "threads")) {
        /* do nothing, default */
    } else if (!strcmp(mode, "native")) {
        *flags |= BDRV_O_NATIVE_AIO;
#ifdef CONFIG_LINUX_IO_URING
    } else if (!strcmp(mode, "io_uring")) {
        *flags |= BDRV_O_IO_URING;
#endif
    } else {
        return -1;
    }

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
int bdrv_parse_cache_mode(const char *mode, int *flags, bool *writethrough)
{
    *flags &= ~BDRV_O_CACHE_MASK;

    if (!strcmp(mode, "off") || !strcmp(mode, "none")) {
        *writethrough = false;
        *flags |= BDRV_O_NOCACHE;
    } else if (!strcmp(mode, "directsync")) {
        *writethrough = true;
        *flags |= BDRV_O_NOCACHE;
    } else if (!strcmp(mode, "writeback")) {
        *writethrough = false;
    } else if (!strcmp(mode, "unsafe")) {
        *writethrough = false;
        *flags |= BDRV_O_NO_FLUSH;
    } else if (!strcmp(mode, "writethrough")) {
        *writethrough = true;
    } else {
        return -1;
    }

    return 0;
}

static char *bdrv_child_get_parent_desc(BdrvChild *c)
{
    BlockDriverState *parent = c->opaque;
    return g_strdup(bdrv_get_device_or_node_name(parent));
}

static void bdrv_child_cb_drained_begin(BdrvChild *child)
{
    BlockDriverState *bs = child->opaque;
    bdrv_do_drained_begin_quiesce(bs, NULL, false);
}

static bool bdrv_child_cb_drained_poll(BdrvChild *child)
{
    BlockDriverState *bs = child->opaque;
    return bdrv_drain_poll(bs, false, NULL, false);
}

static void bdrv_child_cb_drained_end(BdrvChild *child,
                                      int *drained_end_counter)
{
    BlockDriverState *bs = child->opaque;
    bdrv_drained_end_no_poll(bs, drained_end_counter);
}

static void bdrv_child_cb_attach(BdrvChild *child)
{
    BlockDriverState *bs = child->opaque;
    bdrv_apply_subtree_drain(child, bs);
}

static void bdrv_child_cb_detach(BdrvChild *child)
{
    BlockDriverState *bs = child->opaque;
    bdrv_unapply_subtree_drain(child, bs);
}

static int bdrv_child_cb_inactivate(BdrvChild *child)
{
    BlockDriverState *bs = child->opaque;
    assert(bs->open_flags & BDRV_O_INACTIVE);
    return 0;
}

static bool bdrv_child_cb_can_set_aio_ctx(BdrvChild *child, AioContext *ctx,
                                          GSList **ignore, Error **errp)
{
    BlockDriverState *bs = child->opaque;
    return bdrv_can_set_aio_context(bs, ctx, ignore, errp);
}

static void bdrv_child_cb_set_aio_ctx(BdrvChild *child, AioContext *ctx,
                                      GSList **ignore)
{
    BlockDriverState *bs = child->opaque;
    return bdrv_set_aio_context_ignore(bs, ctx, ignore);
}

/*
 * Returns the options and flags that a temporary snapshot should get, based on
 * the originally requested flags (the originally requested image will have
 * flags like a backing file)
 */
static void bdrv_temp_snapshot_options(int *child_flags, QDict *child_options,
                                       int parent_flags, QDict *parent_options)
{
    *child_flags = (parent_flags & ~BDRV_O_SNAPSHOT) | BDRV_O_TEMPORARY;

    /* For temporary files, unconditional cache=unsafe is fine */
    qdict_set_default_str(child_options, BDRV_OPT_CACHE_DIRECT, "off");
    qdict_set_default_str(child_options, BDRV_OPT_CACHE_NO_FLUSH, "on");

    /* Copy the read-only and discard options from the parent */
    qdict_copy_default(child_options, parent_options, BDRV_OPT_READ_ONLY);
    qdict_copy_default(child_options, parent_options, BDRV_OPT_DISCARD);

    /* aio=native doesn't work for cache.direct=off, so disable it for the
     * temporary snapshot */
    *child_flags &= ~BDRV_O_NATIVE_AIO;
}

/*
 * Returns the options and flags that bs->file should get if a protocol driver
 * is expected, based on the given options and flags for the parent BDS
 */
static void bdrv_inherited_options(int *child_flags, QDict *child_options,
                                   int parent_flags, QDict *parent_options)
{
    int flags = parent_flags;

    /* Enable protocol handling, disable format probing for bs->file */
    flags |= BDRV_O_PROTOCOL;

    /* If the cache mode isn't explicitly set, inherit direct and no-flush from
     * the parent. */
    qdict_copy_default(child_options, parent_options, BDRV_OPT_CACHE_DIRECT);
    qdict_copy_default(child_options, parent_options, BDRV_OPT_CACHE_NO_FLUSH);
    qdict_copy_default(child_options, parent_options, BDRV_OPT_FORCE_SHARE);

    /* Inherit the read-only option from the parent if it's not set */
    qdict_copy_default(child_options, parent_options, BDRV_OPT_READ_ONLY);
    qdict_copy_default(child_options, parent_options, BDRV_OPT_AUTO_READ_ONLY);

    /* Our block drivers take care to send flushes and respect unmap policy,
     * so we can default to enable both on lower layers regardless of the
     * corresponding parent options. */
    qdict_set_default_str(child_options, BDRV_OPT_DISCARD, "unmap");

    /* Clear flags that only apply to the top layer */
    flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING | BDRV_O_COPY_ON_READ |
               BDRV_O_NO_IO);

    *child_flags = flags;
}

const BdrvChildRole child_file = {
    .parent_is_bds   = true,
    .get_parent_desc = bdrv_child_get_parent_desc,
    .inherit_options = bdrv_inherited_options,
    .drained_begin   = bdrv_child_cb_drained_begin,
    .drained_poll    = bdrv_child_cb_drained_poll,
    .drained_end     = bdrv_child_cb_drained_end,
    .attach          = bdrv_child_cb_attach,
    .detach          = bdrv_child_cb_detach,
    .inactivate      = bdrv_child_cb_inactivate,
    .can_set_aio_ctx = bdrv_child_cb_can_set_aio_ctx,
    .set_aio_ctx     = bdrv_child_cb_set_aio_ctx,
};

/*
 * Returns the options and flags that bs->file should get if the use of formats
 * (and not only protocols) is permitted for it, based on the given options and
 * flags for the parent BDS
 */
static void bdrv_inherited_fmt_options(int *child_flags, QDict *child_options,
                                       int parent_flags, QDict *parent_options)
{
    child_file.inherit_options(child_flags, child_options,
                               parent_flags, parent_options);

    *child_flags &= ~(BDRV_O_PROTOCOL | BDRV_O_NO_IO);
}

const BdrvChildRole child_format = {
    .parent_is_bds   = true,
    .get_parent_desc = bdrv_child_get_parent_desc,
    .inherit_options = bdrv_inherited_fmt_options,
    .drained_begin   = bdrv_child_cb_drained_begin,
    .drained_poll    = bdrv_child_cb_drained_poll,
    .drained_end     = bdrv_child_cb_drained_end,
    .attach          = bdrv_child_cb_attach,
    .detach          = bdrv_child_cb_detach,
    .inactivate      = bdrv_child_cb_inactivate,
    .can_set_aio_ctx = bdrv_child_cb_can_set_aio_ctx,
    .set_aio_ctx     = bdrv_child_cb_set_aio_ctx,
};

static void bdrv_backing_attach(BdrvChild *c)
{
    BlockDriverState *parent = c->opaque;
    BlockDriverState *backing_hd = c->bs;

    assert(!parent->backing_blocker);
    error_setg(&parent->backing_blocker,
               "node is used as backing hd of '%s'",
               bdrv_get_device_or_node_name(parent));

    bdrv_refresh_filename(backing_hd);

    parent->open_flags &= ~BDRV_O_NO_BACKING;
    pstrcpy(parent->backing_file, sizeof(parent->backing_file),
            backing_hd->filename);
    pstrcpy(parent->backing_format, sizeof(parent->backing_format),
            backing_hd->drv ? backing_hd->drv->format_name : "");

    bdrv_op_block_all(backing_hd, parent->backing_blocker);
    /* Otherwise we won't be able to commit or stream */
    bdrv_op_unblock(backing_hd, BLOCK_OP_TYPE_COMMIT_TARGET,
                    parent->backing_blocker);
    bdrv_op_unblock(backing_hd, BLOCK_OP_TYPE_STREAM,
                    parent->backing_blocker);
    /*
     * We do backup in 3 ways:
     * 1. drive backup
     *    The target bs is new opened, and the source is top BDS
     * 2. blockdev backup
     *    Both the source and the target are top BDSes.
     * 3. internal backup(used for block replication)
     *    Both the source and the target are backing file
     *
     * In case 1 and 2, neither the source nor the target is the backing file.
     * In case 3, we will block the top BDS, so there is only one block job
     * for the top BDS and its backing chain.
     */
    bdrv_op_unblock(backing_hd, BLOCK_OP_TYPE_BACKUP_SOURCE,
                    parent->backing_blocker);
    bdrv_op_unblock(backing_hd, BLOCK_OP_TYPE_BACKUP_TARGET,
                    parent->backing_blocker);

    bdrv_child_cb_attach(c);
}

static void bdrv_backing_detach(BdrvChild *c)
{
    BlockDriverState *parent = c->opaque;

    assert(parent->backing_blocker);
    bdrv_op_unblock_all(c->bs, parent->backing_blocker);
    error_free(parent->backing_blocker);
    parent->backing_blocker = NULL;

    bdrv_child_cb_detach(c);
}

/*
 * Returns the options and flags that bs->backing should get, based on the
 * given options and flags for the parent BDS
 */
static void bdrv_backing_options(int *child_flags, QDict *child_options,
                                 int parent_flags, QDict *parent_options)
{
    int flags = parent_flags;

    /* The cache mode is inherited unmodified for backing files; except WCE,
     * which is only applied on the top level (BlockBackend) */
    qdict_copy_default(child_options, parent_options, BDRV_OPT_CACHE_DIRECT);
    qdict_copy_default(child_options, parent_options, BDRV_OPT_CACHE_NO_FLUSH);
    qdict_copy_default(child_options, parent_options, BDRV_OPT_FORCE_SHARE);

    /* backing files always opened read-only */
    qdict_set_default_str(child_options, BDRV_OPT_READ_ONLY, "on");
    qdict_set_default_str(child_options, BDRV_OPT_AUTO_READ_ONLY, "off");
    flags &= ~BDRV_O_COPY_ON_READ;

    /* snapshot=on is handled on the top layer */
    flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_TEMPORARY);

    *child_flags = flags;
}

static int bdrv_backing_update_filename(BdrvChild *c, BlockDriverState *base,
                                        const char *filename, Error **errp)
{
    BlockDriverState *parent = c->opaque;
    bool read_only = bdrv_is_read_only(parent);
    int ret;

    if (read_only) {
        ret = bdrv_reopen_set_read_only(parent, false, errp);
        if (ret < 0) {
            return ret;
        }
    }

    ret = bdrv_change_backing_file(parent, filename,
                                   base->drv ? base->drv->format_name : "");
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not update backing file link");
    }

    if (read_only) {
        bdrv_reopen_set_read_only(parent, true, NULL);
    }

    return ret;
}

const BdrvChildRole child_backing = {
    .parent_is_bds   = true,
    .get_parent_desc = bdrv_child_get_parent_desc,
    .attach          = bdrv_backing_attach,
    .detach          = bdrv_backing_detach,
    .inherit_options = bdrv_backing_options,
    .drained_begin   = bdrv_child_cb_drained_begin,
    .drained_poll    = bdrv_child_cb_drained_poll,
    .drained_end     = bdrv_child_cb_drained_end,
    .inactivate      = bdrv_child_cb_inactivate,
    .update_filename = bdrv_backing_update_filename,
    .can_set_aio_ctx = bdrv_child_cb_can_set_aio_ctx,
    .set_aio_ctx     = bdrv_child_cb_set_aio_ctx,
};

static int bdrv_open_flags(BlockDriverState *bs, int flags)
{
    int open_flags = flags;

    /*
     * Clear flags that are internal to the block layer before opening the
     * image.
     */
    open_flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING | BDRV_O_PROTOCOL);

    return open_flags;
}

static void update_flags_from_options(int *flags, QemuOpts *opts)
{
    *flags &= ~(BDRV_O_CACHE_MASK | BDRV_O_RDWR | BDRV_O_AUTO_RDONLY);

    if (qemu_opt_get_bool_del(opts, BDRV_OPT_CACHE_NO_FLUSH, false)) {
        *flags |= BDRV_O_NO_FLUSH;
    }

    if (qemu_opt_get_bool_del(opts, BDRV_OPT_CACHE_DIRECT, false)) {
        *flags |= BDRV_O_NOCACHE;
    }

    if (!qemu_opt_get_bool_del(opts, BDRV_OPT_READ_ONLY, false)) {
        *flags |= BDRV_O_RDWR;
    }

    if (qemu_opt_get_bool_del(opts, BDRV_OPT_AUTO_READ_ONLY, false)) {
        *flags |= BDRV_O_AUTO_RDONLY;
    }
}

static void update_options_from_flags(QDict *options, int flags)
{
    if (!qdict_haskey(options, BDRV_OPT_CACHE_DIRECT)) {
        qdict_put_bool(options, BDRV_OPT_CACHE_DIRECT, flags & BDRV_O_NOCACHE);
    }
    if (!qdict_haskey(options, BDRV_OPT_CACHE_NO_FLUSH)) {
        qdict_put_bool(options, BDRV_OPT_CACHE_NO_FLUSH,
                       flags & BDRV_O_NO_FLUSH);
    }
    if (!qdict_haskey(options, BDRV_OPT_READ_ONLY)) {
        qdict_put_bool(options, BDRV_OPT_READ_ONLY, !(flags & BDRV_O_RDWR));
    }
    if (!qdict_haskey(options, BDRV_OPT_AUTO_READ_ONLY)) {
        qdict_put_bool(options, BDRV_OPT_AUTO_READ_ONLY,
                       flags & BDRV_O_AUTO_RDONLY);
    }
}

static void bdrv_assign_node_name(BlockDriverState *bs,
                                  const char *node_name,
                                  Error **errp)
{
    char *gen_node_name = NULL;

    if (!node_name) {
        node_name = gen_node_name = id_generate(ID_BLOCK);
    } else if (!id_wellformed(node_name)) {
        /*
         * Check for empty string or invalid characters, but not if it is
         * generated (generated names use characters not available to the user)
         */
        error_setg(errp, "Invalid node name");
        return;
    }

    /* takes care of avoiding namespaces collisions */
    if (blk_by_name(node_name)) {
        error_setg(errp, "node-name=%s is conflicting with a device id",
                   node_name);
        goto out;
    }

    /* takes care of avoiding duplicates node names */
    if (bdrv_find_node(node_name)) {
        error_setg(errp, "Duplicate node name");
        goto out;
    }

    /* Make sure that the node name isn't truncated */
    if (strlen(node_name) >= sizeof(bs->node_name)) {
        error_setg(errp, "Node name too long");
        goto out;
    }

    /* copy node name into the bs and insert it into the graph list */
    pstrcpy(bs->node_name, sizeof(bs->node_name), node_name);
    QTAILQ_INSERT_TAIL(&graph_bdrv_states, bs, node_list);
out:
    g_free(gen_node_name);
}

static int bdrv_open_driver(BlockDriverState *bs, BlockDriver *drv,
                            const char *node_name, QDict *options,
                            int open_flags, Error **errp)
{
    Error *local_err = NULL;
    int i, ret;

    bdrv_assign_node_name(bs, node_name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -EINVAL;
    }

    bs->drv = drv;
    bs->read_only = !(bs->open_flags & BDRV_O_RDWR);
    bs->opaque = g_malloc0(drv->instance_size);

    if (drv->bdrv_file_open) {
        assert(!drv->bdrv_needs_filename || bs->filename[0]);
        ret = drv->bdrv_file_open(bs, options, open_flags, &local_err);
    } else if (drv->bdrv_open) {
        ret = drv->bdrv_open(bs, options, open_flags, &local_err);
    } else {
        ret = 0;
    }

    if (ret < 0) {
        if (local_err) {
            error_propagate(errp, local_err);
        } else if (bs->filename[0]) {
            error_setg_errno(errp, -ret, "Could not open '%s'", bs->filename);
        } else {
            error_setg_errno(errp, -ret, "Could not open image");
        }
        goto open_failed;
    }

    ret = refresh_total_sectors(bs, bs->total_sectors);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not refresh total sector count");
        return ret;
    }

    bdrv_refresh_limits(bs, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -EINVAL;
    }

    assert(bdrv_opt_mem_align(bs) != 0);
    assert(bdrv_min_mem_align(bs) != 0);
    assert(is_power_of_2(bs->bl.request_alignment));

    for (i = 0; i < bs->quiesce_counter; i++) {
        if (drv->bdrv_co_drain_begin) {
            drv->bdrv_co_drain_begin(bs);
        }
    }

    return 0;
open_failed:
    bs->drv = NULL;
    if (bs->file != NULL) {
        bdrv_unref_child(bs, bs->file);
        bs->file = NULL;
    }
    g_free(bs->opaque);
    bs->opaque = NULL;
    return ret;
}

BlockDriverState *bdrv_new_open_driver(BlockDriver *drv, const char *node_name,
                                       int flags, Error **errp)
{
    BlockDriverState *bs;
    int ret;

    bs = bdrv_new();
    bs->open_flags = flags;
    bs->explicit_options = qdict_new();
    bs->options = qdict_new();
    bs->opaque = NULL;

    update_options_from_flags(bs->options, flags);

    ret = bdrv_open_driver(bs, drv, node_name, bs->options, flags, errp);
    if (ret < 0) {
        qobject_unref(bs->explicit_options);
        bs->explicit_options = NULL;
        qobject_unref(bs->options);
        bs->options = NULL;
        bdrv_unref(bs);
        return NULL;
    }

    return bs;
}

QemuOptsList bdrv_runtime_opts = {
    .name = "bdrv_common",
    .head = QTAILQ_HEAD_INITIALIZER(bdrv_runtime_opts.head),
    .desc = {
        {
            .name = "node-name",
            .type = QEMU_OPT_STRING,
            .help = "Node name of the block device node",
        },
        {
            .name = "driver",
            .type = QEMU_OPT_STRING,
            .help = "Block driver to use for the node",
        },
        {
            .name = BDRV_OPT_CACHE_DIRECT,
            .type = QEMU_OPT_BOOL,
            .help = "Bypass software writeback cache on the host",
        },
        {
            .name = BDRV_OPT_CACHE_NO_FLUSH,
            .type = QEMU_OPT_BOOL,
            .help = "Ignore flush requests",
        },
        {
            .name = BDRV_OPT_READ_ONLY,
            .type = QEMU_OPT_BOOL,
            .help = "Node is opened in read-only mode",
        },
        {
            .name = BDRV_OPT_AUTO_READ_ONLY,
            .type = QEMU_OPT_BOOL,
            .help = "Node can become read-only if opening read-write fails",
        },
        {
            .name = "detect-zeroes",
            .type = QEMU_OPT_STRING,
            .help = "try to optimize zero writes (off, on, unmap)",
        },
        {
            .name = BDRV_OPT_DISCARD,
            .type = QEMU_OPT_STRING,
            .help = "discard operation (ignore/off, unmap/on)",
        },
        {
            .name = BDRV_OPT_FORCE_SHARE,
            .type = QEMU_OPT_BOOL,
            .help = "always accept other writers (default: off)",
        },
        { /* end of list */ }
    },
};

/*
 * Common part for opening disk images and files
 *
 * Removes all processed options from *options.
 */
static int bdrv_open_common(BlockDriverState *bs, BlockBackend *file,
                            QDict *options, Error **errp)
{
    int ret, open_flags;
    const char *filename;
    const char *driver_name = NULL;
    const char *node_name = NULL;
    const char *discard;
    QemuOpts *opts;
    BlockDriver *drv;
    Error *local_err = NULL;

    assert(bs->file == NULL);
    assert(options != NULL && bs->options != options);

    opts = qemu_opts_create(&bdrv_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail_opts;
    }

    update_flags_from_options(&bs->open_flags, opts);

    driver_name = qemu_opt_get(opts, "driver");
    drv = bdrv_find_format(driver_name);
    assert(drv != NULL);

    bs->force_share = qemu_opt_get_bool(opts, BDRV_OPT_FORCE_SHARE, false);

    if (bs->force_share && (bs->open_flags & BDRV_O_RDWR)) {
        error_setg(errp,
                   BDRV_OPT_FORCE_SHARE
                   "=on can only be used with read-only images");
        ret = -EINVAL;
        goto fail_opts;
    }

    if (file != NULL) {
        bdrv_refresh_filename(blk_bs(file));
        filename = blk_bs(file)->filename;
    } else {
        /*
         * Caution: while qdict_get_try_str() is fine, getting
         * non-string types would require more care.  When @options
         * come from -blockdev or blockdev_add, its members are typed
         * according to the QAPI schema, but when they come from
         * -drive, they're all QString.
         */
        filename = qdict_get_try_str(options, "filename");
    }

    if (drv->bdrv_needs_filename && (!filename || !filename[0])) {
        error_setg(errp, "The '%s' block driver requires a file name",
                   drv->format_name);
        ret = -EINVAL;
        goto fail_opts;
    }

    trace_bdrv_open_common(bs, filename ?: "", bs->open_flags,
                           drv->format_name);

    bs->read_only = !(bs->open_flags & BDRV_O_RDWR);

    if (use_bdrv_whitelist && !bdrv_is_whitelisted(drv, bs->read_only)) {
        if (!bs->read_only && bdrv_is_whitelisted(drv, true)) {
            ret = bdrv_apply_auto_read_only(bs, NULL, NULL);
        } else {
            ret = -ENOTSUP;
        }
        if (ret < 0) {
            error_setg(errp,
                       !bs->read_only && bdrv_is_whitelisted(drv, true)
                       ? "Driver '%s' can only be used for read-only devices"
                       : "Driver '%s' is not whitelisted",
                       drv->format_name);
            goto fail_opts;
        }
    }

    /* bdrv_new() and bdrv_close() make it so */
    assert(atomic_read(&bs->copy_on_read) == 0);

    if (bs->open_flags & BDRV_O_COPY_ON_READ) {
        if (!bs->read_only) {
            bdrv_enable_copy_on_read(bs);
        } else {
            error_setg(errp, "Can't use copy-on-read on read-only device");
            ret = -EINVAL;
            goto fail_opts;
        }
    }

    discard = qemu_opt_get(opts, BDRV_OPT_DISCARD);
    if (discard != NULL) {
        if (bdrv_parse_discard_flags(discard, &bs->open_flags) != 0) {
            error_setg(errp, "Invalid discard option");
            ret = -EINVAL;
            goto fail_opts;
        }
    }

    bs->detect_zeroes =
        bdrv_parse_detect_zeroes(opts, bs->open_flags, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail_opts;
    }

    if (filename != NULL) {
        pstrcpy(bs->filename, sizeof(bs->filename), filename);
    } else {
        bs->filename[0] = '\0';
    }
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename), bs->filename);

    /* Open the image, either directly or using a protocol */
    open_flags = bdrv_open_flags(bs, bs->open_flags);
    node_name = qemu_opt_get(opts, "node-name");

    assert(!drv->bdrv_file_open || file == NULL);
    ret = bdrv_open_driver(bs, drv, node_name, options, open_flags, errp);
    if (ret < 0) {
        goto fail_opts;
    }

    qemu_opts_del(opts);
    return 0;

fail_opts:
    qemu_opts_del(opts);
    return ret;
}

static QDict *parse_json_filename(const char *filename, Error **errp)
{
    QObject *options_obj;
    QDict *options;
    int ret;

    ret = strstart(filename, "json:", &filename);
    assert(ret);

    options_obj = qobject_from_json(filename, errp);
    if (!options_obj) {
        error_prepend(errp, "Could not parse the JSON options: ");
        return NULL;
    }

    options = qobject_to(QDict, options_obj);
    if (!options) {
        qobject_unref(options_obj);
        error_setg(errp, "Invalid JSON object given");
        return NULL;
    }

    qdict_flatten(options);

    return options;
}

static void parse_json_protocol(QDict *options, const char **pfilename,
                                Error **errp)
{
    QDict *json_options;
    Error *local_err = NULL;

    /* Parse json: pseudo-protocol */
    if (!*pfilename || !g_str_has_prefix(*pfilename, "json:")) {
        return;
    }

    json_options = parse_json_filename(*pfilename, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Options given in the filename have lower priority than options
     * specified directly */
    qdict_join(options, json_options, false);
    qobject_unref(json_options);
    *pfilename = NULL;
}

/*
 * Fills in default options for opening images and converts the legacy
 * filename/flags pair to option QDict entries.
 * The BDRV_O_PROTOCOL flag in *flags will be set or cleared accordingly if a
 * block driver has been specified explicitly.
 */
static int bdrv_fill_options(QDict **options, const char *filename,
                             int *flags, Error **errp)
{
    const char *drvname;
    bool protocol = *flags & BDRV_O_PROTOCOL;
    bool parse_filename = false;
    BlockDriver *drv = NULL;
    Error *local_err = NULL;

    /*
     * Caution: while qdict_get_try_str() is fine, getting non-string
     * types would require more care.  When @options come from
     * -blockdev or blockdev_add, its members are typed according to
     * the QAPI schema, but when they come from -drive, they're all
     * QString.
     */
    drvname = qdict_get_try_str(*options, "driver");
    if (drvname) {
        drv = bdrv_find_format(drvname);
        if (!drv) {
            error_setg(errp, "Unknown driver '%s'", drvname);
            return -ENOENT;
        }
        /* If the user has explicitly specified the driver, this choice should
         * override the BDRV_O_PROTOCOL flag */
        protocol = drv->bdrv_file_open;
    }

    if (protocol) {
        *flags |= BDRV_O_PROTOCOL;
    } else {
        *flags &= ~BDRV_O_PROTOCOL;
    }

    /* Translate cache options from flags into options */
    update_options_from_flags(*options, *flags);

    /* Fetch the file name from the options QDict if necessary */
    if (protocol && filename) {
        if (!qdict_haskey(*options, "filename")) {
            qdict_put_str(*options, "filename", filename);
            parse_filename = true;
        } else {
            error_setg(errp, "Can't specify 'file' and 'filename' options at "
                             "the same time");
            return -EINVAL;
        }
    }

    /* Find the right block driver */
    /* See cautionary note on accessing @options above */
    filename = qdict_get_try_str(*options, "filename");

    if (!drvname && protocol) {
        if (filename) {
            drv = bdrv_find_protocol(filename, parse_filename, errp);
            if (!drv) {
                return -EINVAL;
            }

            drvname = drv->format_name;
            qdict_put_str(*options, "driver", drvname);
        } else {
            error_setg(errp, "Must specify either driver or file");
            return -EINVAL;
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

static int bdrv_child_check_perm(BdrvChild *c, BlockReopenQueue *q,
                                 uint64_t perm, uint64_t shared,
                                 GSList *ignore_children,
                                 bool *tighten_restrictions, Error **errp);
static void bdrv_child_abort_perm_update(BdrvChild *c);
static void bdrv_child_set_perm(BdrvChild *c, uint64_t perm, uint64_t shared);
static void bdrv_get_cumulative_perm(BlockDriverState *bs, uint64_t *perm,
                                     uint64_t *shared_perm);

typedef struct BlockReopenQueueEntry {
     bool prepared;
     bool perms_checked;
     BDRVReopenState state;
     QTAILQ_ENTRY(BlockReopenQueueEntry) entry;
} BlockReopenQueueEntry;

/*
 * Return the flags that @bs will have after the reopens in @q have
 * successfully completed. If @q is NULL (or @bs is not contained in @q),
 * return the current flags.
 */
static int bdrv_reopen_get_flags(BlockReopenQueue *q, BlockDriverState *bs)
{
    BlockReopenQueueEntry *entry;

    if (q != NULL) {
        QTAILQ_FOREACH(entry, q, entry) {
            if (entry->state.bs == bs) {
                return entry->state.flags;
            }
        }
    }

    return bs->open_flags;
}

/* Returns whether the image file can be written to after the reopen queue @q
 * has been successfully applied, or right now if @q is NULL. */
static bool bdrv_is_writable_after_reopen(BlockDriverState *bs,
                                          BlockReopenQueue *q)
{
    int flags = bdrv_reopen_get_flags(q, bs);

    return (flags & (BDRV_O_RDWR | BDRV_O_INACTIVE)) == BDRV_O_RDWR;
}

/*
 * Return whether the BDS can be written to.  This is not necessarily
 * the same as !bdrv_is_read_only(bs), as inactivated images may not
 * be written to but do not count as read-only images.
 */
bool bdrv_is_writable(BlockDriverState *bs)
{
    return bdrv_is_writable_after_reopen(bs, NULL);
}

static void bdrv_child_perm(BlockDriverState *bs, BlockDriverState *child_bs,
                            BdrvChild *c, const BdrvChildRole *role,
                            BlockReopenQueue *reopen_queue,
                            uint64_t parent_perm, uint64_t parent_shared,
                            uint64_t *nperm, uint64_t *nshared)
{
    assert(bs->drv && bs->drv->bdrv_child_perm);
    bs->drv->bdrv_child_perm(bs, c, role, reopen_queue,
                             parent_perm, parent_shared,
                             nperm, nshared);
    /* TODO Take force_share from reopen_queue */
    if (child_bs && child_bs->force_share) {
        *nshared = BLK_PERM_ALL;
    }
}

/*
 * Check whether permissions on this node can be changed in a way that
 * @cumulative_perms and @cumulative_shared_perms are the new cumulative
 * permissions of all its parents. This involves checking whether all necessary
 * permission changes to child nodes can be performed.
 *
 * Will set *tighten_restrictions to true if and only if new permissions have to
 * be taken or currently shared permissions are to be unshared.  Otherwise,
 * errors are not fatal as long as the caller accepts that the restrictions
 * remain tighter than they need to be.  The caller still has to abort the
 * transaction.
 * @tighten_restrictions cannot be used together with @q: When reopening, we may
 * encounter fatal errors even though no restrictions are to be tightened.  For
 * example, changing a node from RW to RO will fail if the WRITE permission is
 * to be kept.
 *
 * A call to this function must always be followed by a call to bdrv_set_perm()
 * or bdrv_abort_perm_update().
 */
static int bdrv_check_perm(BlockDriverState *bs, BlockReopenQueue *q,
                           uint64_t cumulative_perms,
                           uint64_t cumulative_shared_perms,
                           GSList *ignore_children,
                           bool *tighten_restrictions, Error **errp)
{
    BlockDriver *drv = bs->drv;
    BdrvChild *c;
    int ret;

    assert(!q || !tighten_restrictions);

    if (tighten_restrictions) {
        uint64_t current_perms, current_shared;
        uint64_t added_perms, removed_shared_perms;

        bdrv_get_cumulative_perm(bs, &current_perms, &current_shared);

        added_perms = cumulative_perms & ~current_perms;
        removed_shared_perms = current_shared & ~cumulative_shared_perms;

        *tighten_restrictions = added_perms || removed_shared_perms;
    }

    /* Write permissions never work with read-only images */
    if ((cumulative_perms & (BLK_PERM_WRITE | BLK_PERM_WRITE_UNCHANGED)) &&
        !bdrv_is_writable_after_reopen(bs, q))
    {
        if (!bdrv_is_writable_after_reopen(bs, NULL)) {
            error_setg(errp, "Block node is read-only");
        } else {
            uint64_t current_perms, current_shared;
            bdrv_get_cumulative_perm(bs, &current_perms, &current_shared);
            if (current_perms & (BLK_PERM_WRITE | BLK_PERM_WRITE_UNCHANGED)) {
                error_setg(errp, "Cannot make block node read-only, there is "
                           "a writer on it");
            } else {
                error_setg(errp, "Cannot make block node read-only and create "
                           "a writer on it");
            }
        }

        return -EPERM;
    }

    /* Check this node */
    if (!drv) {
        return 0;
    }

    if (drv->bdrv_check_perm) {
        return drv->bdrv_check_perm(bs, cumulative_perms,
                                    cumulative_shared_perms, errp);
    }

    /* Drivers that never have children can omit .bdrv_child_perm() */
    if (!drv->bdrv_child_perm) {
        assert(QLIST_EMPTY(&bs->children));
        return 0;
    }

    /* Check all children */
    QLIST_FOREACH(c, &bs->children, next) {
        uint64_t cur_perm, cur_shared;
        bool child_tighten_restr;

        bdrv_child_perm(bs, c->bs, c, c->role, q,
                        cumulative_perms, cumulative_shared_perms,
                        &cur_perm, &cur_shared);
        ret = bdrv_child_check_perm(c, q, cur_perm, cur_shared, ignore_children,
                                    tighten_restrictions ? &child_tighten_restr
                                                         : NULL,
                                    errp);
        if (tighten_restrictions) {
            *tighten_restrictions |= child_tighten_restr;
        }
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

/*
 * Notifies drivers that after a previous bdrv_check_perm() call, the
 * permission update is not performed and any preparations made for it (e.g.
 * taken file locks) need to be undone.
 *
 * This function recursively notifies all child nodes.
 */
static void bdrv_abort_perm_update(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    BdrvChild *c;

    if (!drv) {
        return;
    }

    if (drv->bdrv_abort_perm_update) {
        drv->bdrv_abort_perm_update(bs);
    }

    QLIST_FOREACH(c, &bs->children, next) {
        bdrv_child_abort_perm_update(c);
    }
}

static void bdrv_set_perm(BlockDriverState *bs, uint64_t cumulative_perms,
                          uint64_t cumulative_shared_perms)
{
    BlockDriver *drv = bs->drv;
    BdrvChild *c;

    if (!drv) {
        return;
    }

    /* Update this node */
    if (drv->bdrv_set_perm) {
        drv->bdrv_set_perm(bs, cumulative_perms, cumulative_shared_perms);
    }

    /* Drivers that never have children can omit .bdrv_child_perm() */
    if (!drv->bdrv_child_perm) {
        assert(QLIST_EMPTY(&bs->children));
        return;
    }

    /* Update all children */
    QLIST_FOREACH(c, &bs->children, next) {
        uint64_t cur_perm, cur_shared;
        bdrv_child_perm(bs, c->bs, c, c->role, NULL,
                        cumulative_perms, cumulative_shared_perms,
                        &cur_perm, &cur_shared);
        bdrv_child_set_perm(c, cur_perm, cur_shared);
    }
}

static void bdrv_get_cumulative_perm(BlockDriverState *bs, uint64_t *perm,
                                     uint64_t *shared_perm)
{
    BdrvChild *c;
    uint64_t cumulative_perms = 0;
    uint64_t cumulative_shared_perms = BLK_PERM_ALL;

    QLIST_FOREACH(c, &bs->parents, next_parent) {
        cumulative_perms |= c->perm;
        cumulative_shared_perms &= c->shared_perm;
    }

    *perm = cumulative_perms;
    *shared_perm = cumulative_shared_perms;
}

static char *bdrv_child_user_desc(BdrvChild *c)
{
    if (c->role->get_parent_desc) {
        return c->role->get_parent_desc(c);
    }

    return g_strdup("another user");
}

char *bdrv_perm_names(uint64_t perm)
{
    struct perm_name {
        uint64_t perm;
        const char *name;
    } permissions[] = {
        { BLK_PERM_CONSISTENT_READ, "consistent read" },
        { BLK_PERM_WRITE,           "write" },
        { BLK_PERM_WRITE_UNCHANGED, "write unchanged" },
        { BLK_PERM_RESIZE,          "resize" },
        { BLK_PERM_GRAPH_MOD,       "change children" },
        { 0, NULL }
    };

    char *result = g_strdup("");
    struct perm_name *p;

    for (p = permissions; p->name; p++) {
        if (perm & p->perm) {
            char *old = result;
            result = g_strdup_printf("%s%s%s", old, *old ? ", " : "", p->name);
            g_free(old);
        }
    }

    return result;
}

/*
 * Checks whether a new reference to @bs can be added if the new user requires
 * @new_used_perm/@new_shared_perm as its permissions. If @ignore_children is
 * set, the BdrvChild objects in this list are ignored in the calculations;
 * this allows checking permission updates for an existing reference.
 *
 * See bdrv_check_perm() for the semantics of @tighten_restrictions.
 *
 * Needs to be followed by a call to either bdrv_set_perm() or
 * bdrv_abort_perm_update(). */
static int bdrv_check_update_perm(BlockDriverState *bs, BlockReopenQueue *q,
                                  uint64_t new_used_perm,
                                  uint64_t new_shared_perm,
                                  GSList *ignore_children,
                                  bool *tighten_restrictions,
                                  Error **errp)
{
    BdrvChild *c;
    uint64_t cumulative_perms = new_used_perm;
    uint64_t cumulative_shared_perms = new_shared_perm;

    assert(!q || !tighten_restrictions);

    /* There is no reason why anyone couldn't tolerate write_unchanged */
    assert(new_shared_perm & BLK_PERM_WRITE_UNCHANGED);

    QLIST_FOREACH(c, &bs->parents, next_parent) {
        if (g_slist_find(ignore_children, c)) {
            continue;
        }

        if ((new_used_perm & c->shared_perm) != new_used_perm) {
            char *user = bdrv_child_user_desc(c);
            char *perm_names = bdrv_perm_names(new_used_perm & ~c->shared_perm);

            if (tighten_restrictions) {
                *tighten_restrictions = true;
            }

            error_setg(errp, "Conflicts with use by %s as '%s', which does not "
                             "allow '%s' on %s",
                       user, c->name, perm_names, bdrv_get_node_name(c->bs));
            g_free(user);
            g_free(perm_names);
            return -EPERM;
        }

        if ((c->perm & new_shared_perm) != c->perm) {
            char *user = bdrv_child_user_desc(c);
            char *perm_names = bdrv_perm_names(c->perm & ~new_shared_perm);

            if (tighten_restrictions) {
                *tighten_restrictions = true;
            }

            error_setg(errp, "Conflicts with use by %s as '%s', which uses "
                             "'%s' on %s",
                       user, c->name, perm_names, bdrv_get_node_name(c->bs));
            g_free(user);
            g_free(perm_names);
            return -EPERM;
        }

        cumulative_perms |= c->perm;
        cumulative_shared_perms &= c->shared_perm;
    }

    return bdrv_check_perm(bs, q, cumulative_perms, cumulative_shared_perms,
                           ignore_children, tighten_restrictions, errp);
}

/* Needs to be followed by a call to either bdrv_child_set_perm() or
 * bdrv_child_abort_perm_update(). */
static int bdrv_child_check_perm(BdrvChild *c, BlockReopenQueue *q,
                                 uint64_t perm, uint64_t shared,
                                 GSList *ignore_children,
                                 bool *tighten_restrictions, Error **errp)
{
    int ret;

    ignore_children = g_slist_prepend(g_slist_copy(ignore_children), c);
    ret = bdrv_check_update_perm(c->bs, q, perm, shared, ignore_children,
                                 tighten_restrictions, errp);
    g_slist_free(ignore_children);

    if (ret < 0) {
        return ret;
    }

    if (!c->has_backup_perm) {
        c->has_backup_perm = true;
        c->backup_perm = c->perm;
        c->backup_shared_perm = c->shared_perm;
    }
    /*
     * Note: it's OK if c->has_backup_perm was already set, as we can find the
     * same child twice during check_perm procedure
     */

    c->perm = perm;
    c->shared_perm = shared;

    return 0;
}

static void bdrv_child_set_perm(BdrvChild *c, uint64_t perm, uint64_t shared)
{
    uint64_t cumulative_perms, cumulative_shared_perms;

    c->has_backup_perm = false;

    c->perm = perm;
    c->shared_perm = shared;

    bdrv_get_cumulative_perm(c->bs, &cumulative_perms,
                             &cumulative_shared_perms);
    bdrv_set_perm(c->bs, cumulative_perms, cumulative_shared_perms);
}

static void bdrv_child_abort_perm_update(BdrvChild *c)
{
    if (c->has_backup_perm) {
        c->perm = c->backup_perm;
        c->shared_perm = c->backup_shared_perm;
        c->has_backup_perm = false;
    }

    bdrv_abort_perm_update(c->bs);
}

int bdrv_child_try_set_perm(BdrvChild *c, uint64_t perm, uint64_t shared,
                            Error **errp)
{
    Error *local_err = NULL;
    int ret;
    bool tighten_restrictions;

    ret = bdrv_child_check_perm(c, NULL, perm, shared, NULL,
                                &tighten_restrictions, &local_err);
    if (ret < 0) {
        bdrv_child_abort_perm_update(c);
        if (tighten_restrictions) {
            error_propagate(errp, local_err);
        } else {
            /*
             * Our caller may intend to only loosen restrictions and
             * does not expect this function to fail.  Errors are not
             * fatal in such a case, so we can just hide them from our
             * caller.
             */
            error_free(local_err);
            ret = 0;
        }
        return ret;
    }

    bdrv_child_set_perm(c, perm, shared);

    return 0;
}

int bdrv_child_refresh_perms(BlockDriverState *bs, BdrvChild *c, Error **errp)
{
    uint64_t parent_perms, parent_shared;
    uint64_t perms, shared;

    bdrv_get_cumulative_perm(bs, &parent_perms, &parent_shared);
    bdrv_child_perm(bs, c->bs, c, c->role, NULL, parent_perms, parent_shared,
                    &perms, &shared);

    return bdrv_child_try_set_perm(c, perms, shared, errp);
}

void bdrv_filter_default_perms(BlockDriverState *bs, BdrvChild *c,
                               const BdrvChildRole *role,
                               BlockReopenQueue *reopen_queue,
                               uint64_t perm, uint64_t shared,
                               uint64_t *nperm, uint64_t *nshared)
{
    *nperm = perm & DEFAULT_PERM_PASSTHROUGH;
    *nshared = (shared & DEFAULT_PERM_PASSTHROUGH) | DEFAULT_PERM_UNCHANGED;
}

void bdrv_format_default_perms(BlockDriverState *bs, BdrvChild *c,
                               const BdrvChildRole *role,
                               BlockReopenQueue *reopen_queue,
                               uint64_t perm, uint64_t shared,
                               uint64_t *nperm, uint64_t *nshared)
{
    bool backing = (role == &child_backing);
    assert(role == &child_backing || role == &child_file);

    if (!backing) {
        int flags = bdrv_reopen_get_flags(reopen_queue, bs);

        /* Apart from the modifications below, the same permissions are
         * forwarded and left alone as for filters */
        bdrv_filter_default_perms(bs, c, role, reopen_queue, perm, shared,
                                  &perm, &shared);

        /* Format drivers may touch metadata even if the guest doesn't write */
        if (bdrv_is_writable_after_reopen(bs, reopen_queue)) {
            perm |= BLK_PERM_WRITE | BLK_PERM_RESIZE;
        }

        /* bs->file always needs to be consistent because of the metadata. We
         * can never allow other users to resize or write to it. */
        if (!(flags & BDRV_O_NO_IO)) {
            perm |= BLK_PERM_CONSISTENT_READ;
        }
        shared &= ~(BLK_PERM_WRITE | BLK_PERM_RESIZE);
    } else {
        /* We want consistent read from backing files if the parent needs it.
         * No other operations are performed on backing files. */
        perm &= BLK_PERM_CONSISTENT_READ;

        /* If the parent can deal with changing data, we're okay with a
         * writable and resizable backing file. */
        /* TODO Require !(perm & BLK_PERM_CONSISTENT_READ), too? */
        if (shared & BLK_PERM_WRITE) {
            shared = BLK_PERM_WRITE | BLK_PERM_RESIZE;
        } else {
            shared = 0;
        }

        shared |= BLK_PERM_CONSISTENT_READ | BLK_PERM_GRAPH_MOD |
                  BLK_PERM_WRITE_UNCHANGED;
    }

    if (bs->open_flags & BDRV_O_INACTIVE) {
        shared |= BLK_PERM_WRITE | BLK_PERM_RESIZE;
    }

    *nperm = perm;
    *nshared = shared;
}

uint64_t bdrv_qapi_perm_to_blk_perm(BlockPermission qapi_perm)
{
    static const uint64_t permissions[] = {
        [BLOCK_PERMISSION_CONSISTENT_READ]  = BLK_PERM_CONSISTENT_READ,
        [BLOCK_PERMISSION_WRITE]            = BLK_PERM_WRITE,
        [BLOCK_PERMISSION_WRITE_UNCHANGED]  = BLK_PERM_WRITE_UNCHANGED,
        [BLOCK_PERMISSION_RESIZE]           = BLK_PERM_RESIZE,
        [BLOCK_PERMISSION_GRAPH_MOD]        = BLK_PERM_GRAPH_MOD,
    };

    QEMU_BUILD_BUG_ON(ARRAY_SIZE(permissions) != BLOCK_PERMISSION__MAX);
    QEMU_BUILD_BUG_ON(1UL << ARRAY_SIZE(permissions) != BLK_PERM_ALL + 1);

    assert(qapi_perm < BLOCK_PERMISSION__MAX);

    return permissions[qapi_perm];
}

static void bdrv_replace_child_noperm(BdrvChild *child,
                                      BlockDriverState *new_bs)
{
    BlockDriverState *old_bs = child->bs;
    int new_bs_quiesce_counter;
    int drain_saldo;

    assert(!child->frozen);

    if (old_bs && new_bs) {
        assert(bdrv_get_aio_context(old_bs) == bdrv_get_aio_context(new_bs));
    }

    new_bs_quiesce_counter = (new_bs ? new_bs->quiesce_counter : 0);
    drain_saldo = new_bs_quiesce_counter - child->parent_quiesce_counter;

    /*
     * If the new child node is drained but the old one was not, flush
     * all outstanding requests to the old child node.
     */
    while (drain_saldo > 0 && child->role->drained_begin) {
        bdrv_parent_drained_begin_single(child, true);
        drain_saldo--;
    }

    if (old_bs) {
        /* Detach first so that the recursive drain sections coming from @child
         * are already gone and we only end the drain sections that came from
         * elsewhere. */
        if (child->role->detach) {
            child->role->detach(child);
        }
        QLIST_REMOVE(child, next_parent);
    }

    child->bs = new_bs;

    if (new_bs) {
        QLIST_INSERT_HEAD(&new_bs->parents, child, next_parent);

        /*
         * Detaching the old node may have led to the new node's
         * quiesce_counter having been decreased.  Not a problem, we
         * just need to recognize this here and then invoke
         * drained_end appropriately more often.
         */
        assert(new_bs->quiesce_counter <= new_bs_quiesce_counter);
        drain_saldo += new_bs->quiesce_counter - new_bs_quiesce_counter;

        /* Attach only after starting new drained sections, so that recursive
         * drain sections coming from @child don't get an extra .drained_begin
         * callback. */
        if (child->role->attach) {
            child->role->attach(child);
        }
    }

    /*
     * If the old child node was drained but the new one is not, allow
     * requests to come in only after the new node has been attached.
     */
    while (drain_saldo < 0 && child->role->drained_end) {
        bdrv_parent_drained_end_single(child);
        drain_saldo++;
    }
}

/*
 * Updates @child to change its reference to point to @new_bs, including
 * checking and applying the necessary permisson updates both to the old node
 * and to @new_bs.
 *
 * NULL is passed as @new_bs for removing the reference before freeing @child.
 *
 * If @new_bs is not NULL, bdrv_check_perm() must be called beforehand, as this
 * function uses bdrv_set_perm() to update the permissions according to the new
 * reference that @new_bs gets.
 */
static void bdrv_replace_child(BdrvChild *child, BlockDriverState *new_bs)
{
    BlockDriverState *old_bs = child->bs;
    uint64_t perm, shared_perm;

    bdrv_replace_child_noperm(child, new_bs);

    /*
     * Start with the new node's permissions.  If @new_bs is a (direct
     * or indirect) child of @old_bs, we must complete the permission
     * update on @new_bs before we loosen the restrictions on @old_bs.
     * Otherwise, bdrv_check_perm() on @old_bs would re-initiate
     * updating the permissions of @new_bs, and thus not purely loosen
     * restrictions.
     */
    if (new_bs) {
        bdrv_get_cumulative_perm(new_bs, &perm, &shared_perm);
        bdrv_set_perm(new_bs, perm, shared_perm);
    }

    if (old_bs) {
        /* Update permissions for old node. This is guaranteed to succeed
         * because we're just taking a parent away, so we're loosening
         * restrictions. */
        bool tighten_restrictions;
        int ret;

        bdrv_get_cumulative_perm(old_bs, &perm, &shared_perm);
        ret = bdrv_check_perm(old_bs, NULL, perm, shared_perm, NULL,
                              &tighten_restrictions, NULL);
        assert(tighten_restrictions == false);
        if (ret < 0) {
            /* We only tried to loosen restrictions, so errors are not fatal */
            bdrv_abort_perm_update(old_bs);
        } else {
            bdrv_set_perm(old_bs, perm, shared_perm);
        }

        /* When the parent requiring a non-default AioContext is removed, the
         * node moves back to the main AioContext */
        bdrv_try_set_aio_context(old_bs, qemu_get_aio_context(), NULL);
    }
}

/*
 * This function steals the reference to child_bs from the caller.
 * That reference is later dropped by bdrv_root_unref_child().
 *
 * On failure NULL is returned, errp is set and the reference to
 * child_bs is also dropped.
 *
 * The caller must hold the AioContext lock @child_bs, but not that of @ctx
 * (unless @child_bs is already in @ctx).
 */
BdrvChild *bdrv_root_attach_child(BlockDriverState *child_bs,
                                  const char *child_name,
                                  const BdrvChildRole *child_role,
                                  AioContext *ctx,
                                  uint64_t perm, uint64_t shared_perm,
                                  void *opaque, Error **errp)
{
    BdrvChild *child;
    Error *local_err = NULL;
    int ret;

    ret = bdrv_check_update_perm(child_bs, NULL, perm, shared_perm, NULL, NULL,
                                 errp);
    if (ret < 0) {
        bdrv_abort_perm_update(child_bs);
        bdrv_unref(child_bs);
        return NULL;
    }

    child = g_new(BdrvChild, 1);
    *child = (BdrvChild) {
        .bs             = NULL,
        .name           = g_strdup(child_name),
        .role           = child_role,
        .perm           = perm,
        .shared_perm    = shared_perm,
        .opaque         = opaque,
    };

    /* If the AioContexts don't match, first try to move the subtree of
     * child_bs into the AioContext of the new parent. If this doesn't work,
     * try moving the parent into the AioContext of child_bs instead. */
    if (bdrv_get_aio_context(child_bs) != ctx) {
        ret = bdrv_try_set_aio_context(child_bs, ctx, &local_err);
        if (ret < 0 && child_role->can_set_aio_ctx) {
            GSList *ignore = g_slist_prepend(NULL, child);;
            ctx = bdrv_get_aio_context(child_bs);
            if (child_role->can_set_aio_ctx(child, ctx, &ignore, NULL)) {
                error_free(local_err);
                ret = 0;
                g_slist_free(ignore);
                ignore = g_slist_prepend(NULL, child);;
                child_role->set_aio_ctx(child, ctx, &ignore);
            }
            g_slist_free(ignore);
        }
        if (ret < 0) {
            error_propagate(errp, local_err);
            g_free(child);
            bdrv_abort_perm_update(child_bs);
            return NULL;
        }
    }

    /* This performs the matching bdrv_set_perm() for the above check. */
    bdrv_replace_child(child, child_bs);

    return child;
}

/*
 * This function transfers the reference to child_bs from the caller
 * to parent_bs. That reference is later dropped by parent_bs on
 * bdrv_close() or if someone calls bdrv_unref_child().
 *
 * On failure NULL is returned, errp is set and the reference to
 * child_bs is also dropped.
 *
 * If @parent_bs and @child_bs are in different AioContexts, the caller must
 * hold the AioContext lock for @child_bs, but not for @parent_bs.
 */
BdrvChild *bdrv_attach_child(BlockDriverState *parent_bs,
                             BlockDriverState *child_bs,
                             const char *child_name,
                             const BdrvChildRole *child_role,
                             Error **errp)
{
    BdrvChild *child;
    uint64_t perm, shared_perm;

    bdrv_get_cumulative_perm(parent_bs, &perm, &shared_perm);

    assert(parent_bs->drv);
    bdrv_child_perm(parent_bs, child_bs, NULL, child_role, NULL,
                    perm, shared_perm, &perm, &shared_perm);

    child = bdrv_root_attach_child(child_bs, child_name, child_role,
                                   bdrv_get_aio_context(parent_bs),
                                   perm, shared_perm, parent_bs, errp);
    if (child == NULL) {
        return NULL;
    }

    QLIST_INSERT_HEAD(&parent_bs->children, child, next);
    return child;
}

static void bdrv_detach_child(BdrvChild *child)
{
    if (child->next.le_prev) {
        QLIST_REMOVE(child, next);
        child->next.le_prev = NULL;
    }

    bdrv_replace_child(child, NULL);

    g_free(child->name);
    g_free(child);
}

void bdrv_root_unref_child(BdrvChild *child)
{
    BlockDriverState *child_bs;

    child_bs = child->bs;
    bdrv_detach_child(child);
    bdrv_unref(child_bs);
}

/**
 * Clear all inherits_from pointers from children and grandchildren of
 * @root that point to @root, where necessary.
 */
static void bdrv_unset_inherits_from(BlockDriverState *root, BdrvChild *child)
{
    BdrvChild *c;

    if (child->bs->inherits_from == root) {
        /*
         * Remove inherits_from only when the last reference between root and
         * child->bs goes away.
         */
        QLIST_FOREACH(c, &root->children, next) {
            if (c != child && c->bs == child->bs) {
                break;
            }
        }
        if (c == NULL) {
            child->bs->inherits_from = NULL;
        }
    }

    QLIST_FOREACH(c, &child->bs->children, next) {
        bdrv_unset_inherits_from(root, c);
    }
}

void bdrv_unref_child(BlockDriverState *parent, BdrvChild *child)
{
    if (child == NULL) {
        return;
    }

    bdrv_unset_inherits_from(parent, child);
    bdrv_root_unref_child(child);
}


static void bdrv_parent_cb_change_media(BlockDriverState *bs, bool load)
{
    BdrvChild *c;
    QLIST_FOREACH(c, &bs->parents, next_parent) {
        if (c->role->change_media) {
            c->role->change_media(c, load);
        }
    }
}

/* Return true if you can reach parent going through child->inherits_from
 * recursively. If parent or child are NULL, return false */
static bool bdrv_inherits_from_recursive(BlockDriverState *child,
                                         BlockDriverState *parent)
{
    while (child && child != parent) {
        child = child->inherits_from;
    }

    return child != NULL;
}

/*
 * Sets the backing file link of a BDS. A new reference is created; callers
 * which don't need their own reference any more must call bdrv_unref().
 */
void bdrv_set_backing_hd(BlockDriverState *bs, BlockDriverState *backing_hd,
                         Error **errp)
{
    bool update_inherits_from = bdrv_chain_contains(bs, backing_hd) &&
        bdrv_inherits_from_recursive(backing_hd, bs);

    if (bdrv_is_backing_chain_frozen(bs, backing_bs(bs), errp)) {
        return;
    }

    if (backing_hd) {
        bdrv_ref(backing_hd);
    }

    if (bs->backing) {
        bdrv_unref_child(bs, bs->backing);
    }

    if (!backing_hd) {
        bs->backing = NULL;
        goto out;
    }

    bs->backing = bdrv_attach_child(bs, backing_hd, "backing", &child_backing,
                                    errp);
    /* If backing_hd was already part of bs's backing chain, and
     * inherits_from pointed recursively to bs then let's update it to
     * point directly to bs (else it will become NULL). */
    if (bs->backing && update_inherits_from) {
        backing_hd->inherits_from = bs;
    }

out:
    bdrv_refresh_limits(bs, NULL);
}

/*
 * Opens the backing file for a BlockDriverState if not yet open
 *
 * bdref_key specifies the key for the image's BlockdevRef in the options QDict.
 * That QDict has to be flattened; therefore, if the BlockdevRef is a QDict
 * itself, all options starting with "${bdref_key}." are considered part of the
 * BlockdevRef.
 *
 * TODO Can this be unified with bdrv_open_image()?
 */
int bdrv_open_backing_file(BlockDriverState *bs, QDict *parent_options,
                           const char *bdref_key, Error **errp)
{
    char *backing_filename = NULL;
    char *bdref_key_dot;
    const char *reference = NULL;
    int ret = 0;
    bool implicit_backing = false;
    BlockDriverState *backing_hd;
    QDict *options;
    QDict *tmp_parent_options = NULL;
    Error *local_err = NULL;

    if (bs->backing != NULL) {
        goto free_exit;
    }

    /* NULL means an empty set of options */
    if (parent_options == NULL) {
        tmp_parent_options = qdict_new();
        parent_options = tmp_parent_options;
    }

    bs->open_flags &= ~BDRV_O_NO_BACKING;

    bdref_key_dot = g_strdup_printf("%s.", bdref_key);
    qdict_extract_subqdict(parent_options, &options, bdref_key_dot);
    g_free(bdref_key_dot);

    /*
     * Caution: while qdict_get_try_str() is fine, getting non-string
     * types would require more care.  When @parent_options come from
     * -blockdev or blockdev_add, its members are typed according to
     * the QAPI schema, but when they come from -drive, they're all
     * QString.
     */
    reference = qdict_get_try_str(parent_options, bdref_key);
    if (reference || qdict_haskey(options, "file.filename")) {
        /* keep backing_filename NULL */
    } else if (bs->backing_file[0] == '\0' && qdict_size(options) == 0) {
        qobject_unref(options);
        goto free_exit;
    } else {
        if (qdict_size(options) == 0) {
            /* If the user specifies options that do not modify the
             * backing file's behavior, we might still consider it the
             * implicit backing file.  But it's easier this way, and
             * just specifying some of the backing BDS's options is
             * only possible with -drive anyway (otherwise the QAPI
             * schema forces the user to specify everything). */
            implicit_backing = !strcmp(bs->auto_backing_file, bs->backing_file);
        }

        backing_filename = bdrv_get_full_backing_filename(bs, &local_err);
        if (local_err) {
            ret = -EINVAL;
            error_propagate(errp, local_err);
            qobject_unref(options);
            goto free_exit;
        }
    }

    if (!bs->drv || !bs->drv->supports_backing) {
        ret = -EINVAL;
        error_setg(errp, "Driver doesn't support backing files");
        qobject_unref(options);
        goto free_exit;
    }

    if (!reference &&
        bs->backing_format[0] != '\0' && !qdict_haskey(options, "driver")) {
        qdict_put_str(options, "driver", bs->backing_format);
    }

    backing_hd = bdrv_open_inherit(backing_filename, reference, options, 0, bs,
                                   &child_backing, errp);
    if (!backing_hd) {
        bs->open_flags |= BDRV_O_NO_BACKING;
        error_prepend(errp, "Could not open backing file: ");
        ret = -EINVAL;
        goto free_exit;
    }

    if (implicit_backing) {
        bdrv_refresh_filename(backing_hd);
        pstrcpy(bs->auto_backing_file, sizeof(bs->auto_backing_file),
                backing_hd->filename);
    }

    /* Hook up the backing file link; drop our reference, bs owns the
     * backing_hd reference now */
    bdrv_set_backing_hd(bs, backing_hd, &local_err);
    bdrv_unref(backing_hd);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto free_exit;
    }

    qdict_del(parent_options, bdref_key);

free_exit:
    g_free(backing_filename);
    qobject_unref(tmp_parent_options);
    return ret;
}

static BlockDriverState *
bdrv_open_child_bs(const char *filename, QDict *options, const char *bdref_key,
                   BlockDriverState *parent, const BdrvChildRole *child_role,
                   bool allow_none, Error **errp)
{
    BlockDriverState *bs = NULL;
    QDict *image_options;
    char *bdref_key_dot;
    const char *reference;

    assert(child_role != NULL);

    bdref_key_dot = g_strdup_printf("%s.", bdref_key);
    qdict_extract_subqdict(options, &image_options, bdref_key_dot);
    g_free(bdref_key_dot);

    /*
     * Caution: while qdict_get_try_str() is fine, getting non-string
     * types would require more care.  When @options come from
     * -blockdev or blockdev_add, its members are typed according to
     * the QAPI schema, but when they come from -drive, they're all
     * QString.
     */
    reference = qdict_get_try_str(options, bdref_key);
    if (!filename && !reference && !qdict_size(image_options)) {
        if (!allow_none) {
            error_setg(errp, "A block device must be specified for \"%s\"",
                       bdref_key);
        }
        qobject_unref(image_options);
        goto done;
    }

    bs = bdrv_open_inherit(filename, reference, image_options, 0,
                           parent, child_role, errp);
    if (!bs) {
        goto done;
    }

done:
    qdict_del(options, bdref_key);
    return bs;
}

/*
 * Opens a disk image whose options are given as BlockdevRef in another block
 * device's options.
 *
 * If allow_none is true, no image will be opened if filename is false and no
 * BlockdevRef is given. NULL will be returned, but errp remains unset.
 *
 * bdrev_key specifies the key for the image's BlockdevRef in the options QDict.
 * That QDict has to be flattened; therefore, if the BlockdevRef is a QDict
 * itself, all options starting with "${bdref_key}." are considered part of the
 * BlockdevRef.
 *
 * The BlockdevRef will be removed from the options QDict.
 */
BdrvChild *bdrv_open_child(const char *filename,
                           QDict *options, const char *bdref_key,
                           BlockDriverState *parent,
                           const BdrvChildRole *child_role,
                           bool allow_none, Error **errp)
{
    BlockDriverState *bs;

    bs = bdrv_open_child_bs(filename, options, bdref_key, parent, child_role,
                            allow_none, errp);
    if (bs == NULL) {
        return NULL;
    }

    return bdrv_attach_child(parent, bs, bdref_key, child_role, errp);
}

/* TODO Future callers may need to specify parent/child_role in order for
 * option inheritance to work. Existing callers use it for the root node. */
BlockDriverState *bdrv_open_blockdev_ref(BlockdevRef *ref, Error **errp)
{
    BlockDriverState *bs = NULL;
    Error *local_err = NULL;
    QObject *obj = NULL;
    QDict *qdict = NULL;
    const char *reference = NULL;
    Visitor *v = NULL;

    if (ref->type == QTYPE_QSTRING) {
        reference = ref->u.reference;
    } else {
        BlockdevOptions *options = &ref->u.definition;
        assert(ref->type == QTYPE_QDICT);

        v = qobject_output_visitor_new(&obj);
        visit_type_BlockdevOptions(v, NULL, &options, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto fail;
        }
        visit_complete(v, &obj);

        qdict = qobject_to(QDict, obj);
        qdict_flatten(qdict);

        /* bdrv_open_inherit() defaults to the values in bdrv_flags (for
         * compatibility with other callers) rather than what we want as the
         * real defaults. Apply the defaults here instead. */
        qdict_set_default_str(qdict, BDRV_OPT_CACHE_DIRECT, "off");
        qdict_set_default_str(qdict, BDRV_OPT_CACHE_NO_FLUSH, "off");
        qdict_set_default_str(qdict, BDRV_OPT_READ_ONLY, "off");
        qdict_set_default_str(qdict, BDRV_OPT_AUTO_READ_ONLY, "off");

    }

    bs = bdrv_open_inherit(NULL, reference, qdict, 0, NULL, NULL, errp);
    obj = NULL;

fail:
    qobject_unref(obj);
    visit_free(v);
    return bs;
}

static BlockDriverState *bdrv_append_temp_snapshot(BlockDriverState *bs,
                                                   int flags,
                                                   QDict *snapshot_options,
                                                   Error **errp)
{
    /* TODO: extra byte is a hack to ensure MAX_PATH space on Windows. */
    char *tmp_filename = g_malloc0(PATH_MAX + 1);
    int64_t total_size;
    QemuOpts *opts = NULL;
    BlockDriverState *bs_snapshot = NULL;
    Error *local_err = NULL;
    int ret;

    /* if snapshot, we create a temporary backing file and open it
       instead of opening 'filename' directly */

    /* Get the required size from the image */
    total_size = bdrv_getlength(bs);
    if (total_size < 0) {
        error_setg_errno(errp, -total_size, "Could not get image size");
        goto out;
    }

    /* Create the temporary image */
    ret = get_tmp_filename(tmp_filename, PATH_MAX + 1);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not get temporary filename");
        goto out;
    }

    opts = qemu_opts_create(bdrv_qcow2.create_opts, NULL, 0,
                            &error_abort);
    qemu_opt_set_number(opts, BLOCK_OPT_SIZE, total_size, &error_abort);
    ret = bdrv_create(&bdrv_qcow2, tmp_filename, opts, errp);
    qemu_opts_del(opts);
    if (ret < 0) {
        error_prepend(errp, "Could not create temporary overlay '%s': ",
                      tmp_filename);
        goto out;
    }

    /* Prepare options QDict for the temporary file */
    qdict_put_str(snapshot_options, "file.driver", "file");
    qdict_put_str(snapshot_options, "file.filename", tmp_filename);
    qdict_put_str(snapshot_options, "driver", "qcow2");

    bs_snapshot = bdrv_open(NULL, NULL, snapshot_options, flags, errp);
    snapshot_options = NULL;
    if (!bs_snapshot) {
        goto out;
    }

    /* bdrv_append() consumes a strong reference to bs_snapshot
     * (i.e. it will call bdrv_unref() on it) even on error, so in
     * order to be able to return one, we have to increase
     * bs_snapshot's refcount here */
    bdrv_ref(bs_snapshot);
    bdrv_append(bs_snapshot, bs, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        bs_snapshot = NULL;
        goto out;
    }

out:
    qobject_unref(snapshot_options);
    g_free(tmp_filename);
    return bs_snapshot;
}

/*
 * Opens a disk image (raw, qcow2, vmdk, ...)
 *
 * options is a QDict of options to pass to the block drivers, or NULL for an
 * empty set of options. The reference to the QDict belongs to the block layer
 * after the call (even on failure), so if the caller intends to reuse the
 * dictionary, it needs to use qobject_ref() before calling bdrv_open.
 *
 * If *pbs is NULL, a new BDS will be created with a pointer to it stored there.
 * If it is not NULL, the referenced BDS will be reused.
 *
 * The reference parameter may be used to specify an existing block device which
 * should be opened. If specified, neither options nor a filename may be given,
 * nor can an existing BDS be reused (that is, *pbs has to be NULL).
 */
static BlockDriverState *bdrv_open_inherit(const char *filename,
                                           const char *reference,
                                           QDict *options, int flags,
                                           BlockDriverState *parent,
                                           const BdrvChildRole *child_role,
                                           Error **errp)
{
    int ret;
    BlockBackend *file = NULL;
    BlockDriverState *bs;
    BlockDriver *drv = NULL;
    BdrvChild *child;
    const char *drvname;
    const char *backing;
    Error *local_err = NULL;
    QDict *snapshot_options = NULL;
    int snapshot_flags = 0;

    assert(!child_role || !flags);
    assert(!child_role == !parent);

    if (reference) {
        bool options_non_empty = options ? qdict_size(options) : false;
        qobject_unref(options);

        if (filename || options_non_empty) {
            error_setg(errp, "Cannot reference an existing block device with "
                       "additional options or a new filename");
            return NULL;
        }

        bs = bdrv_lookup_bs(reference, reference, errp);
        if (!bs) {
            return NULL;
        }

        bdrv_ref(bs);
        return bs;
    }

    bs = bdrv_new();

    /* NULL means an empty set of options */
    if (options == NULL) {
        options = qdict_new();
    }

    /* json: syntax counts as explicit options, as if in the QDict */
    parse_json_protocol(options, &filename, &local_err);
    if (local_err) {
        goto fail;
    }

    bs->explicit_options = qdict_clone_shallow(options);

    if (child_role) {
        bs->inherits_from = parent;
        child_role->inherit_options(&flags, options,
                                    parent->open_flags, parent->options);
    }

    ret = bdrv_fill_options(&options, filename, &flags, &local_err);
    if (local_err) {
        goto fail;
    }

    /*
     * Set the BDRV_O_RDWR and BDRV_O_ALLOW_RDWR flags.
     * Caution: getting a boolean member of @options requires care.
     * When @options come from -blockdev or blockdev_add, members are
     * typed according to the QAPI schema, but when they come from
     * -drive, they're all QString.
     */
    if (g_strcmp0(qdict_get_try_str(options, BDRV_OPT_READ_ONLY), "on") &&
        !qdict_get_try_bool(options, BDRV_OPT_READ_ONLY, false)) {
        flags |= (BDRV_O_RDWR | BDRV_O_ALLOW_RDWR);
    } else {
        flags &= ~BDRV_O_RDWR;
    }

    if (flags & BDRV_O_SNAPSHOT) {
        snapshot_options = qdict_new();
        bdrv_temp_snapshot_options(&snapshot_flags, snapshot_options,
                                   flags, options);
        /* Let bdrv_backing_options() override "read-only" */
        qdict_del(options, BDRV_OPT_READ_ONLY);
        bdrv_backing_options(&flags, options, flags, options);
    }

    bs->open_flags = flags;
    bs->options = options;
    options = qdict_clone_shallow(options);

    /* Find the right image format driver */
    /* See cautionary note on accessing @options above */
    drvname = qdict_get_try_str(options, "driver");
    if (drvname) {
        drv = bdrv_find_format(drvname);
        if (!drv) {
            error_setg(errp, "Unknown driver: '%s'", drvname);
            goto fail;
        }
    }

    assert(drvname || !(flags & BDRV_O_PROTOCOL));

    /* See cautionary note on accessing @options above */
    backing = qdict_get_try_str(options, "backing");
    if (qobject_to(QNull, qdict_get(options, "backing")) != NULL ||
        (backing && *backing == '\0'))
    {
        if (backing) {
            warn_report("Use of \"backing\": \"\" is deprecated; "
                        "use \"backing\": null instead");
        }
        flags |= BDRV_O_NO_BACKING;
        qdict_del(bs->explicit_options, "backing");
        qdict_del(bs->options, "backing");
        qdict_del(options, "backing");
    }

    /* Open image file without format layer. This BlockBackend is only used for
     * probing, the block drivers will do their own bdrv_open_child() for the
     * same BDS, which is why we put the node name back into options. */
    if ((flags & BDRV_O_PROTOCOL) == 0) {
        BlockDriverState *file_bs;

        file_bs = bdrv_open_child_bs(filename, options, "file", bs,
                                     &child_file, true, &local_err);
        if (local_err) {
            goto fail;
        }
        if (file_bs != NULL) {
            /* Not requesting BLK_PERM_CONSISTENT_READ because we're only
             * looking at the header to guess the image format. This works even
             * in cases where a guest would not see a consistent state. */
            file = blk_new(bdrv_get_aio_context(file_bs), 0, BLK_PERM_ALL);
            blk_insert_bs(file, file_bs, &local_err);
            bdrv_unref(file_bs);
            if (local_err) {
                goto fail;
            }

            qdict_put_str(options, "file", bdrv_get_node_name(file_bs));
        }
    }

    /* Image format probing */
    bs->probed = !drv;
    if (!drv && file) {
        ret = find_image_format(file, filename, &drv, &local_err);
        if (ret < 0) {
            goto fail;
        }
        /*
         * This option update would logically belong in bdrv_fill_options(),
         * but we first need to open bs->file for the probing to work, while
         * opening bs->file already requires the (mostly) final set of options
         * so that cache mode etc. can be inherited.
         *
         * Adding the driver later is somewhat ugly, but it's not an option
         * that would ever be inherited, so it's correct. We just need to make
         * sure to update both bs->options (which has the full effective
         * options for bs) and options (which has file.* already removed).
         */
        qdict_put_str(bs->options, "driver", drv->format_name);
        qdict_put_str(options, "driver", drv->format_name);
    } else if (!drv) {
        error_setg(errp, "Must specify either driver or file");
        goto fail;
    }

    /* BDRV_O_PROTOCOL must be set iff a protocol BDS is about to be created */
    assert(!!(flags & BDRV_O_PROTOCOL) == !!drv->bdrv_file_open);
    /* file must be NULL if a protocol BDS is about to be created
     * (the inverse results in an error message from bdrv_open_common()) */
    assert(!(flags & BDRV_O_PROTOCOL) || !file);

    /* Open the image */
    ret = bdrv_open_common(bs, file, options, &local_err);
    if (ret < 0) {
        goto fail;
    }

    if (file) {
        blk_unref(file);
        file = NULL;
    }

    /* If there is a backing file, use it */
    if ((flags & BDRV_O_NO_BACKING) == 0) {
        ret = bdrv_open_backing_file(bs, options, "backing", &local_err);
        if (ret < 0) {
            goto close_and_fail;
        }
    }

    /* Remove all children options and references
     * from bs->options and bs->explicit_options */
    QLIST_FOREACH(child, &bs->children, next) {
        char *child_key_dot;
        child_key_dot = g_strdup_printf("%s.", child->name);
        qdict_extract_subqdict(bs->explicit_options, NULL, child_key_dot);
        qdict_extract_subqdict(bs->options, NULL, child_key_dot);
        qdict_del(bs->explicit_options, child->name);
        qdict_del(bs->options, child->name);
        g_free(child_key_dot);
    }

    /* Check if any unknown options were used */
    if (qdict_size(options) != 0) {
        const QDictEntry *entry = qdict_first(options);
        if (flags & BDRV_O_PROTOCOL) {
            error_setg(errp, "Block protocol '%s' doesn't support the option "
                       "'%s'", drv->format_name, entry->key);
        } else {
            error_setg(errp,
                       "Block format '%s' does not support the option '%s'",
                       drv->format_name, entry->key);
        }

        goto close_and_fail;
    }

    bdrv_parent_cb_change_media(bs, true);

    qobject_unref(options);
    options = NULL;

    /* For snapshot=on, create a temporary qcow2 overlay. bs points to the
     * temporary snapshot afterwards. */
    if (snapshot_flags) {
        BlockDriverState *snapshot_bs;
        snapshot_bs = bdrv_append_temp_snapshot(bs, snapshot_flags,
                                                snapshot_options, &local_err);
        snapshot_options = NULL;
        if (local_err) {
            goto close_and_fail;
        }
        /* We are not going to return bs but the overlay on top of it
         * (snapshot_bs); thus, we have to drop the strong reference to bs
         * (which we obtained by calling bdrv_new()). bs will not be deleted,
         * though, because the overlay still has a reference to it. */
        bdrv_unref(bs);
        bs = snapshot_bs;
    }

    return bs;

fail:
    blk_unref(file);
    qobject_unref(snapshot_options);
    qobject_unref(bs->explicit_options);
    qobject_unref(bs->options);
    qobject_unref(options);
    bs->options = NULL;
    bs->explicit_options = NULL;
    bdrv_unref(bs);
    error_propagate(errp, local_err);
    return NULL;

close_and_fail:
    bdrv_unref(bs);
    qobject_unref(snapshot_options);
    qobject_unref(options);
    error_propagate(errp, local_err);
    return NULL;
}

BlockDriverState *bdrv_open(const char *filename, const char *reference,
                            QDict *options, int flags, Error **errp)
{
    return bdrv_open_inherit(filename, reference, options, flags, NULL,
                             NULL, errp);
}

/* Return true if the NULL-terminated @list contains @str */
static bool is_str_in_list(const char *str, const char *const *list)
{
    if (str && list) {
        int i;
        for (i = 0; list[i] != NULL; i++) {
            if (!strcmp(str, list[i])) {
                return true;
            }
        }
    }
    return false;
}

/*
 * Check that every option set in @bs->options is also set in
 * @new_opts.
 *
 * Options listed in the common_options list and in
 * @bs->drv->mutable_opts are skipped.
 *
 * Return 0 on success, otherwise return -EINVAL and set @errp.
 */
static int bdrv_reset_options_allowed(BlockDriverState *bs,
                                      const QDict *new_opts, Error **errp)
{
    const QDictEntry *e;
    /* These options are common to all block drivers and are handled
     * in bdrv_reopen_prepare() so they can be left out of @new_opts */
    const char *const common_options[] = {
        "node-name", "discard", "cache.direct", "cache.no-flush",
        "read-only", "auto-read-only", "detect-zeroes", NULL
    };

    for (e = qdict_first(bs->options); e; e = qdict_next(bs->options, e)) {
        if (!qdict_haskey(new_opts, e->key) &&
            !is_str_in_list(e->key, common_options) &&
            !is_str_in_list(e->key, bs->drv->mutable_opts)) {
            error_setg(errp, "Option '%s' cannot be reset "
                       "to its default value", e->key);
            return -EINVAL;
        }
    }

    return 0;
}

/*
 * Returns true if @child can be reached recursively from @bs
 */
static bool bdrv_recurse_has_child(BlockDriverState *bs,
                                   BlockDriverState *child)
{
    BdrvChild *c;

    if (bs == child) {
        return true;
    }

    QLIST_FOREACH(c, &bs->children, next) {
        if (bdrv_recurse_has_child(c->bs, child)) {
            return true;
        }
    }

    return false;
}

/*
 * Adds a BlockDriverState to a simple queue for an atomic, transactional
 * reopen of multiple devices.
 *
 * bs_queue can either be an existing BlockReopenQueue that has had QTAILQ_INIT
 * already performed, or alternatively may be NULL a new BlockReopenQueue will
 * be created and initialized. This newly created BlockReopenQueue should be
 * passed back in for subsequent calls that are intended to be of the same
 * atomic 'set'.
 *
 * bs is the BlockDriverState to add to the reopen queue.
 *
 * options contains the changed options for the associated bs
 * (the BlockReopenQueue takes ownership)
 *
 * flags contains the open flags for the associated bs
 *
 * returns a pointer to bs_queue, which is either the newly allocated
 * bs_queue, or the existing bs_queue being used.
 *
 * bs must be drained between bdrv_reopen_queue() and bdrv_reopen_multiple().
 */
static BlockReopenQueue *bdrv_reopen_queue_child(BlockReopenQueue *bs_queue,
                                                 BlockDriverState *bs,
                                                 QDict *options,
                                                 const BdrvChildRole *role,
                                                 QDict *parent_options,
                                                 int parent_flags,
                                                 bool keep_old_opts)
{
    assert(bs != NULL);

    BlockReopenQueueEntry *bs_entry;
    BdrvChild *child;
    QDict *old_options, *explicit_options, *options_copy;
    int flags;
    QemuOpts *opts;

    /* Make sure that the caller remembered to use a drained section. This is
     * important to avoid graph changes between the recursive queuing here and
     * bdrv_reopen_multiple(). */
    assert(bs->quiesce_counter > 0);

    if (bs_queue == NULL) {
        bs_queue = g_new0(BlockReopenQueue, 1);
        QTAILQ_INIT(bs_queue);
    }

    if (!options) {
        options = qdict_new();
    }

    /* Check if this BlockDriverState is already in the queue */
    QTAILQ_FOREACH(bs_entry, bs_queue, entry) {
        if (bs == bs_entry->state.bs) {
            break;
        }
    }

    /*
     * Precedence of options:
     * 1. Explicitly passed in options (highest)
     * 2. Retained from explicitly set options of bs
     * 3. Inherited from parent node
     * 4. Retained from effective options of bs
     */

    /* Old explicitly set values (don't overwrite by inherited value) */
    if (bs_entry || keep_old_opts) {
        old_options = qdict_clone_shallow(bs_entry ?
                                          bs_entry->state.explicit_options :
                                          bs->explicit_options);
        bdrv_join_options(bs, options, old_options);
        qobject_unref(old_options);
    }

    explicit_options = qdict_clone_shallow(options);

    /* Inherit from parent node */
    if (parent_options) {
        flags = 0;
        role->inherit_options(&flags, options, parent_flags, parent_options);
    } else {
        flags = bdrv_get_flags(bs);
    }

    if (keep_old_opts) {
        /* Old values are used for options that aren't set yet */
        old_options = qdict_clone_shallow(bs->options);
        bdrv_join_options(bs, options, old_options);
        qobject_unref(old_options);
    }

    /* We have the final set of options so let's update the flags */
    options_copy = qdict_clone_shallow(options);
    opts = qemu_opts_create(&bdrv_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options_copy, NULL);
    update_flags_from_options(&flags, opts);
    qemu_opts_del(opts);
    qobject_unref(options_copy);

    /* bdrv_open_inherit() sets and clears some additional flags internally */
    flags &= ~BDRV_O_PROTOCOL;
    if (flags & BDRV_O_RDWR) {
        flags |= BDRV_O_ALLOW_RDWR;
    }

    if (!bs_entry) {
        bs_entry = g_new0(BlockReopenQueueEntry, 1);
        QTAILQ_INSERT_TAIL(bs_queue, bs_entry, entry);
    } else {
        qobject_unref(bs_entry->state.options);
        qobject_unref(bs_entry->state.explicit_options);
    }

    bs_entry->state.bs = bs;
    bs_entry->state.options = options;
    bs_entry->state.explicit_options = explicit_options;
    bs_entry->state.flags = flags;

    /* This needs to be overwritten in bdrv_reopen_prepare() */
    bs_entry->state.perm = UINT64_MAX;
    bs_entry->state.shared_perm = 0;

    /*
     * If keep_old_opts is false then it means that unspecified
     * options must be reset to their original value. We don't allow
     * resetting 'backing' but we need to know if the option is
     * missing in order to decide if we have to return an error.
     */
    if (!keep_old_opts) {
        bs_entry->state.backing_missing =
            !qdict_haskey(options, "backing") &&
            !qdict_haskey(options, "backing.driver");
    }

    QLIST_FOREACH(child, &bs->children, next) {
        QDict *new_child_options = NULL;
        bool child_keep_old = keep_old_opts;

        /* reopen can only change the options of block devices that were
         * implicitly created and inherited options. For other (referenced)
         * block devices, a syntax like "backing.foo" results in an error. */
        if (child->bs->inherits_from != bs) {
            continue;
        }

        /* Check if the options contain a child reference */
        if (qdict_haskey(options, child->name)) {
            const char *childref = qdict_get_try_str(options, child->name);
            /*
             * The current child must not be reopened if the child
             * reference is null or points to a different node.
             */
            if (g_strcmp0(childref, child->bs->node_name)) {
                continue;
            }
            /*
             * If the child reference points to the current child then
             * reopen it with its existing set of options (note that
             * it can still inherit new options from the parent).
             */
            child_keep_old = true;
        } else {
            /* Extract child options ("child-name.*") */
            char *child_key_dot = g_strdup_printf("%s.", child->name);
            qdict_extract_subqdict(explicit_options, NULL, child_key_dot);
            qdict_extract_subqdict(options, &new_child_options, child_key_dot);
            g_free(child_key_dot);
        }

        bdrv_reopen_queue_child(bs_queue, child->bs, new_child_options,
                                child->role, options, flags, child_keep_old);
    }

    return bs_queue;
}

BlockReopenQueue *bdrv_reopen_queue(BlockReopenQueue *bs_queue,
                                    BlockDriverState *bs,
                                    QDict *options, bool keep_old_opts)
{
    return bdrv_reopen_queue_child(bs_queue, bs, options, NULL, NULL, 0,
                                   keep_old_opts);
}

/*
 * Reopen multiple BlockDriverStates atomically & transactionally.
 *
 * The queue passed in (bs_queue) must have been built up previous
 * via bdrv_reopen_queue().
 *
 * Reopens all BDS specified in the queue, with the appropriate
 * flags.  All devices are prepared for reopen, and failure of any
 * device will cause all device changes to be abandoned, and intermediate
 * data cleaned up.
 *
 * If all devices prepare successfully, then the changes are committed
 * to all devices.
 *
 * All affected nodes must be drained between bdrv_reopen_queue() and
 * bdrv_reopen_multiple().
 */
int bdrv_reopen_multiple(BlockReopenQueue *bs_queue, Error **errp)
{
    int ret = -1;
    BlockReopenQueueEntry *bs_entry, *next;

    assert(bs_queue != NULL);

    QTAILQ_FOREACH(bs_entry, bs_queue, entry) {
        assert(bs_entry->state.bs->quiesce_counter > 0);
        if (bdrv_reopen_prepare(&bs_entry->state, bs_queue, errp)) {
            goto cleanup;
        }
        bs_entry->prepared = true;
    }

    QTAILQ_FOREACH(bs_entry, bs_queue, entry) {
        BDRVReopenState *state = &bs_entry->state;
        ret = bdrv_check_perm(state->bs, bs_queue, state->perm,
                              state->shared_perm, NULL, NULL, errp);
        if (ret < 0) {
            goto cleanup_perm;
        }
        /* Check if new_backing_bs would accept the new permissions */
        if (state->replace_backing_bs && state->new_backing_bs) {
            uint64_t nperm, nshared;
            bdrv_child_perm(state->bs, state->new_backing_bs,
                            NULL, &child_backing, bs_queue,
                            state->perm, state->shared_perm,
                            &nperm, &nshared);
            ret = bdrv_check_update_perm(state->new_backing_bs, NULL,
                                         nperm, nshared, NULL, NULL, errp);
            if (ret < 0) {
                goto cleanup_perm;
            }
        }
        bs_entry->perms_checked = true;
    }

    /*
     * If we reach this point, we have success and just need to apply the
     * changes.
     *
     * Reverse order is used to comfort qcow2 driver: on commit it need to write
     * IN_USE flag to the image, to mark bitmaps in the image as invalid. But
     * children are usually goes after parents in reopen-queue, so go from last
     * to first element.
     */
    QTAILQ_FOREACH_REVERSE(bs_entry, bs_queue, entry) {
        bdrv_reopen_commit(&bs_entry->state);
    }

    ret = 0;
cleanup_perm:
    QTAILQ_FOREACH_SAFE(bs_entry, bs_queue, entry, next) {
        BDRVReopenState *state = &bs_entry->state;

        if (!bs_entry->perms_checked) {
            continue;
        }

        if (ret == 0) {
            bdrv_set_perm(state->bs, state->perm, state->shared_perm);
        } else {
            bdrv_abort_perm_update(state->bs);
            if (state->replace_backing_bs && state->new_backing_bs) {
                bdrv_abort_perm_update(state->new_backing_bs);
            }
        }
    }
cleanup:
    QTAILQ_FOREACH_SAFE(bs_entry, bs_queue, entry, next) {
        if (ret) {
            if (bs_entry->prepared) {
                bdrv_reopen_abort(&bs_entry->state);
            }
            qobject_unref(bs_entry->state.explicit_options);
            qobject_unref(bs_entry->state.options);
        }
        if (bs_entry->state.new_backing_bs) {
            bdrv_unref(bs_entry->state.new_backing_bs);
        }
        g_free(bs_entry);
    }
    g_free(bs_queue);

    return ret;
}

int bdrv_reopen_set_read_only(BlockDriverState *bs, bool read_only,
                              Error **errp)
{
    int ret;
    BlockReopenQueue *queue;
    QDict *opts = qdict_new();

    qdict_put_bool(opts, BDRV_OPT_READ_ONLY, read_only);

    bdrv_subtree_drained_begin(bs);
    queue = bdrv_reopen_queue(NULL, bs, opts, true);
    ret = bdrv_reopen_multiple(queue, errp);
    bdrv_subtree_drained_end(bs);

    return ret;
}

static BlockReopenQueueEntry *find_parent_in_reopen_queue(BlockReopenQueue *q,
                                                          BdrvChild *c)
{
    BlockReopenQueueEntry *entry;

    QTAILQ_FOREACH(entry, q, entry) {
        BlockDriverState *bs = entry->state.bs;
        BdrvChild *child;

        QLIST_FOREACH(child, &bs->children, next) {
            if (child == c) {
                return entry;
            }
        }
    }

    return NULL;
}

static void bdrv_reopen_perm(BlockReopenQueue *q, BlockDriverState *bs,
                             uint64_t *perm, uint64_t *shared)
{
    BdrvChild *c;
    BlockReopenQueueEntry *parent;
    uint64_t cumulative_perms = 0;
    uint64_t cumulative_shared_perms = BLK_PERM_ALL;

    QLIST_FOREACH(c, &bs->parents, next_parent) {
        parent = find_parent_in_reopen_queue(q, c);
        if (!parent) {
            cumulative_perms |= c->perm;
            cumulative_shared_perms &= c->shared_perm;
        } else {
            uint64_t nperm, nshared;

            bdrv_child_perm(parent->state.bs, bs, c, c->role, q,
                            parent->state.perm, parent->state.shared_perm,
                            &nperm, &nshared);

            cumulative_perms |= nperm;
            cumulative_shared_perms &= nshared;
        }
    }
    *perm = cumulative_perms;
    *shared = cumulative_shared_perms;
}

/*
 * Take a BDRVReopenState and check if the value of 'backing' in the
 * reopen_state->options QDict is valid or not.
 *
 * If 'backing' is missing from the QDict then return 0.
 *
 * If 'backing' contains the node name of the backing file of
 * reopen_state->bs then return 0.
 *
 * If 'backing' contains a different node name (or is null) then check
 * whether the current backing file can be replaced with the new one.
 * If that's the case then reopen_state->replace_backing_bs is set to
 * true and reopen_state->new_backing_bs contains a pointer to the new
 * backing BlockDriverState (or NULL).
 *
 * Return 0 on success, otherwise return < 0 and set @errp.
 */
static int bdrv_reopen_parse_backing(BDRVReopenState *reopen_state,
                                     Error **errp)
{
    BlockDriverState *bs = reopen_state->bs;
    BlockDriverState *overlay_bs, *new_backing_bs;
    QObject *value;
    const char *str;

    value = qdict_get(reopen_state->options, "backing");
    if (value == NULL) {
        return 0;
    }

    switch (qobject_type(value)) {
    case QTYPE_QNULL:
        new_backing_bs = NULL;
        break;
    case QTYPE_QSTRING:
        str = qobject_get_try_str(value);
        new_backing_bs = bdrv_lookup_bs(NULL, str, errp);
        if (new_backing_bs == NULL) {
            return -EINVAL;
        } else if (bdrv_recurse_has_child(new_backing_bs, bs)) {
            error_setg(errp, "Making '%s' a backing file of '%s' "
                       "would create a cycle", str, bs->node_name);
            return -EINVAL;
        }
        break;
    default:
        /* 'backing' does not allow any other data type */
        g_assert_not_reached();
    }

    /*
     * TODO: before removing the x- prefix from x-blockdev-reopen we
     * should move the new backing file into the right AioContext
     * instead of returning an error.
     */
    if (new_backing_bs) {
        if (bdrv_get_aio_context(new_backing_bs) != bdrv_get_aio_context(bs)) {
            error_setg(errp, "Cannot use a new backing file "
                       "with a different AioContext");
            return -EINVAL;
        }
    }

    /*
     * Find the "actual" backing file by skipping all links that point
     * to an implicit node, if any (e.g. a commit filter node).
     */
    overlay_bs = bs;
    while (backing_bs(overlay_bs) && backing_bs(overlay_bs)->implicit) {
        overlay_bs = backing_bs(overlay_bs);
    }

    /* If we want to replace the backing file we need some extra checks */
    if (new_backing_bs != backing_bs(overlay_bs)) {
        /* Check for implicit nodes between bs and its backing file */
        if (bs != overlay_bs) {
            error_setg(errp, "Cannot change backing link if '%s' has "
                       "an implicit backing file", bs->node_name);
            return -EPERM;
        }
        /* Check if the backing link that we want to replace is frozen */
        if (bdrv_is_backing_chain_frozen(overlay_bs, backing_bs(overlay_bs),
                                         errp)) {
            return -EPERM;
        }
        reopen_state->replace_backing_bs = true;
        if (new_backing_bs) {
            bdrv_ref(new_backing_bs);
            reopen_state->new_backing_bs = new_backing_bs;
        }
    }

    return 0;
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
    int old_flags;
    Error *local_err = NULL;
    BlockDriver *drv;
    QemuOpts *opts;
    QDict *orig_reopen_opts;
    char *discard = NULL;
    bool read_only;
    bool drv_prepared = false;

    assert(reopen_state != NULL);
    assert(reopen_state->bs->drv != NULL);
    drv = reopen_state->bs->drv;

    /* This function and each driver's bdrv_reopen_prepare() remove
     * entries from reopen_state->options as they are processed, so
     * we need to make a copy of the original QDict. */
    orig_reopen_opts = qdict_clone_shallow(reopen_state->options);

    /* Process generic block layer options */
    opts = qemu_opts_create(&bdrv_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, reopen_state->options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto error;
    }

    /* This was already called in bdrv_reopen_queue_child() so the flags
     * are up-to-date. This time we simply want to remove the options from
     * QemuOpts in order to indicate that they have been processed. */
    old_flags = reopen_state->flags;
    update_flags_from_options(&reopen_state->flags, opts);
    assert(old_flags == reopen_state->flags);

    discard = qemu_opt_get_del(opts, BDRV_OPT_DISCARD);
    if (discard != NULL) {
        if (bdrv_parse_discard_flags(discard, &reopen_state->flags) != 0) {
            error_setg(errp, "Invalid discard option");
            ret = -EINVAL;
            goto error;
        }
    }

    reopen_state->detect_zeroes =
        bdrv_parse_detect_zeroes(opts, reopen_state->flags, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto error;
    }

    /* All other options (including node-name and driver) must be unchanged.
     * Put them back into the QDict, so that they are checked at the end
     * of this function. */
    qemu_opts_to_qdict(opts, reopen_state->options);

    /* If we are to stay read-only, do not allow permission change
     * to r/w. Attempting to set to r/w may fail if either BDRV_O_ALLOW_RDWR is
     * not set, or if the BDS still has copy_on_read enabled */
    read_only = !(reopen_state->flags & BDRV_O_RDWR);
    ret = bdrv_can_set_read_only(reopen_state->bs, read_only, true, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto error;
    }

    /* Calculate required permissions after reopening */
    bdrv_reopen_perm(queue, reopen_state->bs,
                     &reopen_state->perm, &reopen_state->shared_perm);

    ret = bdrv_flush(reopen_state->bs);
    if (ret) {
        error_setg_errno(errp, -ret, "Error flushing drive");
        goto error;
    }

    if (drv->bdrv_reopen_prepare) {
        /*
         * If a driver-specific option is missing, it means that we
         * should reset it to its default value.
         * But not all options allow that, so we need to check it first.
         */
        ret = bdrv_reset_options_allowed(reopen_state->bs,
                                         reopen_state->options, errp);
        if (ret) {
            goto error;
        }

        ret = drv->bdrv_reopen_prepare(reopen_state, queue, &local_err);
        if (ret) {
            if (local_err != NULL) {
                error_propagate(errp, local_err);
            } else {
                bdrv_refresh_filename(reopen_state->bs);
                error_setg(errp, "failed while preparing to reopen image '%s'",
                           reopen_state->bs->filename);
            }
            goto error;
        }
    } else {
        /* It is currently mandatory to have a bdrv_reopen_prepare()
         * handler for each supported drv. */
        error_setg(errp, "Block format '%s' used by node '%s' "
                   "does not support reopening files", drv->format_name,
                   bdrv_get_device_or_node_name(reopen_state->bs));
        ret = -1;
        goto error;
    }

    drv_prepared = true;

    /*
     * We must provide the 'backing' option if the BDS has a backing
     * file or if the image file has a backing file name as part of
     * its metadata. Otherwise the 'backing' option can be omitted.
     */
    if (drv->supports_backing && reopen_state->backing_missing &&
        (backing_bs(reopen_state->bs) || reopen_state->bs->backing_file[0])) {
        error_setg(errp, "backing is missing for '%s'",
                   reopen_state->bs->node_name);
        ret = -EINVAL;
        goto error;
    }

    /*
     * Allow changing the 'backing' option. The new value can be
     * either a reference to an existing node (using its node name)
     * or NULL to simply detach the current backing file.
     */
    ret = bdrv_reopen_parse_backing(reopen_state, errp);
    if (ret < 0) {
        goto error;
    }
    qdict_del(reopen_state->options, "backing");

    /* Options that are not handled are only okay if they are unchanged
     * compared to the old state. It is expected that some options are only
     * used for the initial open, but not reopen (e.g. filename) */
    if (qdict_size(reopen_state->options)) {
        const QDictEntry *entry = qdict_first(reopen_state->options);

        do {
            QObject *new = entry->value;
            QObject *old = qdict_get(reopen_state->bs->options, entry->key);

            /* Allow child references (child_name=node_name) as long as they
             * point to the current child (i.e. everything stays the same). */
            if (qobject_type(new) == QTYPE_QSTRING) {
                BdrvChild *child;
                QLIST_FOREACH(child, &reopen_state->bs->children, next) {
                    if (!strcmp(child->name, entry->key)) {
                        break;
                    }
                }

                if (child) {
                    const char *str = qobject_get_try_str(new);
                    if (!strcmp(child->bs->node_name, str)) {
                        continue; /* Found child with this name, skip option */
                    }
                }
            }

            /*
             * TODO: When using -drive to specify blockdev options, all values
             * will be strings; however, when using -blockdev, blockdev-add or
             * filenames using the json:{} pseudo-protocol, they will be
             * correctly typed.
             * In contrast, reopening options are (currently) always strings
             * (because you can only specify them through qemu-io; all other
             * callers do not specify any options).
             * Therefore, when using anything other than -drive to create a BDS,
             * this cannot detect non-string options as unchanged, because
             * qobject_is_equal() always returns false for objects of different
             * type.  In the future, this should be remedied by correctly typing
             * all options.  For now, this is not too big of an issue because
             * the user can simply omit options which cannot be changed anyway,
             * so they will stay unchanged.
             */
            if (!qobject_is_equal(new, old)) {
                error_setg(errp, "Cannot change the option '%s'", entry->key);
                ret = -EINVAL;
                goto error;
            }
        } while ((entry = qdict_next(reopen_state->options, entry)));
    }

    ret = 0;

    /* Restore the original reopen_state->options QDict */
    qobject_unref(reopen_state->options);
    reopen_state->options = qobject_ref(orig_reopen_opts);

error:
    if (ret < 0 && drv_prepared) {
        /* drv->bdrv_reopen_prepare() has succeeded, so we need to
         * call drv->bdrv_reopen_abort() before signaling an error
         * (bdrv_reopen_multiple() will not call bdrv_reopen_abort()
         * when the respective bdrv_reopen_prepare() has failed) */
        if (drv->bdrv_reopen_abort) {
            drv->bdrv_reopen_abort(reopen_state);
        }
    }
    qemu_opts_del(opts);
    qobject_unref(orig_reopen_opts);
    g_free(discard);
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
    BlockDriverState *bs;
    BdrvChild *child;

    assert(reopen_state != NULL);
    bs = reopen_state->bs;
    drv = bs->drv;
    assert(drv != NULL);

    /* If there are any driver level actions to take */
    if (drv->bdrv_reopen_commit) {
        drv->bdrv_reopen_commit(reopen_state);
    }

    /* set BDS specific flags now */
    qobject_unref(bs->explicit_options);
    qobject_unref(bs->options);

    bs->explicit_options   = reopen_state->explicit_options;
    bs->options            = reopen_state->options;
    bs->open_flags         = reopen_state->flags;
    bs->read_only = !(reopen_state->flags & BDRV_O_RDWR);
    bs->detect_zeroes      = reopen_state->detect_zeroes;

    if (reopen_state->replace_backing_bs) {
        qdict_del(bs->explicit_options, "backing");
        qdict_del(bs->options, "backing");
    }

    /* Remove child references from bs->options and bs->explicit_options.
     * Child options were already removed in bdrv_reopen_queue_child() */
    QLIST_FOREACH(child, &bs->children, next) {
        qdict_del(bs->explicit_options, child->name);
        qdict_del(bs->options, child->name);
    }

    /*
     * Change the backing file if a new one was specified. We do this
     * after updating bs->options, so bdrv_refresh_filename() (called
     * from bdrv_set_backing_hd()) has the new values.
     */
    if (reopen_state->replace_backing_bs) {
        BlockDriverState *old_backing_bs = backing_bs(bs);
        assert(!old_backing_bs || !old_backing_bs->implicit);
        /* Abort the permission update on the backing bs we're detaching */
        if (old_backing_bs) {
            bdrv_abort_perm_update(old_backing_bs);
        }
        bdrv_set_backing_hd(bs, reopen_state->new_backing_bs, &error_abort);
    }

    bdrv_refresh_limits(bs, NULL);
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


static void bdrv_close(BlockDriverState *bs)
{
    BdrvAioNotifier *ban, *ban_next;
    BdrvChild *child, *next;

    assert(!bs->refcnt);

    bdrv_drained_begin(bs); /* complete I/O */
    bdrv_flush(bs);
    bdrv_drain(bs); /* in case flush left pending I/O */

    if (bs->drv) {
        if (bs->drv->bdrv_close) {
            bs->drv->bdrv_close(bs);
        }
        bs->drv = NULL;
    }

    QLIST_FOREACH_SAFE(child, &bs->children, next, next) {
        bdrv_unref_child(bs, child);
    }

    bs->backing = NULL;
    bs->file = NULL;
    g_free(bs->opaque);
    bs->opaque = NULL;
    atomic_set(&bs->copy_on_read, 0);
    bs->backing_file[0] = '\0';
    bs->backing_format[0] = '\0';
    bs->total_sectors = 0;
    bs->encrypted = false;
    bs->sg = false;
    qobject_unref(bs->options);
    qobject_unref(bs->explicit_options);
    bs->options = NULL;
    bs->explicit_options = NULL;
    qobject_unref(bs->full_open_options);
    bs->full_open_options = NULL;

    bdrv_release_named_dirty_bitmaps(bs);
    assert(QLIST_EMPTY(&bs->dirty_bitmaps));

    QLIST_FOREACH_SAFE(ban, &bs->aio_notifiers, list, ban_next) {
        g_free(ban);
    }
    QLIST_INIT(&bs->aio_notifiers);
    bdrv_drained_end(bs);
}

void bdrv_close_all(void)
{
    assert(job_next(NULL) == NULL);
    nbd_export_close_all();

    /* Drop references from requests still in flight, such as canceled block
     * jobs whose AIO context has not been polled yet */
    bdrv_drain_all();

    blk_remove_all_bs();
    blockdev_close_all_bdrv_states();

    assert(QTAILQ_EMPTY(&all_bdrv_states));
}

static bool should_update_child(BdrvChild *c, BlockDriverState *to)
{
    GQueue *queue;
    GHashTable *found;
    bool ret;

    if (c->role->stay_at_node) {
        return false;
    }

    /* If the child @c belongs to the BDS @to, replacing the current
     * c->bs by @to would mean to create a loop.
     *
     * Such a case occurs when appending a BDS to a backing chain.
     * For instance, imagine the following chain:
     *
     *   guest device -> node A -> further backing chain...
     *
     * Now we create a new BDS B which we want to put on top of this
     * chain, so we first attach A as its backing node:
     *
     *                   node B
     *                     |
     *                     v
     *   guest device -> node A -> further backing chain...
     *
     * Finally we want to replace A by B.  When doing that, we want to
     * replace all pointers to A by pointers to B -- except for the
     * pointer from B because (1) that would create a loop, and (2)
     * that pointer should simply stay intact:
     *
     *   guest device -> node B
     *                     |
     *                     v
     *                   node A -> further backing chain...
     *
     * In general, when replacing a node A (c->bs) by a node B (@to),
     * if A is a child of B, that means we cannot replace A by B there
     * because that would create a loop.  Silently detaching A from B
     * is also not really an option.  So overall just leaving A in
     * place there is the most sensible choice.
     *
     * We would also create a loop in any cases where @c is only
     * indirectly referenced by @to. Prevent this by returning false
     * if @c is found (by breadth-first search) anywhere in the whole
     * subtree of @to.
     */

    ret = true;
    found = g_hash_table_new(NULL, NULL);
    g_hash_table_add(found, to);
    queue = g_queue_new();
    g_queue_push_tail(queue, to);

    while (!g_queue_is_empty(queue)) {
        BlockDriverState *v = g_queue_pop_head(queue);
        BdrvChild *c2;

        QLIST_FOREACH(c2, &v->children, next) {
            if (c2 == c) {
                ret = false;
                break;
            }

            if (g_hash_table_contains(found, c2->bs)) {
                continue;
            }

            g_queue_push_tail(queue, c2->bs);
            g_hash_table_add(found, c2->bs);
        }
    }

    g_queue_free(queue);
    g_hash_table_destroy(found);

    return ret;
}

void bdrv_replace_node(BlockDriverState *from, BlockDriverState *to,
                       Error **errp)
{
    BdrvChild *c, *next;
    GSList *list = NULL, *p;
    uint64_t perm = 0, shared = BLK_PERM_ALL;
    int ret;

    /* Make sure that @from doesn't go away until we have successfully attached
     * all of its parents to @to. */
    bdrv_ref(from);

    assert(qemu_get_current_aio_context() == qemu_get_aio_context());
    bdrv_drained_begin(from);

    /* Put all parents into @list and calculate their cumulative permissions */
    QLIST_FOREACH_SAFE(c, &from->parents, next_parent, next) {
        assert(c->bs == from);
        if (!should_update_child(c, to)) {
            continue;
        }
        if (c->frozen) {
            error_setg(errp, "Cannot change '%s' link to '%s'",
                       c->name, from->node_name);
            goto out;
        }
        list = g_slist_prepend(list, c);
        perm |= c->perm;
        shared &= c->shared_perm;
    }

    /* Check whether the required permissions can be granted on @to, ignoring
     * all BdrvChild in @list so that they can't block themselves. */
    ret = bdrv_check_update_perm(to, NULL, perm, shared, list, NULL, errp);
    if (ret < 0) {
        bdrv_abort_perm_update(to);
        goto out;
    }

    /* Now actually perform the change. We performed the permission check for
     * all elements of @list at once, so set the permissions all at once at the
     * very end. */
    for (p = list; p != NULL; p = p->next) {
        c = p->data;

        bdrv_ref(to);
        bdrv_replace_child_noperm(c, to);
        bdrv_unref(from);
    }

    bdrv_get_cumulative_perm(to, &perm, &shared);
    bdrv_set_perm(to, perm, shared);

out:
    g_slist_free(list);
    bdrv_drained_end(from);
    bdrv_unref(from);
}

/*
 * Add new bs contents at the top of an image chain while the chain is
 * live, while keeping required fields on the top layer.
 *
 * This will modify the BlockDriverState fields, and swap contents
 * between bs_new and bs_top. Both bs_new and bs_top are modified.
 *
 * bs_new must not be attached to a BlockBackend.
 *
 * This function does not create any image files.
 *
 * bdrv_append() takes ownership of a bs_new reference and unrefs it because
 * that's what the callers commonly need. bs_new will be referenced by the old
 * parents of bs_top after bdrv_append() returns. If the caller needs to keep a
 * reference of its own, it must call bdrv_ref().
 */
void bdrv_append(BlockDriverState *bs_new, BlockDriverState *bs_top,
                 Error **errp)
{
    Error *local_err = NULL;

    bdrv_set_backing_hd(bs_new, bs_top, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out;
    }

    bdrv_replace_node(bs_top, bs_new, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        bdrv_set_backing_hd(bs_new, NULL, &error_abort);
        goto out;
    }

    /* bs_new is now referenced by its new parents, we don't need the
     * additional reference any more. */
out:
    bdrv_unref(bs_new);
}

static void bdrv_delete(BlockDriverState *bs)
{
    assert(bdrv_op_blocker_is_empty(bs));
    assert(!bs->refcnt);

    /* remove from list, if necessary */
    if (bs->node_name[0] != '\0') {
        QTAILQ_REMOVE(&graph_bdrv_states, bs, node_list);
    }
    QTAILQ_REMOVE(&all_bdrv_states, bs, bs_list);

    bdrv_close(bs);

    g_free(bs);
}

/*
 * Run consistency checks on an image
 *
 * Returns 0 if the check could be completed (it doesn't mean that the image is
 * free of errors) or -errno when an internal error occurred. The results of the
 * check are stored in res.
 */
static int coroutine_fn bdrv_co_check(BlockDriverState *bs,
                                      BdrvCheckResult *res, BdrvCheckMode fix)
{
    if (bs->drv == NULL) {
        return -ENOMEDIUM;
    }
    if (bs->drv->bdrv_co_check == NULL) {
        return -ENOTSUP;
    }

    memset(res, 0, sizeof(*res));
    return bs->drv->bdrv_co_check(bs, res, fix);
}

typedef struct CheckCo {
    BlockDriverState *bs;
    BdrvCheckResult *res;
    BdrvCheckMode fix;
    int ret;
} CheckCo;

static void coroutine_fn bdrv_check_co_entry(void *opaque)
{
    CheckCo *cco = opaque;
    cco->ret = bdrv_co_check(cco->bs, cco->res, cco->fix);
    aio_wait_kick();
}

int bdrv_check(BlockDriverState *bs,
               BdrvCheckResult *res, BdrvCheckMode fix)
{
    Coroutine *co;
    CheckCo cco = {
        .bs = bs,
        .res = res,
        .ret = -EINPROGRESS,
        .fix = fix,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_check_co_entry(&cco);
    } else {
        co = qemu_coroutine_create(bdrv_check_co_entry, &cco);
        bdrv_coroutine_enter(bs, co);
        BDRV_POLL_WHILE(bs, cco.ret == -EINPROGRESS);
    }

    return cco.ret;
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

    if (!drv) {
        return -ENOMEDIUM;
    }

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
        pstrcpy(bs->auto_backing_file, sizeof(bs->auto_backing_file),
                backing_file ?: "");
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
    while (active && bs != backing_bs(active)) {
        active = backing_bs(active);
    }

    return active;
}

/* Given a BDS, searches for the base layer. */
BlockDriverState *bdrv_find_base(BlockDriverState *bs)
{
    return bdrv_find_overlay(bs, NULL);
}

/*
 * Return true if at least one of the backing links between @bs and
 * @base is frozen. @errp is set if that's the case.
 * @base must be reachable from @bs, or NULL.
 */
bool bdrv_is_backing_chain_frozen(BlockDriverState *bs, BlockDriverState *base,
                                  Error **errp)
{
    BlockDriverState *i;

    for (i = bs; i != base; i = backing_bs(i)) {
        if (i->backing && i->backing->frozen) {
            error_setg(errp, "Cannot change '%s' link from '%s' to '%s'",
                       i->backing->name, i->node_name,
                       backing_bs(i)->node_name);
            return true;
        }
    }

    return false;
}

/*
 * Freeze all backing links between @bs and @base.
 * If any of the links is already frozen the operation is aborted and
 * none of the links are modified.
 * @base must be reachable from @bs, or NULL.
 * Returns 0 on success. On failure returns < 0 and sets @errp.
 */
int bdrv_freeze_backing_chain(BlockDriverState *bs, BlockDriverState *base,
                              Error **errp)
{
    BlockDriverState *i;

    if (bdrv_is_backing_chain_frozen(bs, base, errp)) {
        return -EPERM;
    }

    for (i = bs; i != base; i = backing_bs(i)) {
        if (i->backing && backing_bs(i)->never_freeze) {
            error_setg(errp, "Cannot freeze '%s' link to '%s'",
                       i->backing->name, backing_bs(i)->node_name);
            return -EPERM;
        }
    }

    for (i = bs; i != base; i = backing_bs(i)) {
        if (i->backing) {
            i->backing->frozen = true;
        }
    }

    return 0;
}

/*
 * Unfreeze all backing links between @bs and @base. The caller must
 * ensure that all links are frozen before using this function.
 * @base must be reachable from @bs, or NULL.
 */
void bdrv_unfreeze_backing_chain(BlockDriverState *bs, BlockDriverState *base)
{
    BlockDriverState *i;

    for (i = bs; i != base; i = backing_bs(i)) {
        if (i->backing) {
            assert(i->backing->frozen);
            i->backing->frozen = false;
        }
    }
}

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
int bdrv_drop_intermediate(BlockDriverState *top, BlockDriverState *base,
                           const char *backing_file_str)
{
    BlockDriverState *explicit_top = top;
    bool update_inherits_from;
    BdrvChild *c, *next;
    Error *local_err = NULL;
    int ret = -EIO;

    bdrv_ref(top);
    bdrv_subtree_drained_begin(top);

    if (!top->drv || !base->drv) {
        goto exit;
    }

    /* Make sure that base is in the backing chain of top */
    if (!bdrv_chain_contains(top, base)) {
        goto exit;
    }

    /* This function changes all links that point to top and makes
     * them point to base. Check that none of them is frozen. */
    QLIST_FOREACH(c, &top->parents, next_parent) {
        if (c->frozen) {
            goto exit;
        }
    }

    /* If 'base' recursively inherits from 'top' then we should set
     * base->inherits_from to top->inherits_from after 'top' and all
     * other intermediate nodes have been dropped.
     * If 'top' is an implicit node (e.g. "commit_top") we should skip
     * it because no one inherits from it. We use explicit_top for that. */
    while (explicit_top && explicit_top->implicit) {
        explicit_top = backing_bs(explicit_top);
    }
    update_inherits_from = bdrv_inherits_from_recursive(base, explicit_top);

    /* success - we can delete the intermediate states, and link top->base */
    /* TODO Check graph modification op blockers (BLK_PERM_GRAPH_MOD) once
     * we've figured out how they should work. */
    if (!backing_file_str) {
        bdrv_refresh_filename(base);
        backing_file_str = base->filename;
    }

    QLIST_FOREACH_SAFE(c, &top->parents, next_parent, next) {
        /* Check whether we are allowed to switch c from top to base */
        GSList *ignore_children = g_slist_prepend(NULL, c);
        ret = bdrv_check_update_perm(base, NULL, c->perm, c->shared_perm,
                                     ignore_children, NULL, &local_err);
        g_slist_free(ignore_children);
        if (ret < 0) {
            error_report_err(local_err);
            goto exit;
        }

        /* If so, update the backing file path in the image file */
        if (c->role->update_filename) {
            ret = c->role->update_filename(c, base, backing_file_str,
                                           &local_err);
            if (ret < 0) {
                bdrv_abort_perm_update(base);
                error_report_err(local_err);
                goto exit;
            }
        }

        /* Do the actual switch in the in-memory graph.
         * Completes bdrv_check_update_perm() transaction internally. */
        bdrv_ref(base);
        bdrv_replace_child(c, base);
        bdrv_unref(top);
    }

    if (update_inherits_from) {
        base->inherits_from = explicit_top->inherits_from;
    }

    ret = 0;
exit:
    bdrv_subtree_drained_end(top);
    bdrv_unref(top);
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
        return bdrv_get_allocated_file_size(bs->file->bs);
    }
    return -ENOTSUP;
}

/*
 * bdrv_measure:
 * @drv: Format driver
 * @opts: Creation options for new image
 * @in_bs: Existing image containing data for new image (may be NULL)
 * @errp: Error object
 * Returns: A #BlockMeasureInfo (free using qapi_free_BlockMeasureInfo())
 *          or NULL on error
 *
 * Calculate file size required to create a new image.
 *
 * If @in_bs is given then space for allocated clusters and zero clusters
 * from that image are included in the calculation.  If @opts contains a
 * backing file that is shared by @in_bs then backing clusters may be omitted
 * from the calculation.
 *
 * If @in_bs is NULL then the calculation includes no allocated clusters
 * unless a preallocation option is given in @opts.
 *
 * Note that @in_bs may use a different BlockDriver from @drv.
 *
 * If an error occurs the @errp pointer is set.
 */
BlockMeasureInfo *bdrv_measure(BlockDriver *drv, QemuOpts *opts,
                               BlockDriverState *in_bs, Error **errp)
{
    if (!drv->bdrv_measure) {
        error_setg(errp, "Block driver '%s' does not support size measurement",
                   drv->format_name);
        return NULL;
    }

    return drv->bdrv_measure(opts, in_bs, errp);
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

    ret = ret > INT64_MAX / BDRV_SECTOR_SIZE ? -EFBIG : ret;
    return ret < 0 ? ret : ret * BDRV_SECTOR_SIZE;
}

/* return 0 as number of sectors if no device present or error */
void bdrv_get_geometry(BlockDriverState *bs, uint64_t *nb_sectors_ptr)
{
    int64_t nb_sectors = bdrv_nb_sectors(bs);

    *nb_sectors_ptr = nb_sectors < 0 ? 0 : nb_sectors;
}

bool bdrv_is_sg(BlockDriverState *bs)
{
    return bs->sg;
}

bool bdrv_is_encrypted(BlockDriverState *bs)
{
    if (bs->backing && bs->backing->bs->encrypted) {
        return true;
    }
    return bs->encrypted;
}

const char *bdrv_get_format_name(BlockDriverState *bs)
{
    return bs->drv ? bs->drv->format_name : NULL;
}

static int qsort_strcmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void bdrv_iterate_format(void (*it)(void *opaque, const char *name),
                         void *opaque, bool read_only)
{
    BlockDriver *drv;
    int count = 0;
    int i;
    const char **formats = NULL;

    QLIST_FOREACH(drv, &bdrv_drivers, list) {
        if (drv->format_name) {
            bool found = false;
            int i = count;

            if (use_bdrv_whitelist && !bdrv_is_whitelisted(drv, read_only)) {
                continue;
            }

            while (formats && i && !found) {
                found = !strcmp(formats[--i], drv->format_name);
            }

            if (!found) {
                formats = g_renew(const char *, formats, count + 1);
                formats[count++] = drv->format_name;
            }
        }
    }

    for (i = 0; i < (int)ARRAY_SIZE(block_driver_modules); i++) {
        const char *format_name = block_driver_modules[i].format_name;

        if (format_name) {
            bool found = false;
            int j = count;

            if (use_bdrv_whitelist &&
                !bdrv_format_is_whitelisted(format_name, read_only)) {
                continue;
            }

            while (formats && j && !found) {
                found = !strcmp(formats[--j], format_name);
            }

            if (!found) {
                formats = g_renew(const char *, formats, count + 1);
                formats[count++] = format_name;
            }
        }
    }

    qsort(formats, count, sizeof(formats[0]), qsort_strcmp);

    for (i = 0; i < count; i++) {
        it(opaque, formats[i]);
    }

    g_free(formats);
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
BlockDeviceInfoList *bdrv_named_nodes_list(Error **errp)
{
    BlockDeviceInfoList *list, *entry;
    BlockDriverState *bs;

    list = NULL;
    QTAILQ_FOREACH(bs, &graph_bdrv_states, node_list) {
        BlockDeviceInfo *info = bdrv_block_device_info(NULL, bs, errp);
        if (!info) {
            qapi_free_BlockDeviceInfoList(list);
            return NULL;
        }
        entry = g_malloc0(sizeof(*entry));
        entry->value = info;
        entry->next = list;
        list = entry;
    }

    return list;
}

#define QAPI_LIST_ADD(list, element) do { \
    typeof(list) _tmp = g_new(typeof(*(list)), 1); \
    _tmp->value = (element); \
    _tmp->next = (list); \
    (list) = _tmp; \
} while (0)

typedef struct XDbgBlockGraphConstructor {
    XDbgBlockGraph *graph;
    GHashTable *graph_nodes;
} XDbgBlockGraphConstructor;

static XDbgBlockGraphConstructor *xdbg_graph_new(void)
{
    XDbgBlockGraphConstructor *gr = g_new(XDbgBlockGraphConstructor, 1);

    gr->graph = g_new0(XDbgBlockGraph, 1);
    gr->graph_nodes = g_hash_table_new(NULL, NULL);

    return gr;
}

static XDbgBlockGraph *xdbg_graph_finalize(XDbgBlockGraphConstructor *gr)
{
    XDbgBlockGraph *graph = gr->graph;

    g_hash_table_destroy(gr->graph_nodes);
    g_free(gr);

    return graph;
}

static uintptr_t xdbg_graph_node_num(XDbgBlockGraphConstructor *gr, void *node)
{
    uintptr_t ret = (uintptr_t)g_hash_table_lookup(gr->graph_nodes, node);

    if (ret != 0) {
        return ret;
    }

    /*
     * Start counting from 1, not 0, because 0 interferes with not-found (NULL)
     * answer of g_hash_table_lookup.
     */
    ret = g_hash_table_size(gr->graph_nodes) + 1;
    g_hash_table_insert(gr->graph_nodes, node, (void *)ret);

    return ret;
}

static void xdbg_graph_add_node(XDbgBlockGraphConstructor *gr, void *node,
                                XDbgBlockGraphNodeType type, const char *name)
{
    XDbgBlockGraphNode *n;

    n = g_new0(XDbgBlockGraphNode, 1);

    n->id = xdbg_graph_node_num(gr, node);
    n->type = type;
    n->name = g_strdup(name);

    QAPI_LIST_ADD(gr->graph->nodes, n);
}

static void xdbg_graph_add_edge(XDbgBlockGraphConstructor *gr, void *parent,
                                const BdrvChild *child)
{
    BlockPermission qapi_perm;
    XDbgBlockGraphEdge *edge;

    edge = g_new0(XDbgBlockGraphEdge, 1);

    edge->parent = xdbg_graph_node_num(gr, parent);
    edge->child = xdbg_graph_node_num(gr, child->bs);
    edge->name = g_strdup(child->name);

    for (qapi_perm = 0; qapi_perm < BLOCK_PERMISSION__MAX; qapi_perm++) {
        uint64_t flag = bdrv_qapi_perm_to_blk_perm(qapi_perm);

        if (flag & child->perm) {
            QAPI_LIST_ADD(edge->perm, qapi_perm);
        }
        if (flag & child->shared_perm) {
            QAPI_LIST_ADD(edge->shared_perm, qapi_perm);
        }
    }

    QAPI_LIST_ADD(gr->graph->edges, edge);
}


XDbgBlockGraph *bdrv_get_xdbg_block_graph(Error **errp)
{
    BlockBackend *blk;
    BlockJob *job;
    BlockDriverState *bs;
    BdrvChild *child;
    XDbgBlockGraphConstructor *gr = xdbg_graph_new();

    for (blk = blk_all_next(NULL); blk; blk = blk_all_next(blk)) {
        char *allocated_name = NULL;
        const char *name = blk_name(blk);

        if (!*name) {
            name = allocated_name = blk_get_attached_dev_id(blk);
        }
        xdbg_graph_add_node(gr, blk, X_DBG_BLOCK_GRAPH_NODE_TYPE_BLOCK_BACKEND,
                           name);
        g_free(allocated_name);
        if (blk_root(blk)) {
            xdbg_graph_add_edge(gr, blk, blk_root(blk));
        }
    }

    for (job = block_job_next(NULL); job; job = block_job_next(job)) {
        GSList *el;

        xdbg_graph_add_node(gr, job, X_DBG_BLOCK_GRAPH_NODE_TYPE_BLOCK_JOB,
                           job->job.id);
        for (el = job->nodes; el; el = el->next) {
            xdbg_graph_add_edge(gr, job, (BdrvChild *)el->data);
        }
    }

    QTAILQ_FOREACH(bs, &graph_bdrv_states, node_list) {
        xdbg_graph_add_node(gr, bs, X_DBG_BLOCK_GRAPH_NODE_TYPE_BLOCK_DRIVER,
                           bs->node_name);
        QLIST_FOREACH(child, &bs->children, next) {
            xdbg_graph_add_edge(gr, bs, child);
        }
    }

    return xdbg_graph_finalize(gr);
}

BlockDriverState *bdrv_lookup_bs(const char *device,
                                 const char *node_name,
                                 Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;

    if (device) {
        blk = blk_by_name(device);

        if (blk) {
            bs = blk_bs(blk);
            if (!bs) {
                error_setg(errp, "Device '%s' has no medium", device);
            }

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
        top = backing_bs(top);
    }

    return top != NULL;
}

BlockDriverState *bdrv_next_node(BlockDriverState *bs)
{
    if (!bs) {
        return QTAILQ_FIRST(&graph_bdrv_states);
    }
    return QTAILQ_NEXT(bs, node_list);
}

BlockDriverState *bdrv_next_all_states(BlockDriverState *bs)
{
    if (!bs) {
        return QTAILQ_FIRST(&all_bdrv_states);
    }
    return QTAILQ_NEXT(bs, bs_list);
}

const char *bdrv_get_node_name(const BlockDriverState *bs)
{
    return bs->node_name;
}

const char *bdrv_get_parent_name(const BlockDriverState *bs)
{
    BdrvChild *c;
    const char *name;

    /* If multiple parents have a name, just pick the first one. */
    QLIST_FOREACH(c, &bs->parents, next_parent) {
        if (c->role->get_name) {
            name = c->role->get_name(c);
            if (name && *name) {
                return name;
            }
        }
    }

    return NULL;
}

/* TODO check what callers really want: bs->node_name or blk_name() */
const char *bdrv_get_device_name(const BlockDriverState *bs)
{
    return bdrv_get_parent_name(bs) ?: "";
}

/* This can be used to identify nodes that might not have a device
 * name associated. Since node and device names live in the same
 * namespace, the result is unambiguous. The exception is if both are
 * absent, then this returns an empty (non-null) string. */
const char *bdrv_get_device_or_node_name(const BlockDriverState *bs)
{
    return bdrv_get_parent_name(bs) ?: bs->node_name;
}

int bdrv_get_flags(BlockDriverState *bs)
{
    return bs->open_flags;
}

int bdrv_has_zero_init_1(BlockDriverState *bs)
{
    return 1;
}

int bdrv_has_zero_init(BlockDriverState *bs)
{
    if (!bs->drv) {
        return 0;
    }

    /* If BS is a copy on write image, it is initialized to
       the contents of the base image, which may not be zeroes.  */
    if (bs->backing) {
        return 0;
    }
    if (bs->drv->bdrv_has_zero_init) {
        return bs->drv->bdrv_has_zero_init(bs);
    }
    if (bs->file && bs->drv->is_filter) {
        return bdrv_has_zero_init(bs->file->bs);
    }

    /* safe default */
    return 0;
}

int bdrv_has_zero_init_truncate(BlockDriverState *bs)
{
    if (!bs->drv) {
        return 0;
    }

    if (bs->backing) {
        /* Depends on the backing image length, but better safe than sorry */
        return 0;
    }
    if (bs->drv->bdrv_has_zero_init_truncate) {
        return bs->drv->bdrv_has_zero_init_truncate(bs);
    }
    if (bs->file && bs->drv->is_filter) {
        return bdrv_has_zero_init_truncate(bs->file->bs);
    }

    /* safe default */
    return 0;
}

bool bdrv_unallocated_blocks_are_zero(BlockDriverState *bs)
{
    BlockDriverInfo bdi;

    if (bs->backing) {
        return false;
    }

    if (bdrv_get_info(bs, &bdi) == 0) {
        return bdi.unallocated_blocks_are_zero;
    }

    return false;
}

bool bdrv_can_write_zeroes_with_unmap(BlockDriverState *bs)
{
    if (!(bs->open_flags & BDRV_O_UNMAP)) {
        return false;
    }

    return bs->supported_zero_flags & BDRV_REQ_MAY_UNMAP;
}

void bdrv_get_backing_filename(BlockDriverState *bs,
                               char *filename, int filename_size)
{
    pstrcpy(filename, filename_size, bs->backing_file);
}

int bdrv_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BlockDriver *drv = bs->drv;
    /* if bs->drv == NULL, bs is closed, so there's nothing to do here */
    if (!drv) {
        return -ENOMEDIUM;
    }
    if (!drv->bdrv_get_info) {
        if (bs->file && drv->is_filter) {
            return bdrv_get_info(bs->file->bs, bdi);
        }
        return -ENOTSUP;
    }
    memset(bdi, 0, sizeof(*bdi));
    return drv->bdrv_get_info(bs, bdi);
}

ImageInfoSpecific *bdrv_get_specific_info(BlockDriverState *bs,
                                          Error **errp)
{
    BlockDriver *drv = bs->drv;
    if (drv && drv->bdrv_get_specific_info) {
        return drv->bdrv_get_specific_info(bs, errp);
    }
    return NULL;
}

BlockStatsSpecific *bdrv_get_specific_stats(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    if (!drv || !drv->bdrv_get_specific_stats) {
        return NULL;
    }
    return drv->bdrv_get_specific_stats(bs);
}

void bdrv_debug_event(BlockDriverState *bs, BlkdebugEvent event)
{
    if (!bs || !bs->drv || !bs->drv->bdrv_debug_event) {
        return;
    }

    bs->drv->bdrv_debug_event(bs, event);
}

static BlockDriverState *bdrv_find_debug_node(BlockDriverState *bs)
{
    while (bs && bs->drv && !bs->drv->bdrv_debug_breakpoint) {
        if (bs->file) {
            bs = bs->file->bs;
            continue;
        }

        if (bs->drv->is_filter && bs->backing) {
            bs = bs->backing->bs;
            continue;
        }

        break;
    }

    if (bs && bs->drv && bs->drv->bdrv_debug_breakpoint) {
        assert(bs->drv->bdrv_debug_remove_breakpoint);
        return bs;
    }

    return NULL;
}

int bdrv_debug_breakpoint(BlockDriverState *bs, const char *event,
                          const char *tag)
{
    bs = bdrv_find_debug_node(bs);
    if (bs) {
        return bs->drv->bdrv_debug_breakpoint(bs, event, tag);
    }

    return -ENOTSUP;
}

int bdrv_debug_remove_breakpoint(BlockDriverState *bs, const char *tag)
{
    bs = bdrv_find_debug_node(bs);
    if (bs) {
        return bs->drv->bdrv_debug_remove_breakpoint(bs, tag);
    }

    return -ENOTSUP;
}

int bdrv_debug_resume(BlockDriverState *bs, const char *tag)
{
    while (bs && (!bs->drv || !bs->drv->bdrv_debug_resume)) {
        bs = bs->file ? bs->file->bs : NULL;
    }

    if (bs && bs->drv && bs->drv->bdrv_debug_resume) {
        return bs->drv->bdrv_debug_resume(bs, tag);
    }

    return -ENOTSUP;
}

bool bdrv_debug_is_suspended(BlockDriverState *bs, const char *tag)
{
    while (bs && bs->drv && !bs->drv->bdrv_debug_is_suspended) {
        bs = bs->file ? bs->file->bs : NULL;
    }

    if (bs && bs->drv && bs->drv->bdrv_debug_is_suspended) {
        return bs->drv->bdrv_debug_is_suspended(bs, tag);
    }

    return false;
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

    is_protocol = path_has_protocol(backing_file);

    for (curr_bs = bs; curr_bs->backing; curr_bs = curr_bs->backing->bs) {

        /* If either of the filename paths is actually a protocol, then
         * compare unmodified paths; otherwise make paths relative */
        if (is_protocol || path_has_protocol(curr_bs->backing_file)) {
            char *backing_file_full_ret;

            if (strcmp(backing_file, curr_bs->backing_file) == 0) {
                retval = curr_bs->backing->bs;
                break;
            }
            /* Also check against the full backing filename for the image */
            backing_file_full_ret = bdrv_get_full_backing_filename(curr_bs,
                                                                   NULL);
            if (backing_file_full_ret) {
                bool equal = strcmp(backing_file, backing_file_full_ret) == 0;
                g_free(backing_file_full_ret);
                if (equal) {
                    retval = curr_bs->backing->bs;
                    break;
                }
            }
        } else {
            /* If not an absolute filename path, make it relative to the current
             * image's filename path */
            filename_tmp = bdrv_make_absolute_filename(curr_bs, backing_file,
                                                       NULL);
            /* We are going to compare canonicalized absolute pathnames */
            if (!filename_tmp || !realpath(filename_tmp, filename_full)) {
                g_free(filename_tmp);
                continue;
            }
            g_free(filename_tmp);

            /* We need to make sure the backing filename we are comparing against
             * is relative to the current image filename (or absolute) */
            filename_tmp = bdrv_get_full_backing_filename(curr_bs, NULL);
            if (!filename_tmp || !realpath(filename_tmp, backing_file_full)) {
                g_free(filename_tmp);
                continue;
            }
            g_free(filename_tmp);

            if (strcmp(backing_file_full, filename_full) == 0) {
                retval = curr_bs->backing->bs;
                break;
            }
        }
    }

    g_free(filename_full);
    g_free(backing_file_full);
    return retval;
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

static void coroutine_fn bdrv_co_invalidate_cache(BlockDriverState *bs,
                                                  Error **errp)
{
    BdrvChild *child, *parent;
    uint64_t perm, shared_perm;
    Error *local_err = NULL;
    int ret;
    BdrvDirtyBitmap *bm;

    if (!bs->drv)  {
        return;
    }

    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_co_invalidate_cache(child->bs, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /*
     * Update permissions, they may differ for inactive nodes.
     *
     * Note that the required permissions of inactive images are always a
     * subset of the permissions required after activating the image. This
     * allows us to just get the permissions upfront without restricting
     * drv->bdrv_invalidate_cache().
     *
     * It also means that in error cases, we don't have to try and revert to
     * the old permissions (which is an operation that could fail, too). We can
     * just keep the extended permissions for the next time that an activation
     * of the image is tried.
     */
    if (bs->open_flags & BDRV_O_INACTIVE) {
        bs->open_flags &= ~BDRV_O_INACTIVE;
        bdrv_get_cumulative_perm(bs, &perm, &shared_perm);
        ret = bdrv_check_perm(bs, NULL, perm, shared_perm, NULL, NULL, &local_err);
        if (ret < 0) {
            bs->open_flags |= BDRV_O_INACTIVE;
            error_propagate(errp, local_err);
            return;
        }
        bdrv_set_perm(bs, perm, shared_perm);

        if (bs->drv->bdrv_co_invalidate_cache) {
            bs->drv->bdrv_co_invalidate_cache(bs, &local_err);
            if (local_err) {
                bs->open_flags |= BDRV_O_INACTIVE;
                error_propagate(errp, local_err);
                return;
            }
        }

        FOR_EACH_DIRTY_BITMAP(bs, bm) {
            bdrv_dirty_bitmap_skip_store(bm, false);
        }

        ret = refresh_total_sectors(bs, bs->total_sectors);
        if (ret < 0) {
            bs->open_flags |= BDRV_O_INACTIVE;
            error_setg_errno(errp, -ret, "Could not refresh total sector count");
            return;
        }
    }

    QLIST_FOREACH(parent, &bs->parents, next_parent) {
        if (parent->role->activate) {
            parent->role->activate(parent, &local_err);
            if (local_err) {
                bs->open_flags |= BDRV_O_INACTIVE;
                error_propagate(errp, local_err);
                return;
            }
        }
    }
}

typedef struct InvalidateCacheCo {
    BlockDriverState *bs;
    Error **errp;
    bool done;
} InvalidateCacheCo;

static void coroutine_fn bdrv_invalidate_cache_co_entry(void *opaque)
{
    InvalidateCacheCo *ico = opaque;
    bdrv_co_invalidate_cache(ico->bs, ico->errp);
    ico->done = true;
    aio_wait_kick();
}

void bdrv_invalidate_cache(BlockDriverState *bs, Error **errp)
{
    Coroutine *co;
    InvalidateCacheCo ico = {
        .bs = bs,
        .done = false,
        .errp = errp
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        bdrv_invalidate_cache_co_entry(&ico);
    } else {
        co = qemu_coroutine_create(bdrv_invalidate_cache_co_entry, &ico);
        bdrv_coroutine_enter(bs, co);
        BDRV_POLL_WHILE(bs, !ico.done);
    }
}

void bdrv_invalidate_cache_all(Error **errp)
{
    BlockDriverState *bs;
    Error *local_err = NULL;
    BdrvNextIterator it;

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        aio_context_acquire(aio_context);
        bdrv_invalidate_cache(bs, &local_err);
        aio_context_release(aio_context);
        if (local_err) {
            error_propagate(errp, local_err);
            bdrv_next_cleanup(&it);
            return;
        }
    }
}

static bool bdrv_has_bds_parent(BlockDriverState *bs, bool only_active)
{
    BdrvChild *parent;

    QLIST_FOREACH(parent, &bs->parents, next_parent) {
        if (parent->role->parent_is_bds) {
            BlockDriverState *parent_bs = parent->opaque;
            if (!only_active || !(parent_bs->open_flags & BDRV_O_INACTIVE)) {
                return true;
            }
        }
    }

    return false;
}

static int bdrv_inactivate_recurse(BlockDriverState *bs)
{
    BdrvChild *child, *parent;
    bool tighten_restrictions;
    uint64_t perm, shared_perm;
    int ret;

    if (!bs->drv) {
        return -ENOMEDIUM;
    }

    /* Make sure that we don't inactivate a child before its parent.
     * It will be covered by recursion from the yet active parent. */
    if (bdrv_has_bds_parent(bs, true)) {
        return 0;
    }

    assert(!(bs->open_flags & BDRV_O_INACTIVE));

    /* Inactivate this node */
    if (bs->drv->bdrv_inactivate) {
        ret = bs->drv->bdrv_inactivate(bs);
        if (ret < 0) {
            return ret;
        }
    }

    QLIST_FOREACH(parent, &bs->parents, next_parent) {
        if (parent->role->inactivate) {
            ret = parent->role->inactivate(parent);
            if (ret < 0) {
                return ret;
            }
        }
    }

    bs->open_flags |= BDRV_O_INACTIVE;

    /* Update permissions, they may differ for inactive nodes */
    bdrv_get_cumulative_perm(bs, &perm, &shared_perm);
    ret = bdrv_check_perm(bs, NULL, perm, shared_perm, NULL,
                          &tighten_restrictions, NULL);
    assert(tighten_restrictions == false);
    if (ret < 0) {
        /* We only tried to loosen restrictions, so errors are not fatal */
        bdrv_abort_perm_update(bs);
    } else {
        bdrv_set_perm(bs, perm, shared_perm);
    }


    /* Recursively inactivate children */
    QLIST_FOREACH(child, &bs->children, next) {
        ret = bdrv_inactivate_recurse(child->bs);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

int bdrv_inactivate_all(void)
{
    BlockDriverState *bs = NULL;
    BdrvNextIterator it;
    int ret = 0;
    GSList *aio_ctxs = NULL, *ctx;

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        AioContext *aio_context = bdrv_get_aio_context(bs);

        if (!g_slist_find(aio_ctxs, aio_context)) {
            aio_ctxs = g_slist_prepend(aio_ctxs, aio_context);
            aio_context_acquire(aio_context);
        }
    }

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        /* Nodes with BDS parents are covered by recursion from the last
         * parent that gets inactivated. Don't inactivate them a second
         * time if that has already happened. */
        if (bdrv_has_bds_parent(bs, false)) {
            continue;
        }
        ret = bdrv_inactivate_recurse(bs);
        if (ret < 0) {
            bdrv_next_cleanup(&it);
            goto out;
        }
    }

out:
    for (ctx = aio_ctxs; ctx != NULL; ctx = ctx->next) {
        AioContext *aio_context = ctx->data;
        aio_context_release(aio_context);
    }
    g_slist_free(aio_ctxs);

    return ret;
}

/**************************************************************/
/* removable device support */

/**
 * Return TRUE if the media is present
 */
bool bdrv_is_inserted(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    BdrvChild *child;

    if (!drv) {
        return false;
    }
    if (drv->bdrv_is_inserted) {
        return drv->bdrv_is_inserted(bs);
    }
    QLIST_FOREACH(child, &bs->children, next) {
        if (!bdrv_is_inserted(child->bs)) {
            return false;
        }
    }
    return true;
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
    if (!bs) {
        return;
    }
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
        error_propagate_prepend(errp, error_copy(blocker->reason),
                                "Node '%s' is busy: ",
                                bdrv_get_device_or_node_name(bs));
        return true;
    }
    return false;
}

void bdrv_op_block(BlockDriverState *bs, BlockOpType op, Error *reason)
{
    BdrvOpBlocker *blocker;
    assert((int) op >= 0 && op < BLOCK_OP_TYPE_MAX);

    blocker = g_new0(BdrvOpBlocker, 1);
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

void bdrv_img_create(const char *filename, const char *fmt,
                     const char *base_filename, const char *base_fmt,
                     char *options, uint64_t img_size, int flags, bool quiet,
                     Error **errp)
{
    QemuOptsList *create_opts = NULL;
    QemuOpts *opts = NULL;
    const char *backing_fmt, *backing_file;
    int64_t size;
    BlockDriver *drv, *proto_drv;
    Error *local_err = NULL;
    int ret = 0;

    /* Find driver and parse its options */
    drv = bdrv_find_format(fmt);
    if (!drv) {
        error_setg(errp, "Unknown file format '%s'", fmt);
        return;
    }

    proto_drv = bdrv_find_protocol(filename, true, errp);
    if (!proto_drv) {
        return;
    }

    if (!drv->create_opts) {
        error_setg(errp, "Format driver '%s' does not support image creation",
                   drv->format_name);
        return;
    }

    if (!proto_drv->create_opts) {
        error_setg(errp, "Protocol driver '%s' does not support image creation",
                   proto_drv->format_name);
        return;
    }

    /* Create parameter list */
    create_opts = qemu_opts_append(create_opts, drv->create_opts);
    create_opts = qemu_opts_append(create_opts, proto_drv->create_opts);

    opts = qemu_opts_create(create_opts, NULL, 0, &error_abort);

    /* Parse -o options */
    if (options) {
        qemu_opts_do_parse(opts, options, NULL, &local_err);
        if (local_err) {
            goto out;
        }
    }

    if (!qemu_opt_get(opts, BLOCK_OPT_SIZE)) {
        qemu_opt_set_number(opts, BLOCK_OPT_SIZE, img_size, &error_abort);
    } else if (img_size != UINT64_C(-1)) {
        error_setg(errp, "The image size must be specified only once");
        goto out;
    }

    if (base_filename) {
        qemu_opt_set(opts, BLOCK_OPT_BACKING_FILE, base_filename, &local_err);
        if (local_err) {
            error_setg(errp, "Backing file not supported for file format '%s'",
                       fmt);
            goto out;
        }
    }

    if (base_fmt) {
        qemu_opt_set(opts, BLOCK_OPT_BACKING_FMT, base_fmt, &local_err);
        if (local_err) {
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

    /* The size for the image must always be specified, unless we have a backing
     * file and we have not been forbidden from opening it. */
    size = qemu_opt_get_size(opts, BLOCK_OPT_SIZE, img_size);
    if (backing_file && !(flags & BDRV_O_NO_BACKING)) {
        BlockDriverState *bs;
        char *full_backing;
        int back_flags;
        QDict *backing_options = NULL;

        full_backing =
            bdrv_get_full_backing_filename_from_filename(filename, backing_file,
                                                         &local_err);
        if (local_err) {
            goto out;
        }
        assert(full_backing);

        /* backing files always opened read-only */
        back_flags = flags;
        back_flags &= ~(BDRV_O_RDWR | BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING);

        backing_options = qdict_new();
        if (backing_fmt) {
            qdict_put_str(backing_options, "driver", backing_fmt);
        }
        qdict_put_bool(backing_options, BDRV_OPT_FORCE_SHARE, true);

        bs = bdrv_open(full_backing, NULL, backing_options, back_flags,
                       &local_err);
        g_free(full_backing);
        if (!bs && size != -1) {
            /* Couldn't open BS, but we have a size, so it's nonfatal */
            warn_reportf_err(local_err,
                            "Could not verify backing image. "
                            "This may become an error in future versions.\n");
            local_err = NULL;
        } else if (!bs) {
            /* Couldn't open bs, do not have size */
            error_append_hint(&local_err,
                              "Could not open backing image to determine size.\n");
            goto out;
        } else {
            if (size == -1) {
                /* Opened BS, have no size */
                size = bdrv_getlength(bs);
                if (size < 0) {
                    error_setg_errno(errp, -size, "Could not get size of '%s'",
                                     backing_file);
                    bdrv_unref(bs);
                    goto out;
                }
                qemu_opt_set_number(opts, BLOCK_OPT_SIZE, size, &error_abort);
            }
            bdrv_unref(bs);
        }
    } /* (backing_file && !(flags & BDRV_O_NO_BACKING)) */

    if (size == -1) {
        error_setg(errp, "Image creation needs a size parameter");
        goto out;
    }

    if (!quiet) {
        printf("Formatting '%s', fmt=%s ", filename, fmt);
        qemu_opts_print(opts, " ");
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
    error_propagate(errp, local_err);
}

AioContext *bdrv_get_aio_context(BlockDriverState *bs)
{
    return bs ? bs->aio_context : qemu_get_aio_context();
}

void bdrv_coroutine_enter(BlockDriverState *bs, Coroutine *co)
{
    aio_co_enter(bdrv_get_aio_context(bs), co);
}

static void bdrv_do_remove_aio_context_notifier(BdrvAioNotifier *ban)
{
    QLIST_REMOVE(ban, list);
    g_free(ban);
}

static void bdrv_detach_aio_context(BlockDriverState *bs)
{
    BdrvAioNotifier *baf, *baf_tmp;

    assert(!bs->walking_aio_notifiers);
    bs->walking_aio_notifiers = true;
    QLIST_FOREACH_SAFE(baf, &bs->aio_notifiers, list, baf_tmp) {
        if (baf->deleted) {
            bdrv_do_remove_aio_context_notifier(baf);
        } else {
            baf->detach_aio_context(baf->opaque);
        }
    }
    /* Never mind iterating again to check for ->deleted.  bdrv_close() will
     * remove remaining aio notifiers if we aren't called again.
     */
    bs->walking_aio_notifiers = false;

    if (bs->drv && bs->drv->bdrv_detach_aio_context) {
        bs->drv->bdrv_detach_aio_context(bs);
    }

    if (bs->quiesce_counter) {
        aio_enable_external(bs->aio_context);
    }
    bs->aio_context = NULL;
}

static void bdrv_attach_aio_context(BlockDriverState *bs,
                                    AioContext *new_context)
{
    BdrvAioNotifier *ban, *ban_tmp;

    if (bs->quiesce_counter) {
        aio_disable_external(new_context);
    }

    bs->aio_context = new_context;

    if (bs->drv && bs->drv->bdrv_attach_aio_context) {
        bs->drv->bdrv_attach_aio_context(bs, new_context);
    }

    assert(!bs->walking_aio_notifiers);
    bs->walking_aio_notifiers = true;
    QLIST_FOREACH_SAFE(ban, &bs->aio_notifiers, list, ban_tmp) {
        if (ban->deleted) {
            bdrv_do_remove_aio_context_notifier(ban);
        } else {
            ban->attached_aio_context(new_context, ban->opaque);
        }
    }
    bs->walking_aio_notifiers = false;
}

/*
 * Changes the AioContext used for fd handlers, timers, and BHs by this
 * BlockDriverState and all its children and parents.
 *
 * Must be called from the main AioContext.
 *
 * The caller must own the AioContext lock for the old AioContext of bs, but it
 * must not own the AioContext lock for new_context (unless new_context is the
 * same as the current context of bs).
 *
 * @ignore will accumulate all visited BdrvChild object. The caller is
 * responsible for freeing the list afterwards.
 */
void bdrv_set_aio_context_ignore(BlockDriverState *bs,
                                 AioContext *new_context, GSList **ignore)
{
    AioContext *old_context = bdrv_get_aio_context(bs);
    BdrvChild *child;

    g_assert(qemu_get_current_aio_context() == qemu_get_aio_context());

    if (old_context == new_context) {
        return;
    }

    bdrv_drained_begin(bs);

    QLIST_FOREACH(child, &bs->children, next) {
        if (g_slist_find(*ignore, child)) {
            continue;
        }
        *ignore = g_slist_prepend(*ignore, child);
        bdrv_set_aio_context_ignore(child->bs, new_context, ignore);
    }
    QLIST_FOREACH(child, &bs->parents, next_parent) {
        if (g_slist_find(*ignore, child)) {
            continue;
        }
        assert(child->role->set_aio_ctx);
        *ignore = g_slist_prepend(*ignore, child);
        child->role->set_aio_ctx(child, new_context, ignore);
    }

    bdrv_detach_aio_context(bs);

    /* Acquire the new context, if necessary */
    if (qemu_get_aio_context() != new_context) {
        aio_context_acquire(new_context);
    }

    bdrv_attach_aio_context(bs, new_context);

    /*
     * If this function was recursively called from
     * bdrv_set_aio_context_ignore(), there may be nodes in the
     * subtree that have not yet been moved to the new AioContext.
     * Release the old one so bdrv_drained_end() can poll them.
     */
    if (qemu_get_aio_context() != old_context) {
        aio_context_release(old_context);
    }

    bdrv_drained_end(bs);

    if (qemu_get_aio_context() != old_context) {
        aio_context_acquire(old_context);
    }
    if (qemu_get_aio_context() != new_context) {
        aio_context_release(new_context);
    }
}

static bool bdrv_parent_can_set_aio_context(BdrvChild *c, AioContext *ctx,
                                            GSList **ignore, Error **errp)
{
    if (g_slist_find(*ignore, c)) {
        return true;
    }
    *ignore = g_slist_prepend(*ignore, c);

    /* A BdrvChildRole that doesn't handle AioContext changes cannot
     * tolerate any AioContext changes */
    if (!c->role->can_set_aio_ctx) {
        char *user = bdrv_child_user_desc(c);
        error_setg(errp, "Changing iothreads is not supported by %s", user);
        g_free(user);
        return false;
    }
    if (!c->role->can_set_aio_ctx(c, ctx, ignore, errp)) {
        assert(!errp || *errp);
        return false;
    }
    return true;
}

bool bdrv_child_can_set_aio_context(BdrvChild *c, AioContext *ctx,
                                    GSList **ignore, Error **errp)
{
    if (g_slist_find(*ignore, c)) {
        return true;
    }
    *ignore = g_slist_prepend(*ignore, c);
    return bdrv_can_set_aio_context(c->bs, ctx, ignore, errp);
}

/* @ignore will accumulate all visited BdrvChild object. The caller is
 * responsible for freeing the list afterwards. */
bool bdrv_can_set_aio_context(BlockDriverState *bs, AioContext *ctx,
                              GSList **ignore, Error **errp)
{
    BdrvChild *c;

    if (bdrv_get_aio_context(bs) == ctx) {
        return true;
    }

    QLIST_FOREACH(c, &bs->parents, next_parent) {
        if (!bdrv_parent_can_set_aio_context(c, ctx, ignore, errp)) {
            return false;
        }
    }
    QLIST_FOREACH(c, &bs->children, next) {
        if (!bdrv_child_can_set_aio_context(c, ctx, ignore, errp)) {
            return false;
        }
    }

    return true;
}

int bdrv_child_try_set_aio_context(BlockDriverState *bs, AioContext *ctx,
                                   BdrvChild *ignore_child, Error **errp)
{
    GSList *ignore;
    bool ret;

    ignore = ignore_child ? g_slist_prepend(NULL, ignore_child) : NULL;
    ret = bdrv_can_set_aio_context(bs, ctx, &ignore, errp);
    g_slist_free(ignore);

    if (!ret) {
        return -EPERM;
    }

    ignore = ignore_child ? g_slist_prepend(NULL, ignore_child) : NULL;
    bdrv_set_aio_context_ignore(bs, ctx, &ignore);
    g_slist_free(ignore);

    return 0;
}

int bdrv_try_set_aio_context(BlockDriverState *bs, AioContext *ctx,
                             Error **errp)
{
    return bdrv_child_try_set_aio_context(bs, ctx, NULL, errp);
}

void bdrv_add_aio_context_notifier(BlockDriverState *bs,
        void (*attached_aio_context)(AioContext *new_context, void *opaque),
        void (*detach_aio_context)(void *opaque), void *opaque)
{
    BdrvAioNotifier *ban = g_new(BdrvAioNotifier, 1);
    *ban = (BdrvAioNotifier){
        .attached_aio_context = attached_aio_context,
        .detach_aio_context   = detach_aio_context,
        .opaque               = opaque
    };

    QLIST_INSERT_HEAD(&bs->aio_notifiers, ban, list);
}

void bdrv_remove_aio_context_notifier(BlockDriverState *bs,
                                      void (*attached_aio_context)(AioContext *,
                                                                   void *),
                                      void (*detach_aio_context)(void *),
                                      void *opaque)
{
    BdrvAioNotifier *ban, *ban_next;

    QLIST_FOREACH_SAFE(ban, &bs->aio_notifiers, list, ban_next) {
        if (ban->attached_aio_context == attached_aio_context &&
            ban->detach_aio_context   == detach_aio_context   &&
            ban->opaque               == opaque               &&
            ban->deleted              == false)
        {
            if (bs->walking_aio_notifiers) {
                ban->deleted = true;
            } else {
                bdrv_do_remove_aio_context_notifier(ban);
            }
            return;
        }
    }

    abort();
}

int bdrv_amend_options(BlockDriverState *bs, QemuOpts *opts,
                       BlockDriverAmendStatusCB *status_cb, void *cb_opaque,
                       Error **errp)
{
    if (!bs->drv) {
        error_setg(errp, "Node is ejected");
        return -ENOMEDIUM;
    }
    if (!bs->drv->bdrv_amend_options) {
        error_setg(errp, "Block driver '%s' does not support option amendment",
                   bs->drv->format_name);
        return -ENOTSUP;
    }
    return bs->drv->bdrv_amend_options(bs, opts, status_cb, cb_opaque, errp);
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
    BdrvNextIterator it;

    /* walk down the bs forest recursively */
    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        bool perm;

        /* try to recurse in this top level bs */
        perm = bdrv_recurse_is_first_non_filter(bs, candidate);

        /* candidate is the first non filter */
        if (perm) {
            bdrv_next_cleanup(&it);
            return true;
        }
    }

    return false;
}

BlockDriverState *check_to_replace_node(BlockDriverState *parent_bs,
                                        const char *node_name, Error **errp)
{
    BlockDriverState *to_replace_bs = bdrv_find_node(node_name);
    AioContext *aio_context;

    if (!to_replace_bs) {
        error_setg(errp, "Node name '%s' not found", node_name);
        return NULL;
    }

    aio_context = bdrv_get_aio_context(to_replace_bs);
    aio_context_acquire(aio_context);

    if (bdrv_op_is_blocked(to_replace_bs, BLOCK_OP_TYPE_REPLACE, errp)) {
        to_replace_bs = NULL;
        goto out;
    }

    /* We don't want arbitrary node of the BDS chain to be replaced only the top
     * most non filter in order to prevent data corruption.
     * Another benefit is that this tests exclude backing files which are
     * blocked by the backing blockers.
     */
    if (!bdrv_recurse_is_first_non_filter(parent_bs, to_replace_bs)) {
        error_setg(errp, "Only top most non filter can be replaced");
        to_replace_bs = NULL;
        goto out;
    }

out:
    aio_context_release(aio_context);
    return to_replace_bs;
}

/**
 * Iterates through the list of runtime option keys that are said to
 * be "strong" for a BDS.  An option is called "strong" if it changes
 * a BDS's data.  For example, the null block driver's "size" and
 * "read-zeroes" options are strong, but its "latency-ns" option is
 * not.
 *
 * If a key returned by this function ends with a dot, all options
 * starting with that prefix are strong.
 */
static const char *const *strong_options(BlockDriverState *bs,
                                         const char *const *curopt)
{
    static const char *const global_options[] = {
        "driver", "filename", NULL
    };

    if (!curopt) {
        return &global_options[0];
    }

    curopt++;
    if (curopt == &global_options[ARRAY_SIZE(global_options) - 1] && bs->drv) {
        curopt = bs->drv->strong_runtime_opts;
    }

    return (curopt && *curopt) ? curopt : NULL;
}

/**
 * Copies all strong runtime options from bs->options to the given
 * QDict.  The set of strong option keys is determined by invoking
 * strong_options().
 *
 * Returns true iff any strong option was present in bs->options (and
 * thus copied to the target QDict) with the exception of "filename"
 * and "driver".  The caller is expected to use this value to decide
 * whether the existence of strong options prevents the generation of
 * a plain filename.
 */
static bool append_strong_runtime_options(QDict *d, BlockDriverState *bs)
{
    bool found_any = false;
    const char *const *option_name = NULL;

    if (!bs->drv) {
        return false;
    }

    while ((option_name = strong_options(bs, option_name))) {
        bool option_given = false;

        assert(strlen(*option_name) > 0);
        if ((*option_name)[strlen(*option_name) - 1] != '.') {
            QObject *entry = qdict_get(bs->options, *option_name);
            if (!entry) {
                continue;
            }

            qdict_put_obj(d, *option_name, qobject_ref(entry));
            option_given = true;
        } else {
            const QDictEntry *entry;
            for (entry = qdict_first(bs->options); entry;
                 entry = qdict_next(bs->options, entry))
            {
                if (strstart(qdict_entry_key(entry), *option_name, NULL)) {
                    qdict_put_obj(d, qdict_entry_key(entry),
                                  qobject_ref(qdict_entry_value(entry)));
                    option_given = true;
                }
            }
        }

        /* While "driver" and "filename" need to be included in a JSON filename,
         * their existence does not prohibit generation of a plain filename. */
        if (!found_any && option_given &&
            strcmp(*option_name, "driver") && strcmp(*option_name, "filename"))
        {
            found_any = true;
        }
    }

    if (!qdict_haskey(d, "driver")) {
        /* Drivers created with bdrv_new_open_driver() may not have a
         * @driver option.  Add it here. */
        qdict_put_str(d, "driver", bs->drv->format_name);
    }

    return found_any;
}

/* Note: This function may return false positives; it may return true
 * even if opening the backing file specified by bs's image header
 * would result in exactly bs->backing. */
static bool bdrv_backing_overridden(BlockDriverState *bs)
{
    if (bs->backing) {
        return strcmp(bs->auto_backing_file,
                      bs->backing->bs->filename);
    } else {
        /* No backing BDS, so if the image header reports any backing
         * file, it must have been suppressed */
        return bs->auto_backing_file[0] != '\0';
    }
}

/* Updates the following BDS fields:
 *  - exact_filename: A filename which may be used for opening a block device
 *                    which (mostly) equals the given BDS (even without any
 *                    other options; so reading and writing must return the same
 *                    results, but caching etc. may be different)
 *  - full_open_options: Options which, when given when opening a block device
 *                       (without a filename), result in a BDS (mostly)
 *                       equalling the given one
 *  - filename: If exact_filename is set, it is copied here. Otherwise,
 *              full_open_options is converted to a JSON object, prefixed with
 *              "json:" (for use through the JSON pseudo protocol) and put here.
 */
void bdrv_refresh_filename(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    BdrvChild *child;
    QDict *opts;
    bool backing_overridden;
    bool generate_json_filename; /* Whether our default implementation should
                                    fill exact_filename (false) or not (true) */

    if (!drv) {
        return;
    }

    /* This BDS's file name may depend on any of its children's file names, so
     * refresh those first */
    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_refresh_filename(child->bs);
    }

    if (bs->implicit) {
        /* For implicit nodes, just copy everything from the single child */
        child = QLIST_FIRST(&bs->children);
        assert(QLIST_NEXT(child, next) == NULL);

        pstrcpy(bs->exact_filename, sizeof(bs->exact_filename),
                child->bs->exact_filename);
        pstrcpy(bs->filename, sizeof(bs->filename), child->bs->filename);

        bs->full_open_options = qobject_ref(child->bs->full_open_options);

        return;
    }

    backing_overridden = bdrv_backing_overridden(bs);

    if (bs->open_flags & BDRV_O_NO_IO) {
        /* Without I/O, the backing file does not change anything.
         * Therefore, in such a case (primarily qemu-img), we can
         * pretend the backing file has not been overridden even if
         * it technically has been. */
        backing_overridden = false;
    }

    /* Gather the options QDict */
    opts = qdict_new();
    generate_json_filename = append_strong_runtime_options(opts, bs);
    generate_json_filename |= backing_overridden;

    if (drv->bdrv_gather_child_options) {
        /* Some block drivers may not want to present all of their children's
         * options, or name them differently from BdrvChild.name */
        drv->bdrv_gather_child_options(bs, opts, backing_overridden);
    } else {
        QLIST_FOREACH(child, &bs->children, next) {
            if (child->role == &child_backing && !backing_overridden) {
                /* We can skip the backing BDS if it has not been overridden */
                continue;
            }

            qdict_put(opts, child->name,
                      qobject_ref(child->bs->full_open_options));
        }

        if (backing_overridden && !bs->backing) {
            /* Force no backing file */
            qdict_put_null(opts, "backing");
        }
    }

    qobject_unref(bs->full_open_options);
    bs->full_open_options = opts;

    if (drv->bdrv_refresh_filename) {
        /* Obsolete information is of no use here, so drop the old file name
         * information before refreshing it */
        bs->exact_filename[0] = '\0';

        drv->bdrv_refresh_filename(bs);
    } else if (bs->file) {
        /* Try to reconstruct valid information from the underlying file */

        bs->exact_filename[0] = '\0';

        /*
         * We can use the underlying file's filename if:
         * - it has a filename,
         * - the file is a protocol BDS, and
         * - opening that file (as this BDS's format) will automatically create
         *   the BDS tree we have right now, that is:
         *   - the user did not significantly change this BDS's behavior with
         *     some explicit (strong) options
         *   - no non-file child of this BDS has been overridden by the user
         *   Both of these conditions are represented by generate_json_filename.
         */
        if (bs->file->bs->exact_filename[0] &&
            bs->file->bs->drv->bdrv_file_open &&
            !generate_json_filename)
        {
            strcpy(bs->exact_filename, bs->file->bs->exact_filename);
        }
    }

    if (bs->exact_filename[0]) {
        pstrcpy(bs->filename, sizeof(bs->filename), bs->exact_filename);
    } else {
        QString *json = qobject_to_json(QOBJECT(bs->full_open_options));
        snprintf(bs->filename, sizeof(bs->filename), "json:%s",
                 qstring_get_str(json));
        qobject_unref(json);
    }
}

char *bdrv_dirname(BlockDriverState *bs, Error **errp)
{
    BlockDriver *drv = bs->drv;

    if (!drv) {
        error_setg(errp, "Node '%s' is ejected", bs->node_name);
        return NULL;
    }

    if (drv->bdrv_dirname) {
        return drv->bdrv_dirname(bs, errp);
    }

    if (bs->file) {
        return bdrv_dirname(bs->file->bs, errp);
    }

    bdrv_refresh_filename(bs);
    if (bs->exact_filename[0] != '\0') {
        return path_combine(bs->exact_filename, "");
    }

    error_setg(errp, "Cannot generate a base directory for %s nodes",
               drv->format_name);
    return NULL;
}

/*
 * Hot add/remove a BDS's child. So the user can take a child offline when
 * it is broken and take a new child online
 */
void bdrv_add_child(BlockDriverState *parent_bs, BlockDriverState *child_bs,
                    Error **errp)
{

    if (!parent_bs->drv || !parent_bs->drv->bdrv_add_child) {
        error_setg(errp, "The node %s does not support adding a child",
                   bdrv_get_device_or_node_name(parent_bs));
        return;
    }

    if (!QLIST_EMPTY(&child_bs->parents)) {
        error_setg(errp, "The node %s already has a parent",
                   child_bs->node_name);
        return;
    }

    parent_bs->drv->bdrv_add_child(parent_bs, child_bs, errp);
}

void bdrv_del_child(BlockDriverState *parent_bs, BdrvChild *child, Error **errp)
{
    BdrvChild *tmp;

    if (!parent_bs->drv || !parent_bs->drv->bdrv_del_child) {
        error_setg(errp, "The node %s does not support removing a child",
                   bdrv_get_device_or_node_name(parent_bs));
        return;
    }

    QLIST_FOREACH(tmp, &parent_bs->children, next) {
        if (tmp == child) {
            break;
        }
    }

    if (!tmp) {
        error_setg(errp, "The node %s does not have a child named %s",
                   bdrv_get_device_or_node_name(parent_bs),
                   bdrv_get_device_or_node_name(child->bs));
        return;
    }

    parent_bs->drv->bdrv_del_child(parent_bs, child, errp);
}
