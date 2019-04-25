/*
 * Block driver for Parallels disk image format
 *
 * Copyright (c) 2007 Alex Beregszaszi
 * Copyright (c) 2015 Denis V. Lunev <den@openvz.org>
 *
 * This code was originally based on comparing different disk images created
 * by Parallels. Currently it is based on opened OpenVZ sources
 * available at
 *     http://git.openvz.org/?p=ploop;a=summary
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
#include "qapi/error.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "sysemu/block-backend.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-block-core.h"
#include "qemu/bswap.h"
#include "qemu/bitmap.h"
#include "migration/blocker.h"
#include "parallels.h"

/**************************************************************/

#define HEADER_MAGIC "WithoutFreeSpace"
#define HEADER_MAGIC2 "WithouFreSpacExt"
#define HEADER_VERSION 2
#define HEADER_INUSE_MAGIC  (0x746F6E59)
#define MAX_PARALLELS_IMAGE_FACTOR (1ull << 32)

static QEnumLookup prealloc_mode_lookup = {
    .array = (const char *const[]) {
        "falloc",
        "truncate",
    },
    .size = PRL_PREALLOC_MODE__MAX
};

#define PARALLELS_OPT_PREALLOC_MODE     "prealloc-mode"
#define PARALLELS_OPT_PREALLOC_SIZE     "prealloc-size"

static QemuOptsList parallels_runtime_opts = {
    .name = "parallels",
    .head = QTAILQ_HEAD_INITIALIZER(parallels_runtime_opts.head),
    .desc = {
        {
            .name = PARALLELS_OPT_PREALLOC_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Preallocation size on image expansion",
            .def_value_str = "128M",
        },
        {
            .name = PARALLELS_OPT_PREALLOC_MODE,
            .type = QEMU_OPT_STRING,
            .help = "Preallocation mode on image expansion "
                    "(allowed values: falloc, truncate)",
            .def_value_str = "falloc",
        },
        { /* end of list */ },
    },
};

static QemuOptsList parallels_create_opts = {
    .name = "parallels-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(parallels_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size",
        },
        {
            .name = BLOCK_OPT_CLUSTER_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Parallels image cluster size",
            .def_value_str = stringify(DEFAULT_CLUSTER_SIZE),
        },
        { /* end of list */ }
    }
};


static int64_t bat2sect(BDRVParallelsState *s, uint32_t idx)
{
    return (uint64_t)le32_to_cpu(s->bat_bitmap[idx]) * s->off_multiplier;
}

static uint32_t bat_entry_off(uint32_t idx)
{
    return sizeof(ParallelsHeader) + sizeof(uint32_t) * idx;
}

static int64_t seek_to_sector(BDRVParallelsState *s, int64_t sector_num)
{
    uint32_t index, offset;

    index = sector_num / s->tracks;
    offset = sector_num % s->tracks;

    /* not allocated */
    if ((index >= s->bat_size) || (s->bat_bitmap[index] == 0)) {
        return -1;
    }
    return bat2sect(s, index) + offset;
}

static int cluster_remainder(BDRVParallelsState *s, int64_t sector_num,
        int nb_sectors)
{
    int ret = s->tracks - sector_num % s->tracks;
    return MIN(nb_sectors, ret);
}

static int64_t block_status(BDRVParallelsState *s, int64_t sector_num,
                            int nb_sectors, int *pnum)
{
    int64_t start_off = -2, prev_end_off = -2;

    *pnum = 0;
    while (nb_sectors > 0 || start_off == -2) {
        int64_t offset = seek_to_sector(s, sector_num);
        int to_end;

        if (start_off == -2) {
            start_off = offset;
            prev_end_off = offset;
        } else if (offset != prev_end_off) {
            break;
        }

        to_end = cluster_remainder(s, sector_num, nb_sectors);
        nb_sectors -= to_end;
        sector_num += to_end;
        *pnum += to_end;

        if (offset > 0) {
            prev_end_off += to_end;
        }
    }
    return start_off;
}

