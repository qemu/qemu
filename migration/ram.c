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
#include "qemu/osdep.h"
#include "cpu.h"
#include <zlib.h>
#include "qapi-event.h"
#include "qemu/cutils.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "qemu/main-loop.h"
#include "xbzrle.h"
#include "ram.h"
#include "migration.h"
#include "migration/register.h"
#include "migration/misc.h"
#include "qemu-file.h"
#include "postcopy-ram.h"
#include "migration/page_cache.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "exec/ram_addr.h"
#include "qemu/rcu_queue.h"
#include "migration/colo.h"
#include "migration/block.h"

/***********************************************************/
/* ram save/restore */

/* RAM_SAVE_FLAG_ZERO used to be named RAM_SAVE_FLAG_COMPRESS, it
 * worked for pages that where filled with the same char.  We switched
 * it to only search for the zero value.  And to avoid confusion with
 * RAM_SSAVE_FLAG_COMPRESS_PAGE just rename it.
 */

#define RAM_SAVE_FLAG_FULL     0x01 /* Obsolete, not used anymore */
#define RAM_SAVE_FLAG_ZERO     0x02
#define RAM_SAVE_FLAG_MEM_SIZE 0x04
#define RAM_SAVE_FLAG_PAGE     0x08
#define RAM_SAVE_FLAG_EOS      0x10
#define RAM_SAVE_FLAG_CONTINUE 0x20
#define RAM_SAVE_FLAG_XBZRLE   0x40
/* 0x80 is reserved in migration.h start with 0x100 next */
#define RAM_SAVE_FLAG_COMPRESS_PAGE    0x100

static inline bool is_zero_range(uint8_t *p, uint64_t size)
{
    return buffer_is_zero(p, size);
}

XBZRLECacheStats xbzrle_counters;

/* struct contains XBZRLE cache and a static page
   used by the compression */
static struct {
    /* buffer used for XBZRLE encoding */
    uint8_t *encoded_buf;
    /* buffer for storing page content */
    uint8_t *current_buf;
    /* Cache for XBZRLE, Protected by lock. */
    PageCache *cache;
    QemuMutex lock;
    /* it will store a page full of zeros */
    uint8_t *zero_target_page;
    /* buffer used for XBZRLE decoding */
    uint8_t *decoded_buf;
} XBZRLE;

static void XBZRLE_cache_lock(void)
{
    if (migrate_use_xbzrle())
        qemu_mutex_lock(&XBZRLE.lock);
}

static void XBZRLE_cache_unlock(void)
{
    if (migrate_use_xbzrle())
        qemu_mutex_unlock(&XBZRLE.lock);
}

/**
 * xbzrle_cache_resize: resize the xbzrle cache
 *
 * This function is called from qmp_migrate_set_cache_size in main
 * thread, possibly while a migration is in progress.  A running
 * migration may be using the cache and might finish during this call,
 * hence changes to the cache are protected by XBZRLE.lock().
 *
 * Returns the new_size or negative in case of error.
 *
 * @new_size: new cache size
 */
int64_t xbzrle_cache_resize(int64_t new_size)
{
    PageCache *new_cache;
    int64_t ret;

    if (new_size < TARGET_PAGE_SIZE) {
        return -1;
    }

    XBZRLE_cache_lock();

    if (XBZRLE.cache != NULL) {
        if (pow2floor(new_size) == migrate_xbzrle_cache_size()) {
            goto out_new_size;
        }
        new_cache = cache_init(new_size / TARGET_PAGE_SIZE,
                                        TARGET_PAGE_SIZE);
        if (!new_cache) {
            error_report("Error creating cache");
            ret = -1;
            goto out;
        }

        cache_fini(XBZRLE.cache);
        XBZRLE.cache = new_cache;
    }

out_new_size:
    ret = pow2floor(new_size);
out:
    XBZRLE_cache_unlock();
    return ret;
}

/*
 * An outstanding page request, on the source, having been received
 * and queued
 */
struct RAMSrcPageRequest {
    RAMBlock *rb;
    hwaddr    offset;
    hwaddr    len;

    QSIMPLEQ_ENTRY(RAMSrcPageRequest) next_req;
};

/* State of RAM for migration */
struct RAMState {
    /* QEMUFile used for this migration */
    QEMUFile *f;
    /* Last block that we have visited searching for dirty pages */
    RAMBlock *last_seen_block;
    /* Last block from where we have sent data */
    RAMBlock *last_sent_block;
    /* Last dirty target page we have sent */
    ram_addr_t last_page;
    /* last ram version we have seen */
    uint32_t last_version;
    /* We are in the first round */
    bool ram_bulk_stage;
    /* How many times we have dirty too many pages */
    int dirty_rate_high_cnt;
    /* these variables are used for bitmap sync */
    /* last time we did a full bitmap_sync */
    int64_t time_last_bitmap_sync;
    /* bytes transferred at start_time */
    uint64_t bytes_xfer_prev;
    /* number of dirty pages since start_time */
    uint64_t num_dirty_pages_period;
    /* xbzrle misses since the beginning of the period */
    uint64_t xbzrle_cache_miss_prev;
    /* number of iterations at the beginning of period */
    uint64_t iterations_prev;
    /* Iterations since start */
    uint64_t iterations;
    /* number of dirty bits in the bitmap */
    uint64_t migration_dirty_pages;
    /* protects modification of the bitmap */
    QemuMutex bitmap_mutex;
    /* The RAMBlock used in the last src_page_requests */
    RAMBlock *last_req_rb;
    /* Queue of outstanding page requests from the destination */
    QemuMutex src_page_req_mutex;
    QSIMPLEQ_HEAD(src_page_requests, RAMSrcPageRequest) src_page_requests;
};
typedef struct RAMState RAMState;

static RAMState *ram_state;

uint64_t ram_bytes_remaining(void)
{
    return ram_state->migration_dirty_pages * TARGET_PAGE_SIZE;
}

MigrationStats ram_counters;

/* used by the search for pages to send */
struct PageSearchStatus {
    /* Current block being searched */
    RAMBlock    *block;
    /* Current page to search from */
    unsigned long page;
    /* Set once we wrap around */
    bool         complete_round;
};
typedef struct PageSearchStatus PageSearchStatus;

struct CompressParam {
    bool done;
    bool quit;
    QEMUFile *file;
    QemuMutex mutex;
    QemuCond cond;
    RAMBlock *block;
    ram_addr_t offset;
};
typedef struct CompressParam CompressParam;

struct DecompressParam {
    bool done;
    bool quit;
    QemuMutex mutex;
    QemuCond cond;
    void *des;
    uint8_t *compbuf;
    int len;
};
typedef struct DecompressParam DecompressParam;

static CompressParam *comp_param;
static QemuThread *compress_threads;
/* comp_done_cond is used to wake up the migration thread when
 * one of the compression threads has finished the compression.
 * comp_done_lock is used to co-work with comp_done_cond.
 */
static QemuMutex comp_done_lock;
static QemuCond comp_done_cond;
/* The empty QEMUFileOps will be used by file in CompressParam */
static const QEMUFileOps empty_ops = { };

static DecompressParam *decomp_param;
static QemuThread *decompress_threads;
static QemuMutex decomp_done_lock;
static QemuCond decomp_done_cond;

static int do_compress_ram_page(QEMUFile *f, RAMBlock *block,
                                ram_addr_t offset);

static void *do_data_compress(void *opaque)
{
    CompressParam *param = opaque;
    RAMBlock *block;
    ram_addr_t offset;

    qemu_mutex_lock(&param->mutex);
    while (!param->quit) {
        if (param->block) {
            block = param->block;
            offset = param->offset;
            param->block = NULL;
            qemu_mutex_unlock(&param->mutex);

            do_compress_ram_page(param->file, block, offset);

            qemu_mutex_lock(&comp_done_lock);
            param->done = true;
            qemu_cond_signal(&comp_done_cond);
            qemu_mutex_unlock(&comp_done_lock);

            qemu_mutex_lock(&param->mutex);
        } else {
            qemu_cond_wait(&param->cond, &param->mutex);
        }
    }
    qemu_mutex_unlock(&param->mutex);

    return NULL;
}

static inline void terminate_compression_threads(void)
{
    int idx, thread_count;

    thread_count = migrate_compress_threads();

    for (idx = 0; idx < thread_count; idx++) {
        qemu_mutex_lock(&comp_param[idx].mutex);
        comp_param[idx].quit = true;
        qemu_cond_signal(&comp_param[idx].cond);
        qemu_mutex_unlock(&comp_param[idx].mutex);
    }
}

static void compress_threads_save_cleanup(void)
{
    int i, thread_count;

    if (!migrate_use_compression()) {
        return;
    }
    terminate_compression_threads();
    thread_count = migrate_compress_threads();
    for (i = 0; i < thread_count; i++) {
        qemu_thread_join(compress_threads + i);
        qemu_fclose(comp_param[i].file);
        qemu_mutex_destroy(&comp_param[i].mutex);
        qemu_cond_destroy(&comp_param[i].cond);
    }
    qemu_mutex_destroy(&comp_done_lock);
    qemu_cond_destroy(&comp_done_cond);
    g_free(compress_threads);
    g_free(comp_param);
    compress_threads = NULL;
    comp_param = NULL;
}

static void compress_threads_save_setup(void)
{
    int i, thread_count;

    if (!migrate_use_compression()) {
        return;
    }
    thread_count = migrate_compress_threads();
    compress_threads = g_new0(QemuThread, thread_count);
    comp_param = g_new0(CompressParam, thread_count);
    qemu_cond_init(&comp_done_cond);
    qemu_mutex_init(&comp_done_lock);
    for (i = 0; i < thread_count; i++) {
        /* comp_param[i].file is just used as a dummy buffer to save data,
         * set its ops to empty.
         */
        comp_param[i].file = qemu_fopen_ops(NULL, &empty_ops);
        comp_param[i].done = true;
        comp_param[i].quit = false;
        qemu_mutex_init(&comp_param[i].mutex);
        qemu_cond_init(&comp_param[i].cond);
        qemu_thread_create(compress_threads + i, "compress",
                           do_data_compress, comp_param + i,
                           QEMU_THREAD_JOINABLE);
    }
}

