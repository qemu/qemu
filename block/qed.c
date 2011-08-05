/*
 * QEMU Enhanced Disk Format
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu-timer.h"
#include "trace.h"
#include "qed.h"
#include "qerror.h"

static void qed_aio_cancel(BlockDriverAIOCB *blockacb)
{
    QEDAIOCB *acb = (QEDAIOCB *)blockacb;
    bool finished = false;

    /* Wait for the request to finish */
    acb->finished = &finished;
    while (!finished) {
        qemu_aio_wait();
    }
}

static AIOPool qed_aio_pool = {
    .aiocb_size         = sizeof(QEDAIOCB),
    .cancel             = qed_aio_cancel,
};

static int bdrv_qed_probe(const uint8_t *buf, int buf_size,
                          const char *filename)
{
    const QEDHeader *header = (const QEDHeader *)buf;

    if (buf_size < sizeof(*header)) {
        return 0;
    }
    if (le32_to_cpu(header->magic) != QED_MAGIC) {
        return 0;
    }
    return 100;
}

/**
 * Check whether an image format is raw
 *
 * @fmt:    Backing file format, may be NULL
 */
static bool qed_fmt_is_raw(const char *fmt)
{
    return fmt && strcmp(fmt, "raw") == 0;
}

static void qed_header_le_to_cpu(const QEDHeader *le, QEDHeader *cpu)
{
    cpu->magic = le32_to_cpu(le->magic);
    cpu->cluster_size = le32_to_cpu(le->cluster_size);
    cpu->table_size = le32_to_cpu(le->table_size);
    cpu->header_size = le32_to_cpu(le->header_size);
    cpu->features = le64_to_cpu(le->features);
    cpu->compat_features = le64_to_cpu(le->compat_features);
    cpu->autoclear_features = le64_to_cpu(le->autoclear_features);
    cpu->l1_table_offset = le64_to_cpu(le->l1_table_offset);
    cpu->image_size = le64_to_cpu(le->image_size);
    cpu->backing_filename_offset = le32_to_cpu(le->backing_filename_offset);
    cpu->backing_filename_size = le32_to_cpu(le->backing_filename_size);
}

static void qed_header_cpu_to_le(const QEDHeader *cpu, QEDHeader *le)
{
    le->magic = cpu_to_le32(cpu->magic);
    le->cluster_size = cpu_to_le32(cpu->cluster_size);
    le->table_size = cpu_to_le32(cpu->table_size);
    le->header_size = cpu_to_le32(cpu->header_size);
    le->features = cpu_to_le64(cpu->features);
    le->compat_features = cpu_to_le64(cpu->compat_features);
    le->autoclear_features = cpu_to_le64(cpu->autoclear_features);
    le->l1_table_offset = cpu_to_le64(cpu->l1_table_offset);
    le->image_size = cpu_to_le64(cpu->image_size);
    le->backing_filename_offset = cpu_to_le32(cpu->backing_filename_offset);
    le->backing_filename_size = cpu_to_le32(cpu->backing_filename_size);
}

static int qed_write_header_sync(BDRVQEDState *s)
{
    QEDHeader le;
    int ret;

    qed_header_cpu_to_le(&s->header, &le);
    ret = bdrv_pwrite(s->bs->file, 0, &le, sizeof(le));
    if (ret != sizeof(le)) {
        return ret;
    }
    return 0;
}

typedef struct {
    GenericCB gencb;
    BDRVQEDState *s;
    struct iovec iov;
    QEMUIOVector qiov;
    int nsectors;
    uint8_t *buf;
} QEDWriteHeaderCB;

static void qed_write_header_cb(void *opaque, int ret)
{
    QEDWriteHeaderCB *write_header_cb = opaque;

    qemu_vfree(write_header_cb->buf);
    gencb_complete(write_header_cb, ret);
}

static void qed_write_header_read_cb(void *opaque, int ret)
{
    QEDWriteHeaderCB *write_header_cb = opaque;
    BDRVQEDState *s = write_header_cb->s;
    BlockDriverAIOCB *acb;

    if (ret) {
        qed_write_header_cb(write_header_cb, ret);
        return;
    }

    /* Update header */
    qed_header_cpu_to_le(&s->header, (QEDHeader *)write_header_cb->buf);

    acb = bdrv_aio_writev(s->bs->file, 0, &write_header_cb->qiov,
                          write_header_cb->nsectors, qed_write_header_cb,
                          write_header_cb);
    if (!acb) {
        qed_write_header_cb(write_header_cb, -EIO);
    }
}

/**
 * Update header in-place (does not rewrite backing filename or other strings)
 *
 * This function only updates known header fields in-place and does not affect
 * extra data after the QED header.
 */
static void qed_write_header(BDRVQEDState *s, BlockDriverCompletionFunc cb,
                             void *opaque)
{
    /* We must write full sectors for O_DIRECT but cannot necessarily generate
     * the data following the header if an unrecognized compat feature is
     * active.  Therefore, first read the sectors containing the header, update
     * them, and write back.
     */

    BlockDriverAIOCB *acb;
    int nsectors = (sizeof(QEDHeader) + BDRV_SECTOR_SIZE - 1) /
                   BDRV_SECTOR_SIZE;
    size_t len = nsectors * BDRV_SECTOR_SIZE;
    QEDWriteHeaderCB *write_header_cb = gencb_alloc(sizeof(*write_header_cb),
                                                    cb, opaque);

    write_header_cb->s = s;
    write_header_cb->nsectors = nsectors;
    write_header_cb->buf = qemu_blockalign(s->bs, len);
    write_header_cb->iov.iov_base = write_header_cb->buf;
    write_header_cb->iov.iov_len = len;
    qemu_iovec_init_external(&write_header_cb->qiov, &write_header_cb->iov, 1);

    acb = bdrv_aio_readv(s->bs->file, 0, &write_header_cb->qiov, nsectors,
                         qed_write_header_read_cb, write_header_cb);
    if (!acb) {
        qed_write_header_cb(write_header_cb, -EIO);
    }
}

