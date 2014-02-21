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
