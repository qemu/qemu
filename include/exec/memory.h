/*
 * Physical memory management API
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef MEMORY_H
#define MEMORY_H

#ifndef CONFIG_USER_ONLY

#include "exec/cpu-common.h"
#include "exec/hwaddr.h"
#include "exec/memattrs.h"
#include "exec/memop.h"
#include "exec/ramlist.h"
#include "qemu/bswap.h"
#include "qemu/queue.h"
#include "qemu/int128.h"
#include "qemu/notify.h"
#include "qom/object.h"
#include "qemu/rcu.h"

#define RAM_ADDR_INVALID (~(ram_addr_t)0)

#define MAX_PHYS_ADDR_SPACE_BITS 62
#define MAX_PHYS_ADDR            (((hwaddr)1 << MAX_PHYS_ADDR_SPACE_BITS) - 1)

#define TYPE_MEMORY_REGION "memory-region"
DECLARE_INSTANCE_CHECKER(MemoryRegion, MEMORY_REGION,
                         TYPE_MEMORY_REGION)

#define TYPE_IOMMU_MEMORY_REGION "iommu-memory-region"
typedef struct IOMMUMemoryRegionClass IOMMUMemoryRegionClass;
DECLARE_OBJ_CHECKERS(IOMMUMemoryRegion, IOMMUMemoryRegionClass,
                     IOMMU_MEMORY_REGION, TYPE_IOMMU_MEMORY_REGION)

#define TYPE_RAM_DISCARD_MANAGER "qemu:ram-discard-manager"
typedef struct RamDiscardManagerClass RamDiscardManagerClass;
typedef struct RamDiscardManager RamDiscardManager;
DECLARE_OBJ_CHECKERS(RamDiscardManager, RamDiscardManagerClass,
                     RAM_DISCARD_MANAGER, TYPE_RAM_DISCARD_MANAGER);

#ifdef CONFIG_FUZZ
void fuzz_dma_read_cb(size_t addr,
                      size_t len,
                      MemoryRegion *mr);
#else
static inline void fuzz_dma_read_cb(size_t addr,
                                    size_t len,
                                    MemoryRegion *mr)
{
    /* Do Nothing */
}
#endif

/* Possible bits for global_dirty_log_{start|stop} */

/* Dirty tracking enabled because migration is running */
#define GLOBAL_DIRTY_MIGRATION  (1U << 0)

/* Dirty tracking enabled because measuring dirty rate */
#define GLOBAL_DIRTY_DIRTY_RATE (1U << 1)

/* Dirty tracking enabled because dirty limit */
#define GLOBAL_DIRTY_LIMIT      (1U << 2)

#define GLOBAL_DIRTY_MASK  (0x7)

extern unsigned int global_dirty_tracking;

typedef struct MemoryRegionOps MemoryRegionOps;

struct ReservedRegion {
    hwaddr low;
    hwaddr high;
    unsigned type;
};

/**
 * struct MemoryRegionSection: describes a fragment of a #MemoryRegion
 *
 * @mr: the region, or %NULL if empty
 * @fv: the flat view of the address space the region is mapped in
 * @offset_within_region: the beginning of the section, relative to @mr's start
 * @size: the size of the section; will not exceed @mr's boundaries
 * @offset_within_address_space: the address of the first byte of the section
 *     relative to the region's address space
 * @readonly: writes to this section are ignored
 * @nonvolatile: this section is non-volatile
 */
struct MemoryRegionSection {
    Int128 size;
    MemoryRegion *mr;
    FlatView *fv;
    hwaddr offset_within_region;
    hwaddr offset_within_address_space;
    bool readonly;
    bool nonvolatile;
};

typedef struct IOMMUTLBEntry IOMMUTLBEntry;

/* See address_space_translate: bit 0 is read, bit 1 is write.  */
typedef enum {
    IOMMU_NONE = 0,
    IOMMU_RO   = 1,
    IOMMU_WO   = 2,
    IOMMU_RW   = 3,
} IOMMUAccessFlags;

#define IOMMU_ACCESS_FLAG(r, w) (((r) ? IOMMU_RO : 0) | ((w) ? IOMMU_WO : 0))

struct IOMMUTLBEntry {
    AddressSpace    *target_as;
    hwaddr           iova;
    hwaddr           translated_addr;
    hwaddr           addr_mask;  /* 0xfff = 4k translation */
    IOMMUAccessFlags perm;
};

/*
 * Bitmap for different IOMMUNotifier capabilities. Each notifier can
 * register with one or multiple IOMMU Notifier capability bit(s).
 *
 * Normally there're two use cases for the notifiers:
 *
 *   (1) When the device needs accurate synchronizations of the vIOMMU page
 *       tables, it needs to register with both MAP|UNMAP notifies (which
 *       is defined as IOMMU_NOTIFIER_IOTLB_EVENTS below).
 *
 *       Regarding to accurate synchronization, it's when the notified
 *       device maintains a shadow page table and must be notified on each
 *       guest MAP (page table entry creation) and UNMAP (invalidation)
 *       events (e.g. VFIO). Both notifications must be accurate so that
 *       the shadow page table is fully in sync with the guest view.
 *
 *   (2) When the device doesn't need accurate synchronizations of the
 *       vIOMMU page tables, it needs to register only with UNMAP or
 *       DEVIOTLB_UNMAP notifies.
 *
 *       It's when the device maintains a cache of IOMMU translations
 *       (IOTLB) and is able to fill that cache by requesting translations
 *       from the vIOMMU through a protocol similar to ATS (Address
 *       Translation Service).
 *
 *       Note that in this mode the vIOMMU will not maintain a shadowed
 *       page table for the address space, and the UNMAP messages can cover
 *       more than the pages that used to get mapped.  The IOMMU notifiee
 *       should be able to take care of over-sized invalidations.
 */
typedef enum {
    IOMMU_NOTIFIER_NONE = 0,
    /* Notify cache invalidations */
    IOMMU_NOTIFIER_UNMAP = 0x1,
    /* Notify entry changes (newly created entries) */
    IOMMU_NOTIFIER_MAP = 0x2,
    /* Notify changes on device IOTLB entries */
    IOMMU_NOTIFIER_DEVIOTLB_UNMAP = 0x04,
} IOMMUNotifierFlag;

#define IOMMU_NOTIFIER_IOTLB_EVENTS (IOMMU_NOTIFIER_MAP | IOMMU_NOTIFIER_UNMAP)
#define IOMMU_NOTIFIER_DEVIOTLB_EVENTS IOMMU_NOTIFIER_DEVIOTLB_UNMAP
#define IOMMU_NOTIFIER_ALL (IOMMU_NOTIFIER_IOTLB_EVENTS | \
                            IOMMU_NOTIFIER_DEVIOTLB_EVENTS)

struct IOMMUNotifier;
typedef void (*IOMMUNotify)(struct IOMMUNotifier *notifier,
                            IOMMUTLBEntry *data);

struct IOMMUNotifier {
    IOMMUNotify notify;
    IOMMUNotifierFlag notifier_flags;
    /* Notify for address space range start <= addr <= end */
    hwaddr start;
    hwaddr end;
    int iommu_idx;
    QLIST_ENTRY(IOMMUNotifier) node;
};
typedef struct IOMMUNotifier IOMMUNotifier;

typedef struct IOMMUTLBEvent {
    IOMMUNotifierFlag type;
    IOMMUTLBEntry entry;
} IOMMUTLBEvent;

/* RAM is pre-allocated and passed into qemu_ram_alloc_from_ptr */
#define RAM_PREALLOC   (1 << 0)

/* RAM is mmap-ed with MAP_SHARED */
#define RAM_SHARED     (1 << 1)

/* Only a portion of RAM (used_length) is actually used, and migrated.
 * Resizing RAM while migrating can result in the migration being canceled.
 */
#define RAM_RESIZEABLE (1 << 2)

/* UFFDIO_ZEROPAGE is available on this RAMBlock to atomically
 * zero the page and wake waiting processes.
 * (Set during postcopy)
 */
#define RAM_UF_ZEROPAGE (1 << 3)

/* RAM can be migrated */
#define RAM_MIGRATABLE (1 << 4)

/* RAM is a persistent kind memory */
#define RAM_PMEM (1 << 5)


/*
 * UFFDIO_WRITEPROTECT is used on this RAMBlock to
 * support 'write-tracking' migration type.
 * Implies ram_state->ram_wt_enabled.
 */
#define RAM_UF_WRITEPROTECT (1 << 6)

/*
 * RAM is mmap-ed with MAP_NORESERVE. When set, reserving swap space (or huge
 * pages if applicable) is skipped: will bail out if not supported. When not
 * set, the OS will do the reservation, if supported for the memory type.
 */
#define RAM_NORESERVE (1 << 7)

/* RAM that isn't accessible through normal means. */
#define RAM_PROTECTED (1 << 8)

static inline void iommu_notifier_init(IOMMUNotifier *n, IOMMUNotify fn,
                                       IOMMUNotifierFlag flags,
                                       hwaddr start, hwaddr end,
                                       int iommu_idx)
{
    n->notify = fn;
    n->notifier_flags = flags;
    n->start = start;
    n->end = end;
    n->iommu_idx = iommu_idx;
}

/*
 * Memory region callbacks
 */
struct MemoryRegionOps {
    /* Read from the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    uint64_t (*read)(void *opaque,
                     hwaddr addr,
                     unsigned size);
    /* Write to the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    void (*write)(void *opaque,
                  hwaddr addr,
                  uint64_t data,
                  unsigned size);

    MemTxResult (*read_with_attrs)(void *opaque,
                                   hwaddr addr,
                                   uint64_t *data,
                                   unsigned size,
                                   MemTxAttrs attrs);
    MemTxResult (*write_with_attrs)(void *opaque,
                                    hwaddr addr,
                                    uint64_t data,
                                    unsigned size,
                                    MemTxAttrs attrs);

    enum device_endian endianness;
    /* Guest-visible constraints: */
    struct {
        /* If nonzero, specify bounds on access sizes beyond which a machine
         * check is thrown.
         */
        unsigned min_access_size;
        unsigned max_access_size;
        /* If true, unaligned accesses are supported.  Otherwise unaligned
         * accesses throw machine checks.
         */
         bool unaligned;
        /*
         * If present, and returns #false, the transaction is not accepted
         * by the device (and results in machine dependent behaviour such
         * as a machine check exception).
         */
        bool (*accepts)(void *opaque, hwaddr addr,
                        unsigned size, bool is_write,
                        MemTxAttrs attrs);
    } valid;
    /* Internal implementation constraints: */
    struct {
        /* If nonzero, specifies the minimum size implemented.  Smaller sizes
         * will be rounded upwards and a partial result will be returned.
         */
        unsigned min_access_size;
        /* If nonzero, specifies the maximum size implemented.  Larger sizes
         * will be done as a series of accesses with smaller sizes.
         */
        unsigned max_access_size;
        /* If true, unaligned accesses are supported.  Otherwise all accesses
         * are converted to (possibly multiple) naturally aligned accesses.
         */
        bool unaligned;
    } impl;
};

typedef struct MemoryRegionClass {
    /* private */
    ObjectClass parent_class;
} MemoryRegionClass;


enum IOMMUMemoryRegionAttr {
    IOMMU_ATTR_SPAPR_TCE_FD
};

/*
 * IOMMUMemoryRegionClass:
 *
 * All IOMMU implementations need to subclass TYPE_IOMMU_MEMORY_REGION
 * and provide an implementation of at least the @translate method here
 * to handle requests to the memory region. Other methods are optional.
 *
 * The IOMMU implementation must use the IOMMU notifier infrastructure
 * to report whenever mappings are changed, by calling
 * memory_region_notify_iommu() (or, if necessary, by calling
 * memory_region_notify_iommu_one() for each registered notifier).
 *
 * Conceptually an IOMMU provides a mapping from input address
 * to an output TLB entry. If the IOMMU is aware of memory transaction
 * attributes and the output TLB entry depends on the transaction
 * attributes, we represent this using IOMMU indexes. Each index
 * selects a particular translation table that the IOMMU has:
 *
 *   @attrs_to_index returns the IOMMU index for a set of transaction attributes
 *
 *   @translate takes an input address and an IOMMU index
 *
 * and the mapping returned can only depend on the input address and the
 * IOMMU index.
 *
 * Most IOMMUs don't care about the transaction attributes and support
 * only a single IOMMU index. A more complex IOMMU might have one index
 * for secure transactions and one for non-secure transactions.
 */
