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
#include "qemu/cutils.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "qemu/main-loop.h"
#include "qemu/pmem.h"
#include "xbzrle.h"
#include "ram.h"
#include "migration.h"
#include "socket.h"
#include "migration/register.h"
#include "migration/misc.h"
#include "qemu-file.h"
#include "postcopy-ram.h"
#include "page_cache.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-events-migration.h"
#include "qapi/qmp/qerror.h"
#include "trace.h"
#include "exec/ram_addr.h"
#include "exec/target_page.h"
#include "qemu/rcu_queue.h"
#include "migration/colo.h"
#include "block.h"
#include "sysemu/sysemu.h"
#include "qemu/uuid.h"
#include "savevm.h"
#include "qemu/iov.h"

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
 * Returns 0 for success or -1 for error
 *
 * @new_size: new cache size
 * @errp: set *errp if the check failed, with reason
 */
int xbzrle_cache_resize(int64_t new_size, Error **errp)
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

static bool ramblock_is_ignored(RAMBlock *block)
{
    return !qemu_ram_is_migratable(block) ||
           (migrate_ignore_shared() && qemu_ram_is_shared(block));
}

/* Should be holding either ram_list.mutex, or the RCU lock. */
#define RAMBLOCK_FOREACH_NOT_IGNORED(block)            \
    INTERNAL_RAMBLOCK_FOREACH(block)                   \
        if (ramblock_is_ignored(block)) {} else

#define RAMBLOCK_FOREACH_MIGRATABLE(block)             \
    INTERNAL_RAMBLOCK_FOREACH(block)                   \
        if (!qemu_ram_is_migratable(block)) {} else

#undef RAMBLOCK_FOREACH

int foreach_not_ignored_block(RAMBlockIterFunc func, void *opaque)
{
    RAMBlock *block;
    int ret = 0;

    rcu_read_lock();
    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        ret = func(block, opaque);
        if (ret) {
            break;
        }
    }
    rcu_read_unlock();
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
     * same endianess. (Note: big endian won't work.)
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
     * some "misterious" reason.
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

/* Multiple fd's */

#define MULTIFD_MAGIC 0x11223344U
#define MULTIFD_VERSION 1

#define MULTIFD_FLAG_SYNC (1 << 0)

/* This value needs to be a multiple of qemu_target_page_size() */
#define MULTIFD_PACKET_SIZE (512 * 1024)

typedef struct {
    uint32_t magic;
    uint32_t version;
    unsigned char uuid[16]; /* QemuUUID */
    uint8_t id;
    uint8_t unused1[7];     /* Reserved for future use */
    uint64_t unused2[4];    /* Reserved for future use */
} __attribute__((packed)) MultiFDInit_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    /* maximum number of allocated pages */
    uint32_t pages_alloc;
    uint32_t pages_used;
    /* size of the next packet that contains pages */
    uint32_t next_packet_size;
    uint64_t packet_num;
    uint64_t unused[4];    /* Reserved for future use */
    char ramblock[256];
    uint64_t offset[];
} __attribute__((packed)) MultiFDPacket_t;

typedef struct {
    /* number of used pages */
    uint32_t used;
    /* number of allocated pages */
    uint32_t allocated;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* offset of each page */
    ram_addr_t *offset;
    /* pointer to each page */
    struct iovec *iov;
    RAMBlock *block;
} MultiFDPages_t;

typedef struct {
    /* this fields are not changed once the thread is created */
    /* channel number */
    uint8_t id;
    /* channel thread name */
    char *name;
    /* channel thread id */
    QemuThread thread;
    /* communication channel */
    QIOChannel *c;
    /* sem where to wait for more work */
    QemuSemaphore sem;
    /* this mutex protects the following parameters */
    QemuMutex mutex;
    /* is this channel thread running */
    bool running;
    /* should this thread finish */
    bool quit;
    /* thread has work to do */
    int pending_job;
    /* array of pages to sent */
    MultiFDPages_t *pages;
    /* packet allocated len */
    uint32_t packet_len;
    /* pointer to the packet */
    MultiFDPacket_t *packet;
    /* multifd flags for each packet */
    uint32_t flags;
    /* size of the next packet that contains pages */
    uint32_t next_packet_size;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* thread local variables */
    /* packets sent through this channel */
    uint64_t num_packets;
    /* pages sent through this channel */
    uint64_t num_pages;
}  MultiFDSendParams;

typedef struct {
    /* this fields are not changed once the thread is created */
    /* channel number */
    uint8_t id;
    /* channel thread name */
    char *name;
    /* channel thread id */
    QemuThread thread;
    /* communication channel */
    QIOChannel *c;
    /* this mutex protects the following parameters */
    QemuMutex mutex;
    /* is this channel thread running */
    bool running;
    /* array of pages to receive */
    MultiFDPages_t *pages;
    /* packet allocated len */
    uint32_t packet_len;
    /* pointer to the packet */
    MultiFDPacket_t *packet;
    /* multifd flags for each packet */
    uint32_t flags;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* thread local variables */
    /* size of the next packet that contains pages */
    uint32_t next_packet_size;
    /* packets sent through this channel */
    uint64_t num_packets;
    /* pages sent through this channel */
    uint64_t num_pages;
    /* syncs main thread and channels */
    QemuSemaphore sem_sync;
} MultiFDRecvParams;

