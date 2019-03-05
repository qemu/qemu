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
#include "fuse_virtio.h"
#include "fuse_lowlevel.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "passthrough_helpers.h"

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

struct lo_inode {
    struct lo_inode *next; /* protected by lo->mutex */
    struct lo_inode *prev; /* protected by lo->mutex */
    int fd;
    bool is_symlink;
    ino_t ino;
    dev_t dev;
    uint64_t refcount; /* protected by lo->mutex */
    fuse_ino_t fuse_ino;
};

struct lo_cred {
    uid_t euid;
    gid_t egid;
};

enum {
    CACHE_NEVER,
    CACHE_NORMAL,
    CACHE_ALWAYS,
};

struct lo_data {
    pthread_mutex_t mutex;
    int debug;
    int norace;
    int writeback;
    int flock;
    int xattr;
    const char *source;
    double timeout;
    int cache;
    int timeout_set;
    struct lo_inode root; /* protected by lo->mutex */
    struct lo_map ino_map; /* protected by lo->mutex */
    struct lo_map dirp_map; /* protected by lo->mutex */
    struct lo_map fd_map; /* protected by lo->mutex */
};

static const struct fuse_opt lo_opts[] = {
    { "writeback", offsetof(struct lo_data, writeback), 1 },
    { "no_writeback", offsetof(struct lo_data, writeback), 0 },
    { "source=%s", offsetof(struct lo_data, source), 0 },
    { "flock", offsetof(struct lo_data, flock), 1 },
    { "no_flock", offsetof(struct lo_data, flock), 0 },
    { "xattr", offsetof(struct lo_data, xattr), 1 },
    { "no_xattr", offsetof(struct lo_data, xattr), 0 },
    { "timeout=%lf", offsetof(struct lo_data, timeout), 0 },
    { "timeout=", offsetof(struct lo_data, timeout_set), 1 },
    { "cache=never", offsetof(struct lo_data, cache), CACHE_NEVER },
    { "cache=auto", offsetof(struct lo_data, cache), CACHE_NORMAL },
    { "cache=always", offsetof(struct lo_data, cache), CACHE_ALWAYS },
    { "norace", offsetof(struct lo_data, norace), 1 },
    FUSE_OPT_END
};

static void unref_inode(struct lo_data *lo, struct lo_inode *inode, uint64_t n);

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

static struct lo_inode *lo_inode(fuse_req_t req, fuse_ino_t ino)
{
    struct lo_data *lo = lo_data(req);
    struct lo_map_elem *elem;

    pthread_mutex_lock(&lo->mutex);
    elem = lo_map_get(&lo->ino_map, ino);
    pthread_mutex_unlock(&lo->mutex);

    if (!elem) {
        return NULL;
    }

    return elem->inode;
}

static int lo_fd(fuse_req_t req, fuse_ino_t ino)
{
    struct lo_inode *inode = lo_inode(req, ino);
    return inode ? inode->fd : -1;
}

static bool lo_debug(fuse_req_t req)
{
    return lo_data(req)->debug != 0;
}

