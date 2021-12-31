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
 * NVMe subsystem use thes format from the NVMe specifications in the comments
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
 *              subsys=<subsys_id>
 *      -device nvme-ns,drive=<drive_id>,bus=<bus_name>,nsid=<nsid>,\
 *              zoned=<true|false[optional]>, \
 *              subsys=<subsys_id>,detached=<true|false[optional]>
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
 *   Namesapce Sharing Capabilities).
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
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "sysemu/hostmem.h"
#include "hw/pci/msix.h"
#include "migration/vmstate.h"

#include "nvme.h"
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
    [NVME_COMMAND_SET_PROFILE]      = true,
};

static const uint32_t nvme_feature_cap[NVME_FID_MAX] = {
    [NVME_TEMPERATURE_THRESHOLD]    = NVME_FEAT_CAP_CHANGE,
    [NVME_ERROR_RECOVERY]           = NVME_FEAT_CAP_CHANGE | NVME_FEAT_CAP_NS,
    [NVME_VOLATILE_WRITE_CACHE]     = NVME_FEAT_CAP_CHANGE,
    [NVME_NUMBER_OF_QUEUES]         = NVME_FEAT_CAP_CHANGE,
    [NVME_ASYNCHRONOUS_EVENT_CONF]  = NVME_FEAT_CAP_CHANGE,
    [NVME_TIMESTAMP]                = NVME_FEAT_CAP_CHANGE,
    [NVME_COMMAND_SET_PROFILE]      = NVME_FEAT_CAP_CHANGE,
};

static const uint32_t nvme_cse_acs[256] = {
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
    [NVME_ADM_CMD_NS_ATTACHMENT]    = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_NIC,
    [NVME_ADM_CMD_FORMAT_NVM]       = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
};

static const uint32_t nvme_cse_iocs_none[256];

static const uint32_t nvme_cse_iocs_nvm[256] = {
    [NVME_CMD_FLUSH]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE_ZEROES]         = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_READ]                 = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_DSM]                  = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_VERIFY]               = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_COPY]                 = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_COMPARE]              = NVME_CMD_EFF_CSUPP,
};

static const uint32_t nvme_cse_iocs_zoned[256] = {
    [NVME_CMD_FLUSH]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE_ZEROES]         = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_READ]                 = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_DSM]                  = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_VERIFY]               = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_COPY]                 = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_COMPARE]              = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_ZONE_APPEND]          = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_ZONE_MGMT_SEND]       = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_ZONE_MGMT_RECV]       = NVME_CMD_EFF_CSUPP,
};

static void nvme_process_sq(void *opaque);

static uint16_t nvme_sqid(NvmeRequest *req)
{
    return le16_to_cpu(req->sq->sqid);
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

/*
 * Check if we can open a zone without exceeding open/active limits.
 * AOR stands for "Active and Open Resources" (see TP 4053 section 2.5).
 */
static int nvme_aor_check(NvmeNamespace *ns, uint32_t act, uint32_t opn)
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

    return NVME_SUCCESS;
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

    return pci_dma_read(&n->parent_obj, addr, buf, size);
}

static int nvme_addr_write(NvmeCtrl *n, hwaddr addr, void *buf, int size)
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

    return pci_dma_write(&n->parent_obj, addr, buf, size);
}

static bool nvme_nsid_valid(NvmeCtrl *n, uint32_t nsid)
{
    return nsid &&
        (nsid == NVME_NSID_BROADCAST || nsid <= NVME_MAX_NAMESPACES);
}

static int nvme_check_sqid(NvmeCtrl *n, uint16_t sqid)
{
    return sqid < n->params.max_ioqpairs + 1 && n->sq[sqid] != NULL ? 0 : -1;
}

static int nvme_check_cqid(NvmeCtrl *n, uint16_t cqid)
{
    return cqid < n->params.max_ioqpairs + 1 && n->cq[cqid] != NULL ? 0 : -1;
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
    uint32_t intms = ldl_le_p(&n->bar.intms);

    if (msix_enabled(&(n->parent_obj))) {
        return;
    }
    if (~intms & n->irq_status) {
        pci_irq_assert(&n->parent_obj);
    } else {
        pci_irq_deassert(&n->parent_obj);
    }
}

