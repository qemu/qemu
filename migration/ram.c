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
#include "qemu/cutils.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "qemu/madvise.h"
#include "qemu/main-loop.h"
#include "xbzrle.h"
#include "ram-compress.h"
#include "ram.h"
#include "migration.h"
#include "migration-stats.h"
#include "migration/register.h"
#include "migration/misc.h"
#include "qemu-file.h"
#include "postcopy-ram.h"
#include "page_cache.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-migration.h"
#include "qapi/qapi-events-migration.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qmp/qerror.h"
#include "trace.h"
#include "exec/ram_addr.h"
#include "exec/target_page.h"
#include "qemu/rcu_queue.h"
#include "migration/colo.h"
#include "block.h"
#include "sysemu/cpu-throttle.h"
#include "savevm.h"
#include "qemu/iov.h"
#include "multifd.h"
#include "sysemu/runstate.h"
#include "rdma.h"
#include "options.h"
#include "sysemu/dirtylimit.h"
#include "sysemu/kvm.h"

#include "hw/boards.h" /* for machine_dump_guest_core() */

#if defined(__linux__)
#include "qemu/userfaultfd.h"
#endif /* defined(__linux__) */

/***********************************************************/
/* ram save/restore */

/*
 * RAM_SAVE_FLAG_ZERO used to be named RAM_SAVE_FLAG_COMPRESS, it
 * worked for pages that were filled with the same char.  We switched
 * it to only search for the zero value.  And to avoid confusion with
 * RAM_SAVE_FLAG_COMPRESS_PAGE just rename it.
 */
/*
 * RAM_SAVE_FLAG_FULL was obsoleted in 2009, it can be reused now
 */
#define RAM_SAVE_FLAG_FULL     0x01
#define RAM_SAVE_FLAG_ZERO     0x02
#define RAM_SAVE_FLAG_MEM_SIZE 0x04
#define RAM_SAVE_FLAG_PAGE     0x08
#define RAM_SAVE_FLAG_EOS      0x10
#define RAM_SAVE_FLAG_CONTINUE 0x20
#define RAM_SAVE_FLAG_XBZRLE   0x40
/* 0x80 is reserved in rdma.h for RAM_SAVE_FLAG_HOOK */
#define RAM_SAVE_FLAG_COMPRESS_PAGE    0x100
#define RAM_SAVE_FLAG_MULTIFD_FLUSH    0x200
/* We can't use any flag that is bigger than 0x200 */

XBZRLECacheStats xbzrle_counters;

/* used by the search for pages to send */
struct PageSearchStatus {
    /* The migration channel used for a specific host page */
    QEMUFile    *pss_channel;
    /* Last block from where we have sent data */
    RAMBlock *last_sent_block;
    /* Current block being searched */
    RAMBlock    *block;
    /* Current page to search from */
    unsigned long page;
    /* Set once we wrap around */
    bool         complete_round;
    /* Whether we're sending a host page */
    bool          host_page_sending;
    /* The start/end of current host page.  Invalid if host_page_sending==false */
    unsigned long host_page_start;
    unsigned long host_page_end;
};
typedef struct PageSearchStatus PageSearchStatus;

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
    if (migrate_xbzrle()) {
        qemu_mutex_lock(&XBZRLE.lock);
    }
}

static void XBZRLE_cache_unlock(void)
{
    if (migrate_xbzrle()) {
        qemu_mutex_unlock(&XBZRLE.lock);
    }
}

/**
 * xbzrle_cache_resize: resize the xbzrle cache
 *
 * This function is called from migrate_params_apply in main
 * thread, possibly while a migration is in progress.  A running
 * migration may be using the cache and might finish during this call,
 * hence changes to the cache are protected by XBZRLE.lock().
 *
 * Returns 0 for success or -1 for error
 *
 * @new_size: new cache size
 * @errp: set *errp if the check failed, with reason
 */
int xbzrle_cache_resize(uint64_t new_size, Error **errp)
{
    PageCache *new_cache;
    int64_t ret = 0;

    /* Check for truncation */
    if (new_size != (size_t)new_size) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cache size",
                   "exceeding address space");
        return -1;
    }

    if (new_size == migrate_xbzrle_cache_size()) {
        /* nothing to do */
        return 0;
    }

    XBZRLE_cache_lock();

    if (XBZRLE.cache != NULL) {
        new_cache = cache_init(new_size, TARGET_PAGE_SIZE, errp);
        if (!new_cache) {
            ret = -1;
            goto out;
        }

        cache_fini(XBZRLE.cache);
        XBZRLE.cache = new_cache;
    }
out:
    XBZRLE_cache_unlock();
    return ret;
}

static bool postcopy_preempt_active(void)
{
    return migrate_postcopy_preempt() && migration_in_postcopy();
}

bool migrate_ram_is_ignored(RAMBlock *block)
{
    return !qemu_ram_is_migratable(block) ||
           (migrate_ignore_shared() && qemu_ram_is_shared(block)
                                    && qemu_ram_is_named_file(block));
}

#undef RAMBLOCK_FOREACH

int foreach_not_ignored_block(RAMBlockIterFunc func, void *opaque)
{
    RAMBlock *block;
    int ret = 0;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        ret = func(block, opaque);
        if (ret) {
            break;
        }
    }
    return ret;
}

static void ramblock_recv_map_init(void)
{
    RAMBlock *rb;

    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
        assert(!rb->receivedmap);
        rb->receivedmap = bitmap_new(rb->max_length >> qemu_target_page_bits());
    }
}

int ramblock_recv_bitmap_test(RAMBlock *rb, void *host_addr)
{
    return test_bit(ramblock_recv_bitmap_offset(host_addr, rb),
                    rb->receivedmap);
}

bool ramblock_recv_bitmap_test_byte_offset(RAMBlock *rb, uint64_t byte_offset)
{
    return test_bit(byte_offset >> TARGET_PAGE_BITS, rb->receivedmap);
}

void ramblock_recv_bitmap_set(RAMBlock *rb, void *host_addr)
{
    set_bit_atomic(ramblock_recv_bitmap_offset(host_addr, rb), rb->receivedmap);
}

void ramblock_recv_bitmap_set_range(RAMBlock *rb, void *host_addr,
                                    size_t nr)
{
    bitmap_set_atomic(rb->receivedmap,
                      ramblock_recv_bitmap_offset(host_addr, rb),
                      nr);
}

#define  RAMBLOCK_RECV_BITMAP_ENDING  (0x0123456789abcdefULL)

/*
 * Format: bitmap_size (8 bytes) + whole_bitmap (N bytes).
 *
 * Returns >0 if success with sent bytes, or <0 if error.
 */
int64_t ramblock_recv_bitmap_send(QEMUFile *file,
                                  const char *block_name)
{
    RAMBlock *block = qemu_ram_block_by_name(block_name);
    unsigned long *le_bitmap, nbits;
    uint64_t size;

    if (!block) {
        error_report("%s: invalid block name: %s", __func__, block_name);
        return -1;
    }

    nbits = block->postcopy_length >> TARGET_PAGE_BITS;

    /*
     * Make sure the tmp bitmap buffer is big enough, e.g., on 32bit
     * machines we may need 4 more bytes for padding (see below
     * comment). So extend it a bit before hand.
     */
    le_bitmap = bitmap_new(nbits + BITS_PER_LONG);

    /*
     * Always use little endian when sending the bitmap. This is
     * required that when source and destination VMs are not using the
     * same endianness. (Note: big endian won't work.)
     */
    bitmap_to_le(le_bitmap, block->receivedmap, nbits);

    /* Size of the bitmap, in bytes */
    size = DIV_ROUND_UP(nbits, 8);

    /*
     * size is always aligned to 8 bytes for 64bit machines, but it
     * may not be true for 32bit machines. We need this padding to
     * make sure the migration can survive even between 32bit and
     * 64bit machines.
     */
    size = ROUND_UP(size, 8);

    qemu_put_be64(file, size);
    qemu_put_buffer(file, (const uint8_t *)le_bitmap, size);
    g_free(le_bitmap);
    /*
     * Mark as an end, in case the middle part is screwed up due to
     * some "mysterious" reason.
     */
    qemu_put_be64(file, RAMBLOCK_RECV_BITMAP_ENDING);
    int ret = qemu_fflush(file);
    if (ret) {
        return ret;
    }

    return size + sizeof(size);
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
    /*
     * PageSearchStatus structures for the channels when send pages.
     * Protected by the bitmap_mutex.
     */
    PageSearchStatus pss[RAM_CHANNEL_MAX];
    /* UFFD file descriptor, used in 'write-tracking' migration */
    int uffdio_fd;
    /* total ram size in bytes */
    uint64_t ram_bytes_total;
    /* Last block that we have visited searching for dirty pages */
    RAMBlock *last_seen_block;
    /* Last dirty target page we have sent */
    ram_addr_t last_page;
    /* last ram version we have seen */
    uint32_t last_version;
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
    /* Amount of xbzrle pages since the beginning of the period */
    uint64_t xbzrle_pages_prev;
    /* Amount of xbzrle encoded bytes since the beginning of the period */
    uint64_t xbzrle_bytes_prev;
    /* Are we really using XBZRLE (e.g., after the first round). */
    bool xbzrle_started;
    /* Are we on the last stage of migration */
    bool last_stage;

    /* total handled target pages at the beginning of period */
    uint64_t target_page_count_prev;
    /* total handled target pages since start */
    uint64_t target_page_count;
    /* number of dirty bits in the bitmap */
    uint64_t migration_dirty_pages;
    /*
     * Protects:
     * - dirty/clear bitmap
     * - migration_dirty_pages
     * - pss structures
     */
    QemuMutex bitmap_mutex;
    /* The RAMBlock used in the last src_page_requests */
    RAMBlock *last_req_rb;
    /* Queue of outstanding page requests from the destination */
    QemuMutex src_page_req_mutex;
    QSIMPLEQ_HEAD(, RAMSrcPageRequest) src_page_requests;

    /*
     * This is only used when postcopy is in recovery phase, to communicate
     * between the migration thread and the return path thread on dirty
     * bitmap synchronizations.  This field is unused in other stages of
     * RAM migration.
     */
    unsigned int postcopy_bmap_sync_requested;
};
typedef struct RAMState RAMState;

static RAMState *ram_state;

static NotifierWithReturnList precopy_notifier_list;

/* Whether postcopy has queued requests? */
static bool postcopy_has_request(RAMState *rs)
{
    return !QSIMPLEQ_EMPTY_ATOMIC(&rs->src_page_requests);
}

void precopy_infrastructure_init(void)
{
    notifier_with_return_list_init(&precopy_notifier_list);
}

void precopy_add_notifier(NotifierWithReturn *n)
{
    notifier_with_return_list_add(&precopy_notifier_list, n);
}

void precopy_remove_notifier(NotifierWithReturn *n)
{
    notifier_with_return_remove(n);
}

int precopy_notify(PrecopyNotifyReason reason, Error **errp)
{
    PrecopyNotifyData pnd;
    pnd.reason = reason;
    pnd.errp = errp;

    return notifier_with_return_list_notify(&precopy_notifier_list, &pnd);
}

uint64_t ram_bytes_remaining(void)
{
    return ram_state ? (ram_state->migration_dirty_pages * TARGET_PAGE_SIZE) :
                       0;
}

void ram_transferred_add(uint64_t bytes)
{
    if (runstate_is_running()) {
        stat64_add(&mig_stats.precopy_bytes, bytes);
    } else if (migration_in_postcopy()) {
        stat64_add(&mig_stats.postcopy_bytes, bytes);
    } else {
        stat64_add(&mig_stats.downtime_bytes, bytes);
    }
}

struct MigrationOps {
    int (*ram_save_target_page)(RAMState *rs, PageSearchStatus *pss);
};
typedef struct MigrationOps MigrationOps;

MigrationOps *migration_ops;

static int ram_save_host_page_urgent(PageSearchStatus *pss);

/* NOTE: page is the PFN not real ram_addr_t. */
static void pss_init(PageSearchStatus *pss, RAMBlock *rb, ram_addr_t page)
{
    pss->block = rb;
    pss->page = page;
    pss->complete_round = false;
}

/*
 * Check whether two PSSs are actively sending the same page.  Return true
 * if it is, false otherwise.
 */
static bool pss_overlap(PageSearchStatus *pss1, PageSearchStatus *pss2)
{
    return pss1->host_page_sending && pss2->host_page_sending &&
        (pss1->host_page_start == pss2->host_page_start);
}

/**
 * save_page_header: write page header to wire
 *
 * If this is the 1st block, it also writes the block identification
 *
 * Returns the number of bytes written
 *
 * @pss: current PSS channel status
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 *          in the lower bits, it contains flags
 */
static size_t save_page_header(PageSearchStatus *pss, QEMUFile *f,
                               RAMBlock *block, ram_addr_t offset)
{
    size_t size, len;
    bool same_block = (block == pss->last_sent_block);

    if (same_block) {
        offset |= RAM_SAVE_FLAG_CONTINUE;
    }
    qemu_put_be64(f, offset);
    size = 8;

    if (!same_block) {
        len = strlen(block->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)block->idstr, len);
        size += 1 + len;
        pss->last_sent_block = block;
    }
    return size;
}

/**
 * mig_throttle_guest_down: throttle down the guest
 *
 * Reduce amount of guest cpu execution to hopefully slow down memory
 * writes. If guest dirty memory rate is reduced below the rate at
 * which we can transfer pages to the destination then we should be
 * able to complete migration. Some workloads dirty memory way too
 * fast and will not effectively converge, even with auto-converge.
 */
static void mig_throttle_guest_down(uint64_t bytes_dirty_period,
                                    uint64_t bytes_dirty_threshold)
{
    uint64_t pct_initial = migrate_cpu_throttle_initial();
    uint64_t pct_increment = migrate_cpu_throttle_increment();
    bool pct_tailslow = migrate_cpu_throttle_tailslow();
    int pct_max = migrate_max_cpu_throttle();

    uint64_t throttle_now = cpu_throttle_get_percentage();
    uint64_t cpu_now, cpu_ideal, throttle_inc;

    /* We have not started throttling yet. Let's start it. */
    if (!cpu_throttle_active()) {
        cpu_throttle_set(pct_initial);
    } else {
        /* Throttling already on, just increase the rate */
        if (!pct_tailslow) {
            throttle_inc = pct_increment;
        } else {
            /* Compute the ideal CPU percentage used by Guest, which may
             * make the dirty rate match the dirty rate threshold. */
            cpu_now = 100 - throttle_now;
            cpu_ideal = cpu_now * (bytes_dirty_threshold * 1.0 /
                        bytes_dirty_period);
            throttle_inc = MIN(cpu_now - cpu_ideal, pct_increment);
        }
        cpu_throttle_set(MIN(throttle_now + throttle_inc, pct_max));
    }
}

void mig_throttle_counter_reset(void)
{
    RAMState *rs = ram_state;

    rs->time_last_bitmap_sync = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    rs->num_dirty_pages_period = 0;
    rs->bytes_xfer_prev = migration_transferred_bytes();
}

