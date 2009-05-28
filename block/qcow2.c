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

//#define DEBUG_ALLOC
//#define DEBUG_ALLOC2
//#define DEBUG_EXT


typedef struct {
    uint32_t magic;
    uint32_t len;
} QCowExtension;
#define  QCOW_EXT_MAGIC_END 0
#define  QCOW_EXT_MAGIC_BACKING_FORMAT 0xE2792ACA



static int qcow_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const QCowHeader *cow_header = (const void *)buf;

    if (buf_size >= sizeof(QCowHeader) &&
        be32_to_cpu(cow_header->magic) == QCOW_MAGIC &&
        be32_to_cpu(cow_header->version) == QCOW_VERSION)
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
static int qcow_read_extensions(BlockDriverState *bs, uint64_t start_offset,
                                uint64_t end_offset)
{
    BDRVQcowState *s = bs->opaque;
    QCowExtension ext;
    uint64_t offset;

#ifdef DEBUG_EXT
    printf("qcow_read_extensions: start=%ld end=%ld\n", start_offset, end_offset);
#endif
    offset = start_offset;
    while (offset < end_offset) {

#ifdef DEBUG_EXT
        /* Sanity check */
        if (offset > s->cluster_size)
            printf("qcow_handle_extension: suspicious offset %lu\n", offset);

        printf("attemting to read extended header in offset %lu\n", offset);
#endif

        if (bdrv_pread(s->hd, offset, &ext, sizeof(ext)) != sizeof(ext)) {
            fprintf(stderr, "qcow_handle_extension: ERROR: pread fail from offset %llu\n",
                    (unsigned long long)offset);
            return 1;
        }
        be32_to_cpus(&ext.magic);
        be32_to_cpus(&ext.len);
        offset += sizeof(ext);
#ifdef DEBUG_EXT
        printf("ext.magic = 0x%x\n", ext.magic);
#endif
        switch (ext.magic) {
        case QCOW_EXT_MAGIC_END:
            return 0;

        case QCOW_EXT_MAGIC_BACKING_FORMAT:
            if (ext.len >= sizeof(bs->backing_format)) {
                fprintf(stderr, "ERROR: ext_backing_format: len=%u too large"
                        " (>=%zu)\n",
                        ext.len, sizeof(bs->backing_format));
                return 2;
            }
            if (bdrv_pread(s->hd, offset , bs->backing_format,
                           ext.len) != ext.len)
                return 3;
            bs->backing_format[ext.len] = '\0';
#ifdef DEBUG_EXT
            printf("Qcow2: Got format extension %s\n", bs->backing_format);
#endif
            offset += ((ext.len + 7) & ~7);
            break;

        default:
            /* unknown magic -- just skip it */
            offset += ((ext.len + 7) & ~7);
            break;
        }
    }

    return 0;
}