static void lo_init(void *userdata, struct fuse_conn_info *conn)
{
    struct lo_data *lo = (struct lo_data *)userdata;

    if (conn->capable & FUSE_CAP_EXPORT_SUPPORT) {
        conn->want |= FUSE_CAP_EXPORT_SUPPORT;
    }

    if (lo->writeback && conn->capable & FUSE_CAP_WRITEBACK_CACHE) {
        if (lo->debug) {
            fuse_log(FUSE_LOG_DEBUG, "lo_init: activating writeback\n");
        }
        conn->want |= FUSE_CAP_WRITEBACK_CACHE;
    }
    if (lo->flock && conn->capable & FUSE_CAP_FLOCK_LOCKS) {
        if (lo->debug) {
            fuse_log(FUSE_LOG_DEBUG, "lo_init: activating flock locks\n");
        }
        conn->want |= FUSE_CAP_FLOCK_LOCKS;
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

static int lo_parent_and_name(struct lo_data *lo, struct lo_inode *inode,
                              char path[PATH_MAX], struct lo_inode **parent)
{
    char procname[64];
    char *last;
    struct stat stat;
    struct lo_inode *p;
    int retries = 2;
    int res;

retry:
    sprintf(procname, "/proc/self/fd/%i", inode->fd);

    res = readlink(procname, path, PATH_MAX);
    if (res < 0) {
        fuse_log(FUSE_LOG_WARNING, "%s: readlink failed: %m\n", __func__);
        goto fail_noretry;
    }

    if (res >= PATH_MAX) {
        fuse_log(FUSE_LOG_WARNING, "%s: readlink overflowed\n", __func__);
        goto fail_noretry;
    }
    path[res] = '\0';

    last = strrchr(path, '/');
    if (last == NULL) {
        /* Shouldn't happen */
        fuse_log(
            FUSE_LOG_WARNING,
            "%s: INTERNAL ERROR: bad path read from proc\n", __func__);
        goto fail_noretry;
    }
    if (last == path) {
        p = &lo->root;
        pthread_mutex_lock(&lo->mutex);
        p->refcount++;
        pthread_mutex_unlock(&lo->mutex);
    } else {
        *last = '\0';
        res = fstatat(AT_FDCWD, last == path ? "/" : path, &stat, 0);
        if (res == -1) {
            if (!retries) {
                fuse_log(FUSE_LOG_WARNING,
                         "%s: failed to stat parent: %m\n", __func__);
            }
            goto fail;
        }
        p = lo_find(lo, &stat);
        if (p == NULL) {
            if (!retries) {
                fuse_log(FUSE_LOG_WARNING,
                         "%s: failed to find parent\n", __func__);
            }
            goto fail;
        }
    }
    last++;
    res = fstatat(p->fd, last, &stat, AT_SYMLINK_NOFOLLOW);
    if (res == -1) {
        if (!retries) {
            fuse_log(FUSE_LOG_WARNING,
                     "%s: failed to stat last\n", __func__);
        }
        goto fail_unref;
    }
    if (stat.st_dev != inode->dev || stat.st_ino != inode->ino) {
        if (!retries) {
            fuse_log(FUSE_LOG_WARNING,
                     "%s: failed to match last\n", __func__);
        }
        goto fail_unref;
    }
    *parent = p;
    memmove(path, last, strlen(last) + 1);

    return 0;

fail_unref:
    unref_inode(lo, p, 1);
fail:
    if (retries) {
        retries--;
        goto retry;
    }
fail_noretry:
    errno = EIO;
    return -1;
}

static int utimensat_empty(struct lo_data *lo, struct lo_inode *inode,
                           const struct timespec *tv)
{
    int res;
    struct lo_inode *parent;
    char path[PATH_MAX];

    if (inode->is_symlink) {
        res = utimensat(inode->fd, "", tv, AT_EMPTY_PATH);
        if (res == -1 && errno == EINVAL) {
            /* Sorry, no race free way to set times on symlink. */
            if (lo->norace) {
                errno = EPERM;
            } else {
                goto fallback;
            }
        }
        return res;
    }
    sprintf(path, "/proc/self/fd/%i", inode->fd);

    return utimensat(AT_FDCWD, path, tv, 0);

fallback:
    res = lo_parent_and_name(lo, inode, path, &parent);
    if (res != -1) {
        res = utimensat(parent->fd, path, tv, AT_SYMLINK_NOFOLLOW);
        unref_inode(lo, parent, 1);
    }

    return res;
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
            sprintf(procname, "/proc/self/fd/%i", ifd);
            res = chmod(procname, attr->st_mode);
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
        if (fi) {
            res = ftruncate(fd, attr->st_size);
        } else {
            sprintf(procname, "/proc/self/fd/%i", ifd);
            res = truncate(procname, attr->st_size);
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
            res = utimensat_empty(lo, inode, tv);
        }
        if (res == -1) {
            goto out_err;
        }
    }

    return lo_getattr(req, ino, fi);

out_err:
    saverr = errno;
    fuse_reply_err(req, saverr);
}

static struct lo_inode *lo_find(struct lo_data *lo, struct stat *st)
{
    struct lo_inode *p;
    struct lo_inode *ret = NULL;

    pthread_mutex_lock(&lo->mutex);
    for (p = lo->root.next; p != &lo->root; p = p->next) {
        if (p->ino == st->st_ino && p->dev == st->st_dev) {
            assert(p->refcount > 0);
            ret = p;
            ret->refcount++;
            break;
        }
    }
    pthread_mutex_unlock(&lo->mutex);
    return ret;
}

static int lo_do_lookup(fuse_req_t req, fuse_ino_t parent, const char *name,
                        struct fuse_entry_param *e)
{
    int newfd;
    int res;
    int saverr;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode, *dir = lo_inode(req, parent);

    memset(e, 0, sizeof(*e));
    e->attr_timeout = lo->timeout;
    e->entry_timeout = lo->timeout;

    /* Do not allow escaping root directory */
    if (dir == &lo->root && strcmp(name, "..") == 0) {
        name = ".";
    }

    newfd = openat(lo_fd(req, parent), name, O_PATH | O_NOFOLLOW);
    if (newfd == -1) {
        goto out_err;
    }

    res = fstatat(newfd, "", &e->attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    if (res == -1) {
        goto out_err;
    }

    inode = lo_find(lo_data(req), &e->attr);
    if (inode) {
        close(newfd);
        newfd = -1;
    } else {
        struct lo_inode *prev, *next;

        saverr = ENOMEM;
        inode = calloc(1, sizeof(struct lo_inode));
        if (!inode) {
            goto out_err;
        }

        inode->is_symlink = S_ISLNK(e->attr.st_mode);
        inode->refcount = 1;
        inode->fd = newfd;
        inode->ino = e->attr.st_ino;
        inode->dev = e->attr.st_dev;

        pthread_mutex_lock(&lo->mutex);
        inode->fuse_ino = lo_add_inode_mapping(req, inode);
        prev = &lo->root;
        next = prev->next;
        next->prev = inode;
        inode->next = next;
        inode->prev = prev;
        prev->next = inode;
        pthread_mutex_unlock(&lo->mutex);
    }
    e->ino = inode->fuse_ino;

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n",
                 (unsigned long long)parent, name, (unsigned long long)e->ino);
    }

    return 0;

out_err:
    saverr = errno;
    if (newfd != -1) {
        close(newfd);
    }
    return saverr;
}

