/*
 * 9p backend
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

/*
 * Not so fast! You might want to read the 9p developer docs first:
 * https://wiki.qemu.org/Documentation/9p
 */

#include "qemu/osdep.h"
#include "fsdev/qemu-fsdev.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "coth.h"
#include "9p-xattr.h"
#include "9p-util.h"

/*
 * Intended to be called from bottom-half (e.g. background I/O thread)
 * context.
 */
static int do_readdir(V9fsPDU *pdu, V9fsFidState *fidp, struct dirent **dent)
{
    int err = 0;
    V9fsState *s = pdu->s;
    struct dirent *entry;

    errno = 0;
    entry = s->ops->readdir(&s->ctx, &fidp->fs);
    if (!entry && errno) {
        *dent = NULL;
        err = -errno;
    } else {
        *dent = entry;
    }
    return err;
}

/*
 * TODO: This will be removed for performance reasons.
 * Use v9fs_co_readdir_many() instead.
 */
int coroutine_fn v9fs_co_readdir(V9fsPDU *pdu, V9fsFidState *fidp,
                                 struct dirent **dent)
{
    int err;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_co_run_in_worker({
        err = do_readdir(pdu, fidp, dent);
    });
    return err;
}

/*
 * This is solely executed on a background IO thread.
 *
 * See v9fs_co_readdir_many() (as its only user) below for details.
 */
static int coroutine_fn
do_readdir_many(V9fsPDU *pdu, V9fsFidState *fidp, struct V9fsDirEnt **entries,
                off_t offset, int32_t maxsize, bool dostat)
{
    V9fsState *s = pdu->s;
    V9fsString name;
    int len, err = 0;
    int32_t size = 0;
    off_t saved_dir_pos;
    struct dirent *dent;
    struct V9fsDirEnt *e = NULL;
    V9fsPath path;
    struct stat stbuf;

    *entries = NULL;
    v9fs_path_init(&path);

    /*
     * TODO: Here should be a warn_report_once() if lock failed.
     *
     * With a good 9p client we should not get into concurrency here,
     * because a good client would not use the same fid for concurrent
     * requests. We do the lock here for safety reasons though. However
     * the client would then suffer performance issues, so better log that
     * issue here.
     */
    v9fs_readdir_lock(&fidp->fs.dir);

    /* seek directory to requested initial position */
    if (offset == 0) {
        s->ops->rewinddir(&s->ctx, &fidp->fs);
    } else {
        s->ops->seekdir(&s->ctx, &fidp->fs, offset);
    }

    /* save the directory position */
    saved_dir_pos = s->ops->telldir(&s->ctx, &fidp->fs);
    if (saved_dir_pos < 0) {
        err = saved_dir_pos;
        goto out;
    }

    while (true) {
        /* interrupt loop if request was cancelled by a Tflush request */
        if (v9fs_request_cancelled(pdu)) {
            err = -EINTR;
            break;
        }

        /* get directory entry from fs driver */
        err = do_readdir(pdu, fidp, &dent);
        if (err || !dent) {
            break;
        }

        /*
         * stop this loop as soon as it would exceed the allowed maximum
         * response message size for the directory entries collected so far,
         * because anything beyond that size would need to be discarded by
         * 9p controller (main thread / top half) anyway
         */
        v9fs_string_init(&name);
        v9fs_string_sprintf(&name, "%s", dent->d_name);
        len = v9fs_readdir_response_size(&name);
        v9fs_string_free(&name);
        if (size + len > maxsize) {
            /* this is not an error case actually */
            break;
        }

        /* append next node to result chain */
        if (!e) {
            *entries = e = g_new0(V9fsDirEnt, 1);
        } else {
            e = e->next = g_new0(V9fsDirEnt, 1);
        }
        e->dent = qemu_dirent_dup(dent);

        /* perform a full stat() for directory entry if requested by caller */
        if (dostat) {
            err = s->ops->name_to_path(
                &s->ctx, &fidp->path, dent->d_name, &path
            );
            if (err < 0) {
                err = -errno;
                break;
            }

            err = s->ops->lstat(&s->ctx, &path, &stbuf);
            if (err < 0) {
                err = -errno;
                break;
            }

            e->st = g_new0(struct stat, 1);
            memcpy(e->st, &stbuf, sizeof(struct stat));
        }

        size += len;
        saved_dir_pos = qemu_dirent_off(dent);
    }

    /* restore (last) saved position */
    s->ops->seekdir(&s->ctx, &fidp->fs, saved_dir_pos);

out:
    v9fs_readdir_unlock(&fidp->fs.dir);
    v9fs_path_free(&path);
    if (err < 0) {
        return err;
    }
    return size;
}