static int64_t allocate_clusters(BlockDriverState *bs, int64_t sector_num,
                                 int nb_sectors, int *pnum)
{
    int ret;
    BDRVParallelsState *s = bs->opaque;
    int64_t pos, space, idx, to_allocate, i, len;

    pos = block_status(s, sector_num, nb_sectors, pnum);
    if (pos > 0) {
        return pos;
    }

    idx = sector_num / s->tracks;
    to_allocate = DIV_ROUND_UP(sector_num + *pnum, s->tracks) - idx;

    /* This function is called only by parallels_co_writev(), which will never
     * pass a sector_num at or beyond the end of the image (because the block
     * layer never passes such a sector_num to that function). Therefore, idx
     * is always below s->bat_size.
     * block_status() will limit *pnum so that sector_num + *pnum will not
     * exceed the image end. Therefore, idx + to_allocate cannot exceed
     * s->bat_size.
     * Note that s->bat_size is an unsigned int, therefore idx + to_allocate
     * will always fit into a uint32_t. */
    assert(idx < s->bat_size && idx + to_allocate <= s->bat_size);

    space = to_allocate * s->tracks;
    len = bdrv_getlength(bs->file->bs);
    if (len < 0) {
        return len;
    }
    if (s->data_end + space > (len >> BDRV_SECTOR_BITS)) {
        space += s->prealloc_size;
        if (s->prealloc_mode == PRL_PREALLOC_MODE_FALLOCATE) {
            ret = bdrv_pwrite_zeroes(bs->file,
                                     s->data_end << BDRV_SECTOR_BITS,
                                     space << BDRV_SECTOR_BITS, 0);
        } else {
            ret = bdrv_truncate(bs->file,
                                (s->data_end + space) << BDRV_SECTOR_BITS,
                                PREALLOC_MODE_OFF, NULL);
        }
        if (ret < 0) {
            return ret;
        }
    }

    /* Try to read from backing to fill empty clusters
     * FIXME: 1. previous write_zeroes may be redundant
     *        2. most of data we read from backing will be rewritten by
     *           parallels_co_writev. On aligned-to-cluster write we do not need
     *           this read at all.
     *        3. it would be good to combine write of data from backing and new
     *           data into one write call */
    if (bs->backing) {
        int64_t nb_cow_sectors = to_allocate * s->tracks;
        int64_t nb_cow_bytes = nb_cow_sectors << BDRV_SECTOR_BITS;
        void *buf = qemu_blockalign(bs, nb_cow_bytes);

        ret = bdrv_co_pread(bs->backing, idx * s->tracks * BDRV_SECTOR_SIZE,
                            nb_cow_bytes, buf, 0);
        if (ret < 0) {
            qemu_vfree(buf);
            return ret;
        }

        ret = bdrv_co_pwritev(bs->file, s->data_end * BDRV_SECTOR_SIZE,
                              nb_cow_bytes, buf, 0);
        qemu_vfree(buf);
        if (ret < 0) {
            return ret;
        }
    }

    for (i = 0; i < to_allocate; i++) {
        s->bat_bitmap[idx + i] = cpu_to_le32(s->data_end / s->off_multiplier);
        s->data_end += s->tracks;
        bitmap_set(s->bat_dirty_bmap,
                   bat_entry_off(idx + i) / s->bat_dirty_block, 1);
    }

    return bat2sect(s, idx) + sector_num % s->tracks;
}


static coroutine_fn int parallels_co_flush_to_os(BlockDriverState *bs)
{
    BDRVParallelsState *s = bs->opaque;
    unsigned long size = DIV_ROUND_UP(s->header_size, s->bat_dirty_block);
    unsigned long bit;

    qemu_co_mutex_lock(&s->lock);

    bit = find_first_bit(s->bat_dirty_bmap, size);
    while (bit < size) {
        uint32_t off = bit * s->bat_dirty_block;
        uint32_t to_write = s->bat_dirty_block;
        int ret;

        if (off + to_write > s->header_size) {
            to_write = s->header_size - off;
        }
        ret = bdrv_pwrite(bs->file, off, (uint8_t *)s->header + off,
                          to_write);
        if (ret < 0) {
            qemu_co_mutex_unlock(&s->lock);
            return ret;
        }
        bit = find_next_bit(s->bat_dirty_bmap, size, bit + 1);
    }
    bitmap_zero(s->bat_dirty_bmap, size);

    qemu_co_mutex_unlock(&s->lock);
    return 0;
}