/**
 * save_page_header: write page header to wire
 *
 * If this is the 1st block, it also writes the block identification
 *
 * Returns the number of bytes written
 *
 * @f: QEMUFile where to send the data
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 *          in the lower bits, it contains flags
 */
static size_t save_page_header(RAMState *rs, QEMUFile *f,  RAMBlock *block,
                               ram_addr_t offset)
{
    size_t size, len;

    if (block == rs->last_sent_block) {
        offset |= RAM_SAVE_FLAG_CONTINUE;
    }
    qemu_put_be64(f, offset);
    size = 8;

    if (!(offset & RAM_SAVE_FLAG_CONTINUE)) {
        len = strlen(block->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)block->idstr, len);
        size += 1 + len;
        rs->last_sent_block = block;
    }
    return size;
}

/**
 * mig_throttle_guest_down: throotle down the guest
 *
 * Reduce amount of guest cpu execution to hopefully slow down memory
 * writes. If guest dirty memory rate is reduced below the rate at
 * which we can transfer pages to the destination then we should be
 * able to complete migration. Some workloads dirty memory way too
 * fast and will not effectively converge, even with auto-converge.
 */
static void mig_throttle_guest_down(void)
{
    MigrationState *s = migrate_get_current();
    uint64_t pct_initial = s->parameters.cpu_throttle_initial;
    uint64_t pct_icrement = s->parameters.cpu_throttle_increment;

    /* We have not started throttling yet. Let's start it. */
    if (!cpu_throttle_active()) {
        cpu_throttle_set(pct_initial);
    } else {
        /* Throttling already on, just increase the rate */
        cpu_throttle_set(cpu_throttle_get_percentage() + pct_icrement);
    }
}

/**
 * xbzrle_cache_zero_page: insert a zero page in the XBZRLE cache
 *
 * @rs: current RAM state
 * @current_addr: address for the zero page
 *
 * Update the xbzrle cache to reflect a page that's been sent as all 0.
 * The important thing is that a stale (not-yet-0'd) page be replaced
 * by the new data.
 * As a bonus, if the page wasn't in the cache it gets added so that
 * when a small write is made into the 0'd page it gets XBZRLE sent.
 */
static void xbzrle_cache_zero_page(RAMState *rs, ram_addr_t current_addr)
{
    if (rs->ram_bulk_stage || !migrate_use_xbzrle()) {
        return;
    }

    /* We don't care if this fails to allocate a new cache page
     * as long as it updated an old one */
    cache_insert(XBZRLE.cache, current_addr, XBZRLE.zero_target_page,
                 ram_counters.dirty_sync_count);
}

#define ENCODING_FLAG_XBZRLE 0x1

/**
 * save_xbzrle_page: compress and send current page
 *
 * Returns: 1 means that we wrote the page
 *          0 means that page is identical to the one already sent
 *          -1 means that xbzrle would be longer than normal
 *
 * @rs: current RAM state
 * @current_data: pointer to the address of the page contents
 * @current_addr: addr of the page
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @last_stage: if we are at the completion stage
 */
static int save_xbzrle_page(RAMState *rs, uint8_t **current_data,
                            ram_addr_t current_addr, RAMBlock *block,
                            ram_addr_t offset, bool last_stage)
{
    int encoded_len = 0, bytes_xbzrle;
    uint8_t *prev_cached_page;

    if (!cache_is_cached(XBZRLE.cache, current_addr,
                         ram_counters.dirty_sync_count)) {
        xbzrle_counters.cache_miss++;
        if (!last_stage) {
            if (cache_insert(XBZRLE.cache, current_addr, *current_data,
                             ram_counters.dirty_sync_count) == -1) {
                return -1;
            } else {
                /* update *current_data when the page has been
                   inserted into cache */
                *current_data = get_cached_data(XBZRLE.cache, current_addr);
            }
        }
        return -1;
    }

    prev_cached_page = get_cached_data(XBZRLE.cache, current_addr);

    /* save current buffer into memory */
    memcpy(XBZRLE.current_buf, *current_data, TARGET_PAGE_SIZE);

    /* XBZRLE encoding (if there is no overflow) */
    encoded_len = xbzrle_encode_buffer(prev_cached_page, XBZRLE.current_buf,
                                       TARGET_PAGE_SIZE, XBZRLE.encoded_buf,
                                       TARGET_PAGE_SIZE);
    if (encoded_len == 0) {
        trace_save_xbzrle_page_skipping();
        return 0;
    } else if (encoded_len == -1) {
        trace_save_xbzrle_page_overflow();
        xbzrle_counters.overflow++;
        /* update data in the cache */
        if (!last_stage) {
            memcpy(prev_cached_page, *current_data, TARGET_PAGE_SIZE);
            *current_data = prev_cached_page;
        }
        return -1;
    }

    /* we need to update the data in the cache, in order to get the same data */
    if (!last_stage) {
        memcpy(prev_cached_page, XBZRLE.current_buf, TARGET_PAGE_SIZE);
    }

    /* Send XBZRLE based compressed page */
    bytes_xbzrle = save_page_header(rs, rs->f, block,
                                    offset | RAM_SAVE_FLAG_XBZRLE);
    qemu_put_byte(rs->f, ENCODING_FLAG_XBZRLE);
    qemu_put_be16(rs->f, encoded_len);
    qemu_put_buffer(rs->f, XBZRLE.encoded_buf, encoded_len);
    bytes_xbzrle += encoded_len + 1 + 2;
    xbzrle_counters.pages++;
    xbzrle_counters.bytes += bytes_xbzrle;
    ram_counters.transferred += bytes_xbzrle;

    return 1;
}

/**
 * migration_bitmap_find_dirty: find the next dirty page from start
 *
 * Called with rcu_read_lock() to protect migration_bitmap
 *
 * Returns the byte offset within memory region of the start of a dirty page
 *
 * @rs: current RAM state
 * @rb: RAMBlock where to search for dirty pages
 * @start: page where we start the search
 */
static inline
unsigned long migration_bitmap_find_dirty(RAMState *rs, RAMBlock *rb,
                                          unsigned long start)
{
    unsigned long size = rb->used_length >> TARGET_PAGE_BITS;
    unsigned long *bitmap = rb->bmap;
    unsigned long next;

    if (rs->ram_bulk_stage && start > 0) {
        next = start + 1;
    } else {
        next = find_next_bit(bitmap, size, start);
    }

    return next;
}

static inline bool migration_bitmap_clear_dirty(RAMState *rs,
                                                RAMBlock *rb,
                                                unsigned long page)
{
    bool ret;

    ret = test_and_clear_bit(page, rb->bmap);

    if (ret) {
        rs->migration_dirty_pages--;
    }
    return ret;
}

static void migration_bitmap_sync_range(RAMState *rs, RAMBlock *rb,
                                        ram_addr_t start, ram_addr_t length)
{
    rs->migration_dirty_pages +=
        cpu_physical_memory_sync_dirty_bitmap(rb, start, length,
                                              &rs->num_dirty_pages_period);
}

/**
 * ram_pagesize_summary: calculate all the pagesizes of a VM
 *
 * Returns a summary bitmap of the page sizes of all RAMBlocks
 *
 * For VMs with just normal pages this is equivalent to the host page
 * size. If it's got some huge pages then it's the OR of all the
 * different page sizes.
 */
uint64_t ram_pagesize_summary(void)
{
    RAMBlock *block;
    uint64_t summary = 0;

    RAMBLOCK_FOREACH(block) {
        summary |= block->page_size;
    }

    return summary;
}

static void migration_bitmap_sync(RAMState *rs)
{
    RAMBlock *block;
    int64_t end_time;
    uint64_t bytes_xfer_now;

    ram_counters.dirty_sync_count++;

    if (!rs->time_last_bitmap_sync) {
        rs->time_last_bitmap_sync = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    }

    trace_migration_bitmap_sync_start();
    memory_global_dirty_log_sync();

    qemu_mutex_lock(&rs->bitmap_mutex);
    rcu_read_lock();
    RAMBLOCK_FOREACH(block) {
        migration_bitmap_sync_range(rs, block, 0, block->used_length);
    }
    rcu_read_unlock();
    qemu_mutex_unlock(&rs->bitmap_mutex);

    trace_migration_bitmap_sync_end(rs->num_dirty_pages_period);

    end_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    /* more than 1 second = 1000 millisecons */
    if (end_time > rs->time_last_bitmap_sync + 1000) {
        /* calculate period counters */
        ram_counters.dirty_pages_rate = rs->num_dirty_pages_period * 1000
            / (end_time - rs->time_last_bitmap_sync);
        bytes_xfer_now = ram_counters.transferred;

        /* During block migration the auto-converge logic incorrectly detects
         * that ram migration makes no progress. Avoid this by disabling the
         * throttling logic during the bulk phase of block migration. */
        if (migrate_auto_converge() && !blk_mig_bulk_active()) {
            /* The following detection logic can be refined later. For now:
               Check to see if the dirtied bytes is 50% more than the approx.
               amount of bytes that just got transferred since the last time we
               were in this routine. If that happens twice, start or increase
               throttling */

            if ((rs->num_dirty_pages_period * TARGET_PAGE_SIZE >
                   (bytes_xfer_now - rs->bytes_xfer_prev) / 2) &&
                (++rs->dirty_rate_high_cnt >= 2)) {
                    trace_migration_throttle();
                    rs->dirty_rate_high_cnt = 0;
                    mig_throttle_guest_down();
            }
        }

        if (migrate_use_xbzrle()) {
            if (rs->iterations_prev != rs->iterations) {
                xbzrle_counters.cache_miss_rate =
                   (double)(xbzrle_counters.cache_miss -
                            rs->xbzrle_cache_miss_prev) /
                   (rs->iterations - rs->iterations_prev);
            }
            rs->iterations_prev = rs->iterations;
            rs->xbzrle_cache_miss_prev = xbzrle_counters.cache_miss;
        }

        /* reset period counters */
        rs->time_last_bitmap_sync = end_time;
        rs->num_dirty_pages_period = 0;
        rs->bytes_xfer_prev = bytes_xfer_now;
    }
    if (migrate_use_events()) {
        qapi_event_send_migration_pass(ram_counters.dirty_sync_count, NULL);
    }
}

