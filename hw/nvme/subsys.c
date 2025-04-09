/*
 * QEMU NVM Express Subsystem: nvme-subsys
 *
 * Copyright (c) 2021 Minwoo Im <minwoo.im.dev@gmail.com>
 *
 * This code is licensed under the GNU GPL v2.  Refer COPYING.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"

#include "nvme.h"

#define NVME_DEFAULT_RU_SIZE (96 * MiB)

static int nvme_subsys_reserve_cntlids(NvmeCtrl *n, int start, int num)
{
    NvmeSubsystem *subsys = n->subsys;
    NvmeSecCtrlEntry *list = n->sec_ctrl_list;
    NvmeSecCtrlEntry *sctrl;
    int i, cnt = 0;

    for (i = start; i < ARRAY_SIZE(subsys->ctrls) && cnt < num; i++) {
        if (!subsys->ctrls[i]) {
            sctrl = &list[cnt];
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
    NvmeSecCtrlEntry *list = n->sec_ctrl_list;
    NvmeSecCtrlEntry *sctrl;
    int i, cntlid;

    for (i = 0; i < n->params.sriov_max_vfs; i++) {
        sctrl = &list[i];
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
    int cntlid, num_rsvd, num_vfs = n->params.sriov_max_vfs;

    if (pci_is_vf(&n->parent_obj)) {
        cntlid = le16_to_cpu(sctrl->scid);
    } else {
        n->sec_ctrl_list = g_new0(NvmeSecCtrlEntry, num_vfs);

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

static bool nvme_calc_rgif(uint16_t nruh, uint16_t nrg, uint8_t *rgif)
{
    uint16_t val;
    unsigned int i;

    if (unlikely(nrg == 1)) {
        /* PIDRG_NORGI scenario, all of pid is used for PHID */
        *rgif = 0;
        return true;
    }

    val = nrg;
    i = 0;
    while (val) {
        val >>= 1;
        i++;
    }
    *rgif = i;

    /* ensure remaining bits suffice to represent number of phids in a RG */
    if (unlikely((UINT16_MAX >> i) < nruh)) {
        *rgif = 0;
        return false;
    }

    return true;
}

static bool nvme_subsys_setup_fdp(NvmeSubsystem *subsys, Error **errp)
{
    NvmeEnduranceGroup *endgrp = &subsys->endgrp;

    if (!subsys->params.fdp.runs) {
        error_setg(errp, "fdp.runs must be non-zero");
        return false;
    }

    endgrp->fdp.runs = subsys->params.fdp.runs;

    if (!subsys->params.fdp.nrg) {
        error_setg(errp, "fdp.nrg must be non-zero");
        return false;
    }

    endgrp->fdp.nrg = subsys->params.fdp.nrg;

    if (!subsys->params.fdp.nruh ||
        subsys->params.fdp.nruh > NVME_FDP_MAXPIDS) {
        error_setg(errp, "fdp.nruh must be non-zero and less than %u",
                   NVME_FDP_MAXPIDS);
        return false;
    }

    endgrp->fdp.nruh = subsys->params.fdp.nruh;

    if (!nvme_calc_rgif(endgrp->fdp.nruh, endgrp->fdp.nrg, &endgrp->fdp.rgif)) {
        error_setg(errp,
                   "cannot derive a valid rgif (nruh %"PRIu16" nrg %"PRIu32")",
                   endgrp->fdp.nruh, endgrp->fdp.nrg);
        return false;
    }

    endgrp->fdp.ruhs = g_new(NvmeRuHandle, endgrp->fdp.nruh);

    for (uint16_t ruhid = 0; ruhid < endgrp->fdp.nruh; ruhid++) {
        endgrp->fdp.ruhs[ruhid] = (NvmeRuHandle) {
            .ruht = NVME_RUHT_INITIALLY_ISOLATED,
            .ruha = NVME_RUHA_UNUSED,
        };

        endgrp->fdp.ruhs[ruhid].rus = g_new(NvmeReclaimUnit, endgrp->fdp.nrg);
    }

    endgrp->fdp.enabled = true;

    return true;
}

static bool nvme_subsys_setup(NvmeSubsystem *subsys, Error **errp)
{
    const char *nqn = subsys->params.nqn ?
        subsys->params.nqn : subsys->parent_obj.id;

    snprintf((char *)subsys->subnqn, sizeof(subsys->subnqn),
             "nqn.2019-08.org.qemu:%s", nqn);

    if (subsys->params.fdp.enabled && !nvme_subsys_setup_fdp(subsys, errp)) {
        return false;
    }

    return true;
}

static void nvme_subsys_realize(DeviceState *dev, Error **errp)
{
    NvmeSubsystem *subsys = NVME_SUBSYS(dev);

    qbus_init(&subsys->bus, sizeof(NvmeBus), TYPE_NVME_BUS, dev, dev->id);

    nvme_subsys_setup(subsys, errp);
}

static const Property nvme_subsystem_props[] = {
    DEFINE_PROP_STRING("nqn", NvmeSubsystem, params.nqn),
    DEFINE_PROP_BOOL("fdp", NvmeSubsystem, params.fdp.enabled, false),
    DEFINE_PROP_SIZE("fdp.runs", NvmeSubsystem, params.fdp.runs,
                     NVME_DEFAULT_RU_SIZE),
    DEFINE_PROP_UINT32("fdp.nrg", NvmeSubsystem, params.fdp.nrg, 1),
    DEFINE_PROP_UINT16("fdp.nruh", NvmeSubsystem, params.fdp.nruh, 0),
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
