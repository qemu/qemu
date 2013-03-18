/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/mman.h>
#endif
#include "config.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "sysemu/arch_init.h"
#include "audio/audio.h"
#include "hw/pc.h"
#include "hw/pci/pci.h"
#include "hw/audiodev.h"
#include "sysemu/kvm.h"
#include "migration/migration.h"
#include "exec/gdbstub.h"
#include "hw/smbios.h"
#include "exec/address-spaces.h"
#include "hw/pcspk.h"
#include "migration/page_cache.h"
#include "qemu/config-file.h"
#include "qmp-commands.h"
#include "trace.h"
#include "exec/cpu-all.h"

#ifdef DEBUG_ARCH_INIT
#define DPRINTF(fmt, ...) \
    do { fprintf(stdout, "arch_init: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#ifdef TARGET_SPARC
int graphic_width = 1024;
int graphic_height = 768;
int graphic_depth = 8;
#else
int graphic_width = 800;
int graphic_height = 600;
int graphic_depth = 15;
#endif


#if defined(TARGET_ALPHA)
#define QEMU_ARCH QEMU_ARCH_ALPHA
#elif defined(TARGET_ARM)
#define QEMU_ARCH QEMU_ARCH_ARM
#elif defined(TARGET_CRIS)
#define QEMU_ARCH QEMU_ARCH_CRIS
#elif defined(TARGET_I386)
#define QEMU_ARCH QEMU_ARCH_I386
#elif defined(TARGET_M68K)
#define QEMU_ARCH QEMU_ARCH_M68K
#elif defined(TARGET_LM32)
#define QEMU_ARCH QEMU_ARCH_LM32
#elif defined(TARGET_MICROBLAZE)
#define QEMU_ARCH QEMU_ARCH_MICROBLAZE
#elif defined(TARGET_MIPS)
#define QEMU_ARCH QEMU_ARCH_MIPS
#elif defined(TARGET_MOXIE)
#define QEMU_ARCH QEMU_ARCH_MOXIE
#elif defined(TARGET_OPENRISC)
#define QEMU_ARCH QEMU_ARCH_OPENRISC
#elif defined(TARGET_PPC)
#define QEMU_ARCH QEMU_ARCH_PPC
#elif defined(TARGET_S390X)
#define QEMU_ARCH QEMU_ARCH_S390X
#elif defined(TARGET_SH4)
#define QEMU_ARCH QEMU_ARCH_SH4
#elif defined(TARGET_SPARC)
#define QEMU_ARCH QEMU_ARCH_SPARC
#elif defined(TARGET_XTENSA)
#define QEMU_ARCH QEMU_ARCH_XTENSA
#elif defined(TARGET_UNICORE32)
#define QEMU_ARCH QEMU_ARCH_UNICORE32
#endif

const uint32_t arch_type = QEMU_ARCH;

/***********************************************************/
/* ram save/restore */

#define RAM_SAVE_FLAG_FULL     0x01 /* Obsolete, not used anymore */
#define RAM_SAVE_FLAG_COMPRESS 0x02
#define RAM_SAVE_FLAG_MEM_SIZE 0x04
#define RAM_SAVE_FLAG_PAGE     0x08
#define RAM_SAVE_FLAG_EOS      0x10
#define RAM_SAVE_FLAG_CONTINUE 0x20
#define RAM_SAVE_FLAG_XBZRLE   0x40

#ifdef __ALTIVEC__
#include <altivec.h>
#define VECTYPE        vector unsigned char
#define SPLAT(p)       vec_splat(vec_ld(0, p), 0)
#define ALL_EQ(v1, v2) vec_all_eq(v1, v2)
/* altivec.h may redefine the bool macro as vector type.
 * Reset it to POSIX semantics. */
#undef bool
#define bool _Bool
#elif defined __SSE2__
#include <emmintrin.h>
#define VECTYPE        __m128i
#define SPLAT(p)       _mm_set1_epi8(*(p))
#define ALL_EQ(v1, v2) (_mm_movemask_epi8(_mm_cmpeq_epi8(v1, v2)) == 0xFFFF)
#else
#define VECTYPE        unsigned long
#define SPLAT(p)       (*(p) * (~0UL / 255))
#define ALL_EQ(v1, v2) ((v1) == (v2))
#endif


static struct defconfig_file {
    const char *filename;
    /* Indicates it is an user config file (disabled by -no-user-config) */
    bool userconfig;
} default_config_files[] = {
    { CONFIG_QEMU_CONFDIR "/qemu.conf",                   true },
    { CONFIG_QEMU_CONFDIR "/target-" TARGET_ARCH ".conf", true },
    { NULL }, /* end of list */
};


int qemu_read_default_config_files(bool userconfig)
{
    int ret;
    struct defconfig_file *f;

    for (f = default_config_files; f->filename; f++) {
        if (!userconfig && f->userconfig) {
            continue;
        }
        ret = qemu_read_config_file(f->filename);
        if (ret < 0 && ret != -ENOENT) {
            return ret;
        }
    }
    
    return 0;
}

static int is_dup_page(uint8_t *page)
{
    VECTYPE *p = (VECTYPE *)page;
    VECTYPE val = SPLAT(page);
    int i;

    for (i = 0; i < TARGET_PAGE_SIZE / sizeof(VECTYPE); i++) {
        if (!ALL_EQ(val, p[i])) {
            return 0;
        }
    }

    return 1;
}

/* struct contains XBZRLE cache and a static page
   used by the compression */
static struct {
    /* buffer used for XBZRLE encoding */
    uint8_t *encoded_buf;
    /* buffer for storing page content */
    uint8_t *current_buf;
    /* buffer used for XBZRLE decoding */
    uint8_t *decoded_buf;
    /* Cache for XBZRLE */
    PageCache *cache;
} XBZRLE = {
    .encoded_buf = NULL,
    .current_buf = NULL,
    .decoded_buf = NULL,
    .cache = NULL,
};


int64_t xbzrle_cache_resize(int64_t new_size)
{
    if (XBZRLE.cache != NULL) {
        return cache_resize(XBZRLE.cache, new_size / TARGET_PAGE_SIZE) *
            TARGET_PAGE_SIZE;
    }
    return pow2floor(new_size);
}

/* accounting for migration statistics */
typedef struct AccountingInfo {
    uint64_t dup_pages;
    uint64_t norm_pages;
    uint64_t iterations;
    uint64_t xbzrle_bytes;
    uint64_t xbzrle_pages;
    uint64_t xbzrle_cache_miss;
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

uint64_t xbzrle_mig_pages_overflow(void)
{
    return acct_info.xbzrle_overflows;
}

static size_t save_block_hdr(QEMUFile *f, RAMBlock *block, ram_addr_t offset,
                             int cont, int flag)
{
    size_t size;

    qemu_put_be64(f, offset | cont | flag);
    size = 8;

    if (!cont) {
        qemu_put_byte(f, strlen(block->idstr));
        qemu_put_buffer(f, (uint8_t *)block->idstr,
                        strlen(block->idstr));
        size += 1 + strlen(block->idstr);
    }
    return size;
}

#define ENCODING_FLAG_XBZRLE 0x1

static int save_xbzrle_page(QEMUFile *f, uint8_t *current_data,
                            ram_addr_t current_addr, RAMBlock *block,
                            ram_addr_t offset, int cont, bool last_stage)
{
    int encoded_len = 0, bytes_sent = -1;
    uint8_t *prev_cached_page;

    if (!cache_is_cached(XBZRLE.cache, current_addr)) {
        if (!last_stage) {
            cache_insert(XBZRLE.cache, current_addr, current_data);
        }
        acct_info.xbzrle_cache_miss++;
        return -1;
    }

    prev_cached_page = get_cached_data(XBZRLE.cache, current_addr);

    /* save current buffer into memory */
    memcpy(XBZRLE.current_buf, current_data, TARGET_PAGE_SIZE);

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
        memcpy(prev_cached_page, current_data, TARGET_PAGE_SIZE);
        return -1;
    }

    /* we need to update the data in the cache, in order to get the same data */
    if (!last_stage) {
        memcpy(prev_cached_page, XBZRLE.current_buf, TARGET_PAGE_SIZE);
    }

    /* Send XBZRLE based compressed page */
    bytes_sent = save_block_hdr(f, block, offset, cont, RAM_SAVE_FLAG_XBZRLE);
    qemu_put_byte(f, ENCODING_FLAG_XBZRLE);
    qemu_put_be16(f, encoded_len);
    qemu_put_buffer(f, XBZRLE.encoded_buf, encoded_len);
    bytes_sent += encoded_len + 1 + 2;
    acct_info.xbzrle_pages++;
    acct_info.xbzrle_bytes += bytes_sent;

    return bytes_sent;
}


/* This is the last block that we have visited serching for dirty pages
 */
static RAMBlock *last_seen_block;
/* This is the last block from where we have sent data */
static RAMBlock *last_sent_block;
static ram_addr_t last_offset;
static unsigned long *migration_bitmap;
static uint64_t migration_dirty_pages;
static uint32_t last_version;

static inline
ram_addr_t migration_bitmap_find_and_reset_dirty(MemoryRegion *mr,
                                                 ram_addr_t start)
{
    unsigned long base = mr->ram_addr >> TARGET_PAGE_BITS;
    unsigned long nr = base + (start >> TARGET_PAGE_BITS);
    unsigned long size = base + (int128_get64(mr->size) >> TARGET_PAGE_BITS);

    unsigned long next = find_next_bit(migration_bitmap, size, nr);

    if (next < size) {
        clear_bit(next, migration_bitmap);
        migration_dirty_pages--;
    }
    return (next - base) << TARGET_PAGE_BITS;
}

static inline bool migration_bitmap_set_dirty(MemoryRegion *mr,
                                              ram_addr_t offset)
{
    bool ret;
    int nr = (mr->ram_addr + offset) >> TARGET_PAGE_BITS;

    ret = test_and_set_bit(nr, migration_bitmap);

    if (!ret) {
        migration_dirty_pages++;
    }
    return ret;
}

/* Needs iothread lock! */

static void migration_bitmap_sync(void)
{
    RAMBlock *block;
    ram_addr_t addr;
    uint64_t num_dirty_pages_init = migration_dirty_pages;
    MigrationState *s = migrate_get_current();
    static int64_t start_time;
    static int64_t num_dirty_pages_period;
    int64_t end_time;

    if (!start_time) {
        start_time = qemu_get_clock_ms(rt_clock);
    }

    trace_migration_bitmap_sync_start();
    memory_global_sync_dirty_bitmap(get_system_memory());

    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        for (addr = 0; addr < block->length; addr += TARGET_PAGE_SIZE) {
            if (memory_region_test_and_clear_dirty(block->mr,
                                                   addr, TARGET_PAGE_SIZE,
                                                   DIRTY_MEMORY_MIGRATION)) {
                migration_bitmap_set_dirty(block->mr, addr);
            }
        }
    }
    trace_migration_bitmap_sync_end(migration_dirty_pages
                                    - num_dirty_pages_init);
    num_dirty_pages_period += migration_dirty_pages - num_dirty_pages_init;
    end_time = qemu_get_clock_ms(rt_clock);

    /* more than 1 second = 1000 millisecons */
    if (end_time > start_time + 1000) {
        s->dirty_pages_rate = num_dirty_pages_period * 1000
            / (end_time - start_time);
        s->dirty_bytes_rate = s->dirty_pages_rate * TARGET_PAGE_SIZE;
        start_time = end_time;
        num_dirty_pages_period = 0;
    }
}

/*
 * ram_save_block: Writes a page of memory to the stream f
 *
 * Returns:  The number of bytes written.
 *           0 means no dirty pages
 */

static int ram_save_block(QEMUFile *f, bool last_stage)
{
    RAMBlock *block = last_seen_block;
    ram_addr_t offset = last_offset;
    bool complete_round = false;
    int bytes_sent = 0;
    MemoryRegion *mr;
    ram_addr_t current_addr;

    if (!block)
        block = QTAILQ_FIRST(&ram_list.blocks);

    while (true) {
        mr = block->mr;
        offset = migration_bitmap_find_and_reset_dirty(mr, offset);
        if (complete_round && block == last_seen_block &&
            offset >= last_offset) {
            break;
        }
        if (offset >= block->length) {
            offset = 0;
            block = QTAILQ_NEXT(block, next);
            if (!block) {
                block = QTAILQ_FIRST(&ram_list.blocks);
                complete_round = true;
            }
        } else {
            uint8_t *p;
            int cont = (block == last_sent_block) ?
                RAM_SAVE_FLAG_CONTINUE : 0;

            p = memory_region_get_ram_ptr(mr) + offset;

            /* In doubt sent page as normal */
            bytes_sent = -1;
            if (is_dup_page(p)) {
                acct_info.dup_pages++;
                bytes_sent = save_block_hdr(f, block, offset, cont,
                                            RAM_SAVE_FLAG_COMPRESS);
                qemu_put_byte(f, *p);
                bytes_sent += 1;
            } else if (migrate_use_xbzrle()) {
                current_addr = block->offset + offset;
                bytes_sent = save_xbzrle_page(f, p, current_addr, block,
                                              offset, cont, last_stage);
                if (!last_stage) {
                    p = get_cached_data(XBZRLE.cache, current_addr);
                }
            }

            /* XBZRLE overflow or normal page */
            if (bytes_sent == -1) {
                bytes_sent = save_block_hdr(f, block, offset, cont, RAM_SAVE_FLAG_PAGE);
                qemu_put_buffer(f, p, TARGET_PAGE_SIZE);
                bytes_sent += TARGET_PAGE_SIZE;
                acct_info.norm_pages++;
            }

            /* if page is unmodified, continue to the next */
            if (bytes_sent > 0) {
                last_sent_block = block;
                break;
            }
        }
    }
    last_seen_block = block;
    last_offset = offset;

    return bytes_sent;
}

static uint64_t bytes_transferred;

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

    QTAILQ_FOREACH(block, &ram_list.blocks, next)
        total += block->length;

    return total;
}

