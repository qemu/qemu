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
#include "sysemu/blockdev.h"

struct BlockBackend {
    char *name;
    int refcnt;
    BlockDriverState *bs;
    DriveInfo *legacy_dinfo;
    QTAILQ_ENTRY(BlockBackend) link; /* for blk_backends */
};

static void drive_info_del(DriveInfo *dinfo);

/* All the BlockBackends (except for hidden ones) */
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
    if (!id_wellformed(name)) {
        error_setg(errp, "Invalid device name");
        return NULL;
    }
    if (blk_by_name(name)) {
        error_setg(errp, "Device with id '%s' already exists", name);
        return NULL;
    }
    if (bdrv_find_node(name)) {
        error_setg(errp,
                   "Device name '%s' conflicts with an existing node name",
                   name);
        return NULL;
    }

    blk = g_new0(BlockBackend, 1);
    blk->name = g_strdup(name);
    blk->refcnt = 1;
    QTAILQ_INSERT_TAIL(&blk_backends, blk, link);
    return blk;
}

/*
 * Create a new BlockBackend with a new BlockDriverState attached.
 * Otherwise just like blk_new(), which see.
 */
BlockBackend *blk_new_with_bs(const char *name, Error **errp)
{
    BlockBackend *blk;
    BlockDriverState *bs;

    blk = blk_new(name, errp);
    if (!blk) {
        return NULL;
    }

    bs = bdrv_new_root();
    blk->bs = bs;
    bs->blk = blk;
    return blk;
}

static void blk_delete(BlockBackend *blk)
{
    assert(!blk->refcnt);
    if (blk->bs) {
        assert(blk->bs->blk == blk);
        blk->bs->blk = NULL;
        bdrv_unref(blk->bs);
        blk->bs = NULL;
    }
    /* Avoid double-remove after blk_hide_on_behalf_of_do_drive_del() */
    if (blk->name[0]) {
        QTAILQ_REMOVE(&blk_backends, blk, link);
    }
    g_free(blk->name);
    drive_info_del(blk->legacy_dinfo);
    g_free(blk);
}

static void drive_info_del(DriveInfo *dinfo)
{
    if (!dinfo) {
        return;
    }
    qemu_opts_del(dinfo->opts);
    g_free(dinfo->id);
    g_free(dinfo->serial);
    g_free(dinfo);
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
 * Return @blk's name, a non-null string.
 * Wart: the name is empty iff @blk has been hidden with
 * blk_hide_on_behalf_of_do_drive_del().
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

/*
 * Return the BlockDriverState attached to @blk if any, else null.
 */
BlockDriverState *blk_bs(BlockBackend *blk)
{
    return blk->bs;
}

/*
 * Return @blk's DriveInfo if any, else null.
 */
DriveInfo *blk_legacy_dinfo(BlockBackend *blk)
{
    return blk->legacy_dinfo;
}

/*
 * Set @blk's DriveInfo to @dinfo, and return it.
 * @blk must not have a DriveInfo set already.
 * No other BlockBackend may have the same DriveInfo set.
 */
DriveInfo *blk_set_legacy_dinfo(BlockBackend *blk, DriveInfo *dinfo)
{
    assert(!blk->legacy_dinfo);
    return blk->legacy_dinfo = dinfo;
}

/*
 * Return the BlockBackend with DriveInfo @dinfo.
 * It must exist.
 */
BlockBackend *blk_by_legacy_dinfo(DriveInfo *dinfo)
{
    BlockBackend *blk;

    QTAILQ_FOREACH(blk, &blk_backends, link) {
        if (blk->legacy_dinfo == dinfo) {
            return blk;
        }
    }
    abort();
}

/*
 * Hide @blk.
 * @blk must not have been hidden already.
 * Make attached BlockDriverState, if any, anonymous.
 * Once hidden, @blk is invisible to all functions that don't receive
 * it as argument.  For example, blk_by_name() won't return it.
 * Strictly for use by do_drive_del().
 * TODO get rid of it!
 */
void blk_hide_on_behalf_of_do_drive_del(BlockBackend *blk)
{
    QTAILQ_REMOVE(&blk_backends, blk, link);
    blk->name[0] = 0;
    if (blk->bs) {
        bdrv_make_anon(blk->bs);
    }
}
