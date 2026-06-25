/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RAM Discard Manager
 *
 * Copyright Red Hat, Inc. 2026
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/queue.h"
#include "system/memory.h"

static uint64_t ram_discard_source_get_min_granularity(const RamDiscardSource *rds,
                                                       const MemoryRegion *mr)
{
    RamDiscardSourceClass *rdsc = RAM_DISCARD_SOURCE_GET_CLASS(rds);

    g_assert(rdsc->get_min_granularity);
    return rdsc->get_min_granularity(rds, mr);
}

static bool ram_discard_source_is_populated(const RamDiscardSource *rds,
                                            const MemoryRegionSection *section)
{
    RamDiscardSourceClass *rdsc = RAM_DISCARD_SOURCE_GET_CLASS(rds);

    g_assert(rdsc->is_populated);
    return rdsc->is_populated(rds, section);
}

/*
 * Iterate a single source's populated or discarded regions and call
 * replay_fn for each contiguous run.
 */
static int replay_source_by_state(const RamDiscardSource *source,
                                  const MemoryRegion *mr,
                                  const MemoryRegionSection *section,
                                  bool replay_populated,
                                  ReplayRamDiscardState replay_fn,
                                  void *opaque)
{
    uint64_t granularity, offset, size, end, pos, run_start = 0;
    bool in_run = false;
    int ret = 0;

    granularity = ram_discard_source_get_min_granularity(source, mr);
    offset = section->offset_within_region;
    size = int128_get64(section->size);
    end = offset + size;

    /* Align iteration to granularity boundaries */
    pos = QEMU_ALIGN_DOWN(offset, granularity);

    for (; pos < end; pos += granularity) {
        MemoryRegionSection chunk = {
            .mr = section->mr,
            .offset_within_region = pos,
            .size = int128_make64(granularity),
        };
        bool populated = ram_discard_source_is_populated(source, &chunk);

        if (populated == replay_populated) {
            if (!in_run) {
                run_start = pos;
                in_run = true;
            }
        } else if (in_run) {
            MemoryRegionSection tmp = *section;

            if (memory_region_section_intersect_range(&tmp, run_start,
                                                      pos - run_start)) {
                ret = replay_fn(&tmp, opaque);
                if (ret) {
                    return ret;
                }
            }
            in_run = false;
        }
    }

    if (in_run) {
        MemoryRegionSection tmp = *section;

        if (memory_region_section_intersect_range(&tmp, run_start,
                                                  pos - run_start)) {
            ret = replay_fn(&tmp, opaque);
        }
    }

    return ret;
}

RamDiscardManager *ram_discard_manager_new(MemoryRegion *mr)
{
    RamDiscardManager *rdm;

    rdm = RAM_DISCARD_MANAGER(object_new(TYPE_RAM_DISCARD_MANAGER));
    rdm->mr = mr;
    return rdm;
}

static void ram_discard_manager_update_granularity(RamDiscardManager *rdm)
{
    RamDiscardSourceEntry *entry;
    uint64_t granularity = 0;

    QLIST_FOREACH(entry, &rdm->source_list, next) {
        uint64_t src_granularity;

        src_granularity =
            ram_discard_source_get_min_granularity(entry->rds, rdm->mr);
        g_assert(src_granularity != 0);
        if (granularity == 0) {
            granularity = src_granularity;
        } else {
            granularity = MIN(granularity, src_granularity);
        }
    }
    rdm->min_granularity = granularity;
}

static RamDiscardSourceEntry *
ram_discard_manager_find_source(RamDiscardManager *rdm, RamDiscardSource *rds)
{
    RamDiscardSourceEntry *entry;

    QLIST_FOREACH(entry, &rdm->source_list, next) {
        if (entry->rds == rds) {
            return entry;
        }
    }
    return NULL;
}

static int rdl_populate_cb(const MemoryRegionSection *section, void *opaque)
{
    RamDiscardListener *rdl = opaque;
    MemoryRegionSection tmp = *rdl->section;

    g_assert(section->mr == rdl->section->mr);

    if (!memory_region_section_intersect_range(&tmp,
                                               section->offset_within_region,
                                               int128_get64(section->size))) {
        return 0;
    }

    return rdl->notify_populate(rdl, &tmp);
}

