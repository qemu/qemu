/*
 * RFifoLock tests
 *
 * Copyright Red Hat, Inc. 2013
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/rfifolock.h"

static void test_nesting(void)
{
    RFifoLock lock;

    /* Trivial test, ensure the lock is recursive */
    rfifolock_init(&lock, NULL, NULL);
    rfifolock_lock(&lock);
    rfifolock_lock(&lock);
    rfifolock_lock(&lock);
    rfifolock_unlock(&lock);
    rfifolock_unlock(&lock);
    rfifolock_unlock(&lock);
    rfifolock_destroy(&lock);
}

typedef struct {
    RFifoLock lock;
    int fd[2];
} CallbackTestData;

static void rfifolock_cb(void *opaque)
{
    CallbackTestData *data = opaque;
    int ret;
    char c = 0;

    ret = write(data->fd[1], &c, sizeof(c));
    g_assert(ret == 1);
}

static void *callback_thread(void *opaque)
{
    CallbackTestData *data = opaque;

    /* The other thread holds the lock so the contention callback will be
     * invoked...
     */
    rfifolock_lock(&data->lock);
    rfifolock_unlock(&data->lock);
    return NULL;
}

static void test_callback(void)
{
    CallbackTestData data;
    QemuThread thread;
    int ret;
    char c;

    rfifolock_init(&data.lock, rfifolock_cb, &data);
    ret = qemu_pipe(data.fd);
    g_assert(ret == 0);

    /* Hold lock but allow the callback to kick us by writing to the pipe */
    rfifolock_lock(&data.lock);
    qemu_thread_create(&thread, "callback_thread",
                       callback_thread, &data, QEMU_THREAD_JOINABLE);
    ret = read(data.fd[0], &c, sizeof(c));
    g_assert(ret == 1);
    rfifolock_unlock(&data.lock);
    /* If we got here then the callback was invoked, as expected */

    qemu_thread_join(&thread);
    close(data.fd[0]);
    close(data.fd[1]);
    rfifolock_destroy(&data.lock);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nesting", test_nesting);
    g_test_add_func("/callback", test_callback);
    return g_test_run();
}
