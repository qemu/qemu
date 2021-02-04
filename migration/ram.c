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
#include "page_cache.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-migration.h"
#include "qapi/qapi-events-migration.h"
#include "qapi/qmp/qerror.h"
#include "trace.h"
#include "exec/ram_addr.h"
#include "exec/target_page.h"
#include "qemu/rcu_queue.h"
#include "migration/colo.h"
#include "block.h"
#include "sysemu/sysemu.h"
#include "sysemu/cpu-throttle.h"
#include "savevm.h"
#include "qemu/iov.h"
#include "multifd.h"
#include "sysemu/runstate.h"

#if defined(__linux__)
#include "qemu/userfaultfd.h"
#endif /* defined(__linux__) */

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
    if (migrate_use_xbzrle()) {
        qemu_mutex_lock(&XBZRLE.lock);
    }
}

static void XBZRLE_cache_unlock(void)
{
    if (migrate_use_xbzrle()) {
        qemu_mutex_unlock(&XBZRLE.lock);
    }
}

/**
 * xbzrle_cache_resize: resize the xbzrle cache
 *
 * This function is called from qmp_migrate_set_cache_size in main
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

bool ramblock_is_ignored(RAMBlock *block)
{
    return !qemu_ram_is_migratable(block) ||
           (migrate_ignore_shared() && qemu_ram_is_shared(block));
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

    nbits = block->used_length >> TARGET_PAGE_BITS;

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
    /*
     * Mark as an end, in case the middle part is screwed up due to
     * some "mysterious" reason.
     */
    qemu_put_be64(file, RAMBLOCK_RECV_BITMAP_ENDING);
    qemu_fflush(file);

    g_free(le_bitmap);

    if (qemu_file_get_error(file)) {
        return qemu_file_get_error(file);
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
    /* QEMUFile used for this migration */
    QEMUFile *f;
    /* UFFD file descriptor, used in 'write-tracking' migration */
    int uffdio_fd;
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
    /* The free page optimization is enabled */
    bool fpo_enabled;
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

    /* compression statistics since the beginning of the period */
    /* amount of count that no free thread to compress data */
    uint64_t compress_thread_busy_prev;
    /* amount bytes after compression */
    uint64_t compressed_size_prev;
    /* amount of compressed pages */
    uint64_t compress_pages_prev;

    /* total handled target pages at the beginning of period */
    uint64_t target_page_count_prev;
    /* total handled target pages since start */
    uint64_t target_page_count;
    /* number of dirty bits in the bitmap */
    uint64_t migration_dirty_pages;
    /* Protects modification of the bitmap and migration dirty pages */
    QemuMutex bitmap_mutex;
    /* The RAMBlock used in the last src_page_requests */
    RAMBlock *last_req_rb;
    /* Queue of outstanding page requests from the destination */
    QemuMutex src_page_req_mutex;
    QSIMPLEQ_HEAD(, RAMSrcPageRequest) src_page_requests;
};
typedef struct RAMState RAMState;

static RAMState *ram_state;

static NotifierWithReturnList precopy_notifier_list;

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

void precopy_enable_free_page_optimization(void)
{
    if (!ram_state) {
        return;
    }

    ram_state->fpo_enabled = true;
}

