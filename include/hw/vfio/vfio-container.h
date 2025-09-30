/*
 * VFIO BASE CONTAINER
 *
 * Copyright (C) 2023 Intel Corporation.
 * Copyright Red Hat, Inc. 2023
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Eric Auger <eric.auger@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_CONTAINER_H
#define HW_VFIO_VFIO_CONTAINER_H

#include "system/memory.h"

typedef struct VFIODevice VFIODevice;
typedef struct VFIOIOMMUClass VFIOIOMMUClass;

typedef struct {
    unsigned long *bitmap;
    hwaddr size;
    hwaddr pages;
} VFIOBitmap;

typedef struct VFIOAddressSpace {
    AddressSpace *as;
    QLIST_HEAD(, VFIOContainer) containers;
    QLIST_ENTRY(VFIOAddressSpace) list;
} VFIOAddressSpace;

/*
 * This is the base object for vfio container backends
 */
struct VFIOContainer {
    Object parent_obj;

    VFIOAddressSpace *space;
    MemoryListener listener;
    Error *error;
    bool initialized;
    uint64_t dirty_pgsizes;
    uint64_t max_dirty_bitmap_size;
    unsigned long pgsizes;
    unsigned int dma_max_mappings;
    bool dirty_pages_supported;
    bool dirty_pages_started; /* Protected by BQL */
    QLIST_HEAD(, VFIOGuestIOMMU) giommu_list;
    QLIST_HEAD(, VFIORamDiscardListener) vrdl_list;
    QLIST_ENTRY(VFIOContainer) next;
    QLIST_HEAD(, VFIODevice) device_list;
    GList *iova_ranges;
    NotifierWithReturn cpr_reboot_notifier;
};

#define TYPE_VFIO_IOMMU "vfio-iommu"
OBJECT_DECLARE_TYPE(VFIOContainer, VFIOIOMMUClass, VFIO_IOMMU)

typedef struct VFIOGuestIOMMU {
    VFIOContainer *bcontainer;
    IOMMUMemoryRegion *iommu_mr;
    hwaddr iommu_offset;
    IOMMUNotifier n;
    QLIST_ENTRY(VFIOGuestIOMMU) giommu_next;
} VFIOGuestIOMMU;

typedef struct VFIORamDiscardListener {
    VFIOContainer *bcontainer;
    MemoryRegion *mr;
    hwaddr offset_within_address_space;
    hwaddr size;
    uint64_t granularity;
    RamDiscardListener listener;
    QLIST_ENTRY(VFIORamDiscardListener) next;
} VFIORamDiscardListener;

VFIOAddressSpace *vfio_address_space_get(AddressSpace *as);
void vfio_address_space_put(VFIOAddressSpace *space);
void vfio_address_space_insert(VFIOAddressSpace *space,
                               VFIOContainer *bcontainer);

int vfio_container_dma_map(VFIOContainer *bcontainer,
                           hwaddr iova, uint64_t size,
                           void *vaddr, bool readonly, MemoryRegion *mr);
int vfio_container_dma_unmap(VFIOContainer *bcontainer,
                             hwaddr iova, uint64_t size,
                             IOMMUTLBEntry *iotlb, bool unmap_all);
bool vfio_container_add_section_window(VFIOContainer *bcontainer,
                                       MemoryRegionSection *section,
                                       Error **errp);
void vfio_container_del_section_window(VFIOContainer *bcontainer,
                                       MemoryRegionSection *section);
int vfio_container_set_dirty_page_tracking(VFIOContainer *bcontainer,
                                           bool start, Error **errp);
bool vfio_container_dirty_tracking_is_started(
    const VFIOContainer *bcontainer);
bool vfio_container_devices_dirty_tracking_is_supported(
    const VFIOContainer *bcontainer);
int vfio_container_query_dirty_bitmap(const VFIOContainer *bcontainer,
                                      uint64_t iova, uint64_t size,
                                      hwaddr translated_addr, Error **errp);

GList *vfio_container_get_iova_ranges(const VFIOContainer *bcontainer);

static inline uint64_t
vfio_container_get_page_size_mask(const VFIOContainer *bcontainer)
{
    assert(bcontainer);
    return bcontainer->pgsizes;
}

#define TYPE_VFIO_IOMMU_LEGACY TYPE_VFIO_IOMMU "-legacy"
#define TYPE_VFIO_IOMMU_SPAPR TYPE_VFIO_IOMMU "-spapr"
#define TYPE_VFIO_IOMMU_IOMMUFD TYPE_VFIO_IOMMU "-iommufd"
#define TYPE_VFIO_IOMMU_USER TYPE_VFIO_IOMMU "-user"

struct VFIOIOMMUClass {
    ObjectClass parent_class;

    /**
     * @setup
     *
     * Perform basic setup of the container, including configuring IOMMU
     * capabilities, IOVA ranges, supported page sizes, etc.
     *
     * @bcontainer: #VFIOContainer
     * @errp: pointer to Error*, to store an error if it happens.
     *
     * Returns true to indicate success and false for error.
     */
    bool (*setup)(VFIOContainer *bcontainer, Error **errp);

