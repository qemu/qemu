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

struct VFIOContainer;
struct VFIOContainerBase;
struct VFIOGroup;

typedef struct VFIOContainerCPR {
    Error *blocker;
    bool vaddr_unmapped;
    NotifierWithReturn transfer_notifier;
    MemoryListener remap_listener;
    int (*saved_dma_map)(const struct VFIOContainerBase *bcontainer,
                         hwaddr iova, ram_addr_t size,
                         void *vaddr, bool readonly, MemoryRegion *mr);
} VFIOContainerCPR;

typedef struct VFIODeviceCPR {
    Error *mdev_blocker;
} VFIODeviceCPR;

bool vfio_legacy_cpr_register_container(struct VFIOContainer *container,
                                        Error **errp);
void vfio_legacy_cpr_unregister_container(struct VFIOContainer *container);

int vfio_cpr_reboot_notifier(NotifierWithReturn *notifier, MigrationEvent *e,
                             Error **errp);

bool vfio_cpr_register_container(struct VFIOContainerBase *bcontainer,
                                 Error **errp);
void vfio_cpr_unregister_container(struct VFIOContainerBase *bcontainer);

int vfio_cpr_group_get_device_fd(int d, const char *name);

bool vfio_cpr_container_match(struct VFIOContainer *container,
                              struct VFIOGroup *group, int fd);

void vfio_cpr_giommu_remap(struct VFIOContainerBase *bcontainer,
                           MemoryRegionSection *section);

bool vfio_cpr_ram_discard_register_listener(
    struct VFIOContainerBase *bcontainer, MemoryRegionSection *section);

#endif /* HW_VFIO_VFIO_CPR_H */
