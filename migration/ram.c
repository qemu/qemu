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
#include "qemu-common.h"
#include "cpu.h"
#include <zlib.h>
#include "qapi-event.h"
#include "qemu/cutils.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "migration/migration.h"
#include "migration/postcopy-ram.h"
#include "exec/address-spaces.h"
#include "migration/page_cache.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "exec/ram_addr.h"
#include "qemu/rcu_queue.h"

#ifdef DEBUG_MIGRATION_RAM
#define DPRINTF(fmt, ...) \
    do { fprintf(stdout, "migration_ram: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static int dirty_rate_high_cnt;

static uint64_t bitmap_sync_count;

/***********************************************************/
/* ram save/restore */

#define RAM_SAVE_FLAG_FULL     0x01 /* Obsolete, not used anymore */
#define RAM_SAVE_FLAG_COMPRESS 0x02
#define RAM_SAVE_FLAG_MEM_SIZE 0x04
#define RAM_SAVE_FLAG_PAGE     0x08
#define RAM_SAVE_FLAG_EOS      0x10
#define RAM_SAVE_FLAG_CONTINUE 0x20
#define RAM_SAVE_FLAG_XBZRLE   0x40
/* 0x80 is reserved in migration.h start with 0x100 next */
#define RAM_SAVE_FLAG_COMPRESS_PAGE    0x100

static const uint8_t ZERO_TARGET_PAGE[TARGET_PAGE_SIZE];

static inline bool is_zero_range(uint8_t *p, uint64_t size)
{
    return buffer_find_nonzero_offset(p, size) == size;
}

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
} XBZRLE;

/* buffer used for XBZRLE decoding */
static uint8_t *xbzrle_decoded_buf;

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

/*
 * called from qmp_migrate_set_cache_size in main thread, possibly while
 * a migration is in progress.
 * A running migration maybe using the cache and might finish during this
 * call, hence changes to the cache are protected by XBZRLE.lock().
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

/* accounting for migration statistics */
typedef struct AccountingInfo {
    uint64_t dup_pages;
    uint64_t skipped_pages;
    uint64_t norm_pages;
    uint64_t iterations;
    uint64_t xbzrle_bytes;
    uint64_t xbzrle_pages;
    uint64_t xbzrle_cache_miss;
    double xbzrle_cache_miss_rate;
    uint64_t xbzrle_overflows;
} AccountingInfo;

static AccountingInfo acct_info;

static void acct_clear(void)
{
    memset(&acct_info, 0, sizeof(acct_info));
}

uint64_t dup_mig_bytes_transferred(void)
{
    return acct_info.dup_pages * TARGET_PAGE_SIZE;
}

uint64_t dup_mig_pages_transferred(void)
{
    return acct_info.dup_pages;
}

uint64_t skipped_mig_bytes_transferred(void)
{
    return acct_info.skipped_pages * TARGET_PAGE_SIZE;
}

uint64_t skipped_mig_pages_transferred(void)
{
    return acct_info.skipped_pages;
}

uint64_t norm_mig_bytes_transferred(void)
{
    return acct_info.norm_pages * TARGET_PAGE_SIZE;
}

uint64_t norm_mig_pages_transferred(void)
{
    return acct_info.norm_pages;
}

uint64_t xbzrle_mig_bytes_transferred(void)
{
    return acct_info.xbzrle_bytes;
}

uint64_t xbzrle_mig_pages_transferred(void)
{
    return acct_info.xbzrle_pages;
}

uint64_t xbzrle_mig_pages_cache_miss(void)
{
    return acct_info.xbzrle_cache_miss;
}

double xbzrle_mig_cache_miss_rate(void)
{
    return acct_info.xbzrle_cache_miss_rate;
}

uint64_t xbzrle_mig_pages_overflow(void)
{
    return acct_info.xbzrle_overflows;
}

/* This is the last block that we have visited serching for dirty pages
 */
static RAMBlock *last_seen_block;
/* This is the last block from where we have sent data */
static RAMBlock *last_sent_block;
static ram_addr_t last_offset;
static QemuMutex migration_bitmap_mutex;
static uint64_t migration_dirty_pages;
static uint32_t last_version;
static bool ram_bulk_stage;

/* used by the search for pages to send */
struct PageSearchStatus {
    /* Current block being searched */
    RAMBlock    *block;
    /* Current offset to search from */
    ram_addr_t   offset;
    /* Set once we wrap around */
    bool         complete_round;
};
typedef struct PageSearchStatus PageSearchStatus;

static struct BitmapRcu {
    struct rcu_head rcu;
    /* Main migration bitmap */
    unsigned long *bmap;
    /* bitmap of pages that haven't been sent even once
     * only maintained and used in postcopy at the moment
     * where it's used to send the dirtymap at the start
     * of the postcopy phase
     */
    unsigned long *unsentmap;
} *migration_bitmap_rcu;

struct CompressParam {
    bool start;
    bool done;
    QEMUFile *file;
    QemuMutex mutex;
    QemuCond cond;
    RAMBlock *block;
    ram_addr_t offset;
};
typedef struct CompressParam CompressParam;

struct DecompressParam {
    bool start;
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
static QemuMutex *comp_done_lock;
static QemuCond *comp_done_cond;
/* The empty QEMUFileOps will be used by file in CompressParam */
static const QEMUFileOps empty_ops = { };

static bool compression_switch;
static bool quit_comp_thread;
static bool quit_decomp_thread;
static DecompressParam *decomp_param;
static QemuThread *decompress_threads;

static int do_compress_ram_page(CompressParam *param);

static void *do_data_compress(void *opaque)
{
    CompressParam *param = opaque;

    while (!quit_comp_thread) {
        qemu_mutex_lock(&param->mutex);
        /* Re-check the quit_comp_thread in case of
         * terminate_compression_threads is called just before
         * qemu_mutex_lock(&param->mutex) and after
         * while(!quit_comp_thread), re-check it here can make
         * sure the compression thread terminate as expected.
         */
        while (!param->start && !quit_comp_thread) {
            qemu_cond_wait(&param->cond, &param->mutex);
        }
        if (!quit_comp_thread) {
            do_compress_ram_page(param);
        }
        param->start = false;
        qemu_mutex_unlock(&param->mutex);

        qemu_mutex_lock(comp_done_lock);
        param->done = true;
        qemu_cond_signal(comp_done_cond);
        qemu_mutex_unlock(comp_done_lock);
    }

    return NULL;
}

static inline void terminate_compression_threads(void)
{
    int idx, thread_count;

    thread_count = migrate_compress_threads();
    quit_comp_thread = true;
    for (idx = 0; idx < thread_count; idx++) {
        qemu_mutex_lock(&comp_param[idx].mutex);
        qemu_cond_signal(&comp_param[idx].cond);
        qemu_mutex_unlock(&comp_param[idx].mutex);
    }
}

void migrate_compress_threads_join(void)
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
    qemu_mutex_destroy(comp_done_lock);
    qemu_cond_destroy(comp_done_cond);
    g_free(compress_threads);
    g_free(comp_param);
    g_free(comp_done_cond);
    g_free(comp_done_lock);
    compress_threads = NULL;
    comp_param = NULL;
    comp_done_cond = NULL;
    comp_done_lock = NULL;
}

void migrate_compress_threads_create(void)
{
    int i, thread_count;

    if (!migrate_use_compression()) {
        return;
    }
    quit_comp_thread = false;
    compression_switch = true;
    thread_count = migrate_compress_threads();
    compress_threads = g_new0(QemuThread, thread_count);
    comp_param = g_new0(CompressParam, thread_count);
    comp_done_cond = g_new0(QemuCond, 1);
    comp_done_lock = g_new0(QemuMutex, 1);
    qemu_cond_init(comp_done_cond);
    qemu_mutex_init(comp_done_lock);
    for (i = 0; i < thread_count; i++) {
        /* com_param[i].file is just used as a dummy buffer to save data, set
         * it's ops to empty.
         */
        comp_param[i].file = qemu_fopen_ops(NULL, &empty_ops);
        comp_param[i].done = true;
        qemu_mutex_init(&comp_param[i].mutex);
        qemu_cond_init(&comp_param[i].cond);
        qemu_thread_create(compress_threads + i, "compress",
                           do_data_compress, comp_param + i,
                           QEMU_THREAD_JOINABLE);
    }
}

/**
 * save_page_header: Write page header to wire
 *
 * If this is the 1st block, it also writes the block identification
 *
 * Returns: Number of bytes written
 *
 * @f: QEMUFile where to send the data
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 *          in the lower bits, it contains flags
 */
static size_t save_page_header(QEMUFile *f, RAMBlock *block, ram_addr_t offset)
{
    size_t size, len;

    qemu_put_be64(f, offset);
    size = 8;

    if (!(offset & RAM_SAVE_FLAG_CONTINUE)) {
        len = strlen(block->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)block->idstr, len);
        size += 1 + len;
    }
    return size;
}

