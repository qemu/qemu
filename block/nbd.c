/*
 * QEMU Block driver for  NBD
 *
 * Copyright (c) 2019 Virtuozzo International GmbH.
 * Copyright (C) 2016 Red Hat, Inc.
 * Copyright (C) 2008 Bull S.A.S.
 *     Author: Laurent Vivier <Laurent.Vivier@bull.net>
 *
 * Some parts:
 *    Copyright (C) 2007 Anthony Liguori <anthony@codemonkey.ws>
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

#include "trace.h"
#include "qemu/uri.h"
#include "qemu/option.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/atomic.h"

#include "qapi/qapi-visit-sockets.h"
#include "qapi/qmp/qstring.h"
#include "qapi/clone-visitor.h"

#include "block/qdict.h"
#include "block/nbd.h"
#include "block/block_int.h"

#include "qemu/yank.h"

#define EN_OPTSTR ":exportname="
#define MAX_NBD_REQUESTS    16

#define HANDLE_TO_INDEX(bs, handle) ((handle) ^ (uint64_t)(intptr_t)(bs))
#define INDEX_TO_HANDLE(bs, index)  ((index)  ^ (uint64_t)(intptr_t)(bs))

typedef struct {
    Coroutine *coroutine;
    uint64_t offset;        /* original offset of the request */
    bool receiving;         /* waiting for connection_co? */
} NBDClientRequest;

typedef enum NBDClientState {
    NBD_CLIENT_CONNECTING_WAIT,
    NBD_CLIENT_CONNECTING_NOWAIT,
    NBD_CLIENT_CONNECTED,
    NBD_CLIENT_QUIT
} NBDClientState;

typedef enum NBDConnectThreadState {
    /* No thread, no pending results */
    CONNECT_THREAD_NONE,

    /* Thread is running, no results for now */
    CONNECT_THREAD_RUNNING,

    /*
     * Thread is running, but requestor exited. Thread should close
     * the new socket and free the connect state on exit.
     */
    CONNECT_THREAD_RUNNING_DETACHED,

    /* Thread finished, results are stored in a state */
    CONNECT_THREAD_FAIL,
    CONNECT_THREAD_SUCCESS
} NBDConnectThreadState;

typedef struct NBDConnectThread {
    /* Initialization constants */
    SocketAddress *saddr; /* address to connect to */
    /*
     * Bottom half to schedule on completion. Scheduled only if bh_ctx is not
     * NULL
     */
    QEMUBHFunc *bh_func;
    void *bh_opaque;

    /*
     * Result of last attempt. Valid in FAIL and SUCCESS states.
     * If you want to steal error, don't forget to set pointer to NULL.
     */
    QIOChannelSocket *sioc;
    Error *err;

    /* state and bh_ctx are protected by mutex */
    QemuMutex mutex;
    NBDConnectThreadState state; /* current state of the thread */
    AioContext *bh_ctx; /* where to schedule bh (NULL means don't schedule) */
} NBDConnectThread;

typedef struct BDRVNBDState {
    QIOChannelSocket *sioc; /* The master data channel */
    QIOChannel *ioc; /* The current I/O channel which may differ (eg TLS) */
    NBDExportInfo info;

    CoMutex send_mutex;
    CoQueue free_sema;
    Coroutine *connection_co;
    Coroutine *teardown_co;
    QemuCoSleepState *connection_co_sleep_ns_state;
    bool drained;
    bool wait_drained_end;
    int in_flight;
    NBDClientState state;
    int connect_status;
    Error *connect_err;
    bool wait_in_flight;

    QEMUTimer *reconnect_delay_timer;

    NBDClientRequest requests[MAX_NBD_REQUESTS];
    NBDReply reply;
    BlockDriverState *bs;

    /* Connection parameters */
    uint32_t reconnect_delay;
    SocketAddress *saddr;
    char *export, *tlscredsid;
    QCryptoTLSCreds *tlscreds;
    const char *hostname;
    char *x_dirty_bitmap;
    bool alloc_depth;

    bool wait_connect;
    NBDConnectThread *connect_thread;
} BDRVNBDState;

static int nbd_establish_connection(BlockDriverState *bs, SocketAddress *saddr,
                                    Error **errp);
static int nbd_co_establish_connection(BlockDriverState *bs, Error **errp);
static void nbd_co_establish_connection_cancel(BlockDriverState *bs,
                                               bool detach);
static int nbd_client_handshake(BlockDriverState *bs, Error **errp);
static void nbd_yank(void *opaque);

static void nbd_clear_bdrvstate(BDRVNBDState *s)
{
    object_unref(OBJECT(s->tlscreds));
    qapi_free_SocketAddress(s->saddr);
    s->saddr = NULL;
    g_free(s->export);
    s->export = NULL;
    g_free(s->tlscredsid);
    s->tlscredsid = NULL;
    g_free(s->x_dirty_bitmap);
    s->x_dirty_bitmap = NULL;
}

static void nbd_channel_error(BDRVNBDState *s, int ret)
{
    if (ret == -EIO) {
        if (qatomic_load_acquire(&s->state) == NBD_CLIENT_CONNECTED) {
            s->state = s->reconnect_delay ? NBD_CLIENT_CONNECTING_WAIT :
                                            NBD_CLIENT_CONNECTING_NOWAIT;
        }
    } else {
        if (qatomic_load_acquire(&s->state) == NBD_CLIENT_CONNECTED) {
            qio_channel_shutdown(s->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        }
        s->state = NBD_CLIENT_QUIT;
    }
}

static void nbd_recv_coroutines_wake_all(BDRVNBDState *s)
{
    int i;

    for (i = 0; i < MAX_NBD_REQUESTS; i++) {
        NBDClientRequest *req = &s->requests[i];

        if (req->coroutine && req->receiving) {
            aio_co_wake(req->coroutine);
        }
    }
}

static void reconnect_delay_timer_del(BDRVNBDState *s)
{
    if (s->reconnect_delay_timer) {
        timer_free(s->reconnect_delay_timer);
        s->reconnect_delay_timer = NULL;
    }
}

static void reconnect_delay_timer_cb(void *opaque)
{
    BDRVNBDState *s = opaque;

    if (qatomic_load_acquire(&s->state) == NBD_CLIENT_CONNECTING_WAIT) {
        s->state = NBD_CLIENT_CONNECTING_NOWAIT;
        while (qemu_co_enter_next(&s->free_sema, NULL)) {
            /* Resume all queued requests */
        }
    }

    reconnect_delay_timer_del(s);
}

static void reconnect_delay_timer_init(BDRVNBDState *s, uint64_t expire_time_ns)
{
    if (qatomic_load_acquire(&s->state) != NBD_CLIENT_CONNECTING_WAIT) {
        return;
    }

    assert(!s->reconnect_delay_timer);
    s->reconnect_delay_timer = aio_timer_new(bdrv_get_aio_context(s->bs),
                                             QEMU_CLOCK_REALTIME,
                                             SCALE_NS,
                                             reconnect_delay_timer_cb, s);
    timer_mod(s->reconnect_delay_timer, expire_time_ns);
}

static void nbd_client_detach_aio_context(BlockDriverState *bs)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    /* Timer is deleted in nbd_client_co_drain_begin() */
    assert(!s->reconnect_delay_timer);
    /*
     * If reconnect is in progress we may have no ->ioc.  It will be
     * re-instantiated in the proper aio context once the connection is
     * reestablished.
     */
    if (s->ioc) {
        qio_channel_detach_aio_context(QIO_CHANNEL(s->ioc));
    }
}

static void nbd_client_attach_aio_context_bh(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    if (s->connection_co) {
        /*
         * The node is still drained, so we know the coroutine has yielded in
         * nbd_read_eof(), the only place where bs->in_flight can reach 0, or
         * it is entered for the first time. Both places are safe for entering
         * the coroutine.
         */
        qemu_aio_coroutine_enter(bs->aio_context, s->connection_co);
    }
    bdrv_dec_in_flight(bs);
}

static void nbd_client_attach_aio_context(BlockDriverState *bs,
                                          AioContext *new_context)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    /*
     * s->connection_co is either yielded from nbd_receive_reply or from
     * nbd_co_reconnect_loop()
     */
    if (qatomic_load_acquire(&s->state) == NBD_CLIENT_CONNECTED) {
        qio_channel_attach_aio_context(QIO_CHANNEL(s->ioc), new_context);
    }

    bdrv_inc_in_flight(bs);

    /*
     * Need to wait here for the BH to run because the BH must run while the
     * node is still drained.
     */
    aio_wait_bh_oneshot(new_context, nbd_client_attach_aio_context_bh, bs);
}

static void coroutine_fn nbd_client_co_drain_begin(BlockDriverState *bs)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    s->drained = true;
    if (s->connection_co_sleep_ns_state) {
        qemu_co_sleep_wake(s->connection_co_sleep_ns_state);
    }

    nbd_co_establish_connection_cancel(bs, false);

    reconnect_delay_timer_del(s);

    if (qatomic_load_acquire(&s->state) == NBD_CLIENT_CONNECTING_WAIT) {
        s->state = NBD_CLIENT_CONNECTING_NOWAIT;
        qemu_co_queue_restart_all(&s->free_sema);
    }
}

static void coroutine_fn nbd_client_co_drain_end(BlockDriverState *bs)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    s->drained = false;
    if (s->wait_drained_end) {
        s->wait_drained_end = false;
        aio_co_wake(s->connection_co);
    }
}


static void nbd_teardown_connection(BlockDriverState *bs)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    if (s->ioc) {
        /* finish any pending coroutines */
        qio_channel_shutdown(s->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
    } else if (s->sioc) {
        /* abort negotiation */
        qio_channel_shutdown(QIO_CHANNEL(s->sioc), QIO_CHANNEL_SHUTDOWN_BOTH,
                             NULL);
    }

    s->state = NBD_CLIENT_QUIT;
    if (s->connection_co) {
        if (s->connection_co_sleep_ns_state) {
            qemu_co_sleep_wake(s->connection_co_sleep_ns_state);
        }
        nbd_co_establish_connection_cancel(bs, true);
    }
    if (qemu_in_coroutine()) {
        s->teardown_co = qemu_coroutine_self();
        /* connection_co resumes us when it terminates */
        qemu_coroutine_yield();
        s->teardown_co = NULL;
    } else {
        BDRV_POLL_WHILE(bs, s->connection_co);
    }
    assert(!s->connection_co);
}