struct IOMMUMemoryRegionClass {
    /* private: */
    MemoryRegionClass parent_class;

    /* public: */
    /**
     * @translate:
     *
     * Return a TLB entry that contains a given address.
     *
     * The IOMMUAccessFlags indicated via @flag are optional and may
     * be specified as IOMMU_NONE to indicate that the caller needs
     * the full translation information for both reads and writes. If
     * the access flags are specified then the IOMMU implementation
     * may use this as an optimization, to stop doing a page table
     * walk as soon as it knows that the requested permissions are not
     * allowed. If IOMMU_NONE is passed then the IOMMU must do the
     * full page table walk and report the permissions in the returned
     * IOMMUTLBEntry. (Note that this implies that an IOMMU may not
     * return different mappings for reads and writes.)
     *
     * The returned information remains valid while the caller is
     * holding the big QEMU lock or is inside an RCU critical section;
     * if the caller wishes to cache the mapping beyond that it must
     * register an IOMMU notifier so it can invalidate its cached
     * information when the IOMMU mapping changes.
     *
     * @iommu: the IOMMUMemoryRegion
     *
     * @hwaddr: address to be translated within the memory region
     *
     * @flag: requested access permission
     *
     * @iommu_idx: IOMMU index for the translation
     */
    IOMMUTLBEntry (*translate)(IOMMUMemoryRegion *iommu, hwaddr addr,
                               IOMMUAccessFlags flag, int iommu_idx);
    /**
     * @get_min_page_size:
     *
     * Returns minimum supported page size in bytes.
     *
     * If this method is not provided then the minimum is assumed to
     * be TARGET_PAGE_SIZE.
     *
     * @iommu: the IOMMUMemoryRegion
     */
    uint64_t (*get_min_page_size)(IOMMUMemoryRegion *iommu);
    /**
     * @notify_flag_changed:
     *
     * Called when IOMMU Notifier flag changes (ie when the set of
     * events which IOMMU users are requesting notification for changes).
     * Optional method -- need not be provided if the IOMMU does not
     * need to know exactly which events must be notified.
     *
     * @iommu: the IOMMUMemoryRegion
     *
     * @old_flags: events which previously needed to be notified
     *
     * @new_flags: events which now need to be notified
     *
     * Returns 0 on success, or a negative errno; in particular
     * returns -EINVAL if the new flag bitmap is not supported by the
     * IOMMU memory region. In case of failure, the error object
     * must be created
     */
    int (*notify_flag_changed)(IOMMUMemoryRegion *iommu,
                               IOMMUNotifierFlag old_flags,
                               IOMMUNotifierFlag new_flags,
                               Error **errp);
    /**
     * @replay:
     *
     * Called to handle memory_region_iommu_replay().
     *
     * The default implementation of memory_region_iommu_replay() is to
     * call the IOMMU translate method for every page in the address space
     * with flag == IOMMU_NONE and then call the notifier if translate
     * returns a valid mapping. If this method is implemented then it
     * overrides the default behaviour, and must provide the full semantics
     * of memory_region_iommu_replay(), by calling @notifier for every
     * translation present in the IOMMU.
     *
     * Optional method -- an IOMMU only needs to provide this method
     * if the default is inefficient or produces undesirable side effects.
     *
     * Note: this is not related to record-and-replay functionality.
     */
    void (*replay)(IOMMUMemoryRegion *iommu, IOMMUNotifier *notifier);

    /**
     * @get_attr:
     *
     * Get IOMMU misc attributes. This is an optional method that
     * can be used to allow users of the IOMMU to get implementation-specific
     * information. The IOMMU implements this method to handle calls
     * by IOMMU users to memory_region_iommu_get_attr() by filling in
     * the arbitrary data pointer for any IOMMUMemoryRegionAttr values that
     * the IOMMU supports. If the method is unimplemented then
     * memory_region_iommu_get_attr() will always return -EINVAL.
     *
     * @iommu: the IOMMUMemoryRegion
     *
     * @attr: attribute being queried
     *
     * @data: memory to fill in with the attribute data
     *
     * Returns 0 on success, or a negative errno; in particular
     * returns -EINVAL for unrecognized or unimplemented attribute types.
     */
    int (*get_attr)(IOMMUMemoryRegion *iommu, enum IOMMUMemoryRegionAttr attr,
                    void *data);

    /**
     * @attrs_to_index:
     *
     * Return the IOMMU index to use for a given set of transaction attributes.
     *
     * Optional method: if an IOMMU only supports a single IOMMU index then
     * the default implementation of memory_region_iommu_attrs_to_index()
     * will return 0.
     *
     * The indexes supported by an IOMMU must be contiguous, starting at 0.
     *
     * @iommu: the IOMMUMemoryRegion
     * @attrs: memory transaction attributes
     */
    int (*attrs_to_index)(IOMMUMemoryRegion *iommu, MemTxAttrs attrs);

    /**
     * @num_indexes:
     *
     * Return the number of IOMMU indexes this IOMMU supports.
     *
     * Optional method: if this method is not provided, then
     * memory_region_iommu_num_indexes() will return 1, indicating that
     * only a single IOMMU index is supported.
     *
     * @iommu: the IOMMUMemoryRegion
     */
    int (*num_indexes)(IOMMUMemoryRegion *iommu);

    /**
     * @iommu_set_page_size_mask:
     *
     * Restrict the page size mask that can be supported with a given IOMMU
     * memory region. Used for example to propagate host physical IOMMU page
     * size mask limitations to the virtual IOMMU.
     *
     * Optional method: if this method is not provided, then the default global
     * page mask is used.
     *
     * @iommu: the IOMMUMemoryRegion
     *
     * @page_size_mask: a bitmask of supported page sizes. At least one bit,
     * representing the smallest page size, must be set. Additional set bits
     * represent supported block sizes. For example a host physical IOMMU that
     * uses page tables with a page size of 4kB, and supports 2MB and 4GB
     * blocks, will set mask 0x40201000. A granule of 4kB with indiscriminate
     * block sizes is specified with mask 0xfffffffffffff000.
     *
     * Returns 0 on success, or a negative error. In case of failure, the error
     * object must be created.
     */
     int (*iommu_set_page_size_mask)(IOMMUMemoryRegion *iommu,
                                     uint64_t page_size_mask,
                                     Error **errp);
};

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
     * @section: the #MemoryRegionSection to get populated. The section
     *           is aligned within the memory region to the minimum granularity
     *           unless it would exceed the registered section.
     */
    NotifyRamDiscard notify_discard;

    /*
     * @double_discard_supported:
     *
     * The listener suppors getting @notify_discard notifications that span
     * already discarded parts.
     */
    bool double_discard_supported;

    MemoryRegionSection *section;
    QLIST_ENTRY(RamDiscardListener) next;
};

static inline void ram_discard_listener_init(RamDiscardListener *rdl,
                                             NotifyRamPopulate populate_fn,
                                             NotifyRamDiscard discard_fn,
                                             bool double_discard_supported)
{
    rdl->notify_populate = populate_fn;
    rdl->notify_discard = discard_fn;
    rdl->double_discard_supported = double_discard_supported;
}

typedef int (*ReplayRamPopulate)(MemoryRegionSection *section, void *opaque);
typedef void (*ReplayRamDiscard)(MemoryRegionSection *section, void *opaque);

/*
 * RamDiscardManagerClass:
 *
 * A #RamDiscardManager coordinates which parts of specific RAM #MemoryRegion
 * regions are currently populated to be used/accessed by the VM, notifying
 * after parts were discarded (freeing up memory) and before parts will be
 * populated (consuming memory), to be used/accessed by the VM.
 *
 * A #RamDiscardManager can only be set for a RAM #MemoryRegion while the
 * #MemoryRegion isn't mapped yet; it cannot change while the #MemoryRegion is
 * mapped.
 *
 * The #RamDiscardManager is intended to be used by technologies that are
 * incompatible with discarding of RAM (e.g., VFIO, which may pin all
 * memory inside a #MemoryRegion), and require proper coordination to only
 * map the currently populated parts, to hinder parts that are expected to
 * remain discarded from silently getting populated and consuming memory.
 * Technologies that support discarding of RAM don't have to bother and can
 * simply map the whole #MemoryRegion.
 *
 * An example #RamDiscardManager is virtio-mem, which logically (un)plugs
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
struct RamDiscardManagerClass {
    /* private */
    InterfaceClass parent_class;

    /* public */

    /**
     * @get_min_granularity:
     *
     * Get the minimum granularity in which listeners will get notified
     * about changes within the #MemoryRegion via the #RamDiscardManager.
     *
     * @rdm: the #RamDiscardManager
     * @mr: the #MemoryRegion
     *
     * Returns the minimum granularity.
     */
    uint64_t (*get_min_granularity)(const RamDiscardManager *rdm,
                                    const MemoryRegion *mr);

    /**
     * @is_populated:
     *
     * Check whether the given #MemoryRegionSection is completely populated
     * (i.e., no parts are currently discarded) via the #RamDiscardManager.
     * There are no alignment requirements.
     *
     * @rdm: the #RamDiscardManager
     * @section: the #MemoryRegionSection
     *
     * Returns whether the given range is completely populated.
     */
    bool (*is_populated)(const RamDiscardManager *rdm,
                         const MemoryRegionSection *section);

    /**
     * @replay_populated:
     *
     * Call the #ReplayRamPopulate callback for all populated parts within the
     * #MemoryRegionSection via the #RamDiscardManager.
     *
     * In case any call fails, no further calls are made.
     *
     * @rdm: the #RamDiscardManager
     * @section: the #MemoryRegionSection
     * @replay_fn: the #ReplayRamPopulate callback
     * @opaque: pointer to forward to the callback
     *
     * Returns 0 on success, or a negative error if any notification failed.
     */
    int (*replay_populated)(const RamDiscardManager *rdm,
                            MemoryRegionSection *section,
                            ReplayRamPopulate replay_fn, void *opaque);

    /**
     * @replay_discarded:
     *
     * Call the #ReplayRamDiscard callback for all discarded parts within the
     * #MemoryRegionSection via the #RamDiscardManager.
     *
     * @rdm: the #RamDiscardManager
     * @section: the #MemoryRegionSection
     * @replay_fn: the #ReplayRamDiscard callback
     * @opaque: pointer to forward to the callback
     */
    void (*replay_discarded)(const RamDiscardManager *rdm,
                             MemoryRegionSection *section,
                             ReplayRamDiscard replay_fn, void *opaque);

    /**
     * @register_listener:
     *
     * Register a #RamDiscardListener for the given #MemoryRegionSection and
     * immediately notify the #RamDiscardListener about all populated parts
     * within the #MemoryRegionSection via the #RamDiscardManager.
     *
     * In case any notification fails, no further notifications are triggered
     * and an error is logged.
     *
     * @rdm: the #RamDiscardManager
     * @rdl: the #RamDiscardListener
     * @section: the #MemoryRegionSection
     */
    void (*register_listener)(RamDiscardManager *rdm,
                              RamDiscardListener *rdl,
                              MemoryRegionSection *section);

    /**
     * @unregister_listener:
     *
     * Unregister a previously registered #RamDiscardListener via the
     * #RamDiscardManager after notifying the #RamDiscardListener about all
     * populated parts becoming unpopulated within the registered
     * #MemoryRegionSection.
     *
     * @rdm: the #RamDiscardManager
     * @rdl: the #RamDiscardListener
     */
    void (*unregister_listener)(RamDiscardManager *rdm,
                                RamDiscardListener *rdl);
};

uint64_t ram_discard_manager_get_min_granularity(const RamDiscardManager *rdm,
                                                 const MemoryRegion *mr);

bool ram_discard_manager_is_populated(const RamDiscardManager *rdm,
                                      const MemoryRegionSection *section);

int ram_discard_manager_replay_populated(const RamDiscardManager *rdm,
                                         MemoryRegionSection *section,
                                         ReplayRamPopulate replay_fn,
                                         void *opaque);

