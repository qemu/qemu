/*
 * Block driver for the QCOW version 2 format
 *
 * Copyright (c) 2004-2006 Fabrice Bellard
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
#include "qemu-common.h"
#include "block_int.h"
#include "module.h"
#include <zlib.h>
#include "aes.h"
#include "block/qcow2.h"
#include "qemu-error.h"
#include "qerror.h"

/*
  Differences with QCOW:

  - Support for multiple incremental snapshots.
  - Memory management by reference counts.
  - Clusters which have a reference count of one have the bit
    QCOW_OFLAG_COPIED to optimize write performance.
  - Size of compressed clusters is stored in sectors to reduce bit usage
    in the cluster offsets.
  - Support for storing additional data (such as the VM state) in the
    snapshots.
  - If a backing store is used, the cluster size is not constrained
    (could be backported to QCOW).
  - L2 tables have always a size of one cluster.
*/


typedef struct {
    uint32_t magic;
    uint32_t len;
} QCowExtension;
#define  QCOW2_EXT_MAGIC_END 0
#define  QCOW2_EXT_MAGIC_BACKING_FORMAT 0xE2792ACA

static int qcow2_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const QCowHeader *cow_header = (const void *)buf;

    if (buf_size >= sizeof(QCowHeader) &&
        be32_to_cpu(cow_header->magic) == QCOW_MAGIC &&
        be32_to_cpu(cow_header->version) >= QCOW_VERSION)
        return 100;
    else
        return 0;
}


/* 
 * read qcow2 extension and fill bs
 * start reading from start_offset
 * finish reading upon magic of value 0 or when end_offset reached
 * unknown magic is skipped (future extension this version knows nothing about)
 * return 0 upon success, non-0 otherwise
 */
static int qcow2_read_extensions(BlockDriverState *bs, uint64_t start_offset,
                                 uint64_t end_offset)
{
    QCowExtension ext;
    uint64_t offset;

#ifdef DEBUG_EXT
    printf("qcow2_read_extensions: start=%ld end=%ld\n", start_offset, end_offset);
#endif
    offset = start_offset;
    while (offset < end_offset) {

#ifdef DEBUG_EXT
        BDRVQcowState *s = bs->opaque;
        /* Sanity check */
        if (offset > s->cluster_size)
            printf("qcow2_read_extension: suspicious offset %lu\n", offset);

        printf("attemting to read extended header in offset %lu\n", offset);
#endif

        if (bdrv_pread(bs->file, offset, &ext, sizeof(ext)) != sizeof(ext)) {
            fprintf(stderr, "qcow2_read_extension: ERROR: "
                    "pread fail from offset %" PRIu64 "\n",
                    offset);
            return 1;
        }
        be32_to_cpus(&ext.magic);
        be32_to_cpus(&ext.len);
        offset += sizeof(ext);
#ifdef DEBUG_EXT
        printf("ext.magic = 0x%x\n", ext.magic);
#endif
        switch (ext.magic) {
        case QCOW2_EXT_MAGIC_END:
            return 0;

        case QCOW2_EXT_MAGIC_BACKING_FORMAT:
            if (ext.len >= sizeof(bs->backing_format)) {
                fprintf(stderr, "ERROR: ext_backing_format: len=%u too large"
                        " (>=%zu)\n",
                        ext.len, sizeof(bs->backing_format));
                return 2;
            }
            if (bdrv_pread(bs->file, offset , bs->backing_format,
                           ext.len) != ext.len)
                return 3;
            bs->backing_format[ext.len] = '\0';
#ifdef DEBUG_EXT
            printf("Qcow2: Got format extension %s\n", bs->backing_format);
#endif
            offset = ((offset + ext.len + 7) & ~7);
            break;

        default:
            /* unknown magic -- just skip it */
            offset = ((offset + ext.len + 7) & ~7);
            break;
        }
    }

    return 0;
}


