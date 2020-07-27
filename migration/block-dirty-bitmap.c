/*
 * Block dirty bitmap postcopy migration
 *
 * Copyright IBM, Corp. 2009
 * Copyright (c) 2016-2017 Virtuozzo International GmbH. All rights reserved.
 *
 * Authors:
 *  Liran Schour   <lirans@il.ibm.com>
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 * This file is derived from migration/block.c, so it's author and IBM copyright
 * are here, although content is quite different.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 *
 *                                ***
 *
 * Here postcopy migration of dirty bitmaps is realized. Only QMP-addressable
 * bitmaps are migrated.
 *
 * Bitmap migration implies creating bitmap with the same name and granularity
 * in destination QEMU. If the bitmap with the same name (for the same node)
 * already exists on destination an error will be generated.
 *
 * format of migration:
 *
 * # Header (shared for different chunk types)
 * 1, 2 or 4 bytes: flags (see qemu_{put,put}_flags)
 * [ 1 byte: node name size ] \  flags & DEVICE_NAME
 * [ n bytes: node name     ] /
 * [ 1 byte: bitmap name size ] \  flags & BITMAP_NAME
 * [ n bytes: bitmap name     ] /
 *
 * # Start of bitmap migration (flags & START)
 * header
 * be64: granularity
 * 1 byte: bitmap flags (corresponds to BdrvDirtyBitmap)
 *   bit 0    -  bitmap is enabled
 *   bit 1    -  bitmap is persistent
 *   bit 2    -  bitmap is autoloading
 *   bits 3-7 - reserved, must be zero
 *
 * # Complete of bitmap migration (flags & COMPLETE)
 * header
 *
 * # Data chunk of bitmap migration
 * header
 * be64: start sector
 * be32: number of sectors
 * [ be64: buffer size  ] \ ! (flags & ZEROES)
 * [ n bytes: buffer    ] /
 *
 * The last chunk in stream should contain flags & EOS. The chunk may skip
 * device and/or bitmap names, assuming them to be the same with the previous
 * chunk.
 */

#include "qemu/osdep.h"
#include "block/block.h"
#include "block/block_int.h"
#include "sysemu/block-backend.h"
#include "sysemu/runstate.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "migration/misc.h"
#include "migration/migration.h"
#include "qemu-file.h"
#include "migration/vmstate.h"
#include "migration/register.h"
#include "qemu/hbitmap.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "trace.h"

#define CHUNK_SIZE     (1 << 10)

/* Flags occupy one, two or four bytes (Big Endian). The size is determined as
 * follows:
 * in first (most significant) byte bit 8 is clear  -->  one byte
 * in first byte bit 8 is set    -->  two or four bytes, depending on second
 *                                    byte:
 *    | in second byte bit 8 is clear  -->  two bytes
 *    | in second byte bit 8 is set    -->  four bytes
 */
#define DIRTY_BITMAP_MIG_FLAG_EOS           0x01
#define DIRTY_BITMAP_MIG_FLAG_ZEROES        0x02
#define DIRTY_BITMAP_MIG_FLAG_BITMAP_NAME   0x04
#define DIRTY_BITMAP_MIG_FLAG_DEVICE_NAME   0x08
#define DIRTY_BITMAP_MIG_FLAG_START         0x10
#define DIRTY_BITMAP_MIG_FLAG_COMPLETE      0x20
#define DIRTY_BITMAP_MIG_FLAG_BITS          0x40

#define DIRTY_BITMAP_MIG_EXTRA_FLAGS        0x80

#define DIRTY_BITMAP_MIG_START_FLAG_ENABLED          0x01
#define DIRTY_BITMAP_MIG_START_FLAG_PERSISTENT       0x02
/* 0x04 was "AUTOLOAD" flags on elder versions, no it is ignored */
#define DIRTY_BITMAP_MIG_START_FLAG_RESERVED_MASK    0xf8

