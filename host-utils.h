/*
 * Utility compute operations used by translated code.
 *
 * Copyright (c) 2007 Thiemo Seufer
 * Copyright (c) 2007 Jocelyn Mayer
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

/* Note that some of those functions may end up calling libgcc functions,
   depending on the host machine. It is up to the target emulation to
   cope with that. */

/* Binary search for leading zeros.  */

static always_inline int clz32(uint32_t val)
{
    int cnt = 0;

    if (!(val & 0xFFFF0000U)) {
        cnt += 16;
        val <<= 16;
    }
    if (!(val & 0xFF000000U)) {
        cnt += 8;
        val <<= 8;
    }
    if (!(val & 0xF0000000U)) {
        cnt += 4;
        val <<= 4;
    }
    if (!(val & 0xC0000000U)) {
        cnt += 2;
        val <<= 2;
    }
    if (!(val & 0x80000000U)) {
        cnt++;
        val <<= 1;
    }
    if (!(val & 0x80000000U)) {
        cnt++;
    }
    return cnt;
}

static always_inline int clo32(uint32_t val)
{
    return clz32(~val);
}

static always_inline int clz64(uint64_t val)
{
    int cnt = 0;

    if (!(val & 0xFFFFFFFF00000000ULL)) {
        cnt += 32;
        val <<= 32;
    }
    if (!(val & 0xFFFF000000000000ULL)) {
        cnt += 16;
        val <<= 16;
    }
    if (!(val & 0xFF00000000000000ULL)) {
        cnt += 8;
        val <<= 8;
    }
    if (!(val & 0xF000000000000000ULL)) {
        cnt += 4;
        val <<= 4;
    }
    if (!(val & 0xC000000000000000ULL)) {
        cnt += 2;
        val <<= 2;
    }
    if (!(val & 0x8000000000000000ULL)) {
        cnt++;
        val <<= 1;
    }
    if (!(val & 0x8000000000000000ULL)) {
        cnt++;
    }
    return cnt;
}

static always_inline int clo64(uint64_t val)
{
    return clz64(~val);
}
