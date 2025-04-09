/*
 * QEMU NVM Express Controller
 *
 * Copyright (c) 2012, Intel Corporation
 *
 * Written by Keith Busch <keith.busch@intel.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */

/**
 * Reference Specs: http://www.nvmexpress.org, 1.4, 1.3, 1.2, 1.1, 1.0e
 *
 *  https://nvmexpress.org/developers/nvme-specification/
 *
 *
 * Notes on coding style
 * ---------------------
 * While QEMU coding style prefers lowercase hexadecimals in constants, the
 * NVMe subsystem use this format from the NVMe specifications in the comments
 * (i.e. 'h' suffix instead of '0x' prefix).
 *
 * Usage
 * -----
 * See docs/system/nvme.rst for extensive documentation.
 *
 * Add options:
 *      -drive file=<file>,if=none,id=<drive_id>
 *      -device nvme-subsys,id=<subsys_id>,nqn=<nqn_id>
 *      -device nvme,serial=<serial>,id=<bus_name>, \
 *              cmb_size_mb=<cmb_size_mb[optional]>, \
 *              [pmrdev=<mem_backend_file_id>,] \
 *              max_ioqpairs=<N[optional]>, \
 *              aerl=<N[optional]>,aer_max_queued=<N[optional]>, \
 *              mdts=<N[optional]>,vsl=<N[optional]>, \
 *              zoned.zasl=<N[optional]>, \
 *              zoned.auto_transition=<on|off[optional]>, \
 *              sriov_max_vfs=<N[optional]> \
 *              sriov_vq_flexible=<N[optional]> \
 *              sriov_vi_flexible=<N[optional]> \
 *              sriov_max_vi_per_vf=<N[optional]> \
 *              sriov_max_vq_per_vf=<N[optional]> \
 *              atomic.dn=<on|off[optional]>, \
 *              atomic.awun<N[optional]>, \
 *              atomic.awupf<N[optional]>, \
 *              subsys=<subsys_id>
 *      -device nvme-ns,drive=<drive_id>,bus=<bus_name>,nsid=<nsid>,\
 *              zoned=<true|false[optional]>, \
 *              subsys=<subsys_id>,shared=<true|false[optional]>, \
 *              detached=<true|false[optional]>, \
 *              zoned.zone_size=<N[optional]>, \
 *              zoned.zone_capacity=<N[optional]>, \
 *              zoned.descr_ext_size=<N[optional]>, \
 *              zoned.max_active=<N[optional]>, \
 *              zoned.max_open=<N[optional]>, \
 *              zoned.cross_read=<true|false[optional]>
 *
 * Note cmb_size_mb denotes size of CMB in MB. CMB is assumed to be at
 * offset 0 in BAR2 and supports only WDS, RDS and SQS for now. By default, the
 * device will use the "v1.4 CMB scheme" - use the `legacy-cmb` parameter to
 * always enable the CMBLOC and CMBSZ registers (v1.3 behavior).
 *
 * Enabling pmr emulation can be achieved by pointing to memory-backend-file.
 * For example:
 * -object memory-backend-file,id=<mem_id>,share=on,mem-path=<file_path>, \
 *  size=<size> .... -device nvme,...,pmrdev=<mem_id>
 *
 * The PMR will use BAR 4/5 exclusively.
 *
 * To place controller(s) and namespace(s) to a subsystem, then provide
 * nvme-subsys device as above.
 *
 * nvme subsystem device parameters
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * - `nqn`
 *   This parameter provides the `<nqn_id>` part of the string
 *   `nqn.2019-08.org.qemu:<nqn_id>` which will be reported in the SUBNQN field
 *   of subsystem controllers. Note that `<nqn_id>` should be unique per
 *   subsystem, but this is not enforced by QEMU. If not specified, it will
 *   default to the value of the `id` parameter (`<subsys_id>`).
 *
 * nvme device parameters
 * ~~~~~~~~~~~~~~~~~~~~~~
 * - `subsys`
 *   Specifying this parameter attaches the controller to the subsystem and
 *   the SUBNQN field in the controller will report the NQN of the subsystem
 *   device. This also enables multi controller capability represented in
 *   Identify Controller data structure in CMIC (Controller Multi-path I/O and
 *   Namespace Sharing Capabilities).
 *
 * - `aerl`
 *   The Asynchronous Event Request Limit (AERL). Indicates the maximum number
 *   of concurrently outstanding Asynchronous Event Request commands support
 *   by the controller. This is a 0's based value.
 *
 * - `aer_max_queued`
 *   This is the maximum number of events that the device will enqueue for
 *   completion when there are no outstanding AERs. When the maximum number of
 *   enqueued events are reached, subsequent events will be dropped.
 *
 * - `mdts`
 *   Indicates the maximum data transfer size for a command that transfers data
 *   between host-accessible memory and the controller. The value is specified
 *   as a power of two (2^n) and is in units of the minimum memory page size
 *   (CAP.MPSMIN). The default value is 7 (i.e. 512 KiB).
 *
 * - `vsl`
 *   Indicates the maximum data size limit for the Verify command. Like `mdts`,
 *   this value is specified as a power of two (2^n) and is in units of the
 *   minimum memory page size (CAP.MPSMIN). The default value is 7 (i.e. 512
 *   KiB).
 *
 * - `zoned.zasl`
 *   Indicates the maximum data transfer size for the Zone Append command. Like
 *   `mdts`, the value is specified as a power of two (2^n) and is in units of
 *   the minimum memory page size (CAP.MPSMIN). The default value is 0 (i.e.
 *   defaulting to the value of `mdts`).
 *
 * - `zoned.auto_transition`
 *   Indicates if zones in zone state implicitly opened can be automatically
 *   transitioned to zone state closed for resource management purposes.
 *   Defaults to 'on'.
 *
 * - `sriov_max_vfs`
 *   Indicates the maximum number of PCIe virtual functions supported
 *   by the controller. The default value is 0. Specifying a non-zero value
 *   enables reporting of both SR-IOV and ARI capabilities by the NVMe device.
 *   Virtual function controllers will not report SR-IOV capability.
 *
 *   NOTE: Single Root I/O Virtualization support is experimental.
 *   All the related parameters may be subject to change.
 *
 * - `sriov_vq_flexible`
 *   Indicates the total number of flexible queue resources assignable to all
 *   the secondary controllers. Implicitly sets the number of primary
 *   controller's private resources to `(max_ioqpairs - sriov_vq_flexible)`.
 *
 * - `sriov_vi_flexible`
 *   Indicates the total number of flexible interrupt resources assignable to
 *   all the secondary controllers. Implicitly sets the number of primary
 *   controller's private resources to `(msix_qsize - sriov_vi_flexible)`.
 *
 * - `sriov_max_vi_per_vf`
 *   Indicates the maximum number of virtual interrupt resources assignable
 *   to a secondary controller. The default 0 resolves to
 *   `(sriov_vi_flexible / sriov_max_vfs)`.
 *
 * - `sriov_max_vq_per_vf`
 *   Indicates the maximum number of virtual queue resources assignable to
 *   a secondary controller. The default 0 resolves to
 *   `(sriov_vq_flexible / sriov_max_vfs)`.
 *
 * nvme namespace device parameters
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * - `shared`
 *   When the parent nvme device (as defined explicitly by the 'bus' parameter
 *   or implicitly by the most recently defined NvmeBus) is linked to an
 *   nvme-subsys device, the namespace will be attached to all controllers in
 *   the subsystem. If set to 'off' (the default), the namespace will remain a
 *   private namespace and may only be attached to a single controller at a
 *   time.
 *
 * - `detached`
 *   This parameter is only valid together with the `subsys` parameter. If left
 *   at the default value (`false/off`), the namespace will be attached to all
 *   controllers in the NVMe subsystem at boot-up. If set to `true/on`, the
 *   namespace will be available in the subsystem but not attached to any
 *   controllers.
 *
 * Setting `zoned` to true selects Zoned Command Set at the namespace.
 * In this case, the following namespace properties are available to configure
 * zoned operation:
 *     zoned.zone_size=<zone size in bytes, default: 128MiB>
 *         The number may be followed by K, M, G as in kilo-, mega- or giga-.
 *
 *     zoned.zone_capacity=<zone capacity in bytes, default: zone size>
 *         The value 0 (default) forces zone capacity to be the same as zone
 *         size. The value of this property may not exceed zone size.
 *
 *     zoned.descr_ext_size=<zone descriptor extension size, default 0>
 *         This value needs to be specified in 64B units. If it is zero,
 *         namespace(s) will not support zone descriptor extensions.
 *
 *     zoned.max_active=<Maximum Active Resources (zones), default: 0>
 *         The default value means there is no limit to the number of
 *         concurrently active zones.
 *
 *     zoned.max_open=<Maximum Open Resources (zones), default: 0>
 *         The default value means there is no limit to the number of
 *         concurrently open zones.
 *
 *     zoned.cross_read=<enable RAZB, default: false>
 *         Setting this property to true enables Read Across Zone Boundaries.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "system/system.h"
#include "system/block-backend.h"
#include "system/hostmem.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie_sriov.h"
#include "system/spdm-socket.h"
#include "migration/vmstate.h"

#include "nvme.h"
#include "dif.h"
#include "trace.h"

#define NVME_MAX_IOQPAIRS 0xffff
#define NVME_DB_SIZE  4
#define NVME_SPEC_VER 0x00010400
#define NVME_CMB_BIR 2
#define NVME_PMR_BIR 4
#define NVME_TEMPERATURE 0x143
#define NVME_TEMPERATURE_WARNING 0x157
#define NVME_TEMPERATURE_CRITICAL 0x175
#define NVME_NUM_FW_SLOTS 1
#define NVME_DEFAULT_MAX_ZA_SIZE (128 * KiB)
#define NVME_VF_RES_GRANULARITY 1
#define NVME_VF_OFFSET 0x1
#define NVME_VF_STRIDE 1

#define NVME_GUEST_ERR(trace, fmt, ...) \
    do { \
        (trace_##trace)(__VA_ARGS__); \
        qemu_log_mask(LOG_GUEST_ERROR, #trace \
            " in %s: " fmt "\n", __func__, ## __VA_ARGS__); \
    } while (0)

static const bool nvme_feature_support[NVME_FID_MAX] = {
    [NVME_ARBITRATION]              = true,
    [NVME_POWER_MANAGEMENT]         = true,
    [NVME_TEMPERATURE_THRESHOLD]    = true,
    [NVME_ERROR_RECOVERY]           = true,
    [NVME_VOLATILE_WRITE_CACHE]     = true,
    [NVME_NUMBER_OF_QUEUES]         = true,
    [NVME_INTERRUPT_COALESCING]     = true,
    [NVME_INTERRUPT_VECTOR_CONF]    = true,
    [NVME_WRITE_ATOMICITY]          = true,
    [NVME_ASYNCHRONOUS_EVENT_CONF]  = true,
    [NVME_TIMESTAMP]                = true,
    [NVME_HOST_BEHAVIOR_SUPPORT]    = true,
    [NVME_COMMAND_SET_PROFILE]      = true,
    [NVME_FDP_MODE]                 = true,
    [NVME_FDP_EVENTS]               = true,
};

static const uint32_t nvme_feature_cap[NVME_FID_MAX] = {
    [NVME_TEMPERATURE_THRESHOLD]    = NVME_FEAT_CAP_CHANGE,
    [NVME_ERROR_RECOVERY]           = NVME_FEAT_CAP_CHANGE | NVME_FEAT_CAP_NS,
    [NVME_VOLATILE_WRITE_CACHE]     = NVME_FEAT_CAP_CHANGE,
    [NVME_NUMBER_OF_QUEUES]         = NVME_FEAT_CAP_CHANGE,
    [NVME_WRITE_ATOMICITY]          = NVME_FEAT_CAP_CHANGE,
    [NVME_ASYNCHRONOUS_EVENT_CONF]  = NVME_FEAT_CAP_CHANGE,
    [NVME_TIMESTAMP]                = NVME_FEAT_CAP_CHANGE,
    [NVME_HOST_BEHAVIOR_SUPPORT]    = NVME_FEAT_CAP_CHANGE,
    [NVME_COMMAND_SET_PROFILE]      = NVME_FEAT_CAP_CHANGE,
    [NVME_FDP_MODE]                 = NVME_FEAT_CAP_CHANGE,
    [NVME_FDP_EVENTS]               = NVME_FEAT_CAP_CHANGE | NVME_FEAT_CAP_NS,
};

static const uint32_t nvme_cse_acs_default[256] = {
    [NVME_ADM_CMD_DELETE_SQ]        = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_CREATE_SQ]        = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_GET_LOG_PAGE]     = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_DELETE_CQ]        = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_CREATE_CQ]        = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_IDENTIFY]         = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_ABORT]            = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_SET_FEATURES]     = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_GET_FEATURES]     = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_ASYNC_EV_REQ]     = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_NS_ATTACHMENT]    = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_NIC |
                                      NVME_CMD_EFF_CCC,
    [NVME_ADM_CMD_FORMAT_NVM]       = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_ADM_CMD_DIRECTIVE_RECV]   = NVME_CMD_EFF_CSUPP,
    [NVME_ADM_CMD_DIRECTIVE_SEND]   = NVME_CMD_EFF_CSUPP,
};

static const uint32_t nvme_cse_iocs_nvm_default[256] = {
    [NVME_CMD_FLUSH]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE_ZEROES]         = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_READ]                 = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_DSM]                  = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_VERIFY]               = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_COPY]                 = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_COMPARE]              = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_IO_MGMT_RECV]         = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_IO_MGMT_SEND]         = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
};

static const uint32_t nvme_cse_iocs_zoned_default[256] = {
    [NVME_CMD_FLUSH]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE_ZEROES]         = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_READ]                 = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_DSM]                  = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_VERIFY]               = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_COPY]                 = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_COMPARE]              = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_IO_MGMT_RECV]         = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_IO_MGMT_SEND]         = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,

    [NVME_CMD_ZONE_APPEND]          = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_ZONE_MGMT_SEND]       = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_ZONE_MGMT_RECV]       = NVME_CMD_EFF_CSUPP,
};

static void nvme_process_sq(void *opaque);
static void nvme_ctrl_reset(NvmeCtrl *n, NvmeResetType rst);
static inline uint64_t nvme_get_timestamp(const NvmeCtrl *n);

static uint16_t nvme_sqid(NvmeRequest *req)
{
    return le16_to_cpu(req->sq->sqid);
}

static inline uint16_t nvme_make_pid(NvmeNamespace *ns, uint16_t rg,
                                     uint16_t ph)
{
    uint16_t rgif = ns->endgrp->fdp.rgif;

    if (!rgif) {
        return ph;
    }

    return (rg << (16 - rgif)) | ph;
}

static inline bool nvme_ph_valid(NvmeNamespace *ns, uint16_t ph)
{
    return ph < ns->fdp.nphs;
}

static inline bool nvme_rg_valid(NvmeEnduranceGroup *endgrp, uint16_t rg)
{
    return rg < endgrp->fdp.nrg;
}

static inline uint16_t nvme_pid2ph(NvmeNamespace *ns, uint16_t pid)
{
    uint16_t rgif = ns->endgrp->fdp.rgif;

    if (!rgif) {
        return pid;
    }

    return pid & ((1 << (15 - rgif)) - 1);
}

static inline uint16_t nvme_pid2rg(NvmeNamespace *ns, uint16_t pid)
{
    uint16_t rgif = ns->endgrp->fdp.rgif;

    if (!rgif) {
        return 0;
    }

    return pid >> (16 - rgif);
}

static inline bool nvme_parse_pid(NvmeNamespace *ns, uint16_t pid,
                                  uint16_t *ph, uint16_t *rg)
{
    *rg = nvme_pid2rg(ns, pid);
    *ph = nvme_pid2ph(ns, pid);

    return nvme_ph_valid(ns, *ph) && nvme_rg_valid(ns->endgrp, *rg);
}

static void nvme_assign_zone_state(NvmeNamespace *ns, NvmeZone *zone,
                                   NvmeZoneState state)
{
    if (QTAILQ_IN_USE(zone, entry)) {
        switch (nvme_get_zone_state(zone)) {
        case NVME_ZONE_STATE_EXPLICITLY_OPEN:
            QTAILQ_REMOVE(&ns->exp_open_zones, zone, entry);
            break;
        case NVME_ZONE_STATE_IMPLICITLY_OPEN:
            QTAILQ_REMOVE(&ns->imp_open_zones, zone, entry);
            break;
        case NVME_ZONE_STATE_CLOSED:
            QTAILQ_REMOVE(&ns->closed_zones, zone, entry);
            break;
        case NVME_ZONE_STATE_FULL:
            QTAILQ_REMOVE(&ns->full_zones, zone, entry);
        default:
            ;
        }
    }

    nvme_set_zone_state(zone, state);

    switch (state) {
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        QTAILQ_INSERT_TAIL(&ns->exp_open_zones, zone, entry);
        break;
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        QTAILQ_INSERT_TAIL(&ns->imp_open_zones, zone, entry);
        break;
    case NVME_ZONE_STATE_CLOSED:
        QTAILQ_INSERT_TAIL(&ns->closed_zones, zone, entry);
        break;
    case NVME_ZONE_STATE_FULL:
        QTAILQ_INSERT_TAIL(&ns->full_zones, zone, entry);
    case NVME_ZONE_STATE_READ_ONLY:
        break;
    default:
        zone->d.za = 0;
    }
}

static uint16_t nvme_zns_check_resources(NvmeNamespace *ns, uint32_t act,
                                         uint32_t opn, uint32_t zrwa)
{
    if (ns->params.max_active_zones != 0 &&
        ns->nr_active_zones + act > ns->params.max_active_zones) {
        trace_pci_nvme_err_insuff_active_res(ns->params.max_active_zones);
        return NVME_ZONE_TOO_MANY_ACTIVE | NVME_DNR;
    }

    if (ns->params.max_open_zones != 0 &&
        ns->nr_open_zones + opn > ns->params.max_open_zones) {
        trace_pci_nvme_err_insuff_open_res(ns->params.max_open_zones);
        return NVME_ZONE_TOO_MANY_OPEN | NVME_DNR;
    }

    if (zrwa > ns->zns.numzrwa) {
        return NVME_NOZRWA | NVME_DNR;
    }

    return NVME_SUCCESS;
}

/*
 * Check if we can open a zone without exceeding open/active limits.
 * AOR stands for "Active and Open Resources" (see TP 4053 section 2.5).
 */
static uint16_t nvme_aor_check(NvmeNamespace *ns, uint32_t act, uint32_t opn)
{
    return nvme_zns_check_resources(ns, act, opn, 0);
}

static NvmeFdpEvent *nvme_fdp_alloc_event(NvmeCtrl *n, NvmeFdpEventBuffer *ebuf)
{
    NvmeFdpEvent *ret = NULL;
    bool is_full = ebuf->next == ebuf->start && ebuf->nelems;

    ret = &ebuf->events[ebuf->next++];
    if (unlikely(ebuf->next == NVME_FDP_MAX_EVENTS)) {
        ebuf->next = 0;
    }
    if (is_full) {
        ebuf->start = ebuf->next;
    } else {
        ebuf->nelems++;
    }

    memset(ret, 0, sizeof(NvmeFdpEvent));
    ret->timestamp = nvme_get_timestamp(n);

    return ret;
}

static inline int log_event(NvmeRuHandle *ruh, uint8_t event_type)
{
    return (ruh->event_filter >> nvme_fdp_evf_shifts[event_type]) & 0x1;
}

static bool nvme_update_ruh(NvmeCtrl *n, NvmeNamespace *ns, uint16_t pid)
{
    NvmeEnduranceGroup *endgrp = ns->endgrp;
    NvmeRuHandle *ruh;
    NvmeReclaimUnit *ru;
    NvmeFdpEvent *e = NULL;
    uint16_t ph, rg, ruhid;

    if (!nvme_parse_pid(ns, pid, &ph, &rg)) {
        return false;
    }

    ruhid = ns->fdp.phs[ph];

    ruh = &endgrp->fdp.ruhs[ruhid];
    ru = &ruh->rus[rg];

    if (ru->ruamw) {
        if (log_event(ruh, FDP_EVT_RU_NOT_FULLY_WRITTEN)) {
            e = nvme_fdp_alloc_event(n, &endgrp->fdp.host_events);
            e->type = FDP_EVT_RU_NOT_FULLY_WRITTEN;
            e->flags = FDPEF_PIV | FDPEF_NSIDV | FDPEF_LV;
            e->pid = cpu_to_le16(pid);
            e->nsid = cpu_to_le32(ns->params.nsid);
            e->rgid = cpu_to_le16(rg);
            e->ruhid = cpu_to_le16(ruhid);
        }

        /* log (eventual) GC overhead of prematurely swapping the RU */
        nvme_fdp_stat_inc(&endgrp->fdp.mbmw, nvme_l2b(ns, ru->ruamw));
    }

    ru->ruamw = ruh->ruamw;

    return true;
}

static bool nvme_addr_is_cmb(NvmeCtrl *n, hwaddr addr)
{
    hwaddr hi, lo;

    if (!n->cmb.cmse) {
        return false;
    }

    lo = n->params.legacy_cmb ? n->cmb.mem.addr : n->cmb.cba;
    hi = lo + int128_get64(n->cmb.mem.size);

    return addr >= lo && addr < hi;
}

static inline void *nvme_addr_to_cmb(NvmeCtrl *n, hwaddr addr)
{
    hwaddr base = n->params.legacy_cmb ? n->cmb.mem.addr : n->cmb.cba;
    return &n->cmb.buf[addr - base];
}

static bool nvme_addr_is_pmr(NvmeCtrl *n, hwaddr addr)
{
    hwaddr hi;

    if (!n->pmr.cmse) {
        return false;
    }

    hi = n->pmr.cba + int128_get64(n->pmr.dev->mr.size);

    return addr >= n->pmr.cba && addr < hi;
}

static inline void *nvme_addr_to_pmr(NvmeCtrl *n, hwaddr addr)
{
    return memory_region_get_ram_ptr(&n->pmr.dev->mr) + (addr - n->pmr.cba);
}

static inline bool nvme_addr_is_iomem(NvmeCtrl *n, hwaddr addr)
{
    hwaddr hi, lo;

    /*
     * The purpose of this check is to guard against invalid "local" access to
     * the iomem (i.e. controller registers). Thus, we check against the range
     * covered by the 'bar0' MemoryRegion since that is currently composed of
     * two subregions (the NVMe "MBAR" and the MSI-X table/pba). Note, however,
     * that if the device model is ever changed to allow the CMB to be located
     * in BAR0 as well, then this must be changed.
     */
    lo = n->bar0.addr;
    hi = lo + int128_get64(n->bar0.size);

    return addr >= lo && addr < hi;
}

static int nvme_addr_read(NvmeCtrl *n, hwaddr addr, void *buf, int size)
{
    hwaddr hi = addr + size - 1;
    if (hi < addr) {
        return 1;
    }

    if (n->bar.cmbsz && nvme_addr_is_cmb(n, addr) && nvme_addr_is_cmb(n, hi)) {
        memcpy(buf, nvme_addr_to_cmb(n, addr), size);
        return 0;
    }

    if (nvme_addr_is_pmr(n, addr) && nvme_addr_is_pmr(n, hi)) {
        memcpy(buf, nvme_addr_to_pmr(n, addr), size);
        return 0;
    }

    return pci_dma_read(PCI_DEVICE(n), addr, buf, size);
}

static int nvme_addr_write(NvmeCtrl *n, hwaddr addr, const void *buf, int size)
{
    hwaddr hi = addr + size - 1;
    if (hi < addr) {
        return 1;
    }

    if (n->bar.cmbsz && nvme_addr_is_cmb(n, addr) && nvme_addr_is_cmb(n, hi)) {
        memcpy(nvme_addr_to_cmb(n, addr), buf, size);
        return 0;
    }

    if (nvme_addr_is_pmr(n, addr) && nvme_addr_is_pmr(n, hi)) {
        memcpy(nvme_addr_to_pmr(n, addr), buf, size);
        return 0;
    }

    return pci_dma_write(PCI_DEVICE(n), addr, buf, size);
}

static bool nvme_nsid_valid(NvmeCtrl *n, uint32_t nsid)
{
    return nsid &&
        (nsid == NVME_NSID_BROADCAST || nsid <= NVME_MAX_NAMESPACES);
}

static int nvme_check_sqid(NvmeCtrl *n, uint16_t sqid)
{
    return sqid < n->conf_ioqpairs + 1 && n->sq[sqid] != NULL ? 0 : -1;
}

static int nvme_check_cqid(NvmeCtrl *n, uint16_t cqid)
{
    return cqid < n->conf_ioqpairs + 1 && n->cq[cqid] != NULL ? 0 : -1;
}

static void nvme_inc_cq_tail(NvmeCQueue *cq)
{
    cq->tail++;
    if (cq->tail >= cq->size) {
        cq->tail = 0;
        cq->phase = !cq->phase;
    }
}

static void nvme_inc_sq_head(NvmeSQueue *sq)
{
    sq->head = (sq->head + 1) % sq->size;
}

static uint8_t nvme_cq_full(NvmeCQueue *cq)
{
    return (cq->tail + 1) % cq->size == cq->head;
}

static uint8_t nvme_sq_empty(NvmeSQueue *sq)
{
    return sq->head == sq->tail;
}

static void nvme_irq_check(NvmeCtrl *n)
{
    PCIDevice *pci = PCI_DEVICE(n);
    uint32_t intms = ldl_le_p(&n->bar.intms);

    if (msix_enabled(pci)) {
        return;
    }

    /* vfs does not implement intx */
    if (pci_is_vf(pci)) {
        return;
    }

    if (~intms & n->irq_status) {
        pci_irq_assert(pci);
    } else {
        pci_irq_deassert(pci);
    }
}

static void nvme_irq_assert(NvmeCtrl *n, NvmeCQueue *cq)
{
    PCIDevice *pci = PCI_DEVICE(n);

    if (cq->irq_enabled) {
        if (msix_enabled(pci)) {
            trace_pci_nvme_irq_msix(cq->vector);
            msix_notify(pci, cq->vector);
        } else {
            trace_pci_nvme_irq_pin();
            assert(cq->vector < 32);
            n->irq_status |= 1 << cq->vector;
            nvme_irq_check(n);
        }
    } else {
        trace_pci_nvme_irq_masked();
    }
}

static void nvme_irq_deassert(NvmeCtrl *n, NvmeCQueue *cq)
{
    if (cq->irq_enabled) {
        if (msix_enabled(PCI_DEVICE(n))) {
            return;
        } else {
            assert(cq->vector < 32);
            if (!n->cq_pending) {
                n->irq_status &= ~(1 << cq->vector);
            }
            nvme_irq_check(n);
        }
    }
}

static void nvme_req_clear(NvmeRequest *req)
{
    req->ns = NULL;
    req->opaque = NULL;
    req->aiocb = NULL;
    memset(&req->cqe, 0x0, sizeof(req->cqe));
    req->status = NVME_SUCCESS;
}

static inline void nvme_sg_init(NvmeCtrl *n, NvmeSg *sg, bool dma)
{
    if (dma) {
        pci_dma_sglist_init(&sg->qsg, PCI_DEVICE(n), 0);
        sg->flags = NVME_SG_DMA;
    } else {
        qemu_iovec_init(&sg->iov, 0);
    }

    sg->flags |= NVME_SG_ALLOC;
}

static inline void nvme_sg_unmap(NvmeSg *sg)
{
    if (!(sg->flags & NVME_SG_ALLOC)) {
        return;
    }

    if (sg->flags & NVME_SG_DMA) {
        qemu_sglist_destroy(&sg->qsg);
    } else {
        qemu_iovec_destroy(&sg->iov);
    }

    memset(sg, 0x0, sizeof(*sg));
}

/*
 * When metadata is transferred as extended LBAs, the DPTR mapped into `sg`
 * holds both data and metadata. This function splits the data and metadata
 * into two separate QSG/IOVs.
 */
static void nvme_sg_split(NvmeSg *sg, NvmeNamespace *ns, NvmeSg *data,
                          NvmeSg *mdata)
{
    NvmeSg *dst = data;
    uint32_t trans_len, count = ns->lbasz;
    uint64_t offset = 0;
    bool dma = sg->flags & NVME_SG_DMA;
    size_t sge_len;
    size_t sg_len = dma ? sg->qsg.size : sg->iov.size;
    int sg_idx = 0;

    assert(sg->flags & NVME_SG_ALLOC);

    while (sg_len) {
        sge_len = dma ? sg->qsg.sg[sg_idx].len : sg->iov.iov[sg_idx].iov_len;

        trans_len = MIN(sg_len, count);
        trans_len = MIN(trans_len, sge_len - offset);

        if (dst) {
            if (dma) {
                qemu_sglist_add(&dst->qsg, sg->qsg.sg[sg_idx].base + offset,
                                trans_len);
            } else {
                qemu_iovec_add(&dst->iov,
                               sg->iov.iov[sg_idx].iov_base + offset,
                               trans_len);
            }
        }

        sg_len -= trans_len;
        count -= trans_len;
        offset += trans_len;

        if (count == 0) {
            dst = (dst == data) ? mdata : data;
            count = (dst == data) ? ns->lbasz : ns->lbaf.ms;
        }

        if (sge_len == offset) {
            offset = 0;
            sg_idx++;
        }
    }
}

static uint16_t nvme_map_addr_cmb(NvmeCtrl *n, QEMUIOVector *iov, hwaddr addr,
                                  size_t len)
{
    if (!len) {
        return NVME_SUCCESS;
    }

    trace_pci_nvme_map_addr_cmb(addr, len);

    if (!nvme_addr_is_cmb(n, addr) || !nvme_addr_is_cmb(n, addr + len - 1)) {
        return NVME_DATA_TRAS_ERROR;
    }

    qemu_iovec_add(iov, nvme_addr_to_cmb(n, addr), len);

    return NVME_SUCCESS;
}

static uint16_t nvme_map_addr_pmr(NvmeCtrl *n, QEMUIOVector *iov, hwaddr addr,
                                  size_t len)
{
    if (!len) {
        return NVME_SUCCESS;
    }

    if (!nvme_addr_is_pmr(n, addr) || !nvme_addr_is_pmr(n, addr + len - 1)) {
        return NVME_DATA_TRAS_ERROR;
    }

    qemu_iovec_add(iov, nvme_addr_to_pmr(n, addr), len);

    return NVME_SUCCESS;
}

static uint16_t nvme_map_addr(NvmeCtrl *n, NvmeSg *sg, hwaddr addr, size_t len)
{
    bool cmb = false, pmr = false;

    if (!len) {
        return NVME_SUCCESS;
    }

    trace_pci_nvme_map_addr(addr, len);

    if (nvme_addr_is_iomem(n, addr)) {
        return NVME_DATA_TRAS_ERROR;
    }

    if (nvme_addr_is_cmb(n, addr)) {
        cmb = true;
    } else if (nvme_addr_is_pmr(n, addr)) {
        pmr = true;
    }

    if (cmb || pmr) {
        if (sg->flags & NVME_SG_DMA) {
            return NVME_INVALID_USE_OF_CMB | NVME_DNR;
        }

        if (sg->iov.niov + 1 > IOV_MAX) {
            goto max_mappings_exceeded;
        }

        if (cmb) {
            return nvme_map_addr_cmb(n, &sg->iov, addr, len);
        } else {
            return nvme_map_addr_pmr(n, &sg->iov, addr, len);
        }
    }

    if (!(sg->flags & NVME_SG_DMA)) {
        return NVME_INVALID_USE_OF_CMB | NVME_DNR;
    }

    if (sg->qsg.nsg + 1 > IOV_MAX) {
        goto max_mappings_exceeded;
    }

    qemu_sglist_add(&sg->qsg, addr, len);

    return NVME_SUCCESS;

max_mappings_exceeded:
    NVME_GUEST_ERR(pci_nvme_ub_too_many_mappings,
                   "number of mappings exceed 1024");
    return NVME_INTERNAL_DEV_ERROR | NVME_DNR;
}

static inline bool nvme_addr_is_dma(NvmeCtrl *n, hwaddr addr)
{
    return !(nvme_addr_is_cmb(n, addr) || nvme_addr_is_pmr(n, addr));
}

static uint16_t nvme_map_prp(NvmeCtrl *n, NvmeSg *sg, uint64_t prp1,
                             uint64_t prp2, uint32_t len)
{
    hwaddr trans_len = n->page_size - (prp1 % n->page_size);
    trans_len = MIN(len, trans_len);
    int num_prps = (len >> n->page_bits) + 1;
    uint16_t status;
    int ret;

    trace_pci_nvme_map_prp(trans_len, len, prp1, prp2, num_prps);

    nvme_sg_init(n, sg, nvme_addr_is_dma(n, prp1));

    status = nvme_map_addr(n, sg, prp1, trans_len);
    if (status) {
        goto unmap;
    }

    len -= trans_len;
    if (len) {
        if (len > n->page_size) {
            g_autofree uint64_t *prp_list = g_new(uint64_t, n->max_prp_ents);
            uint32_t nents, prp_trans;
            int i = 0;

            /*
             * The first PRP list entry, pointed to by PRP2 may contain offset.
             * Hence, we need to calculate the number of entries in based on
             * that offset.
             */
            nents = (n->page_size - (prp2 & (n->page_size - 1))) >> 3;
            prp_trans = MIN(n->max_prp_ents, nents) * sizeof(uint64_t);
            ret = nvme_addr_read(n, prp2, (void *)prp_list, prp_trans);
            if (ret) {
                trace_pci_nvme_err_addr_read(prp2);
                status = NVME_DATA_TRAS_ERROR;
                goto unmap;
            }
            while (len != 0) {
                uint64_t prp_ent = le64_to_cpu(prp_list[i]);

                if (i == nents - 1 && len > n->page_size) {
                    if (unlikely(prp_ent & (n->page_size - 1))) {
                        trace_pci_nvme_err_invalid_prplist_ent(prp_ent);
                        status = NVME_INVALID_PRP_OFFSET | NVME_DNR;
                        goto unmap;
                    }

                    i = 0;
                    nents = (len + n->page_size - 1) >> n->page_bits;
                    nents = MIN(nents, n->max_prp_ents);
                    prp_trans = nents * sizeof(uint64_t);
                    ret = nvme_addr_read(n, prp_ent, (void *)prp_list,
                                         prp_trans);
                    if (ret) {
                        trace_pci_nvme_err_addr_read(prp_ent);
                        status = NVME_DATA_TRAS_ERROR;
                        goto unmap;
                    }
                    prp_ent = le64_to_cpu(prp_list[i]);
                }

                if (unlikely(prp_ent & (n->page_size - 1))) {
                    trace_pci_nvme_err_invalid_prplist_ent(prp_ent);
                    status = NVME_INVALID_PRP_OFFSET | NVME_DNR;
                    goto unmap;
                }

                trans_len = MIN(len, n->page_size);
                status = nvme_map_addr(n, sg, prp_ent, trans_len);
                if (status) {
                    goto unmap;
                }

                len -= trans_len;
                i++;
            }
        } else {
            if (unlikely(prp2 & (n->page_size - 1))) {
                trace_pci_nvme_err_invalid_prp2_align(prp2);
                status = NVME_INVALID_PRP_OFFSET | NVME_DNR;
                goto unmap;
            }
            status = nvme_map_addr(n, sg, prp2, len);
            if (status) {
                goto unmap;
            }
        }
    }

    return NVME_SUCCESS;

unmap:
    nvme_sg_unmap(sg);
    return status;
}

/*
 * Map 'nsgld' data descriptors from 'segment'. The function will subtract the
 * number of bytes mapped in len.
 */