static bool nbd_client_connecting(BDRVNBDState *s)
{
    NBDClientState state = qatomic_load_acquire(&s->state);
    return state == NBD_CLIENT_CONNECTING_WAIT ||
        state == NBD_CLIENT_CONNECTING_NOWAIT;
}

static bool nbd_client_connecting_wait(BDRVNBDState *s)
{
    return qatomic_load_acquire(&s->state) == NBD_CLIENT_CONNECTING_WAIT;
}

static void connect_bh(void *opaque)
{
    BDRVNBDState *state = opaque;

    assert(state->wait_connect);
    state->wait_connect = false;
    aio_co_wake(state->connection_co);
}

static void nbd_init_connect_thread(BDRVNBDState *s)
{
    s->connect_thread = g_new(NBDConnectThread, 1);

    *s->connect_thread = (NBDConnectThread) {
        .saddr = QAPI_CLONE(SocketAddress, s->saddr),
        .state = CONNECT_THREAD_NONE,
        .bh_func = connect_bh,
        .bh_opaque = s,
    };

    qemu_mutex_init(&s->connect_thread->mutex);
}

static void nbd_free_connect_thread(NBDConnectThread *thr)
{
    if (thr->sioc) {
        qio_channel_close(QIO_CHANNEL(thr->sioc), NULL);
    }
    error_free(thr->err);
    qapi_free_SocketAddress(thr->saddr);
    g_free(thr);
}

static void *connect_thread_func(void *opaque)
{
    NBDConnectThread *thr = opaque;
    int ret;
    bool do_free = false;

    thr->sioc = qio_channel_socket_new();

    error_free(thr->err);
    thr->err = NULL;
    ret = qio_channel_socket_connect_sync(thr->sioc, thr->saddr, &thr->err);
    if (ret < 0) {
        object_unref(OBJECT(thr->sioc));
        thr->sioc = NULL;
    }

    qemu_mutex_lock(&thr->mutex);

    switch (thr->state) {
    case CONNECT_THREAD_RUNNING:
        thr->state = ret < 0 ? CONNECT_THREAD_FAIL : CONNECT_THREAD_SUCCESS;
        if (thr->bh_ctx) {
            aio_bh_schedule_oneshot(thr->bh_ctx, thr->bh_func, thr->bh_opaque);

            /* play safe, don't reuse bh_ctx on further connection attempts */
            thr->bh_ctx = NULL;
        }
        break;
    case CONNECT_THREAD_RUNNING_DETACHED:
        do_free = true;
        break;
    default:
        abort();
    }

    qemu_mutex_unlock(&thr->mutex);

    if (do_free) {
        nbd_free_connect_thread(thr);
    }

    return NULL;
}

static int coroutine_fn
nbd_co_establish_connection(BlockDriverState *bs, Error **errp)
{
    int ret;
    QemuThread thread;
    BDRVNBDState *s = bs->opaque;
    NBDConnectThread *thr = s->connect_thread;

    qemu_mutex_lock(&thr->mutex);

    switch (thr->state) {
    case CONNECT_THREAD_FAIL:
    case CONNECT_THREAD_NONE:
        error_free(thr->err);
        thr->err = NULL;
        thr->state = CONNECT_THREAD_RUNNING;
        qemu_thread_create(&thread, "nbd-connect",
                           connect_thread_func, thr, QEMU_THREAD_DETACHED);
        break;
    case CONNECT_THREAD_SUCCESS:
        /* Previous attempt finally succeeded in background */
        thr->state = CONNECT_THREAD_NONE;
        s->sioc = thr->sioc;
        thr->sioc = NULL;
        yank_register_function(BLOCKDEV_YANK_INSTANCE(bs->node_name),
                               nbd_yank, bs);
        qemu_mutex_unlock(&thr->mutex);
        return 0;
    case CONNECT_THREAD_RUNNING:
        /* Already running, will wait */
        break;
    default:
        abort();
    }

    thr->bh_ctx = qemu_get_current_aio_context();

    qemu_mutex_unlock(&thr->mutex);


    /*
     * We are going to wait for connect-thread finish, but
     * nbd_client_co_drain_begin() can interrupt.
     *
     * Note that wait_connect variable is not visible for connect-thread. It
     * doesn't need mutex protection, it used only inside home aio context of
     * bs.
     */
    s->wait_connect = true;
    qemu_coroutine_yield();

    qemu_mutex_lock(&thr->mutex);

    switch (thr->state) {
    case CONNECT_THREAD_SUCCESS:
    case CONNECT_THREAD_FAIL:
        thr->state = CONNECT_THREAD_NONE;
        error_propagate(errp, thr->err);
        thr->err = NULL;
        s->sioc = thr->sioc;
        thr->sioc = NULL;
        if (s->sioc) {
            yank_register_function(BLOCKDEV_YANK_INSTANCE(bs->node_name),
                                   nbd_yank, bs);
        }
        ret = (s->sioc ? 0 : -1);
        break;
    case CONNECT_THREAD_RUNNING:
    case CONNECT_THREAD_RUNNING_DETACHED:
        /*
         * Obviously, drained section wants to start. Report the attempt as
         * failed. Still connect thread is executing in background, and its
         * result may be used for next connection attempt.
         */
        ret = -1;
        error_setg(errp, "Connection attempt cancelled by other operation");
        break;

    case CONNECT_THREAD_NONE:
        /*
         * Impossible. We've seen this thread running. So it should be
         * running or at least give some results.
         */
        abort();

    default:
        abort();
    }

    qemu_mutex_unlock(&thr->mutex);

    return ret;
}

/*
 * nbd_co_establish_connection_cancel
 * Cancel nbd_co_establish_connection asynchronously: it will finish soon, to
 * allow drained section to begin.
 *
 * If detach is true, also cleanup the state (or if thread is running, move it
 * to CONNECT_THREAD_RUNNING_DETACHED state). s->connect_thread becomes NULL if
 * detach is true.
 */
static void nbd_co_establish_connection_cancel(BlockDriverState *bs,
                                               bool detach)
{
    BDRVNBDState *s = bs->opaque;
    NBDConnectThread *thr = s->connect_thread;
    bool wake = false;
    bool do_free = false;

    qemu_mutex_lock(&thr->mutex);

    if (thr->state == CONNECT_THREAD_RUNNING) {
        /* We can cancel only in running state, when bh is not yet scheduled */
        thr->bh_ctx = NULL;
        if (s->wait_connect) {
            s->wait_connect = false;
            wake = true;
        }
        if (detach) {
            thr->state = CONNECT_THREAD_RUNNING_DETACHED;
            s->connect_thread = NULL;
        }
    } else if (detach) {
        do_free = true;
    }

    qemu_mutex_unlock(&thr->mutex);

    if (do_free) {
        nbd_free_connect_thread(thr);
        s->connect_thread = NULL;
    }

    if (wake) {
        aio_co_wake(s->connection_co);
    }
}

static coroutine_fn void nbd_reconnect_attempt(BDRVNBDState *s)
{
    int ret;
    Error *local_err = NULL;

    if (!nbd_client_connecting(s)) {
        return;
    }

    /* Wait for completion of all in-flight requests */

    qemu_co_mutex_lock(&s->send_mutex);

    while (s->in_flight > 0) {
        qemu_co_mutex_unlock(&s->send_mutex);
        nbd_recv_coroutines_wake_all(s);
        s->wait_in_flight = true;
        qemu_coroutine_yield();
        s->wait_in_flight = false;
        qemu_co_mutex_lock(&s->send_mutex);
    }

    qemu_co_mutex_unlock(&s->send_mutex);

    if (!nbd_client_connecting(s)) {
        return;
    }

    /*
     * Now we are sure that nobody is accessing the channel, and no one will
     * try until we set the state to CONNECTED.
     */

    /* Finalize previous connection if any */
    if (s->ioc) {
        qio_channel_detach_aio_context(QIO_CHANNEL(s->ioc));
        yank_unregister_function(BLOCKDEV_YANK_INSTANCE(s->bs->node_name),
                                 nbd_yank, s->bs);
        object_unref(OBJECT(s->sioc));
        s->sioc = NULL;
        object_unref(OBJECT(s->ioc));
        s->ioc = NULL;
    }

    if (nbd_co_establish_connection(s->bs, &local_err) < 0) {
        ret = -ECONNREFUSED;
        goto out;
    }

    bdrv_dec_in_flight(s->bs);

    ret = nbd_client_handshake(s->bs, &local_err);

    if (s->drained) {
        s->wait_drained_end = true;
        while (s->drained) {
            /*
             * We may be entered once from nbd_client_attach_aio_context_bh
             * and then from nbd_client_co_drain_end. So here is a loop.
             */
            qemu_coroutine_yield();
        }
    }
    bdrv_inc_in_flight(s->bs);

out:
    s->connect_status = ret;
    error_free(s->connect_err);
    s->connect_err = NULL;
    error_propagate(&s->connect_err, local_err);

    if (ret >= 0) {
        /* successfully connected */
        s->state = NBD_CLIENT_CONNECTED;
        qemu_co_queue_restart_all(&s->free_sema);
    }
}

static coroutine_fn void nbd_co_reconnect_loop(BDRVNBDState *s)
{
    uint64_t timeout = 1 * NANOSECONDS_PER_SECOND;
    uint64_t max_timeout = 16 * NANOSECONDS_PER_SECOND;

    if (qatomic_load_acquire(&s->state) == NBD_CLIENT_CONNECTING_WAIT) {
        reconnect_delay_timer_init(s, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) +
                                   s->reconnect_delay * NANOSECONDS_PER_SECOND);
    }

    nbd_reconnect_attempt(s);

    while (nbd_client_connecting(s)) {
        if (s->drained) {
            bdrv_dec_in_flight(s->bs);
            s->wait_drained_end = true;
            while (s->drained) {
                /*
                 * We may be entered once from nbd_client_attach_aio_context_bh
                 * and then from nbd_client_co_drain_end. So here is a loop.
                 */
                qemu_coroutine_yield();
            }
            bdrv_inc_in_flight(s->bs);
        } else {
            qemu_co_sleep_ns_wakeable(QEMU_CLOCK_REALTIME, timeout,
                                      &s->connection_co_sleep_ns_state);
            if (s->drained) {
                continue;
            }
            if (timeout < max_timeout) {
                timeout *= 2;
            }
        }

        nbd_reconnect_attempt(s);
    }

    reconnect_delay_timer_del(s);
}

