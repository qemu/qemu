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
#define NVME_MAX_NAMESPACES     256

typedef struct NvmeCtrl NvmeCtrl;
typedef struct NvmeNamespace NvmeNamespace;
typedef struct NvmeSubsystem {
    DeviceState parent_obj;
    uint8_t     subnqn[256];

    NvmeCtrl    *ctrls[NVME_SUBSYS_MAX_CTRLS];
    /* Allocated namespaces for this subsystem */
    NvmeNamespace *namespaces[NVME_MAX_NAMESPACES + 1];

    struct {
        char *nqn;
    } params;
} NvmeSubsystem;

int nvme_subsys_register_ctrl(NvmeCtrl *n, Error **errp);

static inline NvmeCtrl *nvme_subsys_ctrl(NvmeSubsystem *subsys,
        uint32_t cntlid)
{
    if (!subsys || cntlid >= NVME_SUBSYS_MAX_CTRLS) {
        return NULL;
    }

    return subsys->ctrls[cntlid];
}

/*
 * Return allocated namespace of the specified nsid in the subsystem.
 */
static inline NvmeNamespace *nvme_subsys_ns(NvmeSubsystem *subsys,
        uint32_t nsid)
{
    if (!subsys || !nsid || nsid > NVME_MAX_NAMESPACES) {
        return NULL;
    }

    return subsys->namespaces[nsid];
}

#endif /* NVME_SUBSYS_H */
