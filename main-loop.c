/*
 * QEMU System Emulator
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

#include "qemu-common.h"
#include "qemu-timer.h"
#include "slirp/slirp.h"
#include "main-loop.h"

#ifndef _WIN32

#include "compatfd.h"

static int io_thread_fd = -1;

void qemu_notify_event(void)
{
    /* Write 8 bytes to be compatible with eventfd.  */
    static const uint64_t val = 1;
    ssize_t ret;

    if (io_thread_fd == -1) {
        return;
    }
    do {
        ret = write(io_thread_fd, &val, sizeof(val));
    } while (ret < 0 && errno == EINTR);

    /* EAGAIN is fine, a read must be pending.  */
    if (ret < 0 && errno != EAGAIN) {
        fprintf(stderr, "qemu_notify_event: write() failed: %s\n",
                strerror(errno));
        exit(1);
    }
}

static void qemu_event_read(void *opaque)
{
    int fd = (intptr_t)opaque;
    ssize_t len;
    char buffer[512];

    /* Drain the notify pipe.  For eventfd, only 8 bytes will be read.  */
    do {
        len = read(fd, buffer, sizeof(buffer));
    } while ((len == -1 && errno == EINTR) || len == sizeof(buffer));
}

static int qemu_event_init(void)
{
    int err;
    int fds[2];

    err = qemu_eventfd(fds);
    if (err == -1) {
        return -errno;
    }
    err = fcntl_setfl(fds[0], O_NONBLOCK);
    if (err < 0) {
        goto fail;
    }
    err = fcntl_setfl(fds[1], O_NONBLOCK);
    if (err < 0) {
        goto fail;
    }
    qemu_set_fd_handler2(fds[0], NULL, qemu_event_read, NULL,
                         (void *)(intptr_t)fds[0]);

    io_thread_fd = fds[1];
    return 0;

fail:
    close(fds[0]);
    close(fds[1]);
    return err;
}

/* If we have signalfd, we mask out the signals we want to handle and then
 * use signalfd to listen for them.  We rely on whatever the current signal
 * handler is to dispatch the signals when we receive them.
 */
static void sigfd_handler(void *opaque)
{
    int fd = (intptr_t)opaque;
    struct qemu_signalfd_siginfo info;
    struct sigaction action;
    ssize_t len;

    while (1) {
        do {
            len = read(fd, &info, sizeof(info));
        } while (len == -1 && errno == EINTR);

        if (len == -1 && errno == EAGAIN) {
            break;
        }

        if (len != sizeof(info)) {
            printf("read from sigfd returned %zd: %m\n", len);
            return;
        }

        sigaction(info.ssi_signo, NULL, &action);
        if ((action.sa_flags & SA_SIGINFO) && action.sa_sigaction) {
            action.sa_sigaction(info.ssi_signo,
                                (siginfo_t *)&info, NULL);
        } else if (action.sa_handler) {
            action.sa_handler(info.ssi_signo);
        }
    }
}

static int qemu_signal_init(void)
{
    int sigfd;
    sigset_t set;

    /*
     * SIG_IPI must be blocked in the main thread and must not be caught
     * by sigwait() in the signal thread. Otherwise, the cpu thread will
     * not catch it reliably.
     */
    sigemptyset(&set);
    sigaddset(&set, SIG_IPI);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    sigemptyset(&set);
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGBUS);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    sigfd = qemu_signalfd(&set);
    if (sigfd == -1) {
        fprintf(stderr, "failed to create signalfd\n");
        return -errno;
    }

    fcntl_setfl(sigfd, O_NONBLOCK);

    qemu_set_fd_handler2(sigfd, NULL, sigfd_handler, NULL,
                         (void *)(intptr_t)sigfd);

    return 0;
}

#else /* _WIN32 */

HANDLE qemu_event_handle;

static void dummy_event_handler(void *opaque)
{
}

static int qemu_event_init(void)
{
    qemu_event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!qemu_event_handle) {
        fprintf(stderr, "Failed CreateEvent: %ld\n", GetLastError());
        return -1;
    }
    qemu_add_wait_object(qemu_event_handle, dummy_event_handler, NULL);
    return 0;
}

void qemu_notify_event(void)
{
    if (!SetEvent(qemu_event_handle)) {
        fprintf(stderr, "qemu_notify_event: SetEvent failed: %ld\n",
                GetLastError());
        exit(1);
    }
}

