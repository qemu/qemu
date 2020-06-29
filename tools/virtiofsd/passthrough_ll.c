/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * This program can be distributed under the terms of the GNU GPLv2.
 * See the file COPYING.
 */

/*
 *
 * This file system mirrors the existing file system hierarchy of the
 * system, starting at the root file system. This is implemented by
 * just "passing through" all requests to the corresponding user-space
 * libc functions. In contrast to passthrough.c and passthrough_fh.c,
 * this implementation uses the low-level API. Its performance should
 * be the least bad among the three, but many operations are not
 * implemented. In particular, it is not possible to remove files (or
 * directories) because the code necessary to defer actual removal
 * until the file is not opened anymore would make the example much
 * more complicated.
 *
 * When writeback caching is enabled (-o writeback mount option), it
 * is only possible to write to files for which the mounting user has
 * read permissions. This is because the writeback cache requires the
 * kernel to be able to issue read requests for all files (which the
 * passthrough filesystem cannot satisfy if it can't read the file in
 * the underlying filesystem).
 *
 * Compile with:
 *
 *     gcc -Wall passthrough_ll.c `pkg-config fuse3 --cflags --libs` -o
 * passthrough_ll
 *
 * ## Source code ##
 * \include passthrough_ll.c
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "fuse_virtio.h"
#include "fuse_log.h"
#include "fuse_lowlevel.h"
#include <assert.h>
#include <cap-ng.h>
#include <dirent.h>
#include <errno.h>
#include <glib.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <syslog.h>
#include <unistd.h>

#include "passthrough_helpers.h"
#include "seccomp.h"

/* Keep track of inode posix locks for each owner. */
struct lo_inode_plock {
    uint64_t lock_owner;
    int fd; /* fd for OFD locks */
};

struct lo_map_elem {
    union {
        struct lo_inode *inode;
        struct lo_dirp *dirp;
        int fd;
        ssize_t freelist;
    };
    bool in_use;
};

/* Maps FUSE fh or ino values to internal objects */
struct lo_map {
    struct lo_map_elem *elems;
    size_t nelems;
    ssize_t freelist;
};

struct lo_key {
    ino_t ino;
    dev_t dev;
};

struct lo_inode {
    int fd;

    /*
     * Atomic reference count for this object.  The nlookup field holds a
     * reference and release it when nlookup reaches 0.
     */
    gint refcount;

    struct lo_key key;

    /*
     * This counter keeps the inode alive during the FUSE session.
     * Incremented when the FUSE inode number is sent in a reply
     * (FUSE_LOOKUP, FUSE_READDIRPLUS, etc).  Decremented when an inode is
     * released by requests like FUSE_FORGET, FUSE_RMDIR, FUSE_RENAME, etc.
     *
     * Note that this value is untrusted because the client can manipulate
     * it arbitrarily using FUSE_FORGET requests.
     *
     * Protected by lo->mutex.
     */
    uint64_t nlookup;

    fuse_ino_t fuse_ino;
    pthread_mutex_t plock_mutex;
    GHashTable *posix_locks; /* protected by lo_inode->plock_mutex */

    mode_t filetype;
};

struct lo_cred {
    uid_t euid;
    gid_t egid;
};

enum {
    CACHE_NONE,
    CACHE_AUTO,
    CACHE_ALWAYS,
};

struct lo_data {
    pthread_mutex_t mutex;
    int debug;
    int writeback;
    int flock;
    int posix_lock;
    int xattr;
    char *source;
    char *modcaps;
    double timeout;
    int cache;
    int timeout_set;
    int readdirplus_set;
    int readdirplus_clear;
    struct lo_inode root;
    GHashTable *inodes; /* protected by lo->mutex */
    struct lo_map ino_map; /* protected by lo->mutex */
    struct lo_map dirp_map; /* protected by lo->mutex */
    struct lo_map fd_map; /* protected by lo->mutex */

    /* An O_PATH file descriptor to /proc/self/fd/ */
    int proc_self_fd;
};

static const struct fuse_opt lo_opts[] = {
    { "writeback", offsetof(struct lo_data, writeback), 1 },
    { "no_writeback", offsetof(struct lo_data, writeback), 0 },
    { "source=%s", offsetof(struct lo_data, source), 0 },
    { "flock", offsetof(struct lo_data, flock), 1 },
    { "no_flock", offsetof(struct lo_data, flock), 0 },
    { "posix_lock", offsetof(struct lo_data, posix_lock), 1 },
    { "no_posix_lock", offsetof(struct lo_data, posix_lock), 0 },
    { "xattr", offsetof(struct lo_data, xattr), 1 },
    { "no_xattr", offsetof(struct lo_data, xattr), 0 },
    { "modcaps=%s", offsetof(struct lo_data, modcaps), 0 },
    { "timeout=%lf", offsetof(struct lo_data, timeout), 0 },
    { "timeout=", offsetof(struct lo_data, timeout_set), 1 },
    { "cache=none", offsetof(struct lo_data, cache), CACHE_NONE },
    { "cache=auto", offsetof(struct lo_data, cache), CACHE_AUTO },
    { "cache=always", offsetof(struct lo_data, cache), CACHE_ALWAYS },
    { "readdirplus", offsetof(struct lo_data, readdirplus_set), 1 },
    { "no_readdirplus", offsetof(struct lo_data, readdirplus_clear), 1 },
    FUSE_OPT_END
};
static bool use_syslog = false;
static int current_log_level;
static void unref_inode_lolocked(struct lo_data *lo, struct lo_inode *inode,
                                 uint64_t n);

static struct {
    pthread_mutex_t mutex;
    void *saved;
} cap;
/* That we loaded cap-ng in the current thread from the saved */
static __thread bool cap_loaded = 0;

static struct lo_inode *lo_find(struct lo_data *lo, struct stat *st);