static coroutine_fn void nbd_connection_entry(void *opaque)
{
    BDRVNBDState *s = opaque;
    uint64_t i;
    int ret = 0;
    Error *local_err = NULL;

    while (qatomic_load_acquire(&s->state) != NBD_CLIENT_QUIT) {
        /*
         * The NBD client can only really be considered idle when it has
         * yielded from qio_channel_readv_all_eof(), waiting for data. This is
         * the point where the additional scheduled coroutine entry happens
         * after nbd_client_attach_aio_context().
         *
         * Therefore we keep an additional in_flight reference all the time and
         * only drop it temporarily here.
         */

        if (nbd_client_connecting(s)) {
            nbd_co_reconnect_loop(s);
        }

        if (qatomic_load_acquire(&s->state) != NBD_CLIENT_CONNECTED) {
            continue;
        }

        assert(s->reply.handle == 0);
        ret = nbd_receive_reply(s->bs, s->ioc, &s->reply, &local_err);

        if (local_err) {
            trace_nbd_read_reply_entry_fail(ret, error_get_pretty(local_err));
            error_free(local_err);
            local_err = NULL;
        }
        if (ret <= 0) {
            nbd_channel_error(s, ret ? ret : -EIO);
            continue;
        }

        /*
         * There's no need for a mutex on the receive side, because the
         * handler acts as a synchronization point and ensures that only
         * one coroutine is called until the reply finishes.
         */
        i = HANDLE_TO_INDEX(s, s->reply.handle);
        if (i >= MAX_NBD_REQUESTS ||
            !s->requests[i].coroutine ||
            !s->requests[i].receiving ||
            (nbd_reply_is_structured(&s->reply) && !s->info.structured_reply))
        {
            nbd_channel_error(s, -EINVAL);
            continue;
        }

        /*
         * We're woken up again by the request itself.  Note that there
         * is no race between yielding and reentering connection_co.  This
         * is because:
         *
         * - if the request runs on the same AioContext, it is only
         *   entered after we yield
         *
         * - if the request runs on a different AioContext, reentering
         *   connection_co happens through a bottom half, which can only
         *   run after we yield.
         */
        aio_co_wake(s->requests[i].coroutine);
        qemu_coroutine_yield();
    }

    qemu_co_queue_restart_all(&s->free_sema);
    nbd_recv_coroutines_wake_all(s);
    bdrv_dec_in_flight(s->bs);

    s->connection_co = NULL;
    if (s->ioc) {
        qio_channel_detach_aio_context(QIO_CHANNEL(s->ioc));
        yank_unregister_function(BLOCKDEV_YANK_INSTANCE(s->bs->node_name),
                                 nbd_yank, s->bs);
        object_unref(OBJECT(s->sioc));
        s->sioc = NULL;
        object_unref(OBJECT(s->ioc));
        s->ioc = NULL;
    }

    if (s->teardown_co) {
        aio_co_wake(s->teardown_co);
    }
    aio_wait_kick();
}

static int nbd_co_send_request(BlockDriverState *bs,
                               NBDRequest *request,
                               QEMUIOVector *qiov)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    int rc, i = -1;

    qemu_co_mutex_lock(&s->send_mutex);
    while (s->in_flight == MAX_NBD_REQUESTS || nbd_client_connecting_wait(s)) {
        qemu_co_queue_wait(&s->free_sema, &s->send_mutex);
    }

    if (qatomic_load_acquire(&s->state) != NBD_CLIENT_CONNECTED) {
        rc = -EIO;
        goto err;
    }

    s->in_flight++;

    for (i = 0; i < MAX_NBD_REQUESTS; i++) {
        if (s->requests[i].coroutine == NULL) {
            break;
        }
    }

    g_assert(qemu_in_coroutine());
    assert(i < MAX_NBD_REQUESTS);

    s->requests[i].coroutine = qemu_coroutine_self();
    s->requests[i].offset = request->from;
    s->requests[i].receiving = false;

    request->handle = INDEX_TO_HANDLE(s, i);

    assert(s->ioc);

    if (qiov) {
        qio_channel_set_cork(s->ioc, true);
        rc = nbd_send_request(s->ioc, request);
        if (qatomic_load_acquire(&s->state) == NBD_CLIENT_CONNECTED &&
            rc >= 0) {
            if (qio_channel_writev_all(s->ioc, qiov->iov, qiov->niov,
                                       NULL) < 0) {
                rc = -EIO;
            }
        } else if (rc >= 0) {
            rc = -EIO;
        }
        qio_channel_set_cork(s->ioc, false);
    } else {
        rc = nbd_send_request(s->ioc, request);
    }

err:
    if (rc < 0) {
        nbd_channel_error(s, rc);
        if (i != -1) {
            s->requests[i].coroutine = NULL;
            s->in_flight--;
        }
        if (s->in_flight == 0 && s->wait_in_flight) {
            aio_co_wake(s->connection_co);
        } else {
            qemu_co_queue_next(&s->free_sema);
        }
    }
    qemu_co_mutex_unlock(&s->send_mutex);
    return rc;
}

static inline uint16_t payload_advance16(uint8_t **payload)
{
    *payload += 2;
    return lduw_be_p(*payload - 2);
}

static inline uint32_t payload_advance32(uint8_t **payload)
{
    *payload += 4;
    return ldl_be_p(*payload - 4);
}

static inline uint64_t payload_advance64(uint8_t **payload)
{
    *payload += 8;
    return ldq_be_p(*payload - 8);
}

static int nbd_parse_offset_hole_payload(BDRVNBDState *s,
                                         NBDStructuredReplyChunk *chunk,
                                         uint8_t *payload, uint64_t orig_offset,
                                         QEMUIOVector *qiov, Error **errp)
{
    uint64_t offset;
    uint32_t hole_size;

    if (chunk->length != sizeof(offset) + sizeof(hole_size)) {
        error_setg(errp, "Protocol error: invalid payload for "
                         "NBD_REPLY_TYPE_OFFSET_HOLE");
        return -EINVAL;
    }

    offset = payload_advance64(&payload);
    hole_size = payload_advance32(&payload);

    if (!hole_size || offset < orig_offset || hole_size > qiov->size ||
        offset > orig_offset + qiov->size - hole_size) {
        error_setg(errp, "Protocol error: server sent chunk exceeding requested"
                         " region");
        return -EINVAL;
    }
    if (s->info.min_block &&
        !QEMU_IS_ALIGNED(hole_size, s->info.min_block)) {
        trace_nbd_structured_read_compliance("hole");
    }

    qemu_iovec_memset(qiov, offset - orig_offset, 0, hole_size);

    return 0;
}

/*
 * nbd_parse_blockstatus_payload
 * Based on our request, we expect only one extent in reply, for the
 * base:allocation context.
 */
static int nbd_parse_blockstatus_payload(BDRVNBDState *s,
                                         NBDStructuredReplyChunk *chunk,
                                         uint8_t *payload, uint64_t orig_length,
                                         NBDExtent *extent, Error **errp)
{
    uint32_t context_id;

    /* The server succeeded, so it must have sent [at least] one extent */
    if (chunk->length < sizeof(context_id) + sizeof(*extent)) {
        error_setg(errp, "Protocol error: invalid payload for "
                         "NBD_REPLY_TYPE_BLOCK_STATUS");
        return -EINVAL;
    }

    context_id = payload_advance32(&payload);
    if (s->info.context_id != context_id) {
        error_setg(errp, "Protocol error: unexpected context id %d for "
                         "NBD_REPLY_TYPE_BLOCK_STATUS, when negotiated context "
                         "id is %d", context_id,
                         s->info.context_id);
        return -EINVAL;
    }

    extent->length = payload_advance32(&payload);
    extent->flags = payload_advance32(&payload);

    if (extent->length == 0) {
        error_setg(errp, "Protocol error: server sent status chunk with "
                   "zero length");
        return -EINVAL;
    }

    /*
     * A server sending unaligned block status is in violation of the
     * protocol, but as qemu-nbd 3.1 is such a server (at least for
     * POSIX files that are not a multiple of 512 bytes, since qemu
     * rounds files up to 512-byte multiples but lseek(SEEK_HOLE)
     * still sees an implicit hole beyond the real EOF), it's nicer to
     * work around the misbehaving server. If the request included
     * more than the final unaligned block, truncate it back to an
     * aligned result; if the request was only the final block, round
     * up to the full block and change the status to fully-allocated
     * (always a safe status, even if it loses information).
     */
    if (s->info.min_block && !QEMU_IS_ALIGNED(extent->length,
                                                   s->info.min_block)) {
        trace_nbd_parse_blockstatus_compliance("extent length is unaligned");
        if (extent->length > s->info.min_block) {
            extent->length = QEMU_ALIGN_DOWN(extent->length,
                                             s->info.min_block);
        } else {
            extent->length = s->info.min_block;
            extent->flags = 0;
        }
    }

    /*
     * We used NBD_CMD_FLAG_REQ_ONE, so the server should not have
     * sent us any more than one extent, nor should it have included
     * status beyond our request in that extent. However, it's easy
     * enough to ignore the server's noncompliance without killing the
     * connection; just ignore trailing extents, and clamp things to
     * the length of our request.
     */
    if (chunk->length > sizeof(context_id) + sizeof(*extent)) {
        trace_nbd_parse_blockstatus_compliance("more than one extent");
    }
    if (extent->length > orig_length) {
        extent->length = orig_length;
        trace_nbd_parse_blockstatus_compliance("extent length too large");
    }

    /*
     * HACK: if we are using x-dirty-bitmaps to access
     * qemu:allocation-depth, treat all depths > 2 the same as 2,
     * since nbd_client_co_block_status is only expecting the low two
     * bits to be set.
     */
    if (s->alloc_depth && extent->flags > 2) {
        extent->flags = 2;
    }

    return 0;
}