static int qemu_signal_init(void)
{
    return 0;
}
#endif

int qemu_init_main_loop(void)
{
    int ret;

    qemu_mutex_lock_iothread();
    ret = qemu_signal_init();
    if (ret) {
        return ret;
    }

    /* Note eventfd must be drained before signalfd handlers run */
    ret = qemu_event_init();
    if (ret) {
        return ret;
    }

    return 0;
}


static GPollFD poll_fds[1024 * 2]; /* this is probably overkill */
static int n_poll_fds;
static int max_priority;

static void glib_select_fill(int *max_fd, fd_set *rfds, fd_set *wfds,
                             fd_set *xfds, struct timeval *tv)
{
    GMainContext *context = g_main_context_default();
    int i;
    int timeout = 0, cur_timeout;

    g_main_context_prepare(context, &max_priority);

    n_poll_fds = g_main_context_query(context, max_priority, &timeout,
                                      poll_fds, ARRAY_SIZE(poll_fds));
    g_assert(n_poll_fds <= ARRAY_SIZE(poll_fds));

    for (i = 0; i < n_poll_fds; i++) {
        GPollFD *p = &poll_fds[i];

        if ((p->events & G_IO_IN)) {
            FD_SET(p->fd, rfds);
            *max_fd = MAX(*max_fd, p->fd);
        }
        if ((p->events & G_IO_OUT)) {
            FD_SET(p->fd, wfds);
            *max_fd = MAX(*max_fd, p->fd);
        }
        if ((p->events & G_IO_ERR)) {
            FD_SET(p->fd, xfds);
            *max_fd = MAX(*max_fd, p->fd);
        }
    }

    cur_timeout = (tv->tv_sec * 1000) + ((tv->tv_usec + 500) / 1000);
    if (timeout >= 0 && timeout < cur_timeout) {
        tv->tv_sec = timeout / 1000;
        tv->tv_usec = (timeout % 1000) * 1000;
    }
}

static void glib_select_poll(fd_set *rfds, fd_set *wfds, fd_set *xfds,
                             bool err)
{
    GMainContext *context = g_main_context_default();

    if (!err) {
        int i;

        for (i = 0; i < n_poll_fds; i++) {
            GPollFD *p = &poll_fds[i];

            if ((p->events & G_IO_IN) && FD_ISSET(p->fd, rfds)) {
                p->revents |= G_IO_IN;
            }
            if ((p->events & G_IO_OUT) && FD_ISSET(p->fd, wfds)) {
                p->revents |= G_IO_OUT;
            }
            if ((p->events & G_IO_ERR) && FD_ISSET(p->fd, xfds)) {
                p->revents |= G_IO_ERR;
            }
        }
    }

    if (g_main_context_check(context, max_priority, poll_fds, n_poll_fds)) {
        g_main_context_dispatch(context);
    }
}

#ifdef _WIN32
/***********************************************************/
/* Polling handling */

typedef struct PollingEntry {
    PollingFunc *func;
    void *opaque;
    struct PollingEntry *next;
} PollingEntry;

static PollingEntry *first_polling_entry;

int qemu_add_polling_cb(PollingFunc *func, void *opaque)
{
    PollingEntry **ppe, *pe;
    pe = g_malloc0(sizeof(PollingEntry));
    pe->func = func;
    pe->opaque = opaque;
    for(ppe = &first_polling_entry; *ppe != NULL; ppe = &(*ppe)->next);
    *ppe = pe;
    return 0;
}

void qemu_del_polling_cb(PollingFunc *func, void *opaque)
{
    PollingEntry **ppe, *pe;
    for(ppe = &first_polling_entry; *ppe != NULL; ppe = &(*ppe)->next) {
        pe = *ppe;
        if (pe->func == func && pe->opaque == opaque) {
            *ppe = pe->next;
            g_free(pe);
            break;
        }
    }
}

/***********************************************************/
/* Wait objects support */
typedef struct WaitObjects {
    int num;
    HANDLE events[MAXIMUM_WAIT_OBJECTS + 1];
    WaitObjectFunc *func[MAXIMUM_WAIT_OBJECTS + 1];
    void *opaque[MAXIMUM_WAIT_OBJECTS + 1];
} WaitObjects;

