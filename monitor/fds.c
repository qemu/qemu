/*
 * QEMU monitor file descriptor passing
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "monitor-internal.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "sysemu/runstate.h"

/* file descriptors passed via SCM_RIGHTS */
typedef struct mon_fd_t mon_fd_t;
struct mon_fd_t {
    char *name;
    int fd;
    QLIST_ENTRY(mon_fd_t) next;
};

/* file descriptor associated with a file descriptor set */
typedef struct MonFdsetFd MonFdsetFd;
struct MonFdsetFd {
    int fd;
    bool removed;
    char *opaque;
    QLIST_ENTRY(MonFdsetFd) next;
};

/* file descriptor set containing fds passed via SCM_RIGHTS */
typedef struct MonFdset MonFdset;
struct MonFdset {
    int64_t id;
    QLIST_HEAD(, MonFdsetFd) fds;
    QLIST_HEAD(, MonFdsetFd) dup_fds;
    QLIST_ENTRY(MonFdset) next;
};

/* Protects mon_fdsets */
static QemuMutex mon_fdsets_lock;
static QLIST_HEAD(, MonFdset) mon_fdsets;

static bool monitor_add_fd(Monitor *mon, int fd, const char *fdname, Error **errp)
{
    mon_fd_t *monfd;

    if (qemu_isdigit(fdname[0])) {
        close(fd);
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "fdname",
                   "a name not starting with a digit");
        return false;
    }

    /* See close() call below. */
    qemu_mutex_lock(&mon->mon_lock);
    QLIST_FOREACH(monfd, &mon->fds, next) {
        int tmp_fd;

        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        tmp_fd = monfd->fd;
        monfd->fd = fd;
        qemu_mutex_unlock(&mon->mon_lock);
        /* Make sure close() is outside critical section */
        close(tmp_fd);
        return true;
    }

    monfd = g_new0(mon_fd_t, 1);
    monfd->name = g_strdup(fdname);
    monfd->fd = fd;

    QLIST_INSERT_HEAD(&mon->fds, monfd, next);
    qemu_mutex_unlock(&mon->mon_lock);
    return true;
}

#ifdef CONFIG_POSIX
void qmp_getfd(const char *fdname, Error **errp)
{
    Monitor *cur_mon = monitor_cur();
    int fd;

    fd = qemu_chr_fe_get_msgfd(&cur_mon->chr);
    if (fd == -1) {
        error_setg(errp, "No file descriptor supplied via SCM_RIGHTS");
        return;
    }

    monitor_add_fd(cur_mon, fd, fdname, errp);
}
#endif

void qmp_closefd(const char *fdname, Error **errp)
{
    Monitor *cur_mon = monitor_cur();
    mon_fd_t *monfd;
    int tmp_fd;

    qemu_mutex_lock(&cur_mon->mon_lock);
    QLIST_FOREACH(monfd, &cur_mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        QLIST_REMOVE(monfd, next);
        tmp_fd = monfd->fd;
        g_free(monfd->name);
        g_free(monfd);
        qemu_mutex_unlock(&cur_mon->mon_lock);
        /* Make sure close() is outside critical section */
        close(tmp_fd);
        return;
    }

    qemu_mutex_unlock(&cur_mon->mon_lock);
    error_setg(errp, "File descriptor named '%s' not found", fdname);
}

int monitor_get_fd(Monitor *mon, const char *fdname, Error **errp)
{
    mon_fd_t *monfd;

    QEMU_LOCK_GUARD(&mon->mon_lock);
    QLIST_FOREACH(monfd, &mon->fds, next) {
        int fd;

        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        fd = monfd->fd;
        assert(fd >= 0);

        /* caller takes ownership of fd */
        QLIST_REMOVE(monfd, next);
        g_free(monfd->name);
        g_free(monfd);

        return fd;
    }

    error_setg(errp, "File descriptor named '%s' has not been found", fdname);
    return -1;
}

static void monitor_fdset_cleanup(MonFdset *mon_fdset)
{
    MonFdsetFd *mon_fdset_fd;
    MonFdsetFd *mon_fdset_fd_next;

    QLIST_FOREACH_SAFE(mon_fdset_fd, &mon_fdset->fds, next, mon_fdset_fd_next) {
        if ((mon_fdset_fd->removed ||
                (QLIST_EMPTY(&mon_fdset->dup_fds) && mon_refcount == 0)) &&
                runstate_is_running()) {
            close(mon_fdset_fd->fd);
            g_free(mon_fdset_fd->opaque);
            QLIST_REMOVE(mon_fdset_fd, next);
            g_free(mon_fdset_fd);
        }
    }

    if (QLIST_EMPTY(&mon_fdset->fds) && QLIST_EMPTY(&mon_fdset->dup_fds)) {
        QLIST_REMOVE(mon_fdset, next);
        g_free(mon_fdset);
    }
}