static void migration_end(void)
{
    if (migration_bitmap) {
        memory_global_dirty_log_stop();
        g_free(migration_bitmap);
        migration_bitmap = NULL;
    }

    if (XBZRLE.cache) {
        cache_fini(XBZRLE.cache);
        g_free(XBZRLE.cache);
        g_free(XBZRLE.encoded_buf);
        g_free(XBZRLE.current_buf);
        g_free(XBZRLE.decoded_buf);
        XBZRLE.cache = NULL;
    }
}

static void ram_migration_cancel(void *opaque)
{
    migration_end();
}

static void reset_ram_globals(void)
{
    last_seen_block = NULL;
    last_sent_block = NULL;
    last_offset = 0;
    last_version = ram_list.version;
}

#define MAX_WAIT 50 /* ms, half buffered_file limit */

static int ram_save_setup(QEMUFile *f, void *opaque)
{
    RAMBlock *block;
    int64_t ram_pages = last_ram_offset() >> TARGET_PAGE_BITS;

    migration_bitmap = bitmap_new(ram_pages);
    bitmap_set(migration_bitmap, 0, ram_pages);
    migration_dirty_pages = ram_pages;

    if (migrate_use_xbzrle()) {
        XBZRLE.cache = cache_init(migrate_xbzrle_cache_size() /
                                  TARGET_PAGE_SIZE,
                                  TARGET_PAGE_SIZE);
        if (!XBZRLE.cache) {
            DPRINTF("Error creating cache\n");
            return -1;
        }
        XBZRLE.encoded_buf = g_malloc0(TARGET_PAGE_SIZE);
        XBZRLE.current_buf = g_malloc(TARGET_PAGE_SIZE);
        acct_clear();
    }

    qemu_mutex_lock_iothread();
    qemu_mutex_lock_ramlist();
    bytes_transferred = 0;
    reset_ram_globals();

    memory_global_dirty_log_start();
    migration_bitmap_sync();
    qemu_mutex_unlock_iothread();

    qemu_put_be64(f, ram_bytes_total() | RAM_SAVE_FLAG_MEM_SIZE);

    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        qemu_put_byte(f, strlen(block->idstr));
        qemu_put_buffer(f, (uint8_t *)block->idstr, strlen(block->idstr));
        qemu_put_be64(f, block->length);
    }

    qemu_mutex_unlock_ramlist();
    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    return 0;
}

