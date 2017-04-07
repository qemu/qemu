/*
 * QEMU Block driver for DMG images
 *
 * Copyright (c) 2004 Johannes E. Schindelin
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
#include "qemu-common.h"
#include "block/block_int.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "dmg.h"

int (*dmg_uncompress_bz2)(char *next_in, unsigned int avail_in,
                          char *next_out, unsigned int avail_out);

enum {
    /* Limit chunk sizes to prevent unreasonable amounts of memory being used
     * or truncating when converting to 32-bit types
     */
    DMG_LENGTHS_MAX = 64 * 1024 * 1024, /* 64 MB */
    DMG_SECTORCOUNTS_MAX = DMG_LENGTHS_MAX / 512,
};

static int dmg_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    int len;

    if (!filename) {
        return 0;
    }

    len = strlen(filename);
    if (len > 4 && !strcmp(filename + len - 4, ".dmg")) {
        return 2;
    }
    return 0;
}

static int read_uint64(BlockDriverState *bs, int64_t offset, uint64_t *result)
{
    uint64_t buffer;
    int ret;

    ret = bdrv_pread(bs->file, offset, &buffer, 8);
    if (ret < 0) {
        return ret;
    }

    *result = be64_to_cpu(buffer);
    return 0;
}

static int read_uint32(BlockDriverState *bs, int64_t offset, uint32_t *result)
{
    uint32_t buffer;
    int ret;

    ret = bdrv_pread(bs->file, offset, &buffer, 4);
    if (ret < 0) {
        return ret;
    }

    *result = be32_to_cpu(buffer);
    return 0;
}

static inline uint64_t buff_read_uint64(const uint8_t *buffer, int64_t offset)
{
    return be64_to_cpu(*(uint64_t *)&buffer[offset]);
}

static inline uint32_t buff_read_uint32(const uint8_t *buffer, int64_t offset)
{
    return be32_to_cpu(*(uint32_t *)&buffer[offset]);
}

/* Increase max chunk sizes, if necessary.  This function is used to calculate
 * the buffer sizes needed for compressed/uncompressed chunk I/O.
 */
static void update_max_chunk_size(BDRVDMGState *s, uint32_t chunk,
                                  uint32_t *max_compressed_size,
                                  uint32_t *max_sectors_per_chunk)
{
    uint32_t compressed_size = 0;
    uint32_t uncompressed_sectors = 0;

    switch (s->types[chunk]) {
    case 0x80000005: /* zlib compressed */
    case 0x80000006: /* bzip2 compressed */
        compressed_size = s->lengths[chunk];
        uncompressed_sectors = s->sectorcounts[chunk];
        break;
    case 1: /* copy */
        uncompressed_sectors = (s->lengths[chunk] + 511) / 512;
        break;
    case 2: /* zero */
        /* as the all-zeroes block may be large, it is treated specially: the
         * sector is not copied from a large buffer, a simple memset is used
         * instead. Therefore uncompressed_sectors does not need to be set. */
        break;
    }

    if (compressed_size > *max_compressed_size) {
        *max_compressed_size = compressed_size;
    }
    if (uncompressed_sectors > *max_sectors_per_chunk) {
        *max_sectors_per_chunk = uncompressed_sectors;
    }
}

static int64_t dmg_find_koly_offset(BdrvChild *file, Error **errp)
{
    BlockDriverState *file_bs = file->bs;
    int64_t length;
    int64_t offset = 0;
    uint8_t buffer[515];
    int i, ret;

    /* bdrv_getlength returns a multiple of block size (512), rounded up. Since
     * dmg images can have odd sizes, try to look for the "koly" magic which
     * marks the begin of the UDIF trailer (512 bytes). This magic can be found
     * in the last 511 bytes of the second-last sector or the first 4 bytes of
     * the last sector (search space: 515 bytes) */
    length = bdrv_getlength(file_bs);
    if (length < 0) {
        error_setg_errno(errp, -length,
            "Failed to get file size while reading UDIF trailer");
        return length;
    } else if (length < 512) {
        error_setg(errp, "dmg file must be at least 512 bytes long");
        return -EINVAL;
    }
    if (length > 511 + 512) {
        offset = length - 511 - 512;
    }
    length = length < 515 ? length : 515;
    ret = bdrv_pread(file, offset, buffer, length);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed while reading UDIF trailer");
        return ret;
    }
    for (i = 0; i < length - 3; i++) {
        if (buffer[i] == 'k' && buffer[i+1] == 'o' &&
            buffer[i+2] == 'l' && buffer[i+3] == 'y') {
            return offset + i;
        }
    }
    error_setg(errp, "Could not locate UDIF trailer in dmg file");
    return -EINVAL;
}

