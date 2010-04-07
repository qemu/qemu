
#include "qemu-common.h"
#include "block_int.h"
#include "module.h"

typedef struct RAWState {
    BlockDriverState *hd;
} RAWState;

static int raw_open(BlockDriverState *bs, const char *filename, int flags)
{
    RAWState *s = bs->opaque;
    int ret;

    ret = bdrv_file_open(&s->hd, filename, flags);
    if (!ret) {
        bs->sg = s->hd->sg;
    }

    return ret;
}

static int raw_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    RAWState *s = bs->opaque;
    return bdrv_read(s->hd, sector_num, buf, nb_sectors);
}

static int raw_write(BlockDriverState *bs, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    RAWState *s = bs->opaque;
    return bdrv_write(s->hd, sector_num, buf, nb_sectors);
}

static BlockDriverAIOCB *raw_aio_readv(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    RAWState *s = bs->opaque;

    return bdrv_aio_readv(s->hd, sector_num, qiov, nb_sectors, cb, opaque);
}

static BlockDriverAIOCB *raw_aio_writev(BlockDriverState *bs,
    int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    RAWState *s = bs->opaque;

    return bdrv_aio_writev(s->hd, sector_num, qiov, nb_sectors, cb, opaque);
}

static void raw_close(BlockDriverState *bs)
{
    RAWState *s = bs->opaque;
    bdrv_delete(s->hd);
}

static void raw_flush(BlockDriverState *bs)
{
    RAWState *s = bs->opaque;
    bdrv_flush(s->hd);
}

static BlockDriverAIOCB *raw_aio_flush(BlockDriverState *bs,
    BlockDriverCompletionFunc *cb, void *opaque)
{
    RAWState *s = bs->opaque;
    return bdrv_aio_flush(s->hd, cb, opaque);
}

static int64_t raw_getlength(BlockDriverState *bs)
{
    RAWState *s = bs->opaque;
    return bdrv_getlength(s->hd);
}

static int raw_truncate(BlockDriverState *bs, int64_t offset)
{
    RAWState *s = bs->opaque;
    return bdrv_truncate(s->hd, offset);
}

static int raw_probe(const uint8_t *buf, int buf_size, const char *filename)
{
   return 1; /* everything can be opened as raw image */
}

static int raw_is_inserted(BlockDriverState *bs)
{
    RAWState *s = bs->opaque;
    return bdrv_is_inserted(s->hd);
}

static int raw_eject(BlockDriverState *bs, int eject_flag)
{
    RAWState *s = bs->opaque;
    return bdrv_eject(s->hd, eject_flag);
}

static int raw_set_locked(BlockDriverState *bs, int locked)
{
    RAWState *s = bs->opaque;
    bdrv_set_locked(s->hd, locked);
    return 0;
}

static int raw_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
   RAWState *s = bs->opaque;
   return bdrv_ioctl(s->hd, req, buf);
}

static BlockDriverAIOCB *raw_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque)
{
   RAWState *s = bs->opaque;
   return bdrv_aio_ioctl(s->hd, req, buf, cb, opaque);
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

static BlockDriver bdrv_raw = {
    .format_name        = "raw",

    .instance_size      = sizeof(RAWState),

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
};

static void bdrv_raw_init(void)
{
    bdrv_register(&bdrv_raw);
}

block_init(bdrv_raw_init);