/**
 * save_zero_page: send the zero page to the stream
 *
 * Returns the number of pages written.
 *
 * @rs: current RAM state
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @p: pointer to the page
 */
static int save_zero_page(RAMState *rs, RAMBlock *block, ram_addr_t offset,
                          uint8_t *p)
{
    int pages = -1;

    if (is_zero_range(p, TARGET_PAGE_SIZE)) {
        ram_counters.duplicate++;
        ram_counters.transferred +=
            save_page_header(rs, rs->f, block, offset | RAM_SAVE_FLAG_ZERO);
        qemu_put_byte(rs->f, 0);
        ram_counters.transferred += 1;
        pages = 1;
    }

    return pages;
}

static void ram_release_pages(const char *rbname, uint64_t offset, int pages)
{
    if (!migrate_release_ram() || !migration_in_postcopy()) {
        return;
    }

    ram_discard_range(rbname, offset, pages << TARGET_PAGE_BITS);
}

/**
 * ram_save_page: send the given page to the stream
 *
 * Returns the number of pages written.
 *          < 0 - error
 *          >=0 - Number of pages written - this might legally be 0
 *                if xbzrle noticed the page was the same.
 *
 * @rs: current RAM state
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @last_stage: if we are at the completion stage
 */
static int ram_save_page(RAMState *rs, PageSearchStatus *pss, bool last_stage)
{
    int pages = -1;
    uint64_t bytes_xmit;
    ram_addr_t current_addr;
    uint8_t *p;
    int ret;
    bool send_async = true;
    RAMBlock *block = pss->block;
    ram_addr_t offset = pss->page << TARGET_PAGE_BITS;

    p = block->host + offset;
    trace_ram_save_page(block->idstr, (uint64_t)offset, p);

    /* In doubt sent page as normal */
    bytes_xmit = 0;
    ret = ram_control_save_page(rs->f, block->offset,
                           offset, TARGET_PAGE_SIZE, &bytes_xmit);
    if (bytes_xmit) {
        ram_counters.transferred += bytes_xmit;
        pages = 1;
    }

    XBZRLE_cache_lock();

    current_addr = block->offset + offset;

    if (ret != RAM_SAVE_CONTROL_NOT_SUPP) {
        if (ret != RAM_SAVE_CONTROL_DELAYED) {
            if (bytes_xmit > 0) {
                ram_counters.normal++;
            } else if (bytes_xmit == 0) {
                ram_counters.duplicate++;
            }
        }
    } else {
        pages = save_zero_page(rs, block, offset, p);
        if (pages > 0) {
            /* Must let xbzrle know, otherwise a previous (now 0'd) cached
             * page would be stale
             */
            xbzrle_cache_zero_page(rs, current_addr);
            ram_release_pages(block->idstr, offset, pages);
        } else if (!rs->ram_bulk_stage &&
                   !migration_in_postcopy() && migrate_use_xbzrle()) {
            pages = save_xbzrle_page(rs, &p, current_addr, block,
                                     offset, last_stage);
            if (!last_stage) {
                /* Can't send this cached data async, since the cache page
                 * might get updated before it gets to the wire
                 */
                send_async = false;
            }
        }
    }

    /* XBZRLE overflow or normal page */
    if (pages == -1) {
        ram_counters.transferred +=
            save_page_header(rs, rs->f, block, offset | RAM_SAVE_FLAG_PAGE);
        if (send_async) {
            qemu_put_buffer_async(rs->f, p, TARGET_PAGE_SIZE,
                                  migrate_release_ram() &
                                  migration_in_postcopy());
        } else {
            qemu_put_buffer(rs->f, p, TARGET_PAGE_SIZE);
        }
        ram_counters.transferred += TARGET_PAGE_SIZE;
        pages = 1;
        ram_counters.normal++;
    }

    XBZRLE_cache_unlock();

    return pages;
}

static int do_compress_ram_page(QEMUFile *f, RAMBlock *block,
                                ram_addr_t offset)
{
    RAMState *rs = ram_state;
    int bytes_sent, blen;
    uint8_t *p = block->host + (offset & TARGET_PAGE_MASK);

    bytes_sent = save_page_header(rs, f, block, offset |
                                  RAM_SAVE_FLAG_COMPRESS_PAGE);
    blen = qemu_put_compression_data(f, p, TARGET_PAGE_SIZE,
                                     migrate_compress_level());
    if (blen < 0) {
        bytes_sent = 0;
        qemu_file_set_error(migrate_get_current()->to_dst_file, blen);
        error_report("compressed data failed!");
    } else {
        bytes_sent += blen;
        ram_release_pages(block->idstr, offset & TARGET_PAGE_MASK, 1);
    }

    return bytes_sent;
}

static void flush_compressed_data(RAMState *rs)
{
    int idx, len, thread_count;

    if (!migrate_use_compression()) {
        return;
    }
    thread_count = migrate_compress_threads();

    qemu_mutex_lock(&comp_done_lock);
    for (idx = 0; idx < thread_count; idx++) {
        while (!comp_param[idx].done) {
            qemu_cond_wait(&comp_done_cond, &comp_done_lock);
        }
    }
    qemu_mutex_unlock(&comp_done_lock);

    for (idx = 0; idx < thread_count; idx++) {
        qemu_mutex_lock(&comp_param[idx].mutex);
        if (!comp_param[idx].quit) {
            len = qemu_put_qemu_file(rs->f, comp_param[idx].file);
            ram_counters.transferred += len;
        }
        qemu_mutex_unlock(&comp_param[idx].mutex);
    }
}

static inline void set_compress_params(CompressParam *param, RAMBlock *block,
                                       ram_addr_t offset)
{
    param->block = block;
    param->offset = offset;
}

static int compress_page_with_multi_thread(RAMState *rs, RAMBlock *block,
                                           ram_addr_t offset)
{
    int idx, thread_count, bytes_xmit = -1, pages = -1;

    thread_count = migrate_compress_threads();
    qemu_mutex_lock(&comp_done_lock);
    while (true) {
        for (idx = 0; idx < thread_count; idx++) {
            if (comp_param[idx].done) {
                comp_param[idx].done = false;
                bytes_xmit = qemu_put_qemu_file(rs->f, comp_param[idx].file);
                qemu_mutex_lock(&comp_param[idx].mutex);
                set_compress_params(&comp_param[idx], block, offset);
                qemu_cond_signal(&comp_param[idx].cond);
                qemu_mutex_unlock(&comp_param[idx].mutex);
                pages = 1;
                ram_counters.normal++;
                ram_counters.transferred += bytes_xmit;
                break;
            }
        }
        if (pages > 0) {
            break;
        } else {
            qemu_cond_wait(&comp_done_cond, &comp_done_lock);
        }
    }
    qemu_mutex_unlock(&comp_done_lock);

    return pages;
}

/**
 * ram_save_compressed_page: compress the given page and send it to the stream
 *
 * Returns the number of pages written.
 *
 * @rs: current RAM state
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @last_stage: if we are at the completion stage
 */
static int ram_save_compressed_page(RAMState *rs, PageSearchStatus *pss,
                                    bool last_stage)
{
    int pages = -1;
    uint64_t bytes_xmit = 0;
    uint8_t *p;
    int ret, blen;
    RAMBlock *block = pss->block;
    ram_addr_t offset = pss->page << TARGET_PAGE_BITS;

    p = block->host + offset;

    ret = ram_control_save_page(rs->f, block->offset,
                                offset, TARGET_PAGE_SIZE, &bytes_xmit);
    if (bytes_xmit) {
        ram_counters.transferred += bytes_xmit;
        pages = 1;
    }
    if (ret != RAM_SAVE_CONTROL_NOT_SUPP) {
        if (ret != RAM_SAVE_CONTROL_DELAYED) {
            if (bytes_xmit > 0) {
                ram_counters.normal++;
            } else if (bytes_xmit == 0) {
                ram_counters.duplicate++;
            }
        }
    } else {
        /* When starting the process of a new block, the first page of
         * the block should be sent out before other pages in the same
         * block, and all the pages in last block should have been sent
         * out, keeping this order is important, because the 'cont' flag
         * is used to avoid resending the block name.
         */
        if (block != rs->last_sent_block) {
            flush_compressed_data(rs);
            pages = save_zero_page(rs, block, offset, p);
            if (pages == -1) {
                /* Make sure the first page is sent out before other pages */
                bytes_xmit = save_page_header(rs, rs->f, block, offset |
                                              RAM_SAVE_FLAG_COMPRESS_PAGE);
                blen = qemu_put_compression_data(rs->f, p, TARGET_PAGE_SIZE,
                                                 migrate_compress_level());
                if (blen > 0) {
                    ram_counters.transferred += bytes_xmit + blen;
                    ram_counters.normal++;
                    pages = 1;
                } else {
                    qemu_file_set_error(rs->f, blen);
                    error_report("compressed data failed!");
                }
            }
            if (pages > 0) {
                ram_release_pages(block->idstr, offset, pages);
            }
        } else {
            pages = save_zero_page(rs, block, offset, p);
            if (pages == -1) {
                pages = compress_page_with_multi_thread(rs, block, offset);
            } else {
                ram_release_pages(block->idstr, offset, pages);
            }
        }
    }

    return pages;
}

/**
 * find_dirty_block: find the next dirty page and update any state
 * associated with the search process.
 *
 * Returns if a page is found
 *
 * @rs: current RAM state
 * @pss: data about the state of the current dirty page scan
 * @again: set to false if the search has scanned the whole of RAM
 */