/* used when building the sector table */
typedef struct DmgHeaderState {
    /* used internally by dmg_read_mish_block to remember offsets of blocks
     * across calls */
    uint64_t data_fork_offset;
    /* exported for dmg_open */
    uint32_t max_compressed_size;
    uint32_t max_sectors_per_chunk;
} DmgHeaderState;

static bool dmg_is_known_block_type(uint32_t entry_type)
{
    switch (entry_type) {
    case 0x00000001:    /* uncompressed */
    case 0x00000002:    /* zeroes */
    case 0x80000005:    /* zlib */
        return true;
    case 0x80000006:    /* bzip2 */
        return !!dmg_uncompress_bz2;
    default:
        return false;
    }
}

static int dmg_read_mish_block(BDRVDMGState *s, DmgHeaderState *ds,
                               uint8_t *buffer, uint32_t count)
{
    uint32_t type, i;
    int ret;
    size_t new_size;
    uint32_t chunk_count;
    int64_t offset = 0;
    uint64_t data_offset;
    uint64_t in_offset = ds->data_fork_offset;
    uint64_t out_offset;

    type = buff_read_uint32(buffer, offset);
    /* skip data that is not a valid MISH block (invalid magic or too small) */
    if (type != 0x6d697368 || count < 244) {
        /* assume success for now */
        return 0;
    }

    /* chunk offsets are relative to this sector number */
    out_offset = buff_read_uint64(buffer, offset + 8);

    /* location in data fork for (compressed) blob (in bytes) */
    data_offset = buff_read_uint64(buffer, offset + 0x18);
    in_offset += data_offset;

    /* move to begin of chunk entries */
    offset += 204;

    chunk_count = (count - 204) / 40;
    new_size = sizeof(uint64_t) * (s->n_chunks + chunk_count);
    s->types = g_realloc(s->types, new_size / 2);
    s->offsets = g_realloc(s->offsets, new_size);
    s->lengths = g_realloc(s->lengths, new_size);
    s->sectors = g_realloc(s->sectors, new_size);
    s->sectorcounts = g_realloc(s->sectorcounts, new_size);

    for (i = s->n_chunks; i < s->n_chunks + chunk_count; i++) {
        s->types[i] = buff_read_uint32(buffer, offset);
        if (!dmg_is_known_block_type(s->types[i])) {
            chunk_count--;
            i--;
            offset += 40;
            continue;
        }

        /* sector number */
        s->sectors[i] = buff_read_uint64(buffer, offset + 8);
        s->sectors[i] += out_offset;

        /* sector count */
        s->sectorcounts[i] = buff_read_uint64(buffer, offset + 0x10);

        /* all-zeroes sector (type 2) does not need to be "uncompressed" and can
         * therefore be unbounded. */
        if (s->types[i] != 2 && s->sectorcounts[i] > DMG_SECTORCOUNTS_MAX) {
            error_report("sector count %" PRIu64 " for chunk %" PRIu32
                         " is larger than max (%u)",
                         s->sectorcounts[i], i, DMG_SECTORCOUNTS_MAX);
            ret = -EINVAL;
            goto fail;
        }

        /* offset in (compressed) data fork */
        s->offsets[i] = buff_read_uint64(buffer, offset + 0x18);
        s->offsets[i] += in_offset;

        /* length in (compressed) data fork */
        s->lengths[i] = buff_read_uint64(buffer, offset + 0x20);

        if (s->lengths[i] > DMG_LENGTHS_MAX) {
            error_report("length %" PRIu64 " for chunk %" PRIu32
                         " is larger than max (%u)",
                         s->lengths[i], i, DMG_LENGTHS_MAX);
            ret = -EINVAL;
            goto fail;
        }

        update_max_chunk_size(s, i, &ds->max_compressed_size,
                              &ds->max_sectors_per_chunk);
        offset += 40;
    }
    s->n_chunks += chunk_count;
    return 0;

fail:
    return ret;
}