void monitor_fdsets_cleanup(void)
{
    MonFdset *mon_fdset;
    MonFdset *mon_fdset_next;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH_SAFE(mon_fdset, &mon_fdsets, next, mon_fdset_next) {
        monitor_fdset_cleanup(mon_fdset);
    }
}

AddfdInfo *qmp_add_fd(bool has_fdset_id, int64_t fdset_id,
                      const char *opaque, Error **errp)
{
    int fd;
    Monitor *mon = monitor_cur();
    AddfdInfo *fdinfo;

    fd = qemu_chr_fe_get_msgfd(&mon->chr);
    if (fd == -1) {
        error_setg(errp, "No file descriptor supplied via SCM_RIGHTS");
        goto error;
    }

    fdinfo = monitor_fdset_add_fd(fd, has_fdset_id, fdset_id, opaque, errp);
    if (fdinfo) {
        return fdinfo;
    }

error:
    if (fd != -1) {
        close(fd);
    }
    return NULL;
}

#ifdef WIN32
void qmp_get_win32_socket(const char *infos, const char *fdname, Error **errp)
{
    g_autofree WSAPROTOCOL_INFOW *info = NULL;
    gsize len;
    SOCKET sk;
    int fd;

    info = (void *)g_base64_decode(infos, &len);
    if (len != sizeof(*info)) {
        error_setg(errp, "Invalid WSAPROTOCOL_INFOW value");
        return;
    }

    sk = WSASocketW(FROM_PROTOCOL_INFO,
                    FROM_PROTOCOL_INFO,
                    FROM_PROTOCOL_INFO,
                    info, 0, 0);
    if (sk == INVALID_SOCKET) {
        error_setg_win32(errp, WSAGetLastError(), "Couldn't import socket");
        return;
    }

    fd = _open_osfhandle(sk, _O_BINARY);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Failed to associate a FD with the SOCKET");
        closesocket(sk);
        return;
    }

    monitor_add_fd(monitor_cur(), fd, fdname, errp);
}
#endif


void qmp_remove_fd(int64_t fdset_id, bool has_fd, int64_t fd, Error **errp)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    char fd_str[60];

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        if (mon_fdset->id != fdset_id) {
            continue;
        }
        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            if (has_fd) {
                if (mon_fdset_fd->fd != fd) {
                    continue;
                }
                mon_fdset_fd->removed = true;
                break;
            } else {
                mon_fdset_fd->removed = true;
            }
        }
        if (has_fd && !mon_fdset_fd) {
            goto error;
        }
        monitor_fdset_cleanup(mon_fdset);
        return;
    }

error:
    if (has_fd) {
        snprintf(fd_str, sizeof(fd_str), "fdset-id:%" PRId64 ", fd:%" PRId64,
                 fdset_id, fd);
    } else {
        snprintf(fd_str, sizeof(fd_str), "fdset-id:%" PRId64, fdset_id);
    }
    error_setg(errp, "File descriptor named '%s' not found", fd_str);
}

FdsetInfoList *qmp_query_fdsets(Error **errp)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    FdsetInfoList *fdset_list = NULL;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        FdsetInfo *fdset_info = g_malloc0(sizeof(*fdset_info));

        fdset_info->fdset_id = mon_fdset->id;

        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            FdsetFdInfo *fdsetfd_info;

            fdsetfd_info = g_malloc0(sizeof(*fdsetfd_info));
            fdsetfd_info->fd = mon_fdset_fd->fd;
            fdsetfd_info->opaque = g_strdup(mon_fdset_fd->opaque);

            QAPI_LIST_PREPEND(fdset_info->fds, fdsetfd_info);
        }

        QAPI_LIST_PREPEND(fdset_list, fdset_info);
    }

    return fdset_list;
}