static void nvme_irq_assert(NvmeCtrl *n, NvmeCQueue *cq)
{
    if (cq->irq_enabled) {
        if (msix_enabled(&(n->parent_obj))) {
            trace_pci_nvme_irq_msix(cq->vector);
            msix_notify(&(n->parent_obj), cq->vector);
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
        if (msix_enabled(&(n->parent_obj))) {
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
        pci_dma_sglist_init(&sg->qsg, &n->parent_obj, 0);
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
 * When metadata is transfered as extended LBAs, the DPTR mapped into `sg`
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
            uint64_t prp_list[n->max_prp_ents];
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
        case NVME_SGL_DESCR_TYPE_BIT_BUCKET:
            if (cmd->opcode == NVME_CMD_WRITE) {
                continue;
            }
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

        if (type == NVME_SGL_DESCR_TYPE_BIT_BUCKET) {
            goto next;
        }

        addr = le64_to_cpu(segment[i].addr);

        if (UINT64_MAX - addr < dlen) {
            return NVME_DATA_SGL_LEN_INVALID | NVME_DNR;
        }

        status = nvme_map_addr(n, sg, addr, trans_len);
        if (status) {
            return status;
        }

next:
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
    const int SEG_CHUNK_SIZE = 256;

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
        if ((!seg_len || seg_len & 0xf) &&
            (NVME_SGL_TYPE(sgld->type) != NVME_SGL_DESCR_TYPE_BIT_BUCKET)) {
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
         * If the segment ends with a Data Block or Bit Bucket Descriptor Type,
         * then we are done.
         */
        switch (NVME_SGL_TYPE(last_sgld->type)) {
        case NVME_SGL_DESCR_TYPE_DATA_BLOCK:
        case NVME_SGL_DESCR_TYPE_BIT_BUCKET:
            status = nvme_map_sgl_data(n, sg, segment, nsgld, &len, cmd);
            if (status) {
                goto unmap;
            }

            goto out;

        default:
            break;
        }

        /*
         * If the last descriptor was not a Data Block or Bit Bucket, then the
         * current segment must not be a Last Segment.
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

    if (nvme_ns_ext(ns) && !(pi && pract && ns->lbaf.ms == 8)) {
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

static uint16_t nvme_tx(NvmeCtrl *n, NvmeSg *sg, uint8_t *ptr, uint32_t len,
                        NvmeTxDirection dir)
{
    assert(sg->flags & NVME_SG_ALLOC);

    if (sg->flags & NVME_SG_DMA) {
        const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
        uint64_t residual;

        if (dir == NVME_TX_DIRECTION_TO_DEVICE) {
            residual = dma_buf_write(ptr, len, &sg->qsg, attrs);
        } else {
            residual = dma_buf_read(ptr, len, &sg->qsg, attrs);
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

static inline uint16_t nvme_c2h(NvmeCtrl *n, uint8_t *ptr, uint32_t len,
                                NvmeRequest *req)
{
    uint16_t status;

    status = nvme_map_dptr(n, &req->sg, len, &req->cmd);
    if (status) {
        return status;
    }

    return nvme_tx(n, &req->sg, ptr, len, NVME_TX_DIRECTION_FROM_DEVICE);
}

static inline uint16_t nvme_h2c(NvmeCtrl *n, uint8_t *ptr, uint32_t len,
                                NvmeRequest *req)
{
    uint16_t status;

    status = nvme_map_dptr(n, &req->sg, len, &req->cmd);
    if (status) {
        return status;
    }

    return nvme_tx(n, &req->sg, ptr, len, NVME_TX_DIRECTION_TO_DEVICE);
}

uint16_t nvme_bounce_data(NvmeCtrl *n, uint8_t *ptr, uint32_t len,
                          NvmeTxDirection dir, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    bool pi = !!NVME_ID_NS_DPS_TYPE(ns->id_ns.dps);
    bool pract = !!(le16_to_cpu(rw->control) & NVME_RW_PRINFO_PRACT);

    if (nvme_ns_ext(ns) && !(pi && pract && ns->lbaf.ms == 8)) {
        return nvme_tx_interleaved(n, &req->sg, ptr, len, ns->lbasz,
                                   ns->lbaf.ms, 0, dir);
    }

    return nvme_tx(n, &req->sg, ptr, len, dir);
}

uint16_t nvme_bounce_mdata(NvmeCtrl *n, uint8_t *ptr, uint32_t len,
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
                                 BlockCompletionFunc *cb, NvmeRequest *req)
{
    assert(req->sg.flags & NVME_SG_ALLOC);

    if (req->sg.flags & NVME_SG_DMA) {
        req->aiocb = dma_blk_read(blk, &req->sg.qsg, offset, BDRV_SECTOR_SIZE,
                                  cb, req);
    } else {
        req->aiocb = blk_aio_preadv(blk, offset, &req->sg.iov, 0, cb, req);
    }
}

static inline void nvme_blk_write(BlockBackend *blk, int64_t offset,
                                  BlockCompletionFunc *cb, NvmeRequest *req)
{
    assert(req->sg.flags & NVME_SG_ALLOC);

    if (req->sg.flags & NVME_SG_DMA) {
        req->aiocb = dma_blk_write(blk, &req->sg.qsg, offset, BDRV_SECTOR_SIZE,
                                   cb, req);
    } else {
        req->aiocb = blk_aio_pwritev(blk, offset, &req->sg.iov, 0, cb, req);
    }
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

        if (nvme_cq_full(cq)) {
            break;
        }

        sq = req->sq;
        req->cqe.status = cpu_to_le16((req->status << 1) | cq->phase);
        req->cqe.sq_id = cpu_to_le16(sq->sqid);
        req->cqe.sq_head = cpu_to_le16(sq->head);
        addr = cq->dma_addr + cq->tail * n->cqe_size;
        ret = pci_dma_write(&n->parent_obj, addr, (void *)&req->cqe,
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
    timer_mod(cq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 500);
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
    n->aer_mask &= ~(1 << event_type);
    if (!QTAILQ_EMPTY(&n->aer_queue)) {
        nvme_process_aers(n);
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

static void nvme_aio_err(NvmeRequest *req, int ret)
{
    uint16_t status = NVME_SUCCESS;
    Error *local_err = NULL;

    switch (req->cmd.opcode) {
    case NVME_CMD_READ:
        status = NVME_UNRECOVERED_READ;
        break;
    case NVME_CMD_FLUSH:
    case NVME_CMD_WRITE:
    case NVME_CMD_WRITE_ZEROES:
    case NVME_CMD_ZONE_APPEND:
        status = NVME_WRITE_FAULT;
        break;
    default:
        status = NVME_INTERNAL_DEV_ERROR;
        break;
    }

    trace_pci_nvme_err_aio(nvme_cid(req), strerror(-ret), status);

    error_setg_errno(&local_err, -ret, "aio failed");
    error_report_err(local_err);

    /*
     * Set the command status code to the first encountered error but allow a
     * subsequent Internal Device Error to trump it.
     */
    if (req->status && status != NVME_INTERNAL_DEV_ERROR) {
        return;
    }

    req->status = status;
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
        assert(false);
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

    if (unlikely(slba != zone->w_ptr)) {
        trace_pci_nvme_err_write_not_at_wp(slba, zone->d.zslba, zone->w_ptr);
        return NVME_ZONE_INVALID_WRITE;
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
        assert(false);
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
        status = nvme_aor_check(ns, act, 1);
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

static inline uint16_t nvme_zrm_open(NvmeCtrl *n, NvmeNamespace *ns,
                                     NvmeZone *zone)
{
    return nvme_zrm_open_flags(n, ns, zone, 0);
}

static void nvme_advance_zone_wp(NvmeNamespace *ns, NvmeZone *zone,
                                 uint32_t nlb)
{
    zone->d.wp += nlb;

    if (zone->d.wp == nvme_zone_wr_boundary(zone)) {
        nvme_zrm_finish(ns, zone);
    }
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

    nvme_advance_zone_wp(ns, zone, nlb);
}

static inline bool nvme_is_write(NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;

    return rw->opcode == NVME_CMD_WRITE ||
           rw->opcode == NVME_CMD_ZONE_APPEND ||
           rw->opcode == NVME_CMD_WRITE_ZEROES;
}

static AioContext *nvme_get_aio_context(BlockAIOCB *acb)
{
    return qemu_get_aio_context();
}

static void nvme_misc_cb(void *opaque, int ret)
{
    NvmeRequest *req = opaque;

    trace_pci_nvme_misc_cb(nvme_cid(req));

    if (ret) {
        nvme_aio_err(req, ret);
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
        block_acct_failed(stats, acct);
        nvme_aio_err(req, ret);
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
                return nvme_blk_read(blk, offset, nvme_rw_complete_cb, req);
            }

            return nvme_blk_write(blk, offset, nvme_rw_complete_cb, req);
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
    uint32_t reftag = le32_to_cpu(rw->reftag);
    uint16_t status;

    trace_pci_nvme_verify_cb(nvme_cid(req), prinfo, apptag, appmask, reftag);

    if (ret) {
        block_acct_failed(stats, acct);
        nvme_aio_err(req, ret);
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
    uint32_t reftag = le32_to_cpu(rw->reftag);
    struct nvme_compare_ctx *ctx = req->opaque;
    g_autofree uint8_t *buf = NULL;
    BlockBackend *blk = ns->blkconf.blk;
    BlockAcctCookie *acct = &req->acct;
    BlockAcctStats *stats = blk_get_stats(blk);
    uint16_t status = NVME_SUCCESS;

    trace_pci_nvme_compare_mdata_cb(nvme_cid(req));

    if (ret) {
        block_acct_failed(stats, acct);
        nvme_aio_err(req, ret);
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
            pil = ns->lbaf.ms - sizeof(NvmeDifTuple);
        }

        for (bufp = buf; mbufp < end; bufp += ns->lbaf.ms, mbufp += ns->lbaf.ms) {
            if (memcmp(bufp + pil, mbufp + pil, ns->lbaf.ms - pil)) {
                req->status = NVME_CMP_FAILURE;
                goto out;
            }
        }

        goto out;
    }

    if (memcmp(buf, ctx->mdata.bounce, ctx->mdata.iov.size)) {
        req->status = NVME_CMP_FAILURE;
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
        nvme_aio_err(req, ret);
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
        req->status = NVME_CMP_FAILURE;
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
    QEMUBH *bh;
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
         * the command ran to completion and nvme_dsm_bh is scheduled to run.
         */
        assert(iocb->idx == iocb->nr);
    }
}

static const AIOCBInfo nvme_dsm_aiocb_info = {
    .aiocb_size   = sizeof(NvmeDSMAIOCB),
    .cancel_async = nvme_dsm_cancel,
};

static void nvme_dsm_bh(void *opaque)
{
    NvmeDSMAIOCB *iocb = opaque;

    iocb->common.cb(iocb->common.opaque, iocb->ret);

    qemu_bh_delete(iocb->bh);
    iocb->bh = NULL;
    qemu_aio_unref(iocb);
}

static void nvme_dsm_cb(void *opaque, int ret);

static void nvme_dsm_md_cb(void *opaque, int ret)
{
    NvmeDSMAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    NvmeDsmRange *range;
    uint64_t slba;
    uint32_t nlb;

    if (ret < 0) {
        iocb->ret = ret;
        goto done;
    }

    if (!ns->lbaf.ms) {
        nvme_dsm_cb(iocb, 0);
        return;
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
            iocb->ret = ret;
            goto done;
        }

        nvme_dsm_cb(iocb, 0);
    }

    iocb->aiocb = blk_aio_pwrite_zeroes(ns->blkconf.blk, nvme_moff(ns, slba),
                                        nvme_m2b(ns, nlb), BDRV_REQ_MAY_UNMAP,
                                        nvme_dsm_cb, iocb);
    return;

done:
    iocb->aiocb = NULL;
    qemu_bh_schedule(iocb->bh);
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

    if (ret < 0) {
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
    qemu_bh_schedule(iocb->bh);
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
        iocb->bh = qemu_bh_new(nvme_dsm_bh, iocb);
        iocb->ret = 0;
        iocb->range = g_new(NvmeDsmRange, nr);
        iocb->nr = nr;
        iocb->idx = 0;

        status = nvme_h2c(n, (uint8_t *)iocb->range, sizeof(NvmeDsmRange) * nr,
                          req);
        if (status) {
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

    if (len > n->page_size << n->params.vsl) {
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
    QEMUBH *bh;
    int ret;

    NvmeCopySourceRange *ranges;
    int nr;
    int idx;

    uint8_t *bounce;
    QEMUIOVector iov;
    struct {
        BlockAcctCookie read;
        BlockAcctCookie write;
    } acct;

    uint32_t reftag;
    uint64_t slba;

    NvmeZone *zone;
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

static void nvme_copy_bh(void *opaque)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    BlockAcctStats *stats = blk_get_stats(ns->blkconf.blk);

    if (iocb->idx != iocb->nr) {
        req->cqe.result = cpu_to_le32(iocb->idx);
    }

    qemu_iovec_destroy(&iocb->iov);
    g_free(iocb->bounce);

    qemu_bh_delete(iocb->bh);
    iocb->bh = NULL;

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

static void nvme_copy_cb(void *opaque, int ret);

static void nvme_copy_out_completed_cb(void *opaque, int ret)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    NvmeCopySourceRange *range = &iocb->ranges[iocb->idx];
    uint32_t nlb = le32_to_cpu(range->nlb) + 1;

    if (ret < 0) {
        iocb->ret = ret;
        goto out;
    } else if (iocb->ret < 0) {
        goto out;
    }

    if (ns->params.zoned) {
        nvme_advance_zone_wp(ns, iocb->zone, nlb);
    }

    iocb->idx++;
    iocb->slba += nlb;
out:
    nvme_copy_cb(iocb, iocb->ret);
}

static void nvme_copy_out_cb(void *opaque, int ret)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    NvmeCopySourceRange *range;
    uint32_t nlb;
    size_t mlen;
    uint8_t *mbounce;

    if (ret < 0) {
        iocb->ret = ret;
        goto out;
    } else if (iocb->ret < 0) {
        goto out;
    }

    if (!ns->lbaf.ms) {
        nvme_copy_out_completed_cb(iocb, 0);
        return;
    }

    range = &iocb->ranges[iocb->idx];
    nlb = le32_to_cpu(range->nlb) + 1;

    mlen = nvme_m2b(ns, nlb);
    mbounce = iocb->bounce + nvme_l2b(ns, nlb);

    qemu_iovec_reset(&iocb->iov);
    qemu_iovec_add(&iocb->iov, mbounce, mlen);

    iocb->aiocb = blk_aio_pwritev(ns->blkconf.blk, nvme_moff(ns, iocb->slba),
                                  &iocb->iov, 0, nvme_copy_out_completed_cb,
                                  iocb);

    return;

out:
    nvme_copy_cb(iocb, ret);
}

static void nvme_copy_in_completed_cb(void *opaque, int ret)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    NvmeCopySourceRange *range;
    uint32_t nlb;
    size_t len;
    uint16_t status;

    if (ret < 0) {
        iocb->ret = ret;
        goto out;
    } else if (iocb->ret < 0) {
        goto out;
    }

    range = &iocb->ranges[iocb->idx];
    nlb = le32_to_cpu(range->nlb) + 1;
    len = nvme_l2b(ns, nlb);

    trace_pci_nvme_copy_out(iocb->slba, nlb);

    if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
        NvmeCopyCmd *copy = (NvmeCopyCmd *)&req->cmd;

        uint16_t prinfor = ((copy->control[0] >> 4) & 0xf);
        uint16_t prinfow = ((copy->control[2] >> 2) & 0xf);

        uint16_t apptag = le16_to_cpu(range->apptag);
        uint16_t appmask = le16_to_cpu(range->appmask);
        uint32_t reftag = le32_to_cpu(range->reftag);

        uint64_t slba = le64_to_cpu(range->slba);
        size_t mlen = nvme_m2b(ns, nlb);
        uint8_t *mbounce = iocb->bounce + nvme_l2b(ns, nlb);

        status = nvme_dif_check(ns, iocb->bounce, len, mbounce, mlen, prinfor,
                                slba, apptag, appmask, &reftag);
        if (status) {
            goto invalid;
        }

        apptag = le16_to_cpu(copy->apptag);
        appmask = le16_to_cpu(copy->appmask);

        if (prinfow & NVME_PRINFO_PRACT) {
            status = nvme_check_prinfo(ns, prinfow, iocb->slba, iocb->reftag);
            if (status) {
                goto invalid;
            }

            nvme_dif_pract_generate_dif(ns, iocb->bounce, len, mbounce, mlen,
                                        apptag, &iocb->reftag);
        } else {
            status = nvme_dif_check(ns, iocb->bounce, len, mbounce, mlen,
                                    prinfow, iocb->slba, apptag, appmask,
                                    &iocb->reftag);
            if (status) {
                goto invalid;
            }
        }
    }

    status = nvme_check_bounds(ns, iocb->slba, nlb);
    if (status) {
        goto invalid;
    }

    if (ns->params.zoned) {
        status = nvme_check_zone_write(ns, iocb->zone, iocb->slba, nlb);
        if (status) {
            goto invalid;
        }

        iocb->zone->w_ptr += nlb;
    }

    qemu_iovec_reset(&iocb->iov);
    qemu_iovec_add(&iocb->iov, iocb->bounce, len);

    iocb->aiocb = blk_aio_pwritev(ns->blkconf.blk, nvme_l2b(ns, iocb->slba),
                                  &iocb->iov, 0, nvme_copy_out_cb, iocb);

    return;

invalid:
    req->status = status;
    iocb->aiocb = NULL;
    if (iocb->bh) {
        qemu_bh_schedule(iocb->bh);
    }

    return;

out:
    nvme_copy_cb(iocb, ret);
}

static void nvme_copy_in_cb(void *opaque, int ret)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    NvmeCopySourceRange *range;
    uint64_t slba;
    uint32_t nlb;

    if (ret < 0) {
        iocb->ret = ret;
        goto out;
    } else if (iocb->ret < 0) {
        goto out;
    }

    if (!ns->lbaf.ms) {
        nvme_copy_in_completed_cb(iocb, 0);
        return;
    }

    range = &iocb->ranges[iocb->idx];
    slba = le64_to_cpu(range->slba);
    nlb = le32_to_cpu(range->nlb) + 1;

    qemu_iovec_reset(&iocb->iov);
    qemu_iovec_add(&iocb->iov, iocb->bounce + nvme_l2b(ns, nlb),
                   nvme_m2b(ns, nlb));

    iocb->aiocb = blk_aio_preadv(ns->blkconf.blk, nvme_moff(ns, slba),
                                 &iocb->iov, 0, nvme_copy_in_completed_cb,
                                 iocb);
    return;

out:
    nvme_copy_cb(iocb, iocb->ret);
}

static void nvme_copy_cb(void *opaque, int ret)
{
    NvmeCopyAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    NvmeCopySourceRange *range;
    uint64_t slba;
    uint32_t nlb;
    size_t len;
    uint16_t status;

    if (ret < 0) {
        iocb->ret = ret;
        goto done;
    } else if (iocb->ret < 0) {
        goto done;
    }

    if (iocb->idx == iocb->nr) {
        goto done;
    }

    range = &iocb->ranges[iocb->idx];
    slba = le64_to_cpu(range->slba);
    nlb = le32_to_cpu(range->nlb) + 1;
    len = nvme_l2b(ns, nlb);

    trace_pci_nvme_copy_source_range(slba, nlb);

    if (nlb > le16_to_cpu(ns->id_ns.mssrl)) {
        status = NVME_CMD_SIZE_LIMIT | NVME_DNR;
        goto invalid;
    }

    status = nvme_check_bounds(ns, slba, nlb);
    if (status) {
        goto invalid;
    }

    if (NVME_ERR_REC_DULBE(ns->features.err_rec)) {
        status = nvme_check_dulbe(ns, slba, nlb);
        if (status) {
            goto invalid;
        }
    }

    if (ns->params.zoned) {
        status = nvme_check_zone_read(ns, slba, nlb);
        if (status) {
            goto invalid;
        }
    }

    qemu_iovec_reset(&iocb->iov);
    qemu_iovec_add(&iocb->iov, iocb->bounce, len);

    iocb->aiocb = blk_aio_preadv(ns->blkconf.blk, nvme_l2b(ns, slba),
                                 &iocb->iov, 0, nvme_copy_in_cb, iocb);
    return;

invalid:
    req->status = status;
done:
    iocb->aiocb = NULL;
    if (iocb->bh) {
        qemu_bh_schedule(iocb->bh);
    }
}


static uint16_t nvme_copy(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    NvmeCopyCmd *copy = (NvmeCopyCmd *)&req->cmd;
    NvmeCopyAIOCB *iocb = blk_aio_get(&nvme_copy_aiocb_info, ns->blkconf.blk,
                                      nvme_misc_cb, req);
    uint16_t nr = copy->nr + 1;
    uint8_t format = copy->control[0] & 0xf;
    uint16_t prinfor = ((copy->control[0] >> 4) & 0xf);
    uint16_t prinfow = ((copy->control[2] >> 2) & 0xf);

    uint16_t status;

    trace_pci_nvme_copy(nvme_cid(req), nvme_nsid(ns), nr, format);

    iocb->ranges = NULL;
    iocb->zone = NULL;

    if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps) &&
        ((prinfor & NVME_PRINFO_PRACT) != (prinfow & NVME_PRINFO_PRACT))) {
        status = NVME_INVALID_FIELD | NVME_DNR;
        goto invalid;
    }

    if (!(n->id_ctrl.ocfs & (1 << format))) {
        trace_pci_nvme_err_copy_invalid_format(format);
        status = NVME_INVALID_FIELD | NVME_DNR;
        goto invalid;
    }

    if (nr > ns->id_ns.msrc + 1) {
        status = NVME_CMD_SIZE_LIMIT | NVME_DNR;
        goto invalid;
    }

    iocb->ranges = g_new(NvmeCopySourceRange, nr);

    status = nvme_h2c(n, (uint8_t *)iocb->ranges,
                      sizeof(NvmeCopySourceRange) * nr, req);
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

    iocb->req = req;
    iocb->bh = qemu_bh_new(nvme_copy_bh, iocb);
    iocb->ret = 0;
    iocb->nr = nr;
    iocb->idx = 0;
    iocb->reftag = le32_to_cpu(copy->reftag);
    iocb->bounce = g_malloc_n(le16_to_cpu(ns->id_ns.mssrl),
                              ns->lbasz + ns->lbaf.ms);

    qemu_iovec_init(&iocb->iov, 1);

    block_acct_start(blk_get_stats(ns->blkconf.blk), &iocb->acct.read, 0,
                     BLOCK_ACCT_READ);
    block_acct_start(blk_get_stats(ns->blkconf.blk), &iocb->acct.write, 0,
                     BLOCK_ACCT_WRITE);

    req->aiocb = &iocb->common;
    nvme_copy_cb(iocb, 0);

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

    status = nvme_check_mdts(n, len);
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
    QEMUBH *bh;
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
    }
}

static const AIOCBInfo nvme_flush_aiocb_info = {
    .aiocb_size = sizeof(NvmeFlushAIOCB),
    .cancel_async = nvme_flush_cancel,
    .get_aio_context = nvme_get_aio_context,
};

static void nvme_flush_ns_cb(void *opaque, int ret)
{
    NvmeFlushAIOCB *iocb = opaque;
    NvmeNamespace *ns = iocb->ns;

    if (ret < 0) {
        iocb->ret = ret;
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
    iocb->aiocb = NULL;
    qemu_bh_schedule(iocb->bh);
}

static void nvme_flush_bh(void *opaque)
{
    NvmeFlushAIOCB *iocb = opaque;
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
    qemu_bh_delete(iocb->bh);
    iocb->bh = NULL;

    iocb->common.cb(iocb->common.opaque, iocb->ret);

    qemu_aio_unref(iocb);

    return;
}

static uint16_t nvme_flush(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeFlushAIOCB *iocb;
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);
    uint16_t status;

    iocb = qemu_aio_get(&nvme_flush_aiocb_info, NULL, nvme_misc_cb, req);

    iocb->req = req;
    iocb->bh = qemu_bh_new(nvme_flush_bh, iocb);
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
    qemu_bh_schedule(iocb->bh);

    return NVME_NO_COMPLETE;

out:
    qemu_bh_delete(iocb->bh);
    iocb->bh = NULL;
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

    if (nvme_ns_ext(ns)) {
        mapped_size += nvme_m2b(ns, nlb);

        if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
            bool pract = prinfo & NVME_PRINFO_PRACT;

            if (pract && ns->lbaf.ms == 8) {
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
    nvme_blk_read(blk, data_offset, nvme_rw_cb, req);
    return NVME_NO_COMPLETE;

invalid:
    block_acct_invalid(blk_get_stats(blk), BLOCK_ACCT_READ);
    return status | NVME_DNR;
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

    if (nvme_ns_ext(ns)) {
        mapped_size += nvme_m2b(ns, nlb);

        if (NVME_ID_NS_DPS_TYPE(ns->id_ns.dps)) {
            bool pract = prinfo & NVME_PRINFO_PRACT;

            if (pract && ns->lbaf.ms == 8) {
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

        zone->w_ptr += nlb;
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
        nvme_blk_write(blk, data_offset, nvme_rw_cb, req);
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
    return nvme_zrm_open(nvme_ctrl(req), ns, zone);
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
    QEMUBH *bh;
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

static void nvme_zone_reset_bh(void *opaque)
{
    NvmeZoneResetAIOCB *iocb = opaque;

    iocb->common.cb(iocb->common.opaque, iocb->ret);

    qemu_bh_delete(iocb->bh);
    iocb->bh = NULL;
    qemu_aio_unref(iocb);
}

static void nvme_zone_reset_cb(void *opaque, int ret);

static void nvme_zone_reset_epilogue_cb(void *opaque, int ret)
{
    NvmeZoneResetAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;
    int64_t moff;
    int count;

    if (ret < 0) {
        nvme_zone_reset_cb(iocb, ret);
        return;
    }

    if (!ns->lbaf.ms) {
        nvme_zone_reset_cb(iocb, 0);
        return;
    }

    moff = nvme_moff(ns, iocb->zone->d.zslba);
    count = nvme_m2b(ns, ns->zone_size);

    iocb->aiocb = blk_aio_pwrite_zeroes(ns->blkconf.blk, moff, count,
                                        BDRV_REQ_MAY_UNMAP,
                                        nvme_zone_reset_cb, iocb);
    return;
}

static void nvme_zone_reset_cb(void *opaque, int ret)
{
    NvmeZoneResetAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = req->ns;

    if (ret < 0) {
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
    if (iocb->bh) {
        qemu_bh_schedule(iocb->bh);
    }
}

static uint16_t nvme_zone_mgmt_send(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = (NvmeCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    NvmeZone *zone;
    NvmeZoneResetAIOCB *iocb;
    uint8_t *zd_ext;
    uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint64_t slba = 0;
    uint32_t zone_idx = 0;
    uint16_t status;
    uint8_t action;
    bool all;
    enum NvmeZoneProcessingMask proc_mask = NVME_PROC_CURRENT_ZONE;

    action = dw13 & 0xff;
    all = !!(dw13 & 0x100);

    req->status = NVME_SUCCESS;

    if (!all) {
        status = nvme_get_mgmt_zone_slba_idx(ns, cmd, &slba, &zone_idx);
        if (status) {
            return status;
        }
    }

    zone = &ns->zone_array[zone_idx];
    if (slba != zone->d.zslba) {
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
        iocb->bh = qemu_bh_new(nvme_zone_reset_bh, iocb);
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
    NvmeCmd *cmd = (NvmeCmd *)&req->cmd;
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
    header = (NvmeZoneReportHeader *)buf;
    header->nr_zones = cpu_to_le64(nr_zones);

    buf_p = buf + sizeof(NvmeZoneReportHeader);
    for (; zone_idx < ns->num_zones && max_zones > 0; zone_idx++) {
        zone = &ns->zone_array[zone_idx];
        if (nvme_zone_matches_filter(zrasf, zone)) {
            z = (NvmeZoneDescr *)buf_p;
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

static uint16_t nvme_io_cmd(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns;
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);

    trace_pci_nvme_io_cmd(nvme_cid(req), nsid, nvme_sqid(req),
                          req->cmd.opcode, nvme_io_opc_str(req->cmd.opcode));

    if (!nvme_nsid_valid(n, nsid)) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

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

    ns = nvme_ns(n, nsid);
    if (unlikely(!ns)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (!(ns->iocs[req->cmd.opcode] & NVME_CMD_EFF_CSUPP)) {
        trace_pci_nvme_err_invalid_opc(req->cmd.opcode);
        return NVME_INVALID_OPCODE | NVME_DNR;
    }

    if (ns->status) {
        return ns->status;
    }

    if (NVME_CMD_FLAGS_FUSE(req->cmd.flags)) {
        return NVME_INVALID_FIELD;
    }

    req->ns = ns;

    switch (req->cmd.opcode) {
    case NVME_CMD_WRITE_ZEROES:
        return nvme_write_zeroes(n, req);
    case NVME_CMD_ZONE_APPEND:
        return nvme_zone_append(n, req);
    case NVME_CMD_WRITE:
        return nvme_write(n, req);
    case NVME_CMD_READ:
        return nvme_read(n, req);
    case NVME_CMD_COMPARE:
        return nvme_compare(n, req);
    case NVME_CMD_DSM:
        return nvme_dsm(n, req);
    case NVME_CMD_VERIFY:
        return nvme_verify(n, req);
    case NVME_CMD_COPY:
        return nvme_copy(n, req);
    case NVME_CMD_ZONE_MGMT_SEND:
        return nvme_zone_mgmt_send(n, req);
    case NVME_CMD_ZONE_MGMT_RECV:
        return nvme_zone_mgmt_recv(n, req);
    default:
        assert(false);
    }

    return NVME_INVALID_OPCODE | NVME_DNR;
}

static void nvme_free_sq(NvmeSQueue *sq, NvmeCtrl *n)
{
    n->sq[sq->sqid] = NULL;
    timer_free(sq->timer);
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
    sq->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nvme_process_sq, sq);

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
    if (unlikely(!sqid || sqid > n->params.max_ioqpairs ||
        n->sq[sqid] != NULL)) {
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

    stats->units_read += s->nr_bytes[BLOCK_ACCT_READ] >> BDRV_SECTOR_BITS;
    stats->units_written += s->nr_bytes[BLOCK_ACCT_WRITE] >> BDRV_SECTOR_BITS;
    stats->read_commands += s->nr_ops[BLOCK_ACCT_READ];
    stats->write_commands += s->nr_ops[BLOCK_ACCT_WRITE];
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

    smart.data_units_read[0] = cpu_to_le64(DIV_ROUND_UP(stats.units_read,
                                                        1000));
    smart.data_units_written[0] = cpu_to_le64(DIV_ROUND_UP(stats.units_written,
                                                           1000));
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
    const uint32_t *src_iocs = NULL;
    uint32_t trans_len;

    if (off >= sizeof(log)) {
        trace_pci_nvme_err_invalid_log_page_offset(off, sizeof(log));
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    switch (NVME_CC_CSS(ldl_le_p(&n->bar.cc))) {
    case NVME_CC_CSS_NVM:
        src_iocs = nvme_cse_iocs_nvm;
        /* fall through */
    case NVME_CC_CSS_ADMIN_ONLY:
        break;
    case NVME_CC_CSS_CSI:
        switch (csi) {
        case NVME_CSI_NVM:
            src_iocs = nvme_cse_iocs_nvm;
            break;
        case NVME_CSI_ZONED:
            src_iocs = nvme_cse_iocs_zoned;
            break;
        }
    }

    memcpy(log.acs, nvme_cse_acs, sizeof(nvme_cse_acs));

    if (src_iocs) {
        memcpy(log.iocs, src_iocs, sizeof(log.iocs));
    }

    trans_len = MIN(sizeof(log) - off, buf_len);

    return nvme_c2h(n, ((uint8_t *)&log) + off, trans_len, req);
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
    uint32_t numdl, numdu;
    uint64_t off, lpol, lpou;
    size_t   len;
    uint16_t status;

    numdl = (dw10 >> 16);
    numdu = (dw11 & 0xffff);
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
    case NVME_LOG_CHANGED_NSLIST:
        return nvme_changed_nslist(n, rae, len, off, req);
    case NVME_LOG_CMD_EFFECTS:
        return nvme_cmd_effects(n, csi, len, off, req);
    default:
        trace_pci_nvme_err_invalid_log_page(nvme_cid(req), lid);
        return NVME_INVALID_FIELD | NVME_DNR;
    }
}

static void nvme_free_cq(NvmeCQueue *cq, NvmeCtrl *n)
{
    n->cq[cq->cqid] = NULL;
    timer_free(cq->timer);
    if (msix_enabled(&n->parent_obj)) {
        msix_vector_unuse(&n->parent_obj, cq->vector);
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
    int ret;

    if (msix_enabled(&n->parent_obj)) {
        ret = msix_vector_use(&n->parent_obj, vector);
        assert(ret == 0);
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
    n->cq[cqid] = cq;
    cq->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nvme_post_cqes, cq);
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

    trace_pci_nvme_create_cq(prp1, cqid, vector, qsize, qflags,
                             NVME_CQ_FLAGS_IEN(qflags) != 0);

    if (unlikely(!cqid || cqid > n->params.max_ioqpairs ||
        n->cq[cqid] != NULL)) {
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
    if (unlikely(!msix_enabled(&n->parent_obj) && vector)) {
        trace_pci_nvme_err_invalid_create_cq_vector(vector);
        return NVME_INVALID_IRQ_VECTOR | NVME_DNR;
    }
    if (unlikely(vector >= n->params.msix_qsize)) {
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
        id_nvm->dmrsl = cpu_to_le32(n->dmrsl);
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

    return NVME_INVALID_CMD_SET | NVME_DNR;
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
        return nvme_rpt_empty_id_struct(n, req);
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

    /*
     * If the EUI-64 field is 0 and the NGUID field is 0, the namespace must
     * provide a valid Namespace UUID in the Namespace Identification Descriptor
     * data structure. QEMU does not yet support setting NGUID.
     */
    uuid.hdr.nidt = NVME_NIDT_UUID;
    uuid.hdr.nidl = NVME_NIDL_UUID;
    memcpy(uuid.v, ns->params.uuid.data, NVME_NIDL_UUID);
    memcpy(pos, &uuid, sizeof(uuid));
    pos += sizeof(uuid);

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
    case NVME_ID_CNS_CS_NS:
        return nvme_identify_ns_csi(n, req, true);
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

    req->cqe.result = 1;
    if (nvme_check_sqid(n, sqid)) {
        return NVME_INVALID_FIELD | NVME_DNR;
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

static uint16_t nvme_get_feature(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = &req->cmd;
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    uint32_t nsid = le32_to_cpu(cmd->nsid);
    uint32_t result;
    uint8_t fid = NVME_GETSETFEAT_FID(dw10);
    NvmeGetFeatureSelect sel = NVME_GETFEAT_SELECT(dw10);
    uint16_t iv;
    NvmeNamespace *ns;
    int i;

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
        result = (n->params.max_ioqpairs - 1) |
            ((n->params.max_ioqpairs - 1) << 16);
        trace_pci_nvme_getfeat_numq(result);
        break;
    case NVME_INTERRUPT_VECTOR_CONF:
        iv = dw11 & 0xffff;
        if (iv >= n->params.max_ioqpairs + 1) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        result = iv;
        if (iv == n->admin_cq.vector) {
            result |= NVME_INTVC_NOCOALESCING;
        }
        break;
    default:
        result = nvme_feature_default[fid];
        break;
    }

out:
    req->cqe.result = cpu_to_le32(result);
    return NVME_SUCCESS;
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

static uint16_t nvme_set_feature(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns = NULL;

    NvmeCmd *cmd = &req->cmd;
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint32_t dw11 = le32_to_cpu(cmd->cdw11);
    uint32_t nsid = le32_to_cpu(cmd->nsid);
    uint8_t fid = NVME_GETSETFEAT_FID(dw10);
    uint8_t save = NVME_SETFEAT_SAVE(dw10);
    int i;

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
            nvme_smart_event(n, NVME_AER_INFO_SMART_TEMP_THRESH);
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
                                    n->params.max_ioqpairs,
                                    n->params.max_ioqpairs);
        req->cqe.result = cpu_to_le32((n->params.max_ioqpairs - 1) |
                                      ((n->params.max_ioqpairs - 1) << 16));
        break;
    case NVME_ASYNCHRONOUS_EVENT_CONF:
        n->features.async_config = dw11;
        break;
    case NVME_TIMESTAMP:
        return nvme_set_feature_timestamp(n, req);
    case NVME_COMMAND_SET_PROFILE:
        if (dw11 & 0x1ff) {
            trace_pci_nvme_err_invalid_iocsci(dw11 & 0x1ff);
            return NVME_CMD_SET_CMB_REJECTED | NVME_DNR;
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

static void nvme_update_dmrsl(NvmeCtrl *n)
{
    int nsid;

    for (nsid = 1; nsid <= NVME_MAX_NAMESPACES; nsid++) {
        NvmeNamespace *ns = nvme_ns(n, nsid);
        if (!ns) {
            continue;
        }

        n->dmrsl = MIN_NON_ZERO(n->dmrsl,
                                BDRV_REQUEST_MAX_BYTES / nvme_l2b(ns, 1));
    }
}

static void nvme_select_iocs_ns(NvmeCtrl *n, NvmeNamespace *ns)
{
    uint32_t cc = ldl_le_p(&n->bar.cc);

    ns->iocs = nvme_cse_iocs_none;
    switch (ns->csi) {
    case NVME_CSI_NVM:
        if (NVME_CC_CSS(cc) != NVME_CC_CSS_ADMIN_ONLY) {
            ns->iocs = nvme_cse_iocs_nvm;
        }
        break;
    case NVME_CSI_ZONED:
        if (NVME_CC_CSS(cc) == NVME_CC_CSS_CSI) {
            ns->iocs = nvme_cse_iocs_zoned;
        } else if (NVME_CC_CSS(cc) == NVME_CC_CSS_NVM) {
            ns->iocs = nvme_cse_iocs_nvm;
        }
        break;
    }
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
            if (nvme_ns(ctrl, nsid)) {
                return NVME_NS_ALREADY_ATTACHED | NVME_DNR;
            }

            if (ns->attached && !ns->params.shared) {
                return NVME_NS_PRIVATE | NVME_DNR;
            }

            nvme_attach_ns(ctrl, ns);
            nvme_select_iocs_ns(ctrl, ns);

            break;

        case NVME_NS_ATTACHMENT_DETACH:
            if (!nvme_ns(ctrl, nsid)) {
                return NVME_NS_NOT_ATTACHED | NVME_DNR;
            }

            ctrl->namespaces[nsid] = NULL;
            ns->attached--;

            nvme_update_dmrsl(ctrl);

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
    QEMUBH *bh;
    NvmeRequest *req;
    int ret;

    NvmeNamespace *ns;
    uint32_t nsid;
    bool broadcast;
    int64_t offset;
} NvmeFormatAIOCB;

static void nvme_format_bh(void *opaque);

static void nvme_format_cancel(BlockAIOCB *aiocb)
{
    NvmeFormatAIOCB *iocb = container_of(aiocb, NvmeFormatAIOCB, common);

    if (iocb->aiocb) {
        blk_aio_cancel_async(iocb->aiocb);
    }
}

static const AIOCBInfo nvme_format_aiocb_info = {
    .aiocb_size = sizeof(NvmeFormatAIOCB),
    .cancel_async = nvme_format_cancel,
    .get_aio_context = nvme_get_aio_context,
};

static void nvme_format_set(NvmeNamespace *ns, NvmeCmd *cmd)
{
    uint32_t dw10 = le32_to_cpu(cmd->cdw10);
    uint8_t lbaf = dw10 & 0xf;
    uint8_t pi = (dw10 >> 5) & 0x7;
    uint8_t mset = (dw10 >> 4) & 0x1;
    uint8_t pil = (dw10 >> 8) & 0x1;

    trace_pci_nvme_format_set(ns->params.nsid, lbaf, mset, pi, pil);

    ns->id_ns.dps = (pil << 3) | pi;
    ns->id_ns.flbas = lbaf | (mset << 4);

    nvme_ns_init_format(ns);
}

static void nvme_format_ns_cb(void *opaque, int ret)
{
    NvmeFormatAIOCB *iocb = opaque;
    NvmeRequest *req = iocb->req;
    NvmeNamespace *ns = iocb->ns;
    int bytes;

    if (ret < 0) {
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

    nvme_format_set(ns, &req->cmd);
    ns->status = 0x0;
    iocb->ns = NULL;
    iocb->offset = 0;

done:
    iocb->aiocb = NULL;
    qemu_bh_schedule(iocb->bh);
}

static uint16_t nvme_format_check(NvmeNamespace *ns, uint8_t lbaf, uint8_t pi)
{
    if (ns->params.zoned) {
        return NVME_INVALID_FORMAT | NVME_DNR;
    }

    if (lbaf > ns->id_ns.nlbaf) {
        return NVME_INVALID_FORMAT | NVME_DNR;
    }

    if (pi && (ns->id_ns.lbaf[lbaf].ms < sizeof(NvmeDifTuple))) {
        return NVME_INVALID_FORMAT | NVME_DNR;
    }

    if (pi && pi > NVME_ID_NS_DPS_TYPE_3) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static void nvme_format_bh(void *opaque)
{
    NvmeFormatAIOCB *iocb = opaque;
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
    qemu_bh_delete(iocb->bh);
    iocb->bh = NULL;

    iocb->common.cb(iocb->common.opaque, iocb->ret);

    qemu_aio_unref(iocb);
}

static uint16_t nvme_format(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeFormatAIOCB *iocb;
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);
    uint16_t status;

    iocb = qemu_aio_get(&nvme_format_aiocb_info, NULL, nvme_misc_cb, req);

    iocb->req = req;
    iocb->bh = qemu_bh_new(nvme_format_bh, iocb);
    iocb->ret = 0;
    iocb->ns = NULL;
    iocb->nsid = 0;
    iocb->broadcast = (nsid == NVME_NSID_BROADCAST);
    iocb->offset = 0;

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
    qemu_bh_schedule(iocb->bh);

    return NVME_NO_COMPLETE;

out:
    qemu_bh_delete(iocb->bh);
    iocb->bh = NULL;
    qemu_aio_unref(iocb);
    return status;
}

static uint16_t nvme_admin_cmd(NvmeCtrl *n, NvmeRequest *req)
{
    trace_pci_nvme_admin_cmd(nvme_cid(req), nvme_sqid(req), req->cmd.opcode,
                             nvme_adm_opc_str(req->cmd.opcode));

    if (!(nvme_cse_acs[req->cmd.opcode] & NVME_CMD_EFF_CSUPP)) {
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
    case NVME_ADM_CMD_FORMAT_NVM:
        return nvme_format(n, req);
    default:
        assert(false);
    }

    return NVME_INVALID_OPCODE | NVME_DNR;
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

    while (!(nvme_sq_empty(sq) || QTAILQ_EMPTY(&sq->req_list))) {
        addr = sq->dma_addr + sq->head * n->sqe_size;
        if (nvme_addr_read(n, addr, (void *)&cmd, sizeof(cmd))) {
            trace_pci_nvme_err_addr_read(addr);
            trace_pci_nvme_err_cfs();
            stl_le_p(&n->bar.csts, NVME_CSTS_FAILED);
            break;
        }
        nvme_inc_sq_head(sq);

        req = QTAILQ_FIRST(&sq->req_list);
        QTAILQ_REMOVE(&sq->req_list, req, entry);
        QTAILQ_INSERT_TAIL(&sq->out_req_list, req, entry);
        nvme_req_clear(req);
        req->cqe.cid = cmd.cid;
        memcpy(&req->cmd, &cmd, sizeof(NvmeCmd));

        status = sq->sqid ? nvme_io_cmd(n, req) :
            nvme_admin_cmd(n, req);
        if (status != NVME_NO_COMPLETE) {
            req->status = status;
            nvme_enqueue_req_completion(cq, req);
        }
    }
}

static void nvme_ctrl_reset(NvmeCtrl *n)
{
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

    n->aer_queued = 0;
    n->outstanding_aers = 0;
    n->qs_created = false;
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

static void nvme_select_iocs(NvmeCtrl *n)
{
    NvmeNamespace *ns;
    int i;

    for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }

        nvme_select_iocs_ns(n, ns);
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
    if (unlikely(NVME_CC_IOCQES(cc) <
                 NVME_CTRL_CQES_MIN(n->id_ctrl.cqes))) {
        trace_pci_nvme_err_startfail_cqent_too_small(
                    NVME_CC_IOCQES(cc),
                    NVME_CTRL_CQES_MIN(cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOCQES(cc) >
                 NVME_CTRL_CQES_MAX(n->id_ctrl.cqes))) {
        trace_pci_nvme_err_startfail_cqent_too_large(
                    NVME_CC_IOCQES(cc),
                    NVME_CTRL_CQES_MAX(cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOSQES(cc) <
                 NVME_CTRL_SQES_MIN(n->id_ctrl.sqes))) {
        trace_pci_nvme_err_startfail_sqent_too_small(
                    NVME_CC_IOSQES(cc),
                    NVME_CTRL_SQES_MIN(cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOSQES(cc) >
                 NVME_CTRL_SQES_MAX(n->id_ctrl.sqes))) {
        trace_pci_nvme_err_startfail_sqent_too_large(
                    NVME_CC_IOSQES(cc),
                    NVME_CTRL_SQES_MAX(cap));
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
    n->cqe_size = 1 << NVME_CC_IOCQES(cc);
    n->sqe_size = 1 << NVME_CC_IOSQES(cc);
    nvme_init_cq(&n->admin_cq, n, acq, 0, 0, NVME_AQA_ACQS(aqa) + 1, 1);
    nvme_init_sq(&n->admin_sq, n, asq, 0, 0, NVME_AQA_ASQS(aqa) + 1);

    nvme_set_timestamp(n, 0ULL);

    QTAILQ_INIT(&n->aer_queue);

    nvme_select_iocs(n);

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
        if (unlikely(msix_enabled(&(n->parent_obj)))) {
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
        if (unlikely(msix_enabled(&(n->parent_obj)))) {
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
        trace_pci_nvme_mmio_cfg(data & 0xffffffff);

        /* Windows first sends data, then sends enable bit */
        if (!NVME_CC_EN(data) && !NVME_CC_EN(cc) &&
            !NVME_CC_SHN(data) && !NVME_CC_SHN(cc))
        {
            cc = data;
        }

        if (NVME_CC_EN(data) && !NVME_CC_EN(cc)) {
            cc = data;

            /* flush CC since nvme_start_ctrl() needs the value */
            stl_le_p(&n->bar.cc, cc);
            if (unlikely(nvme_start_ctrl(n))) {
                trace_pci_nvme_err_startfail();
                csts = NVME_CSTS_FAILED;
            } else {
                trace_pci_nvme_mmio_start_success();
                csts = NVME_CSTS_READY;
            }
        } else if (!NVME_CC_EN(data) && NVME_CC_EN(cc)) {
            trace_pci_nvme_mmio_stopped();
            nvme_ctrl_reset(n);
            cc = 0;
            csts &= ~NVME_CSTS_READY;
        }

        if (NVME_CC_SHN(data) && !(NVME_CC_SHN(cc))) {
            trace_pci_nvme_mmio_shutdown_set();
            nvme_ctrl_shutdown(n);
            cc = data;
            csts |= NVME_CSTS_SHST_COMPLETE;
        } else if (!NVME_CC_SHN(data) && NVME_CC_SHN(cc)) {
            trace_pci_nvme_mmio_shutdown_cleared();
            csts &= ~NVME_CSTS_SHST_COMPLETE;
            cc = data;
        }

        stl_le_p(&n->bar.cc, cc);
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
        int start_sqs;
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
             * Completion Queue Head Doorbell regiter and an Asynchronous Event
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

        start_sqs = nvme_cq_full(cq) ? 1 : 0;
        cq->head = new_head;
        if (start_sqs) {
            NvmeSQueue *sq;
            QTAILQ_FOREACH(sq, &cq->sq_list, entry) {
                timer_mod(sq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 500);
            }
            timer_mod(cq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 500);
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
        timer_mod(sq->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 500);
    }
}

static void nvme_mmio_write(void *opaque, hwaddr addr, uint64_t data,
                            unsigned size)
{
    NvmeCtrl *n = (NvmeCtrl *)opaque;

    trace_pci_nvme_mmio_write(addr, data, size);

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

static void nvme_check_constraints(NvmeCtrl *n, Error **errp)
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
        return;
    }

    if (params->max_ioqpairs < 1 ||
        params->max_ioqpairs > NVME_MAX_IOQPAIRS) {
        error_setg(errp, "max_ioqpairs must be between 1 and %d",
                   NVME_MAX_IOQPAIRS);
        return;
    }

    if (params->msix_qsize < 1 ||
        params->msix_qsize > PCI_MSIX_FLAGS_QSIZE + 1) {
        error_setg(errp, "msix_qsize must be between 1 and %d",
                   PCI_MSIX_FLAGS_QSIZE + 1);
        return;
    }

    if (!params->serial) {
        error_setg(errp, "serial property not set");
        return;
    }

    if (n->pmr.dev) {
        if (host_memory_backend_is_mapped(n->pmr.dev)) {
            error_setg(errp, "can't use already busy memdev: %s",
                       object_get_canonical_path_component(OBJECT(n->pmr.dev)));
            return;
        }

        if (!is_power_of_2(n->pmr.dev->size)) {
            error_setg(errp, "pmr backend size needs to be power of 2 in size");
            return;
        }

        host_memory_backend_set_mapped(n->pmr.dev, true);
    }

    if (n->params.zasl > n->params.mdts) {
        error_setg(errp, "zoned.zasl (Zone Append Size Limit) must be less "
                   "than or equal to mdts (Maximum Data Transfer Size)");
        return;
    }

    if (!n->params.vsl) {
        error_setg(errp, "vsl must be non-zero");
        return;
    }
}

static void nvme_init_state(NvmeCtrl *n)
{
    /* add one to max_ioqpairs to account for the admin queue pair */
    n->reg_size = pow2ceil(sizeof(NvmeBar) +
                           2 * (n->params.max_ioqpairs + 1) * NVME_DB_SIZE);
    n->sq = g_new0(NvmeSQueue *, n->params.max_ioqpairs + 1);
    n->cq = g_new0(NvmeCQueue *, n->params.max_ioqpairs + 1);
    n->temperature = NVME_TEMPERATURE;
    n->features.temp_thresh_hi = NVME_TEMPERATURE_WARNING;
    n->starttime_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    n->aer_reqs = g_new0(NvmeRequest *, n->params.aerl + 1);
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

static int nvme_init_pci(NvmeCtrl *n, PCIDevice *pci_dev, Error **errp)
{
    uint8_t *pci_conf = pci_dev->config;
    uint64_t bar_size, msix_table_size, msix_pba_size;
    unsigned msix_table_offset, msix_pba_offset;
    int ret;

    Error *err = NULL;

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_config_set_prog_interface(pci_conf, 0x2);

    if (n->params.use_intel_id) {
        pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
        pci_config_set_device_id(pci_conf, 0x5845);
    } else {
        pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_REDHAT);
        pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_REDHAT_NVME);
    }

    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_EXPRESS);
    pcie_endpoint_cap_init(pci_dev, 0x80);

    bar_size = QEMU_ALIGN_UP(n->reg_size, 4 * KiB);
    msix_table_offset = bar_size;
    msix_table_size = PCI_MSIX_ENTRY_SIZE * n->params.msix_qsize;

    bar_size += msix_table_size;
    bar_size = QEMU_ALIGN_UP(bar_size, 4 * KiB);
    msix_pba_offset = bar_size;
    msix_pba_size = QEMU_ALIGN_UP(n->params.msix_qsize, 64) / 8;

    bar_size += msix_pba_size;
    bar_size = pow2ceil(bar_size);

    memory_region_init(&n->bar0, OBJECT(n), "nvme-bar0", bar_size);
    memory_region_init_io(&n->iomem, OBJECT(n), &nvme_mmio_ops, n, "nvme",
                          n->reg_size);
    memory_region_add_subregion(&n->bar0, 0, &n->iomem);

    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64, &n->bar0);
    ret = msix_init(pci_dev, n->params.msix_qsize,
                    &n->bar0, 0, msix_table_offset,
                    &n->bar0, 0, msix_pba_offset, 0, &err);
    if (ret < 0) {
        if (ret == -ENOTSUP) {
            warn_report_err(err);
        } else {
            error_propagate(errp, err);
            return ret;
        }
    }

    if (n->params.cmb_size_mb) {
        nvme_init_cmb(n, pci_dev);
    }

    if (n->pmr.dev) {
        nvme_init_pmr(n, pci_dev);
    }

    return 0;
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

    id->vid = cpu_to_le16(pci_get_word(pci_conf + PCI_VENDOR_ID));
    id->ssvid = cpu_to_le16(pci_get_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID));
    strpadcpy((char *)id->mn, sizeof(id->mn), "QEMU NVMe Ctrl", ' ');
    strpadcpy((char *)id->fr, sizeof(id->fr), "1.0", ' ');
    strpadcpy((char *)id->sn, sizeof(id->sn), n->params.serial, ' ');

    id->cntlid = cpu_to_le16(n->cntlid);

    id->oaes = cpu_to_le32(NVME_OAES_NS_ATTR);

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
    id->oacs = cpu_to_le16(NVME_OACS_NS_MGMT | NVME_OACS_FORMAT);
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

    id->sqes = (0x6 << 4) | 0x6;
    id->cqes = (0x4 << 4) | 0x4;
    id->nn = cpu_to_le32(NVME_MAX_NAMESPACES);
    id->oncs = cpu_to_le16(NVME_ONCS_WRITE_ZEROES | NVME_ONCS_TIMESTAMP |
                           NVME_ONCS_FEATURES | NVME_ONCS_DSM |
                           NVME_ONCS_COMPARE | NVME_ONCS_COPY);

    /*
     * NOTE: If this device ever supports a command set that does NOT use 0x0
     * as a Flush-equivalent operation, support for the broadcast NSID in Flush
     * should probably be removed.
     *
     * See comment in nvme_io_cmd.
     */
    id->vwc = NVME_VWC_NSID_BROADCAST_SUPPORT | NVME_VWC_PRESENT;

    id->ocfs = cpu_to_le16(NVME_OCFS_COPY_FORMAT_0);
    id->sgls = cpu_to_le32(NVME_CTRL_SGLS_SUPPORT_NO_ALIGN |
                           NVME_CTRL_SGLS_BITBUCKET);

    nvme_init_subnqn(n);

    id->psd[0].mp = cpu_to_le16(0x9c4);
    id->psd[0].enlat = cpu_to_le32(0x10);
    id->psd[0].exlat = cpu_to_le32(0x4);

    if (n->subsys) {
        id->cmic |= NVME_CMIC_MULTI_CTRL;
    }

    NVME_CAP_SET_MQES(cap, 0x7ff);
    NVME_CAP_SET_CQR(cap, 1);
    NVME_CAP_SET_TO(cap, 0xf);
    NVME_CAP_SET_CSS(cap, NVME_CAP_CSS_NVM);
    NVME_CAP_SET_CSS(cap, NVME_CAP_CSS_CSI_SUPP);
    NVME_CAP_SET_CSS(cap, NVME_CAP_CSS_ADMIN_ONLY);
    NVME_CAP_SET_MPSMAX(cap, 4);
    NVME_CAP_SET_CMBS(cap, n->params.cmb_size_mb ? 1 : 0);
    NVME_CAP_SET_PMRS(cap, n->pmr.dev ? 1 : 0);
    stq_le_p(&n->bar.cap, cap);

    stl_le_p(&n->bar.vs, NVME_SPEC_VER);
    n->bar.intmc = n->bar.intms = 0;
}

static int nvme_init_subsys(NvmeCtrl *n, Error **errp)
{
    int cntlid;

    if (!n->subsys) {
        return 0;
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

    n->dmrsl = MIN_NON_ZERO(n->dmrsl,
                            BDRV_REQUEST_MAX_BYTES / nvme_l2b(ns, 1));
}

static void nvme_realize(PCIDevice *pci_dev, Error **errp)
{
    NvmeCtrl *n = NVME(pci_dev);
    NvmeNamespace *ns;
    Error *local_err = NULL;

    nvme_check_constraints(n, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    qbus_init(&n->bus, sizeof(NvmeBus), TYPE_NVME_BUS,
              &pci_dev->qdev, n->parent_obj.qdev.id);

    nvme_init_state(n);
    if (nvme_init_pci(n, pci_dev, errp)) {
        return;
    }

    if (nvme_init_subsys(n, errp)) {
        error_propagate(errp, local_err);
        return;
    }
    nvme_init_ctrl(n, pci_dev);

    /* setup a namespace if the controller drive property was given */
    if (n->namespace.blkconf.blk) {
        ns = &n->namespace;
        ns->params.nsid = 1;

        if (nvme_ns_setup(ns, errp)) {
            return;
        }

        nvme_attach_ns(n, ns);
    }
}

static void nvme_exit(PCIDevice *pci_dev)
{
    NvmeCtrl *n = NVME(pci_dev);
    NvmeNamespace *ns;
    int i;

    nvme_ctrl_reset(n);

    if (n->subsys) {
        for (i = 1; i <= NVME_MAX_NAMESPACES; i++) {
            ns = nvme_ns(n, i);
            if (ns) {
                ns->attached--;
            }
        }

        nvme_subsys_unregister_ctrl(n->subsys, n);
    }

    g_free(n->cq);
    g_free(n->sq);
    g_free(n->aer_reqs);

    if (n->params.cmb_size_mb) {
        g_free(n->cmb.buf);
    }

    if (n->pmr.dev) {
        host_memory_backend_set_mapped(n->pmr.dev, false);
    }
    msix_uninit(pci_dev, &n->bar0, &n->bar0);
    memory_region_del_subregion(&n->bar0, &n->iomem);
}

static Property nvme_props[] = {
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
    DEFINE_PROP_UINT8("zoned.zasl", NvmeCtrl, params.zasl, 0),
    DEFINE_PROP_BOOL("zoned.auto_transition", NvmeCtrl,
                     params.auto_transition_zones, true),
    DEFINE_PROP_END_OF_LIST(),
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

static const VMStateDescription nvme_vmstate = {
    .name = "nvme",
    .unmigratable = 1,
};

static void nvme_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = nvme_realize;
    pc->exit = nvme_exit;
    pc->class_id = PCI_CLASS_STORAGE_EXPRESS;
    pc->revision = 2;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Non-Volatile Memory Express";
    device_class_set_props(dc, nvme_props);
    dc->vmsd = &nvme_vmstate;
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