static uint16_t nvme_map_sgl_data(NvmeCtrl *n, NvmeSg *sg,
                                  NvmeSglDescriptor *segment, uint64_t nsgld,
                                  size_t *len, NvmeCmd *cmd)
{
    dma_addr_t addr, trans_len;
    uint32_t dlen;
    uint16_t status;

    for (int i = 0; i < nsgld; i++) {
        uint8_t type = NVME_SGL_TYPE(segment[i].type);

        switch (type) {
        case NVME_SGL_DESCR_TYPE_DATA_BLOCK:
            break;
        case NVME_SGL_DESCR_TYPE_SEGMENT:
        case NVME_SGL_DESCR_TYPE_LAST_SEGMENT:
            return NVME_INVALID_NUM_SGL_DESCRS | NVME_DNR;
        default:
            return NVME_SGL_DESCR_TYPE_INVALID | NVME_DNR;
        }

        dlen = le32_to_cpu(segment[i].len);

        if (!dlen) {
            continue;
        }

        if (*len == 0) {
            /*
             * All data has been mapped, but the SGL contains additional
             * segments and/or descriptors. The controller might accept
             * ignoring the rest of the SGL.
             */
            uint32_t sgls = le32_to_cpu(n->id_ctrl.sgls);
            if (sgls & NVME_CTRL_SGLS_EXCESS_LENGTH) {
                break;
            }

            trace_pci_nvme_err_invalid_sgl_excess_length(dlen);
            return NVME_DATA_SGL_LEN_INVALID | NVME_DNR;
        }

        trans_len = MIN(*len, dlen);

        addr = le64_to_cpu(segment[i].addr);

        if (UINT64_MAX - addr < dlen) {
            return NVME_DATA_SGL_LEN_INVALID | NVME_DNR;
        }

        status = nvme_map_addr(n, sg, addr, trans_len);
        if (status) {
            return status;
        }

        *len -= trans_len;
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_map_sgl(NvmeCtrl *n, NvmeSg *sg, NvmeSglDescriptor sgl,
                             size_t len, NvmeCmd *cmd)
{
    /*
     * Read the segment in chunks of 256 descriptors (one 4k page) to avoid
     * dynamically allocating a potentially huge SGL. The spec allows the SGL
     * to be larger (as in number of bytes required to describe the SGL
     * descriptors and segment chain) than the command transfer size, so it is
     * not bounded by MDTS.
     */
#define SEG_CHUNK_SIZE 256

    NvmeSglDescriptor segment[SEG_CHUNK_SIZE], *sgld, *last_sgld;
    uint64_t nsgld;
    uint32_t seg_len;
    uint16_t status;
    hwaddr addr;
    int ret;

    sgld = &sgl;
    addr = le64_to_cpu(sgl.addr);

    trace_pci_nvme_map_sgl(NVME_SGL_TYPE(sgl.type), len);

    nvme_sg_init(n, sg, nvme_addr_is_dma(n, addr));

    /*
     * If the entire transfer can be described with a single data block it can
     * be mapped directly.
     */
    if (NVME_SGL_TYPE(sgl.type) == NVME_SGL_DESCR_TYPE_DATA_BLOCK) {
        status = nvme_map_sgl_data(n, sg, sgld, 1, &len, cmd);
        if (status) {
            goto unmap;
        }

        goto out;
    }

    for (;;) {
        switch (NVME_SGL_TYPE(sgld->type)) {
        case NVME_SGL_DESCR_TYPE_SEGMENT:
        case NVME_SGL_DESCR_TYPE_LAST_SEGMENT:
            break;
        default:
            return NVME_INVALID_SGL_SEG_DESCR | NVME_DNR;
        }

        seg_len = le32_to_cpu(sgld->len);

        /* check the length of the (Last) Segment descriptor */
        if (!seg_len || seg_len & 0xf) {
            return NVME_INVALID_SGL_SEG_DESCR | NVME_DNR;
        }

        if (UINT64_MAX - addr < seg_len) {
            return NVME_DATA_SGL_LEN_INVALID | NVME_DNR;
        }

        nsgld = seg_len / sizeof(NvmeSglDescriptor);

        while (nsgld > SEG_CHUNK_SIZE) {
            if (nvme_addr_read(n, addr, segment, sizeof(segment))) {
                trace_pci_nvme_err_addr_read(addr);
                status = NVME_DATA_TRAS_ERROR;
                goto unmap;
            }

            status = nvme_map_sgl_data(n, sg, segment, SEG_CHUNK_SIZE,
                                       &len, cmd);
            if (status) {
                goto unmap;
            }

            nsgld -= SEG_CHUNK_SIZE;
            addr += SEG_CHUNK_SIZE * sizeof(NvmeSglDescriptor);
        }

        ret = nvme_addr_read(n, addr, segment, nsgld *
                             sizeof(NvmeSglDescriptor));
        if (ret) {
            trace_pci_nvme_err_addr_read(addr);
            status = NVME_DATA_TRAS_ERROR;
            goto unmap;
        }

        last_sgld = &segment[nsgld - 1];

        /*
         * If the segment ends with a Data Block, then we are done.
         */
        if (NVME_SGL_TYPE(last_sgld->type) == NVME_SGL_DESCR_TYPE_DATA_BLOCK) {
            status = nvme_map_sgl_data(n, sg, segment, nsgld, &len, cmd);
            if (status) {
                goto unmap;
            }

            goto out;
        }

        /*
         * If the last descriptor was not a Data Block, then the current
         * segment must not be a Last Segment.
         */
        if (NVME_SGL_TYPE(sgld->type) == NVME_SGL_DESCR_TYPE_LAST_SEGMENT) {
            status = NVME_INVALID_SGL_SEG_DESCR | NVME_DNR;
            goto unmap;
        }

        sgld = last_sgld;
        addr = le64_to_cpu(sgld->addr);

        /*
         * Do not map the last descriptor; it will be a Segment or Last Segment
         * descriptor and is handled by the next iteration.
         */
        status = nvme_map_sgl_data(n, sg, segment, nsgld - 1, &len, cmd);
        if (status) {
            goto unmap;
        }
    }

out:
    /* if there is any residual left in len, the SGL was too short */
    if (len) {
        status = NVME_DATA_SGL_LEN_INVALID | NVME_DNR;
        goto unmap;
    }

    return NVME_SUCCESS;

unmap:
    nvme_sg_unmap(sg);
    return status;
}

uint16_t nvme_map_dptr(NvmeCtrl *n, NvmeSg *sg, size_t len,
                       NvmeCmd *cmd)
{
    uint64_t prp1, prp2;

    switch (NVME_CMD_FLAGS_PSDT(cmd->flags)) {
    case NVME_PSDT_PRP:
        prp1 = le64_to_cpu(cmd->dptr.prp1);
        prp2 = le64_to_cpu(cmd->dptr.prp2);

        return nvme_map_prp(n, sg, prp1, prp2, len);
    case NVME_PSDT_SGL_MPTR_CONTIGUOUS:
    case NVME_PSDT_SGL_MPTR_SGL:
        return nvme_map_sgl(n, sg, cmd->dptr.sgl, len, cmd);
    default:
        return NVME_INVALID_FIELD;
    }
}

static uint16_t nvme_map_mptr(NvmeCtrl *n, NvmeSg *sg, size_t len,
                              NvmeCmd *cmd)
{
    int psdt = NVME_CMD_FLAGS_PSDT(cmd->flags);
    hwaddr mptr = le64_to_cpu(cmd->mptr);
    uint16_t status;

    if (psdt == NVME_PSDT_SGL_MPTR_SGL) {
        NvmeSglDescriptor sgl;

        if (nvme_addr_read(n, mptr, &sgl, sizeof(sgl))) {
            return NVME_DATA_TRAS_ERROR;
        }

        status = nvme_map_sgl(n, sg, sgl, len, cmd);
        if (status && (status & 0x7ff) == NVME_DATA_SGL_LEN_INVALID) {
            status = NVME_MD_SGL_LEN_INVALID | NVME_DNR;
        }

        return status;
    }

    nvme_sg_init(n, sg, nvme_addr_is_dma(n, mptr));
    status = nvme_map_addr(n, sg, mptr, len);
    if (status) {
        nvme_sg_unmap(sg);
    }

    return status;
}

static uint16_t nvme_map_data(NvmeCtrl *n, uint32_t nlb, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    bool pi = !!NVME_ID_NS_DPS_TYPE(ns->id_ns.dps);
    bool pract = !!(le16_to_cpu(rw->control) & NVME_RW_PRINFO_PRACT);
    size_t len = nvme_l2b(ns, nlb);
    uint16_t status;

    if (nvme_ns_ext(ns) &&
        !(pi && pract && ns->lbaf.ms == nvme_pi_tuple_size(ns))) {
        NvmeSg sg;

        len += nvme_m2b(ns, nlb);

        status = nvme_map_dptr(n, &sg, len, &req->cmd);
        if (status) {
            return status;
        }

        nvme_sg_init(n, &req->sg, sg.flags & NVME_SG_DMA);
        nvme_sg_split(&sg, ns, &req->sg, NULL);
        nvme_sg_unmap(&sg);

        return NVME_SUCCESS;
    }

    return nvme_map_dptr(n, &req->sg, len, &req->cmd);
}

static uint16_t nvme_map_mdata(NvmeCtrl *n, uint32_t nlb, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    size_t len = nvme_m2b(ns, nlb);
    uint16_t status;

    if (nvme_ns_ext(ns)) {
        NvmeSg sg;

        len += nvme_l2b(ns, nlb);

        status = nvme_map_dptr(n, &sg, len, &req->cmd);
        if (status) {
            return status;
        }

        nvme_sg_init(n, &req->sg, sg.flags & NVME_SG_DMA);
        nvme_sg_split(&sg, ns, NULL, &req->sg);
        nvme_sg_unmap(&sg);

        return NVME_SUCCESS;
    }

    return nvme_map_mptr(n, &req->sg, len, &req->cmd);
}

static uint16_t nvme_tx_interleaved(NvmeCtrl *n, NvmeSg *sg, uint8_t *ptr,
                                    uint32_t len, uint32_t bytes,
                                    int32_t skip_bytes, int64_t offset,
                                    NvmeTxDirection dir)
{
    hwaddr addr;
    uint32_t trans_len, count = bytes;
    bool dma = sg->flags & NVME_SG_DMA;
    int64_t sge_len;
    int sg_idx = 0;
    int ret;

    assert(sg->flags & NVME_SG_ALLOC);

    while (len) {
        sge_len = dma ? sg->qsg.sg[sg_idx].len : sg->iov.iov[sg_idx].iov_len;

        if (sge_len - offset < 0) {
            offset -= sge_len;
            sg_idx++;
            continue;
        }

        if (sge_len == offset) {
            offset = 0;
            sg_idx++;
            continue;
        }

        trans_len = MIN(len, count);
        trans_len = MIN(trans_len, sge_len - offset);

        if (dma) {
            addr = sg->qsg.sg[sg_idx].base + offset;
        } else {
            addr = (hwaddr)(uintptr_t)sg->iov.iov[sg_idx].iov_base + offset;
        }

        if (dir == NVME_TX_DIRECTION_TO_DEVICE) {
            ret = nvme_addr_read(n, addr, ptr, trans_len);
        } else {
            ret = nvme_addr_write(n, addr, ptr, trans_len);
        }

        if (ret) {
            return NVME_DATA_TRAS_ERROR;
        }

        ptr += trans_len;
        len -= trans_len;
        count -= trans_len;
        offset += trans_len;

        if (count == 0) {
            count = bytes;
            offset += skip_bytes;
        }
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_tx(NvmeCtrl *n, NvmeSg *sg, void *ptr, uint32_t len,
                        NvmeTxDirection dir)
{
    assert(sg->flags & NVME_SG_ALLOC);

    if (sg->flags & NVME_SG_DMA) {
        const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
        dma_addr_t residual;

        if (dir == NVME_TX_DIRECTION_TO_DEVICE) {
            dma_buf_write(ptr, len, &residual, &sg->qsg, attrs);
        } else {
            dma_buf_read(ptr, len, &residual, &sg->qsg, attrs);
        }

        if (unlikely(residual)) {
            trace_pci_nvme_err_invalid_dma();
            return NVME_INVALID_FIELD | NVME_DNR;
        }
    } else {
        size_t bytes;

        if (dir == NVME_TX_DIRECTION_TO_DEVICE) {
            bytes = qemu_iovec_to_buf(&sg->iov, 0, ptr, len);
        } else {
            bytes = qemu_iovec_from_buf(&sg->iov, 0, ptr, len);
        }

        if (unlikely(bytes != len)) {
            trace_pci_nvme_err_invalid_dma();
            return NVME_INVALID_FIELD | NVME_DNR;
        }
    }

    return NVME_SUCCESS;
}

static inline uint16_t nvme_c2h(NvmeCtrl *n, void *ptr, uint32_t len,
                                NvmeRequest *req)
{
    uint16_t status;

    status = nvme_map_dptr(n, &req->sg, len, &req->cmd);
    if (status) {
        return status;
    }

    return nvme_tx(n, &req->sg, ptr, len, NVME_TX_DIRECTION_FROM_DEVICE);
}

static inline uint16_t nvme_h2c(NvmeCtrl *n, void *ptr, uint32_t len,
                                NvmeRequest *req)
{
    uint16_t status;

    status = nvme_map_dptr(n, &req->sg, len, &req->cmd);
    if (status) {
        return status;
    }

    return nvme_tx(n, &req->sg, ptr, len, NVME_TX_DIRECTION_TO_DEVICE);
}

uint16_t nvme_bounce_data(NvmeCtrl *n, void *ptr, uint32_t len,
                          NvmeTxDirection dir, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    bool pi = !!NVME_ID_NS_DPS_TYPE(ns->id_ns.dps);
    bool pract = !!(le16_to_cpu(rw->control) & NVME_RW_PRINFO_PRACT);

    if (nvme_ns_ext(ns) &&
        !(pi && pract && ns->lbaf.ms == nvme_pi_tuple_size(ns))) {
        return nvme_tx_interleaved(n, &req->sg, ptr, len, ns->lbasz,
                                   ns->lbaf.ms, 0, dir);
    }

    return nvme_tx(n, &req->sg, ptr, len, dir);
}

uint16_t nvme_bounce_mdata(NvmeCtrl *n, void *ptr, uint32_t len,
                           NvmeTxDirection dir, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    uint16_t status;

    if (nvme_ns_ext(ns)) {
        return nvme_tx_interleaved(n, &req->sg, ptr, len, ns->lbaf.ms,
                                   ns->lbasz, ns->lbasz, dir);
    }

    nvme_sg_unmap(&req->sg);

    status = nvme_map_mptr(n, &req->sg, len, &req->cmd);
    if (status) {
        return status;
    }

    return nvme_tx(n, &req->sg, ptr, len, dir);
}

static inline void nvme_blk_read(BlockBackend *blk, int64_t offset,
                                 uint32_t align, BlockCompletionFunc *cb,
                                 NvmeRequest *req)
{
    assert(req->sg.flags & NVME_SG_ALLOC);

    if (req->sg.flags & NVME_SG_DMA) {
        req->aiocb = dma_blk_read(blk, &req->sg.qsg, offset, align, cb, req);
    } else {
        req->aiocb = blk_aio_preadv(blk, offset, &req->sg.iov, 0, cb, req);
    }
}

static inline void nvme_blk_write(BlockBackend *blk, int64_t offset,
                                  uint32_t align, BlockCompletionFunc *cb,
                                  NvmeRequest *req)
{
    assert(req->sg.flags & NVME_SG_ALLOC);

    if (req->sg.flags & NVME_SG_DMA) {
        req->aiocb = dma_blk_write(blk, &req->sg.qsg, offset, align, cb, req);
    } else {
        req->aiocb = blk_aio_pwritev(blk, offset, &req->sg.iov, 0, cb, req);
    }
}

static void nvme_update_cq_eventidx(const NvmeCQueue *cq)
{
    trace_pci_nvme_update_cq_eventidx(cq->cqid, cq->head);

    stl_le_pci_dma(PCI_DEVICE(cq->ctrl), cq->ei_addr, cq->head,
                   MEMTXATTRS_UNSPECIFIED);
}

static void nvme_update_cq_head(NvmeCQueue *cq)
{
    ldl_le_pci_dma(PCI_DEVICE(cq->ctrl), cq->db_addr, &cq->head,
                   MEMTXATTRS_UNSPECIFIED);

    trace_pci_nvme_update_cq_head(cq->cqid, cq->head);
}

static void nvme_post_cqes(void *opaque)
{
    NvmeCQueue *cq = opaque;
    NvmeCtrl *n = cq->ctrl;
    NvmeRequest *req, *next;
    bool pending = cq->head != cq->tail;
    int ret;

    QTAILQ_FOREACH_SAFE(req, &cq->req_list, entry, next) {
        NvmeSQueue *sq;
        hwaddr addr;

        if (n->dbbuf_enabled) {
            nvme_update_cq_eventidx(cq);
            nvme_update_cq_head(cq);
        }

        if (nvme_cq_full(cq)) {
            break;
        }

        sq = req->sq;
        req->cqe.status = cpu_to_le16((req->status << 1) | cq->phase);
        req->cqe.sq_id = cpu_to_le16(sq->sqid);
        req->cqe.sq_head = cpu_to_le16(sq->head);
        addr = cq->dma_addr + (cq->tail << NVME_CQES);
        ret = pci_dma_write(PCI_DEVICE(n), addr, (void *)&req->cqe,
                            sizeof(req->cqe));
        if (ret) {
            trace_pci_nvme_err_addr_write(addr);
            trace_pci_nvme_err_cfs();
            stl_le_p(&n->bar.csts, NVME_CSTS_FAILED);
            break;
        }

        QTAILQ_REMOVE(&cq->req_list, req, entry);

        nvme_inc_cq_tail(cq);
        nvme_sg_unmap(&req->sg);

        if (QTAILQ_EMPTY(&sq->req_list) && !nvme_sq_empty(sq)) {
            qemu_bh_schedule(sq->bh);
        }

        QTAILQ_INSERT_TAIL(&sq->req_list, req, entry);
    }
    if (cq->tail != cq->head) {
        if (cq->irq_enabled && !pending) {
            n->cq_pending++;
        }

        nvme_irq_assert(n, cq);
    }
}

static void nvme_enqueue_req_completion(NvmeCQueue *cq, NvmeRequest *req)
{
    assert(cq->cqid == req->sq->cqid);
    trace_pci_nvme_enqueue_req_completion(nvme_cid(req), cq->cqid,
                                          le32_to_cpu(req->cqe.result),
                                          le32_to_cpu(req->cqe.dw1),
                                          req->status);

    if (req->status) {
        trace_pci_nvme_err_req_status(nvme_cid(req), nvme_nsid(req->ns),
                                      req->status, req->cmd.opcode);
    }

    QTAILQ_REMOVE(&req->sq->out_req_list, req, entry);
    QTAILQ_INSERT_TAIL(&cq->req_list, req, entry);

    qemu_bh_schedule(cq->bh);
}

static void nvme_process_aers(void *opaque)
{
    NvmeCtrl *n = opaque;
    NvmeAsyncEvent *event, *next;

    trace_pci_nvme_process_aers(n->aer_queued);

    QTAILQ_FOREACH_SAFE(event, &n->aer_queue, entry, next) {
        NvmeRequest *req;
        NvmeAerResult *result;

        /* can't post cqe if there is nothing to complete */
        if (!n->outstanding_aers) {
            trace_pci_nvme_no_outstanding_aers();
            break;
        }

        /* ignore if masked (cqe posted, but event not cleared) */
        if (n->aer_mask & (1 << event->result.event_type)) {
            trace_pci_nvme_aer_masked(event->result.event_type, n->aer_mask);
            continue;
        }

        QTAILQ_REMOVE(&n->aer_queue, event, entry);
        n->aer_queued--;

        n->aer_mask |= 1 << event->result.event_type;
        n->outstanding_aers--;

        req = n->aer_reqs[n->outstanding_aers];

        result = (NvmeAerResult *) &req->cqe.result;
        result->event_type = event->result.event_type;
        result->event_info = event->result.event_info;
        result->log_page = event->result.log_page;
        g_free(event);

        trace_pci_nvme_aer_post_cqe(result->event_type, result->event_info,
                                    result->log_page);

        nvme_enqueue_req_completion(&n->admin_cq, req);
    }
}

static void nvme_enqueue_event(NvmeCtrl *n, uint8_t event_type,
                               uint8_t event_info, uint8_t log_page)
{
    NvmeAsyncEvent *event;

    trace_pci_nvme_enqueue_event(event_type, event_info, log_page);

    if (n->aer_queued == n->params.aer_max_queued) {
        trace_pci_nvme_enqueue_event_noqueue(n->aer_queued);
        return;
    }

    event = g_new(NvmeAsyncEvent, 1);
    event->result = (NvmeAerResult) {
        .event_type = event_type,
        .event_info = event_info,
        .log_page   = log_page,
    };

    QTAILQ_INSERT_TAIL(&n->aer_queue, event, entry);
    n->aer_queued++;

    nvme_process_aers(n);
}

static void nvme_smart_event(NvmeCtrl *n, uint8_t event)
{
    uint8_t aer_info;

    /* Ref SPEC <Asynchronous Event Information 0x2013 SMART / Health Status> */
    if (!(NVME_AEC_SMART(n->features.async_config) & event)) {
        return;
    }

    switch (event) {
    case NVME_SMART_SPARE:
        aer_info = NVME_AER_INFO_SMART_SPARE_THRESH;
        break;
    case NVME_SMART_TEMPERATURE:
        aer_info = NVME_AER_INFO_SMART_TEMP_THRESH;
        break;
    case NVME_SMART_RELIABILITY:
    case NVME_SMART_MEDIA_READ_ONLY:
    case NVME_SMART_FAILED_VOLATILE_MEDIA:
    case NVME_SMART_PMR_UNRELIABLE:
        aer_info = NVME_AER_INFO_SMART_RELIABILITY;
        break;
    default:
        return;
    }

    nvme_enqueue_event(n, NVME_AER_TYPE_SMART, aer_info, NVME_LOG_SMART_INFO);
}

static void nvme_clear_events(NvmeCtrl *n, uint8_t event_type)
{
    NvmeAsyncEvent *event, *next;

    n->aer_mask &= ~(1 << event_type);

    QTAILQ_FOREACH_SAFE(event, &n->aer_queue, entry, next) {
        if (event->result.event_type == event_type) {
            QTAILQ_REMOVE(&n->aer_queue, event, entry);
            n->aer_queued--;
            g_free(event);
        }
    }
}

static inline uint16_t nvme_check_mdts(NvmeCtrl *n, size_t len)
{
    uint8_t mdts = n->params.mdts;

    if (mdts && len > n->page_size << mdts) {
        trace_pci_nvme_err_mdts(len);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static inline uint16_t nvme_check_bounds(NvmeNamespace *ns, uint64_t slba,
                                         uint32_t nlb)
{
    uint64_t nsze = le64_to_cpu(ns->id_ns.nsze);

    if (unlikely(UINT64_MAX - slba < nlb || slba + nlb > nsze)) {
        trace_pci_nvme_err_invalid_lba_range(slba, nlb, nsze);
        return NVME_LBA_RANGE | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static int nvme_block_status_all(NvmeNamespace *ns, uint64_t slba,
                                 uint32_t nlb, int flags)
{
    BlockDriverState *bs = blk_bs(ns->blkconf.blk);

    int64_t pnum = 0, bytes = nvme_l2b(ns, nlb);
    int64_t offset = nvme_l2b(ns, slba);
    int ret;

    /*
     * `pnum` holds the number of bytes after offset that shares the same
     * allocation status as the byte at offset. If `pnum` is different from
     * `bytes`, we should check the allocation status of the next range and
     * continue this until all bytes have been checked.
     */
    do {
        bytes -= pnum;

        ret = bdrv_block_status(bs, offset, bytes, &pnum, NULL, NULL);
        if (ret < 0) {
            return ret;
        }


        trace_pci_nvme_block_status(offset, bytes, pnum, ret,
                                    !!(ret & BDRV_BLOCK_ZERO));

        if (!(ret & flags)) {
            return 1;
        }

        offset += pnum;
    } while (pnum != bytes);

    return 0;
}

static uint16_t nvme_check_dulbe(NvmeNamespace *ns, uint64_t slba,
                                 uint32_t nlb)
{
    int ret;
    Error *err = NULL;

    ret = nvme_block_status_all(ns, slba, nlb, BDRV_BLOCK_DATA);
    if (ret) {
        if (ret < 0) {
            error_setg_errno(&err, -ret, "unable to get block status");
            error_report_err(err);

            return NVME_INTERNAL_DEV_ERROR;
        }

        return NVME_DULB;
    }

    return NVME_SUCCESS;
}

static inline uint32_t nvme_zone_idx(NvmeNamespace *ns, uint64_t slba)
{
    return ns->zone_size_log2 > 0 ? slba >> ns->zone_size_log2 :
                                    slba / ns->zone_size;
}

static inline NvmeZone *nvme_get_zone_by_slba(NvmeNamespace *ns, uint64_t slba)
{
    uint32_t zone_idx = nvme_zone_idx(ns, slba);

    if (zone_idx >= ns->num_zones) {
        return NULL;
    }

    return &ns->zone_array[zone_idx];
}

static uint16_t nvme_check_zone_state_for_write(NvmeZone *zone)
{
    uint64_t zslba = zone->d.zslba;

    switch (nvme_get_zone_state(zone)) {
    case NVME_ZONE_STATE_EMPTY:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_CLOSED:
        return NVME_SUCCESS;
    case NVME_ZONE_STATE_FULL:
        trace_pci_nvme_err_zone_is_full(zslba);
        return NVME_ZONE_FULL;
    case NVME_ZONE_STATE_OFFLINE:
        trace_pci_nvme_err_zone_is_offline(zslba);
        return NVME_ZONE_OFFLINE;
    case NVME_ZONE_STATE_READ_ONLY:
        trace_pci_nvme_err_zone_is_read_only(zslba);
        return NVME_ZONE_READ_ONLY;
    default:
        g_assert_not_reached();
    }

    return NVME_INTERNAL_DEV_ERROR;
}

static uint16_t nvme_check_zone_write(NvmeNamespace *ns, NvmeZone *zone,
                                      uint64_t slba, uint32_t nlb)
{
    uint64_t zcap = nvme_zone_wr_boundary(zone);
    uint16_t status;

    status = nvme_check_zone_state_for_write(zone);
    if (status) {
        return status;
    }

    if (zone->d.za & NVME_ZA_ZRWA_VALID) {
        uint64_t ezrwa = zone->w_ptr + 2 * ns->zns.zrwas;

        if (slba < zone->w_ptr || slba + nlb > ezrwa) {
            trace_pci_nvme_err_zone_invalid_write(slba, zone->w_ptr);
            return NVME_ZONE_INVALID_WRITE;
        }
    } else {
        if (unlikely(slba != zone->w_ptr)) {
            trace_pci_nvme_err_write_not_at_wp(slba, zone->d.zslba,
                                               zone->w_ptr);
            return NVME_ZONE_INVALID_WRITE;
        }
    }

    if (unlikely((slba + nlb) > zcap)) {
        trace_pci_nvme_err_zone_boundary(slba, nlb, zcap);
        return NVME_ZONE_BOUNDARY_ERROR;
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_check_zone_state_for_read(NvmeZone *zone)
{
    switch (nvme_get_zone_state(zone)) {
    case NVME_ZONE_STATE_EMPTY:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_FULL:
    case NVME_ZONE_STATE_CLOSED:
    case NVME_ZONE_STATE_READ_ONLY:
        return NVME_SUCCESS;
    case NVME_ZONE_STATE_OFFLINE:
        trace_pci_nvme_err_zone_is_offline(zone->d.zslba);
        return NVME_ZONE_OFFLINE;
    default:
        g_assert_not_reached();
    }

    return NVME_INTERNAL_DEV_ERROR;
}

static uint16_t nvme_check_zone_read(NvmeNamespace *ns, uint64_t slba,
                                     uint32_t nlb)
{
    NvmeZone *zone;
    uint64_t bndry, end;
    uint16_t status;

    zone = nvme_get_zone_by_slba(ns, slba);
    assert(zone);

    bndry = nvme_zone_rd_boundary(ns, zone);
    end = slba + nlb;

    status = nvme_check_zone_state_for_read(zone);
    if (status) {
        ;
    } else if (unlikely(end > bndry)) {
        if (!ns->params.cross_zone_read) {
            status = NVME_ZONE_BOUNDARY_ERROR;
        } else {
            /*
             * Read across zone boundary - check that all subsequent
             * zones that are being read have an appropriate state.
             */
            do {
                zone++;
                status = nvme_check_zone_state_for_read(zone);
                if (status) {
                    break;
                }
            } while (end > nvme_zone_rd_boundary(ns, zone));
        }
    }

    return status;
}

static uint16_t nvme_zrm_finish(NvmeNamespace *ns, NvmeZone *zone)
{
    switch (nvme_get_zone_state(zone)) {
    case NVME_ZONE_STATE_FULL:
        return NVME_SUCCESS;

    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        nvme_aor_dec_open(ns);
        /* fallthrough */
    case NVME_ZONE_STATE_CLOSED:
        nvme_aor_dec_active(ns);

        if (zone->d.za & NVME_ZA_ZRWA_VALID) {
            zone->d.za &= ~NVME_ZA_ZRWA_VALID;
            if (ns->params.numzrwa) {
                ns->zns.numzrwa++;
            }
        }

        /* fallthrough */
    case NVME_ZONE_STATE_EMPTY:
        nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_FULL);
        return NVME_SUCCESS;

    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static uint16_t nvme_zrm_close(NvmeNamespace *ns, NvmeZone *zone)
{
    switch (nvme_get_zone_state(zone)) {
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        nvme_aor_dec_open(ns);
        nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_CLOSED);
        /* fall through */
    case NVME_ZONE_STATE_CLOSED:
        return NVME_SUCCESS;

    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static uint16_t nvme_zrm_reset(NvmeNamespace *ns, NvmeZone *zone)
{
    switch (nvme_get_zone_state(zone)) {
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        nvme_aor_dec_open(ns);
        /* fallthrough */
    case NVME_ZONE_STATE_CLOSED:
        nvme_aor_dec_active(ns);

        if (zone->d.za & NVME_ZA_ZRWA_VALID) {
            if (ns->params.numzrwa) {
                ns->zns.numzrwa++;
            }
        }

        /* fallthrough */
    case NVME_ZONE_STATE_FULL:
        zone->w_ptr = zone->d.zslba;
        zone->d.wp = zone->w_ptr;
        nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_EMPTY);
        /* fallthrough */
    case NVME_ZONE_STATE_EMPTY:
        return NVME_SUCCESS;

    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static void nvme_zrm_auto_transition_zone(NvmeNamespace *ns)
{
    NvmeZone *zone;

    if (ns->params.max_open_zones &&
        ns->nr_open_zones == ns->params.max_open_zones) {
        zone = QTAILQ_FIRST(&ns->imp_open_zones);
        if (zone) {
            /*
             * Automatically close this implicitly open zone.
             */
            QTAILQ_REMOVE(&ns->imp_open_zones, zone, entry);
            nvme_zrm_close(ns, zone);
        }
    }
}

enum {
    NVME_ZRM_AUTO = 1 << 0,
    NVME_ZRM_ZRWA = 1 << 1,
};

static uint16_t nvme_zrm_open_flags(NvmeCtrl *n, NvmeNamespace *ns,
                                    NvmeZone *zone, int flags)
{
    int act = 0;
    uint16_t status;

    switch (nvme_get_zone_state(zone)) {
    case NVME_ZONE_STATE_EMPTY:
        act = 1;

        /* fallthrough */

    case NVME_ZONE_STATE_CLOSED:
        if (n->params.auto_transition_zones) {
            nvme_zrm_auto_transition_zone(ns);
        }
        status = nvme_zns_check_resources(ns, act, 1,
                                          (flags & NVME_ZRM_ZRWA) ? 1 : 0);
        if (status) {
            return status;
        }

        if (act) {
            nvme_aor_inc_active(ns);
        }

        nvme_aor_inc_open(ns);

        if (flags & NVME_ZRM_AUTO) {
            nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_IMPLICITLY_OPEN);
            return NVME_SUCCESS;
        }

        /* fallthrough */

    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        if (flags & NVME_ZRM_AUTO) {
            return NVME_SUCCESS;
        }

        nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_EXPLICITLY_OPEN);

        /* fallthrough */

    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        if (flags & NVME_ZRM_ZRWA) {
            ns->zns.numzrwa--;

            zone->d.za |= NVME_ZA_ZRWA_VALID;
        }

        return NVME_SUCCESS;

    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static inline uint16_t nvme_zrm_auto(NvmeCtrl *n, NvmeNamespace *ns,
                                     NvmeZone *zone)
{
    return nvme_zrm_open_flags(n, ns, zone, NVME_ZRM_AUTO);
}

static void nvme_advance_zone_wp(NvmeNamespace *ns, NvmeZone *zone,
                                 uint32_t nlb)
{
    zone->d.wp += nlb;

    if (zone->d.wp == nvme_zone_wr_boundary(zone)) {
        nvme_zrm_finish(ns, zone);
    }
}

static void nvme_zoned_zrwa_implicit_flush(NvmeNamespace *ns, NvmeZone *zone,
                                           uint32_t nlbc)
{
    uint16_t nzrwafgs = DIV_ROUND_UP(nlbc, ns->zns.zrwafg);

    nlbc = nzrwafgs * ns->zns.zrwafg;

    trace_pci_nvme_zoned_zrwa_implicit_flush(zone->d.zslba, nlbc);

    zone->w_ptr += nlbc;

    nvme_advance_zone_wp(ns, zone, nlbc);
}

static void nvme_finalize_zoned_write(NvmeNamespace *ns, NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeZone *zone;
    uint64_t slba;
    uint32_t nlb;

    slba = le64_to_cpu(rw->slba);
    nlb = le16_to_cpu(rw->nlb) + 1;
    zone = nvme_get_zone_by_slba(ns, slba);
    assert(zone);

    if (zone->d.za & NVME_ZA_ZRWA_VALID) {
        uint64_t ezrwa = zone->w_ptr + ns->zns.zrwas - 1;
        uint64_t elba = slba + nlb - 1;

        if (elba > ezrwa) {
            nvme_zoned_zrwa_implicit_flush(ns, zone, elba - ezrwa);
        }

        return;
    }

    nvme_advance_zone_wp(ns, zone, nlb);
}

static inline bool nvme_is_write(NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;

    return rw->opcode == NVME_CMD_WRITE ||
           rw->opcode == NVME_CMD_ZONE_APPEND ||
           rw->opcode == NVME_CMD_WRITE_ZEROES;
}

static void nvme_misc_cb(void *opaque, int ret)
{
    NvmeRequest *req = opaque;
    uint16_t cid = nvme_cid(req);

    trace_pci_nvme_misc_cb(cid);

    if (ret) {
        if (!req->status) {
            req->status = NVME_INTERNAL_DEV_ERROR;
        }

        trace_pci_nvme_err_aio(cid, strerror(-ret), req->status);
    }

    nvme_enqueue_req_completion(nvme_cq(req), req);
}

void nvme_rw_complete_cb(void *opaque, int ret)
{
    NvmeRequest *req = opaque;
    NvmeNamespace *ns = req->ns;
    BlockBackend *blk = ns->blkconf.blk;
    BlockAcctCookie *acct = &req->acct;
    BlockAcctStats *stats = blk_get_stats(blk);

    trace_pci_nvme_rw_complete_cb(nvme_cid(req), blk_name(blk));

    if (ret) {
        Error *err = NULL;

        block_acct_failed(stats, acct);

        switch (req->cmd.opcode) {
        case NVME_CMD_READ:
            req->status = NVME_UNRECOVERED_READ;
            break;

        case NVME_CMD_WRITE:
        case NVME_CMD_WRITE_ZEROES:
        case NVME_CMD_ZONE_APPEND:
            req->status = NVME_WRITE_FAULT;
            break;

        default:
            req->status = NVME_INTERNAL_DEV_ERROR;
            break;
        }

        trace_pci_nvme_err_aio(nvme_cid(req), strerror(-ret), req->status);

        error_setg_errno(&err, -ret, "aio failed");
        error_report_err(err);
    } else {
        block_acct_done(stats, acct);
    }

    if (ns->params.zoned && nvme_is_write(req)) {
        nvme_finalize_zoned_write(ns, req);
    }

    nvme_enqueue_req_completion(nvme_cq(req), req);
}

static void nvme_rw_cb(void *opaque, int ret)
{
    NvmeRequest *req = opaque;
    NvmeNamespace *ns = req->ns;

    BlockBackend *blk = ns->blkconf.blk;

    trace_pci_nvme_rw_cb(nvme_cid(req), blk_name(blk));

    if (ret) {
        goto out;
    }

    if (ns->lbaf.ms) {
        NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
        uint64_t slba = le64_to_cpu(rw->slba);
        uint32_t nlb = (uint32_t)le16_to_cpu(rw->nlb) + 1;
        uint64_t offset = nvme_moff(ns, slba);

        if (req->cmd.opcode == NVME_CMD_WRITE_ZEROES) {
            size_t mlen = nvme_m2b(ns, nlb);

            req->aiocb = blk_aio_pwrite_zeroes(blk, offset, mlen,
                                               BDRV_REQ_MAY_UNMAP,
                                               nvme_rw_complete_cb, req);
            return;
        }

        if (nvme_ns_ext(ns) || req->cmd.mptr) {
            uint16_t status;

            nvme_sg_unmap(&req->sg);
            status = nvme_map_mdata(nvme_ctrl(req), nlb, req);
            if (status) {
                ret = -EFAULT;
                goto out;
            }

            if (req->cmd.opcode == NVME_CMD_READ) {
                return nvme_blk_read(blk, offset, 1, nvme_rw_complete_cb, req);
            }

            return nvme_blk_write(blk, offset, 1, nvme_rw_complete_cb, req);
        }
    }

out:
    nvme_rw_complete_cb(req, ret);
}

static void nvme_verify_cb(void *opaque, int ret)
{
    NvmeBounceContext *ctx = opaque;
    NvmeRequest *req = ctx->req;
    NvmeNamespace *ns = req->ns;
    BlockBackend *blk = ns->blkconf.blk;
    BlockAcctCookie *acct = &req->acct;
    BlockAcctStats *stats = blk_get_stats(blk);
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint8_t prinfo = NVME_RW_PRINFO(le16_to_cpu(rw->control));
    uint16_t apptag = le16_to_cpu(rw->apptag);
    uint16_t appmask = le16_to_cpu(rw->appmask);
    uint64_t reftag = le32_to_cpu(rw->reftag);
    uint64_t cdw3 = le32_to_cpu(rw->cdw3);
    uint16_t status;

    reftag |= cdw3 << 32;

    trace_pci_nvme_verify_cb(nvme_cid(req), prinfo, apptag, appmask, reftag);

    if (ret) {
        block_acct_failed(stats, acct);
        req->status = NVME_UNRECOVERED_READ;

        trace_pci_nvme_err_aio(nvme_cid(req), strerror(-ret), req->status);

        goto out;
    }

    block_acct_done(stats, acct);

    if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
        status = nvme_dif_mangle_mdata(ns, ctx->mdata.bounce,
                                       ctx->mdata.iov.size, slba);
        if (status) {
            req->status = status;
            goto out;
        }

        req->status = nvme_dif_check(ns, ctx->data.bounce, ctx->data.iov.size,
                                     ctx->mdata.bounce, ctx->mdata.iov.size,
                                     prinfo, slba, apptag, appmask, &reftag);
    }

out:
    qemu_iovec_destroy(&ctx->data.iov);
    g_free(ctx->data.bounce);

    qemu_iovec_destroy(&ctx->mdata.iov);
    g_free(ctx->mdata.bounce);

    g_free(ctx);

    nvme_enqueue_req_completion(nvme_cq(req), req);
}


static void nvme_verify_mdata_in_cb(void *opaque, int ret)
{
    NvmeBounceContext *ctx = opaque;
    NvmeRequest *req = ctx->req;
    NvmeNamespace *ns = req->ns;
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = le16_to_cpu(rw->nlb) + 1;
    size_t mlen = nvme_m2b(ns, nlb);
    uint64_t offset = nvme_moff(ns, slba);
    BlockBackend *blk = ns->blkconf.blk;

    trace_pci_nvme_verify_mdata_in_cb(nvme_cid(req), blk_name(blk));

    if (ret) {
        goto out;
    }

    ctx->mdata.bounce = g_malloc(mlen);

    qemu_iovec_reset(&ctx->mdata.iov);
    qemu_iovec_add(&ctx->mdata.iov, ctx->mdata.bounce, mlen);

    req->aiocb = blk_aio_preadv(blk, offset, &ctx->mdata.iov, 0,
                                nvme_verify_cb, ctx);
    return;

out:
    nvme_verify_cb(ctx, ret);
}

struct nvme_compare_ctx {
    struct {
        QEMUIOVector iov;
        uint8_t *bounce;
    } data;

    struct {
        QEMUIOVector iov;
        uint8_t *bounce;
    } mdata;
};

static void nvme_compare_mdata_cb(void *opaque, int ret)
{
    NvmeRequest *req = opaque;
    NvmeNamespace *ns = req->ns;
    NvmeCtrl *n = nvme_ctrl(req);
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    uint8_t prinfo = NVME_RW_PRINFO(le16_to_cpu(rw->control));
    uint16_t apptag = le16_to_cpu(rw->apptag);
    uint16_t appmask = le16_to_cpu(rw->appmask);
    uint64_t reftag = le32_to_cpu(rw->reftag);
    uint64_t cdw3 = le32_to_cpu(rw->cdw3);
    struct nvme_compare_ctx *ctx = req->opaque;
    g_autofree uint8_t *buf = NULL;
    BlockBackend *blk = ns->blkconf.blk;
    BlockAcctCookie *acct = &req->acct;
    BlockAcctStats *stats = blk_get_stats(blk);
    uint16_t status = NVME_SUCCESS;

    reftag |= cdw3 << 32;

    trace_pci_nvme_compare_mdata_cb(nvme_cid(req));

    if (ret) {
        block_acct_failed(stats, acct);
        req->status = NVME_UNRECOVERED_READ;

        trace_pci_nvme_err_aio(nvme_cid(req), strerror(-ret), req->status);

        goto out;
    }

    buf = g_malloc(ctx->mdata.iov.size);

    status = nvme_bounce_mdata(n, buf, ctx->mdata.iov.size,
                               NVME_TX_DIRECTION_TO_DEVICE, req);
    if (status) {
        req->status = status;
        goto out;
    }

    if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
        uint64_t slba = le64_to_cpu(rw->slba);
        uint8_t *bufp;
        uint8_t *mbufp = ctx->mdata.bounce;
        uint8_t *end = mbufp + ctx->mdata.iov.size;
        int16_t pil = 0;

        status = nvme_dif_check(ns, ctx->data.bounce, ctx->data.iov.size,
                                ctx->mdata.bounce, ctx->mdata.iov.size, prinfo,
                                slba, apptag, appmask, &reftag);
        if (status) {
            req->status = status;
            goto out;
        }

        /*
         * When formatted with protection information, do not compare the DIF
         * tuple.
         */
        if (!(ns->id_ns.dps & NVME_ID_NS_DPS_FIRST_EIGHT)) {
            pil = ns->lbaf.ms - nvme_pi_tuple_size(ns);
        }

        for (bufp = buf; mbufp < end; bufp += ns->lbaf.ms, mbufp += ns->lbaf.ms) {
            if (memcmp(bufp + pil, mbufp + pil, ns->lbaf.ms - pil)) {
                req->status = NVME_CMP_FAILURE | NVME_DNR;
                goto out;
            }
        }

        goto out;
    }

    if (memcmp(buf, ctx->mdata.bounce, ctx->mdata.iov.size)) {
        req->status = NVME_CMP_FAILURE | NVME_DNR;
        goto out;
    }

    block_acct_done(stats, acct);

out:
    qemu_iovec_destroy(&ctx->data.iov);
    g_free(ctx->data.bounce);

    qemu_iovec_destroy(&ctx->mdata.iov);
    g_free(ctx->mdata.bounce);

    g_free(ctx);

    nvme_enqueue_req_completion(nvme_cq(req), req);
}

static void nvme_compare_data_cb(void *opaque, int ret)
{
    NvmeRequest *req = opaque;
    NvmeCtrl *n = nvme_ctrl(req);
    NvmeNamespace *ns = req->ns;
    BlockBackend *blk = ns->blkconf.blk;
    BlockAcctCookie *acct = &req->acct;
    BlockAcctStats *stats = blk_get_stats(blk);

    struct nvme_compare_ctx *ctx = req->opaque;
    g_autofree uint8_t *buf = NULL;
    uint16_t status;

    trace_pci_nvme_compare_data_cb(nvme_cid(req));

    if (ret) {
        block_acct_failed(stats, acct);
        req->status = NVME_UNRECOVERED_READ;

        trace_pci_nvme_err_aio(nvme_cid(req), strerror(-ret), req->status);

        goto out;
    }

    buf = g_malloc(ctx->data.iov.size);

    status = nvme_bounce_data(n, buf, ctx->data.iov.size,
                              NVME_TX_DIRECTION_TO_DEVICE, req);
    if (status) {
        req->status = status;
        goto out;
    }

    if (memcmp(buf, ctx->data.bounce, ctx->data.iov.size)) {
        req->status = NVME_CMP_FAILURE | NVME_DNR;
        goto out;
    }

    if (ns->lbaf.ms) {
        NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
        uint64_t slba = le64_to_cpu(rw->slba);
        uint32_t nlb = le16_to_cpu(rw->nlb) + 1;
        size_t mlen = nvme_m2b(ns, nlb);
        uint64_t offset = nvme_moff(ns, slba);

        ctx->mdata.bounce = g_malloc(mlen);

        qemu_iovec_init(&ctx->mdata.iov, 1);
        qemu_iovec_add(&ctx->mdata.iov, ctx->mdata.bounce, mlen);

        req->aiocb = blk_aio_preadv(blk, offset, &ctx->mdata.iov, 0,
                                    nvme_compare_mdata_cb, req);
        return;
    }

    block_acct_done(stats, acct);

out:
    qemu_iovec_destroy(&ctx->data.iov);
    g_free(ctx->data.bounce);
    g_free(ctx);

    nvme_enqueue_req_completion(nvme_cq(req), req);
}

typedef struct NvmeDSMAIOCB {
    BlockAIOCB common;
    BlockAIOCB *aiocb;
    NvmeRequest *req;
    int ret;

    NvmeDsmRange *range;
    unsigned int nr;
    unsigned int idx;
} NvmeDSMAIOCB;

static void nvme_dsm_cancel(BlockAIOCB *aiocb)
{
    NvmeDSMAIOCB *iocb = container_of(aiocb, NvmeDSMAIOCB, common);

    /* break nvme_dsm_cb loop */
    iocb->idx = iocb->nr;
    iocb->ret = -ECANCELED;

    if (iocb->aiocb) {
        blk_aio_cancel_async(iocb->aiocb);
        iocb->aiocb = NULL;
    } else {
        /*
         * We only reach this if nvme_dsm_cancel() has already been called or
         * the command ran to completion.
         */
        assert(iocb->idx == iocb->nr);
    }
}

static const AIOCBInfo nvme_dsm_aiocb_info = {
    .aiocb_size   = sizeof(NvmeDSMAIOCB),
    .cancel_async = nvme_dsm_cancel,
};

static void nvme_dsm_cb(void *opaque, int ret);

static void nvme_dsm_md_cb(void *opaque, int ret)
{
    NvmeDSMAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    NvmeDsmRange *range;
    uint64_t slba;
    uint32_t nlb;

    if (ret < 0 || iocb->ret < 0 || !ns->lbaf.ms) {
        goto done;
    }

    range = &iocb->range[iocb->idx - 1];
    slba = le64_to_cpu(range->slba);
    nlb = le32_to_cpu(range->nlb);

    /*
     * Check that all block were discarded (zeroed); otherwise we do not zero
     * the metadata.
     */

    ret = nvme_block_status_all(ns, slba, nlb, BDRV_BLOCK_ZERO);
    if (ret) {
        if (ret < 0) {
            goto done;
        }

        nvme_dsm_cb(iocb, 0);
        return;
    }

    iocb->aiocb = blk_aio_pwrite_zeroes(ns->blkconf.blk, nvme_moff(ns, slba),
                                        nvme_m2b(ns, nlb), BDRV_REQ_MAY_UNMAP,
                                        nvme_dsm_cb, iocb);
    return;

done:
    nvme_dsm_cb(iocb, ret);
}

static void nvme_dsm_cb(void *opaque, int ret)
{
    NvmeDSMAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeCtrl *n = nvme_ctrl(req);
    NvmeNamespace *ns = req->ns;
    NvmeDsmRange *range;
    uint64_t slba;
    uint32_t nlb;

    if (iocb->ret < 0) {
        goto done;
    } else if (ret < 0) {
        iocb->ret = ret;
        goto done;
    }

next:
    if (iocb->idx == iocb->nr) {
        goto done;
    }

    range = &iocb->range[iocb->idx++];
    slba = le64_to_cpu(range->slba);
    nlb = le32_to_cpu(range->nlb);

    trace_pci_nvme_dsm_deallocate(slba, nlb);

    if (nlb > n->dmrsl) {
        trace_pci_nvme_dsm_single_range_limit_exceeded(nlb, n->dmrsl);
        goto next;
    }

    if (nvme_check_bounds(ns, slba, nlb)) {
        trace_pci_nvme_err_invalid_lba_range(slba, nlb,
                                             ns->id_ns.nsze);
        goto next;
    }

    iocb->aiocb = blk_aio_pdiscard(ns->blkconf.blk, nvme_l2b(ns, slba),
                                   nvme_l2b(ns, nlb),
                                   nvme_dsm_md_cb, iocb);
    return;

done:
    iocb->aiocb = NULL;
    iocb->common.cb(iocb->common.opaque, iocb->ret);
    g_free(iocb->range);
    qemu_aio_unref(iocb);
}

static uint16_t nvme_dsm(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    NvmeDsmCmd *dsm = (NvmeDsmCmd *) &req->cmd;
    uint32_t attr = le32_to_cpu(dsm->attributes);
    uint32_t nr = (le32_to_cpu(dsm->nr) & 0xff) + 1;
    uint16_t status = NVME_SUCCESS;

    trace_pci_nvme_dsm(nr, attr);

    if (attr & NVME_DSMGMT_AD) {
        NvmeDSMAIOCB *iocb = blk_aio_get(&nvme_dsm_aiocb_info, ns->blkconf.blk,
                                         nvme_misc_cb, req);

        iocb->req = req;
        iocb->ret = 0;
        iocb->range = g_new(NvmeDsmRange, nr);
        iocb->nr = nr;
        iocb->idx = 0;

        status = nvme_h2c(n, (uint8_t *)iocb->range, sizeof(NvmeDsmRange) * nr,
                          req);
        if (status) {
            g_free(iocb->range);
            qemu_aio_unref(iocb);

            return status;
        }

        req->aiocb = &iocb->common;
        nvme_dsm_cb(iocb, 0);

        return NVME_NO_COMPLETE;
    }

    return status;
}

static uint16_t nvme_verify(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    BlockBackend *blk = ns->blkconf.blk;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = le16_to_cpu(rw->nlb) + 1;
    size_t len = nvme_l2b(ns, nlb);
    size_t data_len = len;
    int64_t offset = nvme_l2b(ns, slba);
    uint8_t prinfo = NVME_RW_PRINFO(le16_to_cpu(rw->control));
    uint32_t reftag = le32_to_cpu(rw->reftag);
    NvmeBounceContext *ctx = NULL;
    uint16_t status;

    trace_pci_nvme_verify(nvme_cid(req), nvme_nsid(ns), slba, nlb);

    if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
        status = nvme_check_prinfo(ns, prinfo, slba, reftag);
        if (status) {
            return status;
        }

        if (prinfo & NVME_PRINFO_PRACT) {
            return NVME_INVALID_PROT_INFO | NVME_DNR;
        }
    }

    if (nvme_ns_ext(ns) && !(NVME_ID_CTRL_CTRATT_MEM(n->id_ctrl.ctratt))) {
        data_len += nvme_m2b(ns, nlb);
    }

    if (data_len > (n->page_size << n->params.vsl)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    status = nvme_check_bounds(ns, slba, nlb);
    if (status) {
        return status;
    }

    if (NVME_ERR_REC_DULBE(ns->features.err_rec)) {
        status = nvme_check_dulbe(ns, slba, nlb);
        if (status) {
            return status;
        }
    }

    ctx = g_new0(NvmeBounceContext, 1);
    ctx->req = req;

    ctx->data.bounce = g_malloc(len);

    qemu_iovec_init(&ctx->data.iov, 1);
    qemu_iovec_add(&ctx->data.iov, ctx->data.bounce, len);

    block_acct_start(blk_get_stats(blk), &req->acct, ctx->data.iov.size,
                     BLOCK_ACCT_READ);

    req->aiocb = blk_aio_preadv(ns->blkconf.blk, offset, &ctx->data.iov, 0,
                                nvme_verify_mdata_in_cb, ctx);
    return NVME_NO_COMPLETE;
}

typedef struct NvmeCopyAIOCB {
    BlockAIOCB common;
    BlockAIOCB *aiocb;
    NvmeRequest *req;
    NvmeCtrl *n;
    int ret;

    void *ranges;
    unsigned int format;
    int nr;
    int idx;

    uint8_t *bounce;
    QEMUIOVector iov;
    struct {
        BlockAcctCookie read;
        BlockAcctCookie write;
    } acct;

    uint64_t reftag;
    uint64_t slba;

    NvmeZone *zone;
    NvmeNamespace *sns;
    uint32_t tcl;
} NvmeCopyAIOCB;

static void nvme_copy_cancel(BlockAIOCB *aiocb)
{
    NvmeCopyAIOCB *iocb = container_of(aiocb, NvmeCopyAIOCB, common);

    iocb->ret = -ECANCELED;

    if (iocb->aiocb) {
        blk_aio_cancel_async(iocb->aiocb);
        iocb->aiocb = NULL;
    }
}

static const AIOCBInfo nvme_copy_aiocb_info = {
    .aiocb_size   = sizeof(NvmeCopyAIOCB),
    .cancel_async = nvme_copy_cancel,
};

static void nvme_copy_done(NvmeCopyAIOCB *iocb)
{
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    BlockAcctStats *stats = blk_get_stats(ns->blkconf.blk);

    if (iocb->idx != iocb->nr) {
        req->cqe.result = cpu_to_le32(iocb->idx);
    }

    qemu_iovec_destroy(&iocb->iov);
    g_free(iocb->bounce);

    if (iocb->ret < 0) {
        block_acct_failed(stats, &iocb->acct.read);
        block_acct_failed(stats, &iocb->acct.write);
    } else {
        block_acct_done(stats, &iocb->acct.read);
        block_acct_done(stats, &iocb->acct.write);
    }

    iocb->common.cb(iocb->common.opaque, iocb->ret);
    qemu_aio_unref(iocb);
}

static void nvme_do_copy(NvmeCopyAIOCB *iocb);

static void nvme_copy_source_range_parse_format0_2(void *ranges,
                                                   int idx, uint64_t *slba,
                                                   uint32_t *nlb,
                                                   uint32_t *snsid,
                                                   uint16_t *apptag,
                                                   uint16_t *appmask,
                                                   uint64_t *reftag)
{
    NvmeCopySourceRangeFormat0_2 *_ranges = ranges;

    if (snsid) {
        *snsid = le32_to_cpu(_ranges[idx].sparams);
    }

    if (slba) {
        *slba = le64_to_cpu(_ranges[idx].slba);
    }

    if (nlb) {
        *nlb = le16_to_cpu(_ranges[idx].nlb) + 1;
    }

    if (apptag) {
        *apptag = le16_to_cpu(_ranges[idx].apptag);
    }

    if (appmask) {
        *appmask = le16_to_cpu(_ranges[idx].appmask);
    }

    if (reftag) {
        *reftag = le32_to_cpu(_ranges[idx].reftag);
    }
}

static void nvme_copy_source_range_parse_format1_3(void *ranges, int idx,
                                                   uint64_t *slba,
                                                   uint32_t *nlb,
                                                   uint32_t *snsid,
                                                   uint16_t *apptag,
                                                   uint16_t *appmask,
                                                   uint64_t *reftag)
{
    NvmeCopySourceRangeFormat1_3 *_ranges = ranges;

    if (snsid) {
        *snsid = le32_to_cpu(_ranges[idx].sparams);
    }

    if (slba) {
        *slba = le64_to_cpu(_ranges[idx].slba);
    }

    if (nlb) {
        *nlb = le16_to_cpu(_ranges[idx].nlb) + 1;
    }

    if (apptag) {
        *apptag = le16_to_cpu(_ranges[idx].apptag);
    }

    if (appmask) {
        *appmask = le16_to_cpu(_ranges[idx].appmask);
    }

    if (reftag) {
        *reftag = 0;

        *reftag |= (uint64_t)_ranges[idx].sr[4] << 40;
        *reftag |= (uint64_t)_ranges[idx].sr[5] << 32;
        *reftag |= (uint64_t)_ranges[idx].sr[6] << 24;
        *reftag |= (uint64_t)_ranges[idx].sr[7] << 16;
        *reftag |= (uint64_t)_ranges[idx].sr[8] << 8;
        *reftag |= (uint64_t)_ranges[idx].sr[9];
    }
}

static void nvme_copy_source_range_parse(void *ranges, int idx, uint8_t format,
                                         uint64_t *slba, uint32_t *nlb,
                                         uint32_t *snsid, uint16_t *apptag,
                                         uint16_t *appmask, uint64_t *reftag)
{
    switch (format) {
    case NVME_COPY_FORMAT_0:
    case NVME_COPY_FORMAT_2:
        nvme_copy_source_range_parse_format0_2(ranges, idx, slba, nlb, snsid,
                                               apptag, appmask, reftag);
        break;

    case NVME_COPY_FORMAT_1:
    case NVME_COPY_FORMAT_3:
        nvme_copy_source_range_parse_format1_3(ranges, idx, slba, nlb, snsid,
                                               apptag, appmask, reftag);
        break;

    default:
        abort();
    }
}

static inline uint16_t nvme_check_copy_mcl(NvmeNamespace *ns,
                                           NvmeCopyAIOCB *iocb, uint16_t nr)
{
    uint32_t copy_len = 0;

    for (int idx = 0; idx < nr; idx++) {
        uint32_t nlb;
        nvme_copy_source_range_parse(iocb->ranges, idx, iocb->format, NULL,
                                     &nlb, NULL, NULL, NULL, NULL);
        copy_len += nlb;
    }
    iocb->tcl = copy_len;
    if (copy_len > ns->id_ns.mcl) {
        return NVME_CMD_SIZE_LIMIT | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static void nvme_copy_out_completed_cb(void *opaque, int ret)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *dns = req->ns;
    uint32_t nlb;

    nvme_copy_source_range_parse(iocb->ranges, iocb->idx, iocb->format, NULL,
                                 &nlb, NULL, NULL, NULL, NULL);

    if (ret < 0) {
        iocb->ret = ret;
        req->status = NVME_WRITE_FAULT;
        goto out;
    } else if (iocb->ret < 0) {
        goto out;
    }

    if (dns->params.zoned) {
        nvme_advance_zone_wp(dns, iocb->zone, nlb);
    }

    iocb->idx++;
    iocb->slba += nlb;
out:
    nvme_do_copy(iocb);
}

static void nvme_copy_out_cb(void *opaque, int ret)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *dns = req->ns;
    uint32_t nlb;
    size_t mlen;
    uint8_t *mbounce;

    if (ret < 0 || iocb->ret < 0 || !dns->lbaf.ms) {
        goto out;
    }

    nvme_copy_source_range_parse(iocb->ranges, iocb->idx, iocb->format, NULL,
                                 &nlb, NULL, NULL, NULL, NULL);

    mlen = nvme_m2b(dns, nlb);
    mbounce = iocb->bounce + nvme_l2b(dns, nlb);

    qemu_iovec_reset(&iocb->iov);
    qemu_iovec_add(&iocb->iov, mbounce, mlen);

    iocb->aiocb = blk_aio_pwritev(dns->blkconf.blk, nvme_moff(dns, iocb->slba),
                                  &iocb->iov, 0, nvme_copy_out_completed_cb,
                                  iocb);

    return;

out:
    nvme_copy_out_completed_cb(iocb, ret);
}

static void nvme_copy_in_completed_cb(void *opaque, int ret)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *sns = iocb->sns;
    NvmeNamespace *dns = req->ns;
    NvmeCopyCmd *copy = NULL;
    uint8_t *mbounce = NULL;
    uint32_t nlb;
    uint64_t slba;
    uint16_t apptag, appmask;
    uint64_t reftag;
    size_t len, mlen;
    uint16_t status;

    if (ret < 0) {
        iocb->ret = ret;
        req->status = NVME_UNRECOVERED_READ;
        goto out;
    } else if (iocb->ret < 0) {
        goto out;
    }

    nvme_copy_source_range_parse(iocb->ranges, iocb->idx, iocb->format, &slba,
                                 &nlb, NULL, &apptag, &appmask, &reftag);

    trace_pci_nvme_copy_out(iocb->slba, nlb);

    len = nvme_l2b(sns, nlb);

    if (NVME_ID_NS_DPS_TYPE(sns->id_ns.dps)) {
        copy = (NvmeCopyCmd *)&req->cmd;

        uint16_t prinfor = ((copy->control[0] >> 4) & 0xf);

        mlen = nvme_m2b(sns, nlb);
        mbounce = iocb->bounce + nvme_l2b(sns, nlb);

        status = nvme_dif_mangle_mdata(sns, mbounce, mlen, slba);
        if (status) {
            goto invalid;
        }
        status = nvme_dif_check(sns, iocb->bounce, len, mbounce, mlen, prinfor,
                                slba, apptag, appmask, &reftag);
        if (status) {
            goto invalid;
        }
    }

    if (NVME_ID_NS_DPS_TYPE(dns->id_ns.dps)) {
        copy = (NvmeCopyCmd *)&req->cmd;
        uint16_t prinfow = ((copy->control[2] >> 2) & 0xf);

        mlen = nvme_m2b(dns, nlb);
        mbounce = iocb->bounce + nvme_l2b(dns, nlb);

        apptag = le16_to_cpu(copy->apptag);
        appmask = le16_to_cpu(copy->appmask);

        if (prinfow & NVME_PRINFO_PRACT) {
            status = nvme_check_prinfo(dns, prinfow, iocb->slba, iocb->reftag);
            if (status) {
                goto invalid;
            }

            nvme_dif_pract_generate_dif(dns, iocb->bounce, len, mbounce, mlen,
                                        apptag, &iocb->reftag);
        } else {
            status = nvme_dif_check(dns, iocb->bounce, len, mbounce, mlen,
                                    prinfow, iocb->slba, apptag, appmask,
                                    &iocb->reftag);
            if (status) {
                goto invalid;
            }
        }
    }

    status = nvme_check_bounds(dns, iocb->slba, nlb);
    if (status) {
        goto invalid;
    }

    if (dns->params.zoned) {
        status = nvme_check_zone_write(dns, iocb->zone, iocb->slba, nlb);
        if (status) {
            goto invalid;
        }

        if (!(iocb->zone->d.za & NVME_ZA_ZRWA_VALID)) {
            iocb->zone->w_ptr += nlb;
        }
    }

    qemu_iovec_reset(&iocb->iov);
    qemu_iovec_add(&iocb->iov, iocb->bounce, len);

    block_acct_start(blk_get_stats(dns->blkconf.blk), &iocb->acct.write, 0,
                     BLOCK_ACCT_WRITE);

    iocb->aiocb = blk_aio_pwritev(dns->blkconf.blk, nvme_l2b(dns, iocb->slba),
                                  &iocb->iov, 0, nvme_copy_out_cb, iocb);

    return;

invalid:
    req->status = status;
    iocb->ret = -1;
out:
    nvme_do_copy(iocb);
}

static void nvme_copy_in_cb(void *opaque, int ret)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeNamespace *sns = iocb->sns;
    uint64_t slba;
    uint32_t nlb;

    if (ret < 0 || iocb->ret < 0 || !sns->lbaf.ms) {
        goto out;
    }

    nvme_copy_source_range_parse(iocb->ranges, iocb->idx, iocb->format, &slba,
                                 &nlb, NULL, NULL, NULL, NULL);

    qemu_iovec_reset(&iocb->iov);
    qemu_iovec_add(&iocb->iov, iocb->bounce + nvme_l2b(sns, nlb),
                   nvme_m2b(sns, nlb));

    iocb->aiocb = blk_aio_preadv(sns->blkconf.blk, nvme_moff(sns, slba),
                                 &iocb->iov, 0, nvme_copy_in_completed_cb,
                                 iocb);
    return;

out:
    nvme_copy_in_completed_cb(iocb, ret);
}

static inline bool nvme_csi_supports_copy(uint8_t csi)
{
    return csi == NVME_CSI_NVM || csi == NVME_CSI_ZONED;
}

static inline bool nvme_copy_ns_format_match(NvmeNamespace *sns,
                                             NvmeNamespace *dns)
{
    return sns->lbaf.ds == dns->lbaf.ds && sns->lbaf.ms == dns->lbaf.ms;
}

static bool nvme_copy_matching_ns_format(NvmeNamespace *sns, NvmeNamespace *dns,
                                         bool pi_enable)
{
    if (!nvme_csi_supports_copy(sns->csi) ||
        !nvme_csi_supports_copy(dns->csi)) {
        return false;
    }

    if (!pi_enable && !nvme_copy_ns_format_match(sns, dns)) {
            return false;
    }

    if (pi_enable && (!nvme_copy_ns_format_match(sns, dns) ||
        sns->id_ns.dps != dns->id_ns.dps)) {
            return false;
    }

    return true;
}

static inline bool nvme_copy_corresp_pi_match(NvmeNamespace *sns,
                                              NvmeNamespace *dns)
{
    return sns->lbaf.ms == 0 &&
           ((dns->lbaf.ms == 8 && dns->pif == 0) ||
           (dns->lbaf.ms == 16 && dns->pif == 1));
}

static bool nvme_copy_corresp_pi_format(NvmeNamespace *sns, NvmeNamespace *dns,
                                        bool sns_pi_en)
{
    if (!nvme_csi_supports_copy(sns->csi) ||
        !nvme_csi_supports_copy(dns->csi)) {
        return false;
    }

    if (!sns_pi_en && !nvme_copy_corresp_pi_match(sns, dns)) {
        return false;
    }

    if (sns_pi_en && !nvme_copy_corresp_pi_match(dns, sns)) {
        return false;
    }

    return true;
}

static void nvme_do_copy(NvmeCopyAIOCB *iocb)
{
    NvmeRequest *req = iocb->req;
    NvmeNamespace *sns;
    NvmeNamespace *dns = req->ns;
    NvmeCopyCmd *copy = (NvmeCopyCmd *)&req->cmd;
    uint16_t prinfor = ((copy->control[0] >> 4) & 0xf);
    uint16_t prinfow = ((copy->control[2] >> 2) & 0xf);
    uint64_t slba;
    uint32_t nlb;
    size_t len;
    uint16_t status;
    uint32_t dnsid = le32_to_cpu(req->cmd.nsid);
    uint32_t snsid = dnsid;

    if (iocb->ret < 0) {
        goto done;
    }

    if (iocb->idx == iocb->nr) {
        goto done;
    }

    if (iocb->format == 2 || iocb->format == 3) {
        nvme_copy_source_range_parse(iocb->ranges, iocb->idx, iocb->format,
                                     &slba, &nlb, &snsid, NULL, NULL, NULL);
        if (snsid != dnsid) {
            if (snsid == NVME_NSID_BROADCAST ||
                !nvme_nsid_valid(iocb->n, snsid)) {
                status = NVME_INVALID_NSID | NVME_DNR;
                goto invalid;
            }
            iocb->sns = nvme_ns(iocb->n, snsid);
            if (unlikely(!iocb->sns)) {
                status = NVME_INVALID_FIELD | NVME_DNR;
                goto invalid;
            }
        } else {
            if (((slba + nlb) > iocb->slba) &&
                ((slba + nlb) < (iocb->slba + iocb->tcl))) {
                status = NVME_CMD_OVERLAP_IO_RANGE | NVME_DNR;
                goto invalid;
            }
        }
    } else {
        nvme_copy_source_range_parse(iocb->ranges, iocb->idx, iocb->format,
                                     &slba, &nlb, NULL, NULL, NULL, NULL);
    }

    sns = iocb->sns;
    if ((snsid == dnsid) && NVME_ID_NS_DPS_TYPE(sns->id_ns.dps) &&
        ((prinfor & NVME_PRINFO_PRACT) != (prinfow & NVME_PRINFO_PRACT))) {
        status = NVME_INVALID_FIELD | NVME_DNR;
        goto invalid;
    } else if (snsid != dnsid) {
        if (!NVME_ID_NS_DPS_TYPE(sns->id_ns.dps) &&
            !NVME_ID_NS_DPS_TYPE(dns->id_ns.dps)) {
            if (!nvme_copy_matching_ns_format(sns, dns, false)) {
                status = NVME_CMD_INCOMP_NS_OR_FMT | NVME_DNR;
                goto invalid;
            }
        }
        if (NVME_ID_NS_DPS_TYPE(sns->id_ns.dps) &&
            NVME_ID_NS_DPS_TYPE(dns->id_ns.dps)) {
            if ((prinfor & NVME_PRINFO_PRACT) !=
                (prinfow & NVME_PRINFO_PRACT)) {
                status = NVME_CMD_INCOMP_NS_OR_FMT | NVME_DNR;
                goto invalid;
            } else {
                if (!nvme_copy_matching_ns_format(sns, dns, true)) {
                    status = NVME_CMD_INCOMP_NS_OR_FMT | NVME_DNR;
                    goto invalid;
                }
            }
        }

        if (!NVME_ID_NS_DPS_TYPE(sns->id_ns.dps) &&
            NVME_ID_NS_DPS_TYPE(dns->id_ns.dps)) {
            if (!(prinfow & NVME_PRINFO_PRACT)) {
                status = NVME_CMD_INCOMP_NS_OR_FMT | NVME_DNR;
                goto invalid;
            } else {
                if (!nvme_copy_corresp_pi_format(sns, dns, false)) {
                    status = NVME_CMD_INCOMP_NS_OR_FMT | NVME_DNR;
                    goto invalid;
                }
            }
        }

        if (NVME_ID_NS_DPS_TYPE(sns->id_ns.dps) &&
            !NVME_ID_NS_DPS_TYPE(dns->id_ns.dps)) {
            if (!(prinfor & NVME_PRINFO_PRACT)) {
                status = NVME_CMD_INCOMP_NS_OR_FMT | NVME_DNR;
                goto invalid;
            } else {
                if (!nvme_copy_corresp_pi_format(sns, dns, true)) {
                    status = NVME_CMD_INCOMP_NS_OR_FMT | NVME_DNR;
                    goto invalid;
                }
            }
        }
    }
    len = nvme_l2b(sns, nlb);

    trace_pci_nvme_copy_source_range(slba, nlb);

    if (nlb > le16_to_cpu(sns->id_ns.mssrl)) {
        status = NVME_CMD_SIZE_LIMIT | NVME_DNR;
        goto invalid;
    }

    status = nvme_check_bounds(sns, slba, nlb);
    if (status) {
        goto invalid;
    }

    if (NVME_ERR_REC_DULBE(sns->features.err_rec)) {
        status = nvme_check_dulbe(sns, slba, nlb);
        if (status) {
            goto invalid;
        }
    }

    if (sns->params.zoned) {
        status = nvme_check_zone_read(sns, slba, nlb);
        if (status) {
            goto invalid;
        }
    }

    g_free(iocb->bounce);
    iocb->bounce = g_malloc_n(le16_to_cpu(sns->id_ns.mssrl),
                              sns->lbasz + sns->lbaf.ms);

    qemu_iovec_reset(&iocb->iov);
    qemu_iovec_add(&iocb->iov, iocb->bounce, len);

    block_acct_start(blk_get_stats(sns->blkconf.blk), &iocb->acct.read, 0,
                     BLOCK_ACCT_READ);

    iocb->aiocb = blk_aio_preadv(sns->blkconf.blk, nvme_l2b(sns, slba),
                                 &iocb->iov, 0, nvme_copy_in_cb, iocb);
    return;

invalid:
    req->status = status;
    iocb->ret = -1;
done:
    nvme_copy_done(iocb);
}

static uint16_t nvme_copy(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    NvmeCopyCmd *copy = (NvmeCopyCmd *)&req->cmd;
    NvmeCopyAIOCB *iocb = blk_aio_get(&nvme_copy_aiocb_info, ns->blkconf.blk,
                                      nvme_misc_cb, req);
    uint16_t nr = copy->nr + 1;
    uint8_t format = copy->control[0] & 0xf;
    size_t len = sizeof(NvmeCopySourceRangeFormat0_2);

    uint16_t status;

    trace_pci_nvme_copy(nvme_cid(req), nvme_nsid(ns), nr, format);

    iocb->ranges = NULL;
    iocb->zone = NULL;

    if (!(n->id_ctrl.ocfs & (1 << format)) ||
        ((format == 2 || format == 3) &&
         !(n->features.hbs.cdfe & (1 << format)))) {
        trace_pci_nvme_err_copy_invalid_format(format);
        status = NVME_INVALID_FIELD | NVME_DNR;
        goto invalid;
    }

    if (nr > ns->id_ns.msrc + 1) {
        status = NVME_CMD_SIZE_LIMIT | NVME_DNR;
        goto invalid;
    }

    if ((ns->pif == 0x0 && (format != 0x0 && format != 0x2)) ||
        (ns->pif != 0x0 && (format != 0x1 && format != 0x3))) {
        status = NVME_INVALID_FORMAT | NVME_DNR;
        goto invalid;
    }

    if (ns->pif) {
        len = sizeof(NvmeCopySourceRangeFormat1_3);
    }

    iocb->format = format;
    iocb->ranges = g_malloc_n(nr, len);
    status = nvme_h2c(n, (uint8_t *)iocb->ranges, len * nr, req);
    if (status) {
        goto invalid;
    }

    iocb->slba = le64_to_cpu(copy->sdlba);

    if (ns->params.zoned) {
        iocb->zone = nvme_get_zone_by_slba(ns, iocb->slba);
        if (!iocb->zone) {
            status = NVME_LBA_RANGE | NVME_DNR;
            goto invalid;
        }

        status = nvme_zrm_auto(n, ns, iocb->zone);
        if (status) {
            goto invalid;
        }
    }

    status = nvme_check_copy_mcl(ns, iocb, nr);
    if (status) {
        goto invalid;
    }

    iocb->req = req;
    iocb->ret = 0;
    iocb->nr = nr;
    iocb->idx = 0;
    iocb->reftag = le32_to_cpu(copy->reftag);
    iocb->reftag |= (uint64_t)le32_to_cpu(copy->cdw3) << 32;

    qemu_iovec_init(&iocb->iov, 1);

    req->aiocb = &iocb->common;
    iocb->sns = req->ns;
    iocb->n = n;
    iocb->bounce = NULL;
    nvme_do_copy(iocb);

    return NVME_NO_COMPLETE;

invalid:
    g_free(iocb->ranges);
    qemu_aio_unref(iocb);
    return status;
}

static uint16_t nvme_compare(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    BlockBackend *blk = ns->blkconf.blk;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = le16_to_cpu(rw->nlb) + 1;
    uint8_t prinfo = NVME_RW_PRINFO(le16_to_cpu(rw->control));
    size_t data_len = nvme_l2b(ns, nlb);
    size_t len = data_len;
    int64_t offset = nvme_l2b(ns, slba);
    struct nvme_compare_ctx *ctx = NULL;
    uint16_t status;

    trace_pci_nvme_compare(nvme_cid(req), nvme_nsid(ns), slba, nlb);

    if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps) && (prinfo & NVME_PRINFO_PRACT)) {
        return NVME_INVALID_PROT_INFO | NVME_DNR;
    }

    if (nvme_ns_ext(ns)) {
        len += nvme_m2b(ns, nlb);
    }

    if (NVME_ID_CTRL_CTRATT_MEM(n->id_ctrl.ctratt)) {
        status = nvme_check_mdts(n, data_len);
    } else {
        status = nvme_check_mdts(n, len);
    }
    if (status) {
        return status;
    }

    status = nvme_check_bounds(ns, slba, nlb);
    if (status) {
        return status;
    }

    if (NVME_ERR_REC_DULBE(ns->features.err_rec)) {
        status = nvme_check_dulbe(ns, slba, nlb);
        if (status) {
            return status;
        }
    }

    status = nvme_map_dptr(n, &req->sg, len, &req->cmd);
    if (status) {
        return status;
    }

    ctx = g_new(struct nvme_compare_ctx, 1);
    ctx->data.bounce = g_malloc(data_len);

    req->opaque = ctx;

    qemu_iovec_init(&ctx->data.iov, 1);
    qemu_iovec_add(&ctx->data.iov, ctx->data.bounce, data_len);

    block_acct_start(blk_get_stats(blk), &req->acct, data_len,
                     BLOCK_ACCT_READ);
    req->aiocb = blk_aio_preadv(blk, offset, &ctx->data.iov, 0,
                                nvme_compare_data_cb, req);

    return NVME_NO_COMPLETE;
}

typedef struct NvmeFlushAIOCB {
    BlockAIOCB common;
    BlockAIOCB *aiocb;
    NvmeRequest *req;
    int ret;

    NvmeNamespace *ns;
    uint32_t nsid;
    bool broadcast;
} NvmeFlushAIOCB;

static void nvme_flush_cancel(BlockAIOCB *acb)
{
    NvmeFlushAIOCB *iocb = container_of(acb, NvmeFlushAIOCB, common);

    iocb->ret = -ECANCELED;

    if (iocb->aiocb) {
        blk_aio_cancel_async(iocb->aiocb);
        iocb->aiocb = NULL;
    }
}

static const AIOCBInfo nvme_flush_aiocb_info = {
    .aiocb_size = sizeof(NvmeFlushAIOCB),
    .cancel_async = nvme_flush_cancel,
};

static void nvme_do_flush(NvmeFlushAIOCB *iocb);

static void nvme_flush_ns_cb(void *opaque, int ret)
{
    NvmeFlushAIOCB *iocb = opaque;
    NvmeNamespace *ns = iocb->ns;

    if (ret < 0) {
        iocb->ret = ret;
        iocb->req->status = NVME_WRITE_FAULT;
        goto out;
    } else if (iocb->ret < 0) {
        goto out;
    }

    if (ns) {
        trace_pci_nvme_flush_ns(iocb->nsid);

        iocb->ns = NULL;
        iocb->aiocb = blk_aio_flush(ns->blkconf.blk, nvme_flush_ns_cb, iocb);
        return;
    }

out:
    nvme_do_flush(iocb);
}

static void nvme_do_flush(NvmeFlushAIOCB *iocb)
{
    NvmeRequest *req = iocb->req;
    NvmeCtrl *n = nvme_ctrl(req);
    int i;

    if (iocb->ret < 0) {
        goto done;
    }

    if (iocb->broadcast) {
        for (i = iocb->nsid + 1; i <= NVME_MAX_NAMESPACES; i++) {
            iocb->ns = nvme_ns(n, i);
            if (iocb->ns) {
                iocb->nsid = i;
                break;
            }
        }
    }

    if (!iocb->ns) {
        goto done;
    }

    nvme_flush_ns_cb(iocb, 0);
    return;

done:
    iocb->common.cb(iocb->common.opaque, iocb->ret);
    qemu_aio_unref(iocb);
}

static uint16_t nvme_flush(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeFlushAIOCB *iocb;
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);
    uint16_t status;

    iocb = qemu_aio_get(&nvme_flush_aiocb_info, NULL, nvme_misc_cb, req);

    iocb->req = req;
    iocb->ret = 0;
    iocb->ns = NULL;
    iocb->nsid = 0;
    iocb->broadcast = (nsid == NVME_NSID_BROADCAST);

    if (!iocb->broadcast) {
        if (!nvme_nsid_valid(n, nsid)) {
            status = NVME_INVALID_NSID | NVME_DNR;
            goto out;
        }

        iocb->ns = nvme_ns(n, nsid);
        if (!iocb->ns) {
            status = NVME_INVALID_FIELD | NVME_DNR;
            goto out;
        }

        iocb->nsid = nsid;
    }

    req->aiocb = &iocb->common;
    nvme_do_flush(iocb);

    return NVME_NO_COMPLETE;

out:
    qemu_aio_unref(iocb);

    return status;
}

static uint16_t nvme_read(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = (uint32_t)le16_to_cpu(rw->nlb) + 1;
    uint8_t prinfo = NVME_RW_PRINFO(le16_to_cpu(rw->control));
    uint64_t data_size = nvme_l2b(ns, nlb);
    uint64_t mapped_size = data_size;
    uint64_t data_offset;
    BlockBackend *blk = ns->blkconf.blk;
    uint16_t status;

    if (nvme_ns_ext(ns) && !(NVME_ID_CTRL_CTRATT_MEM(n->id_ctrl.ctratt))) {
        mapped_size += nvme_m2b(ns, nlb);

        if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
            bool pract = prinfo & NVME_PRINFO_PRACT;

            if (pract && ns->lbaf.ms == nvme_pi_tuple_size(ns)) {
                mapped_size = data_size;
            }
        }
    }

    trace_pci_nvme_read(nvme_cid(req), nvme_nsid(ns), nlb, mapped_size, slba);

    status = nvme_check_mdts(n, mapped_size);
    if (status) {
        goto invalid;
    }

    status = nvme_check_bounds(ns, slba, nlb);
    if (status) {
        goto invalid;
    }

    if (ns->params.zoned) {
        status = nvme_check_zone_read(ns, slba, nlb);
        if (status) {
            trace_pci_nvme_err_zone_read_not_ok(slba, nlb, status);
            goto invalid;
        }
    }

    if (NVME_ERR_REC_DULBE(ns->features.err_rec)) {
        status = nvme_check_dulbe(ns, slba, nlb);
        if (status) {
            goto invalid;
        }
    }

    if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
        return nvme_dif_rw(n, req);
    }

    status = nvme_map_data(n, nlb, req);
    if (status) {
        goto invalid;
    }

    data_offset = nvme_l2b(ns, slba);

    block_acct_start(blk_get_stats(blk), &req->acct, data_size,
                     BLOCK_ACCT_READ);
    nvme_blk_read(blk, data_offset, BDRV_SECTOR_SIZE, nvme_rw_cb, req);
    return NVME_NO_COMPLETE;

invalid:
    block_acct_invalid(blk_get_stats(blk), BLOCK_ACCT_READ);
    return status | NVME_DNR;
}

static void nvme_do_write_fdp(NvmeCtrl *n, NvmeRequest *req, uint64_t slba,
                              uint32_t nlb)
{
    NvmeNamespace *ns = req->ns;
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    uint64_t data_size = nvme_l2b(ns, nlb);
    uint32_t dw12 = le32_to_cpu(req->cmd.cdw12);
    uint8_t dtype = (dw12 >> 20) & 0xf;
    uint16_t pid = le16_to_cpu(rw->dspec);
    uint16_t ph, rg, ruhid;
    NvmeReclaimUnit *ru;

    if (dtype != NVME_DIRECTIVE_DATA_PLACEMENT ||
        !nvme_parse_pid(ns, pid, &ph, &rg)) {
        ph = 0;
        rg = 0;
    }

    ruhid = ns->fdp.phs[ph];
    ru = &ns->endgrp->fdp.ruhs[ruhid].rus[rg];

    nvme_fdp_stat_inc(&ns->endgrp->fdp.hbmw, data_size);
    nvme_fdp_stat_inc(&ns->endgrp->fdp.mbmw, data_size);

    while (nlb) {
        if (nlb < ru->ruamw) {
            ru->ruamw -= nlb;
            break;
        }

        nlb -= ru->ruamw;
        nvme_update_ruh(n, ns, pid);
    }
}

static uint16_t nvme_do_write(NvmeCtrl *n, NvmeRequest *req, bool append,
                              bool wrz)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = (uint32_t)le16_to_cpu(rw->nlb) + 1;
    uint16_t ctrl = le16_to_cpu(rw->control);
    uint8_t prinfo = NVME_RW_PRINFO(ctrl);
    uint64_t data_size = nvme_l2b(ns, nlb);
    uint64_t mapped_size = data_size;
    uint64_t data_offset;
    NvmeZone *zone;
    NvmeZonedResult *res = (NvmeZonedResult *)&req->cqe;
    BlockBackend *blk = ns->blkconf.blk;
    uint16_t status;

    if (nvme_ns_ext(ns) && !(NVME_ID_CTRL_CTRATT_MEM(n->id_ctrl.ctratt))) {
        mapped_size += nvme_m2b(ns, nlb);

        if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
            bool pract = prinfo & NVME_PRINFO_PRACT;

            if (pract && ns->lbaf.ms == nvme_pi_tuple_size(ns)) {
                mapped_size -= nvme_m2b(ns, nlb);
            }
        }
    }

    trace_pci_nvme_write(nvme_cid(req), nvme_io_opc_str(rw->opcode),
                         nvme_nsid(ns), nlb, mapped_size, slba);

    if (!wrz) {
        status = nvme_check_mdts(n, mapped_size);
        if (status) {
            goto invalid;
        }
    }

    status = nvme_check_bounds(ns, slba, nlb);
    if (status) {
        goto invalid;
    }

    if (ns->params.zoned) {
        zone = nvme_get_zone_by_slba(ns, slba);
        assert(zone);

        if (append) {
            bool piremap = !!(ctrl & NVME_RW_PIREMAP);

            if (unlikely(zone->d.za & NVME_ZA_ZRWA_VALID)) {
                return NVME_INVALID_ZONE_OP | NVME_DNR;
            }

            if (unlikely(slba != zone->d.zslba)) {
                trace_pci_nvme_err_append_not_at_start(slba, zone->d.zslba);
                status = NVME_INVALID_FIELD;
                goto invalid;
            }

            if (n->params.zasl &&
                data_size > (uint64_t)n->page_size << n->params.zasl) {
                trace_pci_nvme_err_zasl(data_size);
                return NVME_INVALID_FIELD | NVME_DNR;
            }

            slba = zone->w_ptr;
            rw->slba = cpu_to_le64(slba);
            res->slba = cpu_to_le64(slba);

            switch (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
            case NVME_ID_NS_DPS_TYPE_1:
                if (!piremap) {
                    return NVME_INVALID_PROT_INFO | NVME_DNR;
                }

                /* fallthrough */

            case NVME_ID_NS_DPS_TYPE_2:
                if (piremap) {
                    uint32_t reftag = le32_to_cpu(rw->reftag);
                    rw->reftag = cpu_to_le32(reftag + (slba - zone->d.zslba));
                }

                break;

            case NVME_ID_NS_DPS_TYPE_3:
                if (piremap) {
                    return NVME_INVALID_PROT_INFO | NVME_DNR;
                }

                break;
            }
        }

        status = nvme_check_zone_write(ns, zone, slba, nlb);
        if (status) {
            goto invalid;
        }

        status = nvme_zrm_auto(n, ns, zone);
        if (status) {
            goto invalid;
        }

        if (!(zone->d.za & NVME_ZA_ZRWA_VALID)) {
            zone->w_ptr += nlb;
        }
    } else if (ns->endgrp && ns->endgrp->fdp.enabled) {
        nvme_do_write_fdp(n, req, slba, nlb);
    }

    data_offset = nvme_l2b(ns, slba);

    if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
        return nvme_dif_rw(n, req);
    }

    if (!wrz) {
        status = nvme_map_data(n, nlb, req);
        if (status) {
            goto invalid;
        }

        block_acct_start(blk_get_stats(blk), &req->acct, data_size,
                         BLOCK_ACCT_WRITE);
        nvme_blk_write(blk, data_offset, BDRV_SECTOR_SIZE, nvme_rw_cb, req);
    } else {
        req->aiocb = blk_aio_pwrite_zeroes(blk, data_offset, data_size,
                                           BDRV_REQ_MAY_UNMAP, nvme_rw_cb,
                                           req);
    }

    return NVME_NO_COMPLETE;

invalid:
    block_acct_invalid(blk_get_stats(blk), BLOCK_ACCT_WRITE);
    return status | NVME_DNR;
}

static inline uint16_t nvme_write(NvmeCtrl *n, NvmeRequest *req)
{
    return nvme_do_write(n, req, false, false);
}

static inline uint16_t nvme_write_zeroes(NvmeCtrl *n, NvmeRequest *req)
{
    return nvme_do_write(n, req, false, true);
}

static inline uint16_t nvme_zone_append(NvmeCtrl *n, NvmeRequest *req)
{
    return nvme_do_write(n, req, true, false);
}

static uint16_t nvme_get_mgmt_zone_slba_idx(NvmeNamespace *ns, NvmeCmd *c,
                                            uint64_t *slba, uint32_t *zone_idx)
{
    uint32_t dw10 = le32_to_cpu(c->cdw10);
    uint32_t dw11 = le32_to_cpu(c->cdw11);

    if (!ns->params.zoned) {
        trace_pci_nvme_err_invalid_opc(c->opcode);
        return NVME_INVALID_OPCODE | NVME_DNR;
    }

    *slba = ((uint64_t)dw11) << 32 | dw10;
    if (unlikely(*slba >= ns->id_ns.nsze)) {
        trace_pci_nvme_err_invalid_lba_range(*slba, 0, ns->id_ns.nsze);
        *slba = 0;
        return NVME_LBA_RANGE | NVME_DNR;
    }

    *zone_idx = nvme_zone_idx(ns, *slba);
    assert(*zone_idx < ns->num_zones);

    return NVME_SUCCESS;
}

typedef uint16_t (*op_handler_t)(NvmeNamespace *, NvmeZone *, NvmeZoneState,
                                 NvmeRequest *);

enum NvmeZoneProcessingMask {
    NVME_PROC_CURRENT_ZONE    = 0,
    NVME_PROC_OPENED_ZONES    = 1 << 0,
    NVME_PROC_CLOSED_ZONES    = 1 << 1,
    NVME_PROC_READ_ONLY_ZONES = 1 << 2,
    NVME_PROC_FULL_ZONES      = 1 << 3,
};

static uint16_t nvme_open_zone(NvmeNamespace *ns, NvmeZone *zone,
                               NvmeZoneState state, NvmeRequest *req)
{
    NvmeZoneSendCmd *cmd = (NvmeZoneSendCmd *)&req->cmd;
    int flags = 0;

    if (cmd->zsflags & NVME_ZSFLAG_ZRWA_ALLOC) {
        uint16_t ozcs = le16_to_cpu(ns->id_ns_zoned->ozcs);

        if (!(ozcs & NVME_ID_NS_ZONED_OZCS_ZRWASUP)) {
            return NVME_INVALID_ZONE_OP | NVME_DNR;
        }

        if (zone->w_ptr % ns->zns.zrwafg) {
            return NVME_NOZRWA | NVME_DNR;
        }

        flags = NVME_ZRM_ZRWA;
    }

    return nvme_zrm_open_flags(nvme_ctrl(req), ns, zone, flags);
}

static uint16_t nvme_close_zone(NvmeNamespace *ns, NvmeZone *zone,
                                NvmeZoneState state, NvmeRequest *req)
{
    return nvme_zrm_close(ns, zone);
}

static uint16_t nvme_finish_zone(NvmeNamespace *ns, NvmeZone *zone,
                                 NvmeZoneState state, NvmeRequest *req)
{
    return nvme_zrm_finish(ns, zone);
}

static uint16_t nvme_offline_zone(NvmeNamespace *ns, NvmeZone *zone,
                                  NvmeZoneState state, NvmeRequest *req)
{
    switch (state) {
    case NVME_ZONE_STATE_READ_ONLY:
        nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_OFFLINE);
        /* fall through */
    case NVME_ZONE_STATE_OFFLINE:
        return NVME_SUCCESS;
    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static uint16_t nvme_set_zd_ext(NvmeNamespace *ns, NvmeZone *zone)
{
    uint16_t status;
    uint8_t state = nvme_get_zone_state(zone);

    if (state == NVME_ZONE_STATE_EMPTY) {
        status = nvme_aor_check(ns, 1, 0);
        if (status) {
            return status;
        }
        nvme_aor_inc_active(ns);
        zone->d.za |= NVME_ZA_ZD_EXT_VALID;
        nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_CLOSED);
        return NVME_SUCCESS;
    }

    return NVME_ZONE_INVAL_TRANSITION;
}

static uint16_t nvme_bulk_proc_zone(NvmeNamespace *ns, NvmeZone *zone,
                                    enum NvmeZoneProcessingMask proc_mask,
                                    op_handler_t op_hndlr, NvmeRequest *req)
{
    uint16_t status = NVME_SUCCESS;
    NvmeZoneState zs = nvme_get_zone_state(zone);
    bool proc_zone;

    switch (zs) {
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        proc_zone = proc_mask & NVME_PROC_OPENED_ZONES;
        break;
    case NVME_ZONE_STATE_CLOSED:
        proc_zone = proc_mask & NVME_PROC_CLOSED_ZONES;
        break;
    case NVME_ZONE_STATE_READ_ONLY:
        proc_zone = proc_mask & NVME_PROC_READ_ONLY_ZONES;
        break;
    case NVME_ZONE_STATE_FULL:
        proc_zone = proc_mask & NVME_PROC_FULL_ZONES;
        break;
    default:
        proc_zone = false;
    }

    if (proc_zone) {
        status = op_hndlr(ns, zone, zs, req);
    }

    return status;
}

static uint16_t nvme_do_zone_op(NvmeNamespace *ns, NvmeZone *zone,
                                enum NvmeZoneProcessingMask proc_mask,
                                op_handler_t op_hndlr, NvmeRequest *req)
{
    NvmeZone *next;
    uint16_t status = NVME_SUCCESS;
    int i;

    if (!proc_mask) {
        status = op_hndlr(ns, zone, nvme_get_zone_state(zone), req);
    } else {
        if (proc_mask & NVME_PROC_CLOSED_ZONES) {
            QTAILQ_FOREACH_SAFE(zone, &ns->closed_zones, entry, next) {
                status = nvme_bulk_proc_zone(ns, zone, proc_mask, op_hndlr,
                                             req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }
        }
        if (proc_mask & NVME_PROC_OPENED_ZONES) {
            QTAILQ_FOREACH_SAFE(zone, &ns->imp_open_zones, entry, next) {
                status = nvme_bulk_proc_zone(ns, zone, proc_mask, op_hndlr,
                                             req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }

            QTAILQ_FOREACH_SAFE(zone, &ns->exp_open_zones, entry, next) {
                status = nvme_bulk_proc_zone(ns, zone, proc_mask, op_hndlr,
                                             req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }
        }
        if (proc_mask & NVME_PROC_FULL_ZONES) {
            QTAILQ_FOREACH_SAFE(zone, &ns->full_zones, entry, next) {
                status = nvme_bulk_proc_zone(ns, zone, proc_mask, op_hndlr,
                                             req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }
        }

        if (proc_mask & NVME_PROC_READ_ONLY_ZONES) {
            for (i = 0; i < ns->num_zones; i++, zone++) {
                status = nvme_bulk_proc_zone(ns, zone, proc_mask, op_hndlr,
                                             req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }
        }
    }

out:
    return status;
}

typedef struct NvmeZoneResetAIOCB {
    BlockAIOCB common;
    BlockAIOCB *aiocb;
    NvmeRequest *req;
    int ret;

    bool all;
    int idx;
    NvmeZone *zone;
} NvmeZoneResetAIOCB;

static void nvme_zone_reset_cancel(BlockAIOCB *aiocb)
{
    NvmeZoneResetAIOCB *iocb = container_of(aiocb, NvmeZoneResetAIOCB, common);
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;

    iocb->idx = ns->num_zones;

    iocb->ret = -ECANCELED;

    if (iocb->aiocb) {
        blk_aio_cancel_async(iocb->aiocb);
        iocb->aiocb = NULL;
    }
}

static const AIOCBInfo nvme_zone_reset_aiocb_info = {
    .aiocb_size = sizeof(NvmeZoneResetAIOCB),
    .cancel_async = nvme_zone_reset_cancel,
};

static void nvme_zone_reset_cb(void *opaque, int ret);

static void nvme_zone_reset_epilogue_cb(void *opaque, int ret)
{
    NvmeZoneResetAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    int64_t moff;
    int count;

    if (ret < 0 || iocb->ret < 0 || !ns->lbaf.ms) {
        goto out;
    }

    moff = nvme_moff(ns, iocb->zone->d.zslba);
    count = nvme_m2b(ns, ns->zone_size);

    iocb->aiocb = blk_aio_pwrite_zeroes(ns->blkconf.blk, moff, count,
                                        BDRV_REQ_MAY_UNMAP,
                                        nvme_zone_reset_cb, iocb);
    return;

out:
    nvme_zone_reset_cb(iocb, ret);
}

static void nvme_zone_reset_cb(void *opaque, int ret)
{
    NvmeZoneResetAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;

    if (iocb->ret < 0) {
        goto done;
    } else if (ret < 0) {
        iocb->ret = ret;
        goto done;
    }

    if (iocb->zone) {
        nvme_zrm_reset(ns, iocb->zone);

        if (!iocb->all) {
            goto done;
        }
    }

    while (iocb->idx < ns->num_zones) {
        NvmeZone *zone = &ns->zone_array[iocb->idx++];

        switch (nvme_get_zone_state(zone)) {
        case NVME_ZONE_STATE_EMPTY:
            if (!iocb->all) {
                goto done;
            }

            continue;

        case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        case NVME_ZONE_STATE_CLOSED:
        case NVME_ZONE_STATE_FULL:
            iocb->zone = zone;
            break;

        default:
            continue;
        }

        trace_pci_nvme_zns_zone_reset(zone->d.zslba);

        iocb->aiocb = blk_aio_pwrite_zeroes(ns->blkconf.blk,
                                            nvme_l2b(ns, zone->d.zslba),
                                            nvme_l2b(ns, ns->zone_size),
                                            BDRV_REQ_MAY_UNMAP,
                                            nvme_zone_reset_epilogue_cb,
                                            iocb);
        return;
    }

done:
    iocb->aiocb = NULL;

    iocb->common.cb(iocb->common.opaque, iocb->ret);
    qemu_aio_unref(iocb);
}

static uint16_t nvme_zone_mgmt_send_zrwa_flush(NvmeCtrl *n, NvmeZone *zone,
                                               uint64_t elba, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    uint16_t ozcs = le16_to_cpu(ns->id_ns_zoned->ozcs);
    uint64_t wp = zone->d.wp;
    uint32_t nlb = elba - wp + 1;
    uint16_t status;


    if (!(ozcs & NVME_ID_NS_ZONED_OZCS_ZRWASUP)) {
        return NVME_INVALID_ZONE_OP | NVME_DNR;
    }

    if (!(zone->d.za & NVME_ZA_ZRWA_VALID)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (elba < wp || elba > wp + ns->zns.zrwas) {
        return NVME_ZONE_BOUNDARY_ERROR | NVME_DNR;
    }

    if (nlb % ns->zns.zrwafg) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    status = nvme_zrm_auto(n, ns, zone);
    if (status) {
        return status;
    }

    zone->w_ptr += nlb;

    nvme_advance_zone_wp(ns, zone, nlb);

    return NVME_SUCCESS;
}

static uint16_t nvme_zone_mgmt_send(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeZoneSendCmd *cmd = (NvmeZoneSendCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    NvmeZone *zone;
    NvmeZoneResetAIOCB *iocb;
    uint8_t *zd_ext;
    uint64_t slba = 0;
    uint32_t zone_idx = 0;
    uint16_t status;
    uint8_t action = cmd->zsa;
    bool all;
    enum NvmeZoneProcessingMask proc_mask = NVME_PROC_CURRENT_ZONE;

    all = cmd->zsflags & NVME_ZSFLAG_SELECT_ALL;

    req->status = NVME_SUCCESS;

    if (!all) {
        status = nvme_get_mgmt_zone_slba_idx(ns, &req->cmd, &slba, &zone_idx);
        if (status) {
            return status;
        }
    }

    zone = &ns->zone_array[zone_idx];
    if (slba != zone->d.zslba && action != NVME_ZONE_ACTION_ZRWA_FLUSH) {
        trace_pci_nvme_err_unaligned_zone_cmd(action, slba, zone->d.zslba);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    switch (action) {

    case NVME_ZONE_ACTION_OPEN:
        if (all) {
            proc_mask = NVME_PROC_CLOSED_ZONES;
        }
        trace_pci_nvme_open_zone(slba, zone_idx, all);
        status = nvme_do_zone_op(ns, zone, proc_mask, nvme_open_zone, req);
        break;

    case NVME_ZONE_ACTION_CLOSE:
        if (all) {
            proc_mask = NVME_PROC_OPENED_ZONES;
        }
        trace_pci_nvme_close_zone(slba, zone_idx, all);
        status = nvme_do_zone_op(ns, zone, proc_mask, nvme_close_zone, req);
        break;

    case NVME_ZONE_ACTION_FINISH:
        if (all) {
            proc_mask = NVME_PROC_OPENED_ZONES | NVME_PROC_CLOSED_ZONES;
        }
        trace_pci_nvme_finish_zone(slba, zone_idx, all);
        status = nvme_do_zone_op(ns, zone, proc_mask, nvme_finish_zone, req);
        break;

    case NVME_ZONE_ACTION_RESET:
        trace_pci_nvme_reset_zone(slba, zone_idx, all);

        iocb = blk_aio_get(&nvme_zone_reset_aiocb_info, ns->blkconf.blk,
                           nvme_misc_cb, req);

        iocb->req = req;
        iocb->ret = 0;
        iocb->all = all;
        iocb->idx = zone_idx;
        iocb->zone = NULL;

        req->aiocb = &iocb->common;
        nvme_zone_reset_cb(iocb, 0);

        return NVME_NO_COMPLETE;

    case NVME_ZONE_ACTION_OFFLINE:
        if (all) {
            proc_mask = NVME_PROC_READ_ONLY_ZONES;
        }
        trace_pci_nvme_offline_zone(slba, zone_idx, all);
        status = nvme_do_zone_op(ns, zone, proc_mask, nvme_offline_zone, req);
        break;

    case NVME_ZONE_ACTION_SET_ZD_EXT:
        trace_pci_nvme_set_descriptor_extension(slba, zone_idx);
        if (all || !ns->params.zd_extension_size) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }
        zd_ext = nvme_get_zd_extension(ns, zone_idx);
        status = nvme_h2c(n, zd_ext, ns->params.zd_extension_size, req);
        if (status) {
            trace_pci_nvme_err_zd_extension_map_error(zone_idx);
            return status;
        }

        status = nvme_set_zd_ext(ns, zone);
        if (status == NVME_SUCCESS) {
            trace_pci_nvme_zd_extension_set(zone_idx);
            return status;
        }
        break;

    case NVME_ZONE_ACTION_ZRWA_FLUSH:
        if (all) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        return nvme_zone_mgmt_send_zrwa_flush(n, zone, slba, req);

    default:
        trace_pci_nvme_err_invalid_mgmt_action(action);
        status = NVME_INVALID_FIELD;
    }

    if (status == NVME_ZONE_INVAL_TRANSITION) {
        trace_pci_nvme_err_invalid_zone_state_transition(action, slba,
                                                         zone->d.za);
    }
    if (status) {
        status |= NVME_DNR;
    }

    return status;
}

static bool nvme_zone_matches_filter(uint32_t zafs, NvmeZone *zl)
{
    NvmeZoneState zs = nvme_get_zone_state(zl);

    switch (zafs) {
    case NVME_ZONE_REPORT_ALL:
        return true;
    case NVME_ZONE_REPORT_EMPTY:
        return zs == NVME_ZONE_STATE_EMPTY;
    case NVME_ZONE_REPORT_IMPLICITLY_OPEN:
        return zs == NVME_ZONE_STATE_IMPLICITLY_OPEN;
    case NVME_ZONE_REPORT_EXPLICITLY_OPEN:
        return zs == NVME_ZONE_STATE_EXPLICITLY_OPEN;
    case NVME_ZONE_REPORT_CLOSED:
        return zs == NVME_ZONE_STATE_CLOSED;
    case NVME_ZONE_REPORT_FULL:
        return zs == NVME_ZONE_STATE_FULL;
    case NVME_ZONE_REPORT_READ_ONLY:
        return zs == NVME_ZONE_STATE_READ_ONLY;
    case NVME_ZONE_REPORT_OFFLINE:
        return zs == NVME_ZONE_STATE_OFFLINE;
    default:
        return false;
    }
}

static uint16_t nvme_zone_mgmt_recv(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = &req->cmd;
    NvmeNamespace *ns = req->ns;
    /* cdw12 is zero-based number of dwords to return. Convert to bytes */
    uint32_t data_size = (le32_to_cpu(cmd->cdw12) + 1) << 2;
    uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint32_t zone_idx, zra, zrasf, partial;
    uint64_t max_zones, nr_zones = 0;
    uint16_t status;
    uint64_t slba;
    NvmeZoneDescr *z;
    NvmeZone *zone;
    NvmeZoneReportHeader *header;
    void *buf, *buf_p;
    size_t zone_entry_sz;
    int i;

    req->status = NVME_SUCCESS;

    status = nvme_get_mgmt_zone_slba_idx(ns, cmd, &slba, &zone_idx);
    if (status) {
        return status;
    }

    zra = dw13 & 0xff;
    if (zra != NVME_ZONE_REPORT && zra != NVME_ZONE_REPORT_EXTENDED) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    if (zra == NVME_ZONE_REPORT_EXTENDED && !ns->params.zd_extension_size) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    zrasf = (dw13 >> 8) & 0xff;
    if (zrasf > NVME_ZONE_REPORT_OFFLINE) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (data_size < sizeof(NvmeZoneReportHeader)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    status = nvme_check_mdts(n, data_size);
    if (status) {
        return status;
    }

    partial = (dw13 >> 16) & 0x01;

    zone_entry_sz = sizeof(NvmeZoneDescr);
    if (zra == NVME_ZONE_REPORT_EXTENDED) {
        zone_entry_sz += ns->params.zd_extension_size;
    }

    max_zones = (data_size - sizeof(NvmeZoneReportHeader)) / zone_entry_sz;
    buf = g_malloc0(data_size);

    zone = &ns->zone_array[zone_idx];
    for (i = zone_idx; i < ns->num_zones; i++) {
        if (partial && nr_zones >= max_zones) {
            break;
        }
        if (nvme_zone_matches_filter(zrasf, zone++)) {
            nr_zones++;
        }
    }
    header = buf;
    header->nr_zones = cpu_to_le64(nr_zones);

    buf_p = buf + sizeof(NvmeZoneReportHeader);
    for (; zone_idx < ns->num_zones && max_zones > 0; zone_idx++) {
        zone = &ns->zone_array[zone_idx];
        if (nvme_zone_matches_filter(zrasf, zone)) {
            z = buf_p;
            buf_p += sizeof(NvmeZoneDescr);

            z->zt = zone->d.zt;
            z->zs = zone->d.zs;
            z->zcap = cpu_to_le64(zone->d.zcap);
            z->zslba = cpu_to_le64(zone->d.zslba);
            z->za = zone->d.za;

            if (nvme_wp_is_valid(zone)) {
                z->wp = cpu_to_le64(zone->d.wp);
            } else {
                z->wp = cpu_to_le64(~0ULL);
            }

            if (zra == NVME_ZONE_REPORT_EXTENDED) {
                if (zone->d.za & NVME_ZA_ZD_EXT_VALID) {
                    memcpy(buf_p, nvme_get_zd_extension(ns, zone_idx),
                           ns->params.zd_extension_size);
                }
                buf_p += ns->params.zd_extension_size;
            }

            max_zones--;
        }
    }

    status = nvme_c2h(n, (uint8_t *)buf, data_size, req);

    g_free(buf);

    return status;
}

static uint16_t nvme_io_mgmt_recv_ruhs(NvmeCtrl *n, NvmeRequest *req,
                                       size_t len)
{
    NvmeNamespace *ns = req->ns;
    NvmeEnduranceGroup *endgrp;
    NvmeRuhStatus *hdr;
    NvmeRuhStatusDescr *ruhsd;
    unsigned int nruhsd;
    uint16_t rg, ph, *ruhid;
    size_t trans_len;
    g_autofree uint8_t *buf = NULL;

    if (!n->subsys) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (ns->params.nsid == 0 || ns->params.nsid == 0xffffffff) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    if (!n->subsys->endgrp.fdp.enabled) {
        return NVME_FDP_DISABLED | NVME_DNR;
    }

    endgrp = ns->endgrp;

    nruhsd = ns->fdp.nphs * endgrp->fdp.nrg;
    trans_len = sizeof(NvmeRuhStatus) + nruhsd * sizeof(NvmeRuhStatusDescr);
    buf = g_malloc0(trans_len);

    trans_len = MIN(trans_len, len);

    hdr = (NvmeRuhStatus *)buf;
    ruhsd = (NvmeRuhStatusDescr *)(buf + sizeof(NvmeRuhStatus));

    hdr->nruhsd = cpu_to_le16(nruhsd);

    ruhid = ns->fdp.phs;

    for (ph = 0; ph < ns->fdp.nphs; ph++, ruhid++) {
        NvmeRuHandle *ruh = &endgrp->fdp.ruhs[*ruhid];

        for (rg = 0; rg < endgrp->fdp.nrg; rg++, ruhsd++) {
            uint16_t pid = nvme_make_pid(ns, rg, ph);

            ruhsd->pid = cpu_to_le16(pid);
            ruhsd->ruhid = *ruhid;
            ruhsd->earutr = 0;
            ruhsd->ruamw = cpu_to_le64(ruh->rus[rg].ruamw);
        }
    }

    return nvme_c2h(n, buf, trans_len, req);
}

static uint16_t nvme_io_mgmt_recv(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = &req->cmd;
    uint32_t cdw10 = le32_to_cpu(cmd->cdw10);
    uint32_t numd = le32_to_cpu(cmd->cdw11);
    uint8_t mo = (cdw10 & 0xff);
    size_t len = (numd + 1) << 2;

    switch (mo) {
    case NVME_IOMR_MO_NOP:
        return 0;
    case NVME_IOMR_MO_RUH_STATUS:
        return nvme_io_mgmt_recv_ruhs(n, req, len);
    default:
        return NVME_INVALID_FIELD | NVME_DNR;
    };
}

static uint16_t nvme_io_mgmt_send_ruh_update(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = &req->cmd;
    NvmeNamespace *ns = req->ns;
    uint32_t cdw10 = le32_to_cpu(cmd->cdw10);
    uint16_t ret = NVME_SUCCESS;
    uint32_t npid = (cdw10 >> 16) + 1;
    unsigned int i = 0;
    g_autofree uint16_t *pids = NULL;
    uint32_t maxnpid;

    if (!ns->endgrp || !ns->endgrp->fdp.enabled) {
        return NVME_FDP_DISABLED | NVME_DNR;
    }

    maxnpid = n->subsys->endgrp.fdp.nrg * n->subsys->endgrp.fdp.nruh;

    if (unlikely(npid >= MIN(NVME_FDP_MAXPIDS, maxnpid))) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    pids = g_new(uint16_t, npid);

    ret = nvme_h2c(n, pids, npid * sizeof(uint16_t), req);
    if (ret) {
        return ret;
    }

    for (; i < npid; i++) {
        if (!nvme_update_ruh(n, ns, pids[i])) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }
    }

    return ret;
}

static uint16_t nvme_io_mgmt_send(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = &req->cmd;
    uint32_t cdw10 = le32_to_cpu(cmd->cdw10);
    uint8_t mo = (cdw10 & 0xff);

    switch (mo) {
    case NVME_IOMS_MO_NOP:
        return 0;
    case NVME_IOMS_MO_RUH_UPDATE:
        return nvme_io_mgmt_send_ruh_update(n, req);
    default:
        return NVME_INVALID_FIELD | NVME_DNR;
    };
}

static uint16_t __nvme_io_cmd_nvm(NvmeCtrl *n, NvmeRequest *req)
{
    switch (req->cmd.opcode) {
    case NVME_CMD_WRITE:
        return nvme_write(n, req);
    case NVME_CMD_READ:
        return nvme_read(n, req);
    case NVME_CMD_COMPARE:
        return nvme_compare(n, req);
    case NVME_CMD_WRITE_ZEROES:
        return nvme_write_zeroes(n, req);
    case NVME_CMD_DSM:
        return nvme_dsm(n, req);
    case NVME_CMD_VERIFY:
        return nvme_verify(n, req);
    case NVME_CMD_COPY:
        return nvme_copy(n, req);
    case NVME_CMD_IO_MGMT_RECV:
        return nvme_io_mgmt_recv(n, req);
    case NVME_CMD_IO_MGMT_SEND:
        return nvme_io_mgmt_send(n, req);
    }

    g_assert_not_reached();
}

static uint16_t nvme_io_cmd_nvm(NvmeCtrl *n, NvmeRequest *req)
{
    if (!(n->cse.iocs.nvm[req->cmd.opcode] & NVME_CMD_EFF_CSUPP)) {
        trace_pci_nvme_err_invalid_opc(req->cmd.opcode);
        return NVME_INVALID_OPCODE | NVME_DNR;
    }

    return __nvme_io_cmd_nvm(n, req);
}

static uint16_t nvme_io_cmd_zoned(NvmeCtrl *n, NvmeRequest *req)
{
    if (!(n->cse.iocs.zoned[req->cmd.opcode] & NVME_CMD_EFF_CSUPP)) {
        trace_pci_nvme_err_invalid_opc(req->cmd.opcode);
        return NVME_INVALID_OPCODE | NVME_DNR;
    }

    switch (req->cmd.opcode) {
    case NVME_CMD_ZONE_APPEND:
        return nvme_zone_append(n, req);
    case NVME_CMD_ZONE_MGMT_SEND:
        return nvme_zone_mgmt_send(n, req);
    case NVME_CMD_ZONE_MGMT_RECV:
        return nvme_zone_mgmt_recv(n, req);
    }

    return __nvme_io_cmd_nvm(n, req);
}

static uint16_t nvme_io_cmd(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns;
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);

    trace_pci_nvme_io_cmd(nvme_cid(req), nsid, nvme_sqid(req),
                          req->cmd.opcode, nvme_io_opc_str(req->cmd.opcode));

    /*
     * In the base NVM command set, Flush may apply to all namespaces
     * (indicated by NSID being set to FFFFFFFFh). But if that feature is used
     * along with TP 4056 (Namespace Types), it may be pretty screwed up.
     *
     * If NSID is indeed set to FFFFFFFFh, we simply cannot associate the
     * opcode with a specific command since we cannot determine a unique I/O
     * command set. Opcode 0h could have any other meaning than something
     * equivalent to flushing and say it DOES have completely different
     * semantics in some other command set - does an NSID of FFFFFFFFh then
     * mean "for all namespaces, apply whatever command set specific command
     * that uses the 0h opcode?" Or does it mean "for all namespaces, apply
     * whatever command that uses the 0h opcode if, and only if, it allows NSID
     * to be FFFFFFFFh"?
     *
     * Anyway (and luckily), for now, we do not care about this since the
     * device only supports namespace types that includes the NVM Flush command
     * (NVM and Zoned), so always do an NVM Flush.
     */

    if (req->cmd.opcode == NVME_CMD_FLUSH) {
        return nvme_flush(n, req);
    }

    if (!nvme_nsid_valid(n, nsid) || nsid == NVME_NSID_BROADCAST) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    ns = nvme_ns(n, nsid);
    if (unlikely(!ns)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (ns->status) {
        return ns->status;
    }

    if (NVME_CMD_FLAGS_FUSE(req->cmd.flags)) {
        return NVME_INVALID_FIELD;
    }

    req->ns = ns;

    switch (ns->csi) {
    case NVME_CSI_NVM:
        return nvme_io_cmd_nvm(n, req);
    case NVME_CSI_ZONED:
        return nvme_io_cmd_zoned(n, req);
    }

    g_assert_not_reached();
}

static void nvme_cq_notifier(EventNotifier *e)
{
    NvmeCQueue *cq = container_of(e, NvmeCQueue, notifier);
    NvmeCtrl *n = cq->ctrl;

    if (!event_notifier_test_and_clear(e)) {
        return;
    }

    nvme_update_cq_head(cq);

    if (cq->tail == cq->head) {
        if (cq->irq_enabled) {
            n->cq_pending--;
        }

        nvme_irq_deassert(n, cq);
    }

    qemu_bh_schedule(cq->bh);
}

static int nvme_init_cq_ioeventfd(NvmeCQueue *cq)
{
    NvmeCtrl *n = cq->ctrl;
    uint16_t offset = (cq->cqid << 3) + (1 << 2);
    int ret;

    ret = event_notifier_init(&cq->notifier, 0);
    if (ret < 0) {
        return ret;
    }

    event_notifier_set_handler(&cq->notifier, nvme_cq_notifier);
    memory_region_add_eventfd(&n->iomem,
                              0x1000 + offset, 4, false, 0, &cq->notifier);

    return 0;
}

static void nvme_sq_notifier(EventNotifier *e)
{
    NvmeSQueue *sq = container_of(e, NvmeSQueue, notifier);

    if (!event_notifier_test_and_clear(e)) {
        return;
    }

    nvme_process_sq(sq);
}

static int nvme_init_sq_ioeventfd(NvmeSQueue *sq)
{
    NvmeCtrl *n = sq->ctrl;
    uint16_t offset = sq->sqid << 3;
    int ret;

    ret = event_notifier_init(&sq->notifier, 0);
    if (ret < 0) {
        return ret;
    }

    event_notifier_set_handler(&sq->notifier, nvme_sq_notifier);
    memory_region_add_eventfd(&n->iomem,
                              0x1000 + offset, 4, false, 0, &sq->notifier);

    return 0;
}

static void nvme_free_sq(NvmeSQueue *sq, NvmeCtrl *n)
{
    uint16_t offset = sq->sqid << 3;

    n->sq[sq->sqid] = NULL;
    qemu_bh_delete(sq->bh);
    if (sq->ioeventfd_enabled) {
        memory_region_del_eventfd(&n->iomem,
                                  0x1000 + offset, 4, false, 0, &sq->notifier);
        event_notifier_set_handler(&sq->notifier, NULL);
        event_notifier_cleanup(&sq->notifier);
    }
    g_free(sq->io_req);
    if (sq->sqid) {
        g_free(sq);
    }
}

static uint16_t nvme_del_sq(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeDeleteQ *c = (NvmeDeleteQ *)&req->cmd;
    NvmeRequest *r, *next;
    NvmeSQueue *sq;
    NvmeCQueue *cq;
    uint16_t qid = le16_to_cpu(c->qid);

    if (unlikely(!qid || nvme_check_sqid(n, qid))) {
        trace_pci_nvme_err_invalid_del_sq(qid);
        return NVME_INVALID_QID | NVME_DNR;
    }

    trace_pci_nvme_del_sq(qid);

    sq = n->sq[qid];
    while (!QTAILQ_EMPTY(&sq->out_req_list)) {
        r = QTAILQ_FIRST(&sq->out_req_list);
        assert(r->aiocb);
        r->status = NVME_CMD_ABORT_SQ_DEL;
        blk_aio_cancel(r->aiocb);
    }

    assert(QTAILQ_EMPTY(&sq->out_req_list));

    if (!nvme_check_cqid(n, sq->cqid)) {
        cq = n->cq[sq->cqid];
        QTAILQ_REMOVE(&cq->sq_list, sq, entry);

        nvme_post_cqes(cq);
        QTAILQ_FOREACH_SAFE(r, &cq->req_list, entry, next) {
            if (r->sq == sq) {
                QTAILQ_REMOVE(&cq->req_list, r, entry);
                QTAILQ_INSERT_TAIL(&sq->req_list, r, entry);
            }
        }
    }

    nvme_free_sq(sq, n);
    return NVME_SUCCESS;
}

static void nvme_init_sq(NvmeSQueue *sq, NvmeCtrl *n, uint64_t dma_addr,
                         uint16_t sqid, uint16_t cqid, uint16_t size)
{
    int i;
    NvmeCQueue *cq;

    sq->ctrl = n;
    sq->dma_addr = dma_addr;
    sq->sqid = sqid;
    sq->size = size;
    sq->cqid = cqid;
    sq->head = sq->tail = 0;
    sq->io_req = g_new0(NvmeRequest, sq->size);

    QTAILQ_INIT(&sq->req_list);
    QTAILQ_INIT(&sq->out_req_list);
    for (i = 0; i < sq->size; i++) {
        sq->io_req[i].sq = sq;
        QTAILQ_INSERT_TAIL(&(sq->req_list), &sq->io_req[i], entry);
    }

    sq->bh = qemu_bh_new_guarded(nvme_process_sq, sq,
                                 &DEVICE(sq->ctrl)->mem_reentrancy_guard);

    if (n->dbbuf_enabled) {
        sq->db_addr = n->dbbuf_dbs + (sqid << 3);
        sq->ei_addr = n->dbbuf_eis + (sqid << 3);

        if (n->params.ioeventfd && sq->sqid != 0) {
            if (!nvme_init_sq_ioeventfd(sq)) {
                sq->ioeventfd_enabled = true;
            }
        }
    }

    assert(n->cq[cqid]);
    cq = n->cq[cqid];
    QTAILQ_INSERT_TAIL(&(cq->sq_list), sq, entry);
    n->sq[sqid] = sq;
}

static uint16_t nvme_create_sq(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeSQueue *sq;
    NvmeCreateSq *c = (NvmeCreateSq *)&req->cmd;

    uint16_t cqid = le16_to_cpu(c->cqid);
    uint16_t sqid = le16_to_cpu(c->sqid);
    uint16_t qsize = le16_to_cpu(c->qsize);
    uint16_t qflags = le16_to_cpu(c->sq_flags);
    uint64_t prp1 = le64_to_cpu(c->prp1);

    trace_pci_nvme_create_sq(prp1, sqid, cqid, qsize, qflags);

    if (unlikely(!cqid || nvme_check_cqid(n, cqid))) {
        trace_pci_nvme_err_invalid_create_sq_cqid(cqid);
        return NVME_INVALID_CQID | NVME_DNR;
    }
    if (unlikely(!sqid || sqid > n->conf_ioqpairs || n->sq[sqid] != NULL)) {
        trace_pci_nvme_err_invalid_create_sq_sqid(sqid);
        return NVME_INVALID_QID | NVME_DNR;
    }
    if (unlikely(!qsize || qsize > NVME_CAP_MQES(ldq_le_p(&n->bar.cap)))) {
        trace_pci_nvme_err_invalid_create_sq_size(qsize);
        return NVME_MAX_QSIZE_EXCEEDED | NVME_DNR;
    }
    if (unlikely(prp1 & (n->page_size - 1))) {
        trace_pci_nvme_err_invalid_create_sq_addr(prp1);
        return NVME_INVALID_PRP_OFFSET | NVME_DNR;
    }
    if (unlikely(!(NVME_SQ_FLAGS_PC(qflags)))) {
        trace_pci_nvme_err_invalid_create_sq_qflags(NVME_SQ_FLAGS_PC(qflags));
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    sq = g_malloc0(sizeof(*sq));
    nvme_init_sq(sq, n, prp1, sqid, cqid, qsize + 1);
    return NVME_SUCCESS;
}

struct nvme_stats {
    uint64_t units_read;
    uint64_t units_written;
    uint64_t read_commands;
    uint64_t write_commands;
};

static void nvme_set_blk_stats(NvmeNamespace *ns, struct nvme_stats *stats)
{
    BlockAcctStats *s = blk_get_stats(ns->blkconf.blk);

    stats->units_read += s->nr_bytes[BLOCK_ACCT_READ];
    stats->units_written += s->nr_bytes[BLOCK_ACCT_WRITE];
    stats->read_commands += s->nr_ops[BLOCK_ACCT_READ];
    stats->write_commands += s->nr_ops[BLOCK_ACCT_WRITE];
}

static uint16_t nvme_ocp_extended_smart_info(NvmeCtrl *n, uint8_t rae,
                                             uint32_t buf_len, uint64_t off,
                                             NvmeRequest *req)
{
    NvmeNamespace *ns = NULL;
    NvmeSmartLogExtended smart_l = { 0 };
    struct nvme_stats stats = { 0 };
    uint32_t trans_len;

    if (off >= sizeof(smart_l)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    /* accumulate all stats from all namespaces */
    for (int i = 1; i <= NVME_MAX_NAMESPACES; i++) {
        ns = nvme_ns(n, i);
        if (ns) {
            nvme_set_blk_stats(ns, &stats);
        }
    }

    smart_l.physical_media_units_written[0] = cpu_to_le64(stats.units_written);
    smart_l.physical_media_units_read[0] = cpu_to_le64(stats.units_read);
    smart_l.log_page_version = 0x0005;

    static const uint8_t guid[16] = {
        0xC5, 0xAF, 0x10, 0x28, 0xEA, 0xBF, 0xF2, 0xA4,
        0x9C, 0x4F, 0x6F, 0x7C, 0xC9, 0x14, 0xD5, 0xAF
    };
    memcpy(smart_l.log_page_guid, guid, sizeof(smart_l.log_page_guid));

    if (!rae) {
        nvme_clear_events(n, NVME_AER_TYPE_SMART);
    }

    trans_len = MIN(sizeof(smart_l) - off, buf_len);
    return nvme_c2h(n, (uint8_t *) &smart_l + off, trans_len, req);
}

static uint16_t nvme_smart_info(NvmeCtrl *n, uint8_t rae, uint32_t buf_len,
                                uint64_t off, NvmeRequest *req)
{
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);
    struct nvme_stats stats = { 0 };
    NvmeSmartLog smart = { 0 };
    uint32_t trans_len;
    NvmeNamespace *ns;
    time_t current_ms;
    uint64_t u_read, u_written;

    if (off >= sizeof(smart)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (nsid != 0xffffffff) {
        ns = nvme_ns(n, nsid);
        if (!ns) {
            return NVME_INVALID_NSID | NVME_DNR;
        }
        nvme_set_blk_stats(ns, &stats);
    } else {
        int i;

        for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
            ns = nvme_ns(n, i);
            if (!ns) {
                continue;
            }
            nvme_set_blk_stats(ns, &stats);
        }
    }

    trans_len = MIN(sizeof(smart) - off, buf_len);
    smart.critical_warning = n->smart_critical_warning;

    u_read = DIV_ROUND_UP(stats.units_read >> BDRV_SECTOR_BITS, 1000);
    u_written = DIV_ROUND_UP(stats.units_written >> BDRV_SECTOR_BITS, 1000);

    smart.data_units_read[0] = cpu_to_le64(u_read);
    smart.data_units_written[0] = cpu_to_le64(u_written);
    smart.host_read_commands[0] = cpu_to_le64(stats.read_commands);
    smart.host_write_commands[0] = cpu_to_le64(stats.write_commands);

    smart.temperature = cpu_to_le16(n->temperature);

    if ((n->temperature >= n->features.temp_thresh_hi) ||
        (n->temperature <= n->features.temp_thresh_low)) {
        smart.critical_warning |= NVME_SMART_TEMPERATURE;
    }

    current_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    smart.power_on_hours[0] =
        cpu_to_le64((((current_ms - n->starttime_ms) / 1000) / 60) / 60);

    if (!rae) {
        nvme_clear_events(n, NVME_AER_TYPE_SMART);
    }

    return nvme_c2h(n, (uint8_t *) &smart + off, trans_len, req);
}

static uint16_t nvme_endgrp_info(NvmeCtrl *n,  uint8_t rae, uint32_t buf_len,
                                 uint64_t off, NvmeRequest *req)
{
    uint32_t dw11 = le32_to_cpu(req->cmd.cdw11);
    uint16_t endgrpid = (dw11 >> 16) & 0xffff;
    struct nvme_stats stats = {};
    NvmeEndGrpLog info = {};
    int i;

    if (!n->subsys || endgrpid != 0x1) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (off >= sizeof(info)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
        NvmeNamespace *ns = nvme_subsys_ns(n->subsys, i);
        if (!ns) {
            continue;
        }

        nvme_set_blk_stats(ns, &stats);
    }

    info.data_units_read[0] =
        cpu_to_le64(DIV_ROUND_UP(stats.units_read / 1000000000, 1000000000));
    info.data_units_written[0] =
        cpu_to_le64(DIV_ROUND_UP(stats.units_written / 1000000000, 1000000000));
    info.media_units_written[0] =
        cpu_to_le64(DIV_ROUND_UP(stats.units_written / 1000000000, 1000000000));

    info.host_read_commands[0] = cpu_to_le64(stats.read_commands);
    info.host_write_commands[0] = cpu_to_le64(stats.write_commands);

    buf_len = MIN(sizeof(info) - off, buf_len);

    return nvme_c2h(n, (uint8_t *)&info + off, buf_len, req);
}


static uint16_t nvme_fw_log_info(NvmeCtrl *n, uint32_t buf_len, uint64_t off,
                                 NvmeRequest *req)
{
    uint32_t trans_len;
    NvmeFwSlotInfoLog fw_log = {
        .afi = 0x1,
    };

    if (off >= sizeof(fw_log)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    strpadcpy((char *)&fw_log.frs1, sizeof(fw_log.frs1), "1.0", ' ');
    trans_len = MIN(sizeof(fw_log) - off, buf_len);

    return nvme_c2h(n, (uint8_t *) &fw_log + off, trans_len, req);
}

static uint16_t nvme_error_info(NvmeCtrl *n, uint8_t rae, uint32_t buf_len,
                                uint64_t off, NvmeRequest *req)
{
    uint32_t trans_len;
    NvmeErrorLog errlog;

    if (off >= sizeof(errlog)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (!rae) {
        nvme_clear_events(n, NVME_AER_TYPE_ERROR);
    }

    memset(&errlog, 0x0, sizeof(errlog));
    trans_len = MIN(sizeof(errlog) - off, buf_len);

    return nvme_c2h(n, (uint8_t *)&errlog, trans_len, req);
}

static uint16_t nvme_changed_nslist(NvmeCtrl *n, uint8_t rae, uint32_t buf_len,
                                    uint64_t off, NvmeRequest *req)
{
    uint32_t nslist[1024];
    uint32_t trans_len;
    int i = 0;
    uint32_t nsid;

    if (off >= sizeof(nslist)) {
        trace_pci_nvme_err_invalid_log_page_offset(off, sizeof(nslist));
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    memset(nslist, 0x0, sizeof(nslist));
    trans_len = MIN(sizeof(nslist) - off, buf_len);

    while ((nsid = find_first_bit(n->changed_nsids, NVME_CHANGED_NSID_SIZE)) !=
            NVME_CHANGED_NSID_SIZE) {
        /*
         * If more than 1024 namespaces, the first entry in the log page should
         * be set to FFFFFFFFh and the others to 0 as spec.
         */
        if (i == ARRAY_SIZE(nslist)) {
            memset(nslist, 0x0, sizeof(nslist));
            nslist[0] = 0xffffffff;
            break;
        }

        nslist[i++] = nsid;
        clear_bit(nsid, n->changed_nsids);
    }

    /*
     * Remove all the remaining list entries in case returns directly due to
     * more than 1024 namespaces.
     */
    if (nslist[0] == 0xffffffff) {
        bitmap_zero(n->changed_nsids, NVME_CHANGED_NSID_SIZE);
    }

    if (!rae) {
        nvme_clear_events(n, NVME_AER_TYPE_NOTICE);
    }

    return nvme_c2h(n, ((uint8_t *)nslist) + off, trans_len, req);
}

static uint16_t nvme_cmd_effects(NvmeCtrl *n, uint8_t csi, uint32_t buf_len,
                                 uint64_t off, NvmeRequest *req)
{
    NvmeEffectsLog log = {};
    const uint32_t *iocs = NULL;
    uint32_t trans_len;

    if (off >= sizeof(log)) {
        trace_pci_nvme_err_invalid_log_page_offset(off, sizeof(log));
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    switch (NVME_CC_CSS(ldl_le_p(&n->bar.cc))) {
    case NVME_CC_CSS_NVM:
        iocs = n->cse.iocs.nvm;
        break;

    case NVME_CC_CSS_ALL:
        switch (csi) {
        case NVME_CSI_NVM:
            iocs = n->cse.iocs.nvm;
            break;
        case NVME_CSI_ZONED:
            iocs = n->cse.iocs.zoned;
            break;
        }

        break;
    }

    memcpy(log.acs, n->cse.acs, sizeof(log.acs));

    if (iocs) {
        memcpy(log.iocs, iocs, sizeof(log.iocs));
    }

    trans_len = MIN(sizeof(log) - off, buf_len);

    return nvme_c2h(n, ((uint8_t *)&log) + off, trans_len, req);
}

static uint16_t nvme_vendor_specific_log(NvmeCtrl *n, uint8_t rae,
                                         uint32_t buf_len, uint64_t off,
                                         NvmeRequest *req, uint8_t lid)
{
    switch (lid) {
    case NVME_OCP_EXTENDED_SMART_INFO:
        if (n->params.ocp) {
            return nvme_ocp_extended_smart_info(n, rae, buf_len, off, req);
        }
        break;
        /* add a case for each additional vendor specific log id */
    }

    trace_pci_nvme_err_invalid_log_page(nvme_cid(req), lid);
    return NVME_INVALID_FIELD | NVME_DNR;
}

static size_t sizeof_fdp_conf_descr(size_t nruh, size_t vss)
{
    size_t entry_siz = sizeof(NvmeFdpDescrHdr) + nruh * sizeof(NvmeRuhDescr)
                       + vss;
    return ROUND_UP(entry_siz, 8);
}

static uint16_t nvme_fdp_confs(NvmeCtrl *n, uint32_t endgrpid, uint32_t buf_len,
                               uint64_t off, NvmeRequest *req)
{
    uint32_t log_size, trans_len;
    g_autofree uint8_t *buf = NULL;
    NvmeFdpDescrHdr *hdr;
    NvmeRuhDescr *ruhd;
    NvmeEnduranceGroup *endgrp;
    NvmeFdpConfsHdr *log;
    size_t nruh, fdp_descr_size;
    int i;

    if (endgrpid != 1 || !n->subsys) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    endgrp = &n->subsys->endgrp;

    if (endgrp->fdp.enabled) {
        nruh = endgrp->fdp.nruh;
    } else {
        nruh = 1;
    }

    fdp_descr_size = sizeof_fdp_conf_descr(nruh, FDPVSS);
    log_size = sizeof(NvmeFdpConfsHdr) + fdp_descr_size;

    if (off >= log_size) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trans_len = MIN(log_size - off, buf_len);

    buf = g_malloc0(log_size);
    log = (NvmeFdpConfsHdr *)buf;
    hdr = (NvmeFdpDescrHdr *)(log + 1);
    ruhd = (NvmeRuhDescr *)(buf + sizeof(*log) + sizeof(*hdr));

    log->num_confs = cpu_to_le16(0);
    log->size = cpu_to_le32(log_size);

    hdr->descr_size = cpu_to_le16(fdp_descr_size);
    if (endgrp->fdp.enabled) {
        hdr->fdpa = FIELD_DP8(hdr->fdpa, FDPA, VALID, 1);
        hdr->fdpa = FIELD_DP8(hdr->fdpa, FDPA, RGIF, endgrp->fdp.rgif);
        hdr->nrg = cpu_to_le16(endgrp->fdp.nrg);
        hdr->nruh = cpu_to_le16(endgrp->fdp.nruh);
        hdr->maxpids = cpu_to_le16(NVME_FDP_MAXPIDS - 1);
        hdr->nnss = cpu_to_le32(NVME_MAX_NAMESPACES);
        hdr->runs = cpu_to_le64(endgrp->fdp.runs);

        for (i = 0; i < nruh; i++) {
            ruhd->ruht = NVME_RUHT_INITIALLY_ISOLATED;
            ruhd++;
        }
    } else {
        /* 1 bit for RUH in PIF -> 2 RUHs max. */
        hdr->nrg = cpu_to_le16(1);
        hdr->nruh = cpu_to_le16(1);
        hdr->maxpids = cpu_to_le16(NVME_FDP_MAXPIDS - 1);
        hdr->nnss = cpu_to_le32(1);
        hdr->runs = cpu_to_le64(96 * MiB);

        ruhd->ruht = NVME_RUHT_INITIALLY_ISOLATED;
    }

    return nvme_c2h(n, (uint8_t *)buf + off, trans_len, req);
}

static uint16_t nvme_fdp_ruh_usage(NvmeCtrl *n, uint32_t endgrpid,
                                   uint32_t dw10, uint32_t dw12,
                                   uint32_t buf_len, uint64_t off,
                                   NvmeRequest *req)
{
    NvmeRuHandle *ruh;
    NvmeRuhuLog *hdr;
    NvmeRuhuDescr *ruhud;
    NvmeEnduranceGroup *endgrp;
    g_autofree uint8_t *buf = NULL;
    uint32_t log_size, trans_len;
    uint16_t i;

    if (endgrpid != 1 || !n->subsys) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    endgrp = &n->subsys->endgrp;

    if (!endgrp->fdp.enabled) {
        return NVME_FDP_DISABLED | NVME_DNR;
    }

    log_size = sizeof(NvmeRuhuLog) + endgrp->fdp.nruh * sizeof(NvmeRuhuDescr);

    if (off >= log_size) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trans_len = MIN(log_size - off, buf_len);

    buf = g_malloc0(log_size);
    hdr = (NvmeRuhuLog *)buf;
    ruhud = (NvmeRuhuDescr *)(hdr + 1);

    ruh = endgrp->fdp.ruhs;
    hdr->nruh = cpu_to_le16(endgrp->fdp.nruh);

    for (i = 0; i < endgrp->fdp.nruh; i++, ruhud++, ruh++) {
        ruhud->ruha = ruh->ruha;
    }

    return nvme_c2h(n, (uint8_t *)buf + off, trans_len, req);
}

static uint16_t nvme_fdp_stats(NvmeCtrl *n, uint32_t endgrpid, uint32_t buf_len,
                               uint64_t off, NvmeRequest *req)
{
    NvmeEnduranceGroup *endgrp;
    NvmeFdpStatsLog log = {};
    uint32_t trans_len;

    if (off >= sizeof(NvmeFdpStatsLog)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (endgrpid != 1 || !n->subsys) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (!n->subsys->endgrp.fdp.enabled) {
        return NVME_FDP_DISABLED | NVME_DNR;
    }

    endgrp = &n->subsys->endgrp;

    trans_len = MIN(sizeof(log) - off, buf_len);

    /* spec value is 128 bit, we only use 64 bit */
    log.hbmw[0] = cpu_to_le64(endgrp->fdp.hbmw);
    log.mbmw[0] = cpu_to_le64(endgrp->fdp.mbmw);
    log.mbe[0] = cpu_to_le64(endgrp->fdp.mbe);

    return nvme_c2h(n, (uint8_t *)&log + off, trans_len, req);
}

static uint16_t nvme_fdp_events(NvmeCtrl *n, uint32_t endgrpid,
                                uint32_t buf_len, uint64_t off,
                                NvmeRequest *req)
{
    NvmeEnduranceGroup *endgrp;
    NvmeCmd *cmd = &req->cmd;
    bool host_events = (cmd->cdw10 >> 8) & 0x1;
    uint32_t log_size, trans_len;
    NvmeFdpEventBuffer *ebuf;
    g_autofree NvmeFdpEventsLog *elog = NULL;
    NvmeFdpEvent *event;

    if (endgrpid != 1 || !n->subsys) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    endgrp = &n->subsys->endgrp;

    if (!endgrp->fdp.enabled) {
        return NVME_FDP_DISABLED | NVME_DNR;
    }

    if (host_events) {
        ebuf = &endgrp->fdp.host_events;
    } else {
        ebuf = &endgrp->fdp.ctrl_events;
    }

    log_size = sizeof(NvmeFdpEventsLog) + ebuf->nelems * sizeof(NvmeFdpEvent);

    if (off >= log_size) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trans_len = MIN(log_size - off, buf_len);
    elog = g_malloc0(log_size);
    elog->num_events = cpu_to_le32(ebuf->nelems);
    event = (NvmeFdpEvent *)(elog + 1);

    if (ebuf->nelems && ebuf->start == ebuf->next) {
        unsigned int nelems = (NVME_FDP_MAX_EVENTS - ebuf->start);
        /* wrap over, copy [start;NVME_FDP_MAX_EVENTS[ and [0; next[ */
        memcpy(event, &ebuf->events[ebuf->start],
               sizeof(NvmeFdpEvent) * nelems);
        memcpy(event + nelems, ebuf->events,
               sizeof(NvmeFdpEvent) * ebuf->next);
    } else if (ebuf->start < ebuf->next) {
        memcpy(event, &ebuf->events[ebuf->start],
               sizeof(NvmeFdpEvent) * (ebuf->next - ebuf->start));
    }

    return nvme_c2h(n, (uint8_t *)elog + off, trans_len, req);
}

static uint16_t nvme_get_log(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = &req->cmd;

    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    uint32_t dw12 = le32_to_cpu(cmd->cdw12);
    uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint8_t  lid = dw10 & 0xff;
    uint8_t  lsp = (dw10 >> 8) & 0xf;
    uint8_t  rae = (dw10 >> 15) & 0x1;
    uint8_t  csi = le32_to_cpu(cmd->cdw14) >> 24;
    uint32_t numdl, numdu, lspi;
    uint64_t off, lpol, lpou;
    size_t   len;
    uint16_t status;

    numdl = (dw10 >> 16);
    numdu = (dw11 & 0xffff);
    lspi = (dw11 >> 16);
    lpol = dw12;
    lpou = dw13;

    len = (((numdu << 16) | numdl) + 1) << 2;
    off = (lpou << 32ULL) | lpol;

    if (off & 0x3) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    trace_pci_nvme_get_log(nvme_cid(req), lid, lsp, rae, len, off);

    status = nvme_check_mdts(n, len);
    if (status) {
        return status;
    }

    switch (lid) {
    case NVME_LOG_ERROR_INFO:
        return nvme_error_info(n, rae, len, off, req);
    case NVME_LOG_SMART_INFO:
        return nvme_smart_info(n, rae, len, off, req);
    case NVME_LOG_FW_SLOT_INFO:
        return nvme_fw_log_info(n, len, off, req);
    case NVME_LOG_VENDOR_START...NVME_LOG_VENDOR_END:
        return nvme_vendor_specific_log(n, rae, len, off, req, lid);
    case NVME_LOG_CHANGED_NSLIST:
        return nvme_changed_nslist(n, rae, len, off, req);
    case NVME_LOG_CMD_EFFECTS:
        return nvme_cmd_effects(n, csi, len, off, req);
    case NVME_LOG_ENDGRP:
        return nvme_endgrp_info(n, rae, len, off, req);
    case NVME_LOG_FDP_CONFS:
        return nvme_fdp_confs(n, lspi, len, off, req);
    case NVME_LOG_FDP_RUH_USAGE:
        return nvme_fdp_ruh_usage(n, lspi, dw10, dw12, len, off, req);
    case NVME_LOG_FDP_STATS:
        return nvme_fdp_stats(n, lspi, len, off, req);
    case NVME_LOG_FDP_EVENTS:
        return nvme_fdp_events(n, lspi, len, off, req);
    default:
        trace_pci_nvme_err_invalid_log_page(nvme_cid(req), lid);
        return NVME_INVALID_FIELD | NVME_DNR;
    }
}

static void nvme_free_cq(NvmeCQueue *cq, NvmeCtrl *n)
{
    PCIDevice *pci = PCI_DEVICE(n);
    uint16_t offset = (cq->cqid << 3) + (1 << 2);

    n->cq[cq->cqid] = NULL;
    qemu_bh_delete(cq->bh);
    if (cq->ioeventfd_enabled) {
        memory_region_del_eventfd(&n->iomem,
                                  0x1000 + offset, 4, false, 0, &cq->notifier);
        event_notifier_set_handler(&cq->notifier, NULL);
        event_notifier_cleanup(&cq->notifier);
    }
    if (msix_enabled(pci) && cq->irq_enabled) {
        msix_vector_unuse(pci, cq->vector);
    }
    if (cq->cqid) {
        g_free(cq);
    }
}

static uint16_t nvme_del_cq(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeDeleteQ *c = (NvmeDeleteQ *)&req->cmd;
    NvmeCQueue *cq;
    uint16_t qid = le16_to_cpu(c->qid);

    if (unlikely(!qid || nvme_check_cqid(n, qid))) {
        trace_pci_nvme_err_invalid_del_cq_cqid(qid);
        return NVME_INVALID_CQID | NVME_DNR;
    }

    cq = n->cq[qid];
    if (unlikely(!QTAILQ_EMPTY(&cq->sq_list))) {
        trace_pci_nvme_err_invalid_del_cq_notempty(qid);
        return NVME_INVALID_QUEUE_DEL;
    }

    if (cq->irq_enabled && cq->tail != cq->head) {
        n->cq_pending--;
    }

    nvme_irq_deassert(n, cq);
    trace_pci_nvme_del_cq(qid);
    nvme_free_cq(cq, n);
    return NVME_SUCCESS;
}

static void nvme_init_cq(NvmeCQueue *cq, NvmeCtrl *n, uint64_t dma_addr,
                         uint16_t cqid, uint16_t vector, uint16_t size,
                         uint16_t irq_enabled)
{
    PCIDevice *pci = PCI_DEVICE(n);

    if (msix_enabled(pci) && irq_enabled) {
        msix_vector_use(pci, vector);
    }

    cq->ctrl = n;
    cq->cqid = cqid;
    cq->size = size;
    cq->dma_addr = dma_addr;
    cq->phase = 1;
    cq->irq_enabled = irq_enabled;
    cq->vector = vector;
    cq->head = cq->tail = 0;
    QTAILQ_INIT(&cq->req_list);
    QTAILQ_INIT(&cq->sq_list);
    if (n->dbbuf_enabled) {
        cq->db_addr = n->dbbuf_dbs + (cqid << 3) + (1 << 2);
        cq->ei_addr = n->dbbuf_eis + (cqid << 3) + (1 << 2);

        if (n->params.ioeventfd && cqid != 0) {
            if (!nvme_init_cq_ioeventfd(cq)) {
                cq->ioeventfd_enabled = true;
            }
        }
    }
    n->cq[cqid] = cq;
    cq->bh = qemu_bh_new_guarded(nvme_post_cqes, cq,
                                 &DEVICE(cq->ctrl)->mem_reentrancy_guard);
}

static uint16_t nvme_create_cq(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCQueue *cq;
    NvmeCreateCq *c = (NvmeCreateCq *)&req->cmd;
    uint16_t cqid = le16_to_cpu(c->cqid);
    uint16_t vector = le16_to_cpu(c->irq_vector);
    uint16_t qsize = le16_to_cpu(c->qsize);
    uint16_t qflags = le16_to_cpu(c->cq_flags);
    uint64_t prp1 = le64_to_cpu(c->prp1);
    uint32_t cc = ldq_le_p(&n->bar.cc);
    uint8_t iocqes = NVME_CC_IOCQES(cc);
    uint8_t iosqes = NVME_CC_IOSQES(cc);

    trace_pci_nvme_create_cq(prp1, cqid, vector, qsize, qflags,
                             NVME_CQ_FLAGS_IEN(qflags) != 0);

    if (iosqes != NVME_SQES || iocqes != NVME_CQES) {
        trace_pci_nvme_err_invalid_create_cq_entry_size(iosqes, iocqes);
        return NVME_MAX_QSIZE_EXCEEDED | NVME_DNR;
    }

    if (unlikely(!cqid || cqid > n->conf_ioqpairs || n->cq[cqid] != NULL)) {
        trace_pci_nvme_err_invalid_create_cq_cqid(cqid);
        return NVME_INVALID_QID | NVME_DNR;
    }
    if (unlikely(!qsize || qsize > NVME_CAP_MQES(ldq_le_p(&n->bar.cap)))) {
        trace_pci_nvme_err_invalid_create_cq_size(qsize);
        return NVME_MAX_QSIZE_EXCEEDED | NVME_DNR;
    }
    if (unlikely(prp1 & (n->page_size - 1))) {
        trace_pci_nvme_err_invalid_create_cq_addr(prp1);
        return NVME_INVALID_PRP_OFFSET | NVME_DNR;
    }
    if (unlikely(!msix_enabled(PCI_DEVICE(n)) && vector)) {
        trace_pci_nvme_err_invalid_create_cq_vector(vector);
        return NVME_INVALID_IRQ_VECTOR | NVME_DNR;
    }
    if (unlikely(vector >= n->conf_msix_qsize)) {
        trace_pci_nvme_err_invalid_create_cq_vector(vector);
        return NVME_INVALID_IRQ_VECTOR | NVME_DNR;
    }
    if (unlikely(!(NVME_CQ_FLAGS_PC(qflags)))) {
        trace_pci_nvme_err_invalid_create_cq_qflags(NVME_CQ_FLAGS_PC(qflags));
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    cq = g_malloc0(sizeof(*cq));
    nvme_init_cq(cq, n, prp1, cqid, vector, qsize + 1,
                 NVME_CQ_FLAGS_IEN(qflags));

    /*
     * It is only required to set qs_created when creating a completion queue;
     * creating a submission queue without a matching completion queue will
     * fail.
     */
    n->qs_created = true;
    return NVME_SUCCESS;
}

static uint16_t nvme_rpt_empty_id_struct(NvmeCtrl *n, NvmeRequest *req)
{
    uint8_t id[NVME_IDENTIFY_DATA_SIZE] = {};

    return nvme_c2h(n, id, sizeof(id), req);
}

static uint16_t nvme_identify_ctrl(NvmeCtrl *n, NvmeRequest *req)
{
    trace_pci_nvme_identify_ctrl();

    return nvme_c2h(n, (uint8_t *)&n->id_ctrl, sizeof(n->id_ctrl), req);
}

static uint16_t nvme_identify_ctrl_csi(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint8_t id[NVME_IDENTIFY_DATA_SIZE] = {};
    NvmeIdCtrlNvm *id_nvm = (NvmeIdCtrlNvm *)&id;

    trace_pci_nvme_identify_ctrl_csi(c->csi);

    switch (c->csi) {
    case NVME_CSI_NVM:
        id_nvm->vsl = n->params.vsl;
        id_nvm->dmrl = NVME_ID_CTRL_NVM_DMRL_MAX;
        id_nvm->dmrsl = cpu_to_le32(n->dmrsl);
        id_nvm->dmsl = NVME_ID_CTRL_NVM_DMRL_MAX * n->dmrsl;
        break;

    case NVME_CSI_ZONED:
        ((NvmeIdCtrlZoned *)&id)->zasl = n->params.zasl;
        break;

    default:
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return nvme_c2h(n, id, sizeof(id), req);
}

static uint16_t nvme_identify_ns(NvmeCtrl *n, NvmeRequest *req, bool active)
{
    NvmeNamespace *ns;
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint32_t nsid = le32_to_cpu(c->nsid);

    trace_pci_nvme_identify_ns(nsid);

    if (!nvme_nsid_valid(n, nsid) || nsid == NVME_NSID_BROADCAST) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    ns = nvme_ns(n, nsid);
    if (unlikely(!ns)) {
        if (!active) {
            ns = nvme_subsys_ns(n->subsys, nsid);
            if (!ns) {
                return nvme_rpt_empty_id_struct(n, req);
            }
        } else {
            return nvme_rpt_empty_id_struct(n, req);
        }
    }

    if (active || ns->csi == NVME_CSI_NVM) {
        return nvme_c2h(n, (uint8_t *)&ns->id_ns, sizeof(NvmeIdNs), req);
    }

    return NVME_INVALID_IOCS | NVME_DNR;
}

static uint16_t nvme_identify_ctrl_list(NvmeCtrl *n, NvmeRequest *req,
                                        bool attached)
{
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint32_t nsid = le32_to_cpu(c->nsid);
    uint16_t min_id = le16_to_cpu(c->ctrlid);
    uint16_t list[NVME_CONTROLLER_LIST_SIZE] = {};
    uint16_t *ids = &list[1];
    NvmeNamespace *ns;
    NvmeCtrl *ctrl;
    int cntlid, nr_ids = 0;

    trace_pci_nvme_identify_ctrl_list(c->cns, min_id);

    if (!n->subsys) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (attached) {
        if (nsid == NVME_NSID_BROADCAST) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        ns = nvme_subsys_ns(n->subsys, nsid);
        if (!ns) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }
    }

    for (cntlid = min_id; cntlid < ARRAY_SIZE(n->subsys->ctrls); cntlid++) {
        ctrl = nvme_subsys_ctrl(n->subsys, cntlid);
        if (!ctrl) {
            continue;
        }

        if (attached && !nvme_ns(ctrl, nsid)) {
            continue;
        }

        ids[nr_ids++] = cntlid;
    }

    list[0] = nr_ids;

    return nvme_c2h(n, (uint8_t *)list, sizeof(list), req);
}

static uint16_t nvme_identify_pri_ctrl_cap(NvmeCtrl *n, NvmeRequest *req)
{
    trace_pci_nvme_identify_pri_ctrl_cap(le16_to_cpu(n->pri_ctrl_cap.cntlid));

    return nvme_c2h(n, (uint8_t *)&n->pri_ctrl_cap,
                    sizeof(NvmePriCtrlCap), req);
}

static uint16_t nvme_identify_sec_ctrl_list(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint16_t pri_ctrl_id = le16_to_cpu(n->pri_ctrl_cap.cntlid);
    uint16_t min_id = le16_to_cpu(c->ctrlid);
    uint8_t num_sec_ctrl = n->nr_sec_ctrls;
    NvmeSecCtrlList list = {0};
    uint8_t i;

    for (i = 0; i < num_sec_ctrl; i++) {
        if (n->sec_ctrl_list[i].scid >= min_id) {
            list.numcntl = MIN(num_sec_ctrl - i, 127);
            memcpy(&list.sec, n->sec_ctrl_list + i,
                   list.numcntl * sizeof(NvmeSecCtrlEntry));
            break;
        }
    }

    trace_pci_nvme_identify_sec_ctrl_list(pri_ctrl_id, list.numcntl);

    return nvme_c2h(n, (uint8_t *)&list, sizeof(list), req);
}

static uint16_t nvme_identify_ns_ind(NvmeCtrl *n, NvmeRequest *req, bool alloc)
{
    NvmeNamespace *ns;
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint32_t nsid = le32_to_cpu(c->nsid);

    trace_pci_nvme_identify_ns_ind(nsid);

    if (!nvme_nsid_valid(n, nsid) || nsid == NVME_NSID_BROADCAST) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    ns = nvme_ns(n, nsid);
    if (unlikely(!ns)) {
        if (alloc) {
            ns = nvme_subsys_ns(n->subsys, nsid);
            if (!ns) {
                return nvme_rpt_empty_id_struct(n, req);
            }
        } else {
            return nvme_rpt_empty_id_struct(n, req);
        }
    }

    return nvme_c2h(n, (uint8_t *)&ns->id_ns_ind, sizeof(NvmeIdNsInd), req);
}

static uint16_t nvme_identify_ns_csi(NvmeCtrl *n, NvmeRequest *req,
                                     bool active)
{
    NvmeNamespace *ns;
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint32_t nsid = le32_to_cpu(c->nsid);

    trace_pci_nvme_identify_ns_csi(nsid, c->csi);

    if (!nvme_nsid_valid(n, nsid) || nsid == NVME_NSID_BROADCAST) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    ns = nvme_ns(n, nsid);
    if (unlikely(!ns)) {
        if (!active) {
            ns = nvme_subsys_ns(n->subsys, nsid);
            if (!ns) {
                return nvme_rpt_empty_id_struct(n, req);
            }
        } else {
            return nvme_rpt_empty_id_struct(n, req);
        }
    }

    if (c->csi == NVME_CSI_NVM) {
        return nvme_c2h(n, (uint8_t *)&ns->id_ns_nvm, sizeof(NvmeIdNsNvm),
                        req);
    } else if (c->csi == NVME_CSI_ZONED && ns->csi == NVME_CSI_ZONED) {
        return nvme_c2h(n, (uint8_t *)ns->id_ns_zoned, sizeof(NvmeIdNsZoned),
                        req);
    }

    return NVME_INVALID_FIELD | NVME_DNR;
}

static uint16_t nvme_identify_nslist(NvmeCtrl *n, NvmeRequest *req,
                                     bool active)
{
    NvmeNamespace *ns;
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint32_t min_nsid = le32_to_cpu(c->nsid);
    uint8_t list[NVME_IDENTIFY_DATA_SIZE] = {};
    static const int data_len = sizeof(list);
    uint32_t *list_ptr = (uint32_t *)list;
    int i, j = 0;

    trace_pci_nvme_identify_nslist(min_nsid);

    /*
     * Both FFFFFFFFh (NVME_NSID_BROADCAST) and FFFFFFFFEh are invalid values
     * since the Active Namespace ID List should return namespaces with ids
     * *higher* than the NSID specified in the command. This is also specified
     * in the spec (NVM Express v1.3d, Section 5.15.4).
     */
    if (min_nsid >= NVME_NSID_BROADCAST - 1) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            if (!active) {
                ns = nvme_subsys_ns(n->subsys, i);
                if (!ns) {
                    continue;
                }
            } else {
                continue;
            }
        }
        if (ns->params.nsid <= min_nsid) {
            continue;
        }
        list_ptr[j++] = cpu_to_le32(ns->params.nsid);
        if (j == data_len / sizeof(uint32_t)) {
            break;
        }
    }

    return nvme_c2h(n, list, data_len, req);
}

static uint16_t nvme_identify_nslist_csi(NvmeCtrl *n, NvmeRequest *req,
                                         bool active)
{
    NvmeNamespace *ns;
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint32_t min_nsid = le32_to_cpu(c->nsid);
    uint8_t list[NVME_IDENTIFY_DATA_SIZE] = {};
    static const int data_len = sizeof(list);
    uint32_t *list_ptr = (uint32_t *)list;
    int i, j = 0;

    trace_pci_nvme_identify_nslist_csi(min_nsid, c->csi);

    /*
     * Same as in nvme_identify_nslist(), FFFFFFFFh/FFFFFFFFEh are invalid.
     */
    if (min_nsid >= NVME_NSID_BROADCAST - 1) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    if (c->csi != NVME_CSI_NVM && c->csi != NVME_CSI_ZONED) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            if (!active) {
                ns = nvme_subsys_ns(n->subsys, i);
                if (!ns) {
                    continue;
                }
            } else {
                continue;
            }
        }
        if (ns->params.nsid <= min_nsid || c->csi != ns->csi) {
            continue;
        }
        list_ptr[j++] = cpu_to_le32(ns->params.nsid);
        if (j == data_len / sizeof(uint32_t)) {
            break;
        }
    }

    return nvme_c2h(n, list, data_len, req);
}

static uint16_t nvme_endurance_group_list(NvmeCtrl *n, NvmeRequest *req)
{
    uint16_t list[NVME_CONTROLLER_LIST_SIZE] = {};
    uint16_t *nr_ids = &list[0];
    uint16_t *ids = &list[1];
    uint16_t endgid = le32_to_cpu(req->cmd.cdw11) & 0xffff;

    /*
     * The current nvme-subsys only supports Endurance Group #1.
     */
    if (!endgid) {
        *nr_ids = 1;
        ids[0] = 1;
    } else {
        *nr_ids = 0;
    }

    return nvme_c2h(n, list, sizeof(list), req);
}

static uint16_t nvme_identify_ns_descr_list(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns;
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint32_t nsid = le32_to_cpu(c->nsid);
    uint8_t list[NVME_IDENTIFY_DATA_SIZE] = {};
    uint8_t *pos = list;
    struct {
        NvmeIdNsDescr hdr;
        uint8_t v[NVME_NIDL_UUID];
    } QEMU_PACKED uuid = {};
    struct {
        NvmeIdNsDescr hdr;
        uint8_t v[NVME_NIDL_NGUID];
    } QEMU_PACKED nguid = {};
    struct {
        NvmeIdNsDescr hdr;
        uint64_t v;
    } QEMU_PACKED eui64 = {};
    struct {
        NvmeIdNsDescr hdr;
        uint8_t v;
    } QEMU_PACKED csi = {};

    trace_pci_nvme_identify_ns_descr_list(nsid);

    if (!nvme_nsid_valid(n, nsid) || nsid == NVME_NSID_BROADCAST) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    ns = nvme_ns(n, nsid);
    if (unlikely(!ns)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (!qemu_uuid_is_null(&ns->params.uuid)) {
        uuid.hdr.nidt = NVME_NIDT_UUID;
        uuid.hdr.nidl = NVME_NIDL_UUID;
        memcpy(uuid.v, ns->params.uuid.data, NVME_NIDL_UUID);
        memcpy(pos, &uuid, sizeof(uuid));
        pos += sizeof(uuid);
    }

    if (!nvme_nguid_is_null(&ns->params.nguid)) {
        nguid.hdr.nidt = NVME_NIDT_NGUID;
        nguid.hdr.nidl = NVME_NIDL_NGUID;
        memcpy(nguid.v, ns->params.nguid.data, NVME_NIDL_NGUID);
        memcpy(pos, &nguid, sizeof(nguid));
        pos += sizeof(nguid);
    }

    if (ns->params.eui64) {
        eui64.hdr.nidt = NVME_NIDT_EUI64;
        eui64.hdr.nidl = NVME_NIDL_EUI64;
        eui64.v = cpu_to_be64(ns->params.eui64);
        memcpy(pos, &eui64, sizeof(eui64));
        pos += sizeof(eui64);
    }

    csi.hdr.nidt = NVME_NIDT_CSI;
    csi.hdr.nidl = NVME_NIDL_CSI;
    csi.v = ns->csi;
    memcpy(pos, &csi, sizeof(csi));
    pos += sizeof(csi);

    return nvme_c2h(n, list, sizeof(list), req);
}

static uint16_t nvme_identify_cmd_set(NvmeCtrl *n, NvmeRequest *req)
{
    uint8_t list[NVME_IDENTIFY_DATA_SIZE] = {};
    static const int data_len = sizeof(list);

    trace_pci_nvme_identify_cmd_set();

    NVME_SET_CSI(*list, NVME_CSI_NVM);
    NVME_SET_CSI(*list, NVME_CSI_ZONED);

    return nvme_c2h(n, list, data_len, req);
}

static uint16_t nvme_identify(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;

    trace_pci_nvme_identify(nvme_cid(req), c->cns, le16_to_cpu(c->ctrlid),
                            c->csi);

    switch (c->cns) {
    case NVME_ID_CNS_NS:
        return nvme_identify_ns(n, req, true);
    case NVME_ID_CNS_NS_PRESENT:
        return nvme_identify_ns(n, req, false);
    case NVME_ID_CNS_NS_ATTACHED_CTRL_LIST:
        return nvme_identify_ctrl_list(n, req, true);
    case NVME_ID_CNS_CTRL_LIST:
        return nvme_identify_ctrl_list(n, req, false);
    case NVME_ID_CNS_PRIMARY_CTRL_CAP:
        return nvme_identify_pri_ctrl_cap(n, req);
    case NVME_ID_CNS_SECONDARY_CTRL_LIST:
        return nvme_identify_sec_ctrl_list(n, req);
    case NVME_ID_CNS_CS_NS:
        return nvme_identify_ns_csi(n, req, true);
    case NVME_ID_CNS_CS_IND_NS:
        return nvme_identify_ns_ind(n, req, false);
    case NVME_ID_CNS_CS_IND_NS_ALLOCATED:
        return nvme_identify_ns_ind(n, req, true);
    case NVME_ID_CNS_CS_NS_PRESENT:
        return nvme_identify_ns_csi(n, req, false);
    case NVME_ID_CNS_CTRL:
        return nvme_identify_ctrl(n, req);
    case NVME_ID_CNS_CS_CTRL:
        return nvme_identify_ctrl_csi(n, req);
    case NVME_ID_CNS_NS_ACTIVE_LIST:
        return nvme_identify_nslist(n, req, true);
    case NVME_ID_CNS_NS_PRESENT_LIST:
        return nvme_identify_nslist(n, req, false);
    case NVME_ID_CNS_CS_NS_ACTIVE_LIST:
        return nvme_identify_nslist_csi(n, req, true);
    case NVME_ID_CNS_ENDURANCE_GROUP_LIST:
        return nvme_endurance_group_list(n, req);
    case NVME_ID_CNS_CS_NS_PRESENT_LIST:
        return nvme_identify_nslist_csi(n, req, false);
    case NVME_ID_CNS_NS_DESCR_LIST:
        return nvme_identify_ns_descr_list(n, req);
    case NVME_ID_CNS_IO_COMMAND_SET:
        return nvme_identify_cmd_set(n, req);
    default:
        trace_pci_nvme_err_invalid_identify_cns(le32_to_cpu(c->cns));
        return NVME_INVALID_FIELD | NVME_DNR;
    }
}

static uint16_t nvme_abort(NvmeCtrl *n, NvmeRequest *req)
{
    uint16_t sqid = le32_to_cpu(req->cmd.cdw10) & 0xffff;
    uint16_t cid  = (le32_to_cpu(req->cmd.cdw10) >> 16) & 0xffff;
    NvmeSQueue *sq = n->sq[sqid];
    NvmeRequest *r, *next;
    int i;

    req->cqe.result = 1;
    if (nvme_check_sqid(n, sqid)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (sqid == 0) {
        for (i = 0; i < n->outstanding_aers; i++) {
            NvmeRequest *re = n->aer_reqs[i];
            if (re->cqe.cid == cid) {
                memmove(n->aer_reqs + i, n->aer_reqs + i + 1,
                         (n->outstanding_aers - i - 1) * sizeof(NvmeRequest *));
                n->outstanding_aers--;
                re->status = NVME_CMD_ABORT_REQ;
                req->cqe.result = 0;
                nvme_enqueue_req_completion(&n->admin_cq, re);
                return NVME_SUCCESS;
            }
        }
    }

    QTAILQ_FOREACH_SAFE(r, &sq->out_req_list, entry, next) {
        if (r->cqe.cid == cid) {
            if (r->aiocb) {
                r->status = NVME_CMD_ABORT_REQ;
                blk_aio_cancel_async(r->aiocb);
            }
            break;
        }
    }

    return NVME_SUCCESS;
}

static inline void nvme_set_timestamp(NvmeCtrl *n, uint64_t ts)
{
    trace_pci_nvme_setfeat_timestamp(ts);

    n->host_timestamp = le64_to_cpu(ts);
    n->timestamp_set_qemu_clock_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
}

static inline uint64_t nvme_get_timestamp(const NvmeCtrl *n)
{
    uint64_t current_time = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    uint64_t elapsed_time = current_time - n->timestamp_set_qemu_clock_ms;

    union nvme_timestamp {
        struct {
            uint64_t timestamp:48;
            uint64_t sync:1;
            uint64_t origin:3;
            uint64_t rsvd1:12;
        };
        uint64_t all;
    };

    union nvme_timestamp ts;
    ts.all = 0;
    ts.timestamp = n->host_timestamp + elapsed_time;

    /* If the host timestamp is non-zero, set the timestamp origin */
    ts.origin = n->host_timestamp ? 0x01 : 0x00;

    trace_pci_nvme_getfeat_timestamp(ts.all);

    return cpu_to_le64(ts.all);
}

static uint16_t nvme_get_feature_timestamp(NvmeCtrl *n, NvmeRequest *req)
{
    uint64_t timestamp = nvme_get_timestamp(n);

    return nvme_c2h(n, (uint8_t *)&timestamp, sizeof(timestamp), req);
}

static int nvme_get_feature_fdp(NvmeCtrl *n, uint32_t endgrpid,
                                uint32_t *result)
{
    *result = 0;

    if (!n->subsys || !n->subsys->endgrp.fdp.enabled) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    *result = FIELD_DP16(0, FEAT_FDP, FDPE, 1);
    *result = FIELD_DP16(*result, FEAT_FDP, CONF_NDX, 0);

    return NVME_SUCCESS;
}

static uint16_t nvme_get_feature_fdp_events(NvmeCtrl *n, NvmeNamespace *ns,
                                            NvmeRequest *req, uint32_t *result)
{
    NvmeCmd *cmd = &req->cmd;
    uint32_t cdw11 = le32_to_cpu(cmd->cdw11);
    uint16_t ph = cdw11 & 0xffff;
    uint8_t noet = (cdw11 >> 16) & 0xff;
    uint16_t ruhid, ret;
    uint32_t nentries = 0;
    uint8_t s_events_ndx = 0;
    size_t s_events_siz = sizeof(NvmeFdpEventDescr) * noet;
    g_autofree NvmeFdpEventDescr *s_events = g_malloc0(s_events_siz);
    NvmeRuHandle *ruh;
    NvmeFdpEventDescr *s_event;

    if (!n->subsys || !n->subsys->endgrp.fdp.enabled) {
        return NVME_FDP_DISABLED | NVME_DNR;
    }

    if (!nvme_ph_valid(ns, ph)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    ruhid = ns->fdp.phs[ph];
    ruh = &n->subsys->endgrp.fdp.ruhs[ruhid];

    assert(ruh);

    if (unlikely(noet == 0)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    for (uint8_t event_type = 0; event_type < FDP_EVT_MAX; event_type++) {
        uint8_t shift = nvme_fdp_evf_shifts[event_type];
        if (!shift && event_type) {
            /*
             * only first entry (event_type == 0) has a shift value of 0
             * other entries are simply unpopulated.
             */
            continue;
        }

        nentries++;

        s_event = &s_events[s_events_ndx];
        s_event->evt = event_type;
        s_event->evta = (ruh->event_filter >> shift) & 0x1;

        /* break if all `noet` entries are filled */
        if ((++s_events_ndx) == noet) {
            break;
        }
    }

    ret = nvme_c2h(n, s_events, s_events_siz, req);
    if (ret) {
        return ret;
    }

    *result = nentries;
    return NVME_SUCCESS;
}

static uint16_t nvme_get_feature(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = &req->cmd;
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    uint32_t nsid = le32_to_cpu(cmd->nsid);
    uint32_t result = 0;
    uint8_t fid = NVME_GETSETFEAT_FID(dw10);
    NvmeGetFeatureSelect sel = NVME_GETFEAT_SELECT(dw10);
    uint16_t iv;
    NvmeNamespace *ns;
    int i;
    uint16_t endgrpid = 0, ret = NVME_SUCCESS;

    static const uint32_t nvme_feature_default[NVME_FID_MAX] = {
        [NVME_ARBITRATION] = NVME_ARB_AB_NOLIMIT,
    };

    trace_pci_nvme_getfeat(nvme_cid(req), nsid, fid, sel, dw11);

    if (!nvme_feature_support[fid]) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (nvme_feature_cap[fid] & NVME_FEAT_CAP_NS) {
        if (!nvme_nsid_valid(n, nsid) || nsid == NVME_NSID_BROADCAST) {
            /*
             * The Reservation Notification Mask and Reservation Persistence
             * features require a status code of Invalid Field in Command when
             * NSID is FFFFFFFFh. Since the device does not support those
             * features we can always return Invalid Namespace or Format as we
             * should do for all other features.
             */
            return NVME_INVALID_NSID | NVME_DNR;
        }

        if (!nvme_ns(n, nsid)) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }
    }

    switch (sel) {
    case NVME_GETFEAT_SELECT_CURRENT:
        break;
    case NVME_GETFEAT_SELECT_SAVED:
        /* no features are saveable by the controller; fallthrough */
    case NVME_GETFEAT_SELECT_DEFAULT:
        goto defaults;
    case NVME_GETFEAT_SELECT_CAP:
        result = nvme_feature_cap[fid];
        goto out;
    }

    switch (fid) {
    case NVME_TEMPERATURE_THRESHOLD:
        result = 0;

        /*
         * The controller only implements the Composite Temperature sensor, so
         * return 0 for all other sensors.
         */
        if (NVME_TEMP_TMPSEL(dw11) != NVME_TEMP_TMPSEL_COMPOSITE) {
            goto out;
        }

        switch (NVME_TEMP_THSEL(dw11)) {
        case NVME_TEMP_THSEL_OVER:
            result = n->features.temp_thresh_hi;
            goto out;
        case NVME_TEMP_THSEL_UNDER:
            result = n->features.temp_thresh_low;
            goto out;
        }

        return NVME_INVALID_FIELD | NVME_DNR;
    case NVME_ERROR_RECOVERY:
        if (!nvme_nsid_valid(n, nsid)) {
            return NVME_INVALID_NSID | NVME_DNR;
        }

        ns = nvme_ns(n, nsid);
        if (unlikely(!ns)) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        result = ns->features.err_rec;
        goto out;
    case NVME_VOLATILE_WRITE_CACHE:
        result = 0;
        for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
            ns = nvme_ns(n, i);
            if (!ns) {
                continue;
            }

            result = blk_enable_write_cache(ns->blkconf.blk);
            if (result) {
                break;
            }
        }
        trace_pci_nvme_getfeat_vwcache(result ? "enabled" : "disabled");
        goto out;
    case NVME_ASYNCHRONOUS_EVENT_CONF:
        result = n->features.async_config;
        goto out;
    case NVME_TIMESTAMP:
        return nvme_get_feature_timestamp(n, req);
    case NVME_HOST_BEHAVIOR_SUPPORT:
        return nvme_c2h(n, (uint8_t *)&n->features.hbs,
                        sizeof(n->features.hbs), req);
    case NVME_FDP_MODE:
        endgrpid = dw11 & 0xff;

        if (endgrpid != 0x1) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        ret = nvme_get_feature_fdp(n, endgrpid, &result);
        if (ret) {
            return ret;
        }
        goto out;
    case NVME_FDP_EVENTS:
        if (!nvme_nsid_valid(n, nsid)) {
            return NVME_INVALID_NSID | NVME_DNR;
        }

        ns = nvme_ns(n, nsid);
        if (unlikely(!ns)) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        ret = nvme_get_feature_fdp_events(n, ns, req, &result);
        if (ret) {
            return ret;
        }
        goto out;
    default:
        break;
    }

defaults:
    switch (fid) {
    case NVME_TEMPERATURE_THRESHOLD:
        result = 0;

        if (NVME_TEMP_TMPSEL(dw11) != NVME_TEMP_TMPSEL_COMPOSITE) {
            break;
        }

        if (NVME_TEMP_THSEL(dw11) == NVME_TEMP_THSEL_OVER) {
            result = NVME_TEMPERATURE_WARNING;
        }

        break;
    case NVME_NUMBER_OF_QUEUES:
        result = (n->conf_ioqpairs - 1) | ((n->conf_ioqpairs - 1) << 16);
        trace_pci_nvme_getfeat_numq(result);
        break;
    case NVME_INTERRUPT_VECTOR_CONF:
        iv = dw11 & 0xffff;
        if (iv >= n->conf_ioqpairs + 1) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        result = iv;
        if (iv == n->admin_cq.vector) {
            result |= NVME_INTVC_NOCOALESCING;
        }
        break;
    case NVME_FDP_MODE:
        endgrpid = dw11 & 0xff;

        if (endgrpid != 0x1) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        ret = nvme_get_feature_fdp(n, endgrpid, &result);
        if (ret) {
            return ret;
        }
        break;

    case NVME_WRITE_ATOMICITY:
        result = n->dn;
        break;
    default:
        result = nvme_feature_default[fid];
        break;
    }

out:
    req->cqe.result = cpu_to_le32(result);
    return ret;
}

static uint16_t nvme_set_feature_timestamp(NvmeCtrl *n, NvmeRequest *req)
{
    uint16_t ret;
    uint64_t timestamp;

    ret = nvme_h2c(n, (uint8_t *)&timestamp, sizeof(timestamp), req);
    if (ret) {
        return ret;
    }

    nvme_set_timestamp(n, timestamp);

    return NVME_SUCCESS;
}

static uint16_t nvme_set_feature_fdp_events(NvmeCtrl *n, NvmeNamespace *ns,
                                            NvmeRequest *req)
{
    NvmeCmd *cmd = &req->cmd;
    uint32_t cdw11 = le32_to_cpu(cmd->cdw11);
    uint16_t ph = cdw11 & 0xffff;
    uint8_t noet = (cdw11 >> 16) & 0xff;
    uint16_t ret, ruhid;
    uint8_t enable = le32_to_cpu(cmd->cdw12) & 0x1;
    uint8_t event_mask = 0;
    unsigned int i;
    g_autofree uint8_t *events = g_malloc0(noet);
    NvmeRuHandle *ruh = NULL;

    assert(ns);

    if (!n->subsys || !n->subsys->endgrp.fdp.enabled) {
        return NVME_FDP_DISABLED | NVME_DNR;
    }

    if (!nvme_ph_valid(ns, ph)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    ruhid = ns->fdp.phs[ph];
    ruh = &n->subsys->endgrp.fdp.ruhs[ruhid];

    ret = nvme_h2c(n, events, noet, req);
    if (ret) {
        return ret;
    }

    for (i = 0; i < noet; i++) {
        event_mask |= (1 << nvme_fdp_evf_shifts[events[i]]);
    }

    if (enable) {
        ruh->event_filter |= event_mask;
    } else {
        ruh->event_filter = ruh->event_filter & ~event_mask;
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_set_feature(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns = NULL;

    NvmeCmd *cmd = &req->cmd;
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    uint32_t nsid = le32_to_cpu(cmd->nsid);
    uint8_t fid = NVME_GETSETFEAT_FID(dw10);
    uint8_t save = NVME_SETFEAT_SAVE(dw10);
    uint16_t status;
    int i;
    NvmeIdCtrl *id = &n->id_ctrl;
    NvmeAtomic *atomic = &n->atomic;

    trace_pci_nvme_setfeat(nvme_cid(req), nsid, fid, save, dw11);

    if (save && !(nvme_feature_cap[fid] & NVME_FEAT_CAP_SAVE)) {
        return NVME_FID_NOT_SAVEABLE | NVME_DNR;
    }

    if (!nvme_feature_support[fid]) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (nvme_feature_cap[fid] & NVME_FEAT_CAP_NS) {
        if (nsid != NVME_NSID_BROADCAST) {
            if (!nvme_nsid_valid(n, nsid)) {
                return NVME_INVALID_NSID | NVME_DNR;
            }

            ns = nvme_ns(n, nsid);
            if (unlikely(!ns)) {
                return NVME_INVALID_FIELD | NVME_DNR;
            }
        }
    } else if (nsid && nsid != NVME_NSID_BROADCAST) {
        if (!nvme_nsid_valid(n, nsid)) {
            return NVME_INVALID_NSID | NVME_DNR;
        }

        return NVME_FEAT_NOT_NS_SPEC | NVME_DNR;
    }

    if (!(nvme_feature_cap[fid] & NVME_FEAT_CAP_CHANGE)) {
        return NVME_FEAT_NOT_CHANGEABLE | NVME_DNR;
    }

    switch (fid) {
    case NVME_TEMPERATURE_THRESHOLD:
        if (NVME_TEMP_TMPSEL(dw11) != NVME_TEMP_TMPSEL_COMPOSITE) {
            break;
        }

        switch (NVME_TEMP_THSEL(dw11)) {
        case NVME_TEMP_THSEL_OVER:
            n->features.temp_thresh_hi = NVME_TEMP_TMPTH(dw11);
            break;
        case NVME_TEMP_THSEL_UNDER:
            n->features.temp_thresh_low = NVME_TEMP_TMPTH(dw11);
            break;
        default:
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        if ((n->temperature >= n->features.temp_thresh_hi) ||
            (n->temperature <= n->features.temp_thresh_low)) {
            nvme_smart_event(n, NVME_SMART_TEMPERATURE);
        }

        break;
    case NVME_ERROR_RECOVERY:
        if (nsid == NVME_NSID_BROADCAST) {
            for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
                ns = nvme_ns(n, i);

                if (!ns) {
                    continue;
                }

                if (NVME_ID_NS_NSFEAT_DULBE(ns->id_ns.nsfeat)) {
                    ns->features.err_rec = dw11;
                }
            }

            break;
        }

        assert(ns);
        if (NVME_ID_NS_NSFEAT_DULBE(ns->id_ns.nsfeat))  {
            ns->features.err_rec = dw11;
        }
        break;
    case NVME_VOLATILE_WRITE_CACHE:
        for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
            ns = nvme_ns(n, i);
            if (!ns) {
                continue;
            }

            if (!(dw11 & 0x1) && blk_enable_write_cache(ns->blkconf.blk)) {
                blk_flush(ns->blkconf.blk);
            }

            blk_set_enable_write_cache(ns->blkconf.blk, dw11 & 1);
        }

        break;

    case NVME_NUMBER_OF_QUEUES:
        if (n->qs_created) {
            return NVME_CMD_SEQ_ERROR | NVME_DNR;
        }

        /*
         * NVMe v1.3, Section 5.21.1.7: FFFFh is not an allowed value for NCQR
         * and NSQR.
         */
        if ((dw11 & 0xffff) == 0xffff || ((dw11 >> 16) & 0xffff) == 0xffff) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        trace_pci_nvme_setfeat_numq((dw11 & 0xffff) + 1,
                                    ((dw11 >> 16) & 0xffff) + 1,
                                    n->conf_ioqpairs,
                                    n->conf_ioqpairs);
        req->cqe.result = cpu_to_le32((n->conf_ioqpairs - 1) |
                                      ((n->conf_ioqpairs - 1) << 16));
        break;
    case NVME_ASYNCHRONOUS_EVENT_CONF:
        n->features.async_config = dw11;
        break;
    case NVME_TIMESTAMP:
        return nvme_set_feature_timestamp(n, req);
    case NVME_HOST_BEHAVIOR_SUPPORT:
        status = nvme_h2c(n, (uint8_t *)&n->features.hbs,
                          sizeof(n->features.hbs), req);
        if (status) {
            return status;
        }

        for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
            ns = nvme_ns(n, i);

            if (!ns) {
                continue;
            }

            ns->id_ns.nlbaf = ns->nlbaf - 1;
            if (!n->features.hbs.lbafee) {
                ns->id_ns.nlbaf = MIN(ns->id_ns.nlbaf, 15);
            }
        }

        return status;
    case NVME_COMMAND_SET_PROFILE:
        if (dw11 & 0x1ff) {
            trace_pci_nvme_err_invalid_iocsci(dw11 & 0x1ff);
            return NVME_IOCS_COMBINATION_REJECTED | NVME_DNR;
        }
        break;
    case NVME_FDP_MODE:
        /* spec: abort with cmd seq err if there's one or more NS' in endgrp */
        return NVME_CMD_SEQ_ERROR | NVME_DNR;
    case NVME_FDP_EVENTS:
        return nvme_set_feature_fdp_events(n, ns, req);
    case NVME_WRITE_ATOMICITY:

        n->dn = 0x1 & dw11;

        if (n->dn) {
            atomic->atomic_max_write_size = le16_to_cpu(id->awupf) + 1;
        } else {
            atomic->atomic_max_write_size = le16_to_cpu(id->awun) + 1;
        }

        if (atomic->atomic_max_write_size == 1) {
            atomic->atomic_writes = 0;
        } else {
            atomic->atomic_writes = 1;
        }
        break;
    default:
        return NVME_FEAT_NOT_CHANGEABLE | NVME_DNR;
    }
    return NVME_SUCCESS;
}

static uint16_t nvme_aer(NvmeCtrl *n, NvmeRequest *req)
{
    trace_pci_nvme_aer(nvme_cid(req));

    if (n->outstanding_aers > n->params.aerl) {
        trace_pci_nvme_aer_aerl_exceeded();
        return NVME_AER_LIMIT_EXCEEDED;
    }

    n->aer_reqs[n->outstanding_aers] = req;
    n->outstanding_aers++;

    if (!QTAILQ_EMPTY(&n->aer_queue)) {
        nvme_process_aers(n);
    }

    return NVME_NO_COMPLETE;
}

static void nvme_update_dsm_limits(NvmeCtrl *n, NvmeNamespace *ns)
{
    if (ns) {
        n->dmrsl =
            MIN_NON_ZERO(n->dmrsl, BDRV_REQUEST_MAX_BYTES / nvme_l2b(ns, 1));

        return;
    }

    for (uint32_t nsid = 1; nsid <= NVME_MAX_NAMESPACES; nsid++) {
        ns = nvme_ns(n, nsid);
        if (!ns) {
            continue;
        }

        n->dmrsl =
            MIN_NON_ZERO(n->dmrsl, BDRV_REQUEST_MAX_BYTES / nvme_l2b(ns, 1));
    }
}

static bool nvme_csi_supported(NvmeCtrl *n, uint8_t csi)
{
    uint32_t cc;

    switch (csi) {
    case NVME_CSI_NVM:
        return true;

    case NVME_CSI_ZONED:
        cc = ldl_le_p(&n->bar.cc);

        return NVME_CC_CSS(cc) == NVME_CC_CSS_ALL;
    }

    g_assert_not_reached();
}

static void nvme_detach_ns(NvmeCtrl *n, NvmeNamespace *ns)
{
    assert(ns->attached > 0);

    n->namespaces[ns->params.nsid] = NULL;
    ns->attached--;
}

static uint16_t nvme_ns_attachment(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns;
    NvmeCtrl *ctrl;
    uint16_t list[NVME_CONTROLLER_LIST_SIZE] = {};
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);
    uint32_t dw10 = le32_to_cpu(req->cmd.cdw10);
    uint8_t sel = dw10 & 0xf;
    uint16_t *nr_ids = &list[0];
    uint16_t *ids = &list[1];
    uint16_t ret;
    int i;

    trace_pci_nvme_ns_attachment(nvme_cid(req), dw10 & 0xf);

    if (!nvme_nsid_valid(n, nsid)) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    ns = nvme_subsys_ns(n->subsys, nsid);
    if (!ns) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    ret = nvme_h2c(n, (uint8_t *)list, 4096, req);
    if (ret) {
        return ret;
    }

    if (!*nr_ids) {
        return NVME_NS_CTRL_LIST_INVALID | NVME_DNR;
    }

    *nr_ids = MIN(*nr_ids, NVME_CONTROLLER_LIST_SIZE - 1);
    for (i = 0; i < *nr_ids; i++) {
        ctrl = nvme_subsys_ctrl(n->subsys, ids[i]);
        if (!ctrl) {
            return NVME_NS_CTRL_LIST_INVALID | NVME_DNR;
        }

        switch (sel) {
        case NVME_NS_ATTACHMENT_ATTACH:
            if (nvme_ns(n, nsid)) {
                return NVME_NS_ALREADY_ATTACHED | NVME_DNR;
            }

            if (ns->attached && !ns->params.shared) {
                return NVME_NS_PRIVATE | NVME_DNR;
            }

            if (!nvme_csi_supported(n, ns->csi)) {
                return NVME_IOCS_NOT_SUPPORTED | NVME_DNR;
            }

            nvme_attach_ns(ctrl, ns);
            nvme_update_dsm_limits(ctrl, ns);

            break;

        case NVME_NS_ATTACHMENT_DETACH:
            nvme_detach_ns(ctrl, ns);
            nvme_update_dsm_limits(ctrl, NULL);

            break;

        default:
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        /*
         * Add namespace id to the changed namespace id list for event clearing
         * via Get Log Page command.
         */
        if (!test_and_set_bit(nsid, ctrl->changed_nsids)) {
            nvme_enqueue_event(ctrl, NVME_AER_TYPE_NOTICE,
                               NVME_AER_INFO_NOTICE_NS_ATTR_CHANGED,
                               NVME_LOG_CHANGED_NSLIST);
        }
    }

    return NVME_SUCCESS;
}

typedef struct NvmeFormatAIOCB {
    BlockAIOCB common;
    BlockAIOCB *aiocb;
    NvmeRequest *req;
    int ret;

    NvmeNamespace *ns;
    uint32_t nsid;
    bool broadcast;
    int64_t offset;

    uint8_t lbaf;
    uint8_t mset;
    uint8_t pi;
    uint8_t pil;
} NvmeFormatAIOCB;

static void nvme_format_cancel(BlockAIOCB *aiocb)
{
    NvmeFormatAIOCB *iocb = container_of(aiocb, NvmeFormatAIOCB, common);

    iocb->ret = -ECANCELED;

    if (iocb->aiocb) {
        blk_aio_cancel_async(iocb->aiocb);
        iocb->aiocb = NULL;
    }
}

static const AIOCBInfo nvme_format_aiocb_info = {
    .aiocb_size = sizeof(NvmeFormatAIOCB),
    .cancel_async = nvme_format_cancel,
};

static void nvme_format_set(NvmeNamespace *ns, uint8_t lbaf, uint8_t mset,
                            uint8_t pi, uint8_t pil)
{
    uint8_t lbafl = lbaf & 0xf;
    uint8_t lbafu = lbaf >> 4;

    trace_pci_nvme_format_set(ns->params.nsid, lbaf, mset, pi, pil);

    ns->id_ns.dps = (pil << 3) | pi;
    ns->id_ns.flbas = (lbafu << 5) | (mset << 4) | lbafl;

    nvme_ns_init_format(ns);
}

static void nvme_do_format(NvmeFormatAIOCB *iocb);

static void nvme_format_ns_cb(void *opaque, int ret)
{
    NvmeFormatAIOCB *iocb = opaque;
    NvmeNamespace *ns = iocb->ns;
    int bytes;

    if (iocb->ret < 0) {
        goto done;
    } else if (ret < 0) {
        iocb->ret = ret;
        goto done;
    }

    assert(ns);

    if (iocb->offset < ns->size) {
        bytes = MIN(BDRV_REQUEST_MAX_BYTES, ns->size - iocb->offset);

        iocb->aiocb = blk_aio_pwrite_zeroes(ns->blkconf.blk, iocb->offset,
                                            bytes, BDRV_REQ_MAY_UNMAP,
                                            nvme_format_ns_cb, iocb);

        iocb->offset += bytes;
        return;
    }

    nvme_format_set(ns, iocb->lbaf, iocb->mset, iocb->pi, iocb->pil);
    ns->status = 0x0;
    iocb->ns = NULL;
    iocb->offset = 0;

done:
    nvme_do_format(iocb);
}

static uint16_t nvme_format_check(NvmeNamespace *ns, uint8_t lbaf, uint8_t pi)
{
    if (ns->params.zoned) {
        return NVME_INVALID_FORMAT | NVME_DNR;
    }

    if (lbaf > ns->id_ns.nlbaf) {
        return NVME_INVALID_FORMAT | NVME_DNR;
    }

    if (pi && (ns->id_ns.lbaf[lbaf].ms < nvme_pi_tuple_size(ns))) {
        return NVME_INVALID_FORMAT | NVME_DNR;
    }

    if (pi && pi > NVME_ID_NS_DPS_TYPE_3) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static void nvme_do_format(NvmeFormatAIOCB *iocb)
{
    NvmeRequest *req = iocb->req;
    NvmeCtrl *n = nvme_ctrl(req);
    uint32_t dw10 = le32_to_cpu(req->cmd.cdw10);
    uint8_t lbaf = dw10 & 0xf;
    uint8_t pi = (dw10 >> 5) & 0x7;
    uint16_t status;
    int i;

    if (iocb->ret < 0) {
        goto done;
    }

    if (iocb->broadcast) {
        for (i = iocb->nsid + 1; i <= NVME_MAX_NAMESPACES; i++) {
            iocb->ns = nvme_ns(n, i);
            if (iocb->ns) {
                iocb->nsid = i;
                break;
            }
        }
    }

    if (!iocb->ns) {
        goto done;
    }

    status = nvme_format_check(iocb->ns, lbaf, pi);
    if (status) {
        req->status = status;
        goto done;
    }

    iocb->ns->status = NVME_FORMAT_IN_PROGRESS;
    nvme_format_ns_cb(iocb, 0);
    return;

done:
    iocb->common.cb(iocb->common.opaque, iocb->ret);
    qemu_aio_unref(iocb);
}

static uint16_t nvme_format(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeFormatAIOCB *iocb;
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);
    uint32_t dw10 = le32_to_cpu(req->cmd.cdw10);
    uint8_t lbaf = dw10 & 0xf;
    uint8_t mset = (dw10 >> 4) & 0x1;
    uint8_t pi = (dw10 >> 5) & 0x7;
    uint8_t pil = (dw10 >> 8) & 0x1;
    uint8_t lbafu = (dw10 >> 12) & 0x3;
    uint16_t status;

    iocb = qemu_aio_get(&nvme_format_aiocb_info, NULL, nvme_misc_cb, req);

    iocb->req = req;
    iocb->ret = 0;
    iocb->ns = NULL;
    iocb->nsid = 0;
    iocb->lbaf = lbaf;
    iocb->mset = mset;
    iocb->pi = pi;
    iocb->pil = pil;
    iocb->broadcast = (nsid == NVME_NSID_BROADCAST);
    iocb->offset = 0;

    if (n->features.hbs.lbafee) {
        iocb->lbaf |= lbafu << 4;
    }

    if (!iocb->broadcast) {
        if (!nvme_nsid_valid(n, nsid)) {
            status = NVME_INVALID_NSID | NVME_DNR;
            goto out;
        }

        iocb->ns = nvme_ns(n, nsid);
        if (!iocb->ns) {
            status = NVME_INVALID_FIELD | NVME_DNR;
            goto out;
        }
    }

    req->aiocb = &iocb->common;
    nvme_do_format(iocb);

    return NVME_NO_COMPLETE;

out:
    qemu_aio_unref(iocb);

    return status;
}

static void nvme_get_virt_res_num(NvmeCtrl *n, uint8_t rt, int *num_total,
                                  int *num_prim, int *num_sec)
{
    *num_total = le32_to_cpu(rt ?
                             n->pri_ctrl_cap.vifrt : n->pri_ctrl_cap.vqfrt);
    *num_prim = le16_to_cpu(rt ?
                            n->pri_ctrl_cap.virfap : n->pri_ctrl_cap.vqrfap);
    *num_sec = le16_to_cpu(rt ? n->pri_ctrl_cap.virfa : n->pri_ctrl_cap.vqrfa);
}

static uint16_t nvme_assign_virt_res_to_prim(NvmeCtrl *n, NvmeRequest *req,
                                             uint16_t cntlid, uint8_t rt,
                                             int nr)
{
    int num_total, num_prim, num_sec;

    if (cntlid != n->cntlid) {
        return NVME_INVALID_CTRL_ID | NVME_DNR;
    }

    nvme_get_virt_res_num(n, rt, &num_total, &num_prim, &num_sec);

    if (nr > num_total) {
        return NVME_INVALID_NUM_RESOURCES | NVME_DNR;
    }

    if (nr > num_total - num_sec) {
        return NVME_INVALID_RESOURCE_ID | NVME_DNR;
    }

    if (rt) {
        n->next_pri_ctrl_cap.virfap = cpu_to_le16(nr);
    } else {
        n->next_pri_ctrl_cap.vqrfap = cpu_to_le16(nr);
    }

    req->cqe.result = cpu_to_le32(nr);
    return req->status;
}

static void nvme_update_virt_res(NvmeCtrl *n, NvmeSecCtrlEntry *sctrl,
                                 uint8_t rt, int nr)
{
    int prev_nr, prev_total;

    if (rt) {
        prev_nr = le16_to_cpu(sctrl->nvi);
        prev_total = le32_to_cpu(n->pri_ctrl_cap.virfa);
        sctrl->nvi = cpu_to_le16(nr);
        n->pri_ctrl_cap.virfa = cpu_to_le32(prev_total + nr - prev_nr);
    } else {
        prev_nr = le16_to_cpu(sctrl->nvq);
        prev_total = le32_to_cpu(n->pri_ctrl_cap.vqrfa);
        sctrl->nvq = cpu_to_le16(nr);
        n->pri_ctrl_cap.vqrfa = cpu_to_le32(prev_total + nr - prev_nr);
    }
}

static uint16_t nvme_assign_virt_res_to_sec(NvmeCtrl *n, NvmeRequest *req,
                                            uint16_t cntlid, uint8_t rt, int nr)
{
    int num_total, num_prim, num_sec, num_free, diff, limit;
    NvmeSecCtrlEntry *sctrl;

    sctrl = nvme_sctrl_for_cntlid(n, cntlid);
    if (!sctrl) {
        return NVME_INVALID_CTRL_ID | NVME_DNR;
    }

    if (sctrl->scs) {
        return NVME_INVALID_SEC_CTRL_STATE | NVME_DNR;
    }

    limit = le16_to_cpu(rt ? n->pri_ctrl_cap.vifrsm : n->pri_ctrl_cap.vqfrsm);
    if (nr > limit) {
        return NVME_INVALID_NUM_RESOURCES | NVME_DNR;
    }

    nvme_get_virt_res_num(n, rt, &num_total, &num_prim, &num_sec);
    num_free = num_total - num_prim - num_sec;
    diff = nr - le16_to_cpu(rt ? sctrl->nvi : sctrl->nvq);

    if (diff > num_free) {
        return NVME_INVALID_RESOURCE_ID | NVME_DNR;
    }

    nvme_update_virt_res(n, sctrl, rt, nr);
    req->cqe.result = cpu_to_le32(nr);

    return req->status;
}

static uint16_t nvme_virt_set_state(NvmeCtrl *n, uint16_t cntlid, bool online)
{
    PCIDevice *pci = PCI_DEVICE(n);
    NvmeCtrl *sn = NULL;
    NvmeSecCtrlEntry *sctrl;
    int vf_index;

    sctrl = nvme_sctrl_for_cntlid(n, cntlid);
    if (!sctrl) {
        return NVME_INVALID_CTRL_ID | NVME_DNR;
    }

    if (!pci_is_vf(pci)) {
        vf_index = le16_to_cpu(sctrl->vfn) - 1;
        sn = NVME(pcie_sriov_get_vf_at_index(pci, vf_index));
    }

    if (online) {
        if (!sctrl->nvi || (le16_to_cpu(sctrl->nvq) < 2) || !sn) {
            return NVME_INVALID_SEC_CTRL_STATE | NVME_DNR;
        }

        if (!sctrl->scs) {
            sctrl->scs = 0x1;
            nvme_ctrl_reset(sn, NVME_RESET_FUNCTION);
        }
    } else {
        nvme_update_virt_res(n, sctrl, NVME_VIRT_RES_INTERRUPT, 0);
        nvme_update_virt_res(n, sctrl, NVME_VIRT_RES_QUEUE, 0);

        if (sctrl->scs) {
            sctrl->scs = 0x0;
            if (sn) {
                nvme_ctrl_reset(sn, NVME_RESET_FUNCTION);
            }
        }
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_virt_mngmt(NvmeCtrl *n, NvmeRequest *req)
{
    uint32_t dw10 = le32_to_cpu(req->cmd.cdw10);
    uint32_t dw11 = le32_to_cpu(req->cmd.cdw11);
    uint8_t act = dw10 & 0xf;
    uint8_t rt = (dw10 >> 8) & 0x7;
    uint16_t cntlid = (dw10 >> 16) & 0xffff;
    int nr = dw11 & 0xffff;

    trace_pci_nvme_virt_mngmt(nvme_cid(req), act, cntlid, rt ? "VI" : "VQ", nr);

    if (rt != NVME_VIRT_RES_QUEUE && rt != NVME_VIRT_RES_INTERRUPT) {
        return NVME_INVALID_RESOURCE_ID | NVME_DNR;
    }

    switch (act) {
    case NVME_VIRT_MNGMT_ACTION_SEC_ASSIGN:
        return nvme_assign_virt_res_to_sec(n, req, cntlid, rt, nr);
    case NVME_VIRT_MNGMT_ACTION_PRM_ALLOC:
        return nvme_assign_virt_res_to_prim(n, req, cntlid, rt, nr);
    case NVME_VIRT_MNGMT_ACTION_SEC_ONLINE:
        return nvme_virt_set_state(n, cntlid, true);
    case NVME_VIRT_MNGMT_ACTION_SEC_OFFLINE:
        return nvme_virt_set_state(n, cntlid, false);
    default:
        return NVME_INVALID_FIELD | NVME_DNR;
    }
}

static uint16_t nvme_dbbuf_config(NvmeCtrl *n, const NvmeRequest *req)
{
    PCIDevice *pci = PCI_DEVICE(n);
    uint64_t dbs_addr = le64_to_cpu(req->cmd.dptr.prp1);
    uint64_t eis_addr = le64_to_cpu(req->cmd.dptr.prp2);
    int i;

    /* Address should be page aligned */
    if (dbs_addr & (n->page_size - 1) || eis_addr & (n->page_size - 1)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    /* Save shadow buffer base addr for use during queue creation */
    n->dbbuf_dbs = dbs_addr;
    n->dbbuf_eis = eis_addr;
    n->dbbuf_enabled = true;

    for (i = 0; i < n->params.max_ioqpairs + 1; i++) {
        NvmeSQueue *sq = n->sq[i];
        NvmeCQueue *cq = n->cq[i];

        if (sq) {
            /*
             * CAP.DSTRD is 0, so offset of ith sq db_addr is (i<<3)
             * nvme_process_db() uses this hard-coded way to calculate
             * doorbell offsets. Be consistent with that here.
             */
            sq->db_addr = dbs_addr + (i << 3);
            sq->ei_addr = eis_addr + (i << 3);
            stl_le_pci_dma(pci, sq->db_addr, sq->tail, MEMTXATTRS_UNSPECIFIED);

            if (n->params.ioeventfd && sq->sqid != 0) {
                if (!nvme_init_sq_ioeventfd(sq)) {
                    sq->ioeventfd_enabled = true;
                }
            }
        }

        if (cq) {
            /* CAP.DSTRD is 0, so offset of ith cq db_addr is (i<<3)+(1<<2) */
            cq->db_addr = dbs_addr + (i << 3) + (1 << 2);
            cq->ei_addr = eis_addr + (i << 3) + (1 << 2);
            stl_le_pci_dma(pci, cq->db_addr, cq->head, MEMTXATTRS_UNSPECIFIED);

            if (n->params.ioeventfd && cq->cqid != 0) {
                if (!nvme_init_cq_ioeventfd(cq)) {
                    cq->ioeventfd_enabled = true;
                }
            }
        }
    }

    trace_pci_nvme_dbbuf_config(dbs_addr, eis_addr);

    return NVME_SUCCESS;
}

static uint16_t nvme_directive_send(NvmeCtrl *n, NvmeRequest *req)
{
    return NVME_INVALID_FIELD | NVME_DNR;
}

static uint16_t nvme_directive_receive(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns;
    uint32_t dw10 = le32_to_cpu(req->cmd.cdw10);
    uint32_t dw11 = le32_to_cpu(req->cmd.cdw11);
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);
    uint8_t doper, dtype;
    uint32_t numd, trans_len;
    NvmeDirectiveIdentify id = {
        .supported = 1 << NVME_DIRECTIVE_IDENTIFY,
        .enabled = 1 << NVME_DIRECTIVE_IDENTIFY,
    };

    numd = dw10 + 1;
    doper = dw11 & 0xff;
    dtype = (dw11 >> 8) & 0xff;

    trans_len = MIN(sizeof(NvmeDirectiveIdentify), numd << 2);

    if (nsid == NVME_NSID_BROADCAST || dtype != NVME_DIRECTIVE_IDENTIFY ||
        doper != NVME_DIRECTIVE_RETURN_PARAMS) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    ns = nvme_ns(n, nsid);
    if (!ns) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    switch (dtype) {
    case NVME_DIRECTIVE_IDENTIFY:
        switch (doper) {
        case NVME_DIRECTIVE_RETURN_PARAMS:
            if (ns->endgrp && ns->endgrp->fdp.enabled) {
                id.supported |= 1 << NVME_DIRECTIVE_DATA_PLACEMENT;
                id.enabled |= 1 << NVME_DIRECTIVE_DATA_PLACEMENT;
                id.persistent |= 1 << NVME_DIRECTIVE_DATA_PLACEMENT;
            }

            return nvme_c2h(n, (uint8_t *)&id, trans_len, req);

        default:
            return NVME_INVALID_FIELD | NVME_DNR;
        }

    default:
        return NVME_INVALID_FIELD;
    }
}

static uint16_t nvme_admin_cmd(NvmeCtrl *n, NvmeRequest *req)
{
    trace_pci_nvme_admin_cmd(nvme_cid(req), nvme_sqid(req), req->cmd.opcode,
                             nvme_adm_opc_str(req->cmd.opcode));

    if (!(n->cse.acs[req->cmd.opcode] & NVME_CMD_EFF_CSUPP)) {
        trace_pci_nvme_err_invalid_admin_opc(req->cmd.opcode);
        return NVME_INVALID_OPCODE | NVME_DNR;
    }

    /* SGLs shall not be used for Admin commands in NVMe over PCIe */
    if (NVME_CMD_FLAGS_PSDT(req->cmd.flags) != NVME_PSDT_PRP) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (NVME_CMD_FLAGS_FUSE(req->cmd.flags)) {
        return NVME_INVALID_FIELD;
    }

    switch (req->cmd.opcode) {
    case NVME_ADM_CMD_DELETE_SQ:
        return nvme_del_sq(n, req);
    case NVME_ADM_CMD_CREATE_SQ:
        return nvme_create_sq(n, req);
    case NVME_ADM_CMD_GET_LOG_PAGE:
        return nvme_get_log(n, req);
    case NVME_ADM_CMD_DELETE_CQ:
        return nvme_del_cq(n, req);
    case NVME_ADM_CMD_CREATE_CQ:
        return nvme_create_cq(n, req);
    case NVME_ADM_CMD_IDENTIFY:
        return nvme_identify(n, req);
    case NVME_ADM_CMD_ABORT:
        return nvme_abort(n, req);
    case NVME_ADM_CMD_SET_FEATURES:
        return nvme_set_feature(n, req);
    case NVME_ADM_CMD_GET_FEATURES:
        return nvme_get_feature(n, req);
    case NVME_ADM_CMD_ASYNC_EV_REQ:
        return nvme_aer(n, req);
    case NVME_ADM_CMD_NS_ATTACHMENT:
        return nvme_ns_attachment(n, req);
    case NVME_ADM_CMD_VIRT_MNGMT:
        return nvme_virt_mngmt(n, req);
    case NVME_ADM_CMD_DBBUF_CONFIG:
        return nvme_dbbuf_config(n, req);
    case NVME_ADM_CMD_FORMAT_NVM:
        return nvme_format(n, req);
    case NVME_ADM_CMD_DIRECTIVE_SEND:
        return nvme_directive_send(n, req);
    case NVME_ADM_CMD_DIRECTIVE_RECV:
        return nvme_directive_receive(n, req);
    default:
        g_assert_not_reached();
    }

    return NVME_INVALID_OPCODE | NVME_DNR;
}

static void nvme_update_sq_eventidx(const NvmeSQueue *sq)
{
    trace_pci_nvme_update_sq_eventidx(sq->sqid, sq->tail);

    stl_le_pci_dma(PCI_DEVICE(sq->ctrl), sq->ei_addr, sq->tail,
                   MEMTXATTRS_UNSPECIFIED);
}

static void nvme_update_sq_tail(NvmeSQueue *sq)
{
    ldl_le_pci_dma(PCI_DEVICE(sq->ctrl), sq->db_addr, &sq->tail,
                   MEMTXATTRS_UNSPECIFIED);

    trace_pci_nvme_update_sq_tail(sq->sqid, sq->tail);
}

#define NVME_ATOMIC_NO_START        0
#define NVME_ATOMIC_START_ATOMIC    1
#define NVME_ATOMIC_START_NONATOMIC 2

static int nvme_atomic_write_check(NvmeCtrl *n, NvmeCmd *cmd,
    NvmeAtomic *atomic)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)cmd;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = (uint32_t)le16_to_cpu(rw->nlb);
    uint64_t elba = slba + nlb;
    bool cmd_atomic_wr = true;
    int i;

    if ((cmd->opcode == NVME_CMD_READ) || ((cmd->opcode == NVME_CMD_WRITE) &&
        ((rw->nlb + 1) > atomic->atomic_max_write_size))) {
        cmd_atomic_wr = false;
    }

    /*
     * Walk the queues to see if there are any atomic conflicts.
     */
    for (i = 1; i < n->params.max_ioqpairs + 1; i++) {
        NvmeSQueue *sq;
        NvmeRequest *req;
        NvmeRwCmd *req_rw;
        uint64_t req_slba;
        uint32_t req_nlb;
        uint64_t req_elba;

        sq = n->sq[i];
        if (!sq) {
            continue;
        }

        /*
         * Walk all the requests on a given queue.
         */
        QTAILQ_FOREACH(req, &sq->out_req_list, entry) {
            req_rw = (NvmeRwCmd *)&req->cmd;

            if (((req_rw->opcode == NVME_CMD_WRITE) ||
                 (req_rw->opcode == NVME_CMD_READ)) &&
                (cmd->nsid == req->ns->params.nsid)) {
                req_slba = le64_to_cpu(req_rw->slba);
                req_nlb = (uint32_t)le16_to_cpu(req_rw->nlb);
                req_elba = req_slba + req_nlb;

                if (cmd_atomic_wr) {
                    if ((elba >= req_slba) && (slba <= req_elba)) {
                        return NVME_ATOMIC_NO_START;
                    }
                } else {
                    if (req->atomic_write && ((elba >= req_slba) &&
                        (slba <= req_elba))) {
                        return NVME_ATOMIC_NO_START;
                    }
                }
            }
        }
    }
    if (cmd_atomic_wr) {
        return NVME_ATOMIC_START_ATOMIC;
    }
    return NVME_ATOMIC_START_NONATOMIC;
}

static NvmeAtomic *nvme_get_atomic(NvmeCtrl *n, NvmeCmd *cmd)
{
    if (n->atomic.atomic_writes) {
        return &n->atomic;
    }
    return NULL;
}

static void nvme_process_sq(void *opaque)
{
    NvmeSQueue *sq = opaque;
    NvmeCtrl *n = sq->ctrl;
    NvmeCQueue *cq = n->cq[sq->cqid];

    uint16_t status;
    hwaddr addr;
    NvmeCmd cmd;
    NvmeRequest *req;

    if (n->dbbuf_enabled) {
        nvme_update_sq_tail(sq);
    }

    while (!(nvme_sq_empty(sq) || QTAILQ_EMPTY(&sq->req_list))) {
        NvmeAtomic *atomic;
        bool cmd_is_atomic;

        addr = sq->dma_addr + (sq->head << NVME_SQES);
        if (nvme_addr_read(n, addr, (void *)&cmd, sizeof(cmd))) {
            trace_pci_nvme_err_addr_read(addr);
            trace_pci_nvme_err_cfs();
            stl_le_p(&n->bar.csts, NVME_CSTS_FAILED);
            break;
        }

        atomic = nvme_get_atomic(n, &cmd);

        cmd_is_atomic = false;
        if (sq->sqid && atomic) {
            int ret;

            ret = nvme_atomic_write_check(n, &cmd, atomic);
            switch (ret) {
            case NVME_ATOMIC_NO_START:
                qemu_bh_schedule(sq->bh);
                return;
            case NVME_ATOMIC_START_ATOMIC:
                cmd_is_atomic = true;
                break;
            case NVME_ATOMIC_START_NONATOMIC:
            default:
                break;
            }
        }
        nvme_inc_sq_head(sq);

        req = QTAILQ_FIRST(&sq->req_list);
        QTAILQ_REMOVE(&sq->req_list, req, entry);
        QTAILQ_INSERT_TAIL(&sq->out_req_list, req, entry);
        nvme_req_clear(req);
        req->cqe.cid = cmd.cid;
        memcpy(&req->cmd, &cmd, sizeof(NvmeCmd));

        if (sq->sqid && atomic) {
            req->atomic_write = cmd_is_atomic;
        }

        status = sq->sqid ? nvme_io_cmd(n, req) :
            nvme_admin_cmd(n, req);
        if (status != NVME_NO_COMPLETE) {
            req->status = status;
            nvme_enqueue_req_completion(cq, req);
        }

        if (n->dbbuf_enabled) {
            nvme_update_sq_eventidx(sq);
            nvme_update_sq_tail(sq);
        }
    }
}

static void nvme_update_msixcap_ts(PCIDevice *pci_dev, uint32_t table_size)
{
    uint8_t *config;

    if (!msix_present(pci_dev)) {
        return;
    }

    assert(table_size > 0 && table_size <= pci_dev->msix_entries_nr);

    config = pci_dev->config + pci_dev->msix_cap;
    pci_set_word_by_mask(config + PCI_MSIX_FLAGS, PCI_MSIX_FLAGS_QSIZE,
                         table_size - 1);
}

static void nvme_activate_virt_res(NvmeCtrl *n)
{
    PCIDevice *pci_dev = PCI_DEVICE(n);
    NvmePriCtrlCap *cap = &n->pri_ctrl_cap;
    NvmeSecCtrlEntry *sctrl;

    /* -1 to account for the admin queue */
    if (pci_is_vf(pci_dev)) {
        sctrl = nvme_sctrl(n);
        cap->vqprt = sctrl->nvq;
        cap->viprt = sctrl->nvi;
        n->conf_ioqpairs = sctrl->nvq ? le16_to_cpu(sctrl->nvq) - 1 : 0;
        n->conf_msix_qsize = sctrl->nvi ? le16_to_cpu(sctrl->nvi) : 1;
    } else {
        cap->vqrfap = n->next_pri_ctrl_cap.vqrfap;
        cap->virfap = n->next_pri_ctrl_cap.virfap;
        n->conf_ioqpairs = le16_to_cpu(cap->vqprt) +
                           le16_to_cpu(cap->vqrfap) - 1;
        n->conf_msix_qsize = le16_to_cpu(cap->viprt) +
                             le16_to_cpu(cap->virfap);
    }
}

static void nvme_ctrl_reset(NvmeCtrl *n, NvmeResetType rst)
{
    PCIDevice *pci_dev = PCI_DEVICE(n);
    NvmeSecCtrlEntry *sctrl;
    NvmeNamespace *ns;
    int i;

    for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }

        nvme_ns_drain(ns);
    }

    for (i = 0; i < n->params.max_ioqpairs + 1; i++) {
        if (n->sq[i] != NULL) {
            nvme_free_sq(n->sq[i], n);
        }
    }
    for (i = 0; i < n->params.max_ioqpairs + 1; i++) {
        if (n->cq[i] != NULL) {
            nvme_free_cq(n->cq[i], n);
        }
    }

    while (!QTAILQ_EMPTY(&n->aer_queue)) {
        NvmeAsyncEvent *event = QTAILQ_FIRST(&n->aer_queue);
        QTAILQ_REMOVE(&n->aer_queue, event, entry);
        g_free(event);
    }

    if (n->params.sriov_max_vfs) {
        if (!pci_is_vf(pci_dev)) {
            for (i = 0; i < n->nr_sec_ctrls; i++) {
                sctrl = &n->sec_ctrl_list[i];
                nvme_virt_set_state(n, le16_to_cpu(sctrl->scid), false);
            }
        }

        if (rst != NVME_RESET_CONTROLLER) {
            nvme_activate_virt_res(n);
        }
    }

    n->aer_queued = 0;
    n->aer_mask = 0;
    n->outstanding_aers = 0;
    n->qs_created = false;

    n->dn = n->params.atomic_dn; /* Set Disable Normal */

    nvme_update_msixcap_ts(pci_dev, n->conf_msix_qsize);

    if (pci_is_vf(pci_dev)) {
        sctrl = nvme_sctrl(n);

        stl_le_p(&n->bar.csts, sctrl->scs ? 0 : NVME_CSTS_FAILED);
    } else {
        stl_le_p(&n->bar.csts, 0);
    }

    stl_le_p(&n->bar.intms, 0);
    stl_le_p(&n->bar.intmc, 0);
    stl_le_p(&n->bar.cc, 0);

    n->dbbuf_dbs = 0;
    n->dbbuf_eis = 0;
    n->dbbuf_enabled = false;
}

static void nvme_ctrl_shutdown(NvmeCtrl *n)
{
    NvmeNamespace *ns;
    int i;

    if (n->pmr.dev) {
        memory_region_msync(&n->pmr.dev->mr, 0, n->pmr.dev->size);
    }

    for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }

        nvme_ns_shutdown(ns);
    }
}

static int nvme_start_ctrl(NvmeCtrl *n)
{
    uint64_t cap = ldq_le_p(&n->bar.cap);
    uint32_t cc = ldl_le_p(&n->bar.cc);
    uint32_t aqa = ldl_le_p(&n->bar.aqa);
    uint64_t asq = ldq_le_p(&n->bar.asq);
    uint64_t acq = ldq_le_p(&n->bar.acq);
    uint32_t page_bits = NVME_CC_MPS(cc) + 12;
    uint32_t page_size = 1 << page_bits;
    NvmeSecCtrlEntry *sctrl = nvme_sctrl(n);

    if (pci_is_vf(PCI_DEVICE(n)) && !sctrl->scs) {
        trace_pci_nvme_err_startfail_virt_state(le16_to_cpu(sctrl->nvi),
                                                le16_to_cpu(sctrl->nvq));
        return -1;
    }
    if (unlikely(n->cq[0])) {
        trace_pci_nvme_err_startfail_cq();
        return -1;
    }
    if (unlikely(n->sq[0])) {
        trace_pci_nvme_err_startfail_sq();
        return -1;
    }
    if (unlikely(asq & (page_size - 1))) {
        trace_pci_nvme_err_startfail_asq_misaligned(asq);
        return -1;
    }
    if (unlikely(acq & (page_size - 1))) {
        trace_pci_nvme_err_startfail_acq_misaligned(acq);
        return -1;
    }
    if (unlikely(!(NVME_CAP_CSS(cap) & (1 << NVME_CC_CSS(cc))))) {
        trace_pci_nvme_err_startfail_css(NVME_CC_CSS(cc));
        return -1;
    }
    if (unlikely(NVME_CC_MPS(cc) < NVME_CAP_MPSMIN(cap))) {
        trace_pci_nvme_err_startfail_page_too_small(
                    NVME_CC_MPS(cc),
                    NVME_CAP_MPSMIN(cap));
        return -1;
    }
    if (unlikely(NVME_CC_MPS(cc) >
                 NVME_CAP_MPSMAX(cap))) {
        trace_pci_nvme_err_startfail_page_too_large(
                    NVME_CC_MPS(cc),
                    NVME_CAP_MPSMAX(cap));
        return -1;
    }
    if (unlikely(!NVME_AQA_ASQS(aqa))) {
        trace_pci_nvme_err_startfail_asqent_sz_zero();
        return -1;
    }
    if (unlikely(!NVME_AQA_ACQS(aqa))) {
        trace_pci_nvme_err_startfail_acqent_sz_zero();
        return -1;
    }

    n->page_bits = page_bits;
    n->page_size = page_size;
    n->max_prp_ents = n->page_size / sizeof(uint64_t);
    nvme_init_cq(&n->admin_cq, n, acq, 0, 0, NVME_AQA_ACQS(aqa) + 1, 1);
    nvme_init_sq(&n->admin_sq, n, asq, 0, 0, NVME_AQA_ASQS(aqa) + 1);

    nvme_set_timestamp(n, 0ULL);

    /* verify that the command sets of attached namespaces are supported */
    for (int i = 1; i <= NVME_MAX_NAMESPACES; i++) {
        NvmeNamespace *ns = nvme_subsys_ns(n->subsys, i);

        if (!ns || (!ns->params.shared && ns->ctrl != n)) {
            continue;
        }

        if (nvme_csi_supported(n, ns->csi) && !ns->params.detached) {
            if (!ns->attached || ns->params.shared) {
                nvme_attach_ns(n, ns);
            }
        }
    }

    nvme_update_dsm_limits(n, NULL);

    return 0;
}

static void nvme_cmb_enable_regs(NvmeCtrl *n)
{
    uint32_t cmbloc = ldl_le_p(&n->bar.cmbloc);
    uint32_t cmbsz = ldl_le_p(&n->bar.cmbsz);

    NVME_CMBLOC_SET_CDPCILS(cmbloc, 1);
    NVME_CMBLOC_SET_CDPMLS(cmbloc, 1);
    NVME_CMBLOC_SET_BIR(cmbloc, NVME_CMB_BIR);
    stl_le_p(&n->bar.cmbloc, cmbloc);

    NVME_CMBSZ_SET_SQS(cmbsz, 1);
    NVME_CMBSZ_SET_CQS(cmbsz, 0);
    NVME_CMBSZ_SET_LISTS(cmbsz, 1);
    NVME_CMBSZ_SET_RDS(cmbsz, 1);
    NVME_CMBSZ_SET_WDS(cmbsz, 1);
    NVME_CMBSZ_SET_SZU(cmbsz, 2); /* MBs */
    NVME_CMBSZ_SET_SZ(cmbsz, n->params.cmb_size_mb);
    stl_le_p(&n->bar.cmbsz, cmbsz);
}

static void nvme_write_bar(NvmeCtrl *n, hwaddr offset, uint64_t data,
                           unsigned size)
{
    PCIDevice *pci = PCI_DEVICE(n);
    uint64_t cap = ldq_le_p(&n->bar.cap);
    uint32_t cc = ldl_le_p(&n->bar.cc);
    uint32_t intms = ldl_le_p(&n->bar.intms);
    uint32_t csts = ldl_le_p(&n->bar.csts);
    uint32_t pmrsts = ldl_le_p(&n->bar.pmrsts);

    if (unlikely(offset & (sizeof(uint32_t) - 1))) {
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_misaligned32,
                       "MMIO write not 32-bit aligned,"
                       " offset=0x%"PRIx64"", offset);
        /* should be ignored, fall through for now */
    }

    if (unlikely(size < sizeof(uint32_t))) {
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_toosmall,
                       "MMIO write smaller than 32-bits,"
                       " offset=0x%"PRIx64", size=%u",
                       offset, size);
        /* should be ignored, fall through for now */
    }

    switch (offset) {
    case NVME_REG_INTMS:
        if (unlikely(msix_enabled(pci))) {
            NVME_GUEST_ERR(pci_nvme_ub_mmiowr_intmask_with_msix,
                           "undefined access to interrupt mask set"
                           " when MSI-X is enabled");
            /* should be ignored, fall through for now */
        }
        intms |= data;
        stl_le_p(&n->bar.intms, intms);
        n->bar.intmc = n->bar.intms;
        trace_pci_nvme_mmio_intm_set(data & 0xffffffff, intms);
        nvme_irq_check(n);
        break;
    case NVME_REG_INTMC:
        if (unlikely(msix_enabled(pci))) {
            NVME_GUEST_ERR(pci_nvme_ub_mmiowr_intmask_with_msix,
                           "undefined access to interrupt mask clr"
                           " when MSI-X is enabled");
            /* should be ignored, fall through for now */
        }
        intms &= ~data;
        stl_le_p(&n->bar.intms, intms);
        n->bar.intmc = n->bar.intms;
        trace_pci_nvme_mmio_intm_clr(data & 0xffffffff, intms);
        nvme_irq_check(n);
        break;
    case NVME_REG_CC:
        stl_le_p(&n->bar.cc, data);

        trace_pci_nvme_mmio_cfg(data & 0xffffffff);

        if (NVME_CC_SHN(data) && !(NVME_CC_SHN(cc))) {
            trace_pci_nvme_mmio_shutdown_set();
            nvme_ctrl_shutdown(n);
            csts &= ~(CSTS_SHST_MASK << CSTS_SHST_SHIFT);
            csts |= NVME_CSTS_SHST_COMPLETE;
        } else if (!NVME_CC_SHN(data) && NVME_CC_SHN(cc)) {
            trace_pci_nvme_mmio_shutdown_cleared();
            csts &= ~(CSTS_SHST_MASK << CSTS_SHST_SHIFT);
        }

        if (NVME_CC_EN(data) && !NVME_CC_EN(cc)) {
            if (unlikely(nvme_start_ctrl(n))) {
                trace_pci_nvme_err_startfail();
                csts = NVME_CSTS_FAILED;
            } else {
                trace_pci_nvme_mmio_start_success();
                csts = NVME_CSTS_READY;
            }
        } else if (!NVME_CC_EN(data) && NVME_CC_EN(cc)) {
            trace_pci_nvme_mmio_stopped();
            nvme_ctrl_reset(n, NVME_RESET_CONTROLLER);

            break;
        }

        stl_le_p(&n->bar.csts, csts);

        break;
    case NVME_REG_CSTS:
        if (data & (1 << 4)) {
            NVME_GUEST_ERR(pci_nvme_ub_mmiowr_ssreset_w1c_unsupported,
                           "attempted to W1C CSTS.NSSRO"
                           " but CAP.NSSRS is zero (not supported)");
        } else if (data != 0) {
            NVME_GUEST_ERR(pci_nvme_ub_mmiowr_ro_csts,
                           "attempted to set a read only bit"
                           " of controller status");
        }
        break;
    case NVME_REG_NSSR:
        if (data == 0x4e564d65) {
            trace_pci_nvme_ub_mmiowr_ssreset_unsupported();
        } else {
            /* The spec says that writes of other values have no effect */
            return;
        }
        break;
    case NVME_REG_AQA:
        stl_le_p(&n->bar.aqa, data);
        trace_pci_nvme_mmio_aqattr(data & 0xffffffff);
        break;
    case NVME_REG_ASQ:
        stn_le_p(&n->bar.asq, size, data);
        trace_pci_nvme_mmio_asqaddr(data);
        break;
    case NVME_REG_ASQ + 4:
        stl_le_p((uint8_t *)&n->bar.asq + 4, data);
        trace_pci_nvme_mmio_asqaddr_hi(data, ldq_le_p(&n->bar.asq));
        break;
    case NVME_REG_ACQ:
        trace_pci_nvme_mmio_acqaddr(data);
        stn_le_p(&n->bar.acq, size, data);
        break;
    case NVME_REG_ACQ + 4:
        stl_le_p((uint8_t *)&n->bar.acq + 4, data);
        trace_pci_nvme_mmio_acqaddr_hi(data, ldq_le_p(&n->bar.acq));
        break;
    case NVME_REG_CMBLOC:
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_cmbloc_reserved,
                       "invalid write to reserved CMBLOC"
                       " when CMBSZ is zero, ignored");
        return;
    case NVME_REG_CMBSZ:
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_cmbsz_readonly,
                       "invalid write to read only CMBSZ, ignored");
        return;
    case NVME_REG_CMBMSC:
        if (!NVME_CAP_CMBS(cap)) {
            return;
        }

        stn_le_p(&n->bar.cmbmsc, size, data);
        n->cmb.cmse = false;

        if (NVME_CMBMSC_CRE(data)) {
            nvme_cmb_enable_regs(n);

            if (NVME_CMBMSC_CMSE(data)) {
                uint64_t cmbmsc = ldq_le_p(&n->bar.cmbmsc);
                hwaddr cba = NVME_CMBMSC_CBA(cmbmsc) << CMBMSC_CBA_SHIFT;
                if (cba + int128_get64(n->cmb.mem.size) < cba) {
                    uint32_t cmbsts = ldl_le_p(&n->bar.cmbsts);
                    NVME_CMBSTS_SET_CBAI(cmbsts, 1);
                    stl_le_p(&n->bar.cmbsts, cmbsts);
                    return;
                }

                n->cmb.cba = cba;
                n->cmb.cmse = true;
            }
        } else {
            n->bar.cmbsz = 0;
            n->bar.cmbloc = 0;
        }

        return;
    case NVME_REG_CMBMSC + 4:
        stl_le_p((uint8_t *)&n->bar.cmbmsc + 4, data);
        return;

    case NVME_REG_PMRCAP:
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_pmrcap_readonly,
                       "invalid write to PMRCAP register, ignored");
        return;
    case NVME_REG_PMRCTL:
        if (!NVME_CAP_PMRS(cap)) {
            return;
        }

        stl_le_p(&n->bar.pmrctl, data);
        if (NVME_PMRCTL_EN(data)) {
            memory_region_set_enabled(&n->pmr.dev->mr, true);
            pmrsts = 0;
        } else {
            memory_region_set_enabled(&n->pmr.dev->mr, false);
            NVME_PMRSTS_SET_NRDY(pmrsts, 1);
            n->pmr.cmse = false;
        }
        stl_le_p(&n->bar.pmrsts, pmrsts);
        return;
    case NVME_REG_PMRSTS:
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_pmrsts_readonly,
                       "invalid write to PMRSTS register, ignored");
        return;
    case NVME_REG_PMREBS:
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_pmrebs_readonly,
                       "invalid write to PMREBS register, ignored");
        return;
    case NVME_REG_PMRSWTP:
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_pmrswtp_readonly,
                       "invalid write to PMRSWTP register, ignored");
        return;
    case NVME_REG_PMRMSCL:
        if (!NVME_CAP_PMRS(cap)) {
            return;
        }

        stl_le_p(&n->bar.pmrmscl, data);
        n->pmr.cmse = false;

        if (NVME_PMRMSCL_CMSE(data)) {
            uint64_t pmrmscu = ldl_le_p(&n->bar.pmrmscu);
            hwaddr cba = pmrmscu << 32 |
                (NVME_PMRMSCL_CBA(data) << PMRMSCL_CBA_SHIFT);
            if (cba + int128_get64(n->pmr.dev->mr.size) < cba) {
                NVME_PMRSTS_SET_CBAI(pmrsts, 1);
                stl_le_p(&n->bar.pmrsts, pmrsts);
                return;
            }

            n->pmr.cmse = true;
            n->pmr.cba = cba;
        }

        return;
    case NVME_REG_PMRMSCU:
        if (!NVME_CAP_PMRS(cap)) {
            return;
        }

        stl_le_p(&n->bar.pmrmscu, data);
        return;
    default:
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_invalid,
                       "invalid MMIO write,"
                       " offset=0x%"PRIx64", data=%"PRIx64"",
                       offset, data);
        break;
    }
}

static uint64_t nvme_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;
    uint8_t *ptr = (uint8_t *)&n->bar;

    trace_pci_nvme_mmio_read(addr, size);

    if (unlikely(addr & (sizeof(uint32_t) - 1))) {
        NVME_GUEST_ERR(pci_nvme_ub_mmiord_misaligned32,
                       "MMIO read not 32-bit aligned,"
                       " offset=0x%"PRIx64"", addr);
        /* should RAZ, fall through for now */
    } else if (unlikely(size < sizeof(uint32_t))) {
        NVME_GUEST_ERR(pci_nvme_ub_mmiord_toosmall,
                       "MMIO read smaller than 32-bits,"
                       " offset=0x%"PRIx64"", addr);
        /* should RAZ, fall through for now */
    }

    if (addr > sizeof(n->bar) - size) {
        NVME_GUEST_ERR(pci_nvme_ub_mmiord_invalid_ofs,
                       "MMIO read beyond last register,"
                       " offset=0x%"PRIx64", returning 0", addr);

        return 0;
    }

    if (pci_is_vf(PCI_DEVICE(n)) && !nvme_sctrl(n)->scs &&
        addr != NVME_REG_CSTS) {
        trace_pci_nvme_err_ignored_mmio_vf_offline(addr, size);
        return 0;
    }

    /*
     * When PMRWBM bit 1 is set then read from
     * from PMRSTS should ensure prior writes
     * made it to persistent media
     */
    if (addr == NVME_REG_PMRSTS &&
        (NVME_PMRCAP_PMRWBM(ldl_le_p(&n->bar.pmrcap)) & 0x02)) {
        memory_region_msync(&n->pmr.dev->mr, 0, n->pmr.dev->size);
    }

    return ldn_le_p(ptr + addr, size);
}

