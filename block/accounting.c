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
#include "qemu/timer.h"

static QEMUClockType clock_type = QEMU_CLOCK_REALTIME;

void block_acct_start(BlockAcctStats *stats, BlockAcctCookie *cookie,
                      int64_t bytes, enum BlockAcctType type)
{
    assert(type < BLOCK_MAX_IOTYPE);

    cookie->bytes = bytes;
    cookie->start_time_ns = qemu_clock_get_ns(clock_type);
    cookie->type = type;
}

void block_acct_done(BlockAcctStats *stats, BlockAcctCookie *cookie)
{
    int64_t time_ns = qemu_clock_get_ns(clock_type);
    int64_t latency_ns = time_ns - cookie->start_time_ns;

    assert(cookie->type < BLOCK_MAX_IOTYPE);

    stats->nr_bytes[cookie->type] += cookie->bytes;
    stats->nr_ops[cookie->type]++;
    stats->total_time_ns[cookie->type] += latency_ns;
    stats->last_access_time_ns = time_ns;
}

void block_acct_failed(BlockAcctStats *stats, BlockAcctCookie *cookie)
{
    int64_t time_ns = qemu_clock_get_ns(clock_type);

    assert(cookie->type < BLOCK_MAX_IOTYPE);

    stats->failed_ops[cookie->type]++;
    stats->total_time_ns[cookie->type] += time_ns - cookie->start_time_ns;
    stats->last_access_time_ns = time_ns;
}

void block_acct_invalid(BlockAcctStats *stats, enum BlockAcctType type)
{
    assert(type < BLOCK_MAX_IOTYPE);

    /* block_acct_done() and block_acct_failed() update
     * total_time_ns[], but this one does not. The reason is that
     * invalid requests are accounted during their submission,
     * therefore there's no actual I/O involved. */

    stats->invalid_ops[type]++;
    stats->last_access_time_ns = qemu_clock_get_ns(clock_type);
}

void block_acct_merge_done(BlockAcctStats *stats, enum BlockAcctType type,
                      int num_requests)
{
    assert(type < BLOCK_MAX_IOTYPE);
    stats->merged[type] += num_requests;
}

int64_t block_acct_idle_time_ns(BlockAcctStats *stats)
{
    return qemu_clock_get_ns(clock_type) - stats->last_access_time_ns;
}