void ram_discard_manager_replay_discarded(const RamDiscardManager *rdm,
                                          MemoryRegionSection *section,
                                          ReplayRamDiscard replay_fn,
                                          void *opaque);

void ram_discard_manager_register_listener(RamDiscardManager *rdm,
                                           RamDiscardListener *rdl,
                                           MemoryRegionSection *section);

void ram_discard_manager_unregister_listener(RamDiscardManager *rdm,
                                             RamDiscardListener *rdl);

bool memory_get_xlat_addr(IOMMUTLBEntry *iotlb, void **vaddr,
                          ram_addr_t *ram_addr, bool *read_only,
                          bool *mr_has_discard_manager);

typedef struct CoalescedMemoryRange CoalescedMemoryRange;
typedef struct MemoryRegionIoeventfd MemoryRegionIoeventfd;

/** MemoryRegion:
 *
 * A struct representing a memory region.
 */
struct MemoryRegion {
    Object parent_obj;

    /* private: */

    /* The following fields should fit in a cache line */
    bool romd_mode;
    bool ram;
    bool subpage;
    bool readonly; /* For RAM regions */
    bool nonvolatile;
    bool rom_device;
    bool flush_coalesced_mmio;
    uint8_t dirty_log_mask;
    bool is_iommu;
    RAMBlock *ram_block;
    Object *owner;
    /* owner as TYPE_DEVICE. Used for re-entrancy checks in MR access hotpath */
    DeviceState *dev;

    const MemoryRegionOps *ops;
    void *opaque;
    MemoryRegion *container;
    int mapped_via_alias; /* Mapped via an alias, container might be NULL */
    Int128 size;
    hwaddr addr;
    void (*destructor)(MemoryRegion *mr);
    uint64_t align;
    bool terminates;
    bool ram_device;
    bool enabled;
    bool warning_printed; /* For reservations */
    uint8_t vga_logging_count;
    MemoryRegion *alias;
    hwaddr alias_offset;
    int32_t priority;
    QTAILQ_HEAD(, MemoryRegion) subregions;
    QTAILQ_ENTRY(MemoryRegion) subregions_link;
    QTAILQ_HEAD(, CoalescedMemoryRange) coalesced;
    const char *name;
    unsigned ioeventfd_nb;
    MemoryRegionIoeventfd *ioeventfds;
    RamDiscardManager *rdm; /* Only for RAM */

    /* For devices designed to perform re-entrant IO into their own IO MRs */
    bool disable_reentrancy_guard;
};

struct IOMMUMemoryRegion {
    MemoryRegion parent_obj;

    QLIST_HEAD(, IOMMUNotifier) iommu_notify;
    IOMMUNotifierFlag iommu_notify_flags;
};

#define IOMMU_NOTIFIER_FOREACH(n, mr) \
    QLIST_FOREACH((n), &(mr)->iommu_notify, node)

/**
 * struct MemoryListener: callbacks structure for updates to the physical memory map
 *
 * Allows a component to adjust to changes in the guest-visible memory map.
 * Use with memory_listener_register() and memory_listener_unregister().
 */
struct MemoryListener {
    /**
     * @begin:
     *
     * Called at the beginning of an address space update transaction.
     * Followed by calls to #MemoryListener.region_add(),
     * #MemoryListener.region_del(), #MemoryListener.region_nop(),
     * #MemoryListener.log_start() and #MemoryListener.log_stop() in
     * increasing address order.
     *
     * @listener: The #MemoryListener.
     */
    void (*begin)(MemoryListener *listener);

    /**
     * @commit:
     *
     * Called at the end of an address space update transaction,
     * after the last call to #MemoryListener.region_add(),
     * #MemoryListener.region_del() or #MemoryListener.region_nop(),
     * #MemoryListener.log_start() and #MemoryListener.log_stop().
     *
     * @listener: The #MemoryListener.
     */
    void (*commit)(MemoryListener *listener);

    /**
     * @region_add:
     *
     * Called during an address space update transaction,
     * for a section of the address space that is new in this address space
     * space since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     */
    void (*region_add)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @region_del:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has disappeared in the address
     * space since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The old #MemoryRegionSection.
     */
    void (*region_del)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @region_nop:
     *
     * Called during an address space update transaction,
     * for a section of the address space that is in the same place in the address
     * space as in the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     */
    void (*region_nop)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @log_start:
     *
     * Called during an address space update transaction, after
     * one of #MemoryListener.region_add(), #MemoryListener.region_del() or
     * #MemoryListener.region_nop(), if dirty memory logging clients have
     * become active since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     * @old: A bitmap of dirty memory logging clients that were active in
     * the previous transaction.
     * @new: A bitmap of dirty memory logging clients that are active in
     * the current transaction.
     */
    void (*log_start)(MemoryListener *listener, MemoryRegionSection *section,
                      int old, int new);

    /**
     * @log_stop:
     *
     * Called during an address space update transaction, after
     * one of #MemoryListener.region_add(), #MemoryListener.region_del() or
     * #MemoryListener.region_nop() and possibly after
     * #MemoryListener.log_start(), if dirty memory logging clients have
     * become inactive since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     * @old: A bitmap of dirty memory logging clients that were active in
     * the previous transaction.
     * @new: A bitmap of dirty memory logging clients that are active in
     * the current transaction.
     */
    void (*log_stop)(MemoryListener *listener, MemoryRegionSection *section,
                     int old, int new);

    /**
     * @log_sync:
     *
     * Called by memory_region_snapshot_and_clear_dirty() and
     * memory_global_dirty_log_sync(), before accessing QEMU's "official"
     * copy of the dirty memory bitmap for a #MemoryRegionSection.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     */
    void (*log_sync)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @log_sync_global:
     *
     * This is the global version of @log_sync when the listener does
     * not have a way to synchronize the log with finer granularity.
     * When the listener registers with @log_sync_global defined, then
     * its @log_sync must be NULL.  Vice versa.
     *
     * @listener: The #MemoryListener.
     * @last_stage: The last stage to synchronize the log during migration.
     * The caller should gurantee that the synchronization with true for
     * @last_stage is triggered for once after all VCPUs have been stopped.
     */
    void (*log_sync_global)(MemoryListener *listener, bool last_stage);

    /**
     * @log_clear:
     *
     * Called before reading the dirty memory bitmap for a
     * #MemoryRegionSection.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     */
    void (*log_clear)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @log_global_start:
     *
     * Called by memory_global_dirty_log_start(), which
     * enables the %DIRTY_LOG_MIGRATION client on all memory regions in
     * the address space.  #MemoryListener.log_global_start() is also
     * called when a #MemoryListener is added, if global dirty logging is
     * active at that time.
     *
     * @listener: The #MemoryListener.
     */
    void (*log_global_start)(MemoryListener *listener);

    /**
     * @log_global_stop:
     *
     * Called by memory_global_dirty_log_stop(), which
     * disables the %DIRTY_LOG_MIGRATION client on all memory regions in
     * the address space.
     *
     * @listener: The #MemoryListener.
     */
    void (*log_global_stop)(MemoryListener *listener);

    /**
     * @log_global_after_sync:
     *
     * Called after reading the dirty memory bitmap
     * for any #MemoryRegionSection.
     *
     * @listener: The #MemoryListener.
     */
    void (*log_global_after_sync)(MemoryListener *listener);

    /**
     * @eventfd_add:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has had a new ioeventfd
     * registration since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     * @match_data: The @match_data parameter for the new ioeventfd.
     * @data: The @data parameter for the new ioeventfd.
     * @e: The #EventNotifier parameter for the new ioeventfd.
     */
    void (*eventfd_add)(MemoryListener *listener, MemoryRegionSection *section,
                        bool match_data, uint64_t data, EventNotifier *e);

    /**
     * @eventfd_del:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has dropped an ioeventfd
     * registration since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     * @match_data: The @match_data parameter for the dropped ioeventfd.
     * @data: The @data parameter for the dropped ioeventfd.
     * @e: The #EventNotifier parameter for the dropped ioeventfd.
     */
    void (*eventfd_del)(MemoryListener *listener, MemoryRegionSection *section,
                        bool match_data, uint64_t data, EventNotifier *e);

    /**
     * @coalesced_io_add:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has had a new coalesced
     * MMIO range registration since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     * @addr: The starting address for the coalesced MMIO range.
     * @len: The length of the coalesced MMIO range.
     */
    void (*coalesced_io_add)(MemoryListener *listener, MemoryRegionSection *section,
                               hwaddr addr, hwaddr len);

    /**
     * @coalesced_io_del:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has dropped a coalesced
     * MMIO range since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     * @addr: The starting address for the coalesced MMIO range.
     * @len: The length of the coalesced MMIO range.
     */
    void (*coalesced_io_del)(MemoryListener *listener, MemoryRegionSection *section,
                               hwaddr addr, hwaddr len);
    /**
     * @priority:
     *
     * Govern the order in which memory listeners are invoked. Lower priorities
     * are invoked earlier for "add" or "start" callbacks, and later for "delete"
     * or "stop" callbacks.
     */
    unsigned priority;

    /**
     * @name:
     *
     * Name of the listener.  It can be used in contexts where we'd like to
     * identify one memory listener with the rest.
     */
    const char *name;

    /* private: */
    AddressSpace *address_space;
    QTAILQ_ENTRY(MemoryListener) link;
    QTAILQ_ENTRY(MemoryListener) link_as;
};

/**
 * struct AddressSpace: describes a mapping of addresses to #MemoryRegion objects
 */
struct AddressSpace {
    /* private: */
    struct rcu_head rcu;
    char *name;
    MemoryRegion *root;

    /* Accessed via RCU.  */
    struct FlatView *current_map;

    int ioeventfd_nb;
    struct MemoryRegionIoeventfd *ioeventfds;
    QTAILQ_HEAD(, MemoryListener) listeners;
    QTAILQ_ENTRY(AddressSpace) address_spaces_link;
};

typedef struct AddressSpaceDispatch AddressSpaceDispatch;
typedef struct FlatRange FlatRange;

/* Flattened global view of current active memory hierarchy.  Kept in sorted
 * order.
 */
struct FlatView {
    struct rcu_head rcu;
    unsigned ref;
    FlatRange *ranges;
    unsigned nr;
    unsigned nr_allocated;
    struct AddressSpaceDispatch *dispatch;
    MemoryRegion *root;
};

static inline FlatView *address_space_to_flatview(AddressSpace *as)
{
    return qatomic_rcu_read(&as->current_map);
}

/**
 * typedef flatview_cb: callback for flatview_for_each_range()
 *
 * @start: start address of the range within the FlatView
 * @len: length of the range in bytes
 * @mr: MemoryRegion covering this range
 * @offset_in_region: offset of the first byte of the range within @mr
 * @opaque: data pointer passed to flatview_for_each_range()
 *
 * Returns: true to stop the iteration, false to keep going.
 */
typedef bool (*flatview_cb)(Int128 start,
                            Int128 len,
                            const MemoryRegion *mr,
                            hwaddr offset_in_region,
                            void *opaque);

/**
 * flatview_for_each_range: Iterate through a FlatView
 * @fv: the FlatView to iterate through
 * @cb: function to call for each range
 * @opaque: opaque data pointer to pass to @cb
 *
 * A FlatView is made up of a list of non-overlapping ranges, each of
 * which is a slice of a MemoryRegion. This function iterates through
 * each range in @fv, calling @cb. The callback function can terminate
 * iteration early by returning 'true'.
 */
void flatview_for_each_range(FlatView *fv, flatview_cb cb, void *opaque);

static inline bool MemoryRegionSection_eq(MemoryRegionSection *a,
                                          MemoryRegionSection *b)
{
    return a->mr == b->mr &&
           a->fv == b->fv &&
           a->offset_within_region == b->offset_within_region &&
           a->offset_within_address_space == b->offset_within_address_space &&
           int128_eq(a->size, b->size) &&
           a->readonly == b->readonly &&
           a->nonvolatile == b->nonvolatile;
}

/**
 * memory_region_section_new_copy: Copy a memory region section
 *
 * Allocate memory for a new copy, copy the memory region section, and
 * properly take a reference on all relevant members.
 *
 * @s: the #MemoryRegionSection to copy
 */
