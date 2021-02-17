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
#include "standard-headers/linux/fuse.h"
#include <cap-ng.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <syslog.h>

#include "qemu/cutils.h"
#include "passthrough_helpers.h"
#include "passthrough_seccomp.h"

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
    uint64_t mnt_id;
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
     * released by a FUSE_FORGET request.
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

enum {
    SANDBOX_NAMESPACE,
    SANDBOX_CHROOT,
};

typedef struct xattr_map_entry {
    char *key;
    char *prepend;
    unsigned int flags;
} XattrMapEntry;

struct lo_data {
    pthread_mutex_t mutex;
    int sandbox;
    int debug;
    int writeback;
    int flock;
    int posix_lock;
    int xattr;
    char *xattrmap;
    char *source;
    char *modcaps;
    double timeout;
    int cache;
    int timeout_set;
    int readdirplus_set;
    int readdirplus_clear;
    int allow_direct_io;
    int announce_submounts;
    bool use_statx;
    struct lo_inode root;
    GHashTable *inodes; /* protected by lo->mutex */
    struct lo_map ino_map; /* protected by lo->mutex */
    struct lo_map dirp_map; /* protected by lo->mutex */
    struct lo_map fd_map; /* protected by lo->mutex */
    XattrMapEntry *xattr_map_list;
    size_t xattr_map_nentries;

    /* An O_PATH file descriptor to /proc/self/fd/ */
    int proc_self_fd;
    int user_killpriv_v2, killpriv_v2;
};