static int ram_save_iterate(QEMUFile *f, void *opaque)
{
    int ret;
    int i;
    int64_t t0;
    int total_sent = 0;

    qemu_mutex_lock_ramlist();

    if (ram_list.version != last_version) {
        reset_ram_globals();
    }

    t0 = qemu_get_clock_ns(rt_clock);
    i = 0;
    while ((ret = qemu_file_rate_limit(f)) == 0) {
        int bytes_sent;

        bytes_sent = ram_save_block(f, false);
        /* no more blocks to sent */
        if (bytes_sent == 0) {
            break;
        }
        total_sent += bytes_sent;
        acct_info.iterations++;
        /* we want to check in the 1st loop, just in case it was the 1st time
           and we had to sync the dirty bitmap.
           qemu_get_clock_ns() is a bit expensive, so we only check each some
           iterations
        */
        if ((i & 63) == 0) {
            uint64_t t1 = (qemu_get_clock_ns(rt_clock) - t0) / 1000000;
            if (t1 > MAX_WAIT) {
                DPRINTF("big wait: %" PRIu64 " milliseconds, %d iterations\n",
                        t1, i);
                break;
            }
        }
        i++;
    }

    qemu_mutex_unlock_ramlist();

    if (ret < 0) {
        bytes_transferred += total_sent;
        return ret;
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    total_sent += 8;
    bytes_transferred += total_sent;

    return total_sent;
}

static int ram_save_complete(QEMUFile *f, void *opaque)
{
    qemu_mutex_lock_ramlist();
    migration_bitmap_sync();

    /* try transferring iterative blocks of memory */

    /* flush all remaining blocks regardless of rate limiting */
    while (true) {
        int bytes_sent;

        bytes_sent = ram_save_block(f, true);
        /* no more blocks to sent */
        if (bytes_sent == 0) {
            break;
        }
        bytes_transferred += bytes_sent;
    }
    migration_end();

    qemu_mutex_unlock_ramlist();
    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    return 0;
}

static uint64_t ram_save_pending(QEMUFile *f, void *opaque, uint64_t max_size)
{
    uint64_t remaining_size;

    remaining_size = ram_save_remaining() * TARGET_PAGE_SIZE;

    if (remaining_size < max_size) {
        qemu_mutex_lock_iothread();
        migration_bitmap_sync();
        qemu_mutex_unlock_iothread();
        remaining_size = ram_save_remaining() * TARGET_PAGE_SIZE;
    }
    return remaining_size;
}

static int load_xbzrle(QEMUFile *f, ram_addr_t addr, void *host)
{
    int ret, rc = 0;
    unsigned int xh_len;
    int xh_flags;

    if (!XBZRLE.decoded_buf) {
        XBZRLE.decoded_buf = g_malloc(TARGET_PAGE_SIZE);
    }

    /* extract RLE header */
    xh_flags = qemu_get_byte(f);
    xh_len = qemu_get_be16(f);

    if (xh_flags != ENCODING_FLAG_XBZRLE) {
        fprintf(stderr, "Failed to load XBZRLE page - wrong compression!\n");
        return -1;
    }

    if (xh_len > TARGET_PAGE_SIZE) {
        fprintf(stderr, "Failed to load XBZRLE page - len overflow!\n");
        return -1;
    }
    /* load data and decode */
    qemu_get_buffer(f, XBZRLE.decoded_buf, xh_len);

    /* decode RLE */
    ret = xbzrle_decode_buffer(XBZRLE.decoded_buf, xh_len, host,
                               TARGET_PAGE_SIZE);
    if (ret == -1) {
        fprintf(stderr, "Failed to load XBZRLE page - decode error!\n");
        rc = -1;
    } else  if (ret > TARGET_PAGE_SIZE) {
        fprintf(stderr, "Failed to load XBZRLE page - size %d exceeds %d!\n",
                ret, TARGET_PAGE_SIZE);
        abort();
    }

    return rc;
}

static inline void *host_from_stream_offset(QEMUFile *f,
                                            ram_addr_t offset,
                                            int flags)
{
    static RAMBlock *block = NULL;
    char id[256];
    uint8_t len;

    if (flags & RAM_SAVE_FLAG_CONTINUE) {
        if (!block) {
            fprintf(stderr, "Ack, bad migration stream!\n");
            return NULL;
        }

        return memory_region_get_ram_ptr(block->mr) + offset;
    }

    len = qemu_get_byte(f);
    qemu_get_buffer(f, (uint8_t *)id, len);
    id[len] = 0;

    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        if (!strncmp(id, block->idstr, sizeof(id)))
            return memory_region_get_ram_ptr(block->mr) + offset;
    }

    fprintf(stderr, "Can't find block %s!\n", id);
    return NULL;
}

