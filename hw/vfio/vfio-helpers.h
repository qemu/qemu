/*
 * VFIO helpers
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_HELPERS_H
#define HW_VFIO_VFIO_HELPERS_H

#ifdef CONFIG_LINUX
#include <linux/vfio.h>

extern int vfio_kvm_device_fd;

struct vfio_info_cap_header *
vfio_get_cap(void *ptr, uint32_t cap_offset, uint16_t id);
struct vfio_info_cap_header *
vfio_get_device_info_cap(struct vfio_device_info *info, uint16_t id);
struct vfio_info_cap_header *
vfio_get_region_info_cap(struct vfio_region_info *info, uint16_t id);
struct vfio_info_cap_header *
vfio_get_iommu_type1_info_cap(struct vfio_iommu_type1_info *info, uint16_t id);
bool vfio_get_info_dma_avail(struct vfio_iommu_type1_info *info,
                             unsigned int *avail);
#endif

int vfio_bitmap_alloc(VFIOBitmap *vbmap, hwaddr size);
struct vfio_device_info *vfio_get_device_info(int fd);

int vfio_kvm_device_add_fd(int fd, Error **errp);
int vfio_kvm_device_del_fd(int fd, Error **errp);

bool vfio_arch_wants_loading_config_after_iter(void);

#endif /* HW_VFIO_VFIO_HELPERS_H */