static int is_dot_or_dotdot(const char *name)
{
    return name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

/* Is `path` a single path component that is not "." or ".."? */
static int is_safe_path_component(const char *path)
{
    if (strchr(path, '/')) {
        return 0;
    }

    return !is_dot_or_dotdot(path);
}

static struct lo_data *lo_data(fuse_req_t req)
{
    return (struct lo_data *)fuse_req_userdata(req);
}

/*
 * Load capng's state from our saved state if the current thread
 * hadn't previously been loaded.
 * returns 0 on success
 */
static int load_capng(void)
{
    if (!cap_loaded) {
        pthread_mutex_lock(&cap.mutex);
        capng_restore_state(&cap.saved);
        /*
         * restore_state free's the saved copy
         * so make another.
         */
        cap.saved = capng_save_state();
        if (!cap.saved) {
            pthread_mutex_unlock(&cap.mutex);
            fuse_log(FUSE_LOG_ERR, "capng_save_state (thread)\n");
            return -EINVAL;
        }
        pthread_mutex_unlock(&cap.mutex);

        /*
         * We want to use the loaded state for our pid,
         * not the original
         */
        capng_setpid(syscall(SYS_gettid));
        cap_loaded = true;
    }
    return 0;
}

/*
 * Helpers for dropping and regaining effective capabilities. Returns 0
 * on success, error otherwise
 */
static int drop_effective_cap(const char *cap_name, bool *cap_dropped)
{
    int cap, ret;

    cap = capng_name_to_capability(cap_name);
    if (cap < 0) {
        ret = errno;
        fuse_log(FUSE_LOG_ERR, "capng_name_to_capability(%s) failed:%s\n",
                 cap_name, strerror(errno));
        goto out;
    }

    if (load_capng()) {
        ret = errno;
        fuse_log(FUSE_LOG_ERR, "load_capng() failed\n");
        goto out;
    }

    /* We dont have this capability in effective set already. */
    if (!capng_have_capability(CAPNG_EFFECTIVE, cap)) {
        ret = 0;
        goto out;
    }

    if (capng_update(CAPNG_DROP, CAPNG_EFFECTIVE, cap)) {
        ret = errno;
        fuse_log(FUSE_LOG_ERR, "capng_update(DROP,) failed\n");
        goto out;
    }

    if (capng_apply(CAPNG_SELECT_CAPS)) {
        ret = errno;
        fuse_log(FUSE_LOG_ERR, "drop:capng_apply() failed\n");
        goto out;
    }

    ret = 0;
    if (cap_dropped) {
        *cap_dropped = true;
    }

out:
    return ret;
}

static int gain_effective_cap(const char *cap_name)
{
    int cap;
    int ret = 0;

    cap = capng_name_to_capability(cap_name);
    if (cap < 0) {
        ret = errno;
        fuse_log(FUSE_LOG_ERR, "capng_name_to_capability(%s) failed:%s\n",
                 cap_name, strerror(errno));
        goto out;
    }

    if (load_capng()) {
        ret = errno;
        fuse_log(FUSE_LOG_ERR, "load_capng() failed\n");
        goto out;
    }

    if (capng_update(CAPNG_ADD, CAPNG_EFFECTIVE, cap)) {
        ret = errno;
        fuse_log(FUSE_LOG_ERR, "capng_update(ADD,) failed\n");
        goto out;
    }

    if (capng_apply(CAPNG_SELECT_CAPS)) {
        ret = errno;
        fuse_log(FUSE_LOG_ERR, "gain:capng_apply() failed\n");
        goto out;
    }
    ret = 0;

out:
    return ret;
}

static void lo_map_init(struct lo_map *map)
{
    map->elems = NULL;
    map->nelems = 0;
    map->freelist = -1;
}

static void lo_map_destroy(struct lo_map *map)
{
    free(map->elems);
}

static int lo_map_grow(struct lo_map *map, size_t new_nelems)
{
    struct lo_map_elem *new_elems;
    size_t i;

    if (new_nelems <= map->nelems) {
        return 1;
    }

    new_elems = realloc(map->elems, sizeof(map->elems[0]) * new_nelems);
    if (!new_elems) {
        return 0;
    }

    for (i = map->nelems; i < new_nelems; i++) {
        new_elems[i].freelist = i + 1;
        new_elems[i].in_use = false;
    }
    new_elems[new_nelems - 1].freelist = -1;

    map->elems = new_elems;
    map->freelist = map->nelems;
    map->nelems = new_nelems;
    return 1;
}

static struct lo_map_elem *lo_map_alloc_elem(struct lo_map *map)
{
    struct lo_map_elem *elem;

    if (map->freelist == -1 && !lo_map_grow(map, map->nelems + 256)) {
        return NULL;
    }

    elem = &map->elems[map->freelist];
    map->freelist = elem->freelist;

    elem->in_use = true;

    return elem;
}

static struct lo_map_elem *lo_map_reserve(struct lo_map *map, size_t key)
{
    ssize_t *prev;

    if (!lo_map_grow(map, key + 1)) {
        return NULL;
    }

    for (prev = &map->freelist; *prev != -1;
         prev = &map->elems[*prev].freelist) {
        if (*prev == key) {
            struct lo_map_elem *elem = &map->elems[key];

            *prev = elem->freelist;
            elem->in_use = true;
            return elem;
        }
    }
    return NULL;
}

static struct lo_map_elem *lo_map_get(struct lo_map *map, size_t key)
{
    if (key >= map->nelems) {
        return NULL;
    }
    if (!map->elems[key].in_use) {
        return NULL;
    }
    return &map->elems[key];
}

static void lo_map_remove(struct lo_map *map, size_t key)
{
    struct lo_map_elem *elem;

    if (key >= map->nelems) {
        return;
    }

    elem = &map->elems[key];
    if (!elem->in_use) {
        return;
    }

    elem->in_use = false;

    elem->freelist = map->freelist;
    map->freelist = key;
}

/* Assumes lo->mutex is held */
static ssize_t lo_add_fd_mapping(fuse_req_t req, int fd)
{
    struct lo_map_elem *elem;

    elem = lo_map_alloc_elem(&lo_data(req)->fd_map);
    if (!elem) {
        return -1;
    }

    elem->fd = fd;
    return elem - lo_data(req)->fd_map.elems;
}

/* Assumes lo->mutex is held */
static ssize_t lo_add_dirp_mapping(fuse_req_t req, struct lo_dirp *dirp)
{
    struct lo_map_elem *elem;

    elem = lo_map_alloc_elem(&lo_data(req)->dirp_map);
    if (!elem) {
        return -1;
    }

    elem->dirp = dirp;
    return elem - lo_data(req)->dirp_map.elems;
}

/* Assumes lo->mutex is held */
static ssize_t lo_add_inode_mapping(fuse_req_t req, struct lo_inode *inode)
{
    struct lo_map_elem *elem;

    elem = lo_map_alloc_elem(&lo_data(req)->ino_map);
    if (!elem) {
        return -1;
    }

    elem->inode = inode;
    return elem - lo_data(req)->ino_map.elems;
}

static void lo_inode_put(struct lo_data *lo, struct lo_inode **inodep)
{
    struct lo_inode *inode = *inodep;

    if (!inode) {
        return;
    }

    *inodep = NULL;

    if (g_atomic_int_dec_and_test(&inode->refcount)) {
        close(inode->fd);
        free(inode);
    }
}

/* Caller must release refcount using lo_inode_put() */
static struct lo_inode *lo_inode(fuse_req_t req, fuse_ino_t ino)
{
    struct lo_data *lo = lo_data(req);
    struct lo_map_elem *elem;

    pthread_mutex_lock(&lo->mutex);
    elem = lo_map_get(&lo->ino_map, ino);
    if (elem) {
        g_atomic_int_inc(&elem->inode->refcount);
    }
    pthread_mutex_unlock(&lo->mutex);

    if (!elem) {
        return NULL;
    }

    return elem->inode;
}

/*
 * TODO Remove this helper and force callers to hold an inode refcount until
 * they are done with the fd.  This will be done in a later patch to make
 * review easier.
 */
static int lo_fd(fuse_req_t req, fuse_ino_t ino)
{
    struct lo_inode *inode = lo_inode(req, ino);
    int fd;

    if (!inode) {
        return -1;
    }

    fd = inode->fd;
    lo_inode_put(lo_data(req), &inode);
    return fd;
}

static void lo_init(void *userdata, struct fuse_conn_info *conn)
{
    struct lo_data *lo = (struct lo_data *)userdata;

    if (conn->capable & FUSE_CAP_EXPORT_SUPPORT) {
        conn->want |= FUSE_CAP_EXPORT_SUPPORT;
    }

    if (lo->writeback && conn->capable & FUSE_CAP_WRITEBACK_CACHE) {
        fuse_log(FUSE_LOG_DEBUG, "lo_init: activating writeback\n");
        conn->want |= FUSE_CAP_WRITEBACK_CACHE;
    }
    if (conn->capable & FUSE_CAP_FLOCK_LOCKS) {
        if (lo->flock) {
            fuse_log(FUSE_LOG_DEBUG, "lo_init: activating flock locks\n");
            conn->want |= FUSE_CAP_FLOCK_LOCKS;
        } else {
            fuse_log(FUSE_LOG_DEBUG, "lo_init: disabling flock locks\n");
            conn->want &= ~FUSE_CAP_FLOCK_LOCKS;
        }
    }

    if (conn->capable & FUSE_CAP_POSIX_LOCKS) {
        if (lo->posix_lock) {
            fuse_log(FUSE_LOG_DEBUG, "lo_init: activating posix locks\n");
            conn->want |= FUSE_CAP_POSIX_LOCKS;
        } else {
            fuse_log(FUSE_LOG_DEBUG, "lo_init: disabling posix locks\n");
            conn->want &= ~FUSE_CAP_POSIX_LOCKS;
        }
    }

    if ((lo->cache == CACHE_NONE && !lo->readdirplus_set) ||
        lo->readdirplus_clear) {
        fuse_log(FUSE_LOG_DEBUG, "lo_init: disabling readdirplus\n");
        conn->want &= ~FUSE_CAP_READDIRPLUS;
    }
}

static void lo_getattr(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info *fi)
{
    int res;
    struct stat buf;
    struct lo_data *lo = lo_data(req);

    (void)fi;

    res =
        fstatat(lo_fd(req, ino), "", &buf, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    if (res == -1) {
        return (void)fuse_reply_err(req, errno);
    }

    fuse_reply_attr(req, &buf, lo->timeout);
}

static int lo_fi_fd(fuse_req_t req, struct fuse_file_info *fi)
{
    struct lo_data *lo = lo_data(req);
    struct lo_map_elem *elem;

    pthread_mutex_lock(&lo->mutex);
    elem = lo_map_get(&lo->fd_map, fi->fh);
    pthread_mutex_unlock(&lo->mutex);

    if (!elem) {
        return -1;
    }

    return elem->fd;
}

static void lo_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr,
                       int valid, struct fuse_file_info *fi)
{
    int saverr;
    char procname[64];
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;
    int ifd;
    int res;
    int fd;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    ifd = inode->fd;

    /* If fi->fh is invalid we'll report EBADF later */
    if (fi) {
        fd = lo_fi_fd(req, fi);
    }

    if (valid & FUSE_SET_ATTR_MODE) {
        if (fi) {
            res = fchmod(fd, attr->st_mode);
        } else {
            sprintf(procname, "%i", ifd);
            res = fchmodat(lo->proc_self_fd, procname, attr->st_mode, 0);
        }
        if (res == -1) {
            goto out_err;
        }
    }
    if (valid & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)) {
        uid_t uid = (valid & FUSE_SET_ATTR_UID) ? attr->st_uid : (uid_t)-1;
        gid_t gid = (valid & FUSE_SET_ATTR_GID) ? attr->st_gid : (gid_t)-1;

        res = fchownat(ifd, "", uid, gid, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
        if (res == -1) {
            goto out_err;
        }
    }
    if (valid & FUSE_SET_ATTR_SIZE) {
        int truncfd;

        if (fi) {
            truncfd = fd;
        } else {
            sprintf(procname, "%i", ifd);
            truncfd = openat(lo->proc_self_fd, procname, O_RDWR);
            if (truncfd < 0) {
                goto out_err;
            }
        }

        res = ftruncate(truncfd, attr->st_size);
        if (!fi) {
            saverr = errno;
            close(truncfd);
            errno = saverr;
        }
        if (res == -1) {
            goto out_err;
        }
    }
    if (valid & (FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME)) {
        struct timespec tv[2];

        tv[0].tv_sec = 0;
        tv[1].tv_sec = 0;
        tv[0].tv_nsec = UTIME_OMIT;
        tv[1].tv_nsec = UTIME_OMIT;

        if (valid & FUSE_SET_ATTR_ATIME_NOW) {
            tv[0].tv_nsec = UTIME_NOW;
        } else if (valid & FUSE_SET_ATTR_ATIME) {
            tv[0] = attr->st_atim;
        }

        if (valid & FUSE_SET_ATTR_MTIME_NOW) {
            tv[1].tv_nsec = UTIME_NOW;
        } else if (valid & FUSE_SET_ATTR_MTIME) {
            tv[1] = attr->st_mtim;
        }

        if (fi) {
            res = futimens(fd, tv);
        } else {
            sprintf(procname, "%i", inode->fd);
            res = utimensat(lo->proc_self_fd, procname, tv, 0);
        }
        if (res == -1) {
            goto out_err;
        }
    }
    lo_inode_put(lo, &inode);

    return lo_getattr(req, ino, fi);

out_err:
    saverr = errno;
    lo_inode_put(lo, &inode);
    fuse_reply_err(req, saverr);
}

static struct lo_inode *lo_find(struct lo_data *lo, struct stat *st)
{
    struct lo_inode *p;
    struct lo_key key = {
        .ino = st->st_ino,
        .dev = st->st_dev,
    };

    pthread_mutex_lock(&lo->mutex);
    p = g_hash_table_lookup(lo->inodes, &key);
    if (p) {
        assert(p->nlookup > 0);
        p->nlookup++;
        g_atomic_int_inc(&p->refcount);
    }
    pthread_mutex_unlock(&lo->mutex);

    return p;
}

/* value_destroy_func for posix_locks GHashTable */
static void posix_locks_value_destroy(gpointer data)
{
    struct lo_inode_plock *plock = data;

    /*
     * We had used open() for locks and had only one fd. So
     * closing this fd should release all OFD locks.
     */
    close(plock->fd);
    free(plock);
}

/*
 * Increments nlookup and caller must release refcount using
 * lo_inode_put(&parent).
 */
static int lo_do_lookup(fuse_req_t req, fuse_ino_t parent, const char *name,
                        struct fuse_entry_param *e)
{
    int newfd;
    int res;
    int saverr;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode = NULL;
    struct lo_inode *dir = lo_inode(req, parent);

    /*
     * name_to_handle_at() and open_by_handle_at() can reach here with fuse
     * mount point in guest, but we don't have its inode info in the
     * ino_map.
     */
    if (!dir) {
        return ENOENT;
    }

    memset(e, 0, sizeof(*e));
    e->attr_timeout = lo->timeout;
    e->entry_timeout = lo->timeout;

    /* Do not allow escaping root directory */
    if (dir == &lo->root && strcmp(name, "..") == 0) {
        name = ".";
    }

    newfd = openat(dir->fd, name, O_PATH | O_NOFOLLOW);
    if (newfd == -1) {
        goto out_err;
    }

    res = fstatat(newfd, "", &e->attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    if (res == -1) {
        goto out_err;
    }

    inode = lo_find(lo, &e->attr);
    if (inode) {
        close(newfd);
    } else {
        inode = calloc(1, sizeof(struct lo_inode));
        if (!inode) {
            goto out_err;
        }

        /* cache only filetype */
        inode->filetype = (e->attr.st_mode & S_IFMT);

        /*
         * One for the caller and one for nlookup (released in
         * unref_inode_lolocked())
         */
        g_atomic_int_set(&inode->refcount, 2);

        inode->nlookup = 1;
        inode->fd = newfd;
        inode->key.ino = e->attr.st_ino;
        inode->key.dev = e->attr.st_dev;
        pthread_mutex_init(&inode->plock_mutex, NULL);
        inode->posix_locks = g_hash_table_new_full(
            g_direct_hash, g_direct_equal, NULL, posix_locks_value_destroy);

        pthread_mutex_lock(&lo->mutex);
        inode->fuse_ino = lo_add_inode_mapping(req, inode);
        g_hash_table_insert(lo->inodes, &inode->key, inode);
        pthread_mutex_unlock(&lo->mutex);
    }
    e->ino = inode->fuse_ino;
    lo_inode_put(lo, &inode);
    lo_inode_put(lo, &dir);

    fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n", (unsigned long long)parent,
             name, (unsigned long long)e->ino);

    return 0;

out_err:
    saverr = errno;
    if (newfd != -1) {
        close(newfd);
    }
    lo_inode_put(lo, &inode);
    lo_inode_put(lo, &dir);
    return saverr;
}

static void lo_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    struct fuse_entry_param e;
    int err;

    fuse_log(FUSE_LOG_DEBUG, "lo_lookup(parent=%" PRIu64 ", name=%s)\n", parent,
             name);

    /*
     * Don't use is_safe_path_component(), allow "." and ".." for NFS export
     * support.
     */
    if (strchr(name, '/')) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    err = lo_do_lookup(req, parent, name, &e);
    if (err) {
        fuse_reply_err(req, err);
    } else {
        fuse_reply_entry(req, &e);
    }
}