static int qcow_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVQcowState *s = bs->opaque;
    int len, i, shift, ret;
    QCowHeader header;
    uint64_t ext_end;

    /* Performance is terrible right now with cache=writethrough due mainly
     * to reference count updates.  If the user does not explicitly specify
     * a caching type, force to writeback caching.
     */
    if ((flags & BDRV_O_CACHE_DEF)) {
        flags |= BDRV_O_CACHE_WB;
        flags &= ~BDRV_O_CACHE_DEF;
    }
    ret = bdrv_file_open(&s->hd, filename, flags);
    if (ret < 0)
        return ret;
    if (bdrv_pread(s->hd, 0, &header, sizeof(header)) != sizeof(header))
        goto fail;
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

    if (header.magic != QCOW_MAGIC || header.version != QCOW_VERSION)
        goto fail;
    if (header.size <= 1 ||
        header.cluster_bits < MIN_CLUSTER_BITS ||
        header.cluster_bits > MAX_CLUSTER_BITS)
        goto fail;
    if (header.crypt_method > QCOW_CRYPT_AES)
        goto fail;
    s->crypt_method_header = header.crypt_method;
    if (s->crypt_method_header)
        bs->encrypted = 1;
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
    shift = s->cluster_bits + s->l2_bits;
    s->l1_vm_state_index = (header.size + (1LL << shift) - 1) >> shift;
    /* the L1 table must contain at least enough entries to put
       header.size bytes */
    if (s->l1_size < s->l1_vm_state_index)
        goto fail;
    s->l1_table_offset = header.l1_table_offset;
    s->l1_table = qemu_malloc(s->l1_size * sizeof(uint64_t));
    if (bdrv_pread(s->hd, s->l1_table_offset, s->l1_table, s->l1_size * sizeof(uint64_t)) !=
        s->l1_size * sizeof(uint64_t))
        goto fail;
    for(i = 0;i < s->l1_size; i++) {
        be64_to_cpus(&s->l1_table[i]);
    }
    /* alloc L2 cache */
    s->l2_cache = qemu_malloc(s->l2_size * L2_CACHE_SIZE * sizeof(uint64_t));
    s->cluster_cache = qemu_malloc(s->cluster_size);
    /* one more sector for decompressed data alignment */
    s->cluster_data = qemu_malloc(QCOW_MAX_CRYPT_CLUSTERS * s->cluster_size
                                  + 512);
    s->cluster_cache_offset = -1;

    if (qcow2_refcount_init(bs) < 0)
        goto fail;

    /* read qcow2 extensions */
    if (header.backing_file_offset)
        ext_end = header.backing_file_offset;
    else
        ext_end = s->cluster_size;
    if (qcow_read_extensions(bs, sizeof(header), ext_end))
        goto fail;

    /* read the backing file name */
    if (header.backing_file_offset != 0) {
        len = header.backing_file_size;
        if (len > 1023)
            len = 1023;
        if (bdrv_pread(s->hd, header.backing_file_offset, bs->backing_file, len) != len)
            goto fail;
        bs->backing_file[len] = '\0';
    }
    if (qcow2_read_snapshots(bs) < 0)
        goto fail;

#ifdef DEBUG_ALLOC
    check_refcounts(bs);
#endif
    return 0;

 fail:
    qcow2_free_snapshots(bs);
    qcow2_refcount_close(bs);
    qemu_free(s->l1_table);
    qemu_free(s->l2_cache);
    qemu_free(s->cluster_cache);
    qemu_free(s->cluster_data);
    bdrv_delete(s->hd);
    return -1;
}

static int qcow_set_key(BlockDriverState *bs, const char *key)
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

static int qcow_is_allocated(BlockDriverState *bs, int64_t sector_num,
                             int nb_sectors, int *pnum)
{
    uint64_t cluster_offset;

    *pnum = nb_sectors;
    cluster_offset = qcow2_get_cluster_offset(bs, sector_num << 9, pnum);

    return (cluster_offset != 0);
}

/* handle reading after the end of the backing file */
int qcow2_backing_read1(BlockDriverState *bs,
                  int64_t sector_num, uint8_t *buf, int nb_sectors)
{
    int n1;
    if ((sector_num + nb_sectors) <= bs->total_sectors)
        return nb_sectors;
    if (sector_num >= bs->total_sectors)
        n1 = 0;
    else
        n1 = bs->total_sectors - sector_num;
    memset(buf + n1 * 512, 0, 512 * (nb_sectors - n1));
    return n1;
}

typedef struct QCowAIOCB {
    BlockDriverAIOCB common;
    int64_t sector_num;
    QEMUIOVector *qiov;
    uint8_t *buf;
    void *orig_buf;
    int nb_sectors;
    int n;
    uint64_t cluster_offset;
    uint8_t *cluster_data;
    BlockDriverAIOCB *hd_aiocb;
    struct iovec hd_iov;
    QEMUIOVector hd_qiov;
    QEMUBH *bh;
    QCowL2Meta l2meta;
} QCowAIOCB;