static int qcow2_open(BlockDriverState *bs, int flags)
{
    BDRVQcowState *s = bs->opaque;
    int len, i, ret = 0;
    QCowHeader header;
    uint64_t ext_end;
    bool writethrough;

    ret = bdrv_pread(bs->file, 0, &header, sizeof(header));
    if (ret < 0) {
        goto fail;
    }
    be32_to_cpus(&header.magic);
    be32_to_cpus(&header.version);
    be64_to_cpus(&header.backing_file_offset);
    be32_to_cpus(&header.backing_file_size);
    be64_to_cpus(&header.size);
    be32_to_cpus(&header.cluster_bits);
    be32_to_cpus(&header.crypt_method);
    be64_to_cpus(&header.l1_table_offset);
    be32_to_cpus(&header.l1_size);
    be64_to_cpus(&header.refcount_table_offset);
    be32_to_cpus(&header.refcount_table_clusters);
    be64_to_cpus(&header.snapshots_offset);
    be32_to_cpus(&header.nb_snapshots);

    if (header.magic != QCOW_MAGIC) {
        ret = -EINVAL;
        goto fail;
    }
    if (header.version != QCOW_VERSION) {
        char version[64];
        snprintf(version, sizeof(version), "QCOW version %d", header.version);
        qerror_report(QERR_UNKNOWN_BLOCK_FORMAT_FEATURE,
            bs->device_name, "qcow2", version);
        ret = -ENOTSUP;
        goto fail;
    }
    if (header.cluster_bits < MIN_CLUSTER_BITS ||
        header.cluster_bits > MAX_CLUSTER_BITS) {
        ret = -EINVAL;
        goto fail;
    }
    if (header.crypt_method > QCOW_CRYPT_AES) {
        ret = -EINVAL;
        goto fail;
    }
    s->crypt_method_header = header.crypt_method;
    if (s->crypt_method_header) {
        bs->encrypted = 1;
    }
    s->cluster_bits = header.cluster_bits;
    s->cluster_size = 1 << s->cluster_bits;
    s->cluster_sectors = 1 << (s->cluster_bits - 9);
    s->l2_bits = s->cluster_bits - 3; /* L2 is always one cluster */
    s->l2_size = 1 << s->l2_bits;
    bs->total_sectors = header.size / 512;
    s->csize_shift = (62 - (s->cluster_bits - 8));
    s->csize_mask = (1 << (s->cluster_bits - 8)) - 1;
    s->cluster_offset_mask = (1LL << s->csize_shift) - 1;
    s->refcount_table_offset = header.refcount_table_offset;
    s->refcount_table_size =
        header.refcount_table_clusters << (s->cluster_bits - 3);

    s->snapshots_offset = header.snapshots_offset;
    s->nb_snapshots = header.nb_snapshots;

    /* read the level 1 table */
    s->l1_size = header.l1_size;
    s->l1_vm_state_index = size_to_l1(s, header.size);
    /* the L1 table must contain at least enough entries to put
       header.size bytes */
    if (s->l1_size < s->l1_vm_state_index) {
        ret = -EINVAL;
        goto fail;
    }
    s->l1_table_offset = header.l1_table_offset;
    if (s->l1_size > 0) {
        s->l1_table = g_malloc0(
            align_offset(s->l1_size * sizeof(uint64_t), 512));
        ret = bdrv_pread(bs->file, s->l1_table_offset, s->l1_table,
                         s->l1_size * sizeof(uint64_t));
        if (ret < 0) {
            goto fail;
        }
        for(i = 0;i < s->l1_size; i++) {
            be64_to_cpus(&s->l1_table[i]);
        }
    }

    /* alloc L2 table/refcount block cache */
    writethrough = ((flags & BDRV_O_CACHE_WB) == 0);
    s->l2_table_cache = qcow2_cache_create(bs, L2_CACHE_SIZE, writethrough);
    s->refcount_block_cache = qcow2_cache_create(bs, REFCOUNT_CACHE_SIZE,
        writethrough);

    s->cluster_cache = g_malloc(s->cluster_size);
    /* one more sector for decompressed data alignment */
    s->cluster_data = g_malloc(QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size
                                  + 512);
    s->cluster_cache_offset = -1;

    ret = qcow2_refcount_init(bs);
    if (ret != 0) {
        goto fail;
    }

    QLIST_INIT(&s->cluster_allocs);

    /* read qcow2 extensions */
    if (header.backing_file_offset) {
        ext_end = header.backing_file_offset;
    } else {
        ext_end = s->cluster_size;
    }
    if (qcow2_read_extensions(bs, sizeof(header), ext_end)) {
        ret = -EINVAL;
        goto fail;
    }

    /* read the backing file name */
    if (header.backing_file_offset != 0) {
        len = header.backing_file_size;
        if (len > 1023) {
            len = 1023;
        }
        ret = bdrv_pread(bs->file, header.backing_file_offset,
                         bs->backing_file, len);
        if (ret < 0) {
            goto fail;
        }
        bs->backing_file[len] = '\0';
    }
    if (qcow2_read_snapshots(bs) < 0) {
        ret = -EINVAL;
        goto fail;
    }

    /* Initialise locks */
    qemu_co_mutex_init(&s->lock);

#ifdef DEBUG_ALLOC
    {
        BdrvCheckResult result = {0};
        qcow2_check_refcounts(bs, &result);
    }
#endif
    return ret;

 fail:
    qcow2_free_snapshots(bs);
    qcow2_refcount_close(bs);
    g_free(s->l1_table);
    if (s->l2_table_cache) {
        qcow2_cache_destroy(bs, s->l2_table_cache);
    }
    g_free(s->cluster_cache);
    g_free(s->cluster_data);
    return ret;
}

static int qcow2_set_key(BlockDriverState *bs, const char *key)
{
    BDRVQcowState *s = bs->opaque;
    uint8_t keybuf[16];
    int len, i;

    memset(keybuf, 0, 16);
    len = strlen(key);
    if (len > 16)
        len = 16;
    /* XXX: we could compress the chars to 7 bits to increase
       entropy */
    for(i = 0;i < len;i++) {
        keybuf[i] = key[i];
    }
    s->crypt_method = s->crypt_method_header;

    if (AES_set_encrypt_key(keybuf, 128, &s->aes_encrypt_key) != 0)
        return -1;
    if (AES_set_decrypt_key(keybuf, 128, &s->aes_decrypt_key) != 0)
        return -1;
#if 0
    /* test */
    {
        uint8_t in[16];
        uint8_t out[16];
        uint8_t tmp[16];
        for(i=0;i<16;i++)
            in[i] = i;
        AES_encrypt(in, tmp, &s->aes_encrypt_key);
        AES_decrypt(tmp, out, &s->aes_decrypt_key);
        for(i = 0; i < 16; i++)
            printf(" %02x", tmp[i]);
        printf("\n");
        for(i = 0; i < 16; i++)
            printf(" %02x", out[i]);
        printf("\n");
    }
#endif
    return 0;
}

