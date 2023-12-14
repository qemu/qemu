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
 * [ 1 byte: node alias size ] \  flags & DEVICE_NAME
 * [ n bytes: node alias     ] /
 * [ 1 byte: bitmap alias size ] \  flags & BITMAP_NAME
 * [ n bytes: bitmap alias     ] /
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
#include "block/dirty-bitmap.h"
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
#include "qemu/id.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/clone-visitor.h"
#include "trace.h"
#include "options.h"

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
/* 0x04 was "AUTOLOAD" flags on older versions, now it is ignored */
#define DIRTY_BITMAP_MIG_START_FLAG_RESERVED_MASK    0xf8

/* State of one bitmap during save process */
typedef struct SaveBitmapState {
    /* Written during setup phase. */
    BlockDriverState *bs;
    char *node_alias;
    char *bitmap_alias;
    BdrvDirtyBitmap *bitmap;
    uint64_t total_sectors;
    uint64_t sectors_per_chunk;
    QSIMPLEQ_ENTRY(SaveBitmapState) entry;
    uint8_t flags;

    /* For bulk phase. */
    bool bulk_completed;
    uint64_t cur_sector;
} SaveBitmapState;

/* State of the dirty bitmap migration (DBM) during save process */
typedef struct DBMSaveState {
    QSIMPLEQ_HEAD(, SaveBitmapState) dbms_list;

    bool bulk_completed;
    bool no_bitmaps;

    /* for send_bitmap_bits() */
    BlockDriverState *prev_bs;
    BdrvDirtyBitmap *prev_bitmap;
} DBMSaveState;

typedef struct LoadBitmapState {
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;
    bool migrated;
    bool enabled;
} LoadBitmapState;

/* State of the dirty bitmap migration (DBM) during load process */
typedef struct DBMLoadState {
    uint32_t flags;
    char node_alias[256];
    char bitmap_alias[256];
    char bitmap_name[BDRV_BITMAP_MAX_NAME_SIZE + 1];
    BlockDriverState *bs;
    BdrvDirtyBitmap *bitmap;

    bool before_vm_start_handled; /* set in dirty_bitmap_mig_before_vm_start */
    BitmapMigrationBitmapAlias *bmap_inner;

    /*
     * cancelled
     * Incoming migration is cancelled for some reason. That means that we
     * still should read our chunks from migration stream, to not affect other
     * migration objects (like RAM), but just ignore them and do not touch any
     * bitmaps or nodes.
     */
    bool cancelled;

    GSList *bitmaps;
    QemuMutex lock; /* protect bitmaps */
} DBMLoadState;

typedef struct DBMState {
    DBMSaveState save;
    DBMLoadState load;
} DBMState;

static DBMState dbm_state;

/* For hash tables that map node/bitmap names to aliases */
typedef struct AliasMapInnerNode {
    char *string;
    GHashTable *subtree;
} AliasMapInnerNode;

static void free_alias_map_inner_node(void *amin_ptr)
{
    AliasMapInnerNode *amin = amin_ptr;

    g_free(amin->string);
    g_hash_table_unref(amin->subtree);
    g_free(amin);
}

/**
 * Construct an alias map based on the given QMP structure.
 *
 * (Note that we cannot store such maps in the MigrationParameters
 * object, because that struct is defined by the QAPI schema, which
 * makes it basically impossible to have dicts with arbitrary keys.
 * Therefore, we instead have to construct these maps when migration
 * starts.)
 *
 * @bbm is the block_bitmap_mapping from the migration parameters.
 *
 * If @name_to_alias is true, the returned hash table will map node
 * and bitmap names to their respective aliases (for outgoing
 * migration).
 *
 * If @name_to_alias is false, the returned hash table will map node
 * and bitmap aliases to their respective names (for incoming
 * migration).
 *
 * The hash table maps node names/aliases to AliasMapInnerNode
 * objects, whose .string is the respective node alias/name, and whose
 * .subtree table maps bitmap names/aliases to the respective bitmap
 * alias/name.
 */
