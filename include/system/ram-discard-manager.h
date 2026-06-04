/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RAM Discard Manager
 *
 * Copyright Red Hat, Inc. 2026
 */

#ifndef RAM_DISCARD_MANAGER_H
#define RAM_DISCARD_MANAGER_H

#include "qemu/typedefs.h"
#include "qom/object.h"
#include "qemu/queue.h"

#define TYPE_RAM_DISCARD_MANAGER "ram-discard-manager"
typedef struct RamDiscardManagerClass RamDiscardManagerClass;
typedef struct RamDiscardManager RamDiscardManager;
DECLARE_OBJ_CHECKERS(RamDiscardManager, RamDiscardManagerClass,
                     RAM_DISCARD_MANAGER, TYPE_RAM_DISCARD_MANAGER);

#define TYPE_RAM_DISCARD_SOURCE "ram-discard-source"
typedef struct RamDiscardSourceClass RamDiscardSourceClass;
typedef struct RamDiscardSource RamDiscardSource;
DECLARE_OBJ_CHECKERS(RamDiscardSource, RamDiscardSourceClass,
                     RAM_DISCARD_SOURCE, TYPE_RAM_DISCARD_SOURCE);

typedef struct RamDiscardListener RamDiscardListener;
typedef int (*NotifyRamPopulate)(RamDiscardListener *rdl,
                                 MemoryRegionSection *section);
typedef void (*NotifyRamDiscard)(RamDiscardListener *rdl,
                                 MemoryRegionSection *section);

struct RamDiscardListener {
    /*
     * @notify_populate:
     *
     * Notification that previously discarded memory is about to get populated.
     * Listeners are able to object. If any listener objects, already
     * successfully notified listeners are notified about a discard again.
     *
     * @rdl: the #RamDiscardListener getting notified
     * @section: the #MemoryRegionSection to get populated. The section
     *           is aligned within the memory region to the minimum granularity
     *           unless it would exceed the registered section.
     *
     * Returns 0 on success. If the notification is rejected by the listener,
     * an error is returned.
     */
    NotifyRamPopulate notify_populate;

    /*
     * @notify_discard:
     *
     * Notification that previously populated memory was discarded successfully
     * and listeners should drop all references to such memory and prevent
     * new population (e.g., unmap).
     *
     * @rdl: the #RamDiscardListener getting notified
     * @section: the #MemoryRegionSection to get discarded. The section
     *           is aligned within the memory region to the minimum granularity
     *           unless it would exceed the registered section.
     */
    NotifyRamDiscard notify_discard;

    MemoryRegionSection *section;
    QLIST_ENTRY(RamDiscardListener) next;
};

static inline void ram_discard_listener_init(RamDiscardListener *rdl,
                                             NotifyRamPopulate populate_fn,
                                             NotifyRamDiscard discard_fn)
{
    rdl->notify_populate = populate_fn;
    rdl->notify_discard = discard_fn;
}

/**
 * typedef ReplayRamDiscardState:
 *
 * The callback handler for #RamDiscardSourceClass.replay_populated/
 * #RamDiscardSourceClass.replay_discarded to invoke on populated/discarded
 * parts.
 *
 * @section: the #MemoryRegionSection of populated/discarded part
 * @opaque: pointer to forward to the callback
 *
 * Returns 0 on success, or a negative error if failed.
 */
typedef int (*ReplayRamDiscardState)(MemoryRegionSection *section,
                                     void *opaque);

/*
 * RamDiscardSourceClass:
 *
 * A #RamDiscardSource provides information about which parts of a specific
 * RAM #MemoryRegion are currently populated (accessible) vs discarded.
 *
 * This is an interface that state providers (like virtio-mem or
 * RamBlockAttributes) implement to provide discard state information. A
 * #RamDiscardManager wraps sources and manages listener registrations and
 * notifications.
 */
struct RamDiscardSourceClass {
    /* private */
    InterfaceClass parent_class;

    /* public */

    /**
     * @get_min_granularity:
     *
     * Get the minimum granularity in which listeners will get notified
     * about changes within the #MemoryRegion via the #RamDiscardSource.
     *
     * @rds: the #RamDiscardSource
     * @mr: the #MemoryRegion
     *
     * Returns the minimum granularity.
     */
    uint64_t (*get_min_granularity)(const RamDiscardSource *rds,
                                    const MemoryRegion *mr);

    /**
     * @is_populated:
     *
     * Check whether the given #MemoryRegionSection is completely populated
     * (i.e., no parts are currently discarded) via the #RamDiscardSource.
     * There are no alignment requirements.
     *
     * @rds: the #RamDiscardSource
     * @section: the #MemoryRegionSection
     *
     * Returns whether the given range is completely populated.
     */
    bool (*is_populated)(const RamDiscardSource *rds,
                         const MemoryRegionSection *section);

    /**
     * @replay_populated:
     *
     * Call the #ReplayRamDiscardState callback for all populated parts within
     * the #MemoryRegionSection via the #RamDiscardSource.
     *
     * In case any call fails, no further calls are made.
     *
     * @rds: the #RamDiscardSource
     * @section: the #MemoryRegionSection
     * @replay_fn: the #ReplayRamDiscardState callback
     * @opaque: pointer to forward to the callback
     *
     * Returns 0 on success, or a negative error if any notification failed.
     */
    int (*replay_populated)(const RamDiscardSource *rds,
                            MemoryRegionSection *section,
                            ReplayRamDiscardState replay_fn, void *opaque);

