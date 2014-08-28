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

#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>
#include "block/block_int.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qstring.h"
#include "qapi-event.h"

#define HASH_LENGTH 32

#define QUORUM_OPT_VOTE_THRESHOLD "vote-threshold"
#define QUORUM_OPT_BLKVERIFY      "blkverify"
#define QUORUM_OPT_REWRITE        "rewrite-corrupted"
#define QUORUM_OPT_READ_PATTERN   "read-pattern"

/* This union holds a vote hash value */
typedef union QuorumVoteValue {
    char h[HASH_LENGTH];       /* SHA-256 hash */
    int64_t l;                 /* simpler 64 bits hash */
} QuorumVoteValue;

/* A vote item */
typedef struct QuorumVoteItem {
    int index;
    QLIST_ENTRY(QuorumVoteItem) next;
} QuorumVoteItem;

/* this structure is a vote version. A version is the set of votes sharing the
 * same vote value.
 * The set of votes will be tracked with the items field and its cardinality is
 * vote_count.
 */
typedef struct QuorumVoteVersion {
    QuorumVoteValue value;
    int index;
    int vote_count;
    QLIST_HEAD(, QuorumVoteItem) items;
    QLIST_ENTRY(QuorumVoteVersion) next;
} QuorumVoteVersion;

/* this structure holds a group of vote versions together */
typedef struct QuorumVotes {
    QLIST_HEAD(, QuorumVoteVersion) vote_list;
    bool (*compare)(QuorumVoteValue *a, QuorumVoteValue *b);
} QuorumVotes;

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
    bool rewrite_corrupted;/* true if the driver must rewrite-on-read corrupted
                            * block if Quorum is reached.
                            */

    QuorumReadPattern read_pattern;
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

    int rewrite_count;          /* number of replica to rewrite: count down to
                                 * zero once writes are fired
                                 */

    QuorumVotes votes;

    bool is_read;
    int vote_ret;
    int child_iter;             /* which child to read in fifo pattern */
};

static bool quorum_vote(QuorumAIOCB *acb);

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
    int i, ret = 0;

    if (acb->vote_ret) {
        ret = acb->vote_ret;
    }

    acb->common.cb(acb->common.opaque, ret);

    if (acb->is_read) {
        /* on the quorum case acb->child_iter == s->num_children - 1 */
        for (i = 0; i <= acb->child_iter; i++) {
            qemu_vfree(acb->qcrs[i].buf);
            qemu_iovec_destroy(&acb->qcrs[i].qiov);
        }
    }

    g_free(acb->qcrs);
    qemu_aio_release(acb);
}

static bool quorum_sha256_compare(QuorumVoteValue *a, QuorumVoteValue *b)
{
    return !memcmp(a->h, b->h, HASH_LENGTH);
}

static bool quorum_64bits_compare(QuorumVoteValue *a, QuorumVoteValue *b)
{
    return a->l == b->l;
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
    acb->rewrite_count = 0;
    acb->votes.compare = quorum_sha256_compare;
    QLIST_INIT(&acb->votes.vote_list);
    acb->is_read = false;
    acb->vote_ret = 0;

    for (i = 0; i < s->num_children; i++) {
        acb->qcrs[i].buf = NULL;
        acb->qcrs[i].ret = 0;
        acb->qcrs[i].parent = acb;
    }

    return acb;
}

static void quorum_report_bad(QuorumAIOCB *acb, char *node_name, int ret)
{
    const char *msg = NULL;
    if (ret < 0) {
        msg = strerror(-ret);
    }
    qapi_event_send_quorum_report_bad(!!msg, msg, node_name,
                                      acb->sector_num, acb->nb_sectors, &error_abort);
}

static void quorum_report_failure(QuorumAIOCB *acb)
{
    const char *reference = acb->common.bs->device_name[0] ?
                            acb->common.bs->device_name :
                            acb->common.bs->node_name;

    qapi_event_send_quorum_failure(reference, acb->sector_num,
                                   acb->nb_sectors, &error_abort);
}

static int quorum_vote_error(QuorumAIOCB *acb);

