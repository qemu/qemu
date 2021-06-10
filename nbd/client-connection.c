/*
 * QEMU Block driver for  NBD
 *
 * Copyright (c) 2021 Virtuozzo International GmbH.
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

#include "block/nbd.h"

#include "qapi/qapi-visit-sockets.h"
#include "qapi/clone-visitor.h"

struct NBDClientConnection {
    /* Initialization constants */
    SocketAddress *saddr; /* address to connect to */

    QemuMutex mutex;

    /*
     * @sioc and @err represent a connection attempt.  While running
     * is true, they are only used by the connection thread, and mutex
     * locking is not needed.  Once the thread finishes,
     * nbd_co_establish_connection then steals these pointers while
     * under the mutex.
     */
    QIOChannelSocket *sioc;
    Error *err;

    /* All further fields are accessed only under mutex */
    bool running; /* thread is running now */
    bool detached; /* thread is detached and should cleanup the state */

    /*
     * wait_co: if non-NULL, which coroutine to wake in
     * nbd_co_establish_connection() after yield()
     */
    Coroutine *wait_co;
};

NBDClientConnection *nbd_client_connection_new(const SocketAddress *saddr)
{
    NBDClientConnection *conn = g_new(NBDClientConnection, 1);

    *conn = (NBDClientConnection) {
        .saddr = QAPI_CLONE(SocketAddress, saddr),
    };

    qemu_mutex_init(&conn->mutex);

    return conn;
}

static void nbd_client_connection_do_free(NBDClientConnection *conn)
{
    if (conn->sioc) {
        qio_channel_close(QIO_CHANNEL(conn->sioc), NULL);
        object_unref(OBJECT(conn->sioc));
    }
    error_free(conn->err);
    qapi_free_SocketAddress(conn->saddr);
    g_free(conn);
}

static void *connect_thread_func(void *opaque)
{
    NBDClientConnection *conn = opaque;
    int ret;
    bool do_free;

    conn->sioc = qio_channel_socket_new();

    error_free(conn->err);
    conn->err = NULL;
    ret = qio_channel_socket_connect_sync(conn->sioc, conn->saddr, &conn->err);
    if (ret < 0) {
        object_unref(OBJECT(conn->sioc));
        conn->sioc = NULL;
    }

    qio_channel_set_delay(QIO_CHANNEL(conn->sioc), false);

    qemu_mutex_lock(&conn->mutex);

    assert(conn->running);
    conn->running = false;
    if (conn->wait_co) {
        aio_co_wake(conn->wait_co);
        conn->wait_co = NULL;
    }
    do_free = conn->detached;

    qemu_mutex_unlock(&conn->mutex);

    if (do_free) {
        nbd_client_connection_do_free(conn);
    }

    return NULL;
}

void nbd_client_connection_release(NBDClientConnection *conn)
{
    bool do_free = false;

    if (!conn) {
        return;
    }

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        assert(!conn->detached);
        if (conn->running) {
            conn->detached = true;
        } else {
            do_free = true;
        }
    }

    if (do_free) {
        nbd_client_connection_do_free(conn);
    }
}

/*
 * Get a new connection in context of @conn:
 *   if the thread is running, wait for completion
 *   if the thread already succeeded in the background, and user didn't get the
 *     result, just return it now
 *   otherwise the thread is not running, so start a thread and wait for
 *     completion
 */
QIOChannelSocket *coroutine_fn
nbd_co_establish_connection(NBDClientConnection *conn, Error **errp)
{
    QemuThread thread;

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        /*
         * Don't call nbd_co_establish_connection() in several coroutines in
         * parallel. Only one call at once is supported.
         */
        assert(!conn->wait_co);

        if (!conn->running) {
            if (conn->sioc) {
                /* Previous attempt finally succeeded in background */
                return g_steal_pointer(&conn->sioc);
            }

            conn->running = true;
            error_free(conn->err);
            conn->err = NULL;
            qemu_thread_create(&thread, "nbd-connect",
                               connect_thread_func, conn, QEMU_THREAD_DETACHED);
        }

        conn->wait_co = qemu_coroutine_self();
    }

    /*
     * We are going to wait for connect-thread finish, but
     * nbd_co_establish_connection_cancel() can interrupt.
     */
    qemu_coroutine_yield();

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        if (conn->running) {
            /*
             * The connection attempt was canceled and the coroutine resumed
             * before the connection thread finished its job.  Report the
             * attempt as failed, but leave the connection thread running,
             * to reuse it for the next connection attempt.
             */
            error_setg(errp, "Connection attempt cancelled by other operation");
            return NULL;
        } else {
            error_propagate(errp, conn->err);
            conn->err = NULL;
            return g_steal_pointer(&conn->sioc);
        }
    }

    abort(); /* unreachable */
}

/*
 * nbd_co_establish_connection_cancel
 * Cancel nbd_co_establish_connection() asynchronously.
 *
 * Note that this function neither directly stops the thread nor closes the
 * socket, but rather safely wakes nbd_co_establish_connection() which is
 * sleeping in yield()
 */
void nbd_co_establish_connection_cancel(NBDClientConnection *conn)
{
    Coroutine *wait_co;

    WITH_QEMU_LOCK_GUARD(&conn->mutex) {
        wait_co = g_steal_pointer(&conn->wait_co);
    }

    if (wait_co) {
        aio_co_wake(wait_co);
    }
}