/*
 * nbd_parse_error_payload
 * on success @errp contains message describing nbd error reply
 */
static int nbd_parse_error_payload(NBDStructuredReplyChunk *chunk,
                                   uint8_t *payload, int *request_ret,
                                   Error **errp)
{
    uint32_t error;
    uint16_t message_size;

    assert(chunk->type & (1 << 15));

    if (chunk->length < sizeof(error) + sizeof(message_size)) {
        error_setg(errp,
                   "Protocol error: invalid payload for structured error");
        return -EINVAL;
    }

    error = nbd_errno_to_system_errno(payload_advance32(&payload));
    if (error == 0) {
        error_setg(errp, "Protocol error: server sent structured error chunk "
                         "with error = 0");
        return -EINVAL;
    }

    *request_ret = -error;
    message_size = payload_advance16(&payload);

    if (message_size > chunk->length - sizeof(error) - sizeof(message_size)) {
        error_setg(errp, "Protocol error: server sent structured error chunk "
                         "with incorrect message size");
        return -EINVAL;
    }

    /* TODO: Add a trace point to mention the server complaint */

    /* TODO handle ERROR_OFFSET */

    return 0;
}

static int nbd_co_receive_offset_data_payload(BDRVNBDState *s,
                                              uint64_t orig_offset,
                                              QEMUIOVector *qiov, Error **errp)
{
    QEMUIOVector sub_qiov;
    uint64_t offset;
    size_t data_size;
    int ret;
    NBDStructuredReplyChunk *chunk = &s->reply.structured;

    assert(nbd_reply_is_structured(&s->reply));

    /* The NBD spec requires at least one byte of payload */
    if (chunk->length <= sizeof(offset)) {
        error_setg(errp, "Protocol error: invalid payload for "
                         "NBD_REPLY_TYPE_OFFSET_DATA");
        return -EINVAL;
    }

    if (nbd_read64(s->ioc, &offset, "OFFSET_DATA offset", errp) < 0) {
        return -EIO;
    }

    data_size = chunk->length - sizeof(offset);
    assert(data_size);
    if (offset < orig_offset || data_size > qiov->size ||
        offset > orig_offset + qiov->size - data_size) {
        error_setg(errp, "Protocol error: server sent chunk exceeding requested"
                         " region");
        return -EINVAL;
    }
    if (s->info.min_block && !QEMU_IS_ALIGNED(data_size, s->info.min_block)) {
        trace_nbd_structured_read_compliance("data");
    }

    qemu_iovec_init(&sub_qiov, qiov->niov);
    qemu_iovec_concat(&sub_qiov, qiov, offset - orig_offset, data_size);
    ret = qio_channel_readv_all(s->ioc, sub_qiov.iov, sub_qiov.niov, errp);
    qemu_iovec_destroy(&sub_qiov);

    return ret < 0 ? -EIO : 0;
}

#define NBD_MAX_MALLOC_PAYLOAD 1000
static coroutine_fn int nbd_co_receive_structured_payload(
        BDRVNBDState *s, void **payload, Error **errp)
{
    int ret;
    uint32_t len;

    assert(nbd_reply_is_structured(&s->reply));

    len = s->reply.structured.length;

    if (len == 0) {
        return 0;
    }

    if (payload == NULL) {
        error_setg(errp, "Unexpected structured payload");
        return -EINVAL;
    }

    if (len > NBD_MAX_MALLOC_PAYLOAD) {
        error_setg(errp, "Payload too large");
        return -EINVAL;
    }

    *payload = g_new(char, len);
    ret = nbd_read(s->ioc, *payload, len, "structured payload", errp);
    if (ret < 0) {
        g_free(*payload);
        *payload = NULL;
        return ret;
    }

    return 0;
}

/*
 * nbd_co_do_receive_one_chunk
 * for simple reply:
 *   set request_ret to received reply error
 *   if qiov is not NULL: read payload to @qiov
 * for structured reply chunk:
 *   if error chunk: read payload, set @request_ret, do not set @payload
 *   else if offset_data chunk: read payload data to @qiov, do not set @payload
 *   else: read payload to @payload
 *
 * If function fails, @errp contains corresponding error message, and the
 * connection with the server is suspect.  If it returns 0, then the
 * transaction succeeded (although @request_ret may be a negative errno
 * corresponding to the server's error reply), and errp is unchanged.
 */
static coroutine_fn int nbd_co_do_receive_one_chunk(
        BDRVNBDState *s, uint64_t handle, bool only_structured,
        int *request_ret, QEMUIOVector *qiov, void **payload, Error **errp)
{
    int ret;
    int i = HANDLE_TO_INDEX(s, handle);
    void *local_payload = NULL;
    NBDStructuredReplyChunk *chunk;

    if (payload) {
        *payload = NULL;
    }
    *request_ret = 0;

    /* Wait until we're woken up by nbd_connection_entry.  */
    s->requests[i].receiving = true;
    qemu_coroutine_yield();
    s->requests[i].receiving = false;
    if (qatomic_load_acquire(&s->state) != NBD_CLIENT_CONNECTED) {
        error_setg(errp, "Connection closed");
        return -EIO;
    }
    assert(s->ioc);

    assert(s->reply.handle == handle);

    if (nbd_reply_is_simple(&s->reply)) {
        if (only_structured) {
            error_setg(errp, "Protocol error: simple reply when structured "
                             "reply chunk was expected");
            return -EINVAL;
        }

        *request_ret = -nbd_errno_to_system_errno(s->reply.simple.error);
        if (*request_ret < 0 || !qiov) {
            return 0;
        }

        return qio_channel_readv_all(s->ioc, qiov->iov, qiov->niov,
                                     errp) < 0 ? -EIO : 0;
    }

    /* handle structured reply chunk */
    assert(s->info.structured_reply);
    chunk = &s->reply.structured;

    if (chunk->type == NBD_REPLY_TYPE_NONE) {
        if (!(chunk->flags & NBD_REPLY_FLAG_DONE)) {
            error_setg(errp, "Protocol error: NBD_REPLY_TYPE_NONE chunk without"
                       " NBD_REPLY_FLAG_DONE flag set");
            return -EINVAL;
        }
        if (chunk->length) {
            error_setg(errp, "Protocol error: NBD_REPLY_TYPE_NONE chunk with"
                       " nonzero length");
            return -EINVAL;
        }
        return 0;
    }

    if (chunk->type == NBD_REPLY_TYPE_OFFSET_DATA) {
        if (!qiov) {
            error_setg(errp, "Unexpected NBD_REPLY_TYPE_OFFSET_DATA chunk");
            return -EINVAL;
        }

        return nbd_co_receive_offset_data_payload(s, s->requests[i].offset,
                                                  qiov, errp);
    }

    if (nbd_reply_type_is_error(chunk->type)) {
        payload = &local_payload;
    }

    ret = nbd_co_receive_structured_payload(s, payload, errp);
    if (ret < 0) {
        return ret;
    }

    if (nbd_reply_type_is_error(chunk->type)) {
        ret = nbd_parse_error_payload(chunk, local_payload, request_ret, errp);
        g_free(local_payload);
        return ret;
    }

    return 0;
}

/*
 * nbd_co_receive_one_chunk
 * Read reply, wake up connection_co and set s->quit if needed.
 * Return value is a fatal error code or normal nbd reply error code
 */
static coroutine_fn int nbd_co_receive_one_chunk(
        BDRVNBDState *s, uint64_t handle, bool only_structured,
        int *request_ret, QEMUIOVector *qiov, NBDReply *reply, void **payload,
        Error **errp)
{
    int ret = nbd_co_do_receive_one_chunk(s, handle, only_structured,
                                          request_ret, qiov, payload, errp);

    if (ret < 0) {
        memset(reply, 0, sizeof(*reply));
        nbd_channel_error(s, ret);
    } else {
        /* For assert at loop start in nbd_connection_entry */
        *reply = s->reply;
    }
    s->reply.handle = 0;

    if (s->connection_co && !s->wait_in_flight) {
        /*
         * We must check s->wait_in_flight, because we may entered by
         * nbd_recv_coroutines_wake_all(), in this case we should not
         * wake connection_co here, it will woken by last request.
         */
        aio_co_wake(s->connection_co);
    }

    return ret;
}

typedef struct NBDReplyChunkIter {
    int ret;
    int request_ret;
    Error *err;
    bool done, only_structured;
} NBDReplyChunkIter;

static void nbd_iter_channel_error(NBDReplyChunkIter *iter,
                                   int ret, Error **local_err)
{
    assert(local_err && *local_err);
    assert(ret < 0);

    if (!iter->ret) {
        iter->ret = ret;
        error_propagate(&iter->err, *local_err);
    } else {
        error_free(*local_err);
    }

    *local_err = NULL;
}

static void nbd_iter_request_error(NBDReplyChunkIter *iter, int ret)
{
    assert(ret < 0);

    if (!iter->request_ret) {
        iter->request_ret = ret;
    }
}

/*
 * NBD_FOREACH_REPLY_CHUNK
 * The pointer stored in @payload requires g_free() to free it.
 */
#define NBD_FOREACH_REPLY_CHUNK(s, iter, handle, structured, \
                                qiov, reply, payload) \
    for (iter = (NBDReplyChunkIter) { .only_structured = structured }; \
         nbd_reply_chunk_iter_receive(s, &iter, handle, qiov, reply, payload);)

/*
 * nbd_reply_chunk_iter_receive
 * The pointer stored in @payload requires g_free() to free it.
 */