typedef struct DirtyBitmapMigBitmapState {
    /* Written during setup phase. */
    BlockDriverState *bs;
    const char *node_name;
    BdrvDirtyBitmap *bitmap;
    uint64_t total_sectors;
    uint64_t sectors_per_chunk;
    QSIMPLEQ_ENTRY(DirtyBitmapMigBitmapState) entry;
    uint8_t flags;

    /* For bulk phase. */
    bool bulk_completed;
    uint64_t cur_sector;
} DirtyBitmapMigBitmapState;

typedef struct DirtyBitmapMigState {
    QSIMPLEQ_HEAD(, DirtyBitmapMigBitmapState) dbms_list;

    bool bulk_completed;
    bool no_bitmaps;

    /* for send_bitmap_bits() */
    BlockDriverState *prev_bs;
    BdrvDirtyBitmap *prev_bitmap;
} DirtyBitmapMigState;

typedef struct DirtyBitmapLoadState {
    uint32_t flags;
    char node_name[256];
    char bitmap_name[256];
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;
} DirtyBitmapLoadState;

static DirtyBitmapMigState dirty_bitmap_mig_state;

typedef struct DirtyBitmapLoadBitmapState {
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;
    bool migrated;
} DirtyBitmapLoadBitmapState;
static GSList *enabled_bitmaps;
QemuMutex finish_lock;

void init_dirty_bitmap_incoming_migration(void)
{
    qemu_mutex_init(&finish_lock);
}

static uint32_t qemu_get_bitmap_flags(QEMUFile *f)
{
    uint8_t flags = qemu_get_byte(f);
    if (flags & DIRTY_BITMAP_MIG_EXTRA_FLAGS) {
        flags = flags << 8 | qemu_get_byte(f);
        if (flags & DIRTY_BITMAP_MIG_EXTRA_FLAGS) {
            flags = flags << 16 | qemu_get_be16(f);
        }
    }

    return flags;
}

static void qemu_put_bitmap_flags(QEMUFile *f, uint32_t flags)
{
    /* The code currently do not send flags more than one byte */
    assert(!(flags & (0xffffff00 | DIRTY_BITMAP_MIG_EXTRA_FLAGS)));

    qemu_put_byte(f, flags);
}

static void send_bitmap_header(QEMUFile *f, DirtyBitmapMigBitmapState *dbms,
                               uint32_t additional_flags)
{
    BlockDriverState *bs = dbms->bs;
    BdrvDirtyBitmap *bitmap = dbms->bitmap;
    uint32_t flags = additional_flags;
    trace_send_bitmap_header_enter();

    if (bs != dirty_bitmap_mig_state.prev_bs) {
        dirty_bitmap_mig_state.prev_bs = bs;
        flags |= DIRTY_BITMAP_MIG_FLAG_DEVICE_NAME;
    }

    if (bitmap != dirty_bitmap_mig_state.prev_bitmap) {
        dirty_bitmap_mig_state.prev_bitmap = bitmap;
        flags |= DIRTY_BITMAP_MIG_FLAG_BITMAP_NAME;
    }

    qemu_put_bitmap_flags(f, flags);

    if (flags & DIRTY_BITMAP_MIG_FLAG_DEVICE_NAME) {
        qemu_put_counted_string(f, dbms->node_name);
    }

    if (flags & DIRTY_BITMAP_MIG_FLAG_BITMAP_NAME) {
        qemu_put_counted_string(f, bdrv_dirty_bitmap_name(bitmap));
    }
}

static void send_bitmap_start(QEMUFile *f, DirtyBitmapMigBitmapState *dbms)
{
    send_bitmap_header(f, dbms, DIRTY_BITMAP_MIG_FLAG_START);
    qemu_put_be32(f, bdrv_dirty_bitmap_granularity(dbms->bitmap));
    qemu_put_byte(f, dbms->flags);
}