static int ram_load(QEMUFile *f, void *opaque, int version_id)
{
    ram_addr_t addr;
    int flags, ret = 0;
    int error;
    static uint64_t seq_iter;

    seq_iter++;

    if (version_id < 4 || version_id > 4) {
        return -EINVAL;
    }

    do {
        addr = qemu_get_be64(f);

        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        if (flags & RAM_SAVE_FLAG_MEM_SIZE) {
            if (version_id == 4) {
                /* Synchronize RAM block list */
                char id[256];
                ram_addr_t length;
                ram_addr_t total_ram_bytes = addr;

                while (total_ram_bytes) {
                    RAMBlock *block;
                    uint8_t len;

                    len = qemu_get_byte(f);
                    qemu_get_buffer(f, (uint8_t *)id, len);
                    id[len] = 0;
                    length = qemu_get_be64(f);

                    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
                        if (!strncmp(id, block->idstr, sizeof(id))) {
                            if (block->length != length) {
                                ret =  -EINVAL;
                                goto done;
                            }
                            break;
                        }
                    }

                    if (!block) {
                        fprintf(stderr, "Unknown ramblock \"%s\", cannot "
                                "accept migration\n", id);
                        ret = -EINVAL;
                        goto done;
                    }

                    total_ram_bytes -= length;
                }
            }
        }

        if (flags & RAM_SAVE_FLAG_COMPRESS) {
            void *host;
            uint8_t ch;

            host = host_from_stream_offset(f, addr, flags);
            if (!host) {
                return -EINVAL;
            }

            ch = qemu_get_byte(f);
            memset(host, ch, TARGET_PAGE_SIZE);
#ifndef _WIN32
            if (ch == 0 &&
                (!kvm_enabled() || kvm_has_sync_mmu()) &&
                getpagesize() <= TARGET_PAGE_SIZE) {
                qemu_madvise(host, TARGET_PAGE_SIZE, QEMU_MADV_DONTNEED);
            }
#endif
        } else if (flags & RAM_SAVE_FLAG_PAGE) {
            void *host;

            host = host_from_stream_offset(f, addr, flags);
            if (!host) {
                return -EINVAL;
            }

            qemu_get_buffer(f, host, TARGET_PAGE_SIZE);
        } else if (flags & RAM_SAVE_FLAG_XBZRLE) {
            void *host = host_from_stream_offset(f, addr, flags);
            if (!host) {
                return -EINVAL;
            }

            if (load_xbzrle(f, addr, host) < 0) {
                ret = -EINVAL;
                goto done;
            }
        }
        error = qemu_file_get_error(f);
        if (error) {
            ret = error;
            goto done;
        }
    } while (!(flags & RAM_SAVE_FLAG_EOS));