static uint64_t qed_max_image_size(uint32_t cluster_size, uint32_t table_size)
{
    uint64_t table_entries;
    uint64_t l2_size;

    table_entries = (table_size * cluster_size) / sizeof(uint64_t);
    l2_size = table_entries * cluster_size;

    return l2_size * table_entries;
}

static bool qed_is_cluster_size_valid(uint32_t cluster_size)
{
    if (cluster_size < QED_MIN_CLUSTER_SIZE ||
        cluster_size > QED_MAX_CLUSTER_SIZE) {
        return false;
    }
    if (cluster_size & (cluster_size - 1)) {
        return false; /* not power of 2 */
    }
    return true;
}

static bool qed_is_table_size_valid(uint32_t table_size)
{
    if (table_size < QED_MIN_TABLE_SIZE ||
        table_size > QED_MAX_TABLE_SIZE) {
        return false;
    }
    if (table_size & (table_size - 1)) {
        return false; /* not power of 2 */
    }
    return true;
}

static bool qed_is_image_size_valid(uint64_t image_size, uint32_t cluster_size,
                                    uint32_t table_size)
{
    if (image_size % BDRV_SECTOR_SIZE != 0) {
        return false; /* not multiple of sector size */
    }
    if (image_size > qed_max_image_size(cluster_size, table_size)) {
        return false; /* image is too large */
    }
    return true;
}

/**
 * Read a string of known length from the image file
 *
 * @file:       Image file
 * @offset:     File offset to start of string, in bytes
 * @n:          String length in bytes
 * @buf:        Destination buffer
 * @buflen:     Destination buffer length in bytes
 * @ret:        0 on success, -errno on failure
 *
 * The string is NUL-terminated.
 */
static int qed_read_string(BlockDriverState *file, uint64_t offset, size_t n,
                           char *buf, size_t buflen)
{
    int ret;
    if (n >= buflen) {
        return -EINVAL;
    }
    ret = bdrv_pread(file, offset, buf, n);
    if (ret < 0) {
        return ret;
    }
    buf[n] = '\0';
    return 0;
}

/**
 * Allocate new clusters
 *
 * @s:          QED state
 * @n:          Number of contiguous clusters to allocate
 * @ret:        Offset of first allocated cluster
 *
 * This function only produces the offset where the new clusters should be
 * written.  It updates BDRVQEDState but does not make any changes to the image
 * file.
 */
static uint64_t qed_alloc_clusters(BDRVQEDState *s, unsigned int n)
{
    uint64_t offset = s->file_size;
    s->file_size += n * s->header.cluster_size;
    return offset;
}

QEDTable *qed_alloc_table(BDRVQEDState *s)
{
    /* Honor O_DIRECT memory alignment requirements */
    return qemu_blockalign(s->bs,
                           s->header.cluster_size * s->header.table_size);
}

/**
 * Allocate a new zeroed L2 table
 */
static CachedL2Table *qed_new_l2_table(BDRVQEDState *s)
{
    CachedL2Table *l2_table = qed_alloc_l2_cache_entry(&s->l2_cache);

    l2_table->table = qed_alloc_table(s);
    l2_table->offset = qed_alloc_clusters(s, s->header.table_size);

    memset(l2_table->table->offsets, 0,
           s->header.cluster_size * s->header.table_size);
    return l2_table;
}

static void qed_aio_next_io(void *opaque, int ret);

static void qed_plug_allocating_write_reqs(BDRVQEDState *s)
{
    assert(!s->allocating_write_reqs_plugged);

    s->allocating_write_reqs_plugged = true;
}

static void qed_unplug_allocating_write_reqs(BDRVQEDState *s)
{
    QEDAIOCB *acb;

    assert(s->allocating_write_reqs_plugged);

    s->allocating_write_reqs_plugged = false;

    acb = QSIMPLEQ_FIRST(&s->allocating_write_reqs);
    if (acb) {
        qed_aio_next_io(acb, 0);
    }
}

static void qed_finish_clear_need_check(void *opaque, int ret)
{
    /* Do nothing */
}

static void qed_flush_after_clear_need_check(void *opaque, int ret)
{
    BDRVQEDState *s = opaque;

    bdrv_aio_flush(s->bs, qed_finish_clear_need_check, s);

    /* No need to wait until flush completes */
    qed_unplug_allocating_write_reqs(s);
}

static void qed_clear_need_check(void *opaque, int ret)
{
    BDRVQEDState *s = opaque;

    if (ret) {
        qed_unplug_allocating_write_reqs(s);
        return;
    }

    s->header.features &= ~QED_F_NEED_CHECK;
    qed_write_header(s, qed_flush_after_clear_need_check, s);
}

static void qed_need_check_timer_cb(void *opaque)
{
    BDRVQEDState *s = opaque;

    /* The timer should only fire when allocating writes have drained */
    assert(!QSIMPLEQ_FIRST(&s->allocating_write_reqs));

    trace_qed_need_check_timer_cb(s);

    qed_plug_allocating_write_reqs(s);

    /* Ensure writes are on disk before clearing flag */
    bdrv_aio_flush(s->bs, qed_clear_need_check, s);
}

static void qed_start_need_check_timer(BDRVQEDState *s)
{
    trace_qed_start_need_check_timer(s);

    /* Use vm_clock so we don't alter the image file while suspended for
     * migration.
     */
    qemu_mod_timer(s->need_check_timer, qemu_get_clock_ns(vm_clock) +
                   get_ticks_per_sec() * QED_NEED_CHECK_TIMEOUT);
}

/* It's okay to call this multiple times or when no timer is started */
static void qed_cancel_need_check_timer(BDRVQEDState *s)
{
    trace_qed_cancel_need_check_timer(s);
    qemu_del_timer(s->need_check_timer);
}