static void qcow_aio_cancel(BlockDriverAIOCB *blockacb)
{
    QCowAIOCB *acb = (QCowAIOCB *)blockacb;
    if (acb->hd_aiocb)
        bdrv_aio_cancel(acb->hd_aiocb);
    qemu_aio_release(acb);
}

static AIOPool qcow_aio_pool = {
    .aiocb_size         = sizeof(QCowAIOCB),
    .cancel             = qcow_aio_cancel,
};

static void qcow_aio_read_cb(void *opaque, int ret);
static void qcow_aio_read_bh(void *opaque)
{
    QCowAIOCB *acb = opaque;
    qemu_bh_delete(acb->bh);
    acb->bh = NULL;
    qcow_aio_read_cb(opaque, 0);
}

static int qcow_schedule_bh(QEMUBHFunc *cb, QCowAIOCB *acb)
{
    if (acb->bh)
        return -EIO;

    acb->bh = qemu_bh_new(cb, acb);
    if (!acb->bh)
        return -EIO;

    qemu_bh_schedule(acb->bh);

    return 0;
}

static void qcow_aio_read_cb(void *opaque, int ret)
{
    QCowAIOCB *acb = opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVQcowState *s = bs->opaque;
    int index_in_cluster, n1;

    acb->hd_aiocb = NULL;
    if (ret < 0)
        goto done;

    /* post process the read buffer */
    if (!acb->cluster_offset) {
        /* nothing to do */
    } else if (acb->cluster_offset & QCOW_OFLAG_COMPRESSED) {
        /* nothing to do */
    } else {
        if (s->crypt_method) {
            qcow2_encrypt_sectors(s, acb->sector_num, acb->buf, acb->buf,
                            acb->n, 0,
                            &s->aes_decrypt_key);
        }
    }

    acb->nb_sectors -= acb->n;
    acb->sector_num += acb->n;
    acb->buf += acb->n * 512;

    if (acb->nb_sectors == 0) {
        /* request completed */
        ret = 0;
        goto done;
    }

    /* prepare next AIO request */
    acb->n = acb->nb_sectors;
    acb->cluster_offset =
        qcow2_get_cluster_offset(bs, acb->sector_num << 9, &acb->n);
    index_in_cluster = acb->sector_num & (s->cluster_sectors - 1);

    if (!acb->cluster_offset) {
        if (bs->backing_hd) {
            /* read from the base image */
            n1 = qcow2_backing_read1(bs->backing_hd, acb->sector_num,
                               acb->buf, acb->n);
            if (n1 > 0) {
                acb->hd_iov.iov_base = (void *)acb->buf;
                acb->hd_iov.iov_len = acb->n * 512;
                qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
                acb->hd_aiocb = bdrv_aio_readv(bs->backing_hd, acb->sector_num,
                                    &acb->hd_qiov, acb->n,
				    qcow_aio_read_cb, acb);
                if (acb->hd_aiocb == NULL)
                    goto done;
            } else {
                ret = qcow_schedule_bh(qcow_aio_read_bh, acb);
                if (ret < 0)
                    goto done;
            }
        } else {
            /* Note: in this case, no need to wait */
            memset(acb->buf, 0, 512 * acb->n);
            ret = qcow_schedule_bh(qcow_aio_read_bh, acb);
            if (ret < 0)
                goto done;
        }
    } else if (acb->cluster_offset & QCOW_OFLAG_COMPRESSED) {
        /* add AIO support for compressed blocks ? */
        if (qcow2_decompress_cluster(s, acb->cluster_offset) < 0)
            goto done;
        memcpy(acb->buf,
               s->cluster_cache + index_in_cluster * 512, 512 * acb->n);
        ret = qcow_schedule_bh(qcow_aio_read_bh, acb);
        if (ret < 0)
            goto done;
    } else {
        if ((acb->cluster_offset & 511) != 0) {
            ret = -EIO;
            goto done;
        }

        acb->hd_iov.iov_base = (void *)acb->buf;
        acb->hd_iov.iov_len = acb->n * 512;
        qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
        acb->hd_aiocb = bdrv_aio_readv(s->hd,
                            (acb->cluster_offset >> 9) + index_in_cluster,
                            &acb->hd_qiov, acb->n, qcow_aio_read_cb, acb);
        if (acb->hd_aiocb == NULL)
            goto done;
    }

    return;
done:
    if (acb->qiov->niov > 1) {
        qemu_iovec_from_buffer(acb->qiov, acb->orig_buf, acb->qiov->size);
        qemu_vfree(acb->orig_buf);
    }
    acb->common.cb(acb->common.opaque, ret);
    qemu_aio_release(acb);
}