static bool nbd_reply_chunk_iter_receive(BDRVNBDState *s,
                                         NBDReplyChunkIter *iter,
                                         uint64_t handle,
                                         QEMUIOVector *qiov, NBDReply *reply,
                                         void **payload)
{
    int ret, request_ret;
    NBDReply local_reply;
    NBDStructuredReplyChunk *chunk;
    Error *local_err = NULL;
    if (qatomic_load_acquire(&s->state) != NBD_CLIENT_CONNECTED) {
        error_setg(&local_err, "Connection closed");
        nbd_iter_channel_error(iter, -EIO, &local_err);
        goto break_loop;
    }

    if (iter->done) {
        /* Previous iteration was last. */
        goto break_loop;
    }

    if (reply == NULL) {
        reply = &local_reply;
    }

    ret = nbd_co_receive_one_chunk(s, handle, iter->only_structured,
                                   &request_ret, qiov, reply, payload,
                                   &local_err);
    if (ret < 0) {
        nbd_iter_channel_error(iter, ret, &local_err);
    } else if (request_ret < 0) {
        nbd_iter_request_error(iter, request_ret);
    }

    /* Do not execute the body of NBD_FOREACH_REPLY_CHUNK for simple reply. */
    if (nbd_reply_is_simple(reply) ||
        qatomic_load_acquire(&s->state) != NBD_CLIENT_CONNECTED) {
        goto break_loop;
    }

    chunk = &reply->structured;
    iter->only_structured = true;

    if (chunk->type == NBD_REPLY_TYPE_NONE) {
        /* NBD_REPLY_FLAG_DONE is already checked in nbd_co_receive_one_chunk */
        assert(chunk->flags & NBD_REPLY_FLAG_DONE);
        goto break_loop;
    }

    if (chunk->flags & NBD_REPLY_FLAG_DONE) {
        /* This iteration is last. */
        iter->done = true;
    }

    /* Execute the loop body */
    return true;

break_loop:
    s->requests[HANDLE_TO_INDEX(s, handle)].coroutine = NULL;

    qemu_co_mutex_lock(&s->send_mutex);
    s->in_flight--;
    if (s->in_flight == 0 && s->wait_in_flight) {
        aio_co_wake(s->connection_co);
    } else {
        qemu_co_queue_next(&s->free_sema);
    }
    qemu_co_mutex_unlock(&s->send_mutex);

    return false;
}

static int nbd_co_receive_return_code(BDRVNBDState *s, uint64_t handle,
                                      int *request_ret, Error **errp)
{
    NBDReplyChunkIter iter;

    NBD_FOREACH_REPLY_CHUNK(s, iter, handle, false, NULL, NULL, NULL) {
        /* nbd_reply_chunk_iter_receive does all the work */
    }

    error_propagate(errp, iter.err);
    *request_ret = iter.request_ret;
    return iter.ret;
}

static int nbd_co_receive_cmdread_reply(BDRVNBDState *s, uint64_t handle,
                                        uint64_t offset, QEMUIOVector *qiov,
                                        int *request_ret, Error **errp)
{
    NBDReplyChunkIter iter;
    NBDReply reply;
    void *payload = NULL;
    Error *local_err = NULL;

    NBD_FOREACH_REPLY_CHUNK(s, iter, handle, s->info.structured_reply,
                            qiov, &reply, &payload)
    {
        int ret;
        NBDStructuredReplyChunk *chunk = &reply.structured;

        assert(nbd_reply_is_structured(&reply));

        switch (chunk->type) {
        case NBD_REPLY_TYPE_OFFSET_DATA:
            /*
             * special cased in nbd_co_receive_one_chunk, data is already
             * in qiov
             */
            break;
        case NBD_REPLY_TYPE_OFFSET_HOLE:
            ret = nbd_parse_offset_hole_payload(s, &reply.structured, payload,
                                                offset, qiov, &local_err);
            if (ret < 0) {
                nbd_channel_error(s, ret);
                nbd_iter_channel_error(&iter, ret, &local_err);
            }
            break;
        default:
            if (!nbd_reply_type_is_error(chunk->type)) {
                /* not allowed reply type */
                nbd_channel_error(s, -EINVAL);
                error_setg(&local_err,
                           "Unexpected reply type: %d (%s) for CMD_READ",
                           chunk->type, nbd_reply_type_lookup(chunk->type));
                nbd_iter_channel_error(&iter, -EINVAL, &local_err);
            }
        }

        g_free(payload);
        payload = NULL;
    }

    error_propagate(errp, iter.err);
    *request_ret = iter.request_ret;
    return iter.ret;
}

static int nbd_co_receive_blockstatus_reply(BDRVNBDState *s,
                                            uint64_t handle, uint64_t length,
                                            NBDExtent *extent,
                                            int *request_ret, Error **errp)
{
    NBDReplyChunkIter iter;
    NBDReply reply;
    void *payload = NULL;
    Error *local_err = NULL;
    bool received = false;

    assert(!extent->length);
    NBD_FOREACH_REPLY_CHUNK(s, iter, handle, false, NULL, &reply, &payload) {
        int ret;
        NBDStructuredReplyChunk *chunk = &reply.structured;

        assert(nbd_reply_is_structured(&reply));

        switch (chunk->type) {
        case NBD_REPLY_TYPE_BLOCK_STATUS:
            if (received) {
                nbd_channel_error(s, -EINVAL);
                error_setg(&local_err, "Several BLOCK_STATUS chunks in reply");
                nbd_iter_channel_error(&iter, -EINVAL, &local_err);
            }
            received = true;

            ret = nbd_parse_blockstatus_payload(s, &reply.structured,
                                                payload, length, extent,
                                                &local_err);
            if (ret < 0) {
                nbd_channel_error(s, ret);
                nbd_iter_channel_error(&iter, ret, &local_err);
            }
            break;
        default:
            if (!nbd_reply_type_is_error(chunk->type)) {
                nbd_channel_error(s, -EINVAL);
                error_setg(&local_err,
                           "Unexpected reply type: %d (%s) "
                           "for CMD_BLOCK_STATUS",
                           chunk->type, nbd_reply_type_lookup(chunk->type));
                nbd_iter_channel_error(&iter, -EINVAL, &local_err);
            }
        }

        g_free(payload);
        payload = NULL;
    }

    if (!extent->length && !iter.request_ret) {
        error_setg(&local_err, "Server did not reply with any status extents");
        nbd_iter_channel_error(&iter, -EIO, &local_err);
    }

    error_propagate(errp, iter.err);
    *request_ret = iter.request_ret;
    return iter.ret;
}

static int nbd_co_request(BlockDriverState *bs, NBDRequest *request,
                          QEMUIOVector *write_qiov)
{
    int ret, request_ret;
    Error *local_err = NULL;
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    assert(request->type != NBD_CMD_READ);
    if (write_qiov) {
        assert(request->type == NBD_CMD_WRITE);
        assert(request->len == iov_size(write_qiov->iov, write_qiov->niov));
    } else {
        assert(request->type != NBD_CMD_WRITE);
    }

    do {
        ret = nbd_co_send_request(bs, request, write_qiov);
        if (ret < 0) {
            continue;
        }

        ret = nbd_co_receive_return_code(s, request->handle,
                                         &request_ret, &local_err);
        if (local_err) {
            trace_nbd_co_request_fail(request->from, request->len,
                                      request->handle, request->flags,
                                      request->type,
                                      nbd_cmd_lookup(request->type),
                                      ret, error_get_pretty(local_err));
            error_free(local_err);
            local_err = NULL;
        }
    } while (ret < 0 && nbd_client_connecting_wait(s));

    return ret ? ret : request_ret;
}

static int nbd_client_co_preadv(BlockDriverState *bs, uint64_t offset,
                                uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    int ret, request_ret;
    Error *local_err = NULL;
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    NBDRequest request = {
        .type = NBD_CMD_READ,
        .from = offset,
        .len = bytes,
    };

    assert(bytes <= NBD_MAX_BUFFER_SIZE);
    assert(!flags);

    if (!bytes) {
        return 0;
    }
    /*
     * Work around the fact that the block layer doesn't do
     * byte-accurate sizing yet - if the read exceeds the server's
     * advertised size because the block layer rounded size up, then
     * truncate the request to the server and tail-pad with zero.
     */
    if (offset >= s->info.size) {
        assert(bytes < BDRV_SECTOR_SIZE);
        qemu_iovec_memset(qiov, 0, 0, bytes);
        return 0;
    }
    if (offset + bytes > s->info.size) {
        uint64_t slop = offset + bytes - s->info.size;

        assert(slop < BDRV_SECTOR_SIZE);
        qemu_iovec_memset(qiov, bytes - slop, 0, slop);
        request.len -= slop;
    }

    do {
        ret = nbd_co_send_request(bs, &request, NULL);
        if (ret < 0) {
            continue;
        }

        ret = nbd_co_receive_cmdread_reply(s, request.handle, offset, qiov,
                                           &request_ret, &local_err);
        if (local_err) {
            trace_nbd_co_request_fail(request.from, request.len, request.handle,
                                      request.flags, request.type,
                                      nbd_cmd_lookup(request.type),
                                      ret, error_get_pretty(local_err));
            error_free(local_err);
            local_err = NULL;
        }
    } while (ret < 0 && nbd_client_connecting_wait(s));

    return ret ? ret : request_ret;
}

static int nbd_client_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                 uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    NBDRequest request = {
        .type = NBD_CMD_WRITE,
        .from = offset,
        .len = bytes,
    };

    assert(!(s->info.flags & NBD_FLAG_READ_ONLY));
    if (flags & BDRV_REQ_FUA) {
        assert(s->info.flags & NBD_FLAG_SEND_FUA);
        request.flags |= NBD_CMD_FLAG_FUA;
    }

    assert(bytes <= NBD_MAX_BUFFER_SIZE);

    if (!bytes) {
        return 0;
    }
    return nbd_co_request(bs, &request, qiov);
}

static int nbd_client_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                                       int bytes, BdrvRequestFlags flags)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    NBDRequest request = {
        .type = NBD_CMD_WRITE_ZEROES,
        .from = offset,
        .len = bytes,
    };

    assert(!(s->info.flags & NBD_FLAG_READ_ONLY));
    if (!(s->info.flags & NBD_FLAG_SEND_WRITE_ZEROES)) {
        return -ENOTSUP;
    }

    if (flags & BDRV_REQ_FUA) {
        assert(s->info.flags & NBD_FLAG_SEND_FUA);
        request.flags |= NBD_CMD_FLAG_FUA;
    }
    if (!(flags & BDRV_REQ_MAY_UNMAP)) {
        request.flags |= NBD_CMD_FLAG_NO_HOLE;
    }
    if (flags & BDRV_REQ_NO_FALLBACK) {
        assert(s->info.flags & NBD_FLAG_SEND_FAST_ZERO);
        request.flags |= NBD_CMD_FLAG_FAST_ZERO;
    }

    if (!bytes) {
        return 0;
    }
    return nbd_co_request(bs, &request, NULL);
}