/*
 * On some archs, setres*id is limited to 2^16 but they
 * provide setres*id32 variants that allow 2^32.
 * Others just let setres*id do 2^32 anyway.
 */
#ifdef SYS_setresgid32
#define OURSYS_setresgid SYS_setresgid32
#else
#define OURSYS_setresgid SYS_setresgid
#endif

#ifdef SYS_setresuid32
#define OURSYS_setresuid SYS_setresuid32
#else
#define OURSYS_setresuid SYS_setresuid
#endif

/*
 * Change to uid/gid of caller so that file is created with
 * ownership of caller.
 * TODO: What about selinux context?
 */
static int lo_change_cred(fuse_req_t req, struct lo_cred *old)
{
    int res;

    old->euid = geteuid();
    old->egid = getegid();

    res = syscall(OURSYS_setresgid, -1, fuse_req_ctx(req)->gid, -1);
    if (res == -1) {
        return errno;
    }

    res = syscall(OURSYS_setresuid, -1, fuse_req_ctx(req)->uid, -1);
    if (res == -1) {
        int errno_save = errno;

        syscall(OURSYS_setresgid, -1, old->egid, -1);
        return errno_save;
    }

    return 0;
}

/* Regain Privileges */
static void lo_restore_cred(struct lo_cred *old)
{
    int res;

    res = syscall(OURSYS_setresuid, -1, old->euid, -1);
    if (res == -1) {
        fuse_log(FUSE_LOG_ERR, "seteuid(%u): %m\n", old->euid);
        exit(1);
    }

    res = syscall(OURSYS_setresgid, -1, old->egid, -1);
    if (res == -1) {
        fuse_log(FUSE_LOG_ERR, "setegid(%u): %m\n", old->egid);
        exit(1);
    }
}

static void lo_mknod_symlink(fuse_req_t req, fuse_ino_t parent,
                             const char *name, mode_t mode, dev_t rdev,
                             const char *link)
{
    int res;
    int saverr;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *dir;
    struct fuse_entry_param e;
    struct lo_cred old = {};

    if (!is_safe_path_component(name)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    dir = lo_inode(req, parent);
    if (!dir) {
        fuse_reply_err(req, EBADF);
        return;
    }

    saverr = lo_change_cred(req, &old);
    if (saverr) {
        goto out;
    }

    res = mknod_wrapper(dir->fd, name, link, mode, rdev);

    saverr = errno;

    lo_restore_cred(&old);

    if (res == -1) {
        goto out;
    }

    saverr = lo_do_lookup(req, parent, name, &e);
    if (saverr) {
        goto out;
    }

    fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n", (unsigned long long)parent,
             name, (unsigned long long)e.ino);

    fuse_reply_entry(req, &e);
    lo_inode_put(lo, &dir);
    return;

out:
    lo_inode_put(lo, &dir);
    fuse_reply_err(req, saverr);
}

static void lo_mknod(fuse_req_t req, fuse_ino_t parent, const char *name,
                     mode_t mode, dev_t rdev)
{
    lo_mknod_symlink(req, parent, name, mode, rdev, NULL);
}

static void lo_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
                     mode_t mode)
{
    lo_mknod_symlink(req, parent, name, S_IFDIR | mode, 0, NULL);
}

static void lo_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,
                       const char *name)
{
    lo_mknod_symlink(req, parent, name, S_IFLNK, 0, link);
}