static bool find_dirty_block(RAMState *rs, PageSearchStatus *pss, bool *again)
{
    pss->page = migration_bitmap_find_dirty(rs, pss->block, pss->page);
    if (pss->complete_round && pss->block == rs->last_seen_block &&
        pss->page >= rs->last_page) {
        /*
         * We've been once around the RAM and haven't found anything.
         * Give up.
         */
        *again = false;
        return false;
    }
    if ((pss->page << TARGET_PAGE_BITS) >= pss->block->used_length) {
        /* Didn't find anything in this RAM Block */
        pss->page = 0;
        pss->block = QLIST_NEXT_RCU(pss->block, next);
        if (!pss->block) {
            /* Hit the end of the list */
            pss->block = QLIST_FIRST_RCU(&ram_list.blocks);
            /* Flag that we've looped */
            pss->complete_round = true;
            rs->ram_bulk_stage = false;
            if (migrate_use_xbzrle()) {
                /* If xbzrle is on, stop using the data compression at this
                 * point. In theory, xbzrle can do better than compression.
                 */
                flush_compressed_data(rs);
            }
        }
        /* Didn't find anything this time, but try again on the new block */
        *again = true;
        return false;
    } else {
        /* Can go around again, but... */
        *again = true;
        /* We've found something so probably don't need to */
        return true;
    }
}

/**
 * unqueue_page: gets a page of the queue
 *
 * Helper for 'get_queued_page' - gets a page off the queue
 *
 * Returns the block of the page (or NULL if none available)
 *
 * @rs: current RAM state
 * @offset: used to return the offset within the RAMBlock
 */
static RAMBlock *unqueue_page(RAMState *rs, ram_addr_t *offset)
{
    RAMBlock *block = NULL;

    qemu_mutex_lock(&rs->src_page_req_mutex);
    if (!QSIMPLEQ_EMPTY(&rs->src_page_requests)) {
        struct RAMSrcPageRequest *entry =
                                QSIMPLEQ_FIRST(&rs->src_page_requests);
        block = entry->rb;
        *offset = entry->offset;

        if (entry->len > TARGET_PAGE_SIZE) {
            entry->len -= TARGET_PAGE_SIZE;
            entry->offset += TARGET_PAGE_SIZE;
        } else {
            memory_region_unref(block->mr);
            QSIMPLEQ_REMOVE_HEAD(&rs->src_page_requests, next_req);
            g_free(entry);
        }
    }
    qemu_mutex_unlock(&rs->src_page_req_mutex);

    return block;
}

/**
 * get_queued_page: unqueue a page from the postocpy requests
 *
 * Skips pages that are already sent (!dirty)
 *
 * Returns if a queued page is found
 *
 * @rs: current RAM state
 * @pss: data about the state of the current dirty page scan
 */
static bool get_queued_page(RAMState *rs, PageSearchStatus *pss)
{
    RAMBlock  *block;
    ram_addr_t offset;
    bool dirty;

    do {
        block = unqueue_page(rs, &offset);
        /*
         * We're sending this page, and since it's postcopy nothing else
         * will dirty it, and we must make sure it doesn't get sent again
         * even if this queue request was received after the background
         * search already sent it.
         */
        if (block) {
            unsigned long page;

            page = offset >> TARGET_PAGE_BITS;
            dirty = test_bit(page, block->bmap);
            if (!dirty) {
                trace_get_queued_page_not_dirty(block->idstr, (uint64_t)offset,
                       page, test_bit(page, block->unsentmap));
            } else {
                trace_get_queued_page(block->idstr, (uint64_t)offset, page);
            }
        }

    } while (block && !dirty);

    if (block) {
        /*
         * As soon as we start servicing pages out of order, then we have
         * to kill the bulk stage, since the bulk stage assumes
         * in (migration_bitmap_find_and_reset_dirty) that every page is
         * dirty, that's no longer true.
         */
        rs->ram_bulk_stage = false;

        /*
         * We want the background search to continue from the queued page
         * since the guest is likely to want other pages near to the page
         * it just requested.
         */
        pss->block = block;
        pss->page = offset >> TARGET_PAGE_BITS;
    }

    return !!block;
}

/**
 * migration_page_queue_free: drop any remaining pages in the ram
 * request queue
 *
 * It should be empty at the end anyway, but in error cases there may
 * be some left.  in case that there is any page left, we drop it.
 *
 */
static void migration_page_queue_free(RAMState *rs)
{
    struct RAMSrcPageRequest *mspr, *next_mspr;
    /* This queue generally should be empty - but in the case of a failed
     * migration might have some droppings in.
     */
    rcu_read_lock();
    QSIMPLEQ_FOREACH_SAFE(mspr, &rs->src_page_requests, next_req, next_mspr) {
        memory_region_unref(mspr->rb->mr);
        QSIMPLEQ_REMOVE_HEAD(&rs->src_page_requests, next_req);
        g_free(mspr);
    }
    rcu_read_unlock();
}

/**
 * ram_save_queue_pages: queue the page for transmission
 *
 * A request from postcopy destination for example.
 *
 * Returns zero on success or negative on error
 *
 * @rbname: Name of the RAMBLock of the request. NULL means the
 *          same that last one.
 * @start: starting address from the start of the RAMBlock
 * @len: length (in bytes) to send
 */
int ram_save_queue_pages(const char *rbname, ram_addr_t start, ram_addr_t len)
{
    RAMBlock *ramblock;
    RAMState *rs = ram_state;

    ram_counters.postcopy_requests++;
    rcu_read_lock();
    if (!rbname) {
        /* Reuse last RAMBlock */
        ramblock = rs->last_req_rb;

        if (!ramblock) {
            /*
             * Shouldn't happen, we can't reuse the last RAMBlock if
             * it's the 1st request.
             */
            error_report("ram_save_queue_pages no previous block");
            goto err;
        }
    } else {
        ramblock = qemu_ram_block_by_name(rbname);

        if (!ramblock) {
            /* We shouldn't be asked for a non-existent RAMBlock */
            error_report("ram_save_queue_pages no block '%s'", rbname);
            goto err;
        }
        rs->last_req_rb = ramblock;
    }
    trace_ram_save_queue_pages(ramblock->idstr, start, len);
    if (start+len > ramblock->used_length) {
        error_report("%s request overrun start=" RAM_ADDR_FMT " len="
                     RAM_ADDR_FMT " blocklen=" RAM_ADDR_FMT,
                     __func__, start, len, ramblock->used_length);
        goto err;
    }

    struct RAMSrcPageRequest *new_entry =
        g_malloc0(sizeof(struct RAMSrcPageRequest));
    new_entry->rb = ramblock;
    new_entry->offset = start;
    new_entry->len = len;

    memory_region_ref(ramblock->mr);
    qemu_mutex_lock(&rs->src_page_req_mutex);
    QSIMPLEQ_INSERT_TAIL(&rs->src_page_requests, new_entry, next_req);
    qemu_mutex_unlock(&rs->src_page_req_mutex);
    rcu_read_unlock();

    return 0;

err:
    rcu_read_unlock();
    return -1;
}

/**
 * ram_save_target_page: save one target page
 *
 * Returns the number of pages written
 *
 * @rs: current RAM state
 * @ms: current migration state
 * @pss: data about the page we want to send
 * @last_stage: if we are at the completion stage
 */
static int ram_save_target_page(RAMState *rs, PageSearchStatus *pss,
                                bool last_stage)
{
    int res = 0;

    /* Check the pages is dirty and if it is send it */
    if (migration_bitmap_clear_dirty(rs, pss->block, pss->page)) {
        /*
         * If xbzrle is on, stop using the data compression after first
         * round of migration even if compression is enabled. In theory,
         * xbzrle can do better than compression.
         */
        if (migrate_use_compression() &&
            (rs->ram_bulk_stage || !migrate_use_xbzrle())) {
            res = ram_save_compressed_page(rs, pss, last_stage);
        } else {
            res = ram_save_page(rs, pss, last_stage);
        }

        if (res < 0) {
            return res;
        }
        if (pss->block->unsentmap) {
            clear_bit(pss->page, pss->block->unsentmap);
        }
    }

    return res;
}

/**
 * ram_save_host_page: save a whole host page
 *
 * Starting at *offset send pages up to the end of the current host
 * page. It's valid for the initial offset to point into the middle of
 * a host page in which case the remainder of the hostpage is sent.
 * Only dirty target pages are sent. Note that the host page size may
 * be a huge page for this block.
 * The saving stops at the boundary of the used_length of the block
 * if the RAMBlock isn't a multiple of the host page size.
 *
 * Returns the number of pages written or negative on error
 *
 * @rs: current RAM state
 * @ms: current migration state
 * @pss: data about the page we want to send
 * @last_stage: if we are at the completion stage
 */
static int ram_save_host_page(RAMState *rs, PageSearchStatus *pss,
                              bool last_stage)
{
    int tmppages, pages = 0;
    size_t pagesize_bits =
        qemu_ram_pagesize(pss->block) >> TARGET_PAGE_BITS;

    do {
        tmppages = ram_save_target_page(rs, pss, last_stage);
        if (tmppages < 0) {
            return tmppages;
        }

        pages += tmppages;
        pss->page++;
    } while ((pss->page & (pagesize_bits - 1)) &&
             offset_in_ramblock(pss->block, pss->page << TARGET_PAGE_BITS));

    /* The offset we leave with is the last one we looked at */
    pss->page--;
    return pages;
}

/**
 * ram_find_and_save_block: finds a dirty page and sends it to f
 *
 * Called within an RCU critical section.
 *
 * Returns the number of pages written where zero means no dirty pages
 *
 * @rs: current RAM state
 * @last_stage: if we are at the completion stage
 *
 * On systems where host-page-size > target-page-size it will send all the
 * pages in a host page that are dirty.
 */

