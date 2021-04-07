/*
 * QEMU NVM Express Subsystem: nvme-subsys
 *
 * Copyright (c) 2021 Minwoo Im <minwoo.im.dev@gmail.com>
 *
 * This code is licensed under the GNU GPL v2.  Refer COPYING.
 */

#include "qemu/units.h"
#include "qemu/osdep.h"
#include "qemu/uuid.h"
#include "qemu/iov.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "hw/block/block.h"
#include "block/aio.h"
#include "block/accounting.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "nvme.h"
#include "nvme-subsys.h"

int nvme_subsys_register_ctrl(NvmeCtrl *n, Error **errp)
{
    NvmeSubsystem *subsys = n->subsys;
    int cntlid;

    for (cntlid = 0; cntlid < ARRAY_SIZE(subsys->ctrls); cntlid++) {
        if (!subsys->ctrls[cntlid]) {
            break;
        }
    }

    if (cntlid == ARRAY_SIZE(subsys->ctrls)) {
        error_setg(errp, "no more free controller id");
        return -1;
    }

    subsys->ctrls[cntlid] = n;

    return cntlid;
}

static void nvme_subsys_setup(NvmeSubsystem *subsys)
{
    const char *nqn = subsys->params.nqn ?
        subsys->params.nqn : subsys->parent_obj.id;

    snprintf((char *)subsys->subnqn, sizeof(subsys->subnqn),
             "nqn.2019-08.org.qemu:%s", nqn);
}

static void nvme_subsys_realize(DeviceState *dev, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYS(dev);

    nvme_subsys_setup(subsys);
}

static Property nvme_subsystem_props[] = {
    DEFINE_PROP_STRING("nqn", NvmeSubsystem, params.nqn),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvme_subsys_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    dc->realize = nvme_subsys_realize;
    dc->desc = "Virtual NVMe subsystem";

    device_class_set_props(dc, nvme_subsystem_props);
}

static const TypeInfo nvme_subsys_info = {
    .name = TYPE_NVME_SUBSYS,
    .parent = TYPE_DEVICE,
    .class_init = nvme_subsys_class_init,
    .instance_size = sizeof(NvmeSubsystem),
};

static void nvme_subsys_register_types(void)
{
    type_register_static(&nvme_subsys_info);
}

type_init(nvme_subsys_register_types)