static void send_bitmap_complete(QEMUFile *f, DirtyBitmapMigBitmapState *dbms)
{
    send_bitmap_header(f, dbms, DIRTY_BITMAP_MIG_FLAG_COMPLETE);
}

static void send_bitmap_bits(QEMUFile *f, DirtyBitmapMigBitmapState *dbms,
                             uint64_t start_sector, uint32_t nr_sectors)
{
    /* align for buffer_is_zero() */
    uint64_t align = 4 * sizeof(long);
    uint64_t unaligned_size =
        bdrv_dirty_bitmap_serialization_size(
            dbms->bitmap, start_sector << BDRV_SECTOR_BITS,
            (uint64_t)nr_sectors << BDRV_SECTOR_BITS);
    uint64_t buf_size = QEMU_ALIGN_UP(unaligned_size, align);
    uint8_t *buf = g_malloc0(buf_size);
    uint32_t flags = DIRTY_BITMAP_MIG_FLAG_BITS;

    bdrv_dirty_bitmap_serialize_part(
        dbms->bitmap, buf, start_sector << BDRV_SECTOR_BITS,
        (uint64_t)nr_sectors << BDRV_SECTOR_BITS);

    if (buffer_is_zero(buf, buf_size)) {
        g_free(buf);
        buf = NULL;
        flags |= DIRTY_BITMAP_MIG_FLAG_ZEROES;
    }

    trace_send_bitmap_bits(flags, start_sector, nr_sectors, buf_size);

    send_bitmap_header(f, dbms, flags);

    qemu_put_be64(f, start_sector);
    qemu_put_be32(f, nr_sectors);

    /* if a block is zero we need to flush here since the network
     * bandwidth is now a lot higher than the storage device bandwidth.
     * thus if we queue zero blocks we slow down the migration. */
    if (flags & DIRTY_BITMAP_MIG_FLAG_ZEROES) {
        qemu_fflush(f);
    } else {
        qemu_put_be64(f, buf_size);
        qemu_put_buffer(f, buf, buf_size);
    }

    g_free(buf);
}

/* Called with iothread lock taken.  */
static void dirty_bitmap_mig_cleanup(void)
{
    DirtyBitmapMigBitmapState *dbms;

    while ((dbms = QSIMPLEQ_FIRST(&dirty_bitmap_mig_state.dbms_list)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&dirty_bitmap_mig_state.dbms_list, entry);
        bdrv_dirty_bitmap_set_busy(dbms->bitmap, false);
        bdrv_unref(dbms->bs);
        g_free(dbms);
    }
}