static void lo_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    struct fuse_entry_param e;
    int err;

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG, "lo_lookup(parent=%" PRIu64 ", name=%s)\n",
                 parent, name);
    }

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

    saverr = ENOMEM;

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

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n",
                 (unsigned long long)parent, name, (unsigned long long)e.ino);
    }

    fuse_reply_entry(req, &e);
    return;

out:
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

static int linkat_empty_nofollow(struct lo_data *lo, struct lo_inode *inode,
                                 int dfd, const char *name)
{
    int res;
    struct lo_inode *parent;
    char path[PATH_MAX];

    if (inode->is_symlink) {
        res = linkat(inode->fd, "", dfd, name, AT_EMPTY_PATH);
        if (res == -1 && (errno == ENOENT || errno == EINVAL)) {
            /* Sorry, no race free way to hard-link a symlink. */
            if (lo->norace) {
                errno = EPERM;
            } else {
                goto fallback;
            }
        }
        return res;
    }

    sprintf(path, "/proc/self/fd/%i", inode->fd);

    return linkat(AT_FDCWD, path, dfd, name, AT_SYMLINK_FOLLOW);

fallback:
    res = lo_parent_and_name(lo, inode, path, &parent);
    if (res != -1) {
        res = linkat(parent->fd, path, dfd, name, 0);
        unref_inode(lo, parent, 1);
    }

    return res;
}

static void lo_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t parent,
                    const char *name)
{
    int res;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;
    struct fuse_entry_param e;
    int saverr;

    if (!is_safe_path_component(name)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    memset(&e, 0, sizeof(struct fuse_entry_param));
    e.attr_timeout = lo->timeout;
    e.entry_timeout = lo->timeout;

    res = linkat_empty_nofollow(lo, inode, lo_fd(req, parent), name);
    if (res == -1) {
        goto out_err;
    }

    res = fstatat(inode->fd, "", &e.attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    if (res == -1) {
        goto out_err;
    }

    pthread_mutex_lock(&lo->mutex);
    inode->refcount++;
    pthread_mutex_unlock(&lo->mutex);
    e.ino = inode->fuse_ino;

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n",
                 (unsigned long long)parent, name, (unsigned long long)e.ino);
    }

    fuse_reply_entry(req, &e);
    return;

out_err:
    saverr = errno;
    fuse_reply_err(req, saverr);
}