static void nvme_process_db(NvmeCtrl *n, hwaddr addr, int val)
{
    PCIDevice *pci = PCI_DEVICE(n);
    uint32_t qid;

    if (unlikely(addr & ((1 << 2) - 1))) {
        NVME_GUEST_ERR(pci_nvme_ub_db_wr_misaligned,
                       "doorbell write not 32-bit aligned,"
                       " offset=0x%"PRIx64", ignoring", addr);
        return;
    }

    if (((addr - 0x1000) >> 2) & 1) {
        /* Completion queue doorbell write */

        uint16_t new_head = val & 0xffff;
        NvmeCQueue *cq;

        qid = (addr - (0x1000 + (1 << 2))) >> 3;
        if (unlikely(nvme_check_cqid(n, qid))) {
            NVME_GUEST_ERR(pci_nvme_ub_db_wr_invalid_cq,
                           "completion queue doorbell write"
                           " for nonexistent queue,"
                           " sqid=%"PRIu32", ignoring", qid);

            /*
             * NVM Express v1.3d, Section 4.1 state: "If host software writes
             * an invalid value to the Submission Queue Tail Doorbell or
             * Completion Queue Head Doorbell register and an Asynchronous Event
             * Request command is outstanding, then an asynchronous event is
             * posted to the Admin Completion Queue with a status code of
             * Invalid Doorbell Write Value."
             *
             * Also note that the spec includes the "Invalid Doorbell Register"
             * status code, but nowhere does it specify when to use it.
             * However, it seems reasonable to use it here in a similar
             * fashion.
             */
            if (n->outstanding_aers) {
                nvme_enqueue_event(n, NVME_AER_TYPE_ERROR,
                                   NVME_AER_INFO_ERR_INVALID_DB_REGISTER,
                                   NVME_LOG_ERROR_INFO);
            }

            return;
        }

        cq = n->cq[qid];
        if (unlikely(new_head >= cq->size)) {
            NVME_GUEST_ERR(pci_nvme_ub_db_wr_invalid_cqhead,
                           "completion queue doorbell write value"
                           " beyond queue size, sqid=%"PRIu32","
                           " new_head=%"PRIu16", ignoring",
                           qid, new_head);

            if (n->outstanding_aers) {
                nvme_enqueue_event(n, NVME_AER_TYPE_ERROR,
                                   NVME_AER_INFO_ERR_INVALID_DB_VALUE,
                                   NVME_LOG_ERROR_INFO);
            }

            return;
        }

        trace_pci_nvme_mmio_doorbell_cq(cq->cqid, new_head);

        /* scheduled deferred cqe posting if queue was previously full */
        if (nvme_cq_full(cq)) {
            qemu_bh_schedule(cq->bh);
        }

        cq->head = new_head;
        if (!qid && n->dbbuf_enabled) {
            stl_le_pci_dma(pci, cq->db_addr, cq->head, MEMTXATTRS_UNSPECIFIED);
        }

        if (cq->tail == cq->head) {
            if (cq->irq_enabled) {
                n->cq_pending--;
            }

            nvme_irq_deassert(n, cq);
        }
    } else {
        /* Submission queue doorbell write */

        uint16_t new_tail = val & 0xffff;
        NvmeSQueue *sq;

        qid = (addr - 0x1000) >> 3;
        if (unlikely(nvme_check_sqid(n, qid))) {
            NVME_GUEST_ERR(pci_nvme_ub_db_wr_invalid_sq,
                           "submission queue doorbell write"
                           " for nonexistent queue,"
                           " sqid=%"PRIu32", ignoring", qid);

            if (n->outstanding_aers) {
                nvme_enqueue_event(n, NVME_AER_TYPE_ERROR,
                                   NVME_AER_INFO_ERR_INVALID_DB_REGISTER,
                                   NVME_LOG_ERROR_INFO);
            }

            return;
        }

        sq = n->sq[qid];
        if (unlikely(new_tail >= sq->size)) {
            NVME_GUEST_ERR(pci_nvme_ub_db_wr_invalid_sqtail,
                           "submission queue doorbell write value"
                           " beyond queue size, sqid=%"PRIu32","
                           " new_tail=%"PRIu16", ignoring",
                           qid, new_tail);

            if (n->outstanding_aers) {
                nvme_enqueue_event(n, NVME_AER_TYPE_ERROR,
                                   NVME_AER_INFO_ERR_INVALID_DB_VALUE,
                                   NVME_LOG_ERROR_INFO);
            }

            return;
        }

        trace_pci_nvme_mmio_doorbell_sq(sq->sqid, new_tail);

        sq->tail = new_tail;
        if (!qid && n->dbbuf_enabled) {
            /*
             * The spec states "the host shall also update the controller's
             * corresponding doorbell property to match the value of that entry
             * in the Shadow Doorbell buffer."
             *
             * Since this context is currently a VM trap, we can safely enforce
             * the requirement from the device side in case the host is
             * misbehaving.
             *
             * Note, we shouldn't have to do this, but various drivers
             * including ones that run on Linux, are not updating Admin Queues,
             * so we can't trust reading it for an appropriate sq tail.
             */
            stl_le_pci_dma(pci, sq->db_addr, sq->tail, MEMTXATTRS_UNSPECIFIED);
        }

        qemu_bh_schedule(sq->bh);
    }
}