AddfdInfo *monitor_fdset_add_fd(int fd, bool has_fdset_id, int64_t fdset_id,
                                const char *opaque, Error **errp)
{
    MonFdset *mon_fdset = NULL;
    MonFdsetFd *mon_fdset_fd;
    AddfdInfo *fdinfo;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    if (has_fdset_id) {
        QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
            /* Break if match found or match impossible due to ordering by ID */
            if (fdset_id <= mon_fdset->id) {
                if (fdset_id < mon_fdset->id) {
                    mon_fdset = NULL;
                }
                break;
            }
        }
    }

    if (mon_fdset == NULL) {
        int64_t fdset_id_prev = -1;
        MonFdset *mon_fdset_cur = QLIST_FIRST(&mon_fdsets);

        if (has_fdset_id) {
            if (fdset_id < 0) {
                error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "fdset-id",
                           "a non-negative value");
                return NULL;
            }
            /* Use specified fdset ID */
            QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
                mon_fdset_cur = mon_fdset;
                if (fdset_id < mon_fdset_cur->id) {
                    break;
                }
            }
        } else {
            /* Use first available fdset ID */
            QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
                mon_fdset_cur = mon_fdset;
                if (fdset_id_prev == mon_fdset_cur->id - 1) {
                    fdset_id_prev = mon_fdset_cur->id;
                    continue;
                }
                break;
            }
        }

        mon_fdset = g_malloc0(sizeof(*mon_fdset));
        if (has_fdset_id) {
            mon_fdset->id = fdset_id;
        } else {
            mon_fdset->id = fdset_id_prev + 1;
        }

        /* The fdset list is ordered by fdset ID */
        if (!mon_fdset_cur) {
            QLIST_INSERT_HEAD(&mon_fdsets, mon_fdset, next);
        } else if (mon_fdset->id < mon_fdset_cur->id) {
            QLIST_INSERT_BEFORE(mon_fdset_cur, mon_fdset, next);
        } else {
            QLIST_INSERT_AFTER(mon_fdset_cur, mon_fdset, next);
        }
    }

    mon_fdset_fd = g_malloc0(sizeof(*mon_fdset_fd));
    mon_fdset_fd->fd = fd;
    mon_fdset_fd->removed = false;
    mon_fdset_fd->opaque = g_strdup(opaque);
    QLIST_INSERT_HEAD(&mon_fdset->fds, mon_fdset_fd, next);

    fdinfo = g_malloc0(sizeof(*fdinfo));
    fdinfo->fdset_id = mon_fdset->id;
    fdinfo->fd = mon_fdset_fd->fd;

    return fdinfo;
}

int monitor_fdset_dup_fd_add(int64_t fdset_id, int flags)
{
#ifdef _WIN32
    return -ENOENT;
#else
    MonFdset *mon_fdset;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        MonFdsetFd *mon_fdset_fd;
        MonFdsetFd *mon_fdset_fd_dup;
        int fd = -1;
        int dup_fd;
        int mon_fd_flags;

        if (mon_fdset->id != fdset_id) {
            continue;
        }

        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            mon_fd_flags = fcntl(mon_fdset_fd->fd, F_GETFL);
            if (mon_fd_flags == -1) {
                return -1;
            }

            if ((flags & O_ACCMODE) == (mon_fd_flags & O_ACCMODE)) {
                fd = mon_fdset_fd->fd;
                break;
            }
        }

        if (fd == -1) {
            errno = EACCES;
            return -1;
        }

        dup_fd = qemu_dup_flags(fd, flags);
        if (dup_fd == -1) {
            return -1;
        }

        mon_fdset_fd_dup = g_malloc0(sizeof(*mon_fdset_fd_dup));
        mon_fdset_fd_dup->fd = dup_fd;
        QLIST_INSERT_HEAD(&mon_fdset->dup_fds, mon_fdset_fd_dup, next);
        return dup_fd;
    }

    errno = ENOENT;
    return -1;
#endif
}

static int64_t monitor_fdset_dup_fd_find_remove(int dup_fd, bool remove)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd_dup;

    QEMU_LOCK_GUARD(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        QLIST_FOREACH(mon_fdset_fd_dup, &mon_fdset->dup_fds, next) {
            if (mon_fdset_fd_dup->fd == dup_fd) {
                if (remove) {
                    QLIST_REMOVE(mon_fdset_fd_dup, next);
                    g_free(mon_fdset_fd_dup);
                    if (QLIST_EMPTY(&mon_fdset->dup_fds)) {
                        monitor_fdset_cleanup(mon_fdset);
                    }
                    return -1;
                } else {
                    return mon_fdset->id;
                }
            }
        }
    }

    return -1;
}

int64_t monitor_fdset_dup_fd_find(int dup_fd)
{
    return monitor_fdset_dup_fd_find_remove(dup_fd, false);
}

void monitor_fdset_dup_fd_remove(int dup_fd)
{
    monitor_fdset_dup_fd_find_remove(dup_fd, true);
}

int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp)
{
    int fd;

    if (!qemu_isdigit(fdname[0]) && mon) {
        fd = monitor_get_fd(mon, fdname, errp);
    } else {
        fd = qemu_parse_fd(fdname);
        if (fd < 0) {
            error_setg(errp, "Invalid file descriptor number '%s'",
                       fdname);
        }
    }

    return fd;
}

static void __attribute__((__constructor__)) monitor_fds_init(void)
{
    qemu_mutex_init(&mon_fdsets_lock);
}