static int qcow2_is_allocated(BlockDriverState *bs, int64_t sector_num,
                              int nb_sectors, int *pnum)
{
    uint64_t cluster_offset;
    int ret;

    *pnum = nb_sectors;
    /* FIXME We can get errors here, but the bdrv_is_allocated interface can't
     * pass them on today */
    ret = qcow2_get_cluster_offset(bs, sector_num << 9, pnum, &cluster_offset);
    if (ret < 0) {
        *pnum = 0;
    }

    return (cluster_offset != 0);
}

/* handle reading after the end of the backing file */
int qcow2_backing_read1(BlockDriverState *bs, QEMUIOVector *qiov,
                  int64_t sector_num, int nb_sectors)
{
    int n1;
    if ((sector_num + nb_sectors) <= bs->total_sectors)
        return nb_sectors;
    if (sector_num >= bs->total_sectors)
        n1 = 0;
    else
        n1 = bs->total_sectors - sector_num;

    qemu_iovec_memset_skip(qiov, 0, 512 * (nb_sectors - n1), 512 * n1);

    return n1;
}

typedef struct QCowAIOCB {
    BlockDriverAIOCB common;
    int64_t sector_num;
    QEMUIOVector *qiov;
    int remaining_sectors;
    int cur_nr_sectors;	/* number of sectors in current iteration */
    uint64_t bytes_done;
    uint64_t cluster_offset;
    uint8_t *cluster_data;
    bool is_write;
    QEMUIOVector hd_qiov;
    QEMUBH *bh;
    QCowL2Meta l2meta;
    QLIST_ENTRY(QCowAIOCB) next_depend;
} QCowAIOCB;

static void qcow2_aio_cancel(BlockDriverAIOCB *blockacb)
{
    QCowAIOCB *acb = container_of(blockacb, QCowAIOCB, common);
    qemu_aio_release(acb);
}

static AIOPool qcow2_aio_pool = {
    .aiocb_size         = sizeof(QCowAIOCB),
    .cancel             = qcow2_aio_cancel,
};

/*
 * Returns 0 when the request is completed successfully, 1 when there is still
 * a part left to do and -errno in error cases.
 */
static int qcow2_aio_read_cb(QCowAIOCB *acb)
{
    BlockDriverState *bs = acb->common.bs;
    BDRVQcowState *s = bs->opaque;
    int index_in_cluster, n1;
    int ret;

    /* post process the read buffer */
    if (!acb->cluster_offset) {
        /* nothing to do */
    } else if (acb->cluster_offset & QCOW_OFLAG_COMPRESSED) {
        /* nothing to do */
    } else {
        if (s->crypt_method) {
            qcow2_encrypt_sectors(s, acb->sector_num,  acb->cluster_data,
                acb->cluster_data, acb->cur_nr_sectors, 0, &s->aes_decrypt_key);
            qemu_iovec_reset(&acb->hd_qiov);
            qemu_iovec_copy(&acb->hd_qiov, acb->qiov, acb->bytes_done,
                acb->cur_nr_sectors * 512);
            qemu_iovec_from_buffer(&acb->hd_qiov, acb->cluster_data,
                512 * acb->cur_nr_sectors);
        }
    }

    acb->remaining_sectors -= acb->cur_nr_sectors;
    acb->sector_num += acb->cur_nr_sectors;
    acb->bytes_done += acb->cur_nr_sectors * 512;

    if (acb->remaining_sectors == 0) {
        /* request completed */
        return 0;
    }

    /* prepare next AIO request */
    acb->cur_nr_sectors = acb->remaining_sectors;
    if (s->crypt_method) {
        acb->cur_nr_sectors = MIN(acb->cur_nr_sectors,
            QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors);
    }

    ret = qcow2_get_cluster_offset(bs, acb->sector_num << 9,
        &acb->cur_nr_sectors, &acb->cluster_offset);
    if (ret < 0) {
        return ret;
    }

    index_in_cluster = acb->sector_num & (s->cluster_sectors - 1);

    qemu_iovec_reset(&acb->hd_qiov);
    qemu_iovec_copy(&acb->hd_qiov, acb->qiov, acb->bytes_done,
        acb->cur_nr_sectors * 512);

    if (!acb->cluster_offset) {

        if (bs->backing_hd) {
            /* read from the base image */
            n1 = qcow2_backing_read1(bs->backing_hd, &acb->hd_qiov,
                acb->sector_num, acb->cur_nr_sectors);
            if (n1 > 0) {
                BLKDBG_EVENT(bs->file, BLKDBG_READ_BACKING_AIO);
                qemu_co_mutex_unlock(&s->lock);
                ret = bdrv_co_readv(bs->backing_hd, acb->sector_num,
                                    n1, &acb->hd_qiov);
                qemu_co_mutex_lock(&s->lock);
                if (ret < 0) {
                    return ret;
                }
            }
            return 1;
        } else {
            /* Note: in this case, no need to wait */
            qemu_iovec_memset(&acb->hd_qiov, 0, 512 * acb->cur_nr_sectors);
            return 1;
        }
    } else if (acb->cluster_offset & QCOW_OFLAG_COMPRESSED) {
        /* add AIO support for compressed blocks ? */
        ret = qcow2_decompress_cluster(bs, acb->cluster_offset);
        if (ret < 0) {
            return ret;
        }

        qemu_iovec_from_buffer(&acb->hd_qiov,
            s->cluster_cache + index_in_cluster * 512,
            512 * acb->cur_nr_sectors);

        return 1;
    } else {
        if ((acb->cluster_offset & 511) != 0) {
            return -EIO;
        }

        if (s->crypt_method) {
            /*
             * For encrypted images, read everything into a temporary
             * contiguous buffer on which the AES functions can work.
             */
            if (!acb->cluster_data) {
                acb->cluster_data =
                    g_malloc0(QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);
            }

            assert(acb->cur_nr_sectors <=
                QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors);
            qemu_iovec_reset(&acb->hd_qiov);
            qemu_iovec_add(&acb->hd_qiov, acb->cluster_data,
                512 * acb->cur_nr_sectors);
        }

        BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
        qemu_co_mutex_unlock(&s->lock);
        ret = bdrv_co_readv(bs->file,
                            (acb->cluster_offset >> 9) + index_in_cluster,
                            acb->cur_nr_sectors, &acb->hd_qiov);
        qemu_co_mutex_lock(&s->lock);
        if (ret < 0) {
            return ret;
        }
    }

    return 1;
}