static void nvme_mmio_write(void *opaque, hwaddr addr, uint64_t data,
                            unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;

    trace_pci_nvme_mmio_write(addr, data, size);

    if (pci_is_vf(PCI_DEVICE(n)) && !nvme_sctrl(n)->scs &&
        addr != NVME_REG_CSTS) {
        trace_pci_nvme_err_ignored_mmio_vf_offline(addr, size);
        return;
    }

    if (addr < sizeof(n->bar)) {
        nvme_write_bar(n, addr, data, size);
    } else {
        nvme_process_db(n, addr, data);
    }
}

static const MemoryRegionOps nvme_mmio_ops = {
    .read = nvme_mmio_read,
    .write = nvme_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 8,
    },
};

static void nvme_cmb_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;
    stn_le_p(&n->cmb.buf[addr], size, data);
}

static uint64_t nvme_cmb_read(void *opaque, hwaddr addr, unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;
    return ldn_le_p(&n->cmb.buf[addr], size);
}

static const MemoryRegionOps nvme_cmb_ops = {
    .read = nvme_cmb_read,
    .write = nvme_cmb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static bool nvme_check_params(NvmeCtrl *n, Error **errp)
{
    NvmeParams *params = &n->params;

    if (params->num_queues) {
        warn_report("num_queues is deprecated; please use max_ioqpairs "
                    "instead");

        params->max_ioqpairs = params->num_queues - 1;
    }

    if (n->namespace.blkconf.blk && n->subsys) {
        error_setg(errp, "subsystem support is unavailable with legacy "
                   "namespace ('drive' property)");
        return false;
    }

    if (params->max_ioqpairs < 1 ||
        params->max_ioqpairs > NVME_MAX_IOQPAIRS) {
        error_setg(errp, "max_ioqpairs must be between 1 and %d",
                   NVME_MAX_IOQPAIRS);
        return false;
    }

    if (params->msix_qsize < 1 ||
        params->msix_qsize > PCI_MSIX_FLAGS_QSIZE + 1) {
        error_setg(errp, "msix_qsize must be between 1 and %d",
                   PCI_MSIX_FLAGS_QSIZE + 1);
        return false;
    }

    if (!params->serial) {
        error_setg(errp, "serial property not set");
        return false;
    }

    if (params->mqes < 1) {
        error_setg(errp, "mqes property cannot be less than 1");
        return false;
    }

    if (n->pmr.dev) {
        if (params->msix_exclusive_bar) {
            error_setg(errp, "not enough BARs available to enable PMR");
            return false;
        }

        if (host_memory_backend_is_mapped(n->pmr.dev)) {
            error_setg(errp, "can't use already busy memdev: %s",
                       object_get_canonical_path_component(OBJECT(n->pmr.dev)));
            return false;
        }

        if (!is_power_of_2(n->pmr.dev->size)) {
            error_setg(errp, "pmr backend size needs to be power of 2 in size");
            return false;
        }

        host_memory_backend_set_mapped(n->pmr.dev, true);
    }

    if (n->params.zasl > n->params.mdts) {
        error_setg(errp, "zoned.zasl (Zone Append Size Limit) must be less "
                   "than or equal to mdts (Maximum Data Transfer Size)");
        return false;
    }

    if (!n->params.vsl) {
        error_setg(errp, "vsl must be non-zero");
        return false;
    }

    if (params->sriov_max_vfs) {
        if (!n->subsys) {
            error_setg(errp, "subsystem is required for the use of SR-IOV");
            return false;
        }

        if (params->cmb_size_mb) {
            error_setg(errp, "CMB is not supported with SR-IOV");
            return false;
        }

        if (n->pmr.dev) {
            error_setg(errp, "PMR is not supported with SR-IOV");
            return false;
        }

        if (!params->sriov_vq_flexible || !params->sriov_vi_flexible) {
            error_setg(errp, "both sriov_vq_flexible and sriov_vi_flexible"
                       " must be set for the use of SR-IOV");
            return false;
        }

        if (params->sriov_vq_flexible < params->sriov_max_vfs * 2) {
            error_setg(errp, "sriov_vq_flexible must be greater than or equal"
                       " to %d (sriov_max_vfs * 2)", params->sriov_max_vfs * 2);
            return false;
        }

        if (params->max_ioqpairs < params->sriov_vq_flexible + 2) {
            error_setg(errp, "(max_ioqpairs - sriov_vq_flexible) must be"
                       " greater than or equal to 2");
            return false;
        }

        if (params->sriov_vi_flexible < params->sriov_max_vfs) {
            error_setg(errp, "sriov_vi_flexible must be greater than or equal"
                       " to %d (sriov_max_vfs)", params->sriov_max_vfs);
            return false;
        }

        if (params->msix_qsize < params->sriov_vi_flexible + 1) {
            error_setg(errp, "(msix_qsize - sriov_vi_flexible) must be"
                       " greater than or equal to 1");
            return false;
        }

        if (params->sriov_max_vi_per_vf &&
            (params->sriov_max_vi_per_vf - 1) % NVME_VF_RES_GRANULARITY) {
            error_setg(errp, "sriov_max_vi_per_vf must meet:"
                       " (sriov_max_vi_per_vf - 1) %% %d == 0 and"
                       " sriov_max_vi_per_vf >= 1", NVME_VF_RES_GRANULARITY);
            return false;
        }

        if (params->sriov_max_vq_per_vf &&
            (params->sriov_max_vq_per_vf < 2 ||
             (params->sriov_max_vq_per_vf - 1) % NVME_VF_RES_GRANULARITY)) {
            error_setg(errp, "sriov_max_vq_per_vf must meet:"
                       " (sriov_max_vq_per_vf - 1) %% %d == 0 and"
                       " sriov_max_vq_per_vf >= 2", NVME_VF_RES_GRANULARITY);
            return false;
        }
    }

    return true;
}

static void nvme_init_state(NvmeCtrl *n)
{
    NvmePriCtrlCap *cap = &n->pri_ctrl_cap;
    NvmeSecCtrlEntry *list = n->sec_ctrl_list;
    NvmeSecCtrlEntry *sctrl;
    PCIDevice *pci = PCI_DEVICE(n);
    NvmeAtomic *atomic = &n->atomic;
    NvmeIdCtrl *id = &n->id_ctrl;
    uint8_t max_vfs;
    int i;

    if (pci_is_vf(pci)) {
        sctrl = nvme_sctrl(n);
        max_vfs = 0;
        n->conf_ioqpairs = sctrl->nvq ? le16_to_cpu(sctrl->nvq) - 1 : 0;
        n->conf_msix_qsize = sctrl->nvi ? le16_to_cpu(sctrl->nvi) : 1;
    } else {
        max_vfs = n->params.sriov_max_vfs;
        n->conf_ioqpairs = n->params.max_ioqpairs;
        n->conf_msix_qsize = n->params.msix_qsize;
    }

    n->sq = g_new0(NvmeSQueue *, n->params.max_ioqpairs + 1);
    n->cq = g_new0(NvmeCQueue *, n->params.max_ioqpairs + 1);
    n->temperature = NVME_TEMPERATURE;
    n->features.temp_thresh_hi = NVME_TEMPERATURE_WARNING;
    n->starttime_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    n->aer_reqs = g_new0(NvmeRequest *, n->params.aerl + 1);
    QTAILQ_INIT(&n->aer_queue);

    n->nr_sec_ctrls = max_vfs;
    for (i = 0; i < max_vfs; i++) {
        sctrl = &list[i];
        sctrl->pcid = cpu_to_le16(n->cntlid);
        sctrl->vfn = cpu_to_le16(i + 1);
    }

    cap->cntlid = cpu_to_le16(n->cntlid);
    cap->crt = NVME_CRT_VQ | NVME_CRT_VI;

    if (pci_is_vf(pci)) {
        cap->vqprt = cpu_to_le16(1 + n->conf_ioqpairs);
    } else {
        cap->vqprt = cpu_to_le16(1 + n->params.max_ioqpairs -
                                 n->params.sriov_vq_flexible);
        cap->vqfrt = cpu_to_le32(n->params.sriov_vq_flexible);
        cap->vqrfap = cap->vqfrt;
        cap->vqgran = cpu_to_le16(NVME_VF_RES_GRANULARITY);
        cap->vqfrsm = n->params.sriov_max_vq_per_vf ?
                        cpu_to_le16(n->params.sriov_max_vq_per_vf) :
                        cap->vqfrt / MAX(max_vfs, 1);
    }

    if (pci_is_vf(pci)) {
        cap->viprt = cpu_to_le16(n->conf_msix_qsize);
    } else {
        cap->viprt = cpu_to_le16(n->params.msix_qsize -
                                 n->params.sriov_vi_flexible);
        cap->vifrt = cpu_to_le32(n->params.sriov_vi_flexible);
        cap->virfap = cap->vifrt;
        cap->vigran = cpu_to_le16(NVME_VF_RES_GRANULARITY);
        cap->vifrsm = n->params.sriov_max_vi_per_vf ?
                        cpu_to_le16(n->params.sriov_max_vi_per_vf) :
                        cap->vifrt / MAX(max_vfs, 1);
    }

    /* Atomic Write */
    id->awun = cpu_to_le16(n->params.atomic_awun);
    id->awupf = cpu_to_le16(n->params.atomic_awupf);
    n->dn = n->params.atomic_dn;

    if (id->awun || id->awupf) {
        if (id->awupf > id->awun) {
            id->awupf = 0;
        }

        if (n->dn) {
            atomic->atomic_max_write_size = id->awupf + 1;
        } else {
            atomic->atomic_max_write_size = id->awun + 1;
        }

        if (atomic->atomic_max_write_size == 1) {
            atomic->atomic_writes = 0;
        } else {
            atomic->atomic_writes = 1;
        }
    }
}

static void nvme_init_cmb(NvmeCtrl *n, PCIDevice *pci_dev)
{
    uint64_t cmb_size = n->params.cmb_size_mb * MiB;
    uint64_t cap = ldq_le_p(&n->bar.cap);

    n->cmb.buf = g_malloc0(cmb_size);
    memory_region_init_io(&n->cmb.mem, OBJECT(n), &nvme_cmb_ops, n,
                          "nvme-cmb", cmb_size);
    pci_register_bar(pci_dev, NVME_CMB_BIR,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64 |
                     PCI_BASE_ADDRESS_MEM_PREFETCH, &n->cmb.mem);

    NVME_CAP_SET_CMBS(cap, 1);
    stq_le_p(&n->bar.cap, cap);

    if (n->params.legacy_cmb) {
        nvme_cmb_enable_regs(n);
        n->cmb.cmse = true;
    }
}

static void nvme_init_pmr(NvmeCtrl *n, PCIDevice *pci_dev)
{
    uint32_t pmrcap = ldl_le_p(&n->bar.pmrcap);

    NVME_PMRCAP_SET_RDS(pmrcap, 1);
    NVME_PMRCAP_SET_WDS(pmrcap, 1);
    NVME_PMRCAP_SET_BIR(pmrcap, NVME_PMR_BIR);
    /* Turn on bit 1 support */
    NVME_PMRCAP_SET_PMRWBM(pmrcap, 0x02);
    NVME_PMRCAP_SET_CMSS(pmrcap, 1);
    stl_le_p(&n->bar.pmrcap, pmrcap);

    pci_register_bar(pci_dev, NVME_PMR_BIR,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64 |
                     PCI_BASE_ADDRESS_MEM_PREFETCH, &n->pmr.dev->mr);

    memory_region_set_enabled(&n->pmr.dev->mr, false);
}

static uint64_t nvme_mbar_size(unsigned total_queues, unsigned total_irqs,
                               unsigned *msix_table_offset,
                               unsigned *msix_pba_offset)
{
    uint64_t bar_size, msix_table_size;

    bar_size = sizeof(NvmeBar) + 2 * total_queues * NVME_DB_SIZE;

    if (total_irqs == 0) {
        goto out;
    }

    bar_size = QEMU_ALIGN_UP(bar_size, 4 * KiB);

    if (msix_table_offset) {
        *msix_table_offset = bar_size;
    }

    msix_table_size = PCI_MSIX_ENTRY_SIZE * total_irqs;
    bar_size += msix_table_size;
    bar_size = QEMU_ALIGN_UP(bar_size, 4 * KiB);

    if (msix_pba_offset) {
        *msix_pba_offset = bar_size;
    }

    bar_size += QEMU_ALIGN_UP(total_irqs, 64) / 8;

out:
    return pow2ceil(bar_size);
}

static bool nvme_init_sriov(NvmeCtrl *n, PCIDevice *pci_dev, uint16_t offset,
                            Error **errp)
{
    uint16_t vf_dev_id = n->params.use_intel_id ?
                         PCI_DEVICE_ID_INTEL_NVME : PCI_DEVICE_ID_REDHAT_NVME;
    NvmePriCtrlCap *cap = &n->pri_ctrl_cap;
    uint64_t bar_size = nvme_mbar_size(le16_to_cpu(cap->vqfrsm),
                                      le16_to_cpu(cap->vifrsm),
                                      NULL, NULL);

    if (!pcie_sriov_pf_init(pci_dev, offset, "nvme", vf_dev_id,
                            n->params.sriov_max_vfs, n->params.sriov_max_vfs,
                            NVME_VF_OFFSET, NVME_VF_STRIDE, errp)) {
        return false;
    }

    pcie_sriov_pf_init_vf_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                              PCI_BASE_ADDRESS_MEM_TYPE_64, bar_size);

    return true;
}