/* Called with iothread lock taken. */
static int init_dirty_bitmap_migration(void)
{
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;
    DirtyBitmapMigBitmapState *dbms;
    Error *local_err = NULL;

    dirty_bitmap_mig_state.bulk_completed = false;
    dirty_bitmap_mig_state.prev_bs = NULL;
    dirty_bitmap_mig_state.prev_bitmap = NULL;
    dirty_bitmap_mig_state.no_bitmaps = false;

    for (bs = bdrv_next_all_states(NULL); bs; bs = bdrv_next_all_states(bs)) {
        const char *name = bdrv_get_device_or_node_name(bs);

        FOR_EACH_DIRTY_BITMAP(bs, bitmap) {
            if (!bdrv_dirty_bitmap_name(bitmap)) {
                continue;
            }

            if (!name || strcmp(name, "") == 0) {
                error_report("Found bitmap '%s' in unnamed node %p. It can't "
                             "be migrated", bdrv_dirty_bitmap_name(bitmap), bs);
                goto fail;
            }

            if (bdrv_dirty_bitmap_check(bitmap, BDRV_BITMAP_DEFAULT,
                                        &local_err)) {
                error_report_err(local_err);
                goto fail;
            }

            bdrv_ref(bs);
            bdrv_dirty_bitmap_set_busy(bitmap, true);

            dbms = g_new0(DirtyBitmapMigBitmapState, 1);
            dbms->bs = bs;
            dbms->node_name = name;
            dbms->bitmap = bitmap;
            dbms->total_sectors = bdrv_nb_sectors(bs);
            dbms->sectors_per_chunk = CHUNK_SIZE * 8 *
                bdrv_dirty_bitmap_granularity(bitmap) >> BDRV_SECTOR_BITS;
            if (bdrv_dirty_bitmap_enabled(bitmap)) {
                dbms->flags |= DIRTY_BITMAP_MIG_START_FLAG_ENABLED;
            }
            if (bdrv_dirty_bitmap_get_persistence(bitmap)) {
                dbms->flags |= DIRTY_BITMAP_MIG_START_FLAG_PERSISTENT;
            }

            QSIMPLEQ_INSERT_TAIL(&dirty_bitmap_mig_state.dbms_list,
                                 dbms, entry);
        }
    }

    /* unset migration flags here, to not roll back it */
    QSIMPLEQ_FOREACH(dbms, &dirty_bitmap_mig_state.dbms_list, entry) {
        bdrv_dirty_bitmap_skip_store(dbms->bitmap, true);
    }

    if (QSIMPLEQ_EMPTY(&dirty_bitmap_mig_state.dbms_list)) {
        dirty_bitmap_mig_state.no_bitmaps = true;
    }

    return 0;

fail:
    dirty_bitmap_mig_cleanup();

    return -1;
}

/* Called with no lock taken.  */
static void bulk_phase_send_chunk(QEMUFile *f, DirtyBitmapMigBitmapState *dbms)
{
    uint32_t nr_sectors = MIN(dbms->total_sectors - dbms->cur_sector,
                             dbms->sectors_per_chunk);

    send_bitmap_bits(f, dbms, dbms->cur_sector, nr_sectors);

    dbms->cur_sector += nr_sectors;
    if (dbms->cur_sector >= dbms->total_sectors) {
        dbms->bulk_completed = true;
    }
}

/* Called with no lock taken.  */
static void bulk_phase(QEMUFile *f, bool limit)
{
    DirtyBitmapMigBitmapState *dbms;

    QSIMPLEQ_FOREACH(dbms, &dirty_bitmap_mig_state.dbms_list, entry) {
        while (!dbms->bulk_completed) {
            bulk_phase_send_chunk(f, dbms);
            if (limit && qemu_file_rate_limit(f)) {
                return;
            }
        }
    }

    dirty_bitmap_mig_state.bulk_completed = true;
}

/* for SaveVMHandlers */
static void dirty_bitmap_save_cleanup(void *opaque)
{
    dirty_bitmap_mig_cleanup();
}

static int dirty_bitmap_save_iterate(QEMUFile *f, void *opaque)
{
    trace_dirty_bitmap_save_iterate(migration_in_postcopy());

    if (migration_in_postcopy() && !dirty_bitmap_mig_state.bulk_completed) {
        bulk_phase(f, true);
    }

    qemu_put_bitmap_flags(f, DIRTY_BITMAP_MIG_FLAG_EOS);

    return dirty_bitmap_mig_state.bulk_completed;
}

/* Called with iothread lock taken.  */

static int dirty_bitmap_save_complete(QEMUFile *f, void *opaque)
{
    DirtyBitmapMigBitmapState *dbms;
    trace_dirty_bitmap_save_complete_enter();

    if (!dirty_bitmap_mig_state.bulk_completed) {
        bulk_phase(f, false);
    }

    QSIMPLEQ_FOREACH(dbms, &dirty_bitmap_mig_state.dbms_list, entry) {
        send_bitmap_complete(f, dbms);
    }

    qemu_put_bitmap_flags(f, DIRTY_BITMAP_MIG_FLAG_EOS);

    trace_dirty_bitmap_save_complete_finish();

    dirty_bitmap_mig_cleanup();
    return 0;
}