static void lo_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t parent,
                    const char *name)
{
    int res;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *parent_inode;
    struct lo_inode *inode;
    struct fuse_entry_param e;
    char procname[64];
    int saverr;

    if (!is_safe_path_component(name)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    parent_inode = lo_inode(req, parent);
    inode = lo_inode(req, ino);
    if (!parent_inode || !inode) {
        errno = EBADF;
        goto out_err;
    }

    memset(&e, 0, sizeof(struct fuse_entry_param));
    e.attr_timeout = lo->timeout;
    e.entry_timeout = lo->timeout;

    sprintf(procname, "%i", inode->fd);
    res = linkat(lo->proc_self_fd, procname, parent_inode->fd, name,
                 AT_SYMLINK_FOLLOW);
    if (res == -1) {
        goto out_err;
    }

    res = fstatat(inode->fd, "", &e.attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    if (res == -1) {
        goto out_err;
    }

    pthread_mutex_lock(&lo->mutex);
    inode->nlookup++;
    pthread_mutex_unlock(&lo->mutex);
    e.ino = inode->fuse_ino;

    fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n", (unsigned long long)parent,
             name, (unsigned long long)e.ino);

    fuse_reply_entry(req, &e);
    lo_inode_put(lo, &parent_inode);
    lo_inode_put(lo, &inode);
    return;

out_err:
    saverr = errno;
    lo_inode_put(lo, &parent_inode);
    lo_inode_put(lo, &inode);
    fuse_reply_err(req, saverr);
}

/* Increments nlookup and caller must release refcount using lo_inode_put() */
static struct lo_inode *lookup_name(fuse_req_t req, fuse_ino_t parent,
                                    const char *name)
{
    int res;
    struct stat attr;

    res = fstatat(lo_fd(req, parent), name, &attr,
                  AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    if (res == -1) {
        return NULL;
    }

    return lo_find(lo_data(req), &attr);
}

static void lo_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int res;
    struct lo_inode *inode;
    struct lo_data *lo = lo_data(req);

    if (!is_safe_path_component(name)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    inode = lookup_name(req, parent, name);
    if (!inode) {
        fuse_reply_err(req, EIO);
        return;
    }

    res = unlinkat(lo_fd(req, parent), name, AT_REMOVEDIR);

    fuse_reply_err(req, res == -1 ? errno : 0);
    unref_inode_lolocked(lo, inode, 1);
    lo_inode_put(lo, &inode);
}

static void lo_rename(fuse_req_t req, fuse_ino_t parent, const char *name,
                      fuse_ino_t newparent, const char *newname,
                      unsigned int flags)
{
    int res;
    struct lo_inode *parent_inode;
    struct lo_inode *newparent_inode;
    struct lo_inode *oldinode = NULL;
    struct lo_inode *newinode = NULL;
    struct lo_data *lo = lo_data(req);

    if (!is_safe_path_component(name) || !is_safe_path_component(newname)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    parent_inode = lo_inode(req, parent);
    newparent_inode = lo_inode(req, newparent);
    if (!parent_inode || !newparent_inode) {
        fuse_reply_err(req, EBADF);
        goto out;
    }

    oldinode = lookup_name(req, parent, name);
    newinode = lookup_name(req, newparent, newname);

    if (!oldinode) {
        fuse_reply_err(req, EIO);
        goto out;
    }

    if (flags) {
#ifndef SYS_renameat2
        fuse_reply_err(req, EINVAL);
#else
        res = syscall(SYS_renameat2, parent_inode->fd, name,
                        newparent_inode->fd, newname, flags);
        if (res == -1 && errno == ENOSYS) {
            fuse_reply_err(req, EINVAL);
        } else {
            fuse_reply_err(req, res == -1 ? errno : 0);
        }
#endif
        goto out;
    }

    res = renameat(parent_inode->fd, name, newparent_inode->fd, newname);

    fuse_reply_err(req, res == -1 ? errno : 0);
out:
    unref_inode_lolocked(lo, oldinode, 1);
    unref_inode_lolocked(lo, newinode, 1);
    lo_inode_put(lo, &oldinode);
    lo_inode_put(lo, &newinode);
    lo_inode_put(lo, &parent_inode);
    lo_inode_put(lo, &newparent_inode);
}

static void lo_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int res;
    struct lo_inode *inode;
    struct lo_data *lo = lo_data(req);

    if (!is_safe_path_component(name)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    inode = lookup_name(req, parent, name);
    if (!inode) {
        fuse_reply_err(req, EIO);
        return;
    }

    res = unlinkat(lo_fd(req, parent), name, 0);

    fuse_reply_err(req, res == -1 ? errno : 0);
    unref_inode_lolocked(lo, inode, 1);
    lo_inode_put(lo, &inode);
}

/* To be called with lo->mutex held */
static void unref_inode(struct lo_data *lo, struct lo_inode *inode, uint64_t n)
{
    if (!inode) {
        return;
    }

    assert(inode->nlookup >= n);
    inode->nlookup -= n;
    if (!inode->nlookup) {
        lo_map_remove(&lo->ino_map, inode->fuse_ino);
        g_hash_table_remove(lo->inodes, &inode->key);
        if (g_hash_table_size(inode->posix_locks)) {
            fuse_log(FUSE_LOG_WARNING, "Hash table is not empty\n");
        }
        g_hash_table_destroy(inode->posix_locks);
        pthread_mutex_destroy(&inode->plock_mutex);

        /* Drop our refcount from lo_do_lookup() */
        lo_inode_put(lo, &inode);
    }
}

static void unref_inode_lolocked(struct lo_data *lo, struct lo_inode *inode,
                                 uint64_t n)
{
    if (!inode) {
        return;
    }

    pthread_mutex_lock(&lo->mutex);
    unref_inode(lo, inode, n);
    pthread_mutex_unlock(&lo->mutex);
}

static void lo_forget_one(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup)
{
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;

    inode = lo_inode(req, ino);
    if (!inode) {
        return;
    }

    fuse_log(FUSE_LOG_DEBUG, "  forget %lli %lli -%lli\n",
             (unsigned long long)ino, (unsigned long long)inode->nlookup,
             (unsigned long long)nlookup);

    unref_inode_lolocked(lo, inode, nlookup);
    lo_inode_put(lo, &inode);
}

static void lo_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup)
{
    lo_forget_one(req, ino, nlookup);
    fuse_reply_none(req);
}

static void lo_forget_multi(fuse_req_t req, size_t count,
                            struct fuse_forget_data *forgets)
{
    int i;

    for (i = 0; i < count; i++) {
        lo_forget_one(req, forgets[i].ino, forgets[i].nlookup);
    }
    fuse_reply_none(req);
}

static void lo_readlink(fuse_req_t req, fuse_ino_t ino)
{
    char buf[PATH_MAX + 1];
    int res;

    res = readlinkat(lo_fd(req, ino), "", buf, sizeof(buf));
    if (res == -1) {
        return (void)fuse_reply_err(req, errno);
    }

    if (res == sizeof(buf)) {
        return (void)fuse_reply_err(req, ENAMETOOLONG);
    }

    buf[res] = '\0';

    fuse_reply_readlink(req, buf);
}

struct lo_dirp {
    gint refcount;
    DIR *dp;
    struct dirent *entry;
    off_t offset;
};

static void lo_dirp_put(struct lo_dirp **dp)
{
    struct lo_dirp *d = *dp;

    if (!d) {
        return;
    }
    *dp = NULL;

    if (g_atomic_int_dec_and_test(&d->refcount)) {
        closedir(d->dp);
        free(d);
    }
}

/* Call lo_dirp_put() on the return value when no longer needed */
static struct lo_dirp *lo_dirp(fuse_req_t req, struct fuse_file_info *fi)
{
    struct lo_data *lo = lo_data(req);
    struct lo_map_elem *elem;

    pthread_mutex_lock(&lo->mutex);
    elem = lo_map_get(&lo->dirp_map, fi->fh);
    if (elem) {
        g_atomic_int_inc(&elem->dirp->refcount);
    }
    pthread_mutex_unlock(&lo->mutex);
    if (!elem) {
        return NULL;
    }

    return elem->dirp;
}

static void lo_opendir(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info *fi)
{
    int error = ENOMEM;
    struct lo_data *lo = lo_data(req);
    struct lo_dirp *d;
    int fd;
    ssize_t fh;

    d = calloc(1, sizeof(struct lo_dirp));
    if (d == NULL) {
        goto out_err;
    }

    fd = openat(lo_fd(req, ino), ".", O_RDONLY);
    if (fd == -1) {
        goto out_errno;
    }

    d->dp = fdopendir(fd);
    if (d->dp == NULL) {
        goto out_errno;
    }

    d->offset = 0;
    d->entry = NULL;

    g_atomic_int_set(&d->refcount, 1); /* paired with lo_releasedir() */
    pthread_mutex_lock(&lo->mutex);
    fh = lo_add_dirp_mapping(req, d);
    pthread_mutex_unlock(&lo->mutex);
    if (fh == -1) {
        goto out_err;
    }

    fi->fh = fh;
    if (lo->cache == CACHE_ALWAYS) {
        fi->cache_readdir = 1;
    }
    fuse_reply_open(req, fi);
    return;

out_errno:
    error = errno;
out_err:
    if (d) {
        if (d->dp) {
            closedir(d->dp);
        } else if (fd != -1) {
            close(fd);
        }
        free(d);
    }
    fuse_reply_err(req, error);
}

static void lo_do_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                          off_t offset, struct fuse_file_info *fi, int plus)
{
    struct lo_data *lo = lo_data(req);
    struct lo_dirp *d = NULL;
    struct lo_inode *dinode;
    char *buf = NULL;
    char *p;
    size_t rem = size;
    int err = EBADF;

    dinode = lo_inode(req, ino);
    if (!dinode) {
        goto error;
    }

    d = lo_dirp(req, fi);
    if (!d) {
        goto error;
    }

    err = ENOMEM;
    buf = calloc(1, size);
    if (!buf) {
        goto error;
    }
    p = buf;

    if (offset != d->offset) {
        seekdir(d->dp, offset);
        d->entry = NULL;
        d->offset = offset;
    }
    while (1) {
        size_t entsize;
        off_t nextoff;
        const char *name;

        if (!d->entry) {
            errno = 0;
            d->entry = readdir(d->dp);
            if (!d->entry) {
                if (errno) { /* Error */
                    err = errno;
                    goto error;
                } else { /* End of stream */
                    break;
                }
            }
        }
        nextoff = d->entry->d_off;
        name = d->entry->d_name;

        fuse_ino_t entry_ino = 0;
        struct fuse_entry_param e = (struct fuse_entry_param){
            .attr.st_ino = d->entry->d_ino,
            .attr.st_mode = d->entry->d_type << 12,
        };

        /* Hide root's parent directory */
        if (dinode == &lo->root && strcmp(name, "..") == 0) {
            e.attr.st_ino = lo->root.key.ino;
            e.attr.st_mode = DT_DIR << 12;
        }

        if (plus) {
            if (!is_dot_or_dotdot(name)) {
                err = lo_do_lookup(req, ino, name, &e);
                if (err) {
                    goto error;
                }
                entry_ino = e.ino;
            }

            entsize = fuse_add_direntry_plus(req, p, rem, name, &e, nextoff);
        } else {
            entsize = fuse_add_direntry(req, p, rem, name, &e.attr, nextoff);
        }
        if (entsize > rem) {
            if (entry_ino != 0) {
                lo_forget_one(req, entry_ino, 1);
            }
            break;
        }

        p += entsize;
        rem -= entsize;

        d->entry = NULL;
        d->offset = nextoff;
    }

    err = 0;
error:
    lo_dirp_put(&d);
    lo_inode_put(lo, &dinode);

    /*
     * If there's an error, we can only signal it if we haven't stored
     * any entries yet - otherwise we'd end up with wrong lookup
     * counts for the entries that are already in the buffer. So we
     * return what we've collected until that point.
     */
    if (err && rem == size) {
        fuse_reply_err(req, err);
    } else {
        fuse_reply_buf(req, buf, size - rem);
    }
    free(buf);
}

static void lo_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                       off_t offset, struct fuse_file_info *fi)
{
    lo_do_readdir(req, ino, size, offset, fi, 0);
}

static void lo_readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size,
                           off_t offset, struct fuse_file_info *fi)
{
    lo_do_readdir(req, ino, size, offset, fi, 1);
}

static void lo_releasedir(fuse_req_t req, fuse_ino_t ino,
                          struct fuse_file_info *fi)
{
    struct lo_data *lo = lo_data(req);
    struct lo_map_elem *elem;
    struct lo_dirp *d;

    (void)ino;

    pthread_mutex_lock(&lo->mutex);
    elem = lo_map_get(&lo->dirp_map, fi->fh);
    if (!elem) {
        pthread_mutex_unlock(&lo->mutex);
        fuse_reply_err(req, EBADF);
        return;
    }

    d = elem->dirp;
    lo_map_remove(&lo->dirp_map, fi->fh);
    pthread_mutex_unlock(&lo->mutex);

    lo_dirp_put(&d); /* paired with lo_opendir() */