static GHashTable *construct_alias_map(const BitmapMigrationNodeAliasList *bbm,
                                       bool name_to_alias,
                                       Error **errp)
{
    GHashTable *alias_map;
    size_t max_node_name_len = sizeof_field(BlockDriverState, node_name) - 1;

    alias_map = g_hash_table_new_full(g_str_hash, g_str_equal,
                                      g_free, free_alias_map_inner_node);

    for (; bbm; bbm = bbm->next) {
        const BitmapMigrationNodeAlias *bmna = bbm->value;
        const BitmapMigrationBitmapAliasList *bmbal;
        AliasMapInnerNode *amin;
        GHashTable *bitmaps_map;
        const char *node_map_from, *node_map_to;
        GDestroyNotify gdn;

        if (!id_wellformed(bmna->alias)) {
            error_setg(errp, "The node alias '%s' is not well-formed",
                       bmna->alias);
            goto fail;
        }

        if (strlen(bmna->alias) > UINT8_MAX) {
            error_setg(errp, "The node alias '%s' is longer than %u bytes",
                       bmna->alias, UINT8_MAX);
            goto fail;
        }

        if (strlen(bmna->node_name) > max_node_name_len) {
            error_setg(errp, "The node name '%s' is longer than %zu bytes",
                       bmna->node_name, max_node_name_len);
            goto fail;
        }

        if (name_to_alias) {
            if (g_hash_table_contains(alias_map, bmna->node_name)) {
                error_setg(errp, "The node name '%s' is mapped twice",
                           bmna->node_name);
                goto fail;
            }

            node_map_from = bmna->node_name;
            node_map_to = bmna->alias;
        } else {
            if (g_hash_table_contains(alias_map, bmna->alias)) {
                error_setg(errp, "The node alias '%s' is used twice",
                           bmna->alias);
                goto fail;
            }

            node_map_from = bmna->alias;
            node_map_to = bmna->node_name;
        }

        gdn = (GDestroyNotify) qapi_free_BitmapMigrationBitmapAlias;
        bitmaps_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                            gdn);

        amin = g_new(AliasMapInnerNode, 1);
        *amin = (AliasMapInnerNode){
            .string = g_strdup(node_map_to),
            .subtree = bitmaps_map,
        };

        g_hash_table_insert(alias_map, g_strdup(node_map_from), amin);

        for (bmbal = bmna->bitmaps; bmbal; bmbal = bmbal->next) {
            const BitmapMigrationBitmapAlias *bmba = bmbal->value;
            const char *bmap_map_from;

            if (strlen(bmba->alias) > UINT8_MAX) {
                error_setg(errp,
                           "The bitmap alias '%s' is longer than %u bytes",
                           bmba->alias, UINT8_MAX);
                goto fail;
            }

            if (strlen(bmba->name) > BDRV_BITMAP_MAX_NAME_SIZE) {
                error_setg(errp, "The bitmap name '%s' is longer than %d bytes",
                           bmba->name, BDRV_BITMAP_MAX_NAME_SIZE);
                goto fail;
            }

            if (name_to_alias) {
                bmap_map_from = bmba->name;

                if (g_hash_table_contains(bitmaps_map, bmba->name)) {
                    error_setg(errp, "The bitmap '%s'/'%s' is mapped twice",
                               bmna->node_name, bmba->name);
                    goto fail;
                }
            } else {
                bmap_map_from = bmba->alias;

                if (g_hash_table_contains(bitmaps_map, bmba->alias)) {
                    error_setg(errp, "The bitmap alias '%s'/'%s' is used twice",
                               bmna->alias, bmba->alias);
                    goto fail;
                }
            }

            g_hash_table_insert(bitmaps_map, g_strdup(bmap_map_from),
                                QAPI_CLONE(BitmapMigrationBitmapAlias, bmba));
        }
    }

    return alias_map;

fail:
    g_hash_table_destroy(alias_map);
    return NULL;
}

/**
 * Run construct_alias_map() in both directions to check whether @bbm
 * is valid.
 * (This function is to be used by migration/migration.c to validate
 * the user-specified block-bitmap-mapping migration parameter.)
 *
 * Returns true if and only if the mapping is valid.
 */
bool check_dirty_bitmap_mig_alias_map(const BitmapMigrationNodeAliasList *bbm,
                                      Error **errp)
{
    GHashTable *alias_map;

    alias_map = construct_alias_map(bbm, true, errp);
    if (!alias_map) {
        return false;
    }
    g_hash_table_destroy(alias_map);

    alias_map = construct_alias_map(bbm, false, errp);
    if (!alias_map) {
        return false;
    }
    g_hash_table_destroy(alias_map);

    return true;
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
    /* The code currently does not send flags as more than one byte */
    assert(!(flags & (0xffffff00 | DIRTY_BITMAP_MIG_EXTRA_FLAGS)));

    qemu_put_byte(f, flags);
}