static int rdl_discard_cb(const MemoryRegionSection *section, void *opaque)
{
    RamDiscardListener *rdl = opaque;
    MemoryRegionSection tmp = *rdl->section;

    g_assert(section->mr == rdl->section->mr);

    if (!memory_region_section_intersect_range(&tmp,
                                               section->offset_within_region,
                                               int128_get64(section->size))) {
        return 0;
    }

    rdl->notify_discard(rdl, &tmp);
    return 0;
}

static bool rdm_is_all_populated_skip(const RamDiscardManager *rdm,
                                      const MemoryRegionSection *section,
                                      const RamDiscardSource *skip_source)
{
    RamDiscardSourceEntry *entry;

    QLIST_FOREACH(entry, &rdm->source_list, next) {
        if (skip_source && entry->rds == skip_source) {
            continue;
        }
        if (!ram_discard_source_is_populated(entry->rds, section)) {
            return false;
        }
    }
    return true;
}

typedef struct SourceNotifyCtx {
    RamDiscardManager *rdm;
    RamDiscardListener *rdl;
    RamDiscardSource *source; /* added or removed */
} SourceNotifyCtx;

/*
 * Unified helper to replay regions based on populated state.
 * If replay_populated is true: replay regions where ALL sources are populated.
 * If replay_populated is false: replay regions where ANY source is discarded.
 */
static int replay_by_populated_state(const RamDiscardManager *rdm,
                                     const MemoryRegionSection *section,
                                     const RamDiscardSource *skip_source,
                                     bool replay_populated,
                                     ReplayRamDiscardState replay_fn,
                                     void *user_opaque)
{
    uint64_t granularity = rdm->min_granularity;
    uint64_t offset, end_offset;
    uint64_t run_start = 0;
    bool in_run = false;
    int ret = 0;

    if (QLIST_EMPTY(&rdm->source_list)) {
        if (replay_populated) {
            return replay_fn(section, user_opaque);
        }
        return 0;
    }

    g_assert(granularity != 0);

    offset = section->offset_within_region;
    end_offset = offset + int128_get64(section->size);

    while (offset < end_offset) {
        MemoryRegionSection subsection = {
            .mr = section->mr,
            .offset_within_region = offset,
            .size = int128_make64(MIN(granularity, end_offset - offset)),
        };
        bool all_populated;
        bool included;

        all_populated = rdm_is_all_populated_skip(rdm, &subsection,
                                                     skip_source);
        included = replay_populated ? all_populated : !all_populated;

        if (included) {
            if (!in_run) {
                run_start = offset;
                in_run = true;
            }
        } else {
            if (in_run) {
                MemoryRegionSection run_section = {
                    .mr = section->mr,
                    .offset_within_region = run_start,
                    .size = int128_make64(offset - run_start),
                };
                ret = replay_fn(&run_section, user_opaque);
                if (ret) {
                    return ret;
                }
                in_run = false;
            }
        }
        if (granularity > end_offset - offset) {
            break;
        }
        offset += granularity;
    }

    if (in_run) {
        MemoryRegionSection run_section = {
            .mr = section->mr,
            .offset_within_region = run_start,
            .size = int128_make64(end_offset - run_start),
        };
        ret = replay_fn(&run_section, user_opaque);
    }

    return ret;
}

static int add_source_check_discard_cb(const MemoryRegionSection *section,
                                       void *opaque)
{
    SourceNotifyCtx *ctx = opaque;

    return replay_by_populated_state(ctx->rdm, section, ctx->source, true,
                                     rdl_discard_cb, ctx->rdl);
}

static int del_source_check_populate_cb(const MemoryRegionSection *section,
                                        void *opaque)
{
    SourceNotifyCtx *ctx = opaque;

    return replay_by_populated_state(ctx->rdm, section, ctx->source, true,
                                     rdl_populate_cb, ctx->rdl);
}