static int multifd_send_initial_packet(MultiFDSendParams *p, Error **errp)
{
    MultiFDInit_t msg;
    int ret;

    msg.magic = cpu_to_be32(MULTIFD_MAGIC);
    msg.version = cpu_to_be32(MULTIFD_VERSION);
    msg.id = p->id;
    memcpy(msg.uuid, &qemu_uuid.data, sizeof(msg.uuid));

    ret = qio_channel_write_all(p->c, (char *)&msg, sizeof(msg), errp);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

static int multifd_recv_initial_packet(QIOChannel *c, Error **errp)
{
    MultiFDInit_t msg;
    int ret;

    ret = qio_channel_read_all(c, (char *)&msg, sizeof(msg), errp);
    if (ret != 0) {
        return -1;
    }

    msg.magic = be32_to_cpu(msg.magic);
    msg.version = be32_to_cpu(msg.version);

    if (msg.magic != MULTIFD_MAGIC) {
        error_setg(errp, "multifd: received packet magic %x "
                   "expected %x", msg.magic, MULTIFD_MAGIC);
        return -1;
    }

    if (msg.version != MULTIFD_VERSION) {
        error_setg(errp, "multifd: received packet version %d "
                   "expected %d", msg.version, MULTIFD_VERSION);
        return -1;
    }

    if (memcmp(msg.uuid, &qemu_uuid, sizeof(qemu_uuid))) {
        char *uuid = qemu_uuid_unparse_strdup(&qemu_uuid);
        char *msg_uuid = qemu_uuid_unparse_strdup((const QemuUUID *)msg.uuid);

        error_setg(errp, "multifd: received uuid '%s' and expected "
                   "uuid '%s' for channel %hhd", msg_uuid, uuid, msg.id);
        g_free(uuid);
        g_free(msg_uuid);
        return -1;
    }

    if (msg.id > migrate_multifd_channels()) {
        error_setg(errp, "multifd: received channel version %d "
                   "expected %d", msg.version, MULTIFD_VERSION);
        return -1;
    }

    return msg.id;
}

static MultiFDPages_t *multifd_pages_init(size_t size)
{
    MultiFDPages_t *pages = g_new0(MultiFDPages_t, 1);

    pages->allocated = size;
    pages->iov = g_new0(struct iovec, size);
    pages->offset = g_new0(ram_addr_t, size);

    return pages;
}

static void multifd_pages_clear(MultiFDPages_t *pages)
{
    pages->used = 0;
    pages->allocated = 0;
    pages->packet_num = 0;
    pages->block = NULL;
    g_free(pages->iov);
    pages->iov = NULL;
    g_free(pages->offset);
    pages->offset = NULL;
    g_free(pages);
}

static void multifd_send_fill_packet(MultiFDSendParams *p)
{
    MultiFDPacket_t *packet = p->packet;
    uint32_t page_max = MULTIFD_PACKET_SIZE / qemu_target_page_size();
    int i;

    packet->magic = cpu_to_be32(MULTIFD_MAGIC);
    packet->version = cpu_to_be32(MULTIFD_VERSION);
    packet->flags = cpu_to_be32(p->flags);
    packet->pages_alloc = cpu_to_be32(page_max);
    packet->pages_used = cpu_to_be32(p->pages->used);
    packet->next_packet_size = cpu_to_be32(p->next_packet_size);
    packet->packet_num = cpu_to_be64(p->packet_num);

    if (p->pages->block) {
        strncpy(packet->ramblock, p->pages->block->idstr, 256);
    }

    for (i = 0; i < p->pages->used; i++) {
        packet->offset[i] = cpu_to_be64(p->pages->offset[i]);
    }
}

static int multifd_recv_unfill_packet(MultiFDRecvParams *p, Error **errp)
{
    MultiFDPacket_t *packet = p->packet;
    uint32_t pages_max = MULTIFD_PACKET_SIZE / qemu_target_page_size();
    RAMBlock *block;
    int i;

    packet->magic = be32_to_cpu(packet->magic);
    if (packet->magic != MULTIFD_MAGIC) {
        error_setg(errp, "multifd: received packet "
                   "magic %x and expected magic %x",
                   packet->magic, MULTIFD_MAGIC);
        return -1;
    }

    packet->version = be32_to_cpu(packet->version);
    if (packet->version != MULTIFD_VERSION) {
        error_setg(errp, "multifd: received packet "
                   "version %d and expected version %d",
                   packet->version, MULTIFD_VERSION);
        return -1;
    }

    p->flags = be32_to_cpu(packet->flags);

    packet->pages_alloc = be32_to_cpu(packet->pages_alloc);
    /*
     * If we recevied a packet that is 100 times bigger than expected
     * just stop migration.  It is a magic number.
     */
    if (packet->pages_alloc > pages_max * 100) {
        error_setg(errp, "multifd: received packet "
                   "with size %d and expected a maximum size of %d",
                   packet->pages_alloc, pages_max * 100) ;
        return -1;
    }
    /*
     * We received a packet that is bigger than expected but inside
     * reasonable limits (see previous comment).  Just reallocate.
     */
    if (packet->pages_alloc > p->pages->allocated) {
        multifd_pages_clear(p->pages);
        p->pages = multifd_pages_init(packet->pages_alloc);
    }

    p->pages->used = be32_to_cpu(packet->pages_used);
    if (p->pages->used > packet->pages_alloc) {
        error_setg(errp, "multifd: received packet "
                   "with %d pages and expected maximum pages are %d",
                   p->pages->used, packet->pages_alloc) ;
        return -1;
    }

    p->next_packet_size = be32_to_cpu(packet->next_packet_size);
    p->packet_num = be64_to_cpu(packet->packet_num);

    if (p->pages->used) {
        /* make sure that ramblock is 0 terminated */
        packet->ramblock[255] = 0;
        block = qemu_ram_block_by_name(packet->ramblock);
        if (!block) {
            error_setg(errp, "multifd: unknown ram block %s",
                       packet->ramblock);
            return -1;
        }
    }

    for (i = 0; i < p->pages->used; i++) {
        ram_addr_t offset = be64_to_cpu(packet->offset[i]);

        if (offset > (block->used_length - TARGET_PAGE_SIZE)) {
            error_setg(errp, "multifd: offset too long " RAM_ADDR_FMT
                       " (max " RAM_ADDR_FMT ")",
                       offset, block->max_length);
            return -1;
        }
        p->pages->iov[i].iov_base = block->host + offset;
        p->pages->iov[i].iov_len = TARGET_PAGE_SIZE;
    }

    return 0;
}

struct {
    MultiFDSendParams *params;
    /* array of pages to sent */
    MultiFDPages_t *pages;
    /* syncs main thread and channels */
    QemuSemaphore sem_sync;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* send channels ready */
    QemuSemaphore channels_ready;
} *multifd_send_state;

/*
 * How we use multifd_send_state->pages and channel->pages?
 *
 * We create a pages for each channel, and a main one.  Each time that
 * we need to send a batch of pages we interchange the ones between
 * multifd_send_state and the channel that is sending it.  There are
 * two reasons for that:
 *    - to not have to do so many mallocs during migration
 *    - to make easier to know what to free at the end of migration
 *
 * This way we always know who is the owner of each "pages" struct,
 * and we don't need any locking.  It belongs to the migration thread
 * or to the channel thread.  Switching is safe because the migration
 * thread is using the channel mutex when changing it, and the channel
 * have to had finish with its own, otherwise pending_job can't be
 * false.
 */

static void multifd_send_pages(void)
{
    int i;
    static int next_channel;
    MultiFDSendParams *p = NULL; /* make happy gcc */
    MultiFDPages_t *pages = multifd_send_state->pages;
    uint64_t transferred;

    qemu_sem_wait(&multifd_send_state->channels_ready);
    for (i = next_channel;; i = (i + 1) % migrate_multifd_channels()) {
        p = &multifd_send_state->params[i];

        qemu_mutex_lock(&p->mutex);
        if (!p->pending_job) {
            p->pending_job++;
            next_channel = (i + 1) % migrate_multifd_channels();
            break;
        }
        qemu_mutex_unlock(&p->mutex);
    }
    p->pages->used = 0;

    p->packet_num = multifd_send_state->packet_num++;
    p->pages->block = NULL;
    multifd_send_state->pages = p->pages;
    p->pages = pages;
    transferred = ((uint64_t) pages->used) * TARGET_PAGE_SIZE + p->packet_len;
    ram_counters.multifd_bytes += transferred;
    ram_counters.transferred += transferred;;
    qemu_mutex_unlock(&p->mutex);
    qemu_sem_post(&p->sem);
}

static void multifd_queue_page(RAMBlock *block, ram_addr_t offset)
{
    MultiFDPages_t *pages = multifd_send_state->pages;

    if (!pages->block) {
        pages->block = block;
    }

    if (pages->block == block) {
        pages->offset[pages->used] = offset;
        pages->iov[pages->used].iov_base = block->host + offset;
        pages->iov[pages->used].iov_len = TARGET_PAGE_SIZE;
        pages->used++;

        if (pages->used < pages->allocated) {
            return;
        }
    }

    multifd_send_pages();

    if (pages->block != block) {
        multifd_queue_page(block, offset);
    }
}

static void multifd_send_terminate_threads(Error *err)
{
    int i;

    if (err) {
        MigrationState *s = migrate_get_current();
        migrate_set_error(s, err);
        if (s->state == MIGRATION_STATUS_SETUP ||
            s->state == MIGRATION_STATUS_PRE_SWITCHOVER ||
            s->state == MIGRATION_STATUS_DEVICE ||
            s->state == MIGRATION_STATUS_ACTIVE) {
            migrate_set_state(&s->state, s->state,
                              MIGRATION_STATUS_FAILED);
        }
    }

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        qemu_mutex_lock(&p->mutex);
        p->quit = true;
        qemu_sem_post(&p->sem);
        qemu_mutex_unlock(&p->mutex);
    }
}