/**
 * xbzrle_cache_zero_page: insert a zero page in the XBZRLE cache
 *
 * @current_addr: address for the zero page
 *
 * Update the xbzrle cache to reflect a page that's been sent as all 0.
 * The important thing is that a stale (not-yet-0'd) page be replaced
 * by the new data.
 * As a bonus, if the page wasn't in the cache it gets added so that
 * when a small write is made into the 0'd page it gets XBZRLE sent.
 */
static void xbzrle_cache_zero_page(ram_addr_t current_addr)
{
    /* We don't care if this fails to allocate a new cache page
     * as long as it updated an old one */
    cache_insert(XBZRLE.cache, current_addr, XBZRLE.zero_target_page,
                 stat64_get(&mig_stats.dirty_sync_count));
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
 * @pss: current PSS channel
 * @current_data: pointer to the address of the page contents
 * @current_addr: addr of the page
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 */
static int save_xbzrle_page(RAMState *rs, PageSearchStatus *pss,
                            uint8_t **current_data, ram_addr_t current_addr,
                            RAMBlock *block, ram_addr_t offset)
{
    int encoded_len = 0, bytes_xbzrle;
    uint8_t *prev_cached_page;
    QEMUFile *file = pss->pss_channel;
    uint64_t generation = stat64_get(&mig_stats.dirty_sync_count);

    if (!cache_is_cached(XBZRLE.cache, current_addr, generation)) {
        xbzrle_counters.cache_miss++;
        if (!rs->last_stage) {
            if (cache_insert(XBZRLE.cache, current_addr, *current_data,
                             generation) == -1) {
                return -1;
            } else {
                /* update *current_data when the page has been
                   inserted into cache */
                *current_data = get_cached_data(XBZRLE.cache, current_addr);
            }
        }
        return -1;
    }

    /*
     * Reaching here means the page has hit the xbzrle cache, no matter what
     * encoding result it is (normal encoding, overflow or skipping the page),
     * count the page as encoded. This is used to calculate the encoding rate.
     *
     * Example: 2 pages (8KB) being encoded, first page encoding generates 2KB,
     * 2nd page turns out to be skipped (i.e. no new bytes written to the
     * page), the overall encoding rate will be 8KB / 2KB = 4, which has the
     * skipped page included. In this way, the encoding rate can tell if the
     * guest page is good for xbzrle encoding.
     */
    xbzrle_counters.pages++;
    prev_cached_page = get_cached_data(XBZRLE.cache, current_addr);

    /* save current buffer into memory */
    memcpy(XBZRLE.current_buf, *current_data, TARGET_PAGE_SIZE);

    /* XBZRLE encoding (if there is no overflow) */
    encoded_len = xbzrle_encode_buffer(prev_cached_page, XBZRLE.current_buf,
                                       TARGET_PAGE_SIZE, XBZRLE.encoded_buf,
                                       TARGET_PAGE_SIZE);

    /*
     * Update the cache contents, so that it corresponds to the data
     * sent, in all cases except where we skip the page.
     */
    if (!rs->last_stage && encoded_len != 0) {
        memcpy(prev_cached_page, XBZRLE.current_buf, TARGET_PAGE_SIZE);
        /*
         * In the case where we couldn't compress, ensure that the caller
         * sends the data from the cache, since the guest might have
         * changed the RAM since we copied it.
         */
        *current_data = prev_cached_page;
    }

    if (encoded_len == 0) {
        trace_save_xbzrle_page_skipping();
        return 0;
    } else if (encoded_len == -1) {
        trace_save_xbzrle_page_overflow();
        xbzrle_counters.overflow++;
        xbzrle_counters.bytes += TARGET_PAGE_SIZE;
        return -1;
    }

    /* Send XBZRLE based compressed page */
    bytes_xbzrle = save_page_header(pss, pss->pss_channel, block,
                                    offset | RAM_SAVE_FLAG_XBZRLE);
    qemu_put_byte(file, ENCODING_FLAG_XBZRLE);
    qemu_put_be16(file, encoded_len);
    qemu_put_buffer(file, XBZRLE.encoded_buf, encoded_len);
    bytes_xbzrle += encoded_len + 1 + 2;
    /*
     * Like compressed_size (please see update_compress_thread_counts),
     * the xbzrle encoded bytes don't count the 8 byte header with
     * RAM_SAVE_FLAG_CONTINUE.
     */
    xbzrle_counters.bytes += bytes_xbzrle - 8;
    ram_transferred_add(bytes_xbzrle);

    return 1;
}

/**
 * pss_find_next_dirty: find the next dirty page of current ramblock
 *
 * This function updates pss->page to point to the next dirty page index
 * within the ramblock to migrate, or the end of ramblock when nothing
 * found.  Note that when pss->host_page_sending==true it means we're
 * during sending a host page, so we won't look for dirty page that is
 * outside the host page boundary.
 *
 * @pss: the current page search status
 */
static void pss_find_next_dirty(PageSearchStatus *pss)
{
    RAMBlock *rb = pss->block;
    unsigned long size = rb->used_length >> TARGET_PAGE_BITS;
    unsigned long *bitmap = rb->bmap;

    if (migrate_ram_is_ignored(rb)) {
        /* Points directly to the end, so we know no dirty page */
        pss->page = size;
        return;
    }

    /*
     * If during sending a host page, only look for dirty pages within the
     * current host page being send.
     */
    if (pss->host_page_sending) {
        assert(pss->host_page_end);
        size = MIN(size, pss->host_page_end);
    }

    pss->page = find_next_bit(bitmap, size, pss->page);
}

static void migration_clear_memory_region_dirty_bitmap(RAMBlock *rb,
                                                       unsigned long page)
{
    uint8_t shift;
    hwaddr size, start;

    if (!rb->clear_bmap || !clear_bmap_test_and_clear(rb, page)) {
        return;
    }

    shift = rb->clear_bmap_shift;
    /*
     * CLEAR_BITMAP_SHIFT_MIN should always guarantee this... this
     * can make things easier sometimes since then start address
     * of the small chunk will always be 64 pages aligned so the
     * bitmap will always be aligned to unsigned long. We should
     * even be able to remove this restriction but I'm simply
     * keeping it.
     */
    assert(shift >= 6);

    size = 1ULL << (TARGET_PAGE_BITS + shift);
    start = QEMU_ALIGN_DOWN((ram_addr_t)page << TARGET_PAGE_BITS, size);
    trace_migration_bitmap_clear_dirty(rb->idstr, start, size, page);
    memory_region_clear_dirty_bitmap(rb->mr, start, size);
}

static void
migration_clear_memory_region_dirty_bitmap_range(RAMBlock *rb,
                                                 unsigned long start,
                                                 unsigned long npages)
{
    unsigned long i, chunk_pages = 1UL << rb->clear_bmap_shift;
    unsigned long chunk_start = QEMU_ALIGN_DOWN(start, chunk_pages);
    unsigned long chunk_end = QEMU_ALIGN_UP(start + npages, chunk_pages);

    /*
     * Clear pages from start to start + npages - 1, so the end boundary is
     * exclusive.
     */
    for (i = chunk_start; i < chunk_end; i += chunk_pages) {
        migration_clear_memory_region_dirty_bitmap(rb, i);
    }
}

/*
 * colo_bitmap_find_diry:find contiguous dirty pages from start
 *
 * Returns the page offset within memory region of the start of the contiguout
 * dirty page
 *
 * @rs: current RAM state
 * @rb: RAMBlock where to search for dirty pages
 * @start: page where we start the search
 * @num: the number of contiguous dirty pages
 */
static inline
unsigned long colo_bitmap_find_dirty(RAMState *rs, RAMBlock *rb,
                                     unsigned long start, unsigned long *num)
{
    unsigned long size = rb->used_length >> TARGET_PAGE_BITS;
    unsigned long *bitmap = rb->bmap;
    unsigned long first, next;

    *num = 0;

    if (migrate_ram_is_ignored(rb)) {
        return size;
    }

    first = find_next_bit(bitmap, size, start);
    if (first >= size) {
        return first;
    }
    next = find_next_zero_bit(bitmap, size, first + 1);
    assert(next >= first);
    *num = next - first;
    return first;
}

static inline bool migration_bitmap_clear_dirty(RAMState *rs,
                                                RAMBlock *rb,
                                                unsigned long page)
{
    bool ret;

    /*
     * Clear dirty bitmap if needed.  This _must_ be called before we
     * send any of the page in the chunk because we need to make sure
     * we can capture further page content changes when we sync dirty
     * log the next time.  So as long as we are going to send any of
     * the page in the chunk we clear the remote dirty bitmap for all.
     * Clearing it earlier won't be a problem, but too late will.
     */
    migration_clear_memory_region_dirty_bitmap(rb, page);

    ret = test_and_clear_bit(page, rb->bmap);
    if (ret) {
        rs->migration_dirty_pages--;
    }

    return ret;
}

static void dirty_bitmap_clear_section(MemoryRegionSection *section,
                                       void *opaque)
{
    const hwaddr offset = section->offset_within_region;
    const hwaddr size = int128_get64(section->size);
    const unsigned long start = offset >> TARGET_PAGE_BITS;
    const unsigned long npages = size >> TARGET_PAGE_BITS;
    RAMBlock *rb = section->mr->ram_block;
    uint64_t *cleared_bits = opaque;

    /*
     * We don't grab ram_state->bitmap_mutex because we expect to run
     * only when starting migration or during postcopy recovery where
     * we don't have concurrent access.
     */
    if (!migration_in_postcopy() && !migrate_background_snapshot()) {
        migration_clear_memory_region_dirty_bitmap_range(rb, start, npages);
    }
    *cleared_bits += bitmap_count_one_with_offset(rb->bmap, start, npages);
    bitmap_clear(rb->bmap, start, npages);
}

/*
 * Exclude all dirty pages from migration that fall into a discarded range as
 * managed by a RamDiscardManager responsible for the mapped memory region of
 * the RAMBlock. Clear the corresponding bits in the dirty bitmaps.
 *
 * Discarded pages ("logically unplugged") have undefined content and must
 * not get migrated, because even reading these pages for migration might
 * result in undesired behavior.
 *
 * Returns the number of cleared bits in the RAMBlock dirty bitmap.
 *
 * Note: The result is only stable while migrating (precopy/postcopy).
 */
static uint64_t ramblock_dirty_bitmap_clear_discarded_pages(RAMBlock *rb)
{
    uint64_t cleared_bits = 0;

    if (rb->mr && rb->bmap && memory_region_has_ram_discard_manager(rb->mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(rb->mr);
        MemoryRegionSection section = {
            .mr = rb->mr,
            .offset_within_region = 0,
            .size = int128_make64(qemu_ram_get_used_length(rb)),
        };

        ram_discard_manager_replay_discarded(rdm, &section,
                                             dirty_bitmap_clear_section,
                                             &cleared_bits);
    }
    return cleared_bits;
}

/*
 * Check if a host-page aligned page falls into a discarded range as managed by
 * a RamDiscardManager responsible for the mapped memory region of the RAMBlock.
 *
 * Note: The result is only stable while migrating (precopy/postcopy).
 */
bool ramblock_page_is_discarded(RAMBlock *rb, ram_addr_t start)
{
    if (rb->mr && memory_region_has_ram_discard_manager(rb->mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(rb->mr);
        MemoryRegionSection section = {
            .mr = rb->mr,
            .offset_within_region = start,
            .size = int128_make64(qemu_ram_pagesize(rb)),
        };

        return !ram_discard_manager_is_populated(rdm, &section);
    }
    return false;
}

/* Called with RCU critical section */
static void ramblock_sync_dirty_bitmap(RAMState *rs, RAMBlock *rb)
{
    uint64_t new_dirty_pages =
        cpu_physical_memory_sync_dirty_bitmap(rb, 0, rb->used_length);

    rs->migration_dirty_pages += new_dirty_pages;
    rs->num_dirty_pages_period += new_dirty_pages;
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

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        summary |= block->page_size;
    }

    return summary;
}

uint64_t ram_get_total_transferred_pages(void)
{
    return stat64_get(&mig_stats.normal_pages) +
        stat64_get(&mig_stats.zero_pages) +
        compress_ram_pages() + xbzrle_counters.pages;
}

static void migration_update_rates(RAMState *rs, int64_t end_time)
{
    uint64_t page_count = rs->target_page_count - rs->target_page_count_prev;

    /* calculate period counters */
    stat64_set(&mig_stats.dirty_pages_rate,
               rs->num_dirty_pages_period * 1000 /
               (end_time - rs->time_last_bitmap_sync));

    if (!page_count) {
        return;
    }

    if (migrate_xbzrle()) {
        double encoded_size, unencoded_size;

        xbzrle_counters.cache_miss_rate = (double)(xbzrle_counters.cache_miss -
            rs->xbzrle_cache_miss_prev) / page_count;
        rs->xbzrle_cache_miss_prev = xbzrle_counters.cache_miss;
        unencoded_size = (xbzrle_counters.pages - rs->xbzrle_pages_prev) *
                         TARGET_PAGE_SIZE;
        encoded_size = xbzrle_counters.bytes - rs->xbzrle_bytes_prev;
        if (xbzrle_counters.pages == rs->xbzrle_pages_prev || !encoded_size) {
            xbzrle_counters.encoding_rate = 0;
        } else {
            xbzrle_counters.encoding_rate = unencoded_size / encoded_size;
        }
        rs->xbzrle_pages_prev = xbzrle_counters.pages;
        rs->xbzrle_bytes_prev = xbzrle_counters.bytes;
    }
    compress_update_rates(page_count);
}

/*
 * Enable dirty-limit to throttle down the guest
 */
static void migration_dirty_limit_guest(void)
{
    /*
     * dirty page rate quota for all vCPUs fetched from
     * migration parameter 'vcpu_dirty_limit'
     */
    static int64_t quota_dirtyrate;
    MigrationState *s = migrate_get_current();

    /*
     * If dirty limit already enabled and migration parameter
     * vcpu-dirty-limit untouched.
     */
    if (dirtylimit_in_service() &&
        quota_dirtyrate == s->parameters.vcpu_dirty_limit) {
        return;
    }

    quota_dirtyrate = s->parameters.vcpu_dirty_limit;

    /*
     * Set all vCPU a quota dirtyrate, note that the second
     * parameter will be ignored if setting all vCPU for the vm
     */
    qmp_set_vcpu_dirty_limit(false, -1, quota_dirtyrate, NULL);
    trace_migration_dirty_limit_guest(quota_dirtyrate);
}

static void migration_trigger_throttle(RAMState *rs)
{
    uint64_t threshold = migrate_throttle_trigger_threshold();
    uint64_t bytes_xfer_period =
        migration_transferred_bytes() - rs->bytes_xfer_prev;
    uint64_t bytes_dirty_period = rs->num_dirty_pages_period * TARGET_PAGE_SIZE;
    uint64_t bytes_dirty_threshold = bytes_xfer_period * threshold / 100;

    /* During block migration the auto-converge logic incorrectly detects
     * that ram migration makes no progress. Avoid this by disabling the
     * throttling logic during the bulk phase of block migration. */
    if (blk_mig_bulk_active()) {
        return;
    }

    /*
     * The following detection logic can be refined later. For now:
     * Check to see if the ratio between dirtied bytes and the approx.
     * amount of bytes that just got transferred since the last time
     * we were in this routine reaches the threshold. If that happens
     * twice, start or increase throttling.
     */
    if ((bytes_dirty_period > bytes_dirty_threshold) &&
        (++rs->dirty_rate_high_cnt >= 2)) {
        rs->dirty_rate_high_cnt = 0;
        if (migrate_auto_converge()) {
            trace_migration_throttle();
            mig_throttle_guest_down(bytes_dirty_period,
                                    bytes_dirty_threshold);
        } else if (migrate_dirty_limit()) {
            migration_dirty_limit_guest();
        }
    }
}

static void migration_bitmap_sync(RAMState *rs, bool last_stage)
{
    RAMBlock *block;
    int64_t end_time;

    stat64_add(&mig_stats.dirty_sync_count, 1);

    if (!rs->time_last_bitmap_sync) {
        rs->time_last_bitmap_sync = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    }

    trace_migration_bitmap_sync_start();
    memory_global_dirty_log_sync(last_stage);

    qemu_mutex_lock(&rs->bitmap_mutex);
    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            ramblock_sync_dirty_bitmap(rs, block);
        }
        stat64_set(&mig_stats.dirty_bytes_last_sync, ram_bytes_remaining());
    }
    qemu_mutex_unlock(&rs->bitmap_mutex);

    memory_global_after_dirty_log_sync();
    trace_migration_bitmap_sync_end(rs->num_dirty_pages_period);

    end_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    /* more than 1 second = 1000 millisecons */
    if (end_time > rs->time_last_bitmap_sync + 1000) {
        migration_trigger_throttle(rs);

        migration_update_rates(rs, end_time);

        rs->target_page_count_prev = rs->target_page_count;

        /* reset period counters */
        rs->time_last_bitmap_sync = end_time;
        rs->num_dirty_pages_period = 0;
        rs->bytes_xfer_prev = migration_transferred_bytes();
    }
    if (migrate_events()) {
        uint64_t generation = stat64_get(&mig_stats.dirty_sync_count);
        qapi_event_send_migration_pass(generation);
    }
}