static int coroutine_fn parallels_co_block_status(BlockDriverState *bs,
                                                  bool want_zero,
                                                  int64_t offset,
                                                  int64_t bytes,
                                                  int64_t *pnum,
                                                  int64_t *map,
                                                  BlockDriverState **file)
{
    BDRVParallelsState *s = bs->opaque;
    int count;

    assert(QEMU_IS_ALIGNED(offset | bytes, BDRV_SECTOR_SIZE));
    qemu_co_mutex_lock(&s->lock);
    offset = block_status(s, offset >> BDRV_SECTOR_BITS,
                          bytes >> BDRV_SECTOR_BITS, &count);
    qemu_co_mutex_unlock(&s->lock);

    *pnum = count * BDRV_SECTOR_SIZE;
    if (offset < 0) {
        return 0;
    }

    *map = offset * BDRV_SECTOR_SIZE;
    *file = bs->file->bs;
    return BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
}

static coroutine_fn int parallels_co_writev(BlockDriverState *bs,
                                            int64_t sector_num, int nb_sectors,
                                            QEMUIOVector *qiov, int flags)
{
    BDRVParallelsState *s = bs->opaque;
    uint64_t bytes_done = 0;
    QEMUIOVector hd_qiov;
    int ret = 0;

    assert(!flags);
    qemu_iovec_init(&hd_qiov, qiov->niov);

    while (nb_sectors > 0) {
        int64_t position;
        int n, nbytes;

        qemu_co_mutex_lock(&s->lock);
        position = allocate_clusters(bs, sector_num, nb_sectors, &n);
        qemu_co_mutex_unlock(&s->lock);
        if (position < 0) {
            ret = (int)position;
            break;
        }

        nbytes = n << BDRV_SECTOR_BITS;

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_concat(&hd_qiov, qiov, bytes_done, nbytes);

        ret = bdrv_co_pwritev(bs->file, position * BDRV_SECTOR_SIZE, nbytes,
                              &hd_qiov, 0);
        if (ret < 0) {
            break;
        }

        nb_sectors -= n;
        sector_num += n;
        bytes_done += nbytes;
    }

    qemu_iovec_destroy(&hd_qiov);
    return ret;
}

static coroutine_fn int parallels_co_readv(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov)
{
    BDRVParallelsState *s = bs->opaque;
    uint64_t bytes_done = 0;
    QEMUIOVector hd_qiov;
    int ret = 0;

    qemu_iovec_init(&hd_qiov, qiov->niov);

    while (nb_sectors > 0) {
        int64_t position;
        int n, nbytes;

        qemu_co_mutex_lock(&s->lock);
        position = block_status(s, sector_num, nb_sectors, &n);
        qemu_co_mutex_unlock(&s->lock);

        nbytes = n << BDRV_SECTOR_BITS;

        qemu_iovec_reset(&hd_qiov);
        qemu_iovec_concat(&hd_qiov, qiov, bytes_done, nbytes);

        if (position < 0) {
            if (bs->backing) {
                ret = bdrv_co_preadv(bs->backing, sector_num * BDRV_SECTOR_SIZE,
                                     nbytes, &hd_qiov, 0);
                if (ret < 0) {
                    break;
                }
            } else {
                qemu_iovec_memset(&hd_qiov, 0, 0, nbytes);
            }
        } else {
            ret = bdrv_co_preadv(bs->file, position * BDRV_SECTOR_SIZE, nbytes,
                                 &hd_qiov, 0);
            if (ret < 0) {
                break;
            }
        }

        nb_sectors -= n;
        sector_num += n;
        bytes_done += nbytes;
    }

    qemu_iovec_destroy(&hd_qiov);
    return ret;
}