void multifd_save_cleanup(void)
{
    int i;

    if (!migrate_use_multifd()) {
        return;
    }
    multifd_send_terminate_threads(NULL);
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        if (p->running) {
            qemu_thread_join(&p->thread);
        }
        socket_send_channel_destroy(p->c);
        p->c = NULL;
        qemu_mutex_destroy(&p->mutex);
        qemu_sem_destroy(&p->sem);
        g_free(p->name);
        p->name = NULL;
        multifd_pages_clear(p->pages);
        p->pages = NULL;
        p->packet_len = 0;
        g_free(p->packet);
        p->packet = NULL;
    }
    qemu_sem_destroy(&multifd_send_state->channels_ready);
    qemu_sem_destroy(&multifd_send_state->sem_sync);
    g_free(multifd_send_state->params);
    multifd_send_state->params = NULL;
    multifd_pages_clear(multifd_send_state->pages);
    multifd_send_state->pages = NULL;
    g_free(multifd_send_state);
    multifd_send_state = NULL;
}

static void multifd_send_sync_main(void)
{
    int i;

    if (!migrate_use_multifd()) {
        return;
    }
    if (multifd_send_state->pages->used) {
        multifd_send_pages();
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        trace_multifd_send_sync_main_signal(p->id);

        qemu_mutex_lock(&p->mutex);

        p->packet_num = multifd_send_state->packet_num++;
        p->flags |= MULTIFD_FLAG_SYNC;
        p->pending_job++;
        qemu_mutex_unlock(&p->mutex);
        qemu_sem_post(&p->sem);
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        trace_multifd_send_sync_main_wait(p->id);
        qemu_sem_wait(&multifd_send_state->sem_sync);
    }
    trace_multifd_send_sync_main(multifd_send_state->packet_num);
}