static QCowAIOCB *qcow2_aio_setup(BlockDriverState *bs, int64_t sector_num,
                                  QEMUIOVector *qiov, int nb_sectors,
                                  BlockDriverCompletionFunc *cb,
                                  void *opaque, int is_write)
{
    QCowAIOCB *acb;

    acb = qemu_aio_get(&qcow2_aio_pool, bs, cb, opaque);
    if (!acb)
        return NULL;
    acb->sector_num = sector_num;
    acb->qiov = qiov;
    acb->is_write = is_write;

    qemu_iovec_init(&acb->hd_qiov, qiov->niov);

    acb->bytes_done = 0;
    acb->remaining_sectors = nb_sectors;
    acb->cur_nr_sectors = 0;
    acb->cluster_offset = 0;
    acb->l2meta.nb_clusters = 0;
    qemu_co_queue_init(&acb->l2meta.dependent_requests);
    return acb;
}

static int qcow2_co_readv(BlockDriverState *bs, int64_t sector_num,
                          int nb_sectors, QEMUIOVector *qiov)
{
    BDRVQcowState *s = bs->opaque;
    QCowAIOCB *acb;
    int ret;

    acb = qcow2_aio_setup(bs, sector_num, qiov, nb_sectors, NULL, NULL, 0);

    qemu_co_mutex_lock(&s->lock);
    do {
        ret = qcow2_aio_read_cb(acb);
    } while (ret > 0);
    qemu_co_mutex_unlock(&s->lock);

    qemu_iovec_destroy(&acb->hd_qiov);
    qemu_aio_release(acb);

    return ret;
}

static void run_dependent_requests(BDRVQcowState *s, QCowL2Meta *m)
{
    /* Take the request off the list of running requests */
    if (m->nb_clusters != 0) {
        QLIST_REMOVE(m, next_in_flight);
    }

    /* Restart all dependent requests */
    if (!qemu_co_queue_empty(&m->dependent_requests)) {
        qemu_co_mutex_unlock(&s->lock);
        while(qemu_co_queue_next(&m->dependent_requests));
        qemu_co_mutex_lock(&s->lock);
    }
}

/*
 * Returns 0 when the request is completed successfully, 1 when there is still
 * a part left to do and -errno in error cases.
 */
static int qcow2_aio_write_cb(QCowAIOCB *acb)
{
    BlockDriverState *bs = acb->common.bs;
    BDRVQcowState *s = bs->opaque;
    int index_in_cluster;
    int n_end;
    int ret;

    ret = qcow2_alloc_cluster_link_l2(bs, &acb->l2meta);

    run_dependent_requests(s, &acb->l2meta);

    if (ret < 0) {
        return ret;
    }

    acb->remaining_sectors -= acb->cur_nr_sectors;
    acb->sector_num += acb->cur_nr_sectors;
    acb->bytes_done += acb->cur_nr_sectors * 512;

    if (acb->remaining_sectors == 0) {
        /* request completed */
        return 0;
    }

    index_in_cluster = acb->sector_num & (s->cluster_sectors - 1);
    n_end = index_in_cluster + acb->remaining_sectors;
    if (s->crypt_method &&
        n_end > QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors)
        n_end = QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors;

    ret = qcow2_alloc_cluster_offset(bs, acb->sector_num << 9,
        index_in_cluster, n_end, &acb->cur_nr_sectors, &acb->l2meta);
    if (ret < 0) {
        return ret;
    }

    acb->cluster_offset = acb->l2meta.cluster_offset;
    assert((acb->cluster_offset & 511) == 0);

    qemu_iovec_reset(&acb->hd_qiov);
    qemu_iovec_copy(&acb->hd_qiov, acb->qiov, acb->bytes_done,
        acb->cur_nr_sectors * 512);

    if (s->crypt_method) {
        if (!acb->cluster_data) {
            acb->cluster_data = g_malloc0(QCOW_MAX_CRYPT_CLUSTERS *
                                             s->cluster_size);
        }

        assert(acb->hd_qiov.size <= QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size);
        qemu_iovec_to_buffer(&acb->hd_qiov, acb->cluster_data);

        qcow2_encrypt_sectors(s, acb->sector_num, acb->cluster_data,
            acb->cluster_data, acb->cur_nr_sectors, 1, &s->aes_encrypt_key);

        qemu_iovec_reset(&acb->hd_qiov);
        qemu_iovec_add(&acb->hd_qiov, acb->cluster_data,
            acb->cur_nr_sectors * 512);
    }

    BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
    qemu_co_mutex_unlock(&s->lock);
    ret = bdrv_co_writev(bs->file,
                         (acb->cluster_offset >> 9) + index_in_cluster,
                         acb->cur_nr_sectors, &acb->hd_qiov);
    qemu_co_mutex_lock(&s->lock);
    if (ret < 0) {
        return ret;
    }

    return 1;
}