MemoryRegionSection *memory_region_section_new_copy(MemoryRegionSection *s);

/**
 * memory_region_section_new_copy: Free a copied memory region section
 *
 * Free a copy of a memory section created via memory_region_section_new_copy().
 * properly dropping references on all relevant members.
 *
 * @s: the #MemoryRegionSection to copy
 */
void memory_region_section_free_copy(MemoryRegionSection *s);

/**
 * memory_region_init: Initialize a memory region
 *
 * The region typically acts as a container for other memory regions.  Use
 * memory_region_add_subregion() to add subregions.
 *
 * @mr: the #MemoryRegion to be initialized
 * @owner: the object that tracks the region's reference count
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region; any subregions beyond this size will be clipped
 */
void memory_region_init(MemoryRegion *mr,
                        Object *owner,
                        const char *name,
                        uint64_t size);

/**
 * memory_region_ref: Add 1 to a memory region's reference count
 *
 * Whenever memory regions are accessed outside the BQL, they need to be
 * preserved against hot-unplug.  MemoryRegions actually do not have their
 * own reference count; they piggyback on a QOM object, their "owner".
 * This function adds a reference to the owner.
 *
 * All MemoryRegions must have an owner if they can disappear, even if the
 * device they belong to operates exclusively under the BQL.  This is because
 * the region could be returned at any time by memory_region_find, and this
 * is usually under guest control.
 *
 * @mr: the #MemoryRegion
 */
void memory_region_ref(MemoryRegion *mr);

/**
 * memory_region_unref: Remove 1 to a memory region's reference count
 *
 * Whenever memory regions are accessed outside the BQL, they need to be
 * preserved against hot-unplug.  MemoryRegions actually do not have their
 * own reference count; they piggyback on a QOM object, their "owner".
 * This function removes a reference to the owner and possibly destroys it.
 *
 * @mr: the #MemoryRegion
 */
void memory_region_unref(MemoryRegion *mr);

/**
 * memory_region_init_io: Initialize an I/O memory region.
 *
 * Accesses into the region will cause the callbacks in @ops to be called.
 * if @size is nonzero, subregions will be clipped to @size.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @ops: a structure containing read and write callbacks to be used when
 *       I/O is performed on the region.
 * @opaque: passed to the read and write callbacks of the @ops structure.
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_io(MemoryRegion *mr,
                           Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size);

/**
 * memory_region_init_ram_nomigrate:  Initialize RAM memory region.  Accesses
 *                                    into the region will modify memory
 *                                    directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_ram_nomigrate(MemoryRegion *mr,
                                      Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      Error **errp);

/**
 * memory_region_init_ram_flags_nomigrate:  Initialize RAM memory region.
 *                                          Accesses into the region will
 *                                          modify memory directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @ram_flags: RamBlock flags. Supported flags: RAM_SHARED, RAM_NORESERVE.
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_ram_flags_nomigrate(MemoryRegion *mr,
                                            Object *owner,
                                            const char *name,
                                            uint64_t size,
                                            uint32_t ram_flags,
                                            Error **errp);

/**
 * memory_region_init_resizeable_ram:  Initialize memory region with resizable
 *                                     RAM.  Accesses into the region will
 *                                     modify memory directly.  Only an initial
 *                                     portion of this RAM is actually used.
 *                                     Changing the size while migrating
 *                                     can result in the migration being
 *                                     canceled.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: used size of the region.
 * @max_size: max size of the region.
 * @resized: callback to notify owner about used size change.
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_resizeable_ram(MemoryRegion *mr,
                                       Object *owner,
                                       const char *name,
                                       uint64_t size,
                                       uint64_t max_size,
                                       void (*resized)(const char*,
                                                       uint64_t length,
                                                       void *host),
                                       Error **errp);
#ifdef CONFIG_POSIX

/**
 * memory_region_init_ram_from_file:  Initialize RAM memory region with a
 *                                    mmap-ed backend.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @align: alignment of the region base address; if 0, the default alignment
 *         (getpagesize()) will be used.
 * @ram_flags: RamBlock flags. Supported flags: RAM_SHARED, RAM_PMEM,
 *             RAM_NORESERVE,
 * @path: the path in which to allocate the RAM.
 * @offset: offset within the file referenced by path
 * @readonly: true to open @path for reading, false for read/write.
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_ram_from_file(MemoryRegion *mr,
                                      Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      uint64_t align,
                                      uint32_t ram_flags,
                                      const char *path,
                                      ram_addr_t offset,
                                      bool readonly,
                                      Error **errp);

/**
 * memory_region_init_ram_from_fd:  Initialize RAM memory region with a
 *                                  mmap-ed backend.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: the name of the region.
 * @size: size of the region.
 * @ram_flags: RamBlock flags. Supported flags: RAM_SHARED, RAM_PMEM,
 *             RAM_NORESERVE, RAM_PROTECTED.
 * @fd: the fd to mmap.
 * @offset: offset within the file referenced by fd
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_ram_from_fd(MemoryRegion *mr,
                                    Object *owner,
                                    const char *name,
                                    uint64_t size,
                                    uint32_t ram_flags,
                                    int fd,
                                    ram_addr_t offset,
                                    Error **errp);
#endif

/**
 * memory_region_init_ram_ptr:  Initialize RAM memory region from a
 *                              user-provided pointer.  Accesses into the
 *                              region will modify memory directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @ptr: memory to be mapped; must contain at least @size bytes.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 */
void memory_region_init_ram_ptr(MemoryRegion *mr,
                                Object *owner,
                                const char *name,
                                uint64_t size,
                                void *ptr);

/**
 * memory_region_init_ram_device_ptr:  Initialize RAM device memory region from
 *                                     a user-provided pointer.
 *
 * A RAM device represents a mapping to a physical device, such as to a PCI
 * MMIO BAR of an vfio-pci assigned device.  The memory region may be mapped
 * into the VM address space and access to the region will modify memory
 * directly.  However, the memory region should not be included in a memory
 * dump (device may not be enabled/mapped at the time of the dump), and
 * operations incompatible with manipulating MMIO should be avoided.  Replaces
 * skip_dump flag.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: the name of the region.
 * @size: size of the region.
 * @ptr: memory to be mapped; must contain at least @size bytes.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM memory region to be migrated; that is the responsibility of the caller.
 * (For RAM device memory regions, migrating the contents rarely makes sense.)
 */
void memory_region_init_ram_device_ptr(MemoryRegion *mr,
                                       Object *owner,
                                       const char *name,
                                       uint64_t size,
                                       void *ptr);

/**
 * memory_region_init_alias: Initialize a memory region that aliases all or a
 *                           part of another memory region.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: used for debugging; not visible to the user or ABI
 * @orig: the region to be referenced; @mr will be equivalent to
 *        @orig between @offset and @offset + @size - 1.
 * @offset: start of the section in @orig to be referenced.
 * @size: size of the region.
 */
void memory_region_init_alias(MemoryRegion *mr,
                              Object *owner,
                              const char *name,
                              MemoryRegion *orig,
                              hwaddr offset,
                              uint64_t size);

/**
 * memory_region_init_rom_nomigrate: Initialize a ROM memory region.
 *
 * This has the same effect as calling memory_region_init_ram_nomigrate()
 * and then marking the resulting region read-only with
 * memory_region_set_readonly().
 *
 * Note that this function does not do anything to cause the data in the
 * RAM side of the memory region to be migrated; that is the responsibility
 * of the caller.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_init_rom_nomigrate(MemoryRegion *mr,
                                      Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      Error **errp);

/**
 * memory_region_init_rom_device_nomigrate:  Initialize a ROM memory region.
 *                                 Writes are handled via callbacks.
 *
 * Note that this function does not do anything to cause the data in the
 * RAM side of the memory region to be migrated; that is the responsibility
 * of the caller.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @ops: callbacks for write access handling (must not be NULL).
 * @opaque: passed to the read and write callbacks of the @ops structure.
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_init_rom_device_nomigrate(MemoryRegion *mr,
                                             Object *owner,
                                             const MemoryRegionOps *ops,
                                             void *opaque,
                                             const char *name,
                                             uint64_t size,
                                             Error **errp);

/**
 * memory_region_init_iommu: Initialize a memory region of a custom type
 * that translates addresses
 *
 * An IOMMU region translates addresses and forwards accesses to a target
 * memory region.
 *
 * The IOMMU implementation must define a subclass of TYPE_IOMMU_MEMORY_REGION.
 * @_iommu_mr should be a pointer to enough memory for an instance of
 * that subclass, @instance_size is the size of that subclass, and
 * @mrtypename is its name. This function will initialize @_iommu_mr as an
 * instance of the subclass, and its methods will then be called to handle
 * accesses to the memory region. See the documentation of
 * #IOMMUMemoryRegionClass for further details.
 *
 * @_iommu_mr: the #IOMMUMemoryRegion to be initialized
 * @instance_size: the IOMMUMemoryRegion subclass instance size
 * @mrtypename: the type name of the #IOMMUMemoryRegion
 * @owner: the object that tracks the region's reference count
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_iommu(void *_iommu_mr,
                              size_t instance_size,
                              const char *mrtypename,
                              Object *owner,
                              const char *name,
                              uint64_t size);

/**
 * memory_region_init_ram - Initialize RAM memory region.  Accesses into the
 *                          region will modify memory directly.
 *
 * @mr: the #MemoryRegion to be initialized
 * @owner: the object that tracks the region's reference count (must be
 *         TYPE_DEVICE or a subclass of TYPE_DEVICE, or NULL)
 * @name: name of the memory region
 * @size: size of the region in bytes
 * @errp: pointer to Error*, to store an error if it happens.
 *
 * This function allocates RAM for a board model or device, and
 * arranges for it to be migrated (by calling vmstate_register_ram()
 * if @owner is a DeviceState, or vmstate_register_ram_global() if
 * @owner is NULL).
 *
 * TODO: Currently we restrict @owner to being either NULL (for
 * global RAM regions with no owner) or devices, so that we can
 * give the RAM block a unique name for migration purposes.
 * We should lift this restriction and allow arbitrary Objects.
 * If you pass a non-NULL non-device @owner then we will assert.
 */
void memory_region_init_ram(MemoryRegion *mr,
                            Object *owner,
                            const char *name,
                            uint64_t size,
                            Error **errp);

/**
 * memory_region_init_rom: Initialize a ROM memory region.
 *
 * This has the same effect as calling memory_region_init_ram()
 * and then marking the resulting region read-only with
 * memory_region_set_readonly(). This includes arranging for the
 * contents to be migrated.
 *
 * TODO: Currently we restrict @owner to being either NULL (for
 * global RAM regions with no owner) or devices, so that we can
 * give the RAM block a unique name for migration purposes.
 * We should lift this restriction and allow arbitrary Objects.
 * If you pass a non-NULL non-device @owner then we will assert.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_init_rom(MemoryRegion *mr,
                            Object *owner,
                            const char *name,
                            uint64_t size,
                            Error **errp);

/**
 * memory_region_init_rom_device:  Initialize a ROM memory region.
 *                                 Writes are handled via callbacks.
 *
 * This function initializes a memory region backed by RAM for reads
 * and callbacks for writes, and arranges for the RAM backing to
 * be migrated (by calling vmstate_register_ram()
 * if @owner is a DeviceState, or vmstate_register_ram_global() if
 * @owner is NULL).
 *
 * TODO: Currently we restrict @owner to being either NULL (for
 * global RAM regions with no owner) or devices, so that we can
 * give the RAM block a unique name for migration purposes.
 * We should lift this restriction and allow arbitrary Objects.
 * If you pass a non-NULL non-device @owner then we will assert.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @owner: the object that tracks the region's reference count
 * @ops: callbacks for write access handling (must not be NULL).
 * @opaque: passed to the read and write callbacks of the @ops structure.
 * @name: Region name, becomes part of RAMBlock name used in migration stream
 *        must be unique within any device
 * @size: size of the region.
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_init_rom_device(MemoryRegion *mr,
                                   Object *owner,
                                   const MemoryRegionOps *ops,
                                   void *opaque,
                                   const char *name,
                                   uint64_t size,
                                   Error **errp);


/**
 * memory_region_owner: get a memory region's owner.
 *
 * @mr: the memory region being queried.
 */
