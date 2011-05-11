/*
 * Coroutine internals
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

#ifndef QEMU_COROUTINE_INT_H
#define QEMU_COROUTINE_INT_H

#include <setjmp.h>
#include "qemu-common.h"
#include "qemu-queue.h"
#include "qemu-coroutine.h"

enum {
    /* setjmp() return values */
    COROUTINE_YIELD = 1,
    COROUTINE_TERMINATE = 2,
};

struct Coroutine {
    bool initialized;
    struct Coroutine *caller;

    size_t stack_size;
    char *stack;

    /* Used to pass arguments/return values for coroutines */
    void *data;
    CoroutineEntry *entry;

    QLIST_ENTRY(Coroutine) pool_next;

    jmp_buf env;
};

int qemu_coroutine_init_env(Coroutine *co);

#endif