static int nbd_client_co_flush(BlockDriverState *bs)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    NBDRequest request = { .type = NBD_CMD_FLUSH };

    if (!(s->info.flags & NBD_FLAG_SEND_FLUSH)) {
        return 0;
    }

    request.from = 0;
    request.len = 0;

    return nbd_co_request(bs, &request, NULL);
}

static int nbd_client_co_pdiscard(BlockDriverState *bs, int64_t offset,
                                  int bytes)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    NBDRequest request = {
        .type = NBD_CMD_TRIM,
        .from = offset,
        .len = bytes,
    };

    assert(!(s->info.flags & NBD_FLAG_READ_ONLY));
    if (!(s->info.flags & NBD_FLAG_SEND_TRIM) || !bytes) {
        return 0;
    }

    return nbd_co_request(bs, &request, NULL);
}

static int coroutine_fn nbd_client_co_block_status(
        BlockDriverState *bs, bool want_zero, int64_t offset, int64_t bytes,
        int64_t *pnum, int64_t *map, BlockDriverState **file)
{
    int ret, request_ret;
    NBDExtent extent = { 0 };
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    Error *local_err = NULL;

    NBDRequest request = {
        .type = NBD_CMD_BLOCK_STATUS,
        .from = offset,
        .len = MIN(QEMU_ALIGN_DOWN(INT_MAX, bs->bl.request_alignment),
                   MIN(bytes, s->info.size - offset)),
        .flags = NBD_CMD_FLAG_REQ_ONE,
    };

    if (!s->info.base_allocation) {
        *pnum = bytes;
        *map = offset;
        *file = bs;
        return BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
    }

    /*
     * Work around the fact that the block layer doesn't do
     * byte-accurate sizing yet - if the status request exceeds the
     * server's advertised size because the block layer rounded size
     * up, we truncated the request to the server (above), or are
     * called on just the hole.
     */
    if (offset >= s->info.size) {
        *pnum = bytes;
        assert(bytes < BDRV_SECTOR_SIZE);
        /* Intentionally don't report offset_valid for the hole */
        return BDRV_BLOCK_ZERO;
    }

    if (s->info.min_block) {
        assert(QEMU_IS_ALIGNED(request.len, s->info.min_block));
    }
    do {
        ret = nbd_co_send_request(bs, &request, NULL);
        if (ret < 0) {
            continue;
        }

        ret = nbd_co_receive_blockstatus_reply(s, request.handle, bytes,
                                               &extent, &request_ret,
                                               &local_err);
        if (local_err) {
            trace_nbd_co_request_fail(request.from, request.len, request.handle,
                                      request.flags, request.type,
                                      nbd_cmd_lookup(request.type),
                                      ret, error_get_pretty(local_err));
            error_free(local_err);
            local_err = NULL;
        }
    } while (ret < 0 && nbd_client_connecting_wait(s));

    if (ret < 0 || request_ret < 0) {
        return ret ? ret : request_ret;
    }

    assert(extent.length);
    *pnum = extent.length;
    *map = offset;
    *file = bs;
    return (extent.flags & NBD_STATE_HOLE ? 0 : BDRV_BLOCK_DATA) |
        (extent.flags & NBD_STATE_ZERO ? BDRV_BLOCK_ZERO : 0) |
        BDRV_BLOCK_OFFSET_VALID;
}

static int nbd_client_reopen_prepare(BDRVReopenState *state,
                                     BlockReopenQueue *queue, Error **errp)
{
    BDRVNBDState *s = (BDRVNBDState *)state->bs->opaque;

    if ((state->flags & BDRV_O_RDWR) && (s->info.flags & NBD_FLAG_READ_ONLY)) {
        error_setg(errp, "Can't reopen read-only NBD mount as read/write");
        return -EACCES;
    }
    return 0;
}

static void nbd_yank(void *opaque)
{
    BlockDriverState *bs = opaque;
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    qatomic_store_release(&s->state, NBD_CLIENT_QUIT);
    qio_channel_shutdown(QIO_CHANNEL(s->sioc), QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
}

static void nbd_client_close(BlockDriverState *bs)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    NBDRequest request = { .type = NBD_CMD_DISC };

    if (s->ioc) {
        nbd_send_request(s->ioc, &request);
    }

    nbd_teardown_connection(bs);
}

static int nbd_establish_connection(BlockDriverState *bs,
                                    SocketAddress *saddr,
                                    Error **errp)
{
    ERRP_GUARD();
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    s->sioc = qio_channel_socket_new();
    qio_channel_set_name(QIO_CHANNEL(s->sioc), "nbd-client");

    qio_channel_socket_connect_sync(s->sioc, saddr, errp);
    if (*errp) {
        object_unref(OBJECT(s->sioc));
        s->sioc = NULL;
        return -1;
    }

    yank_register_function(BLOCKDEV_YANK_INSTANCE(bs->node_name), nbd_yank, bs);
    qio_channel_set_delay(QIO_CHANNEL(s->sioc), false);

    return 0;
}

/* nbd_client_handshake takes ownership on s->sioc. On failure it's unref'ed. */
static int nbd_client_handshake(BlockDriverState *bs, Error **errp)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    AioContext *aio_context = bdrv_get_aio_context(bs);
    int ret;

    trace_nbd_client_handshake(s->export);
    qio_channel_set_blocking(QIO_CHANNEL(s->sioc), false, NULL);
    qio_channel_attach_aio_context(QIO_CHANNEL(s->sioc), aio_context);

    s->info.request_sizes = true;
    s->info.structured_reply = true;
    s->info.base_allocation = true;
    s->info.x_dirty_bitmap = g_strdup(s->x_dirty_bitmap);
    s->info.name = g_strdup(s->export ?: "");
    ret = nbd_receive_negotiate(aio_context, QIO_CHANNEL(s->sioc), s->tlscreds,
                                s->hostname, &s->ioc, &s->info, errp);
    g_free(s->info.x_dirty_bitmap);
    g_free(s->info.name);
    if (ret < 0) {
        yank_unregister_function(BLOCKDEV_YANK_INSTANCE(bs->node_name),
                                 nbd_yank, bs);
        object_unref(OBJECT(s->sioc));
        s->sioc = NULL;
        return ret;
    }
    if (s->x_dirty_bitmap) {
        if (!s->info.base_allocation) {
            error_setg(errp, "requested x-dirty-bitmap %s not found",
                       s->x_dirty_bitmap);
            ret = -EINVAL;
            goto fail;
        }
        if (strcmp(s->x_dirty_bitmap, "qemu:allocation-depth") == 0) {
            s->alloc_depth = true;
        }
    }
    if (s->info.flags & NBD_FLAG_READ_ONLY) {
        ret = bdrv_apply_auto_read_only(bs, "NBD export is read-only", errp);
        if (ret < 0) {
            goto fail;
        }
    }
    if (s->info.flags & NBD_FLAG_SEND_FUA) {
        bs->supported_write_flags = BDRV_REQ_FUA;
        bs->supported_zero_flags |= BDRV_REQ_FUA;
    }
    if (s->info.flags & NBD_FLAG_SEND_WRITE_ZEROES) {
        bs->supported_zero_flags |= BDRV_REQ_MAY_UNMAP;
        if (s->info.flags & NBD_FLAG_SEND_FAST_ZERO) {
            bs->supported_zero_flags |= BDRV_REQ_NO_FALLBACK;
        }
    }

    if (!s->ioc) {
        s->ioc = QIO_CHANNEL(s->sioc);
        object_ref(OBJECT(s->ioc));
    }

    trace_nbd_client_handshake_success(s->export);

    return 0;

 fail:
    /*
     * We have connected, but must fail for other reasons.
     * Send NBD_CMD_DISC as a courtesy to the server.
     */
    {
        NBDRequest request = { .type = NBD_CMD_DISC };

        nbd_send_request(s->ioc ?: QIO_CHANNEL(s->sioc), &request);

        yank_unregister_function(BLOCKDEV_YANK_INSTANCE(bs->node_name),
                                 nbd_yank, bs);
        object_unref(OBJECT(s->sioc));
        s->sioc = NULL;

        return ret;
    }
}

/*
 * Parse nbd_open options
 */

static int nbd_parse_uri(const char *filename, QDict *options)
{
    URI *uri;
    const char *p;
    QueryParams *qp = NULL;
    int ret = 0;
    bool is_unix;

    uri = uri_parse(filename);
    if (!uri) {
        return -EINVAL;
    }

    /* transport */
    if (!g_strcmp0(uri->scheme, "nbd")) {
        is_unix = false;
    } else if (!g_strcmp0(uri->scheme, "nbd+tcp")) {
        is_unix = false;
    } else if (!g_strcmp0(uri->scheme, "nbd+unix")) {
        is_unix = true;
    } else {
        ret = -EINVAL;
        goto out;
    }

    p = uri->path ? uri->path : "";
    if (p[0] == '/') {
        p++;
    }
    if (p[0]) {
        qdict_put_str(options, "export", p);
    }

    qp = query_params_parse(uri->query);
    if (qp->n > 1 || (is_unix && !qp->n) || (!is_unix && qp->n)) {
        ret = -EINVAL;
        goto out;
    }

    if (is_unix) {
        /* nbd+unix:///export?socket=path */
        if (uri->server || uri->port || strcmp(qp->p[0].name, "socket")) {
            ret = -EINVAL;
            goto out;
        }
        qdict_put_str(options, "server.type", "unix");
        qdict_put_str(options, "server.path", qp->p[0].value);
    } else {
        QString *host;
        char *port_str;

        /* nbd[+tcp]://host[:port]/export */
        if (!uri->server) {
            ret = -EINVAL;
            goto out;
        }

        /* strip braces from literal IPv6 address */
        if (uri->server[0] == '[') {
            host = qstring_from_substr(uri->server, 1,
                                       strlen(uri->server) - 1);
        } else {
            host = qstring_from_str(uri->server);
        }

        qdict_put_str(options, "server.type", "inet");
        qdict_put(options, "server.host", host);

        port_str = g_strdup_printf("%d", uri->port ?: NBD_DEFAULT_PORT);
        qdict_put_str(options, "server.port", port_str);
        g_free(port_str);
    }

out:
    if (qp) {
        query_params_free(qp);
    }
    uri_free(uri);
    return ret;
}