static void lo_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int res;
    if (!is_safe_path_component(name)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    res = unlinkat(lo_fd(req, parent), name, AT_REMOVEDIR);

    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void lo_rename(fuse_req_t req, fuse_ino_t parent, const char *name,
                      fuse_ino_t newparent, const char *newname,
                      unsigned int flags)
{
    int res;

    if (!is_safe_path_component(name) || !is_safe_path_component(newname)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (flags) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    res = renameat(lo_fd(req, parent), name, lo_fd(req, newparent), newname);

    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void lo_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int res;

    if (!is_safe_path_component(name)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    res = unlinkat(lo_fd(req, parent), name, 0);

    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void unref_inode(struct lo_data *lo, struct lo_inode *inode, uint64_t n)
{
    if (!inode) {
        return;
    }

    pthread_mutex_lock(&lo->mutex);
    assert(inode->refcount >= n);
    inode->refcount -= n;
    if (!inode->refcount) {
        struct lo_inode *prev, *next;

        prev = inode->prev;
        next = inode->next;
        next->prev = prev;
        prev->next = next;

        lo_map_remove(&lo->ino_map, inode->fuse_ino);
        pthread_mutex_unlock(&lo->mutex);
        close(inode->fd);
        free(inode);
    } else {
        pthread_mutex_unlock(&lo->mutex);
    }
}

static void lo_forget_one(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup)
{
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;

    inode = lo_inode(req, ino);
    if (!inode) {
        return;
    }

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG, "  forget %lli %lli -%lli\n",
                 (unsigned long long)ino, (unsigned long long)inode->refcount,
                 (unsigned long long)nlookup);
    }

    unref_inode(lo, inode, nlookup);
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
    DIR *dp;
    struct dirent *entry;
    off_t offset;
};

static struct lo_dirp *lo_dirp(fuse_req_t req, struct fuse_file_info *fi)
{
    struct lo_data *lo = lo_data(req);
    struct lo_map_elem *elem;

    pthread_mutex_lock(&lo->mutex);
    elem = lo_map_get(&lo->dirp_map, fi->fh);
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

    pthread_mutex_lock(&lo->mutex);
    fh = lo_add_dirp_mapping(req, d);
    pthread_mutex_unlock(&lo->mutex);
    if (fh == -1) {
        goto out_err;
    }

    fi->fh = fh;
    if (lo->cache == CACHE_ALWAYS) {
        fi->keep_cache = 1;
    }
    fuse_reply_open(req, fi);
    return;

out_errno:
    error = errno;
out_err:
    if (d) {
        if (d->dp) {
            closedir(d->dp);
        }
        if (fd != -1) {
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
    struct lo_dirp *d;
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
            e.attr.st_ino = lo->root.ino;
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
    struct lo_dirp *d;

    (void)ino;

    d = lo_dirp(req, fi);
    if (!d) {
        fuse_reply_err(req, EBADF);
        return;
    }

    pthread_mutex_lock(&lo->mutex);
    lo_map_remove(&lo->dirp_map, fi->fh);
    pthread_mutex_unlock(&lo->mutex);

    closedir(d->dp);
    free(d);
    fuse_reply_err(req, 0);
}

static void lo_create(fuse_req_t req, fuse_ino_t parent, const char *name,
                      mode_t mode, struct fuse_file_info *fi)
{
    int fd;
    struct lo_data *lo = lo_data(req);
    struct fuse_entry_param e;
    int err;
    struct lo_cred old = {};

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG, "lo_create(parent=%" PRIu64 ", name=%s)\n",
                 parent, name);
    }

    if (!is_safe_path_component(name)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    err = lo_change_cred(req, &old);
    if (err) {
        goto out;
    }

    fd = openat(lo_fd(req, parent), name, (fi->flags | O_CREAT) & ~O_NOFOLLOW,
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
            fuse_reply_err(req, ENOMEM);
            return;
        }

        fi->fh = fh;
        err = lo_do_lookup(req, parent, name, &e);
    }
    if (lo->cache == CACHE_NEVER) {
        fi->direct_io = 1;
    } else if (lo->cache == CACHE_ALWAYS) {
        fi->keep_cache = 1;
    }

out:
    if (err) {
        fuse_reply_err(req, err);
    } else {
        fuse_reply_create(req, &e, fi);
    }
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
    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void lo_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    int fd;
    ssize_t fh;
    char buf[64];
    struct lo_data *lo = lo_data(req);

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG, "lo_open(ino=%" PRIu64 ", flags=%d)\n", ino,
                 fi->flags);
    }

    /*
     * With writeback cache, kernel may send read requests even
     * when userspace opened write-only
     */
    if (lo->writeback && (fi->flags & O_ACCMODE) == O_WRONLY) {
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
    if (lo->writeback && (fi->flags & O_APPEND)) {
        fi->flags &= ~O_APPEND;
    }

    sprintf(buf, "/proc/self/fd/%i", lo_fd(req, ino));
    fd = open(buf, fi->flags & ~O_NOFOLLOW);
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
    if (lo->cache == CACHE_NEVER) {
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
    int fd;

    (void)ino;

    fd = lo_fi_fd(req, fi);

    pthread_mutex_lock(&lo->mutex);
    lo_map_remove(&lo->fd_map, fi->fh);
    pthread_mutex_unlock(&lo->mutex);

    close(fd);
    fuse_reply_err(req, 0);
}

static void lo_flush(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    int res;
    (void)ino;
    res = close(dup(lo_fi_fd(req, fi)));
    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void lo_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                     struct fuse_file_info *fi)
{
    int res;
    (void)ino;
    int fd;
    char *buf;

    fuse_log(FUSE_LOG_DEBUG, "lo_fsync(ino=%" PRIu64 ", fi=0x%p)\n", ino,
             (void *)fi);

    if (!fi) {
        res = asprintf(&buf, "/proc/self/fd/%i", lo_fd(req, ino));
        if (res == -1) {
            return (void)fuse_reply_err(req, errno);
        }

        fd = open(buf, O_RDWR);
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

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG,
                 "lo_read(ino=%" PRIu64 ", size=%zd, "
                 "off=%lu)\n",
                 ino, size, (unsigned long)offset);
    }

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

    out_buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
    out_buf.buf[0].fd = lo_fi_fd(req, fi);
    out_buf.buf[0].pos = off;

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG,
                 "lo_write(ino=%" PRIu64 ", size=%zd, off=%lu)\n", ino,
                 out_buf.buf[0].size, (unsigned long)off);
    }

    res = fuse_buf_copy(&out_buf, in_buf);
    if (res < 0) {
        fuse_reply_err(req, -res);
    } else {
        fuse_reply_write(req, (size_t)res);
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
    char *value = NULL;
    char procname[64];
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    saverr = ENOSYS;
    if (!lo_data(req)->xattr) {
        goto out;
    }

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG,
                 "lo_getxattr(ino=%" PRIu64 ", name=%s size=%zd)\n", ino, name,
                 size);
    }

    if (inode->is_symlink) {
        /* Sorry, no race free way to getxattr on symlink. */
        saverr = EPERM;
        goto out;
    }

    sprintf(procname, "/proc/self/fd/%i", inode->fd);

    if (size) {
        value = malloc(size);
        if (!value) {
            goto out_err;
        }

        ret = getxattr(procname, name, value, size);
        if (ret == -1) {
            goto out_err;
        }
        saverr = 0;
        if (ret == 0) {
            goto out;
        }

        fuse_reply_buf(req, value, ret);
    } else {
        ret = getxattr(procname, name, NULL, 0);
        if (ret == -1) {
            goto out_err;
        }

        fuse_reply_xattr(req, ret);
    }
