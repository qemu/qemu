/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2011-2015 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
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

#ifndef QEMU_MIGRATION_RAM_H
#define QEMU_MIGRATION_RAM_H

#include "qapi/qapi-types-migration.h"
#include "exec/cpu-common.h"
#include "io/channel.h"

extern MigrationStats ram_counters;
extern XBZRLECacheStats xbzrle_counters;
extern CompressionStats compression_counters;

bool ramblock_is_ignored(RAMBlock *block);
/* Should be holding either ram_list.mutex, or the RCU lock. */
#define RAMBLOCK_FOREACH_NOT_IGNORED(block)            \
    INTERNAL_RAMBLOCK_FOREACH(block)                   \
        if (ramblock_is_ignored(block)) {} else

#define RAMBLOCK_FOREACH_MIGRATABLE(block)             \
    INTERNAL_RAMBLOCK_FOREACH(block)                   \
        if (!qemu_ram_is_migratable(block)) {} else

int xbzrle_cache_resize(uint64_t new_size, Error **errp);
uint64_t ram_bytes_remaining(void);
uint64_t ram_bytes_total(void);
void mig_throttle_counter_reset(void);

uint64_t ram_pagesize_summary(void);
int ram_save_queue_pages(const char *rbname, ram_addr_t start, ram_addr_t len);
void acct_update_position(QEMUFile *f, size_t size, bool zero);
void ram_debug_dump_bitmap(unsigned long *todump, bool expected,
                           unsigned long pages);
void ram_postcopy_migrated_memory_release(MigrationState *ms);
/* For outgoing discard bitmap */
int ram_postcopy_send_discard_bitmap(MigrationState *ms);
/* For incoming postcopy discard */
int ram_discard_range(const char *block_name, uint64_t start, size_t length);
int ram_postcopy_incoming_init(MigrationIncomingState *mis);

void ram_handle_compressed(void *host, uint8_t ch, uint64_t size);

int ramblock_recv_bitmap_test(RAMBlock *rb, void *host_addr);
bool ramblock_recv_bitmap_test_byte_offset(RAMBlock *rb, uint64_t byte_offset);
void ramblock_recv_bitmap_set(RAMBlock *rb, void *host_addr);
void ramblock_recv_bitmap_set_range(RAMBlock *rb, void *host_addr, size_t nr);
int64_t ramblock_recv_bitmap_send(QEMUFile *file,
                                  const char *block_name);
int ram_dirty_bitmap_reload(MigrationState *s, RAMBlock *rb);
bool ramblock_page_is_discarded(RAMBlock *rb, ram_addr_t start);

/* ram cache */
int colo_init_ram_cache(void);
void colo_flush_ram_cache(void);
void colo_release_ram_cache(void);
void colo_incoming_start_dirty_log(void);

/* Background snapshot */
bool ram_write_tracking_available(void);
bool ram_write_tracking_compatible(void);
void ram_write_tracking_prepare(void);
int ram_write_tracking_start(void);
void ram_write_tracking_stop(void);

#endif