static int qcow2_co_writev(BlockDriverState *bs,
                           int64_t sector_num,
                           int nb_sectors,
                           QEMUIOVector *qiov)
{
    BDRVQcowState *s = bs->opaque;
    QCowAIOCB *acb;
    int ret;

    acb = qcow2_aio_setup(bs, sector_num, qiov, nb_sectors, NULL, NULL, 1);
    s->cluster_cache_offset = -1; /* disable compressed cache */

    qemu_co_mutex_lock(&s->lock);
    do {
        ret = qcow2_aio_write_cb(acb);
    } while (ret > 0);
    qemu_co_mutex_unlock(&s->lock);

    qemu_iovec_destroy(&acb->hd_qiov);
    qemu_aio_release(acb);

    return ret;
}

static void qcow2_close(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    g_free(s->l1_table);

    qcow2_cache_flush(bs, s->l2_table_cache);
    qcow2_cache_flush(bs, s->refcount_block_cache);

    qcow2_cache_destroy(bs, s->l2_table_cache);
    qcow2_cache_destroy(bs, s->refcount_block_cache);

    g_free(s->cluster_cache);
    g_free(s->cluster_data);
    qcow2_refcount_close(bs);
}

/*
 * Updates the variable length parts of the qcow2 header, i.e. the backing file
 * name and all extensions. qcow2 was not designed to allow such changes, so if
 * we run out of space (we can only use the first cluster) this function may
 * fail.
 *
 * Returns 0 on success, -errno in error cases.
 */
static int qcow2_update_ext_header(BlockDriverState *bs,
    const char *backing_file, const char *backing_fmt)
{
    size_t backing_file_len = 0;
    size_t backing_fmt_len = 0;
    BDRVQcowState *s = bs->opaque;
    QCowExtension ext_backing_fmt = {0, 0};
    int ret;

    /* Backing file format doesn't make sense without a backing file */
    if (backing_fmt && !backing_file) {
        return -EINVAL;
    }

    /* Prepare the backing file format extension if needed */
    if (backing_fmt) {
        ext_backing_fmt.len = cpu_to_be32(strlen(backing_fmt));
        ext_backing_fmt.magic = cpu_to_be32(QCOW2_EXT_MAGIC_BACKING_FORMAT);
        backing_fmt_len = ((sizeof(ext_backing_fmt)
            + strlen(backing_fmt) + 7) & ~7);
    }

    /* Check if we can fit the new header into the first cluster */
    if (backing_file) {
        backing_file_len = strlen(backing_file);
    }

    size_t header_size = sizeof(QCowHeader) + backing_file_len
        + backing_fmt_len;

    if (header_size > s->cluster_size) {
        return -ENOSPC;
    }

    /* Rewrite backing file name and qcow2 extensions */
    size_t ext_size = header_size - sizeof(QCowHeader);
    uint8_t buf[ext_size];
    size_t offset = 0;
    size_t backing_file_offset = 0;

    if (backing_file) {
        if (backing_fmt) {
            int padding = backing_fmt_len -
                (sizeof(ext_backing_fmt) + strlen(backing_fmt));

            memcpy(buf + offset, &ext_backing_fmt, sizeof(ext_backing_fmt));
            offset += sizeof(ext_backing_fmt);

            memcpy(buf + offset, backing_fmt, strlen(backing_fmt));
            offset += strlen(backing_fmt);

            memset(buf + offset, 0, padding);
            offset += padding;
        }

        memcpy(buf + offset, backing_file, backing_file_len);
        backing_file_offset = sizeof(QCowHeader) + offset;
    }

    ret = bdrv_pwrite_sync(bs->file, sizeof(QCowHeader), buf, ext_size);
    if (ret < 0) {
        goto fail;
    }

    /* Update header fields */
    uint64_t be_backing_file_offset = cpu_to_be64(backing_file_offset);
    uint32_t be_backing_file_size = cpu_to_be32(backing_file_len);

    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, backing_file_offset),
        &be_backing_file_offset, sizeof(uint64_t));
    if (ret < 0) {
        goto fail;
    }

    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, backing_file_size),
        &be_backing_file_size, sizeof(uint32_t));
    if (ret < 0) {
        goto fail;
    }

    ret = 0;
fail:
    return ret;
}

static int qcow2_change_backing_file(BlockDriverState *bs,
    const char *backing_file, const char *backing_fmt)
{
    return qcow2_update_ext_header(bs, backing_file, backing_fmt);
}