out_free:
    free(value);
    return;

out_err:
    saverr = errno;
out:
    fuse_reply_err(req, saverr);
    goto out_free;
}

static void lo_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size)
{
    char *value = NULL;
    char procname[64];
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    saverr = ENOSYS;
    if (!lo_data(req)->xattr) {
        goto out;
    }

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG, "lo_listxattr(ino=%" PRIu64 ", size=%zd)\n",
                 ino, size);
    }

    if (inode->is_symlink) {
        /* Sorry, no race free way to listxattr on symlink. */
        saverr = EPERM;
        goto out;
    }

    sprintf(procname, "/proc/self/fd/%i", inode->fd);

    if (size) {
        value = malloc(size);
        if (!value) {
            goto out_err;
        }

        ret = listxattr(procname, value, size);
        if (ret == -1) {
            goto out_err;
        }
        saverr = 0;
        if (ret == 0) {
            goto out;
        }

        fuse_reply_buf(req, value, ret);
    } else {
        ret = listxattr(procname, NULL, 0);
        if (ret == -1) {
            goto out_err;
        }

        fuse_reply_xattr(req, ret);
    }
out_free:
    free(value);
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
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    saverr = ENOSYS;
    if (!lo_data(req)->xattr) {
        goto out;
    }

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG,
                 "lo_setxattr(ino=%" PRIu64 ", name=%s value=%s size=%zd)\n",
                 ino, name, value, size);
    }

    if (inode->is_symlink) {
        /* Sorry, no race free way to setxattr on symlink. */
        saverr = EPERM;
        goto out;
    }

    sprintf(procname, "/proc/self/fd/%i", inode->fd);

    ret = setxattr(procname, name, value, size, flags);
    saverr = ret == -1 ? errno : 0;