static QCowAIOCB *qcow_aio_setup(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int is_write)
{
    QCowAIOCB *acb;

    acb = qemu_aio_get(&qcow_aio_pool, bs, cb, opaque);
    if (!acb)
        return NULL;
    acb->hd_aiocb = NULL;
    acb->sector_num = sector_num;
    acb->qiov = qiov;
    if (qiov->niov > 1) {
        acb->buf = acb->orig_buf = qemu_blockalign(bs, qiov->size);
        if (is_write)
            qemu_iovec_to_buffer(qiov, acb->buf);
    } else {
        acb->buf = (uint8_t *)qiov->iov->iov_base;
    }
    acb->nb_sectors = nb_sectors;
    acb->n = 0;
    acb->cluster_offset = 0;
    acb->l2meta.nb_clusters = 0;
    return acb;
}

static BlockDriverAIOCB *qcow_aio_readv(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    QCowAIOCB *acb;

    acb = qcow_aio_setup(bs, sector_num, qiov, nb_sectors, cb, opaque, 0);
    if (!acb)
        return NULL;

    qcow_aio_read_cb(acb, 0);
    return &acb->common;
}

static void qcow_aio_write_cb(void *opaque, int ret)
{
    QCowAIOCB *acb = opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVQcowState *s = bs->opaque;
    int index_in_cluster;
    const uint8_t *src_buf;
    int n_end;

    acb->hd_aiocb = NULL;

    if (ret < 0)
        goto done;

    if (qcow2_alloc_cluster_link_l2(bs, acb->cluster_offset, &acb->l2meta) < 0) {
        qcow2_free_any_clusters(bs, acb->cluster_offset, acb->l2meta.nb_clusters);
        goto done;
    }

    acb->nb_sectors -= acb->n;
    acb->sector_num += acb->n;
    acb->buf += acb->n * 512;

    if (acb->nb_sectors == 0) {
        /* request completed */
        ret = 0;
        goto done;
    }

    index_in_cluster = acb->sector_num & (s->cluster_sectors - 1);
    n_end = index_in_cluster + acb->nb_sectors;
    if (s->crypt_method &&
        n_end > QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors)
        n_end = QCOW_MAX_CRYPT_CLUSTERS * s->cluster_sectors;

    acb->cluster_offset = qcow2_alloc_cluster_offset(bs, acb->sector_num << 9,
                                          index_in_cluster,
                                          n_end, &acb->n, &acb->l2meta);
    if (!acb->cluster_offset || (acb->cluster_offset & 511) != 0) {
        ret = -EIO;
        goto done;
    }
    if (s->crypt_method) {
        if (!acb->cluster_data) {
            acb->cluster_data = qemu_mallocz(QCOW_MAX_CRYPT_CLUSTERS *
                                             s->cluster_size);
        }
        qcow2_encrypt_sectors(s, acb->sector_num, acb->cluster_data, acb->buf,
                        acb->n, 1, &s->aes_encrypt_key);
        src_buf = acb->cluster_data;
    } else {
        src_buf = acb->buf;
    }
    acb->hd_iov.iov_base = (void *)src_buf;
    acb->hd_iov.iov_len = acb->n * 512;
    qemu_iovec_init_external(&acb->hd_qiov, &acb->hd_iov, 1);
    acb->hd_aiocb = bdrv_aio_writev(s->hd,
                                    (acb->cluster_offset >> 9) + index_in_cluster,
                                    &acb->hd_qiov, acb->n,
                                    qcow_aio_write_cb, acb);
    if (acb->hd_aiocb == NULL)
        goto done;

    return;

done:
    if (acb->qiov->niov > 1)
        qemu_vfree(acb->orig_buf);
    acb->common.cb(acb->common.opaque, ret);
    qemu_aio_release(acb);
}