static void send_bitmap_header(QEMUFile *f, DBMSaveState *s,
                               SaveBitmapState *dbms, uint32_t additional_flags)
{
    BlockDriverState *bs = dbms->bs;
    BdrvDirtyBitmap *bitmap = dbms->bitmap;
    uint32_t flags = additional_flags;
    trace_send_bitmap_header_enter();

    if (bs != s->prev_bs) {
        s->prev_bs = bs;
        flags |= DIRTY_BITMAP_MIG_FLAG_DEVICE_NAME;
    }

    if (bitmap != s->prev_bitmap) {
        s->prev_bitmap = bitmap;
        flags |= DIRTY_BITMAP_MIG_FLAG_BITMAP_NAME;
    }

    qemu_put_bitmap_flags(f, flags);

    if (flags & DIRTY_BITMAP_MIG_FLAG_DEVICE_NAME) {
        qemu_put_counted_string(f, dbms->node_alias);
    }

    if (flags & DIRTY_BITMAP_MIG_FLAG_BITMAP_NAME) {
        qemu_put_counted_string(f, dbms->bitmap_alias);
    }
}

static void send_bitmap_start(QEMUFile *f, DBMSaveState *s,
                              SaveBitmapState *dbms)
{
    send_bitmap_header(f, s, dbms, DIRTY_BITMAP_MIG_FLAG_START);
    qemu_put_be32(f, bdrv_dirty_bitmap_granularity(dbms->bitmap));
    qemu_put_byte(f, dbms->flags);
}

static void send_bitmap_complete(QEMUFile *f, DBMSaveState *s,
                                 SaveBitmapState *dbms)
{
    send_bitmap_header(f, s, dbms, DIRTY_BITMAP_MIG_FLAG_COMPLETE);
}

