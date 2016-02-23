/*
 * QEMU System Emulator block accounting
 *
 * Copyright (c) 2011 Christoph Hellwig
 * Copyright (c) 2015 Igalia, S.L.
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
#ifndef BLOCK_ACCOUNTING_H
#define BLOCK_ACCOUNTING_H


#include "qemu/typedefs.h"
#include "qemu/timed-average.h"

typedef struct BlockAcctTimedStats BlockAcctTimedStats;

enum BlockAcctType {
    BLOCK_ACCT_READ,
    BLOCK_ACCT_WRITE,
    BLOCK_ACCT_FLUSH,
    BLOCK_MAX_IOTYPE,
};

struct BlockAcctTimedStats {
    TimedAverage latency[BLOCK_MAX_IOTYPE];
    unsigned interval_length; /* in seconds */
    QSLIST_ENTRY(BlockAcctTimedStats) entries;
};

typedef struct BlockAcctStats {
    uint64_t nr_bytes[BLOCK_MAX_IOTYPE];
    uint64_t nr_ops[BLOCK_MAX_IOTYPE];
    uint64_t invalid_ops[BLOCK_MAX_IOTYPE];
    uint64_t failed_ops[BLOCK_MAX_IOTYPE];
    uint64_t total_time_ns[BLOCK_MAX_IOTYPE];
    uint64_t merged[BLOCK_MAX_IOTYPE];
    int64_t last_access_time_ns;
    QSLIST_HEAD(, BlockAcctTimedStats) intervals;
    bool account_invalid;
    bool account_failed;
} BlockAcctStats;

typedef struct BlockAcctCookie {
    int64_t bytes;
    int64_t start_time_ns;
    enum BlockAcctType type;
} BlockAcctCookie;

void block_acct_init(BlockAcctStats *stats, bool account_invalid,
                     bool account_failed);
void block_acct_cleanup(BlockAcctStats *stats);
void block_acct_add_interval(BlockAcctStats *stats, unsigned interval_length);
BlockAcctTimedStats *block_acct_interval_next(BlockAcctStats *stats,
                                              BlockAcctTimedStats *s);
void block_acct_start(BlockAcctStats *stats, BlockAcctCookie *cookie,
                      int64_t bytes, enum BlockAcctType type);
void block_acct_done(BlockAcctStats *stats, BlockAcctCookie *cookie);
void block_acct_failed(BlockAcctStats *stats, BlockAcctCookie *cookie);
void block_acct_invalid(BlockAcctStats *stats, enum BlockAcctType type);
void block_acct_merge_done(BlockAcctStats *stats, enum BlockAcctType type,
                           int num_requests);
int64_t block_acct_idle_time_ns(BlockAcctStats *stats);
double block_acct_queue_depth(BlockAcctTimedStats *stats,
                              enum BlockAcctType type);

#endif