static bool quorum_has_too_much_io_failed(QuorumAIOCB *acb)
{
    BDRVQuorumState *s = acb->common.bs->opaque;

    if (acb->success_count < s->threshold) {
        acb->vote_ret = quorum_vote_error(acb);
        quorum_report_failure(acb);
        return true;
    }

    return false;
}

static void quorum_rewrite_aio_cb(void *opaque, int ret)
{
    QuorumAIOCB *acb = opaque;

    /* one less rewrite to do */
    acb->rewrite_count--;

    /* wait until all rewrite callbacks have completed */
    if (acb->rewrite_count) {
        return;
    }

    quorum_aio_finalize(acb);
}

static BlockDriverAIOCB *read_fifo_child(QuorumAIOCB *acb);

static void quorum_copy_qiov(QEMUIOVector *dest, QEMUIOVector *source)
{
    int i;
    assert(dest->niov == source->niov);
    assert(dest->size == source->size);
    for (i = 0; i < source->niov; i++) {
        assert(dest->iov[i].iov_len == source->iov[i].iov_len);
        memcpy(dest->iov[i].iov_base,
               source->iov[i].iov_base,
               source->iov[i].iov_len);
    }
}

static void quorum_aio_cb(void *opaque, int ret)
{
    QuorumChildRequest *sacb = opaque;
    QuorumAIOCB *acb = sacb->parent;
    BDRVQuorumState *s = acb->common.bs->opaque;
    bool rewrite = false;

    if (acb->is_read && s->read_pattern == QUORUM_READ_PATTERN_FIFO) {
        /* We try to read next child in FIFO order if we fail to read */
        if (ret < 0 && ++acb->child_iter < s->num_children) {
            read_fifo_child(acb);
            return;
        }

        if (ret == 0) {
            quorum_copy_qiov(acb->qiov, &acb->qcrs[acb->child_iter].qiov);
        }
        acb->vote_ret = ret;
        quorum_aio_finalize(acb);
        return;
    }

    sacb->ret = ret;
    acb->count++;
    if (ret == 0) {
        acb->success_count++;
    } else {
        quorum_report_bad(acb, sacb->aiocb->bs->node_name, ret);
    }
    assert(acb->count <= s->num_children);
    assert(acb->success_count <= s->num_children);
    if (acb->count < s->num_children) {
        return;
    }

    /* Do the vote on read */
    if (acb->is_read) {
        rewrite = quorum_vote(acb);
    } else {
        quorum_has_too_much_io_failed(acb);
    }

    /* if no rewrite is done the code will finish right away */
    if (!rewrite) {
        quorum_aio_finalize(acb);
    }
}

static void quorum_report_bad_versions(BDRVQuorumState *s,
                                       QuorumAIOCB *acb,
                                       QuorumVoteValue *value)
{
    QuorumVoteVersion *version;
    QuorumVoteItem *item;

    QLIST_FOREACH(version, &acb->votes.vote_list, next) {
        if (acb->votes.compare(&version->value, value)) {
            continue;
        }
        QLIST_FOREACH(item, &version->items, next) {
            quorum_report_bad(acb, s->bs[item->index]->node_name, 0);
        }
    }
}

static bool quorum_rewrite_bad_versions(BDRVQuorumState *s, QuorumAIOCB *acb,
                                        QuorumVoteValue *value)
{
    QuorumVoteVersion *version;
    QuorumVoteItem *item;
    int count = 0;

    /* first count the number of bad versions: done first to avoid concurrency
     * issues.
     */
    QLIST_FOREACH(version, &acb->votes.vote_list, next) {
        if (acb->votes.compare(&version->value, value)) {
            continue;
        }
        QLIST_FOREACH(item, &version->items, next) {
            count++;
        }
    }

    /* quorum_rewrite_aio_cb will count down this to zero */
    acb->rewrite_count = count;

    /* now fire the correcting rewrites */
    QLIST_FOREACH(version, &acb->votes.vote_list, next) {
        if (acb->votes.compare(&version->value, value)) {
            continue;
        }
        QLIST_FOREACH(item, &version->items, next) {
            bdrv_aio_writev(s->bs[item->index], acb->sector_num, acb->qiov,
                            acb->nb_sectors, quorum_rewrite_aio_cb, acb);
        }
    }

    /* return true if any rewrite is done else false */
    return count;
}

