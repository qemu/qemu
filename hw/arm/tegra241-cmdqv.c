/*
 * Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 * NVIDIA Tegra241 CMDQ-Virtualization extension for SMMUv3
 *
 * Written by Nicolin Chen, Shameer Kolothum
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/arm/smmuv3.h"
#include "hw/arm/smmuv3-common.h"
#include "smmuv3-accel.h"
#include "tegra241-cmdqv.h"

static uint64_t tegra241_cmdqv_read_mmio(void *opaque, hwaddr offset,
                                         unsigned size)
{
    return 0;
}

static void tegra241_cmdqv_write_mmio(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size)
{
}

static void tegra241_cmdqv_free_viommu(SMMUv3State *s)
{
    SMMUv3AccelState *accel = s->s_accel;
    IOMMUFDViommu *viommu = accel->viommu;
    Tegra241CMDQV *cmdqv = accel->cmdqv;
    IOMMUFDVeventq *veventq = cmdqv->veventq;

    if (!viommu) {
        return;
    }
    if (veventq) {
        close(veventq->veventq_fd);
        iommufd_backend_free_id(viommu->iommufd, veventq->veventq_id);
        g_free(veventq);
        cmdqv->veventq = NULL;
    }
    if (cmdqv->vintf_page0) {
        munmap(cmdqv->vintf_page0, VINTF_PAGE_SIZE);
        cmdqv->vintf_page0 = NULL;
    }
    iommufd_backend_free_id(viommu->iommufd, viommu->viommu_id);
}

static bool
tegra241_cmdqv_alloc_viommu(SMMUv3State *s, HostIOMMUDeviceIOMMUFD *idev,
                            uint32_t *out_viommu_id, Error **errp)
{
    Tegra241CMDQV *cmdqv = s->s_accel->cmdqv;
    uint32_t viommu_id, veventq_id, veventq_fd;
    IOMMUFDVeventq *veventq;

    if (!iommufd_backend_alloc_viommu(idev->iommufd, idev->devid,
                                      IOMMU_VIOMMU_TYPE_TEGRA241_CMDQV,
                                      idev->hwpt_id, cmdqv->cmdqv_data,
                                      sizeof(*cmdqv->cmdqv_data), &viommu_id,
                                      errp)) {
        return false;
    }

    if (!iommufd_backend_viommu_mmap(idev->iommufd, viommu_id, VINTF_PAGE_SIZE,
                                     cmdqv->cmdqv_data->out_vintf_mmap_offset,
                                     &cmdqv->vintf_page0, errp)) {
        error_append_hint(errp, "Tegra241 CMDQV: failed to mmap VINTF page0");
        goto free_viommu;
    }

    if (!iommufd_backend_alloc_veventq(idev->iommufd, viommu_id,
                                       IOMMU_VEVENTQ_TYPE_TEGRA241_CMDQV,
                                       1 << SMMU_EVENTQS, &veventq_id,
                                       &veventq_fd,
                                       errp)) {
        error_append_hint(errp, "Tegra241 CMDQV: failed to alloc veventq");
        goto munmap_page0;
    }

    veventq = g_new(IOMMUFDVeventq, 1);
    veventq->veventq_id = veventq_id;
    veventq->veventq_fd = veventq_fd;
    cmdqv->veventq = veventq;

    *out_viommu_id = viommu_id;
    return true;

munmap_page0:
    munmap(cmdqv->vintf_page0, VINTF_PAGE_SIZE);
    cmdqv->vintf_page0 = NULL;
free_viommu:
    iommufd_backend_free_id(idev->iommufd, viommu_id);
    return false;
}

static void tegra241_cmdqv_reset(SMMUv3State *s)
{
}

static const MemoryRegionOps mmio_cmdqv_ops = {
    .read = tegra241_cmdqv_read_mmio,
    .write = tegra241_cmdqv_write_mmio,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static bool tegra241_cmdqv_init(SMMUv3State *s, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(OBJECT(s));
    SMMUv3AccelState *accel = s->s_accel;
    Tegra241CMDQV *cmdqv;

    cmdqv = g_new0(Tegra241CMDQV, 1);
    cmdqv->cmdqv_data = g_new0(struct iommu_viommu_tegra241_cmdqv, 1);
    memory_region_init_io(&cmdqv->mmio_cmdqv, OBJECT(s), &mmio_cmdqv_ops, cmdqv,
                          "tegra241-cmdqv", TEGRA241_CMDQV_IO_LEN);
    sysbus_init_mmio(sbd, &cmdqv->mmio_cmdqv);
    sysbus_init_irq(sbd, &cmdqv->irq);
    cmdqv->s_accel = accel;
    accel->cmdqv = cmdqv;
    return true;
}

static bool tegra241_cmdqv_probe(SMMUv3State *s, HostIOMMUDeviceIOMMUFD *idev,
                                 Error **errp)
{
    uint32_t data_type = IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV;
    struct iommu_hw_info_tegra241_cmdqv cmdqv_info;
    uint64_t caps;

    if (!iommufd_backend_get_device_info(idev->iommufd, idev->devid, &data_type,
                                         &cmdqv_info, sizeof(cmdqv_info), &caps,
                                         NULL, errp)) {
        return false;
    }
    if (data_type != IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV) {
        error_setg(errp, "Host CMDQV: unexpected data type %u (expected %u)",
                   data_type, IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV);
        return false;
    }
    if (cmdqv_info.version != CMDQV_VER) {
        error_setg(errp, "Host CMDQV: unsupported version %u (expected %u)",
                   cmdqv_info.version, CMDQV_VER);
        return false;
    }
    if (cmdqv_info.log2vcmdqs < CMDQV_NUM_CMDQ_LOG2) {
        error_setg(errp, "Host CMDQV: insufficient vCMDQs log2=%u (need >= %u)",
                   cmdqv_info.log2vcmdqs, CMDQV_NUM_CMDQ_LOG2);
        return false;
    }
    if (cmdqv_info.log2vsids < CMDQV_NUM_SID_PER_VI_LOG2) {
        error_setg(errp, "Host CMDQV: insufficient SIDs log2=%u (need >= %u)",
                   cmdqv_info.log2vsids, CMDQV_NUM_SID_PER_VI_LOG2);
        return false;
    }
    return true;
}

static const SMMUv3AccelCmdqvOps tegra241_cmdqv_ops = {
    .probe = tegra241_cmdqv_probe,
    .init = tegra241_cmdqv_init,
    .alloc_viommu = tegra241_cmdqv_alloc_viommu,
    .free_viommu = tegra241_cmdqv_free_viommu,
    .reset = tegra241_cmdqv_reset,
};

const SMMUv3AccelCmdqvOps *tegra241_cmdqv_get_ops(void)
{
    return &tegra241_cmdqv_ops;
}