static int nvme_add_pm_capability(PCIDevice *pci_dev, uint8_t offset)
{
    Error *err = NULL;
    int ret;

    ret = pci_pm_init(pci_dev, offset, &err);
    if (err) {
        error_report_err(err);
        return ret;
    }

    pci_set_word(pci_dev->config + offset + PCI_PM_PMC,
                 PCI_PM_CAP_VER_1_2);
    pci_set_word(pci_dev->config + offset + PCI_PM_CTRL,
                 PCI_PM_CTRL_NO_SOFT_RESET);
    pci_set_word(pci_dev->wmask + offset + PCI_PM_CTRL,
                 PCI_PM_CTRL_STATE_MASK);

    return 0;
}

static bool pcie_doe_spdm_rsp(DOECap *doe_cap)
{
    void *req = pcie_doe_get_write_mbox_ptr(doe_cap);
    uint32_t req_len = pcie_doe_get_obj_len(req) * 4;
    void *rsp = doe_cap->read_mbox;
    uint32_t rsp_len = SPDM_SOCKET_MAX_MESSAGE_BUFFER_SIZE;

    uint32_t recvd = spdm_socket_rsp(doe_cap->spdm_socket,
                             SPDM_SOCKET_TRANSPORT_TYPE_PCI_DOE,
                             req, req_len, rsp, rsp_len);
    doe_cap->read_mbox_len += DIV_ROUND_UP(recvd, 4);

    return recvd != 0;
}