static void migration_bitmap_sync_precopy(RAMState *rs, bool last_stage)
{
    Error *local_err = NULL;

    /*
     * The current notifier usage is just an optimization to migration, so we
     * don't stop the normal migration process in the error case.
     */
    if (precopy_notify(PRECOPY_NOTIFY_BEFORE_BITMAP_SYNC, &local_err)) {
        error_report_err(local_err);
        local_err = NULL;
    }

    migration_bitmap_sync(rs, last_stage);

    if (precopy_notify(PRECOPY_NOTIFY_AFTER_BITMAP_SYNC, &local_err)) {
        error_report_err(local_err);
    }
}

void ram_release_page(const char *rbname, uint64_t offset)
{
    if (!migrate_release_ram() || !migration_in_postcopy()) {
        return;
    }

    ram_discard_range(rbname, offset, TARGET_PAGE_SIZE);
}

/**
 * save_zero_page: send the zero page to the stream
 *
 * Returns the number of pages written.
 *
 * @rs: current RAM state
 * @pss: current PSS channel
 * @offset: offset inside the block for the page
 */
static int save_zero_page(RAMState *rs, PageSearchStatus *pss,
                          ram_addr_t offset)
{
    uint8_t *p = pss->block->host + offset;
    QEMUFile *file = pss->pss_channel;
    int len = 0;

    if (!buffer_is_zero(p, TARGET_PAGE_SIZE)) {
        return 0;
    }

    len += save_page_header(pss, file, pss->block, offset | RAM_SAVE_FLAG_ZERO);
    qemu_put_byte(file, 0);
    len += 1;
    ram_release_page(pss->block->idstr, offset);

    stat64_add(&mig_stats.zero_pages, 1);
    ram_transferred_add(len);

    /*
     * Must let xbzrle know, otherwise a previous (now 0'd) cached
     * page would be stale.
     */
    if (rs->xbzrle_started) {
        XBZRLE_cache_lock();
        xbzrle_cache_zero_page(pss->block->offset + offset);
        XBZRLE_cache_unlock();
    }

    return len;
}

/*
 * @pages: the number of pages written by the control path,
 *        < 0 - error
 *        > 0 - number of pages written
 *
 * Return true if the pages has been saved, otherwise false is returned.
 */
static bool control_save_page(PageSearchStatus *pss,
                              ram_addr_t offset, int *pages)
{
    int ret;

    ret = rdma_control_save_page(pss->pss_channel, pss->block->offset, offset,
                                 TARGET_PAGE_SIZE);
    if (ret == RAM_SAVE_CONTROL_NOT_SUPP) {
        return false;
    }

    if (ret == RAM_SAVE_CONTROL_DELAYED) {
        *pages = 1;
        return true;
    }
    *pages = ret;
    return true;
}

/*
 * directly send the page to the stream
 *
 * Returns the number of pages written.
 *
 * @pss: current PSS channel
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @buf: the page to be sent
 * @async: send to page asyncly
 */
static int save_normal_page(PageSearchStatus *pss, RAMBlock *block,
                            ram_addr_t offset, uint8_t *buf, bool async)
{
    QEMUFile *file = pss->pss_channel;

    ram_transferred_add(save_page_header(pss, pss->pss_channel, block,
                                         offset | RAM_SAVE_FLAG_PAGE));
    if (async) {
        qemu_put_buffer_async(file, buf, TARGET_PAGE_SIZE,
                              migrate_release_ram() &&
                              migration_in_postcopy());
    } else {
        qemu_put_buffer(file, buf, TARGET_PAGE_SIZE);
    }
    ram_transferred_add(TARGET_PAGE_SIZE);
    stat64_add(&mig_stats.normal_pages, 1);
    return 1;
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
 */
static int ram_save_page(RAMState *rs, PageSearchStatus *pss)
{
    int pages = -1;
    uint8_t *p;
    bool send_async = true;
    RAMBlock *block = pss->block;
    ram_addr_t offset = ((ram_addr_t)pss->page) << TARGET_PAGE_BITS;
    ram_addr_t current_addr = block->offset + offset;

    p = block->host + offset;
    trace_ram_save_page(block->idstr, (uint64_t)offset, p);

    XBZRLE_cache_lock();
    if (rs->xbzrle_started && !migration_in_postcopy()) {
        pages = save_xbzrle_page(rs, pss, &p, current_addr,
                                 block, offset);
        if (!rs->last_stage) {
            /* Can't send this cached data async, since the cache page
             * might get updated before it gets to the wire
             */
            send_async = false;
        }
    }

    /* XBZRLE overflow or normal page */
    if (pages == -1) {
        pages = save_normal_page(pss, block, offset, p, send_async);
    }

    XBZRLE_cache_unlock();

    return pages;
}

static int ram_save_multifd_page(QEMUFile *file, RAMBlock *block,
                                 ram_addr_t offset)
{
    if (multifd_queue_page(file, block, offset) < 0) {
        return -1;
    }
    stat64_add(&mig_stats.normal_pages, 1);

    return 1;
}

int compress_send_queued_data(CompressParam *param)
{
    PageSearchStatus *pss = &ram_state->pss[RAM_CHANNEL_PRECOPY];
    MigrationState *ms = migrate_get_current();
    QEMUFile *file = ms->to_dst_file;
    int len = 0;

    RAMBlock *block = param->block;
    ram_addr_t offset = param->offset;

    if (param->result == RES_NONE) {
        return 0;
    }

    assert(block == pss->last_sent_block);

    if (param->result == RES_ZEROPAGE) {
        assert(qemu_file_buffer_empty(param->file));
        len += save_page_header(pss, file, block, offset | RAM_SAVE_FLAG_ZERO);
        qemu_put_byte(file, 0);
        len += 1;
        ram_release_page(block->idstr, offset);
    } else if (param->result == RES_COMPRESS) {
        assert(!qemu_file_buffer_empty(param->file));
        len += save_page_header(pss, file, block,
                                offset | RAM_SAVE_FLAG_COMPRESS_PAGE);
        len += qemu_put_qemu_file(file, param->file);
    } else {
        abort();
    }

    update_compress_thread_counts(param, len);

    return len;
}

#define PAGE_ALL_CLEAN 0
#define PAGE_TRY_AGAIN 1
#define PAGE_DIRTY_FOUND 2
/**
 * find_dirty_block: find the next dirty page and update any state
 * associated with the search process.
 *
 * Returns:
 *         <0: An error happened
 *         PAGE_ALL_CLEAN: no dirty page found, give up
 *         PAGE_TRY_AGAIN: no dirty page found, retry for next block
 *         PAGE_DIRTY_FOUND: dirty page found
 *
 * @rs: current RAM state
 * @pss: data about the state of the current dirty page scan
 * @again: set to false if the search has scanned the whole of RAM
 */
static int find_dirty_block(RAMState *rs, PageSearchStatus *pss)
{
    /* Update pss->page for the next dirty bit in ramblock */
    pss_find_next_dirty(pss);

    if (pss->complete_round && pss->block == rs->last_seen_block &&
        pss->page >= rs->last_page) {
        /*
         * We've been once around the RAM and haven't found anything.
         * Give up.
         */
        return PAGE_ALL_CLEAN;
    }
    if (!offset_in_ramblock(pss->block,
                            ((ram_addr_t)pss->page) << TARGET_PAGE_BITS)) {
        /* Didn't find anything in this RAM Block */
        pss->page = 0;
        pss->block = QLIST_NEXT_RCU(pss->block, next);
        if (!pss->block) {
            if (migrate_multifd() &&
                !migrate_multifd_flush_after_each_section()) {
                QEMUFile *f = rs->pss[RAM_CHANNEL_PRECOPY].pss_channel;
                int ret = multifd_send_sync_main(f);
                if (ret < 0) {
                    return ret;
                }
                qemu_put_be64(f, RAM_SAVE_FLAG_MULTIFD_FLUSH);
                qemu_fflush(f);
            }
            /*
             * If memory migration starts over, we will meet a dirtied page
             * which may still exists in compression threads's ring, so we
             * should flush the compressed data to make sure the new page
             * is not overwritten by the old one in the destination.
             *
             * Also If xbzrle is on, stop using the data compression at this
             * point. In theory, xbzrle can do better than compression.
             */
            compress_flush_data();

            /* Hit the end of the list */
            pss->block = QLIST_FIRST_RCU(&ram_list.blocks);
            /* Flag that we've looped */
            pss->complete_round = true;
            /* After the first round, enable XBZRLE. */
            if (migrate_xbzrle()) {
                rs->xbzrle_started = true;
            }
        }
        /* Didn't find anything this time, but try again on the new block */
        return PAGE_TRY_AGAIN;
    } else {
        /* We've found something */
        return PAGE_DIRTY_FOUND;
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
    struct RAMSrcPageRequest *entry;
    RAMBlock *block = NULL;

    if (!postcopy_has_request(rs)) {
        return NULL;
    }

    QEMU_LOCK_GUARD(&rs->src_page_req_mutex);

    /*
     * This should _never_ change even after we take the lock, because no one
     * should be taking anything off the request list other than us.
     */
    assert(postcopy_has_request(rs));

    entry = QSIMPLEQ_FIRST(&rs->src_page_requests);
    block = entry->rb;
    *offset = entry->offset;

    if (entry->len > TARGET_PAGE_SIZE) {
        entry->len -= TARGET_PAGE_SIZE;
        entry->offset += TARGET_PAGE_SIZE;
    } else {
        memory_region_unref(block->mr);
        QSIMPLEQ_REMOVE_HEAD(&rs->src_page_requests, next_req);
        g_free(entry);
        migration_consume_urgent_request();
    }

    return block;
}

#if defined(__linux__)
/**
 * poll_fault_page: try to get next UFFD write fault page and, if pending fault
 *   is found, return RAM block pointer and page offset
 *
 * Returns pointer to the RAMBlock containing faulting page,
 *   NULL if no write faults are pending
 *
 * @rs: current RAM state
 * @offset: page offset from the beginning of the block
 */
static RAMBlock *poll_fault_page(RAMState *rs, ram_addr_t *offset)
{
    struct uffd_msg uffd_msg;
    void *page_address;
    RAMBlock *block;
    int res;

    if (!migrate_background_snapshot()) {
        return NULL;
    }

    res = uffd_read_events(rs->uffdio_fd, &uffd_msg, 1);
    if (res <= 0) {
        return NULL;
    }

    page_address = (void *)(uintptr_t) uffd_msg.arg.pagefault.address;
    block = qemu_ram_block_from_host(page_address, false, offset);
    assert(block && (block->flags & RAM_UF_WRITEPROTECT) != 0);
    return block;
}

/**
 * ram_save_release_protection: release UFFD write protection after
 *   a range of pages has been saved
 *
 * @rs: current RAM state
 * @pss: page-search-status structure
 * @start_page: index of the first page in the range relative to pss->block
 *
 * Returns 0 on success, negative value in case of an error
*/
static int ram_save_release_protection(RAMState *rs, PageSearchStatus *pss,
        unsigned long start_page)
{
    int res = 0;

    /* Check if page is from UFFD-managed region. */
    if (pss->block->flags & RAM_UF_WRITEPROTECT) {
        void *page_address = pss->block->host + (start_page << TARGET_PAGE_BITS);
        uint64_t run_length = (pss->page - start_page) << TARGET_PAGE_BITS;

        /* Flush async buffers before un-protect. */
        qemu_fflush(pss->pss_channel);
        /* Un-protect memory range. */
        res = uffd_change_protection(rs->uffdio_fd, page_address, run_length,
                false, false);
    }

    return res;
}

/* ram_write_tracking_available: check if kernel supports required UFFD features
 *
 * Returns true if supports, false otherwise
 */
bool ram_write_tracking_available(void)
{
    uint64_t uffd_features;
    int res;

    res = uffd_query_features(&uffd_features);
    return (res == 0 &&
            (uffd_features & UFFD_FEATURE_PAGEFAULT_FLAG_WP) != 0);
}

/* ram_write_tracking_compatible: check if guest configuration is
 *   compatible with 'write-tracking'
 *
 * Returns true if compatible, false otherwise
 */
bool ram_write_tracking_compatible(void)
{
    const uint64_t uffd_ioctls_mask = BIT(_UFFDIO_WRITEPROTECT);
    int uffd_fd;
    RAMBlock *block;
    bool ret = false;

    /* Open UFFD file descriptor */
    uffd_fd = uffd_create_fd(UFFD_FEATURE_PAGEFAULT_FLAG_WP, false);
    if (uffd_fd < 0) {
        return false;
    }

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        uint64_t uffd_ioctls;

        /* Nothing to do with read-only and MMIO-writable regions */
        if (block->mr->readonly || block->mr->rom_device) {
            continue;
        }
        /* Try to register block memory via UFFD-IO to track writes */
        if (uffd_register_memory(uffd_fd, block->host, block->max_length,
                UFFDIO_REGISTER_MODE_WP, &uffd_ioctls)) {
            goto out;
        }
        if ((uffd_ioctls & uffd_ioctls_mask) != uffd_ioctls_mask) {
            goto out;
        }
    }
    ret = true;

out:
    uffd_close_fd(uffd_fd);
    return ret;
}

static inline void populate_read_range(RAMBlock *block, ram_addr_t offset,
                                       ram_addr_t size)
{
    const ram_addr_t end = offset + size;

    /*
     * We read one byte of each page; this will preallocate page tables if
     * required and populate the shared zeropage on MAP_PRIVATE anonymous memory
     * where no page was populated yet. This might require adaption when
     * supporting other mappings, like shmem.
     */
    for (; offset < end; offset += block->page_size) {
        char tmp = *((char *)block->host + offset);

        /* Don't optimize the read out */
        asm volatile("" : "+r" (tmp));
    }
}

static inline int populate_read_section(MemoryRegionSection *section,
                                        void *opaque)
{
    const hwaddr size = int128_get64(section->size);
    hwaddr offset = section->offset_within_region;
    RAMBlock *block = section->mr->ram_block;

    populate_read_range(block, offset, size);
    return 0;
}

/*
 * ram_block_populate_read: preallocate page tables and populate pages in the
 *   RAM block by reading a byte of each page.
 *
 * Since it's solely used for userfault_fd WP feature, here we just
 *   hardcode page size to qemu_real_host_page_size.
 *
 * @block: RAM block to populate
 */
static void ram_block_populate_read(RAMBlock *rb)
{
    /*
     * Skip populating all pages that fall into a discarded range as managed by
     * a RamDiscardManager responsible for the mapped memory region of the
     * RAMBlock. Such discarded ("logically unplugged") parts of a RAMBlock
     * must not get populated automatically. We don't have to track
     * modifications via userfaultfd WP reliably, because these pages will
     * not be part of the migration stream either way -- see
     * ramblock_dirty_bitmap_exclude_discarded_pages().
     *
     * Note: The result is only stable while migrating (precopy/postcopy).
     */
    if (rb->mr && memory_region_has_ram_discard_manager(rb->mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(rb->mr);
        MemoryRegionSection section = {
            .mr = rb->mr,
            .offset_within_region = 0,
            .size = rb->mr->size,
        };

        ram_discard_manager_replay_populated(rdm, &section,
                                             populate_read_section, NULL);
    } else {
        populate_read_range(rb, 0, rb->used_length);
    }
}

/*
 * ram_write_tracking_prepare: prepare for UFFD-WP memory tracking
 */
void ram_write_tracking_prepare(void)
{
    RAMBlock *block;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        /* Nothing to do with read-only and MMIO-writable regions */
        if (block->mr->readonly || block->mr->rom_device) {
            continue;
        }

        /*
         * Populate pages of the RAM block before enabling userfault_fd
         * write protection.
         *
         * This stage is required since ioctl(UFFDIO_WRITEPROTECT) with
         * UFFDIO_WRITEPROTECT_MODE_WP mode setting would silently skip
         * pages with pte_none() entries in page table.
         */
        ram_block_populate_read(block);
    }
}

static inline int uffd_protect_section(MemoryRegionSection *section,
                                       void *opaque)
{
    const hwaddr size = int128_get64(section->size);
    const hwaddr offset = section->offset_within_region;
    RAMBlock *rb = section->mr->ram_block;
    int uffd_fd = (uintptr_t)opaque;

    return uffd_change_protection(uffd_fd, rb->host + offset, size, true,
                                  false);
}

static int ram_block_uffd_protect(RAMBlock *rb, int uffd_fd)
{
    assert(rb->flags & RAM_UF_WRITEPROTECT);

    /* See ram_block_populate_read() */
    if (rb->mr && memory_region_has_ram_discard_manager(rb->mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(rb->mr);
        MemoryRegionSection section = {
            .mr = rb->mr,
            .offset_within_region = 0,
            .size = rb->mr->size,
        };

        return ram_discard_manager_replay_populated(rdm, &section,
                                                    uffd_protect_section,
                                                    (void *)(uintptr_t)uffd_fd);
    }
    return uffd_change_protection(uffd_fd, rb->host,
                                  rb->used_length, true, false);
}

/*
 * ram_write_tracking_start: start UFFD-WP memory tracking
 *
 * Returns 0 for success or negative value in case of error
 */
int ram_write_tracking_start(void)
{
    int uffd_fd;
    RAMState *rs = ram_state;
    RAMBlock *block;

    /* Open UFFD file descriptor */
    uffd_fd = uffd_create_fd(UFFD_FEATURE_PAGEFAULT_FLAG_WP, true);
    if (uffd_fd < 0) {
        return uffd_fd;
    }
    rs->uffdio_fd = uffd_fd;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        /* Nothing to do with read-only and MMIO-writable regions */
        if (block->mr->readonly || block->mr->rom_device) {
            continue;
        }

        /* Register block memory with UFFD to track writes */
        if (uffd_register_memory(rs->uffdio_fd, block->host,
                block->max_length, UFFDIO_REGISTER_MODE_WP, NULL)) {
            goto fail;
        }
        block->flags |= RAM_UF_WRITEPROTECT;
        memory_region_ref(block->mr);

        /* Apply UFFD write protection to the block memory range */
        if (ram_block_uffd_protect(block, uffd_fd)) {
            goto fail;
        }

        trace_ram_write_tracking_ramblock_start(block->idstr, block->page_size,
                block->host, block->max_length);
    }

    return 0;

fail:
    error_report("ram_write_tracking_start() failed: restoring initial memory state");

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        if ((block->flags & RAM_UF_WRITEPROTECT) == 0) {
            continue;
        }
        uffd_unregister_memory(rs->uffdio_fd, block->host, block->max_length);
        /* Cleanup flags and remove reference */
        block->flags &= ~RAM_UF_WRITEPROTECT;
        memory_region_unref(block->mr);
    }

    uffd_close_fd(uffd_fd);
    rs->uffdio_fd = -1;
    return -1;
}

