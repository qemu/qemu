/*
 * common header for vfio based device assignment support
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Based on qemu-kvm device-assignment:
 *  Adapted for KVM by Qumranet.
 *  Copyright (c) 2007, Neocleus, Alex Novik (alex@neocleus.com)
 *  Copyright (c) 2007, Neocleus, Guy Zana (guy@neocleus.com)
 *  Copyright (C) 2008, Qumranet, Amit Shah (amit.shah@qumranet.com)
 *  Copyright (C) 2008, Red Hat, Amit Shah (amit.shah@redhat.com)
 *  Copyright (C) 2008, IBM, Muli Ben-Yehuda (muli@il.ibm.com)
 */

#ifndef HW_VFIO_VFIO_COMMON_H
#define HW_VFIO_VFIO_COMMON_H

#include "qemu-common.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "qemu/queue.h"
#include "qemu/notify.h"
#ifdef CONFIG_LINUX
#include <linux/vfio.h>
#endif

#define ERR_PREFIX "vfio error: %s: "
#define WARN_PREFIX "vfio warning: %s: "

/*#define DEBUG_VFIO*/
#ifdef DEBUG_VFIO
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "vfio: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

enum {
    VFIO_DEVICE_TYPE_PCI = 0,
    VFIO_DEVICE_TYPE_PLATFORM = 1,
};

typedef struct VFIOMmap {
    MemoryRegion mem;
    void *mmap;
    off_t offset;
    size_t size;
} VFIOMmap;

typedef struct VFIORegion {
    struct VFIODevice *vbasedev;
    off_t fd_offset; /* offset of region within device fd */
    MemoryRegion *mem; /* slow, read/write access */
    size_t size;
    uint32_t flags; /* VFIO region flags (rd/wr/mmap) */
    uint32_t nr_mmaps;
    VFIOMmap *mmaps;
    uint8_t nr; /* cache the region number for debug */
} VFIORegion;

typedef struct VFIOAddressSpace {
    AddressSpace *as;
    QLIST_HEAD(, VFIOContainer) containers;
    QLIST_ENTRY(VFIOAddressSpace) list;
} VFIOAddressSpace;

struct VFIOGroup;

typedef struct VFIOContainer {
    VFIOAddressSpace *space;
    int fd; /* /dev/vfio/vfio, empowered by the attached groups */
    MemoryListener listener;
    MemoryListener prereg_listener;
    unsigned iommu_type;
    int error;
    bool initialized;
    /*
     * This assumes the host IOMMU can support only a single
     * contiguous IOVA window.  We may need to generalize that in
     * future
     */
    QLIST_HEAD(, VFIOGuestIOMMU) giommu_list;
    QLIST_HEAD(, VFIOHostDMAWindow) hostwin_list;
    QLIST_HEAD(, VFIOGroup) group_list;
    QLIST_ENTRY(VFIOContainer) next;
} VFIOContainer;

typedef struct VFIOGuestIOMMU {
    VFIOContainer *container;
    MemoryRegion *iommu;
    hwaddr iommu_offset;
    IOMMUNotifier n;
    QLIST_ENTRY(VFIOGuestIOMMU) giommu_next;
} VFIOGuestIOMMU;

typedef struct VFIOHostDMAWindow {
    hwaddr min_iova;
    hwaddr max_iova;
    uint64_t iova_pgsizes;
    QLIST_ENTRY(VFIOHostDMAWindow) hostwin_next;
} VFIOHostDMAWindow;

typedef struct VFIODeviceOps VFIODeviceOps;

typedef struct VFIODevice {
    QLIST_ENTRY(VFIODevice) next;
    struct VFIOGroup *group;
    char *sysfsdev;
    char *name;
    int fd;
    int type;
    bool reset_works;
    bool needs_reset;
    bool no_mmap;
    VFIODeviceOps *ops;
    unsigned int num_irqs;
    unsigned int num_regions;
    unsigned int flags;
} VFIODevice;

struct VFIODeviceOps {
    void (*vfio_compute_needs_reset)(VFIODevice *vdev);
    int (*vfio_hot_reset_multi)(VFIODevice *vdev);
    void (*vfio_eoi)(VFIODevice *vdev);
};

typedef struct VFIOGroup {
    int fd;
    int groupid;
    VFIOContainer *container;
    QLIST_HEAD(, VFIODevice) device_list;
    QLIST_ENTRY(VFIOGroup) next;
    QLIST_ENTRY(VFIOGroup) container_next;
} VFIOGroup;

void vfio_put_base_device(VFIODevice *vbasedev);
void vfio_disable_irqindex(VFIODevice *vbasedev, int index);
void vfio_unmask_single_irqindex(VFIODevice *vbasedev, int index);
void vfio_mask_single_irqindex(VFIODevice *vbasedev, int index);
void vfio_region_write(void *opaque, hwaddr addr,
                           uint64_t data, unsigned size);
uint64_t vfio_region_read(void *opaque,
                          hwaddr addr, unsigned size);
int vfio_region_setup(Object *obj, VFIODevice *vbasedev, VFIORegion *region,
                      int index, const char *name);
int vfio_region_mmap(VFIORegion *region);
void vfio_region_mmaps_set_enabled(VFIORegion *region, bool enabled);
void vfio_region_exit(VFIORegion *region);
void vfio_region_finalize(VFIORegion *region);
void vfio_reset_handler(void *opaque);
VFIOGroup *vfio_get_group(int groupid, AddressSpace *as, Error **errp);
void vfio_put_group(VFIOGroup *group);
int vfio_get_device(VFIOGroup *group, const char *name,
                    VFIODevice *vbasedev, Error **errp);

extern const MemoryRegionOps vfio_region_ops;
extern QLIST_HEAD(vfio_group_head, VFIOGroup) vfio_group_list;
extern QLIST_HEAD(vfio_as_head, VFIOAddressSpace) vfio_address_spaces;

#ifdef CONFIG_LINUX
int vfio_get_region_info(VFIODevice *vbasedev, int index,
                         struct vfio_region_info **info);
int vfio_get_dev_region_info(VFIODevice *vbasedev, uint32_t type,
                             uint32_t subtype, struct vfio_region_info **info);
#endif
extern const MemoryListener vfio_prereg_listener;

int vfio_spapr_create_window(VFIOContainer *container,
                             MemoryRegionSection *section,
                             hwaddr *pgsize);
int vfio_spapr_remove_window(VFIOContainer *container,
                             hwaddr offset_within_address_space);

#endif /* HW_VFIO_VFIO_COMMON_H */
