/*
 * VFIO Device interface
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

#ifndef HW_VFIO_VFIO_DEVICE_H
#define HW_VFIO_VFIO_DEVICE_H

#include "system/memory.h"
#include "qemu/queue.h"
#ifdef CONFIG_LINUX
#include <linux/vfio.h>
#endif
#include "system/system.h"
#include "hw/vfio/vfio-container.h"
#include "hw/vfio/vfio-cpr.h"
#include "system/host_iommu_device.h"
#include "system/iommufd.h"

#define VFIO_MSG_PREFIX "vfio %s: "

enum {
    VFIO_DEVICE_TYPE_PCI = 0,
    VFIO_DEVICE_TYPE_UNUSED = 1,
    VFIO_DEVICE_TYPE_CCW = 2,
    VFIO_DEVICE_TYPE_AP = 3,
};

typedef struct VFIODeviceOps VFIODeviceOps;
typedef struct VFIODeviceIOOps VFIODeviceIOOps;
typedef struct VFIOMigration VFIOMigration;

typedef struct IOMMUFDBackend IOMMUFDBackend;
typedef struct VFIOIOASHwpt VFIOIOASHwpt;
typedef struct VFIOUserProxy VFIOUserProxy;

typedef struct VFIODevice {
    QLIST_ENTRY(VFIODevice) next;
    QLIST_ENTRY(VFIODevice) container_next;
    QLIST_ENTRY(VFIODevice) global_next;
    struct VFIOGroup *group;
    VFIOContainer *bcontainer;
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
    OnOffAuto migration_load_config_after_iter;
    uint64_t migration_max_queued_buffers_size;
    bool migration_events;
    bool use_region_fds;
    VFIODeviceOps *ops;
    VFIODeviceIOOps *io_ops;
    unsigned int num_irqs;
    unsigned int num_initial_regions;
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
    struct vfio_region_info **reginfo;
    int *region_fds;
    VFIODeviceCPR cpr;
    VFIOUserProxy *proxy;
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

/*
 * Given a return value of either a short number of bytes read or -errno,
 * construct a meaningful error message.
 */
#define strreaderror(ret) \
    (ret < 0 ? strerror(-ret) : "short read")

/*
 * Given a return value of either a short number of bytes written or -errno,
 * construct a meaningful error message.
 */
#define strwriteerror(ret) \
    (ret < 0 ? strerror(-ret) : "short write")

void vfio_device_irq_disable(VFIODevice *vbasedev, int index);
void vfio_device_irq_unmask(VFIODevice *vbasedev, int index);
void vfio_device_irq_mask(VFIODevice *vbasedev, int index);
bool vfio_device_irq_set_signaling(VFIODevice *vbasedev, int index, int subindex,
                                   int action, int fd, Error **errp);

void vfio_device_reset_handler(void *opaque);
bool vfio_device_is_mdev(VFIODevice *vbasedev);
bool vfio_device_hiod_create_and_realize(VFIODevice *vbasedev,
                                         const char *typename, Error **errp);
bool vfio_device_attach(char *name, VFIODevice *vbasedev,
                        AddressSpace *as, Error **errp);
bool vfio_device_attach_by_iommu_type(const char *iommu_type, char *name,
                                      VFIODevice *vbasedev, AddressSpace *as,
                                      Error **errp);
void vfio_device_detach(VFIODevice *vbasedev);
VFIODevice *vfio_get_vfio_device(Object *obj);

typedef QLIST_HEAD(VFIODeviceList, VFIODevice) VFIODeviceList;
extern VFIODeviceList vfio_device_list;

#ifdef CONFIG_LINUX
/*
 * How devices communicate with the server.  The default option is through
 * ioctl() to the kernel VFIO driver, but vfio-user can use a socket to a remote
 * process.
 */
