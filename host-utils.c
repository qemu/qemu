/*
 * Utility compute operations used by translated code.
 *
 * Copyright (c) 2007 Aurelien Jarno
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

#include "vl.h"

/* Signed 64x64 -> 128 multiplication */

void muls64(int64_t *phigh, int64_t *plow, int64_t a, int64_t b)
{
#if defined(__x86_64__)
    __asm__ ("imul %0\n\t"
             : "=d" (*phigh), "=a" (*plow)
             : "a" (a), "0" (b)
             );
#else
    int64_t ph;
    uint64_t pm1, pm2, pl;

    pl = (uint64_t)((uint32_t)a) * (uint64_t)((uint32_t)b);
    pm1 = (a >> 32) * (uint32_t)b;
    pm2 = (uint32_t)a * (b >> 32);
    ph = (a >> 32) * (b >> 32);

    ph += (int64_t)pm1 >> 32;
    pm1 = (uint64_t)((uint32_t)pm1) + pm2 + (pl >> 32);

    *phigh = ph + ((int64_t)pm1 >> 32);
    *plow = (pm1 << 32) + (uint32_t)pl;
#endif
}

/* Unsigned 64x64 -> 128 multiplication */
void mulu64(uint64_t *phigh, uint64_t *plow, uint64_t a, uint64_t b)
{
#if defined(__x86_64__)
    __asm__ ("mul %0\n\t"
             : "=d" (*phigh), "=a" (*plow)
             : "a" (a), "0" (b)
            );
#else
    uint64_t ph, pm1, pm2, pl;

    pl = (uint64_t)((uint32_t)a) * (uint64_t)((uint32_t)b);
    pm1 = (a >> 32) * (uint32_t)b;
    pm2 = (uint32_t)a * (b >> 32);
    ph = (a >> 32) * (b >> 32);

    ph += pm1 >> 32;
    pm1 = (uint64_t)((uint32_t)pm1) + pm2 + (pl >> 32);

    *phigh = ph + (pm1 >> 32);
    *plow = (pm1 << 32) + (uint32_t)pl;
#endif
}
