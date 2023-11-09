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
#include "qemu/error-report.h"
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
#include "qemu/memalign.h"
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

static uint32_t host_cluster_index(BDRVParallelsState *s, int64_t off)
{
    off -= s->data_start << BDRV_SECTOR_BITS;
    return off / s->cluster_size;
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

static void parallels_set_bat_entry(BDRVParallelsState *s,
                                    uint32_t index, uint32_t offset)
{
    s->bat_bitmap[index] = cpu_to_le32(offset);
    bitmap_set(s->bat_dirty_bmap, bat_entry_off(index) / s->bat_dirty_block, 1);
}

static int mark_used(BlockDriverState *bs, unsigned long *bitmap,
                     uint32_t bitmap_size, int64_t off, uint32_t count)
{
    BDRVParallelsState *s = bs->opaque;
    uint32_t cluster_index = host_cluster_index(s, off);
    unsigned long next_used;
    if (cluster_index + count > bitmap_size) {
        return -E2BIG;
    }
    next_used = find_next_bit(bitmap, bitmap_size, cluster_index);
    if (next_used < cluster_index + count) {
        return -EBUSY;
    }
    bitmap_set(bitmap, cluster_index, count);
    return 0;
}

/*
 * Collect used bitmap. The image can contain errors, we should fill the
 * bitmap anyway, as much as we can. This information will be used for
 * error resolution.
 */
static int GRAPH_RDLOCK parallels_fill_used_bitmap(BlockDriverState *bs)
{
    BDRVParallelsState *s = bs->opaque;
    int64_t payload_bytes;
    uint32_t i;
    int err = 0;

    payload_bytes = bdrv_getlength(bs->file->bs);
    if (payload_bytes < 0) {
        return payload_bytes;
    }
    payload_bytes -= s->data_start * BDRV_SECTOR_SIZE;
    if (payload_bytes < 0) {
        return -EINVAL;
    }

    s->used_bmap_size = DIV_ROUND_UP(payload_bytes, s->cluster_size);
    if (s->used_bmap_size == 0) {
        return 0;
    }
    s->used_bmap = bitmap_try_new(s->used_bmap_size);
    if (s->used_bmap == NULL) {
        return -ENOMEM;
    }

    for (i = 0; i < s->bat_size; i++) {
        int err2;
        int64_t host_off = bat2sect(s, i) << BDRV_SECTOR_BITS;
        if (host_off == 0) {
            continue;
        }

        err2 = mark_used(bs, s->used_bmap, s->used_bmap_size, host_off, 1);
        if (err2 < 0 && err == 0) {
            err = err2;
        }
    }
    return err;
}

static void parallels_free_used_bitmap(BlockDriverState *bs)
{
    BDRVParallelsState *s = bs->opaque;
    s->used_bmap_size = 0;
    g_free(s->used_bmap);
}

static int64_t coroutine_fn GRAPH_RDLOCK
allocate_clusters(BlockDriverState *bs, int64_t sector_num,
                  int nb_sectors, int *pnum)
{
    int ret = 0;
    BDRVParallelsState *s = bs->opaque;
    int64_t i, pos, idx, to_allocate, first_free, host_off;

    pos = block_status(s, sector_num, nb_sectors, pnum);
    if (pos > 0) {
        return pos;
    }

    idx = sector_num / s->tracks;
    to_allocate = DIV_ROUND_UP(sector_num + *pnum, s->tracks) - idx;

    /*
     * This function is called only by parallels_co_writev(), which will never
     * pass a sector_num at or beyond the end of the image (because the block
     * layer never passes such a sector_num to that function). Therefore, idx
     * is always below s->bat_size.
     * block_status() will limit *pnum so that sector_num + *pnum will not
     * exceed the image end. Therefore, idx + to_allocate cannot exceed
     * s->bat_size.
     * Note that s->bat_size is an unsigned int, therefore idx + to_allocate
     * will always fit into a uint32_t.
     */
    assert(idx < s->bat_size && idx + to_allocate <= s->bat_size);

    first_free = find_first_zero_bit(s->used_bmap, s->used_bmap_size);
    if (first_free == s->used_bmap_size) {
        uint32_t new_usedsize;
        int64_t bytes = to_allocate * s->cluster_size;
        bytes += s->prealloc_size * BDRV_SECTOR_SIZE;

        host_off = s->data_end * BDRV_SECTOR_SIZE;

        /*
         * We require the expanded size to read back as zero. If the
         * user permitted truncation, we try that; but if it fails, we
         * force the safer-but-slower fallocate.
         */
        if (s->prealloc_mode == PRL_PREALLOC_MODE_TRUNCATE) {
            ret = bdrv_co_truncate(bs->file, host_off + bytes,
                                   false, PREALLOC_MODE_OFF,
                                   BDRV_REQ_ZERO_WRITE, NULL);
            if (ret == -ENOTSUP) {
                s->prealloc_mode = PRL_PREALLOC_MODE_FALLOCATE;
            }
        }
        if (s->prealloc_mode == PRL_PREALLOC_MODE_FALLOCATE) {
            ret = bdrv_co_pwrite_zeroes(bs->file, host_off, bytes, 0);
        }
        if (ret < 0) {
            return ret;
        }

        new_usedsize = s->used_bmap_size + bytes / s->cluster_size;
        s->used_bmap = bitmap_zero_extend(s->used_bmap, s->used_bmap_size,
                                          new_usedsize);
        s->used_bmap_size = new_usedsize;
    } else {
        int64_t next_used;
        next_used = find_next_bit(s->used_bmap, s->used_bmap_size, first_free);

        /* Not enough continuous clusters in the middle, adjust the size */
        if (next_used - first_free < to_allocate) {
            to_allocate = next_used - first_free;
            *pnum = (idx + to_allocate) * s->tracks - sector_num;
        }

        host_off = s->data_start * BDRV_SECTOR_SIZE;
        host_off += first_free * s->cluster_size;

        /*
         * No need to preallocate if we are using tail area from the above
         * branch. In the other case we are likely re-using hole. Preallocate
         * the space if required by the prealloc_mode.
         */
        if (s->prealloc_mode == PRL_PREALLOC_MODE_FALLOCATE &&
                host_off < s->data_end * BDRV_SECTOR_SIZE) {
            ret = bdrv_co_pwrite_zeroes(bs->file, host_off,
                                        s->cluster_size * to_allocate, 0);
            if (ret < 0) {
                return ret;
            }
        }
    }

    /*
     * Try to read from backing to fill empty clusters
     * FIXME: 1. previous write_zeroes may be redundant
     *        2. most of data we read from backing will be rewritten by
     *           parallels_co_writev. On aligned-to-cluster write we do not need
     *           this read at all.
     *        3. it would be good to combine write of data from backing and new
     *           data into one write call.
     */
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

        ret = bdrv_co_pwrite(bs->file, s->data_end * BDRV_SECTOR_SIZE,
                             nb_cow_bytes, buf, 0);
        qemu_vfree(buf);
        if (ret < 0) {
            return ret;
        }
    }

    ret = mark_used(bs, s->used_bmap, s->used_bmap_size, host_off, to_allocate);
    if (ret < 0) {
        /* Image consistency is broken. Alarm! */
        return ret;
    }
    for (i = 0; i < to_allocate; i++) {
        parallels_set_bat_entry(s, idx + i,
                host_off / BDRV_SECTOR_SIZE / s->off_multiplier);
        host_off += s->cluster_size;
    }
    if (host_off > s->data_end * BDRV_SECTOR_SIZE) {
        s->data_end = host_off / BDRV_SECTOR_SIZE;
    }

    return bat2sect(s, idx) + sector_num % s->tracks;
}