Object *memory_region_owner(MemoryRegion *mr);

/**
 * memory_region_size: get a memory region's size.
 *
 * @mr: the memory region being queried.
 */
uint64_t memory_region_size(MemoryRegion *mr);

/**
 * memory_region_is_ram: check whether a memory region is random access
 *
 * Returns %true if a memory region is random access.
 *
 * @mr: the memory region being queried
 */
static inline bool memory_region_is_ram(MemoryRegion *mr)
{
    return mr->ram;
}

/**
 * memory_region_is_ram_device: check whether a memory region is a ram device
 *
 * Returns %true if a memory region is a device backed ram region
 *
 * @mr: the memory region being queried
 */
bool memory_region_is_ram_device(MemoryRegion *mr);

/**
 * memory_region_is_romd: check whether a memory region is in ROMD mode
 *
 * Returns %true if a memory region is a ROM device and currently set to allow
 * direct reads.
 *
 * @mr: the memory region being queried
 */
static inline bool memory_region_is_romd(MemoryRegion *mr)
{
    return mr->rom_device && mr->romd_mode;
}

/**
 * memory_region_is_protected: check whether a memory region is protected
 *
 * Returns %true if a memory region is protected RAM and cannot be accessed
 * via standard mechanisms, e.g. DMA.
 *
 * @mr: the memory region being queried
 */
bool memory_region_is_protected(MemoryRegion *mr);

/**
 * memory_region_get_iommu: check whether a memory region is an iommu
 *
 * Returns pointer to IOMMUMemoryRegion if a memory region is an iommu,
 * otherwise NULL.
 *
 * @mr: the memory region being queried
 */
static inline IOMMUMemoryRegion *memory_region_get_iommu(MemoryRegion *mr)
{
    if (mr->alias) {
        return memory_region_get_iommu(mr->alias);
    }
    if (mr->is_iommu) {
        return (IOMMUMemoryRegion *) mr;
    }
    return NULL;
}

/**
 * memory_region_get_iommu_class_nocheck: returns iommu memory region class
 *   if an iommu or NULL if not
 *
 * Returns pointer to IOMMUMemoryRegionClass if a memory region is an iommu,
 * otherwise NULL. This is fast path avoiding QOM checking, use with caution.
 *
 * @iommu_mr: the memory region being queried
 */
static inline IOMMUMemoryRegionClass *memory_region_get_iommu_class_nocheck(
        IOMMUMemoryRegion *iommu_mr)
{
    return (IOMMUMemoryRegionClass *) (((Object *)iommu_mr)->class);
}

#define memory_region_is_iommu(mr) (memory_region_get_iommu(mr) != NULL)

/**
 * memory_region_iommu_get_min_page_size: get minimum supported page size
 * for an iommu
 *
 * Returns minimum supported page size for an iommu.
 *
 * @iommu_mr: the memory region being queried
 */
uint64_t memory_region_iommu_get_min_page_size(IOMMUMemoryRegion *iommu_mr);

/**
 * memory_region_notify_iommu: notify a change in an IOMMU translation entry.
 *
 * Note: for any IOMMU implementation, an in-place mapping change
 * should be notified with an UNMAP followed by a MAP.
 *
 * @iommu_mr: the memory region that was changed
 * @iommu_idx: the IOMMU index for the translation table which has changed
 * @event: TLB event with the new entry in the IOMMU translation table.
 *         The entry replaces all old entries for the same virtual I/O address
 *         range.
 */
void memory_region_notify_iommu(IOMMUMemoryRegion *iommu_mr,
                                int iommu_idx,
                                IOMMUTLBEvent event);

/**
 * memory_region_notify_iommu_one: notify a change in an IOMMU translation
 *                           entry to a single notifier
 *
 * This works just like memory_region_notify_iommu(), but it only
 * notifies a specific notifier, not all of them.
 *
 * @notifier: the notifier to be notified
 * @event: TLB event with the new entry in the IOMMU translation table.
 *         The entry replaces all old entries for the same virtual I/O address
 *         range.
 */
void memory_region_notify_iommu_one(IOMMUNotifier *notifier,
                                    IOMMUTLBEvent *event);

/**
 * memory_region_unmap_iommu_notifier_range: notify a unmap for an IOMMU
 *                                           translation that covers the
 *                                           range of a notifier
 *
 * @notifier: the notifier to be notified
 */
void memory_region_unmap_iommu_notifier_range(IOMMUNotifier *notifier);


/**
 * memory_region_register_iommu_notifier: register a notifier for changes to
 * IOMMU translation entries.
 *
 * Returns 0 on success, or a negative errno otherwise. In particular,
 * -EINVAL indicates that at least one of the attributes of the notifier
 * is not supported (flag/range) by the IOMMU memory region. In case of error
 * the error object must be created.
 *
 * @mr: the memory region to observe
 * @n: the IOMMUNotifier to be added; the notify callback receives a
 *     pointer to an #IOMMUTLBEntry as the opaque value; the pointer
 *     ceases to be valid on exit from the notifier.
 * @errp: pointer to Error*, to store an error if it happens.
 */
int memory_region_register_iommu_notifier(MemoryRegion *mr,
                                          IOMMUNotifier *n, Error **errp);

/**
 * memory_region_iommu_replay: replay existing IOMMU translations to
 * a notifier with the minimum page granularity returned by
 * mr->iommu_ops->get_page_size().
 *
 * Note: this is not related to record-and-replay functionality.
 *
 * @iommu_mr: the memory region to observe
 * @n: the notifier to which to replay iommu mappings
 */
void memory_region_iommu_replay(IOMMUMemoryRegion *iommu_mr, IOMMUNotifier *n);

/**
 * memory_region_unregister_iommu_notifier: unregister a notifier for
 * changes to IOMMU translation entries.
 *
 * @mr: the memory region which was observed and for which notity_stopped()
 *      needs to be called
 * @n: the notifier to be removed.
 */
void memory_region_unregister_iommu_notifier(MemoryRegion *mr,
                                             IOMMUNotifier *n);

/**
 * memory_region_iommu_get_attr: return an IOMMU attr if get_attr() is
 * defined on the IOMMU.
 *
 * Returns 0 on success, or a negative errno otherwise. In particular,
 * -EINVAL indicates that the IOMMU does not support the requested
 * attribute.
 *
 * @iommu_mr: the memory region
 * @attr: the requested attribute
 * @data: a pointer to the requested attribute data
 */
int memory_region_iommu_get_attr(IOMMUMemoryRegion *iommu_mr,
                                 enum IOMMUMemoryRegionAttr attr,
                                 void *data);

/**
 * memory_region_iommu_attrs_to_index: return the IOMMU index to
 * use for translations with the given memory transaction attributes.
 *
 * @iommu_mr: the memory region
 * @attrs: the memory transaction attributes
 */
int memory_region_iommu_attrs_to_index(IOMMUMemoryRegion *iommu_mr,
                                       MemTxAttrs attrs);

/**
 * memory_region_iommu_num_indexes: return the total number of IOMMU
 * indexes that this IOMMU supports.
 *
 * @iommu_mr: the memory region
 */
int memory_region_iommu_num_indexes(IOMMUMemoryRegion *iommu_mr);

/**
 * memory_region_iommu_set_page_size_mask: set the supported page
 * sizes for a given IOMMU memory region
 *
 * @iommu_mr: IOMMU memory region
 * @page_size_mask: supported page size mask
 * @errp: pointer to Error*, to store an error if it happens.
 */
int memory_region_iommu_set_page_size_mask(IOMMUMemoryRegion *iommu_mr,
                                           uint64_t page_size_mask,
                                           Error **errp);

/**
 * memory_region_name: get a memory region's name
 *
 * Returns the string that was used to initialize the memory region.
 *
 * @mr: the memory region being queried
 */
const char *memory_region_name(const MemoryRegion *mr);

/**
 * memory_region_is_logging: return whether a memory region is logging writes
 *
 * Returns %true if the memory region is logging writes for the given client
 *
 * @mr: the memory region being queried
 * @client: the client being queried
 */
bool memory_region_is_logging(MemoryRegion *mr, uint8_t client);

/**
 * memory_region_get_dirty_log_mask: return the clients for which a
 * memory region is logging writes.
 *
 * Returns a bitmap of clients, in which the DIRTY_MEMORY_* constants
 * are the bit indices.
 *
 * @mr: the memory region being queried
 */
uint8_t memory_region_get_dirty_log_mask(MemoryRegion *mr);

/**
 * memory_region_is_rom: check whether a memory region is ROM
 *
 * Returns %true if a memory region is read-only memory.
 *
 * @mr: the memory region being queried
 */
static inline bool memory_region_is_rom(MemoryRegion *mr)
{
    return mr->ram && mr->readonly;
}

/**
 * memory_region_is_nonvolatile: check whether a memory region is non-volatile
 *
 * Returns %true is a memory region is non-volatile memory.
 *
 * @mr: the memory region being queried
 */
static inline bool memory_region_is_nonvolatile(MemoryRegion *mr)
{
    return mr->nonvolatile;
}

/**
 * memory_region_get_fd: Get a file descriptor backing a RAM memory region.
 *
 * Returns a file descriptor backing a file-based RAM memory region,
 * or -1 if the region is not a file-based RAM memory region.
 *
 * @mr: the RAM or alias memory region being queried.
 */
int memory_region_get_fd(MemoryRegion *mr);

/**
 * memory_region_from_host: Convert a pointer into a RAM memory region
 * and an offset within it.
 *
 * Given a host pointer inside a RAM memory region (created with
 * memory_region_init_ram() or memory_region_init_ram_ptr()), return
 * the MemoryRegion and the offset within it.
 *
 * Use with care; by the time this function returns, the returned pointer is
 * not protected by RCU anymore.  If the caller is not within an RCU critical
 * section and does not hold the iothread lock, it must have other means of
 * protecting the pointer, such as a reference to the region that includes
 * the incoming ram_addr_t.
 *
 * @ptr: the host pointer to be converted
 * @offset: the offset within memory region
 */
MemoryRegion *memory_region_from_host(void *ptr, ram_addr_t *offset);

/**
 * memory_region_get_ram_ptr: Get a pointer into a RAM memory region.
 *
 * Returns a host pointer to a RAM memory region (created with
 * memory_region_init_ram() or memory_region_init_ram_ptr()).
 *
 * Use with care; by the time this function returns, the returned pointer is
 * not protected by RCU anymore.  If the caller is not within an RCU critical
 * section and does not hold the iothread lock, it must have other means of
 * protecting the pointer, such as a reference to the region that includes
 * the incoming ram_addr_t.
 *
 * @mr: the memory region being queried.
 */
void *memory_region_get_ram_ptr(MemoryRegion *mr);

/* memory_region_ram_resize: Resize a RAM region.
 *
 * Resizing RAM while migrating can result in the migration being canceled.
 * Care has to be taken if the guest might have already detected the memory.
 *
 * @mr: a memory region created with @memory_region_init_resizeable_ram.
 * @newsize: the new size the region
 * @errp: pointer to Error*, to store an error if it happens.
 */
void memory_region_ram_resize(MemoryRegion *mr, ram_addr_t newsize,
                              Error **errp);

/**
 * memory_region_msync: Synchronize selected address range of
 * a memory mapped region
 *
 * @mr: the memory region to be msync
 * @addr: the initial address of the range to be sync
 * @size: the size of the range to be sync
 */
void memory_region_msync(MemoryRegion *mr, hwaddr addr, hwaddr size);

/**
 * memory_region_writeback: Trigger cache writeback for
 * selected address range
 *
 * @mr: the memory region to be updated
 * @addr: the initial address of the range to be written back
 * @size: the size of the range to be written back
 */
void memory_region_writeback(MemoryRegion *mr, hwaddr addr, hwaddr size);

/**
 * memory_region_set_log: Turn dirty logging on or off for a region.
 *
 * Turns dirty logging on or off for a specified client (display, migration).
 * Only meaningful for RAM regions.
 *
 * @mr: the memory region being updated.
 * @log: whether dirty logging is to be enabled or disabled.
 * @client: the user of the logging information; %DIRTY_MEMORY_VGA only.
 */
