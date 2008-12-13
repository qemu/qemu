/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "config.h"
#include "osdep.h"
#include "cpu.h" // For TARGET_LONG_BITS
#include "tcg.h"

int64_t tcg_helper_shl_i64(int64_t arg1, int64_t arg2)
{
    return arg1 << arg2;
}

int64_t tcg_helper_shr_i64(int64_t arg1, int64_t arg2)
{
    return (uint64_t)arg1 >> arg2;
}

int64_t tcg_helper_sar_i64(int64_t arg1, int64_t arg2)
{
    return arg1 >> arg2;
}

int64_t tcg_helper_div_i64(int64_t arg1, int64_t arg2)
{
    return arg1 / arg2;
}

int64_t tcg_helper_rem_i64(int64_t arg1, int64_t arg2)
{
    return arg1 % arg2;
}

uint64_t tcg_helper_divu_i64(uint64_t arg1, uint64_t arg2)
{
    return arg1 / arg2;
}

uint64_t tcg_helper_remu_i64(uint64_t arg1, uint64_t arg2)
{
    return arg1 % arg2;
}