static int coroutine_fn GRAPH_RDLOCK
parallels_co_flush_to_os(BlockDriverState *bs)
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
        ret = bdrv_co_pwrite(bs->file, off, to_write,
                             (uint8_t *)s->header + off, 0);
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

static int coroutine_fn GRAPH_RDLOCK
parallels_co_block_status(BlockDriverState *bs, bool want_zero, int64_t offset,
                          int64_t bytes, int64_t *pnum, int64_t *map,
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

static int coroutine_fn GRAPH_RDLOCK
parallels_co_writev(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                    QEMUIOVector *qiov, int flags)
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

static int coroutine_fn GRAPH_RDLOCK
parallels_co_readv(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                   QEMUIOVector *qiov)
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


static int coroutine_fn GRAPH_RDLOCK
parallels_co_pdiscard(BlockDriverState *bs, int64_t offset, int64_t bytes)
{
    int ret = 0;
    uint32_t cluster, count;
    BDRVParallelsState *s = bs->opaque;

    /*
     * The image does not support ZERO mark inside the BAT, which means that
     * stale data could be exposed from the backing file.
     */
    if (bs->backing) {
        return -ENOTSUP;
    }

    if (!QEMU_IS_ALIGNED(offset, s->cluster_size)) {
        return -ENOTSUP;
    } else if (!QEMU_IS_ALIGNED(bytes, s->cluster_size)) {
        return -ENOTSUP;
    }

    cluster = offset / s->cluster_size;
    count = bytes / s->cluster_size;

    qemu_co_mutex_lock(&s->lock);
    for (; count > 0; cluster++, count--) {
        int64_t host_off = bat2sect(s, cluster) << BDRV_SECTOR_BITS;
        if (host_off == 0) {
            continue;
        }

        ret = bdrv_co_pdiscard(bs->file, host_off, s->cluster_size);
        if (ret < 0) {
            goto done;
        }

        parallels_set_bat_entry(s, cluster, 0);
        bitmap_clear(s->used_bmap, host_cluster_index(s, host_off), 1);
    }
done:
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
parallels_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset, int64_t bytes,
                           BdrvRequestFlags flags)
{
    /*
     * The zero flag is missed in the Parallels format specification. We can
     * resort to discard if we have no backing file (this condition is checked
     * inside parallels_co_pdiscard().
     */
    return parallels_co_pdiscard(bs, offset, bytes);
}


static void parallels_check_unclean(BlockDriverState *bs,
                                    BdrvCheckResult *res,
                                    BdrvCheckMode fix)
{
    BDRVParallelsState *s = bs->opaque;

    if (!s->header_unclean) {
        return;
    }

    fprintf(stderr, "%s image was not closed correctly\n",
            fix & BDRV_FIX_ERRORS ? "Repairing" : "ERROR");
    res->corruptions++;
    if (fix & BDRV_FIX_ERRORS) {
        /* parallels_close will do the job right */
        res->corruptions_fixed++;
        s->header_unclean = false;
    }
}

/*
 * Returns true if data_off is correct, otherwise false. In both cases
 * correct_offset is set to the proper value.
 */
static bool parallels_test_data_off(BDRVParallelsState *s,
                                    int64_t file_nb_sectors,
                                    uint32_t *correct_offset)
{
    uint32_t data_off, min_off;
    bool old_magic;

    /*
     * There are two slightly different image formats: with "WithoutFreeSpace"
     * or "WithouFreSpacExt" magic words. Call the first one as "old magic".
     * In such images data_off field can be zero. In this case the offset is
     * calculated as the end of BAT table plus some padding to ensure sector
     * size alignment.
     */
    old_magic = !memcmp(s->header->magic, HEADER_MAGIC, 16);

    min_off = DIV_ROUND_UP(bat_entry_off(s->bat_size), BDRV_SECTOR_SIZE);
    if (!old_magic) {
        min_off = ROUND_UP(min_off, s->cluster_size / BDRV_SECTOR_SIZE);
    }

    if (correct_offset) {
        *correct_offset = min_off;
    }

    data_off = le32_to_cpu(s->header->data_off);
    if (data_off == 0 && old_magic) {
        return true;
    }

    if (data_off < min_off || data_off > file_nb_sectors) {
        return false;
    }

    if (correct_offset) {
        *correct_offset = data_off;
    }

    return true;
}

static int coroutine_fn GRAPH_RDLOCK
parallels_check_data_off(BlockDriverState *bs, BdrvCheckResult *res,
                         BdrvCheckMode fix)
{
    BDRVParallelsState *s = bs->opaque;
    int64_t file_size;
    uint32_t data_off;

    file_size = bdrv_co_nb_sectors(bs->file->bs);
    if (file_size < 0) {
        res->check_errors++;
        return file_size;
    }

    if (parallels_test_data_off(s, file_size, &data_off)) {
        return 0;
    }

    res->corruptions++;
    if (fix & BDRV_FIX_ERRORS) {
        int err;
        s->header->data_off = cpu_to_le32(data_off);
        s->data_start = data_off;

        parallels_free_used_bitmap(bs);
        err = parallels_fill_used_bitmap(bs);
        if (err == -ENOMEM) {
            res->check_errors++;
            return err;
        }

        res->corruptions_fixed++;
    }

    fprintf(stderr, "%s data_off field has incorrect value\n",
            fix & BDRV_FIX_ERRORS ? "Repairing" : "ERROR");

    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
parallels_check_outside_image(BlockDriverState *bs, BdrvCheckResult *res,
                              BdrvCheckMode fix)
{
    BDRVParallelsState *s = bs->opaque;
    uint32_t i;
    int64_t off, high_off, size;

    size = bdrv_co_getlength(bs->file->bs);
    if (size < 0) {
        res->check_errors++;
        return size;
    }

    high_off = 0;
    for (i = 0; i < s->bat_size; i++) {
        off = bat2sect(s, i) << BDRV_SECTOR_BITS;
        if (off + s->cluster_size > size) {
            fprintf(stderr, "%s cluster %u is outside image\n",
                    fix & BDRV_FIX_ERRORS ? "Repairing" : "ERROR", i);
            res->corruptions++;
            if (fix & BDRV_FIX_ERRORS) {
                parallels_set_bat_entry(s, i, 0);
                res->corruptions_fixed++;
            }
            continue;
        }
        if (high_off < off) {
            high_off = off;
        }
    }

    if (high_off == 0) {
        res->image_end_offset = s->data_end << BDRV_SECTOR_BITS;
    } else {
        res->image_end_offset = high_off + s->cluster_size;
        s->data_end = res->image_end_offset >> BDRV_SECTOR_BITS;
    }

    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
parallels_check_leak(BlockDriverState *bs, BdrvCheckResult *res,
                     BdrvCheckMode fix, bool explicit)
{
    BDRVParallelsState *s = bs->opaque;
    int64_t size;
    int ret;

    size = bdrv_co_getlength(bs->file->bs);
    if (size < 0) {
        res->check_errors++;
        return size;
    }

    if (size > res->image_end_offset) {
        int64_t count;
        count = DIV_ROUND_UP(size - res->image_end_offset, s->cluster_size);
        if (explicit) {
            fprintf(stderr,
                    "%s space leaked at the end of the image %" PRId64 "\n",
                    fix & BDRV_FIX_LEAKS ? "Repairing" : "ERROR",
                    size - res->image_end_offset);
            res->leaks += count;
        }
        if (fix & BDRV_FIX_LEAKS) {
            Error *local_err = NULL;

            /*
             * In order to really repair the image, we must shrink it.
             * That means we have to pass exact=true.
             */
            ret = bdrv_co_truncate(bs->file, res->image_end_offset, true,
                                   PREALLOC_MODE_OFF, 0, &local_err);
            if (ret < 0) {
                error_report_err(local_err);
                res->check_errors++;
                return ret;
            }
            if (explicit) {
                res->leaks_fixed += count;
            }
        }
    }

    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
parallels_check_duplicate(BlockDriverState *bs, BdrvCheckResult *res,
                          BdrvCheckMode fix)
{
    BDRVParallelsState *s = bs->opaque;
    int64_t host_off, host_sector, guest_sector;
    unsigned long *bitmap;
    uint32_t i, bitmap_size, bat_entry;
    int n, ret = 0;
    uint64_t *buf = NULL;
    bool fixed = false;

    /*
     * Create a bitmap of used clusters.
     * If a bit is set, there is a BAT entry pointing to this cluster.
     * Loop through the BAT entries, check bits relevant to an entry offset.
     * If bit is set, this entry is duplicated. Otherwise set the bit.
     *
     * We shouldn't worry about newly allocated clusters outside the image
     * because they are created higher then any existing cluster pointed by
     * a BAT entry.
     */
    bitmap_size = host_cluster_index(s, res->image_end_offset);
    if (bitmap_size == 0) {
        return 0;
    }
    if (res->image_end_offset % s->cluster_size) {
        /* A not aligned image end leads to a bitmap shorter by 1 */
        bitmap_size++;
    }

    bitmap = bitmap_new(bitmap_size);

    buf = qemu_blockalign(bs, s->cluster_size);

    for (i = 0; i < s->bat_size; i++) {
        host_off = bat2sect(s, i) << BDRV_SECTOR_BITS;
        if (host_off == 0) {
            continue;
        }

        ret = mark_used(bs, bitmap, bitmap_size, host_off, 1);
        assert(ret != -E2BIG);
        if (ret == 0) {
            continue;
        }

        /* this cluster duplicates another one */
        fprintf(stderr, "%s duplicate offset in BAT entry %u\n",
                fix & BDRV_FIX_ERRORS ? "Repairing" : "ERROR", i);

        res->corruptions++;

        if (!(fix & BDRV_FIX_ERRORS)) {
            continue;
        }

        /*
         * Reset the entry and allocate a new cluster
         * for the relevant guest offset. In this way we let
         * the lower layer to place the new cluster properly.
         * Copy the original cluster to the allocated one.
         * But before save the old offset value for repairing
         * if we have an error.
         */
        bat_entry = s->bat_bitmap[i];
        parallels_set_bat_entry(s, i, 0);

        ret = bdrv_co_pread(bs->file, host_off, s->cluster_size, buf, 0);
        if (ret < 0) {
            res->check_errors++;
            goto out_repair_bat;
        }

        guest_sector = (i * (int64_t)s->cluster_size) >> BDRV_SECTOR_BITS;
        host_sector = allocate_clusters(bs, guest_sector, s->tracks, &n);
        if (host_sector < 0) {
            res->check_errors++;
            goto out_repair_bat;
        }
        host_off = host_sector << BDRV_SECTOR_BITS;

        ret = bdrv_co_pwrite(bs->file, host_off, s->cluster_size, buf, 0);
        if (ret < 0) {
            res->check_errors++;
            goto out_repair_bat;
        }

        if (host_off + s->cluster_size > res->image_end_offset) {
            res->image_end_offset = host_off + s->cluster_size;
        }

        /*
         * In the future allocate_cluster() will reuse holed offsets
         * inside the image. Keep the used clusters bitmap content
         * consistent for the new allocated clusters too.
         *
         * Note, clusters allocated outside the current image are not
         * considered, and the bitmap size doesn't change. This specifically
         * means that -E2BIG is OK.
         */
        ret = mark_used(bs, bitmap, bitmap_size, host_off, 1);
        if (ret == -EBUSY) {
            res->check_errors++;
            goto out_repair_bat;
        }

        fixed = true;
        res->corruptions_fixed++;

    }

    if (fixed) {
        /*
         * When new clusters are allocated, the file size increases by
         * 128 Mb. We need to truncate the file to the right size. Let
         * the leak fix code make its job without res changing.
         */
        ret = parallels_check_leak(bs, res, fix, false);
    }

out_free:
    g_free(buf);
    g_free(bitmap);
    return ret;
/*
 * We can get here only from places where index and old_offset have
 * meaningful values.
 */
out_repair_bat:
    s->bat_bitmap[i] = bat_entry;
    goto out_free;
}

static void parallels_collect_statistics(BlockDriverState *bs,
                                         BdrvCheckResult *res,
                                         BdrvCheckMode fix)
{
    BDRVParallelsState *s = bs->opaque;
    int64_t off, prev_off;
    uint32_t i;

    res->bfi.total_clusters = s->bat_size;
    res->bfi.compressed_clusters = 0; /* compression is not supported */

    prev_off = 0;
    for (i = 0; i < s->bat_size; i++) {
        off = bat2sect(s, i) << BDRV_SECTOR_BITS;
        /*
         * If BDRV_FIX_ERRORS is not set, out-of-image BAT entries were not
         * fixed. Skip not allocated and out-of-image BAT entries.
         */
        if (off == 0 || off + s->cluster_size > res->image_end_offset) {
            prev_off = 0;
            continue;
        }

        if (prev_off != 0 && (prev_off + s->cluster_size) != off) {
            res->bfi.fragmented_clusters++;
        }
        prev_off = off;
        res->bfi.allocated_clusters++;
    }
}

static int coroutine_fn GRAPH_RDLOCK
parallels_co_check(BlockDriverState *bs, BdrvCheckResult *res,
                   BdrvCheckMode fix)
{
    BDRVParallelsState *s = bs->opaque;
    int ret;

    WITH_QEMU_LOCK_GUARD(&s->lock) {
        parallels_check_unclean(bs, res, fix);

        ret = parallels_check_data_off(bs, res, fix);
        if (ret < 0) {
            return ret;
        }

        ret = parallels_check_outside_image(bs, res, fix);
        if (ret < 0) {
            return ret;
        }

        ret = parallels_check_leak(bs, res, fix, true);
        if (ret < 0) {
            return ret;
        }

        ret = parallels_check_duplicate(bs, res, fix);
        if (ret < 0) {
            return ret;
        }

        parallels_collect_statistics(bs, res, fix);
    }

    ret = bdrv_co_flush(bs);
    if (ret < 0) {
        res->check_errors++;
    }

    return ret;
}


static int coroutine_fn GRAPH_UNLOCKED
parallels_co_create(BlockdevCreateOptions* opts, Error **errp)
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
    bs = bdrv_co_open_blockdev_ref(parallels_opts->file, errp);
    if (bs == NULL) {
        return -EIO;
    }

    blk = blk_co_new_with_bs(bs, BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL,
                             errp);
    if (!blk) {
        ret = -EPERM;
        goto out;
    }
    blk_set_allow_write_beyond_eof(blk, true);

    /* Create image format */
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

    ret = blk_co_pwrite(blk, 0, BDRV_SECTOR_SIZE, tmp, 0);
    if (ret < 0) {
        goto exit;
    }
    ret = blk_co_pwrite_zeroes(blk, BDRV_SECTOR_SIZE,
                               (bat_sectors - 1) << BDRV_SECTOR_BITS, 0);
    if (ret < 0) {
        goto exit;
    }

    ret = 0;
out:
    blk_co_unref(blk);
    bdrv_co_unref(bs);
    return ret;

exit:
    error_setg_errno(errp, -ret, "Failed to create Parallels image");
    goto out;
}

static int coroutine_fn GRAPH_UNLOCKED
parallels_co_create_opts(BlockDriver *drv, const char *filename,
                         QemuOpts *opts, Error **errp)
{
    BlockdevCreateOptions *create_options = NULL;
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
    ret = bdrv_co_create_file(filename, opts, errp);
    if (ret < 0) {
        goto done;
    }

    bs = bdrv_co_open(filename, NULL, NULL,
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

    visit_type_BlockdevCreateOptions(v, NULL, &create_options, errp);
    visit_free(v);
    if (!create_options) {
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
    bdrv_co_unref(bs);
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

static int GRAPH_RDLOCK parallels_update_header(BlockDriverState *bs)
{
    BDRVParallelsState *s = bs->opaque;
    unsigned size = MAX(bdrv_opt_mem_align(bs->file->bs),
                        sizeof(ParallelsHeader));

    if (size > s->header_size) {
        size = s->header_size;
    }
    return bdrv_pwrite_sync(bs->file, 0, size, s->header, 0);
}


static int parallels_opts_prealloc(BlockDriverState *bs, QDict *options,
                                   Error **errp)
{
    int err;
    char *buf;
    int64_t bytes;
    BDRVParallelsState *s = bs->opaque;
    Error *local_err = NULL;
    QemuOpts *opts = qemu_opts_create(&parallels_runtime_opts, NULL, 0, errp);
    if (!opts) {
        return -ENOMEM;
    }

    err = -EINVAL;
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        goto done;
    }

    bytes = qemu_opt_get_size_del(opts, PARALLELS_OPT_PREALLOC_SIZE, 0);
    s->prealloc_size = bytes >> BDRV_SECTOR_BITS;
    buf = qemu_opt_get_del(opts, PARALLELS_OPT_PREALLOC_MODE);
    /* prealloc_mode can be downgraded later during allocate_clusters */
    s->prealloc_mode = qapi_enum_parse(&prealloc_mode_lookup, buf,
                                       PRL_PREALLOC_MODE_FALLOCATE,
                                       &local_err);
    g_free(buf);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        goto done;
    }
    err = 0;

done:
    qemu_opts_del(opts);
    return err;
}

static int parallels_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    BDRVParallelsState *s = bs->opaque;
    ParallelsHeader ph;
    int ret, size, i;
    int64_t file_nb_sectors, sector;
    uint32_t data_start;
    bool need_check = false;

    ret = parallels_opts_prealloc(bs, options, errp);
    if (ret < 0) {
        return ret;
    }

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    file_nb_sectors = bdrv_nb_sectors(bs->file->bs);
    if (file_nb_sectors < 0) {
        return -EINVAL;
    }

    ret = bdrv_pread(bs->file, 0, sizeof(ph), &ph, 0);
    if (ret < 0) {
        return ret;
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
        return -EINVAL;
    }
    if (s->tracks > INT32_MAX/513) {
        error_setg(errp, "Invalid image: Too big cluster");
        return -EFBIG;
    }
    s->prealloc_size = MAX(s->tracks, s->prealloc_size);
    s->cluster_size = s->tracks << BDRV_SECTOR_BITS;

    s->bat_size = le32_to_cpu(ph.bat_entries);
    if (s->bat_size > INT_MAX / sizeof(uint32_t)) {
        error_setg(errp, "Catalog too large");
        return -EFBIG;
    }

    size = bat_entry_off(s->bat_size);
    s->header_size = ROUND_UP(size, bdrv_opt_mem_align(bs->file->bs));
    s->header = qemu_try_blockalign(bs->file->bs, s->header_size);
    if (s->header == NULL) {
        return -ENOMEM;
    }

    ret = bdrv_pread(bs->file, 0, s->header_size, s->header, 0);
    if (ret < 0) {
        goto fail;
    }
    s->bat_bitmap = (uint32_t *)(s->header + 1);

    if (le32_to_cpu(ph.inuse) == HEADER_INUSE_MAGIC) {
        need_check = s->header_unclean = true;
    }

    {
        bool ok = parallels_test_data_off(s, file_nb_sectors, &data_start);
        need_check = need_check || !ok;
    }

    s->data_start = data_start;
    s->data_end = s->data_start;
    if (s->data_end < (s->header_size >> BDRV_SECTOR_BITS)) {
        /*
         * There is not enough unused space to fit to block align between BAT
         * and actual data. We can't avoid read-modify-write...
         */
        s->header_size = size;
    }

    if (ph.ext_off) {
        if (flags & BDRV_O_RDWR) {
            /*
             * It's unsafe to open image RW if there is an extension (as we
             * don't support it). But parallels driver in QEMU historically
             * ignores the extension, so print warning and don't care.
             */
            warn_report("Format Extension ignored in RW mode");
        } else {
            ret = parallels_read_format_extension(
                    bs, le64_to_cpu(ph.ext_off) << BDRV_SECTOR_BITS, errp);
            if (ret < 0) {
                goto fail;
            }
        }
    }

    if ((flags & BDRV_O_RDWR) && !(flags & BDRV_O_INACTIVE)) {
        s->header->inuse = cpu_to_le32(HEADER_INUSE_MAGIC);
        ret = parallels_update_header(bs);
        if (ret < 0) {
            goto fail;
        }
    }

    s->bat_dirty_block = 4 * qemu_real_host_page_size();
    s->bat_dirty_bmap =
        bitmap_new(DIV_ROUND_UP(s->header_size, s->bat_dirty_block));

    /* Disable migration until bdrv_activate method is added */
    error_setg(&s->migration_blocker, "The Parallels format used by node '%s' "
               "does not support live migration",
               bdrv_get_device_or_node_name(bs));

    ret = migrate_add_blocker_normal(&s->migration_blocker, errp);
    if (ret < 0) {
        goto fail;
    }
    qemu_co_mutex_init(&s->lock);

    for (i = 0; i < s->bat_size; i++) {
        sector = bat2sect(s, i);
        if (sector + s->tracks > s->data_end) {
            s->data_end = sector + s->tracks;
        }
    }
    need_check = need_check || s->data_end > file_nb_sectors;

    if (!need_check) {
        ret = parallels_fill_used_bitmap(bs);
        if (ret == -ENOMEM) {
            goto fail;
        }
        need_check = need_check || ret < 0; /* These are correctable errors */
    }

    /*
     * We don't repair the image here if it's opened for checks. Also we don't
     * want to change inactive images and can't change readonly images.
     */
    if ((flags & (BDRV_O_CHECK | BDRV_O_INACTIVE)) || !(flags & BDRV_O_RDWR)) {
        return 0;
    }

    /* Repair the image if corruption was detected. */
    if (need_check) {
        BdrvCheckResult res;
        ret = bdrv_check(bs, &res, BDRV_FIX_ERRORS | BDRV_FIX_LEAKS);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not repair corrupted image");
            migrate_del_blocker(&s->migration_blocker);
            goto fail;
        }
    }
    return 0;

fail_format:
    error_setg(errp, "Image not in Parallels format");
    return -EINVAL;

fail:
    /*
     * "s" object was allocated by g_malloc0 so we can safely
     * try to free its fields even they were not allocated.
     */
    parallels_free_used_bitmap(bs);

    g_free(s->bat_dirty_bmap);
    qemu_vfree(s->header);
    return ret;
}


static void parallels_close(BlockDriverState *bs)
{
    BDRVParallelsState *s = bs->opaque;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if ((bs->open_flags & BDRV_O_RDWR) && !(bs->open_flags & BDRV_O_INACTIVE)) {
        s->header->inuse = 0;
        parallels_update_header(bs);

        /* errors are ignored, so we might as well pass exact=true */
        bdrv_truncate(bs->file, s->data_end << BDRV_SECTOR_BITS, true,
                      PREALLOC_MODE_OFF, 0, NULL);
    }

    parallels_free_used_bitmap(bs);

    g_free(s->bat_dirty_bmap);
    qemu_vfree(s->header);

    migrate_del_blocker(&s->migration_blocker);
}

static bool parallels_is_support_dirty_bitmaps(BlockDriverState *bs)
{
    return 1;
}

static BlockDriver bdrv_parallels = {
    .format_name                = "parallels",
    .instance_size              = sizeof(BDRVParallelsState),
    .create_opts                = &parallels_create_opts,
    .is_format                  = true,
    .supports_backing           = true,

    .bdrv_has_zero_init         = bdrv_has_zero_init_1,
    .bdrv_supports_persistent_dirty_bitmap = parallels_is_support_dirty_bitmaps,

    .bdrv_probe                 = parallels_probe,
    .bdrv_open                  = parallels_open,
    .bdrv_close                 = parallels_close,
    .bdrv_child_perm            = bdrv_default_perms,
    .bdrv_co_block_status       = parallels_co_block_status,
    .bdrv_co_flush_to_os        = parallels_co_flush_to_os,
    .bdrv_co_readv              = parallels_co_readv,
    .bdrv_co_writev             = parallels_co_writev,
    .bdrv_co_create             = parallels_co_create,
    .bdrv_co_create_opts        = parallels_co_create_opts,
    .bdrv_co_check              = parallels_co_check,
    .bdrv_co_pdiscard           = parallels_co_pdiscard,
    .bdrv_co_pwrite_zeroes      = parallels_co_pwrite_zeroes,
};

static void bdrv_parallels_init(void)
{
    bdrv_register(&bdrv_parallels);
}

block_init(bdrv_parallels_init);