static void send_bitmap_bits(QEMUFile *f, DBMSaveState *s,
                             SaveBitmapState *dbms,
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

    send_bitmap_header(f, s, dbms, flags);

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
static void dirty_bitmap_do_save_cleanup(DBMSaveState *s)
{
    SaveBitmapState *dbms;

    while ((dbms = QSIMPLEQ_FIRST(&s->dbms_list)) != NULL) {
        QSIMPLEQ_REMOVE_HEAD(&s->dbms_list, entry);
        bdrv_dirty_bitmap_set_busy(dbms->bitmap, false);
        bdrv_unref(dbms->bs);
        g_free(dbms->node_alias);
        g_free(dbms->bitmap_alias);
        g_free(dbms);
    }
}

/* Called with iothread lock taken. */
static int add_bitmaps_to_list(DBMSaveState *s, BlockDriverState *bs,
                               const char *bs_name, GHashTable *alias_map)
{
    BdrvDirtyBitmap *bitmap;
    SaveBitmapState *dbms;
    GHashTable *bitmap_aliases;
    const char *node_alias, *bitmap_name, *bitmap_alias;
    Error *local_err = NULL;

    /* When an alias map is given, @bs_name must be @bs's node name */
    assert(!alias_map || !strcmp(bs_name, bdrv_get_node_name(bs)));

    FOR_EACH_DIRTY_BITMAP(bs, bitmap) {
        if (bdrv_dirty_bitmap_name(bitmap)) {
            break;
        }
    }
    if (!bitmap) {
        return 0;
    }

    bitmap_name = bdrv_dirty_bitmap_name(bitmap);

    if (!bs_name || strcmp(bs_name, "") == 0) {
        error_report("Bitmap '%s' in unnamed node can't be migrated",
                     bitmap_name);
        return -1;
    }

    if (alias_map) {
        const AliasMapInnerNode *amin = g_hash_table_lookup(alias_map, bs_name);

        if (!amin) {
            /* Skip bitmaps on nodes with no alias */
            return 0;
        }

        node_alias = amin->string;
        bitmap_aliases = amin->subtree;
    } else {
        node_alias = bs_name;
        bitmap_aliases = NULL;
    }

    if (node_alias[0] == '#') {
        error_report("Bitmap '%s' in a node with auto-generated "
                     "name '%s' can't be migrated",
                     bitmap_name, node_alias);
        return -1;
    }

    FOR_EACH_DIRTY_BITMAP(bs, bitmap) {
        BitmapMigrationBitmapAliasTransform *bitmap_transform = NULL;
        bitmap_name = bdrv_dirty_bitmap_name(bitmap);
        if (!bitmap_name) {
            continue;
        }

        if (bdrv_dirty_bitmap_check(bitmap, BDRV_BITMAP_DEFAULT, &local_err)) {
            error_report_err(local_err);
            return -1;
        }

        if (bitmap_aliases) {
            BitmapMigrationBitmapAlias *bmap_inner;

            bmap_inner = g_hash_table_lookup(bitmap_aliases, bitmap_name);
            if (!bmap_inner) {
                /* Skip bitmaps with no alias */
                continue;
            }

            bitmap_alias = bmap_inner->alias;
            if (bmap_inner->transform) {
                bitmap_transform = bmap_inner->transform;
            }
        } else {
            if (strlen(bitmap_name) > UINT8_MAX) {
                error_report("Cannot migrate bitmap '%s' on node '%s': "
                             "Name is longer than %u bytes",
                             bitmap_name, bs_name, UINT8_MAX);
                return -1;
            }
            bitmap_alias = bitmap_name;
        }

        bdrv_ref(bs);
        bdrv_dirty_bitmap_set_busy(bitmap, true);

        dbms = g_new0(SaveBitmapState, 1);
        dbms->bs = bs;
        dbms->node_alias = g_strdup(node_alias);
        dbms->bitmap_alias = g_strdup(bitmap_alias);
        dbms->bitmap = bitmap;
        dbms->total_sectors = bdrv_nb_sectors(bs);
        dbms->sectors_per_chunk = CHUNK_SIZE * 8LLU *
            (bdrv_dirty_bitmap_granularity(bitmap) >> BDRV_SECTOR_BITS);
        assert(dbms->sectors_per_chunk != 0);
        if (bdrv_dirty_bitmap_enabled(bitmap)) {
            dbms->flags |= DIRTY_BITMAP_MIG_START_FLAG_ENABLED;
        }
        if (bitmap_transform &&
            bitmap_transform->has_persistent) {
            if (bitmap_transform->persistent) {
                dbms->flags |= DIRTY_BITMAP_MIG_START_FLAG_PERSISTENT;
            }
        } else {
            if (bdrv_dirty_bitmap_get_persistence(bitmap)) {
                dbms->flags |= DIRTY_BITMAP_MIG_START_FLAG_PERSISTENT;
            }
        }

        QSIMPLEQ_INSERT_TAIL(&s->dbms_list, dbms, entry);
    }

    return 0;
}

/* Called with iothread lock taken. */
static int init_dirty_bitmap_migration(DBMSaveState *s)
{
    BlockDriverState *bs;
    SaveBitmapState *dbms;
    GHashTable *handled_by_blk = g_hash_table_new(NULL, NULL);
    BlockBackend *blk;
    GHashTable *alias_map = NULL;

    /* Runs in the migration thread, but holds the iothread lock */
    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    if (migrate_has_block_bitmap_mapping()) {
        alias_map = construct_alias_map(migrate_block_bitmap_mapping(), true,
                                        &error_abort);
    }

    s->bulk_completed = false;
    s->prev_bs = NULL;
    s->prev_bitmap = NULL;
    s->no_bitmaps = false;

    if (!alias_map) {
        /*
         * Use blockdevice name for direct (or filtered) children of named block
         * backends.
         */
        for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
            const char *name = blk_name(blk);

            if (!name || strcmp(name, "") == 0) {
                continue;
            }

            bs = blk_bs(blk);

            /* Skip filters without bitmaps */
            while (bs && bs->drv && bs->drv->is_filter &&
                   !bdrv_has_named_bitmaps(bs))
            {
                bs = bdrv_filter_bs(bs);
            }

            if (bs && bs->drv && !bs->drv->is_filter) {
                if (add_bitmaps_to_list(s, bs, name, NULL)) {
                    goto fail;
                }
                g_hash_table_add(handled_by_blk, bs);
            }
        }
    }

    for (bs = bdrv_next_all_states(NULL); bs; bs = bdrv_next_all_states(bs)) {
        if (g_hash_table_contains(handled_by_blk, bs)) {
            continue;
        }

        if (add_bitmaps_to_list(s, bs, bdrv_get_node_name(bs), alias_map)) {
            goto fail;
        }
    }

    /* unset migration flags here, to not roll back it */
    QSIMPLEQ_FOREACH(dbms, &s->dbms_list, entry) {
        bdrv_dirty_bitmap_skip_store(dbms->bitmap, true);
    }

    if (QSIMPLEQ_EMPTY(&s->dbms_list)) {
        s->no_bitmaps = true;
    }

    g_hash_table_destroy(handled_by_blk);
    if (alias_map) {
        g_hash_table_destroy(alias_map);
    }

    return 0;

fail:
    g_hash_table_destroy(handled_by_blk);
    if (alias_map) {
        g_hash_table_destroy(alias_map);
    }
    dirty_bitmap_do_save_cleanup(s);

    return -1;
}