static int coroutine_fn parallels_co_check(BlockDriverState *bs,
                                           BdrvCheckResult *res,
                                           BdrvCheckMode fix)
{
    BDRVParallelsState *s = bs->opaque;
    int64_t size, prev_off, high_off;
    int ret;
    uint32_t i;
    bool flush_bat = false;
    int cluster_size = s->tracks << BDRV_SECTOR_BITS;

    size = bdrv_getlength(bs->file->bs);
    if (size < 0) {
        res->check_errors++;
        return size;
    }

    qemu_co_mutex_lock(&s->lock);
    if (s->header_unclean) {
        fprintf(stderr, "%s image was not closed correctly\n",
                fix & BDRV_FIX_ERRORS ? "Repairing" : "ERROR");
        res->corruptions++;
        if (fix & BDRV_FIX_ERRORS) {
            /* parallels_close will do the job right */
            res->corruptions_fixed++;
            s->header_unclean = false;
        }
    }

    res->bfi.total_clusters = s->bat_size;
    res->bfi.compressed_clusters = 0; /* compression is not supported */

    high_off = 0;
    prev_off = 0;
    for (i = 0; i < s->bat_size; i++) {
        int64_t off = bat2sect(s, i) << BDRV_SECTOR_BITS;
        if (off == 0) {
            prev_off = 0;
            continue;
        }

        /* cluster outside the image */
        if (off > size) {
            fprintf(stderr, "%s cluster %u is outside image\n",
                    fix & BDRV_FIX_ERRORS ? "Repairing" : "ERROR", i);
            res->corruptions++;
            if (fix & BDRV_FIX_ERRORS) {
                prev_off = 0;
                s->bat_bitmap[i] = 0;
                res->corruptions_fixed++;
                flush_bat = true;
                continue;
            }
        }

        res->bfi.allocated_clusters++;
        if (off > high_off) {
            high_off = off;
        }

        if (prev_off != 0 && (prev_off + cluster_size) != off) {
            res->bfi.fragmented_clusters++;
        }
        prev_off = off;
    }

    ret = 0;
    if (flush_bat) {
        ret = bdrv_pwrite_sync(bs->file, 0, s->header, s->header_size);
        if (ret < 0) {
            res->check_errors++;
            goto out;
        }
    }

    res->image_end_offset = high_off + cluster_size;
    if (size > res->image_end_offset) {
        int64_t count;
        count = DIV_ROUND_UP(size - res->image_end_offset, cluster_size);
        fprintf(stderr, "%s space leaked at the end of the image %" PRId64 "\n",
                fix & BDRV_FIX_LEAKS ? "Repairing" : "ERROR",
                size - res->image_end_offset);
        res->leaks += count;
        if (fix & BDRV_FIX_LEAKS) {
            Error *local_err = NULL;
            ret = bdrv_truncate(bs->file, res->image_end_offset,
                                PREALLOC_MODE_OFF, &local_err);
            if (ret < 0) {
                error_report_err(local_err);
                res->check_errors++;
                goto out;
            }
            res->leaks_fixed += count;
        }
    }

out:
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}