static DOEProtocol doe_spdm_prot[] = {
    { PCI_VENDOR_ID_PCI_SIG, PCI_SIG_DOE_CMA, pcie_doe_spdm_rsp },
    { PCI_VENDOR_ID_PCI_SIG, PCI_SIG_DOE_SECURED_CMA, pcie_doe_spdm_rsp },
    { }
};

static bool nvme_init_pci(NvmeCtrl *n, PCIDevice *pci_dev, Error **errp)
{
    ERRP_GUARD();
    uint8_t *pci_conf = pci_dev->config;
    uint64_t bar_size;
    unsigned msix_table_offset = 0, msix_pba_offset = 0;
    unsigned nr_vectors;
    int ret;

    pci_conf[PCI_INTERRUPT_PIN] = pci_is_vf(pci_dev) ? 0 : 1;
    pci_config_set_prog_interface(pci_conf, 0x2);

    if (n->params.use_intel_id) {
        pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_NVME);
    } else {
        pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_REDHAT);
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_REDHAT_NVME);
    }

    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_EXPRESS);
    nvme_add_pm_capability(pci_dev, 0x60);
    pcie_endpoint_cap_init(pci_dev, 0x80);
    pcie_cap_flr_init(pci_dev);
    if (n->params.sriov_max_vfs) {
        pcie_ari_init(pci_dev, 0x100);
    }

    if (n->params.msix_exclusive_bar && !pci_is_vf(pci_dev)) {
        bar_size = nvme_mbar_size(n->params.max_ioqpairs + 1, 0, NULL, NULL);
        memory_region_init_io(&n->iomem, OBJECT(n), &nvme_mmio_ops, n, "nvme",
                              bar_size);
        pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64, &n->iomem);
        ret = msix_init_exclusive_bar(pci_dev, n->params.msix_qsize, 4, errp);
    } else {
        assert(n->params.msix_qsize >= 1);

        /* add one to max_ioqpairs to account for the admin queue pair */
        if (!pci_is_vf(pci_dev)) {
            nr_vectors = n->params.msix_qsize;
            bar_size = nvme_mbar_size(n->params.max_ioqpairs + 1,
                                      nr_vectors, &msix_table_offset,
                                      &msix_pba_offset);
        } else {
            NvmeCtrl *pn = NVME(pcie_sriov_get_pf(pci_dev));
            NvmePriCtrlCap *cap = &pn->pri_ctrl_cap;

            nr_vectors = le16_to_cpu(cap->vifrsm);
            bar_size = nvme_mbar_size(le16_to_cpu(cap->vqfrsm), nr_vectors,
                                      &msix_table_offset, &msix_pba_offset);
        }

        memory_region_init(&n->bar0, OBJECT(n), "nvme-bar0", bar_size);
        memory_region_init_io(&n->iomem, OBJECT(n), &nvme_mmio_ops, n, "nvme",
                              msix_table_offset);
        memory_region_add_subregion(&n->bar0, 0, &n->iomem);

        if (pci_is_vf(pci_dev)) {
            pcie_sriov_vf_register_bar(pci_dev, 0, &n->bar0);
        } else {
            pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                             PCI_BASE_ADDRESS_MEM_TYPE_64, &n->bar0);
        }

        ret = msix_init(pci_dev, nr_vectors,
                        &n->bar0, 0, msix_table_offset,
                        &n->bar0, 0, msix_pba_offset, 0, errp);
    }

    if (ret == -ENOTSUP) {
        /* report that msix is not supported, but do not error out */
        warn_report_err(*errp);
        *errp = NULL;
    } else if (ret < 0) {
        /* propagate error to caller */
        return false;
    }

    if (!pci_is_vf(pci_dev) && n->params.sriov_max_vfs &&
        !nvme_init_sriov(n, pci_dev, 0x120, errp)) {
        return false;
    }

    nvme_update_msixcap_ts(pci_dev, n->conf_msix_qsize);

    pcie_cap_deverr_init(pci_dev);

    /* DOE Initialisation */
    if (pci_dev->spdm_port) {
        uint16_t doe_offset = n->params.sriov_max_vfs ?
                                  PCI_CONFIG_SPACE_SIZE + PCI_ARI_SIZEOF
                                  : PCI_CONFIG_SPACE_SIZE;

        pcie_doe_init(pci_dev, &pci_dev->doe_spdm, doe_offset,
                      doe_spdm_prot, true, 0);

        pci_dev->doe_spdm.spdm_socket = spdm_socket_connect(pci_dev->spdm_port,
                                                            errp);

        if (pci_dev->doe_spdm.spdm_socket < 0) {
            return false;
        }
    }

    if (n->params.cmb_size_mb) {
        nvme_init_cmb(n, pci_dev);
    }

    if (n->pmr.dev) {
        nvme_init_pmr(n, pci_dev);
    }

    return true;
}

