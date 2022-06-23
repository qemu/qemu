/*
 * QEMU NVM Express Subsystem: nvme-subsys
 *
 * Copyright (c) 2021 Minwoo Im <minwoo.im.dev@gmail.com>
 *
 * This code is licensed under the GNU GPL v2.  Refer COPYING.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "nvme.h"

static int nvme_subsys_reserve_cntlids(NvmeCtrl *n, int start, int num)
{
    NvmeSubsystem *subsys = n->subsys;
    NvmeSecCtrlList *list = &n->sec_ctrl_list;
    NvmeSecCtrlEntry *sctrl;
    int i, cnt = 0;

    for (i = start; i < ARRAY_SIZE(subsys->ctrls) && cnt < num; i++) {
        if (!subsys->ctrls[i]) {
            sctrl = &list->sec[cnt];
            sctrl->scid = cpu_to_le16(i);
            subsys->ctrls[i] = SUBSYS_SLOT_RSVD;
            cnt++;
        }
    }

    return cnt;
}

static void nvme_subsys_unreserve_cntlids(NvmeCtrl *n)
{
    NvmeSubsystem *subsys = n->subsys;
    NvmeSecCtrlList *list = &n->sec_ctrl_list;
    NvmeSecCtrlEntry *sctrl;
    int i, cntlid;

    for (i = 0; i < n->params.sriov_max_vfs; i++) {
        sctrl = &list->sec[i];
        cntlid = le16_to_cpu(sctrl->scid);

        if (cntlid) {
            assert(subsys->ctrls[cntlid] == SUBSYS_SLOT_RSVD);
            subsys->ctrls[cntlid] = NULL;
            sctrl->scid = 0;
        }
    }
}

int nvme_subsys_register_ctrl(NvmeCtrl *n, Error **errp)
{
    NvmeSubsystem *subsys = n->subsys;
    NvmeSecCtrlEntry *sctrl = nvme_sctrl(n);
    int cntlid, nsid, num_rsvd, num_vfs = n->params.sriov_max_vfs;

    if (pci_is_vf(&n->parent_obj)) {
        cntlid = le16_to_cpu(sctrl->scid);
    } else {
        for (cntlid = 0; cntlid < ARRAY_SIZE(subsys->ctrls); cntlid++) {
            if (!subsys->ctrls[cntlid]) {
                break;
            }
        }

        if (cntlid == ARRAY_SIZE(subsys->ctrls)) {
            error_setg(errp, "no more free controller id");
            return -1;
        }

        num_rsvd = nvme_subsys_reserve_cntlids(n, cntlid + 1, num_vfs);
        if (num_rsvd != num_vfs) {
            nvme_subsys_unreserve_cntlids(n);
            error_setg(errp,
                       "no more free controller ids for secondary controllers");
            return -1;
        }
    }

    if (!subsys->serial) {
        subsys->serial = g_strdup(n->params.serial);
    } else if (strcmp(subsys->serial, n->params.serial)) {
        error_setg(errp, "invalid controller serial");
        return -1;
    }

    subsys->ctrls[cntlid] = n;

    for (nsid = 1; nsid < ARRAY_SIZE(subsys->namespaces); nsid++) {
        NvmeNamespace *ns = subsys->namespaces[nsid];
        if (ns && ns->params.shared && !ns->params.detached) {
            nvme_attach_ns(n, ns);
        }
    }

    return cntlid;
}

void nvme_subsys_unregister_ctrl(NvmeSubsystem *subsys, NvmeCtrl *n)
{
    if (pci_is_vf(&n->parent_obj)) {
        subsys->ctrls[n->cntlid] = SUBSYS_SLOT_RSVD;
    } else {
        subsys->ctrls[n->cntlid] = NULL;
        nvme_subsys_unreserve_cntlids(n);
    }

    n->cntlid = -1;
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

    qbus_init(&subsys->bus, sizeof(NvmeBus), TYPE_NVME_BUS, dev, dev->id);

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
    dc->hotpluggable = false;

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
