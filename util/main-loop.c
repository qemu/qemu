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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/timer.h"
#include "system/cpu-timers.h"
#include "exec/icount.h"
#include "system/replay.h"
#include "qemu/main-loop.h"
#include "block/aio.h"
#include "block/thread-pool.h"
#include "qemu/error-report.h"
#include "qemu/queue.h"
#include "qom/object.h"

#ifndef _WIN32
#include <sys/wait.h>
#endif

#ifndef _WIN32

/* If we have signalfd, we mask out the signals we want to handle and then
 * use signalfd to listen for them.  We rely on whatever the current signal
 * handler is to dispatch the signals when we receive them.
 */
/*
 * Disable CFI checks.
 * We are going to call a signal handler directly. Such handler may or may not
 * have been defined in our binary, so there's no guarantee that the pointer
 * used to set the handler is a cfi-valid pointer. Since the handlers are
 * stored in kernel memory, changing the handler to an attacker-defined
 * function requires being able to call a sigaction() syscall,
 * which is not as easy as overwriting a pointer in memory.
 */
QEMU_DISABLE_CFI
static void sigfd_handler(void *opaque)
{
    int fd = (intptr_t)opaque;
    struct qemu_signalfd_siginfo info;
    struct sigaction action;
    ssize_t len;

    while (1) {
        len = RETRY_ON_EINTR(read(fd, &info, sizeof(info)));

        if (len == -1 && errno == EAGAIN) {
            break;
        }

        if (len != sizeof(info)) {
            error_report("read from sigfd returned %zd: %s", len,
                         g_strerror(errno));
            return;
        }

        sigaction(info.ssi_signo, NULL, &action);
        if ((action.sa_flags & SA_SIGINFO) && action.sa_sigaction) {
            sigaction_invoke(&action, &info);
        } else if (action.sa_handler) {
            action.sa_handler(info.ssi_signo);
        }
    }
}

static int qemu_signal_init(Error **errp)
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
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGBUS);
    /* SIGINT cannot be handled via signalfd, so that ^C can be used
     * to interrupt QEMU when it is being run under gdb.  SIGHUP and
     * SIGTERM are also handled asynchronously, even though it is not
     * strictly necessary, because they use the same handler as SIGINT.
     */
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    sigdelset(&set, SIG_IPI);
    sigfd = qemu_signalfd(&set);
    if (sigfd == -1) {
        error_setg_errno(errp, errno, "failed to create signalfd");
        return -errno;
    }

    g_unix_set_fd_nonblocking(sigfd, true, NULL);

    qemu_set_fd_handler(sigfd, sigfd_handler, NULL, (void *)(intptr_t)sigfd);

    return 0;
}

#else /* _WIN32 */

static int qemu_signal_init(Error **errp)
{
    return 0;
}
#endif

static AioContext *qemu_aio_context;
static QEMUBH *qemu_notify_bh;

static void notify_event_cb(void *opaque)
{
    /* No need to do anything; this bottom half is only used to
     * kick the kernel out of ppoll/poll/WaitForMultipleObjects.
     */
}

AioContext *qemu_get_aio_context(void)
{
    return qemu_aio_context;
}

void qemu_notify_event(void)
{
    if (!qemu_aio_context) {
        return;
    }
    qemu_bh_schedule(qemu_notify_bh);
}

static GArray *gpollfds;

int qemu_init_main_loop(Error **errp)
{
    int ret;
    GSource *src;

    init_clocks(qemu_timer_notify_cb);

    ret = qemu_signal_init(errp);
    if (ret) {
        return ret;
    }

    qemu_aio_context = aio_context_new(errp);
    if (!qemu_aio_context) {
        return -EMFILE;
    }
    qemu_set_current_aio_context(qemu_aio_context);
    qemu_notify_bh = qemu_bh_new(notify_event_cb, NULL);
    gpollfds = g_array_new(FALSE, FALSE, sizeof(GPollFD));
    src = aio_get_g_source(qemu_aio_context);
    g_source_set_name(src, "aio-context");
    g_source_attach(src, NULL);
    g_source_unref(src);
    src = iohandler_get_g_source();
    g_source_set_name(src, "io-handler");
    g_source_attach(src, NULL);
    g_source_unref(src);
    return 0;
}