/* Called with no lock taken.  */
static void bulk_phase_send_chunk(QEMUFile *f, DBMSaveState *s,
                                  SaveBitmapState *dbms)
{
    uint32_t nr_sectors = MIN(dbms->total_sectors - dbms->cur_sector,
                             dbms->sectors_per_chunk);

    send_bitmap_bits(f, s, dbms, dbms->cur_sector, nr_sectors);

    dbms->cur_sector += nr_sectors;
    if (dbms->cur_sector >= dbms->total_sectors) {
        dbms->bulk_completed = true;
    }
}

/* Called with no lock taken.  */
static void bulk_phase(QEMUFile *f, DBMSaveState *s, bool limit)
{
    SaveBitmapState *dbms;

    QSIMPLEQ_FOREACH(dbms, &s->dbms_list, entry) {
        while (!dbms->bulk_completed) {
            bulk_phase_send_chunk(f, s, dbms);
            if (limit && migration_rate_exceeded(f)) {
                return;
            }
        }
    }

    s->bulk_completed = true;
}

/* for SaveVMHandlers */
static void dirty_bitmap_save_cleanup(void *opaque)
{
    DBMSaveState *s = &((DBMState *)opaque)->save;

    dirty_bitmap_do_save_cleanup(s);
}

static int dirty_bitmap_save_iterate(QEMUFile *f, void *opaque)
{
    DBMSaveState *s = &((DBMState *)opaque)->save;

    trace_dirty_bitmap_save_iterate(migration_in_postcopy());

    if (migration_in_postcopy() && !s->bulk_completed) {
        bulk_phase(f, s, true);
    }

    qemu_put_bitmap_flags(f, DIRTY_BITMAP_MIG_FLAG_EOS);

    return s->bulk_completed;
}

/* Called with iothread lock taken.  */

static int dirty_bitmap_save_complete(QEMUFile *f, void *opaque)
{
    DBMSaveState *s = &((DBMState *)opaque)->save;
    SaveBitmapState *dbms;
    trace_dirty_bitmap_save_complete_enter();

    if (!s->bulk_completed) {
        bulk_phase(f, s, false);
    }

    QSIMPLEQ_FOREACH(dbms, &s->dbms_list, entry) {
        send_bitmap_complete(f, s, dbms);
    }

    qemu_put_bitmap_flags(f, DIRTY_BITMAP_MIG_FLAG_EOS);

    trace_dirty_bitmap_save_complete_finish();

    dirty_bitmap_save_cleanup(opaque);
    return 0;
}

static void dirty_bitmap_state_pending(void *opaque,
                                       uint64_t *must_precopy,
                                       uint64_t *can_postcopy)
{
    DBMSaveState *s = &((DBMState *)opaque)->save;
    SaveBitmapState *dbms;
    uint64_t pending = 0;

    qemu_mutex_lock_iothread();

    QSIMPLEQ_FOREACH(dbms, &s->dbms_list, entry) {
        uint64_t gran = bdrv_dirty_bitmap_granularity(dbms->bitmap);
        uint64_t sectors = dbms->bulk_completed ? 0 :
                           dbms->total_sectors - dbms->cur_sector;

        pending += DIV_ROUND_UP(sectors * BDRV_SECTOR_SIZE, gran);
    }

    qemu_mutex_unlock_iothread();

    trace_dirty_bitmap_state_pending(pending);

    *can_postcopy += pending;
}

/* First occurrence of this bitmap. It should be created if doesn't exist */
static int dirty_bitmap_load_start(QEMUFile *f, DBMLoadState *s)
{
    Error *local_err = NULL;
    uint32_t granularity = qemu_get_be32(f);
    uint8_t flags = qemu_get_byte(f);
    LoadBitmapState *b;
    bool persistent;

    if (s->cancelled) {
        return 0;
    }

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

    if (s->bmap_inner &&
        s->bmap_inner->transform &&
        s->bmap_inner->transform->has_persistent) {
        persistent = s->bmap_inner->transform->persistent;
    } else {
        persistent = flags & DIRTY_BITMAP_MIG_START_FLAG_PERSISTENT;
    }

    if (persistent) {
        bdrv_dirty_bitmap_set_persistence(s->bitmap, true);
    }

    bdrv_disable_dirty_bitmap(s->bitmap);
    if (flags & DIRTY_BITMAP_MIG_START_FLAG_ENABLED) {
        bdrv_dirty_bitmap_create_successor(s->bitmap, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -EINVAL;
        }
    } else {
        bdrv_dirty_bitmap_set_busy(s->bitmap, true);
    }

    b = g_new(LoadBitmapState, 1);
    b->bs = s->bs;
    b->bitmap = s->bitmap;
    b->migrated = false;
    b->enabled = flags & DIRTY_BITMAP_MIG_START_FLAG_ENABLED;

    s->bitmaps = g_slist_prepend(s->bitmaps, b);

    return 0;
}