static void dirty_bitmap_save_pending(QEMUFile *f, void *opaque,
                                      uint64_t max_size,
                                      uint64_t *res_precopy_only,
                                      uint64_t *res_compatible,
                                      uint64_t *res_postcopy_only)
{
    DirtyBitmapMigBitmapState *dbms;
    uint64_t pending = 0;

    qemu_mutex_lock_iothread();

    QSIMPLEQ_FOREACH(dbms, &dirty_bitmap_mig_state.dbms_list, entry) {
        uint64_t gran = bdrv_dirty_bitmap_granularity(dbms->bitmap);
        uint64_t sectors = dbms->bulk_completed ? 0 :
                           dbms->total_sectors - dbms->cur_sector;

        pending += DIV_ROUND_UP(sectors * BDRV_SECTOR_SIZE, gran);
    }

    qemu_mutex_unlock_iothread();

    trace_dirty_bitmap_save_pending(pending, max_size);

    *res_postcopy_only += pending;
}

/* First occurrence of this bitmap. It should be created if doesn't exist */
static int dirty_bitmap_load_start(QEMUFile *f, DirtyBitmapLoadState *s)
{
    Error *local_err = NULL;
    uint32_t granularity = qemu_get_be32(f);
    uint8_t flags = qemu_get_byte(f);

    if (s->bitmap) {
        error_report("Bitmap with the same name ('%s') already exists on "
                     "destination", bdrv_dirty_bitmap_name(s->bitmap));
        return -EINVAL;
    } else {
        s->bitmap = bdrv_create_dirty_bitmap(s->bs, granularity,
                                             s->bitmap_name, &local_err);
        if (!s->bitmap) {
            error_report_err(local_err);
            return -EINVAL;
        }
    }

    if (flags & DIRTY_BITMAP_MIG_START_FLAG_RESERVED_MASK) {
        error_report("Unknown flags in migrated dirty bitmap header: %x",
                     flags);
        return -EINVAL;
    }

    if (flags & DIRTY_BITMAP_MIG_START_FLAG_PERSISTENT) {
        bdrv_dirty_bitmap_set_persistence(s->bitmap, true);
    }

    bdrv_disable_dirty_bitmap(s->bitmap);
    if (flags & DIRTY_BITMAP_MIG_START_FLAG_ENABLED) {
        DirtyBitmapLoadBitmapState *b;

        bdrv_dirty_bitmap_create_successor(s->bitmap, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -EINVAL;
        }

        b = g_new(DirtyBitmapLoadBitmapState, 1);
        b->bs = s->bs;
        b->bitmap = s->bitmap;
        b->migrated = false;
        enabled_bitmaps = g_slist_prepend(enabled_bitmaps, b);
    }

    return 0;
}

void dirty_bitmap_mig_before_vm_start(void)
{
    GSList *item;

    qemu_mutex_lock(&finish_lock);

    for (item = enabled_bitmaps; item; item = g_slist_next(item)) {
        DirtyBitmapLoadBitmapState *b = item->data;

        if (b->migrated) {
            bdrv_enable_dirty_bitmap(b->bitmap);
        } else {
            bdrv_dirty_bitmap_enable_successor(b->bitmap);
        }

        g_free(b);
    }

    g_slist_free(enabled_bitmaps);
    enabled_bitmaps = NULL;

    qemu_mutex_unlock(&finish_lock);
}

