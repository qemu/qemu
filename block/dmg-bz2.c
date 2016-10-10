/*
 * DMG bzip2 uncompression
 *
 * Copyright (c) 2004 Johannes E. Schindelin
 * Copyright (c) 2016 Red Hat, Inc.
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
#include "qemu-common.h"
#include "dmg.h"
#include <bzlib.h>

static int dmg_uncompress_bz2_do(char *next_in, unsigned int avail_in,
                                 char *next_out, unsigned int avail_out)
{
    int ret;
    uint64_t total_out;
    bz_stream bzstream = {};

    ret = BZ2_bzDecompressInit(&bzstream, 0, 0);
    if (ret != BZ_OK) {
        return -1;
    }
    bzstream.next_in = next_in;
    bzstream.avail_in = avail_in;
    bzstream.next_out = next_out;
    bzstream.avail_out = avail_out;
    ret = BZ2_bzDecompress(&bzstream);
    total_out = ((uint64_t)bzstream.total_out_hi32 << 32) +
                bzstream.total_out_lo32;
    BZ2_bzDecompressEnd(&bzstream);
    if (ret != BZ_STREAM_END ||
        total_out != avail_out) {
        return -1;
    }
    return 0;
}

__attribute__((constructor))
static void dmg_bz2_init(void)
{
    assert(!dmg_uncompress_bz2);
    dmg_uncompress_bz2 = dmg_uncompress_bz2_do;
}