int ram_discard_manager_add_source(RamDiscardManager *rdm,
                                   RamDiscardSource *source)
{
    RamDiscardSourceEntry *entry;
    RamDiscardListener *rdl, *rdl2;
    int ret = 0;

    if (ram_discard_manager_find_source(rdm, source)) {
        return -EBUSY;
    }

    /*
     * If there are existing listeners, notify them about regions that
     * become discarded due to adding this source. Only notify for regions
     * that were previously populated (all other sources agreed).
     */
    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        SourceNotifyCtx ctx = {
            .rdm = rdm,
            .rdl = rdl,
            /* no need to set source */
        };
        ret = replay_source_by_state(source, rdm->mr, rdl->section,
                                     false,
                                     add_source_check_discard_cb, &ctx);
        if (ret) {
            break;
        }
    }
    if (ret) {
        QLIST_FOREACH(rdl2, &rdm->rdl_list, next) {
            SourceNotifyCtx ctx = {
                .rdm = rdm,
                .rdl = rdl2,
            };
            replay_source_by_state(source, rdm->mr, rdl2->section,
                                   false,
                                   del_source_check_populate_cb,
                                   &ctx);
            if (rdl == rdl2) {
                break;
            }
        }

        return ret;
    }

    entry = g_new0(RamDiscardSourceEntry, 1);
    entry->rds = source;
    QLIST_INSERT_HEAD(&rdm->source_list, entry, next);

    ram_discard_manager_update_granularity(rdm);

    return ret;
}

int ram_discard_manager_del_source(RamDiscardManager *rdm,
                                   RamDiscardSource *source)
{
    RamDiscardSourceEntry *entry;
    RamDiscardListener *rdl, *rdl2;
    int ret = 0;

    entry = ram_discard_manager_find_source(rdm, source);
    if (!entry) {
        return -ENOENT;
    }

    /*
     * If there are existing listeners, check if any regions become
     * populated due to removing this source.
     */
    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        SourceNotifyCtx ctx = {
            .rdm = rdm,
            .rdl = rdl,
            .source = source,
        };
        /*
         * From the previously discarded regions, check if any
         * regions become populated.
         */
        ret = replay_source_by_state(source, rdm->mr, rdl->section,
                                     false,
                                     del_source_check_populate_cb,
                                     &ctx);
        if (ret) {
            break;
        }
    }
    if (ret) {
        QLIST_FOREACH(rdl2, &rdm->rdl_list, next) {
            SourceNotifyCtx ctx = {
                .rdm = rdm,
                .rdl = rdl2,
                .source = source,
            };
            replay_source_by_state(source, rdm->mr, rdl2->section,
                                   false,
                                   add_source_check_discard_cb,
                                   &ctx);
            if (rdl == rdl2) {
                break;
            }
        }

        return ret;
    }

    QLIST_REMOVE(entry, next);
    g_free(entry);
    ram_discard_manager_update_granularity(rdm);
    return ret;
}

uint64_t ram_discard_manager_get_min_granularity(const RamDiscardManager *rdm,
                                                 const MemoryRegion *mr)
{
    g_assert(mr == rdm->mr);
    return rdm->min_granularity;
}

/*
 * Aggregated query: returns true only if ALL sources report populated (AND).
 */
bool ram_discard_manager_is_populated(const RamDiscardManager *rdm,
                                      const MemoryRegionSection *section)
{
    RamDiscardSourceEntry *entry;

    QLIST_FOREACH(entry, &rdm->source_list, next) {
        if (!ram_discard_source_is_populated(entry->rds, section)) {
            return false;
        }
    }
    return true;
}

int ram_discard_manager_replay_populated(const RamDiscardManager *rdm,
                                         const MemoryRegionSection *section,
                                         ReplayRamDiscardState replay_fn,
                                         void *opaque)
{
    return replay_by_populated_state(rdm, section, NULL, true,
                                     replay_fn, opaque);
}

int ram_discard_manager_replay_discarded(const RamDiscardManager *rdm,
                                         const MemoryRegionSection *section,
                                         ReplayRamDiscardState replay_fn,
                                         void *opaque)
{
    return replay_by_populated_state(rdm, section, NULL, false,
                                     replay_fn, opaque);
}

static void ram_discard_manager_initfn(Object *obj)
{
    RamDiscardManager *rdm = RAM_DISCARD_MANAGER(obj);

    QLIST_INIT(&rdm->source_list);
    QLIST_INIT(&rdm->rdl_list);
    rdm->min_granularity = 0;
}

static void ram_discard_manager_finalize(Object *obj)
{
    RamDiscardManager *rdm = RAM_DISCARD_MANAGER(obj);

    g_assert(QLIST_EMPTY(&rdm->rdl_list));
    g_assert(QLIST_EMPTY(&rdm->source_list));
}

