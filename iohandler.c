/*
 * QEMU System Emulator - managing I/O handler
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#include "config-host.h"
#include "qemu-common.h"
#include "qemu-char.h"
#include "qemu-queue.h"

#ifndef _WIN32
#include <sys/wait.h>
#endif

typedef struct IOHandlerRecord {
    int fd;
    IOCanReadHandler *fd_read_poll;
    IOHandler *fd_read;
    IOHandler *fd_write;
    int deleted;
    void *opaque;
    QLIST_ENTRY(IOHandlerRecord) next;
} IOHandlerRecord;

static QLIST_HEAD(, IOHandlerRecord) io_handlers =
    QLIST_HEAD_INITIALIZER(io_handlers);


/* XXX: fd_read_poll should be suppressed, but an API change is
   necessary in the character devices to suppress fd_can_read(). */
int qemu_set_fd_handler2(int fd,
                         IOCanReadHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    IOHandlerRecord *ioh;

    if (!fd_read && !fd_write) {
        QLIST_FOREACH(ioh, &io_handlers, next) {
            if (ioh->fd == fd) {
                ioh->deleted = 1;
                break;
            }
        }
    } else {
        QLIST_FOREACH(ioh, &io_handlers, next) {
            if (ioh->fd == fd)
                goto found;
        }
        ioh = g_malloc0(sizeof(IOHandlerRecord));
        QLIST_INSERT_HEAD(&io_handlers, ioh, next);
    found:
        ioh->fd = fd;
        ioh->fd_read_poll = fd_read_poll;
        ioh->fd_read = fd_read;
        ioh->fd_write = fd_write;
        ioh->opaque = opaque;
        ioh->deleted = 0;
    }
    return 0;
}

typedef struct IOTrampoline
{
    GIOChannel *chan;
    IOHandler *fd_read;
    IOHandler *fd_write;
    void *opaque;
    guint tag;
} IOTrampoline;

static gboolean fd_trampoline(GIOChannel *chan, GIOCondition cond, gpointer opaque)
{
    IOTrampoline *tramp = opaque;

    if ((cond & G_IO_IN) && tramp->fd_read) {
        tramp->fd_read(tramp->opaque);
    }

    if ((cond & G_IO_OUT) && tramp->fd_write) {
        tramp->fd_write(tramp->opaque);
    }

    return TRUE;
}

int qemu_set_fd_handler(int fd,
                        IOHandler *fd_read,
                        IOHandler *fd_write,
                        void *opaque)
{
    static IOTrampoline fd_trampolines[FD_SETSIZE];
    IOTrampoline *tramp = &fd_trampolines[fd];

    if (tramp->tag != 0) {
        g_io_channel_unref(tramp->chan);
        g_source_remove(tramp->tag);
        tramp->tag = 0;
    }

    if (fd_read || fd_write || opaque) {
        GIOCondition cond = 0;

        tramp->fd_read = fd_read;
        tramp->fd_write = fd_write;
        tramp->opaque = opaque;

        if (fd_read) {
            cond |= G_IO_IN | G_IO_ERR;
        }

        if (fd_write) {
            cond |= G_IO_OUT | G_IO_ERR;
        }

        tramp->chan = g_io_channel_unix_new(fd);
        tramp->tag = g_io_add_watch(tramp->chan, cond, fd_trampoline, tramp);
    }

    return 0;
}

void qemu_iohandler_fill(int *pnfds, fd_set *readfds, fd_set *writefds, fd_set *xfds)
{
    IOHandlerRecord *ioh;

    QLIST_FOREACH(ioh, &io_handlers, next) {
        if (ioh->deleted)
            continue;
        if (ioh->fd_read &&
            (!ioh->fd_read_poll ||
             ioh->fd_read_poll(ioh->opaque) != 0)) {
            FD_SET(ioh->fd, readfds);
            if (ioh->fd > *pnfds)
                *pnfds = ioh->fd;
        }
        if (ioh->fd_write) {
            FD_SET(ioh->fd, writefds);
            if (ioh->fd > *pnfds)
                *pnfds = ioh->fd;
        }
    }
}

void qemu_iohandler_poll(fd_set *readfds, fd_set *writefds, fd_set *xfds, int ret)
{
    if (ret > 0) {
        IOHandlerRecord *pioh, *ioh;

        QLIST_FOREACH_SAFE(ioh, &io_handlers, next, pioh) {
            if (!ioh->deleted && ioh->fd_read && FD_ISSET(ioh->fd, readfds)) {
                ioh->fd_read(ioh->opaque);
            }
            if (!ioh->deleted && ioh->fd_write && FD_ISSET(ioh->fd, writefds)) {
                ioh->fd_write(ioh->opaque);
            }

            /* Do this last in case read/write handlers marked it for deletion */
            if (ioh->deleted) {
                QLIST_REMOVE(ioh, next);
                g_free(ioh);
            }
        }
    }
}

/* reaping of zombies.  right now we're not passing the status to
   anyone, but it would be possible to add a callback.  */
#ifndef _WIN32
typedef struct ChildProcessRecord {
    int pid;
    QLIST_ENTRY(ChildProcessRecord) next;
} ChildProcessRecord;

static QLIST_HEAD(, ChildProcessRecord) child_watches =
    QLIST_HEAD_INITIALIZER(child_watches);

static QEMUBH *sigchld_bh;

static void sigchld_handler(int signal)
{
    qemu_bh_schedule(sigchld_bh);
}

static void sigchld_bh_handler(void *opaque)
{
    ChildProcessRecord *rec, *next;

    QLIST_FOREACH_SAFE(rec, &child_watches, next, next) {
        if (waitpid(rec->pid, NULL, WNOHANG) == rec->pid) {
            QLIST_REMOVE(rec, next);
            g_free(rec);
        }
    }
}

static void qemu_init_child_watch(void)
{
    struct sigaction act;
    sigchld_bh = qemu_bh_new(sigchld_bh_handler, NULL);

    act.sa_handler = sigchld_handler;
    act.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &act, NULL);
}

int qemu_add_child_watch(pid_t pid)
{
    ChildProcessRecord *rec;

    if (!sigchld_bh) {
        qemu_init_child_watch();
    }

    QLIST_FOREACH(rec, &child_watches, next) {
        if (rec->pid == pid) {
            return 1;
        }
    }
    rec = g_malloc0(sizeof(ChildProcessRecord));
    rec->pid = pid;
    QLIST_INSERT_HEAD(&child_watches, rec, next);
    return 0;
}
#endif