/**
 * v9fs_co_readdir_many() - Reads multiple directory entries in one rush.
 *
 * @pdu: the causing 9p (T_readdir) client request
 * @fidp: already opened directory where readdir shall be performed on
 * @entries: output for directory entries (must not be NULL)
 * @offset: initial position inside the directory the function shall
 *          seek to before retrieving the directory entries
 * @maxsize: maximum result message body size (in bytes)
 * @dostat: whether a stat() should be performed and returned for
 *          each directory entry
 * Return: resulting response message body size (in bytes) on success,
 *         negative error code otherwise
 *
 * Retrieves the requested (max. amount of) directory entries from the fs
 * driver. This function must only be called by the main IO thread (top half).
 * Internally this function call will be dispatched to a background IO thread
 * (bottom half) where it is eventually executed by the fs driver.
 *
 * Acquiring multiple directory entries in one rush from the fs
 * driver, instead of retrieving each directory entry individually, is very
 * beneficial from performance point of view. Because for every fs driver
 * request latency is added, which in practice could lead to overall
 * latencies of several hundred ms for reading all entries (of just a single
 * directory) if every directory entry was individually requested from fs
 * driver.
 *
 * NOTE: You must ALWAYS call v9fs_free_dirents(entries) after calling
 * v9fs_co_readdir_many(), both on success and on error cases of this
 * function, to avoid memory leaks once @entries are no longer needed.
 */
int coroutine_fn v9fs_co_readdir_many(V9fsPDU *pdu, V9fsFidState *fidp,
                                      struct V9fsDirEnt **entries,
                                      off_t offset, int32_t maxsize,
                                      bool dostat)
{
    int err = 0;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_co_run_in_worker({
        err = do_readdir_many(pdu, fidp, entries, offset, maxsize, dostat);
    });
    return err;
}

off_t v9fs_co_telldir(V9fsPDU *pdu, V9fsFidState *fidp)
{
    off_t err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_co_run_in_worker(
        {
            err = s->ops->telldir(&s->ctx, &fidp->fs);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

void coroutine_fn v9fs_co_seekdir(V9fsPDU *pdu, V9fsFidState *fidp,
                                  off_t offset)
{
    V9fsState *s = pdu->s;
    if (v9fs_request_cancelled(pdu)) {
        return;
    }
    v9fs_co_run_in_worker(
        {
            s->ops->seekdir(&s->ctx, &fidp->fs, offset);
        });
}

void coroutine_fn v9fs_co_rewinddir(V9fsPDU *pdu, V9fsFidState *fidp)
{
    V9fsState *s = pdu->s;
    if (v9fs_request_cancelled(pdu)) {
        return;
    }
    v9fs_co_run_in_worker(
        {
            s->ops->rewinddir(&s->ctx, &fidp->fs);
        });
}

int coroutine_fn v9fs_co_mkdir(V9fsPDU *pdu, V9fsFidState *fidp,
                               V9fsString *name, mode_t mode, uid_t uid,
                               gid_t gid, struct stat *stbuf)
{
    int err;
    FsCred cred;
    V9fsPath path;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    cred_init(&cred);
    cred.fc_mode = mode;
    cred.fc_uid = uid;
    cred.fc_gid = gid;
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            err = s->ops->mkdir(&s->ctx, &fidp->path, name->data,  &cred);
            if (err < 0) {
                err = -errno;
            } else {
                v9fs_path_init(&path);
                err = v9fs_name_to_path(s, &fidp->path, name->data, &path);
                if (!err) {
                    err = s->ops->lstat(&s->ctx, &path, stbuf);
                    if (err < 0) {
                        err = -errno;
                    }
                }
                v9fs_path_free(&path);
            }
        });
    v9fs_path_unlock(s);
    return err;
}

int coroutine_fn v9fs_co_opendir(V9fsPDU *pdu, V9fsFidState *fidp)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            err = s->ops->opendir(&s->ctx, &fidp->path, &fidp->fs);
            if (err < 0) {
                err = -errno;
            } else {
                err = 0;
            }
        });
    v9fs_path_unlock(s);
    if (!err) {
        total_open_fd++;
        if (total_open_fd > open_fd_hw) {
            v9fs_reclaim_fd(pdu);
        }
    }
    return err;
}

int coroutine_fn v9fs_co_closedir(V9fsPDU *pdu, V9fsFidOpenState *fs)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_co_run_in_worker(
        {
            err = s->ops->closedir(&s->ctx, fs);
            if (err < 0) {
                err = -errno;
            }
        });
    if (!err) {
        total_open_fd--;
    }
    return err;
}