static int dmg_read_resource_fork(BlockDriverState *bs, DmgHeaderState *ds,
                                  uint64_t info_begin, uint64_t info_length)
{
    BDRVDMGState *s = bs->opaque;
    int ret;
    uint32_t count, rsrc_data_offset;
    uint8_t *buffer = NULL;
    uint64_t info_end;
    uint64_t offset;

    /* read offset from begin of resource fork (info_begin) to resource data */
    ret = read_uint32(bs, info_begin, &rsrc_data_offset);
    if (ret < 0) {
        goto fail;
    } else if (rsrc_data_offset > info_length) {
        ret = -EINVAL;
        goto fail;
    }

    /* read length of resource data */
    ret = read_uint32(bs, info_begin + 8, &count);
    if (ret < 0) {
        goto fail;
    } else if (count == 0 || rsrc_data_offset + count > info_length) {
        ret = -EINVAL;
        goto fail;
    }

    /* begin of resource data (consisting of one or more resources) */
    offset = info_begin + rsrc_data_offset;

    /* end of resource data (there is possibly a following resource map
     * which will be ignored). */
    info_end = offset + count;

    /* read offsets (mish blocks) from one or more resources in resource data */
    while (offset < info_end) {
        /* size of following resource */
        ret = read_uint32(bs, offset, &count);
        if (ret < 0) {
            goto fail;
        } else if (count == 0 || count > info_end - offset) {
            ret = -EINVAL;
            goto fail;
        }
        offset += 4;

        buffer = g_realloc(buffer, count);
        ret = bdrv_pread(bs->file, offset, buffer, count);
        if (ret < 0) {
            goto fail;
        }

        ret = dmg_read_mish_block(s, ds, buffer, count);
        if (ret < 0) {
            goto fail;
        }
        /* advance offset by size of resource */
        offset += count;
    }
    ret = 0;

fail:
    g_free(buffer);
    return ret;
}

static int dmg_read_plist_xml(BlockDriverState *bs, DmgHeaderState *ds,
                              uint64_t info_begin, uint64_t info_length)
{
    BDRVDMGState *s = bs->opaque;
    int ret;
    uint8_t *buffer = NULL;
    char *data_begin, *data_end;

    /* Have at least some length to avoid NULL for g_malloc. Attempt to set a
     * safe upper cap on the data length. A test sample had a XML length of
     * about 1 MiB. */
    if (info_length == 0 || info_length > 16 * 1024 * 1024) {
        ret = -EINVAL;
        goto fail;
    }

    buffer = g_malloc(info_length + 1);
    buffer[info_length] = '\0';
    ret = bdrv_pread(bs->file, info_begin, buffer, info_length);
    if (ret != info_length) {
        ret = -EINVAL;
        goto fail;
    }

    /* look for <data>...</data>. The data is 284 (0x11c) bytes after base64
     * decode. The actual data element has 431 (0x1af) bytes which includes tabs
     * and line feeds. */
    data_end = (char *)buffer;
    while ((data_begin = strstr(data_end, "<data>")) != NULL) {
        guchar *mish;
        gsize out_len = 0;

        data_begin += 6;
        data_end = strstr(data_begin, "</data>");
        /* malformed XML? */
        if (data_end == NULL) {
            ret = -EINVAL;
            goto fail;
        }
        *data_end++ = '\0';
        mish = g_base64_decode(data_begin, &out_len);
        ret = dmg_read_mish_block(s, ds, mish, (uint32_t)out_len);
        g_free(mish);
        if (ret < 0) {
            goto fail;
        }
    }
    ret = 0;

fail:
    g_free(buffer);
    return ret;
}

