/*
 * Simple C functions to supplement the C library
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
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/bswap.h"
#include "host/cpuinfo.h"

typedef bool (*biz_accel_fn)(const void *, size_t);

static bool buffer_is_zero_int_lt256(const void *buf, size_t len)
{
    uint64_t t;
    const uint64_t *p, *e;

    /*
     * Use unaligned memory access functions to handle
     * the beginning and end of the buffer.
     */
    if (unlikely(len <= 8)) {
        return (ldl_he_p(buf) | ldl_he_p(buf + len - 4)) == 0;
    }

    t = ldq_he_p(buf) | ldq_he_p(buf + len - 8);
    p = QEMU_ALIGN_PTR_DOWN(buf + 8, 8);
    e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 8);

    /* Read 0 to 31 aligned words from the middle. */
    while (p < e) {
        t |= *p++;
    }
    return t == 0;
}

static bool buffer_is_zero_int_ge256(const void *buf, size_t len)
{
    /*
     * Use unaligned memory access functions to handle
     * the beginning and end of the buffer.
     */
    uint64_t t = ldq_he_p(buf) | ldq_he_p(buf + len - 8);
    const uint64_t *p = QEMU_ALIGN_PTR_DOWN(buf + 8, 8);
    const uint64_t *e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 8);

    /* Collect a partial block at the tail end. */
    t |= e[-7] | e[-6] | e[-5] | e[-4] | e[-3] | e[-2] | e[-1];

    /*
     * Loop over 64 byte blocks.
     * With the head and tail removed, e - p >= 30,
     * so the loop must iterate at least 3 times.
     */
    do {
        if (t) {
            return false;
        }
        t = p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7];
        p += 8;
    } while (p < e - 7);

    return t == 0;
}

#include "host/bufferiszero.c.inc"

static biz_accel_fn buffer_is_zero_accel;
static unsigned accel_index;

bool buffer_is_zero_ool(const void *buf, size_t len)
{
    if (unlikely(len == 0)) {
        return true;
    }
    if (!buffer_is_zero_sample3(buf, len)) {
        return false;
    }
    /* All bytes are covered for any len <= 3.  */
    if (unlikely(len <= 3)) {
        return true;
    }

    if (likely(len >= 256)) {
        return buffer_is_zero_accel(buf, len);
    }
    return buffer_is_zero_int_lt256(buf, len);
}

bool buffer_is_zero_ge256(const void *buf, size_t len)
{
    return buffer_is_zero_accel(buf, len);
}

bool test_buffer_is_zero_next_accel(void)
{
    if (accel_index != 0) {
        buffer_is_zero_accel = accel_table[--accel_index];
        return true;
    }
    return false;
}

static void __attribute__((constructor)) init_accel(void)
{
    accel_index = best_accel();
    buffer_is_zero_accel = accel_table[accel_index];
}
