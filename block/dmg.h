/*
 * Header for DMG driver
 *
 * Copyright (c) 2004-2006 Fabrice Bellard
 * Copyright (c) 2016 Red hat, Inc.
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

#ifndef BLOCK_DMG_H
#define BLOCK_DMG_H

#include "block/block_int.h"
#include <zlib.h>

typedef struct BDRVDMGState {
    CoMutex lock;
    /* each chunk contains a certain number of sectors,
     * offsets[i] is the offset in the .dmg file,
     * lengths[i] is the length of the compressed chunk,
     * sectors[i] is the sector beginning at offsets[i],
     * sectorcounts[i] is the number of sectors in that chunk,
     * the sectors array is ordered
     * 0<=i<n_chunks */

    uint32_t n_chunks;
    uint32_t *types;
    uint64_t *offsets;
    uint64_t *lengths;
    uint64_t *sectors;
    uint64_t *sectorcounts;
    uint32_t current_chunk;
    uint8_t *compressed_chunk;
    uint8_t *uncompressed_chunk;
    z_stream zstream;
} BDRVDMGState;

typedef int BdrvDmgUncompressFunc(char *next_in, unsigned int avail_in,
                                  char *next_out, unsigned int avail_out);

extern BdrvDmgUncompressFunc *dmg_uncompress_bz2;
extern BdrvDmgUncompressFunc *dmg_uncompress_lzfse;

#endif