static int dmg_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVDMGState *s = bs->opaque;
    DmgHeaderState ds;
    uint64_t rsrc_fork_offset, rsrc_fork_length;
    uint64_t plist_xml_offset, plist_xml_length;
    int64_t offset;
    int ret;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    ret = bdrv_set_read_only(bs, true, errp);
    if (ret < 0) {
        return ret;
    }

    block_module_load_one("dmg-bz2");

    s->n_chunks = 0;
    s->offsets = s->lengths = s->sectors = s->sectorcounts = NULL;
    /* used by dmg_read_mish_block to keep track of the current I/O position */
    ds.data_fork_offset = 0;
    ds.max_compressed_size = 1;
    ds.max_sectors_per_chunk = 1;

    /* locate the UDIF trailer */
    offset = dmg_find_koly_offset(bs->file, errp);
    if (offset < 0) {
        ret = offset;
        goto fail;
    }

    /* offset of data fork (DataForkOffset) */
    ret = read_uint64(bs, offset + 0x18, &ds.data_fork_offset);
    if (ret < 0) {
        goto fail;
    } else if (ds.data_fork_offset > offset) {
        ret = -EINVAL;
        goto fail;
    }

    /* offset of resource fork (RsrcForkOffset) */
    ret = read_uint64(bs, offset + 0x28, &rsrc_fork_offset);
    if (ret < 0) {
        goto fail;
    }
    ret = read_uint64(bs, offset + 0x30, &rsrc_fork_length);
    if (ret < 0) {
        goto fail;
    }
    if (rsrc_fork_offset >= offset ||
        rsrc_fork_length > offset - rsrc_fork_offset) {
        ret = -EINVAL;
        goto fail;
    }
    /* offset of property list (XMLOffset) */
    ret = read_uint64(bs, offset + 0xd8, &plist_xml_offset);
    if (ret < 0) {
        goto fail;
    }
    ret = read_uint64(bs, offset + 0xe0, &plist_xml_length);
    if (ret < 0) {
        goto fail;
    }
    if (plist_xml_offset >= offset ||
        plist_xml_length > offset - plist_xml_offset) {
        ret = -EINVAL;
        goto fail;
    }
    ret = read_uint64(bs, offset + 0x1ec, (uint64_t *)&bs->total_sectors);
    if (ret < 0) {
        goto fail;
    }
    if (bs->total_sectors < 0) {
        ret = -EINVAL;
        goto fail;
    }
    if (rsrc_fork_length != 0) {
        ret = dmg_read_resource_fork(bs, &ds,
                                     rsrc_fork_offset, rsrc_fork_length);
        if (ret < 0) {
            goto fail;
        }
    } else if (plist_xml_length != 0) {
        ret = dmg_read_plist_xml(bs, &ds, plist_xml_offset, plist_xml_length);
        if (ret < 0) {
            goto fail;
        }
    } else {
        ret = -EINVAL;
        goto fail;
    }

    /* initialize zlib engine */
    s->compressed_chunk = qemu_try_blockalign(bs->file->bs,
                                              ds.max_compressed_size + 1);
    s->uncompressed_chunk = qemu_try_blockalign(bs->file->bs,
                                                512 * ds.max_sectors_per_chunk);
    if (s->compressed_chunk == NULL || s->uncompressed_chunk == NULL) {
        ret = -ENOMEM;
        goto fail;
    }

    if (inflateInit(&s->zstream) != Z_OK) {
        ret = -EINVAL;
        goto fail;
    }

    s->current_chunk = s->n_chunks;

    qemu_co_mutex_init(&s->lock);
    return 0;

fail:
    g_free(s->types);
    g_free(s->offsets);
    g_free(s->lengths);
    g_free(s->sectors);
    g_free(s->sectorcounts);
    qemu_vfree(s->compressed_chunk);
    qemu_vfree(s->uncompressed_chunk);
    return ret;
}

static void dmg_refresh_limits(BlockDriverState *bs, Error **errp)
{
    bs->bl.request_alignment = BDRV_SECTOR_SIZE; /* No sub-sector I/O */
}

static inline int is_sector_in_chunk(BDRVDMGState* s,
                uint32_t chunk_num, uint64_t sector_num)
{
    if (chunk_num >= s->n_chunks || s->sectors[chunk_num] > sector_num ||
            s->sectors[chunk_num] + s->sectorcounts[chunk_num] <= sector_num) {
        return 0;
    } else {
        return -1;
    }
}

static inline uint32_t search_chunk(BDRVDMGState *s, uint64_t sector_num)
{
    /* binary search */
    uint32_t chunk1 = 0, chunk2 = s->n_chunks, chunk3;
    while (chunk1 != chunk2) {
        chunk3 = (chunk1 + chunk2) / 2;
        if (s->sectors[chunk3] > sector_num) {
            chunk2 = chunk3;
        } else if (s->sectors[chunk3] + s->sectorcounts[chunk3] > sector_num) {
            return chunk3;
        } else {
            chunk1 = chunk3;
        }
    }
    return s->n_chunks; /* error */
}