    /**
     * @listener_begin
     *
     * Called at the beginning of an address space update transaction.
     * See #MemoryListener.
     *
     * @bcontainer: #VFIOContainer
     */
    void (*listener_begin)(VFIOContainer *bcontainer);

    /**
     * @listener_commit
     *
     * Called at the end of an address space update transaction,
     * See #MemoryListener.
     *
     * @bcontainer: #VFIOContainer
     */
    void (*listener_commit)(VFIOContainer *bcontainer);

    /**
     * @dma_map
     *
     * Map an address range into the container. Note that the memory region is
     * referenced within an RCU read lock region across this call.
     *
     * @bcontainer: #VFIOContainer to use
     * @iova: start address to map
     * @size: size of the range to map
     * @vaddr: process virtual address of mapping
     * @readonly: true if mapping should be readonly
     * @mr: the memory region for this mapping
     *
     * Returns 0 to indicate success and -errno otherwise.
     */
    int (*dma_map)(const VFIOContainer *bcontainer,
                   hwaddr iova, uint64_t size,
                   void *vaddr, bool readonly, MemoryRegion *mr);
    /**
     * @dma_map_file
     *
     * Map a file range for the container.
     *
     * @bcontainer: #VFIOContainer to use for map
     * @iova: start address to map
     * @size: size of the range to map
     * @fd: descriptor of the file to map
     * @start: starting file offset of the range to map
     * @readonly: map read only if true
     */
    int (*dma_map_file)(const VFIOContainer *bcontainer,
                        hwaddr iova, uint64_t size,
                        int fd, unsigned long start, bool readonly);
    /**
     * @dma_unmap
     *
     * Unmap an address range from the container.
     *
     * @bcontainer: #VFIOContainer to use for unmap
     * @iova: start address to unmap
     * @size: size of the range to unmap
     * @iotlb: The IOMMU TLB mapping entry (or NULL)
     * @unmap_all: if set, unmap the entire address space
     *
     * Returns 0 to indicate success and -errno otherwise.
     */
    int (*dma_unmap)(const VFIOContainer *bcontainer,
                     hwaddr iova, uint64_t size,
                     IOMMUTLBEntry *iotlb, bool unmap_all);


    /**
     * @attach_device
     *
     * Associate the given device with a container and do some related
     * initialization of the device context.
     *
     * @name: name of the device
     * @vbasedev: the device
     * @as: address space to use
     * @errp: pointer to Error*, to store an error if it happens.
     *
     * Returns true to indicate success and false for error.
     */
    bool (*attach_device)(const char *name, VFIODevice *vbasedev,
                          AddressSpace *as, Error **errp);

    /*
     * @detach_device
     *
     * Detach the given device from its container and clean up any necessary
     * state.
     *
     * @vbasedev: the device to disassociate
     */
    void (*detach_device)(VFIODevice *vbasedev);

    /* migration feature */

    /**
     * @set_dirty_page_tracking
     *
     * Start or stop dirty pages tracking on VFIO container
     *
     * @bcontainer: #VFIOContainer on which to de/activate dirty
     *              page tracking
     * @start: indicates whether to start or stop dirty pages tracking
     * @errp: pointer to Error*, to store an error if it happens.
     *
     * Returns zero to indicate success and negative for error.
     */
    int (*set_dirty_page_tracking)(const VFIOContainer *bcontainer,
                                   bool start, Error **errp);
    /**
     * @query_dirty_bitmap
     *
     * Get bitmap of dirty pages from container
     *
     * @bcontainer: #VFIOContainer from which to get dirty pages
     * @vbmap: #VFIOBitmap internal bitmap structure
     * @iova: iova base address
     * @size: size of iova range
     * @errp: pointer to Error*, to store an error if it happens.
     *
     * Returns zero to indicate success and negative for error.
     */
    int (*query_dirty_bitmap)(const VFIOContainer *bcontainer,
                VFIOBitmap *vbmap, hwaddr iova, hwaddr size, Error **errp);
    /* PCI specific */
    int (*pci_hot_reset)(VFIODevice *vbasedev, bool single);

    /* SPAPR specific */
    bool (*add_window)(VFIOContainer *bcontainer,
                       MemoryRegionSection *section,
                       Error **errp);
    void (*del_window)(VFIOContainer *bcontainer,
                       MemoryRegionSection *section);
    void (*release)(VFIOContainer *bcontainer);
};

VFIORamDiscardListener *vfio_find_ram_discard_listener(
    VFIOContainer *bcontainer, MemoryRegionSection *section);

void vfio_container_region_add(VFIOContainer *bcontainer,
                               MemoryRegionSection *section, bool cpr_remap);

#endif /* HW_VFIO_VFIO_CONTAINER_H */