static BlockDriverAIOCB *qcow_aio_writev(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVQcowState *s = bs->opaque;
    QCowAIOCB *acb;

    s->cluster_cache_offset = -1; /* disable compressed cache */

    acb = qcow_aio_setup(bs, sector_num, qiov, nb_sectors, cb, opaque, 1);
    if (!acb)
        return NULL;

    qcow_aio_write_cb(acb, 0);
    return &acb->common;
}

static void qcow_close(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    qemu_free(s->l1_table);
    qemu_free(s->l2_cache);
    qemu_free(s->cluster_cache);
    qemu_free(s->cluster_data);
    qcow2_refcount_close(bs);
    bdrv_delete(s->hd);
}

static int get_bits_from_size(size_t size)
{
    int res = 0;

    if (size == 0) {
        return -1;
    }

    while (size != 1) {
        /* Not a power of two */
        if (size & 1) {
            return -1;
        }

        size >>= 1;
        res++;
    }

    return res;
}

static int qcow_create2(const char *filename, int64_t total_size,
                        const char *backing_file, const char *backing_format,
                        int flags, size_t cluster_size)
{

    int fd, header_size, backing_filename_len, l1_size, i, shift, l2_bits;
    int ref_clusters, backing_format_len = 0;
    QCowHeader header;
    uint64_t tmp, offset;
    QCowCreateState s1, *s = &s1;
    QCowExtension ext_bf = {0, 0};


    memset(s, 0, sizeof(*s));

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0)
        return -1;
    memset(&header, 0, sizeof(header));
    header.magic = cpu_to_be32(QCOW_MAGIC);
    header.version = cpu_to_be32(QCOW_VERSION);
    header.size = cpu_to_be64(total_size * 512);
    header_size = sizeof(header);
    backing_filename_len = 0;
    if (backing_file) {
        if (backing_format) {
            ext_bf.magic = QCOW_EXT_MAGIC_BACKING_FORMAT;
            backing_format_len = strlen(backing_format);
            ext_bf.len = (backing_format_len + 7) & ~7;
            header_size += ((sizeof(ext_bf) + ext_bf.len + 7) & ~7);
        }
        header.backing_file_offset = cpu_to_be64(header_size);
        backing_filename_len = strlen(backing_file);
        header.backing_file_size = cpu_to_be32(backing_filename_len);
        header_size += backing_filename_len;
    }

    /* Cluster size */
    s->cluster_bits = get_bits_from_size(cluster_size);
    if (s->cluster_bits < MIN_CLUSTER_BITS ||
        s->cluster_bits > MAX_CLUSTER_BITS)
    {
        fprintf(stderr, "Cluster size must be a power of two between "
            "%d and %dk\n",
            1 << MIN_CLUSTER_BITS,
            1 << (MAX_CLUSTER_BITS - 10));
        return -EINVAL;
    }
    s->cluster_size = 1 << s->cluster_bits;

    header.cluster_bits = cpu_to_be32(s->cluster_bits);
    header_size = (header_size + 7) & ~7;
    if (flags & BLOCK_FLAG_ENCRYPT) {
        header.crypt_method = cpu_to_be32(QCOW_CRYPT_AES);
    } else {
        header.crypt_method = cpu_to_be32(QCOW_CRYPT_NONE);
    }
    l2_bits = s->cluster_bits - 3;
    shift = s->cluster_bits + l2_bits;
    l1_size = (((total_size * 512) + (1LL << shift) - 1) >> shift);
    offset = align_offset(header_size, s->cluster_size);
    s->l1_table_offset = offset;
    header.l1_table_offset = cpu_to_be64(s->l1_table_offset);
    header.l1_size = cpu_to_be32(l1_size);
    offset += align_offset(l1_size * sizeof(uint64_t), s->cluster_size);

    s->refcount_table = qemu_mallocz(s->cluster_size);

    s->refcount_table_offset = offset;
    header.refcount_table_offset = cpu_to_be64(offset);
    header.refcount_table_clusters = cpu_to_be32(1);
    offset += s->cluster_size;
    s->refcount_block_offset = offset;

    /* count how many refcount blocks needed */
    tmp = offset >> s->cluster_bits;
    ref_clusters = (tmp >> (s->cluster_bits - REFCOUNT_SHIFT)) + 1;
    for (i=0; i < ref_clusters; i++) {
        s->refcount_table[i] = cpu_to_be64(offset);
        offset += s->cluster_size;
    }

    s->refcount_block = qemu_mallocz(ref_clusters * s->cluster_size);

    /* update refcounts */
    qcow2_create_refcount_update(s, 0, header_size);
    qcow2_create_refcount_update(s, s->l1_table_offset,
        l1_size * sizeof(uint64_t));
    qcow2_create_refcount_update(s, s->refcount_table_offset, s->cluster_size);
    qcow2_create_refcount_update(s, s->refcount_block_offset,
        ref_clusters * s->cluster_size);

    /* write all the data */
    write(fd, &header, sizeof(header));
    if (backing_file) {
        if (backing_format_len) {
            char zero[16];
            int d = ext_bf.len - backing_format_len;

            memset(zero, 0, sizeof(zero));
            cpu_to_be32s(&ext_bf.magic);
            cpu_to_be32s(&ext_bf.len);
            write(fd, &ext_bf, sizeof(ext_bf));
            write(fd, backing_format, backing_format_len);
            if (d>0) {
                write(fd, zero, d);
            }
        }
        write(fd, backing_file, backing_filename_len);
    }
    lseek(fd, s->l1_table_offset, SEEK_SET);
    tmp = 0;
    for(i = 0;i < l1_size; i++) {
        write(fd, &tmp, sizeof(tmp));
    }
    lseek(fd, s->refcount_table_offset, SEEK_SET);
    write(fd, s->refcount_table, s->cluster_size);

    lseek(fd, s->refcount_block_offset, SEEK_SET);
    write(fd, s->refcount_block, ref_clusters * s->cluster_size);

    qemu_free(s->refcount_table);
    qemu_free(s->refcount_block);
    close(fd);
    return 0;
}

