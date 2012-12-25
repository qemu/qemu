
#include "qemu-common.h"
#include "block/block_int.h"
#include "qemu/module.h"

static int raw_open(BlockDriverState *bs, int flags)
{
    bs->sg = bs->file->sg;
    return 0;
}

/* We have nothing to do for raw reopen, stubs just return
 * success */
static int raw_reopen_prepare(BDRVReopenState *state,
                              BlockReopenQueue *queue,  Error **errp)
{
    return 0;
}

static int coroutine_fn raw_co_readv(BlockDriverState *bs, int64_t sector_num,
                                     int nb_sectors, QEMUIOVector *qiov)
{
    BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);
    return bdrv_co_readv(bs->file, sector_num, nb_sectors, qiov);
}

static int coroutine_fn raw_co_writev(BlockDriverState *bs, int64_t sector_num,
                                      int nb_sectors, QEMUIOVector *qiov)
{
    BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
    return bdrv_co_writev(bs->file, sector_num, nb_sectors, qiov);
}

static void raw_close(BlockDriverState *bs)
{
}

static int coroutine_fn raw_co_is_allocated(BlockDriverState *bs,
                                            int64_t sector_num,
                                            int nb_sectors, int *pnum)
{
    return bdrv_co_is_allocated(bs->file, sector_num, nb_sectors, pnum);
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

static int coroutine_fn raw_co_discard(BlockDriverState *bs,
                                       int64_t sector_num, int nb_sectors)
{
    return bdrv_co_discard(bs->file, sector_num, nb_sectors);
}

static int raw_is_inserted(BlockDriverState *bs)
{
    return bdrv_is_inserted(bs->file);
}

static int raw_media_changed(BlockDriverState *bs)
{
    return bdrv_media_changed(bs->file);
}

static void raw_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_eject(bs->file, eject_flag);
}

static void raw_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_lock_medium(bs->file, locked);
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

    /* It's really 0, but we need to make g_malloc() happy */
    .instance_size      = 1,

    .bdrv_open          = raw_open,
    .bdrv_close         = raw_close,

    .bdrv_reopen_prepare  = raw_reopen_prepare,

    .bdrv_co_readv          = raw_co_readv,
    .bdrv_co_writev         = raw_co_writev,
    .bdrv_co_is_allocated   = raw_co_is_allocated,
    .bdrv_co_discard        = raw_co_discard,

    .bdrv_probe         = raw_probe,
    .bdrv_getlength     = raw_getlength,
    .bdrv_truncate      = raw_truncate,

    .bdrv_is_inserted   = raw_is_inserted,
    .bdrv_media_changed = raw_media_changed,
    .bdrv_eject         = raw_eject,
    .bdrv_lock_medium   = raw_lock_medium,

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