/*
 * before_vm_start_handle_item
 *
 * g_slist_foreach helper
 *
 * item is LoadBitmapState*
 * opaque is DBMLoadState*
 */
static void before_vm_start_handle_item(void *item, void *opaque)
{
    DBMLoadState *s = opaque;
    LoadBitmapState *b = item;

    if (b->enabled) {
        if (b->migrated) {
            bdrv_enable_dirty_bitmap(b->bitmap);
        } else {
            bdrv_dirty_bitmap_enable_successor(b->bitmap);
        }
    }

    if (b->migrated) {
        s->bitmaps = g_slist_remove(s->bitmaps, b);
        g_free(b);
    }
}

void dirty_bitmap_mig_before_vm_start(void)
{
    DBMLoadState *s = &dbm_state.load;
    qemu_mutex_lock(&s->lock);

    assert(!s->before_vm_start_handled);
    g_slist_foreach(s->bitmaps, before_vm_start_handle_item, s);
    s->before_vm_start_handled = true;

    qemu_mutex_unlock(&s->lock);
}

static void cancel_incoming_locked(DBMLoadState *s)
{
    GSList *item;

    if (s->cancelled) {
        return;
    }

    s->cancelled = true;
    s->bs = NULL;
    s->bitmap = NULL;

    /* Drop all unfinished bitmaps */
    for (item = s->bitmaps; item; item = g_slist_next(item)) {
        LoadBitmapState *b = item->data;

        /*
         * Bitmap must be unfinished, as finished bitmaps should already be
         * removed from the list.
         */
        assert(!s->before_vm_start_handled || !b->migrated);
        if (bdrv_dirty_bitmap_has_successor(b->bitmap)) {
            bdrv_reclaim_dirty_bitmap(b->bitmap, &error_abort);
        } else {
            bdrv_dirty_bitmap_set_busy(b->bitmap, false);
        }
        bdrv_release_dirty_bitmap(b->bitmap);
    }

    g_slist_free_full(s->bitmaps, g_free);
    s->bitmaps = NULL;
}

void dirty_bitmap_mig_cancel_outgoing(void)
{
    dirty_bitmap_do_save_cleanup(&dbm_state.save);
}

void dirty_bitmap_mig_cancel_incoming(void)
{
    DBMLoadState *s = &dbm_state.load;

    qemu_mutex_lock(&s->lock);

    cancel_incoming_locked(s);

    qemu_mutex_unlock(&s->lock);
}

static void dirty_bitmap_load_complete(QEMUFile *f, DBMLoadState *s)
{
    GSList *item;
    trace_dirty_bitmap_load_complete();

    if (s->cancelled) {
        return;
    }

    bdrv_dirty_bitmap_deserialize_finish(s->bitmap);

    if (bdrv_dirty_bitmap_has_successor(s->bitmap)) {
        bdrv_reclaim_dirty_bitmap(s->bitmap, &error_abort);
    } else {
        bdrv_dirty_bitmap_set_busy(s->bitmap, false);
    }

    for (item = s->bitmaps; item; item = g_slist_next(item)) {
        LoadBitmapState *b = item->data;

        if (b->bitmap == s->bitmap) {
            b->migrated = true;
            if (s->before_vm_start_handled) {
                s->bitmaps = g_slist_remove(s->bitmaps, b);
                g_free(b);
            }
            break;
        }
    }
}

