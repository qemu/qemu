/*
 * Quorum Block filter
 *
 * Copyright (C) 2012-2014 Nodalink, EURL.
 *
 * Author:
 *   Benoît Canet <benoit.canet@irqsave.net>
 *
 * Based on the design and code of blkverify.c (Copyright (C) 2010 IBM, Corp)
 * and blkmirror.c (Copyright (C) 2011 Red Hat, Inc).
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "block/block_int.h"

/* the following structure holds the state of one quorum instance */
typedef struct BDRVQuorumState {
    BlockDriverState **bs; /* children BlockDriverStates */
    int num_children;      /* children count */
    int threshold;         /* if less than threshold children reads gave the
                            * same result a quorum error occurs.
                            */
    bool is_blkverify;     /* true if the driver is in blkverify mode
                            * Writes are mirrored on two children devices.
                            * On reads the two children devices' contents are
                            * compared and if a difference is spotted its
                            * location is printed and the code aborts.
                            * It is useful to debug other block drivers by
                            * comparing them with a reference one.
                            */
} BDRVQuorumState;

typedef struct QuorumAIOCB QuorumAIOCB;

/* Quorum will create one instance of the following structure per operation it
 * performs on its children.
 * So for each read/write operation coming from the upper layer there will be
 * $children_count QuorumChildRequest.
 */
typedef struct QuorumChildRequest {
    BlockDriverAIOCB *aiocb;
    QEMUIOVector qiov;
    uint8_t *buf;
    int ret;
    QuorumAIOCB *parent;
} QuorumChildRequest;

/* Quorum will use the following structure to track progress of each read/write
 * operation received by the upper layer.
 * This structure hold pointers to the QuorumChildRequest structures instances
 * used to do operations on each children and track overall progress.
 */
struct QuorumAIOCB {
    BlockDriverAIOCB common;

    /* Request metadata */
    uint64_t sector_num;
    int nb_sectors;

    QEMUIOVector *qiov;         /* calling IOV */

    QuorumChildRequest *qcrs;   /* individual child requests */
    int count;                  /* number of completed AIOCB */
    int success_count;          /* number of successfully completed AIOCB */

    bool is_read;
    int vote_ret;
};

static void quorum_aio_cancel(BlockDriverAIOCB *blockacb)
{
    QuorumAIOCB *acb = container_of(blockacb, QuorumAIOCB, common);
    BDRVQuorumState *s = acb->common.bs->opaque;
    int i;

    /* cancel all callbacks */
    for (i = 0; i < s->num_children; i++) {
        bdrv_aio_cancel(acb->qcrs[i].aiocb);
    }

    g_free(acb->qcrs);
    qemu_aio_release(acb);
}

static AIOCBInfo quorum_aiocb_info = {
    .aiocb_size         = sizeof(QuorumAIOCB),
    .cancel             = quorum_aio_cancel,
};

static void quorum_aio_finalize(QuorumAIOCB *acb)
{
    BDRVQuorumState *s = acb->common.bs->opaque;
    int i, ret = 0;

    acb->common.cb(acb->common.opaque, ret);

    if (acb->is_read) {
        for (i = 0; i < s->num_children; i++) {
            qemu_vfree(acb->qcrs[i].buf);
            qemu_iovec_destroy(&acb->qcrs[i].qiov);
        }
    }

    g_free(acb->qcrs);
    qemu_aio_release(acb);
}

static QuorumAIOCB *quorum_aio_get(BDRVQuorumState *s,
                                   BlockDriverState *bs,
                                   QEMUIOVector *qiov,
                                   uint64_t sector_num,
                                   int nb_sectors,
                                   BlockDriverCompletionFunc *cb,
                                   void *opaque)
{
    QuorumAIOCB *acb = qemu_aio_get(&quorum_aiocb_info, bs, cb, opaque);
    int i;

    acb->common.bs->opaque = s;
    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;
    acb->qiov = qiov;
    acb->qcrs = g_new0(QuorumChildRequest, s->num_children);
    acb->count = 0;
    acb->success_count = 0;
    acb->is_read = false;
    acb->vote_ret = 0;

    for (i = 0; i < s->num_children; i++) {
        acb->qcrs[i].buf = NULL;
        acb->qcrs[i].ret = 0;
        acb->qcrs[i].parent = acb;
    }

    return acb;
}

static void quorum_aio_cb(void *opaque, int ret)
{
    QuorumChildRequest *sacb = opaque;
    QuorumAIOCB *acb = sacb->parent;
    BDRVQuorumState *s = acb->common.bs->opaque;

    sacb->ret = ret;
    acb->count++;
    if (ret == 0) {
        acb->success_count++;
    }
    assert(acb->count <= s->num_children);
    assert(acb->success_count <= s->num_children);
    if (acb->count < s->num_children) {
        return;
    }

    quorum_aio_finalize(acb);
}

static BlockDriverAIOCB *quorum_aio_readv(BlockDriverState *bs,
                                         int64_t sector_num,
                                         QEMUIOVector *qiov,
                                         int nb_sectors,
                                         BlockDriverCompletionFunc *cb,
                                         void *opaque)
{
    BDRVQuorumState *s = bs->opaque;
    QuorumAIOCB *acb = quorum_aio_get(s, bs, qiov, sector_num,
                                      nb_sectors, cb, opaque);
    int i;

    acb->is_read = true;

    for (i = 0; i < s->num_children; i++) {
        acb->qcrs[i].buf = qemu_blockalign(s->bs[i], qiov->size);
        qemu_iovec_init(&acb->qcrs[i].qiov, qiov->niov);
        qemu_iovec_clone(&acb->qcrs[i].qiov, qiov, acb->qcrs[i].buf);
    }

    for (i = 0; i < s->num_children; i++) {
        bdrv_aio_readv(s->bs[i], sector_num, qiov, nb_sectors,
                       quorum_aio_cb, &acb->qcrs[i]);
    }

    return &acb->common;
}

static BlockDriverAIOCB *quorum_aio_writev(BlockDriverState *bs,
                                          int64_t sector_num,
                                          QEMUIOVector *qiov,
                                          int nb_sectors,
                                          BlockDriverCompletionFunc *cb,
                                          void *opaque)
{
    BDRVQuorumState *s = bs->opaque;
    QuorumAIOCB *acb = quorum_aio_get(s, bs, qiov, sector_num, nb_sectors,
                                      cb, opaque);
    int i;

    for (i = 0; i < s->num_children; i++) {
        acb->qcrs[i].aiocb = bdrv_aio_writev(s->bs[i], sector_num, qiov,
                                             nb_sectors, &quorum_aio_cb,
                                             &acb->qcrs[i]);
    }

    return &acb->common;
}

static BlockDriver bdrv_quorum = {
    .format_name        = "quorum",
    .protocol_name      = "quorum",

    .instance_size      = sizeof(BDRVQuorumState),

    .bdrv_aio_readv     = quorum_aio_readv,
    .bdrv_aio_writev    = quorum_aio_writev,
};

static void bdrv_quorum_init(void)
{
    bdrv_register(&bdrv_quorum);
}

block_init(bdrv_quorum_init);