static void quorum_count_vote(QuorumVotes *votes,
                              QuorumVoteValue *value,
                              int index)
{
    QuorumVoteVersion *v = NULL, *version = NULL;
    QuorumVoteItem *item;

    /* look if we have something with this hash */
    QLIST_FOREACH(v, &votes->vote_list, next) {
        if (votes->compare(&v->value, value)) {
            version = v;
            break;
        }
    }

    /* It's a version not yet in the list add it */
    if (!version) {
        version = g_new0(QuorumVoteVersion, 1);
        QLIST_INIT(&version->items);
        memcpy(&version->value, value, sizeof(version->value));
        version->index = index;
        version->vote_count = 0;
        QLIST_INSERT_HEAD(&votes->vote_list, version, next);
    }

    version->vote_count++;

    item = g_new0(QuorumVoteItem, 1);
    item->index = index;
    QLIST_INSERT_HEAD(&version->items, item, next);
}

static void quorum_free_vote_list(QuorumVotes *votes)
{
    QuorumVoteVersion *version, *next_version;
    QuorumVoteItem *item, *next_item;

    QLIST_FOREACH_SAFE(version, &votes->vote_list, next, next_version) {
        QLIST_REMOVE(version, next);
        QLIST_FOREACH_SAFE(item, &version->items, next, next_item) {
            QLIST_REMOVE(item, next);
            g_free(item);
        }
        g_free(version);
    }
}

static int quorum_compute_hash(QuorumAIOCB *acb, int i, QuorumVoteValue *hash)
{
    int j, ret;
    gnutls_hash_hd_t dig;
    QEMUIOVector *qiov = &acb->qcrs[i].qiov;

    ret = gnutls_hash_init(&dig, GNUTLS_DIG_SHA256);

    if (ret < 0) {
        return ret;
    }

    for (j = 0; j < qiov->niov; j++) {
        ret = gnutls_hash(dig, qiov->iov[j].iov_base, qiov->iov[j].iov_len);
        if (ret < 0) {
            break;
        }
    }

    gnutls_hash_deinit(dig, (void *) hash);
    return ret;
}

static QuorumVoteVersion *quorum_get_vote_winner(QuorumVotes *votes)
{
    int max = 0;
    QuorumVoteVersion *candidate, *winner = NULL;

    QLIST_FOREACH(candidate, &votes->vote_list, next) {
        if (candidate->vote_count > max) {
            max = candidate->vote_count;
            winner = candidate;
        }
    }

    return winner;
}

/* qemu_iovec_compare is handy for blkverify mode because it returns the first
 * differing byte location. Yet it is handcoded to compare vectors one byte
 * after another so it does not benefit from the libc SIMD optimizations.
 * quorum_iovec_compare is written for speed and should be used in the non
 * blkverify mode of quorum.
 */
static bool quorum_iovec_compare(QEMUIOVector *a, QEMUIOVector *b)
{
    int i;
    int result;

    assert(a->niov == b->niov);
    for (i = 0; i < a->niov; i++) {
        assert(a->iov[i].iov_len == b->iov[i].iov_len);
        result = memcmp(a->iov[i].iov_base,
                        b->iov[i].iov_base,
                        a->iov[i].iov_len);
        if (result) {
            return false;
        }
    }

    return true;
}

static void GCC_FMT_ATTR(2, 3) quorum_err(QuorumAIOCB *acb,
                                          const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "quorum: sector_num=%" PRId64 " nb_sectors=%d ",
            acb->sector_num, acb->nb_sectors);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static bool quorum_compare(QuorumAIOCB *acb,
                           QEMUIOVector *a,
                           QEMUIOVector *b)
{
    BDRVQuorumState *s = acb->common.bs->opaque;
    ssize_t offset;

    /* This driver will replace blkverify in this particular case */
    if (s->is_blkverify) {
        offset = qemu_iovec_compare(a, b);
        if (offset != -1) {
            quorum_err(acb, "contents mismatch in sector %" PRId64,
                       acb->sector_num +
                       (uint64_t)(offset / BDRV_SECTOR_SIZE));
        }
        return true;
    }

    return quorum_iovec_compare(a, b);
}