/**
 * ram_write_tracking_stop: stop UFFD-WP memory tracking and remove protection
 */
void ram_write_tracking_stop(void)
{
    RAMState *rs = ram_state;
    RAMBlock *block;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        if ((block->flags & RAM_UF_WRITEPROTECT) == 0) {
            continue;
        }
        uffd_unregister_memory(rs->uffdio_fd, block->host, block->max_length);

        trace_ram_write_tracking_ramblock_stop(block->idstr, block->page_size,
                block->host, block->max_length);

        /* Cleanup flags and remove reference */
        block->flags &= ~RAM_UF_WRITEPROTECT;
        memory_region_unref(block->mr);
    }

    /* Finally close UFFD file descriptor */
    uffd_close_fd(rs->uffdio_fd);
    rs->uffdio_fd = -1;
}

#else
/* No target OS support, stubs just fail or ignore */

static RAMBlock *poll_fault_page(RAMState *rs, ram_addr_t *offset)
{
    (void) rs;
    (void) offset;

    return NULL;
}

static int ram_save_release_protection(RAMState *rs, PageSearchStatus *pss,
        unsigned long start_page)
{
    (void) rs;
    (void) pss;
    (void) start_page;

    return 0;
}

bool ram_write_tracking_available(void)
{
    return false;
}

bool ram_write_tracking_compatible(void)
{
    assert(0);
    return false;
}

int ram_write_tracking_start(void)
{
    assert(0);
    return -1;
}

void ram_write_tracking_stop(void)
{
    assert(0);
}
#endif /* defined(__linux__) */