static int coroutine_fn parallels_co_create(BlockdevCreateOptions* opts,
                                            Error **errp)
{
    BlockdevCreateOptionsParallels *parallels_opts;
    BlockDriverState *bs;
    BlockBackend *blk;
    int64_t total_size, cl_size;
    uint32_t bat_entries, bat_sectors;
    ParallelsHeader header;
    uint8_t tmp[BDRV_SECTOR_SIZE];
    int ret;

    assert(opts->driver == BLOCKDEV_DRIVER_PARALLELS);
    parallels_opts = &opts->u.parallels;

    /* Sanity checks */
    total_size = parallels_opts->size;

    if (parallels_opts->has_cluster_size) {
        cl_size = parallels_opts->cluster_size;
    } else {
        cl_size = DEFAULT_CLUSTER_SIZE;
    }

    /* XXX What is the real limit here? This is an insanely large maximum. */
    if (cl_size >= INT64_MAX / MAX_PARALLELS_IMAGE_FACTOR) {
        error_setg(errp, "Cluster size is too large");
        return -EINVAL;
    }
    if (total_size >= MAX_PARALLELS_IMAGE_FACTOR * cl_size) {
        error_setg(errp, "Image size is too large for this cluster size");
        return -E2BIG;
    }

    if (!QEMU_IS_ALIGNED(total_size, BDRV_SECTOR_SIZE)) {
        error_setg(errp, "Image size must be a multiple of 512 bytes");
        return -EINVAL;
    }

    if (!QEMU_IS_ALIGNED(cl_size, BDRV_SECTOR_SIZE)) {
        error_setg(errp, "Cluster size must be a multiple of 512 bytes");
        return -EINVAL;
    }

    /* Create BlockBackend to write to the image */
    bs = bdrv_open_blockdev_ref(parallels_opts->file, errp);
    if (bs == NULL) {
        return -EIO;
    }

    blk = blk_new(bdrv_get_aio_context(bs),
                  BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL);
    ret = blk_insert_bs(blk, bs, errp);
    if (ret < 0) {
        goto out;
    }
    blk_set_allow_write_beyond_eof(blk, true);

    /* Create image format */
    ret = blk_truncate(blk, 0, PREALLOC_MODE_OFF, errp);
    if (ret < 0) {
        goto out;
    }

    bat_entries = DIV_ROUND_UP(total_size, cl_size);
    bat_sectors = DIV_ROUND_UP(bat_entry_off(bat_entries), cl_size);
    bat_sectors = (bat_sectors *  cl_size) >> BDRV_SECTOR_BITS;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, HEADER_MAGIC2, sizeof(header.magic));
    header.version = cpu_to_le32(HEADER_VERSION);
    /* don't care much about geometry, it is not used on image level */
    header.heads = cpu_to_le32(HEADS_NUMBER);
    header.cylinders = cpu_to_le32(total_size / BDRV_SECTOR_SIZE
                                   / HEADS_NUMBER / SEC_IN_CYL);
    header.tracks = cpu_to_le32(cl_size >> BDRV_SECTOR_BITS);
    header.bat_entries = cpu_to_le32(bat_entries);
    header.nb_sectors = cpu_to_le64(DIV_ROUND_UP(total_size, BDRV_SECTOR_SIZE));
    header.data_off = cpu_to_le32(bat_sectors);

    /* write all the data */
    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp, &header, sizeof(header));

    ret = blk_pwrite(blk, 0, tmp, BDRV_SECTOR_SIZE, 0);
    if (ret < 0) {
        goto exit;
    }
    ret = blk_pwrite_zeroes(blk, BDRV_SECTOR_SIZE,
                            (bat_sectors - 1) << BDRV_SECTOR_BITS, 0);
    if (ret < 0) {
        goto exit;
    }

    ret = 0;
out:
    blk_unref(blk);
    bdrv_unref(bs);
    return ret;

exit:
    error_setg_errno(errp, -ret, "Failed to create Parallels image");
    goto out;
}

static int coroutine_fn parallels_co_create_opts(const char *filename,
                                                 QemuOpts *opts,
                                                 Error **errp)
{
    BlockdevCreateOptions *create_options = NULL;
    Error *local_err = NULL;
    BlockDriverState *bs = NULL;
    QDict *qdict;
    Visitor *v;
    int ret;

    static const QDictRenames opt_renames[] = {
        { BLOCK_OPT_CLUSTER_SIZE,       "cluster-size" },
        { NULL, NULL },
    };

    /* Parse options and convert legacy syntax */
    qdict = qemu_opts_to_qdict_filtered(opts, NULL, &parallels_create_opts,
                                        true);

    if (!qdict_rename_keys(qdict, opt_renames, errp)) {
        ret = -EINVAL;
        goto done;
    }

    /* Create and open the file (protocol layer) */
    ret = bdrv_create_file(filename, opts, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto done;
    }

    bs = bdrv_open(filename, NULL, NULL,
                   BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL, errp);
    if (bs == NULL) {
        ret = -EIO;
        goto done;
    }

    /* Now get the QAPI type BlockdevCreateOptions */
    qdict_put_str(qdict, "driver", "parallels");
    qdict_put_str(qdict, "file", bs->node_name);

    v = qobject_input_visitor_new_flat_confused(qdict, errp);
    if (!v) {
        ret = -EINVAL;
        goto done;
    }

    visit_type_BlockdevCreateOptions(v, NULL, &create_options, &local_err);
    visit_free(v);

    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto done;
    }

    /* Silently round up sizes */
    create_options->u.parallels.size =
        ROUND_UP(create_options->u.parallels.size, BDRV_SECTOR_SIZE);
    create_options->u.parallels.cluster_size =
        ROUND_UP(create_options->u.parallels.cluster_size, BDRV_SECTOR_SIZE);

    /* Create the Parallels image (format layer) */
    ret = parallels_co_create(create_options, errp);
    if (ret < 0) {
        goto done;
    }
    ret = 0;