/* Reduce amount of guest cpu execution to hopefully slow down memory writes.
 * If guest dirty memory rate is reduced below the rate at which we can
 * transfer pages to the destination then we should be able to complete
 * migration. Some workloads dirty memory way too fast and will not effectively
 * converge, even with auto-converge.
 */
static void mig_throttle_guest_down(void)
{
    MigrationState *s = migrate_get_current();
    uint64_t pct_initial =
            s->parameters[MIGRATION_PARAMETER_CPU_THROTTLE_INITIAL];
    uint64_t pct_icrement =
            s->parameters[MIGRATION_PARAMETER_CPU_THROTTLE_INCREMENT];

    /* We have not started throttling yet. Let's start it. */
    if (!cpu_throttle_active()) {
        cpu_throttle_set(pct_initial);
    } else {
        /* Throttling already on, just increase the rate */
        cpu_throttle_set(cpu_throttle_get_percentage() + pct_icrement);
    }
}

/* Update the xbzrle cache to reflect a page that's been sent as all 0.
 * The important thing is that a stale (not-yet-0'd) page be replaced
 * by the new data.
 * As a bonus, if the page wasn't in the cache it gets added so that
 * when a small write is made into the 0'd page it gets XBZRLE sent
 */
static void xbzrle_cache_zero_page(ram_addr_t current_addr)
{
    if (ram_bulk_stage || !migrate_use_xbzrle()) {
        return;
    }

    /* We don't care if this fails to allocate a new cache page
     * as long as it updated an old one */
    cache_insert(XBZRLE.cache, current_addr, ZERO_TARGET_PAGE,
                 bitmap_sync_count);
}

#define ENCODING_FLAG_XBZRLE 0x1

/**
 * save_xbzrle_page: compress and send current page
 *
 * Returns: 1 means that we wrote the page
 *          0 means that page is identical to the one already sent
 *          -1 means that xbzrle would be longer than normal
 *
 * @f: QEMUFile where to send the data
 * @current_data:
 * @current_addr:
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @last_stage: if we are at the completion stage
 * @bytes_transferred: increase it with the number of transferred bytes
 */
