/*
 * Win32 coroutine initialization code
 *
 * Copyright (c) 2011 Kevin Wolf <kwolf@redhat.com>
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
#include "qemu-coroutine-int.h"

typedef struct
{
    Coroutine base;

    LPVOID fiber;
    CoroutineAction action;
} CoroutineWin32;

static __thread CoroutineWin32 leader;
static __thread Coroutine *current;

CoroutineAction qemu_coroutine_switch(Coroutine *from_, Coroutine *to_,
                                      CoroutineAction action)
{
    CoroutineWin32 *from = DO_UPCAST(CoroutineWin32, base, from_);
    CoroutineWin32 *to = DO_UPCAST(CoroutineWin32, base, to_);

    current = to_;

    to->action = action;
    SwitchToFiber(to->fiber);
    return from->action;
}

static void CALLBACK coroutine_trampoline(void *co_)
{
    Coroutine *co = co_;

    while (true) {
        co->entry(co->entry_arg);
        qemu_coroutine_switch(co, co->caller, COROUTINE_TERMINATE);
    }
}

Coroutine *qemu_coroutine_new(void)
{
    const size_t stack_size = 1 << 20;
    CoroutineWin32 *co;

    co = g_malloc0(sizeof(*co));
    co->fiber = CreateFiber(stack_size, coroutine_trampoline, &co->base);
    return &co->base;
}

void qemu_coroutine_delete(Coroutine *co_)
{
    CoroutineWin32 *co = DO_UPCAST(CoroutineWin32, base, co_);

    DeleteFiber(co->fiber);
    g_free(co);
}

Coroutine *qemu_coroutine_self(void)
{
    if (!current) {
        current = &leader.base;
        leader.fiber = ConvertThreadToFiber(NULL);
    }
    return current;
}

bool qemu_in_coroutine(void)
{
    return current && current->caller;
}
