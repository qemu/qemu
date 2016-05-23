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
#include "trace.h"
#include "block/block_int.h"
#include "block/blockjob.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qjson.h"
#include "sysemu/block-backend.h"
#include "sysemu/sysemu.h"
#include "qemu/notify.h"
#include "qemu/coroutine.h"
#include "block/qapi.h"
#include "qmp-commands.h"
#include "qemu/timer.h"
#include "qapi-event.h"
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
        return MAX(4096, getpagesize());
    }

    return bs->bl.opt_mem_alignment;
}

size_t bdrv_min_mem_align(BlockDriverState *bs)
{
    if (!bs || !bs->drv) {
        /* page size or 4k (hdd sector size) should be on the safe side */
        return MAX(4096, getpagesize());
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

void bdrv_get_full_backing_filename_from_filename(const char *backed,
                                                  const char *backing,
                                                  char *dest, size_t sz,
                                                  Error **errp)
{
    if (backing[0] == '\0' || path_has_protocol(backing) ||
        path_is_absolute(backing))
    {
        pstrcpy(dest, sz, backing);
    } else if (backed[0] == '\0' || strstart(backed, "json:", NULL)) {
        error_setg(errp, "Cannot use relative backing file names for '%s'",
                   backed);
    } else {
        path_combine(dest, sz, backed, backing);
    }
}

void bdrv_get_full_backing_filename(BlockDriverState *bs, char *dest, size_t sz,
                                    Error **errp)
{
    char *backed = bs->exact_filename[0] ? bs->exact_filename : bs->filename;

    bdrv_get_full_backing_filename_from_filename(backed, bs->backing_file,
                                                 dest, sz, errp);
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
    bs->refcnt = 1;
    bs->aio_context = qemu_get_aio_context();

    QTAILQ_INSERT_TAIL(&all_bdrv_states, bs, bs_list);

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

    drv = bdrv_find_protocol(filename, true, errp);
    if (drv == NULL) {
        return -ENOENT;
    }

    ret = bdrv_create(drv, filename, opts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }
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

BlockDriver *bdrv_find_protocol(const char *filename,
                                bool allow_protocol_prefix,
                                Error **errp)
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
        return &bdrv_file;
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

    error_setg(errp, "Unknown protocol '%s'", protocol);
    return NULL;
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

static int find_image_format(BlockDriverState *bs, const char *filename,
                             BlockDriver **pdrv, Error **errp)
{
    BlockDriver *drv;
    uint8_t buf[BLOCK_PROBE_BUF_SIZE];
    int ret = 0;

    /* Return the raw BlockDriver * to scsi-generic devices or empty drives */
    if (bdrv_is_sg(bs) || !bdrv_is_inserted(bs) || bdrv_getlength(bs) == 0) {
        *pdrv = &bdrv_raw;
        return ret;
    }

    ret = bdrv_pread(bs, 0, buf, sizeof(buf));
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
static int refresh_total_sectors(BlockDriverState *bs, int64_t hint)
{
    BlockDriver *drv = bs->drv;

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

static void bdrv_child_cb_drained_begin(BdrvChild *child)
{
    BlockDriverState *bs = child->opaque;
    bdrv_drained_begin(bs);
}

static void bdrv_child_cb_drained_end(BdrvChild *child)
{
    BlockDriverState *bs = child->opaque;
    bdrv_drained_end(bs);
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

    /* Our block drivers take care to send flushes and respect unmap policy,
     * so we can default to enable both on lower layers regardless of the
     * corresponding parent options. */
    flags |= BDRV_O_UNMAP;

    /* Clear flags that only apply to the top layer */
    flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING | BDRV_O_COPY_ON_READ |
               BDRV_O_NO_IO);

    *child_flags = flags;
}

const BdrvChildRole child_file = {
    .inherit_options = bdrv_inherited_options,
    .drained_begin   = bdrv_child_cb_drained_begin,
    .drained_end     = bdrv_child_cb_drained_end,
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
    .inherit_options = bdrv_inherited_fmt_options,
    .drained_begin   = bdrv_child_cb_drained_begin,
    .drained_end     = bdrv_child_cb_drained_end,
};

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

    /* backing files always opened read-only */
    flags &= ~(BDRV_O_RDWR | BDRV_O_COPY_ON_READ);

    /* snapshot=on is handled on the top layer */
    flags &= ~(BDRV_O_SNAPSHOT | BDRV_O_TEMPORARY);

    *child_flags = flags;
}

static const BdrvChildRole child_backing = {
    .inherit_options = bdrv_backing_options,
    .drained_begin   = bdrv_child_cb_drained_begin,
    .drained_end     = bdrv_child_cb_drained_end,
};

static int bdrv_open_flags(BlockDriverState *bs, int flags)
{
    int open_flags = flags;

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

static void update_flags_from_options(int *flags, QemuOpts *opts)
{
    *flags &= ~BDRV_O_CACHE_MASK;

    assert(qemu_opt_find(opts, BDRV_OPT_CACHE_NO_FLUSH));
    if (qemu_opt_get_bool(opts, BDRV_OPT_CACHE_NO_FLUSH, false)) {
        *flags |= BDRV_O_NO_FLUSH;
    }

    assert(qemu_opt_find(opts, BDRV_OPT_CACHE_DIRECT));
    if (qemu_opt_get_bool(opts, BDRV_OPT_CACHE_DIRECT, false)) {
        *flags |= BDRV_O_NOCACHE;
    }
}

static void update_options_from_flags(QDict *options, int flags)
{
    if (!qdict_haskey(options, BDRV_OPT_CACHE_DIRECT)) {
        qdict_put(options, BDRV_OPT_CACHE_DIRECT,
                  qbool_from_bool(flags & BDRV_O_NOCACHE));
    }
    if (!qdict_haskey(options, BDRV_OPT_CACHE_NO_FLUSH)) {
        qdict_put(options, BDRV_OPT_CACHE_NO_FLUSH,
                  qbool_from_bool(flags & BDRV_O_NO_FLUSH));
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

    /* copy node name into the bs and insert it into the graph list */
    pstrcpy(bs->node_name, sizeof(bs->node_name), node_name);
    QTAILQ_INSERT_TAIL(&graph_bdrv_states, bs, node_list);
out:
    g_free(gen_node_name);
}

static QemuOptsList bdrv_runtime_opts = {
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
        { /* end of list */ }
    },
};

/*
 * Common part for opening disk images and files
 *
 * Removes all processed options from *options.
 */
static int bdrv_open_common(BlockDriverState *bs, BdrvChild *file,
                            QDict *options, Error **errp)
{
    int ret, open_flags;
    const char *filename;
    const char *driver_name = NULL;
    const char *node_name = NULL;
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

    driver_name = qemu_opt_get(opts, "driver");
    drv = bdrv_find_format(driver_name);
    assert(drv != NULL);

    if (file != NULL) {
        filename = file->bs->filename;
    } else {
        filename = qdict_get_try_str(options, "filename");
    }

    if (drv->bdrv_needs_filename && !filename) {
        error_setg(errp, "The '%s' block driver requires a file name",
                   drv->format_name);
        ret = -EINVAL;
        goto fail_opts;
    }

    trace_bdrv_open_common(bs, filename ?: "", bs->open_flags,
                           drv->format_name);

    node_name = qemu_opt_get(opts, "node-name");
    bdrv_assign_node_name(bs, node_name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail_opts;
    }

    bs->request_alignment = 512;
    bs->zero_beyond_eof = true;
    bs->read_only = !(bs->open_flags & BDRV_O_RDWR);

    if (use_bdrv_whitelist && !bdrv_is_whitelisted(drv, bs->read_only)) {
        error_setg(errp,
                   !bs->read_only && bdrv_is_whitelisted(drv, true)
                        ? "Driver '%s' can only be used for read-only devices"
                        : "Driver '%s' is not whitelisted",
                   drv->format_name);
        ret = -ENOTSUP;
        goto fail_opts;
    }

    assert(bs->copy_on_read == 0); /* bdrv_new() and bdrv_close() make it so */
    if (bs->open_flags & BDRV_O_COPY_ON_READ) {
        if (!bs->read_only) {
            bdrv_enable_copy_on_read(bs);
        } else {
            error_setg(errp, "Can't use copy-on-read on read-only device");
            ret = -EINVAL;
            goto fail_opts;
        }
    }

    if (filename != NULL) {
        pstrcpy(bs->filename, sizeof(bs->filename), filename);
    } else {
        bs->filename[0] = '\0';
    }
    pstrcpy(bs->exact_filename, sizeof(bs->exact_filename), bs->filename);

    bs->drv = drv;
    bs->opaque = g_malloc0(drv->instance_size);

    /* Apply cache mode options */
    update_flags_from_options(&bs->open_flags, opts);

    /* Open the image, either directly or using a protocol */
    open_flags = bdrv_open_flags(bs, bs->open_flags);
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
    assert(bdrv_min_mem_align(bs) != 0);
    assert((bs->request_alignment != 0) || bdrv_is_sg(bs));

    qemu_opts_del(opts);
    return 0;

free_and_fail:
    bs->file = NULL;
    g_free(bs->opaque);
    bs->opaque = NULL;
    bs->drv = NULL;
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
    QDECREF(json_options);
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

    if (!drvname && protocol) {
        if (filename) {
            drv = bdrv_find_protocol(filename, parse_filename, errp);
            if (!drv) {
                return -EINVAL;
            }

            drvname = drv->format_name;
            qdict_put(*options, "driver", qstring_from_str(drvname));
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

static void bdrv_replace_child(BdrvChild *child, BlockDriverState *new_bs)
{
    BlockDriverState *old_bs = child->bs;

    if (old_bs) {
        if (old_bs->quiesce_counter && child->role->drained_end) {
            child->role->drained_end(child);
        }
        QLIST_REMOVE(child, next_parent);
    }

    child->bs = new_bs;

    if (new_bs) {
        QLIST_INSERT_HEAD(&new_bs->parents, child, next_parent);
        if (new_bs->quiesce_counter && child->role->drained_begin) {
            child->role->drained_begin(child);
        }
    }
}

BdrvChild *bdrv_root_attach_child(BlockDriverState *child_bs,
                                  const char *child_name,
                                  const BdrvChildRole *child_role,
                                  void *opaque)
{
    BdrvChild *child = g_new(BdrvChild, 1);
    *child = (BdrvChild) {
        .bs     = NULL,
        .name   = g_strdup(child_name),
        .role   = child_role,
        .opaque = opaque,
    };

    bdrv_replace_child(child, child_bs);

    return child;
}

BdrvChild *bdrv_attach_child(BlockDriverState *parent_bs,
                             BlockDriverState *child_bs,
                             const char *child_name,
                             const BdrvChildRole *child_role)
{
    BdrvChild *child = bdrv_root_attach_child(child_bs, child_name, child_role,
                                              parent_bs);
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

void bdrv_unref_child(BlockDriverState *parent, BdrvChild *child)
{
    if (child == NULL) {
        return;
    }

    if (child->bs->inherits_from == parent) {
        child->bs->inherits_from = NULL;
    }

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

static void bdrv_parent_cb_resize(BlockDriverState *bs)
{
    BdrvChild *c;
    QLIST_FOREACH(c, &bs->parents, next_parent) {
        if (c->role->resize) {
            c->role->resize(c);
        }
    }
}

/*
 * Sets the backing file link of a BDS. A new reference is created; callers
 * which don't need their own reference any more must call bdrv_unref().
 */
void bdrv_set_backing_hd(BlockDriverState *bs, BlockDriverState *backing_hd)
{
    if (backing_hd) {
        bdrv_ref(backing_hd);
    }

    if (bs->backing) {
        assert(bs->backing_blocker);
        bdrv_op_unblock_all(bs->backing->bs, bs->backing_blocker);
        bdrv_unref_child(bs, bs->backing);
    } else if (backing_hd) {
        error_setg(&bs->backing_blocker,
                   "node is used as backing hd of '%s'",
                   bdrv_get_device_or_node_name(bs));
    }

    if (!backing_hd) {
        error_free(bs->backing_blocker);
        bs->backing_blocker = NULL;
        bs->backing = NULL;
        goto out;
    }
    bs->backing = bdrv_attach_child(bs, backing_hd, "backing", &child_backing);
    bs->open_flags &= ~BDRV_O_NO_BACKING;
    pstrcpy(bs->backing_file, sizeof(bs->backing_file), backing_hd->filename);
    pstrcpy(bs->backing_format, sizeof(bs->backing_format),
            backing_hd->drv ? backing_hd->drv->format_name : "");

    bdrv_op_block_all(backing_hd, bs->backing_blocker);
    /* Otherwise we won't be able to commit due to check in bdrv_commit */
    bdrv_op_unblock(backing_hd, BLOCK_OP_TYPE_COMMIT_TARGET,
                    bs->backing_blocker);
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
    char *backing_filename = g_malloc0(PATH_MAX);
    char *bdref_key_dot;
    const char *reference = NULL;
    int ret = 0;
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

    reference = qdict_get_try_str(parent_options, bdref_key);
    if (reference || qdict_haskey(options, "file.filename")) {
        backing_filename[0] = '\0';
    } else if (bs->backing_file[0] == '\0' && qdict_size(options) == 0) {
        QDECREF(options);
        goto free_exit;
    } else {
        bdrv_get_full_backing_filename(bs, backing_filename, PATH_MAX,
                                       &local_err);
        if (local_err) {
            ret = -EINVAL;
            error_propagate(errp, local_err);
            QDECREF(options);
            goto free_exit;
        }
    }

    if (!bs->drv || !bs->drv->supports_backing) {
        ret = -EINVAL;
        error_setg(errp, "Driver doesn't support backing files");
        QDECREF(options);
        goto free_exit;
    }

    if (bs->backing_format[0] != '\0' && !qdict_haskey(options, "driver")) {
        qdict_put(options, "driver", qstring_from_str(bs->backing_format));
    }

    backing_hd = bdrv_open_inherit(*backing_filename ? backing_filename : NULL,
                                   reference, options, 0, bs, &child_backing,
                                   errp);
    if (!backing_hd) {
        bs->open_flags |= BDRV_O_NO_BACKING;
        error_prepend(errp, "Could not open backing file: ");
        ret = -EINVAL;
        goto free_exit;
    }

    /* Hook up the backing file link; drop our reference, bs owns the
     * backing_hd reference now */
    bdrv_set_backing_hd(bs, backing_hd);
    bdrv_unref(backing_hd);

    qdict_del(parent_options, bdref_key);

free_exit:
    g_free(backing_filename);
    QDECREF(tmp_parent_options);
    return ret;
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
                           BlockDriverState* parent,
                           const BdrvChildRole *child_role,
                           bool allow_none, Error **errp)
{
    BdrvChild *c = NULL;
    BlockDriverState *bs;
    QDict *image_options;
    char *bdref_key_dot;
    const char *reference;

    assert(child_role != NULL);

    bdref_key_dot = g_strdup_printf("%s.", bdref_key);
    qdict_extract_subqdict(options, &image_options, bdref_key_dot);
    g_free(bdref_key_dot);

    reference = qdict_get_try_str(options, bdref_key);
    if (!filename && !reference && !qdict_size(image_options)) {
        if (!allow_none) {
            error_setg(errp, "A block device must be specified for \"%s\"",
                       bdref_key);
        }
        QDECREF(image_options);
        goto done;
    }

    bs = bdrv_open_inherit(filename, reference, image_options, 0,
                           parent, child_role, errp);
    if (!bs) {
        goto done;
    }

    c = bdrv_attach_child(parent, bs, bdref_key, child_role);

done:
    qdict_del(options, bdref_key);
    return c;
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
    BlockDriverState *bs_snapshot;
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
    qdict_put(snapshot_options, "file.driver",
              qstring_from_str("file"));
    qdict_put(snapshot_options, "file.filename",
              qstring_from_str(tmp_filename));
    qdict_put(snapshot_options, "driver",
              qstring_from_str("qcow2"));

    bs_snapshot = bdrv_open(NULL, NULL, snapshot_options, flags, errp);
    snapshot_options = NULL;
    if (!bs_snapshot) {
        ret = -EINVAL;
        goto out;
    }

    /* bdrv_append() consumes a strong reference to bs_snapshot (i.e. it will
     * call bdrv_unref() on it), so in order to be able to return one, we have
     * to increase bs_snapshot's refcount here */
    bdrv_ref(bs_snapshot);
    bdrv_append(bs_snapshot, bs);

    g_free(tmp_filename);
    return bs_snapshot;

out:
    QDECREF(snapshot_options);
    g_free(tmp_filename);
    return NULL;
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
static BlockDriverState *bdrv_open_inherit(const char *filename,
                                           const char *reference,
                                           QDict *options, int flags,
                                           BlockDriverState *parent,
                                           const BdrvChildRole *child_role,
                                           Error **errp)
{
    int ret;
    BdrvChild *file = NULL;
    BlockDriverState *bs;
    BlockDriver *drv = NULL;
    const char *drvname;
    const char *backing;
    Error *local_err = NULL;
    QDict *snapshot_options = NULL;
    int snapshot_flags = 0;

    assert(!child_role || !flags);
    assert(!child_role == !parent);

    if (reference) {
        bool options_non_empty = options ? qdict_size(options) : false;
        QDECREF(options);

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

    bs->open_flags = flags;
    bs->options = options;
    options = qdict_clone_shallow(options);

    /* Find the right image format driver */
    drvname = qdict_get_try_str(options, "driver");
    if (drvname) {
        drv = bdrv_find_format(drvname);
        if (!drv) {
            error_setg(errp, "Unknown driver: '%s'", drvname);
            goto fail;
        }
    }

    assert(drvname || !(flags & BDRV_O_PROTOCOL));

    backing = qdict_get_try_str(options, "backing");
    if (backing && *backing == '\0') {
        flags |= BDRV_O_NO_BACKING;
        qdict_del(options, "backing");
    }

    /* Open image file without format layer */
    if ((flags & BDRV_O_PROTOCOL) == 0) {
        if (flags & BDRV_O_RDWR) {
            flags |= BDRV_O_ALLOW_RDWR;
        }
        if (flags & BDRV_O_SNAPSHOT) {
            snapshot_options = qdict_new();
            bdrv_temp_snapshot_options(&snapshot_flags, snapshot_options,
                                       flags, options);
            bdrv_backing_options(&flags, options, flags, options);
        }

        bs->open_flags = flags;

        file = bdrv_open_child(filename, options, "file", bs,
                               &child_file, true, &local_err);
        if (local_err) {
            goto fail;
        }
    }

    /* Image format probing */
    bs->probed = !drv;
    if (!drv && file) {
        ret = find_image_format(file->bs, filename, &drv, &local_err);
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
        qdict_put(bs->options, "driver", qstring_from_str(drv->format_name));
        qdict_put(options, "driver", qstring_from_str(drv->format_name));
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

    if (file && (bs->file != file)) {
        bdrv_unref_child(bs, file);
        file = NULL;
    }

    /* If there is a backing file, use it */
    if ((flags & BDRV_O_NO_BACKING) == 0) {
        ret = bdrv_open_backing_file(bs, options, "backing", &local_err);
        if (ret < 0) {
            goto close_and_fail;
        }
    }

    bdrv_refresh_filename(bs);

    /* Check if any unknown options were used */
    if (options && (qdict_size(options) != 0)) {
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

    if (!bdrv_key_required(bs)) {
        bdrv_parent_cb_change_media(bs, true);
    } else if (!runstate_check(RUN_STATE_PRELAUNCH)
               && !runstate_check(RUN_STATE_INMIGRATE)
               && !runstate_check(RUN_STATE_PAUSED)) { /* HACK */
        error_setg(errp,
                   "Guest must be stopped for opening of encrypted image");
        goto close_and_fail;
    }

    QDECREF(options);

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
    if (file != NULL) {
        bdrv_unref_child(bs, file);
    }
    QDECREF(snapshot_options);
    QDECREF(bs->explicit_options);
    QDECREF(bs->options);
    QDECREF(options);
    bs->options = NULL;
    bdrv_unref(bs);
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return NULL;

close_and_fail:
    bdrv_unref(bs);
    QDECREF(snapshot_options);
    QDECREF(options);
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return NULL;
}

BlockDriverState *bdrv_open(const char *filename, const char *reference,
                            QDict *options, int flags, Error **errp)
{
    return bdrv_open_inherit(filename, reference, options, flags, NULL,
                             NULL, errp);
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
 * options contains the changed options for the associated bs
 * (the BlockReopenQueue takes ownership)
 *
 * flags contains the open flags for the associated bs
 *
 * returns a pointer to bs_queue, which is either the newly allocated
 * bs_queue, or the existing bs_queue being used.
 *
 */
static BlockReopenQueue *bdrv_reopen_queue_child(BlockReopenQueue *bs_queue,
                                                 BlockDriverState *bs,
                                                 QDict *options,
                                                 int flags,
                                                 const BdrvChildRole *role,
                                                 QDict *parent_options,
                                                 int parent_flags)
{
    assert(bs != NULL);

    BlockReopenQueueEntry *bs_entry;
    BdrvChild *child;
    QDict *old_options, *explicit_options;

    if (bs_queue == NULL) {
        bs_queue = g_new0(BlockReopenQueue, 1);
        QSIMPLEQ_INIT(bs_queue);
    }

    if (!options) {
        options = qdict_new();
    }

    /*
     * Precedence of options:
     * 1. Explicitly passed in options (highest)
     * 2. Set in flags (only for top level)
     * 3. Retained from explicitly set options of bs
     * 4. Inherited from parent node
     * 5. Retained from effective options of bs
     */

    if (!parent_options) {
        /*
         * Any setting represented by flags is always updated. If the
         * corresponding QDict option is set, it takes precedence. Otherwise
         * the flag is translated into a QDict option. The old setting of bs is
         * not considered.
         */
        update_options_from_flags(options, flags);
    }

    /* Old explicitly set values (don't overwrite by inherited value) */
    old_options = qdict_clone_shallow(bs->explicit_options);
    bdrv_join_options(bs, options, old_options);
    QDECREF(old_options);

    explicit_options = qdict_clone_shallow(options);

    /* Inherit from parent node */
    if (parent_options) {
        assert(!flags);
        role->inherit_options(&flags, options, parent_flags, parent_options);
    }

    /* Old values are used for options that aren't set yet */
    old_options = qdict_clone_shallow(bs->options);
    bdrv_join_options(bs, options, old_options);
    QDECREF(old_options);

    /* bdrv_open() masks this flag out */
    flags &= ~BDRV_O_PROTOCOL;

    QLIST_FOREACH(child, &bs->children, next) {
        QDict *new_child_options;
        char *child_key_dot;

        /* reopen can only change the options of block devices that were
         * implicitly created and inherited options. For other (referenced)
         * block devices, a syntax like "backing.foo" results in an error. */
        if (child->bs->inherits_from != bs) {
            continue;
        }

        child_key_dot = g_strdup_printf("%s.", child->name);
        qdict_extract_subqdict(options, &new_child_options, child_key_dot);
        g_free(child_key_dot);

        bdrv_reopen_queue_child(bs_queue, child->bs, new_child_options, 0,
                                child->role, options, flags);
    }

    bs_entry = g_new0(BlockReopenQueueEntry, 1);
    QSIMPLEQ_INSERT_TAIL(bs_queue, bs_entry, entry);

    bs_entry->state.bs = bs;
    bs_entry->state.options = options;
    bs_entry->state.explicit_options = explicit_options;
    bs_entry->state.flags = flags;

    return bs_queue;
}

BlockReopenQueue *bdrv_reopen_queue(BlockReopenQueue *bs_queue,
                                    BlockDriverState *bs,
                                    QDict *options, int flags)
{
    return bdrv_reopen_queue_child(bs_queue, bs, options, flags,
                                   NULL, NULL, 0);
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
        } else if (ret) {
            QDECREF(bs_entry->state.explicit_options);
        }
        QDECREF(bs_entry->state.options);
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
    BlockReopenQueue *queue = bdrv_reopen_queue(NULL, bs, NULL, bdrv_flags);

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
    QemuOpts *opts;
    const char *value;

    assert(reopen_state != NULL);
    assert(reopen_state->bs->drv != NULL);
    drv = reopen_state->bs->drv;

    /* Process generic block layer options */
    opts = qemu_opts_create(&bdrv_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, reopen_state->options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto error;
    }

    update_flags_from_options(&reopen_state->flags, opts);

    /* node-name and driver must be unchanged. Put them back into the QDict, so
     * that they are checked at the end of this function. */
    value = qemu_opt_get(opts, "node-name");
    if (value) {
        qdict_put(reopen_state->options, "node-name", qstring_from_str(value));
    }

    value = qemu_opt_get(opts, "driver");
    if (value) {
        qdict_put(reopen_state->options, "driver", qstring_from_str(value));
    }

    /* if we are to stay read-only, do not allow permission change
     * to r/w */
    if (!(reopen_state->bs->open_flags & BDRV_O_ALLOW_RDWR) &&
        reopen_state->flags & BDRV_O_RDWR) {
        error_setg(errp, "Node '%s' is read only",
                   bdrv_get_device_or_node_name(reopen_state->bs));
        goto error;
    }


    ret = bdrv_flush(reopen_state->bs);
    if (ret) {
        error_setg_errno(errp, -ret, "Error flushing drive");
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
        error_setg(errp, "Block format '%s' used by node '%s' "
                   "does not support reopening files", drv->format_name,
                   bdrv_get_device_or_node_name(reopen_state->bs));
        ret = -1;
        goto error;
    }

    /* Options that are not handled are only okay if they are unchanged
     * compared to the old state. It is expected that some options are only
     * used for the initial open, but not reopen (e.g. filename) */
    if (qdict_size(reopen_state->options)) {
        const QDictEntry *entry = qdict_first(reopen_state->options);

        do {
            QString *new_obj = qobject_to_qstring(entry->value);
            const char *new = qstring_get_str(new_obj);
            const char *old = qdict_get_try_str(reopen_state->bs->options,
                                                entry->key);

            if (!old || strcmp(new, old)) {
                error_setg(errp, "Cannot change the option '%s'", entry->key);
                ret = -EINVAL;
                goto error;
            }
        } while ((entry = qdict_next(reopen_state->options, entry)));
    }

    ret = 0;

error:
    qemu_opts_del(opts);
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
    QDECREF(reopen_state->bs->explicit_options);

    reopen_state->bs->explicit_options   = reopen_state->explicit_options;
    reopen_state->bs->open_flags         = reopen_state->flags;
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

    QDECREF(reopen_state->explicit_options);
}


static void bdrv_close(BlockDriverState *bs)
{
    BdrvAioNotifier *ban, *ban_next;

    assert(!bs->job);
    assert(!bs->refcnt);

    bdrv_drained_begin(bs); /* complete I/O */
    bdrv_flush(bs);
    bdrv_drain(bs); /* in case flush left pending I/O */

    bdrv_release_named_dirty_bitmaps(bs);
    assert(QLIST_EMPTY(&bs->dirty_bitmaps));

    if (bs->drv) {
        BdrvChild *child, *next;

        bs->drv->bdrv_close(bs);
        bs->drv = NULL;

        bdrv_set_backing_hd(bs, NULL);

        if (bs->file != NULL) {
            bdrv_unref_child(bs, bs->file);
            bs->file = NULL;
        }

        QLIST_FOREACH_SAFE(child, &bs->children, next, next) {
            /* TODO Remove bdrv_unref() from drivers' close function and use
             * bdrv_unref_child() here */
            if (child->bs->inherits_from == bs) {
                child->bs->inherits_from = NULL;
            }
            bdrv_detach_child(child);
        }

        g_free(bs->opaque);
        bs->opaque = NULL;
        bs->copy_on_read = 0;
        bs->backing_file[0] = '\0';
        bs->backing_format[0] = '\0';
        bs->total_sectors = 0;
        bs->encrypted = 0;
        bs->valid_key = 0;
        bs->sg = 0;
        bs->zero_beyond_eof = false;
        QDECREF(bs->options);
        QDECREF(bs->explicit_options);
        bs->options = NULL;
        QDECREF(bs->full_open_options);
        bs->full_open_options = NULL;
    }

    QLIST_FOREACH_SAFE(ban, &bs->aio_notifiers, list, ban_next) {
        g_free(ban);
    }
    QLIST_INIT(&bs->aio_notifiers);
    bdrv_drained_end(bs);
}

void bdrv_close_all(void)
{
    BlockDriverState *bs;
    AioContext *aio_context;

    /* Drop references from requests still in flight, such as canceled block
     * jobs whose AIO context has not been polled yet */
    bdrv_drain_all();

    blk_remove_all_bs();
    blockdev_close_all_bdrv_states();

    /* Cancel all block jobs */
    while (!QTAILQ_EMPTY(&all_bdrv_states)) {
        QTAILQ_FOREACH(bs, &all_bdrv_states, bs_list) {
            aio_context = bdrv_get_aio_context(bs);

            aio_context_acquire(aio_context);
            if (bs->job) {
                block_job_cancel_sync(bs->job);
                aio_context_release(aio_context);
                break;
            }
            aio_context_release(aio_context);
        }

        /* All the remaining BlockDriverStates are referenced directly or
         * indirectly from block jobs, so there needs to be at least one BDS
         * directly used by a block job */
        assert(bs);
    }
}

static void change_parent_backing_link(BlockDriverState *from,
                                       BlockDriverState *to)
{
    BdrvChild *c, *next;

    QLIST_FOREACH_SAFE(c, &from->parents, next_parent, next) {
        assert(c->role != &child_backing);
        bdrv_ref(to);
        bdrv_replace_child(c, to);
        bdrv_unref(from);
    }
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
void bdrv_append(BlockDriverState *bs_new, BlockDriverState *bs_top)
{
    assert(!bdrv_requests_pending(bs_top));
    assert(!bdrv_requests_pending(bs_new));

    bdrv_ref(bs_top);

    change_parent_backing_link(bs_top, bs_new);
    bdrv_set_backing_hd(bs_new, bs_top);
    bdrv_unref(bs_top);

    /* bs_new is now referenced by its new parents, we don't need the
     * additional reference any more. */
    bdrv_unref(bs_new);
}

void bdrv_replace_in_backing_chain(BlockDriverState *old, BlockDriverState *new)
{
    assert(!bdrv_requests_pending(old));
    assert(!bdrv_requests_pending(new));

    bdrv_ref(old);

    change_parent_backing_link(old, new);

    /* Change backing files if a previously independent node is added to the
     * chain. For active commit, we replace top by its own (indirect) backing
     * file and don't do anything here so we don't build a loop. */
    if (new->backing == NULL && !bdrv_chain_contains(backing_bs(old), new)) {
        bdrv_set_backing_hd(new, backing_bs(old));
        bdrv_set_backing_hd(old, NULL);
    }

    bdrv_unref(old);
}

static void bdrv_delete(BlockDriverState *bs)
{
    assert(!bs->job);
    assert(bdrv_op_blocker_is_empty(bs));
    assert(!bs->refcnt);

    bdrv_close(bs);

    /* remove from list, if necessary */
    if (bs->node_name[0] != '\0') {
        QTAILQ_REMOVE(&graph_bdrv_states, bs, node_list);
    }
    QTAILQ_REMOVE(&all_bdrv_states, bs, bs_list);

    g_free(bs);
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
    if (bs->drv == NULL) {
        return -ENOMEDIUM;
    }
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

    if (!drv)
        return -ENOMEDIUM;

    if (!bs->backing) {
        return -ENOTSUP;
    }

    if (bdrv_op_is_blocked(bs, BLOCK_OP_TYPE_COMMIT_SOURCE, NULL) ||
        bdrv_op_is_blocked(bs->backing->bs, BLOCK_OP_TYPE_COMMIT_TARGET, NULL)) {
        return -EBUSY;
    }

    ro = bs->backing->bs->read_only;
    open_flags =  bs->backing->bs->open_flags;

    if (ro) {
        if (bdrv_reopen(bs->backing->bs, open_flags | BDRV_O_RDWR, NULL)) {
            return -EACCES;
        }
    }

    length = bdrv_getlength(bs);
    if (length < 0) {
        ret = length;
        goto ro_cleanup;
    }

    backing_length = bdrv_getlength(bs->backing->bs);
    if (backing_length < 0) {
        ret = backing_length;
        goto ro_cleanup;
    }

    /* If our top snapshot is larger than the backing file image,
     * grow the backing file image if possible.  If not possible,
     * we must return an error */
    if (length > backing_length) {
        ret = bdrv_truncate(bs->backing->bs, length);
        if (ret < 0) {
            goto ro_cleanup;
        }
    }

    total_sectors = length >> BDRV_SECTOR_BITS;

    /* qemu_try_blockalign() for bs will choose an alignment that works for
     * bs->backing->bs as well, so no need to compare the alignment manually. */
    buf = qemu_try_blockalign(bs, COMMIT_BUF_SECTORS * BDRV_SECTOR_SIZE);
    if (buf == NULL) {
        ret = -ENOMEM;
        goto ro_cleanup;
    }

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

            ret = bdrv_write(bs->backing->bs, sector, buf, n);
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
    if (bs->backing) {
        bdrv_flush(bs->backing->bs);
    }

    ret = 0;
ro_cleanup:
    qemu_vfree(buf);

    if (ro) {
        /* ignoring error return here */
        bdrv_reopen(bs->backing->bs, open_flags & ~BDRV_O_RDWR, NULL);
    }

    return ret;
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
    BlockDriverState *new_top_bs = NULL;
    int ret = -EIO;

    if (!top->drv || !base->drv) {
        goto exit;
    }

    new_top_bs = bdrv_find_overlay(active, top);

    if (new_top_bs == NULL) {
        /* we could not find the image above 'top', this is an error */
        goto exit;
    }

    /* special case of new_top_bs->backing->bs already pointing to base - nothing
     * to do, no intermediate images */
    if (backing_bs(new_top_bs) == base) {
        ret = 0;
        goto exit;
    }

    /* Make sure that base is in the backing chain of top */
    if (!bdrv_chain_contains(top, base)) {
        goto exit;
    }

    /* success - we can delete the intermediate states, and link top->base */
    backing_file_str = backing_file_str ? backing_file_str : base->filename;
    ret = bdrv_change_backing_file(new_top_bs, backing_file_str,
                                   base->drv ? base->drv->format_name : "");
    if (ret) {
        goto exit;
    }
    bdrv_set_backing_hd(new_top_bs, base);

    ret = 0;
exit:
    return ret;
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
        bdrv_dirty_bitmap_truncate(bs);
        bdrv_parent_cb_resize(bs);
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
        return bdrv_get_allocated_file_size(bs->file->bs);
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

    ret = ret > INT64_MAX / BDRV_SECTOR_SIZE ? -EFBIG : ret;
    return ret < 0 ? ret : ret * BDRV_SECTOR_SIZE;
}

/* return 0 as number of sectors if no device present or error */
void bdrv_get_geometry(BlockDriverState *bs, uint64_t *nb_sectors_ptr)
{
    int64_t nb_sectors = bdrv_nb_sectors(bs);

    *nb_sectors_ptr = nb_sectors < 0 ? 0 : nb_sectors;
}

int bdrv_is_read_only(BlockDriverState *bs)
{
    return bs->read_only;
}

int bdrv_is_sg(BlockDriverState *bs)
{
    return bs->sg;
}

int bdrv_is_encrypted(BlockDriverState *bs)
{
    if (bs->backing && bs->backing->bs->encrypted) {
        return 1;
    }
    return bs->encrypted;
}

int bdrv_key_required(BlockDriverState *bs)
{
    BdrvChild *backing = bs->backing;

    if (backing && backing->bs->encrypted && !backing->bs->valid_key) {
        return 1;
    }
    return (bs->encrypted && !bs->valid_key);
}

int bdrv_set_key(BlockDriverState *bs, const char *key)
{
    int ret;
    if (bs->backing && bs->backing->bs->encrypted) {
        ret = bdrv_set_key(bs->backing->bs, key);
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
        /* call the change callback now, we skipped it on open */
        bs->valid_key = 1;
        bdrv_parent_cb_change_media(bs, true);
    }
    return ret;
}

/*
 * Provide an encryption key for @bs.
 * If @key is non-null:
 *     If @bs is not encrypted, fail.
 *     Else if the key is invalid, fail.
 *     Else set @bs's key to @key, replacing the existing key, if any.
 * If @key is null:
 *     If @bs is encrypted and still lacks a key, fail.
 *     Else do nothing.
 * On failure, store an error object through @errp if non-null.
 */
void bdrv_add_key(BlockDriverState *bs, const char *key, Error **errp)
{
    if (key) {
        if (!bdrv_is_encrypted(bs)) {
            error_setg(errp, "Node '%s' is not encrypted",
                      bdrv_get_device_or_node_name(bs));
        } else if (bdrv_set_key(bs, key) < 0) {
            error_setg(errp, QERR_INVALID_PASSWORD);
        }
    } else {
        if (bdrv_key_required(bs)) {
            error_set(errp, ERROR_CLASS_DEVICE_ENCRYPTED,
                      "'%s' (%s) is encrypted",
                      bdrv_get_device_or_node_name(bs),
                      bdrv_get_encrypted_filename(bs));
        }
    }
}

const char *bdrv_get_format_name(BlockDriverState *bs)
{
    return bs->drv ? bs->drv->format_name : NULL;
}

static int qsort_strcmp(const void *a, const void *b)
{
    return strcmp(a, b);
}

void bdrv_iterate_format(void (*it)(void *opaque, const char *name),
                         void *opaque)
{
    BlockDriver *drv;
    int count = 0;
    int i;
    const char **formats = NULL;

    QLIST_FOREACH(drv, &bdrv_drivers, list) {
        if (drv->format_name) {
            bool found = false;
            int i = count;
            while (formats && i && !found) {
                found = !strcmp(formats[--i], drv->format_name);
            }

            if (!found) {
                formats = g_renew(const char *, formats, count + 1);
                formats[count++] = drv->format_name;
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
    assert(bs->drv);

    /* If BS is a copy on write image, it is initialized to
       the contents of the base image, which may not be zeroes.  */
    if (bs->backing) {
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
    BlockDriverInfo bdi;

    if (bs->backing || !(bs->open_flags & BDRV_O_UNMAP)) {
        return false;
    }

    if (bdrv_get_info(bs, &bdi) == 0) {
        return bdi.can_write_zeroes_with_unmap;
    }

    return false;
}

const char *bdrv_get_encrypted_filename(BlockDriverState *bs)
{
    if (bs->backing && bs->backing->bs->encrypted)
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

void bdrv_debug_event(BlockDriverState *bs, BlkdebugEvent event)
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
        bs = bs->file ? bs->file->bs : NULL;
    }

    if (bs && bs->drv && bs->drv->bdrv_debug_breakpoint) {
        return bs->drv->bdrv_debug_breakpoint(bs, event, tag);
    }

    return -ENOTSUP;
}

int bdrv_debug_remove_breakpoint(BlockDriverState *bs, const char *tag)
{
    while (bs && bs->drv && !bs->drv->bdrv_debug_remove_breakpoint) {
        bs = bs->file ? bs->file->bs : NULL;
    }

    if (bs && bs->drv && bs->drv->bdrv_debug_remove_breakpoint) {
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

    for (curr_bs = bs; curr_bs->backing; curr_bs = curr_bs->backing->bs) {

        /* If either of the filename paths is actually a protocol, then
         * compare unmodified paths; otherwise make paths relative */
        if (is_protocol || path_has_protocol(curr_bs->backing_file)) {
            if (strcmp(backing_file, curr_bs->backing_file) == 0) {
                retval = curr_bs->backing->bs;
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
                retval = curr_bs->backing->bs;
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

    if (!bs->backing) {
        return 0;
    }

    return 1 + bdrv_get_backing_file_depth(bs->backing->bs);
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

void bdrv_invalidate_cache(BlockDriverState *bs, Error **errp)
{
    BdrvChild *child;
    Error *local_err = NULL;
    int ret;

    if (!bs->drv)  {
        return;
    }

    if (!(bs->open_flags & BDRV_O_INACTIVE)) {
        return;
    }
    bs->open_flags &= ~BDRV_O_INACTIVE;

    if (bs->drv->bdrv_invalidate_cache) {
        bs->drv->bdrv_invalidate_cache(bs, &local_err);
        if (local_err) {
            bs->open_flags |= BDRV_O_INACTIVE;
            error_propagate(errp, local_err);
            return;
        }
    }

    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_invalidate_cache(child->bs, &local_err);
        if (local_err) {
            bs->open_flags |= BDRV_O_INACTIVE;
            error_propagate(errp, local_err);
            return;
        }
    }

    ret = refresh_total_sectors(bs, bs->total_sectors);
    if (ret < 0) {
        bs->open_flags |= BDRV_O_INACTIVE;
        error_setg_errno(errp, -ret, "Could not refresh total sector count");
        return;
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
            return;
        }
    }
}

static int bdrv_inactivate_recurse(BlockDriverState *bs,
                                   bool setting_flag)
{
    BdrvChild *child;
    int ret;

    if (!setting_flag && bs->drv->bdrv_inactivate) {
        ret = bs->drv->bdrv_inactivate(bs);
        if (ret < 0) {
            return ret;
        }
    }

    QLIST_FOREACH(child, &bs->children, next) {
        ret = bdrv_inactivate_recurse(child->bs, setting_flag);
        if (ret < 0) {
            return ret;
        }
    }

    if (setting_flag) {
        bs->open_flags |= BDRV_O_INACTIVE;
    }
    return 0;
}

int bdrv_inactivate_all(void)
{
    BlockDriverState *bs = NULL;
    BdrvNextIterator it;
    int ret = 0;
    int pass;

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        aio_context_acquire(bdrv_get_aio_context(bs));
    }

    /* We do two passes of inactivation. The first pass calls to drivers'
     * .bdrv_inactivate callbacks recursively so all cache is flushed to disk;
     * the second pass sets the BDRV_O_INACTIVE flag so that no further write
     * is allowed. */
    for (pass = 0; pass < 2; pass++) {
        for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
            ret = bdrv_inactivate_recurse(bs, pass);
            if (ret < 0) {
                goto out;
            }
        }
    }

out:
    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        aio_context_release(bdrv_get_aio_context(bs));
    }

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
    const char *device_name;

    if (drv && drv->bdrv_eject) {
        drv->bdrv_eject(bs, eject_flag);
    }

    device_name = bdrv_get_device_name(bs);
    if (device_name[0] != '\0') {
        qapi_event_send_device_tray_moved(device_name,
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
        if (errp) {
            *errp = error_copy(blocker->reason);
            error_prepend(errp, "Node '%s' is busy: ",
                          bdrv_get_device_or_node_name(bs));
        }
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
                     char *options, uint64_t img_size, int flags,
                     Error **errp, bool quiet)
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

    create_opts = qemu_opts_append(create_opts, drv->create_opts);
    create_opts = qemu_opts_append(create_opts, proto_drv->create_opts);

    /* Create parameter list with default values */
    opts = qemu_opts_create(create_opts, NULL, 0, &error_abort);
    qemu_opt_set_number(opts, BLOCK_OPT_SIZE, img_size, &error_abort);

    /* Parse -o options */
    if (options) {
        qemu_opts_do_parse(opts, options, NULL, &local_err);
        if (local_err) {
            error_report_err(local_err);
            local_err = NULL;
            error_setg(errp, "Invalid options for file format '%s'", fmt);
            goto out;
        }
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

    // The size for the image must always be specified, with one exception:
    // If we are using a backing file, we can obtain the size from there
    size = qemu_opt_get_size(opts, BLOCK_OPT_SIZE, 0);
    if (size == -1) {
        if (backing_file) {
            BlockDriverState *bs;
            char *full_backing = g_new0(char, PATH_MAX);
            int64_t size;
            int back_flags;
            QDict *backing_options = NULL;

            bdrv_get_full_backing_filename_from_filename(filename, backing_file,
                                                         full_backing, PATH_MAX,
                                                         &local_err);
            if (local_err) {
                g_free(full_backing);
                goto out;
            }

            /* backing files always opened read-only */
            back_flags = flags;
            back_flags &= ~(BDRV_O_RDWR | BDRV_O_SNAPSHOT | BDRV_O_NO_BACKING);

            if (backing_fmt) {
                backing_options = qdict_new();
                qdict_put(backing_options, "driver",
                          qstring_from_str(backing_fmt));
            }

            bs = bdrv_open(full_backing, NULL, backing_options, back_flags,
                           &local_err);
            g_free(full_backing);
            if (!bs) {
                goto out;
            }
            size = bdrv_getlength(bs);
            if (size < 0) {
                error_setg_errno(errp, -size, "Could not get size of '%s'",
                                 backing_file);
                bdrv_unref(bs);
                goto out;
            }

            qemu_opt_set_number(opts, BLOCK_OPT_SIZE, size, &error_abort);

            bdrv_unref(bs);
        } else {
            error_setg(errp, "Image creation needs a size parameter");
            goto out;
        }
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
    BdrvAioNotifier *baf;
    BdrvChild *child;

    if (!bs->drv) {
        return;
    }

    QLIST_FOREACH(baf, &bs->aio_notifiers, list) {
        baf->detach_aio_context(baf->opaque);
    }

    if (bs->drv->bdrv_detach_aio_context) {
        bs->drv->bdrv_detach_aio_context(bs);
    }
    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_detach_aio_context(child->bs);
    }

    bs->aio_context = NULL;
}

void bdrv_attach_aio_context(BlockDriverState *bs,
                             AioContext *new_context)
{
    BdrvAioNotifier *ban;
    BdrvChild *child;

    if (!bs->drv) {
        return;
    }

    bs->aio_context = new_context;

    QLIST_FOREACH(child, &bs->children, next) {
        bdrv_attach_aio_context(child->bs, new_context);
    }
    if (bs->drv->bdrv_attach_aio_context) {
        bs->drv->bdrv_attach_aio_context(bs, new_context);
    }

    QLIST_FOREACH(ban, &bs->aio_notifiers, list) {
        ban->attached_aio_context(new_context, ban->opaque);
    }
}

void bdrv_set_aio_context(BlockDriverState *bs, AioContext *new_context)
{
    bdrv_drain(bs); /* ensure there are no in-flight requests */

    bdrv_detach_aio_context(bs);

    /* This function executes in the old AioContext so acquire the new one in
     * case it runs in a different thread.
     */
    aio_context_acquire(new_context);
    bdrv_attach_aio_context(bs, new_context);
    aio_context_release(new_context);
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
            ban->opaque               == opaque)
        {
            QLIST_REMOVE(ban, list);
            g_free(ban);

            return;
        }
    }

    abort();
}

int bdrv_amend_options(BlockDriverState *bs, QemuOpts *opts,
                       BlockDriverAmendStatusCB *status_cb, void *cb_opaque)
{
    if (!bs->drv->bdrv_amend_options) {
        return -ENOTSUP;
    }
    return bs->drv->bdrv_amend_options(bs, opts, status_cb, cb_opaque);
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

static bool append_open_options(QDict *d, BlockDriverState *bs)
{
    const QDictEntry *entry;
    QemuOptDesc *desc;
    BdrvChild *child;
    bool found_any = false;
    const char *p;

    for (entry = qdict_first(bs->options); entry;
         entry = qdict_next(bs->options, entry))
    {
        /* Exclude options for children */
        QLIST_FOREACH(child, &bs->children, next) {
            if (strstart(qdict_entry_key(entry), child->name, &p)
                && (!*p || *p == '.'))
            {
                break;
            }
        }
        if (child) {
            continue;
        }

        /* And exclude all non-driver-specific options */
        for (desc = bdrv_runtime_opts.desc; desc->name; desc++) {
            if (!strcmp(qdict_entry_key(entry), desc->name)) {
                break;
            }
        }
        if (desc->name) {
            continue;
        }

        qobject_incref(qdict_entry_value(entry));
        qdict_put_obj(d, qdict_entry_key(entry), qdict_entry_value(entry));
        found_any = true;
    }

    return found_any;
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
    QDict *opts;

    if (!drv) {
        return;
    }

    /* This BDS's file name will most probably depend on its file's name, so
     * refresh that first */
    if (bs->file) {
        bdrv_refresh_filename(bs->file->bs);
    }

    if (drv->bdrv_refresh_filename) {
        /* Obsolete information is of no use here, so drop the old file name
         * information before refreshing it */
        bs->exact_filename[0] = '\0';
        if (bs->full_open_options) {
            QDECREF(bs->full_open_options);
            bs->full_open_options = NULL;
        }

        opts = qdict_new();
        append_open_options(opts, bs);
        drv->bdrv_refresh_filename(bs, opts);
        QDECREF(opts);
    } else if (bs->file) {
        /* Try to reconstruct valid information from the underlying file */
        bool has_open_options;

        bs->exact_filename[0] = '\0';
        if (bs->full_open_options) {
            QDECREF(bs->full_open_options);
            bs->full_open_options = NULL;
        }

        opts = qdict_new();
        has_open_options = append_open_options(opts, bs);

        /* If no specific options have been given for this BDS, the filename of
         * the underlying file should suffice for this one as well */
        if (bs->file->bs->exact_filename[0] && !has_open_options) {
            strcpy(bs->exact_filename, bs->file->bs->exact_filename);
        }
        /* Reconstructing the full options QDict is simple for most format block
         * drivers, as long as the full options are known for the underlying
         * file BDS. The full options QDict of that file BDS should somehow
         * contain a representation of the filename, therefore the following
         * suffices without querying the (exact_)filename of this BDS. */
        if (bs->file->bs->full_open_options) {
            qdict_put_obj(opts, "driver",
                          QOBJECT(qstring_from_str(drv->format_name)));
            QINCREF(bs->file->bs->full_open_options);
            qdict_put_obj(opts, "file",
                          QOBJECT(bs->file->bs->full_open_options));

            bs->full_open_options = opts;
        } else {
            QDECREF(opts);
        }
    } else if (!bs->full_open_options && qdict_size(bs->options)) {
        /* There is no underlying file BDS (at least referenced by BDS.file),
         * so the full options QDict should be equal to the options given
         * specifically for this block device when it was opened (plus the
         * driver specification).
         * Because those options don't change, there is no need to update
         * full_open_options when it's already set. */

        opts = qdict_new();
        append_open_options(opts, bs);
        qdict_put_obj(opts, "driver",
                      QOBJECT(qstring_from_str(drv->format_name)));

        if (bs->exact_filename[0]) {
            /* This may not work for all block protocol drivers (some may
             * require this filename to be parsed), but we have to find some
             * default solution here, so just include it. If some block driver
             * does not support pure options without any filename at all or
             * needs some special format of the options QDict, it needs to
             * implement the driver-specific bdrv_refresh_filename() function.
             */
            qdict_put_obj(opts, "filename",
                          QOBJECT(qstring_from_str(bs->exact_filename)));
        }

        bs->full_open_options = opts;
    }

    if (bs->exact_filename[0]) {
        pstrcpy(bs->filename, sizeof(bs->filename), bs->exact_filename);
    } else if (bs->full_open_options) {
        QString *json = qobject_to_json(QOBJECT(bs->full_open_options));
        snprintf(bs->filename, sizeof(bs->filename), "json:%s",
                 qstring_get_str(json));
        QDECREF(json);
    }
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