static int preallocate(BlockDriverState *bs)
{
    uint64_t nb_sectors;
    uint64_t offset;
    int num;
    int ret;
    QCowL2Meta meta;

    nb_sectors = bdrv_getlength(bs) >> 9;
    offset = 0;
    qemu_co_queue_init(&meta.dependent_requests);
    meta.cluster_offset = 0;

    while (nb_sectors) {
        num = MIN(nb_sectors, INT_MAX >> 9);
        ret = qcow2_alloc_cluster_offset(bs, offset, 0, num, &num, &meta);
        if (ret < 0) {
            return ret;
        }

        ret = qcow2_alloc_cluster_link_l2(bs, &meta);
        if (ret < 0) {
            qcow2_free_any_clusters(bs, meta.cluster_offset, meta.nb_clusters);
            return ret;
        }

        /* There are no dependent requests, but we need to remove our request
         * from the list of in-flight requests */
        run_dependent_requests(bs->opaque, &meta);

        /* TODO Preallocate data if requested */

        nb_sectors -= num;
        offset += num << 9;
    }

    /*
     * It is expected that the image file is large enough to actually contain
     * all of the allocated clusters (otherwise we get failing reads after
     * EOF). Extend the image to the last allocated sector.
     */
    if (meta.cluster_offset != 0) {
        uint8_t buf[512];
        memset(buf, 0, 512);
        ret = bdrv_write(bs->file, (meta.cluster_offset >> 9) + num - 1, buf, 1);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int qcow2_create2(const char *filename, int64_t total_size,
                         const char *backing_file, const char *backing_format,
                         int flags, size_t cluster_size, int prealloc,
                         QEMUOptionParameter *options)
{
    /* Calulate cluster_bits */
    int cluster_bits;
    cluster_bits = ffs(cluster_size) - 1;
    if (cluster_bits < MIN_CLUSTER_BITS || cluster_bits > MAX_CLUSTER_BITS ||
        (1 << cluster_bits) != cluster_size)
    {
        error_report(
            "Cluster size must be a power of two between %d and %dk",
            1 << MIN_CLUSTER_BITS, 1 << (MAX_CLUSTER_BITS - 10));
        return -EINVAL;
    }

    /*
     * Open the image file and write a minimal qcow2 header.
     *
     * We keep things simple and start with a zero-sized image. We also
     * do without refcount blocks or a L1 table for now. We'll fix the
     * inconsistency later.
     *
     * We do need a refcount table because growing the refcount table means
     * allocating two new refcount blocks - the seconds of which would be at
     * 2 GB for 64k clusters, and we don't want to have a 2 GB initial file
     * size for any qcow2 image.
     */
    BlockDriverState* bs;
    QCowHeader header;
    uint8_t* refcount_table;
    int ret;

    ret = bdrv_create_file(filename, options);
    if (ret < 0) {
        return ret;
    }

    ret = bdrv_file_open(&bs, filename, BDRV_O_RDWR);
    if (ret < 0) {
        return ret;
    }

    /* Write the header */
    memset(&header, 0, sizeof(header));
    header.magic = cpu_to_be32(QCOW_MAGIC);
    header.version = cpu_to_be32(QCOW_VERSION);
    header.cluster_bits = cpu_to_be32(cluster_bits);
    header.size = cpu_to_be64(0);
    header.l1_table_offset = cpu_to_be64(0);
    header.l1_size = cpu_to_be32(0);
    header.refcount_table_offset = cpu_to_be64(cluster_size);
    header.refcount_table_clusters = cpu_to_be32(1);

    if (flags & BLOCK_FLAG_ENCRYPT) {
        header.crypt_method = cpu_to_be32(QCOW_CRYPT_AES);
    } else {
        header.crypt_method = cpu_to_be32(QCOW_CRYPT_NONE);
    }

    ret = bdrv_pwrite(bs, 0, &header, sizeof(header));
    if (ret < 0) {
        goto out;
    }

    /* Write an empty refcount table */
    refcount_table = g_malloc0(cluster_size);
    ret = bdrv_pwrite(bs, cluster_size, refcount_table, cluster_size);
    g_free(refcount_table);

    if (ret < 0) {
        goto out;
    }

    bdrv_close(bs);

    /*
     * And now open the image and make it consistent first (i.e. increase the
     * refcount of the cluster that is occupied by the header and the refcount
     * table)
     */
    BlockDriver* drv = bdrv_find_format("qcow2");
    assert(drv != NULL);
    ret = bdrv_open(bs, filename,
        BDRV_O_RDWR | BDRV_O_CACHE_WB | BDRV_O_NO_FLUSH, drv);
    if (ret < 0) {
        goto out;
    }

    ret = qcow2_alloc_clusters(bs, 2 * cluster_size);
    if (ret < 0) {
        goto out;

    } else if (ret != 0) {
        error_report("Huh, first cluster in empty image is already in use?");
        abort();
    }

    /* Okay, now that we have a valid image, let's give it the right size */
    ret = bdrv_truncate(bs, total_size * BDRV_SECTOR_SIZE);
    if (ret < 0) {
        goto out;
    }

    /* Want a backing file? There you go.*/
    if (backing_file) {
        ret = bdrv_change_backing_file(bs, backing_file, backing_format);
        if (ret < 0) {
            goto out;
        }
    }

    /* And if we're supposed to preallocate metadata, do that now */
    if (prealloc) {
        ret = preallocate(bs);
        if (ret < 0) {
            goto out;
        }
    }

    ret = 0;
out:
    bdrv_delete(bs);
    return ret;
}

static int qcow2_create(const char *filename, QEMUOptionParameter *options)
{
    const char *backing_file = NULL;
    const char *backing_fmt = NULL;
    uint64_t sectors = 0;
    int flags = 0;
    size_t cluster_size = DEFAULT_CLUSTER_SIZE;
    int prealloc = 0;

    /* Read out options */
    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            sectors = options->value.n / 512;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FILE)) {
            backing_file = options->value.s;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FMT)) {
            backing_fmt = options->value.s;
        } else if (!strcmp(options->name, BLOCK_OPT_ENCRYPT)) {
            flags |= options->value.n ? BLOCK_FLAG_ENCRYPT : 0;
        } else if (!strcmp(options->name, BLOCK_OPT_CLUSTER_SIZE)) {
            if (options->value.n) {
                cluster_size = options->value.n;
            }
        } else if (!strcmp(options->name, BLOCK_OPT_PREALLOC)) {
            if (!options->value.s || !strcmp(options->value.s, "off")) {
                prealloc = 0;
            } else if (!strcmp(options->value.s, "metadata")) {
                prealloc = 1;
            } else {
                fprintf(stderr, "Invalid preallocation mode: '%s'\n",
                    options->value.s);
                return -EINVAL;
            }
        }
        options++;
    }

    if (backing_file && prealloc) {
        fprintf(stderr, "Backing file and preallocation cannot be used at "
            "the same time\n");
        return -EINVAL;
    }

    return qcow2_create2(filename, sectors, backing_file, backing_fmt, flags,
                         cluster_size, prealloc, options);
}