void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client);

/**
 * memory_region_set_dirty: Mark a range of bytes as dirty in a memory region.
 *
 * Marks a range of bytes as dirty, after it has been dirtied outside
 * guest code.
 *
 * @mr: the memory region being dirtied.
 * @addr: the address (relative to the start of the region) being dirtied.
 * @size: size of the range being dirtied.
 */
void memory_region_set_dirty(MemoryRegion *mr, hwaddr addr,
                             hwaddr size);

/**
 * memory_region_clear_dirty_bitmap - clear dirty bitmap for memory range
 *
 * This function is called when the caller wants to clear the remote
 * dirty bitmap of a memory range within the memory region.  This can
 * be used by e.g. KVM to manually clear dirty log when
 * KVM_CAP_MANUAL_DIRTY_LOG_PROTECT is declared support by the host
 * kernel.
 *
 * @mr:     the memory region to clear the dirty log upon
 * @start:  start address offset within the memory region
 * @len:    length of the memory region to clear dirty bitmap
 */
void memory_region_clear_dirty_bitmap(MemoryRegion *mr, hwaddr start,
                                      hwaddr len);

/**
 * memory_region_snapshot_and_clear_dirty: Get a snapshot of the dirty
 *                                         bitmap and clear it.
 *
 * Creates a snapshot of the dirty bitmap, clears the dirty bitmap and
 * returns the snapshot.  The snapshot can then be used to query dirty
 * status, using memory_region_snapshot_get_dirty.  Snapshotting allows
 * querying the same page multiple times, which is especially useful for
 * display updates where the scanlines often are not page aligned.
 *
 * The dirty bitmap region which gets copied into the snapshot (and
 * cleared afterwards) can be larger than requested.  The boundaries
 * are rounded up/down so complete bitmap longs (covering 64 pages on
 * 64bit hosts) can be copied over into the bitmap snapshot.  Which
 * isn't a problem for display updates as the extra pages are outside
 * the visible area, and in case the visible area changes a full
 * display redraw is due anyway.  Should other use cases for this
 * function emerge we might have to revisit this implementation
 * detail.
 *
 * Use g_free to release DirtyBitmapSnapshot.
 *
 * @mr: the memory region being queried.
 * @addr: the address (relative to the start of the region) being queried.
 * @size: the size of the range being queried.
 * @client: the user of the logging information; typically %DIRTY_MEMORY_VGA.
 */
DirtyBitmapSnapshot *memory_region_snapshot_and_clear_dirty(MemoryRegion *mr,
                                                            hwaddr addr,
                                                            hwaddr size,
                                                            unsigned client);

/**
 * memory_region_snapshot_get_dirty: Check whether a range of bytes is dirty
 *                                   in the specified dirty bitmap snapshot.
 *
 * @mr: the memory region being queried.
 * @snap: the dirty bitmap snapshot
 * @addr: the address (relative to the start of the region) being queried.
 * @size: the size of the range being queried.
 */
bool memory_region_snapshot_get_dirty(MemoryRegion *mr,
                                      DirtyBitmapSnapshot *snap,
                                      hwaddr addr, hwaddr size);

/**
 * memory_region_reset_dirty: Mark a range of pages as clean, for a specified
 *                            client.
 *
 * Marks a range of pages as no longer dirty.
 *
 * @mr: the region being updated.
 * @addr: the start of the subrange being cleaned.
 * @size: the size of the subrange being cleaned.
 * @client: the user of the logging information; %DIRTY_MEMORY_MIGRATION or
 *          %DIRTY_MEMORY_VGA.
 */
void memory_region_reset_dirty(MemoryRegion *mr, hwaddr addr,
                               hwaddr size, unsigned client);

/**
 * memory_region_flush_rom_device: Mark a range of pages dirty and invalidate
 *                                 TBs (for self-modifying code).
 *
 * The MemoryRegionOps->write() callback of a ROM device must use this function
 * to mark byte ranges that have been modified internally, such as by directly
 * accessing the memory returned by memory_region_get_ram_ptr().
 *
 * This function marks the range dirty and invalidates TBs so that TCG can
 * detect self-modifying code.
 *
 * @mr: the region being flushed.
 * @addr: the start, relative to the start of the region, of the range being
 *        flushed.
 * @size: the size, in bytes, of the range being flushed.
 */
void memory_region_flush_rom_device(MemoryRegion *mr, hwaddr addr, hwaddr size);

/**
 * memory_region_set_readonly: Turn a memory region read-only (or read-write)
 *
 * Allows a memory region to be marked as read-only (turning it into a ROM).
 * only useful on RAM regions.
 *
 * @mr: the region being updated.
 * @readonly: whether rhe region is to be ROM or RAM.
 */
void memory_region_set_readonly(MemoryRegion *mr, bool readonly);

/**
 * memory_region_set_nonvolatile: Turn a memory region non-volatile
 *
 * Allows a memory region to be marked as non-volatile.
 * only useful on RAM regions.
 *
 * @mr: the region being updated.
 * @nonvolatile: whether rhe region is to be non-volatile.
 */
void memory_region_set_nonvolatile(MemoryRegion *mr, bool nonvolatile);

/**
 * memory_region_rom_device_set_romd: enable/disable ROMD mode
 *
 * Allows a ROM device (initialized with memory_region_init_rom_device() to
 * set to ROMD mode (default) or MMIO mode.  When it is in ROMD mode, the
 * device is mapped to guest memory and satisfies read access directly.
 * When in MMIO mode, reads are forwarded to the #MemoryRegion.read function.
 * Writes are always handled by the #MemoryRegion.write function.
 *
 * @mr: the memory region to be updated
 * @romd_mode: %true to put the region into ROMD mode
 */
void memory_region_rom_device_set_romd(MemoryRegion *mr, bool romd_mode);

/**
 * memory_region_set_coalescing: Enable memory coalescing for the region.
 *
 * Enabled writes to a region to be queued for later processing. MMIO ->write
 * callbacks may be delayed until a non-coalesced MMIO is issued.
 * Only useful for IO regions.  Roughly similar to write-combining hardware.
 *
 * @mr: the memory region to be write coalesced
 */
void memory_region_set_coalescing(MemoryRegion *mr);

/**
 * memory_region_add_coalescing: Enable memory coalescing for a sub-range of
 *                               a region.
 *
 * Like memory_region_set_coalescing(), but works on a sub-range of a region.
 * Multiple calls can be issued coalesced disjoint ranges.
 *
 * @mr: the memory region to be updated.
 * @offset: the start of the range within the region to be coalesced.
 * @size: the size of the subrange to be coalesced.
 */
void memory_region_add_coalescing(MemoryRegion *mr,
                                  hwaddr offset,
                                  uint64_t size);

/**
 * memory_region_clear_coalescing: Disable MMIO coalescing for the region.
 *
 * Disables any coalescing caused by memory_region_set_coalescing() or
 * memory_region_add_coalescing().  Roughly equivalent to uncacheble memory
 * hardware.
 *
 * @mr: the memory region to be updated.
 */
void memory_region_clear_coalescing(MemoryRegion *mr);

/**
 * memory_region_set_flush_coalesced: Enforce memory coalescing flush before
 *                                    accesses.
 *
 * Ensure that pending coalesced MMIO request are flushed before the memory
 * region is accessed. This property is automatically enabled for all regions
 * passed to memory_region_set_coalescing() and memory_region_add_coalescing().
 *
 * @mr: the memory region to be updated.
 */
void memory_region_set_flush_coalesced(MemoryRegion *mr);

/**
 * memory_region_clear_flush_coalesced: Disable memory coalescing flush before
 *                                      accesses.
 *
 * Clear the automatic coalesced MMIO flushing enabled via
 * memory_region_set_flush_coalesced. Note that this service has no effect on
 * memory regions that have MMIO coalescing enabled for themselves. For them,
 * automatic flushing will stop once coalescing is disabled.
 *
 * @mr: the memory region to be updated.
 */
void memory_region_clear_flush_coalesced(MemoryRegion *mr);

/**
 * memory_region_add_eventfd: Request an eventfd to be triggered when a word
 *                            is written to a location.
 *
 * Marks a word in an IO region (initialized with memory_region_init_io())
 * as a trigger for an eventfd event.  The I/O callback will not be called.
 * The caller must be prepared to handle failure (that is, take the required
 * action if the callback _is_ called).
 *
 * @mr: the memory region being updated.
 * @addr: the address within @mr that is to be monitored
 * @size: the size of the access to trigger the eventfd
 * @match_data: whether to match against @data, instead of just @addr
 * @data: the data to match against the guest write
 * @e: event notifier to be triggered when @addr, @size, and @data all match.
 **/
void memory_region_add_eventfd(MemoryRegion *mr,
                               hwaddr addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               EventNotifier *e);

/**
 * memory_region_del_eventfd: Cancel an eventfd.
 *
 * Cancels an eventfd trigger requested by a previous
 * memory_region_add_eventfd() call.
 *
 * @mr: the memory region being updated.
 * @addr: the address within @mr that is to be monitored
 * @size: the size of the access to trigger the eventfd
 * @match_data: whether to match against @data, instead of just @addr
 * @data: the data to match against the guest write
 * @e: event notifier to be triggered when @addr, @size, and @data all match.
 */
void memory_region_del_eventfd(MemoryRegion *mr,
                               hwaddr addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               EventNotifier *e);

/**
 * memory_region_add_subregion: Add a subregion to a container.
 *
 * Adds a subregion at @offset.  The subregion may not overlap with other
 * subregions (except for those explicitly marked as overlapping).  A region
 * may only be added once as a subregion (unless removed with
 * memory_region_del_subregion()); use memory_region_init_alias() if you
 * want a region to be a subregion in multiple locations.
 *
 * @mr: the region to contain the new subregion; must be a container
 *      initialized with memory_region_init().
 * @offset: the offset relative to @mr where @subregion is added.
 * @subregion: the subregion to be added.
 */
void memory_region_add_subregion(MemoryRegion *mr,
                                 hwaddr offset,
                                 MemoryRegion *subregion);
/**
 * memory_region_add_subregion_overlap: Add a subregion to a container
 *                                      with overlap.
 *
 * Adds a subregion at @offset.  The subregion may overlap with other
 * subregions.  Conflicts are resolved by having a higher @priority hide a
 * lower @priority. Subregions without priority are taken as @priority 0.
 * A region may only be added once as a subregion (unless removed with
 * memory_region_del_subregion()); use memory_region_init_alias() if you
 * want a region to be a subregion in multiple locations.
 *
 * @mr: the region to contain the new subregion; must be a container
 *      initialized with memory_region_init().
 * @offset: the offset relative to @mr where @subregion is added.
 * @subregion: the subregion to be added.
 * @priority: used for resolving overlaps; highest priority wins.
 */
void memory_region_add_subregion_overlap(MemoryRegion *mr,
                                         hwaddr offset,
                                         MemoryRegion *subregion,
                                         int priority);

/**
 * memory_region_get_ram_addr: Get the ram address associated with a memory
 *                             region
 *
 * @mr: the region to be queried
 */
ram_addr_t memory_region_get_ram_addr(MemoryRegion *mr);

uint64_t memory_region_get_alignment(const MemoryRegion *mr);
/**
 * memory_region_del_subregion: Remove a subregion.
 *
 * Removes a subregion from its container.
 *
 * @mr: the container to be updated.
 * @subregion: the region being removed; must be a current subregion of @mr.
 */
void memory_region_del_subregion(MemoryRegion *mr,
                                 MemoryRegion *subregion);

/*
 * memory_region_set_enabled: dynamically enable or disable a region
 *
 * Enables or disables a memory region.  A disabled memory region
 * ignores all accesses to itself and its subregions.  It does not
 * obscure sibling subregions with lower priority - it simply behaves as
 * if it was removed from the hierarchy.
 *
 * Regions default to being enabled.
 *
 * @mr: the region to be updated
 * @enabled: whether to enable or disable the region
 */
void memory_region_set_enabled(MemoryRegion *mr, bool enabled);

