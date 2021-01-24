/*
 * QEMU NVM Express Subsystem: nvme-subsys
 *
 * Copyright (c) 2021 Minwoo Im <minwoo.im.dev@gmail.com>
 *
 * This code is licensed under the GNU GPL v2.  Refer COPYING.
 */

#ifndef NVME_SUBSYS_H
#define NVME_SUBSYS_H

#define TYPE_NVME_SUBSYS "nvme-subsys"
#define NVME_SUBSYS(obj) \
    OBJECT_CHECK(NvmeSubsystem, (obj), TYPE_NVME_SUBSYS)

#define NVME_SUBSYS_MAX_CTRLS   32

typedef struct NvmeCtrl NvmeCtrl;
typedef struct NvmeNamespace NvmeNamespace;
typedef struct NvmeSubsystem {
    DeviceState parent_obj;
    uint8_t     subnqn[256];

    struct {
        char *nqn;
    } params;
} NvmeSubsystem;

#endif /* NVME_SUBSYS_H */