static void main_loop_update_params(EventLoopBase *base, Error **errp)
{
    ERRP_GUARD();

    if (!qemu_aio_context) {
        error_setg(errp, "qemu aio context not ready");
        return;
    }

    aio_context_set_aio_params(qemu_aio_context, base->aio_max_batch);

    aio_context_set_thread_pool_params(qemu_aio_context, base->thread_pool_min,
                                       base->thread_pool_max, errp);
}

MainLoop *mloop;

static void main_loop_init(EventLoopBase *base, Error **errp)
{
    MainLoop *m = MAIN_LOOP(base);

    if (mloop) {
        error_setg(errp, "only one main-loop instance allowed");
        return;
    }

    main_loop_update_params(base, errp);

    mloop = m;
}

static bool main_loop_can_be_deleted(EventLoopBase *base)
{
    return false;
}

static void main_loop_class_init(ObjectClass *oc, void *class_data)
{
    EventLoopBaseClass *bc = EVENT_LOOP_BASE_CLASS(oc);

    bc->init = main_loop_init;
    bc->update_params = main_loop_update_params;
    bc->can_be_deleted = main_loop_can_be_deleted;
}

static const TypeInfo main_loop_info = {
    .name = TYPE_MAIN_LOOP,
    .parent = TYPE_EVENT_LOOP_BASE,
    .class_init = main_loop_class_init,
    .instance_size = sizeof(MainLoop),
};

static void main_loop_register_types(void)
{
    type_register_static(&main_loop_info);
}

type_init(main_loop_register_types)

static int max_priority;

#ifndef _WIN32
static int glib_pollfds_idx;
static int glib_n_poll_fds;

static void glib_pollfds_fill(int64_t *cur_timeout)
{
    GMainContext *context = g_main_context_default();
    int timeout = 0;
    int64_t timeout_ns;
    int n;

    g_main_context_prepare(context, &max_priority);

    glib_pollfds_idx = gpollfds->len;
    n = glib_n_poll_fds;
    do {
        GPollFD *pfds;
        glib_n_poll_fds = n;
        g_array_set_size(gpollfds, glib_pollfds_idx + glib_n_poll_fds);
        pfds = &g_array_index(gpollfds, GPollFD, glib_pollfds_idx);
        n = g_main_context_query(context, max_priority, &timeout, pfds,
                                 glib_n_poll_fds);
    } while (n != glib_n_poll_fds);

    if (timeout < 0) {
        timeout_ns = -1;
    } else {
        timeout_ns = (int64_t)timeout * (int64_t)SCALE_MS;
    }

    *cur_timeout = qemu_soonest_timeout(timeout_ns, *cur_timeout);
}

static void glib_pollfds_poll(void)
{
    GMainContext *context = g_main_context_default();
    GPollFD *pfds = &g_array_index(gpollfds, GPollFD, glib_pollfds_idx);

    if (g_main_context_check(context, max_priority, pfds, glib_n_poll_fds)) {
        g_main_context_dispatch(context);
    }
}

#define MAX_MAIN_LOOP_SPIN (1000)

static int os_host_main_loop_wait(int64_t timeout)
{
    GMainContext *context = g_main_context_default();
    int ret;

    g_main_context_acquire(context);

    glib_pollfds_fill(&timeout);

    bql_unlock();
    replay_mutex_unlock();

    ret = qemu_poll_ns((GPollFD *)gpollfds->data, gpollfds->len, timeout);

    replay_mutex_lock();
    bql_lock();

    glib_pollfds_poll();

    g_main_context_release(context);

    return ret;
}
#else
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
    pe = g_new0(PollingEntry, 1);
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
    int revents[MAXIMUM_WAIT_OBJECTS];
    HANDLE events[MAXIMUM_WAIT_OBJECTS];
    WaitObjectFunc *func[MAXIMUM_WAIT_OBJECTS];
    void *opaque[MAXIMUM_WAIT_OBJECTS];
} WaitObjects;

static WaitObjects wait_objects = {0};