/**
 * get_queued_page: unqueue a page from the postcopy requests
 *
 * Skips pages that are already sent (!dirty)
 *
 * Returns true if a queued page is found
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
                                                page);
            } else {
                trace_get_queued_page(block->idstr, (uint64_t)offset, page);
            }
        }

    } while (block && !dirty);

    if (!block) {
        /*
         * Poll write faults too if background snapshot is enabled; that's
         * when we have vcpus got blocked by the write protected pages.
         */
        block = poll_fault_page(rs, &offset);
    }

    if (block) {
        /*
         * We want the background search to continue from the queued page
         * since the guest is likely to want other pages near to the page
         * it just requested.
         */
        pss->block = block;
        pss->page = offset >> TARGET_PAGE_BITS;

        /*
         * This unqueued page would break the "one round" check, even is
         * really rare.
         */
        pss->complete_round = false;
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
    RCU_READ_LOCK_GUARD();
    QSIMPLEQ_FOREACH_SAFE(mspr, &rs->src_page_requests, next_req, next_mspr) {
        memory_region_unref(mspr->rb->mr);
        QSIMPLEQ_REMOVE_HEAD(&rs->src_page_requests, next_req);
        g_free(mspr);
    }
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
int ram_save_queue_pages(const char *rbname, ram_addr_t start, ram_addr_t len,
                         Error **errp)
{
    RAMBlock *ramblock;
    RAMState *rs = ram_state;

    stat64_add(&mig_stats.postcopy_requests, 1);
    RCU_READ_LOCK_GUARD();

    if (!rbname) {
        /* Reuse last RAMBlock */
        ramblock = rs->last_req_rb;

        if (!ramblock) {
            /*
             * Shouldn't happen, we can't reuse the last RAMBlock if
             * it's the 1st request.
             */
            error_setg(errp, "MIG_RP_MSG_REQ_PAGES has no previous block");
            return -1;
        }
    } else {
        ramblock = qemu_ram_block_by_name(rbname);

        if (!ramblock) {
            /* We shouldn't be asked for a non-existent RAMBlock */
            error_setg(errp, "MIG_RP_MSG_REQ_PAGES has no block '%s'", rbname);
            return -1;
        }
        rs->last_req_rb = ramblock;
    }
    trace_ram_save_queue_pages(ramblock->idstr, start, len);
    if (!offset_in_ramblock(ramblock, start + len - 1)) {
        error_setg(errp, "MIG_RP_MSG_REQ_PAGES request overrun, "
                   "start=" RAM_ADDR_FMT " len="
                   RAM_ADDR_FMT " blocklen=" RAM_ADDR_FMT,
                   start, len, ramblock->used_length);
        return -1;
    }

    /*
     * When with postcopy preempt, we send back the page directly in the
     * rp-return thread.
     */
    if (postcopy_preempt_active()) {
        ram_addr_t page_start = start >> TARGET_PAGE_BITS;
        size_t page_size = qemu_ram_pagesize(ramblock);
        PageSearchStatus *pss = &ram_state->pss[RAM_CHANNEL_POSTCOPY];
        int ret = 0;

        qemu_mutex_lock(&rs->bitmap_mutex);

        pss_init(pss, ramblock, page_start);
        /*
         * Always use the preempt channel, and make sure it's there.  It's
         * safe to access without lock, because when rp-thread is running
         * we should be the only one who operates on the qemufile
         */
        pss->pss_channel = migrate_get_current()->postcopy_qemufile_src;
        assert(pss->pss_channel);

        /*
         * It must be either one or multiple of host page size.  Just
         * assert; if something wrong we're mostly split brain anyway.
         */
        assert(len % page_size == 0);
        while (len) {
            if (ram_save_host_page_urgent(pss)) {
                error_setg(errp, "ram_save_host_page_urgent() failed: "
                           "ramblock=%s, start_addr=0x"RAM_ADDR_FMT,
                           ramblock->idstr, start);
                ret = -1;
                break;
            }
            /*
             * NOTE: after ram_save_host_page_urgent() succeeded, pss->page
             * will automatically be moved and point to the next host page
             * we're going to send, so no need to update here.
             *
             * Normally QEMU never sends >1 host page in requests, so
             * logically we don't even need that as the loop should only
             * run once, but just to be consistent.
             */
            len -= page_size;
        };
        qemu_mutex_unlock(&rs->bitmap_mutex);

        return ret;
    }

    struct RAMSrcPageRequest *new_entry =
        g_new0(struct RAMSrcPageRequest, 1);
    new_entry->rb = ramblock;
    new_entry->offset = start;
    new_entry->len = len;

    memory_region_ref(ramblock->mr);
    qemu_mutex_lock(&rs->src_page_req_mutex);
    QSIMPLEQ_INSERT_TAIL(&rs->src_page_requests, new_entry, next_req);
    migration_make_urgent_request();
    qemu_mutex_unlock(&rs->src_page_req_mutex);

    return 0;
}

/*
 * try to compress the page before posting it out, return true if the page
 * has been properly handled by compression, otherwise needs other
 * paths to handle it
 */
static bool save_compress_page(RAMState *rs, PageSearchStatus *pss,
                               ram_addr_t offset)
{
    if (!migrate_compress()) {
        return false;
    }

    /*
     * When starting the process of a new block, the first page of
     * the block should be sent out before other pages in the same
     * block, and all the pages in last block should have been sent
     * out, keeping this order is important, because the 'cont' flag
     * is used to avoid resending the block name.
     *
     * We post the fist page as normal page as compression will take
     * much CPU resource.
     */
    if (pss->block != pss->last_sent_block) {
        compress_flush_data();
        return false;
    }

    return compress_page_with_multi_thread(pss->block, offset,
                                           compress_send_queued_data);
}

/**
 * ram_save_target_page_legacy: save one target page
 *
 * Returns the number of pages written
 *
 * @rs: current RAM state
 * @pss: data about the page we want to send
 */
static int ram_save_target_page_legacy(RAMState *rs, PageSearchStatus *pss)
{
    RAMBlock *block = pss->block;
    ram_addr_t offset = ((ram_addr_t)pss->page) << TARGET_PAGE_BITS;
    int res;

    if (control_save_page(pss, offset, &res)) {
        return res;
    }

    if (save_compress_page(rs, pss, offset)) {
        return 1;
    }

    if (save_zero_page(rs, pss, offset)) {
        return 1;
    }

    /*
     * Do not use multifd in postcopy as one whole host page should be
     * placed.  Meanwhile postcopy requires atomic update of pages, so even
     * if host page size == guest page size the dest guest during run may
     * still see partially copied pages which is data corruption.
     */
    if (migrate_multifd() && !migration_in_postcopy()) {
        return ram_save_multifd_page(pss->pss_channel, block, offset);
    }

    return ram_save_page(rs, pss);
}

/* Should be called before sending a host page */
static void pss_host_page_prepare(PageSearchStatus *pss)
{
    /* How many guest pages are there in one host page? */
    size_t guest_pfns = qemu_ram_pagesize(pss->block) >> TARGET_PAGE_BITS;

    pss->host_page_sending = true;
    if (guest_pfns <= 1) {
        /*
         * This covers both when guest psize == host psize, or when guest
         * has larger psize than the host (guest_pfns==0).
         *
         * For the latter, we always send one whole guest page per
         * iteration of the host page (example: an Alpha VM on x86 host
         * will have guest psize 8K while host psize 4K).
         */
        pss->host_page_start = pss->page;
        pss->host_page_end = pss->page + 1;
    } else {
        /*
         * The host page spans over multiple guest pages, we send them
         * within the same host page iteration.
         */
        pss->host_page_start = ROUND_DOWN(pss->page, guest_pfns);
        pss->host_page_end = ROUND_UP(pss->page + 1, guest_pfns);
    }
}

/*
 * Whether the page pointed by PSS is within the host page being sent.
 * Must be called after a previous pss_host_page_prepare().
 */
static bool pss_within_range(PageSearchStatus *pss)
{
    ram_addr_t ram_addr;

    assert(pss->host_page_sending);

    /* Over host-page boundary? */
    if (pss->page >= pss->host_page_end) {
        return false;
    }

    ram_addr = ((ram_addr_t)pss->page) << TARGET_PAGE_BITS;

    return offset_in_ramblock(pss->block, ram_addr);
}

static void pss_host_page_finish(PageSearchStatus *pss)
{
    pss->host_page_sending = false;
    /* This is not needed, but just to reset it */
    pss->host_page_start = pss->host_page_end = 0;
}

/*
 * Send an urgent host page specified by `pss'.  Need to be called with
 * bitmap_mutex held.
 *
 * Returns 0 if save host page succeeded, false otherwise.
 */
static int ram_save_host_page_urgent(PageSearchStatus *pss)
{
    bool page_dirty, sent = false;
    RAMState *rs = ram_state;
    int ret = 0;

    trace_postcopy_preempt_send_host_page(pss->block->idstr, pss->page);
    pss_host_page_prepare(pss);

    /*
     * If precopy is sending the same page, let it be done in precopy, or
     * we could send the same page in two channels and none of them will
     * receive the whole page.
     */
    if (pss_overlap(pss, &ram_state->pss[RAM_CHANNEL_PRECOPY])) {
        trace_postcopy_preempt_hit(pss->block->idstr,
                                   pss->page << TARGET_PAGE_BITS);
        return 0;
    }

    do {
        page_dirty = migration_bitmap_clear_dirty(rs, pss->block, pss->page);

        if (page_dirty) {
            /* Be strict to return code; it must be 1, or what else? */
            if (migration_ops->ram_save_target_page(rs, pss) != 1) {
                error_report_once("%s: ram_save_target_page failed", __func__);
                ret = -1;
                goto out;
            }
            sent = true;
        }
        pss_find_next_dirty(pss);
    } while (pss_within_range(pss));
out:
    pss_host_page_finish(pss);
    /* For urgent requests, flush immediately if sent */
    if (sent) {
        qemu_fflush(pss->pss_channel);
    }
    return ret;
}

/**
 * ram_save_host_page: save a whole host page
 *
 * Starting at *offset send pages up to the end of the current host
 * page. It's valid for the initial offset to point into the middle of
 * a host page in which case the remainder of the hostpage is sent.
 * Only dirty target pages are sent. Note that the host page size may
 * be a huge page for this block.
 *
 * The saving stops at the boundary of the used_length of the block
 * if the RAMBlock isn't a multiple of the host page size.
 *
 * The caller must be with ram_state.bitmap_mutex held to call this
 * function.  Note that this function can temporarily release the lock, but
 * when the function is returned it'll make sure the lock is still held.
 *
 * Returns the number of pages written or negative on error
 *
 * @rs: current RAM state
 * @pss: data about the page we want to send
 */
static int ram_save_host_page(RAMState *rs, PageSearchStatus *pss)
{
    bool page_dirty, preempt_active = postcopy_preempt_active();
    int tmppages, pages = 0;
    size_t pagesize_bits =
        qemu_ram_pagesize(pss->block) >> TARGET_PAGE_BITS;
    unsigned long start_page = pss->page;
    int res;

    if (migrate_ram_is_ignored(pss->block)) {
        error_report("block %s should not be migrated !", pss->block->idstr);
        return 0;
    }

    /* Update host page boundary information */
    pss_host_page_prepare(pss);

    do {
        page_dirty = migration_bitmap_clear_dirty(rs, pss->block, pss->page);

        /* Check the pages is dirty and if it is send it */
        if (page_dirty) {
            /*
             * Properly yield the lock only in postcopy preempt mode
             * because both migration thread and rp-return thread can
             * operate on the bitmaps.
             */
            if (preempt_active) {
                qemu_mutex_unlock(&rs->bitmap_mutex);
            }
            tmppages = migration_ops->ram_save_target_page(rs, pss);
            if (tmppages >= 0) {
                pages += tmppages;
                /*
                 * Allow rate limiting to happen in the middle of huge pages if
                 * something is sent in the current iteration.
                 */
                if (pagesize_bits > 1 && tmppages > 0) {
                    migration_rate_limit();
                }
            }
            if (preempt_active) {
                qemu_mutex_lock(&rs->bitmap_mutex);
            }
        } else {
            tmppages = 0;
        }

        if (tmppages < 0) {
            pss_host_page_finish(pss);
            return tmppages;
        }

        pss_find_next_dirty(pss);
    } while (pss_within_range(pss));

    pss_host_page_finish(pss);

    res = ram_save_release_protection(rs, pss, start_page);
    return (res < 0 ? res : pages);
}

/**
 * ram_find_and_save_block: finds a dirty page and sends it to f
 *
 * Called within an RCU critical section.
 *
 * Returns the number of pages written where zero means no dirty pages,
 * or negative on error
 *
 * @rs: current RAM state
 *
 * On systems where host-page-size > target-page-size it will send all the
 * pages in a host page that are dirty.
 */
static int ram_find_and_save_block(RAMState *rs)
{
    PageSearchStatus *pss = &rs->pss[RAM_CHANNEL_PRECOPY];
    int pages = 0;

    /* No dirty page as there is zero RAM */
    if (!rs->ram_bytes_total) {
        return pages;
    }

    /*
     * Always keep last_seen_block/last_page valid during this procedure,
     * because find_dirty_block() relies on these values (e.g., we compare
     * last_seen_block with pss.block to see whether we searched all the
     * ramblocks) to detect the completion of migration.  Having NULL value
     * of last_seen_block can conditionally cause below loop to run forever.
     */
    if (!rs->last_seen_block) {
        rs->last_seen_block = QLIST_FIRST_RCU(&ram_list.blocks);
        rs->last_page = 0;
    }

    pss_init(pss, rs->last_seen_block, rs->last_page);

    while (true){
        if (!get_queued_page(rs, pss)) {
            /* priority queue empty, so just search for something dirty */
            int res = find_dirty_block(rs, pss);
            if (res != PAGE_DIRTY_FOUND) {
                if (res == PAGE_ALL_CLEAN) {
                    break;
                } else if (res == PAGE_TRY_AGAIN) {
                    continue;
                } else if (res < 0) {
                    pages = res;
                    break;
                }
            }
        }
        pages = ram_save_host_page(rs, pss);
        if (pages) {
            break;
        }
    }

    rs->last_seen_block = pss->block;
    rs->last_page = pss->page;

    return pages;
}

static uint64_t ram_bytes_total_with_ignored(void)
{
    RAMBlock *block;
    uint64_t total = 0;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        total += block->used_length;
    }
    return total;
}

uint64_t ram_bytes_total(void)
{
    RAMBlock *block;
    uint64_t total = 0;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        total += block->used_length;
    }
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

static void ram_state_cleanup(RAMState **rsp)
{
    if (*rsp) {
        migration_page_queue_free(*rsp);
        qemu_mutex_destroy(&(*rsp)->bitmap_mutex);
        qemu_mutex_destroy(&(*rsp)->src_page_req_mutex);
        g_free(*rsp);
        *rsp = NULL;
    }
}

static void xbzrle_cleanup(void)
{
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
}

static void ram_save_cleanup(void *opaque)
{
    RAMState **rsp = opaque;
    RAMBlock *block;

    /* We don't use dirty log with background snapshots */
    if (!migrate_background_snapshot()) {
        /* caller have hold iothread lock or is in a bh, so there is
         * no writing race against the migration bitmap
         */
        if (global_dirty_tracking & GLOBAL_DIRTY_MIGRATION) {
            /*
             * do not stop dirty log without starting it, since
             * memory_global_dirty_log_stop will assert that
             * memory_global_dirty_log_start/stop used in pairs
             */
            memory_global_dirty_log_stop(GLOBAL_DIRTY_MIGRATION);
        }
    }

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        g_free(block->clear_bmap);
        block->clear_bmap = NULL;
        g_free(block->bmap);
        block->bmap = NULL;
    }

    xbzrle_cleanup();
    compress_threads_save_cleanup();
    ram_state_cleanup(rsp);
    g_free(migration_ops);
    migration_ops = NULL;
}

static void ram_state_reset(RAMState *rs)
{
    int i;

    for (i = 0; i < RAM_CHANNEL_MAX; i++) {
        rs->pss[i].last_sent_block = NULL;
    }

    rs->last_seen_block = NULL;
    rs->last_page = 0;
    rs->last_version = ram_list.version;
    rs->xbzrle_started = false;
}

#define MAX_WAIT 50 /* ms, half buffered_file limit */

/* **** functions for postcopy ***** */

void ram_postcopy_migrated_memory_release(MigrationState *ms)
{
    struct RAMBlock *block;

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        unsigned long *bitmap = block->bmap;
        unsigned long range = block->used_length >> TARGET_PAGE_BITS;
        unsigned long run_start = find_next_zero_bit(bitmap, range, 0);

        while (run_start < range) {
            unsigned long run_end = find_next_bit(bitmap, range, run_start + 1);
            ram_discard_range(block->idstr,
                              ((ram_addr_t)run_start) << TARGET_PAGE_BITS,
                              ((ram_addr_t)(run_end - run_start))
                                << TARGET_PAGE_BITS);
            run_start = find_next_zero_bit(bitmap, range, run_end + 1);
        }
    }
}

/**
 * postcopy_send_discard_bm_ram: discard a RAMBlock
 *
 * Callback from postcopy_each_ram_send_discard for each RAMBlock
 *
 * @ms: current migration state
 * @block: RAMBlock to discard
 */
static void postcopy_send_discard_bm_ram(MigrationState *ms, RAMBlock *block)
{
    unsigned long end = block->used_length >> TARGET_PAGE_BITS;
    unsigned long current;
    unsigned long *bitmap = block->bmap;

    for (current = 0; current < end; ) {
        unsigned long one = find_next_bit(bitmap, end, current);
        unsigned long zero, discard_length;

        if (one >= end) {
            break;
        }

        zero = find_next_zero_bit(bitmap, end, one + 1);

        if (zero >= end) {
            discard_length = end - one;
        } else {
            discard_length = zero - one;
        }
        postcopy_discard_send_range(ms, one, discard_length);
        current = one + discard_length;
    }
}

static void postcopy_chunk_hostpages_pass(MigrationState *ms, RAMBlock *block);

/**
 * postcopy_each_ram_send_discard: discard all RAMBlocks
 *
 * Utility for the outgoing postcopy code.
 *   Calls postcopy_send_discard_bm_ram for each RAMBlock
 *   passing it bitmap indexes and name.
 * (qemu_ram_foreach_block ends up passing unscaled lengths
 *  which would mean postcopy code would have to deal with target page)
 *
 * @ms: current migration state
 */
static void postcopy_each_ram_send_discard(MigrationState *ms)
{
    struct RAMBlock *block;

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        postcopy_discard_send_init(ms, block->idstr);

        /*
         * Deal with TPS != HPS and huge pages.  It discard any partially sent
         * host-page size chunks, mark any partially dirty host-page size
         * chunks as all dirty.  In this case the host-page is the host-page
         * for the particular RAMBlock, i.e. it might be a huge page.
         */
        postcopy_chunk_hostpages_pass(ms, block);

        /*
         * Postcopy sends chunks of bitmap over the wire, but it
         * just needs indexes at this point, avoids it having
         * target page specific code.
         */
        postcopy_send_discard_bm_ram(ms, block);
        postcopy_discard_send_finish(ms);
    }
}

/**
 * postcopy_chunk_hostpages_pass: canonicalize bitmap in hostpages
 *
 * Helper for postcopy_chunk_hostpages; it's called twice to
 * canonicalize the two bitmaps, that are similar, but one is
 * inverted.
 *
 * Postcopy requires that all target pages in a hostpage are dirty or
 * clean, not a mix.  This function canonicalizes the bitmaps.
 *
 * @ms: current migration state
 * @block: block that contains the page we want to canonicalize
 */
static void postcopy_chunk_hostpages_pass(MigrationState *ms, RAMBlock *block)
{
    RAMState *rs = ram_state;
    unsigned long *bitmap = block->bmap;
    unsigned int host_ratio = block->page_size / TARGET_PAGE_SIZE;
    unsigned long pages = block->used_length >> TARGET_PAGE_BITS;
    unsigned long run_start;

    if (block->page_size == TARGET_PAGE_SIZE) {
        /* Easy case - TPS==HPS for a non-huge page RAMBlock */
        return;
    }

    /* Find a dirty page */
    run_start = find_next_bit(bitmap, pages, 0);

    while (run_start < pages) {

        /*
         * If the start of this run of pages is in the middle of a host
         * page, then we need to fixup this host page.
         */
        if (QEMU_IS_ALIGNED(run_start, host_ratio)) {
            /* Find the end of this run */
            run_start = find_next_zero_bit(bitmap, pages, run_start + 1);
            /*
             * If the end isn't at the start of a host page, then the
             * run doesn't finish at the end of a host page
             * and we need to discard.
             */
        }

        if (!QEMU_IS_ALIGNED(run_start, host_ratio)) {
            unsigned long page;
            unsigned long fixup_start_addr = QEMU_ALIGN_DOWN(run_start,
                                                             host_ratio);
            run_start = QEMU_ALIGN_UP(run_start, host_ratio);

            /* Clean up the bitmap */
            for (page = fixup_start_addr;
                 page < fixup_start_addr + host_ratio; page++) {
                /*
                 * Remark them as dirty, updating the count for any pages
                 * that weren't previously dirty.
                 */
                rs->migration_dirty_pages += !test_and_set_bit(page, bitmap);
            }
        }

        /* Find the next dirty page for the next iteration */
        run_start = find_next_bit(bitmap, pages, run_start);
    }
}

