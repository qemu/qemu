/*
 * reqlist API
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2021 Virtuozzo International GmbH.
 *
 * Authors:
 *  Dietmar Maurer (dietmar@proxmox.com)
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef REQLIST_H
#define REQLIST_H

#include "qemu/coroutine.h"

/*
 * The API is not thread-safe and shouldn't be. The struct is public to be part
 * of other structures and protected by third-party locks, see
 * block/block-copy.c for example.
 */

typedef struct BlockReq {
    int64_t offset;
    int64_t bytes;

    CoQueue wait_queue; /* coroutines blocked on this req */
    QLIST_ENTRY(BlockReq) list;
} BlockReq;

typedef QLIST_HEAD(, BlockReq) BlockReqList;

/*
 * Initialize new request and add it to the list. Caller must be sure that
 * there are no conflicting requests in the list.
 */
void reqlist_init_req(BlockReqList *reqs, BlockReq *req, int64_t offset,
                      int64_t bytes);
/* Search for request in the list intersecting with @offset/@bytes area. */
BlockReq *reqlist_find_conflict(BlockReqList *reqs, int64_t offset,
                                int64_t bytes);

/*
 * If there are no intersecting requests return false. Otherwise, wait for the
 * first found intersecting request to finish and return true.
 *
 * @lock is passed to qemu_co_queue_wait()
 * False return value proves that lock was released at no point.
 */
bool coroutine_fn reqlist_wait_one(BlockReqList *reqs, int64_t offset,
                                   int64_t bytes, CoMutex *lock);

/*
 * Wait for all intersecting requests. It just calls reqlist_wait_one() in a
 * loop, caller is responsible to stop producing new requests in this region
 * in parallel, otherwise reqlist_wait_all() may never return.
 */
void coroutine_fn reqlist_wait_all(BlockReqList *reqs, int64_t offset,
                                   int64_t bytes, CoMutex *lock);

/*
 * Shrink request and wake all waiting coroutines (maybe some of them are not
 * intersecting with shrunk request).
 */
void coroutine_fn reqlist_shrink_req(BlockReq *req, int64_t new_bytes);

/*
 * Remove request and wake all waiting coroutines. Do not release any memory.
 */
void coroutine_fn reqlist_remove_req(BlockReq *req);

#endif /* REQLIST_H */