static int bdrv_qed_open(BlockDriverState *bs, int flags)
{
    BDRVQEDState *s = bs->opaque;
    QEDHeader le_header;
    int64_t file_size;
    int ret;

    s->bs = bs;
    QSIMPLEQ_INIT(&s->allocating_write_reqs);

    ret = bdrv_pread(bs->file, 0, &le_header, sizeof(le_header));
    if (ret < 0) {
        return ret;
    }
    ret = 0; /* ret should always be 0 or -errno */
    qed_header_le_to_cpu(&le_header, &s->header);

    if (s->header.magic != QED_MAGIC) {
        return -EINVAL;
    }
    if (s->header.features & ~QED_FEATURE_MASK) {
        /* image uses unsupported feature bits */
        char buf[64];
        snprintf(buf, sizeof(buf), "%" PRIx64,
            s->header.features & ~QED_FEATURE_MASK);
        qerror_report(QERR_UNKNOWN_BLOCK_FORMAT_FEATURE,
            bs->device_name, "QED", buf);
        return -ENOTSUP;
    }
    if (!qed_is_cluster_size_valid(s->header.cluster_size)) {
        return -EINVAL;
    }

    /* Round down file size to the last cluster */
    file_size = bdrv_getlength(bs->file);
    if (file_size < 0) {
        return file_size;
    }
    s->file_size = qed_start_of_cluster(s, file_size);

    if (!qed_is_table_size_valid(s->header.table_size)) {
        return -EINVAL;
    }
    if (!qed_is_image_size_valid(s->header.image_size,
                                 s->header.cluster_size,
                                 s->header.table_size)) {
        return -EINVAL;
    }
    if (!qed_check_table_offset(s, s->header.l1_table_offset)) {
        return -EINVAL;
    }

    s->table_nelems = (s->header.cluster_size * s->header.table_size) /
                      sizeof(uint64_t);
    s->l2_shift = ffs(s->header.cluster_size) - 1;
    s->l2_mask = s->table_nelems - 1;
    s->l1_shift = s->l2_shift + ffs(s->table_nelems) - 1;

    if ((s->header.features & QED_F_BACKING_FILE)) {
        if ((uint64_t)s->header.backing_filename_offset +
            s->header.backing_filename_size >
            s->header.cluster_size * s->header.header_size) {
            return -EINVAL;
        }

        ret = qed_read_string(bs->file, s->header.backing_filename_offset,
                              s->header.backing_filename_size, bs->backing_file,
                              sizeof(bs->backing_file));
        if (ret < 0) {
            return ret;
        }

        if (s->header.features & QED_F_BACKING_FORMAT_NO_PROBE) {
            pstrcpy(bs->backing_format, sizeof(bs->backing_format), "raw");
        }
    }

    /* Reset unknown autoclear feature bits.  This is a backwards
     * compatibility mechanism that allows images to be opened by older
     * programs, which "knock out" unknown feature bits.  When an image is
     * opened by a newer program again it can detect that the autoclear
     * feature is no longer valid.
     */
    if ((s->header.autoclear_features & ~QED_AUTOCLEAR_FEATURE_MASK) != 0 &&
        !bdrv_is_read_only(bs->file)) {
        s->header.autoclear_features &= QED_AUTOCLEAR_FEATURE_MASK;

        ret = qed_write_header_sync(s);
        if (ret) {
            return ret;
        }

        /* From here on only known autoclear feature bits are valid */
        bdrv_flush(bs->file);
    }

    s->l1_table = qed_alloc_table(s);
    qed_init_l2_cache(&s->l2_cache);

    ret = qed_read_l1_table_sync(s);
    if (ret) {
        goto out;
    }

    /* If image was not closed cleanly, check consistency */
    if (s->header.features & QED_F_NEED_CHECK) {
        /* Read-only images cannot be fixed.  There is no risk of corruption
         * since write operations are not possible.  Therefore, allow
         * potentially inconsistent images to be opened read-only.  This can
         * aid data recovery from an otherwise inconsistent image.
         */
        if (!bdrv_is_read_only(bs->file)) {
            BdrvCheckResult result = {0};

            ret = qed_check(s, &result, true);
            if (ret) {
                goto out;
            }
            if (!result.corruptions && !result.check_errors) {
                /* Ensure fixes reach storage before clearing check bit */
                bdrv_flush(s->bs);

                s->header.features &= ~QED_F_NEED_CHECK;
                qed_write_header_sync(s);
            }
        }
    }

    s->need_check_timer = qemu_new_timer_ns(vm_clock,
                                            qed_need_check_timer_cb, s);

out:
    if (ret) {
        qed_free_l2_cache(&s->l2_cache);
        qemu_vfree(s->l1_table);
    }
    return ret;
}

static void bdrv_qed_close(BlockDriverState *bs)
{
    BDRVQEDState *s = bs->opaque;

    qed_cancel_need_check_timer(s);
    qemu_free_timer(s->need_check_timer);

    /* Ensure writes reach stable storage */
    bdrv_flush(bs->file);

    /* Clean shutdown, no check required on next open */
    if (s->header.features & QED_F_NEED_CHECK) {
        s->header.features &= ~QED_F_NEED_CHECK;
        qed_write_header_sync(s);
    }

    qed_free_l2_cache(&s->l2_cache);
    qemu_vfree(s->l1_table);
}

static int bdrv_qed_flush(BlockDriverState *bs)
{
    return bdrv_flush(bs->file);
}