static void *multifd_send_thread(void *opaque)
{
    MultiFDSendParams *p = opaque;
    Error *local_err = NULL;
    int ret;

    trace_multifd_send_thread_start(p->id);
    rcu_register_thread();

    if (multifd_send_initial_packet(p, &local_err) < 0) {
        goto out;
    }
    /* initial packet */
    p->num_packets = 1;

    while (true) {
        qemu_sem_wait(&p->sem);
        qemu_mutex_lock(&p->mutex);

        if (p->pending_job) {
            uint32_t used = p->pages->used;
            uint64_t packet_num = p->packet_num;
            uint32_t flags = p->flags;

            p->next_packet_size = used * qemu_target_page_size();
            multifd_send_fill_packet(p);
            p->flags = 0;
            p->num_packets++;
            p->num_pages += used;
            p->pages->used = 0;
            qemu_mutex_unlock(&p->mutex);

            trace_multifd_send(p->id, packet_num, used, flags,
                               p->next_packet_size);

            ret = qio_channel_write_all(p->c, (void *)p->packet,
                                        p->packet_len, &local_err);
            if (ret != 0) {
                break;
            }

            if (used) {
                ret = qio_channel_writev_all(p->c, p->pages->iov,
                                             used, &local_err);
                if (ret != 0) {
                    break;
                }
            }

            qemu_mutex_lock(&p->mutex);
            p->pending_job--;
            qemu_mutex_unlock(&p->mutex);

            if (flags & MULTIFD_FLAG_SYNC) {
                qemu_sem_post(&multifd_send_state->sem_sync);
            }
            qemu_sem_post(&multifd_send_state->channels_ready);
        } else if (p->quit) {
            qemu_mutex_unlock(&p->mutex);
            break;
        } else {
            qemu_mutex_unlock(&p->mutex);
            /* sometimes there are spurious wakeups */
        }
    }

out:
    if (local_err) {
        multifd_send_terminate_threads(local_err);
    }

    qemu_mutex_lock(&p->mutex);
    p->running = false;
    qemu_mutex_unlock(&p->mutex);

    rcu_unregister_thread();
    trace_multifd_send_thread_end(p->id, p->num_packets, p->num_pages);

    return NULL;
}

static void multifd_new_send_channel_async(QIOTask *task, gpointer opaque)
{
    MultiFDSendParams *p = opaque;
    QIOChannel *sioc = QIO_CHANNEL(qio_task_get_source(task));
    Error *local_err = NULL;

    if (qio_task_propagate_error(task, &local_err)) {
        migrate_set_error(migrate_get_current(), local_err);
        multifd_save_cleanup();
    } else {
        p->c = QIO_CHANNEL(sioc);
        qio_channel_set_delay(p->c, false);
        p->running = true;
        qemu_thread_create(&p->thread, p->name, multifd_send_thread, p,
                           QEMU_THREAD_JOINABLE);
    }
}

int multifd_save_setup(void)
{
    int thread_count;
    uint32_t page_count = MULTIFD_PACKET_SIZE / qemu_target_page_size();
    uint8_t i;

    if (!migrate_use_multifd()) {
        return 0;
    }
    thread_count = migrate_multifd_channels();
    multifd_send_state = g_malloc0(sizeof(*multifd_send_state));
    multifd_send_state->params = g_new0(MultiFDSendParams, thread_count);
    multifd_send_state->pages = multifd_pages_init(page_count);
    qemu_sem_init(&multifd_send_state->sem_sync, 0);
    qemu_sem_init(&multifd_send_state->channels_ready, 0);

    for (i = 0; i < thread_count; i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        qemu_mutex_init(&p->mutex);
        qemu_sem_init(&p->sem, 0);
        p->quit = false;
        p->pending_job = 0;
        p->id = i;
        p->pages = multifd_pages_init(page_count);
        p->packet_len = sizeof(MultiFDPacket_t)
                      + sizeof(ram_addr_t) * page_count;
        p->packet = g_malloc0(p->packet_len);
        p->name = g_strdup_printf("multifdsend_%d", i);
        socket_send_channel_create(multifd_new_send_channel_async, p);
    }
    return 0;
}