static void dirty_bitmap_load_complete(QEMUFile *f, DirtyBitmapLoadState *s)
{
    GSList *item;
    trace_dirty_bitmap_load_complete();
    bdrv_dirty_bitmap_deserialize_finish(s->bitmap);

    qemu_mutex_lock(&finish_lock);

    for (item = enabled_bitmaps; item; item = g_slist_next(item)) {
        DirtyBitmapLoadBitmapState *b = item->data;

        if (b->bitmap == s->bitmap) {
            b->migrated = true;
            break;
        }
    }

    if (bdrv_dirty_bitmap_has_successor(s->bitmap)) {
        bdrv_dirty_bitmap_lock(s->bitmap);
        if (enabled_bitmaps == NULL) {
            /* in postcopy */
            bdrv_reclaim_dirty_bitmap_locked(s->bitmap, &error_abort);
            bdrv_enable_dirty_bitmap_locked(s->bitmap);
        } else {
            /* target not started, successor must be empty */
            int64_t count = bdrv_get_dirty_count(s->bitmap);
            BdrvDirtyBitmap *ret = bdrv_reclaim_dirty_bitmap_locked(s->bitmap,
                                                                    NULL);
            /* bdrv_reclaim_dirty_bitmap can fail only on no successor (it
             * must be) or on merge fail, but merge can't fail when second
             * bitmap is empty
             */
            assert(ret == s->bitmap &&
                   count == bdrv_get_dirty_count(s->bitmap));
        }
        bdrv_dirty_bitmap_unlock(s->bitmap);
    }

    qemu_mutex_unlock(&finish_lock);
}

static int dirty_bitmap_load_bits(QEMUFile *f, DirtyBitmapLoadState *s)
{
    uint64_t first_byte = qemu_get_be64(f) << BDRV_SECTOR_BITS;
    uint64_t nr_bytes = (uint64_t)qemu_get_be32(f) << BDRV_SECTOR_BITS;
    trace_dirty_bitmap_load_bits_enter(first_byte >> BDRV_SECTOR_BITS,
                                       nr_bytes >> BDRV_SECTOR_BITS);

    if (s->flags & DIRTY_BITMAP_MIG_FLAG_ZEROES) {
        trace_dirty_bitmap_load_bits_zeroes();
        bdrv_dirty_bitmap_deserialize_zeroes(s->bitmap, first_byte, nr_bytes,
                                             false);
    } else {
        size_t ret;
        uint8_t *buf;
        uint64_t buf_size = qemu_get_be64(f);
        uint64_t needed_size =
            bdrv_dirty_bitmap_serialization_size(s->bitmap,
                                                 first_byte, nr_bytes);

        if (needed_size > buf_size ||
            buf_size > QEMU_ALIGN_UP(needed_size, 4 * sizeof(long))
             /* Here used same alignment as in send_bitmap_bits */
        ) {
            error_report("Migrated bitmap granularity doesn't "
                         "match the destination bitmap '%s' granularity",
                         bdrv_dirty_bitmap_name(s->bitmap));
            return -EINVAL;
        }

        buf = g_malloc(buf_size);
        ret = qemu_get_buffer(f, buf, buf_size);
        if (ret != buf_size) {
            error_report("Failed to read bitmap bits");
            g_free(buf);
            return -EIO;
        }

        bdrv_dirty_bitmap_deserialize_part(s->bitmap, buf, first_byte, nr_bytes,
                                           false);
        g_free(buf);
    }

    return 0;
}

static int dirty_bitmap_load_header(QEMUFile *f, DirtyBitmapLoadState *s)
{
    Error *local_err = NULL;
    bool nothing;
    s->flags = qemu_get_bitmap_flags(f);
    trace_dirty_bitmap_load_header(s->flags);

    nothing = s->flags == (s->flags & DIRTY_BITMAP_MIG_FLAG_EOS);

    if (s->flags & DIRTY_BITMAP_MIG_FLAG_DEVICE_NAME) {
        if (!qemu_get_counted_string(f, s->node_name)) {
            error_report("Unable to read node name string");
            return -EINVAL;
        }
        s->bs = bdrv_lookup_bs(s->node_name, s->node_name, &local_err);
        if (!s->bs) {
            error_report_err(local_err);
            return -EINVAL;
        }
    } else if (!s->bs && !nothing) {
        error_report("Error: block device name is not set");
        return -EINVAL;
    }

    if (s->flags & DIRTY_BITMAP_MIG_FLAG_BITMAP_NAME) {
        if (!qemu_get_counted_string(f, s->bitmap_name)) {
            error_report("Unable to read bitmap name string");
            return -EINVAL;
        }
        s->bitmap = bdrv_find_dirty_bitmap(s->bs, s->bitmap_name);

        /* bitmap may be NULL here, it wouldn't be an error if it is the
         * first occurrence of the bitmap */
        if (!s->bitmap && !(s->flags & DIRTY_BITMAP_MIG_FLAG_START)) {
            error_report("Error: unknown dirty bitmap "
                         "'%s' for block device '%s'",
                         s->bitmap_name, s->node_name);
            return -EINVAL;
        }
    } else if (!s->bitmap && !nothing) {
        error_report("Error: block device name is not set");
        return -EINVAL;
    }

    return 0;
}