int ram_discard_manager_notify_populate(RamDiscardManager *rdm,
                                        RamDiscardSource *source,
                                        uint64_t offset, uint64_t size)
{
    RamDiscardListener *rdl, *rdl2;
    MemoryRegionSection section = {
        .mr = rdm->mr,
        .offset_within_region = offset,
        .size = int128_make64(size),
    };
    int ret = 0;

    g_assert(ram_discard_manager_find_source(rdm, source));

    /*
     * Only notify about regions that are populated in ALL sources.
     * Skip the calling source: it has implicitly declared itself populated
     * for this range but may not have updated its bitmap yet.
     */
    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        ret = replay_by_populated_state(rdm, &section, source, true,
                                        rdl_populate_cb, rdl);
        if (ret) {
            break;
        }
    }

    if (ret) {
        /*
         * Rollback: notify discard for listeners we already notified,
         * including the failing listener which may have been partially
         * notified. Listeners must handle discard notifications for
         * regions they didn't receive populate notifications for.
         */
        QLIST_FOREACH(rdl2, &rdm->rdl_list, next) {
            replay_by_populated_state(rdm, &section, source, true,
                                      rdl_discard_cb, rdl2);
            if (rdl2 == rdl) {
                break;
            }
        }
    }
    return ret;
}

void ram_discard_manager_notify_discard(RamDiscardManager *rdm,
                                        RamDiscardSource *source,
                                        uint64_t offset, uint64_t size)
{
    RamDiscardListener *rdl;
    MemoryRegionSection section = {
        .mr = rdm->mr,
        .offset_within_region = offset,
        .size = int128_make64(size),
    };

    g_assert(ram_discard_manager_find_source(rdm, source));

    /*
     * Only notify about ranges that were aggregately populated before this
     * source's discard. Since the source has already updated its state,
     * we use replay_by_populated_state with this source skipped - it will
     * replay only the ranges where all OTHER sources are populated.
     */
    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        replay_by_populated_state(rdm, &section, source, true,
                                  rdl_discard_cb, rdl);
    }
}

void ram_discard_manager_notify_discard_all(RamDiscardManager *rdm,
                                            RamDiscardSource *source)
{
    RamDiscardListener *rdl;

    g_assert(ram_discard_manager_find_source(rdm, source));

    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        rdl->notify_discard(rdl, rdl->section);
    }
}

void ram_discard_manager_register_listener(RamDiscardManager *rdm,
                                           RamDiscardListener *rdl,
                                           MemoryRegionSection *section)
{
    int ret;

    g_assert(section->mr == rdm->mr);

    object_ref(rdm);
    rdl->section = memory_region_section_new_copy(section);
    QLIST_INSERT_HEAD(&rdm->rdl_list, rdl, next);

    ret = ram_discard_manager_replay_populated(rdm, rdl->section,
                                               rdl_populate_cb, rdl);
    if (ret) {
        error_report("%s: Replaying populated ranges failed: %s", __func__,
                     strerror(-ret));
    }
}

void ram_discard_manager_unregister_listener(RamDiscardManager *rdm,
                                             RamDiscardListener *rdl)
{
    g_assert(rdl->section);
    g_assert(rdl->section->mr == rdm->mr);

    rdl->notify_discard(rdl, rdl->section);
    memory_region_section_free_copy(rdl->section);
    rdl->section = NULL;
    QLIST_REMOVE(rdl, next);
    object_unref(rdm);
}

int ram_discard_manager_replay_populated_to_listeners(RamDiscardManager *rdm)
{
    RamDiscardListener *rdl;
    int ret = 0;

    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        ret = ram_discard_manager_replay_populated(rdm, rdl->section,
                                                   rdl_populate_cb, rdl);
        if (ret) {
            break;
        }
    }
    return ret;
}

static const TypeInfo ram_discard_manager_info = {
    .parent             = TYPE_OBJECT,
    .name               = TYPE_RAM_DISCARD_MANAGER,
    .instance_size      = sizeof(RamDiscardManager),
    .instance_init      = ram_discard_manager_initfn,
    .instance_finalize  = ram_discard_manager_finalize,
};

static const TypeInfo ram_discard_source_info = {
    .parent             = TYPE_INTERFACE,
    .name               = TYPE_RAM_DISCARD_SOURCE,
    .class_size         = sizeof(RamDiscardSourceClass),
};

static void ram_discard_manager_register_types(void)
{
    type_register_static(&ram_discard_manager_info);
    type_register_static(&ram_discard_source_info);
}

type_init(ram_discard_manager_register_types)