static int qcow2_make_empty(BlockDriverState *bs)
{
#if 0
    /* XXX: not correct */
    BDRVQcowState *s = bs->opaque;
    uint32_t l1_length = s->l1_size * sizeof(uint64_t);
    int ret;

    memset(s->l1_table, 0, l1_length);
    if (bdrv_pwrite(bs->file, s->l1_table_offset, s->l1_table, l1_length) < 0)
        return -1;
    ret = bdrv_truncate(bs->file, s->l1_table_offset + l1_length);
    if (ret < 0)
        return ret;

    l2_cache_reset(bs);
#endif
    return 0;
}

static int qcow2_discard(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors)
{
    return qcow2_discard_clusters(bs, sector_num << BDRV_SECTOR_BITS,
        nb_sectors);
}

static int qcow2_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVQcowState *s = bs->opaque;
    int ret, new_l1_size;

    if (offset & 511) {
        return -EINVAL;
    }

    /* cannot proceed if image has snapshots */
    if (s->nb_snapshots) {
        return -ENOTSUP;
    }

    /* shrinking is currently not supported */
    if (offset < bs->total_sectors * 512) {
        return -ENOTSUP;
    }

    new_l1_size = size_to_l1(s, offset);
    ret = qcow2_grow_l1_table(bs, new_l1_size, true);
    if (ret < 0) {
        return ret;
    }

    /* write updated header.size */
    offset = cpu_to_be64(offset);
    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, size),
                           &offset, sizeof(uint64_t));
    if (ret < 0) {
        return ret;
    }

    s->l1_vm_state_index = new_l1_size;
    return 0;
}

/* XXX: put compressed sectors first, then all the cluster aligned
   tables to avoid losing bytes in alignment */
static int qcow2_write_compressed(BlockDriverState *bs, int64_t sector_num,
                                  const uint8_t *buf, int nb_sectors)
{
    BDRVQcowState *s = bs->opaque;
    z_stream strm;
    int ret, out_len;
    uint8_t *out_buf;
    uint64_t cluster_offset;

    if (nb_sectors == 0) {
        /* align end of file to a sector boundary to ease reading with
           sector based I/Os */
        cluster_offset = bdrv_getlength(bs->file);
        cluster_offset = (cluster_offset + 511) & ~511;
        bdrv_truncate(bs->file, cluster_offset);
        return 0;
    }

    if (nb_sectors != s->cluster_sectors)
        return -EINVAL;

    out_buf = g_malloc(s->cluster_size + (s->cluster_size / 1000) + 128);

    /* best compression, small window, no zlib header */
    memset(&strm, 0, sizeof(strm));
    ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION,
                       Z_DEFLATED, -12,
                       9, Z_DEFAULT_STRATEGY);
    if (ret != 0) {
        g_free(out_buf);
        return -1;
    }

    strm.avail_in = s->cluster_size;
    strm.next_in = (uint8_t *)buf;
    strm.avail_out = s->cluster_size;
    strm.next_out = out_buf;

    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END && ret != Z_OK) {
        g_free(out_buf);
        deflateEnd(&strm);
        return -1;
    }
    out_len = strm.next_out - out_buf;

    deflateEnd(&strm);

    if (ret != Z_STREAM_END || out_len >= s->cluster_size) {
        /* could not compress: write normal cluster */
        bdrv_write(bs, sector_num, buf, s->cluster_sectors);
    } else {
        cluster_offset = qcow2_alloc_compressed_cluster_offset(bs,
            sector_num << 9, out_len);
        if (!cluster_offset)
            return -1;
        cluster_offset &= s->cluster_offset_mask;
        BLKDBG_EVENT(bs->file, BLKDBG_WRITE_COMPRESSED);
        if (bdrv_pwrite(bs->file, cluster_offset, out_buf, out_len) != out_len) {
            g_free(out_buf);
            return -1;
        }
    }

    g_free(out_buf);
    return 0;
}