static const struct fuse_opt lo_opts[] = {
    { "sandbox=namespace",
      offsetof(struct lo_data, sandbox),
      SANDBOX_NAMESPACE },
    { "sandbox=chroot",
      offsetof(struct lo_data, sandbox),
      SANDBOX_CHROOT },
    { "writeback", offsetof(struct lo_data, writeback), 1 },
    { "no_writeback", offsetof(struct lo_data, writeback), 0 },
    { "source=%s", offsetof(struct lo_data, source), 0 },
    { "flock", offsetof(struct lo_data, flock), 1 },
    { "no_flock", offsetof(struct lo_data, flock), 0 },
    { "posix_lock", offsetof(struct lo_data, posix_lock), 1 },
    { "no_posix_lock", offsetof(struct lo_data, posix_lock), 0 },
    { "xattr", offsetof(struct lo_data, xattr), 1 },
    { "no_xattr", offsetof(struct lo_data, xattr), 0 },
    { "xattrmap=%s", offsetof(struct lo_data, xattrmap), 0 },
    { "modcaps=%s", offsetof(struct lo_data, modcaps), 0 },
    { "timeout=%lf", offsetof(struct lo_data, timeout), 0 },
    { "timeout=", offsetof(struct lo_data, timeout_set), 1 },
    { "cache=none", offsetof(struct lo_data, cache), CACHE_NONE },
    { "cache=auto", offsetof(struct lo_data, cache), CACHE_AUTO },
    { "cache=always", offsetof(struct lo_data, cache), CACHE_ALWAYS },
    { "readdirplus", offsetof(struct lo_data, readdirplus_set), 1 },
    { "no_readdirplus", offsetof(struct lo_data, readdirplus_clear), 1 },
    { "allow_direct_io", offsetof(struct lo_data, allow_direct_io), 1 },
    { "no_allow_direct_io", offsetof(struct lo_data, allow_direct_io), 0 },
    { "announce_submounts", offsetof(struct lo_data, announce_submounts), 1 },
    { "killpriv_v2", offsetof(struct lo_data, user_killpriv_v2), 1 },
    { "no_killpriv_v2", offsetof(struct lo_data, user_killpriv_v2), 0 },
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

static struct lo_inode *lo_find(struct lo_data *lo, struct stat *st,
                                uint64_t mnt_id);

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
static ssize_t lo_add_fd_mapping(struct lo_data *lo, int fd)
{
    struct lo_map_elem *elem;

    elem = lo_map_alloc_elem(&lo->fd_map);
    if (!elem) {
        return -1;
    }

    elem->fd = fd;
    return elem - lo->fd_map.elems;
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

/*
 * Open a file descriptor for an inode. Returns -EBADF if the inode is not a
 * regular file or a directory.
 *
 * Use this helper function instead of raw openat(2) to prevent security issues
 * when a malicious client opens special files such as block device nodes.
 * Symlink inodes are also rejected since symlinks must already have been
 * traversed on the client side.
 */
static int lo_inode_open(struct lo_data *lo, struct lo_inode *inode,
                         int open_flags)
{
    g_autofree char *fd_str = g_strdup_printf("%d", inode->fd);
    int fd;

    if (!S_ISREG(inode->filetype) && !S_ISDIR(inode->filetype)) {
        return -EBADF;
    }

    /*
     * The file is a symlink so O_NOFOLLOW must be ignored. We checked earlier
     * that the inode is not a special file but if an external process races
     * with us then symlinks are traversed here. It is not possible to escape
     * the shared directory since it is mounted as "/" though.
     */
    fd = openat(lo->proc_self_fd, fd_str, open_flags & ~O_NOFOLLOW);
    if (fd < 0) {
        return -errno;
    }
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

    if (!(conn->capable & FUSE_CAP_SUBMOUNTS) && lo->announce_submounts) {
        fuse_log(FUSE_LOG_WARNING, "lo_init: Cannot announce submounts, client "
                 "does not support it\n");
        lo->announce_submounts = false;
    }

    if (lo->user_killpriv_v2 == 1) {
        /*
         * User explicitly asked for this option. Enable it unconditionally.
         * If connection does not have this capability, it should fail
         * in fuse_lowlevel.c
         */
        fuse_log(FUSE_LOG_DEBUG, "lo_init: enabling killpriv_v2\n");
        conn->want |= FUSE_CAP_HANDLE_KILLPRIV_V2;
        lo->killpriv_v2 = 1;
    } else if (lo->user_killpriv_v2 == -1 &&
               conn->capable & FUSE_CAP_HANDLE_KILLPRIV_V2) {
        /*
         * User did not specify a value for killpriv_v2. By default enable it
         * if connection offers this capability
         */
        fuse_log(FUSE_LOG_DEBUG, "lo_init: enabling killpriv_v2\n");
        conn->want |= FUSE_CAP_HANDLE_KILLPRIV_V2;
        lo->killpriv_v2 = 1;
    } else {
        /*
         * Either user specified to disable killpriv_v2, or connection does
         * not offer this capability. Disable killpriv_v2 in both the cases
         */
        fuse_log(FUSE_LOG_DEBUG, "lo_init: disabling killpriv_v2\n");
        conn->want &= ~FUSE_CAP_HANDLE_KILLPRIV_V2;
        lo->killpriv_v2 = 0;
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
    int fd = -1;

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
            saverr = errno;
            goto out_err;
        }
    }
    if (valid & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)) {
        uid_t uid = (valid & FUSE_SET_ATTR_UID) ? attr->st_uid : (uid_t)-1;
        gid_t gid = (valid & FUSE_SET_ATTR_GID) ? attr->st_gid : (gid_t)-1;

        res = fchownat(ifd, "", uid, gid, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
        if (res == -1) {
            saverr = errno;
            goto out_err;
        }
    }
    if (valid & FUSE_SET_ATTR_SIZE) {
        int truncfd;
        bool kill_suidgid;
        bool cap_fsetid_dropped = false;

        kill_suidgid = lo->killpriv_v2 && (valid & FUSE_SET_ATTR_KILL_SUIDGID);
        if (fi) {
            truncfd = fd;
        } else {
            truncfd = lo_inode_open(lo, inode, O_RDWR);
            if (truncfd < 0) {
                saverr = -truncfd;
                goto out_err;
            }
        }

        if (kill_suidgid) {
            res = drop_effective_cap("FSETID", &cap_fsetid_dropped);
            if (res != 0) {
                saverr = res;
                if (!fi) {
                    close(truncfd);
                }
                goto out_err;
            }
        }

        res = ftruncate(truncfd, attr->st_size);
        saverr = res == -1 ? errno : 0;

        if (cap_fsetid_dropped) {
            if (gain_effective_cap("FSETID")) {
                fuse_log(FUSE_LOG_ERR, "Failed to gain CAP_FSETID\n");
            }
        }
        if (!fi) {
            close(truncfd);
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
            saverr = errno;
            goto out_err;
        }
    }
    lo_inode_put(lo, &inode);

    return lo_getattr(req, ino, fi);

out_err:
    lo_inode_put(lo, &inode);
    fuse_reply_err(req, saverr);
}

static struct lo_inode *lo_find(struct lo_data *lo, struct stat *st,
                                uint64_t mnt_id)
{
    struct lo_inode *p;
    struct lo_key key = {
        .ino = st->st_ino,
        .dev = st->st_dev,
        .mnt_id = mnt_id,
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

static int do_statx(struct lo_data *lo, int dirfd, const char *pathname,
                    struct stat *statbuf, int flags, uint64_t *mnt_id)
{
    int res;

#if defined(CONFIG_STATX) && defined(STATX_MNT_ID)
    if (lo->use_statx) {
        struct statx statxbuf;

        res = statx(dirfd, pathname, flags, STATX_BASIC_STATS | STATX_MNT_ID,
                    &statxbuf);
        if (!res) {
            memset(statbuf, 0, sizeof(*statbuf));
            statbuf->st_dev = makedev(statxbuf.stx_dev_major,
                                      statxbuf.stx_dev_minor);
            statbuf->st_ino = statxbuf.stx_ino;
            statbuf->st_mode = statxbuf.stx_mode;
            statbuf->st_nlink = statxbuf.stx_nlink;
            statbuf->st_uid = statxbuf.stx_uid;
            statbuf->st_gid = statxbuf.stx_gid;
            statbuf->st_rdev = makedev(statxbuf.stx_rdev_major,
                                       statxbuf.stx_rdev_minor);
            statbuf->st_size = statxbuf.stx_size;
            statbuf->st_blksize = statxbuf.stx_blksize;
            statbuf->st_blocks = statxbuf.stx_blocks;
            statbuf->st_atim.tv_sec = statxbuf.stx_atime.tv_sec;
            statbuf->st_atim.tv_nsec = statxbuf.stx_atime.tv_nsec;
            statbuf->st_mtim.tv_sec = statxbuf.stx_mtime.tv_sec;
            statbuf->st_mtim.tv_nsec = statxbuf.stx_mtime.tv_nsec;
            statbuf->st_ctim.tv_sec = statxbuf.stx_ctime.tv_sec;
            statbuf->st_ctim.tv_nsec = statxbuf.stx_ctime.tv_nsec;

            if (statxbuf.stx_mask & STATX_MNT_ID) {
                *mnt_id = statxbuf.stx_mnt_id;
            } else {
                *mnt_id = 0;
            }
            return 0;
        } else if (errno != ENOSYS) {
            return -1;
        }
        lo->use_statx = false;
        /* fallback */
    }
#endif
    res = fstatat(dirfd, pathname, statbuf, flags);
    if (res == -1) {
        return -1;
    }
    *mnt_id = 0;

    return 0;
}

/*
 * Increments nlookup on the inode on success. unref_inode_lolocked() must be
 * called eventually to decrement nlookup again. If inodep is non-NULL, the
 * inode pointer is stored and the caller must call lo_inode_put().
 */
static int lo_do_lookup(fuse_req_t req, fuse_ino_t parent, const char *name,
                        struct fuse_entry_param *e,
                        struct lo_inode **inodep)
{
    int newfd;
    int res;
    int saverr;
    uint64_t mnt_id;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode = NULL;
    struct lo_inode *dir = lo_inode(req, parent);

    if (inodep) {
        *inodep = NULL; /* in case there is an error */
    }

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

    res = do_statx(lo, newfd, "", &e->attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW,
                   &mnt_id);
    if (res == -1) {
        goto out_err;
    }

    if (S_ISDIR(e->attr.st_mode) && lo->announce_submounts &&
        (e->attr.st_dev != dir->key.dev || mnt_id != dir->key.mnt_id)) {
        e->attr_flags |= FUSE_ATTR_SUBMOUNT;
    }

    inode = lo_find(lo, &e->attr, mnt_id);
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
        inode->key.mnt_id = mnt_id;
        if (lo->posix_lock) {
            pthread_mutex_init(&inode->plock_mutex, NULL);
            inode->posix_locks = g_hash_table_new_full(
                g_direct_hash, g_direct_equal, NULL, posix_locks_value_destroy);
        }
        pthread_mutex_lock(&lo->mutex);
        inode->fuse_ino = lo_add_inode_mapping(req, inode);
        g_hash_table_insert(lo->inodes, &inode->key, inode);
        pthread_mutex_unlock(&lo->mutex);
    }
    e->ino = inode->fuse_ino;

    /* Transfer ownership of inode pointer to caller or drop it */
    if (inodep) {
        *inodep = inode;
    } else {
        lo_inode_put(lo, &inode);
    }

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

    err = lo_do_lookup(req, parent, name, &e, NULL);
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

    saverr = lo_do_lookup(req, parent, name, &e, NULL);
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
    uint64_t mnt_id;
    struct stat attr;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *dir = lo_inode(req, parent);

    if (!dir) {
        return NULL;
    }

    res = do_statx(lo, dir->fd, name, &attr,
                   AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW, &mnt_id);
    lo_inode_put(lo, &dir);
    if (res == -1) {
        return NULL;
    }

    return lo_find(lo, &attr, mnt_id);
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
        if (lo->posix_lock) {
            if (g_hash_table_size(inode->posix_locks)) {
                fuse_log(FUSE_LOG_WARNING, "Hash table is not empty\n");
            }
            g_hash_table_destroy(inode->posix_locks);
            pthread_mutex_destroy(&inode->plock_mutex);
        }
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
                err = lo_do_lookup(req, ino, name, &e, NULL);
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

static void update_open_flags(int writeback, int allow_direct_io,
                              struct fuse_file_info *fi)
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
     * cache on host as well. Therefore, we discard it by default
     * ('-o no_allow_direct_io'). If somebody needs that behavior,
     * the '-o allow_direct_io' option should be set.
     */
    if (!allow_direct_io) {
        fi->flags &= ~O_DIRECT;
    }
}

/*
 * Open a regular file, set up an fd mapping, and fill out the struct
 * fuse_file_info for it. If existing_fd is not negative, use that fd instead
 * opening a new one. Takes ownership of existing_fd.
 *
 * Returns 0 on success or a positive errno.
 */
static int lo_do_open(struct lo_data *lo, struct lo_inode *inode,
                      int existing_fd, struct fuse_file_info *fi)
{
    ssize_t fh;
    int fd = existing_fd;
    int err;
    bool cap_fsetid_dropped = false;
    bool kill_suidgid = lo->killpriv_v2 && fi->kill_priv;

    update_open_flags(lo->writeback, lo->allow_direct_io, fi);

    if (fd < 0) {
        if (kill_suidgid) {
            err = drop_effective_cap("FSETID", &cap_fsetid_dropped);
            if (err) {
                return err;
            }
        }

        fd = lo_inode_open(lo, inode, fi->flags);

        if (cap_fsetid_dropped) {
            if (gain_effective_cap("FSETID")) {
                fuse_log(FUSE_LOG_ERR, "Failed to gain CAP_FSETID\n");
            }
        }
        if (fd < 0) {
            return -fd;
        }
    }

    pthread_mutex_lock(&lo->mutex);
    fh = lo_add_fd_mapping(lo, fd);
    pthread_mutex_unlock(&lo->mutex);
    if (fh == -1) {
        close(fd);
        return ENOMEM;
    }

    fi->fh = fh;
    if (lo->cache == CACHE_NONE) {
        fi->direct_io = 1;
    } else if (lo->cache == CACHE_ALWAYS) {
        fi->keep_cache = 1;
    }
    return 0;
}

static void lo_create(fuse_req_t req, fuse_ino_t parent, const char *name,
                      mode_t mode, struct fuse_file_info *fi)
{
    int fd = -1;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *parent_inode;
    struct lo_inode *inode = NULL;
    struct fuse_entry_param e;
    int err;
    struct lo_cred old = {};

    fuse_log(FUSE_LOG_DEBUG, "lo_create(parent=%" PRIu64 ", name=%s)"
             " kill_priv=%d\n", parent, name, fi->kill_priv);

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

    update_open_flags(lo->writeback, lo->allow_direct_io, fi);

    /* Try to create a new file but don't open existing files */
    fd = openat(parent_inode->fd, name, fi->flags | O_CREAT | O_EXCL, mode);
    err = fd == -1 ? errno : 0;

    lo_restore_cred(&old);

    /* Ignore the error if file exists and O_EXCL was not given */
    if (err && (err != EEXIST || (fi->flags & O_EXCL))) {
        goto out;
    }

    err = lo_do_lookup(req, parent, name, &e, &inode);
    if (err) {
        goto out;
    }

    err = lo_do_open(lo, inode, fd, fi);
    fd = -1; /* lo_do_open() takes ownership of fd */
    if (err) {
        /* Undo lo_do_lookup() nlookup ref */
        unref_inode_lolocked(lo, inode, 1);
    }

out:
    lo_inode_put(lo, &inode);
    lo_inode_put(lo, &parent_inode);

    if (err) {
        if (fd >= 0) {
            close(fd);
        }

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
    /* TODO: What if file is not writable? */
    fd = lo_inode_open(lo, inode, O_RDWR);
    if (fd < 0) {
        *err = -fd;
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

    if (!lo->posix_lock) {
        fuse_reply_err(req, ENOSYS);
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

    if (!lo->posix_lock) {
        fuse_reply_err(req, ENOSYS);
        return;
    }

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
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode = lo_inode(req, ino);
    int err;

    fuse_log(FUSE_LOG_DEBUG, "lo_open(ino=%" PRIu64 ", flags=%d, kill_priv=%d)"
             "\n", ino, fi->flags, fi->kill_priv);

    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    err = lo_do_open(lo, inode, -1, fi);
    lo_inode_put(lo, &inode);
    if (err) {
        fuse_reply_err(req, err);
    } else {
        fuse_reply_open(req, fi);
    }
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
    struct lo_data *lo = lo_data(req);

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    if (!S_ISREG(inode->filetype)) {
        lo_inode_put(lo, &inode);
        fuse_reply_err(req, EBADF);
        return;
    }

    /* An fd is going away. Cleanup associated posix locks */
    if (lo->posix_lock) {
        pthread_mutex_lock(&inode->plock_mutex);
        g_hash_table_remove(inode->posix_locks,
            GUINT_TO_POINTER(fi->lock_owner));
        pthread_mutex_unlock(&inode->plock_mutex);
    }
    res = close(dup(lo_fi_fd(req, fi)));
    lo_inode_put(lo, &inode);
    fuse_reply_err(req, res == -1 ? errno : 0);
}

static void lo_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                     struct fuse_file_info *fi)
{
    struct lo_inode *inode = lo_inode(req, ino);
    struct lo_data *lo = lo_data(req);
    int res;
    int fd;

    fuse_log(FUSE_LOG_DEBUG, "lo_fsync(ino=%" PRIu64 ", fi=0x%p)\n", ino,
             (void *)fi);

    if (!inode) {
        fuse_reply_err(req, EBADF);
        return;
    }

    if (!fi) {
        fd = lo_inode_open(lo, inode, O_RDWR);
        if (fd < 0) {
            res = -fd;
            goto out;
        }
    } else {
        fd = lo_fi_fd(req, fi);
    }

    if (datasync) {
        res = fdatasync(fd) == -1 ? errno : 0;
    } else {
        res = fsync(fd) == -1 ? errno : 0;
    }
    if (!fi) {
        close(fd);
    }
out:
    lo_inode_put(lo, &inode);
    fuse_reply_err(req, res);
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
             "lo_write_buf(ino=%" PRIu64 ", size=%zd, off=%lu kill_priv=%d)\n",
             ino, out_buf.buf[0].size, (unsigned long)off, fi->kill_priv);

    /*
     * If kill_priv is set, drop CAP_FSETID which should lead to kernel
     * clearing setuid/setgid on file. Note, for WRITE, we need to do
     * this even if killpriv_v2 is not enabled. fuse direct write path
     * relies on this.
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

/* types */
/*
 * Exit; process attribute unmodified if matched.
 * An empty key applies to all.
 */
#define XATTR_MAP_FLAG_OK      (1 <<  0)
/*
 * The attribute is unwanted;
 * EPERM on write, hidden on read.
 */
#define XATTR_MAP_FLAG_BAD     (1 <<  1)
/*
 * For attr that start with 'key' prepend 'prepend'
 * 'key' may be empty to prepend for all attrs
 * key is defined from set/remove point of view.
 * Automatically reversed on read
 */
#define XATTR_MAP_FLAG_PREFIX  (1 <<  2)

/* scopes */
/* Apply rule to get/set/remove */
#define XATTR_MAP_FLAG_CLIENT  (1 << 16)
/* Apply rule to list */
#define XATTR_MAP_FLAG_SERVER  (1 << 17)
/* Apply rule to all */
#define XATTR_MAP_FLAG_ALL   (XATTR_MAP_FLAG_SERVER | XATTR_MAP_FLAG_CLIENT)

static void add_xattrmap_entry(struct lo_data *lo,
                               const XattrMapEntry *new_entry)
{
    XattrMapEntry *res = g_realloc_n(lo->xattr_map_list,
                                     lo->xattr_map_nentries + 1,
                                     sizeof(XattrMapEntry));
    res[lo->xattr_map_nentries++] = *new_entry;

    lo->xattr_map_list = res;
}

static void free_xattrmap(struct lo_data *lo)
{
    XattrMapEntry *map = lo->xattr_map_list;
    size_t i;

    if (!map) {
        return;
    }

    for (i = 0; i < lo->xattr_map_nentries; i++) {
        g_free(map[i].key);
        g_free(map[i].prepend);
    };

    g_free(map);
    lo->xattr_map_list = NULL;
    lo->xattr_map_nentries = -1;
}

/*
 * Handle the 'map' type, which is sugar for a set of commands
 * for the common case of prefixing a subset or everything,
 * and allowing anything not prefixed through.
 * It must be the last entry in the stream, although there
 * can be other entries before it.
 * The form is:
 *    :map:key:prefix:
 *
 * key maybe empty in which case all entries are prefixed.
 */
static void parse_xattrmap_map(struct lo_data *lo,
                               const char *rule, char sep)
{
    const char *tmp;
    char *key;
    char *prefix;
    XattrMapEntry tmp_entry;

    if (*rule != sep) {
        fuse_log(FUSE_LOG_ERR,
                 "%s: Expecting '%c' after 'map' keyword, found '%c'\n",
                 __func__, sep, *rule);
        exit(1);
    }

    rule++;

    /* At start of 'key' field */
    tmp = strchr(rule, sep);
    if (!tmp) {
        fuse_log(FUSE_LOG_ERR,
                 "%s: Missing '%c' at end of key field in map rule\n",
                 __func__, sep);
        exit(1);
    }

    key = g_strndup(rule, tmp - rule);
    rule = tmp + 1;

    /* At start of prefix field */
    tmp = strchr(rule, sep);
    if (!tmp) {
        fuse_log(FUSE_LOG_ERR,
                 "%s: Missing '%c' at end of prefix field in map rule\n",
                 __func__, sep);
        exit(1);
    }

    prefix = g_strndup(rule, tmp - rule);
    rule = tmp + 1;

    /*
     * This should be the end of the string, we don't allow
     * any more commands after 'map'.
     */
    if (*rule) {
        fuse_log(FUSE_LOG_ERR,
                 "%s: Expecting end of command after map, found '%c'\n",
                 __func__, *rule);
        exit(1);
    }

    /* 1st: Prefix matches/everything */
    tmp_entry.flags = XATTR_MAP_FLAG_PREFIX | XATTR_MAP_FLAG_ALL;
    tmp_entry.key = g_strdup(key);
    tmp_entry.prepend = g_strdup(prefix);
    add_xattrmap_entry(lo, &tmp_entry);

    if (!*key) {
        /* Prefix all case */

        /* 2nd: Hide any non-prefixed entries on the host */
        tmp_entry.flags = XATTR_MAP_FLAG_BAD | XATTR_MAP_FLAG_ALL;
        tmp_entry.key = g_strdup("");
        tmp_entry.prepend = g_strdup("");
        add_xattrmap_entry(lo, &tmp_entry);
    } else {
        /* Prefix matching case */

        /* 2nd: Hide non-prefixed but matching entries on the host */
        tmp_entry.flags = XATTR_MAP_FLAG_BAD | XATTR_MAP_FLAG_SERVER;
        tmp_entry.key = g_strdup(""); /* Not used */
        tmp_entry.prepend = g_strdup(key);
        add_xattrmap_entry(lo, &tmp_entry);

        /* 3rd: Stop the client accessing prefixed attributes directly */
        tmp_entry.flags = XATTR_MAP_FLAG_BAD | XATTR_MAP_FLAG_CLIENT;
        tmp_entry.key = g_strdup(prefix);
        tmp_entry.prepend = g_strdup(""); /* Not used */
        add_xattrmap_entry(lo, &tmp_entry);

        /* 4th: Everything else is OK */
        tmp_entry.flags = XATTR_MAP_FLAG_OK | XATTR_MAP_FLAG_ALL;
        tmp_entry.key = g_strdup("");
        tmp_entry.prepend = g_strdup("");
        add_xattrmap_entry(lo, &tmp_entry);
    }

    g_free(key);
    g_free(prefix);
}

static void parse_xattrmap(struct lo_data *lo)
{
    const char *map = lo->xattrmap;
    const char *tmp;

    lo->xattr_map_nentries = 0;
    while (*map) {
        XattrMapEntry tmp_entry;
        char sep;

        if (isspace(*map)) {
            map++;
            continue;
        }
        /* The separator is the first non-space of the rule */
        sep = *map++;
        if (!sep) {
            break;
        }

        tmp_entry.flags = 0;
        /* Start of 'type' */
        if (strstart(map, "prefix", &map)) {
            tmp_entry.flags |= XATTR_MAP_FLAG_PREFIX;
        } else if (strstart(map, "ok", &map)) {
            tmp_entry.flags |= XATTR_MAP_FLAG_OK;
        } else if (strstart(map, "bad", &map)) {
            tmp_entry.flags |= XATTR_MAP_FLAG_BAD;
        } else if (strstart(map, "map", &map)) {
            /*
             * map is sugar that adds a number of rules, and must be
             * the last entry.
             */
            parse_xattrmap_map(lo, map, sep);
            return;
        } else {
            fuse_log(FUSE_LOG_ERR,
                     "%s: Unexpected type;"
                     "Expecting 'prefix', 'ok', 'bad' or 'map' in rule %zu\n",
                     __func__, lo->xattr_map_nentries);
            exit(1);
        }

        if (*map++ != sep) {
            fuse_log(FUSE_LOG_ERR,
                     "%s: Missing '%c' at end of type field of rule %zu\n",
                     __func__, sep, lo->xattr_map_nentries);
            exit(1);
        }

        /* Start of 'scope' */
        if (strstart(map, "client", &map)) {
            tmp_entry.flags |= XATTR_MAP_FLAG_CLIENT;
        } else if (strstart(map, "server", &map)) {
            tmp_entry.flags |= XATTR_MAP_FLAG_SERVER;
        } else if (strstart(map, "all", &map)) {
            tmp_entry.flags |= XATTR_MAP_FLAG_ALL;
        } else {
            fuse_log(FUSE_LOG_ERR,
                     "%s: Unexpected scope;"
                     " Expecting 'client', 'server', or 'all', in rule %zu\n",
                     __func__, lo->xattr_map_nentries);
            exit(1);
        }

        if (*map++ != sep) {
            fuse_log(FUSE_LOG_ERR,
                     "%s: Expecting '%c' found '%c'"
                     " after scope in rule %zu\n",
                     __func__, sep, *map, lo->xattr_map_nentries);
            exit(1);
        }

        /* At start of 'key' field */
        tmp = strchr(map, sep);
        if (!tmp) {
            fuse_log(FUSE_LOG_ERR,
                     "%s: Missing '%c' at end of key field of rule %zu",
                     __func__, sep, lo->xattr_map_nentries);
            exit(1);
        }
        tmp_entry.key = g_strndup(map, tmp - map);
        map = tmp + 1;

        /* At start of 'prepend' field */
        tmp = strchr(map, sep);
        if (!tmp) {
            fuse_log(FUSE_LOG_ERR,
                     "%s: Missing '%c' at end of prepend field of rule %zu",
                     __func__, sep, lo->xattr_map_nentries);
            exit(1);
        }
        tmp_entry.prepend = g_strndup(map, tmp - map);
        map = tmp + 1;

        add_xattrmap_entry(lo, &tmp_entry);
        /* End of rule - go around again for another rule */
    }

    if (!lo->xattr_map_nentries) {
        fuse_log(FUSE_LOG_ERR, "Empty xattr map\n");
        exit(1);
    }
}

/*
 * For use with getxattr/setxattr/removexattr, where the client
 * gives us a name and we may need to choose a different one.
 * Allocates a buffer for the result placing it in *out_name.
 *   If there's no change then *out_name is not set.
 * Returns 0 on success
 * Can return -EPERM to indicate we block a given attribute
 *   (in which case out_name is not allocated)
 * Can return -ENOMEM to indicate out_name couldn't be allocated.
 */
static int xattr_map_client(const struct lo_data *lo, const char *client_name,
                            char **out_name)
{
    size_t i;
    for (i = 0; i < lo->xattr_map_nentries; i++) {
        const XattrMapEntry *cur_entry = lo->xattr_map_list + i;

        if ((cur_entry->flags & XATTR_MAP_FLAG_CLIENT) &&
            (strstart(client_name, cur_entry->key, NULL))) {
            if (cur_entry->flags & XATTR_MAP_FLAG_BAD) {
                return -EPERM;
            }
            if (cur_entry->flags & XATTR_MAP_FLAG_OK) {
                /* Unmodified name */
                return 0;
            }
            if (cur_entry->flags & XATTR_MAP_FLAG_PREFIX) {
                *out_name = g_try_malloc(strlen(client_name) +
                                         strlen(cur_entry->prepend) + 1);
                if (!*out_name) {
                    return -ENOMEM;
                }
                sprintf(*out_name, "%s%s", cur_entry->prepend, client_name);
                return 0;
            }
        }
    }

    return -EPERM;
}

/*
 * For use with listxattr where the server fs gives us a name and we may need
 * to sanitize this for the client.
 * Returns a pointer to the result in *out_name
 *   This is always the original string or the current string with some prefix
 *   removed; no reallocation is done.
 * Returns 0 on success
 * Can return -ENODATA to indicate the name should be dropped from the list.
 */
static int xattr_map_server(const struct lo_data *lo, const char *server_name,
                            const char **out_name)
{
    size_t i;
    const char *end;

    for (i = 0; i < lo->xattr_map_nentries; i++) {
        const XattrMapEntry *cur_entry = lo->xattr_map_list + i;

        if ((cur_entry->flags & XATTR_MAP_FLAG_SERVER) &&
            (strstart(server_name, cur_entry->prepend, &end))) {
            if (cur_entry->flags & XATTR_MAP_FLAG_BAD) {
                return -ENODATA;
            }
            if (cur_entry->flags & XATTR_MAP_FLAG_OK) {
                *out_name = server_name;
                return 0;
            }
            if (cur_entry->flags & XATTR_MAP_FLAG_PREFIX) {
                /* Remove prefix */
                *out_name = end;
                return 0;
            }
        }
    }

    return -ENODATA;
}

static void lo_getxattr(fuse_req_t req, fuse_ino_t ino, const char *in_name,
                        size_t size)
{
    struct lo_data *lo = lo_data(req);
    char *value = NULL;
    char procname[64];
    const char *name;
    char *mapped_name;
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;
    int fd = -1;

    mapped_name = NULL;
    name = in_name;
    if (lo->xattrmap) {
        ret = xattr_map_client(lo, in_name, &mapped_name);
        if (ret < 0) {
            if (ret == -EPERM) {
                ret = -ENODATA;
            }
            fuse_reply_err(req, -ret);
            return;
        }
        if (mapped_name) {
            name = mapped_name;
        }
    }

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        g_free(mapped_name);
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
    g_free(mapped_name);
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

        if (lo->xattr_map_list) {
            /*
             * Map the names back, some attributes might be dropped,
             * some shortened, but not increased, so we shouldn't
             * run out of room.
             */
            size_t out_index, in_index;
            out_index = 0;
            in_index = 0;
            while (in_index < ret) {
                const char *map_out;
                char *in_ptr = value + in_index;
                /* Length of current attribute name */
                size_t in_len = strlen(value + in_index) + 1;

                int mapret = xattr_map_server(lo, in_ptr, &map_out);
                if (mapret != -ENODATA && mapret != 0) {
                    /* Shouldn't happen */
                    saverr = -mapret;
                    goto out;
                }
                if (mapret == 0) {
                    /* Either unchanged, or truncated */
                    size_t out_len;
                    if (map_out != in_ptr) {
                        /* +1 copies the NIL */
                        out_len = strlen(map_out) + 1;
                    } else {
                        /* No change */
                        out_len = in_len;
                    }
                    /*
                     * Move result along, may still be needed for an unchanged
                     * entry if a previous entry was changed.
                     */
                    memmove(value + out_index, map_out, out_len);

                    out_index += out_len;
                }
                in_index += in_len;
            }
            ret = out_index;
            if (ret == 0) {
                goto out;
            }
        }
        fuse_reply_buf(req, value, ret);
    } else {
        /*
         * xattrmap only ever shortens the result,
         * so we don't need to do anything clever with the
         * allocation length here.
         */
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

static void lo_setxattr(fuse_req_t req, fuse_ino_t ino, const char *in_name,
                        const char *value, size_t size, int flags)
{
    char procname[64];
    const char *name;
    char *mapped_name;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;
    int fd = -1;

    mapped_name = NULL;
    name = in_name;
    if (lo->xattrmap) {
        ret = xattr_map_client(lo, in_name, &mapped_name);
        if (ret < 0) {
            fuse_reply_err(req, -ret);
            return;
        }
        if (mapped_name) {
            name = mapped_name;
        }
    }

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        g_free(mapped_name);
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
    g_free(mapped_name);
    fuse_reply_err(req, saverr);
}

static void lo_removexattr(fuse_req_t req, fuse_ino_t ino, const char *in_name)
{
    char procname[64];
    const char *name;
    char *mapped_name;
    struct lo_data *lo = lo_data(req);
    struct lo_inode *inode;
    ssize_t ret;
    int saverr;
    int fd = -1;

    mapped_name = NULL;
    name = in_name;
    if (lo->xattrmap) {
        ret = xattr_map_client(lo, in_name, &mapped_name);
        if (ret < 0) {
            fuse_reply_err(req, -ret);
            return;
        }
        if (mapped_name) {
            name = mapped_name;
        }
    }

    inode = lo_inode(req, ino);
    if (!inode) {
        fuse_reply_err(req, EBADF);
        g_free(mapped_name);
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
    g_free(mapped_name);
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

    /*
     * We only need /proc/self/fd. Prevent ".." from accessing parent
     * directories of /proc/self/fd by bind-mounting it over /proc. Since / was
     * previously remounted with MS_REC | MS_SLAVE this mount change only
     * affects our process.
     */
    if (mount("/proc/self/fd", "/proc", NULL, MS_BIND, NULL) < 0) {
        fuse_log(FUSE_LOG_ERR, "mount(/proc/self/fd, MS_BIND): %m\n");
        exit(1);
    }

    /* Get the /proc (actually /proc/self/fd, see above) file descriptor */
    lo->proc_self_fd = open("/proc", O_PATH);
    if (lo->proc_self_fd == -1) {
        fuse_log(FUSE_LOG_ERR, "open(/proc, O_PATH): %m\n");
        exit(1);
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
 * Only keep capabilities in allowlist that are needed for file system operation
 * The (possibly NULL) modcaps_in string passed in is free'd before exit.
 */
static void setup_capabilities(char *modcaps_in)
{
    char *modcaps = modcaps_in;
    pthread_mutex_lock(&cap.mutex);
    capng_restore_state(&cap.saved);

    /*
     * Add to allowlist file system-related capabilities that are needed for a
     * file server to act like root.  Drop everything else like networking and
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
 * Use chroot as a weaker sandbox for environments where the process is
 * launched without CAP_SYS_ADMIN.
 */
static void setup_chroot(struct lo_data *lo)
{
    lo->proc_self_fd = open("/proc/self/fd", O_PATH);
    if (lo->proc_self_fd == -1) {
        fuse_log(FUSE_LOG_ERR, "open(\"/proc/self/fd\", O_PATH): %m\n");
        exit(1);
    }

    /*
     * Make the shared directory the file system root so that FUSE_OPEN
     * (lo_open()) cannot escape the shared directory by opening a symlink.
     *
     * The chroot(2) syscall is later disabled by seccomp and the
     * CAP_SYS_CHROOT capability is dropped so that tampering with the chroot
     * is not possible.
     *
     * However, it's still possible to escape the chroot via lo->proc_self_fd
     * but that requires first gaining control of the process.
     */
    if (chroot(lo->source) != 0) {
        fuse_log(FUSE_LOG_ERR, "chroot(\"%s\"): %m\n", lo->source);
        exit(1);
    }

    /* Move into the chroot */
    if (chdir("/") != 0) {
        fuse_log(FUSE_LOG_ERR, "chdir(\"/\"): %m\n");
        exit(1);
    }
}

/*
 * Lock down this process to prevent access to other processes or files outside
 * source directory.  This reduces the impact of arbitrary code execution bugs.
 */
static void setup_sandbox(struct lo_data *lo, struct fuse_session *se,
                          bool enable_syslog)
{
    if (lo->sandbox == SANDBOX_NAMESPACE) {
        setup_namespaces(lo, se);
        setup_mounts(lo->source);
    } else {
        setup_chroot(lo);
    }

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
    struct timespec ts;
    struct tm tm;
    char sec_fmt[sizeof "2020-12-07 18:17:54"];
    char zone_fmt[sizeof "+0100"];

    if (current_log_level < level) {
        return;
    }

    if (current_log_level == FUSE_LOG_DEBUG) {
        if (use_syslog) {
            /* no timestamp needed */
            localfmt = g_strdup_printf("[ID: %08ld] %s", syscall(__NR_gettid),
                                       fmt);
        } else {
            /* try formatting a broken-down timestamp */
            if (clock_gettime(CLOCK_REALTIME, &ts) != -1 &&
                localtime_r(&ts.tv_sec, &tm) != NULL &&
                strftime(sec_fmt, sizeof sec_fmt, "%Y-%m-%d %H:%M:%S",
                         &tm) != 0 &&
                strftime(zone_fmt, sizeof zone_fmt, "%z", &tm) != 0) {
                localfmt = g_strdup_printf("[%s.%02ld%s] [ID: %08ld] %s",
                                           sec_fmt,
                                           ts.tv_nsec / (10L * 1000 * 1000),
                                           zone_fmt, syscall(__NR_gettid),
                                           fmt);
            } else {
                /* fall back to a flat timestamp */
                localfmt = g_strdup_printf("[%" PRId64 "] [ID: %08ld] %s",
                                           get_clock(), syscall(__NR_gettid),
                                           fmt);
            }
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
    uint64_t mnt_id;

    fd = open("/", O_PATH);
    if (fd == -1) {
        fuse_log(FUSE_LOG_ERR, "open(%s, O_PATH): %m\n", lo->source);
        exit(1);
    }

    res = do_statx(lo, fd, "", &stat, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW,
                   &mnt_id);
    if (res == -1) {
        fuse_log(FUSE_LOG_ERR, "fstatat(%s): %m\n", lo->source);
        exit(1);
    }

    root->filetype = S_IFDIR;
    root->fd = fd;
    root->key.ino = stat.st_ino;
    root->key.dev = stat.st_dev;
    root->key.mnt_id = mnt_id;
    root->nlookup = 2;
    g_atomic_int_set(&root->refcount, 2);
    if (lo->posix_lock) {
        pthread_mutex_init(&root->plock_mutex, NULL);
        root->posix_locks = g_hash_table_new_full(
            g_direct_hash, g_direct_equal, NULL, posix_locks_value_destroy);
    }
}

static guint lo_key_hash(gconstpointer key)
{
    const struct lo_key *lkey = key;

    return (guint)lkey->ino + (guint)lkey->dev + (guint)lkey->mnt_id;
}

static gboolean lo_key_equal(gconstpointer a, gconstpointer b)
{
    const struct lo_key *la = a;
    const struct lo_key *lb = b;

    return la->ino == lb->ino && la->dev == lb->dev && la->mnt_id == lb->mnt_id;
}

static void fuse_lo_data_cleanup(struct lo_data *lo)
{
    if (lo->inodes) {
        g_hash_table_destroy(lo->inodes);
    }

    if (lo->root.posix_locks) {
        g_hash_table_destroy(lo->root.posix_locks);
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

    free(lo->xattrmap);
    free_xattrmap(lo);
    free(lo->source);
}

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_session *se;
    struct fuse_cmdline_opts opts;
    struct lo_data lo = {
        .sandbox = SANDBOX_NAMESPACE,
        .debug = 0,
        .writeback = 0,
        .posix_lock = 0,
        .allow_direct_io = 0,
        .proc_self_fd = -1,
        .user_killpriv_v2 = -1,
    };
    struct lo_map_elem *root_elem;
    struct lo_map_elem *reserve_elem;
    int ret = -1;

    /* Initialize time conversion information for localtime_r(). */
    tzset();

    /* Don't mask creation mode, kernel already did that */
    umask(0);

    qemu_init_exec_dir(argv[0]);

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
    reserve_elem = lo_map_reserve(&lo.ino_map, 0);
    if (!reserve_elem) {
        fuse_log(FUSE_LOG_ERR, "failed to alloc reserve_elem.\n");
        goto err_out1;
    }
    reserve_elem->in_use = false;
    root_elem = lo_map_reserve(&lo.ino_map, lo.root.fuse_ino);
    if (!root_elem) {
        fuse_log(FUSE_LOG_ERR, "failed to alloc root_elem.\n");
        goto err_out1;
    }
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

    if (opts.log_level != 0) {
        current_log_level = opts.log_level;
    } else {
        /* default log level is INFO */
        current_log_level = FUSE_LOG_INFO;
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
        if (!lo.source) {
            fuse_log(FUSE_LOG_ERR, "failed to strdup source\n");
            goto err_out1;
        }
    }

    if (lo.xattrmap) {
        parse_xattrmap(&lo);
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

    lo.use_statx = true;

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
