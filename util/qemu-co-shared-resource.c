/*
 * Helper functionality for distributing a fixed total amount of
 * an abstract resource among multiple coroutines.
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
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
#include "qemu/coroutine.h"
#include "qemu/co-shared-resource.h"

struct SharedResource {
    uint64_t total; /* Set in shres_create() and not changed anymore */

    /* State fields protected by lock */
    uint64_t available;
    CoQueue queue;

    QemuMutex lock;
};

SharedResource *shres_create(uint64_t total)
{
    SharedResource *s = g_new0(SharedResource, 1);

    s->total = s->available = total;
    qemu_co_queue_init(&s->queue);
    qemu_mutex_init(&s->lock);

    return s;
}

void shres_destroy(SharedResource *s)
{
    assert(s->available == s->total);
    qemu_mutex_destroy(&s->lock);
    g_free(s);
}

/* Called with lock held. */
static bool co_try_get_from_shres_locked(SharedResource *s, uint64_t n)
{
    if (s->available >= n) {
        s->available -= n;
        return true;
    }

    return false;
}

void coroutine_fn co_get_from_shres(SharedResource *s, uint64_t n)
{
    assert(n <= s->total);
    QEMU_LOCK_GUARD(&s->lock);
    while (!co_try_get_from_shres_locked(s, n)) {
        qemu_co_queue_wait(&s->queue, &s->lock);
    }
}

void coroutine_fn co_put_to_shres(SharedResource *s, uint64_t n)
{
    QEMU_LOCK_GUARD(&s->lock);
    assert(s->total - s->available >= n);
    s->available += n;
    qemu_co_queue_restart_all(&s->queue);
}