done:
    qobject_unref(qdict);
    bdrv_unref(bs);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}


static int parallels_probe(const uint8_t *buf, int buf_size,
                           const char *filename)
{
    const ParallelsHeader *ph = (const void *)buf;

    if (buf_size < sizeof(ParallelsHeader)) {
        return 0;
    }

    if ((!memcmp(ph->magic, HEADER_MAGIC, 16) ||
           !memcmp(ph->magic, HEADER_MAGIC2, 16)) &&
           (le32_to_cpu(ph->version) == HEADER_VERSION)) {
        return 100;
    }

    return 0;
}

static int parallels_update_header(BlockDriverState *bs)
{
    BDRVParallelsState *s = bs->opaque;
    unsigned size = MAX(bdrv_opt_mem_align(bs->file->bs),
                        sizeof(ParallelsHeader));

    if (size > s->header_size) {
        size = s->header_size;
    }
    return bdrv_pwrite_sync(bs->file, 0, s->header, size);
}

static int parallels_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    BDRVParallelsState *s = bs->opaque;
    ParallelsHeader ph;
    int ret, size, i;
    QemuOpts *opts = NULL;
    Error *local_err = NULL;
    char *buf;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    ret = bdrv_pread(bs->file, 0, &ph, sizeof(ph));
    if (ret < 0) {
        goto fail;
    }

    bs->total_sectors = le64_to_cpu(ph.nb_sectors);

    if (le32_to_cpu(ph.version) != HEADER_VERSION) {
        goto fail_format;
    }
    if (!memcmp(ph.magic, HEADER_MAGIC, 16)) {
        s->off_multiplier = 1;
        bs->total_sectors = 0xffffffff & bs->total_sectors;
    } else if (!memcmp(ph.magic, HEADER_MAGIC2, 16)) {
        s->off_multiplier = le32_to_cpu(ph.tracks);
    } else {
        goto fail_format;
    }

    s->tracks = le32_to_cpu(ph.tracks);
    if (s->tracks == 0) {
        error_setg(errp, "Invalid image: Zero sectors per track");
        ret = -EINVAL;
        goto fail;
    }
    if (s->tracks > INT32_MAX/513) {
        error_setg(errp, "Invalid image: Too big cluster");
        ret = -EFBIG;
        goto fail;
    }

    s->bat_size = le32_to_cpu(ph.bat_entries);
    if (s->bat_size > INT_MAX / sizeof(uint32_t)) {
        error_setg(errp, "Catalog too large");
        ret = -EFBIG;
        goto fail;
    }

    size = bat_entry_off(s->bat_size);
    s->header_size = ROUND_UP(size, bdrv_opt_mem_align(bs->file->bs));
    s->header = qemu_try_blockalign(bs->file->bs, s->header_size);
    if (s->header == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    s->data_end = le32_to_cpu(ph.data_off);
    if (s->data_end == 0) {
        s->data_end = ROUND_UP(bat_entry_off(s->bat_size), BDRV_SECTOR_SIZE);
    }
    if (s->data_end < s->header_size) {
        /* there is not enough unused space to fit to block align between BAT
           and actual data. We can't avoid read-modify-write... */
        s->header_size = size;
    }

    ret = bdrv_pread(bs->file, 0, s->header, s->header_size);
    if (ret < 0) {
        goto fail;
    }
    s->bat_bitmap = (uint32_t *)(s->header + 1);

    for (i = 0; i < s->bat_size; i++) {
        int64_t off = bat2sect(s, i);
        if (off >= s->data_end) {
            s->data_end = off + s->tracks;
        }
    }

    if (le32_to_cpu(ph.inuse) == HEADER_INUSE_MAGIC) {
        /* Image was not closed correctly. The check is mandatory */
        s->header_unclean = true;
        if ((flags & BDRV_O_RDWR) && !(flags & BDRV_O_CHECK)) {
            error_setg(errp, "parallels: Image was not closed correctly; "
                       "cannot be opened read/write");
            ret = -EACCES;
            goto fail;
        }
    }

    opts = qemu_opts_create(&parallels_runtime_opts, NULL, 0, &local_err);
    if (local_err != NULL) {
        goto fail_options;
    }

    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err != NULL) {
        goto fail_options;
    }

    s->prealloc_size =
        qemu_opt_get_size_del(opts, PARALLELS_OPT_PREALLOC_SIZE, 0);
    s->prealloc_size = MAX(s->tracks, s->prealloc_size >> BDRV_SECTOR_BITS);
    buf = qemu_opt_get_del(opts, PARALLELS_OPT_PREALLOC_MODE);
    s->prealloc_mode = qapi_enum_parse(&prealloc_mode_lookup, buf,
                                       PRL_PREALLOC_MODE_FALLOCATE,
                                       &local_err);
    g_free(buf);
    if (local_err != NULL) {
        goto fail_options;
    }

    if (!bdrv_has_zero_init(bs->file->bs)) {
        s->prealloc_mode = PRL_PREALLOC_MODE_FALLOCATE;
    }

    if ((flags & BDRV_O_RDWR) && !(flags & BDRV_O_INACTIVE)) {
        s->header->inuse = cpu_to_le32(HEADER_INUSE_MAGIC);
        ret = parallels_update_header(bs);
        if (ret < 0) {
            goto fail;
        }
    }

    s->bat_dirty_block = 4 * getpagesize();
    s->bat_dirty_bmap =
        bitmap_new(DIV_ROUND_UP(s->header_size, s->bat_dirty_block));

    /* Disable migration until bdrv_invalidate_cache method is added */
    error_setg(&s->migration_blocker, "The Parallels format used by node '%s' "
               "does not support live migration",
               bdrv_get_device_or_node_name(bs));
    ret = migrate_add_blocker(s->migration_blocker, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_free(s->migration_blocker);
        goto fail;
    }
    qemu_co_mutex_init(&s->lock);
    return 0;