static int dirty_bitmap_load_bits(QEMUFile *f, DBMLoadState *s)
{
    uint64_t first_byte = qemu_get_be64(f) << BDRV_SECTOR_BITS;
    uint64_t nr_bytes = (uint64_t)qemu_get_be32(f) << BDRV_SECTOR_BITS;
    trace_dirty_bitmap_load_bits_enter(first_byte >> BDRV_SECTOR_BITS,
                                       nr_bytes >> BDRV_SECTOR_BITS);

    if (s->flags & DIRTY_BITMAP_MIG_FLAG_ZEROES) {
        trace_dirty_bitmap_load_bits_zeroes();
        if (!s->cancelled) {
            bdrv_dirty_bitmap_deserialize_zeroes(s->bitmap, first_byte,
                                                 nr_bytes, false);
        }
    } else {
        size_t ret;
        g_autofree uint8_t *buf = NULL;
        uint64_t buf_size = qemu_get_be64(f);
        uint64_t needed_size;

        /*
         * The actual check for buf_size is done a bit later. We can't do it in
         * cancelled mode as we don't have the bitmap to check the constraints
         * (so, we allocate a buffer and read prior to the check). On the other
         * hand, we shouldn't blindly g_malloc the number from the stream.
         * Actually one chunk should not be larger than CHUNK_SIZE. Let's allow
         * a bit larger (which means that bitmap migration will fail anyway and
         * the whole migration will most probably fail soon due to broken
         * stream).
         */
        if (buf_size > 10 * CHUNK_SIZE) {
            error_report("Bitmap migration stream buffer allocation request "
                         "is too large");
            return -EIO;
        }

        buf = g_malloc(buf_size);
        ret = qemu_get_buffer(f, buf, buf_size);
        if (ret != buf_size) {
            error_report("Failed to read bitmap bits");
            return -EIO;
        }

        if (s->cancelled) {
            return 0;
        }

        needed_size = bdrv_dirty_bitmap_serialization_size(s->bitmap,
                                                           first_byte,
                                                           nr_bytes);

        if (needed_size > buf_size ||
            buf_size > QEMU_ALIGN_UP(needed_size, 4 * sizeof(long))
             /* Here used same alignment as in send_bitmap_bits */
        ) {
            error_report("Migrated bitmap granularity doesn't "
                         "match the destination bitmap '%s' granularity",
                         bdrv_dirty_bitmap_name(s->bitmap));
            cancel_incoming_locked(s);
            return 0;
        }

        bdrv_dirty_bitmap_deserialize_part(s->bitmap, buf, first_byte, nr_bytes,
                                           false);
    }

    return 0;
}

static int dirty_bitmap_load_header(QEMUFile *f, DBMLoadState *s,
                                    GHashTable *alias_map)
{
    GHashTable *bitmap_alias_map = NULL;
    Error *local_err = NULL;
    bool nothing;
    s->flags = qemu_get_bitmap_flags(f);
    trace_dirty_bitmap_load_header(s->flags);

    nothing = s->flags == (s->flags & DIRTY_BITMAP_MIG_FLAG_EOS);

    if (s->flags & DIRTY_BITMAP_MIG_FLAG_DEVICE_NAME) {
        if (!qemu_get_counted_string(f, s->node_alias)) {
            error_report("Unable to read node alias string");
            return -EINVAL;
        }

        if (!s->cancelled) {
            if (alias_map) {
                const AliasMapInnerNode *amin;

                amin = g_hash_table_lookup(alias_map, s->node_alias);
                if (!amin) {
                    error_setg(&local_err, "Error: Unknown node alias '%s'",
                               s->node_alias);
                    s->bs = NULL;
                } else {
                    bitmap_alias_map = amin->subtree;
                    s->bs = bdrv_lookup_bs(NULL, amin->string, &local_err);
                }
            } else {
                s->bs = bdrv_lookup_bs(s->node_alias, s->node_alias,
                                       &local_err);
            }
            if (!s->bs) {
                error_report_err(local_err);
                cancel_incoming_locked(s);
            }
        }
    } else if (s->bs) {
        if (alias_map) {
            const AliasMapInnerNode *amin;

            /* Must be present in the map, or s->bs would not be set */
            amin = g_hash_table_lookup(alias_map, s->node_alias);
            assert(amin != NULL);

            bitmap_alias_map = amin->subtree;
        }
    } else if (!nothing && !s->cancelled) {
        error_report("Error: block device name is not set");
        cancel_incoming_locked(s);
    }

    assert(nothing || s->cancelled || !!alias_map == !!bitmap_alias_map);

    if (s->flags & DIRTY_BITMAP_MIG_FLAG_BITMAP_NAME) {
        const char *bitmap_name;

        if (!qemu_get_counted_string(f, s->bitmap_alias)) {
            error_report("Unable to read bitmap alias string");
            return -EINVAL;
        }

        bitmap_name = s->bitmap_alias;
        if (!s->cancelled && bitmap_alias_map) {
            BitmapMigrationBitmapAlias *bmap_inner;

            bmap_inner = g_hash_table_lookup(bitmap_alias_map, s->bitmap_alias);
            if (!bmap_inner) {
                error_report("Error: Unknown bitmap alias '%s' on node "
                             "'%s' (alias '%s')", s->bitmap_alias,
                             s->bs->node_name, s->node_alias);
                cancel_incoming_locked(s);
            } else {
                bitmap_name = bmap_inner->name;
            }

            s->bmap_inner = bmap_inner;
        }

        if (!s->cancelled) {
            g_strlcpy(s->bitmap_name, bitmap_name, sizeof(s->bitmap_name));
            s->bitmap = bdrv_find_dirty_bitmap(s->bs, s->bitmap_name);

            /*
             * bitmap may be NULL here, it wouldn't be an error if it is the
             * first occurrence of the bitmap
             */
            if (!s->bitmap && !(s->flags & DIRTY_BITMAP_MIG_FLAG_START)) {
                error_report("Error: unknown dirty bitmap "
                             "'%s' for block device '%s'",
                             s->bitmap_name, s->bs->node_name);
                cancel_incoming_locked(s);
            }
        }
    } else if (!s->bitmap && !nothing && !s->cancelled) {
        error_report("Error: block device name is not set");
        cancel_incoming_locked(s);
    }

    return 0;
}