    /**
     * @replay_discarded:
     *
     * Call the #ReplayRamDiscardState callback for all discarded parts within
     * the #MemoryRegionSection via the #RamDiscardSource.
     *
     * @rds: the #RamDiscardSource
     * @section: the #MemoryRegionSection
     * @replay_fn: the #ReplayRamDiscardState callback
     * @opaque: pointer to forward to the callback
     *
     * Returns 0 on success, or a negative error if any notification failed.
     */
    int (*replay_discarded)(const RamDiscardSource *rds,
                            MemoryRegionSection *section,
                            ReplayRamDiscardState replay_fn, void *opaque);
};

/**
 * RamDiscardManager:
 *
 * A #RamDiscardManager coordinates which parts of specific RAM #MemoryRegion
 * regions are currently populated to be used/accessed by the VM, notifying
 * after parts were discarded (freeing up memory) and before parts will be
 * populated (consuming memory), to be used/accessed by the VM.
 *
 * A #RamDiscardManager can only be set for a RAM #MemoryRegion while the
 * #MemoryRegion isn't mapped into an address space yet (either directly
 * or via an alias); it cannot change while the #MemoryRegion is
 * mapped into an address space.
 *
 * The #RamDiscardManager is intended to be used by technologies that are
 * incompatible with discarding of RAM (e.g., VFIO, which may pin all
 * memory inside a #MemoryRegion), and require proper coordination to only
 * map the currently populated parts, to hinder parts that are expected to
 * remain discarded from silently getting populated and consuming memory.
 * Technologies that support discarding of RAM don't have to bother and can
 * simply map the whole #MemoryRegion.
 *
 * An example #RamDiscardSource is virtio-mem, which logically (un)plugs
 * memory within an assigned RAM #MemoryRegion, coordinated with the VM.
 * Logically unplugging memory consists of discarding RAM. The VM agreed to not
 * access unplugged (discarded) memory - especially via DMA. virtio-mem will
 * properly coordinate with listeners before memory is plugged (populated),
 * and after memory is unplugged (discarded).
 *
 * Listeners are called in multiples of the minimum granularity (unless it
 * would exceed the registered range) and changes are aligned to the minimum
 * granularity within the #MemoryRegion. Listeners have to prepare for memory
 * becoming discarded in a different granularity than it was populated and the
 * other way around.
 */
struct RamDiscardManager {
    Object parent;

    RamDiscardSource *rds;
    MemoryRegion *mr;
    QLIST_HEAD(, RamDiscardListener) rdl_list;
};

RamDiscardManager *ram_discard_manager_new(MemoryRegion *mr,
                                           RamDiscardSource *rds);

uint64_t ram_discard_manager_get_min_granularity(const RamDiscardManager *rdm,
                                                 const MemoryRegion *mr);

bool ram_discard_manager_is_populated(const RamDiscardManager *rdm,
                                      const MemoryRegionSection *section);

/**
 * ram_discard_manager_replay_populated:
 *
 * A wrapper to call the #RamDiscardSourceClass.replay_populated callback
 * of the #RamDiscardSource sources.
 *
 * @rdm: the #RamDiscardManager
 * @section: the #MemoryRegionSection
 * @replay_fn: the #ReplayRamDiscardState callback
 * @opaque: pointer to forward to the callback
 *
 * Returns 0 on success, or a negative error if any notification failed.
 */
int ram_discard_manager_replay_populated(const RamDiscardManager *rdm,
                                         MemoryRegionSection *section,
                                         ReplayRamDiscardState replay_fn,
                                         void *opaque);

/**
 * ram_discard_manager_replay_discarded:
 *
 * A wrapper to call the #RamDiscardSourceClass.replay_discarded callback
 * of the #RamDiscardSource sources.
 *
 * @rdm: the #RamDiscardManager
 * @section: the #MemoryRegionSection
 * @replay_fn: the #ReplayRamDiscardState callback
 * @opaque: pointer to forward to the callback
 *
 * Returns 0 on success, or a negative error if any notification failed.
 */
int ram_discard_manager_replay_discarded(const RamDiscardManager *rdm,
                                         MemoryRegionSection *section,
                                         ReplayRamDiscardState replay_fn,
                                         void *opaque);

void ram_discard_manager_register_listener(RamDiscardManager *rdm,
                                           RamDiscardListener *rdl,
                                           MemoryRegionSection *section);

void ram_discard_manager_unregister_listener(RamDiscardManager *rdm,
                                             RamDiscardListener *rdl);

/*
 * Note: later refactoring should take the source into account and the manager
 *       should be able to aggregate multiple sources.
 */
int ram_discard_manager_notify_populate(RamDiscardManager *rdm,
                                        uint64_t offset, uint64_t size);

/*
 * Note: later refactoring should take the source into account and the manager
 *       should be able to aggregate multiple sources.
 */
void ram_discard_manager_notify_discard(RamDiscardManager *rdm,
                                        uint64_t offset, uint64_t size);

/*
 * Note: later refactoring should take the source into account and the manager
 *       should be able to aggregate multiple sources.
 */
void ram_discard_manager_notify_discard_all(RamDiscardManager *rdm);

/*
 * Replay populated sections to all registered listeners.
 *
 * Note: later refactoring should take the source into account and the manager
 *       should be able to aggregate multiple sources.
 */
int ram_discard_manager_replay_populated_to_listeners(RamDiscardManager *rdm);

#endif /* RAM_DISCARD_MANAGER_H */