out:
    fuse_reply_err(req, saverr);
}

static void lo_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name)
{
    char procname[64];
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    saverr = ENOSYS;
    if (!lo_data(req)->xattr) {
        goto out;
    }

    if (lo_debug(req)) {
        fuse_log(FUSE_LOG_DEBUG, "lo_removexattr(ino=%" PRIu64 ", name=%s)\n",
                 ino, name);
    }

    if (inode->is_symlink) {
        /* Sorry, no race free way to setxattr on symlink. */
        saverr = EPERM;
        goto out;
    }

    sprintf(procname, "/proc/self/fd/%i", inode->fd);

    ret = removexattr(procname, name);
    saverr = ret == -1 ? errno : 0;

out:
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
        fuse_reply_err(req, -errno);
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
};

/* Print vhost-user.json backend program capabilities */
static void print_capabilities(void)
{
    printf("{\n");
    printf("  \"type\": \"fs\"\n");
    printf("}\n");
}

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_session *se;
    struct fuse_cmdline_opts opts;
    struct lo_data lo = { .debug = 0, .writeback = 0 };
    struct lo_map_elem *root_elem;
    int ret = -1;

    /* Don't mask creation mode, kernel already did that */
    umask(0);

    pthread_mutex_init(&lo.mutex, NULL);
    lo.root.next = lo.root.prev = &lo.root;
    lo.root.fd = -1;
    lo.root.fuse_ino = FUSE_ROOT_ID;
    lo.cache = CACHE_NORMAL;

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
        return 1;
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
        return 1;
    }

    lo.debug = opts.debug;
    lo.root.refcount = 2;
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
        lo.source = "/";
    }
    lo.root.is_symlink = false;
    if (!lo.timeout_set) {
        switch (lo.cache) {
        case CACHE_NEVER:
            lo.timeout = 0.0;
            break;

        case CACHE_NORMAL:
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

    lo.root.fd = open(lo.source, O_PATH);
    if (lo.root.fd == -1) {
        fuse_log(FUSE_LOG_ERR, "open(\"%s\", O_PATH): %m\n", lo.source);
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

    /* Block until ctrl+c or fusermount -u */
    ret = virtio_loop(se);

    fuse_session_unmount(se);
err_out3:
    fuse_remove_signal_handlers(se);
err_out2:
    fuse_session_destroy(se);
err_out1:
    fuse_opt_free_args(&args);

    lo_map_destroy(&lo.fd_map);
    lo_map_destroy(&lo.dirp_map);
    lo_map_destroy(&lo.ino_map);

    if (lo.root.fd >= 0) {
        close(lo.root.fd);
    }

    return ret ? 1 : 0;
}