fail_format:
    error_setg(errp, "Image not in Parallels format");
    ret = -EINVAL;
fail:
    qemu_vfree(s->header);
    return ret;

fail_options:
    error_propagate(errp, local_err);
    ret = -EINVAL;
    goto fail;
}


static void parallels_close(BlockDriverState *bs)
{
    BDRVParallelsState *s = bs->opaque;

    if ((bs->open_flags & BDRV_O_RDWR) && !(bs->open_flags & BDRV_O_INACTIVE)) {
        s->header->inuse = 0;
        parallels_update_header(bs);
        bdrv_truncate(bs->file, s->data_end << BDRV_SECTOR_BITS,
                      PREALLOC_MODE_OFF, NULL);
    }

    g_free(s->bat_dirty_bmap);
    qemu_vfree(s->header);

    migrate_del_blocker(s->migration_blocker);
    error_free(s->migration_blocker);
}

static BlockDriver bdrv_parallels = {
    .format_name	= "parallels",
    .instance_size	= sizeof(BDRVParallelsState),
    .bdrv_probe		= parallels_probe,
    .bdrv_open		= parallels_open,
    .bdrv_close		= parallels_close,
    .bdrv_child_perm          = bdrv_format_default_perms,
    .bdrv_co_block_status     = parallels_co_block_status,
    .bdrv_has_zero_init       = bdrv_has_zero_init_1,
    .bdrv_co_flush_to_os      = parallels_co_flush_to_os,
    .bdrv_co_readv  = parallels_co_readv,
    .bdrv_co_writev = parallels_co_writev,
    .supports_backing = true,
    .bdrv_co_create      = parallels_co_create,
    .bdrv_co_create_opts = parallels_co_create_opts,
    .bdrv_co_check  = parallels_co_check,
    .create_opts    = &parallels_create_opts,
};

static void bdrv_parallels_init(void)
{
    bdrv_register(&bdrv_parallels);
}

block_init(bdrv_parallels_init);