static void nvme_init_subnqn(NvmeCtrl *n)
{
    NvmeSubsystem *subsys = n->subsys;
    NvmeIdCtrl *id = &n->id_ctrl;

    if (!subsys) {
        snprintf((char *)id->subnqn, sizeof(id->subnqn),
                 "nqn.2019-08.org.qemu:%s", n->params.serial);
    } else {
        pstrcpy((char *)id->subnqn, sizeof(id->subnqn), (char*)subsys->subnqn);
    }
}

static void nvme_init_ctrl(NvmeCtrl *n, PCIDevice *pci_dev)
{
    NvmeIdCtrl *id = &n->id_ctrl;
    uint8_t *pci_conf = pci_dev->config;
    uint64_t cap = ldq_le_p(&n->bar.cap);
    NvmeSecCtrlEntry *sctrl = nvme_sctrl(n);
    uint32_t ctratt;
    uint16_t oacs;

    memcpy(n->cse.acs, nvme_cse_acs_default, sizeof(n->cse.acs));
    memcpy(n->cse.iocs.nvm, nvme_cse_iocs_nvm_default, sizeof(n->cse.iocs.nvm));
    memcpy(n->cse.iocs.zoned, nvme_cse_iocs_zoned_default,
           sizeof(n->cse.iocs.zoned));

    id->vid = cpu_to_le16(pci_get_word(pci_conf + PCI_VENDOR_ID));
    id->ssvid = cpu_to_le16(pci_get_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID));
    strpadcpy((char *)id->mn, sizeof(id->mn), "QEMU NVMe Ctrl", ' ');
    strpadcpy((char *)id->fr, sizeof(id->fr), QEMU_VERSION, ' ');
    strpadcpy((char *)id->sn, sizeof(id->sn), n->params.serial, ' ');

    id->cntlid = cpu_to_le16(n->cntlid);

    id->oaes = cpu_to_le32(NVME_OAES_NS_ATTR);

    ctratt = NVME_CTRATT_ELBAS;
    if (n->params.ctratt.mem) {
        ctratt |= NVME_CTRATT_MEM;
    }

    id->rab = 6;

    if (n->params.use_intel_id) {
        id->ieee[0] = 0xb3;
        id->ieee[1] = 0x02;
        id->ieee[2] = 0x00;
    } else {
        id->ieee[0] = 0x00;
        id->ieee[1] = 0x54;
        id->ieee[2] = 0x52;
    }

    id->mdts = n->params.mdts;
    id->ver = cpu_to_le32(NVME_SPEC_VER);

    oacs = NVME_OACS_NMS | NVME_OACS_FORMAT | NVME_OACS_DIRECTIVES;

    if (n->params.dbcs) {
        oacs |= NVME_OACS_DBCS;

        n->cse.acs[NVME_ADM_CMD_DBBUF_CONFIG] = NVME_CMD_EFF_CSUPP;
    }

    if (n->params.sriov_max_vfs) {
        oacs |= NVME_OACS_VMS;

        n->cse.acs[NVME_ADM_CMD_VIRT_MNGMT] = NVME_CMD_EFF_CSUPP;
    }

    id->oacs = cpu_to_le16(oacs);

    id->cntrltype = 0x1;

    /*
     * Because the controller always completes the Abort command immediately,
     * there can never be more than one concurrently executing Abort command,
     * so this value is never used for anything. Note that there can easily be
     * many Abort commands in the queues, but they are not considered
     * "executing" until processed by nvme_abort.
     *
     * The specification recommends a value of 3 for Abort Command Limit (four
     * concurrently outstanding Abort commands), so lets use that though it is
     * inconsequential.
     */
    id->acl = 3;
    id->aerl = n->params.aerl;
    id->frmw = (NVME_NUM_FW_SLOTS << 1) | NVME_FRMW_SLOT1_RO;
    id->lpa = NVME_LPA_NS_SMART | NVME_LPA_CSE | NVME_LPA_EXTENDED;

    /* recommended default value (~70 C) */
    id->wctemp = cpu_to_le16(NVME_TEMPERATURE_WARNING);
    id->cctemp = cpu_to_le16(NVME_TEMPERATURE_CRITICAL);

    id->sqes = (NVME_SQES << 4) | NVME_SQES;
    id->cqes = (NVME_CQES << 4) | NVME_CQES;
    id->nn = cpu_to_le32(NVME_MAX_NAMESPACES);
    id->oncs = cpu_to_le16(NVME_ONCS_WRITE_ZEROES | NVME_ONCS_TIMESTAMP |
                           NVME_ONCS_FEATURES | NVME_ONCS_DSM |
                           NVME_ONCS_COMPARE | NVME_ONCS_COPY |
                           NVME_ONCS_NVMCSA | NVME_ONCS_NVMAFC);

    /*
     * NOTE: If this device ever supports a command set that does NOT use 0x0
     * as a Flush-equivalent operation, support for the broadcast NSID in Flush
     * should probably be removed.
     *
     * See comment in nvme_io_cmd.
     */
    id->vwc = NVME_VWC_NSID_BROADCAST_SUPPORT | NVME_VWC_PRESENT;

    id->ocfs = cpu_to_le16(NVME_OCFS_COPY_FORMAT_0 | NVME_OCFS_COPY_FORMAT_1 |
                            NVME_OCFS_COPY_FORMAT_2 | NVME_OCFS_COPY_FORMAT_3);
    id->sgls = cpu_to_le32(NVME_CTRL_SGLS_SUPPORT_NO_ALIGN |
                           NVME_CTRL_SGLS_MPTR_SGL);

    nvme_init_subnqn(n);

    id->psd[0].mp = cpu_to_le16(0x9c4);
    id->psd[0].enlat = cpu_to_le32(0x10);
    id->psd[0].exlat = cpu_to_le32(0x4);

    id->cmic |= NVME_CMIC_MULTI_CTRL;
    ctratt |= NVME_CTRATT_ENDGRPS;

    id->endgidmax = cpu_to_le16(0x1);

    if (n->subsys->endgrp.fdp.enabled) {
        ctratt |= NVME_CTRATT_FDPS;
    }

    id->ctratt = cpu_to_le32(ctratt);

    NVME_CAP_SET_MQES(cap, n->params.mqes);
    NVME_CAP_SET_CQR(cap, 1);
    NVME_CAP_SET_TO(cap, 0xf);
    NVME_CAP_SET_CSS(cap, NVME_CAP_CSS_NCSS);
    NVME_CAP_SET_CSS(cap, NVME_CAP_CSS_IOCSS);
    NVME_CAP_SET_MPSMAX(cap, 4);
    NVME_CAP_SET_CMBS(cap, n->params.cmb_size_mb ? 1 : 0);
    NVME_CAP_SET_PMRS(cap, n->pmr.dev ? 1 : 0);
    stq_le_p(&n->bar.cap, cap);

    stl_le_p(&n->bar.vs, NVME_SPEC_VER);
    n->bar.intmc = n->bar.intms = 0;

    if (pci_is_vf(pci_dev) && !sctrl->scs) {
        stl_le_p(&n->bar.csts, NVME_CSTS_FAILED);
    }
}