static int qed_create(const char *filename, uint32_t cluster_size,
                      uint64_t image_size, uint32_t table_size,
                      const char *backing_file, const char *backing_fmt)
{
    QEDHeader header = {
        .magic = QED_MAGIC,
        .cluster_size = cluster_size,
        .table_size = table_size,
        .header_size = 1,
        .features = 0,
        .compat_features = 0,
        .l1_table_offset = cluster_size,
        .image_size = image_size,
    };
    QEDHeader le_header;
    uint8_t *l1_table = NULL;
    size_t l1_size = header.cluster_size * header.table_size;
    int ret = 0;
    BlockDriverState *bs = NULL;

    ret = bdrv_create_file(filename, NULL);
    if (ret < 0) {
        return ret;
    }

    ret = bdrv_file_open(&bs, filename, BDRV_O_RDWR | BDRV_O_CACHE_WB);
    if (ret < 0) {
        return ret;
    }

    /* File must start empty and grow, check truncate is supported */
    ret = bdrv_truncate(bs, 0);
    if (ret < 0) {
        goto out;
    }

    if (backing_file) {
        header.features |= QED_F_BACKING_FILE;
        header.backing_filename_offset = sizeof(le_header);
        header.backing_filename_size = strlen(backing_file);

        if (qed_fmt_is_raw(backing_fmt)) {
            header.features |= QED_F_BACKING_FORMAT_NO_PROBE;
        }
    }

    qed_header_cpu_to_le(&header, &le_header);
    ret = bdrv_pwrite(bs, 0, &le_header, sizeof(le_header));
    if (ret < 0) {
        goto out;
    }
    ret = bdrv_pwrite(bs, sizeof(le_header), backing_file,
                      header.backing_filename_size);
    if (ret < 0) {
        goto out;
    }

    l1_table = qemu_mallocz(l1_size);
    ret = bdrv_pwrite(bs, header.l1_table_offset, l1_table, l1_size);
    if (ret < 0) {
        goto out;
    }

    ret = 0; /* success */
out:
    qemu_free(l1_table);
    bdrv_delete(bs);
    return ret;
}

static int bdrv_qed_create(const char *filename, QEMUOptionParameter *options)
{
    uint64_t image_size = 0;
    uint32_t cluster_size = QED_DEFAULT_CLUSTER_SIZE;
    uint32_t table_size = QED_DEFAULT_TABLE_SIZE;
    const char *backing_file = NULL;
    const char *backing_fmt = NULL;

    while (options && options->name) {
        if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
            image_size = options->value.n;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FILE)) {
            backing_file = options->value.s;
        } else if (!strcmp(options->name, BLOCK_OPT_BACKING_FMT)) {
            backing_fmt = options->value.s;
        } else if (!strcmp(options->name, BLOCK_OPT_CLUSTER_SIZE)) {
            if (options->value.n) {
                cluster_size = options->value.n;
            }
        } else if (!strcmp(options->name, BLOCK_OPT_TABLE_SIZE)) {
            if (options->value.n) {
                table_size = options->value.n;
            }
        }
        options++;
    }

    if (!qed_is_cluster_size_valid(cluster_size)) {
        fprintf(stderr, "QED cluster size must be within range [%u, %u] and power of 2\n",
                QED_MIN_CLUSTER_SIZE, QED_MAX_CLUSTER_SIZE);
        return -EINVAL;
    }
    if (!qed_is_table_size_valid(table_size)) {
        fprintf(stderr, "QED table size must be within range [%u, %u] and power of 2\n",
                QED_MIN_TABLE_SIZE, QED_MAX_TABLE_SIZE);
        return -EINVAL;
    }
    if (!qed_is_image_size_valid(image_size, cluster_size, table_size)) {
        fprintf(stderr, "QED image size must be a non-zero multiple of "
                        "cluster size and less than %" PRIu64 " bytes\n",
                qed_max_image_size(cluster_size, table_size));
        return -EINVAL;
    }

    return qed_create(filename, cluster_size, image_size, table_size,
                      backing_file, backing_fmt);
}

typedef struct {
    int is_allocated;
    int *pnum;
} QEDIsAllocatedCB;

static void qed_is_allocated_cb(void *opaque, int ret, uint64_t offset, size_t len)
{
    QEDIsAllocatedCB *cb = opaque;
    *cb->pnum = len / BDRV_SECTOR_SIZE;
    cb->is_allocated = (ret == QED_CLUSTER_FOUND || ret == QED_CLUSTER_ZERO);
}

static int bdrv_qed_is_allocated(BlockDriverState *bs, int64_t sector_num,
                                  int nb_sectors, int *pnum)
{
    BDRVQEDState *s = bs->opaque;
    uint64_t pos = (uint64_t)sector_num * BDRV_SECTOR_SIZE;
    size_t len = (size_t)nb_sectors * BDRV_SECTOR_SIZE;
    QEDIsAllocatedCB cb = {
        .is_allocated = -1,
        .pnum = pnum,
    };
    QEDRequest request = { .l2_table = NULL };

    qed_find_cluster(s, &request, pos, len, qed_is_allocated_cb, &cb);

    while (cb.is_allocated == -1) {
        qemu_aio_wait();
    }

    qed_unref_l2_cache_entry(request.l2_table);

    return cb.is_allocated;
}

static int bdrv_qed_make_empty(BlockDriverState *bs)
{
    return -ENOTSUP;
}

static BDRVQEDState *acb_to_s(QEDAIOCB *acb)
{
    return acb->common.bs->opaque;
}

/**
 * Read from the backing file or zero-fill if no backing file
 *
 * @s:          QED state
 * @pos:        Byte position in device
 * @qiov:       Destination I/O vector
 * @cb:         Completion function
 * @opaque:     User data for completion function
 *
 * This function reads qiov->size bytes starting at pos from the backing file.
 * If there is no backing file then zeroes are read.
 */
static void qed_read_backing_file(BDRVQEDState *s, uint64_t pos,
                                  QEMUIOVector *qiov,
                                  BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriverAIOCB *aiocb;
    uint64_t backing_length = 0;
    size_t size;

    /* If there is a backing file, get its length.  Treat the absence of a
     * backing file like a zero length backing file.
     */
    if (s->bs->backing_hd) {
        int64_t l = bdrv_getlength(s->bs->backing_hd);
        if (l < 0) {
            cb(opaque, l);
            return;
        }
        backing_length = l;
    }

    /* Zero all sectors if reading beyond the end of the backing file */
    if (pos >= backing_length ||
        pos + qiov->size > backing_length) {
        qemu_iovec_memset(qiov, 0, qiov->size);
    }

    /* Complete now if there are no backing file sectors to read */
    if (pos >= backing_length) {
        cb(opaque, 0);
        return;
    }

    /* If the read straddles the end of the backing file, shorten it */
    size = MIN((uint64_t)backing_length - pos, qiov->size);

    BLKDBG_EVENT(s->bs->file, BLKDBG_READ_BACKING);
    aiocb = bdrv_aio_readv(s->bs->backing_hd, pos / BDRV_SECTOR_SIZE,
                           qiov, size / BDRV_SECTOR_SIZE, cb, opaque);
    if (!aiocb) {
        cb(opaque, -EIO);
    }
}