static int ram_find_and_save_block(RAMState *rs, bool last_stage)
{
    PageSearchStatus pss;
    int pages = 0;
    bool again, found;

    /* No dirty page as there is zero RAM */
    if (!ram_bytes_total()) {
        return pages;
    }

    pss.block = rs->last_seen_block;
    pss.page = rs->last_page;
    pss.complete_round = false;

    if (!pss.block) {
        pss.block = QLIST_FIRST_RCU(&ram_list.blocks);
    }

    do {
        again = true;
        found = get_queued_page(rs, &pss);

        if (!found) {
            /* priority queue empty, so just search for something dirty */
            found = find_dirty_block(rs, &pss, &again);
        }

        if (found) {
            pages = ram_save_host_page(rs, &pss, last_stage);
        }
    } while (!pages && again);

    rs->last_seen_block = pss.block;
    rs->last_page = pss.page;

    return pages;
}

void acct_update_position(QEMUFile *f, size_t size, bool zero)
{
    uint64_t pages = size / TARGET_PAGE_SIZE;

    if (zero) {
        ram_counters.duplicate += pages;
    } else {
        ram_counters.normal += pages;
        ram_counters.transferred += size;
        qemu_update_position(f, size);
    }
}

uint64_t ram_bytes_total(void)
{
    RAMBlock *block;
    uint64_t total = 0;

    rcu_read_lock();
    RAMBLOCK_FOREACH(block) {
        total += block->used_length;
    }
    rcu_read_unlock();
    return total;
}

static void xbzrle_load_setup(void)
{
    XBZRLE.decoded_buf = g_malloc(TARGET_PAGE_SIZE);
}

static void xbzrle_load_cleanup(void)
{
    g_free(XBZRLE.decoded_buf);
    XBZRLE.decoded_buf = NULL;
}

static void ram_save_cleanup(void *opaque)
{
    RAMState **rsp = opaque;
    RAMBlock *block;

    /* caller have hold iothread lock or is in a bh, so there is
     * no writing race against this migration_bitmap
     */
    memory_global_dirty_log_stop();

    QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
        g_free(block->bmap);
        block->bmap = NULL;
        g_free(block->unsentmap);
        block->unsentmap = NULL;
    }

    XBZRLE_cache_lock();
    if (XBZRLE.cache) {
        cache_fini(XBZRLE.cache);
        g_free(XBZRLE.encoded_buf);
        g_free(XBZRLE.current_buf);
        g_free(XBZRLE.zero_target_page);
        XBZRLE.cache = NULL;
        XBZRLE.encoded_buf = NULL;
        XBZRLE.current_buf = NULL;
        XBZRLE.zero_target_page = NULL;
    }
    XBZRLE_cache_unlock();
    migration_page_queue_free(*rsp);
    compress_threads_save_cleanup();
    g_free(*rsp);
    *rsp = NULL;
}

static void ram_state_reset(RAMState *rs)
{
    rs->last_seen_block = NULL;
    rs->last_sent_block = NULL;
    rs->last_page = 0;
    rs->last_version = ram_list.version;
    rs->ram_bulk_stage = true;
}

#define MAX_WAIT 50 /* ms, half buffered_file limit */

/*
 * 'expected' is the value you expect the bitmap mostly to be full
 * of; it won't bother printing lines that are all this value.
 * If 'todump' is null the migration bitmap is dumped.
 */
void ram_debug_dump_bitmap(unsigned long *todump, bool expected,
                           unsigned long pages)
{
    int64_t cur;
    int64_t linelen = 128;
    char linebuf[129];

    for (cur = 0; cur < pages; cur += linelen) {
        int64_t curb;
        bool found = false;
        /*
         * Last line; catch the case where the line length
         * is longer than remaining ram
         */
        if (cur + linelen > pages) {
            linelen = pages - cur;
        }
        for (curb = 0; curb < linelen; curb++) {
            bool thisbit = test_bit(cur + curb, todump);
            linebuf[curb] = thisbit ? '1' : '.';
            found = found || (thisbit != expected);
        }
        if (found) {
            linebuf[curb] = '\0';
            fprintf(stderr,  "0x%08" PRIx64 " : %s\n", cur, linebuf);
        }
    }
}

/* **** functions for postcopy ***** */

void ram_postcopy_migrated_memory_release(MigrationState *ms)
{
    struct RAMBlock *block;

    RAMBLOCK_FOREACH(block) {
        unsigned long *bitmap = block->bmap;
        unsigned long range = block->used_length >> TARGET_PAGE_BITS;
        unsigned long run_start = find_next_zero_bit(bitmap, range, 0);

        while (run_start < range) {
            unsigned long run_end = find_next_bit(bitmap, range, run_start + 1);
            ram_discard_range(block->idstr, run_start << TARGET_PAGE_BITS,
                              (run_end - run_start) << TARGET_PAGE_BITS);
            run_start = find_next_zero_bit(bitmap, range, run_end + 1);
        }
    }
}

/**
 * postcopy_send_discard_bm_ram: discard a RAMBlock
 *
 * Returns zero on success
 *
 * Callback from postcopy_each_ram_send_discard for each RAMBlock
 * Note: At this point the 'unsentmap' is the processed bitmap combined
 *       with the dirtymap; so a '1' means it's either dirty or unsent.
 *
 * @ms: current migration state
 * @pds: state for postcopy
 * @start: RAMBlock starting page
 * @length: RAMBlock size
 */
static int postcopy_send_discard_bm_ram(MigrationState *ms,
                                        PostcopyDiscardState *pds,
                                        RAMBlock *block)
{
    unsigned long end = block->used_length >> TARGET_PAGE_BITS;
    unsigned long current;
    unsigned long *unsentmap = block->unsentmap;

    for (current = 0; current < end; ) {
        unsigned long one = find_next_bit(unsentmap, end, current);

        if (one <= end) {
            unsigned long zero = find_next_zero_bit(unsentmap, end, one + 1);
            unsigned long discard_length;

            if (zero >= end) {
                discard_length = end - one;
            } else {
                discard_length = zero - one;
            }
            if (discard_length) {
                postcopy_discard_send_range(ms, pds, one, discard_length);
            }
            current = one + discard_length;
        } else {
            current = one;
        }
    }

    return 0;
}

/**
 * postcopy_each_ram_send_discard: discard all RAMBlocks
 *
 * Returns 0 for success or negative for error
 *
 * Utility for the outgoing postcopy code.
 *   Calls postcopy_send_discard_bm_ram for each RAMBlock
 *   passing it bitmap indexes and name.
 * (qemu_ram_foreach_block ends up passing unscaled lengths
 *  which would mean postcopy code would have to deal with target page)
 *
 * @ms: current migration state
 */
