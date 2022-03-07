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

#include "qemu/osdep.h"
#include "qemu/range.h"

#include "block/reqlist.h"

void reqlist_init_req(BlockReqList *reqs, BlockReq *req, int64_t offset,
                      int64_t bytes)
{
    assert(!reqlist_find_conflict(reqs, offset, bytes));

    *req = (BlockReq) {
        .offset = offset,
        .bytes = bytes,
    };
    qemu_co_queue_init(&req->wait_queue);
    QLIST_INSERT_HEAD(reqs, req, list);
}

BlockReq *reqlist_find_conflict(BlockReqList *reqs, int64_t offset,
                                int64_t bytes)
{
    BlockReq *r;

    QLIST_FOREACH(r, reqs, list) {
        if (ranges_overlap(offset, bytes, r->offset, r->bytes)) {
            return r;
        }
    }

    return NULL;
}

bool coroutine_fn reqlist_wait_one(BlockReqList *reqs, int64_t offset,
                                   int64_t bytes, CoMutex *lock)
{
    BlockReq *r = reqlist_find_conflict(reqs, offset, bytes);

    if (!r) {
        return false;
    }

    qemu_co_queue_wait(&r->wait_queue, lock);

    return true;
}

void coroutine_fn reqlist_wait_all(BlockReqList *reqs, int64_t offset,
                                   int64_t bytes, CoMutex *lock)
{
    while (reqlist_wait_one(reqs, offset, bytes, lock)) {
        /* continue */
    }
}

void coroutine_fn reqlist_shrink_req(BlockReq *req, int64_t new_bytes)
{
    if (new_bytes == req->bytes) {
        return;
    }

    assert(new_bytes > 0 && new_bytes < req->bytes);

    req->bytes = new_bytes;
    qemu_co_queue_restart_all(&req->wait_queue);
}

void coroutine_fn reqlist_remove_req(BlockReq *req)
{
    QLIST_REMOVE(req, list);
    qemu_co_queue_restart_all(&req->wait_queue);
}