done:
    DPRINTF("Completed load of VM with exit code %d seq iteration "
            "%" PRIu64 "\n", ret, seq_iter);
    return ret;
}

SaveVMHandlers savevm_ram_handlers = {
    .save_live_setup = ram_save_setup,
    .save_live_iterate = ram_save_iterate,
    .save_live_complete = ram_save_complete,
    .save_live_pending = ram_save_pending,
    .load_state = ram_load,
    .cancel = ram_migration_cancel,
};

#ifdef HAS_AUDIO
struct soundhw {
    const char *name;
    const char *descr;
    int enabled;
    int isa;
    union {
        int (*init_isa) (ISABus *bus);
        int (*init_pci) (PCIBus *bus);
    } init;
};

static struct soundhw soundhw[] = {
#ifdef HAS_AUDIO_CHOICE
#ifdef CONFIG_PCSPK
    {
        "pcspk",
        "PC speaker",
        0,
        1,
        { .init_isa = pcspk_audio_init }
    },
#endif

#ifdef CONFIG_SB16
    {
        "sb16",
        "Creative Sound Blaster 16",
        0,
        1,
        { .init_isa = SB16_init }
    },
#endif

#ifdef CONFIG_CS4231A
    {
        "cs4231a",
        "CS4231A",
        0,
        1,
        { .init_isa = cs4231a_init }
    },
#endif

#ifdef CONFIG_ADLIB
    {
        "adlib",
#ifdef HAS_YMF262
        "Yamaha YMF262 (OPL3)",
#else
        "Yamaha YM3812 (OPL2)",
#endif
        0,
        1,
        { .init_isa = Adlib_init }
    },
#endif

#ifdef CONFIG_GUS
    {
        "gus",
        "Gravis Ultrasound GF1",
        0,
        1,
        { .init_isa = GUS_init }
    },
#endif

#ifdef CONFIG_AC97
    {
        "ac97",
        "Intel 82801AA AC97 Audio",
        0,
        0,
        { .init_pci = ac97_init }
    },
#endif

#ifdef CONFIG_ES1370
    {
        "es1370",
        "ENSONIQ AudioPCI ES1370",
        0,
        0,
        { .init_pci = es1370_init }
    },
#endif

#ifdef CONFIG_HDA
    {
        "hda",
        "Intel HD Audio",
        0,
        0,
        { .init_pci = intel_hda_and_codec_init }
    },
#endif

#endif /* HAS_AUDIO_CHOICE */