typedef struct {
    GenericCB gencb;
    BDRVQEDState *s;
    QEMUIOVector qiov;
    struct iovec iov;
    uint64_t offset;
} CopyFromBackingFileCB;

static void qed_copy_from_backing_file_cb(void *opaque, int ret)
{
    CopyFromBackingFileCB *copy_cb = opaque;
    qemu_vfree(copy_cb->iov.iov_base);
    gencb_complete(&copy_cb->gencb, ret);
}

static void qed_copy_from_backing_file_write(void *opaque, int ret)
{
    CopyFromBackingFileCB *copy_cb = opaque;
    BDRVQEDState *s = copy_cb->s;
    BlockDriverAIOCB *aiocb;

    if (ret) {
        qed_copy_from_backing_file_cb(copy_cb, ret);
        return;
    }

    BLKDBG_EVENT(s->bs->file, BLKDBG_COW_WRITE);
    aiocb = bdrv_aio_writev(s->bs->file, copy_cb->offset / BDRV_SECTOR_SIZE,
                            &copy_cb->qiov,
                            copy_cb->qiov.size / BDRV_SECTOR_SIZE,
                            qed_copy_from_backing_file_cb, copy_cb);
    if (!aiocb) {
        qed_copy_from_backing_file_cb(copy_cb, -EIO);
    }
}

/**
 * Copy data from backing file into the image
 *
 * @s:          QED state
 * @pos:        Byte position in device
 * @len:        Number of bytes
 * @offset:     Byte offset in image file
 * @cb:         Completion function
 * @opaque:     User data for completion function
 */
static void qed_copy_from_backing_file(BDRVQEDState *s, uint64_t pos,
                                       uint64_t len, uint64_t offset,
                                       BlockDriverCompletionFunc *cb,
                                       void *opaque)
{
    CopyFromBackingFileCB *copy_cb;

    /* Skip copy entirely if there is no work to do */
    if (len == 0) {
        cb(opaque, 0);
        return;
    }

    copy_cb = gencb_alloc(sizeof(*copy_cb), cb, opaque);
    copy_cb->s = s;
    copy_cb->offset = offset;
    copy_cb->iov.iov_base = qemu_blockalign(s->bs, len);
    copy_cb->iov.iov_len = len;
    qemu_iovec_init_external(&copy_cb->qiov, &copy_cb->iov, 1);

    qed_read_backing_file(s, pos, &copy_cb->qiov,
                          qed_copy_from_backing_file_write, copy_cb);
}

/**
 * Link one or more contiguous clusters into a table
 *
 * @s:              QED state
 * @table:          L2 table
 * @index:          First cluster index
 * @n:              Number of contiguous clusters
 * @cluster:        First cluster offset
 *
 * The cluster offset may be an allocated byte offset in the image file, the
 * zero cluster marker, or the unallocated cluster marker.
 */
static void qed_update_l2_table(BDRVQEDState *s, QEDTable *table, int index,
                                unsigned int n, uint64_t cluster)
{
    int i;
    for (i = index; i < index + n; i++) {
        table->offsets[i] = cluster;
        if (!qed_offset_is_unalloc_cluster(cluster) &&
            !qed_offset_is_zero_cluster(cluster)) {
            cluster += s->header.cluster_size;
        }
    }
}

static void qed_aio_complete_bh(void *opaque)
{
    QEDAIOCB *acb = opaque;
    BlockDriverCompletionFunc *cb = acb->common.cb;
    void *user_opaque = acb->common.opaque;
    int ret = acb->bh_ret;
    bool *finished = acb->finished;

    qemu_bh_delete(acb->bh);
    qemu_aio_release(acb);

    /* Invoke callback */
    cb(user_opaque, ret);

    /* Signal cancel completion */
    if (finished) {
        *finished = true;
    }
}

static void qed_aio_complete(QEDAIOCB *acb, int ret)
{
    BDRVQEDState *s = acb_to_s(acb);

    trace_qed_aio_complete(s, acb, ret);

    /* Free resources */
    qemu_iovec_destroy(&acb->cur_qiov);
    qed_unref_l2_cache_entry(acb->request.l2_table);

    /* Arrange for a bh to invoke the completion function */
    acb->bh_ret = ret;
    acb->bh = qemu_bh_new(qed_aio_complete_bh, acb);
    qemu_bh_schedule(acb->bh);

    /* Start next allocating write request waiting behind this one.  Note that
     * requests enqueue themselves when they first hit an unallocated cluster
     * but they wait until the entire request is finished before waking up the
     * next request in the queue.  This ensures that we don't cycle through
     * requests multiple times but rather finish one at a time completely.
     */
    if (acb == QSIMPLEQ_FIRST(&s->allocating_write_reqs)) {
        QSIMPLEQ_REMOVE_HEAD(&s->allocating_write_reqs, next);
        acb = QSIMPLEQ_FIRST(&s->allocating_write_reqs);
        if (acb) {
            qed_aio_next_io(acb, 0);
        } else if (s->header.features & QED_F_NEED_CHECK) {
            qed_start_need_check_timer(s);
        }
    }
}

/**
 * Commit the current L2 table to the cache
 */
static void qed_commit_l2_update(void *opaque, int ret)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);
    CachedL2Table *l2_table = acb->request.l2_table;

    qed_commit_l2_cache_entry(&s->l2_cache, l2_table);

    /* This is guaranteed to succeed because we just committed the entry to the
     * cache.
     */
    acb->request.l2_table = qed_find_l2_cache_entry(&s->l2_cache,
                                                    l2_table->offset);
    assert(acb->request.l2_table != NULL);

    qed_aio_next_io(opaque, ret);
}

/**
 * Update L1 table with new L2 table offset and write it out
 */