/**
 * ram_postcopy_send_discard_bitmap: transmit the discard bitmap
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
void ram_postcopy_send_discard_bitmap(MigrationState *ms)
{
    RAMState *rs = ram_state;

    RCU_READ_LOCK_GUARD();

    /* This should be our last sync, the src is now paused */
    migration_bitmap_sync(rs, false);

    /* Easiest way to make sure we don't resume in the middle of a host-page */
    rs->pss[RAM_CHANNEL_PRECOPY].last_sent_block = NULL;
    rs->last_seen_block = NULL;
    rs->last_page = 0;

    postcopy_each_ram_send_discard(ms);

    trace_ram_postcopy_send_discard_bitmap();
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
    trace_ram_discard_range(rbname, start, length);

    RCU_READ_LOCK_GUARD();
    RAMBlock *rb = qemu_ram_block_by_name(rbname);

    if (!rb) {
        error_report("ram_discard_range: Failed to find block '%s'", rbname);
        return -1;
    }

    /*
     * On source VM, we don't need to update the received bitmap since
     * we don't even have one.
     */
    if (rb->receivedmap) {
        bitmap_clear(rb->receivedmap, start >> qemu_target_page_bits(),
                     length >> qemu_target_page_bits());
    }

    return ram_block_discard_range(rb, start, length);
}

/*
 * For every allocation, we will try not to crash the VM if the
 * allocation failed.
 */
static int xbzrle_init(void)
{
    Error *local_err = NULL;

    if (!migrate_xbzrle()) {
        return 0;
    }

    XBZRLE_cache_lock();

    XBZRLE.zero_target_page = g_try_malloc0(TARGET_PAGE_SIZE);
    if (!XBZRLE.zero_target_page) {
        error_report("%s: Error allocating zero page", __func__);
        goto err_out;
    }

    XBZRLE.cache = cache_init(migrate_xbzrle_cache_size(),
                              TARGET_PAGE_SIZE, &local_err);
    if (!XBZRLE.cache) {
        error_report_err(local_err);
        goto free_zero_page;
    }

    XBZRLE.encoded_buf = g_try_malloc0(TARGET_PAGE_SIZE);
    if (!XBZRLE.encoded_buf) {
        error_report("%s: Error allocating encoded_buf", __func__);
        goto free_cache;
    }

    XBZRLE.current_buf = g_try_malloc(TARGET_PAGE_SIZE);
    if (!XBZRLE.current_buf) {
        error_report("%s: Error allocating current_buf", __func__);
        goto free_encoded_buf;
    }

    /* We are all good */
    XBZRLE_cache_unlock();
    return 0;

free_encoded_buf:
    g_free(XBZRLE.encoded_buf);
    XBZRLE.encoded_buf = NULL;
free_cache:
    cache_fini(XBZRLE.cache);
    XBZRLE.cache = NULL;
free_zero_page:
    g_free(XBZRLE.zero_target_page);
    XBZRLE.zero_target_page = NULL;
err_out:
    XBZRLE_cache_unlock();
    return -ENOMEM;
}

static int ram_state_init(RAMState **rsp)
{
    *rsp = g_try_new0(RAMState, 1);

    if (!*rsp) {
        error_report("%s: Init ramstate fail", __func__);
        return -1;
    }

    qemu_mutex_init(&(*rsp)->bitmap_mutex);
    qemu_mutex_init(&(*rsp)->src_page_req_mutex);
    QSIMPLEQ_INIT(&(*rsp)->src_page_requests);
    (*rsp)->ram_bytes_total = ram_bytes_total();

    /*
     * Count the total number of pages used by ram blocks not including any
     * gaps due to alignment or unplugs.
     * This must match with the initial values of dirty bitmap.
     */
    (*rsp)->migration_dirty_pages = (*rsp)->ram_bytes_total >> TARGET_PAGE_BITS;
    ram_state_reset(*rsp);

    return 0;
}

static void ram_list_init_bitmaps(void)
{
    MigrationState *ms = migrate_get_current();
    RAMBlock *block;
    unsigned long pages;
    uint8_t shift;

    /* Skip setting bitmap if there is no RAM */
    if (ram_bytes_total()) {
        shift = ms->clear_bitmap_shift;
        if (shift > CLEAR_BITMAP_SHIFT_MAX) {
            error_report("clear_bitmap_shift (%u) too big, using "
                         "max value (%u)", shift, CLEAR_BITMAP_SHIFT_MAX);
            shift = CLEAR_BITMAP_SHIFT_MAX;
        } else if (shift < CLEAR_BITMAP_SHIFT_MIN) {
            error_report("clear_bitmap_shift (%u) too small, using "
                         "min value (%u)", shift, CLEAR_BITMAP_SHIFT_MIN);
            shift = CLEAR_BITMAP_SHIFT_MIN;
        }

        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            pages = block->max_length >> TARGET_PAGE_BITS;
            /*
             * The initial dirty bitmap for migration must be set with all
             * ones to make sure we'll migrate every guest RAM page to
             * destination.
             * Here we set RAMBlock.bmap all to 1 because when rebegin a
             * new migration after a failed migration, ram_list.
             * dirty_memory[DIRTY_MEMORY_MIGRATION] don't include the whole
             * guest memory.
             */
            block->bmap = bitmap_new(pages);
            bitmap_set(block->bmap, 0, pages);
            block->clear_bmap_shift = shift;
            block->clear_bmap = bitmap_new(clear_bmap_size(pages, shift));
        }
    }
}

static void migration_bitmap_clear_discarded_pages(RAMState *rs)
{
    unsigned long pages;
    RAMBlock *rb;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
            pages = ramblock_dirty_bitmap_clear_discarded_pages(rb);
            rs->migration_dirty_pages -= pages;
    }
}

static void ram_init_bitmaps(RAMState *rs)
{
    qemu_mutex_lock_ramlist();

    WITH_RCU_READ_LOCK_GUARD() {
        ram_list_init_bitmaps();
        /* We don't use dirty log with background snapshots */
        if (!migrate_background_snapshot()) {
            memory_global_dirty_log_start(GLOBAL_DIRTY_MIGRATION);
            migration_bitmap_sync_precopy(rs, false);
        }
    }
    qemu_mutex_unlock_ramlist();

    /*
     * After an eventual first bitmap sync, fixup the initial bitmap
     * containing all 1s to exclude any discarded pages from migration.
     */
    migration_bitmap_clear_discarded_pages(rs);
}

static int ram_init_all(RAMState **rsp)
{
    if (ram_state_init(rsp)) {
        return -1;
    }

    if (xbzrle_init()) {
        ram_state_cleanup(rsp);
        return -1;
    }

    ram_init_bitmaps(*rsp);

    return 0;
}

static void ram_state_resume_prepare(RAMState *rs, QEMUFile *out)
{
    RAMBlock *block;
    uint64_t pages = 0;

    /*
     * Postcopy is not using xbzrle/compression, so no need for that.
     * Also, since source are already halted, we don't need to care
     * about dirty page logging as well.
     */

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        pages += bitmap_count_one(block->bmap,
                                  block->used_length >> TARGET_PAGE_BITS);
    }

    /* This may not be aligned with current bitmaps. Recalculate. */
    rs->migration_dirty_pages = pages;

    ram_state_reset(rs);

    /* Update RAMState cache of output QEMUFile */
    rs->pss[RAM_CHANNEL_PRECOPY].pss_channel = out;

    trace_ram_state_resume_prepare(pages);
}

/*
 * This function clears bits of the free pages reported by the caller from the
 * migration dirty bitmap. @addr is the host address corresponding to the
 * start of the continuous guest free pages, and @len is the total bytes of
 * those pages.
 */
void qemu_guest_free_page_hint(void *addr, size_t len)
{
    RAMBlock *block;
    ram_addr_t offset;
    size_t used_len, start, npages;
    MigrationState *s = migrate_get_current();

    /* This function is currently expected to be used during live migration */
    if (!migration_is_setup_or_active(s->state)) {
        return;
    }

    for (; len > 0; len -= used_len, addr += used_len) {
        block = qemu_ram_block_from_host(addr, false, &offset);
        if (unlikely(!block || offset >= block->used_length)) {
            /*
             * The implementation might not support RAMBlock resize during
             * live migration, but it could happen in theory with future
             * updates. So we add a check here to capture that case.
             */
            error_report_once("%s unexpected error", __func__);
            return;
        }

        if (len <= block->used_length - offset) {
            used_len = len;
        } else {
            used_len = block->used_length - offset;
        }

        start = offset >> TARGET_PAGE_BITS;
        npages = used_len >> TARGET_PAGE_BITS;

        qemu_mutex_lock(&ram_state->bitmap_mutex);
        /*
         * The skipped free pages are equavalent to be sent from clear_bmap's
         * perspective, so clear the bits from the memory region bitmap which
         * are initially set. Otherwise those skipped pages will be sent in
         * the next round after syncing from the memory region bitmap.
         */
        migration_clear_memory_region_dirty_bitmap_range(block, start, npages);
        ram_state->migration_dirty_pages -=
                      bitmap_count_one_with_offset(block->bmap, start, npages);
        bitmap_clear(block->bmap, start, npages);
        qemu_mutex_unlock(&ram_state->bitmap_mutex);
    }
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
    int ret;

    if (compress_threads_save_setup()) {
        return -1;
    }

    /* migration has already setup the bitmap, reuse it. */
    if (!migration_in_colo_state()) {
        if (ram_init_all(rsp) != 0) {
            compress_threads_save_cleanup();
            return -1;
        }
    }
    (*rsp)->pss[RAM_CHANNEL_PRECOPY].pss_channel = f;

    WITH_RCU_READ_LOCK_GUARD() {
        qemu_put_be64(f, ram_bytes_total_with_ignored()
                         | RAM_SAVE_FLAG_MEM_SIZE);

        RAMBLOCK_FOREACH_MIGRATABLE(block) {
            qemu_put_byte(f, strlen(block->idstr));
            qemu_put_buffer(f, (uint8_t *)block->idstr, strlen(block->idstr));
            qemu_put_be64(f, block->used_length);
            if (migrate_postcopy_ram() && block->page_size !=
                                          qemu_host_page_size) {
                qemu_put_be64(f, block->page_size);
            }
            if (migrate_ignore_shared()) {
                qemu_put_be64(f, block->mr->addr);
            }
        }
    }

    ret = rdma_registration_start(f, RAM_CONTROL_SETUP);
    if (ret < 0) {
        qemu_file_set_error(f, ret);
        return ret;
    }

    ret = rdma_registration_stop(f, RAM_CONTROL_SETUP);
    if (ret < 0) {
        qemu_file_set_error(f, ret);
        return ret;
    }

    migration_ops = g_malloc0(sizeof(MigrationOps));
    migration_ops->ram_save_target_page = ram_save_target_page_legacy;

    qemu_mutex_unlock_iothread();
    ret = multifd_send_sync_main(f);
    qemu_mutex_lock_iothread();
    if (ret < 0) {
        return ret;
    }

    if (migrate_multifd() && !migrate_multifd_flush_after_each_section()) {
        qemu_put_be64(f, RAM_SAVE_FLAG_MULTIFD_FLUSH);
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    return qemu_fflush(f);
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
    int ret = 0;
    int i;
    int64_t t0;
    int done = 0;

    if (blk_mig_bulk_active()) {
        /* Avoid transferring ram during bulk phase of block migration as
         * the bulk phase will usually take a long time and transferring
         * ram updates during that time is pointless. */
        goto out;
    }

    /*
     * We'll take this lock a little bit long, but it's okay for two reasons.
     * Firstly, the only possible other thread to take it is who calls
     * qemu_guest_free_page_hint(), which should be rare; secondly, see
     * MAX_WAIT (if curious, further see commit 4508bd9ed8053ce) below, which
     * guarantees that we'll at least released it in a regular basis.
     */
    WITH_QEMU_LOCK_GUARD(&rs->bitmap_mutex) {
        WITH_RCU_READ_LOCK_GUARD() {
            if (ram_list.version != rs->last_version) {
                ram_state_reset(rs);
            }

            /* Read version before ram_list.blocks */
            smp_rmb();

            ret = rdma_registration_start(f, RAM_CONTROL_ROUND);
            if (ret < 0) {
                qemu_file_set_error(f, ret);
                goto out;
            }

            t0 = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            i = 0;
            while ((ret = migration_rate_exceeded(f)) == 0 ||
                   postcopy_has_request(rs)) {
                int pages;

                if (qemu_file_get_error(f)) {
                    break;
                }

                pages = ram_find_and_save_block(rs);
                /* no more pages to sent */
                if (pages == 0) {
                    done = 1;
                    break;
                }

                if (pages < 0) {
                    qemu_file_set_error(f, pages);
                    break;
                }

                rs->target_page_count += pages;

                /*
                 * During postcopy, it is necessary to make sure one whole host
                 * page is sent in one chunk.
                 */
                if (migrate_postcopy_ram()) {
                    compress_flush_data();
                }

                /*
                 * we want to check in the 1st loop, just in case it was the 1st
                 * time and we had to sync the dirty bitmap.
                 * qemu_clock_get_ns() is a bit expensive, so we only check each
                 * some iterations
                 */
                if ((i & 63) == 0) {
                    uint64_t t1 = (qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - t0) /
                        1000000;
                    if (t1 > MAX_WAIT) {
                        trace_ram_save_iterate_big_wait(t1, i);
                        break;
                    }
                }
                i++;
            }
        }
    }

    /*
     * Must occur before EOS (or any QEMUFile operation)
     * because of RDMA protocol.
     */
    ret = rdma_registration_stop(f, RAM_CONTROL_ROUND);
    if (ret < 0) {
        qemu_file_set_error(f, ret);
    }

out:
    if (ret >= 0
        && migration_is_setup_or_active(migrate_get_current()->state)) {
        if (migrate_multifd() && migrate_multifd_flush_after_each_section()) {
            ret = multifd_send_sync_main(rs->pss[RAM_CHANNEL_PRECOPY].pss_channel);
            if (ret < 0) {
                return ret;
            }
        }

        qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
        ram_transferred_add(8);
        ret = qemu_fflush(f);
    }
    if (ret < 0) {
        return ret;
    }

    return done;
}

/**
 * ram_save_complete: function called to send the remaining amount of ram
 *
 * Returns zero to indicate success or negative on error
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
    int ret = 0;

    rs->last_stage = !migration_in_colo_state();

    WITH_RCU_READ_LOCK_GUARD() {
        if (!migration_in_postcopy()) {
            migration_bitmap_sync_precopy(rs, true);
        }

        ret = rdma_registration_start(f, RAM_CONTROL_FINISH);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            return ret;
        }

        /* try transferring iterative blocks of memory */

        /* flush all remaining blocks regardless of rate limiting */
        qemu_mutex_lock(&rs->bitmap_mutex);
        while (true) {
            int pages;

            pages = ram_find_and_save_block(rs);
            /* no more blocks to sent */
            if (pages == 0) {
                break;
            }
            if (pages < 0) {
                qemu_mutex_unlock(&rs->bitmap_mutex);
                return pages;
            }
        }
        qemu_mutex_unlock(&rs->bitmap_mutex);

        compress_flush_data();

        ret = rdma_registration_stop(f, RAM_CONTROL_FINISH);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            return ret;
        }
    }

    ret = multifd_send_sync_main(rs->pss[RAM_CHANNEL_PRECOPY].pss_channel);
    if (ret < 0) {
        return ret;
    }

    if (migrate_multifd() && !migrate_multifd_flush_after_each_section()) {
        qemu_put_be64(f, RAM_SAVE_FLAG_MULTIFD_FLUSH);
    }
    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    return qemu_fflush(f);
}

