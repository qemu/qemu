/*
 * QEMU Block backends
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#include "sysemu/block-backend.h"
#include "block/block_int.h"

struct BlockBackend {
    char *name;
    int refcnt;
    QTAILQ_ENTRY(BlockBackend) link; /* for blk_backends */
};

/* All the BlockBackends */
static QTAILQ_HEAD(, BlockBackend) blk_backends =
    QTAILQ_HEAD_INITIALIZER(blk_backends);

/*
 * Create a new BlockBackend with @name, with a reference count of one.
 * @name must not be null or empty.
 * Fail if a BlockBackend with this name already exists.
 * Store an error through @errp on failure, unless it's null.
 * Return the new BlockBackend on success, null on failure.
 */
BlockBackend *blk_new(const char *name, Error **errp)
{
    BlockBackend *blk;

    assert(name && name[0]);
    if (blk_by_name(name)) {
        error_setg(errp, "Device with id '%s' already exists", name);
        return NULL;
    }

    blk = g_new0(BlockBackend, 1);
    blk->name = g_strdup(name);
    blk->refcnt = 1;
    QTAILQ_INSERT_TAIL(&blk_backends, blk, link);
    return blk;
}

static void blk_delete(BlockBackend *blk)
{
    assert(!blk->refcnt);
    QTAILQ_REMOVE(&blk_backends, blk, link);
    g_free(blk->name);
    g_free(blk);
}

/*
 * Increment @blk's reference count.
 * @blk must not be null.
 */
void blk_ref(BlockBackend *blk)
{
    blk->refcnt++;
}

/*
 * Decrement @blk's reference count.
 * If this drops it to zero, destroy @blk.
 * For convenience, do nothing if @blk is null.
 */
void blk_unref(BlockBackend *blk)
{
    if (blk) {
        assert(blk->refcnt > 0);
        if (!--blk->refcnt) {
            blk_delete(blk);
        }
    }
}

/*
 * Return the BlockBackend after @blk.
 * If @blk is null, return the first one.
 * Else, return @blk's next sibling, which may be null.
 *
 * To iterate over all BlockBackends, do
 * for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
 *     ...
 * }
 */
BlockBackend *blk_next(BlockBackend *blk)
{
    return blk ? QTAILQ_NEXT(blk, link) : QTAILQ_FIRST(&blk_backends);
}

/*
 * Return @blk's name, a non-null, non-empty string.
 */
const char *blk_name(BlockBackend *blk)
{
    return blk->name;
}

/*
 * Return the BlockBackend with name @name if it exists, else null.
 * @name must not be null.
 */
BlockBackend *blk_by_name(const char *name)
{
    BlockBackend *blk;

    assert(name);
    QTAILQ_FOREACH(blk, &blk_backends, link) {
        if (!strcmp(name, blk->name)) {
            return blk;
        }
    }
    return NULL;
}