uint64_t ram_bytes_remaining(void)
{
    return ram_state ? (ram_state->migration_dirty_pages * TARGET_PAGE_SIZE) :
                       0;
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

CompressionStats compression_counters;

struct CompressParam {
    bool done;
    bool quit;
    bool zero_page;
    QEMUFile *file;
    QemuMutex mutex;
    QemuCond cond;
    RAMBlock *block;
    ram_addr_t offset;

    /* internally used fields */
    z_stream stream;
    uint8_t *originbuf;
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
    z_stream stream;
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

static QEMUFile *decomp_file;
static DecompressParam *decomp_param;
static QemuThread *decompress_threads;
static QemuMutex decomp_done_lock;
static QemuCond decomp_done_cond;

static bool do_compress_ram_page(QEMUFile *f, z_stream *stream, RAMBlock *block,
                                 ram_addr_t offset, uint8_t *source_buf);

static void *do_data_compress(void *opaque)
{
    CompressParam *param = opaque;
    RAMBlock *block;
    ram_addr_t offset;
    bool zero_page;

    qemu_mutex_lock(&param->mutex);
    while (!param->quit) {
        if (param->block) {
            block = param->block;
            offset = param->offset;
            param->block = NULL;
            qemu_mutex_unlock(&param->mutex);

            zero_page = do_compress_ram_page(param->file, &param->stream,
                                             block, offset, param->originbuf);

            qemu_mutex_lock(&comp_done_lock);
            param->done = true;
            param->zero_page = zero_page;
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

static void compress_threads_save_cleanup(void)
{
    int i, thread_count;

    if (!migrate_use_compression() || !comp_param) {
        return;
    }

    thread_count = migrate_compress_threads();
    for (i = 0; i < thread_count; i++) {
        /*
         * we use it as a indicator which shows if the thread is
         * properly init'd or not
         */
        if (!comp_param[i].file) {
            break;
        }

        qemu_mutex_lock(&comp_param[i].mutex);
        comp_param[i].quit = true;
        qemu_cond_signal(&comp_param[i].cond);
        qemu_mutex_unlock(&comp_param[i].mutex);

        qemu_thread_join(compress_threads + i);
        qemu_mutex_destroy(&comp_param[i].mutex);
        qemu_cond_destroy(&comp_param[i].cond);
        deflateEnd(&comp_param[i].stream);
        g_free(comp_param[i].originbuf);
        qemu_fclose(comp_param[i].file);
        comp_param[i].file = NULL;
    }
    qemu_mutex_destroy(&comp_done_lock);
    qemu_cond_destroy(&comp_done_cond);
    g_free(compress_threads);
    g_free(comp_param);
    compress_threads = NULL;
    comp_param = NULL;
}

static int compress_threads_save_setup(void)
{
    int i, thread_count;

    if (!migrate_use_compression()) {
        return 0;
    }
    thread_count = migrate_compress_threads();
    compress_threads = g_new0(QemuThread, thread_count);
    comp_param = g_new0(CompressParam, thread_count);
    qemu_cond_init(&comp_done_cond);
    qemu_mutex_init(&comp_done_lock);
    for (i = 0; i < thread_count; i++) {
        comp_param[i].originbuf = g_try_malloc(TARGET_PAGE_SIZE);
        if (!comp_param[i].originbuf) {
            goto exit;
        }

        if (deflateInit(&comp_param[i].stream,
                        migrate_compress_level()) != Z_OK) {
            g_free(comp_param[i].originbuf);
            goto exit;
        }

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
    return 0;

exit:
    compress_threads_save_cleanup();
    return -1;
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
static void mig_throttle_guest_down(uint64_t bytes_dirty_period,
                                    uint64_t bytes_dirty_threshold)
{
    MigrationState *s = migrate_get_current();
    uint64_t pct_initial = s->parameters.cpu_throttle_initial;
    uint64_t pct_increment = s->parameters.cpu_throttle_increment;
    bool pct_tailslow = s->parameters.cpu_throttle_tailslow;
    int pct_max = s->parameters.max_cpu_throttle;

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
    if (!last_stage && encoded_len != 0) {
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
    bytes_xbzrle = save_page_header(rs, rs->f, block,
                                    offset | RAM_SAVE_FLAG_XBZRLE);
    qemu_put_byte(rs->f, ENCODING_FLAG_XBZRLE);
    qemu_put_be16(rs->f, encoded_len);
    qemu_put_buffer(rs->f, XBZRLE.encoded_buf, encoded_len);
    bytes_xbzrle += encoded_len + 1 + 2;
    /*
     * Like compressed_size (please see update_compress_thread_counts),
     * the xbzrle encoded bytes don't count the 8 byte header with
     * RAM_SAVE_FLAG_CONTINUE.
     */
    xbzrle_counters.bytes += bytes_xbzrle - 8;
    ram_counters.transferred += bytes_xbzrle;

    return 1;
}

/**
 * migration_bitmap_find_dirty: find the next dirty page from start
 *
 * Returns the page offset within memory region of the start of a dirty page
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

    if (ramblock_is_ignored(rb)) {
        return size;
    }

    /*
     * When the free page optimization is enabled, we need to check the bitmap
     * to send the non-free pages rather than all the pages in the bulk stage.
     */
    if (!rs->fpo_enabled && rs->ram_bulk_stage && start > 0) {
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

    qemu_mutex_lock(&rs->bitmap_mutex);

    /*
     * Clear dirty bitmap if needed.  This _must_ be called before we
     * send any of the page in the chunk because we need to make sure
     * we can capture further page content changes when we sync dirty
     * log the next time.  So as long as we are going to send any of
     * the page in the chunk we clear the remote dirty bitmap for all.
     * Clearing it earlier won't be a problem, but too late will.
     */
    if (rb->clear_bmap && clear_bmap_test_and_clear(rb, page)) {
        uint8_t shift = rb->clear_bmap_shift;
        hwaddr size = 1ULL << (TARGET_PAGE_BITS + shift);
        hwaddr start = (((ram_addr_t)page) << TARGET_PAGE_BITS) & (-size);

        /*
         * CLEAR_BITMAP_SHIFT_MIN should always guarantee this... this
         * can make things easier sometimes since then start address
         * of the small chunk will always be 64 pages aligned so the
         * bitmap will always be aligned to unsigned long.  We should
         * even be able to remove this restriction but I'm simply
         * keeping it.
         */
        assert(shift >= 6);
        trace_migration_bitmap_clear_dirty(rb->idstr, start, size, page);
        memory_region_clear_dirty_bitmap(rb->mr, start, size);
    }

    ret = test_and_clear_bit(page, rb->bmap);

    if (ret) {
        rs->migration_dirty_pages--;
    }
    qemu_mutex_unlock(&rs->bitmap_mutex);

    return ret;
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
    return  ram_counters.normal + ram_counters.duplicate +
                compression_counters.pages + xbzrle_counters.pages;
}

static void migration_update_rates(RAMState *rs, int64_t end_time)
{
    uint64_t page_count = rs->target_page_count - rs->target_page_count_prev;
    double compressed_size;

    /* calculate period counters */
    ram_counters.dirty_pages_rate = rs->num_dirty_pages_period * 1000
                / (end_time - rs->time_last_bitmap_sync);

    if (!page_count) {
        return;
    }

    if (migrate_use_xbzrle()) {
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

    if (migrate_use_compression()) {
        compression_counters.busy_rate = (double)(compression_counters.busy -
            rs->compress_thread_busy_prev) / page_count;
        rs->compress_thread_busy_prev = compression_counters.busy;

        compressed_size = compression_counters.compressed_size -
                          rs->compressed_size_prev;
        if (compressed_size) {
            double uncompressed_size = (compression_counters.pages -
                                    rs->compress_pages_prev) * TARGET_PAGE_SIZE;

            /* Compression-Ratio = Uncompressed-size / Compressed-size */
            compression_counters.compression_rate =
                                        uncompressed_size / compressed_size;

            rs->compress_pages_prev = compression_counters.pages;
            rs->compressed_size_prev = compression_counters.compressed_size;
        }
    }
}

static void migration_trigger_throttle(RAMState *rs)
{
    MigrationState *s = migrate_get_current();
    uint64_t threshold = s->parameters.throttle_trigger_threshold;

    uint64_t bytes_xfer_period = ram_counters.transferred - rs->bytes_xfer_prev;
    uint64_t bytes_dirty_period = rs->num_dirty_pages_period * TARGET_PAGE_SIZE;
    uint64_t bytes_dirty_threshold = bytes_xfer_period * threshold / 100;

    /* During block migration the auto-converge logic incorrectly detects
     * that ram migration makes no progress. Avoid this by disabling the
     * throttling logic during the bulk phase of block migration. */
    if (migrate_auto_converge() && !blk_mig_bulk_active()) {
        /* The following detection logic can be refined later. For now:
           Check to see if the ratio between dirtied bytes and the approx.
           amount of bytes that just got transferred since the last time
           we were in this routine reaches the threshold. If that happens
           twice, start or increase throttling. */

        if ((bytes_dirty_period > bytes_dirty_threshold) &&
            (++rs->dirty_rate_high_cnt >= 2)) {
            trace_migration_throttle();
            rs->dirty_rate_high_cnt = 0;
            mig_throttle_guest_down(bytes_dirty_period,
                                    bytes_dirty_threshold);
        }
    }
}

static void migration_bitmap_sync(RAMState *rs)
{
    RAMBlock *block;
    int64_t end_time;

    ram_counters.dirty_sync_count++;

    if (!rs->time_last_bitmap_sync) {
        rs->time_last_bitmap_sync = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    }

    trace_migration_bitmap_sync_start();
    memory_global_dirty_log_sync();

    qemu_mutex_lock(&rs->bitmap_mutex);
    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            ramblock_sync_dirty_bitmap(rs, block);
        }
        ram_counters.remaining = ram_bytes_remaining();
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
        rs->bytes_xfer_prev = ram_counters.transferred;
    }
    if (migrate_use_events()) {
        qapi_event_send_migration_pass(ram_counters.dirty_sync_count);
    }
}

static void migration_bitmap_sync_precopy(RAMState *rs)
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

    migration_bitmap_sync(rs);

    if (precopy_notify(PRECOPY_NOTIFY_AFTER_BITMAP_SYNC, &local_err)) {
        error_report_err(local_err);
    }
}

/**
 * save_zero_page_to_file: send the zero page to the file
 *
 * Returns the size of data written to the file, 0 means the page is not
 * a zero page
 *
 * @rs: current RAM state
 * @file: the file where the data is saved
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 */
static int save_zero_page_to_file(RAMState *rs, QEMUFile *file,
                                  RAMBlock *block, ram_addr_t offset)
{
    uint8_t *p = block->host + offset;
    int len = 0;

    if (is_zero_range(p, TARGET_PAGE_SIZE)) {
        len += save_page_header(rs, file, block, offset | RAM_SAVE_FLAG_ZERO);
        qemu_put_byte(file, 0);
        len += 1;
    }
    return len;
}

/**
 * save_zero_page: send the zero page to the stream
 *
 * Returns the number of pages written.
 *
 * @rs: current RAM state
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 */
static int save_zero_page(RAMState *rs, RAMBlock *block, ram_addr_t offset)
{
    int len = save_zero_page_to_file(rs, rs->f, block, offset);

    if (len) {
        ram_counters.duplicate++;
        ram_counters.transferred += len;
        return 1;
    }
    return -1;
}

static void ram_release_pages(const char *rbname, uint64_t offset, int pages)
{
    if (!migrate_release_ram() || !migration_in_postcopy()) {
        return;
    }

    ram_discard_range(rbname, offset, ((ram_addr_t)pages) << TARGET_PAGE_BITS);
}

/*
 * @pages: the number of pages written by the control path,
 *        < 0 - error
 *        > 0 - number of pages written
 *
 * Return true if the pages has been saved, otherwise false is returned.
 */
static bool control_save_page(RAMState *rs, RAMBlock *block, ram_addr_t offset,
                              int *pages)
{
    uint64_t bytes_xmit = 0;
    int ret;

    *pages = -1;
    ret = ram_control_save_page(rs->f, block->offset, offset, TARGET_PAGE_SIZE,
                                &bytes_xmit);
    if (ret == RAM_SAVE_CONTROL_NOT_SUPP) {
        return false;
    }

    if (bytes_xmit) {
        ram_counters.transferred += bytes_xmit;
        *pages = 1;
    }

    if (ret == RAM_SAVE_CONTROL_DELAYED) {
        return true;
    }

    if (bytes_xmit > 0) {
        ram_counters.normal++;
    } else if (bytes_xmit == 0) {
        ram_counters.duplicate++;
    }

    return true;
}

/*
 * directly send the page to the stream
 *
 * Returns the number of pages written.
 *
 * @rs: current RAM state
 * @block: block that contains the page we want to send
 * @offset: offset inside the block for the page
 * @buf: the page to be sent
 * @async: send to page asyncly
 */
static int save_normal_page(RAMState *rs, RAMBlock *block, ram_addr_t offset,
                            uint8_t *buf, bool async)
{
    ram_counters.transferred += save_page_header(rs, rs->f, block,
                                                 offset | RAM_SAVE_FLAG_PAGE);
    if (async) {
        qemu_put_buffer_async(rs->f, buf, TARGET_PAGE_SIZE,
                              migrate_release_ram() &
                              migration_in_postcopy());
    } else {
        qemu_put_buffer(rs->f, buf, TARGET_PAGE_SIZE);
    }
    ram_counters.transferred += TARGET_PAGE_SIZE;
    ram_counters.normal++;
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
 * @last_stage: if we are at the completion stage
 */
static int ram_save_page(RAMState *rs, PageSearchStatus *pss, bool last_stage)
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
    if (!rs->ram_bulk_stage && !migration_in_postcopy() &&
        migrate_use_xbzrle()) {
        pages = save_xbzrle_page(rs, &p, current_addr, block,
                                 offset, last_stage);
        if (!last_stage) {
            /* Can't send this cached data async, since the cache page
             * might get updated before it gets to the wire
             */
            send_async = false;
        }
    }

    /* XBZRLE overflow or normal page */
    if (pages == -1) {
        pages = save_normal_page(rs, block, offset, p, send_async);
    }

    XBZRLE_cache_unlock();

    return pages;
}

static int ram_save_multifd_page(RAMState *rs, RAMBlock *block,
                                 ram_addr_t offset)
{
    if (multifd_queue_page(rs->f, block, offset) < 0) {
        return -1;
    }
    ram_counters.normal++;

    return 1;
}

static bool do_compress_ram_page(QEMUFile *f, z_stream *stream, RAMBlock *block,
                                 ram_addr_t offset, uint8_t *source_buf)
{
    RAMState *rs = ram_state;
    uint8_t *p = block->host + (offset & TARGET_PAGE_MASK);
    bool zero_page = false;
    int ret;

    if (save_zero_page_to_file(rs, f, block, offset)) {
        zero_page = true;
        goto exit;
    }

    save_page_header(rs, f, block, offset | RAM_SAVE_FLAG_COMPRESS_PAGE);

    /*
     * copy it to a internal buffer to avoid it being modified by VM
     * so that we can catch up the error during compression and
     * decompression
     */
    memcpy(source_buf, p, TARGET_PAGE_SIZE);
    ret = qemu_put_compression_data(f, stream, source_buf, TARGET_PAGE_SIZE);
    if (ret < 0) {
        qemu_file_set_error(migrate_get_current()->to_dst_file, ret);
        error_report("compressed data failed!");
        return false;
    }

exit:
    ram_release_pages(block->idstr, offset & TARGET_PAGE_MASK, 1);
    return zero_page;
}

static void
update_compress_thread_counts(const CompressParam *param, int bytes_xmit)
{
    ram_counters.transferred += bytes_xmit;

    if (param->zero_page) {
        ram_counters.duplicate++;
        return;
    }

    /* 8 means a header with RAM_SAVE_FLAG_CONTINUE. */
    compression_counters.compressed_size += bytes_xmit - 8;
    compression_counters.pages++;
}

static bool save_page_use_compression(RAMState *rs);

static void flush_compressed_data(RAMState *rs)
{
    int idx, len, thread_count;

    if (!save_page_use_compression(rs)) {
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
            /*
             * it's safe to fetch zero_page without holding comp_done_lock
             * as there is no further request submitted to the thread,
             * i.e, the thread should be waiting for a request at this point.
             */
            update_compress_thread_counts(&comp_param[idx], len);
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
    bool wait = migrate_compress_wait_thread();

    thread_count = migrate_compress_threads();
    qemu_mutex_lock(&comp_done_lock);
retry:
    for (idx = 0; idx < thread_count; idx++) {
        if (comp_param[idx].done) {
            comp_param[idx].done = false;
            bytes_xmit = qemu_put_qemu_file(rs->f, comp_param[idx].file);
            qemu_mutex_lock(&comp_param[idx].mutex);
            set_compress_params(&comp_param[idx], block, offset);
            qemu_cond_signal(&comp_param[idx].cond);
            qemu_mutex_unlock(&comp_param[idx].mutex);
            pages = 1;
            update_compress_thread_counts(&comp_param[idx], bytes_xmit);
            break;
        }
    }

    /*
     * wait for the free thread if the user specifies 'compress-wait-thread',
     * otherwise we will post the page out in the main thread as normal page.
     */
    if (pages < 0 && wait) {
        qemu_cond_wait(&comp_done_cond, &comp_done_lock);
        goto retry;
    }
    qemu_mutex_unlock(&comp_done_lock);

    return pages;
}

/**
 * find_dirty_block: find the next dirty page and update any state
 * associated with the search process.
 *
 * Returns true if a page is found
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
    if ((((ram_addr_t)pss->page) << TARGET_PAGE_BITS)
        >= pss->block->used_length) {
        /* Didn't find anything in this RAM Block */
        pss->page = 0;
        pss->block = QLIST_NEXT_RCU(pss->block, next);
        if (!pss->block) {
            /*
             * If memory migration starts over, we will meet a dirtied page
             * which may still exists in compression threads's ring, so we
             * should flush the compressed data to make sure the new page
             * is not overwritten by the old one in the destination.
             *
             * Also If xbzrle is on, stop using the data compression at this
             * point. In theory, xbzrle can do better than compression.
             */
            flush_compressed_data(rs);

            /* Hit the end of the list */
            pss->block = QLIST_FIRST_RCU(&ram_list.blocks);
            /* Flag that we've looped */
            pss->complete_round = true;
            rs->ram_bulk_stage = false;
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

    if (QSIMPLEQ_EMPTY_ATOMIC(&rs->src_page_requests)) {
        return NULL;
    }

    QEMU_LOCK_GUARD(&rs->src_page_req_mutex);
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
            migration_consume_urgent_request();
        }
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
    RAMBlock *bs;
    int res;

    if (!migrate_background_snapshot()) {
        return NULL;
    }

    res = uffd_read_events(rs->uffdio_fd, &uffd_msg, 1);
    if (res <= 0) {
        return NULL;
    }

    page_address = (void *)(uintptr_t) uffd_msg.arg.pagefault.address;
    bs = qemu_ram_block_from_host(page_address, false, offset);
    assert(bs && (bs->flags & RAM_UF_WRITEPROTECT) != 0);
    return bs;
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
        uint64_t run_length = (pss->page - start_page + 1) << TARGET_PAGE_BITS;

        /* Flush async buffers before un-protect. */
        qemu_fflush(rs->f);
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
    RAMBlock *bs;
    bool ret = false;

    /* Open UFFD file descriptor */
    uffd_fd = uffd_create_fd(UFFD_FEATURE_PAGEFAULT_FLAG_WP, false);
    if (uffd_fd < 0) {
        return false;
    }

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(bs) {
        uint64_t uffd_ioctls;

        /* Nothing to do with read-only and MMIO-writable regions */
        if (bs->mr->readonly || bs->mr->rom_device) {
            continue;
        }
        /* Try to register block memory via UFFD-IO to track writes */
        if (uffd_register_memory(uffd_fd, bs->host, bs->max_length,
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

/*
 * ram_write_tracking_start: start UFFD-WP memory tracking
 *
 * Returns 0 for success or negative value in case of error
 */
int ram_write_tracking_start(void)
{
    int uffd_fd;
    RAMState *rs = ram_state;
    RAMBlock *bs;

    /* Open UFFD file descriptor */
    uffd_fd = uffd_create_fd(UFFD_FEATURE_PAGEFAULT_FLAG_WP, true);
    if (uffd_fd < 0) {
        return uffd_fd;
    }
    rs->uffdio_fd = uffd_fd;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(bs) {
        /* Nothing to do with read-only and MMIO-writable regions */
        if (bs->mr->readonly || bs->mr->rom_device) {
            continue;
        }

        /* Register block memory with UFFD to track writes */
        if (uffd_register_memory(rs->uffdio_fd, bs->host,
                bs->max_length, UFFDIO_REGISTER_MODE_WP, NULL)) {
            goto fail;
        }
        /* Apply UFFD write protection to the block memory range */
        if (uffd_change_protection(rs->uffdio_fd, bs->host,
                bs->max_length, true, false)) {
            goto fail;
        }
        bs->flags |= RAM_UF_WRITEPROTECT;
        memory_region_ref(bs->mr);

        trace_ram_write_tracking_ramblock_start(bs->idstr, bs->page_size,
                bs->host, bs->max_length);
    }

    return 0;

fail:
    error_report("ram_write_tracking_start() failed: restoring initial memory state");

    RAMBLOCK_FOREACH_NOT_IGNORED(bs) {
        if ((bs->flags & RAM_UF_WRITEPROTECT) == 0) {
            continue;
        }
        /*
         * In case some memory block failed to be write-protected
         * remove protection and unregister all succeeded RAM blocks
         */
        uffd_change_protection(rs->uffdio_fd, bs->host, bs->max_length, false, false);
        uffd_unregister_memory(rs->uffdio_fd, bs->host, bs->max_length);
        /* Cleanup flags and remove reference */
        bs->flags &= ~RAM_UF_WRITEPROTECT;
        memory_region_unref(bs->mr);
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
    RAMBlock *bs;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(bs) {
        if ((bs->flags & RAM_UF_WRITEPROTECT) == 0) {
            continue;
        }
        /* Remove protection and unregister all affected RAM blocks */
        uffd_change_protection(rs->uffdio_fd, bs->host, bs->max_length, false, false);
        uffd_unregister_memory(rs->uffdio_fd, bs->host, bs->max_length);

        trace_ram_write_tracking_ramblock_stop(bs->idstr, bs->page_size,
                bs->host, bs->max_length);

        /* Cleanup flags and remove reference */
        bs->flags &= ~RAM_UF_WRITEPROTECT;
        memory_region_unref(bs->mr);
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
int ram_save_queue_pages(const char *rbname, ram_addr_t start, ram_addr_t len)
{
    RAMBlock *ramblock;
    RAMState *rs = ram_state;

    ram_counters.postcopy_requests++;
    RCU_READ_LOCK_GUARD();

    if (!rbname) {
        /* Reuse last RAMBlock */
        ramblock = rs->last_req_rb;

        if (!ramblock) {
            /*
             * Shouldn't happen, we can't reuse the last RAMBlock if
             * it's the 1st request.
             */
            error_report("ram_save_queue_pages no previous block");
            return -1;
        }
    } else {
        ramblock = qemu_ram_block_by_name(rbname);

        if (!ramblock) {
            /* We shouldn't be asked for a non-existent RAMBlock */
            error_report("ram_save_queue_pages no block '%s'", rbname);
            return -1;
        }
        rs->last_req_rb = ramblock;
    }
    trace_ram_save_queue_pages(ramblock->idstr, start, len);
    if (start + len > ramblock->used_length) {
        error_report("%s request overrun start=" RAM_ADDR_FMT " len="
                     RAM_ADDR_FMT " blocklen=" RAM_ADDR_FMT,
                     __func__, start, len, ramblock->used_length);
        return -1;
    }

    struct RAMSrcPageRequest *new_entry =
        g_malloc0(sizeof(struct RAMSrcPageRequest));
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

static bool save_page_use_compression(RAMState *rs)
{
    if (!migrate_use_compression()) {
        return false;
    }

    /*
     * If xbzrle is on, stop using the data compression after first
     * round of migration even if compression is enabled. In theory,
     * xbzrle can do better than compression.
     */
    if (rs->ram_bulk_stage || !migrate_use_xbzrle()) {
        return true;
    }

    return false;
}

/*
 * try to compress the page before posting it out, return true if the page
 * has been properly handled by compression, otherwise needs other
 * paths to handle it
 */
static bool save_compress_page(RAMState *rs, RAMBlock *block, ram_addr_t offset)
{
    if (!save_page_use_compression(rs)) {
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
    if (block != rs->last_sent_block) {
        flush_compressed_data(rs);
        return false;
    }

    if (compress_page_with_multi_thread(rs, block, offset) > 0) {
        return true;
    }

    compression_counters.busy++;
    return false;
}

/**
 * ram_save_target_page: save one target page
 *
 * Returns the number of pages written
 *
 * @rs: current RAM state
 * @pss: data about the page we want to send
 * @last_stage: if we are at the completion stage
 */
static int ram_save_target_page(RAMState *rs, PageSearchStatus *pss,
                                bool last_stage)
{
    RAMBlock *block = pss->block;
    ram_addr_t offset = ((ram_addr_t)pss->page) << TARGET_PAGE_BITS;
    int res;

    if (control_save_page(rs, block, offset, &res)) {
        return res;
    }

    if (save_compress_page(rs, block, offset)) {
        return 1;
    }

    res = save_zero_page(rs, block, offset);
    if (res > 0) {
        /* Must let xbzrle know, otherwise a previous (now 0'd) cached
         * page would be stale
         */
        if (!save_page_use_compression(rs)) {
            XBZRLE_cache_lock();
            xbzrle_cache_zero_page(rs, block->offset + offset);
            XBZRLE_cache_unlock();
        }
        ram_release_pages(block->idstr, offset, res);
        return res;
    }

    /*
     * Do not use multifd for:
     * 1. Compression as the first page in the new block should be posted out
     *    before sending the compressed page
     * 2. In postcopy as one whole host page should be placed
     */
    if (!save_page_use_compression(rs) && migrate_use_multifd()
        && !migration_in_postcopy()) {
        return ram_save_multifd_page(rs, block, offset);
    }

    return ram_save_page(rs, pss, last_stage);
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
    unsigned long start_page = pss->page;
    int res;

    if (ramblock_is_ignored(pss->block)) {
        error_report("block %s should not be migrated !", pss->block->idstr);
        return 0;
    }

    do {
        /* Check the pages is dirty and if it is send it */
        if (!migration_bitmap_clear_dirty(rs, pss->block, pss->page)) {
            pss->page++;
            continue;
        }

        tmppages = ram_save_target_page(rs, pss, last_stage);
        if (tmppages < 0) {
            return tmppages;
        }

        pages += tmppages;
        pss->page++;
        /* Allow rate limiting to happen in the middle of huge pages */
        migration_rate_limit();
    } while ((pss->page & (pagesize_bits - 1)) &&
             offset_in_ramblock(pss->block,
                                ((ram_addr_t)pss->page) << TARGET_PAGE_BITS));
    /* The offset we leave with is the last one we looked at */
    pss->page--;

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

static uint64_t ram_bytes_total_common(bool count_ignored)
{
    RAMBlock *block;
    uint64_t total = 0;

    RCU_READ_LOCK_GUARD();

    if (count_ignored) {
        RAMBLOCK_FOREACH_MIGRATABLE(block) {
            total += block->used_length;
        }
    } else {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            total += block->used_length;
        }
    }
    return total;
}

uint64_t ram_bytes_total(void)
{
    return ram_bytes_total_common(false);
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
        memory_global_dirty_log_stop();
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
}

static void ram_state_reset(RAMState *rs)
{
    rs->last_seen_block = NULL;
    rs->last_sent_block = NULL;
    rs->last_page = 0;
    rs->last_version = ram_list.version;
    rs->ram_bulk_stage = true;
    rs->fpo_enabled = false;
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
 * Returns zero on success
 *
 * Callback from postcopy_each_ram_send_discard for each RAMBlock
 *
 * @ms: current migration state
 * @block: RAMBlock to discard
 */
static int postcopy_send_discard_bm_ram(MigrationState *ms, RAMBlock *block)
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

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        postcopy_discard_send_init(ms, block->idstr);

        /*
         * Postcopy sends chunks of bitmap over the wire, but it
         * just needs indexes at this point, avoids it having
         * target page specific code.
         */
        ret = postcopy_send_discard_bm_ram(ms, block);
        postcopy_discard_send_finish(ms);
        if (ret) {
            return ret;
        }
    }

    return 0;
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
 * postcopy_chunk_hostpages: discard any partially sent host page
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
    postcopy_discard_send_init(ms, block->idstr);

    /*
     * Ensure that all partially dirty host pages are made fully dirty.
     */
    postcopy_chunk_hostpages_pass(ms, block);

    postcopy_discard_send_finish(ms);
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

    RCU_READ_LOCK_GUARD();

    /* This should be our last sync, the src is now paused */
    migration_bitmap_sync(rs);

    /* Easiest way to make sure we don't resume in the middle of a host-page */
    rs->last_seen_block = NULL;
    rs->last_sent_block = NULL;
    rs->last_page = 0;

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        /* Deal with TPS != HPS and huge pages */
        ret = postcopy_chunk_hostpages(ms, block);
        if (ret) {
            return ret;
        }

#ifdef DEBUG_POSTCOPY
        ram_debug_dump_bitmap(block->bmap, true,
                              block->used_length >> TARGET_PAGE_BITS);
#endif
    }
    trace_ram_postcopy_send_discard_bitmap();

    return postcopy_each_ram_send_discard(ms);
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

    if (!migrate_use_xbzrle()) {
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

    /*
     * Count the total number of pages used by ram blocks not including any
     * gaps due to alignment or unplugs.
     * This must match with the initial values of dirty bitmap.
     */
    (*rsp)->migration_dirty_pages = ram_bytes_total() >> TARGET_PAGE_BITS;
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

static void ram_init_bitmaps(RAMState *rs)
{
    /* For memory_global_dirty_log_start below.  */
    qemu_mutex_lock_iothread();
    qemu_mutex_lock_ramlist();

    WITH_RCU_READ_LOCK_GUARD() {
        ram_list_init_bitmaps();
        /* We don't use dirty log with background snapshots */
        if (!migrate_background_snapshot()) {
            memory_global_dirty_log_start();
            migration_bitmap_sync_precopy(rs);
        }
    }
    qemu_mutex_unlock_ramlist();
    qemu_mutex_unlock_iothread();
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

    rs->last_seen_block = NULL;
    rs->last_sent_block = NULL;
    rs->last_page = 0;
    rs->last_version = ram_list.version;
    /*
     * Disable the bulk stage, otherwise we'll resend the whole RAM no
     * matter what we have sent.
     */
    rs->ram_bulk_stage = false;

    /* Update RAMState cache of output QEMUFile */
    rs->f = out;

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
    (*rsp)->f = f;

    WITH_RCU_READ_LOCK_GUARD() {
        qemu_put_be64(f, ram_bytes_total_common(true) | RAM_SAVE_FLAG_MEM_SIZE);

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

    ram_control_before_iterate(f, RAM_CONTROL_SETUP);
    ram_control_after_iterate(f, RAM_CONTROL_SETUP);

    multifd_send_sync_main(f);
    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    qemu_fflush(f);

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

    WITH_RCU_READ_LOCK_GUARD() {
        if (ram_list.version != rs->last_version) {
            ram_state_reset(rs);
        }

        /* Read version before ram_list.blocks */
        smp_rmb();

        ram_control_before_iterate(f, RAM_CONTROL_ROUND);

        t0 = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        i = 0;
        while ((ret = qemu_file_rate_limit(f)) == 0 ||
                !QSIMPLEQ_EMPTY(&rs->src_page_requests)) {
            int pages;

            if (qemu_file_get_error(f)) {
                break;
            }

            pages = ram_find_and_save_block(rs, false);
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
                flush_compressed_data(rs);
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

    /*
     * Must occur before EOS (or any QEMUFile operation)
     * because of RDMA protocol.
     */
    ram_control_after_iterate(f, RAM_CONTROL_ROUND);

out:
    if (ret >= 0
        && migration_is_setup_or_active(migrate_get_current()->state)) {
        multifd_send_sync_main(rs->f);
        qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
        qemu_fflush(f);
        ram_counters.transferred += 8;

        ret = qemu_file_get_error(f);
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

    WITH_RCU_READ_LOCK_GUARD() {
        if (!migration_in_postcopy()) {
            migration_bitmap_sync_precopy(rs);
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
            if (pages < 0) {
                ret = pages;
                break;
            }
        }

        flush_compressed_data(rs);
        ram_control_after_iterate(f, RAM_CONTROL_FINISH);
    }

    if (ret >= 0) {
        multifd_send_sync_main(rs->f);
        qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
        qemu_fflush(f);
    }

    return ret;
}

static void ram_save_pending(QEMUFile *f, void *opaque, uint64_t max_size,
                             uint64_t *res_precopy_only,
                             uint64_t *res_compatible,
                             uint64_t *res_postcopy_only)
{
    RAMState **temp = opaque;
    RAMState *rs = *temp;
    uint64_t remaining_size;

    remaining_size = rs->migration_dirty_pages * TARGET_PAGE_SIZE;

    if (!migration_in_postcopy() &&
        remaining_size < max_size) {
        qemu_mutex_lock_iothread();
        WITH_RCU_READ_LOCK_GUARD() {
            migration_bitmap_sync_precopy(rs);
        }
        qemu_mutex_unlock_iothread();
        remaining_size = rs->migration_dirty_pages * TARGET_PAGE_SIZE;
    }

    if (migrate_postcopy_ram()) {
        /* We can do postcopy, and all the data is postcopiable */
        *res_compatible += remaining_size;
    } else {
        *res_precopy_only += remaining_size;
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
 * @f: QEMUFile where to read the data from
 * @flags: Page flags (mostly to see if it's a continuation of previous block)
 */
static inline RAMBlock *ram_block_from_stream(QEMUFile *f, int flags)
{
    static RAMBlock *block;
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

    if (ramblock_is_ignored(block)) {
        error_report("block %s should not be migrated !", id);
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
    if (record_bitmap &&
        !test_and_set_bit(offset >> TARGET_PAGE_BITS, block->bmap)) {
        ram_state->migration_dirty_pages++;
    }
    return block->colo_cache + offset;
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

/* return the size after decompression, or negative value on error */
static int
qemu_uncompress_data(z_stream *stream, uint8_t *dest, size_t dest_len,
                     const uint8_t *source, size_t source_len)
{
    int err;

    err = inflateReset(stream);
    if (err != Z_OK) {
        return -1;
    }

    stream->avail_in = source_len;
    stream->next_in = (uint8_t *)source;
    stream->avail_out = dest_len;
    stream->next_out = dest;

    err = inflate(stream, Z_NO_FLUSH);
    if (err != Z_STREAM_END) {
        return -1;
    }

    return stream->total_out;
}

static void *do_data_decompress(void *opaque)
{
    DecompressParam *param = opaque;
    unsigned long pagesize;
    uint8_t *des;
    int len, ret;

    qemu_mutex_lock(&param->mutex);
    while (!param->quit) {
        if (param->des) {
            des = param->des;
            len = param->len;
            param->des = 0;
            qemu_mutex_unlock(&param->mutex);

            pagesize = TARGET_PAGE_SIZE;

            ret = qemu_uncompress_data(&param->stream, des, pagesize,
                                       param->compbuf, len);
            if (ret < 0 && migrate_get_current()->decompress_error_check) {
                error_report("decompress data failed");
                qemu_file_set_error(decomp_file, ret);
            }

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

static int wait_for_decompress_done(void)
{
    int idx, thread_count;

    if (!migrate_use_compression()) {
        return 0;
    }

    thread_count = migrate_decompress_threads();
    qemu_mutex_lock(&decomp_done_lock);
    for (idx = 0; idx < thread_count; idx++) {
        while (!decomp_param[idx].done) {
            qemu_cond_wait(&decomp_done_cond, &decomp_done_lock);
        }
    }
    qemu_mutex_unlock(&decomp_done_lock);
    return qemu_file_get_error(decomp_file);
}

static void compress_threads_load_cleanup(void)
{
    int i, thread_count;

    if (!migrate_use_compression()) {
        return;
    }
    thread_count = migrate_decompress_threads();
    for (i = 0; i < thread_count; i++) {
        /*
         * we use it as a indicator which shows if the thread is
         * properly init'd or not
         */
        if (!decomp_param[i].compbuf) {
            break;
        }

        qemu_mutex_lock(&decomp_param[i].mutex);
        decomp_param[i].quit = true;
        qemu_cond_signal(&decomp_param[i].cond);
        qemu_mutex_unlock(&decomp_param[i].mutex);
    }
    for (i = 0; i < thread_count; i++) {
        if (!decomp_param[i].compbuf) {
            break;
        }

        qemu_thread_join(decompress_threads + i);
        qemu_mutex_destroy(&decomp_param[i].mutex);
        qemu_cond_destroy(&decomp_param[i].cond);
        inflateEnd(&decomp_param[i].stream);
        g_free(decomp_param[i].compbuf);
        decomp_param[i].compbuf = NULL;
    }
    g_free(decompress_threads);
    g_free(decomp_param);
    decompress_threads = NULL;
    decomp_param = NULL;
    decomp_file = NULL;
}

static int compress_threads_load_setup(QEMUFile *f)
{
    int i, thread_count;

    if (!migrate_use_compression()) {
        return 0;
    }

    thread_count = migrate_decompress_threads();
    decompress_threads = g_new0(QemuThread, thread_count);
    decomp_param = g_new0(DecompressParam, thread_count);
    qemu_mutex_init(&decomp_done_lock);
    qemu_cond_init(&decomp_done_cond);
    decomp_file = f;
    for (i = 0; i < thread_count; i++) {
        if (inflateInit(&decomp_param[i].stream) != Z_OK) {
            goto exit;
        }

        decomp_param[i].compbuf = g_malloc0(compressBound(TARGET_PAGE_SIZE));
        qemu_mutex_init(&decomp_param[i].mutex);
        qemu_cond_init(&decomp_param[i].cond);
        decomp_param[i].done = true;
        decomp_param[i].quit = false;
        qemu_thread_create(decompress_threads + i, "decompress",
                           do_data_decompress, decomp_param + i,
                           QEMU_THREAD_JOINABLE);
    }
    return 0;
exit:
    compress_threads_load_cleanup();
    return -1;
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

 /*
  * we must set ram_bulk_stage to false, otherwise in
  * migation_bitmap_find_dirty the bitmap will be unused and
  * all the pages in ram cache wil be flushed to the ram of
  * secondary VM.
  */
static void colo_init_ram_state(void)
{
    ram_state_init(&ram_state);
    ram_state->ram_bulk_stage = false;
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
                                                    NULL,
                                                    false);
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
        }
    }

    /*
    * Record the dirty pages that sent by PVM, we use this dirty bitmap together
    * with to decide which page in cache should be flushed into SVM's RAM. Here
    * we use the same name 'ram_bitmap' as for migration.
    */
    if (ram_bytes_total()) {
        RAMBlock *block;

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

    memory_global_dirty_log_sync();
    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            ramblock_sync_dirty_bitmap(ram_state, block);
            /* Discard this dirty bitmap record */
            bitmap_zero(block->bmap, block->max_length >> TARGET_PAGE_BITS);
        }
        memory_global_dirty_log_start();
    }
    ram_state->migration_dirty_pages = 0;
    qemu_mutex_unlock_ramlist();
    qemu_mutex_unlock_iothread();
}

/* It is need to hold the global lock to call this helper */
void colo_release_ram_cache(void)
{
    RAMBlock *block;

    memory_global_dirty_log_stop();
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
    if (compress_threads_load_setup(f)) {
        return -1;
    }

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
    compress_threads_load_cleanup();

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
 */
static int ram_load_postcopy(QEMUFile *f)
{
    int flags = 0, ret = 0;
    bool place_needed = false;
    bool matches_target_page_size = false;
    MigrationIncomingState *mis = migration_incoming_get_current();
    /* Temporary page that is later 'placed' */
    void *postcopy_host_page = mis->postcopy_tmp_page;
    void *this_host = NULL;
    bool all_zero = true;
    int target_pages = 0;

    while (!ret && !(flags & RAM_SAVE_FLAG_EOS)) {
        ram_addr_t addr;
        void *host = NULL;
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

        trace_ram_load_postcopy_loop((uint64_t)addr, flags);
        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE |
                     RAM_SAVE_FLAG_COMPRESS_PAGE)) {
            block = ram_block_from_stream(f, flags);

            host = host_from_ram_block_offset(block, addr);
            if (!host) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            target_pages++;
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
            page_buffer = postcopy_host_page +
                          ((uintptr_t)host & (block->page_size - 1));
            if (target_pages == 1) {
                this_host = (void *)QEMU_ALIGN_DOWN((uintptr_t)host,
                                                    block->page_size);
            } else {
                /* not the 1st TP within the HP */
                if (QEMU_ALIGN_DOWN((uintptr_t)host, block->page_size) !=
                    (uintptr_t)this_host) {
                    error_report("Non-same host page %p/%p",
                                  host, this_host);
                    ret = -EINVAL;
                    break;
                }
            }

            /*
             * If it's the last part of a host page then we place the host
             * page
             */
            if (target_pages == (block->page_size / TARGET_PAGE_SIZE)) {
                place_needed = true;
            }
            place_source = postcopy_host_page;
        }

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_ZERO:
            ch = qemu_get_byte(f);
            /*
             * Can skip to set page_buffer when
             * this is a zero page and (block->page_size == TARGET_PAGE_SIZE).
             */
            if (ch || !matches_target_page_size) {
                memset(page_buffer, ch, TARGET_PAGE_SIZE);
            }
            if (ch) {
                all_zero = false;
            }
            break;

        case RAM_SAVE_FLAG_PAGE:
            all_zero = false;
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
            all_zero = false;
            len = qemu_get_be32(f);
            if (len < 0 || len > compressBound(TARGET_PAGE_SIZE)) {
                error_report("Invalid compressed data length: %d", len);
                ret = -EINVAL;
                break;
            }
            decompress_data_with_multi_threads(f, page_buffer, len);
            break;

        case RAM_SAVE_FLAG_EOS:
            /* normal exit */
            multifd_recv_sync_main();
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
            /* This gets called at the last target page in the host page */
            void *place_dest = (void *)QEMU_ALIGN_DOWN((uintptr_t)host,
                                                       block->page_size);

            if (all_zero) {
                ret = postcopy_place_page_zero(mis, place_dest,
                                               block);
            } else {
                ret = postcopy_place_page(mis, place_dest,
                                          place_source, block);
            }
            place_needed = false;
            target_pages = 0;
            /* Assume we have a zero page until we detect something different */
            all_zero = true;
        }
    }

    return ret;
}

static bool postcopy_is_advised(void)
{
    PostcopyState ps = postcopy_state_get();
    return ps >= POSTCOPY_INCOMING_ADVISE && ps < POSTCOPY_INCOMING_END;
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

    memory_global_dirty_log_sync();
    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            ramblock_sync_dirty_bitmap(ram_state, block);
        }
    }

    trace_colo_flush_ram_cache_begin(ram_state->migration_dirty_pages);
    WITH_RCU_READ_LOCK_GUARD() {
        block = QLIST_FIRST_RCU(&ram_list.blocks);

        while (block) {
            offset = migration_bitmap_find_dirty(ram_state, block, offset);

            if (((ram_addr_t)offset) << TARGET_PAGE_BITS
                >= block->used_length) {
                offset = 0;
                block = QLIST_NEXT_RCU(block, next);
            } else {
                migration_bitmap_clear_dirty(ram_state, block, offset);
                dst_host = block->host
                         + (((ram_addr_t)offset) << TARGET_PAGE_BITS);
                src_host = block->colo_cache
                         + (((ram_addr_t)offset) << TARGET_PAGE_BITS);
                memcpy(dst_host, src_host, TARGET_PAGE_SIZE);
            }
        }
    }
    trace_colo_flush_ram_cache_end();
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
    int flags = 0, ret = 0, invalid_flags = 0, len = 0, i = 0;
    /* ADVISE is earlier, it shows the source has the postcopy capability on */
    bool postcopy_advised = postcopy_is_advised();
    if (!migrate_use_compression()) {
        invalid_flags |= RAM_SAVE_FLAG_COMPRESS_PAGE;
    }

    while (!ret && !(flags & RAM_SAVE_FLAG_EOS)) {
        ram_addr_t addr, total_ram_bytes;
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
            RAMBlock *block = ram_block_from_stream(f, flags);

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
                if (block && !qemu_ram_is_migratable(block)) {
                    error_report("block %s should not be migrated !", id);
                    ret = -EINVAL;
                } else if (block) {
                    if (length != block->used_length) {
                        Error *local_err = NULL;

                        ret = qemu_ram_resize(block, length,
                                              &local_err);
                        if (local_err) {
                            error_report_err(local_err);
                        }
                    }
                    /* For postcopy we need to check hugepage sizes match */
                    if (postcopy_advised && migrate_postcopy_ram() &&
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
                    if (migrate_ignore_shared()) {
                        hwaddr addr = qemu_get_be64(f);
                        if (ramblock_is_ignored(block) &&
                            block->mr->addr != addr) {
                            error_report("Mismatched GPAs for block %s "
                                         "%" PRId64 "!= %" PRId64,
                                         id, (uint64_t)addr,
                                         (uint64_t)block->mr->addr);
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
            multifd_recv_sync_main();
            break;
        default:
            if (flags & RAM_SAVE_FLAG_HOOK) {
                ram_control_load_hook(f, RAM_CONTROL_HOOK, NULL);
            } else {
                error_report("Unknown combination of migration flags: 0x%x",
                             flags);
                ret = -EINVAL;
            }
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
            ret = ram_load_postcopy(f);
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
    int ramblock_count = 0;

    trace_ram_dirty_bitmap_sync_start();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        qemu_savevm_send_recv_bitmap(file, block->idstr);
        trace_ram_dirty_bitmap_request(block->idstr);
        ramblock_count++;
    }

    trace_ram_dirty_bitmap_sync_wait();

    /* Wait until all the ramblocks' dirty bitmap synced */
    while (ramblock_count--) {
        qemu_sem_wait(&s->rp_state.rp_sem);
    }

    trace_ram_dirty_bitmap_sync_complete();

    return 0;
}

static void ram_dirty_bitmap_reload_notify(MigrationState *s)
{
    qemu_sem_post(&s->rp_state.rp_sem);
}

/*
 * Read the received bitmap, revert it as the initial dirty bitmap.
 * This is only used when the postcopy migration is paused but wants
 * to resume from a middle point.
 */
int ram_dirty_bitmap_reload(MigrationState *s, RAMBlock *block)
{
    int ret = -EINVAL;
    QEMUFile *file = s->rp_state.from_dst_file;
    unsigned long *le_bitmap, nbits = block->used_length >> TARGET_PAGE_BITS;
    uint64_t local_size = DIV_ROUND_UP(nbits, 8);
    uint64_t size, end_mark;

    trace_ram_dirty_bitmap_reload_begin(block->idstr);

    if (s->state != MIGRATION_STATUS_POSTCOPY_RECOVER) {
        error_report("%s: incorrect state %s", __func__,
                     MigrationStatus_str(s->state));
        return -EINVAL;
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
        error_report("%s: ramblock '%s' bitmap size mismatch "
                     "(0x%"PRIx64" != 0x%"PRIx64")", __func__,
                     block->idstr, size, local_size);
        ret = -EINVAL;
        goto out;
    }

    size = qemu_get_buffer(file, (uint8_t *)le_bitmap, local_size);
    end_mark = qemu_get_be64(file);

    ret = qemu_file_get_error(file);
    if (ret || size != local_size) {
        error_report("%s: read bitmap failed for ramblock '%s': %d"
                     " (size 0x%"PRIx64", got: 0x%"PRIx64")",
                     __func__, block->idstr, ret, local_size, size);
        ret = -EIO;
        goto out;
    }

    if (end_mark != RAMBLOCK_RECV_BITMAP_ENDING) {
        error_report("%s: ramblock '%s' end mark incorrect: 0x%"PRIx64,
                     __func__, block->idstr, end_mark);
        ret = -EINVAL;
        goto out;
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

    trace_ram_dirty_bitmap_reload_complete(block->idstr);

    /*
     * We succeeded to sync bitmap for current ramblock. If this is
     * the last one to sync, we need to notify the main send thread.
     */
    ram_dirty_bitmap_reload_notify(s);

    ret = 0;
out:
    g_free(le_bitmap);
    return ret;
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

static SaveVMHandlers savevm_ram_handlers = {
    .save_setup = ram_save_setup,
    .save_live_iterate = ram_save_iterate,
    .save_live_complete_postcopy = ram_save_complete,
    .save_live_complete_precopy = ram_save_complete,
    .has_postcopy = ram_has_postcopy,
    .save_live_pending = ram_save_pending,
    .load_state = ram_load,
    .save_cleanup = ram_save_cleanup,
    .load_setup = ram_load_setup,
    .load_cleanup = ram_load_cleanup,
    .resume_prepare = ram_resume_prepare,
};

void ram_mig_init(void)
{
    qemu_mutex_init(&XBZRLE.lock);
    register_savevm_live("ram", 0, 4, &savevm_ram_handlers, &ram_state);
}