static int qcow_create(const char *filename, QEMUOptionParameter *options)
{
    const char *backing_file = NULL;
    const char *backing_fmt = NULL;
    uint64_t sectors = 0;
    int flags = 0;
    size_t cluster_size = 65536;

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
        }
        options++;
    }

    return qcow_create2(filename, sectors, backing_file, backing_fmt, flags,
        cluster_size);
}

static int qcow_make_empty(BlockDriverState *bs)
{
#if 0
    /* XXX: not correct */
    BDRVQcowState *s = bs->opaque;
    uint32_t l1_length = s->l1_size * sizeof(uint64_t);
    int ret;

    memset(s->l1_table, 0, l1_length);
    if (bdrv_pwrite(s->hd, s->l1_table_offset, s->l1_table, l1_length) < 0)
        return -1;
    ret = bdrv_truncate(s->hd, s->l1_table_offset + l1_length);
    if (ret < 0)
        return ret;

    l2_cache_reset(bs);
#endif
    return 0;
}

/* XXX: put compressed sectors first, then all the cluster aligned
   tables to avoid losing bytes in alignment */
static int qcow_write_compressed(BlockDriverState *bs, int64_t sector_num,
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
        cluster_offset = bdrv_getlength(s->hd);
        cluster_offset = (cluster_offset + 511) & ~511;
        bdrv_truncate(s->hd, cluster_offset);
        return 0;
    }

    if (nb_sectors != s->cluster_sectors)
        return -EINVAL;

    out_buf = qemu_malloc(s->cluster_size + (s->cluster_size / 1000) + 128);

    /* best compression, small window, no zlib header */
    memset(&strm, 0, sizeof(strm));
    ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION,
                       Z_DEFLATED, -12,
                       9, Z_DEFAULT_STRATEGY);
    if (ret != 0) {
        qemu_free(out_buf);
        return -1;
    }

    strm.avail_in = s->cluster_size;
    strm.next_in = (uint8_t *)buf;
    strm.avail_out = s->cluster_size;
    strm.next_out = out_buf;

    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END && ret != Z_OK) {
        qemu_free(out_buf);
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
        if (bdrv_pwrite(s->hd, cluster_offset, out_buf, out_len) != out_len) {
            qemu_free(out_buf);
            return -1;
        }
    }

    qemu_free(out_buf);
    return 0;
}

