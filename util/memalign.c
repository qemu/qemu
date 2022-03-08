/*
 * memalign.c: Allocate an aligned memory region
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010-2016 Red Hat, Inc.
 * Copyright (c) 2022 Linaro Ltd
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
#include "qemu/host-utils.h"
#include "qemu/memalign.h"
#include "trace.h"

void *qemu_try_memalign(size_t alignment, size_t size)
{
    void *ptr;

    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    } else {
        g_assert(is_power_of_2(alignment));
    }

    /*
     * Handling of 0 allocations varies among the different
     * platform APIs (for instance _aligned_malloc() will
     * fail) -- ensure that we always return a valid non-NULL
     * pointer that can be freed by qemu_vfree().
     */
    if (size == 0) {
        size++;
    }
#if defined(CONFIG_POSIX_MEMALIGN)
    int ret;
    ret = posix_memalign(&ptr, alignment, size);
    if (ret != 0) {
        errno = ret;
        ptr = NULL;
    }
#elif defined(CONFIG_ALIGNED_MALLOC)
    ptr = _aligned_malloc(size, alignment);
#elif defined(CONFIG_VALLOC)
    ptr = valloc(size);
#elif defined(CONFIG_MEMALIGN)
    ptr = memalign(alignment, size);
#else
    #error No function to allocate aligned memory available
#endif
    trace_qemu_memalign(alignment, size, ptr);
    return ptr;
}

void *qemu_memalign(size_t alignment, size_t size)
{
    void *p = qemu_try_memalign(alignment, size);
    if (p) {
        return p;
    }
    fprintf(stderr,
            "qemu_memalign: failed to allocate %zu bytes at alignment %zu: %s\n",
            size, alignment, strerror(errno));
    abort();
}

void qemu_vfree(void *ptr)
{
    trace_qemu_vfree(ptr);
#if !defined(CONFIG_POSIX_MEMALIGN) && defined(CONFIG_ALIGNED_MALLOC)
    /* Only Windows _aligned_malloc needs a special free function */
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