struct {
    MultiFDRecvParams *params;
    /* number of created threads */
    int count;
    /* syncs main thread and channels */
    QemuSemaphore sem_sync;
    /* global number of generated multifd packets */
    uint64_t packet_num;
} *multifd_recv_state;

static void multifd_recv_terminate_threads(Error *err)
{
    int i;

    if (err) {
        MigrationState *s = migrate_get_current();
        migrate_set_error(s, err);
        if (s->state == MIGRATION_STATUS_SETUP ||
            s->state == MIGRATION_STATUS_ACTIVE) {
            migrate_set_state(&s->state, s->state,
                              MIGRATION_STATUS_FAILED);
        }
    }

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        qemu_mutex_lock(&p->mutex);
        /* We could arrive here for two reasons:
           - normal quit, i.e. everything went fine, just finished
           - error quit: We close the channels so the channel threads
             finish the qio_channel_read_all_eof() */
        qio_channel_shutdown(p->c, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        qemu_mutex_unlock(&p->mutex);
    }
}

int multifd_load_cleanup(Error **errp)
{
    int i;
    int ret = 0;

    if (!migrate_use_multifd()) {
        return 0;
    }
    multifd_recv_terminate_threads(NULL);
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        if (p->running) {
            qemu_thread_join(&p->thread);
        }
        object_unref(OBJECT(p->c));
        p->c = NULL;
        qemu_mutex_destroy(&p->mutex);
        qemu_sem_destroy(&p->sem_sync);
        g_free(p->name);
        p->name = NULL;
        multifd_pages_clear(p->pages);
        p->pages = NULL;
        p->packet_len = 0;
        g_free(p->packet);
        p->packet = NULL;
    }
    qemu_sem_destroy(&multifd_recv_state->sem_sync);
    g_free(multifd_recv_state->params);
    multifd_recv_state->params = NULL;
    g_free(multifd_recv_state);
    multifd_recv_state = NULL;

    return ret;
}

static void multifd_recv_sync_main(void)
{
    int i;

    if (!migrate_use_multifd()) {
        return;
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        trace_multifd_recv_sync_main_wait(p->id);
        qemu_sem_wait(&multifd_recv_state->sem_sync);
        qemu_mutex_lock(&p->mutex);
        if (multifd_recv_state->packet_num < p->packet_num) {
            multifd_recv_state->packet_num = p->packet_num;
        }
        qemu_mutex_unlock(&p->mutex);
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        trace_multifd_recv_sync_main_signal(p->id);
        qemu_sem_post(&p->sem_sync);
    }
    trace_multifd_recv_sync_main(multifd_recv_state->packet_num);
}

static void *multifd_recv_thread(void *opaque)
{
    MultiFDRecvParams *p = opaque;
    Error *local_err = NULL;
    int ret;

    trace_multifd_recv_thread_start(p->id);
    rcu_register_thread();

    while (true) {
        uint32_t used;
        uint32_t flags;

        ret = qio_channel_read_all_eof(p->c, (void *)p->packet,
                                       p->packet_len, &local_err);
        if (ret == 0) {   /* EOF */
            break;
        }
        if (ret == -1) {   /* Error */
            break;
        }

        qemu_mutex_lock(&p->mutex);
        ret = multifd_recv_unfill_packet(p, &local_err);
        if (ret) {
            qemu_mutex_unlock(&p->mutex);
            break;
        }

        used = p->pages->used;
        flags = p->flags;
        trace_multifd_recv(p->id, p->packet_num, used, flags,
                           p->next_packet_size);
        p->num_packets++;
        p->num_pages += used;
        qemu_mutex_unlock(&p->mutex);

        if (used) {
            ret = qio_channel_readv_all(p->c, p->pages->iov,
                                        used, &local_err);
            if (ret != 0) {
                break;
            }
        }

        if (flags & MULTIFD_FLAG_SYNC) {
            qemu_sem_post(&multifd_recv_state->sem_sync);
            qemu_sem_wait(&p->sem_sync);
        }
    }

    if (local_err) {
        multifd_recv_terminate_threads(local_err);
    }
    qemu_mutex_lock(&p->mutex);
    p->running = false;
    qemu_mutex_unlock(&p->mutex);

    rcu_unregister_thread();
    trace_multifd_recv_thread_end(p->id, p->num_packets, p->num_pages);

    return NULL;
}