/* Do a vote to get the error code */
static int quorum_vote_error(QuorumAIOCB *acb)
{
    BDRVQuorumState *s = acb->common.bs->opaque;
    QuorumVoteVersion *winner = NULL;
    QuorumVotes error_votes;
    QuorumVoteValue result_value;
    int i, ret = 0;
    bool error = false;

    QLIST_INIT(&error_votes.vote_list);
    error_votes.compare = quorum_64bits_compare;

    for (i = 0; i < s->num_children; i++) {
        ret = acb->qcrs[i].ret;
        if (ret) {
            error = true;
            result_value.l = ret;
            quorum_count_vote(&error_votes, &result_value, i);
        }
    }

    if (error) {
        winner = quorum_get_vote_winner(&error_votes);
        ret = winner->value.l;
    }

    quorum_free_vote_list(&error_votes);

    return ret;
}

static bool quorum_vote(QuorumAIOCB *acb)
{
    bool quorum = true;
    bool rewrite = false;
    int i, j, ret;
    QuorumVoteValue hash;
    BDRVQuorumState *s = acb->common.bs->opaque;
    QuorumVoteVersion *winner;

    if (quorum_has_too_much_io_failed(acb)) {
        return false;
    }

    /* get the index of the first successful read */
    for (i = 0; i < s->num_children; i++) {
        if (!acb->qcrs[i].ret) {
            break;
        }
    }

    assert(i < s->num_children);

    /* compare this read with all other successful reads stopping at quorum
     * failure
     */
    for (j = i + 1; j < s->num_children; j++) {
        if (acb->qcrs[j].ret) {
            continue;
        }
        quorum = quorum_compare(acb, &acb->qcrs[i].qiov, &acb->qcrs[j].qiov);
        if (!quorum) {
            break;
       }
    }

    /* Every successful read agrees */
    if (quorum) {
        quorum_copy_qiov(acb->qiov, &acb->qcrs[i].qiov);
        return false;
    }

    /* compute hashes for each successful read, also store indexes */
    for (i = 0; i < s->num_children; i++) {
        if (acb->qcrs[i].ret) {
            continue;
        }
        ret = quorum_compute_hash(acb, i, &hash);
        /* if ever the hash computation failed */
        if (ret < 0) {
            acb->vote_ret = ret;
            goto free_exit;
        }
        quorum_count_vote(&acb->votes, &hash, i);
    }

    /* vote to select the most represented version */
    winner = quorum_get_vote_winner(&acb->votes);

    /* if the winner count is smaller than threshold the read fails */
    if (winner->vote_count < s->threshold) {
        quorum_report_failure(acb);
        acb->vote_ret = -EIO;
        goto free_exit;
    }

    /* we have a winner: copy it */
    quorum_copy_qiov(acb->qiov, &acb->qcrs[winner->index].qiov);

    /* some versions are bad print them */
    quorum_report_bad_versions(s, acb, &winner->value);

    /* corruption correction is enabled */
    if (s->rewrite_corrupted) {
        rewrite = quorum_rewrite_bad_versions(s, acb, &winner->value);
    }

free_exit:
    /* free lists */
    quorum_free_vote_list(&acb->votes);
    return rewrite;
}

static BlockDriverAIOCB *read_quorum_children(QuorumAIOCB *acb)
{
    BDRVQuorumState *s = acb->common.bs->opaque;
    int i;

    for (i = 0; i < s->num_children; i++) {
        acb->qcrs[i].buf = qemu_blockalign(s->bs[i], acb->qiov->size);
        qemu_iovec_init(&acb->qcrs[i].qiov, acb->qiov->niov);
        qemu_iovec_clone(&acb->qcrs[i].qiov, acb->qiov, acb->qcrs[i].buf);
    }

    for (i = 0; i < s->num_children; i++) {
        bdrv_aio_readv(s->bs[i], acb->sector_num, &acb->qcrs[i].qiov,
                       acb->nb_sectors, quorum_aio_cb, &acb->qcrs[i]);
    }

    return &acb->common;
}