static int save_xbzrle_page(QEMUFile *f, uint8_t **current_data,
                            ram_addr_t current_addr, RAMBlock *block,
                            ram_addr_t offset, bool last_stage,
                            uint64_t *bytes_transferred)
{
    int encoded_len = 0, bytes_xbzrle;
    uint8_t *prev_cached_page;

    if (!cache_is_cached(XBZRLE.cache, current_addr, bitmap_sync_count)) {
        acct_info.xbzrle_cache_miss++;
        if (!last_stage) {
            if (cache_insert(XBZRLE.cache, current_addr, *current_data,
                             bitmap_sync_count) == -1) {
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
        DPRINTF("Skipping unmodified page\n");
        return 0;
    } else if (encoded_len == -1) {
        DPRINTF("Overflow\n");
        acct_info.xbzrle_overflows++;
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
    bytes_xbzrle = save_page_header(f, block, offset | RAM_SAVE_FLAG_XBZRLE);
    qemu_put_byte(f, ENCODING_FLAG_XBZRLE);
    qemu_put_be16(f, encoded_len);
    qemu_put_buffer(f, XBZRLE.encoded_buf, encoded_len);
    bytes_xbzrle += encoded_len + 1 + 2;
    acct_info.xbzrle_pages++;
    acct_info.xbzrle_bytes += bytes_xbzrle;
    *bytes_transferred += bytes_xbzrle;

    return 1;
}

/* Called with rcu_read_lock() to protect migration_bitmap
 * rb: The RAMBlock  to search for dirty pages in
 * start: Start address (typically so we can continue from previous page)
 * ram_addr_abs: Pointer into which to store the address of the dirty page
 *               within the global ram_addr space
 *
 * Returns: byte offset within memory region of the start of a dirty page
 */
static inline
ram_addr_t migration_bitmap_find_dirty(RAMBlock *rb,
                                       ram_addr_t start,
                                       ram_addr_t *ram_addr_abs)
{
    unsigned long base = rb->offset >> TARGET_PAGE_BITS;
    unsigned long nr = base + (start >> TARGET_PAGE_BITS);
    uint64_t rb_size = rb->used_length;
    unsigned long size = base + (rb_size >> TARGET_PAGE_BITS);
    unsigned long *bitmap;

    unsigned long next;

    bitmap = atomic_rcu_read(&migration_bitmap_rcu)->bmap;
    if (ram_bulk_stage && nr > base) {
        next = nr + 1;
    } else {
        next = find_next_bit(bitmap, size, nr);
    }

    *ram_addr_abs = next << TARGET_PAGE_BITS;
    return (next - base) << TARGET_PAGE_BITS;
}

static inline bool migration_bitmap_clear_dirty(ram_addr_t addr)
{
    bool ret;
    int nr = addr >> TARGET_PAGE_BITS;
    unsigned long *bitmap = atomic_rcu_read(&migration_bitmap_rcu)->bmap;

    ret = test_and_clear_bit(nr, bitmap);

    if (ret) {
        migration_dirty_pages--;
    }
    return ret;
}

static void migration_bitmap_sync_range(ram_addr_t start, ram_addr_t length)
{
    unsigned long *bitmap;
    bitmap = atomic_rcu_read(&migration_bitmap_rcu)->bmap;
    migration_dirty_pages +=
        cpu_physical_memory_sync_dirty_bitmap(bitmap, start, length);
}

/* Fix me: there are too many global variables used in migration process. */
static int64_t start_time;
static int64_t bytes_xfer_prev;
static int64_t num_dirty_pages_period;
static uint64_t xbzrle_cache_miss_prev;
static uint64_t iterations_prev;

static void migration_bitmap_sync_init(void)
{
    start_time = 0;
    bytes_xfer_prev = 0;
    num_dirty_pages_period = 0;
    xbzrle_cache_miss_prev = 0;
    iterations_prev = 0;
}

static void migration_bitmap_sync(void)
{
    RAMBlock *block;
    uint64_t num_dirty_pages_init = migration_dirty_pages;
    MigrationState *s = migrate_get_current();
    int64_t end_time;
    int64_t bytes_xfer_now;

    bitmap_sync_count++;

    if (!bytes_xfer_prev) {
        bytes_xfer_prev = ram_bytes_transferred();
    }

    if (!start_time) {
        start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    }

    trace_migration_bitmap_sync_start();
    address_space_sync_dirty_bitmap(&address_space_memory);

    qemu_mutex_lock(&migration_bitmap_mutex);
    rcu_read_lock();
    QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
        migration_bitmap_sync_range(block->offset, block->used_length);
    }
    rcu_read_unlock();
    qemu_mutex_unlock(&migration_bitmap_mutex);

    trace_migration_bitmap_sync_end(migration_dirty_pages
                                    - num_dirty_pages_init);
    num_dirty_pages_period += migration_dirty_pages - num_dirty_pages_init;
    end_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    /* more than 1 second = 1000 millisecons */
    if (end_time > start_time + 1000) {
        if (migrate_auto_converge()) {
            /* The following detection logic can be refined later. For now:
               Check to see if the dirtied bytes is 50% more than the approx.
               amount of bytes that just got transferred since the last time we
               were in this routine. If that happens twice, start or increase
               throttling */
            bytes_xfer_now = ram_bytes_transferred();

            if (s->dirty_pages_rate &&
               (num_dirty_pages_period * TARGET_PAGE_SIZE >
                   (bytes_xfer_now - bytes_xfer_prev)/2) &&
               (dirty_rate_high_cnt++ >= 2)) {
                    trace_migration_throttle();
                    dirty_rate_high_cnt = 0;
                    mig_throttle_guest_down();
             }
             bytes_xfer_prev = bytes_xfer_now;
        }

        if (migrate_use_xbzrle()) {
            if (iterations_prev != acct_info.iterations) {
                acct_info.xbzrle_cache_miss_rate =
                   (double)(acct_info.xbzrle_cache_miss -
                            xbzrle_cache_miss_prev) /
                   (acct_info.iterations - iterations_prev);
            }
            iterations_prev = acct_info.iterations;
            xbzrle_cache_miss_prev = acct_info.xbzrle_cache_miss;
        }
        s->dirty_pages_rate = num_dirty_pages_period * 1000
            / (end_time - start_time);
        s->dirty_bytes_rate = s->dirty_pages_rate * TARGET_PAGE_SIZE;
        start_time = end_time;
        num_dirty_pages_period = 0;
    }
    s->dirty_sync_count = bitmap_sync_count;
    if (migrate_use_events()) {
        qapi_event_send_migration_pass(bitmap_sync_count, NULL);
    }
}

/**
 * save_zero_page: Send the zero page to the stream
 *
 * Returns: Number of pages written.
 *
 * @f: QEMUFile where to send the data
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @p: pointer to the page
 * @bytes_transferred: increase it with the number of transferred bytes
 */
static int save_zero_page(QEMUFile *f, RAMBlock *block, ram_addr_t offset,
                          uint8_t *p, uint64_t *bytes_transferred)
{
    int pages = -1;

    if (is_zero_range(p, TARGET_PAGE_SIZE)) {
        acct_info.dup_pages++;
        *bytes_transferred += save_page_header(f, block,
                                               offset | RAM_SAVE_FLAG_COMPRESS);
        qemu_put_byte(f, 0);
        *bytes_transferred += 1;
        pages = 1;
    }

    return pages;
}

/**
 * ram_save_page: Send the given page to the stream
 *
 * Returns: Number of pages written.
 *          < 0 - error
 *          >=0 - Number of pages written - this might legally be 0
 *                if xbzrle noticed the page was the same.
 *
 * @f: QEMUFile where to send the data
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @last_stage: if we are at the completion stage
 * @bytes_transferred: increase it with the number of transferred bytes
 */
static int ram_save_page(QEMUFile *f, PageSearchStatus *pss,
                         bool last_stage, uint64_t *bytes_transferred)
{
    int pages = -1;
    uint64_t bytes_xmit;
    ram_addr_t current_addr;
    uint8_t *p;
    int ret;
    bool send_async = true;
    RAMBlock *block = pss->block;
    ram_addr_t offset = pss->offset;

    p = block->host + offset;

    /* In doubt sent page as normal */
    bytes_xmit = 0;
    ret = ram_control_save_page(f, block->offset,
                           offset, TARGET_PAGE_SIZE, &bytes_xmit);
    if (bytes_xmit) {
        *bytes_transferred += bytes_xmit;
        pages = 1;
    }

    XBZRLE_cache_lock();

    current_addr = block->offset + offset;

    if (block == last_sent_block) {
        offset |= RAM_SAVE_FLAG_CONTINUE;
    }
    if (ret != RAM_SAVE_CONTROL_NOT_SUPP) {
        if (ret != RAM_SAVE_CONTROL_DELAYED) {
            if (bytes_xmit > 0) {
                acct_info.norm_pages++;
            } else if (bytes_xmit == 0) {
                acct_info.dup_pages++;
            }
        }
    } else {
        pages = save_zero_page(f, block, offset, p, bytes_transferred);
        if (pages > 0) {
            /* Must let xbzrle know, otherwise a previous (now 0'd) cached
             * page would be stale
             */
            xbzrle_cache_zero_page(current_addr);
        } else if (!ram_bulk_stage && migrate_use_xbzrle()) {
            pages = save_xbzrle_page(f, &p, current_addr, block,
                                     offset, last_stage, bytes_transferred);
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
        *bytes_transferred += save_page_header(f, block,
                                               offset | RAM_SAVE_FLAG_PAGE);
        if (send_async) {
            qemu_put_buffer_async(f, p, TARGET_PAGE_SIZE);
        } else {
            qemu_put_buffer(f, p, TARGET_PAGE_SIZE);
        }
        *bytes_transferred += TARGET_PAGE_SIZE;
        pages = 1;
        acct_info.norm_pages++;
    }

    XBZRLE_cache_unlock();

    return pages;
}

static int do_compress_ram_page(CompressParam *param)
{
    int bytes_sent, blen;
    uint8_t *p;
    RAMBlock *block = param->block;
    ram_addr_t offset = param->offset;

    p = block->host + (offset & TARGET_PAGE_MASK);

    bytes_sent = save_page_header(param->file, block, offset |
                                  RAM_SAVE_FLAG_COMPRESS_PAGE);
    blen = qemu_put_compression_data(param->file, p, TARGET_PAGE_SIZE,
                                     migrate_compress_level());
    bytes_sent += blen;

    return bytes_sent;
}

static inline void start_compression(CompressParam *param)
{
    param->done = false;
    qemu_mutex_lock(&param->mutex);
    param->start = true;
    qemu_cond_signal(&param->cond);
    qemu_mutex_unlock(&param->mutex);
}

static inline void start_decompression(DecompressParam *param)
{
    qemu_mutex_lock(&param->mutex);
    param->start = true;
    qemu_cond_signal(&param->cond);
    qemu_mutex_unlock(&param->mutex);
}

static uint64_t bytes_transferred;

static void flush_compressed_data(QEMUFile *f)
{
    int idx, len, thread_count;

    if (!migrate_use_compression()) {
        return;
    }
    thread_count = migrate_compress_threads();
    for (idx = 0; idx < thread_count; idx++) {
        if (!comp_param[idx].done) {
            qemu_mutex_lock(comp_done_lock);
            while (!comp_param[idx].done && !quit_comp_thread) {
                qemu_cond_wait(comp_done_cond, comp_done_lock);
            }
            qemu_mutex_unlock(comp_done_lock);
        }
        if (!quit_comp_thread) {
            len = qemu_put_qemu_file(f, comp_param[idx].file);
            bytes_transferred += len;
        }
    }
}

static inline void set_compress_params(CompressParam *param, RAMBlock *block,
                                       ram_addr_t offset)
{
    param->block = block;
    param->offset = offset;
}

static int compress_page_with_multi_thread(QEMUFile *f, RAMBlock *block,
                                           ram_addr_t offset,
                                           uint64_t *bytes_transferred)
{
    int idx, thread_count, bytes_xmit = -1, pages = -1;

    thread_count = migrate_compress_threads();
    qemu_mutex_lock(comp_done_lock);
    while (true) {
        for (idx = 0; idx < thread_count; idx++) {
            if (comp_param[idx].done) {
                bytes_xmit = qemu_put_qemu_file(f, comp_param[idx].file);
                set_compress_params(&comp_param[idx], block, offset);
                start_compression(&comp_param[idx]);
                pages = 1;
                acct_info.norm_pages++;
                *bytes_transferred += bytes_xmit;
                break;
            }
        }
        if (pages > 0) {
            break;
        } else {
            qemu_cond_wait(comp_done_cond, comp_done_lock);
        }
    }
    qemu_mutex_unlock(comp_done_lock);

    return pages;
}

/**
 * ram_save_compressed_page: compress the given page and send it to the stream
 *
 * Returns: Number of pages written.
 *
 * @f: QEMUFile where to send the data
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @last_stage: if we are at the completion stage
 * @bytes_transferred: increase it with the number of transferred bytes
 */
static int ram_save_compressed_page(QEMUFile *f, PageSearchStatus *pss,
                                    bool last_stage,
                                    uint64_t *bytes_transferred)
{
    int pages = -1;
    uint64_t bytes_xmit;
    uint8_t *p;
    int ret;
    RAMBlock *block = pss->block;
    ram_addr_t offset = pss->offset;

    p = block->host + offset;

    bytes_xmit = 0;
    ret = ram_control_save_page(f, block->offset,
                                offset, TARGET_PAGE_SIZE, &bytes_xmit);
    if (bytes_xmit) {
        *bytes_transferred += bytes_xmit;
        pages = 1;
    }
    if (block == last_sent_block) {
        offset |= RAM_SAVE_FLAG_CONTINUE;
    }
    if (ret != RAM_SAVE_CONTROL_NOT_SUPP) {
        if (ret != RAM_SAVE_CONTROL_DELAYED) {
            if (bytes_xmit > 0) {
                acct_info.norm_pages++;
            } else if (bytes_xmit == 0) {
                acct_info.dup_pages++;
            }
        }
    } else {
        /* When starting the process of a new block, the first page of
         * the block should be sent out before other pages in the same
         * block, and all the pages in last block should have been sent
         * out, keeping this order is important, because the 'cont' flag
         * is used to avoid resending the block name.
         */
        if (block != last_sent_block) {
            flush_compressed_data(f);
            pages = save_zero_page(f, block, offset, p, bytes_transferred);
            if (pages == -1) {
                set_compress_params(&comp_param[0], block, offset);
                /* Use the qemu thread to compress the data to make sure the
                 * first page is sent out before other pages
                 */
                bytes_xmit = do_compress_ram_page(&comp_param[0]);
                acct_info.norm_pages++;
                qemu_put_qemu_file(f, comp_param[0].file);
                *bytes_transferred += bytes_xmit;
                pages = 1;
            }
        } else {
            pages = save_zero_page(f, block, offset, p, bytes_transferred);
            if (pages == -1) {
                pages = compress_page_with_multi_thread(f, block, offset,
                                                        bytes_transferred);
            }
        }
    }

    return pages;
}

/*
 * Find the next dirty page and update any state associated with
 * the search process.
 *
 * Returns: True if a page is found
 *
 * @f: Current migration stream.
 * @pss: Data about the state of the current dirty page scan.
 * @*again: Set to false if the search has scanned the whole of RAM
 * *ram_addr_abs: Pointer into which to store the address of the dirty page
 *               within the global ram_addr space
 */
static bool find_dirty_block(QEMUFile *f, PageSearchStatus *pss,
                             bool *again, ram_addr_t *ram_addr_abs)
{
    pss->offset = migration_bitmap_find_dirty(pss->block, pss->offset,
                                              ram_addr_abs);
    if (pss->complete_round && pss->block == last_seen_block &&
        pss->offset >= last_offset) {
        /*
         * We've been once around the RAM and haven't found anything.
         * Give up.
         */
        *again = false;
        return false;
    }
    if (pss->offset >= pss->block->used_length) {
        /* Didn't find anything in this RAM Block */
        pss->offset = 0;
        pss->block = QLIST_NEXT_RCU(pss->block, next);
        if (!pss->block) {
            /* Hit the end of the list */
            pss->block = QLIST_FIRST_RCU(&ram_list.blocks);
            /* Flag that we've looped */
            pss->complete_round = true;
            ram_bulk_stage = false;
            if (migrate_use_xbzrle()) {
                /* If xbzrle is on, stop using the data compression at this
                 * point. In theory, xbzrle can do better than compression.
                 */
                flush_compressed_data(f);
                compression_switch = false;
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

/*
 * Helper for 'get_queued_page' - gets a page off the queue
 *      ms:      MigrationState in
 * *offset:      Used to return the offset within the RAMBlock
 * ram_addr_abs: global offset in the dirty/sent bitmaps
 *
 * Returns:      block (or NULL if none available)
 */
static RAMBlock *unqueue_page(MigrationState *ms, ram_addr_t *offset,
                              ram_addr_t *ram_addr_abs)
{
    RAMBlock *block = NULL;

    qemu_mutex_lock(&ms->src_page_req_mutex);
    if (!QSIMPLEQ_EMPTY(&ms->src_page_requests)) {
        struct MigrationSrcPageRequest *entry =
                                QSIMPLEQ_FIRST(&ms->src_page_requests);
        block = entry->rb;
        *offset = entry->offset;
        *ram_addr_abs = (entry->offset + entry->rb->offset) &
                        TARGET_PAGE_MASK;

        if (entry->len > TARGET_PAGE_SIZE) {
            entry->len -= TARGET_PAGE_SIZE;
            entry->offset += TARGET_PAGE_SIZE;
        } else {
            memory_region_unref(block->mr);
            QSIMPLEQ_REMOVE_HEAD(&ms->src_page_requests, next_req);
            g_free(entry);
        }
    }
    qemu_mutex_unlock(&ms->src_page_req_mutex);

    return block;
}

/*
 * Unqueue a page from the queue fed by postcopy page requests; skips pages
 * that are already sent (!dirty)
 *
 *      ms:      MigrationState in
 *     pss:      PageSearchStatus structure updated with found block/offset
 * ram_addr_abs: global offset in the dirty/sent bitmaps
 *
 * Returns:      true if a queued page is found
 */
static bool get_queued_page(MigrationState *ms, PageSearchStatus *pss,
                            ram_addr_t *ram_addr_abs)
{
    RAMBlock  *block;
    ram_addr_t offset;
    bool dirty;

    do {
        block = unqueue_page(ms, &offset, ram_addr_abs);
        /*
         * We're sending this page, and since it's postcopy nothing else
         * will dirty it, and we must make sure it doesn't get sent again
         * even if this queue request was received after the background
         * search already sent it.
         */
        if (block) {
            unsigned long *bitmap;
            bitmap = atomic_rcu_read(&migration_bitmap_rcu)->bmap;
            dirty = test_bit(*ram_addr_abs >> TARGET_PAGE_BITS, bitmap);
            if (!dirty) {
                trace_get_queued_page_not_dirty(
                    block->idstr, (uint64_t)offset,
                    (uint64_t)*ram_addr_abs,
                    test_bit(*ram_addr_abs >> TARGET_PAGE_BITS,
                         atomic_rcu_read(&migration_bitmap_rcu)->unsentmap));
            } else {
                trace_get_queued_page(block->idstr,
                                      (uint64_t)offset,
                                      (uint64_t)*ram_addr_abs);
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
        ram_bulk_stage = false;

        /*
         * We want the background search to continue from the queued page
         * since the guest is likely to want other pages near to the page
         * it just requested.
         */
        pss->block = block;
        pss->offset = offset;
    }

    return !!block;
}

/**
 * flush_page_queue: Flush any remaining pages in the ram request queue
 *    it should be empty at the end anyway, but in error cases there may be
 *    some left.
 *
 * ms: MigrationState
 */
void flush_page_queue(MigrationState *ms)
{
    struct MigrationSrcPageRequest *mspr, *next_mspr;
    /* This queue generally should be empty - but in the case of a failed
     * migration might have some droppings in.
     */
    rcu_read_lock();
    QSIMPLEQ_FOREACH_SAFE(mspr, &ms->src_page_requests, next_req, next_mspr) {
        memory_region_unref(mspr->rb->mr);
        QSIMPLEQ_REMOVE_HEAD(&ms->src_page_requests, next_req);
        g_free(mspr);
    }
    rcu_read_unlock();
}

/**
 * Queue the pages for transmission, e.g. a request from postcopy destination
 *   ms: MigrationStatus in which the queue is held
 *   rbname: The RAMBlock the request is for - may be NULL (to mean reuse last)
 *   start: Offset from the start of the RAMBlock
 *   len: Length (in bytes) to send
 *   Return: 0 on success
 */
int ram_save_queue_pages(MigrationState *ms, const char *rbname,
                         ram_addr_t start, ram_addr_t len)
{
    RAMBlock *ramblock;

    rcu_read_lock();
    if (!rbname) {
        /* Reuse last RAMBlock */
        ramblock = ms->last_req_rb;

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
        ms->last_req_rb = ramblock;
    }
    trace_ram_save_queue_pages(ramblock->idstr, start, len);
    if (start+len > ramblock->used_length) {
        error_report("%s request overrun start=" RAM_ADDR_FMT " len="
                     RAM_ADDR_FMT " blocklen=" RAM_ADDR_FMT,
                     __func__, start, len, ramblock->used_length);
        goto err;
    }

    struct MigrationSrcPageRequest *new_entry =
        g_malloc0(sizeof(struct MigrationSrcPageRequest));
    new_entry->rb = ramblock;
    new_entry->offset = start;
    new_entry->len = len;

    memory_region_ref(ramblock->mr);
    qemu_mutex_lock(&ms->src_page_req_mutex);
    QSIMPLEQ_INSERT_TAIL(&ms->src_page_requests, new_entry, next_req);
    qemu_mutex_unlock(&ms->src_page_req_mutex);
    rcu_read_unlock();

    return 0;

err:
    rcu_read_unlock();
    return -1;
}

/**
 * ram_save_target_page: Save one target page
 *
 *
 * @f: QEMUFile where to send the data
 * @block: pointer to block that contains the page we want to send
 * @offset: offset inside the block for the page;
 * @last_stage: if we are at the completion stage
 * @bytes_transferred: increase it with the number of transferred bytes
 * @dirty_ram_abs: Address of the start of the dirty page in ram_addr_t space
 *
 * Returns: Number of pages written.
 */
static int ram_save_target_page(MigrationState *ms, QEMUFile *f,
                                PageSearchStatus *pss,
                                bool last_stage,
                                uint64_t *bytes_transferred,
                                ram_addr_t dirty_ram_abs)
{
    int res = 0;

    /* Check the pages is dirty and if it is send it */
    if (migration_bitmap_clear_dirty(dirty_ram_abs)) {
        unsigned long *unsentmap;
        if (compression_switch && migrate_use_compression()) {
            res = ram_save_compressed_page(f, pss,
                                           last_stage,
                                           bytes_transferred);
        } else {
            res = ram_save_page(f, pss, last_stage,
                                bytes_transferred);
        }

        if (res < 0) {
            return res;
        }
        unsentmap = atomic_rcu_read(&migration_bitmap_rcu)->unsentmap;
        if (unsentmap) {
            clear_bit(dirty_ram_abs >> TARGET_PAGE_BITS, unsentmap);
        }
        /* Only update last_sent_block if a block was actually sent; xbzrle
         * might have decided the page was identical so didn't bother writing
         * to the stream.
         */
        if (res > 0) {
            last_sent_block = pss->block;
        }
    }

    return res;
}

/**
 * ram_save_host_page: Starting at *offset send pages up to the end
 *                     of the current host page.  It's valid for the initial
 *                     offset to point into the middle of a host page
 *                     in which case the remainder of the hostpage is sent.
 *                     Only dirty target pages are sent.
 *
 * Returns: Number of pages written.
 *
 * @f: QEMUFile where to send the data
 * @block: pointer to block that contains the page we want to send
 * @offset: offset inside the block for the page; updated to last target page
 *          sent
 * @last_stage: if we are at the completion stage
 * @bytes_transferred: increase it with the number of transferred bytes
 * @dirty_ram_abs: Address of the start of the dirty page in ram_addr_t space
 */
static int ram_save_host_page(MigrationState *ms, QEMUFile *f,
                              PageSearchStatus *pss,
                              bool last_stage,
                              uint64_t *bytes_transferred,
                              ram_addr_t dirty_ram_abs)
{
    int tmppages, pages = 0;
    do {
        tmppages = ram_save_target_page(ms, f, pss, last_stage,
                                        bytes_transferred, dirty_ram_abs);
        if (tmppages < 0) {
            return tmppages;
        }

        pages += tmppages;
        pss->offset += TARGET_PAGE_SIZE;
        dirty_ram_abs += TARGET_PAGE_SIZE;
    } while (pss->offset & (qemu_host_page_size - 1));

    /* The offset we leave with is the last one we looked at */
    pss->offset -= TARGET_PAGE_SIZE;
    return pages;
}

/**
 * ram_find_and_save_block: Finds a dirty page and sends it to f
 *
 * Called within an RCU critical section.
 *
 * Returns:  The number of pages written
 *           0 means no dirty pages
 *
 * @f: QEMUFile where to send the data
 * @last_stage: if we are at the completion stage
 * @bytes_transferred: increase it with the number of transferred bytes
 *
 * On systems where host-page-size > target-page-size it will send all the
 * pages in a host page that are dirty.
 */

static int ram_find_and_save_block(QEMUFile *f, bool last_stage,
                                   uint64_t *bytes_transferred)
{
    PageSearchStatus pss;
    MigrationState *ms = migrate_get_current();
    int pages = 0;
    bool again, found;
    ram_addr_t dirty_ram_abs; /* Address of the start of the dirty page in
                                 ram_addr_t space */

    pss.block = last_seen_block;
    pss.offset = last_offset;
    pss.complete_round = false;

    if (!pss.block) {
        pss.block = QLIST_FIRST_RCU(&ram_list.blocks);
    }

    do {
        again = true;
        found = get_queued_page(ms, &pss, &dirty_ram_abs);

        if (!found) {
            /* priority queue empty, so just search for something dirty */
            found = find_dirty_block(f, &pss, &again, &dirty_ram_abs);
        }

        if (found) {
            pages = ram_save_host_page(ms, f, &pss,
                                       last_stage, bytes_transferred,
                                       dirty_ram_abs);
        }
    } while (!pages && again);

    last_seen_block = pss.block;
    last_offset = pss.offset;

    return pages;
}

void acct_update_position(QEMUFile *f, size_t size, bool zero)
{
    uint64_t pages = size / TARGET_PAGE_SIZE;
    if (zero) {
        acct_info.dup_pages += pages;
    } else {
        acct_info.norm_pages += pages;
        bytes_transferred += size;
        qemu_update_position(f, size);
    }
}

static ram_addr_t ram_save_remaining(void)
{
    return migration_dirty_pages;
}

uint64_t ram_bytes_remaining(void)
{
    return ram_save_remaining() * TARGET_PAGE_SIZE;
}

uint64_t ram_bytes_transferred(void)
{
    return bytes_transferred;
}

uint64_t ram_bytes_total(void)
{
    RAMBlock *block;
    uint64_t total = 0;

    rcu_read_lock();
    QLIST_FOREACH_RCU(block, &ram_list.blocks, next)
        total += block->used_length;
    rcu_read_unlock();
    return total;
}

void free_xbzrle_decoded_buf(void)
{
    g_free(xbzrle_decoded_buf);
    xbzrle_decoded_buf = NULL;
}

static void migration_bitmap_free(struct BitmapRcu *bmap)
{
    g_free(bmap->bmap);
    g_free(bmap->unsentmap);
    g_free(bmap);
}

static void ram_migration_cleanup(void *opaque)
{
    /* caller have hold iothread lock or is in a bh, so there is
     * no writing race against this migration_bitmap
     */
    struct BitmapRcu *bitmap = migration_bitmap_rcu;
    atomic_rcu_set(&migration_bitmap_rcu, NULL);
    if (bitmap) {
        memory_global_dirty_log_stop();
        call_rcu(bitmap, migration_bitmap_free, rcu);
    }

    XBZRLE_cache_lock();
    if (XBZRLE.cache) {
        cache_fini(XBZRLE.cache);
        g_free(XBZRLE.encoded_buf);
        g_free(XBZRLE.current_buf);
        XBZRLE.cache = NULL;
        XBZRLE.encoded_buf = NULL;
        XBZRLE.current_buf = NULL;
    }
    XBZRLE_cache_unlock();
}

static void reset_ram_globals(void)
{
    last_seen_block = NULL;
    last_sent_block = NULL;
    last_offset = 0;
    last_version = ram_list.version;
    ram_bulk_stage = true;
}

#define MAX_WAIT 50 /* ms, half buffered_file limit */

void migration_bitmap_extend(ram_addr_t old, ram_addr_t new)
{
    /* called in qemu main thread, so there is
     * no writing race against this migration_bitmap
     */
    if (migration_bitmap_rcu) {
        struct BitmapRcu *old_bitmap = migration_bitmap_rcu, *bitmap;
        bitmap = g_new(struct BitmapRcu, 1);
        bitmap->bmap = bitmap_new(new);

        /* prevent migration_bitmap content from being set bit
         * by migration_bitmap_sync_range() at the same time.
         * it is safe to migration if migration_bitmap is cleared bit
         * at the same time.
         */
        qemu_mutex_lock(&migration_bitmap_mutex);
        bitmap_copy(bitmap->bmap, old_bitmap->bmap, old);
        bitmap_set(bitmap->bmap, old, new - old);

        /* We don't have a way to safely extend the sentmap
         * with RCU; so mark it as missing, entry to postcopy
         * will fail.
         */
        bitmap->unsentmap = NULL;

        atomic_rcu_set(&migration_bitmap_rcu, bitmap);
        qemu_mutex_unlock(&migration_bitmap_mutex);
        migration_dirty_pages += new - old;
        call_rcu(old_bitmap, migration_bitmap_free, rcu);
    }
}

/*
 * 'expected' is the value you expect the bitmap mostly to be full
 * of; it won't bother printing lines that are all this value.
 * If 'todump' is null the migration bitmap is dumped.
 */
void ram_debug_dump_bitmap(unsigned long *todump, bool expected)
{
    int64_t ram_pages = last_ram_offset() >> TARGET_PAGE_BITS;

    int64_t cur;
    int64_t linelen = 128;
    char linebuf[129];

    if (!todump) {
        todump = atomic_rcu_read(&migration_bitmap_rcu)->bmap;
    }

    for (cur = 0; cur < ram_pages; cur += linelen) {
        int64_t curb;
        bool found = false;
        /*
         * Last line; catch the case where the line length
         * is longer than remaining ram
         */
        if (cur + linelen > ram_pages) {
            linelen = ram_pages - cur;
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

/*
 * Callback from postcopy_each_ram_send_discard for each RAMBlock
 * Note: At this point the 'unsentmap' is the processed bitmap combined
 *       with the dirtymap; so a '1' means it's either dirty or unsent.
 * start,length: Indexes into the bitmap for the first bit
 *            representing the named block and length in target-pages
 */
static int postcopy_send_discard_bm_ram(MigrationState *ms,
                                        PostcopyDiscardState *pds,
                                        unsigned long start,
                                        unsigned long length)
{
    unsigned long end = start + length; /* one after the end */
    unsigned long current;
    unsigned long *unsentmap;

    unsentmap = atomic_rcu_read(&migration_bitmap_rcu)->unsentmap;
    for (current = start; current < end; ) {
        unsigned long one = find_next_bit(unsentmap, end, current);

        if (one <= end) {
            unsigned long zero = find_next_zero_bit(unsentmap, end, one + 1);
            unsigned long discard_length;

            if (zero >= end) {
                discard_length = end - one;
            } else {
                discard_length = zero - one;
            }
            postcopy_discard_send_range(ms, pds, one, discard_length);
            current = one + discard_length;
        } else {
            current = one;
        }
    }

    return 0;
}

/*
 * Utility for the outgoing postcopy code.
 *   Calls postcopy_send_discard_bm_ram for each RAMBlock
 *   passing it bitmap indexes and name.
 * Returns: 0 on success
 * (qemu_ram_foreach_block ends up passing unscaled lengths
 *  which would mean postcopy code would have to deal with target page)
 */
static int postcopy_each_ram_send_discard(MigrationState *ms)
{
    struct RAMBlock *block;
    int ret;

    QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
        unsigned long first = block->offset >> TARGET_PAGE_BITS;
        PostcopyDiscardState *pds = postcopy_discard_send_init(ms,
                                                               first,
                                                               block->idstr);

        /*
         * Postcopy sends chunks of bitmap over the wire, but it
         * just needs indexes at this point, avoids it having
         * target page specific code.
         */
        ret = postcopy_send_discard_bm_ram(ms, pds, first,
                                    block->used_length >> TARGET_PAGE_BITS);
        postcopy_discard_send_finish(ms, pds);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

/*
 * Helper for postcopy_chunk_hostpages; it's called twice to cleanup
 *   the two bitmaps, that are similar, but one is inverted.
 *
 * We search for runs of target-pages that don't start or end on a
 * host page boundary;
 * unsent_pass=true: Cleans up partially unsent host pages by searching
 *                 the unsentmap
 * unsent_pass=false: Cleans up partially dirty host pages by searching
 *                 the main migration bitmap
 *
 */
static void postcopy_chunk_hostpages_pass(MigrationState *ms, bool unsent_pass,
                                          RAMBlock *block,
                                          PostcopyDiscardState *pds)
{
    unsigned long *bitmap;
    unsigned long *unsentmap;
    unsigned int host_ratio = qemu_host_page_size / TARGET_PAGE_SIZE;
    unsigned long first = block->offset >> TARGET_PAGE_BITS;
    unsigned long len = block->used_length >> TARGET_PAGE_BITS;
    unsigned long last = first + (len - 1);
    unsigned long run_start;

    bitmap = atomic_rcu_read(&migration_bitmap_rcu)->bmap;
    unsentmap = atomic_rcu_read(&migration_bitmap_rcu)->unsentmap;

    if (unsent_pass) {
        /* Find a sent page */
        run_start = find_next_zero_bit(unsentmap, last + 1, first);
    } else {
        /* Find a dirty page */
        run_start = find_next_bit(bitmap, last + 1, first);
    }

    while (run_start <= last) {
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
                run_end = find_next_bit(unsentmap, last + 1, run_start + 1);
            } else {
                run_end = find_next_zero_bit(bitmap, last + 1, run_start + 1);
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
                migration_dirty_pages += !test_and_set_bit(page, bitmap);
            }
        }

        if (unsent_pass) {
            /* Find the next sent page for the next iteration */
            run_start = find_next_zero_bit(unsentmap, last + 1,
                                           run_start);
        } else {
            /* Find the next dirty page for the next iteration */
            run_start = find_next_bit(bitmap, last + 1, run_start);
        }
    }
}

/*
 * Utility for the outgoing postcopy code.
 *
 * Discard any partially sent host-page size chunks, mark any partially
 * dirty host-page size chunks as all dirty.
 *
 * Returns: 0 on success
 */
static int postcopy_chunk_hostpages(MigrationState *ms)
{
    struct RAMBlock *block;

    if (qemu_host_page_size == TARGET_PAGE_SIZE) {
        /* Easy case - TPS==HPS - nothing to be done */
        return 0;
    }

    /* Easiest way to make sure we don't resume in the middle of a host-page */
    last_seen_block = NULL;
    last_sent_block = NULL;
    last_offset     = 0;

    QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
        unsigned long first = block->offset >> TARGET_PAGE_BITS;

        PostcopyDiscardState *pds =
                         postcopy_discard_send_init(ms, first, block->idstr);

        /* First pass: Discard all partially sent host pages */
        postcopy_chunk_hostpages_pass(ms, true, block, pds);
        /*
         * Second pass: Ensure that all partially dirty host pages are made
         * fully dirty.
         */
        postcopy_chunk_hostpages_pass(ms, false, block, pds);

        postcopy_discard_send_finish(ms, pds);
    } /* ram_list loop */

    return 0;
}

/*
 * Transmit the set of pages to be discarded after precopy to the target
 * these are pages that:
 *     a) Have been previously transmitted but are now dirty again
 *     b) Pages that have never been transmitted, this ensures that
 *        any pages on the destination that have been mapped by background
 *        tasks get discarded (transparent huge pages is the specific concern)
 * Hopefully this is pretty sparse
 */
int ram_postcopy_send_discard_bitmap(MigrationState *ms)
{
    int ret;
    unsigned long *bitmap, *unsentmap;

    rcu_read_lock();

    /* This should be our last sync, the src is now paused */
    migration_bitmap_sync();

    unsentmap = atomic_rcu_read(&migration_bitmap_rcu)->unsentmap;
    if (!unsentmap) {
        /* We don't have a safe way to resize the sentmap, so
         * if the bitmap was resized it will be NULL at this
         * point.
         */
        error_report("migration ram resized during precopy phase");
        rcu_read_unlock();
        return -EINVAL;
    }

    /* Deal with TPS != HPS */
    ret = postcopy_chunk_hostpages(ms);
    if (ret) {
        rcu_read_unlock();
        return ret;
    }

    /*
     * Update the unsentmap to be unsentmap = unsentmap | dirty
     */
    bitmap = atomic_rcu_read(&migration_bitmap_rcu)->bmap;
    bitmap_or(unsentmap, unsentmap, bitmap,
               last_ram_offset() >> TARGET_PAGE_BITS);


    trace_ram_postcopy_send_discard_bitmap();
#ifdef DEBUG_POSTCOPY
    ram_debug_dump_bitmap(unsentmap, true);
#endif

    ret = postcopy_each_ram_send_discard(ms);
    rcu_read_unlock();

    return ret;
}

/*
 * At the start of the postcopy phase of migration, any now-dirty
 * precopied pages are discarded.
 *
 * start, length describe a byte address range within the RAMBlock
 *
 * Returns 0 on success.
 */
int ram_discard_range(MigrationIncomingState *mis,
                      const char *block_name,
                      uint64_t start, size_t length)
{
    int ret = -1;

    rcu_read_lock();
    RAMBlock *rb = qemu_ram_block_by_name(block_name);

    if (!rb) {
        error_report("ram_discard_range: Failed to find block '%s'",
                     block_name);
        goto err;
    }

    uint8_t *host_startaddr = rb->host + start;

    if ((uintptr_t)host_startaddr & (qemu_host_page_size - 1)) {
        error_report("ram_discard_range: Unaligned start address: %p",
                     host_startaddr);
        goto err;
    }

    if ((start + length) <= rb->used_length) {
        uint8_t *host_endaddr = host_startaddr + length;
        if ((uintptr_t)host_endaddr & (qemu_host_page_size - 1)) {
            error_report("ram_discard_range: Unaligned end address: %p",
                         host_endaddr);
            goto err;
        }
        ret = postcopy_ram_discard_range(mis, host_startaddr, length);
    } else {
        error_report("ram_discard_range: Overrun block '%s' (%" PRIu64
                     "/%zx/" RAM_ADDR_FMT")",
                     block_name, start, length, rb->used_length);
    }

err:
    rcu_read_unlock();

    return ret;
}


/* Each of ram_save_setup, ram_save_iterate and ram_save_complete has
 * long-running RCU critical section.  When rcu-reclaims in the code
 * start to become numerous it will be necessary to reduce the
 * granularity of these critical sections.
 */

static int ram_save_setup(QEMUFile *f, void *opaque)
{
    RAMBlock *block;
    int64_t ram_bitmap_pages; /* Size of bitmap in pages, including gaps */

    dirty_rate_high_cnt = 0;
    bitmap_sync_count = 0;
    migration_bitmap_sync_init();
    qemu_mutex_init(&migration_bitmap_mutex);

    if (migrate_use_xbzrle()) {
        XBZRLE_cache_lock();
        XBZRLE.cache = cache_init(migrate_xbzrle_cache_size() /
                                  TARGET_PAGE_SIZE,
                                  TARGET_PAGE_SIZE);
        if (!XBZRLE.cache) {
            XBZRLE_cache_unlock();
            error_report("Error creating cache");
            return -1;
        }
        XBZRLE_cache_unlock();

        /* We prefer not to abort if there is no memory */
        XBZRLE.encoded_buf = g_try_malloc0(TARGET_PAGE_SIZE);
        if (!XBZRLE.encoded_buf) {
            error_report("Error allocating encoded_buf");
            return -1;
        }

        XBZRLE.current_buf = g_try_malloc(TARGET_PAGE_SIZE);
        if (!XBZRLE.current_buf) {
            error_report("Error allocating current_buf");
            g_free(XBZRLE.encoded_buf);
            XBZRLE.encoded_buf = NULL;
            return -1;
        }

        acct_clear();
    }

    /* For memory_global_dirty_log_start below.  */
    qemu_mutex_lock_iothread();

    qemu_mutex_lock_ramlist();
    rcu_read_lock();
    bytes_transferred = 0;
    reset_ram_globals();

    ram_bitmap_pages = last_ram_offset() >> TARGET_PAGE_BITS;
    migration_bitmap_rcu = g_new0(struct BitmapRcu, 1);
    migration_bitmap_rcu->bmap = bitmap_new(ram_bitmap_pages);
    bitmap_set(migration_bitmap_rcu->bmap, 0, ram_bitmap_pages);

    if (migrate_postcopy_ram()) {
        migration_bitmap_rcu->unsentmap = bitmap_new(ram_bitmap_pages);
        bitmap_set(migration_bitmap_rcu->unsentmap, 0, ram_bitmap_pages);
    }

    /*
     * Count the total number of pages used by ram blocks not including any
     * gaps due to alignment or unplugs.
     */
    migration_dirty_pages = ram_bytes_total() >> TARGET_PAGE_BITS;

    memory_global_dirty_log_start();
    migration_bitmap_sync();
    qemu_mutex_unlock_ramlist();
    qemu_mutex_unlock_iothread();

    qemu_put_be64(f, ram_bytes_total() | RAM_SAVE_FLAG_MEM_SIZE);

    QLIST_FOREACH_RCU(block, &ram_list.blocks, next) {
        qemu_put_byte(f, strlen(block->idstr));
        qemu_put_buffer(f, (uint8_t *)block->idstr, strlen(block->idstr));
        qemu_put_be64(f, block->used_length);
    }

    rcu_read_unlock();

    ram_control_before_iterate(f, RAM_CONTROL_SETUP);
    ram_control_after_iterate(f, RAM_CONTROL_SETUP);

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    return 0;
}

static int ram_save_iterate(QEMUFile *f, void *opaque)
{
    int ret;
    int i;
    int64_t t0;
    int pages_sent = 0;

    rcu_read_lock();
    if (ram_list.version != last_version) {
        reset_ram_globals();
    }

    /* Read version before ram_list.blocks */
    smp_rmb();

    ram_control_before_iterate(f, RAM_CONTROL_ROUND);

    t0 = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    i = 0;
    while ((ret = qemu_file_rate_limit(f)) == 0) {
        int pages;

        pages = ram_find_and_save_block(f, false, &bytes_transferred);
        /* no more pages to sent */
        if (pages == 0) {
            break;
        }
        pages_sent += pages;
        acct_info.iterations++;

        /* we want to check in the 1st loop, just in case it was the 1st time
           and we had to sync the dirty bitmap.
           qemu_get_clock_ns() is a bit expensive, so we only check each some
           iterations
        */
        if ((i & 63) == 0) {
            uint64_t t1 = (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - t0) / 1000000;
            if (t1 > MAX_WAIT) {
                DPRINTF("big wait: %" PRIu64 " milliseconds, %d iterations\n",
                        t1, i);
                break;
            }
        }
        i++;
    }
    flush_compressed_data(f);
    rcu_read_unlock();

    /*
     * Must occur before EOS (or any QEMUFile operation)
     * because of RDMA protocol.
     */
    ram_control_after_iterate(f, RAM_CONTROL_ROUND);

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    bytes_transferred += 8;

    ret = qemu_file_get_error(f);
    if (ret < 0) {
        return ret;
    }

    return pages_sent;
}

/* Called with iothread lock */
static int ram_save_complete(QEMUFile *f, void *opaque)
{
    rcu_read_lock();

    if (!migration_in_postcopy(migrate_get_current())) {
        migration_bitmap_sync();
    }

    ram_control_before_iterate(f, RAM_CONTROL_FINISH);

    /* try transferring iterative blocks of memory */

    /* flush all remaining blocks regardless of rate limiting */
    while (true) {
        int pages;

        pages = ram_find_and_save_block(f, true, &bytes_transferred);
        /* no more blocks to sent */
        if (pages == 0) {
            break;
        }
    }

    flush_compressed_data(f);
    ram_control_after_iterate(f, RAM_CONTROL_FINISH);

    rcu_read_unlock();

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    return 0;
}

static void ram_save_pending(QEMUFile *f, void *opaque, uint64_t max_size,
                             uint64_t *non_postcopiable_pending,
                             uint64_t *postcopiable_pending)
{
    uint64_t remaining_size;

    remaining_size = ram_save_remaining() * TARGET_PAGE_SIZE;

    if (!migration_in_postcopy(migrate_get_current()) &&
        remaining_size < max_size) {
        qemu_mutex_lock_iothread();
        rcu_read_lock();
        migration_bitmap_sync();
        rcu_read_unlock();
        qemu_mutex_unlock_iothread();
        remaining_size = ram_save_remaining() * TARGET_PAGE_SIZE;
    }

    /* We can do postcopy, and all the data is postcopiable */
    *postcopiable_pending += remaining_size;
}

static int load_xbzrle(QEMUFile *f, ram_addr_t addr, void *host)
{
    unsigned int xh_len;
    int xh_flags;
    uint8_t *loaded_data;

    if (!xbzrle_decoded_buf) {
        xbzrle_decoded_buf = g_malloc(TARGET_PAGE_SIZE);
    }
    loaded_data = xbzrle_decoded_buf;

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
    /* load data and decode */
    qemu_get_buffer_in_place(f, &loaded_data, xh_len);

    /* decode RLE */
    if (xbzrle_decode_buffer(loaded_data, xh_len, host,
                             TARGET_PAGE_SIZE) == -1) {
        error_report("Failed to load XBZRLE page - decode error!");
        return -1;
    }

    return 0;
}

/* Must be called from within a rcu critical section.
 * Returns a pointer from within the RCU-protected ram_list.
 */
/*
 * Read a RAMBlock ID from the stream f.
 *
 * f: Stream to read from
 * flags: Page flags (mostly to see if it's a continuation of previous block)
 */
static inline RAMBlock *ram_block_from_stream(QEMUFile *f,
                                              int flags)
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

/*
 * If a page (or a whole RDMA chunk) has been
 * determined to be zero, then zap it.
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

    while (!quit_decomp_thread) {
        qemu_mutex_lock(&param->mutex);
        while (!param->start && !quit_decomp_thread) {
            qemu_cond_wait(&param->cond, &param->mutex);
            pagesize = TARGET_PAGE_SIZE;
            if (!quit_decomp_thread) {
                /* uncompress() will return failed in some case, especially
                 * when the page is dirted when doing the compression, it's
                 * not a problem because the dirty page will be retransferred
                 * and uncompress() won't break the data in other pages.
                 */
                uncompress((Bytef *)param->des, &pagesize,
                           (const Bytef *)param->compbuf, param->len);
            }
            param->start = false;
        }
        qemu_mutex_unlock(&param->mutex);
    }

    return NULL;
}

void migrate_decompress_threads_create(void)
{
    int i, thread_count;

    thread_count = migrate_decompress_threads();
    decompress_threads = g_new0(QemuThread, thread_count);
    decomp_param = g_new0(DecompressParam, thread_count);
    quit_decomp_thread = false;
    for (i = 0; i < thread_count; i++) {
        qemu_mutex_init(&decomp_param[i].mutex);
        qemu_cond_init(&decomp_param[i].cond);
        decomp_param[i].compbuf = g_malloc0(compressBound(TARGET_PAGE_SIZE));
        qemu_thread_create(decompress_threads + i, "decompress",
                           do_data_decompress, decomp_param + i,
                           QEMU_THREAD_JOINABLE);
    }
}

void migrate_decompress_threads_join(void)
{
    int i, thread_count;

    quit_decomp_thread = true;
    thread_count = migrate_decompress_threads();
    for (i = 0; i < thread_count; i++) {
        qemu_mutex_lock(&decomp_param[i].mutex);
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
    while (true) {
        for (idx = 0; idx < thread_count; idx++) {
            if (!decomp_param[idx].start) {
                qemu_get_buffer(f, decomp_param[idx].compbuf, len);
                decomp_param[idx].des = host;
                decomp_param[idx].len = len;
                start_decompression(&decomp_param[idx]);
                break;
            }
        }
        if (idx < thread_count) {
            break;
        }
    }
}

/*
 * Allocate data structures etc needed by incoming migration with postcopy-ram
 * postcopy-ram's similarly names postcopy_ram_incoming_init does the work
 */
int ram_postcopy_incoming_init(MigrationIncomingState *mis)
{
    size_t ram_pages = last_ram_offset() >> TARGET_PAGE_BITS;

    return postcopy_ram_incoming_init(mis, ram_pages);
}

/*
 * Called in postcopy mode by ram_load().
 * rcu_read_lock is taken prior to this being called.
 */
static int ram_load_postcopy(QEMUFile *f)
{
    int flags = 0, ret = 0;
    bool place_needed = false;
    bool matching_page_sizes = qemu_host_page_size == TARGET_PAGE_SIZE;
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
        uint8_t ch;

        addr = qemu_get_be64(f);
        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        trace_ram_load_postcopy_loop((uint64_t)addr, flags);
        place_needed = false;
        if (flags & (RAM_SAVE_FLAG_COMPRESS | RAM_SAVE_FLAG_PAGE)) {
            RAMBlock *block = ram_block_from_stream(f, flags);

            host = host_from_ram_block_offset(block, addr);
            if (!host) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            page_buffer = host;
            /*
             * Postcopy requires that we place whole host pages atomically.
             * To make it atomic, the data is read into a temporary page
             * that's moved into place later.
             * The migration protocol uses,  possibly smaller, target-pages
             * however the source ensures it always sends all the components
             * of a host page in order.
             */
            page_buffer = postcopy_host_page +
                          ((uintptr_t)host & ~qemu_host_page_mask);
            /* If all TP are zero then we can optimise the place */
            if (!((uintptr_t)host & ~qemu_host_page_mask)) {
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
                                     ~qemu_host_page_mask) == 0;
            place_source = postcopy_host_page;
        }
        last_host = host;

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_COMPRESS:
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
            if (all_zero) {
                ret = postcopy_place_page_zero(mis,
                                               host + TARGET_PAGE_SIZE -
                                               qemu_host_page_size);
            } else {
                ret = postcopy_place_page(mis, host + TARGET_PAGE_SIZE -
                                               qemu_host_page_size,
                                               place_source);
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
    int flags = 0, ret = 0;
    static uint64_t seq_iter;
    int len = 0;
    /*
     * If system is running in postcopy mode, page inserts to host memory must
     * be atomic
     */
    bool postcopy_running = postcopy_state_get() >= POSTCOPY_INCOMING_LISTENING;

    seq_iter++;

    if (version_id != 4) {
        ret = -EINVAL;
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

        if (flags & (RAM_SAVE_FLAG_COMPRESS | RAM_SAVE_FLAG_PAGE |
                     RAM_SAVE_FLAG_COMPRESS_PAGE | RAM_SAVE_FLAG_XBZRLE)) {
            RAMBlock *block = ram_block_from_stream(f, flags);

            host = host_from_ram_block_offset(block, addr);
            if (!host) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
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

        case RAM_SAVE_FLAG_COMPRESS:
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

    rcu_read_unlock();
    DPRINTF("Completed load of VM with exit code %d seq iteration "
            "%" PRIu64 "\n", ret, seq_iter);
    return ret;
}

static SaveVMHandlers savevm_ram_handlers = {
    .save_live_setup = ram_save_setup,
    .save_live_iterate = ram_save_iterate,
    .save_live_complete_postcopy = ram_save_complete,
    .save_live_complete_precopy = ram_save_complete,
    .save_live_pending = ram_save_pending,
    .load_state = ram_load,
    .cleanup = ram_migration_cleanup,
};

void ram_mig_init(void)
{
    qemu_mutex_init(&XBZRLE.lock);
    register_savevm_live(NULL, "ram", 0, 4, &savevm_ram_handlers, NULL);
}