/*
 * memory_region_set_address: dynamically update the address of a region
 *
 * Dynamically updates the address of a region, relative to its container.
 * May be used on regions are currently part of a memory hierarchy.
 *
 * @mr: the region to be updated
 * @addr: new address, relative to container region
 */
void memory_region_set_address(MemoryRegion *mr, hwaddr addr);

/*
 * memory_region_set_size: dynamically update the size of a region.
 *
 * Dynamically updates the size of a region.
 *
 * @mr: the region to be updated
 * @size: used size of the region.
 */
void memory_region_set_size(MemoryRegion *mr, uint64_t size);

/*
 * memory_region_set_alias_offset: dynamically update a memory alias's offset
 *
 * Dynamically updates the offset into the target region that an alias points
 * to, as if the fourth argument to memory_region_init_alias() has changed.
 *
 * @mr: the #MemoryRegion to be updated; should be an alias.
 * @offset: the new offset into the target memory region
 */
void memory_region_set_alias_offset(MemoryRegion *mr,
                                    hwaddr offset);

/**
 * memory_region_present: checks if an address relative to a @container
 * translates into #MemoryRegion within @container
 *
 * Answer whether a #MemoryRegion within @container covers the address
 * @addr.
 *
 * @container: a #MemoryRegion within which @addr is a relative address
 * @addr: the area within @container to be searched
 */
bool memory_region_present(MemoryRegion *container, hwaddr addr);

/**
 * memory_region_is_mapped: returns true if #MemoryRegion is mapped
 * into another memory region, which does not necessarily imply that it is
 * mapped into an address space.
 *
 * @mr: a #MemoryRegion which should be checked if it's mapped
 */
bool memory_region_is_mapped(MemoryRegion *mr);

/**
 * memory_region_get_ram_discard_manager: get the #RamDiscardManager for a
 * #MemoryRegion
 *
 * The #RamDiscardManager cannot change while a memory region is mapped.
 *
 * @mr: the #MemoryRegion
 */
RamDiscardManager *memory_region_get_ram_discard_manager(MemoryRegion *mr);

/**
 * memory_region_has_ram_discard_manager: check whether a #MemoryRegion has a
 * #RamDiscardManager assigned
 *
 * @mr: the #MemoryRegion
 */
static inline bool memory_region_has_ram_discard_manager(MemoryRegion *mr)
{
    return !!memory_region_get_ram_discard_manager(mr);
}

/**
 * memory_region_set_ram_discard_manager: set the #RamDiscardManager for a
 * #MemoryRegion
 *
 * This function must not be called for a mapped #MemoryRegion, a #MemoryRegion
 * that does not cover RAM, or a #MemoryRegion that already has a
 * #RamDiscardManager assigned.
 *
 * @mr: the #MemoryRegion
 * @rdm: #RamDiscardManager to set
 */
void memory_region_set_ram_discard_manager(MemoryRegion *mr,
                                           RamDiscardManager *rdm);

/**
 * memory_region_find: translate an address/size relative to a
 * MemoryRegion into a #MemoryRegionSection.
 *
 * Locates the first #MemoryRegion within @mr that overlaps the range
 * given by @addr and @size.
 *
 * Returns a #MemoryRegionSection that describes a contiguous overlap.
 * It will have the following characteristics:
 * - @size = 0 iff no overlap was found
 * - @mr is non-%NULL iff an overlap was found
 *
 * Remember that in the return value the @offset_within_region is
 * relative to the returned region (in the .@mr field), not to the
 * @mr argument.
 *
 * Similarly, the .@offset_within_address_space is relative to the
 * address space that contains both regions, the passed and the
 * returned one.  However, in the special case where the @mr argument
 * has no container (and thus is the root of the address space), the
 * following will hold:
 * - @offset_within_address_space >= @addr
 * - @offset_within_address_space + .@size <= @addr + @size
 *
 * @mr: a MemoryRegion within which @addr is a relative address
 * @addr: start of the area within @as to be searched
 * @size: size of the area to be searched
 */
MemoryRegionSection memory_region_find(MemoryRegion *mr,
                                       hwaddr addr, uint64_t size);

/**
 * memory_global_dirty_log_sync: synchronize the dirty log for all memory
 *
 * Synchronizes the dirty page log for all address spaces.
 *
 * @last_stage: whether this is the last stage of live migration
 */
void memory_global_dirty_log_sync(bool last_stage);

/**
 * memory_global_dirty_log_sync: synchronize the dirty log for all memory
 *
 * Synchronizes the vCPUs with a thread that is reading the dirty bitmap.
 * This function must be called after the dirty log bitmap is cleared, and
 * before dirty guest memory pages are read.  If you are using
 * #DirtyBitmapSnapshot, memory_region_snapshot_and_clear_dirty() takes
 * care of doing this.
 */
void memory_global_after_dirty_log_sync(void);

/**
 * memory_region_transaction_begin: Start a transaction.
 *
 * During a transaction, changes will be accumulated and made visible
 * only when the transaction ends (is committed).
 */
void memory_region_transaction_begin(void);

/**
 * memory_region_transaction_commit: Commit a transaction and make changes
 *                                   visible to the guest.
 */
void memory_region_transaction_commit(void);

/**
 * memory_listener_register: register callbacks to be called when memory
 *                           sections are mapped or unmapped into an address
 *                           space
 *
 * @listener: an object containing the callbacks to be called
 * @filter: if non-%NULL, only regions in this address space will be observed
 */
void memory_listener_register(MemoryListener *listener, AddressSpace *filter);

/**
 * memory_listener_unregister: undo the effect of memory_listener_register()
 *
 * @listener: an object containing the callbacks to be removed
 */
void memory_listener_unregister(MemoryListener *listener);

/**
 * memory_global_dirty_log_start: begin dirty logging for all regions
 *
 * @flags: purpose of starting dirty log, migration or dirty rate
 */
void memory_global_dirty_log_start(unsigned int flags);

/**
 * memory_global_dirty_log_stop: end dirty logging for all regions
 *
 * @flags: purpose of stopping dirty log, migration or dirty rate
 */
void memory_global_dirty_log_stop(unsigned int flags);

void mtree_info(bool flatview, bool dispatch_tree, bool owner, bool disabled);

bool memory_region_access_valid(MemoryRegion *mr, hwaddr addr,
                                unsigned size, bool is_write,
                                MemTxAttrs attrs);

/**
 * memory_region_dispatch_read: perform a read directly to the specified
 * MemoryRegion.
 *
 * @mr: #MemoryRegion to access
 * @addr: address within that region
 * @pval: pointer to uint64_t which the data is written to
 * @op: size, sign, and endianness of the memory operation
 * @attrs: memory transaction attributes to use for the access
 */
MemTxResult memory_region_dispatch_read(MemoryRegion *mr,
                                        hwaddr addr,
                                        uint64_t *pval,
                                        MemOp op,
                                        MemTxAttrs attrs);
/**
 * memory_region_dispatch_write: perform a write directly to the specified
 * MemoryRegion.
 *
 * @mr: #MemoryRegion to access
 * @addr: address within that region
 * @data: data to write
 * @op: size, sign, and endianness of the memory operation
 * @attrs: memory transaction attributes to use for the access
 */
MemTxResult memory_region_dispatch_write(MemoryRegion *mr,
                                         hwaddr addr,
                                         uint64_t data,
                                         MemOp op,
                                         MemTxAttrs attrs);

/**
 * address_space_init: initializes an address space
 *
 * @as: an uninitialized #AddressSpace
 * @root: a #MemoryRegion that routes addresses for the address space
 * @name: an address space name.  The name is only used for debugging
 *        output.
 */
void address_space_init(AddressSpace *as, MemoryRegion *root, const char *name);

/**
 * address_space_destroy: destroy an address space
 *
 * Releases all resources associated with an address space.  After an address space
 * is destroyed, its root memory region (given by address_space_init()) may be destroyed
 * as well.
 *
 * @as: address space to be destroyed
 */
void address_space_destroy(AddressSpace *as);

/**
 * address_space_remove_listeners: unregister all listeners of an address space
 *
 * Removes all callbacks previously registered with memory_listener_register()
 * for @as.
 *
 * @as: an initialized #AddressSpace
 */
void address_space_remove_listeners(AddressSpace *as);

/**
 * address_space_rw: read from or write to an address space.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @attrs: memory transaction attributes
 * @buf: buffer with the data transferred
 * @len: the number of bytes to read or write
 * @is_write: indicates the transfer direction
 */
MemTxResult address_space_rw(AddressSpace *as, hwaddr addr,
                             MemTxAttrs attrs, void *buf,
                             hwaddr len, bool is_write);

/**
 * address_space_write: write to address space.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @attrs: memory transaction attributes
 * @buf: buffer with the data transferred
 * @len: the number of bytes to write
 */
MemTxResult address_space_write(AddressSpace *as, hwaddr addr,
                                MemTxAttrs attrs,
                                const void *buf, hwaddr len);

/**
 * address_space_write_rom: write to address space, including ROM.
 *
 * This function writes to the specified address space, but will
 * write data to both ROM and RAM. This is used for non-guest
 * writes like writes from the gdb debug stub or initial loading
 * of ROM contents.
 *
 * Note that portions of the write which attempt to write data to
 * a device will be silently ignored -- only real RAM and ROM will
 * be written to.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @attrs: memory transaction attributes
 * @buf: buffer with the data transferred
 * @len: the number of bytes to write
 */
MemTxResult address_space_write_rom(AddressSpace *as, hwaddr addr,
                                    MemTxAttrs attrs,
                                    const void *buf, hwaddr len);

/* address_space_ld*: load from an address space
 * address_space_st*: store to an address space
 *
 * These functions perform a load or store of the byte, word,
 * longword or quad to the specified address within the AddressSpace.
 * The _le suffixed functions treat the data as little endian;
 * _be indicates big endian; no suffix indicates "same endianness
 * as guest CPU".
 *
 * The "guest CPU endianness" accessors are deprecated for use outside
 * target-* code; devices should be CPU-agnostic and use either the LE
 * or the BE accessors.
 *
 * @as #AddressSpace to be accessed
 * @addr: address within that address space
 * @val: data value, for stores
 * @attrs: memory transaction attributes
 * @result: location to write the success/failure of the transaction;
 *   if NULL, this information is discarded
 */

#define SUFFIX
#define ARG1         as
#define ARG1_DECL    AddressSpace *as
#include "exec/memory_ldst.h.inc"

#define SUFFIX
#define ARG1         as
#define ARG1_DECL    AddressSpace *as
#include "exec/memory_ldst_phys.h.inc"

struct MemoryRegionCache {
    void *ptr;
    hwaddr xlat;
    hwaddr len;
    FlatView *fv;
    MemoryRegionSection mrs;
    bool is_write;
};

#define MEMORY_REGION_CACHE_INVALID ((MemoryRegionCache) { .mrs.mr = NULL })


/* address_space_ld*_cached: load from a cached #MemoryRegion
 * address_space_st*_cached: store into a cached #MemoryRegion
 *
 * These functions perform a load or store of the byte, word,
 * longword or quad to the specified address.  The address is
 * a physical address in the AddressSpace, but it must lie within
 * a #MemoryRegion that was mapped with address_space_cache_init.
 *
 * The _le suffixed functions treat the data as little endian;
 * _be indicates big endian; no suffix indicates "same endianness
 * as guest CPU".
 *
 * The "guest CPU endianness" accessors are deprecated for use outside
 * target-* code; devices should be CPU-agnostic and use either the LE
 * or the BE accessors.
 *
 * @cache: previously initialized #MemoryRegionCache to be accessed
 * @addr: address within the address space
 * @val: data value, for stores
 * @attrs: memory transaction attributes
 * @result: location to write the success/failure of the transaction;
 *   if NULL, this information is discarded
 */

#define SUFFIX       _cached_slow
#define ARG1         cache
#define ARG1_DECL    MemoryRegionCache *cache
#include "exec/memory_ldst.h.inc"

/* Inline fast path for direct RAM access.  */
static inline uint8_t address_space_ldub_cached(MemoryRegionCache *cache,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len);
    if (likely(cache->ptr)) {
        return ldub_p(cache->ptr + addr);
    } else {
        return address_space_ldub_cached_slow(cache, addr, attrs, result);
    }
}

