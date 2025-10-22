/*
 * VFIO CPR
 *
 * Copyright (c) 2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_CPR_H
#define HW_VFIO_VFIO_CPR_H

#include "migration/misc.h"
#include "system/memory.h"

struct VFIOLegacyContainer;
struct VFIOContainer;
struct VFIOGroup;
struct VFIODevice;
struct VFIOPCIDevice;
struct VFIOIOMMUFDContainer;
struct IOMMUFDBackend;

typedef int (*dma_map_fn)(const struct VFIOContainer *bcontainer,
                          hwaddr iova, uint64_t size, void *vaddr,
                          bool readonly, MemoryRegion *mr);

typedef struct VFIOContainerCPR {
    Error *blocker;
    bool vaddr_unmapped;
    NotifierWithReturn transfer_notifier;
    MemoryListener remap_listener;
} VFIOContainerCPR;

typedef struct VFIODeviceCPR {
    Error *mdev_blocker;
    Error *id_blocker;
    uint32_t hwpt_id;
    uint32_t ioas_id;
} VFIODeviceCPR;

typedef struct VFIOPCICPR {
    NotifierWithReturn transfer_notifier;
} VFIOPCICPR;

bool vfio_legacy_cpr_register_container(struct VFIOLegacyContainer *container,
                                        Error **errp);
void vfio_legacy_cpr_unregister_container(
    struct VFIOLegacyContainer *container);

int vfio_cpr_reboot_notifier(NotifierWithReturn *notifier, MigrationEvent *e,
                             Error **errp);

bool vfio_iommufd_cpr_register_container(struct VFIOIOMMUFDContainer *container,
                                         Error **errp);
void vfio_iommufd_cpr_unregister_container(
    struct VFIOIOMMUFDContainer *container);
bool vfio_iommufd_cpr_register_iommufd(struct IOMMUFDBackend *be, Error **errp);
void vfio_iommufd_cpr_unregister_iommufd(struct IOMMUFDBackend *be);
void vfio_iommufd_cpr_register_device(struct VFIODevice *vbasedev);
void vfio_iommufd_cpr_unregister_device(struct VFIODevice *vbasedev);
void vfio_cpr_load_device(struct VFIODevice *vbasedev);

int vfio_cpr_group_get_device_fd(int d, const char *name);

bool vfio_cpr_container_match(struct VFIOLegacyContainer *container,
                              struct VFIOGroup *group, int fd);

void vfio_cpr_giommu_remap(struct VFIOContainer *bcontainer,
                           MemoryRegionSection *section);

bool vfio_cpr_ram_discard_replay_populated(
    struct VFIOContainer *bcontainer, MemoryRegionSection *section);

void vfio_cpr_save_vector_fd(struct VFIOPCIDevice *vdev, const char *name,
                             int nr, int fd);
int vfio_cpr_load_vector_fd(struct VFIOPCIDevice *vdev, const char *name,
                            int nr);
void vfio_cpr_delete_vector_fd(struct VFIOPCIDevice *vdev, const char *name,
                               int nr);

extern const VMStateDescription vfio_cpr_pci_vmstate;
extern const VMStateDescription vmstate_cpr_vfio_devices;

void vfio_cpr_add_kvm_notifier(void);
void vfio_cpr_pci_register_device(struct VFIOPCIDevice *vdev);
void vfio_cpr_pci_unregister_device(struct VFIOPCIDevice *vdev);

#endif /* HW_VFIO_VFIO_CPR_H */
