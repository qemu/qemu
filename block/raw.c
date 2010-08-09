
#include "qemu-common.h"
#include "block_int.h"
#include "module.h"

static int raw_open(BlockDriverState *bs, int flags)
{
    bs->sg = bs->file->sg;
    return 0;
}

/* check for the user attempting to write something that looks like a
   block format header to the beginning of the image and fail out.
*/
static int check_for_block_signature(BlockDriverState *bs, const uint8_t *buf)
{
    static const uint8_t signatures[][4] = {
        { 'Q', 'F', 'I', 0xfb }, /* qcow/qcow2 */
        { 'C', 'O', 'W', 'D' }, /* VMDK3 */
        { 'V', 'M', 'D', 'K' }, /* VMDK4 */
        { 'O', 'O', 'O', 'M' }, /* UML COW */
        {}
    };
    int i;

    for (i = 0; signatures[i][0] != 0; i++) {
        if (memcmp(buf, signatures[i], 4) == 0) {
            return 1;
        }
    }

    return 0;
}

static int check_write_unsafe(BlockDriverState *bs, int64_t sector_num,
                              const uint8_t *buf, int nb_sectors)
{
    /* assume that if the user specifies the format explicitly, then assume
       that they will continue to do so and provide no safety net */
    if (!bs->probed) {
        return 0;
    }

    if (sector_num == 0 && nb_sectors > 0) {
        return check_for_block_signature(bs, buf);
    }

    return 0;
}

static int raw_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    return bdrv_read(bs->file, sector_num, buf, nb_sectors);
}

static int raw_write_scrubbed_bootsect(BlockDriverState *bs,
                                       const uint8_t *buf)
{
    uint8_t bootsect[512];

    /* scrub the dangerous signature */
    memcpy(bootsect, buf, 512);
    memset(bootsect, 0, 4);

    return bdrv_write(bs->file, 0, bootsect, 1);
}

static int raw_write(BlockDriverState *bs, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    if (check_write_unsafe(bs, sector_num, buf, nb_sectors)) {
        int ret;

        ret = raw_write_scrubbed_bootsect(bs, buf);
        if (ret < 0) {
            return ret;
        }

        ret = bdrv_write(bs->file, 1, buf + 512, nb_sectors - 1);
        if (ret < 0) {
            return ret;
        }

        return ret + 512;
    }

    return bdrv_write(bs->file, sector_num, buf, nb_sectors);
}

static BlockDriverAIOCB *raw_aio_readv(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_readv(bs->file, sector_num, qiov, nb_sectors, cb, opaque);
}

typedef struct RawScrubberBounce
{
    BlockDriverCompletionFunc *cb;
    void *opaque;
    QEMUIOVector qiov;
} RawScrubberBounce;

static void raw_aio_writev_scrubbed(void *opaque, int ret)
{
    RawScrubberBounce *b = opaque;

    if (ret < 0) {
        b->cb(b->opaque, ret);
    } else {
        b->cb(b->opaque, ret + 512);
    }

    qemu_iovec_destroy(&b->qiov);
    qemu_free(b);
}

static BlockDriverAIOCB *raw_aio_writev(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    const uint8_t *first_buf;
    int first_buf_index = 0, i;

    /* This is probably being paranoid, but handle cases of zero size
       vectors. */
    for (i = 0; i < qiov->niov; i++) {
        if (qiov->iov[i].iov_len) {
            assert(qiov->iov[i].iov_len >= 512);
            first_buf_index = i;
            break;
        }
    }

    first_buf = qiov->iov[first_buf_index].iov_base;

    if (check_write_unsafe(bs, sector_num, first_buf, nb_sectors)) {
        RawScrubberBounce *b;
        int ret;

        /* write the first sector using sync I/O */
        ret = raw_write_scrubbed_bootsect(bs, first_buf);
        if (ret < 0) {
            return NULL;
        }

        /* adjust request to be everything but first sector */

        b = qemu_malloc(sizeof(*b));
        b->cb = cb;
        b->opaque = opaque;

        qemu_iovec_init(&b->qiov, qiov->nalloc);
        qemu_iovec_concat(&b->qiov, qiov, qiov->size);

        b->qiov.size -= 512;
        b->qiov.iov[first_buf_index].iov_base += 512;
        b->qiov.iov[first_buf_index].iov_len -= 512;

        return bdrv_aio_writev(bs->file, sector_num + 1, &b->qiov,
                               nb_sectors - 1, raw_aio_writev_scrubbed, b);
    }

    return bdrv_aio_writev(bs->file, sector_num, qiov, nb_sectors, cb, opaque);
}

static void raw_close(BlockDriverState *bs)
{
}

static void raw_flush(BlockDriverState *bs)
{
    bdrv_flush(bs->file);
}

static BlockDriverAIOCB *raw_aio_flush(BlockDriverState *bs,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_flush(bs->file, cb, opaque);
}

static int64_t raw_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file);
}

static int raw_truncate(BlockDriverState *bs, int64_t offset)
{
    return bdrv_truncate(bs->file, offset);
}

static int raw_probe(const uint8_t *buf, int buf_size, const char *filename)
{
   return 1; /* everything can be opened as raw image */
}

static int raw_is_inserted(BlockDriverState *bs)
{
    return bdrv_is_inserted(bs->file);
}

static int raw_eject(BlockDriverState *bs, int eject_flag)
{
    return bdrv_eject(bs->file, eject_flag);
}

static int raw_set_locked(BlockDriverState *bs, int locked)
{
    bdrv_set_locked(bs->file, locked);
    return 0;
}

static int raw_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
   return bdrv_ioctl(bs->file, req, buf);
}

static BlockDriverAIOCB *raw_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque)
{
   return bdrv_aio_ioctl(bs->file, req, buf, cb, opaque);
}

static int raw_create(const char *filename, QEMUOptionParameter *options)
{
    return bdrv_create_file(filename, options);
}

static QEMUOptionParameter raw_create_options[] = {
    {
        .name = BLOCK_OPT_SIZE,
        .type = OPT_SIZE,
        .help = "Virtual disk size"
    },
    { NULL }
};

static int raw_has_zero_init(BlockDriverState *bs)
{
    return bdrv_has_zero_init(bs->file);
}

static BlockDriver bdrv_raw = {
    .format_name        = "raw",

    /* It's really 0, but we need to make qemu_malloc() happy */
    .instance_size      = 1,

    .bdrv_open          = raw_open,
    .bdrv_close         = raw_close,
    .bdrv_read          = raw_read,
    .bdrv_write         = raw_write,
    .bdrv_flush         = raw_flush,
    .bdrv_probe         = raw_probe,
    .bdrv_getlength     = raw_getlength,
    .bdrv_truncate      = raw_truncate,

    .bdrv_aio_readv     = raw_aio_readv,
    .bdrv_aio_writev    = raw_aio_writev,
    .bdrv_aio_flush     = raw_aio_flush,

    .bdrv_is_inserted   = raw_is_inserted,
    .bdrv_eject         = raw_eject,
    .bdrv_set_locked    = raw_set_locked,
    .bdrv_ioctl         = raw_ioctl,
    .bdrv_aio_ioctl     = raw_aio_ioctl,

    .bdrv_create        = raw_create,
    .create_options     = raw_create_options,
    .bdrv_has_zero_init = raw_has_zero_init,
};

static void bdrv_raw_init(void)
{
    bdrv_register(&bdrv_raw);
}

block_init(bdrv_raw_init);
