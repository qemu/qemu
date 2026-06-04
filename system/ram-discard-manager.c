/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RAM Discard Manager
 *
 * Copyright Red Hat, Inc. 2026
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
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

static int ram_discard_source_replay_populated(const RamDiscardSource *rds,
                                               MemoryRegionSection *section,
                                               ReplayRamDiscardState replay_fn,
                                               void *opaque)
{
    RamDiscardSourceClass *rdsc = RAM_DISCARD_SOURCE_GET_CLASS(rds);

    g_assert(rdsc->replay_populated);
    return rdsc->replay_populated(rds, section, replay_fn, opaque);
}

static int ram_discard_source_replay_discarded(const RamDiscardSource *rds,
                                               MemoryRegionSection *section,
                                               ReplayRamDiscardState replay_fn,
                                               void *opaque)
{
    RamDiscardSourceClass *rdsc = RAM_DISCARD_SOURCE_GET_CLASS(rds);

    g_assert(rdsc->replay_discarded);
    return rdsc->replay_discarded(rds, section, replay_fn, opaque);
}

RamDiscardManager *ram_discard_manager_new(MemoryRegion *mr,
                                           RamDiscardSource *rds)
{
    RamDiscardManager *rdm;

    rdm = RAM_DISCARD_MANAGER(object_new(TYPE_RAM_DISCARD_MANAGER));
    rdm->rds = rds;
    rdm->mr = mr;
    QLIST_INIT(&rdm->rdl_list);
    return rdm;
}

uint64_t ram_discard_manager_get_min_granularity(const RamDiscardManager *rdm,
                                                 const MemoryRegion *mr)
{
    return ram_discard_source_get_min_granularity(rdm->rds, mr);
}

bool ram_discard_manager_is_populated(const RamDiscardManager *rdm,
                                      const MemoryRegionSection *section)
{
    return ram_discard_source_is_populated(rdm->rds, section);
}

int ram_discard_manager_replay_populated(const RamDiscardManager *rdm,
                                         MemoryRegionSection *section,
                                         ReplayRamDiscardState replay_fn,
                                         void *opaque)
{
    return ram_discard_source_replay_populated(rdm->rds, section,
                                               replay_fn, opaque);
}

int ram_discard_manager_replay_discarded(const RamDiscardManager *rdm,
                                         MemoryRegionSection *section,
                                         ReplayRamDiscardState replay_fn,
                                         void *opaque)
{
    return ram_discard_source_replay_discarded(rdm->rds, section,
                                               replay_fn, opaque);
}

static void ram_discard_manager_initfn(Object *obj)
{
    RamDiscardManager *rdm = RAM_DISCARD_MANAGER(obj);

    QLIST_INIT(&rdm->rdl_list);
}

static void ram_discard_manager_finalize(Object *obj)
{
    RamDiscardManager *rdm = RAM_DISCARD_MANAGER(obj);

    g_assert(QLIST_EMPTY(&rdm->rdl_list));
}

int ram_discard_manager_notify_populate(RamDiscardManager *rdm,
                                        uint64_t offset, uint64_t size)
{
    RamDiscardListener *rdl, *rdl2;
    int ret = 0;

    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        MemoryRegionSection tmp = *rdl->section;

        if (!memory_region_section_intersect_range(&tmp, offset, size)) {
            continue;
        }
        ret = rdl->notify_populate(rdl, &tmp);
        if (ret) {
            break;
        }
    }

    if (ret) {
        /* Notify all already-notified listeners about discard. */
        QLIST_FOREACH(rdl2, &rdm->rdl_list, next) {
            MemoryRegionSection tmp = *rdl2->section;

            if (rdl2 == rdl) {
                break;
            }
            if (!memory_region_section_intersect_range(&tmp, offset, size)) {
                continue;
            }
            rdl2->notify_discard(rdl2, &tmp);
        }
    }
    return ret;
}

void ram_discard_manager_notify_discard(RamDiscardManager *rdm,
                                        uint64_t offset, uint64_t size)
{
    RamDiscardListener *rdl;

    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        MemoryRegionSection tmp = *rdl->section;

        if (!memory_region_section_intersect_range(&tmp, offset, size)) {
            continue;
        }
        rdl->notify_discard(rdl, &tmp);
    }
}

void ram_discard_manager_notify_discard_all(RamDiscardManager *rdm)
{
    RamDiscardListener *rdl;

    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        rdl->notify_discard(rdl, rdl->section);
    }
}

static int rdm_populate_cb(MemoryRegionSection *section, void *opaque)
{
    RamDiscardListener *rdl = opaque;

    return rdl->notify_populate(rdl, section);
}

void ram_discard_manager_register_listener(RamDiscardManager *rdm,
                                           RamDiscardListener *rdl,
                                           MemoryRegionSection *section)
{
    int ret;

    g_assert(section->mr == rdm->mr);

    rdl->section = memory_region_section_new_copy(section);
    QLIST_INSERT_HEAD(&rdm->rdl_list, rdl, next);

    ret = ram_discard_source_replay_populated(rdm->rds, rdl->section,
                                              rdm_populate_cb, rdl);
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
}

int ram_discard_manager_replay_populated_to_listeners(RamDiscardManager *rdm)
{
    RamDiscardListener *rdl;
    int ret = 0;

    QLIST_FOREACH(rdl, &rdm->rdl_list, next) {
        ret = ram_discard_source_replay_populated(rdm->rds, rdl->section,
                                                  rdm_populate_cb, rdl);
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