static void qed_aio_write_l1_update(void *opaque, int ret)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);
    int index;

    if (ret) {
        qed_aio_complete(acb, ret);
        return;
    }

    index = qed_l1_index(s, acb->cur_pos);
    s->l1_table->offsets[index] = acb->request.l2_table->offset;

    qed_write_l1_table(s, index, 1, qed_commit_l2_update, acb);
}

/**
 * Update L2 table with new cluster offsets and write them out
 */
static void qed_aio_write_l2_update(void *opaque, int ret)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);
    bool need_alloc = acb->find_cluster_ret == QED_CLUSTER_L1;
    int index;

    if (ret) {
        goto err;
    }

    if (need_alloc) {
        qed_unref_l2_cache_entry(acb->request.l2_table);
        acb->request.l2_table = qed_new_l2_table(s);
    }

    index = qed_l2_index(s, acb->cur_pos);
    qed_update_l2_table(s, acb->request.l2_table->table, index, acb->cur_nclusters,
                         acb->cur_cluster);

    if (need_alloc) {
        /* Write out the whole new L2 table */
        qed_write_l2_table(s, &acb->request, 0, s->table_nelems, true,
                            qed_aio_write_l1_update, acb);
    } else {
        /* Write out only the updated part of the L2 table */
        qed_write_l2_table(s, &acb->request, index, acb->cur_nclusters, false,
                            qed_aio_next_io, acb);
    }
    return;

err:
    qed_aio_complete(acb, ret);
}

/**
 * Flush new data clusters before updating the L2 table
 *
 * This flush is necessary when a backing file is in use.  A crash during an
 * allocating write could result in empty clusters in the image.  If the write
 * only touched a subregion of the cluster, then backing image sectors have
 * been lost in the untouched region.  The solution is to flush after writing a
 * new data cluster and before updating the L2 table.
 */
static void qed_aio_write_flush_before_l2_update(void *opaque, int ret)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);

    if (!bdrv_aio_flush(s->bs->file, qed_aio_write_l2_update, opaque)) {
        qed_aio_complete(acb, -EIO);
    }
}

/**
 * Write data to the image file
 */
static void qed_aio_write_main(void *opaque, int ret)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);
    uint64_t offset = acb->cur_cluster +
                      qed_offset_into_cluster(s, acb->cur_pos);
    BlockDriverCompletionFunc *next_fn;
    BlockDriverAIOCB *file_acb;

    trace_qed_aio_write_main(s, acb, ret, offset, acb->cur_qiov.size);

    if (ret) {
        qed_aio_complete(acb, ret);
        return;
    }

    if (acb->find_cluster_ret == QED_CLUSTER_FOUND) {
        next_fn = qed_aio_next_io;
    } else {
        if (s->bs->backing_hd) {
            next_fn = qed_aio_write_flush_before_l2_update;
        } else {
            next_fn = qed_aio_write_l2_update;
        }
    }

    BLKDBG_EVENT(s->bs->file, BLKDBG_WRITE_AIO);
    file_acb = bdrv_aio_writev(s->bs->file, offset / BDRV_SECTOR_SIZE,
                               &acb->cur_qiov,
                               acb->cur_qiov.size / BDRV_SECTOR_SIZE,
                               next_fn, acb);
    if (!file_acb) {
        qed_aio_complete(acb, -EIO);
    }
}

/**
 * Populate back untouched region of new data cluster
 */
static void qed_aio_write_postfill(void *opaque, int ret)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);
    uint64_t start = acb->cur_pos + acb->cur_qiov.size;
    uint64_t len =
        qed_start_of_cluster(s, start + s->header.cluster_size - 1) - start;
    uint64_t offset = acb->cur_cluster +
                      qed_offset_into_cluster(s, acb->cur_pos) +
                      acb->cur_qiov.size;

    if (ret) {
        qed_aio_complete(acb, ret);
        return;
    }

    trace_qed_aio_write_postfill(s, acb, start, len, offset);
    qed_copy_from_backing_file(s, start, len, offset,
                                qed_aio_write_main, acb);
}

/**
 * Populate front untouched region of new data cluster
 */
static void qed_aio_write_prefill(void *opaque, int ret)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);
    uint64_t start = qed_start_of_cluster(s, acb->cur_pos);
    uint64_t len = qed_offset_into_cluster(s, acb->cur_pos);

    trace_qed_aio_write_prefill(s, acb, start, len, acb->cur_cluster);
    qed_copy_from_backing_file(s, start, len, acb->cur_cluster,
                                qed_aio_write_postfill, acb);
}

/**
 * Check if the QED_F_NEED_CHECK bit should be set during allocating write
 */
static bool qed_should_set_need_check(BDRVQEDState *s)
{
    /* The flush before L2 update path ensures consistency */
    if (s->bs->backing_hd) {
        return false;
    }

    return !(s->header.features & QED_F_NEED_CHECK);
}

/**
 * Write new data cluster
 *
 * @acb:        Write request
 * @len:        Length in bytes
 *
 * This path is taken when writing to previously unallocated clusters.
 */
static void qed_aio_write_alloc(QEDAIOCB *acb, size_t len)
{
    BDRVQEDState *s = acb_to_s(acb);

    /* Cancel timer when the first allocating request comes in */
    if (QSIMPLEQ_EMPTY(&s->allocating_write_reqs)) {
        qed_cancel_need_check_timer(s);
    }

    /* Freeze this request if another allocating write is in progress */
    if (acb != QSIMPLEQ_FIRST(&s->allocating_write_reqs)) {
        QSIMPLEQ_INSERT_TAIL(&s->allocating_write_reqs, acb, next);
    }
    if (acb != QSIMPLEQ_FIRST(&s->allocating_write_reqs) ||
        s->allocating_write_reqs_plugged) {
        return; /* wait for existing request to finish */
    }

    acb->cur_nclusters = qed_bytes_to_clusters(s,
            qed_offset_into_cluster(s, acb->cur_pos) + len);
    acb->cur_cluster = qed_alloc_clusters(s, acb->cur_nclusters);
    qemu_iovec_copy(&acb->cur_qiov, acb->qiov, acb->qiov_offset, len);

    if (qed_should_set_need_check(s)) {
        s->header.features |= QED_F_NEED_CHECK;
        qed_write_header(s, qed_aio_write_prefill, acb);
    } else {
        qed_aio_write_prefill(acb, 0);
    }
}