    { NULL, NULL, 0, 0, { NULL } }
};

void select_soundhw(const char *optarg)
{
    struct soundhw *c;

    if (is_help_option(optarg)) {
    show_valid_cards:

#ifdef HAS_AUDIO_CHOICE
        printf("Valid sound card names (comma separated):\n");
        for (c = soundhw; c->name; ++c) {
            printf ("%-11s %s\n", c->name, c->descr);
        }
        printf("\n-soundhw all will enable all of the above\n");
#else
        printf("Machine has no user-selectable audio hardware "
               "(it may or may not have always-present audio hardware).\n");
#endif
        exit(!is_help_option(optarg));
    }
    else {
        size_t l;
        const char *p;
        char *e;
        int bad_card = 0;

        if (!strcmp(optarg, "all")) {
            for (c = soundhw; c->name; ++c) {
                c->enabled = 1;
            }
            return;
        }

        p = optarg;
        while (*p) {
            e = strchr(p, ',');
            l = !e ? strlen(p) : (size_t) (e - p);

            for (c = soundhw; c->name; ++c) {
                if (!strncmp(c->name, p, l) && !c->name[l]) {
                    c->enabled = 1;
                    break;
                }
            }

            if (!c->name) {
                if (l > 80) {
                    fprintf(stderr,
                            "Unknown sound card name (too big to show)\n");
                }
                else {
                    fprintf(stderr, "Unknown sound card name `%.*s'\n",
                            (int) l, p);
                }
                bad_card = 1;
            }
            p += l + (e != NULL);
        }

        if (bad_card) {
            goto show_valid_cards;
        }
    }
}