static bool nbd_has_filename_options_conflict(QDict *options, Error **errp)
{
    const QDictEntry *e;

    for (e = qdict_first(options); e; e = qdict_next(options, e)) {
        if (!strcmp(e->key, "host") ||
            !strcmp(e->key, "port") ||
            !strcmp(e->key, "path") ||
            !strcmp(e->key, "export") ||
            strstart(e->key, "server.", NULL))
        {
            error_setg(errp, "Option '%s' cannot be used with a file name",
                       e->key);
            return true;
        }
    }

    return false;
}

static void nbd_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    g_autofree char *file = NULL;
    char *export_name;
    const char *host_spec;
    const char *unixpath;

    if (nbd_has_filename_options_conflict(options, errp)) {
        return;
    }

    if (strstr(filename, "://")) {
        int ret = nbd_parse_uri(filename, options);
        if (ret < 0) {
            error_setg(errp, "No valid URL specified");
        }
        return;
    }

    file = g_strdup(filename);

    export_name = strstr(file, EN_OPTSTR);
    if (export_name) {
        if (export_name[strlen(EN_OPTSTR)] == 0) {
            return;
        }
        export_name[0] = 0; /* truncate 'file' */
        export_name += strlen(EN_OPTSTR);

        qdict_put_str(options, "export", export_name);
    }

    /* extract the host_spec - fail if it's not nbd:... */
    if (!strstart(file, "nbd:", &host_spec)) {
        error_setg(errp, "File name string for NBD must start with 'nbd:'");
        return;
    }

    if (!*host_spec) {
        return;
    }

    /* are we a UNIX or TCP socket? */
    if (strstart(host_spec, "unix:", &unixpath)) {
        qdict_put_str(options, "server.type", "unix");
        qdict_put_str(options, "server.path", unixpath);
    } else {
        InetSocketAddress *addr = g_new(InetSocketAddress, 1);

        if (inet_parse(addr, host_spec, errp)) {
            goto out_inet;
        }

        qdict_put_str(options, "server.type", "inet");
        qdict_put_str(options, "server.host", addr->host);
        qdict_put_str(options, "server.port", addr->port);
    out_inet:
        qapi_free_InetSocketAddress(addr);
    }
}

static bool nbd_process_legacy_socket_options(QDict *output_options,
                                              QemuOpts *legacy_opts,
                                              Error **errp)
{
    const char *path = qemu_opt_get(legacy_opts, "path");
    const char *host = qemu_opt_get(legacy_opts, "host");
    const char *port = qemu_opt_get(legacy_opts, "port");
    const QDictEntry *e;

    if (!path && !host && !port) {
        return true;
    }

    for (e = qdict_first(output_options); e; e = qdict_next(output_options, e))
    {
        if (strstart(e->key, "server.", NULL)) {
            error_setg(errp, "Cannot use 'server' and path/host/port at the "
                       "same time");
            return false;
        }
    }

    if (path && host) {
        error_setg(errp, "path and host may not be used at the same time");
        return false;
    } else if (path) {
        if (port) {
            error_setg(errp, "port may not be used without host");
            return false;
        }

        qdict_put_str(output_options, "server.type", "unix");
        qdict_put_str(output_options, "server.path", path);
    } else if (host) {
        qdict_put_str(output_options, "server.type", "inet");
        qdict_put_str(output_options, "server.host", host);
        qdict_put_str(output_options, "server.port",
                      port ?: stringify(NBD_DEFAULT_PORT));
    }

    return true;
}

static SocketAddress *nbd_config(BDRVNBDState *s, QDict *options,
                                 Error **errp)
{
    SocketAddress *saddr = NULL;
    QDict *addr = NULL;
    Visitor *iv = NULL;

    qdict_extract_subqdict(options, &addr, "server.");
    if (!qdict_size(addr)) {
        error_setg(errp, "NBD server address missing");
        goto done;
    }

    iv = qobject_input_visitor_new_flat_confused(addr, errp);
    if (!iv) {
        goto done;
    }

    if (!visit_type_SocketAddress(iv, NULL, &saddr, errp)) {
        goto done;
    }

done:
    qobject_unref(addr);
    visit_free(iv);
    return saddr;
}

static QCryptoTLSCreds *nbd_get_tls_creds(const char *id, Error **errp)
{
    Object *obj;
    QCryptoTLSCreds *creds;

    obj = object_resolve_path_component(
        object_get_objects_root(), id);
    if (!obj) {
        error_setg(errp, "No TLS credentials with id '%s'",
                   id);
        return NULL;
    }
    creds = (QCryptoTLSCreds *)
        object_dynamic_cast(obj, TYPE_QCRYPTO_TLS_CREDS);
    if (!creds) {
        error_setg(errp, "Object with id '%s' is not TLS credentials",
                   id);
        return NULL;
    }

    if (creds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT) {
        error_setg(errp,
                   "Expecting TLS credentials with a client endpoint");
        return NULL;
    }
    object_ref(obj);
    return creds;
}


static QemuOptsList nbd_runtime_opts = {
    .name = "nbd",
    .head = QTAILQ_HEAD_INITIALIZER(nbd_runtime_opts.head),
    .desc = {
        {
            .name = "host",
            .type = QEMU_OPT_STRING,
            .help = "TCP host to connect to",
        },
        {
            .name = "port",
            .type = QEMU_OPT_STRING,
            .help = "TCP port to connect to",
        },
        {
            .name = "path",
            .type = QEMU_OPT_STRING,
            .help = "Unix socket path to connect to",
        },
        {
            .name = "export",
            .type = QEMU_OPT_STRING,
            .help = "Name of the NBD export to open",
        },
        {
            .name = "tls-creds",
            .type = QEMU_OPT_STRING,
            .help = "ID of the TLS credentials to use",
        },
        {
            .name = "x-dirty-bitmap",
            .type = QEMU_OPT_STRING,
            .help = "experimental: expose named dirty bitmap in place of "
                    "block status",
        },
        {
            .name = "reconnect-delay",
            .type = QEMU_OPT_NUMBER,
            .help = "On an unexpected disconnect, the nbd client tries to "
                    "connect again until succeeding or encountering a serious "
                    "error.  During the first @reconnect-delay seconds, all "
                    "requests are paused and will be rerun on a successful "
                    "reconnect. After that time, any delayed requests and all "
                    "future requests before a successful reconnect will "
                    "immediately fail. Default 0",
        },
        { /* end of list */ }
    },
};

static int nbd_process_options(BlockDriverState *bs, QDict *options,
                               Error **errp)
{
    BDRVNBDState *s = bs->opaque;
    QemuOpts *opts;
    int ret = -EINVAL;

    opts = qemu_opts_create(&nbd_runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        goto error;
    }

    /* Translate @host, @port, and @path to a SocketAddress */
    if (!nbd_process_legacy_socket_options(options, opts, errp)) {
        goto error;
    }

    /* Pop the config into our state object. Exit if invalid. */
    s->saddr = nbd_config(s, options, errp);
    if (!s->saddr) {
        goto error;
    }

    s->export = g_strdup(qemu_opt_get(opts, "export"));
    if (s->export && strlen(s->export) > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "export name too long to send to server");
        goto error;
    }

    s->tlscredsid = g_strdup(qemu_opt_get(opts, "tls-creds"));
    if (s->tlscredsid) {
        s->tlscreds = nbd_get_tls_creds(s->tlscredsid, errp);
        if (!s->tlscreds) {
            goto error;
        }

        /* TODO SOCKET_ADDRESS_KIND_FD where fd has AF_INET or AF_INET6 */
        if (s->saddr->type != SOCKET_ADDRESS_TYPE_INET) {
            error_setg(errp, "TLS only supported over IP sockets");
            goto error;
        }
        s->hostname = s->saddr->u.inet.host;
    }

    s->x_dirty_bitmap = g_strdup(qemu_opt_get(opts, "x-dirty-bitmap"));
    if (s->x_dirty_bitmap && strlen(s->x_dirty_bitmap) > NBD_MAX_STRING_SIZE) {
        error_setg(errp, "x-dirty-bitmap query too long to send to server");
        goto error;
    }

    s->reconnect_delay = qemu_opt_get_number(opts, "reconnect-delay", 0);

    ret = 0;

 error:
    if (ret < 0) {
        nbd_clear_bdrvstate(s);
    }
    qemu_opts_del(opts);
    return ret;
}

static int nbd_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    int ret;
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    ret = nbd_process_options(bs, options, errp);
    if (ret < 0) {
        return ret;
    }

    s->bs = bs;
    qemu_co_mutex_init(&s->send_mutex);
    qemu_co_queue_init(&s->free_sema);

    if (!yank_register_instance(BLOCKDEV_YANK_INSTANCE(bs->node_name), errp)) {
        return -EEXIST;
    }

    /*
     * establish TCP connection, return error if it fails
     * TODO: Configurable retry-until-timeout behaviour.
     */
    if (nbd_establish_connection(bs, s->saddr, errp) < 0) {
        yank_unregister_instance(BLOCKDEV_YANK_INSTANCE(bs->node_name));
        return -ECONNREFUSED;
    }

    ret = nbd_client_handshake(bs, errp);
    if (ret < 0) {
        yank_unregister_instance(BLOCKDEV_YANK_INSTANCE(bs->node_name));
        nbd_clear_bdrvstate(s);
        return ret;
    }
    /* successfully connected */
    s->state = NBD_CLIENT_CONNECTED;

    nbd_init_connect_thread(s);

    s->connection_co = qemu_coroutine_create(nbd_connection_entry, s);
    bdrv_inc_in_flight(bs);
    aio_co_schedule(bdrv_get_aio_context(bs), s->connection_co);

    return 0;
}