static void ram_state_pending_estimate(void *opaque, uint64_t *must_precopy,
                                       uint64_t *can_postcopy)
{
    RAMState **temp = opaque;
    RAMState *rs = *temp;

    uint64_t remaining_size = rs->migration_dirty_pages * TARGET_PAGE_SIZE;

    if (migrate_postcopy_ram()) {
        /* We can do postcopy, and all the data is postcopiable */
        *can_postcopy += remaining_size;
    } else {
        *must_precopy += remaining_size;
    }
}

static void ram_state_pending_exact(void *opaque, uint64_t *must_precopy,
                                    uint64_t *can_postcopy)
{
    MigrationState *s = migrate_get_current();
    RAMState **temp = opaque;
    RAMState *rs = *temp;

    uint64_t remaining_size = rs->migration_dirty_pages * TARGET_PAGE_SIZE;

    if (!migration_in_postcopy() && remaining_size < s->threshold_size) {
        qemu_mutex_lock_iothread();
        WITH_RCU_READ_LOCK_GUARD() {
            migration_bitmap_sync_precopy(rs, false);
        }
        qemu_mutex_unlock_iothread();
        remaining_size = rs->migration_dirty_pages * TARGET_PAGE_SIZE;
    }

    if (migrate_postcopy_ram()) {
        /* We can do postcopy, and all the data is postcopiable */
        *can_postcopy += remaining_size;
    } else {
        *must_precopy += remaining_size;
    }
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
 * @mis: the migration incoming state pointer
 * @f: QEMUFile where to read the data from
 * @flags: Page flags (mostly to see if it's a continuation of previous block)
 * @channel: the channel we're using
 */
static inline RAMBlock *ram_block_from_stream(MigrationIncomingState *mis,
                                              QEMUFile *f, int flags,
                                              int channel)
{
    RAMBlock *block = mis->last_recv_block[channel];
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

    if (migrate_ram_is_ignored(block)) {
        error_report("block %s should not be migrated !", id);
        return NULL;
    }

    mis->last_recv_block[channel] = block;

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

static void *host_page_from_ram_block_offset(RAMBlock *block,
                                             ram_addr_t offset)
{
    /* Note: Explicitly no check against offset_in_ramblock(). */
    return (void *)QEMU_ALIGN_DOWN((uintptr_t)(block->host + offset),
                                   block->page_size);
}

static ram_addr_t host_page_offset_from_ram_block_offset(RAMBlock *block,
                                                         ram_addr_t offset)
{
    return ((uintptr_t)block->host + offset) & (block->page_size - 1);
}

void colo_record_bitmap(RAMBlock *block, ram_addr_t *normal, uint32_t pages)
{
    qemu_mutex_lock(&ram_state->bitmap_mutex);
    for (int i = 0; i < pages; i++) {
        ram_addr_t offset = normal[i];
        ram_state->migration_dirty_pages += !test_and_set_bit(
                                                offset >> TARGET_PAGE_BITS,
                                                block->bmap);
    }
    qemu_mutex_unlock(&ram_state->bitmap_mutex);
}

static inline void *colo_cache_from_block_offset(RAMBlock *block,
                             ram_addr_t offset, bool record_bitmap)
{
    if (!offset_in_ramblock(block, offset)) {
        return NULL;
    }
    if (!block->colo_cache) {
        error_report("%s: colo_cache is NULL in block :%s",
                     __func__, block->idstr);
        return NULL;
    }

    /*
    * During colo checkpoint, we need bitmap of these migrated pages.
    * It help us to decide which pages in ram cache should be flushed
    * into VM's RAM later.
    */
    if (record_bitmap) {
        colo_record_bitmap(block, &offset, 1);
    }
    return block->colo_cache + offset;
}

/**
 * ram_handle_zero: handle the zero page case
 *
 * If a page (or a whole RDMA chunk) has been
 * determined to be zero, then zap it.
 *
 * @host: host address for the zero page
 * @ch: what the page is filled from.  We only support zero
 * @size: size of the zero page
 */
void ram_handle_zero(void *host, uint64_t size)
{
    if (!buffer_is_zero(host, size)) {
        memset(host, 0, size);
    }
}

static void colo_init_ram_state(void)
{
    ram_state_init(&ram_state);
}

/*
 * colo cache: this is for secondary VM, we cache the whole
 * memory of the secondary VM, it is need to hold the global lock
 * to call this helper.
 */
int colo_init_ram_cache(void)
{
    RAMBlock *block;

    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            block->colo_cache = qemu_anon_ram_alloc(block->used_length,
                                                    NULL, false, false);
            if (!block->colo_cache) {
                error_report("%s: Can't alloc memory for COLO cache of block %s,"
                             "size 0x" RAM_ADDR_FMT, __func__, block->idstr,
                             block->used_length);
                RAMBLOCK_FOREACH_NOT_IGNORED(block) {
                    if (block->colo_cache) {
                        qemu_anon_ram_free(block->colo_cache, block->used_length);
                        block->colo_cache = NULL;
                    }
                }
                return -errno;
            }
            if (!machine_dump_guest_core(current_machine)) {
                qemu_madvise(block->colo_cache, block->used_length,
                             QEMU_MADV_DONTDUMP);
            }
        }
    }

    /*
    * Record the dirty pages that sent by PVM, we use this dirty bitmap together
    * with to decide which page in cache should be flushed into SVM's RAM. Here
    * we use the same name 'ram_bitmap' as for migration.
    */
    if (ram_bytes_total()) {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            unsigned long pages = block->max_length >> TARGET_PAGE_BITS;
            block->bmap = bitmap_new(pages);
        }
    }

    colo_init_ram_state();
    return 0;
}

/* TODO: duplicated with ram_init_bitmaps */
void colo_incoming_start_dirty_log(void)
{
    RAMBlock *block = NULL;
    /* For memory_global_dirty_log_start below. */
    qemu_mutex_lock_iothread();
    qemu_mutex_lock_ramlist();

    memory_global_dirty_log_sync(false);
    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            ramblock_sync_dirty_bitmap(ram_state, block);
            /* Discard this dirty bitmap record */
            bitmap_zero(block->bmap, block->max_length >> TARGET_PAGE_BITS);
        }
        memory_global_dirty_log_start(GLOBAL_DIRTY_MIGRATION);
    }
    ram_state->migration_dirty_pages = 0;
    qemu_mutex_unlock_ramlist();
    qemu_mutex_unlock_iothread();
}

/* It is need to hold the global lock to call this helper */
void colo_release_ram_cache(void)
{
    RAMBlock *block;

    memory_global_dirty_log_stop(GLOBAL_DIRTY_MIGRATION);
    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        g_free(block->bmap);
        block->bmap = NULL;
    }

    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            if (block->colo_cache) {
                qemu_anon_ram_free(block->colo_cache, block->used_length);
                block->colo_cache = NULL;
            }
        }
    }
    ram_state_cleanup(&ram_state);
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
    ramblock_recv_map_init();

    return 0;
}

static int ram_load_cleanup(void *opaque)
{
    RAMBlock *rb;

    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
        qemu_ram_block_writeback(rb);
    }

    xbzrle_load_cleanup();

    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
        g_free(rb->receivedmap);
        rb->receivedmap = NULL;
    }

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
    return postcopy_ram_incoming_init(mis);
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
 * @channel: the channel to use for loading
 */
int ram_load_postcopy(QEMUFile *f, int channel)
{
    int flags = 0, ret = 0;
    bool place_needed = false;
    bool matches_target_page_size = false;
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyTmpPage *tmp_page = &mis->postcopy_tmp_pages[channel];

    while (!ret && !(flags & RAM_SAVE_FLAG_EOS)) {
        ram_addr_t addr;
        void *page_buffer = NULL;
        void *place_source = NULL;
        RAMBlock *block = NULL;
        uint8_t ch;
        int len;

        addr = qemu_get_be64(f);

        /*
         * If qemu file error, we should stop here, and then "addr"
         * may be invalid
         */
        ret = qemu_file_get_error(f);
        if (ret) {
            break;
        }

        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        trace_ram_load_postcopy_loop(channel, (uint64_t)addr, flags);
        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE |
                     RAM_SAVE_FLAG_COMPRESS_PAGE)) {
            block = ram_block_from_stream(mis, f, flags, channel);
            if (!block) {
                ret = -EINVAL;
                break;
            }

            /*
             * Relying on used_length is racy and can result in false positives.
             * We might place pages beyond used_length in case RAM was shrunk
             * while in postcopy, which is fine - trying to place via
             * UFFDIO_COPY/UFFDIO_ZEROPAGE will never segfault.
             */
            if (!block->host || addr >= block->postcopy_length) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            tmp_page->target_pages++;
            matches_target_page_size = block->page_size == TARGET_PAGE_SIZE;
            /*
             * Postcopy requires that we place whole host pages atomically;
             * these may be huge pages for RAMBlocks that are backed by
             * hugetlbfs.
             * To make it atomic, the data is read into a temporary page
             * that's moved into place later.
             * The migration protocol uses,  possibly smaller, target-pages
             * however the source ensures it always sends all the components
             * of a host page in one chunk.
             */
            page_buffer = tmp_page->tmp_huge_page +
                          host_page_offset_from_ram_block_offset(block, addr);
            /* If all TP are zero then we can optimise the place */
            if (tmp_page->target_pages == 1) {
                tmp_page->host_addr =
                    host_page_from_ram_block_offset(block, addr);
            } else if (tmp_page->host_addr !=
                       host_page_from_ram_block_offset(block, addr)) {
                /* not the 1st TP within the HP */
                error_report("Non-same host page detected on channel %d: "
                             "Target host page %p, received host page %p "
                             "(rb %s offset 0x"RAM_ADDR_FMT" target_pages %d)",
                             channel, tmp_page->host_addr,
                             host_page_from_ram_block_offset(block, addr),
                             block->idstr, addr, tmp_page->target_pages);
                ret = -EINVAL;
                break;
            }

            /*
             * If it's the last part of a host page then we place the host
             * page
             */
            if (tmp_page->target_pages ==
                (block->page_size / TARGET_PAGE_SIZE)) {
                place_needed = true;
            }
            place_source = tmp_page->tmp_huge_page;
        }

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_ZERO:
            ch = qemu_get_byte(f);
            if (ch != 0) {
                error_report("Found a zero page with value %d", ch);
                ret = -EINVAL;
                break;
            }
            /*
             * Can skip to set page_buffer when
             * this is a zero page and (block->page_size == TARGET_PAGE_SIZE).
             */
            if (!matches_target_page_size) {
                memset(page_buffer, ch, TARGET_PAGE_SIZE);
            }
            break;

        case RAM_SAVE_FLAG_PAGE:
            tmp_page->all_zero = false;
            if (!matches_target_page_size) {
                /* For huge pages, we always use temporary buffer */
                qemu_get_buffer(f, page_buffer, TARGET_PAGE_SIZE);
            } else {
                /*
                 * For small pages that matches target page size, we
                 * avoid the qemu_file copy.  Instead we directly use
                 * the buffer of QEMUFile to place the page.  Note: we
                 * cannot do any QEMUFile operation before using that
                 * buffer to make sure the buffer is valid when
                 * placing the page.
                 */
                qemu_get_buffer_in_place(f, (uint8_t **)&place_source,
                                         TARGET_PAGE_SIZE);
            }
            break;
        case RAM_SAVE_FLAG_COMPRESS_PAGE:
            tmp_page->all_zero = false;
            len = qemu_get_be32(f);
            if (len < 0 || len > compressBound(TARGET_PAGE_SIZE)) {
                error_report("Invalid compressed data length: %d", len);
                ret = -EINVAL;
                break;
            }
            decompress_data_with_multi_threads(f, page_buffer, len);
            break;
        case RAM_SAVE_FLAG_MULTIFD_FLUSH:
            multifd_recv_sync_main();
            break;
        case RAM_SAVE_FLAG_EOS:
            /* normal exit */
            if (migrate_multifd() &&
                migrate_multifd_flush_after_each_section()) {
                multifd_recv_sync_main();
            }
            break;
        default:
            error_report("Unknown combination of migration flags: 0x%x"
                         " (postcopy mode)", flags);
            ret = -EINVAL;
            break;
        }

        /* Got the whole host page, wait for decompress before placing. */
        if (place_needed) {
            ret |= wait_for_decompress_done();
        }

        /* Detect for any possible file errors */
        if (!ret && qemu_file_get_error(f)) {
            ret = qemu_file_get_error(f);
        }

        if (!ret && place_needed) {
            if (tmp_page->all_zero) {
                ret = postcopy_place_page_zero(mis, tmp_page->host_addr, block);
            } else {
                ret = postcopy_place_page(mis, tmp_page->host_addr,
                                          place_source, block);
            }
            place_needed = false;
            postcopy_temp_page_reset(tmp_page);
        }
    }

    return ret;
}

static bool postcopy_is_running(void)
{
    PostcopyState ps = postcopy_state_get();
    return ps >= POSTCOPY_INCOMING_LISTENING && ps < POSTCOPY_INCOMING_END;
}

/*
 * Flush content of RAM cache into SVM's memory.
 * Only flush the pages that be dirtied by PVM or SVM or both.
 */
void colo_flush_ram_cache(void)
{
    RAMBlock *block = NULL;
    void *dst_host;
    void *src_host;
    unsigned long offset = 0;

    memory_global_dirty_log_sync(false);
    qemu_mutex_lock(&ram_state->bitmap_mutex);
    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            ramblock_sync_dirty_bitmap(ram_state, block);
        }
    }

    trace_colo_flush_ram_cache_begin(ram_state->migration_dirty_pages);
    WITH_RCU_READ_LOCK_GUARD() {
        block = QLIST_FIRST_RCU(&ram_list.blocks);

        while (block) {
            unsigned long num = 0;

            offset = colo_bitmap_find_dirty(ram_state, block, offset, &num);
            if (!offset_in_ramblock(block,
                                    ((ram_addr_t)offset) << TARGET_PAGE_BITS)) {
                offset = 0;
                num = 0;
                block = QLIST_NEXT_RCU(block, next);
            } else {
                unsigned long i = 0;

                for (i = 0; i < num; i++) {
                    migration_bitmap_clear_dirty(ram_state, block, offset + i);
                }
                dst_host = block->host
                         + (((ram_addr_t)offset) << TARGET_PAGE_BITS);
                src_host = block->colo_cache
                         + (((ram_addr_t)offset) << TARGET_PAGE_BITS);
                memcpy(dst_host, src_host, TARGET_PAGE_SIZE * num);
                offset += num;
            }
        }
    }
    qemu_mutex_unlock(&ram_state->bitmap_mutex);
    trace_colo_flush_ram_cache_end();
}