static BlockDriverAIOCB *read_fifo_child(QuorumAIOCB *acb)
{
    BDRVQuorumState *s = acb->common.bs->opaque;

    acb->qcrs[acb->child_iter].buf = qemu_blockalign(s->bs[acb->child_iter],
                                                     acb->qiov->size);
    qemu_iovec_init(&acb->qcrs[acb->child_iter].qiov, acb->qiov->niov);
    qemu_iovec_clone(&acb->qcrs[acb->child_iter].qiov, acb->qiov,
                     acb->qcrs[acb->child_iter].buf);
    bdrv_aio_readv(s->bs[acb->child_iter], acb->sector_num,
                   &acb->qcrs[acb->child_iter].qiov, acb->nb_sectors,
                   quorum_aio_cb, &acb->qcrs[acb->child_iter]);

    return &acb->common;
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
    acb->is_read = true;

    if (s->read_pattern == QUORUM_READ_PATTERN_QUORUM) {
        acb->child_iter = s->num_children - 1;
        return read_quorum_children(acb);
    }

    acb->child_iter = 0;
    return read_fifo_child(acb);
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

static int64_t quorum_getlength(BlockDriverState *bs)
{
    BDRVQuorumState *s = bs->opaque;
    int64_t result;
    int i;

    /* check that all file have the same length */
    result = bdrv_getlength(s->bs[0]);
    if (result < 0) {
        return result;
    }
    for (i = 1; i < s->num_children; i++) {
        int64_t value = bdrv_getlength(s->bs[i]);
        if (value < 0) {
            return value;
        }
        if (value != result) {
            return -EIO;
        }
    }

    return result;
}

static void quorum_invalidate_cache(BlockDriverState *bs, Error **errp)
{
    BDRVQuorumState *s = bs->opaque;
    Error *local_err = NULL;
    int i;

    for (i = 0; i < s->num_children; i++) {
        bdrv_invalidate_cache(s->bs[i], &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

static coroutine_fn int quorum_co_flush(BlockDriverState *bs)
{
    BDRVQuorumState *s = bs->opaque;
    QuorumVoteVersion *winner = NULL;
    QuorumVotes error_votes;
    QuorumVoteValue result_value;
    int i;
    int result = 0;

    QLIST_INIT(&error_votes.vote_list);
    error_votes.compare = quorum_64bits_compare;

    for (i = 0; i < s->num_children; i++) {
        result = bdrv_co_flush(s->bs[i]);
        result_value.l = result;
        quorum_count_vote(&error_votes, &result_value, i);
    }

    winner = quorum_get_vote_winner(&error_votes);
    result = winner->value.l;

    quorum_free_vote_list(&error_votes);

    return result;
}

static bool quorum_recurse_is_first_non_filter(BlockDriverState *bs,
                                               BlockDriverState *candidate)
{
    BDRVQuorumState *s = bs->opaque;
    int i;

    for (i = 0; i < s->num_children; i++) {
        bool perm = bdrv_recurse_is_first_non_filter(s->bs[i],
                                                     candidate);
        if (perm) {
            return true;
        }
    }

    return false;
}

static int quorum_valid_threshold(int threshold, int num_children, Error **errp)
{

    if (threshold < 1) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE,
                  "vote-threshold", "value >= 1");
        return -ERANGE;
    }

    if (threshold > num_children) {
        error_setg(errp, "threshold may not exceed children count");
        return -ERANGE;
    }

    return 0;
}

static QemuOptsList quorum_runtime_opts = {
    .name = "quorum",
    .head = QTAILQ_HEAD_INITIALIZER(quorum_runtime_opts.head),
    .desc = {
        {
            .name = QUORUM_OPT_VOTE_THRESHOLD,
            .type = QEMU_OPT_NUMBER,
            .help = "The number of vote needed for reaching quorum",
        },
        {
            .name = QUORUM_OPT_BLKVERIFY,
            .type = QEMU_OPT_BOOL,
            .help = "Trigger block verify mode if set",
        },
        {
            .name = QUORUM_OPT_REWRITE,
            .type = QEMU_OPT_BOOL,
            .help = "Rewrite corrupted block on read quorum",
        },
        {
            .name = QUORUM_OPT_READ_PATTERN,
            .type = QEMU_OPT_STRING,
            .help = "Allowed pattern: quorum, fifo. Quorum is default",
        },
        { /* end of list */ }
    },
};

static int parse_read_pattern(const char *opt)
{
    int i;

    if (!opt) {
        /* Set quorum as default */
        return QUORUM_READ_PATTERN_QUORUM;
    }

    for (i = 0; i < QUORUM_READ_PATTERN_MAX; i++) {
        if (!strcmp(opt, QuorumReadPattern_lookup[i])) {
            return i;
        }
    }

    return -EINVAL;
}

static int quorum_open(BlockDriverState *bs, QDict *options, int flags,
                       Error **errp)
{
    BDRVQuorumState *s = bs->opaque;
    Error *local_err = NULL;
    QemuOpts *opts = NULL;
    bool *opened;
    QDict *sub = NULL;
    QList *list = NULL;
    const QListEntry *lentry;
    int i;
    int ret = 0;

    qdict_flatten(options);
    qdict_extract_subqdict(options, &sub, "children.");
    qdict_array_split(sub, &list);

    if (qdict_size(sub)) {
        error_setg(&local_err, "Invalid option children.%s",
                   qdict_first(sub)->key);
        ret = -EINVAL;
        goto exit;
    }

    /* count how many different children are present */
    s->num_children = qlist_size(list);
    if (s->num_children < 2) {
        error_setg(&local_err,
                   "Number of provided children must be greater than 1");
        ret = -EINVAL;
        goto exit;
    }

    opts = qemu_opts_create(&quorum_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        ret = -EINVAL;
        goto exit;
    }

    s->threshold = qemu_opt_get_number(opts, QUORUM_OPT_VOTE_THRESHOLD, 0);
    ret = parse_read_pattern(qemu_opt_get(opts, QUORUM_OPT_READ_PATTERN));
    if (ret < 0) {
        error_setg(&local_err, "Please set read-pattern as fifo or quorum");
        goto exit;
    }
    s->read_pattern = ret;

    if (s->read_pattern == QUORUM_READ_PATTERN_QUORUM) {
        /* and validate it against s->num_children */
        ret = quorum_valid_threshold(s->threshold, s->num_children, &local_err);
        if (ret < 0) {
            goto exit;
        }

        /* is the driver in blkverify mode */
        if (qemu_opt_get_bool(opts, QUORUM_OPT_BLKVERIFY, false) &&
            s->num_children == 2 && s->threshold == 2) {
            s->is_blkverify = true;
        } else if (qemu_opt_get_bool(opts, QUORUM_OPT_BLKVERIFY, false)) {
            fprintf(stderr, "blkverify mode is set by setting blkverify=on "
                    "and using two files with vote_threshold=2\n");
        }

        s->rewrite_corrupted = qemu_opt_get_bool(opts, QUORUM_OPT_REWRITE,
                                                 false);
        if (s->rewrite_corrupted && s->is_blkverify) {
            error_setg(&local_err,
                       "rewrite-corrupted=on cannot be used with blkverify=on");
            ret = -EINVAL;
            goto exit;
        }
    }

    /* allocate the children BlockDriverState array */
    s->bs = g_new0(BlockDriverState *, s->num_children);
    opened = g_new0(bool, s->num_children);

    for (i = 0, lentry = qlist_first(list); lentry;
         lentry = qlist_next(lentry), i++) {
        QDict *d;
        QString *string;

        switch (qobject_type(lentry->value))
        {
            /* List of options */
            case QTYPE_QDICT:
                d = qobject_to_qdict(lentry->value);
                QINCREF(d);
                ret = bdrv_open(&s->bs[i], NULL, NULL, d, flags, NULL,
                                &local_err);
                break;

            /* QMP reference */
            case QTYPE_QSTRING:
                string = qobject_to_qstring(lentry->value);
                ret = bdrv_open(&s->bs[i], NULL, qstring_get_str(string), NULL,
                                flags, NULL, &local_err);
                break;

            default:
                error_setg(&local_err, "Specification of child block device %i "
                           "is invalid", i);
                ret = -EINVAL;
        }

        if (ret < 0) {
            goto close_exit;
        }
        opened[i] = true;
    }

    g_free(opened);
    goto exit;

close_exit:
    /* cleanup on error */
    for (i = 0; i < s->num_children; i++) {
        if (!opened[i]) {
            continue;
        }
        bdrv_unref(s->bs[i]);
    }
    g_free(s->bs);
    g_free(opened);
exit:
    qemu_opts_del(opts);
    /* propagate error */
    if (local_err) {
        error_propagate(errp, local_err);
    }
    QDECREF(list);
    QDECREF(sub);
    return ret;
}

static void quorum_close(BlockDriverState *bs)
{
    BDRVQuorumState *s = bs->opaque;
    int i;

    for (i = 0; i < s->num_children; i++) {
        bdrv_unref(s->bs[i]);
    }

    g_free(s->bs);
}

static void quorum_detach_aio_context(BlockDriverState *bs)
{
    BDRVQuorumState *s = bs->opaque;
    int i;

    for (i = 0; i < s->num_children; i++) {
        bdrv_detach_aio_context(s->bs[i]);
    }
}

static void quorum_attach_aio_context(BlockDriverState *bs,
                                      AioContext *new_context)
{
    BDRVQuorumState *s = bs->opaque;
    int i;

    for (i = 0; i < s->num_children; i++) {
        bdrv_attach_aio_context(s->bs[i], new_context);
    }
}

static void quorum_refresh_filename(BlockDriverState *bs)
{
    BDRVQuorumState *s = bs->opaque;
    QDict *opts;
    QList *children;
    int i;

    for (i = 0; i < s->num_children; i++) {
        bdrv_refresh_filename(s->bs[i]);
        if (!s->bs[i]->full_open_options) {
            return;
        }
    }

    children = qlist_new();
    for (i = 0; i < s->num_children; i++) {
        QINCREF(s->bs[i]->full_open_options);
        qlist_append_obj(children, QOBJECT(s->bs[i]->full_open_options));
    }

    opts = qdict_new();
    qdict_put_obj(opts, "driver", QOBJECT(qstring_from_str("quorum")));
    qdict_put_obj(opts, QUORUM_OPT_VOTE_THRESHOLD,
                  QOBJECT(qint_from_int(s->threshold)));
    qdict_put_obj(opts, QUORUM_OPT_BLKVERIFY,
                  QOBJECT(qbool_from_int(s->is_blkverify)));
    qdict_put_obj(opts, QUORUM_OPT_REWRITE,
                  QOBJECT(qbool_from_int(s->rewrite_corrupted)));
    qdict_put_obj(opts, "children", QOBJECT(children));

    bs->full_open_options = opts;
}

static BlockDriver bdrv_quorum = {
    .format_name                        = "quorum",
    .protocol_name                      = "quorum",

    .instance_size                      = sizeof(BDRVQuorumState),

    .bdrv_file_open                     = quorum_open,
    .bdrv_close                         = quorum_close,
    .bdrv_refresh_filename              = quorum_refresh_filename,

    .bdrv_co_flush_to_disk              = quorum_co_flush,

    .bdrv_getlength                     = quorum_getlength,

    .bdrv_aio_readv                     = quorum_aio_readv,
    .bdrv_aio_writev                    = quorum_aio_writev,
    .bdrv_invalidate_cache              = quorum_invalidate_cache,

    .bdrv_detach_aio_context            = quorum_detach_aio_context,
    .bdrv_attach_aio_context            = quorum_attach_aio_context,

    .is_filter                          = true,
    .bdrv_recurse_is_first_non_filter   = quorum_recurse_is_first_non_filter,
};

static void bdrv_quorum_init(void)
{
    bdrv_register(&bdrv_quorum);
}

block_init(bdrv_quorum_init);