static int postcopy_each_ram_send_discard(MigrationState *ms)
{
    struct RAMBlock *block;
    int ret;

    RAMBLOCK_FOREACH(block) {
        PostcopyDiscardState *pds =
            postcopy_discard_send_init(ms, block->idstr);

        /*
         * Postcopy sends chunks of bitmap over the wire, but it
         * just needs indexes at this point, avoids it having
         * target page specific code.
         */
        ret = postcopy_send_discard_bm_ram(ms, pds, block);
        postcopy_discard_send_finish(ms, pds);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

/**
 * postcopy_chunk_hostpages_pass: canocalize bitmap in hostpages
 *
 * Helper for postcopy_chunk_hostpages; it's called twice to
 * canonicalize the two bitmaps, that are similar, but one is
 * inverted.
 *
 * Postcopy requires that all target pages in a hostpage are dirty or
 * clean, not a mix.  This function canonicalizes the bitmaps.
 *
 * @ms: current migration state
 * @unsent_pass: if true we need to canonicalize partially unsent host pages
 *               otherwise we need to canonicalize partially dirty host pages
 * @block: block that contains the page we want to canonicalize
 * @pds: state for postcopy
 */
static void postcopy_chunk_hostpages_pass(MigrationState *ms, bool unsent_pass,
                                          RAMBlock *block,
                                          PostcopyDiscardState *pds)
{
    RAMState *rs = ram_state;
    unsigned long *bitmap = block->bmap;
    unsigned long *unsentmap = block->unsentmap;
    unsigned int host_ratio = block->page_size / TARGET_PAGE_SIZE;
    unsigned long pages = block->used_length >> TARGET_PAGE_BITS;
    unsigned long run_start;

    if (block->page_size == TARGET_PAGE_SIZE) {
        /* Easy case - TPS==HPS for a non-huge page RAMBlock */
        return;
    }

    if (unsent_pass) {
        /* Find a sent page */
        run_start = find_next_zero_bit(unsentmap, pages, 0);
    } else {
        /* Find a dirty page */
        run_start = find_next_bit(bitmap, pages, 0);
    }

    while (run_start < pages) {
        bool do_fixup = false;
        unsigned long fixup_start_addr;
        unsigned long host_offset;

        /*
         * If the start of this run of pages is in the middle of a host
         * page, then we need to fixup this host page.
         */
        host_offset = run_start % host_ratio;
        if (host_offset) {
            do_fixup = true;
            run_start -= host_offset;
            fixup_start_addr = run_start;
            /* For the next pass */
            run_start = run_start + host_ratio;
        } else {
            /* Find the end of this run */
            unsigned long run_end;
            if (unsent_pass) {
                run_end = find_next_bit(unsentmap, pages, run_start + 1);
            } else {
                run_end = find_next_zero_bit(bitmap, pages, run_start + 1);
            }
            /*
             * If the end isn't at the start of a host page, then the
             * run doesn't finish at the end of a host page
             * and we need to discard.
             */
            host_offset = run_end % host_ratio;
            if (host_offset) {
                do_fixup = true;
                fixup_start_addr = run_end - host_offset;
                /*
                 * This host page has gone, the next loop iteration starts
                 * from after the fixup
                 */
                run_start = fixup_start_addr + host_ratio;
            } else {
                /*
                 * No discards on this iteration, next loop starts from
                 * next sent/dirty page
                 */
                run_start = run_end + 1;
            }
        }

        if (do_fixup) {
            unsigned long page;

            /* Tell the destination to discard this page */
            if (unsent_pass || !test_bit(fixup_start_addr, unsentmap)) {
                /* For the unsent_pass we:
                 *     discard partially sent pages
                 * For the !unsent_pass (dirty) we:
                 *     discard partially dirty pages that were sent
                 *     (any partially sent pages were already discarded
                 *     by the previous unsent_pass)
                 */
                postcopy_discard_send_range(ms, pds, fixup_start_addr,
                                            host_ratio);
            }

            /* Clean up the bitmap */
            for (page = fixup_start_addr;
                 page < fixup_start_addr + host_ratio; page++) {
                /* All pages in this host page are now not sent */
                set_bit(page, unsentmap);

                /*
                 * Remark them as dirty, updating the count for any pages
                 * that weren't previously dirty.
                 */
                rs->migration_dirty_pages += !test_and_set_bit(page, bitmap);
            }
        }

        if (unsent_pass) {
            /* Find the next sent page for the next iteration */
            run_start = find_next_zero_bit(unsentmap, pages, run_start);
        } else {
            /* Find the next dirty page for the next iteration */
            run_start = find_next_bit(bitmap, pages, run_start);
        }
    }
}

/**
 * postcopy_chuck_hostpages: discrad any partially sent host page
 *
 * Utility for the outgoing postcopy code.
 *
 * Discard any partially sent host-page size chunks, mark any partially
 * dirty host-page size chunks as all dirty.  In this case the host-page
 * is the host-page for the particular RAMBlock, i.e. it might be a huge page
 *
 * Returns zero on success
 *
 * @ms: current migration state
 * @block: block we want to work with
 */
static int postcopy_chunk_hostpages(MigrationState *ms, RAMBlock *block)
{
    PostcopyDiscardState *pds =
        postcopy_discard_send_init(ms, block->idstr);

    /* First pass: Discard all partially sent host pages */
    postcopy_chunk_hostpages_pass(ms, true, block, pds);
    /*
     * Second pass: Ensure that all partially dirty host pages are made
     * fully dirty.
     */
    postcopy_chunk_hostpages_pass(ms, false, block, pds);

    postcopy_discard_send_finish(ms, pds);
    return 0;
}

/**
 * ram_postcopy_send_discard_bitmap: transmit the discard bitmap
 *
 * Returns zero on success
 *
 * Transmit the set of pages to be discarded after precopy to the target
 * these are pages that:
 *     a) Have been previously transmitted but are now dirty again
 *     b) Pages that have never been transmitted, this ensures that
 *        any pages on the destination that have been mapped by background
 *        tasks get discarded (transparent huge pages is the specific concern)
 * Hopefully this is pretty sparse
 *
 * @ms: current migration state
 */
int ram_postcopy_send_discard_bitmap(MigrationState *ms)
{
    RAMState *rs = ram_state;
    RAMBlock *block;
    int ret;

    rcu_read_lock();

    /* This should be our last sync, the src is now paused */
    migration_bitmap_sync(rs);

    /* Easiest way to make sure we don't resume in the middle of a host-page */
    rs->last_seen_block = NULL;
    rs->last_sent_block = NULL;
    rs->last_page = 0;

    QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
        unsigned long pages = block->used_length >> TARGET_PAGE_BITS;
        unsigned long *bitmap = block->bmap;
        unsigned long *unsentmap = block->unsentmap;

        if (!unsentmap) {
            /* We don't have a safe way to resize the sentmap, so
             * if the bitmap was resized it will be NULL at this
             * point.
             */
            error_report("migration ram resized during precopy phase");
            rcu_read_unlock();
            return -EINVAL;
        }
        /* Deal with TPS != HPS and huge pages */
        ret = postcopy_chunk_hostpages(ms, block);
        if (ret) {
            rcu_read_unlock();
            return ret;
        }

        /*
         * Update the unsentmap to be unsentmap = unsentmap | dirty
         */
        bitmap_or(unsentmap, unsentmap, bitmap, pages);
#ifdef DEBUG_POSTCOPY
        ram_debug_dump_bitmap(unsentmap, true, pages);
#endif
    }
    trace_ram_postcopy_send_discard_bitmap();

    ret = postcopy_each_ram_send_discard(ms);
    rcu_read_unlock();

    return ret;
}

/**
 * ram_discard_range: discard dirtied pages at the beginning of postcopy
 *
 * Returns zero on success
 *
 * @rbname: name of the RAMBlock of the request. NULL means the
 *          same that last one.
 * @start: RAMBlock starting page
 * @length: RAMBlock size
 */
int ram_discard_range(const char *rbname, uint64_t start, size_t length)
{
    int ret = -1;

    trace_ram_discard_range(rbname, start, length);

    rcu_read_lock();
    RAMBlock *rb = qemu_ram_block_by_name(rbname);

    if (!rb) {
        error_report("ram_discard_range: Failed to find block '%s'", rbname);
        goto err;
    }

    ret = ram_block_discard_range(rb, start, length);

err:
    rcu_read_unlock();

    return ret;
}

static int ram_state_init(RAMState **rsp)
{
    *rsp = g_new0(RAMState, 1);

    qemu_mutex_init(&(*rsp)->bitmap_mutex);
    qemu_mutex_init(&(*rsp)->src_page_req_mutex);
    QSIMPLEQ_INIT(&(*rsp)->src_page_requests);

    if (migrate_use_xbzrle()) {
        XBZRLE_cache_lock();
        XBZRLE.zero_target_page = g_malloc0(TARGET_PAGE_SIZE);
        XBZRLE.cache = cache_init(migrate_xbzrle_cache_size() /
                                  TARGET_PAGE_SIZE,
                                  TARGET_PAGE_SIZE);
        if (!XBZRLE.cache) {
            XBZRLE_cache_unlock();
            error_report("Error creating cache");
            g_free(*rsp);
            *rsp = NULL;
            return -1;
        }
        XBZRLE_cache_unlock();

        /* We prefer not to abort if there is no memory */
        XBZRLE.encoded_buf = g_try_malloc0(TARGET_PAGE_SIZE);
        if (!XBZRLE.encoded_buf) {
            error_report("Error allocating encoded_buf");
            g_free(*rsp);
            *rsp = NULL;
            return -1;
        }

        XBZRLE.current_buf = g_try_malloc(TARGET_PAGE_SIZE);
        if (!XBZRLE.current_buf) {
            error_report("Error allocating current_buf");
            g_free(XBZRLE.encoded_buf);
            XBZRLE.encoded_buf = NULL;
            g_free(*rsp);
            *rsp = NULL;
            return -1;
        }
    }

    /* For memory_global_dirty_log_start below.  */
    qemu_mutex_lock_iothread();

    qemu_mutex_lock_ramlist();
    rcu_read_lock();
    ram_state_reset(*rsp);

    /* Skip setting bitmap if there is no RAM */
    if (ram_bytes_total()) {
        RAMBlock *block;

        QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
            unsigned long pages = block->max_length >> TARGET_PAGE_BITS;

            block->bmap = bitmap_new(pages);
            bitmap_set(block->bmap, 0, pages);
            if (migrate_postcopy_ram()) {
                block->unsentmap = bitmap_new(pages);
                bitmap_set(block->unsentmap, 0, pages);
            }
        }
    }

    /*
     * Count the total number of pages used by ram blocks not including any
     * gaps due to alignment or unplugs.
     */
    (*rsp)->migration_dirty_pages = ram_bytes_total() >> TARGET_PAGE_BITS;

    memory_global_dirty_log_start();
    migration_bitmap_sync(*rsp);
    qemu_mutex_unlock_ramlist();
    qemu_mutex_unlock_iothread();
    rcu_read_unlock();

    return 0;
}

/*
 * Each of ram_save_setup, ram_save_iterate and ram_save_complete has
 * long-running RCU critical section.  When rcu-reclaims in the code
 * start to become numerous it will be necessary to reduce the
 * granularity of these critical sections.
 */

/**
 * ram_save_setup: Setup RAM for migration
 *
 * Returns zero to indicate success and negative for error
 *
 * @f: QEMUFile where to send the data
 * @opaque: RAMState pointer
 */
static int ram_save_setup(QEMUFile *f, void *opaque)
{
    RAMState **rsp = opaque;
    RAMBlock *block;

    /* migration has already setup the bitmap, reuse it. */
    if (!migration_in_colo_state()) {
        if (ram_state_init(rsp) != 0) {
            return -1;
        }
    }
    (*rsp)->f = f;

    rcu_read_lock();

    qemu_put_be64(f, ram_bytes_total() | RAM_SAVE_FLAG_MEM_SIZE);

    RAMBLOCK_FOREACH(block) {
        qemu_put_byte(f, strlen(block->idstr));
        qemu_put_buffer(f, (uint8_t *)block->idstr, strlen(block->idstr));
        qemu_put_be64(f, block->used_length);
        if (migrate_postcopy_ram() && block->page_size != qemu_host_page_size) {
            qemu_put_be64(f, block->page_size);
        }
    }

    rcu_read_unlock();
    compress_threads_save_setup();

    ram_control_before_iterate(f, RAM_CONTROL_SETUP);
    ram_control_after_iterate(f, RAM_CONTROL_SETUP);

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    return 0;
}

/**
 * ram_save_iterate: iterative stage for migration
 *
 * Returns zero to indicate success and negative for error
 *
 * @f: QEMUFile where to send the data
 * @opaque: RAMState pointer
 */
static int ram_save_iterate(QEMUFile *f, void *opaque)
{
    RAMState **temp = opaque;
    RAMState *rs = *temp;
    int ret;
    int i;
    int64_t t0;
    int done = 0;

    rcu_read_lock();
    if (ram_list.version != rs->last_version) {
        ram_state_reset(rs);
    }

    /* Read version before ram_list.blocks */
    smp_rmb();

    ram_control_before_iterate(f, RAM_CONTROL_ROUND);

    t0 = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    i = 0;
    while ((ret = qemu_file_rate_limit(f)) == 0) {
        int pages;

        pages = ram_find_and_save_block(rs, false);
        /* no more pages to sent */
        if (pages == 0) {
            done = 1;
            break;
        }
        rs->iterations++;

        /* we want to check in the 1st loop, just in case it was the 1st time
           and we had to sync the dirty bitmap.
           qemu_get_clock_ns() is a bit expensive, so we only check each some
           iterations
        */
        if ((i & 63) == 0) {
            uint64_t t1 = (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - t0) / 1000000;
            if (t1 > MAX_WAIT) {
                trace_ram_save_iterate_big_wait(t1, i);
                break;
            }
        }
        i++;
    }
    flush_compressed_data(rs);
    rcu_read_unlock();

    /*
     * Must occur before EOS (or any QEMUFile operation)
     * because of RDMA protocol.
     */
    ram_control_after_iterate(f, RAM_CONTROL_ROUND);

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    ram_counters.transferred += 8;

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        return ret;
    }

    return done;
}