/**
 * Write data cluster in place
 *
 * @acb:        Write request
 * @offset:     Cluster offset in bytes
 * @len:        Length in bytes
 *
 * This path is taken when writing to already allocated clusters.
 */
static void qed_aio_write_inplace(QEDAIOCB *acb, uint64_t offset, size_t len)
{
    /* Calculate the I/O vector */
    acb->cur_cluster = offset;
    qemu_iovec_copy(&acb->cur_qiov, acb->qiov, acb->qiov_offset, len);

    /* Do the actual write */
    qed_aio_write_main(acb, 0);
}

/**
 * Write data cluster
 *
 * @opaque:     Write request
 * @ret:        QED_CLUSTER_FOUND, QED_CLUSTER_L2, QED_CLUSTER_L1,
 *              or -errno
 * @offset:     Cluster offset in bytes
 * @len:        Length in bytes
 *
 * Callback from qed_find_cluster().
 */
static void qed_aio_write_data(void *opaque, int ret,
                               uint64_t offset, size_t len)
{
    QEDAIOCB *acb = opaque;

    trace_qed_aio_write_data(acb_to_s(acb), acb, ret, offset, len);

    acb->find_cluster_ret = ret;

    switch (ret) {
    case QED_CLUSTER_FOUND:
        qed_aio_write_inplace(acb, offset, len);
        break;

    case QED_CLUSTER_L2:
    case QED_CLUSTER_L1:
    case QED_CLUSTER_ZERO:
        qed_aio_write_alloc(acb, len);
        break;

    default:
        qed_aio_complete(acb, ret);
        break;
    }
}

/**
 * Read data cluster
 *
 * @opaque:     Read request
 * @ret:        QED_CLUSTER_FOUND, QED_CLUSTER_L2, QED_CLUSTER_L1,
 *              or -errno
 * @offset:     Cluster offset in bytes
 * @len:        Length in bytes
 *
 * Callback from qed_find_cluster().
 */
static void qed_aio_read_data(void *opaque, int ret,
                              uint64_t offset, size_t len)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);
    BlockDriverState *bs = acb->common.bs;
    BlockDriverAIOCB *file_acb;

    /* Adjust offset into cluster */
    offset += qed_offset_into_cluster(s, acb->cur_pos);

    trace_qed_aio_read_data(s, acb, ret, offset, len);

    if (ret < 0) {
        goto err;
    }

    qemu_iovec_copy(&acb->cur_qiov, acb->qiov, acb->qiov_offset, len);

    /* Handle zero cluster and backing file reads */
    if (ret == QED_CLUSTER_ZERO) {
        qemu_iovec_memset(&acb->cur_qiov, 0, acb->cur_qiov.size);
        qed_aio_next_io(acb, 0);
        return;
    } else if (ret != QED_CLUSTER_FOUND) {
        qed_read_backing_file(s, acb->cur_pos, &acb->cur_qiov,
                              qed_aio_next_io, acb);
        return;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
    file_acb = bdrv_aio_readv(bs->file, offset / BDRV_SECTOR_SIZE,
                              &acb->cur_qiov,
                              acb->cur_qiov.size / BDRV_SECTOR_SIZE,
                              qed_aio_next_io, acb);
    if (!file_acb) {
        ret = -EIO;
        goto err;
    }
    return;

err:
    qed_aio_complete(acb, ret);
}

/**
 * Begin next I/O or complete the request
 */
static void qed_aio_next_io(void *opaque, int ret)
{
    QEDAIOCB *acb = opaque;
    BDRVQEDState *s = acb_to_s(acb);
    QEDFindClusterFunc *io_fn =
        acb->is_write ? qed_aio_write_data : qed_aio_read_data;

    trace_qed_aio_next_io(s, acb, ret, acb->cur_pos + acb->cur_qiov.size);

    /* Handle I/O error */
    if (ret) {
        qed_aio_complete(acb, ret);
        return;
    }

    acb->qiov_offset += acb->cur_qiov.size;
    acb->cur_pos += acb->cur_qiov.size;
    qemu_iovec_reset(&acb->cur_qiov);

    /* Complete request */
    if (acb->cur_pos >= acb->end_pos) {
        qed_aio_complete(acb, 0);
        return;
    }

    /* Find next cluster and start I/O */
    qed_find_cluster(s, &acb->request,
                      acb->cur_pos, acb->end_pos - acb->cur_pos,
                      io_fn, acb);
}

static BlockDriverAIOCB *qed_aio_setup(BlockDriverState *bs,
                                       int64_t sector_num,
                                       QEMUIOVector *qiov, int nb_sectors,
                                       BlockDriverCompletionFunc *cb,
                                       void *opaque, bool is_write)
{
    QEDAIOCB *acb = qemu_aio_get(&qed_aio_pool, bs, cb, opaque);

    trace_qed_aio_setup(bs->opaque, acb, sector_num, nb_sectors,
                         opaque, is_write);

    acb->is_write = is_write;
    acb->finished = NULL;
    acb->qiov = qiov;
    acb->qiov_offset = 0;
    acb->cur_pos = (uint64_t)sector_num * BDRV_SECTOR_SIZE;
    acb->end_pos = acb->cur_pos + nb_sectors * BDRV_SECTOR_SIZE;
    acb->request.l2_table = NULL;
    qemu_iovec_init(&acb->cur_qiov, qiov->niov);

    /* Start request */
    qed_aio_next_io(acb, 0);
    return &acb->common;
}

static BlockDriverAIOCB *bdrv_qed_aio_readv(BlockDriverState *bs,
                                            int64_t sector_num,
                                            QEMUIOVector *qiov, int nb_sectors,
                                            BlockDriverCompletionFunc *cb,
                                            void *opaque)
{
    return qed_aio_setup(bs, sector_num, qiov, nb_sectors, cb, opaque, false);
}