static inline int dmg_read_chunk(BlockDriverState *bs, uint64_t sector_num)
{
    BDRVDMGState *s = bs->opaque;

    if (!is_sector_in_chunk(s, s->current_chunk, sector_num)) {
        int ret;
        uint32_t chunk = search_chunk(s, sector_num);

        if (chunk >= s->n_chunks) {
            return -1;
        }

        s->current_chunk = s->n_chunks;
        switch (s->types[chunk]) { /* block entry type */
        case 0x80000005: { /* zlib compressed */
            /* we need to buffer, because only the chunk as whole can be
             * inflated. */
            ret = bdrv_pread(bs->file, s->offsets[chunk],
                             s->compressed_chunk, s->lengths[chunk]);
            if (ret != s->lengths[chunk]) {
                return -1;
            }

            s->zstream.next_in = s->compressed_chunk;
            s->zstream.avail_in = s->lengths[chunk];
            s->zstream.next_out = s->uncompressed_chunk;
            s->zstream.avail_out = 512 * s->sectorcounts[chunk];
            ret = inflateReset(&s->zstream);
            if (ret != Z_OK) {
                return -1;
            }
            ret = inflate(&s->zstream, Z_FINISH);
            if (ret != Z_STREAM_END ||
                s->zstream.total_out != 512 * s->sectorcounts[chunk]) {
                return -1;
            }
            break; }
        case 0x80000006: /* bzip2 compressed */
            if (!dmg_uncompress_bz2) {
                break;
            }
            /* we need to buffer, because only the chunk as whole can be
             * inflated. */
            ret = bdrv_pread(bs->file, s->offsets[chunk],
                             s->compressed_chunk, s->lengths[chunk]);
            if (ret != s->lengths[chunk]) {
                return -1;
            }

            ret = dmg_uncompress_bz2((char *)s->compressed_chunk,
                                     (unsigned int) s->lengths[chunk],
                                     (char *)s->uncompressed_chunk,
                                     (unsigned int)
                                         (512 * s->sectorcounts[chunk]));
            if (ret < 0) {
                return ret;
            }
            break;
        case 1: /* copy */
            ret = bdrv_pread(bs->file, s->offsets[chunk],
                             s->uncompressed_chunk, s->lengths[chunk]);
            if (ret != s->lengths[chunk]) {
                return -1;
            }
            break;
        case 2: /* zero */
            /* see dmg_read, it is treated specially. No buffer needs to be
             * pre-filled, the zeroes can be set directly. */
            break;
        }
        s->current_chunk = chunk;
    }
    return 0;
}

static int coroutine_fn
dmg_co_preadv(BlockDriverState *bs, uint64_t offset, uint64_t bytes,
              QEMUIOVector *qiov, int flags)
{
    BDRVDMGState *s = bs->opaque;
    uint64_t sector_num = offset >> BDRV_SECTOR_BITS;
    int nb_sectors = bytes >> BDRV_SECTOR_BITS;
    int ret, i;

    assert((offset & (BDRV_SECTOR_SIZE - 1)) == 0);
    assert((bytes & (BDRV_SECTOR_SIZE - 1)) == 0);

    qemu_co_mutex_lock(&s->lock);

    for (i = 0; i < nb_sectors; i++) {
        uint32_t sector_offset_in_chunk;
        void *data;

        if (dmg_read_chunk(bs, sector_num + i) != 0) {
            ret = -EIO;
            goto fail;
        }
        /* Special case: current chunk is all zeroes. Do not perform a memcpy as
         * s->uncompressed_chunk may be too small to cover the large all-zeroes
         * section. dmg_read_chunk is called to find s->current_chunk */
        if (s->types[s->current_chunk] == 2) { /* all zeroes block entry */
            qemu_iovec_memset(qiov, i * 512, 0, 512);
            continue;
        }
        sector_offset_in_chunk = sector_num + i - s->sectors[s->current_chunk];
        data = s->uncompressed_chunk + sector_offset_in_chunk * 512;
        qemu_iovec_from_buf(qiov, i * 512, data, 512);
    }

    ret = 0;
fail:
    qemu_co_mutex_unlock(&s->lock);
    return ret;
}

static void dmg_close(BlockDriverState *bs)
{
    BDRVDMGState *s = bs->opaque;

    g_free(s->types);
    g_free(s->offsets);
    g_free(s->lengths);
    g_free(s->sectors);
    g_free(s->sectorcounts);
    qemu_vfree(s->compressed_chunk);
    qemu_vfree(s->uncompressed_chunk);

    inflateEnd(&s->zstream);
}

static BlockDriver bdrv_dmg = {
    .format_name    = "dmg",
    .instance_size  = sizeof(BDRVDMGState),
    .bdrv_probe     = dmg_probe,
    .bdrv_open      = dmg_open,
    .bdrv_refresh_limits = dmg_refresh_limits,
    .bdrv_child_perm     = bdrv_format_default_perms,
    .bdrv_co_preadv = dmg_co_preadv,
    .bdrv_close     = dmg_close,
};

static void bdrv_dmg_init(void)
{
    bdrv_register(&bdrv_dmg);
}

block_init(bdrv_dmg_init);