/**
 * ram_save_complete: function called to send the remaining amount of ram
 *
 * Returns zero to indicate success
 *
 * Called with iothread lock
 *
 * @f: QEMUFile where to send the data
 * @opaque: RAMState pointer
 */
static int ram_save_complete(QEMUFile *f, void *opaque)
{
    RAMState **temp = opaque;
    RAMState *rs = *temp;

    rcu_read_lock();

    if (!migration_in_postcopy()) {
        migration_bitmap_sync(rs);
    }

    ram_control_before_iterate(f, RAM_CONTROL_FINISH);

    /* try transferring iterative blocks of memory */

    /* flush all remaining blocks regardless of rate limiting */
    while (true) {
        int pages;

        pages = ram_find_and_save_block(rs, !migration_in_colo_state());
        /* no more blocks to sent */
        if (pages == 0) {
            break;
        }
    }

    flush_compressed_data(rs);
    ram_control_after_iterate(f, RAM_CONTROL_FINISH);

    rcu_read_unlock();

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    return 0;
}

static void ram_save_pending(QEMUFile *f, void *opaque, uint64_t max_size,
                             uint64_t *non_postcopiable_pending,
                             uint64_t *postcopiable_pending)
{
    RAMState **temp = opaque;
    RAMState *rs = *temp;
    uint64_t remaining_size;

    remaining_size = rs->migration_dirty_pages * TARGET_PAGE_SIZE;

    if (!migration_in_postcopy() &&
        remaining_size < max_size) {
        qemu_mutex_lock_iothread();
        rcu_read_lock();
        migration_bitmap_sync(rs);
        rcu_read_unlock();
        qemu_mutex_unlock_iothread();
        remaining_size = rs->migration_dirty_pages * TARGET_PAGE_SIZE;
    }

    /* We can do postcopy, and all the data is postcopiable */
    *postcopiable_pending += remaining_size;
}

static int load_xbzrle(QEMUFile *f, ram_addr_t addr, void *host)
{
    unsigned int xh_len;
    int xh_flags;
    uint8_t *loaded_data;

    /* extract RLE header */
    xh_flags = qemu_get_byte(f);
    xh_len = qemu_get_be16(f);

    if (xh_flags != ENCODING_FLAG_XBZRLE) {
        error_report("Failed to load XBZRLE page - wrong compression!");
        return -1;
    }

    if (xh_len > TARGET_PAGE_SIZE) {
        error_report("Failed to load XBZRLE page - len overflow!");
        return -1;
    }
    loaded_data = XBZRLE.decoded_buf;
    /* load data and decode */
    /* it can change loaded_data to point to an internal buffer */
    qemu_get_buffer_in_place(f, &loaded_data, xh_len);

    /* decode RLE */
    if (xbzrle_decode_buffer(loaded_data, xh_len, host,
                             TARGET_PAGE_SIZE) == -1) {
        error_report("Failed to load XBZRLE page - decode error!");
        return -1;
    }

    return 0;
}

/**
 * ram_block_from_stream: read a RAMBlock id from the migration stream
 *
 * Must be called from within a rcu critical section.
 *
 * Returns a pointer from within the RCU-protected ram_list.
 *
 * @f: QEMUFile where to read the data from
 * @flags: Page flags (mostly to see if it's a continuation of previous block)
 */
static inline RAMBlock *ram_block_from_stream(QEMUFile *f, int flags)
{
    static RAMBlock *block = NULL;
    char id[256];
    uint8_t len;

    if (flags & RAM_SAVE_FLAG_CONTINUE) {
        if (!block) {
            error_report("Ack, bad migration stream!");
            return NULL;
        }
        return block;
    }

    len = qemu_get_byte(f);
    qemu_get_buffer(f, (uint8_t *)id, len);
    id[len] = 0;

    block = qemu_ram_block_by_name(id);
    if (!block) {
        error_report("Can't find block %s", id);
        return NULL;
    }

    return block;
}

static inline void *host_from_ram_block_offset(RAMBlock *block,
                                               ram_addr_t offset)
{
    if (!offset_in_ramblock(block, offset)) {
        return NULL;
    }

    return block->host + offset;
}

/**
 * ram_handle_compressed: handle the zero page case
 *
 * If a page (or a whole RDMA chunk) has been
 * determined to be zero, then zap it.
 *
 * @host: host address for the zero page
 * @ch: what the page is filled from.  We only support zero
 * @size: size of the zero page
 */
void ram_handle_compressed(void *host, uint8_t ch, uint64_t size)
{
    if (ch != 0 || !is_zero_range(host, size)) {
        memset(host, ch, size);
    }
}

static void *do_data_decompress(void *opaque)
{
    DecompressParam *param = opaque;
    unsigned long pagesize;
    uint8_t *des;
    int len;

    qemu_mutex_lock(&param->mutex);
    while (!param->quit) {
        if (param->des) {
            des = param->des;
            len = param->len;
            param->des = 0;
            qemu_mutex_unlock(&param->mutex);

            pagesize = TARGET_PAGE_SIZE;
            /* uncompress() will return failed in some case, especially
             * when the page is dirted when doing the compression, it's
             * not a problem because the dirty page will be retransferred
             * and uncompress() won't break the data in other pages.
             */
            uncompress((Bytef *)des, &pagesize,
                       (const Bytef *)param->compbuf, len);

            qemu_mutex_lock(&decomp_done_lock);
            param->done = true;
            qemu_cond_signal(&decomp_done_cond);
            qemu_mutex_unlock(&decomp_done_lock);

            qemu_mutex_lock(&param->mutex);
        } else {
            qemu_cond_wait(&param->cond, &param->mutex);
        }
    }
    qemu_mutex_unlock(&param->mutex);

    return NULL;
}

static void wait_for_decompress_done(void)
{
    int idx, thread_count;

    if (!migrate_use_compression()) {
        return;
    }

    thread_count = migrate_decompress_threads();
    qemu_mutex_lock(&decomp_done_lock);
    for (idx = 0; idx < thread_count; idx++) {
        while (!decomp_param[idx].done) {
            qemu_cond_wait(&decomp_done_cond, &decomp_done_lock);
        }
    }
    qemu_mutex_unlock(&decomp_done_lock);
}

static void compress_threads_load_setup(void)
{
    int i, thread_count;

    if (!migrate_use_compression()) {
        return;
    }
    thread_count = migrate_decompress_threads();
    decompress_threads = g_new0(QemuThread, thread_count);
    decomp_param = g_new0(DecompressParam, thread_count);
    qemu_mutex_init(&decomp_done_lock);
    qemu_cond_init(&decomp_done_cond);
    for (i = 0; i < thread_count; i++) {
        qemu_mutex_init(&decomp_param[i].mutex);
        qemu_cond_init(&decomp_param[i].cond);
        decomp_param[i].compbuf = g_malloc0(compressBound(TARGET_PAGE_SIZE));
        decomp_param[i].done = true;
        decomp_param[i].quit = false;
        qemu_thread_create(decompress_threads + i, "decompress",
                           do_data_decompress, decomp_param + i,
                           QEMU_THREAD_JOINABLE);
    }
}

static void compress_threads_load_cleanup(void)
{
    int i, thread_count;

    if (!migrate_use_compression()) {
        return;
    }
    thread_count = migrate_decompress_threads();
    for (i = 0; i < thread_count; i++) {
        qemu_mutex_lock(&decomp_param[i].mutex);
        decomp_param[i].quit = true;
        qemu_cond_signal(&decomp_param[i].cond);
        qemu_mutex_unlock(&decomp_param[i].mutex);
    }
    for (i = 0; i < thread_count; i++) {
        qemu_thread_join(decompress_threads + i);
        qemu_mutex_destroy(&decomp_param[i].mutex);
        qemu_cond_destroy(&decomp_param[i].cond);
        g_free(decomp_param[i].compbuf);
    }
    g_free(decompress_threads);
    g_free(decomp_param);
    decompress_threads = NULL;
    decomp_param = NULL;
}

static void decompress_data_with_multi_threads(QEMUFile *f,
                                               void *host, int len)
{
    int idx, thread_count;

    thread_count = migrate_decompress_threads();
    qemu_mutex_lock(&decomp_done_lock);
    while (true) {
        for (idx = 0; idx < thread_count; idx++) {
            if (decomp_param[idx].done) {
                decomp_param[idx].done = false;
                qemu_mutex_lock(&decomp_param[idx].mutex);
                qemu_get_buffer(f, decomp_param[idx].compbuf, len);
                decomp_param[idx].des = host;
                decomp_param[idx].len = len;
                qemu_cond_signal(&decomp_param[idx].cond);
                qemu_mutex_unlock(&decomp_param[idx].mutex);
                break;
            }
        }
        if (idx < thread_count) {
            break;
        } else {
            qemu_cond_wait(&decomp_done_cond, &decomp_done_lock);
        }
    }
    qemu_mutex_unlock(&decomp_done_lock);
}

/**
 * ram_load_setup: Setup RAM for migration incoming side
 *
 * Returns zero to indicate success and negative for error
 *
 * @f: QEMUFile where to receive the data
 * @opaque: RAMState pointer
 */
static int ram_load_setup(QEMUFile *f, void *opaque)
{
    xbzrle_load_setup();
    compress_threads_load_setup();
    return 0;
}