static int parse_ramblock(QEMUFile *f, RAMBlock *block, ram_addr_t length)
{
    int ret = 0;
    /* ADVISE is earlier, it shows the source has the postcopy capability on */
    bool postcopy_advised = migration_incoming_postcopy_advised();

    assert(block);

    if (!qemu_ram_is_migratable(block)) {
        error_report("block %s should not be migrated !", block->idstr);
        return -EINVAL;
    }

    if (length != block->used_length) {
        Error *local_err = NULL;

        ret = qemu_ram_resize(block, length, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return ret;
        }
    }
    /* For postcopy we need to check hugepage sizes match */
    if (postcopy_advised && migrate_postcopy_ram() &&
        block->page_size != qemu_host_page_size) {
        uint64_t remote_page_size = qemu_get_be64(f);
        if (remote_page_size != block->page_size) {
            error_report("Mismatched RAM page size %s "
                         "(local) %zd != %" PRId64, block->idstr,
                         block->page_size, remote_page_size);
            return -EINVAL;
        }
    }
    if (migrate_ignore_shared()) {
        hwaddr addr = qemu_get_be64(f);
        if (migrate_ram_is_ignored(block) &&
            block->mr->addr != addr) {
            error_report("Mismatched GPAs for block %s "
                         "%" PRId64 "!= %" PRId64, block->idstr,
                         (uint64_t)addr, (uint64_t)block->mr->addr);
            return -EINVAL;
        }
    }
    ret = rdma_block_notification_handle(f, block->idstr);
    if (ret < 0) {
        qemu_file_set_error(f, ret);
    }

    return ret;
}

static int parse_ramblocks(QEMUFile *f, ram_addr_t total_ram_bytes)
{
    int ret = 0;

    /* Synchronize RAM block list */
    while (!ret && total_ram_bytes) {
        RAMBlock *block;
        char id[256];
        ram_addr_t length;
        int len = qemu_get_byte(f);

        qemu_get_buffer(f, (uint8_t *)id, len);
        id[len] = 0;
        length = qemu_get_be64(f);

        block = qemu_ram_block_by_name(id);
        if (block) {
            ret = parse_ramblock(f, block, length);
        } else {
            error_report("Unknown ramblock \"%s\", cannot accept "
                         "migration", id);
            ret = -EINVAL;
        }
        total_ram_bytes -= length;
    }

    return ret;
}

/**
 * ram_load_precopy: load pages in precopy case
 *
 * Returns 0 for success or -errno in case of error
 *
 * Called in precopy mode by ram_load().
 * rcu_read_lock is taken prior to this being called.
 *
 * @f: QEMUFile where to send the data
 */
static int ram_load_precopy(QEMUFile *f)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    int flags = 0, ret = 0, invalid_flags = 0, len = 0, i = 0;

    if (!migrate_compress()) {
        invalid_flags |= RAM_SAVE_FLAG_COMPRESS_PAGE;
    }

    while (!ret && !(flags & RAM_SAVE_FLAG_EOS)) {
        ram_addr_t addr;
        void *host = NULL, *host_bak = NULL;
        uint8_t ch;

        /*
         * Yield periodically to let main loop run, but an iteration of
         * the main loop is expensive, so do it each some iterations
         */
        if ((i & 32767) == 0 && qemu_in_coroutine()) {
            aio_co_schedule(qemu_get_current_aio_context(),
                            qemu_coroutine_self());
            qemu_coroutine_yield();
        }
        i++;

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
            RAMBlock *block = ram_block_from_stream(mis, f, flags,
                                                    RAM_CHANNEL_PRECOPY);

            host = host_from_ram_block_offset(block, addr);
            /*
             * After going into COLO stage, we should not load the page
             * into SVM's memory directly, we put them into colo_cache firstly.
             * NOTE: We need to keep a copy of SVM's ram in colo_cache.
             * Previously, we copied all these memory in preparing stage of COLO
             * while we need to stop VM, which is a time-consuming process.
             * Here we optimize it by a trick, back-up every page while in
             * migration process while COLO is enabled, though it affects the
             * speed of the migration, but it obviously reduce the downtime of
             * back-up all SVM'S memory in COLO preparing stage.
             */
            if (migration_incoming_colo_enabled()) {
                if (migration_incoming_in_colo_state()) {
                    /* In COLO stage, put all pages into cache temporarily */
                    host = colo_cache_from_block_offset(block, addr, true);
                } else {
                   /*
                    * In migration stage but before COLO stage,
                    * Put all pages into both cache and SVM's memory.
                    */
                    host_bak = colo_cache_from_block_offset(block, addr, false);
                }
            }
            if (!host) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            if (!migration_incoming_in_colo_state()) {
                ramblock_recv_bitmap_set(block, host);
            }

            trace_ram_load_loop(block->idstr, (uint64_t)addr, flags, host);
        }

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_MEM_SIZE:
            ret = parse_ramblocks(f, addr);
            break;

        case RAM_SAVE_FLAG_ZERO:
            ch = qemu_get_byte(f);
            if (ch != 0) {
                error_report("Found a zero page with value %d", ch);
                ret = -EINVAL;
                break;
            }
            ram_handle_zero(host, TARGET_PAGE_SIZE);
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
        case RAM_SAVE_FLAG_MULTIFD_FLUSH:
            multifd_recv_sync_main();
            break;
        case RAM_SAVE_FLAG_EOS:
            /* normal exit */
            if (migrate_multifd() &&
                migrate_multifd_flush_after_each_section()) {
                multifd_recv_sync_main();
            }
            break;
        case RAM_SAVE_FLAG_HOOK:
            ret = rdma_registration_handle(f);
            if (ret < 0) {
                qemu_file_set_error(f, ret);
            }
            break;
        default:
            error_report("Unknown combination of migration flags: 0x%x", flags);
            ret = -EINVAL;
        }
        if (!ret) {
            ret = qemu_file_get_error(f);
        }
        if (!ret && host_bak) {
            memcpy(host_bak, host, TARGET_PAGE_SIZE);
        }
    }

    ret |= wait_for_decompress_done();
    return ret;
}

static int ram_load(QEMUFile *f, void *opaque, int version_id)
{
    int ret = 0;
    static uint64_t seq_iter;
    /*
     * If system is running in postcopy mode, page inserts to host memory must
     * be atomic
     */
    bool postcopy_running = postcopy_is_running();

    seq_iter++;

    if (version_id != 4) {
        return -EINVAL;
    }

    /*
     * This RCU critical section can be very long running.
     * When RCU reclaims in the code start to become numerous,
     * it will be necessary to reduce the granularity of this
     * critical section.
     */
    WITH_RCU_READ_LOCK_GUARD() {
        if (postcopy_running) {
            /*
             * Note!  Here RAM_CHANNEL_PRECOPY is the precopy channel of
             * postcopy migration, we have another RAM_CHANNEL_POSTCOPY to
             * service fast page faults.
             */
            ret = ram_load_postcopy(f, RAM_CHANNEL_PRECOPY);
        } else {
            ret = ram_load_precopy(f);
        }
    }
    trace_ram_load_complete(ret, seq_iter);

    return ret;
}

static bool ram_has_postcopy(void *opaque)
{
    RAMBlock *rb;
    RAMBLOCK_FOREACH_NOT_IGNORED(rb) {
        if (ramblock_is_pmem(rb)) {
            info_report("Block: %s, host: %p is a nvdimm memory, postcopy"
                         "is not supported now!", rb->idstr, rb->host);
            return false;
        }
    }

    return migrate_postcopy_ram();
}

/* Sync all the dirty bitmap with destination VM.  */
static int ram_dirty_bitmap_sync_all(MigrationState *s, RAMState *rs)
{
    RAMBlock *block;
    QEMUFile *file = s->to_dst_file;

    trace_ram_dirty_bitmap_sync_start();

    qatomic_set(&rs->postcopy_bmap_sync_requested, 0);
    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        qemu_savevm_send_recv_bitmap(file, block->idstr);
        trace_ram_dirty_bitmap_request(block->idstr);
        qatomic_inc(&rs->postcopy_bmap_sync_requested);
    }

    trace_ram_dirty_bitmap_sync_wait();

    /* Wait until all the ramblocks' dirty bitmap synced */
    while (qatomic_read(&rs->postcopy_bmap_sync_requested)) {
        if (migration_rp_wait(s)) {
            return -1;
        }
    }

    trace_ram_dirty_bitmap_sync_complete();

    return 0;
}

/*
 * Read the received bitmap, revert it as the initial dirty bitmap.
 * This is only used when the postcopy migration is paused but wants
 * to resume from a middle point.
 *
 * Returns true if succeeded, false for errors.
 */
bool ram_dirty_bitmap_reload(MigrationState *s, RAMBlock *block, Error **errp)
{
    /* from_dst_file is always valid because we're within rp_thread */
    QEMUFile *file = s->rp_state.from_dst_file;
    g_autofree unsigned long *le_bitmap = NULL;
    unsigned long nbits = block->used_length >> TARGET_PAGE_BITS;
    uint64_t local_size = DIV_ROUND_UP(nbits, 8);
    uint64_t size, end_mark;
    RAMState *rs = ram_state;

    trace_ram_dirty_bitmap_reload_begin(block->idstr);

    if (s->state != MIGRATION_STATUS_POSTCOPY_RECOVER) {
        error_setg(errp, "Reload bitmap in incorrect state %s",
                   MigrationStatus_str(s->state));
        return false;
    }

    /*
     * Note: see comments in ramblock_recv_bitmap_send() on why we
     * need the endianness conversion, and the paddings.
     */
    local_size = ROUND_UP(local_size, 8);

    /* Add paddings */
    le_bitmap = bitmap_new(nbits + BITS_PER_LONG);

    size = qemu_get_be64(file);

    /* The size of the bitmap should match with our ramblock */
    if (size != local_size) {
        error_setg(errp, "ramblock '%s' bitmap size mismatch (0x%"PRIx64
                   " != 0x%"PRIx64")", block->idstr, size, local_size);
        return false;
    }

    size = qemu_get_buffer(file, (uint8_t *)le_bitmap, local_size);
    end_mark = qemu_get_be64(file);

    if (qemu_file_get_error(file) || size != local_size) {
        error_setg(errp, "read bitmap failed for ramblock '%s': "
                   "(size 0x%"PRIx64", got: 0x%"PRIx64")",
                   block->idstr, local_size, size);
        return false;
    }

    if (end_mark != RAMBLOCK_RECV_BITMAP_ENDING) {
        error_setg(errp, "ramblock '%s' end mark incorrect: 0x%"PRIx64,
                   block->idstr, end_mark);
        return false;
    }

    /*
     * Endianness conversion. We are during postcopy (though paused).
     * The dirty bitmap won't change. We can directly modify it.
     */
    bitmap_from_le(block->bmap, le_bitmap, nbits);

    /*
     * What we received is "received bitmap". Revert it as the initial
     * dirty bitmap for this ramblock.
     */
    bitmap_complement(block->bmap, block->bmap, nbits);

    /* Clear dirty bits of discarded ranges that we don't want to migrate. */
    ramblock_dirty_bitmap_clear_discarded_pages(block);

    /* We'll recalculate migration_dirty_pages in ram_state_resume_prepare(). */
    trace_ram_dirty_bitmap_reload_complete(block->idstr);

    qatomic_dec(&rs->postcopy_bmap_sync_requested);

    /*
     * We succeeded to sync bitmap for current ramblock. Always kick the
     * migration thread to check whether all requested bitmaps are
     * reloaded.  NOTE: it's racy to only kick when requested==0, because
     * we don't know whether the migration thread may still be increasing
     * it.
     */
    migration_rp_kick(s);

    return true;
}

static int ram_resume_prepare(MigrationState *s, void *opaque)
{
    RAMState *rs = *(RAMState **)opaque;
    int ret;

    ret = ram_dirty_bitmap_sync_all(s, rs);
    if (ret) {
        return ret;
    }

    ram_state_resume_prepare(rs, s->to_dst_file);

    return 0;
}

void postcopy_preempt_shutdown_file(MigrationState *s)
{
    qemu_put_be64(s->postcopy_qemufile_src, RAM_SAVE_FLAG_EOS);
    qemu_fflush(s->postcopy_qemufile_src);
}

static SaveVMHandlers savevm_ram_handlers = {
    .save_setup = ram_save_setup,
    .save_live_iterate = ram_save_iterate,
    .save_live_complete_postcopy = ram_save_complete,
    .save_live_complete_precopy = ram_save_complete,
    .has_postcopy = ram_has_postcopy,
    .state_pending_exact = ram_state_pending_exact,
    .state_pending_estimate = ram_state_pending_estimate,
    .load_state = ram_load,
    .save_cleanup = ram_save_cleanup,
    .load_setup = ram_load_setup,
    .load_cleanup = ram_load_cleanup,
    .resume_prepare = ram_resume_prepare,
};

static void ram_mig_ram_block_resized(RAMBlockNotifier *n, void *host,
                                      size_t old_size, size_t new_size)
{
    PostcopyState ps = postcopy_state_get();
    ram_addr_t offset;
    RAMBlock *rb = qemu_ram_block_from_host(host, false, &offset);
    Error *err = NULL;

    if (!rb) {
        error_report("RAM block not found");
        return;
    }

    if (migrate_ram_is_ignored(rb)) {
        return;
    }

    if (!migration_is_idle()) {
        /*
         * Precopy code on the source cannot deal with the size of RAM blocks
         * changing at random points in time - especially after sending the
         * RAM block sizes in the migration stream, they must no longer change.
         * Abort and indicate a proper reason.
         */
        error_setg(&err, "RAM block '%s' resized during precopy.", rb->idstr);
        migration_cancel(err);
        error_free(err);
    }

    switch (ps) {
    case POSTCOPY_INCOMING_ADVISE:
        /*
         * Update what ram_postcopy_incoming_init()->init_range() does at the
         * time postcopy was advised. Syncing RAM blocks with the source will
         * result in RAM resizes.
         */
        if (old_size < new_size) {
            if (ram_discard_range(rb->idstr, old_size, new_size - old_size)) {
                error_report("RAM block '%s' discard of resized RAM failed",
                             rb->idstr);
            }
        }
        rb->postcopy_length = new_size;
        break;
    case POSTCOPY_INCOMING_NONE:
    case POSTCOPY_INCOMING_RUNNING:
    case POSTCOPY_INCOMING_END:
        /*
         * Once our guest is running, postcopy does no longer care about
         * resizes. When growing, the new memory was not available on the
         * source, no handler needed.
         */
        break;
    default:
        error_report("RAM block '%s' resized during postcopy state: %d",
                     rb->idstr, ps);
        exit(-1);
    }
}

static RAMBlockNotifier ram_mig_ram_notifier = {
    .ram_block_resized = ram_mig_ram_block_resized,
};

void ram_mig_init(void)
{
    qemu_mutex_init(&XBZRLE.lock);
    register_savevm_live("ram", 0, 4, &savevm_ram_handlers, &ram_state);
    ram_block_notifier_add(&ram_mig_ram_notifier);
}