static int nbd_co_flush(BlockDriverState *bs)
{
    return nbd_client_co_flush(bs);
}

static void nbd_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;
    uint32_t min = s->info.min_block;
    uint32_t max = MIN_NON_ZERO(NBD_MAX_BUFFER_SIZE, s->info.max_block);

    /*
     * If the server did not advertise an alignment:
     * - a size that is not sector-aligned implies that an alignment
     *   of 1 can be used to access those tail bytes
     * - advertisement of block status requires an alignment of 1, so
     *   that we don't violate block layer constraints that block
     *   status is always aligned (as we can't control whether the
     *   server will report sub-sector extents, such as a hole at EOF
     *   on an unaligned POSIX file)
     * - otherwise, assume the server is so old that we are safer avoiding
     *   sub-sector requests
     */
    if (!min) {
        min = (!QEMU_IS_ALIGNED(s->info.size, BDRV_SECTOR_SIZE) ||
               s->info.base_allocation) ? 1 : BDRV_SECTOR_SIZE;
    }

    bs->bl.request_alignment = min;
    bs->bl.max_pdiscard = QEMU_ALIGN_DOWN(INT_MAX, min);
    bs->bl.max_pwrite_zeroes = max;
    bs->bl.max_transfer = max;

    if (s->info.opt_block &&
        s->info.opt_block > bs->bl.opt_transfer) {
        bs->bl.opt_transfer = s->info.opt_block;
    }
}

static void nbd_close(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;

    nbd_client_close(bs);
    yank_unregister_instance(BLOCKDEV_YANK_INSTANCE(bs->node_name));
    nbd_clear_bdrvstate(s);
}

/*
 * NBD cannot truncate, but if the caller asks to truncate to the same size, or
 * to a smaller size with exact=false, there is no reason to fail the
 * operation.
 *
 * Preallocation mode is ignored since it does not seems useful to fail when
 * we never change anything.
 */
static int coroutine_fn nbd_co_truncate(BlockDriverState *bs, int64_t offset,
                                        bool exact, PreallocMode prealloc,
                                        BdrvRequestFlags flags, Error **errp)
{
    BDRVNBDState *s = bs->opaque;

    if (offset != s->info.size && exact) {
        error_setg(errp, "Cannot resize NBD nodes");
        return -ENOTSUP;
    }

    if (offset > s->info.size) {
        error_setg(errp, "Cannot grow NBD nodes");
        return -EINVAL;
    }

    return 0;
}

static int64_t nbd_getlength(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;

    return s->info.size;
}

static void nbd_refresh_filename(BlockDriverState *bs)
{
    BDRVNBDState *s = bs->opaque;
    const char *host = NULL, *port = NULL, *path = NULL;
    size_t len = 0;

    if (s->saddr->type == SOCKET_ADDRESS_TYPE_INET) {
        const InetSocketAddress *inet = &s->saddr->u.inet;
        if (!inet->has_ipv4 && !inet->has_ipv6 && !inet->has_to) {
            host = inet->host;
            port = inet->port;
        }
    } else if (s->saddr->type == SOCKET_ADDRESS_TYPE_UNIX) {
        path = s->saddr->u.q_unix.path;
    } /* else can't represent as pseudo-filename */

    if (path && s->export) {
        len = snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                       "nbd+unix:///%s?socket=%s", s->export, path);
    } else if (path && !s->export) {
        len = snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                       "nbd+unix://?socket=%s", path);
    } else if (host && s->export) {
        len = snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                       "nbd://%s:%s/%s", host, port, s->export);
    } else if (host && !s->export) {
        len = snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                       "nbd://%s:%s", host, port);
    }
    if (len >= sizeof(bs->exact_filename)) {
        /* Name is too long to represent exactly, so leave it empty. */
        bs->exact_filename[0] = '\0';
    }
}

static char *nbd_dirname(BlockDriverState *bs, Error **errp)
{
    /* The generic bdrv_dirname() implementation is able to work out some
     * directory name for NBD nodes, but that would be wrong. So far there is no
     * specification for how "export paths" would work, so NBD does not have
     * directory names. */
    error_setg(errp, "Cannot generate a base directory for NBD nodes");
    return NULL;
}

static const char *const nbd_strong_runtime_opts[] = {
    "path",
    "host",
    "port",
    "export",
    "tls-creds",
    "server.",

    NULL
};

static void nbd_cancel_in_flight(BlockDriverState *bs)
{
    BDRVNBDState *s = (BDRVNBDState *)bs->opaque;

    reconnect_delay_timer_del(s);

    if (s->state == NBD_CLIENT_CONNECTING_WAIT) {
        s->state = NBD_CLIENT_CONNECTING_NOWAIT;
        qemu_co_queue_restart_all(&s->free_sema);
    }
}

static BlockDriver bdrv_nbd = {
    .format_name                = "nbd",
    .protocol_name              = "nbd",
    .instance_size              = sizeof(BDRVNBDState),
    .bdrv_parse_filename        = nbd_parse_filename,
    .bdrv_co_create_opts        = bdrv_co_create_opts_simple,
    .create_opts                = &bdrv_create_opts_simple,
    .bdrv_file_open             = nbd_open,
    .bdrv_reopen_prepare        = nbd_client_reopen_prepare,
    .bdrv_co_preadv             = nbd_client_co_preadv,
    .bdrv_co_pwritev            = nbd_client_co_pwritev,
    .bdrv_co_pwrite_zeroes      = nbd_client_co_pwrite_zeroes,
    .bdrv_close                 = nbd_close,
    .bdrv_co_flush_to_os        = nbd_co_flush,
    .bdrv_co_pdiscard           = nbd_client_co_pdiscard,
    .bdrv_refresh_limits        = nbd_refresh_limits,
    .bdrv_co_truncate           = nbd_co_truncate,
    .bdrv_getlength             = nbd_getlength,
    .bdrv_detach_aio_context    = nbd_client_detach_aio_context,
    .bdrv_attach_aio_context    = nbd_client_attach_aio_context,
    .bdrv_co_drain_begin        = nbd_client_co_drain_begin,
    .bdrv_co_drain_end          = nbd_client_co_drain_end,
    .bdrv_refresh_filename      = nbd_refresh_filename,
    .bdrv_co_block_status       = nbd_client_co_block_status,
    .bdrv_dirname               = nbd_dirname,
    .strong_runtime_opts        = nbd_strong_runtime_opts,
    .bdrv_cancel_in_flight      = nbd_cancel_in_flight,
};

static BlockDriver bdrv_nbd_tcp = {
    .format_name                = "nbd",
    .protocol_name              = "nbd+tcp",
    .instance_size              = sizeof(BDRVNBDState),
    .bdrv_parse_filename        = nbd_parse_filename,
    .bdrv_co_create_opts        = bdrv_co_create_opts_simple,
    .create_opts                = &bdrv_create_opts_simple,
    .bdrv_file_open             = nbd_open,
    .bdrv_reopen_prepare        = nbd_client_reopen_prepare,
    .bdrv_co_preadv             = nbd_client_co_preadv,
    .bdrv_co_pwritev            = nbd_client_co_pwritev,
    .bdrv_co_pwrite_zeroes      = nbd_client_co_pwrite_zeroes,
    .bdrv_close                 = nbd_close,
    .bdrv_co_flush_to_os        = nbd_co_flush,
    .bdrv_co_pdiscard           = nbd_client_co_pdiscard,
    .bdrv_refresh_limits        = nbd_refresh_limits,
    .bdrv_co_truncate           = nbd_co_truncate,
    .bdrv_getlength             = nbd_getlength,
    .bdrv_detach_aio_context    = nbd_client_detach_aio_context,
    .bdrv_attach_aio_context    = nbd_client_attach_aio_context,
    .bdrv_co_drain_begin        = nbd_client_co_drain_begin,
    .bdrv_co_drain_end          = nbd_client_co_drain_end,
    .bdrv_refresh_filename      = nbd_refresh_filename,
    .bdrv_co_block_status       = nbd_client_co_block_status,
    .bdrv_dirname               = nbd_dirname,
    .strong_runtime_opts        = nbd_strong_runtime_opts,
    .bdrv_cancel_in_flight      = nbd_cancel_in_flight,
};

static BlockDriver bdrv_nbd_unix = {
    .format_name                = "nbd",
    .protocol_name              = "nbd+unix",
    .instance_size              = sizeof(BDRVNBDState),
    .bdrv_parse_filename        = nbd_parse_filename,
    .bdrv_co_create_opts        = bdrv_co_create_opts_simple,
    .create_opts                = &bdrv_create_opts_simple,
    .bdrv_file_open             = nbd_open,
    .bdrv_reopen_prepare        = nbd_client_reopen_prepare,
    .bdrv_co_preadv             = nbd_client_co_preadv,
    .bdrv_co_pwritev            = nbd_client_co_pwritev,
    .bdrv_co_pwrite_zeroes      = nbd_client_co_pwrite_zeroes,
    .bdrv_close                 = nbd_close,
    .bdrv_co_flush_to_os        = nbd_co_flush,
    .bdrv_co_pdiscard           = nbd_client_co_pdiscard,
    .bdrv_refresh_limits        = nbd_refresh_limits,
    .bdrv_co_truncate           = nbd_co_truncate,
    .bdrv_getlength             = nbd_getlength,
    .bdrv_detach_aio_context    = nbd_client_detach_aio_context,
    .bdrv_attach_aio_context    = nbd_client_attach_aio_context,
    .bdrv_co_drain_begin        = nbd_client_co_drain_begin,
    .bdrv_co_drain_end          = nbd_client_co_drain_end,
    .bdrv_refresh_filename      = nbd_refresh_filename,
    .bdrv_co_block_status       = nbd_client_co_block_status,
    .bdrv_dirname               = nbd_dirname,
    .strong_runtime_opts        = nbd_strong_runtime_opts,
    .bdrv_cancel_in_flight      = nbd_cancel_in_flight,
};

static void bdrv_nbd_init(void)
{
    bdrv_register(&bdrv_nbd);
    bdrv_register(&bdrv_nbd_tcp);
    bdrv_register(&bdrv_nbd_unix);
}

block_init(bdrv_nbd_init);