static BlockDriverAIOCB *bdrv_qed_aio_writev(BlockDriverState *bs,
                                             int64_t sector_num,
                                             QEMUIOVector *qiov, int nb_sectors,
                                             BlockDriverCompletionFunc *cb,
                                             void *opaque)
{
    return qed_aio_setup(bs, sector_num, qiov, nb_sectors, cb, opaque, true);
}

static BlockDriverAIOCB *bdrv_qed_aio_flush(BlockDriverState *bs,
                                            BlockDriverCompletionFunc *cb,
                                            void *opaque)
{
    return bdrv_aio_flush(bs->file, cb, opaque);
}

static int bdrv_qed_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVQEDState *s = bs->opaque;
    uint64_t old_image_size;
    int ret;

    if (!qed_is_image_size_valid(offset, s->header.cluster_size,
                                 s->header.table_size)) {
        return -EINVAL;
    }

    /* Shrinking is currently not supported */
    if ((uint64_t)offset < s->header.image_size) {
        return -ENOTSUP;
    }

    old_image_size = s->header.image_size;
    s->header.image_size = offset;
    ret = qed_write_header_sync(s);
    if (ret < 0) {
        s->header.image_size = old_image_size;
    }
    return ret;
}

static int64_t bdrv_qed_getlength(BlockDriverState *bs)
{
    BDRVQEDState *s = bs->opaque;
    return s->header.image_size;
}

static int bdrv_qed_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVQEDState *s = bs->opaque;

    memset(bdi, 0, sizeof(*bdi));
    bdi->cluster_size = s->header.cluster_size;
    return 0;
}

static int bdrv_qed_change_backing_file(BlockDriverState *bs,
                                        const char *backing_file,
                                        const char *backing_fmt)
{
    BDRVQEDState *s = bs->opaque;
    QEDHeader new_header, le_header;
    void *buffer;
    size_t buffer_len, backing_file_len;
    int ret;

    /* Refuse to set backing filename if unknown compat feature bits are
     * active.  If the image uses an unknown compat feature then we may not
     * know the layout of data following the header structure and cannot safely
     * add a new string.
     */
    if (backing_file && (s->header.compat_features &
                         ~QED_COMPAT_FEATURE_MASK)) {
        return -ENOTSUP;
    }

    memcpy(&new_header, &s->header, sizeof(new_header));

    new_header.features &= ~(QED_F_BACKING_FILE |
                             QED_F_BACKING_FORMAT_NO_PROBE);

    /* Adjust feature flags */
    if (backing_file) {
        new_header.features |= QED_F_BACKING_FILE;

        if (qed_fmt_is_raw(backing_fmt)) {
            new_header.features |= QED_F_BACKING_FORMAT_NO_PROBE;
        }
    }

    /* Calculate new header size */
    backing_file_len = 0;

    if (backing_file) {
        backing_file_len = strlen(backing_file);
    }

    buffer_len = sizeof(new_header);
    new_header.backing_filename_offset = buffer_len;
    new_header.backing_filename_size = backing_file_len;
    buffer_len += backing_file_len;

    /* Make sure we can rewrite header without failing */
    if (buffer_len > new_header.header_size * new_header.cluster_size) {
        return -ENOSPC;
    }

    /* Prepare new header */
    buffer = qemu_malloc(buffer_len);

    qed_header_cpu_to_le(&new_header, &le_header);
    memcpy(buffer, &le_header, sizeof(le_header));
    buffer_len = sizeof(le_header);

    memcpy(buffer + buffer_len, backing_file, backing_file_len);
    buffer_len += backing_file_len;

    /* Write new header */
    ret = bdrv_pwrite_sync(bs->file, 0, buffer, buffer_len);
    qemu_free(buffer);
    if (ret == 0) {
        memcpy(&s->header, &new_header, sizeof(new_header));
    }
    return ret;
}

static int bdrv_qed_check(BlockDriverState *bs, BdrvCheckResult *result)
{
    BDRVQEDState *s = bs->opaque;

    return qed_check(s, result, false);
}

static QEMUOptionParameter qed_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size (in bytes)"
    }, {
        .name = BLOCK_OPT_BACKING_FILE,
        .type = OPT_STRING,
        .help = "File name of a base image"
    }, {
        .name = BLOCK_OPT_BACKING_FMT,
        .type = OPT_STRING,
        .help = "Image format of the base image"
    }, {
        .name = BLOCK_OPT_CLUSTER_SIZE,
        .type = OPT_SIZE,
        .help = "Cluster size (in bytes)",
        .value = { .n = QED_DEFAULT_CLUSTER_SIZE },
    }, {
        .name = BLOCK_OPT_TABLE_SIZE,
        .type = OPT_SIZE,
        .help = "L1/L2 table size (in clusters)"
    },
    { /* end of list */ }
};

static BlockDriver bdrv_qed = {
    .format_name              = "qed",
    .instance_size            = sizeof(BDRVQEDState),
    .create_options           = qed_create_options,

    .bdrv_probe               = bdrv_qed_probe,
    .bdrv_open                = bdrv_qed_open,
    .bdrv_close               = bdrv_qed_close,
    .bdrv_create              = bdrv_qed_create,
    .bdrv_flush               = bdrv_qed_flush,
    .bdrv_is_allocated        = bdrv_qed_is_allocated,
    .bdrv_make_empty          = bdrv_qed_make_empty,
    .bdrv_aio_readv           = bdrv_qed_aio_readv,
    .bdrv_aio_writev          = bdrv_qed_aio_writev,
    .bdrv_aio_flush           = bdrv_qed_aio_flush,
    .bdrv_truncate            = bdrv_qed_truncate,
    .bdrv_getlength           = bdrv_qed_getlength,
    .bdrv_get_info            = bdrv_qed_get_info,
    .bdrv_change_backing_file = bdrv_qed_change_backing_file,
    .bdrv_check               = bdrv_qed_check,
};

static void bdrv_qed_init(void)
{
    bdrv_register(&bdrv_qed);
}

block_init(bdrv_qed_init);
