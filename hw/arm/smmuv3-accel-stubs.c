/*
 * Stubs for accelerated SMMU instance backed by an iommufd vIOMMU object.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/smmuv3.h"
#include "hw/arm/smmuv3-accel.h"

bool smmuv3_accel_init(SMMUv3State *s, Error **errp)
{
    error_setg(errp, "accel=on support not compiled in");
    return false;
}

bool smmuv3_accel_install_ste(SMMUv3State *s, SMMUDevice *sdev, int sid,
                              Error **errp)
{
    return true;
}

bool smmuv3_accel_install_ste_range(SMMUv3State *s, SMMUSIDRange *range,
                                    Error **errp)
{
    return true;
}

bool smmuv3_accel_attach_gbpa_hwpt(SMMUv3State *s, Error **errp)
{
    return true;
}

bool smmuv3_accel_issue_inv_cmd(SMMUv3State *s, void *cmd, SMMUDevice *sdev,
                                Error **errp)
{
    return true;
}

void smmuv3_accel_idr_override(SMMUv3State *s)
{
}

bool smmuv3_accel_alloc_veventq(SMMUv3State *s, Error **errp)
{
    return true;
}

bool smmuv3_accel_event_read_validate(IOMMUFDVeventq *veventq, uint32_t type,
                                      void *buf, size_t size, Error **errp)
{
    return true;
}


void smmuv3_accel_reset(SMMUv3State *s)
{
}

SMMUv3AccelCmdqvType smmuv3_accel_cmdqv_type(Object *obj)
{
    return SMMUV3_CMDQV_NONE;
}