struct VFIODeviceIOOps {
    /**
     * @device_feature
     *
     * Fill in feature info for the given device.
     *
     * @vdev: #VFIODevice to use
     * @feat: feature information to fill in
     *
     * Returns 0 on success or -errno.
     */
    int (*device_feature)(VFIODevice *vdev, struct vfio_device_feature *feat);

    /**
     * @get_region_info
     *
     * Get the information for a given region on the device.
     *
     * @vdev: #VFIODevice to use
     * @info: set @info->index to the region index to look up; the rest of the
     *        struct will be filled in on success
     * @fd: pointer to the fd for the region; will be -1 if not found
     *
     * Returns 0 on success or -errno.
     */
    int (*get_region_info)(VFIODevice *vdev,
                           struct vfio_region_info *info, int *fd);

    /**
     * @get_irq_info
     *
     * @vdev: #VFIODevice to use
     * @irq: set @irq->index to the IRQ index to look up; the rest of the struct
     *       will be filled in on success
     *
     * Returns 0 on success or -errno.
     */
    int (*get_irq_info)(VFIODevice *vdev, struct vfio_irq_info *irq);

    /**
     * @set_irqs
     *
     * Configure IRQs.
     *
     * @vdev: #VFIODevice to use
     * @irqs: IRQ configuration as defined by VFIO docs.
     *
     * Returns 0 on success or -errno.
     */
    int (*set_irqs)(VFIODevice *vdev, struct vfio_irq_set *irqs);

    /**
     * @region_read
     *
     * Read part of a region.
     *
     * @vdev: #VFIODevice to use
     * @nr: region index
     * @off: offset within the region
     * @size: size in bytes to read
     * @data: buffer to read into
     *
     * Returns number of bytes read on success or -errno.
     */
    int (*region_read)(VFIODevice *vdev, uint8_t nr, off_t off, uint32_t size,
                       void *data);

    /**
     * @region_write
     *
     * Write part of a region.
     *
     * @vdev: #VFIODevice to use
     * @nr: region index
     * @off: offset within the region
     * @size: size in bytes to write
     * @data: buffer to write from
     * @post: true if this is a posted write
     *
     * Returns number of bytes write on success or -errno.
     */
    int (*region_write)(VFIODevice *vdev, uint8_t nr, off_t off, uint32_t size,
                        void *data, bool post);
};

void vfio_device_prepare(VFIODevice *vbasedev, VFIOContainer *bcontainer,
                         struct vfio_device_info *info);

void vfio_device_unprepare(VFIODevice *vbasedev);

int vfio_device_get_region_info(VFIODevice *vbasedev, int index,
                                struct vfio_region_info **info);
int vfio_device_get_region_info_type(VFIODevice *vbasedev, uint32_t type,
                                     uint32_t subtype, struct vfio_region_info **info);

/**
 * Return the fd for mapping this region. This is either the device's fd (for
 * e.g. kernel vfio), or a per-region fd (for vfio-user).
 *
 * @vbasedev: #VFIODevice to use
 * @index: region index
 *
 * Returns the fd.
 */
int vfio_device_get_region_fd(VFIODevice *vbasedev, int index);

bool vfio_device_has_region_cap(VFIODevice *vbasedev, int region, uint16_t cap_type);

int vfio_device_get_irq_info(VFIODevice *vbasedev, int index,
                                struct vfio_irq_info *info);
#endif

/* Returns 0 on success, or a negative errno. */
bool vfio_device_get_name(VFIODevice *vbasedev, Error **errp);
void vfio_device_free_name(VFIODevice *vbasedev);
void vfio_device_set_fd(VFIODevice *vbasedev, const char *str, Error **errp);
void vfio_device_init(VFIODevice *vbasedev, int type, VFIODeviceOps *ops,
                      DeviceState *dev, bool ram_discard);
int vfio_device_get_aw_bits(VFIODevice *vdev);

void vfio_kvm_device_close(void);
#endif /* HW_VFIO_VFIO_DEVICE_H */