void audio_init(ISABus *isa_bus, PCIBus *pci_bus)
{
    struct soundhw *c;

    for (c = soundhw; c->name; ++c) {
        if (c->enabled) {
            if (c->isa) {
                if (isa_bus) {
                    c->init.init_isa(isa_bus);
                }
            } else {
                if (pci_bus) {
                    c->init.init_pci(pci_bus);
                }
            }
        }
    }
}
#else
void select_soundhw(const char *optarg)
{
}
void audio_init(ISABus *isa_bus, PCIBus *pci_bus)
{
}
#endif

int qemu_uuid_parse(const char *str, uint8_t *uuid)
{
    int ret;

    if (strlen(str) != 36) {
        return -1;
    }

    ret = sscanf(str, UUID_FMT, &uuid[0], &uuid[1], &uuid[2], &uuid[3],
                 &uuid[4], &uuid[5], &uuid[6], &uuid[7], &uuid[8], &uuid[9],
                 &uuid[10], &uuid[11], &uuid[12], &uuid[13], &uuid[14],
                 &uuid[15]);

    if (ret != 16) {
        return -1;
    }
#ifdef TARGET_I386
    smbios_add_field(1, offsetof(struct smbios_type_1, uuid), 16, uuid);
#endif
    return 0;
}

void do_acpitable_option(const char *optarg)
{
#ifdef TARGET_I386
    if (acpi_table_add(optarg) < 0) {
        fprintf(stderr, "Wrong acpi table provided\n");
        exit(1);
    }
#endif
}

void do_smbios_option(const char *optarg)
{
#ifdef TARGET_I386
    if (smbios_entry_add(optarg) < 0) {
        fprintf(stderr, "Wrong smbios provided\n");
        exit(1);
    }
#endif
}

void cpudef_init(void)
{
#if defined(cpudef_setup)
    cpudef_setup(); /* parse cpu definitions in target config file */
#endif
}

int audio_available(void)
{
#ifdef HAS_AUDIO
    return 1;
#else
    return 0;
#endif
}

int tcg_available(void)
{
    return 1;
}

int kvm_available(void)
{
#ifdef CONFIG_KVM
    return 1;
#else
    return 0;
#endif
}

int xen_available(void)
{
#ifdef CONFIG_XEN
    return 1;
#else
    return 0;
#endif
}


TargetInfo *qmp_query_target(Error **errp)
{
    TargetInfo *info = g_malloc0(sizeof(*info));

    info->arch = TARGET_TYPE;

    return info;
}
