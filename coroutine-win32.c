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

#include "qemu-coroutine-int.h"

static void __attribute__((used)) trampoline(Coroutine *co)
{
    if (!setjmp(co->env)) {
        return;
    }

    while (true) {
        co->entry(co->data);
        if (!setjmp(co->env)) {
            longjmp(co->caller->env, COROUTINE_TERMINATE);
        }
    }
}

int qemu_coroutine_init_env(Coroutine *co)
{
#ifdef __i386__
    asm volatile(
        "mov %%esp, %%ebx;"
        "mov %0, %%esp;"
        "pushl %1;"
        "call _trampoline;"
        "mov %%ebx, %%esp;"
        : : "r" (co->stack + co->stack_size), "r" (co) : "ebx"
    );
#else
    #error This host architecture is not supported for win32
#endif

    return 0;
}