    fuse_reply_err(req, 0);
}

static void update_open_flags(int writeback, struct fuse_file_info *fi)
{
    /*
     * With writeback cache, kernel may send read requests even
     * when userspace opened write-only
     */
    if (writeback && (fi->flags & O_ACCMODE) == O_WRONLY) {
        fi->flags &= ~O_ACCMODE;
        fi->flags |= O_RDWR;
    }

    /*
     * With writeback cache, O_APPEND is handled by the kernel.
     * This breaks atomicity (since the file may change in the
     * underlying filesystem, so that the kernel's idea of the
     * end of the file isn't accurate anymore). In this example,
     * we just accept that. A more rigorous filesystem may want
     * to return an error here
     */
    if (writeback && (fi->flags & O_APPEND)) {
        fi->flags &= ~O_APPEND;
    }

    /*
     * O_DIRECT in guest should not necessarily mean bypassing page
     * cache on host as well. If somebody needs that behavior, it
     * probably should be a configuration knob in daemon.
     */
    fi->flags &= ~O_DIRECT;
}

static void lo_create(fuse_req_t req, fuse_ino_t parent, const char *name,
                      mode_t mode, struct fuse_file_info *fi)
{
    int fd;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *parent_inode;
    struct fuse_entry_param e;
    int err;
    struct lo_cred old = {};

    fuse_log(FUSE_LOG_DEBUG, "lo_create(parent=%" PRIu64 ", name=%s)\n", parent,
             name);

    if (!is_safe_path_component(name)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    parent_inode = lo_inode(req, parent);
    if (!parent_inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    err = lo_change_cred(req, &old);
    if (err) {
        goto out;
    }

    update_open_flags(lo->writeback, fi);

    fd = openat(parent_inode->fd, name, (fi->flags | O_CREAT) & ~O_NOFOLLOW,
                mode);
    err = fd == -1 ? errno : 0;
    lo_restore_cred(&old);

    if (!err) {
        ssize_t fh;

        pthread_mutex_lock(&lo->mutex);
        fh = lo_add_fd_mapping(req, fd);
        pthread_mutex_unlock(&lo->mutex);
        if (fh == -1) {
            close(fd);
            err = ENOMEM;
            goto out;
        }

        fi->fh = fh;
        err = lo_do_lookup(req, parent, name, &e);
    }
    if (lo->cache == CACHE_NONE) {
        fi->direct_io = 1;
    } else if (lo->cache == CACHE_ALWAYS) {
        fi->keep_cache = 1;
    }

out:
    lo_inode_put(lo, &parent_inode);

    if (err) {
        fuse_reply_err(req, err);
    } else {
        fuse_reply_create(req, &e, fi);
    }
}

/* Should be called with inode->plock_mutex held */
static struct lo_inode_plock *lookup_create_plock_ctx(struct lo_data *lo,
                                                      struct lo_inode *inode,
                                                      uint64_t lock_owner,
                                                      pid_t pid, int *err)
{
    struct lo_inode_plock *plock;
    char procname[64];
    int fd;

    plock =
        g_hash_table_lookup(inode->posix_locks, GUINT_TO_POINTER(lock_owner));

    if (plock) {
        return plock;
    }

    plock = malloc(sizeof(struct lo_inode_plock));
    if (!plock) {
        *err = ENOMEM;
        return NULL;
    }

    /* Open another instance of file which can be used for ofd locks. */
    sprintf(procname, "%i", inode->fd);

    /* TODO: What if file is not writable? */
    fd = openat(lo->proc_self_fd, procname, O_RDWR);
    if (fd == -1) {
        *err = errno;
        free(plock);
        return NULL;
    }

    plock->lock_owner = lock_owner;
    plock->fd = fd;
    g_hash_table_insert(inode->posix_locks, GUINT_TO_POINTER(plock->lock_owner),
                        plock);
    return plock;
}

static void lo_getlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
                     struct flock *lock)
{
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;
    struct lo_inode_plock *plock;
    int ret, saverr = 0;

    fuse_log(FUSE_LOG_DEBUG,
             "lo_getlk(ino=%" PRIu64 ", flags=%d)"
             " owner=0x%lx, l_type=%d l_start=0x%lx"
             " l_len=0x%lx\n",
             ino, fi->flags, fi->lock_owner, lock->l_type, lock->l_start,
             lock->l_len);

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    pthread_mutex_lock(&inode->plock_mutex);
    plock =
        lookup_create_plock_ctx(lo, inode, fi->lock_owner, lock->l_pid, &ret);
    if (!plock) {
        saverr = ret;
        goto out;
    }

    ret = fcntl(plock->fd, F_OFD_GETLK, lock);
    if (ret == -1) {
        saverr = errno;
    }

out:
    pthread_mutex_unlock(&inode->plock_mutex);
    lo_inode_put(lo, &inode);

    if (saverr) {
        fuse_reply_err(req, saverr);
    } else {
        fuse_reply_lock(req, lock);
    }
}

static void lo_setlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
                     struct flock *lock, int sleep)
{
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;
    struct lo_inode_plock *plock;
    int ret, saverr = 0;

    fuse_log(FUSE_LOG_DEBUG,
             "lo_setlk(ino=%" PRIu64 ", flags=%d)"
             " cmd=%d pid=%d owner=0x%lx sleep=%d l_whence=%d"
             " l_start=0x%lx l_len=0x%lx\n",
             ino, fi->flags, lock->l_type, lock->l_pid, fi->lock_owner, sleep,
             lock->l_whence, lock->l_start, lock->l_len);

    if (sleep) {
        fuse_reply_err(req, EOPNOTSUPP);
        return;
    }

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    pthread_mutex_lock(&inode->plock_mutex);
    plock =
        lookup_create_plock_ctx(lo, inode, fi->lock_owner, lock->l_pid, &ret);

    if (!plock) {
        saverr = ret;
        goto out;
    }

    /* TODO: Is it alright to modify flock? */
    lock->l_pid = 0;
    ret = fcntl(plock->fd, F_OFD_SETLK, lock);
    if (ret == -1) {
        saverr = errno;
    }

out:
    pthread_mutex_unlock(&inode->plock_mutex);
    lo_inode_put(lo, &inode);

    fuse_reply_err(req, saverr);
}

static void lo_fsyncdir(fuse_req_t req, fuse_ino_t ino, int datasync,
                        struct fuse_file_info *fi)
{
    int res;
    struct lo_dirp *d;
    int fd;

    (void)ino;

    d = lo_dirp(req, fi);
    if (!d) {
        fuse_reply_err(req, EBADF);
        return;
    }

    fd = dirfd(d->dp);
    if (datasync) {
        res = fdatasync(fd);
    } else {
        res = fsync(fd);
    }

    lo_dirp_put(&d);

    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void lo_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    int fd;
    ssize_t fh;
    char buf[64];
    struct lo_data *lo = lo_data(req);

    fuse_log(FUSE_LOG_DEBUG, "lo_open(ino=%" PRIu64 ", flags=%d)\n", ino,
             fi->flags);

    update_open_flags(lo->writeback, fi);

    sprintf(buf, "%i", lo_fd(req, ino));
    fd = openat(lo->proc_self_fd, buf, fi->flags & ~O_NOFOLLOW);
    if (fd == -1) {
        return (void)fuse_reply_err(req, errno);
    }

    pthread_mutex_lock(&lo->mutex);
    fh = lo_add_fd_mapping(req, fd);
    pthread_mutex_unlock(&lo->mutex);
    if (fh == -1) {
        close(fd);
        fuse_reply_err(req, ENOMEM);
        return;
    }

    fi->fh = fh;
    if (lo->cache == CACHE_NONE) {
        fi->direct_io = 1;
    } else if (lo->cache == CACHE_ALWAYS) {
        fi->keep_cache = 1;
    }
    fuse_reply_open(req, fi);
}

static void lo_release(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info *fi)
{
    struct lo_data *lo = lo_data(req);
    struct lo_map_elem *elem;
    int fd = -1;

    (void)ino;

    pthread_mutex_lock(&lo->mutex);
    elem = lo_map_get(&lo->fd_map, fi->fh);
    if (elem) {
        fd = elem->fd;
        elem = NULL;
        lo_map_remove(&lo->fd_map, fi->fh);
    }
    pthread_mutex_unlock(&lo->mutex);

    close(fd);
    fuse_reply_err(req, 0);
}

static void lo_flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    int res;
    (void)ino;
    struct lo_inode *inode;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    /* An fd is going away. Cleanup associated posix locks */
    pthread_mutex_lock(&inode->plock_mutex);
    g_hash_table_remove(inode->posix_locks, GUINT_TO_POINTER(fi->lock_owner));
    pthread_mutex_unlock(&inode->plock_mutex);

    res = close(dup(lo_fi_fd(req, fi)));
    lo_inode_put(lo_data(req), &inode);
    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void lo_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                     struct fuse_file_info *fi)
{
    int res;
    int fd;
    char *buf;

    fuse_log(FUSE_LOG_DEBUG, "lo_fsync(ino=%" PRIu64 ", fi=0x%p)\n", ino,
             (void *)fi);

    if (!fi) {
        struct lo_data *lo = lo_data(req);

        res = asprintf(&buf, "%i", lo_fd(req, ino));
        if (res == -1) {
            return (void)fuse_reply_err(req, errno);
        }

        fd = openat(lo->proc_self_fd, buf, O_RDWR);
        free(buf);
        if (fd == -1) {
            return (void)fuse_reply_err(req, errno);
        }
    } else {
        fd = lo_fi_fd(req, fi);
    }

    if (datasync) {
        res = fdatasync(fd);
    } else {
        res = fsync(fd);
    }
    if (!fi) {
        close(fd);
    }
    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void lo_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    struct fuse_bufvec buf = FUSE_BUFVEC_INIT(size);

    fuse_log(FUSE_LOG_DEBUG,
             "lo_read(ino=%" PRIu64 ", size=%zd, "
             "off=%lu)\n",
             ino, size, (unsigned long)offset);

    buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
    buf.buf[0].fd = lo_fi_fd(req, fi);
    buf.buf[0].pos = offset;

    fuse_reply_data(req, &buf);
}

