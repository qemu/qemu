/*
 * QEMU System Emulator block accounting
 *
 * Copyright (c) 2011 Christoph Hellwig
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

#include "block/accounting.h"
#include "block/block_int.h"

void block_acct_start(BlockAcctStats *stats, BlockAcctCookie *cookie,
                      int64_t bytes, enum BlockAcctType type)
{
    assert(type < BLOCK_MAX_IOTYPE);

    cookie->bytes = bytes;
    cookie->start_time_ns = get_clock();
    cookie->type = type;
}

void block_acct_done(BlockAcctStats *stats, BlockAcctCookie *cookie)
{
    assert(cookie->type < BLOCK_MAX_IOTYPE);

    stats->nr_bytes[cookie->type] += cookie->bytes;
    stats->nr_ops[cookie->type]++;
    stats->total_time_ns[cookie->type] += get_clock() - cookie->start_time_ns;
}


void block_acct_highest_sector(BlockAcctStats *stats, int64_t sector_num,
                               unsigned int nb_sectors)
{
    if (stats->wr_highest_sector < sector_num + nb_sectors - 1) {
        stats->wr_highest_sector = sector_num + nb_sectors - 1;
    }
}