static int ram_load_cleanup(void *opaque)
{
    xbzrle_load_cleanup();
    compress_threads_load_cleanup();
    return 0;
}

/**
 * ram_postcopy_incoming_init: allocate postcopy data structures
 *
 * Returns 0 for success and negative if there was one error
 *
 * @mis: current migration incoming state
 *
 * Allocate data structures etc needed by incoming migration with
 * postcopy-ram. postcopy-ram's similarly names
 * postcopy_ram_incoming_init does the work.
 */
int ram_postcopy_incoming_init(MigrationIncomingState *mis)
{
    unsigned long ram_pages = last_ram_page();

    return postcopy_ram_incoming_init(mis, ram_pages);
}

/**
 * ram_load_postcopy: load a page in postcopy case
 *
 * Returns 0 for success or -errno in case of error
 *
 * Called in postcopy mode by ram_load().
 * rcu_read_lock is taken prior to this being called.
 *
 * @f: QEMUFile where to send the data
 */
static int ram_load_postcopy(QEMUFile *f)
{
    int flags = 0, ret = 0;
    bool place_needed = false;
    bool matching_page_sizes = false;
    MigrationIncomingState *mis = migration_incoming_get_current();
    /* Temporary page that is later 'placed' */
    void *postcopy_host_page = postcopy_get_tmp_page(mis);
    void *last_host = NULL;
    bool all_zero = false;

    while (!ret && !(flags & RAM_SAVE_FLAG_EOS)) {
        ram_addr_t addr;
        void *host = NULL;
        void *page_buffer = NULL;
        void *place_source = NULL;
        RAMBlock *block = NULL;
        uint8_t ch;

        addr = qemu_get_be64(f);
        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        trace_ram_load_postcopy_loop((uint64_t)addr, flags);
        place_needed = false;
        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE)) {
            block = ram_block_from_stream(f, flags);

            host = host_from_ram_block_offset(block, addr);
            if (!host) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            matching_page_sizes = block->page_size == TARGET_PAGE_SIZE;
            /*
             * Postcopy requires that we place whole host pages atomically;
             * these may be huge pages for RAMBlocks that are backed by
             * hugetlbfs.
             * To make it atomic, the data is read into a temporary page
             * that's moved into place later.
             * The migration protocol uses,  possibly smaller, target-pages
             * however the source ensures it always sends all the components
             * of a host page in order.
             */
            page_buffer = postcopy_host_page +
                          ((uintptr_t)host & (block->page_size - 1));
            /* If all TP are zero then we can optimise the place */
            if (!((uintptr_t)host & (block->page_size - 1))) {
                all_zero = true;
            } else {
                /* not the 1st TP within the HP */
                if (host != (last_host + TARGET_PAGE_SIZE)) {
                    error_report("Non-sequential target page %p/%p",
                                  host, last_host);
                    ret = -EINVAL;
                    break;
                }
            }


            /*
             * If it's the last part of a host page then we place the host
             * page
             */
            place_needed = (((uintptr_t)host + TARGET_PAGE_SIZE) &
                                     (block->page_size - 1)) == 0;
            place_source = postcopy_host_page;
        }
        last_host = host;

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_ZERO:
            ch = qemu_get_byte(f);
            memset(page_buffer, ch, TARGET_PAGE_SIZE);
            if (ch) {
                all_zero = false;
            }
            break;

        case RAM_SAVE_FLAG_PAGE:
            all_zero = false;
            if (!place_needed || !matching_page_sizes) {
                qemu_get_buffer(f, page_buffer, TARGET_PAGE_SIZE);
            } else {
                /* Avoids the qemu_file copy during postcopy, which is
                 * going to do a copy later; can only do it when we
                 * do this read in one go (matching page sizes)
                 */
                qemu_get_buffer_in_place(f, (uint8_t **)&place_source,
                                         TARGET_PAGE_SIZE);
            }
            break;
        case RAM_SAVE_FLAG_EOS:
            /* normal exit */
            break;
        default:
            error_report("Unknown combination of migration flags: %#x"
                         " (postcopy mode)", flags);
            ret = -EINVAL;
        }

        if (place_needed) {
            /* This gets called at the last target page in the host page */
            void *place_dest = host + TARGET_PAGE_SIZE - block->page_size;

            if (all_zero) {
                ret = postcopy_place_page_zero(mis, place_dest,
                                               block->page_size);
            } else {
                ret = postcopy_place_page(mis, place_dest,
                                          place_source, block->page_size);
            }
        }
        if (!ret) {
            ret = qemu_file_get_error(f);
        }
    }

    return ret;
}

static int ram_load(QEMUFile *f, void *opaque, int version_id)
{
    int flags = 0, ret = 0, invalid_flags = 0;
    static uint64_t seq_iter;
    int len = 0;
    /*
     * If system is running in postcopy mode, page inserts to host memory must
     * be atomic
     */
    bool postcopy_running = postcopy_state_get() >= POSTCOPY_INCOMING_LISTENING;
    /* ADVISE is earlier, it shows the source has the postcopy capability on */
    bool postcopy_advised = postcopy_state_get() >= POSTCOPY_INCOMING_ADVISE;

    seq_iter++;

    if (version_id != 4) {
        ret = -EINVAL;
    }

    if (!migrate_use_compression()) {
        invalid_flags |= RAM_SAVE_FLAG_COMPRESS_PAGE;
    }
    /* This RCU critical section can be very long running.
     * When RCU reclaims in the code start to become numerous,
     * it will be necessary to reduce the granularity of this
     * critical section.
     */
    rcu_read_lock();

    if (postcopy_running) {
        ret = ram_load_postcopy(f);
    }

    while (!postcopy_running && !ret && !(flags & RAM_SAVE_FLAG_EOS)) {
        ram_addr_t addr, total_ram_bytes;
        void *host = NULL;
        uint8_t ch;

        addr = qemu_get_be64(f);
        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        if (flags & invalid_flags) {
            if (flags & invalid_flags & RAM_SAVE_FLAG_COMPRESS_PAGE) {
                error_report("Received an unexpected compressed page");
            }

            ret = -EINVAL;
            break;
        }

        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE |
                     RAM_SAVE_FLAG_COMPRESS_PAGE | RAM_SAVE_FLAG_XBZRLE)) {
            RAMBlock *block = ram_block_from_stream(f, flags);

            host = host_from_ram_block_offset(block, addr);
            if (!host) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            trace_ram_load_loop(block->idstr, (uint64_t)addr, flags, host);
        }

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_MEM_SIZE:
            /* Synchronize RAM block list */
            total_ram_bytes = addr;
            while (!ret && total_ram_bytes) {
                RAMBlock *block;
                char id[256];
                ram_addr_t length;

                len = qemu_get_byte(f);
                qemu_get_buffer(f, (uint8_t *)id, len);
                id[len] = 0;
                length = qemu_get_be64(f);

                block = qemu_ram_block_by_name(id);
                if (block) {
                    if (length != block->used_length) {
                        Error *local_err = NULL;

                        ret = qemu_ram_resize(block, length,
                                              &local_err);
                        if (local_err) {
                            error_report_err(local_err);
                        }
                    }
                    /* For postcopy we need to check hugepage sizes match */
                    if (postcopy_advised &&
                        block->page_size != qemu_host_page_size) {
                        uint64_t remote_page_size = qemu_get_be64(f);
                        if (remote_page_size != block->page_size) {
                            error_report("Mismatched RAM page size %s "
                                         "(local) %zd != %" PRId64,
                                         id, block->page_size,
                                         remote_page_size);
                            ret = -EINVAL;
                        }
                    }
                    ram_control_load_hook(f, RAM_CONTROL_BLOCK_REG,
                                          block->idstr);
                } else {
                    error_report("Unknown ramblock \"%s\", cannot "
                                 "accept migration", id);
                    ret = -EINVAL;
                }

                total_ram_bytes -= length;
            }
            break;

        case RAM_SAVE_FLAG_ZERO:
            ch = qemu_get_byte(f);
            ram_handle_compressed(host, ch, TARGET_PAGE_SIZE);
            break;

        case RAM_SAVE_FLAG_PAGE:
            qemu_get_buffer(f, host, TARGET_PAGE_SIZE);
            break;

        case RAM_SAVE_FLAG_COMPRESS_PAGE:
            len = qemu_get_be32(f);
            if (len < 0 || len > compressBound(TARGET_PAGE_SIZE)) {
                error_report("Invalid compressed data length: %d", len);
                ret = -EINVAL;
                break;
            }
            decompress_data_with_multi_threads(f, host, len);
            break;

        case RAM_SAVE_FLAG_XBZRLE:
            if (load_xbzrle(f, addr, host) < 0) {
                error_report("Failed to decompress XBZRLE page at "
                             RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            break;
        case RAM_SAVE_FLAG_EOS:
            /* normal exit */
            break;
        default:
            if (flags & RAM_SAVE_FLAG_HOOK) {
                ram_control_load_hook(f, RAM_CONTROL_HOOK, NULL);
            } else {
                error_report("Unknown combination of migration flags: %#x",
                             flags);
                ret = -EINVAL;
            }
        }
        if (!ret) {
            ret = qemu_file_get_error(f);
        }
    }

    wait_for_decompress_done();
    rcu_read_unlock();
    trace_ram_load_complete(ret, seq_iter);
    return ret;
}

static SaveVMHandlers savevm_ram_handlers = {
    .save_setup = ram_save_setup,
    .save_live_iterate = ram_save_iterate,
    .save_live_complete_postcopy = ram_save_complete,
    .save_live_complete_precopy = ram_save_complete,
    .save_live_pending = ram_save_pending,
    .load_state = ram_load,
    .save_cleanup = ram_save_cleanup,
    .load_setup = ram_load_setup,
    .load_cleanup = ram_load_cleanup,
};

void ram_mig_init(void)
{
    qemu_mutex_init(&XBZRLE.lock);
    register_savevm_live(NULL, "ram", 0, 4, &savevm_ram_handlers, &ram_state);
}