/*
 * dirty_bitmap_load
 *
 * Load sequence of dirty bitmap chunks. Return error only on fatal io stream
 * violations. On other errors just cancel bitmaps incoming migration and return
 * 0.
 *
 * Note, than when incoming bitmap migration is canceled, we still must read all
 * our chunks (and just ignore them), to not affect other migration objects.
 */
static int dirty_bitmap_load(QEMUFile *f, void *opaque, int version_id)
{
    GHashTable *alias_map = NULL;
    DBMLoadState *s = &((DBMState *)opaque)->load;
    int ret = 0;

    trace_dirty_bitmap_load_enter();

    if (version_id != 1) {
        QEMU_LOCK_GUARD(&s->lock);
        cancel_incoming_locked(s);
        return -EINVAL;
    }

    if (migrate_has_block_bitmap_mapping()) {
        alias_map = construct_alias_map(migrate_block_bitmap_mapping(), false,
                                        &error_abort);
    }

    do {
        QEMU_LOCK_GUARD(&s->lock);

        ret = dirty_bitmap_load_header(f, s, alias_map);
        if (ret < 0) {
            cancel_incoming_locked(s);
            goto fail;
        }

        if (s->flags & DIRTY_BITMAP_MIG_FLAG_START) {
            ret = dirty_bitmap_load_start(f, s);
        } else if (s->flags & DIRTY_BITMAP_MIG_FLAG_COMPLETE) {
            dirty_bitmap_load_complete(f, s);
        } else if (s->flags & DIRTY_BITMAP_MIG_FLAG_BITS) {
            ret = dirty_bitmap_load_bits(f, s);
        }

        if (!ret) {
            ret = qemu_file_get_error(f);
        }

        if (ret) {
            cancel_incoming_locked(s);
            goto fail;
        }
    } while (!(s->flags & DIRTY_BITMAP_MIG_FLAG_EOS));

    trace_dirty_bitmap_load_success();
    ret = 0;
fail:
    if (alias_map) {
        g_hash_table_destroy(alias_map);
    }
    return ret;
}

static int dirty_bitmap_save_setup(QEMUFile *f, void *opaque)
{
    DBMSaveState *s = &((DBMState *)opaque)->save;
    SaveBitmapState *dbms = NULL;

    if (init_dirty_bitmap_migration(s) < 0) {
        return -1;
    }

    QSIMPLEQ_FOREACH(dbms, &s->dbms_list, entry) {
        send_bitmap_start(f, s, dbms);
    }
    qemu_put_bitmap_flags(f, DIRTY_BITMAP_MIG_FLAG_EOS);
    return 0;
}

static bool dirty_bitmap_is_active(void *opaque)
{
    DBMSaveState *s = &((DBMState *)opaque)->save;

    return migrate_dirty_bitmaps() && !s->no_bitmaps;
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
    .state_pending_exact = dirty_bitmap_state_pending,
    .state_pending_estimate = dirty_bitmap_state_pending,
    .save_live_iterate = dirty_bitmap_save_iterate,
    .is_active_iterate = dirty_bitmap_is_active_iterate,
    .load_state = dirty_bitmap_load,
    .save_cleanup = dirty_bitmap_save_cleanup,
    .is_active = dirty_bitmap_is_active,
};

void dirty_bitmap_mig_init(void)
{
    QSIMPLEQ_INIT(&dbm_state.save.dbms_list);
    qemu_mutex_init(&dbm_state.load.lock);

    register_savevm_live("dirty-bitmap", 0, 1,
                         &savevm_dirty_bitmap_handlers,
                         &dbm_state);
}