static int dirty_bitmap_load(QEMUFile *f, void *opaque, int version_id)
{
    static DirtyBitmapLoadState s;
    int ret = 0;

    trace_dirty_bitmap_load_enter();

    if (version_id != 1) {
        return -EINVAL;
    }

    do {
        ret = dirty_bitmap_load_header(f, &s);
        if (ret < 0) {
            return ret;
        }

        if (s.flags & DIRTY_BITMAP_MIG_FLAG_START) {
            ret = dirty_bitmap_load_start(f, &s);
        } else if (s.flags & DIRTY_BITMAP_MIG_FLAG_COMPLETE) {
            dirty_bitmap_load_complete(f, &s);
        } else if (s.flags & DIRTY_BITMAP_MIG_FLAG_BITS) {
            ret = dirty_bitmap_load_bits(f, &s);
        }

        if (!ret) {
            ret = qemu_file_get_error(f);
        }

        if (ret) {
            return ret;
        }
    } while (!(s.flags & DIRTY_BITMAP_MIG_FLAG_EOS));

    trace_dirty_bitmap_load_success();
    return 0;
}

static int dirty_bitmap_save_setup(QEMUFile *f, void *opaque)
{
    DirtyBitmapMigBitmapState *dbms = NULL;
    if (init_dirty_bitmap_migration() < 0) {
        return -1;
    }

    QSIMPLEQ_FOREACH(dbms, &dirty_bitmap_mig_state.dbms_list, entry) {
        send_bitmap_start(f, dbms);
    }
    qemu_put_bitmap_flags(f, DIRTY_BITMAP_MIG_FLAG_EOS);

    return 0;
}

static bool dirty_bitmap_is_active(void *opaque)
{
    return migrate_dirty_bitmaps() && !dirty_bitmap_mig_state.no_bitmaps;
}

static bool dirty_bitmap_is_active_iterate(void *opaque)
{
    return dirty_bitmap_is_active(opaque) && !runstate_is_running();
}

static bool dirty_bitmap_has_postcopy(void *opaque)
{
    return true;
}

static SaveVMHandlers savevm_dirty_bitmap_handlers = {
    .save_setup = dirty_bitmap_save_setup,
    .save_live_complete_postcopy = dirty_bitmap_save_complete,
    .save_live_complete_precopy = dirty_bitmap_save_complete,
    .has_postcopy = dirty_bitmap_has_postcopy,
    .save_live_pending = dirty_bitmap_save_pending,
    .save_live_iterate = dirty_bitmap_save_iterate,
    .is_active_iterate = dirty_bitmap_is_active_iterate,
    .load_state = dirty_bitmap_load,
    .save_cleanup = dirty_bitmap_save_cleanup,
    .is_active = dirty_bitmap_is_active,
};

void dirty_bitmap_mig_init(void)
{
    QSIMPLEQ_INIT(&dirty_bitmap_mig_state.dbms_list);

    register_savevm_live("dirty-bitmap", 0, 1,
                         &savevm_dirty_bitmap_handlers,
                         &dirty_bitmap_mig_state);
}