static int qcow2_flush(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    int ret;

    ret = qcow2_cache_flush(bs, s->l2_table_cache);
    if (ret < 0) {
        return ret;
    }

    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        return ret;
    }

    return bdrv_flush(bs->file);
}

static BlockDriverAIOCB *qcow2_aio_flush(BlockDriverState *bs,
                                         BlockDriverCompletionFunc *cb,
                                         void *opaque)
{
    BDRVQcowState *s = bs->opaque;
    int ret;

    ret = qcow2_cache_flush(bs, s->l2_table_cache);
    if (ret < 0) {
        return NULL;
    }

    ret = qcow2_cache_flush(bs, s->refcount_block_cache);
    if (ret < 0) {
        return NULL;
    }

    return bdrv_aio_flush(bs->file, cb, opaque);
}

static int64_t qcow2_vm_state_offset(BDRVQcowState *s)
{
	return (int64_t)s->l1_vm_state_index << (s->cluster_bits + s->l2_bits);
}

static int qcow2_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVQcowState *s = bs->opaque;
    bdi->cluster_size = s->cluster_size;
    bdi->vm_state_offset = qcow2_vm_state_offset(s);
    return 0;
}


static int qcow2_check(BlockDriverState *bs, BdrvCheckResult *result)
{
    return qcow2_check_refcounts(bs, result);
}

#if 0
static void dump_refcounts(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    int64_t nb_clusters, k, k1, size;
    int refcount;

    size = bdrv_getlength(bs->file);
    nb_clusters = size_to_clusters(s, size);
    for(k = 0; k < nb_clusters;) {
        k1 = k;
        refcount = get_refcount(bs, k);
        k++;
        while (k < nb_clusters && get_refcount(bs, k) == refcount)
            k++;
        printf("%" PRId64 ": refcount=%d nb=%" PRId64 "\n", k, refcount,
               k - k1);
    }
}
#endif

static int qcow2_save_vmstate(BlockDriverState *bs, const uint8_t *buf,
                              int64_t pos, int size)
{
    BDRVQcowState *s = bs->opaque;
    int growable = bs->growable;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_SAVE);
    bs->growable = 1;
    ret = bdrv_pwrite(bs, qcow2_vm_state_offset(s) + pos, buf, size);
    bs->growable = growable;

    return ret;
}

static int qcow2_load_vmstate(BlockDriverState *bs, uint8_t *buf,
                              int64_t pos, int size)
{
    BDRVQcowState *s = bs->opaque;
    int growable = bs->growable;
    int ret;

    BLKDBG_EVENT(bs->file, BLKDBG_VMSTATE_LOAD);
    bs->growable = 1;
    ret = bdrv_pread(bs, qcow2_vm_state_offset(s) + pos, buf, size);
    bs->growable = growable;

    return ret;
}

static QEMUOptionParameter qcow2_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
    {
        .name = BLOCK_OPT_BACKING_FILE,
        .type = OPT_STRING,
        .help = "File name of a base image"
    },
    {
        .name = BLOCK_OPT_BACKING_FMT,
        .type = OPT_STRING,
        .help = "Image format of the base image"
    },
    {
        .name = BLOCK_OPT_ENCRYPT,
        .type = OPT_FLAG,
        .help = "Encrypt the image"
    },
    {
        .name = BLOCK_OPT_CLUSTER_SIZE,
        .type = OPT_SIZE,
        .help = "qcow2 cluster size",
        .value = { .n = DEFAULT_CLUSTER_SIZE },
    },
    {
        .name = BLOCK_OPT_PREALLOC,
        .type = OPT_STRING,
        .help = "Preallocation mode (allowed values: off, metadata)"
    },
    { NULL }
};

static BlockDriver bdrv_qcow2 = {
    .format_name        = "qcow2",
    .instance_size      = sizeof(BDRVQcowState),
    .bdrv_probe         = qcow2_probe,
    .bdrv_open          = qcow2_open,
    .bdrv_close         = qcow2_close,
    .bdrv_create        = qcow2_create,
    .bdrv_flush         = qcow2_flush,
    .bdrv_is_allocated  = qcow2_is_allocated,
    .bdrv_set_key       = qcow2_set_key,
    .bdrv_make_empty    = qcow2_make_empty,

    .bdrv_co_readv      = qcow2_co_readv,
    .bdrv_co_writev     = qcow2_co_writev,
    .bdrv_aio_flush     = qcow2_aio_flush,

    .bdrv_discard           = qcow2_discard,
    .bdrv_truncate          = qcow2_truncate,
    .bdrv_write_compressed  = qcow2_write_compressed,

    .bdrv_snapshot_create   = qcow2_snapshot_create,
    .bdrv_snapshot_goto     = qcow2_snapshot_goto,
    .bdrv_snapshot_delete   = qcow2_snapshot_delete,
    .bdrv_snapshot_list     = qcow2_snapshot_list,
    .bdrv_snapshot_load_tmp     = qcow2_snapshot_load_tmp,
    .bdrv_get_info      = qcow2_get_info,

    .bdrv_save_vmstate    = qcow2_save_vmstate,
    .bdrv_load_vmstate    = qcow2_load_vmstate,

    .bdrv_change_backing_file   = qcow2_change_backing_file,

    .create_options = qcow2_create_options,
    .bdrv_check = qcow2_check,
};

static void bdrv_qcow2_init(void)
{
    bdrv_register(&bdrv_qcow2);
}

block_init(bdrv_qcow2_init);