int multifd_load_setup(void)
{
    int thread_count;
    uint32_t page_count = MULTIFD_PACKET_SIZE / qemu_target_page_size();
    uint8_t i;

    if (!migrate_use_multifd()) {
        return 0;
    }
    thread_count = migrate_multifd_channels();
    multifd_recv_state = g_malloc0(sizeof(*multifd_recv_state));
    multifd_recv_state->params = g_new0(MultiFDRecvParams, thread_count);
    atomic_set(&multifd_recv_state->count, 0);
    qemu_sem_init(&multifd_recv_state->sem_sync, 0);

    for (i = 0; i < thread_count; i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        qemu_mutex_init(&p->mutex);
        qemu_sem_init(&p->sem_sync, 0);
        p->id = i;
        p->pages = multifd_pages_init(page_count);
        p->packet_len = sizeof(MultiFDPacket_t)
                      + sizeof(ram_addr_t) * page_count;
        p->packet = g_malloc0(p->packet_len);
        p->name = g_strdup_printf("multifdrecv_%d", i);
    }
    return 0;
}

bool multifd_recv_all_channels_created(void)
{
    int thread_count = migrate_multifd_channels();

    if (!migrate_use_multifd()) {
        return true;
    }

    return thread_count == atomic_read(&multifd_recv_state->count);
}

/*
 * Try to receive all multifd channels to get ready for the migration.
 * - Return true and do not set @errp when correctly receving all channels;
 * - Return false and do not set @errp when correctly receiving the current one;
 * - Return false and set @errp when failing to receive the current channel.
 */
bool multifd_recv_new_channel(QIOChannel *ioc, Error **errp)
{
    MultiFDRecvParams *p;
    Error *local_err = NULL;
    int id;

    id = multifd_recv_initial_packet(ioc, &local_err);
    if (id < 0) {
        multifd_recv_terminate_threads(local_err);
        error_propagate_prepend(errp, local_err,
                                "failed to receive packet"
                                " via multifd channel %d: ",
                                atomic_read(&multifd_recv_state->count));
        return false;
    }

    p = &multifd_recv_state->params[id];
    if (p->c != NULL) {
        error_setg(&local_err, "multifd: received id '%d' already setup'",
                   id);
        multifd_recv_terminate_threads(local_err);
        error_propagate(errp, local_err);
        return false;
    }
    p->c = ioc;
    object_ref(OBJECT(ioc));
    /* initial packet */
    p->num_packets = 1;

    p->running = true;
    qemu_thread_create(&p->thread, p->name, multifd_recv_thread, p,
                       QEMU_THREAD_JOINABLE);
    atomic_inc(&multifd_recv_state->count);
    return atomic_read(&multifd_recv_state->count) ==
           migrate_multifd_channels();
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
    int pct_max = s->parameters.max_cpu_throttle;

    /* We have not started throttling yet. Let's start it. */
    if (!cpu_throttle_active()) {
        cpu_throttle_set(pct_initial);
    } else {
        /* Throttling already on, just increase the rate */
        cpu_throttle_set(MIN(cpu_throttle_get_percentage() + pct_icrement,
                         pct_max));
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
    ret = test_and_clear_bit(page, rb->bmap);

    if (ret) {
        rs->migration_dirty_pages--;
    }
    qemu_mutex_unlock(&rs->bitmap_mutex);

    return ret;
}

static void migration_bitmap_sync_range(RAMState *rs, RAMBlock *rb,
                                        ram_addr_t length)
{
    rs->migration_dirty_pages +=
        cpu_physical_memory_sync_dirty_bitmap(rb, 0, length,
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
        xbzrle_counters.cache_miss_rate = (double)(xbzrle_counters.cache_miss -
            rs->xbzrle_cache_miss_prev) / page_count;
        rs->xbzrle_cache_miss_prev = xbzrle_counters.cache_miss;
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
    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        migration_bitmap_sync_range(rs, block, block->used_length);
    }
    ram_counters.remaining = ram_bytes_remaining();
    rcu_read_unlock();
    qemu_mutex_unlock(&rs->bitmap_mutex);

    trace_migration_bitmap_sync_end(rs->num_dirty_pages_period);

    end_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    /* more than 1 second = 1000 millisecons */
    if (end_time > rs->time_last_bitmap_sync + 1000) {
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

        migration_update_rates(rs, end_time);

        rs->target_page_count_prev = rs->target_page_count;

        /* reset period counters */
        rs->time_last_bitmap_sync = end_time;
        rs->num_dirty_pages_period = 0;
        rs->bytes_xfer_prev = bytes_xfer_now;
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

    ram_discard_range(rbname, offset, pages << TARGET_PAGE_BITS);
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
    ram_addr_t offset = pss->page << TARGET_PAGE_BITS;
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
    multifd_queue_page(block, offset);
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
    if ((pss->page << TARGET_PAGE_BITS) >= pss->block->used_length) {
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
            migration_consume_urgent_request();
        }
    }
    qemu_mutex_unlock(&rs->src_page_req_mutex);

    return block;
}

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
    migration_make_urgent_request();
    qemu_mutex_unlock(&rs->src_page_req_mutex);
    rcu_read_unlock();

    return 0;

err:
    rcu_read_unlock();
    return -1;
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
    ram_addr_t offset = pss->page << TARGET_PAGE_BITS;
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
     * do not use multifd for compression as the first page in the new
     * block should be posted out before sending the compressed page
     */
    if (!save_page_use_compression(rs) && migrate_use_multifd()) {
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
        if (pss->block->unsentmap) {
            clear_bit(pss->page, pss->block->unsentmap);
        }

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

    rcu_read_lock();
    if (count_ignored) {
        RAMBLOCK_FOREACH_MIGRATABLE(block) {
            total += block->used_length;
        }
    } else {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            total += block->used_length;
        }
    }
    rcu_read_unlock();
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

    /* caller have hold iothread lock or is in a bh, so there is
     * no writing race against the migration bitmap
     */
    memory_global_dirty_log_stop();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        g_free(block->bmap);
        block->bmap = NULL;
        g_free(block->unsentmap);
        block->unsentmap = NULL;
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

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
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

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
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

    /*
     * On source VM, we don't need to update the received bitmap since
     * we don't even have one.
     */
    if (rb->receivedmap) {
        bitmap_clear(rb->receivedmap, start >> qemu_target_page_bits(),
                     length >> qemu_target_page_bits());
    }

    ret = ram_block_discard_range(rb, start, length);

err:
    rcu_read_unlock();

    return ret;
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
     * This must match with the initial values of dirty bitmap.
     * Currently we initialize the dirty bitmap to all zeros so
     * here the total dirty page count is zero.
     */
    (*rsp)->migration_dirty_pages = 0;
    ram_state_reset(*rsp);

    return 0;
}

