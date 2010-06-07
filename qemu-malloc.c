/*
 * malloc-like functions for system emulation.
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include <stdlib.h>

static void *oom_check(void *ptr)
{
    if (ptr == NULL) {
        abort();
    }
    return ptr;
}

void qemu_free(void *ptr)
{
    free(ptr);
}

static int allow_zero_malloc(void)
{
#if defined(CONFIG_ZERO_MALLOC)
    return 1;
#else
    return 0;
#endif
}

void *qemu_malloc(size_t size)
{
    if (!size && !allow_zero_malloc()) {
        abort();
    }
    return oom_check(malloc(size ? size : 1));
}

void *qemu_realloc(void *ptr, size_t size)
{
    if (!size && !allow_zero_malloc()) {
        abort();
    }
    return oom_check(realloc(ptr, size ? size : 1));
}

void *qemu_mallocz(size_t size)
{
    if (!size && !allow_zero_malloc()) {
        abort();
    }
    return oom_check(calloc(1, size ? size : 1));
}

char *qemu_strdup(const char *str)
{
    char *ptr;
    size_t len = strlen(str);
    ptr = qemu_malloc(len + 1);
    memcpy(ptr, str, len + 1);
    return ptr;
}

char *qemu_strndup(const char *str, size_t size)
{
    const char *end = memchr(str, 0, size);
    char *new;

    if (end) {
        size = end - str;
    }

    new = qemu_malloc(size + 1);
    new[size] = 0;

    return memcpy(new, str, size);
}
