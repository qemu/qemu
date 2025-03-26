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

#include "system/memory.h"
#include "qemu/queue.h"
#ifdef CONFIG_LINUX
#include <linux/vfio.h>
#endif
#include "system/system.h"
#include "hw/vfio/vfio-container-base.h"
#include "system/host_iommu_device.h"
#include "system/iommufd.h"

#define VFIO_MSG_PREFIX "vfio %s: "

enum {
    VFIO_DEVICE_TYPE_PCI = 0,
    VFIO_DEVICE_TYPE_PLATFORM = 1,
    VFIO_DEVICE_TYPE_CCW = 2,
    VFIO_DEVICE_TYPE_AP = 3,
};

typedef struct VFIODeviceOps VFIODeviceOps;
typedef struct VFIOMigration VFIOMigration;

typedef struct IOMMUFDBackend IOMMUFDBackend;
typedef struct VFIOIOASHwpt VFIOIOASHwpt;

typedef struct VFIODevice {
    QLIST_ENTRY(VFIODevice) next;
    QLIST_ENTRY(VFIODevice) container_next;
    QLIST_ENTRY(VFIODevice) global_next;
    struct VFIOGroup *group;
    VFIOContainerBase *bcontainer;
    char *sysfsdev;
    char *name;
    DeviceState *dev;
    int fd;
    int type;
    bool mdev;
    bool reset_works;
    bool needs_reset;
    bool no_mmap;
    bool ram_block_discard_allowed;
    OnOffAuto enable_migration;
    OnOffAuto migration_multifd_transfer;
    bool migration_events;
    VFIODeviceOps *ops;
    unsigned int num_irqs;
    unsigned int num_regions;
    unsigned int flags;
    VFIOMigration *migration;
    Error *migration_blocker;
    OnOffAuto pre_copy_dirty_page_tracking;
    OnOffAuto device_dirty_page_tracking;
    bool dirty_pages_supported;
    bool dirty_tracking; /* Protected by BQL */
    bool iommu_dirty_tracking;
    HostIOMMUDevice *hiod;
    int devid;
    IOMMUFDBackend *iommufd;
    VFIOIOASHwpt *hwpt;
    QLIST_ENTRY(VFIODevice) hwpt_next;
} VFIODevice;

struct VFIODeviceOps {
    void (*vfio_compute_needs_reset)(VFIODevice *vdev);
    int (*vfio_hot_reset_multi)(VFIODevice *vdev);
    void (*vfio_eoi)(VFIODevice *vdev);
    Object *(*vfio_get_object)(VFIODevice *vdev);

    /**
     * @vfio_save_config
     *
     * Save device config state
     *
     * @vdev: #VFIODevice for which to save the config
     * @f: #QEMUFile where to send the data
     * @errp: pointer to Error*, to store an error if it happens.
     *
     * Returns zero to indicate success and negative for error
     */
    int (*vfio_save_config)(VFIODevice *vdev, QEMUFile *f, Error **errp);

    /**
     * @vfio_load_config
     *
     * Load device config state
     *
     * @vdev: #VFIODevice for which to load the config
     * @f: #QEMUFile where to get the data
     *
     * Returns zero to indicate success and negative for error
     */
    int (*vfio_load_config)(VFIODevice *vdev, QEMUFile *f);
};


#define TYPE_HOST_IOMMU_DEVICE_LEGACY_VFIO TYPE_HOST_IOMMU_DEVICE "-legacy-vfio"
#define TYPE_HOST_IOMMU_DEVICE_IOMMUFD_VFIO \
            TYPE_HOST_IOMMU_DEVICE_IOMMUFD "-vfio"

VFIOAddressSpace *vfio_get_address_space(AddressSpace *as);
void vfio_put_address_space(VFIOAddressSpace *space);
void vfio_address_space_insert(VFIOAddressSpace *space,
                               VFIOContainerBase *bcontainer);

void vfio_disable_irqindex(VFIODevice *vbasedev, int index);
void vfio_unmask_single_irqindex(VFIODevice *vbasedev, int index);
void vfio_mask_single_irqindex(VFIODevice *vbasedev, int index);
bool vfio_set_irq_signaling(VFIODevice *vbasedev, int index, int subindex,
                            int action, int fd, Error **errp);

void vfio_reset_handler(void *opaque);
struct vfio_device_info *vfio_get_device_info(int fd);
bool vfio_device_is_mdev(VFIODevice *vbasedev);
bool vfio_device_hiod_realize(VFIODevice *vbasedev, Error **errp);
bool vfio_attach_device(char *name, VFIODevice *vbasedev,
                        AddressSpace *as, Error **errp);
void vfio_detach_device(VFIODevice *vbasedev);
VFIODevice *vfio_get_vfio_device(Object *obj);

int vfio_kvm_device_add_fd(int fd, Error **errp);
int vfio_kvm_device_del_fd(int fd, Error **errp);

bool vfio_cpr_register_container(VFIOContainerBase *bcontainer, Error **errp);
void vfio_cpr_unregister_container(VFIOContainerBase *bcontainer);

typedef QLIST_HEAD(VFIODeviceList, VFIODevice) VFIODeviceList;
extern VFIODeviceList vfio_device_list;
extern const MemoryListener vfio_memory_listener;
extern int vfio_kvm_device_fd;

#ifdef CONFIG_LINUX
int vfio_get_region_info(VFIODevice *vbasedev, int index,
                         struct vfio_region_info **info);
int vfio_get_dev_region_info(VFIODevice *vbasedev, uint32_t type,
                             uint32_t subtype, struct vfio_region_info **info);
bool vfio_has_region_cap(VFIODevice *vbasedev, int region, uint16_t cap_type);
struct vfio_info_cap_header *
vfio_get_region_info_cap(struct vfio_region_info *info, uint16_t id);
bool vfio_get_info_dma_avail(struct vfio_iommu_type1_info *info,
                             unsigned int *avail);
struct vfio_info_cap_header *
vfio_get_device_info_cap(struct vfio_device_info *info, uint16_t id);
struct vfio_info_cap_header *
vfio_get_cap(void *ptr, uint32_t cap_offset, uint16_t id);
#endif

int vfio_bitmap_alloc(VFIOBitmap *vbmap, hwaddr size);
bool vfio_devices_all_dirty_tracking_started(
    const VFIOContainerBase *bcontainer);
bool
vfio_devices_all_device_dirty_tracking(const VFIOContainerBase *bcontainer);
int vfio_devices_query_dirty_bitmap(const VFIOContainerBase *bcontainer,
                VFIOBitmap *vbmap, hwaddr iova, hwaddr size, Error **errp);
int vfio_get_dirty_bitmap(const VFIOContainerBase *bcontainer, uint64_t iova,
                          uint64_t size, ram_addr_t ram_addr, Error **errp);

/* Returns 0 on success, or a negative errno. */
bool vfio_device_get_name(VFIODevice *vbasedev, Error **errp);
void vfio_device_set_fd(VFIODevice *vbasedev, const char *str, Error **errp);
void vfio_device_init(VFIODevice *vbasedev, int type, VFIODeviceOps *ops,
                      DeviceState *dev, bool ram_discard);
int vfio_device_get_aw_bits(VFIODevice *vdev);
#endif /* HW_VFIO_VFIO_COMMON_H */