static void ram_list_init_bitmaps(void)
{
    RAMBlock *block;
    unsigned long pages;

    /* Skip setting bitmap if there is no RAM */
    if (ram_bytes_total()) {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            pages = block->max_length >> TARGET_PAGE_BITS;
            /*
             * The initial dirty bitmap for migration must be set with all
             * ones to make sure we'll migrate every guest RAM page to
             * destination.
             * Here we didn't set RAMBlock.bmap simply because it is already
             * set in ram_list.dirty_memory[DIRTY_MEMORY_MIGRATION] in
             * ram_block_add, and that's where we'll sync the dirty bitmaps.
             * Here setting RAMBlock.bmap would be fine too but not necessary.
             */
            block->bmap = bitmap_new(pages);
            if (migrate_postcopy_ram()) {
                block->unsentmap = bitmap_new(pages);
                bitmap_set(block->unsentmap, 0, pages);
            }
        }
    }
}

static void ram_init_bitmaps(RAMState *rs)
{
    /* For memory_global_dirty_log_start below.  */
    qemu_mutex_lock_iothread();
    qemu_mutex_lock_ramlist();
    rcu_read_lock();

    ram_list_init_bitmaps();
    memory_global_dirty_log_start();
    migration_bitmap_sync_precopy(rs);

    rcu_read_unlock();
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

    rcu_read_lock();

    qemu_put_be64(f, ram_bytes_total_common(true) | RAM_SAVE_FLAG_MEM_SIZE);

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        qemu_put_byte(f, strlen(block->idstr));
        qemu_put_buffer(f, (uint8_t *)block->idstr, strlen(block->idstr));
        qemu_put_be64(f, block->used_length);
        if (migrate_postcopy_ram() && block->page_size != qemu_host_page_size) {
            qemu_put_be64(f, block->page_size);
        }
        if (migrate_ignore_shared()) {
            qemu_put_be64(f, block->mr->addr);
            qemu_put_byte(f, ramblock_is_ignored(block) ? 1 : 0);
        }
    }

    rcu_read_unlock();

    ram_control_before_iterate(f, RAM_CONTROL_SETUP);
    ram_control_after_iterate(f, RAM_CONTROL_SETUP);

    multifd_send_sync_main();
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
    int ret;
    int i;
    int64_t t0;
    int done = 0;

    if (blk_mig_bulk_active()) {
        /* Avoid transferring ram during bulk phase of block migration as
         * the bulk phase will usually take a long time and transferring
         * ram updates during that time is pointless. */
        goto out;
    }

    rcu_read_lock();
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

        /* we want to check in the 1st loop, just in case it was the 1st time
           and we had to sync the dirty bitmap.
           qemu_clock_get_ns() is a bit expensive, so we only check each some
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
    rcu_read_unlock();

    /*
     * Must occur before EOS (or any QEMUFile operation)
     * because of RDMA protocol.
     */
    ram_control_after_iterate(f, RAM_CONTROL_ROUND);

    multifd_send_sync_main();
out:
    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    qemu_fflush(f);
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

    rcu_read_lock();

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

    rcu_read_unlock();

    multifd_send_sync_main();
    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    qemu_fflush(f);

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
        rcu_read_lock();
        migration_bitmap_sync_precopy(rs);
        rcu_read_unlock();
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
                                                 ram_addr_t offset)
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
    if (!test_and_set_bit(offset >> TARGET_PAGE_BITS, block->bmap)) {
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
 * colo cache: this is for secondary VM, we cache the whole
 * memory of the secondary VM, it is need to hold the global lock
 * to call this helper.
 */
int colo_init_ram_cache(void)
{
    RAMBlock *block;

    rcu_read_lock();
    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        block->colo_cache = qemu_anon_ram_alloc(block->used_length,
                                                NULL,
                                                false);
        if (!block->colo_cache) {
            error_report("%s: Can't alloc memory for COLO cache of block %s,"
                         "size 0x" RAM_ADDR_FMT, __func__, block->idstr,
                         block->used_length);
            goto out_locked;
        }
        memcpy(block->colo_cache, block->host, block->used_length);
    }
    rcu_read_unlock();
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
            bitmap_set(block->bmap, 0, pages);
        }
    }
    ram_state = g_new0(RAMState, 1);
    ram_state->migration_dirty_pages = 0;
    qemu_mutex_init(&ram_state->bitmap_mutex);
    memory_global_dirty_log_start();

    return 0;