static void qcow_flush(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    bdrv_flush(s->hd);
}

static int qcow_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVQcowState *s = bs->opaque;
    bdi->cluster_size = s->cluster_size;
    bdi->vm_state_offset = (int64_t)s->l1_vm_state_index <<
        (s->cluster_bits + s->l2_bits);
    return 0;
}


static int qcow_check(BlockDriverState *bs)
{
    return qcow2_check_refcounts(bs);
}

#if 0
static void dump_refcounts(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    int64_t nb_clusters, k, k1, size;
    int refcount;

    size = bdrv_getlength(s->hd);
    nb_clusters = size_to_clusters(s, size);
    for(k = 0; k < nb_clusters;) {
        k1 = k;
        refcount = get_refcount(bs, k);
        k++;
        while (k < nb_clusters && get_refcount(bs, k) == refcount)
            k++;
        printf("%lld: refcount=%d nb=%lld\n", k, refcount, k - k1);
    }
}
#endif

static int qcow_put_buffer(BlockDriverState *bs, const uint8_t *buf,
                           int64_t pos, int size)
{
    int growable = bs->growable;

    bs->growable = 1;
    bdrv_pwrite(bs, pos, buf, size);
    bs->growable = growable;

    return size;
}

static int qcow_get_buffer(BlockDriverState *bs, uint8_t *buf,
                           int64_t pos, int size)
{
    int growable = bs->growable;
    int ret;

    bs->growable = 1;
    ret = bdrv_pread(bs, pos, buf, size);
    bs->growable = growable;

    return ret;
}

static QEMUOptionParameter qcow_create_options[] = {
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
        .help = "qcow2 cluster size"
    },
    { NULL }
};

static BlockDriver bdrv_qcow2 = {
    .format_name	= "qcow2",
    .instance_size	= sizeof(BDRVQcowState),
    .bdrv_probe		= qcow_probe,
    .bdrv_open		= qcow_open,
    .bdrv_close		= qcow_close,
    .bdrv_create	= qcow_create,
    .bdrv_flush		= qcow_flush,
    .bdrv_is_allocated	= qcow_is_allocated,
    .bdrv_set_key	= qcow_set_key,
    .bdrv_make_empty	= qcow_make_empty,

    .bdrv_aio_readv	= qcow_aio_readv,
    .bdrv_aio_writev	= qcow_aio_writev,
    .bdrv_write_compressed = qcow_write_compressed,

    .bdrv_snapshot_create   = qcow2_snapshot_create,
    .bdrv_snapshot_goto     = qcow2_snapshot_goto,
    .bdrv_snapshot_delete   = qcow2_snapshot_delete,
    .bdrv_snapshot_list     = qcow2_snapshot_list,
    .bdrv_get_info	= qcow_get_info,

    .bdrv_put_buffer    = qcow_put_buffer,
    .bdrv_get_buffer    = qcow_get_buffer,

    .create_options = qcow_create_options,
    .bdrv_check = qcow_check,
};

static void bdrv_qcow2_init(void)
{
    bdrv_register(&bdrv_qcow2);
}

block_init(bdrv_qcow2_init);