static WaitObjects wait_objects = {0};

int qemu_add_wait_object(HANDLE handle, WaitObjectFunc *func, void *opaque)
{
    WaitObjects *w = &wait_objects;
    if (w->num >= MAXIMUM_WAIT_OBJECTS) {
        return -1;
    }
    w->events[w->num] = handle;
    w->func[w->num] = func;
    w->opaque[w->num] = opaque;
    w->num++;
    return 0;
}

void qemu_del_wait_object(HANDLE handle, WaitObjectFunc *func, void *opaque)
{
    int i, found;
    WaitObjects *w = &wait_objects;

    found = 0;
    for (i = 0; i < w->num; i++) {
        if (w->events[i] == handle) {
            found = 1;
        }
        if (found) {
            w->events[i] = w->events[i + 1];
            w->func[i] = w->func[i + 1];
            w->opaque[i] = w->opaque[i + 1];
        }
    }
    if (found) {
        w->num--;
    }
}

static void os_host_main_loop_wait(int *timeout)
{
    int ret, ret2, i;
    PollingEntry *pe;

    /* XXX: need to suppress polling by better using win32 events */
    ret = 0;
    for (pe = first_polling_entry; pe != NULL; pe = pe->next) {
        ret |= pe->func(pe->opaque);
    }
    if (ret == 0) {
        int err;
        WaitObjects *w = &wait_objects;

        qemu_mutex_unlock_iothread();
        ret = WaitForMultipleObjects(w->num, w->events, FALSE, *timeout);
        qemu_mutex_lock_iothread();
        if (WAIT_OBJECT_0 + 0 <= ret && ret <= WAIT_OBJECT_0 + w->num - 1) {
            if (w->func[ret - WAIT_OBJECT_0]) {
                w->func[ret - WAIT_OBJECT_0](w->opaque[ret - WAIT_OBJECT_0]);
            }

            /* Check for additional signaled events */
            for (i = (ret - WAIT_OBJECT_0 + 1); i < w->num; i++) {
                /* Check if event is signaled */
                ret2 = WaitForSingleObject(w->events[i], 0);
                if (ret2 == WAIT_OBJECT_0) {
                    if (w->func[i]) {
                        w->func[i](w->opaque[i]);
                    }
                } else if (ret2 != WAIT_TIMEOUT) {
                    err = GetLastError();
                    fprintf(stderr, "WaitForSingleObject error %d %d\n", i, err);
                }
            }
        } else if (ret != WAIT_TIMEOUT) {
            err = GetLastError();
            fprintf(stderr, "WaitForMultipleObjects error %d %d\n", ret, err);
        }
    }

    *timeout = 0;
}
#else
static inline void os_host_main_loop_wait(int *timeout)
{
}
#endif

int main_loop_wait(int nonblocking)
{
    fd_set rfds, wfds, xfds;
    int ret, nfds;
    struct timeval tv;
    int timeout;

    if (nonblocking) {
        timeout = 0;
    } else {
        timeout = qemu_calculate_timeout();
        qemu_bh_update_timeout(&timeout);
    }

    os_host_main_loop_wait(&timeout);

    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    /* poll any events */
    /* XXX: separate device handlers from system ones */
    nfds = -1;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);

#ifdef CONFIG_SLIRP
    slirp_select_fill(&nfds, &rfds, &wfds, &xfds);
#endif
    qemu_iohandler_fill(&nfds, &rfds, &wfds, &xfds);
    glib_select_fill(&nfds, &rfds, &wfds, &xfds, &tv);

    if (timeout > 0) {
        qemu_mutex_unlock_iothread();
    }

    ret = select(nfds + 1, &rfds, &wfds, &xfds, &tv);

    if (timeout > 0) {
        qemu_mutex_lock_iothread();
    }

    glib_select_poll(&rfds, &wfds, &xfds, (ret < 0));
    qemu_iohandler_poll(&rfds, &wfds, &xfds, ret);
#ifdef CONFIG_SLIRP
    slirp_select_poll(&rfds, &wfds, &xfds, (ret < 0));
#endif

    qemu_run_all_timers();

    /* Check bottom-halves last in case any of the earlier events triggered
       them.  */
    qemu_bh_poll();

    return ret;
}
