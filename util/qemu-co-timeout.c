/*
 * Helper functionality for distributing a fixed total amount of
 * an abstract resource among multiple coroutines.
 *
 * Copyright (c) 2022 Virtuozzo International GmbH
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
#include "block/aio.h"

typedef struct QemuCoTimeoutState {
    CoroutineEntry *entry;
    void *opaque;
    QemuCoSleep sleep_state;
    bool marker;
    CleanupFunc *clean;
} QemuCoTimeoutState;

static void coroutine_fn qemu_co_timeout_entry(void *opaque)
{
    QemuCoTimeoutState *s = opaque;

    s->entry(s->opaque);

    if (s->marker) {
        assert(!s->sleep_state.to_wake);
        /* .marker set by qemu_co_timeout, it have been failed */
        if (s->clean) {
            s->clean(s->opaque);
        }
        g_free(s);
    } else {
        s->marker = true;
        qemu_co_sleep_wake(&s->sleep_state);
    }
}

int coroutine_fn qemu_co_timeout(CoroutineEntry *entry, void *opaque,
                                 uint64_t timeout_ns, CleanupFunc clean)
{
    QemuCoTimeoutState *s;
    Coroutine *co;

    if (timeout_ns == 0) {
        entry(opaque);
        return 0;
    }

    s = g_new(QemuCoTimeoutState, 1);
    *s = (QemuCoTimeoutState) {
        .entry = entry,
        .opaque = opaque,
        .clean = clean
    };

    co = qemu_coroutine_create(qemu_co_timeout_entry, s);

    aio_co_enter(qemu_get_current_aio_context(), co);
    qemu_co_sleep_ns_wakeable(&s->sleep_state, QEMU_CLOCK_REALTIME, timeout_ns);

    if (s->marker) {
        /* .marker set by qemu_co_timeout_entry, success */
        g_free(s);
        return 0;
    }

    /* Don't free s, as we can't cancel qemu_co_timeout_entry execution */
    s->marker = true;
    return -ETIMEDOUT;
}
