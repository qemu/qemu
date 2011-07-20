
#include "qemu-common.h"
#include "block_int.h"
#include "module.h"

static int raw_open(BlockDriverState *bs, int flags)
{
    bs->sg = bs->file->sg;
    return 0;
}

static int raw_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    return bdrv_read(bs->file, sector_num, buf, nb_sectors);
}

static int raw_write(BlockDriverState *bs, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    return bdrv_write(bs->file, sector_num, buf, nb_sectors);
}

static BlockDriverAIOCB *raw_aio_readv(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_readv(bs->file, sector_num, qiov, nb_sectors, cb, opaque);
}

static BlockDriverAIOCB *raw_aio_writev(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    return bdrv_aio_writev(bs->file, sector_num, qiov, nb_sectors, cb, opaque);
}

static void raw_close(BlockDriverState *bs)
{
}

static int raw_flush(BlockDriverState *bs)
{
    return bdrv_flush(bs->file);
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

static int raw_discard(BlockDriverState *bs, int64_t sector_num, int nb_sectors)
{
    return bdrv_discard(bs->file, sector_num, nb_sectors);
}

static int raw_is_inserted(BlockDriverState *bs)
{
    return bdrv_is_inserted(bs->file);
}

static int raw_eject(BlockDriverState *bs, int eject_flag)
{
    return bdrv_eject(bs->file, eject_flag);
}

static void raw_set_locked(BlockDriverState *bs, int locked)
{
    bdrv_set_locked(bs->file, locked);
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
    .bdrv_discard       = raw_discard,

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