out_locked:

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        if (block->colo_cache) {
            qemu_anon_ram_free(block->colo_cache, block->used_length);
            block->colo_cache = NULL;
        }
    }

    rcu_read_unlock();
    return -errno;
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

    rcu_read_lock();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        if (block->colo_cache) {
            qemu_anon_ram_free(block->colo_cache, block->used_length);
            block->colo_cache = NULL;
        }
    }

    rcu_read_unlock();
    qemu_mutex_destroy(&ram_state->bitmap_mutex);
    g_free(ram_state);
    ram_state = NULL;
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
        if (ramblock_is_pmem(rb)) {
            pmem_persist(rb->host, rb->used_length);
        }
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
        place_needed = false;
        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE)) {
            block = ram_block_from_stream(f, flags);

            host = host_from_ram_block_offset(block, addr);
            if (!host) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
            matches_target_page_size = block->page_size == TARGET_PAGE_SIZE;
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
        case RAM_SAVE_FLAG_EOS:
            /* normal exit */
            multifd_recv_sync_main();
            break;
        default:
            error_report("Unknown combination of migration flags: %#x"
                         " (postcopy mode)", flags);
            ret = -EINVAL;
            break;
        }

        /* Detect for any possible file errors */
        if (!ret && qemu_file_get_error(f)) {
            ret = qemu_file_get_error(f);
        }

        if (!ret && place_needed) {
            /* This gets called at the last target page in the host page */
            void *place_dest = host + TARGET_PAGE_SIZE - block->page_size;

            if (all_zero) {
                ret = postcopy_place_page_zero(mis, place_dest,
                                               block);
            } else {
                ret = postcopy_place_page(mis, place_dest,
                                          place_source, block);
            }
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
static void colo_flush_ram_cache(void)
{
    RAMBlock *block = NULL;
    void *dst_host;
    void *src_host;
    unsigned long offset = 0;

    memory_global_dirty_log_sync();
    rcu_read_lock();
    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        migration_bitmap_sync_range(ram_state, block, block->used_length);
    }
    rcu_read_unlock();

    trace_colo_flush_ram_cache_begin(ram_state->migration_dirty_pages);
    rcu_read_lock();
    block = QLIST_FIRST_RCU(&ram_list.blocks);

    while (block) {
        offset = migration_bitmap_find_dirty(ram_state, block, offset);

        if (offset << TARGET_PAGE_BITS >= block->used_length) {
            offset = 0;
            block = QLIST_NEXT_RCU(block, next);
        } else {
            migration_bitmap_clear_dirty(ram_state, block, offset);
            dst_host = block->host + (offset << TARGET_PAGE_BITS);
            src_host = block->colo_cache + (offset << TARGET_PAGE_BITS);
            memcpy(dst_host, src_host, TARGET_PAGE_SIZE);
        }
    }

    rcu_read_unlock();
    trace_colo_flush_ram_cache_end();
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
    bool postcopy_running = postcopy_is_running();
    /* ADVISE is earlier, it shows the source has the postcopy capability on */
    bool postcopy_advised = postcopy_is_advised();

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

            /*
             * After going into COLO, we should load the Page into colo_cache.
             */
            if (migration_incoming_in_colo_state()) {
                host = colo_cache_from_block_offset(block, addr);
            } else {
                host = host_from_ram_block_offset(block, addr);
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
                    if (migrate_ignore_shared()) {
                        hwaddr addr = qemu_get_be64(f);
                        bool ignored = qemu_get_byte(f);
                        if (ignored != ramblock_is_ignored(block)) {
                            error_report("RAM block %s should %s be migrated",
                                         id, ignored ? "" : "not");
                            ret = -EINVAL;
                        }
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
                error_report("Unknown combination of migration flags: %#x",
                             flags);
                ret = -EINVAL;
            }
        }
        if (!ret) {
            ret = qemu_file_get_error(f);
        }
    }

    ret |= wait_for_decompress_done();
    rcu_read_unlock();
    trace_ram_load_complete(ret, seq_iter);

    if (!ret  && migration_incoming_in_colo_state()) {
        colo_flush_ram_cache();
    }
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
     * need the endianess convertion, and the paddings.
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
        error_report("%s: ramblock '%s' end mark incorrect: 0x%"PRIu64,
                     __func__, block->idstr, end_mark);
        ret = -EINVAL;
        goto out;
    }

    /*
     * Endianess convertion. We are during postcopy (though paused).
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
    register_savevm_live(NULL, "ram", 0, 4, &savevm_ram_handlers, &ram_state);
}
