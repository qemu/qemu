/*
 * 9p backend
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Harsh Prateek Bora <harsh@linux.vnet.ibm.com>
 *  Venkateswararao Jujjuri(JV) <jvrao@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_9P_COTH_H
#define QEMU_9P_COTH_H

#include "qemu/thread.h"
#include "qemu/coroutine-core.h"
#include "9p.h"

/*
 * we want to use bottom half because we want to make sure the below
 * sequence of events.
 *
 *   1. Yield the coroutine in the QEMU thread.
 *   2. Submit the coroutine to a worker thread.
 *   3. Enter the coroutine in the worker thread.
 * we cannot swap step 1 and 2, because that would imply worker thread
 * can enter coroutine while step1 is still running
 *
 * PERFORMANCE CONSIDERATIONS: As a rule of thumb, keep in mind
 * that hopping between threads adds @b latency! So when handling a
 * 9pfs request, avoid calling v9fs_co_run_in_worker() too often, because
 * this might otherwise sum up to a significant, huge overall latency for
 * providing the response for just a single request. For that reason it
 * is highly recommended to fetch all data from fs driver with a single
 * fs driver request on a background I/O thread (bottom half) in one rush
 * first and then eventually assembling the final response from that data
 * on main I/O thread (top half).
 */
#define v9fs_co_run_in_worker(code_block)                               \
    do {                                                                \
        QEMUBH *co_bh;                                                  \
        co_bh = qemu_bh_new(co_run_in_worker_bh,                        \
                            qemu_coroutine_self());                     \
        qemu_bh_schedule(co_bh);                                        \
        /*                                                              \
         * yield in qemu thread and re-enter back                       \
         * in worker thread                                             \
         */                                                             \
        qemu_coroutine_yield();                                         \
        qemu_bh_delete(co_bh);                                          \
        do {                                                            \
            code_block;                                                 \
        } while (0);                                                    \
        /* re-enter back to qemu thread */                              \
        qemu_coroutine_yield();                                         \
    } while (0)

void co_run_in_worker_bh(void *);
int coroutine_fn v9fs_co_readlink(V9fsPDU *, V9fsPath *, V9fsString *);
int coroutine_fn v9fs_co_readdir(V9fsPDU *, V9fsFidState *, struct dirent **);
int coroutine_fn v9fs_co_readdir_many(V9fsPDU *, V9fsFidState *,
                                      struct V9fsDirEnt **, off_t, int32_t,
                                      bool);
off_t coroutine_fn v9fs_co_telldir(V9fsPDU *, V9fsFidState *);
void coroutine_fn v9fs_co_seekdir(V9fsPDU *, V9fsFidState *, off_t);
void coroutine_fn v9fs_co_rewinddir(V9fsPDU *, V9fsFidState *);
int coroutine_fn v9fs_co_statfs(V9fsPDU *, V9fsPath *, struct statfs *);
int coroutine_fn v9fs_co_lstat(V9fsPDU *, V9fsPath *, struct stat *);
int coroutine_fn v9fs_co_chmod(V9fsPDU *, V9fsPath *, mode_t);
int coroutine_fn v9fs_co_utimensat(V9fsPDU *, V9fsPath *, struct timespec [2]);
int coroutine_fn v9fs_co_chown(V9fsPDU *, V9fsPath *, uid_t, gid_t);
int coroutine_fn v9fs_co_truncate(V9fsPDU *, V9fsPath *, off_t);
int coroutine_fn v9fs_co_llistxattr(V9fsPDU *, V9fsPath *, void *, size_t);
int coroutine_fn v9fs_co_lgetxattr(V9fsPDU *, V9fsPath *,
                                   V9fsString *, void *, size_t);
int coroutine_fn v9fs_co_mknod(V9fsPDU *, V9fsFidState *, V9fsString *, uid_t,
                               gid_t, dev_t, mode_t, struct stat *);
int coroutine_fn v9fs_co_mkdir(V9fsPDU *, V9fsFidState *, V9fsString *,
                               mode_t, uid_t, gid_t, struct stat *);
int coroutine_fn v9fs_co_remove(V9fsPDU *, V9fsPath *);
int coroutine_fn v9fs_co_rename(V9fsPDU *, V9fsPath *, V9fsPath *);
int coroutine_fn v9fs_co_unlinkat(V9fsPDU *, V9fsPath *, V9fsString *,
                                  int flags);
int coroutine_fn v9fs_co_renameat(V9fsPDU *, V9fsPath *, V9fsString *,
                                  V9fsPath *, V9fsString *);
int coroutine_fn v9fs_co_fstat(V9fsPDU *, V9fsFidState *, struct stat *);
int coroutine_fn v9fs_co_opendir(V9fsPDU *, V9fsFidState *);
int coroutine_fn v9fs_co_open(V9fsPDU *, V9fsFidState *, int);
int coroutine_fn v9fs_co_open2(V9fsPDU *, V9fsFidState *, V9fsString *,
                               gid_t, int, int, struct stat *);
int coroutine_fn v9fs_co_lsetxattr(V9fsPDU *, V9fsPath *, V9fsString *,
                                   void *, size_t, int);
int coroutine_fn v9fs_co_lremovexattr(V9fsPDU *, V9fsPath *, V9fsString *);
int coroutine_fn v9fs_co_closedir(V9fsPDU *, V9fsFidOpenState *);
int coroutine_fn v9fs_co_close(V9fsPDU *, V9fsFidOpenState *);
int coroutine_fn v9fs_co_fsync(V9fsPDU *, V9fsFidState *, int);
int coroutine_fn v9fs_co_symlink(V9fsPDU *, V9fsFidState *, V9fsString *,
                                 const char *, gid_t, struct stat *);
int coroutine_fn v9fs_co_link(V9fsPDU *, V9fsFidState *,
                              V9fsFidState *, V9fsString *);
int coroutine_fn v9fs_co_pwritev(V9fsPDU *, V9fsFidState *,
                                 struct iovec *, int, int64_t);
int coroutine_fn v9fs_co_preadv(V9fsPDU *, V9fsFidState *,
                                struct iovec *, int, int64_t);
int coroutine_fn v9fs_co_name_to_path(V9fsPDU *, V9fsPath *,
                                      const char *, V9fsPath *);
int coroutine_fn v9fs_co_st_gen(V9fsPDU *pdu, V9fsPath *path, mode_t,
                                V9fsStatDotl *v9stat);

#endif