int qemu_add_wait_object(HANDLE handle, WaitObjectFunc *func, void *opaque)
{
    int i;
    WaitObjects *w = &wait_objects;

    if (w->num >= MAXIMUM_WAIT_OBJECTS) {
        return -1;
    }

    for (i = 0; i < w->num; i++) {
        /* check if the same handle is added twice */
        if (w->events[i] == handle) {
            return -1;
        }
    }

    w->events[w->num] = handle;
    w->func[w->num] = func;
    w->opaque[w->num] = opaque;
    w->revents[w->num] = 0;
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
        if (found && i < (MAXIMUM_WAIT_OBJECTS - 1)) {
            w->events[i] = w->events[i + 1];
            w->func[i] = w->func[i + 1];
            w->opaque[i] = w->opaque[i + 1];
            w->revents[i] = w->revents[i + 1];
        }
    }
    if (found) {
        w->num--;
    }
}

static int pollfds_fill(GArray *pollfds, fd_set *rfds, fd_set *wfds,
                        fd_set *xfds)
{
    int nfds = -1;
    int i;

    for (i = 0; i < pollfds->len; i++) {
        GPollFD *pfd = &g_array_index(pollfds, GPollFD, i);
        int fd = pfd->fd;
        int events = pfd->events;
        if (events & G_IO_IN) {
            FD_SET(fd, rfds);
            nfds = MAX(nfds, fd);
        }
        if (events & G_IO_OUT) {
            FD_SET(fd, wfds);
            nfds = MAX(nfds, fd);
        }
        if (events & G_IO_PRI) {
            FD_SET(fd, xfds);
            nfds = MAX(nfds, fd);
        }
    }
    return nfds;
}

static void pollfds_poll(GArray *pollfds, int nfds, fd_set *rfds,
                         fd_set *wfds, fd_set *xfds)
{
    int i;

    for (i = 0; i < pollfds->len; i++) {
        GPollFD *pfd = &g_array_index(pollfds, GPollFD, i);
        int fd = pfd->fd;
        int revents = 0;

        if (FD_ISSET(fd, rfds)) {
            revents |= G_IO_IN;
        }
        if (FD_ISSET(fd, wfds)) {
            revents |= G_IO_OUT;
        }
        if (FD_ISSET(fd, xfds)) {
            revents |= G_IO_PRI;
        }
        pfd->revents = revents & pfd->events;
    }
}

static int os_host_main_loop_wait(int64_t timeout)
{
    GMainContext *context = g_main_context_default();
    GPollFD poll_fds[1024 * 2]; /* this is probably overkill */
    int select_ret = 0;
    int g_poll_ret, ret, i, n_poll_fds;
    PollingEntry *pe;
    WaitObjects *w = &wait_objects;
    gint poll_timeout;
    int64_t poll_timeout_ns;
    static struct timeval tv0;
    fd_set rfds, wfds, xfds;
    int nfds;

    g_main_context_acquire(context);

    /* XXX: need to suppress polling by better using win32 events */
    ret = 0;
    for (pe = first_polling_entry; pe != NULL; pe = pe->next) {
        ret |= pe->func(pe->opaque);
    }
    if (ret != 0) {
        g_main_context_release(context);
        return ret;
    }

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);
    nfds = pollfds_fill(gpollfds, &rfds, &wfds, &xfds);
    if (nfds >= 0) {
        select_ret = select(nfds + 1, &rfds, &wfds, &xfds, &tv0);
        if (select_ret != 0) {
            timeout = 0;
        }
        if (select_ret > 0) {
            pollfds_poll(gpollfds, nfds, &rfds, &wfds, &xfds);
        }
    }

    g_main_context_prepare(context, &max_priority);
    n_poll_fds = g_main_context_query(context, max_priority, &poll_timeout,
                                      poll_fds, ARRAY_SIZE(poll_fds));
    g_assert(n_poll_fds + w->num <= ARRAY_SIZE(poll_fds));

    for (i = 0; i < w->num; i++) {
        poll_fds[n_poll_fds + i].fd = (DWORD_PTR)w->events[i];
        poll_fds[n_poll_fds + i].events = G_IO_IN;
    }

    if (poll_timeout < 0) {
        poll_timeout_ns = -1;
    } else {
        poll_timeout_ns = (int64_t)poll_timeout * (int64_t)SCALE_MS;
    }

    poll_timeout_ns = qemu_soonest_timeout(poll_timeout_ns, timeout);

    bql_unlock();

    replay_mutex_unlock();

    g_poll_ret = qemu_poll_ns(poll_fds, n_poll_fds + w->num, poll_timeout_ns);

    replay_mutex_lock();

    bql_lock();
    if (g_poll_ret > 0) {
        for (i = 0; i < w->num; i++) {
            w->revents[i] = poll_fds[n_poll_fds + i].revents;
        }
        for (i = 0; i < w->num; i++) {
            if (w->revents[i] && w->func[i]) {
                w->func[i](w->opaque[i]);
            }
        }
    }

    if (g_main_context_check(context, max_priority, poll_fds, n_poll_fds)) {
        g_main_context_dispatch(context);
    }

    g_main_context_release(context);

    return select_ret || g_poll_ret;
}
#endif