static void lo_write_buf(fuse_req_t req, fuse_ino_t ino,
                         struct fuse_bufvec *in_buf, off_t off,
                         struct fuse_file_info *fi)
{
    (void)ino;
    ssize_t res;
    struct fuse_bufvec out_buf = FUSE_BUFVEC_INIT(fuse_buf_size(in_buf));
    bool cap_fsetid_dropped = false;

    out_buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
    out_buf.buf[0].fd = lo_fi_fd(req, fi);
    out_buf.buf[0].pos = off;

    fuse_log(FUSE_LOG_DEBUG,
             "lo_write_buf(ino=%" PRIu64 ", size=%zd, off=%lu)\n", ino,
             out_buf.buf[0].size, (unsigned long)off);

    /*
     * If kill_priv is set, drop CAP_FSETID which should lead to kernel
     * clearing setuid/setgid on file.
     */
    if (fi->kill_priv) {
        res = drop_effective_cap("FSETID", &cap_fsetid_dropped);
        if (res != 0) {
            fuse_reply_err(req, res);
            return;
        }
    }

    res = fuse_buf_copy(&out_buf, in_buf);
    if (res < 0) {
        fuse_reply_err(req, -res);
    } else {
        fuse_reply_write(req, (size_t)res);
    }

    if (cap_fsetid_dropped) {
        res = gain_effective_cap("FSETID");
        if (res) {
            fuse_log(FUSE_LOG_ERR, "Failed to gain CAP_FSETID\n");
        }
    }
}

static void lo_statfs(fuse_req_t req, fuse_ino_t ino)
{
    int res;
    struct statvfs stbuf;

    res = fstatvfs(lo_fd(req, ino), &stbuf);
    if (res == -1) {
        fuse_reply_err(req, errno);
    } else {
        fuse_reply_statfs(req, &stbuf);
    }
}

static void lo_fallocate(fuse_req_t req, fuse_ino_t ino, int mode, off_t offset,
                         off_t length, struct fuse_file_info *fi)
{
    int err = EOPNOTSUPP;
    (void)ino;

#ifdef CONFIG_FALLOCATE
    err = fallocate(lo_fi_fd(req, fi), mode, offset, length);
    if (err < 0) {
        err = errno;
    }

#elif defined(CONFIG_POSIX_FALLOCATE)
    if (mode) {
        fuse_reply_err(req, EOPNOTSUPP);
        return;
    }

    err = posix_fallocate(lo_fi_fd(req, fi), offset, length);
#endif

    fuse_reply_err(req, err);
}

static void lo_flock(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
                     int op)
{
    int res;
    (void)ino;

    res = flock(lo_fi_fd(req, fi), op);

    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void lo_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
                        size_t size)
{
    struct lo_data *lo = lo_data(req);
    char *value = NULL;
    char procname[64];
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;
    int fd = -1;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    saverr = ENOSYS;
    if (!lo_data(req)->xattr) {
        goto out;
    }

    fuse_log(FUSE_LOG_DEBUG, "lo_getxattr(ino=%" PRIu64 ", name=%s size=%zd)\n",
             ino, name, size);

    if (size) {
        value = malloc(size);
        if (!value) {
            goto out_err;
        }
    }

    sprintf(procname, "%i", inode->fd);
    /*
     * It is not safe to open() non-regular/non-dir files in file server
     * unless O_PATH is used, so use that method for regular files/dir
     * only (as it seems giving less performance overhead).
     * Otherwise, call fchdir() to avoid open().
     */
    if (S_ISREG(inode->filetype) || S_ISDIR(inode->filetype)) {
        fd = openat(lo->proc_self_fd, procname, O_RDONLY);
        if (fd < 0) {
            goto out_err;
        }
        ret = fgetxattr(fd, name, value, size);
    } else {
        /* fchdir should not fail here */
        assert(fchdir(lo->proc_self_fd) == 0);
        ret = getxattr(procname, name, value, size);
        assert(fchdir(lo->root.fd) == 0);
    }

    if (ret == -1) {
        goto out_err;
    }
    if (size) {
        saverr = 0;
        if (ret == 0) {
            goto out;
        }
        fuse_reply_buf(req, value, ret);
    } else {
        fuse_reply_xattr(req, ret);
    }
out_free:
    free(value);

    if (fd >= 0) {
        close(fd);
    }

    lo_inode_put(lo, &inode);
    return;

out_err:
    saverr = errno;
out:
    fuse_reply_err(req, saverr);
    goto out_free;
}

static void lo_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size)
{
    struct lo_data *lo = lo_data(req);
    char *value = NULL;
    char procname[64];
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;
    int fd = -1;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    saverr = ENOSYS;
    if (!lo_data(req)->xattr) {
        goto out;
    }

    fuse_log(FUSE_LOG_DEBUG, "lo_listxattr(ino=%" PRIu64 ", size=%zd)\n", ino,
             size);

    if (size) {
        value = malloc(size);
        if (!value) {
            goto out_err;
        }
    }

    sprintf(procname, "%i", inode->fd);
    if (S_ISREG(inode->filetype) || S_ISDIR(inode->filetype)) {
        fd = openat(lo->proc_self_fd, procname, O_RDONLY);
        if (fd < 0) {
            goto out_err;
        }
        ret = flistxattr(fd, value, size);
    } else {
        /* fchdir should not fail here */
        assert(fchdir(lo->proc_self_fd) == 0);
        ret = listxattr(procname, value, size);
        assert(fchdir(lo->root.fd) == 0);
    }

    if (ret == -1) {
        goto out_err;
    }
    if (size) {
        saverr = 0;
        if (ret == 0) {
            goto out;
        }
        fuse_reply_buf(req, value, ret);
    } else {
        fuse_reply_xattr(req, ret);
    }
out_free:
    free(value);

    if (fd >= 0) {
        close(fd);
    }

    lo_inode_put(lo, &inode);
    return;

out_err:
    saverr = errno;
out:
    fuse_reply_err(req, saverr);
    goto out_free;
}

static void lo_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
                        const char *value, size_t size, int flags)
{
    char procname[64];
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;
    int fd = -1;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    saverr = ENOSYS;
    if (!lo_data(req)->xattr) {
        goto out;
    }

    fuse_log(FUSE_LOG_DEBUG, "lo_setxattr(ino=%" PRIu64
             ", name=%s value=%s size=%zd)\n", ino, name, value, size);

    sprintf(procname, "%i", inode->fd);
    if (S_ISREG(inode->filetype) || S_ISDIR(inode->filetype)) {
        fd = openat(lo->proc_self_fd, procname, O_RDONLY);
        if (fd < 0) {
            saverr = errno;
            goto out;
        }
        ret = fsetxattr(fd, name, value, size, flags);
    } else {
        /* fchdir should not fail here */
        assert(fchdir(lo->proc_self_fd) == 0);
        ret = setxattr(procname, name, value, size, flags);
        assert(fchdir(lo->root.fd) == 0);
    }

    saverr = ret == -1 ? errno : 0;

out:
    if (fd >= 0) {
        close(fd);
    }

    lo_inode_put(lo, &inode);
    fuse_reply_err(req, saverr);
}

static void lo_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name)
{
    char procname[64];
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;
    int fd = -1;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    saverr = ENOSYS;
    if (!lo_data(req)->xattr) {
        goto out;
    }

    fuse_log(FUSE_LOG_DEBUG, "lo_removexattr(ino=%" PRIu64 ", name=%s)\n", ino,
             name);

    sprintf(procname, "%i", inode->fd);
    if (S_ISREG(inode->filetype) || S_ISDIR(inode->filetype)) {
        fd = openat(lo->proc_self_fd, procname, O_RDONLY);
        if (fd < 0) {
            saverr = errno;
            goto out;
        }
        ret = fremovexattr(fd, name);
    } else {
        /* fchdir should not fail here */
        assert(fchdir(lo->proc_self_fd) == 0);
        ret = removexattr(procname, name);
        assert(fchdir(lo->root.fd) == 0);
    }

    saverr = ret == -1 ? errno : 0;

out:
    if (fd >= 0) {
        close(fd);
    }

    lo_inode_put(lo, &inode);
    fuse_reply_err(req, saverr);
}

#ifdef HAVE_COPY_FILE_RANGE
static void lo_copy_file_range(fuse_req_t req, fuse_ino_t ino_in, off_t off_in,
                               struct fuse_file_info *fi_in, fuse_ino_t ino_out,
                               off_t off_out, struct fuse_file_info *fi_out,
                               size_t len, int flags)
{
    int in_fd, out_fd;
    ssize_t res;

    in_fd = lo_fi_fd(req, fi_in);
    out_fd = lo_fi_fd(req, fi_out);

    fuse_log(FUSE_LOG_DEBUG,
             "lo_copy_file_range(ino=%" PRIu64 "/fd=%d, "
             "off=%lu, ino=%" PRIu64 "/fd=%d, "
             "off=%lu, size=%zd, flags=0x%x)\n",
             ino_in, in_fd, off_in, ino_out, out_fd, off_out, len, flags);

    res = copy_file_range(in_fd, &off_in, out_fd, &off_out, len, flags);
    if (res < 0) {
        fuse_reply_err(req, errno);
    } else {
        fuse_reply_write(req, res);
    }
}
#endif

static void lo_lseek(fuse_req_t req, fuse_ino_t ino, off_t off, int whence,
                     struct fuse_file_info *fi)
{
    off_t res;

    (void)ino;
    res = lseek(lo_fi_fd(req, fi), off, whence);
    if (res != -1) {
        fuse_reply_lseek(req, res);
    } else {
        fuse_reply_err(req, errno);
    }
}