static inline void address_space_stb_cached(MemoryRegionCache *cache,
    hwaddr addr, uint8_t val, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len);
    if (likely(cache->ptr)) {
        stb_p(cache->ptr + addr, val);
    } else {
        address_space_stb_cached_slow(cache, addr, val, attrs, result);
    }
}

#define ENDIANNESS   _le
#include "exec/memory_ldst_cached.h.inc"

#define ENDIANNESS   _be
#include "exec/memory_ldst_cached.h.inc"

#define SUFFIX       _cached
#define ARG1         cache
#define ARG1_DECL    MemoryRegionCache *cache
#include "exec/memory_ldst_phys.h.inc"

/* address_space_cache_init: prepare for repeated access to a physical
 * memory region
 *
 * @cache: #MemoryRegionCache to be filled
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @len: length of buffer
 * @is_write: indicates the transfer direction
 *
 * Will only work with RAM, and may map a subset of the requested range by
 * returning a value that is less than @len.  On failure, return a negative
 * errno value.
 *
 * Because it only works with RAM, this function can be used for
 * read-modify-write operations.  In this case, is_write should be %true.
 *
 * Note that addresses passed to the address_space_*_cached functions
 * are relative to @addr.
 */
int64_t address_space_cache_init(MemoryRegionCache *cache,
                                 AddressSpace *as,
                                 hwaddr addr,
                                 hwaddr len,
                                 bool is_write);

/**
 * address_space_cache_invalidate: complete a write to a #MemoryRegionCache
 *
 * @cache: The #MemoryRegionCache to operate on.
 * @addr: The first physical address that was written, relative to the
 * address that was passed to @address_space_cache_init.
 * @access_len: The number of bytes that were written starting at @addr.
 */
void address_space_cache_invalidate(MemoryRegionCache *cache,
                                    hwaddr addr,
                                    hwaddr access_len);

/**
 * address_space_cache_destroy: free a #MemoryRegionCache
 *
 * @cache: The #MemoryRegionCache whose memory should be released.
 */
void address_space_cache_destroy(MemoryRegionCache *cache);

/* address_space_get_iotlb_entry: translate an address into an IOTLB
 * entry. Should be called from an RCU critical section.
 */
IOMMUTLBEntry address_space_get_iotlb_entry(AddressSpace *as, hwaddr addr,
                                            bool is_write, MemTxAttrs attrs);

/* address_space_translate: translate an address range into an address space
 * into a MemoryRegion and an address range into that section.  Should be
 * called from an RCU critical section, to avoid that the last reference
 * to the returned region disappears after address_space_translate returns.
 *
 * @fv: #FlatView to be accessed
 * @addr: address within that address space
 * @xlat: pointer to address within the returned memory region section's
 * #MemoryRegion.
 * @len: pointer to length
 * @is_write: indicates the transfer direction
 * @attrs: memory attributes
 */
MemoryRegion *flatview_translate(FlatView *fv,
                                 hwaddr addr, hwaddr *xlat,
                                 hwaddr *len, bool is_write,
                                 MemTxAttrs attrs);

static inline MemoryRegion *address_space_translate(AddressSpace *as,
                                                    hwaddr addr, hwaddr *xlat,
                                                    hwaddr *len, bool is_write,
                                                    MemTxAttrs attrs)
{
    return flatview_translate(address_space_to_flatview(as),
                              addr, xlat, len, is_write, attrs);
}

/* address_space_access_valid: check for validity of accessing an address
 * space range
 *
 * Check whether memory is assigned to the given address space range, and
 * access is permitted by any IOMMU regions that are active for the address
 * space.
 *
 * For now, addr and len should be aligned to a page size.  This limitation
 * will be lifted in the future.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @len: length of the area to be checked
 * @is_write: indicates the transfer direction
 * @attrs: memory attributes
 */
bool address_space_access_valid(AddressSpace *as, hwaddr addr, hwaddr len,
                                bool is_write, MemTxAttrs attrs);

/* address_space_map: map a physical memory region into a host virtual address
 *
 * May map a subset of the requested range, given by and returned in @plen.
 * May return %NULL and set *@plen to zero(0), if resources needed to perform
 * the mapping are exhausted.
 * Use only for reads OR writes - not for read-modify-write operations.
 * Use cpu_register_map_client() to know when retrying the map operation is
 * likely to succeed.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @plen: pointer to length of buffer; updated on return
 * @is_write: indicates the transfer direction
 * @attrs: memory attributes
 */
void *address_space_map(AddressSpace *as, hwaddr addr,
                        hwaddr *plen, bool is_write, MemTxAttrs attrs);

/* address_space_unmap: Unmaps a memory region previously mapped by address_space_map()
 *
 * Will also mark the memory as dirty if @is_write == %true.  @access_len gives
 * the amount of memory that was actually read or written by the caller.
 *
 * @as: #AddressSpace used
 * @buffer: host pointer as returned by address_space_map()
 * @len: buffer length as returned by address_space_map()
 * @access_len: amount of data actually transferred
 * @is_write: indicates the transfer direction
 */
void address_space_unmap(AddressSpace *as, void *buffer, hwaddr len,
                         bool is_write, hwaddr access_len);


/* Internal functions, part of the implementation of address_space_read.  */
MemTxResult address_space_read_full(AddressSpace *as, hwaddr addr,
                                    MemTxAttrs attrs, void *buf, hwaddr len);
MemTxResult flatview_read_continue(FlatView *fv, hwaddr addr,
                                   MemTxAttrs attrs, void *buf,
                                   hwaddr len, hwaddr addr1, hwaddr l,
                                   MemoryRegion *mr);
void *qemu_map_ram_ptr(RAMBlock *ram_block, ram_addr_t addr);

/* Internal functions, part of the implementation of address_space_read_cached
 * and address_space_write_cached.  */
MemTxResult address_space_read_cached_slow(MemoryRegionCache *cache,
                                           hwaddr addr, void *buf, hwaddr len);
MemTxResult address_space_write_cached_slow(MemoryRegionCache *cache,
                                            hwaddr addr, const void *buf,
                                            hwaddr len);

int memory_access_size(MemoryRegion *mr, unsigned l, hwaddr addr);
bool prepare_mmio_access(MemoryRegion *mr);

static inline bool memory_access_is_direct(MemoryRegion *mr, bool is_write)
{
    if (is_write) {
        return memory_region_is_ram(mr) && !mr->readonly &&
               !mr->rom_device && !memory_region_is_ram_device(mr);
    } else {
        return (memory_region_is_ram(mr) && !memory_region_is_ram_device(mr)) ||
               memory_region_is_romd(mr);
    }
}

/**
 * address_space_read: read from an address space.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).  Called within RCU critical section.
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @attrs: memory transaction attributes
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
static inline __attribute__((__always_inline__))
MemTxResult address_space_read(AddressSpace *as, hwaddr addr,
                               MemTxAttrs attrs, void *buf,
                               hwaddr len)
{
    MemTxResult result = MEMTX_OK;
    hwaddr l, addr1;
    void *ptr;
    MemoryRegion *mr;
    FlatView *fv;

    if (__builtin_constant_p(len)) {
        if (len) {
            RCU_READ_LOCK_GUARD();
            fv = address_space_to_flatview(as);
            l = len;
            mr = flatview_translate(fv, addr, &addr1, &l, false, attrs);
            if (len == l && memory_access_is_direct(mr, false)) {
                ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
                memcpy(buf, ptr, len);
            } else {
                result = flatview_read_continue(fv, addr, attrs, buf, len,
                                                addr1, l, mr);
            }
        }
    } else {
        result = address_space_read_full(as, addr, attrs, buf, len);
    }
    return result;
}

/**
 * address_space_read_cached: read from a cached RAM region
 *
 * @cache: Cached region to be addressed
 * @addr: address relative to the base of the RAM region
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
static inline MemTxResult
address_space_read_cached(MemoryRegionCache *cache, hwaddr addr,
                          void *buf, hwaddr len)
{
    assert(addr < cache->len && len <= cache->len - addr);
    fuzz_dma_read_cb(cache->xlat + addr, len, cache->mrs.mr);
    if (likely(cache->ptr)) {
        memcpy(buf, cache->ptr + addr, len);
        return MEMTX_OK;
    } else {
        return address_space_read_cached_slow(cache, addr, buf, len);
    }
}

/**
 * address_space_write_cached: write to a cached RAM region
 *
 * @cache: Cached region to be addressed
 * @addr: address relative to the base of the RAM region
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
static inline MemTxResult
address_space_write_cached(MemoryRegionCache *cache, hwaddr addr,
                           const void *buf, hwaddr len)
{
    assert(addr < cache->len && len <= cache->len - addr);
    if (likely(cache->ptr)) {
        memcpy(cache->ptr + addr, buf, len);
        return MEMTX_OK;
    } else {
        return address_space_write_cached_slow(cache, addr, buf, len);
    }
}

/**
 * address_space_set: Fill address space with a constant byte.
 *
 * Return a MemTxResult indicating whether the operation succeeded
 * or failed (eg unassigned memory, device rejected the transaction,
 * IOMMU fault).
 *
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @c: constant byte to fill the memory
 * @len: the number of bytes to fill with the constant byte
 * @attrs: memory transaction attributes
 */
MemTxResult address_space_set(AddressSpace *as, hwaddr addr,
                              uint8_t c, hwaddr len, MemTxAttrs attrs);

#ifdef NEED_CPU_H
/* enum device_endian to MemOp.  */
static inline MemOp devend_memop(enum device_endian end)
{
    QEMU_BUILD_BUG_ON(DEVICE_HOST_ENDIAN != DEVICE_LITTLE_ENDIAN &&
                      DEVICE_HOST_ENDIAN != DEVICE_BIG_ENDIAN);

#if HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN
    /* Swap if non-host endianness or native (target) endianness */
    return (end == DEVICE_HOST_ENDIAN) ? 0 : MO_BSWAP;
#else
    const int non_host_endianness =
        DEVICE_LITTLE_ENDIAN ^ DEVICE_BIG_ENDIAN ^ DEVICE_HOST_ENDIAN;

    /* In this case, native (target) endianness needs no swap.  */
    return (end == non_host_endianness) ? MO_BSWAP : 0;
#endif
}
#endif

/*
 * Inhibit technologies that require discarding of pages in RAM blocks, e.g.,
 * to manage the actual amount of memory consumed by the VM (then, the memory
 * provided by RAM blocks might be bigger than the desired memory consumption).
 * This *must* be set if:
 * - Discarding parts of a RAM blocks does not result in the change being
 *   reflected in the VM and the pages getting freed.
 * - All memory in RAM blocks is pinned or duplicated, invaldiating any previous
 *   discards blindly.
 * - Discarding parts of a RAM blocks will result in integrity issues (e.g.,
 *   encrypted VMs).
 * Technologies that only temporarily pin the current working set of a
 * driver are fine, because we don't expect such pages to be discarded
 * (esp. based on guest action like balloon inflation).
 *
 * This is *not* to be used to protect from concurrent discards (esp.,
 * postcopy).
 *
 * Returns 0 if successful. Returns -EBUSY if a technology that relies on
 * discards to work reliably is active.
 */
int ram_block_discard_disable(bool state);

/*
 * See ram_block_discard_disable(): only disable uncoordinated discards,
 * keeping coordinated discards (via the RamDiscardManager) enabled.
 */
int ram_block_uncoordinated_discard_disable(bool state);

/*
 * Inhibit technologies that disable discarding of pages in RAM blocks.
 *
 * Returns 0 if successful. Returns -EBUSY if discards are already set to
 * broken.
 */
int ram_block_discard_require(bool state);

/*
 * See ram_block_discard_require(): only inhibit technologies that disable
 * uncoordinated discarding of pages in RAM blocks, allowing co-existance with
 * technologies that only inhibit uncoordinated discards (via the
 * RamDiscardManager).
 */
int ram_block_coordinated_discard_require(bool state);

/*
 * Test if any discarding of memory in ram blocks is disabled.
 */
bool ram_block_discard_is_disabled(void);

/*
 * Test if any discarding of memory in ram blocks is required to work reliably.
 */
bool ram_block_discard_is_required(void);

#endif

#endif