static NotifierList main_loop_poll_notifiers =
    NOTIFIER_LIST_INITIALIZER(main_loop_poll_notifiers);

void main_loop_poll_add_notifier(Notifier *notify)
{
    notifier_list_add(&main_loop_poll_notifiers, notify);
}

void main_loop_poll_remove_notifier(Notifier *notify)
{
    notifier_remove(notify);
}

void main_loop_wait(int nonblocking)
{
    MainLoopPoll mlpoll = {
        .state = MAIN_LOOP_POLL_FILL,
        .timeout = UINT32_MAX,
        .pollfds = gpollfds,
    };
    int ret;
    int64_t timeout_ns;

    if (nonblocking) {
        mlpoll.timeout = 0;
    }

    /* poll any events */
    g_array_set_size(gpollfds, 0); /* reset for new iteration */
    /* XXX: separate device handlers from system ones */
    notifier_list_notify(&main_loop_poll_notifiers, &mlpoll);

    if (mlpoll.timeout == UINT32_MAX) {
        timeout_ns = -1;
    } else {
        timeout_ns = (uint64_t)mlpoll.timeout * (int64_t)(SCALE_MS);
    }

    timeout_ns = qemu_soonest_timeout(timeout_ns,
                                      timerlistgroup_deadline_ns(
                                          &main_loop_tlg));

    ret = os_host_main_loop_wait(timeout_ns);
    mlpoll.state = ret < 0 ? MAIN_LOOP_POLL_ERR : MAIN_LOOP_POLL_OK;
    notifier_list_notify(&main_loop_poll_notifiers, &mlpoll);

    if (icount_enabled()) {
        /*
         * CPU thread can infinitely wait for event after
         * missing the warp
         */
        icount_start_warp_timer();
    }
    qemu_clock_run_all_timers();
}

/* Functions to operate on the main QEMU AioContext.  */

QEMUBH *qemu_bh_new_full(QEMUBHFunc *cb, void *opaque, const char *name,
                         MemReentrancyGuard *reentrancy_guard)
{
    return aio_bh_new_full(qemu_aio_context, cb, opaque, name,
                           reentrancy_guard);
}

/*
 * Functions to operate on the I/O handler AioContext.
 * This context runs on top of main loop. We can't reuse qemu_aio_context
 * because iohandlers mustn't be polled by aio_poll(qemu_aio_context).
 */
static AioContext *iohandler_ctx;

static void iohandler_init(void)
{
    if (!iohandler_ctx) {
        iohandler_ctx = aio_context_new(&error_abort);
    }
}

AioContext *iohandler_get_aio_context(void)
{
    iohandler_init();
    return iohandler_ctx;
}

GSource *iohandler_get_g_source(void)
{
    iohandler_init();
    return aio_get_g_source(iohandler_ctx);
}

void qemu_set_fd_handler(int fd,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    iohandler_init();
    aio_set_fd_handler(iohandler_ctx, fd, fd_read, fd_write, NULL, NULL,
                       opaque);
}

void event_notifier_set_handler(EventNotifier *e,
                                EventNotifierHandler *handler)
{
    iohandler_init();
    aio_set_event_notifier(iohandler_ctx, e, handler, NULL, NULL);
}