static void lo_destroy(void *userdata)
{
    struct lo_data *lo = (struct lo_data *)userdata;

    pthread_mutex_lock(&lo->mutex);
    while (true) {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init(&iter, lo->inodes);
        if (!g_hash_table_iter_next(&iter, &key, &value)) {
            break;
        }

        struct lo_inode *inode = value;
        unref_inode(lo, inode, inode->nlookup);
    }
    pthread_mutex_unlock(&lo->mutex);
}

static struct fuse_lowlevel_ops lo_oper = {
    .init = lo_init,
    .lookup = lo_lookup,
    .mkdir = lo_mkdir,
    .mknod = lo_mknod,
    .symlink = lo_symlink,
    .link = lo_link,
    .unlink = lo_unlink,
    .rmdir = lo_rmdir,
    .rename = lo_rename,
    .forget = lo_forget,
    .forget_multi = lo_forget_multi,
    .getattr = lo_getattr,
    .setattr = lo_setattr,
    .readlink = lo_readlink,
    .opendir = lo_opendir,
    .readdir = lo_readdir,
    .readdirplus = lo_readdirplus,
    .releasedir = lo_releasedir,
    .fsyncdir = lo_fsyncdir,
    .create = lo_create,
    .getlk = lo_getlk,
    .setlk = lo_setlk,
    .open = lo_open,
    .release = lo_release,
    .flush = lo_flush,
    .fsync = lo_fsync,
    .read = lo_read,
    .write_buf = lo_write_buf,
    .statfs = lo_statfs,
    .fallocate = lo_fallocate,
    .flock = lo_flock,
    .getxattr = lo_getxattr,
    .listxattr = lo_listxattr,
    .setxattr = lo_setxattr,
    .removexattr = lo_removexattr,
#ifdef HAVE_COPY_FILE_RANGE
    .copy_file_range = lo_copy_file_range,
#endif
    .lseek = lo_lseek,
    .destroy = lo_destroy,
};

/* Print vhost-user.json backend program capabilities */
static void print_capabilities(void)
{
    printf("{\n");
    printf("  \"type\": \"fs\"\n");
    printf("}\n");
}

/*
 * Drop all Linux capabilities because the wait parent process only needs to
 * sit in waitpid(2) and terminate.
 */
static void setup_wait_parent_capabilities(void)
{
    capng_setpid(syscall(SYS_gettid));
    capng_clear(CAPNG_SELECT_BOTH);
    capng_apply(CAPNG_SELECT_BOTH);
}

/*
 * Move to a new mount, net, and pid namespaces to isolate this process.
 */
static void setup_namespaces(struct lo_data *lo, struct fuse_session *se)
{
    pid_t child;
    char template[] = "virtiofsd-XXXXXX";
    char *tmpdir;

    /*
     * Create a new pid namespace for *child* processes.  We'll have to
     * fork in order to enter the new pid namespace.  A new mount namespace
     * is also needed so that we can remount /proc for the new pid
     * namespace.
     *
     * Our UNIX domain sockets have been created.  Now we can move to
     * an empty network namespace to prevent TCP/IP and other network
     * activity in case this process is compromised.
     */
    if (unshare(CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET) != 0) {
        fuse_log(FUSE_LOG_ERR, "unshare(CLONE_NEWPID | CLONE_NEWNS): %m\n");
        exit(1);
    }

    child = fork();
    if (child < 0) {
        fuse_log(FUSE_LOG_ERR, "fork() failed: %m\n");
        exit(1);
    }
    if (child > 0) {
        pid_t waited;
        int wstatus;

        setup_wait_parent_capabilities();

        /* The parent waits for the child */
        do {
            waited = waitpid(child, &wstatus, 0);
        } while (waited < 0 && errno == EINTR && !se->exited);

        /* We were terminated by a signal, see fuse_signals.c */
        if (se->exited) {
            exit(0);
        }

        if (WIFEXITED(wstatus)) {
            exit(WEXITSTATUS(wstatus));
        }

        exit(1);
    }

    /* Send us SIGTERM when the parent thread terminates, see prctl(2) */
    prctl(PR_SET_PDEATHSIG, SIGTERM);

    /*
     * If the mounts have shared propagation then we want to opt out so our
     * mount changes don't affect the parent mount namespace.
     */
    if (mount(NULL, "/", NULL, MS_REC | MS_SLAVE, NULL) < 0) {
        fuse_log(FUSE_LOG_ERR, "mount(/, MS_REC|MS_SLAVE): %m\n");
        exit(1);
    }

    /* The child must remount /proc to use the new pid namespace */
    if (mount("proc", "/proc", "proc",
              MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME, NULL) < 0) {
        fuse_log(FUSE_LOG_ERR, "mount(/proc): %m\n");
        exit(1);
    }

    tmpdir = mkdtemp(template);
    if (!tmpdir) {
        fuse_log(FUSE_LOG_ERR, "tmpdir(%s): %m\n", template);
        exit(1);
    }

    if (mount("/proc/self/fd", tmpdir, NULL, MS_BIND, NULL) < 0) {
        fuse_log(FUSE_LOG_ERR, "mount(/proc/self/fd, %s, MS_BIND): %m\n",
                 tmpdir);
        exit(1);
    }

    /* Now we can get our /proc/self/fd directory file descriptor */
    lo->proc_self_fd = open(tmpdir, O_PATH);
    if (lo->proc_self_fd == -1) {
        fuse_log(FUSE_LOG_ERR, "open(%s, O_PATH): %m\n", tmpdir);
        exit(1);
    }

    if (umount2(tmpdir, MNT_DETACH) < 0) {
        fuse_log(FUSE_LOG_ERR, "umount2(%s, MNT_DETACH): %m\n", tmpdir);
        exit(1);
    }

    if (rmdir(tmpdir) < 0) {
        fuse_log(FUSE_LOG_ERR, "rmdir(%s): %m\n", tmpdir);
    }
}

/*
 * Capture the capability state, we'll need to restore this for individual
 * threads later; see load_capng.
 */
static void setup_capng(void)
{
    /* Note this accesses /proc so has to happen before the sandbox */
    if (capng_get_caps_process()) {
        fuse_log(FUSE_LOG_ERR, "capng_get_caps_process\n");
        exit(1);
    }
    pthread_mutex_init(&cap.mutex, NULL);
    pthread_mutex_lock(&cap.mutex);
    cap.saved = capng_save_state();
    if (!cap.saved) {
        fuse_log(FUSE_LOG_ERR, "capng_save_state\n");
        exit(1);
    }
    pthread_mutex_unlock(&cap.mutex);
}

static void cleanup_capng(void)
{
    free(cap.saved);
    cap.saved = NULL;
    pthread_mutex_destroy(&cap.mutex);
}


/*
 * Make the source directory our root so symlinks cannot escape and no other
 * files are accessible.  Assumes unshare(CLONE_NEWNS) was already called.
 */