static int nvme_init_subsys(NvmeCtrl *n, Error **errp)
{
    int cntlid;

    if (!n->subsys) {
        DeviceState *dev = qdev_new(TYPE_NVME_SUBSYS);

        qdev_prop_set_string(dev, "nqn", n->params.serial);

        if (!qdev_realize(dev, NULL, errp)) {
            return -1;
        }

        n->subsys = NVME_SUBSYS(dev);
    }

    cntlid = nvme_subsys_register_ctrl(n, errp);
    if (cntlid < 0) {
        return -1;
    }

    n->cntlid = cntlid;

    return 0;
}

void nvme_attach_ns(NvmeCtrl *n, NvmeNamespace *ns)
{
    uint32_t nsid = ns->params.nsid;
    assert(nsid && nsid <= NVME_MAX_NAMESPACES);

    n->namespaces[nsid] = ns;
    ns->attached++;
}

static void nvme_realize(PCIDevice *pci_dev, Error **errp)
{
    NvmeCtrl *n = NVME(pci_dev);
    DeviceState *dev = DEVICE(pci_dev);
    NvmeNamespace *ns;
    NvmeCtrl *pn = NVME(pcie_sriov_get_pf(pci_dev));

    if (pci_is_vf(pci_dev)) {
        /*
         * VFs derive settings from the parent. PF's lifespan exceeds
         * that of VF's.
         */
        memcpy(&n->params, &pn->params, sizeof(NvmeParams));

        /*
         * Set PF's serial value to a new string memory to prevent 'serial'
         * property object release of PF when a VF is removed from the system.
         */
        n->params.serial = g_strdup(pn->params.serial);
        n->subsys = pn->subsys;

        /*
         * Assigning this link (strong link) causes an `object_unref` later in
         * `object_release_link_property`. Increment the refcount to balance
         * this out.
         */
        object_ref(OBJECT(pn->subsys));
    }

    if (!nvme_check_params(n, errp)) {
        return;
    }

    qbus_init(&n->bus, sizeof(NvmeBus), TYPE_NVME_BUS, dev, dev->id);

    if (nvme_init_subsys(n, errp)) {
        return;
    }
    nvme_init_state(n);
    if (!nvme_init_pci(n, pci_dev, errp)) {
        return;
    }
    nvme_init_ctrl(n, pci_dev);

    /* setup a namespace if the controller drive property was given */
    if (n->namespace.blkconf.blk) {
        ns = &n->namespace;
        ns->params.nsid = 1;
        ns->ctrl = n;

        if (nvme_ns_setup(ns, errp)) {
            return;
        }

        n->subsys->namespaces[ns->params.nsid] = ns;
    }
}

static void nvme_exit(PCIDevice *pci_dev)
{
    NvmeCtrl *n = NVME(pci_dev);
    NvmeNamespace *ns;
    int i;

    nvme_ctrl_reset(n, NVME_RESET_FUNCTION);

    for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
        ns = nvme_ns(n, i);
        if (ns) {
            ns->attached--;
        }
    }

    nvme_subsys_unregister_ctrl(n->subsys, n);

    g_free(n->cq);
    g_free(n->sq);
    g_free(n->aer_reqs);

    if (n->params.cmb_size_mb) {
        g_free(n->cmb.buf);
    }

    if (pci_dev->doe_spdm.spdm_socket > 0) {
        spdm_socket_close(pci_dev->doe_spdm.spdm_socket,
                          SPDM_SOCKET_TRANSPORT_TYPE_PCI_DOE);
    }

    if (n->pmr.dev) {
        host_memory_backend_set_mapped(n->pmr.dev, false);
    }

    if (!pci_is_vf(pci_dev) && n->params.sriov_max_vfs) {
        pcie_sriov_pf_exit(pci_dev);
    }

    if (n->params.msix_exclusive_bar && !pci_is_vf(pci_dev)) {
        msix_uninit_exclusive_bar(pci_dev);
    } else {
        msix_uninit(pci_dev, &n->bar0, &n->bar0);
    }

    memory_region_del_subregion(&n->bar0, &n->iomem);
}

static const Property nvme_props[] = {
    DEFINE_BLOCK_PROPERTIES(NvmeCtrl, namespace.blkconf),
    DEFINE_PROP_LINK("pmrdev", NvmeCtrl, pmr.dev, TYPE_MEMORY_BACKEND,
                     HostMemoryBackend *),
    DEFINE_PROP_LINK("subsys", NvmeCtrl, subsys, TYPE_NVME_SUBSYS,
                     NvmeSubsystem *),
    DEFINE_PROP_STRING("serial", NvmeCtrl, params.serial),
    DEFINE_PROP_UINT32("cmb_size_mb", NvmeCtrl, params.cmb_size_mb, 0),
    DEFINE_PROP_UINT32("num_queues", NvmeCtrl, params.num_queues, 0),
    DEFINE_PROP_UINT32("max_ioqpairs", NvmeCtrl, params.max_ioqpairs, 64),
    DEFINE_PROP_UINT16("msix_qsize", NvmeCtrl, params.msix_qsize, 65),
    DEFINE_PROP_UINT8("aerl", NvmeCtrl, params.aerl, 3),
    DEFINE_PROP_UINT32("aer_max_queued", NvmeCtrl, params.aer_max_queued, 64),
    DEFINE_PROP_UINT8("mdts", NvmeCtrl, params.mdts, 7),
    DEFINE_PROP_UINT8("vsl", NvmeCtrl, params.vsl, 7),
    DEFINE_PROP_BOOL("use-intel-id", NvmeCtrl, params.use_intel_id, false),
    DEFINE_PROP_BOOL("legacy-cmb", NvmeCtrl, params.legacy_cmb, false),
    DEFINE_PROP_BOOL("ioeventfd", NvmeCtrl, params.ioeventfd, false),
    DEFINE_PROP_BOOL("dbcs", NvmeCtrl, params.dbcs, true),
    DEFINE_PROP_UINT8("zoned.zasl", NvmeCtrl, params.zasl, 0),
    DEFINE_PROP_BOOL("zoned.auto_transition", NvmeCtrl,
                     params.auto_transition_zones, true),
    DEFINE_PROP_UINT16("sriov_max_vfs", NvmeCtrl, params.sriov_max_vfs, 0),
    DEFINE_PROP_UINT16("sriov_vq_flexible", NvmeCtrl,
                       params.sriov_vq_flexible, 0),
    DEFINE_PROP_UINT16("sriov_vi_flexible", NvmeCtrl,
                       params.sriov_vi_flexible, 0),
    DEFINE_PROP_UINT32("sriov_max_vi_per_vf", NvmeCtrl,
                       params.sriov_max_vi_per_vf, 0),
    DEFINE_PROP_UINT32("sriov_max_vq_per_vf", NvmeCtrl,
                       params.sriov_max_vq_per_vf, 0),
    DEFINE_PROP_BOOL("msix-exclusive-bar", NvmeCtrl, params.msix_exclusive_bar,
                     false),
    DEFINE_PROP_UINT16("mqes", NvmeCtrl, params.mqes, 0x7ff),
    DEFINE_PROP_UINT16("spdm_port", PCIDevice, spdm_port, 0),
    DEFINE_PROP_BOOL("ctratt.mem", NvmeCtrl, params.ctratt.mem, false),
    DEFINE_PROP_BOOL("atomic.dn", NvmeCtrl, params.atomic_dn, 0),
    DEFINE_PROP_UINT16("atomic.awun", NvmeCtrl, params.atomic_awun, 0),
    DEFINE_PROP_UINT16("atomic.awupf", NvmeCtrl, params.atomic_awupf, 0),
    DEFINE_PROP_BOOL("ocp", NvmeCtrl, params.ocp, false),
};

static void nvme_get_smart_warning(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    NvmeCtrl *n = NVME(obj);
    uint8_t value = n->smart_critical_warning;

    visit_type_uint8(v, name, &value, errp);
}

static void nvme_set_smart_warning(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    NvmeCtrl *n = NVME(obj);
    uint8_t value, old_value, cap = 0, index, event;

    if (!visit_type_uint8(v, name, &value, errp)) {
        return;
    }

    cap = NVME_SMART_SPARE | NVME_SMART_TEMPERATURE | NVME_SMART_RELIABILITY
          | NVME_SMART_MEDIA_READ_ONLY | NVME_SMART_FAILED_VOLATILE_MEDIA;
    if (NVME_CAP_PMRS(ldq_le_p(&n->bar.cap))) {
        cap |= NVME_SMART_PMR_UNRELIABLE;
    }

    if ((value & cap) != value) {
        error_setg(errp, "unsupported smart critical warning bits: 0x%x",
                   value & ~cap);
        return;
    }

    old_value = n->smart_critical_warning;
    n->smart_critical_warning = value;

    /* only inject new bits of smart critical warning */
    for (index = 0; index < NVME_SMART_WARN_MAX; index++) {
        event = 1 << index;
        if (value & ~old_value & event)
            nvme_smart_event(n, event);
    }
}

static void nvme_pci_reset(DeviceState *qdev)
{
    PCIDevice *pci_dev = PCI_DEVICE(qdev);
    NvmeCtrl *n = NVME(pci_dev);

    trace_pci_nvme_pci_reset();
    nvme_ctrl_reset(n, NVME_RESET_FUNCTION);
}

static void nvme_sriov_post_write_config(PCIDevice *dev, uint16_t old_num_vfs)
{
    NvmeCtrl *n = NVME(dev);
    NvmeSecCtrlEntry *sctrl;
    int i;

    for (i = pcie_sriov_num_vfs(dev); i < old_num_vfs; i++) {
        sctrl = &n->sec_ctrl_list[i];
        nvme_virt_set_state(n, le16_to_cpu(sctrl->scid), false);
    }
}

static void nvme_pci_write_config(PCIDevice *dev, uint32_t address,
                                  uint32_t val, int len)
{
    uint16_t old_num_vfs = pcie_sriov_num_vfs(dev);

    if (pcie_find_capability(dev, PCI_EXT_CAP_ID_DOE)) {
        pcie_doe_write_config(&dev->doe_spdm, address, val, len);
    }
    pci_default_write_config(dev, address, val, len);
    pcie_cap_flr_write_config(dev, address, val, len);
    nvme_sriov_post_write_config(dev, old_num_vfs);
}

static uint32_t nvme_pci_read_config(PCIDevice *dev, uint32_t address, int len)
{
    uint32_t val;
    if (dev->spdm_port && pcie_find_capability(dev, PCI_EXT_CAP_ID_DOE)) {
        if (pcie_doe_read_config(&dev->doe_spdm, address, len, &val)) {
            return val;
        }
    }
    return pci_default_read_config(dev, address, len);
}

static const VMStateDescription nvme_vmstate = {
    .name = "nvme",
    .unmigratable = 1,
};

static void nvme_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = nvme_realize;
    pc->config_write = nvme_pci_write_config;
    pc->config_read = nvme_pci_read_config;
    pc->exit = nvme_exit;
    pc->class_id = PCI_CLASS_STORAGE_EXPRESS;
    pc->revision = 2;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Non-Volatile Memory Express";
    device_class_set_props(dc, nvme_props);
    dc->vmsd = &nvme_vmstate;
    device_class_set_legacy_reset(dc, nvme_pci_reset);
}

static void nvme_instance_init(Object *obj)
{
    NvmeCtrl *n = NVME(obj);

    device_add_bootindex_property(obj, &n->namespace.blkconf.bootindex,
                                  "bootindex", "/namespace@1,0",
                                  DEVICE(obj));

    object_property_add(obj, "smart_critical_warning", "uint8",
                        nvme_get_smart_warning,
                        nvme_set_smart_warning, NULL, NULL);
}

static const TypeInfo nvme_info = {
    .name          = TYPE_NVME,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NvmeCtrl),
    .instance_init = nvme_instance_init,
    .class_init    = nvme_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static const TypeInfo nvme_bus_info = {
    .name = TYPE_NVME_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(NvmeBus),
};

static void nvme_register_types(void)
{
    type_register_static(&nvme_info);
    type_register_static(&nvme_bus_info);
}

type_init(nvme_register_types)