static void setup_mounts(const char *source)
{
    int oldroot;
    int newroot;

    if (mount(source, source, NULL, MS_BIND | MS_REC, NULL) < 0) {
        fuse_log(FUSE_LOG_ERR, "mount(%s, %s, MS_BIND): %m\n", source, source);
        exit(1);
    }

    /* This magic is based on lxc's lxc_pivot_root() */
    oldroot = open("/", O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    if (oldroot < 0) {
        fuse_log(FUSE_LOG_ERR, "open(/): %m\n");
        exit(1);
    }

    newroot = open(source, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    if (newroot < 0) {
        fuse_log(FUSE_LOG_ERR, "open(%s): %m\n", source);
        exit(1);
    }

    if (fchdir(newroot) < 0) {
        fuse_log(FUSE_LOG_ERR, "fchdir(newroot): %m\n");
        exit(1);
    }

    if (syscall(__NR_pivot_root, ".", ".") < 0) {
        fuse_log(FUSE_LOG_ERR, "pivot_root(., .): %m\n");
        exit(1);
    }

    if (fchdir(oldroot) < 0) {
        fuse_log(FUSE_LOG_ERR, "fchdir(oldroot): %m\n");
        exit(1);
    }

    if (mount("", ".", "", MS_SLAVE | MS_REC, NULL) < 0) {
        fuse_log(FUSE_LOG_ERR, "mount(., MS_SLAVE | MS_REC): %m\n");
        exit(1);
    }

    if (umount2(".", MNT_DETACH) < 0) {
        fuse_log(FUSE_LOG_ERR, "umount2(., MNT_DETACH): %m\n");
        exit(1);
    }

    if (fchdir(newroot) < 0) {
        fuse_log(FUSE_LOG_ERR, "fchdir(newroot): %m\n");
        exit(1);
    }

    close(newroot);
    close(oldroot);
}

/*
 * Only keep whitelisted capabilities that are needed for file system operation
 * The (possibly NULL) modcaps_in string passed in is free'd before exit.
 */
static void setup_capabilities(char *modcaps_in)
{
    char *modcaps = modcaps_in;
    pthread_mutex_lock(&cap.mutex);
    capng_restore_state(&cap.saved);

    /*
     * Whitelist file system-related capabilities that are needed for a file
     * server to act like root.  Drop everything else like networking and
     * sysadmin capabilities.
     *
     * Exclusions:
     * 1. CAP_LINUX_IMMUTABLE is not included because it's only used via ioctl
     *    and we don't support that.
     * 2. CAP_MAC_OVERRIDE is not included because it only seems to be
     *    used by the Smack LSM.  Omit it until there is demand for it.
     */
    capng_setpid(syscall(SYS_gettid));
    capng_clear(CAPNG_SELECT_BOTH);
    if (capng_updatev(CAPNG_ADD, CAPNG_PERMITTED | CAPNG_EFFECTIVE,
            CAP_CHOWN,
            CAP_DAC_OVERRIDE,
            CAP_DAC_READ_SEARCH,
            CAP_FOWNER,
            CAP_FSETID,
            CAP_SETGID,
            CAP_SETUID,
            CAP_MKNOD,
            CAP_SETFCAP,
            -1)) {
        fuse_log(FUSE_LOG_ERR, "%s: capng_updatev failed\n", __func__);
        exit(1);
    }

    /*
     * The modcaps option is a colon separated list of caps,
     * each preceded by either + or -.
     */
    while (modcaps) {
        capng_act_t action;
        int cap;

        char *next = strchr(modcaps, ':');
        if (next) {
            *next = '\0';
            next++;
        }

        switch (modcaps[0]) {
        case '+':
            action = CAPNG_ADD;
            break;

        case '-':
            action = CAPNG_DROP;
            break;

        default:
            fuse_log(FUSE_LOG_ERR,
                     "%s: Expecting '+'/'-' in modcaps but found '%c'\n",
                     __func__, modcaps[0]);
            exit(1);
        }
        cap = capng_name_to_capability(modcaps + 1);
        if (cap < 0) {
            fuse_log(FUSE_LOG_ERR, "%s: Unknown capability '%s'\n", __func__,
                     modcaps);
            exit(1);
        }
        if (capng_update(action, CAPNG_PERMITTED | CAPNG_EFFECTIVE, cap)) {
            fuse_log(FUSE_LOG_ERR, "%s: capng_update failed for '%s'\n",
                     __func__, modcaps);
            exit(1);
        }

        modcaps = next;
    }
    g_free(modcaps_in);

    if (capng_apply(CAPNG_SELECT_BOTH)) {
        fuse_log(FUSE_LOG_ERR, "%s: capng_apply failed\n", __func__);
        exit(1);
    }

    cap.saved = capng_save_state();
    if (!cap.saved) {
        fuse_log(FUSE_LOG_ERR, "%s: capng_save_state failed\n", __func__);
        exit(1);
    }
    pthread_mutex_unlock(&cap.mutex);
}

/*
 * Lock down this process to prevent access to other processes or files outside
 * source directory.  This reduces the impact of arbitrary code execution bugs.
 */
static void setup_sandbox(struct lo_data *lo, struct fuse_session *se,
                          bool enable_syslog)
{
    setup_namespaces(lo, se);
    setup_mounts(lo->source);
    setup_seccomp(enable_syslog);
    setup_capabilities(g_strdup(lo->modcaps));
}

/* Set the maximum number of open file descriptors */
static void setup_nofile_rlimit(unsigned long rlimit_nofile)
{
    struct rlimit rlim = {
        .rlim_cur = rlimit_nofile,
        .rlim_max = rlimit_nofile,
    };

    if (rlimit_nofile == 0) {
        return; /* nothing to do */
    }

    if (setrlimit(RLIMIT_NOFILE, &rlim) < 0) {
        /* Ignore SELinux denials */
        if (errno == EPERM) {
            return;
        }

        fuse_log(FUSE_LOG_ERR, "setrlimit(RLIMIT_NOFILE): %m\n");
        exit(1);
    }
}

static void log_func(enum fuse_log_level level, const char *fmt, va_list ap)
{
    g_autofree char *localfmt = NULL;

    if (current_log_level < level) {
        return;
    }

    if (current_log_level == FUSE_LOG_DEBUG) {
        if (!use_syslog) {
            localfmt = g_strdup_printf("[%" PRId64 "] [ID: %08ld] %s",
                                       get_clock(), syscall(__NR_gettid), fmt);
        } else {
            localfmt = g_strdup_printf("[ID: %08ld] %s", syscall(__NR_gettid),
                                       fmt);
        }
        fmt = localfmt;
    }

    if (use_syslog) {
        int priority = LOG_ERR;
        switch (level) {
        case FUSE_LOG_EMERG:
            priority = LOG_EMERG;
            break;
        case FUSE_LOG_ALERT:
            priority = LOG_ALERT;
            break;
        case FUSE_LOG_CRIT:
            priority = LOG_CRIT;
            break;
        case FUSE_LOG_ERR:
            priority = LOG_ERR;
            break;
        case FUSE_LOG_WARNING:
            priority = LOG_WARNING;
            break;
        case FUSE_LOG_NOTICE:
            priority = LOG_NOTICE;
            break;
        case FUSE_LOG_INFO:
            priority = LOG_INFO;
            break;
        case FUSE_LOG_DEBUG:
            priority = LOG_DEBUG;
            break;
        }
        vsyslog(priority, fmt, ap);
    } else {
        vfprintf(stderr, fmt, ap);
    }
}

static void setup_root(struct lo_data *lo, struct lo_inode *root)
{
    int fd, res;
    struct stat stat;

    fd = open("/", O_PATH);
    if (fd == -1) {
        fuse_log(FUSE_LOG_ERR, "open(%s, O_PATH): %m\n", lo->source);
        exit(1);
    }

    res = fstatat(fd, "", &stat, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    if (res == -1) {
        fuse_log(FUSE_LOG_ERR, "fstatat(%s): %m\n", lo->source);
        exit(1);
    }

    root->filetype = S_IFDIR;
    root->fd = fd;
    root->key.ino = stat.st_ino;
    root->key.dev = stat.st_dev;
    root->nlookup = 2;
    g_atomic_int_set(&root->refcount, 2);
}

static guint lo_key_hash(gconstpointer key)
{
    const struct lo_key *lkey = key;

    return (guint)lkey->ino + (guint)lkey->dev;
}

static gboolean lo_key_equal(gconstpointer a, gconstpointer b)
{
    const struct lo_key *la = a;
    const struct lo_key *lb = b;

    return la->ino == lb->ino && la->dev == lb->dev;
}

static void fuse_lo_data_cleanup(struct lo_data *lo)
{
    if (lo->inodes) {
        g_hash_table_destroy(lo->inodes);
    }
    lo_map_destroy(&lo->fd_map);
    lo_map_destroy(&lo->dirp_map);
    lo_map_destroy(&lo->ino_map);

    if (lo->proc_self_fd >= 0) {
        close(lo->proc_self_fd);
    }

    if (lo->root.fd >= 0) {
        close(lo->root.fd);
    }

    free(lo->source);
}

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_session *se;
    struct fuse_cmdline_opts opts;
    struct lo_data lo = {
        .debug = 0,
        .writeback = 0,
        .posix_lock = 1,
        .proc_self_fd = -1,
    };
    struct lo_map_elem *root_elem;
    int ret = -1;

    /* Don't mask creation mode, kernel already did that */
    umask(0);

    pthread_mutex_init(&lo.mutex, NULL);
    lo.inodes = g_hash_table_new(lo_key_hash, lo_key_equal);
    lo.root.fd = -1;
    lo.root.fuse_ino = FUSE_ROOT_ID;
    lo.cache = CACHE_AUTO;

    /*
     * Set up the ino map like this:
     * [0] Reserved (will not be used)
     * [1] Root inode
     */
    lo_map_init(&lo.ino_map);
    lo_map_reserve(&lo.ino_map, 0)->in_use = false;
    root_elem = lo_map_reserve(&lo.ino_map, lo.root.fuse_ino);
    root_elem->inode = &lo.root;

    lo_map_init(&lo.dirp_map);
    lo_map_init(&lo.fd_map);

    if (fuse_parse_cmdline(&args, &opts) != 0) {
        goto err_out1;
    }
    fuse_set_log_func(log_func);
    use_syslog = opts.syslog;
    if (use_syslog) {
        openlog("virtiofsd", LOG_PID, LOG_DAEMON);
    }

    if (opts.show_help) {
        printf("usage: %s [options]\n\n", argv[0]);
        fuse_cmdline_help();
        printf("    -o source=PATH             shared directory tree\n");
        fuse_lowlevel_help();
        ret = 0;
        goto err_out1;
    } else if (opts.show_version) {
        fuse_lowlevel_version();
        ret = 0;
        goto err_out1;
    } else if (opts.print_capabilities) {
        print_capabilities();
        ret = 0;
        goto err_out1;
    }

    if (fuse_opt_parse(&args, &lo, lo_opts, NULL) == -1) {
        goto err_out1;
    }

    /*
     * log_level is 0 if not configured via cmd options (0 is LOG_EMERG,
     * and we don't use this log level).
     */
    if (opts.log_level != 0) {
        current_log_level = opts.log_level;
    }
    lo.debug = opts.debug;
    if (lo.debug) {
        current_log_level = FUSE_LOG_DEBUG;
    }
    if (lo.source) {
        struct stat stat;
        int res;

        res = lstat(lo.source, &stat);
        if (res == -1) {
            fuse_log(FUSE_LOG_ERR, "failed to stat source (\"%s\"): %m\n",
                     lo.source);
            exit(1);
        }
        if (!S_ISDIR(stat.st_mode)) {
            fuse_log(FUSE_LOG_ERR, "source is not a directory\n");
            exit(1);
        }
    } else {
        lo.source = strdup("/");
    }
    if (!lo.timeout_set) {
        switch (lo.cache) {
        case CACHE_NONE:
            lo.timeout = 0.0;
            break;

        case CACHE_AUTO:
            lo.timeout = 1.0;
            break;

        case CACHE_ALWAYS:
            lo.timeout = 86400.0;
            break;
        }
    } else if (lo.timeout < 0) {
        fuse_log(FUSE_LOG_ERR, "timeout is negative (%lf)\n", lo.timeout);
        exit(1);
    }

    se = fuse_session_new(&args, &lo_oper, sizeof(lo_oper), &lo);
    if (se == NULL) {
        goto err_out1;
    }

    if (fuse_set_signal_handlers(se) != 0) {
        goto err_out2;
    }

    if (fuse_session_mount(se) != 0) {
        goto err_out3;
    }

    fuse_daemonize(opts.foreground);

    setup_nofile_rlimit(opts.rlimit_nofile);

    /* Must be before sandbox since it wants /proc */
    setup_capng();

    setup_sandbox(&lo, se, opts.syslog);

    setup_root(&lo, &lo.root);
    /* Block until ctrl+c or fusermount -u */
    ret = virtio_loop(se);

    fuse_session_unmount(se);
    cleanup_capng();
err_out3:
    fuse_remove_signal_handlers(se);
err_out2:
    fuse_session_destroy(se);
err_out1:
    fuse_opt_free_args(&args);

    fuse_lo_data_cleanup(&lo);

    return ret ? 1 : 0;
}
