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
 */

/**
 * Usage: add options:
 *      -drive file=<file>,if=none,id=<drive_id>
 *      -device nvme,serial=<serial>,id=<bus_name>, \
 *              cmb_size_mb=<cmb_size_mb[optional]>, \
 *              [pmrdev=<mem_backend_file_id>,] \
 *              max_ioqpairs=<N[optional]>, \
 *              aerl=<N[optional]>, aer_max_queued=<N[optional]>, \
 *              mdts=<N[optional]>,zoned.append_size_limit=<N[optional]> \
 *      -device nvme-ns,drive=<drive_id>,bus=<bus_name>,nsid=<nsid>,\
 *              zoned=<true|false[optional]>
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
 *
 * nvme device parameters
 * ~~~~~~~~~~~~~~~~~~~~~~
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
 * - `zoned.append_size_limit`
 *   The maximum I/O size in bytes that is allowed in Zone Append command.
 *   The default is 128KiB. Since internally this this value is maintained as
 *   ZASL = log2(<maximum append size> / <page size>), some values assigned
 *   to this property may be rounded down and result in a lower maximum ZA
 *   data size being in effect. By setting this property to 0, users can make
 *   ZASL to be equal to MDTS. This property only affects zoned namespaces.
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
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "hw/block/block.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "sysemu/hostmem.h"
#include "sysemu/block-backend.h"
#include "exec/memory.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "nvme.h"
#include "nvme-ns.h"

#define NVME_MAX_IOQPAIRS 0xffff
#define NVME_DB_SIZE  4
#define NVME_SPEC_VER 0x00010400
#define NVME_CMB_BIR 2
#define NVME_PMR_BIR 4
#define NVME_TEMPERATURE 0x143
#define NVME_TEMPERATURE_WARNING 0x157
#define NVME_TEMPERATURE_CRITICAL 0x175
#define NVME_NUM_FW_SLOTS 1

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
};

static const uint32_t nvme_feature_cap[NVME_FID_MAX] = {
    [NVME_TEMPERATURE_THRESHOLD]    = NVME_FEAT_CAP_CHANGE,
    [NVME_ERROR_RECOVERY]           = NVME_FEAT_CAP_CHANGE | NVME_FEAT_CAP_NS,
    [NVME_VOLATILE_WRITE_CACHE]     = NVME_FEAT_CAP_CHANGE,
    [NVME_NUMBER_OF_QUEUES]         = NVME_FEAT_CAP_CHANGE,
    [NVME_ASYNCHRONOUS_EVENT_CONF]  = NVME_FEAT_CAP_CHANGE,
    [NVME_TIMESTAMP]                = NVME_FEAT_CAP_CHANGE,
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
};

static const uint32_t nvme_cse_iocs_none[256];

static const uint32_t nvme_cse_iocs_nvm[256] = {
    [NVME_CMD_FLUSH]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE_ZEROES]         = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_READ]                 = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_DSM]                  = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_COMPARE]              = NVME_CMD_EFF_CSUPP,
};

static const uint32_t nvme_cse_iocs_zoned[256] = {
    [NVME_CMD_FLUSH]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE_ZEROES]         = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_WRITE]                = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_READ]                 = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_DSM]                  = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_COMPARE]              = NVME_CMD_EFF_CSUPP,
    [NVME_CMD_ZONE_APPEND]          = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_ZONE_MGMT_SEND]       = NVME_CMD_EFF_CSUPP | NVME_CMD_EFF_LBCC,
    [NVME_CMD_ZONE_MGMT_RECV]       = NVME_CMD_EFF_CSUPP,
};

static void nvme_process_sq(void *opaque);

static uint16_t nvme_cid(NvmeRequest *req)
{
    if (!req) {
        return 0xffff;
    }

    return le16_to_cpu(req->cqe.cid);
}

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

static bool nvme_nsid_valid(NvmeCtrl *n, uint32_t nsid)
{
    return nsid && (nsid == NVME_NSID_BROADCAST || nsid <= n->num_namespaces);
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
    if (msix_enabled(&(n->parent_obj))) {
        return;
    }
    if (~n->bar.intms & n->irq_status) {
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
            n->irq_status &= ~(1 << cq->vector);
            nvme_irq_check(n);
        }
    }
}

static void nvme_req_clear(NvmeRequest *req)
{
    req->ns = NULL;
    req->opaque = NULL;
    memset(&req->cqe, 0x0, sizeof(req->cqe));
    req->status = NVME_SUCCESS;
}

static void nvme_req_exit(NvmeRequest *req)
{
    if (req->qsg.sg) {
        qemu_sglist_destroy(&req->qsg);
    }

    if (req->iov.iov) {
        qemu_iovec_destroy(&req->iov);
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

static uint16_t nvme_map_addr(NvmeCtrl *n, QEMUSGList *qsg, QEMUIOVector *iov,
                              hwaddr addr, size_t len)
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
        if (qsg && qsg->sg) {
            return NVME_INVALID_USE_OF_CMB | NVME_DNR;
        }

        assert(iov);

        if (!iov->iov) {
            qemu_iovec_init(iov, 1);
        }

        if (cmb) {
            return nvme_map_addr_cmb(n, iov, addr, len);
        } else {
            return nvme_map_addr_pmr(n, iov, addr, len);
        }
    }

    if (iov && iov->iov) {
        return NVME_INVALID_USE_OF_CMB | NVME_DNR;
    }

    assert(qsg);

    if (!qsg->sg) {
        pci_dma_sglist_init(qsg, &n->parent_obj, 1);
    }

    qemu_sglist_add(qsg, addr, len);

    return NVME_SUCCESS;
}

static uint16_t nvme_map_prp(NvmeCtrl *n, uint64_t prp1, uint64_t prp2,
                             uint32_t len, NvmeRequest *req)
{
    hwaddr trans_len = n->page_size - (prp1 % n->page_size);
    trans_len = MIN(len, trans_len);
    int num_prps = (len >> n->page_bits) + 1;
    uint16_t status;
    int ret;

    QEMUSGList *qsg = &req->qsg;
    QEMUIOVector *iov = &req->iov;

    trace_pci_nvme_map_prp(trans_len, len, prp1, prp2, num_prps);

    if (nvme_addr_is_cmb(n, prp1) || (nvme_addr_is_pmr(n, prp1))) {
        qemu_iovec_init(iov, num_prps);
    } else {
        pci_dma_sglist_init(qsg, &n->parent_obj, num_prps);
    }

    status = nvme_map_addr(n, qsg, iov, prp1, trans_len);
    if (status) {
        return status;
    }

    len -= trans_len;
    if (len) {
        if (len > n->page_size) {
            uint64_t prp_list[n->max_prp_ents];
            uint32_t nents, prp_trans;
            int i = 0;

            nents = (len + n->page_size - 1) >> n->page_bits;
            prp_trans = MIN(n->max_prp_ents, nents) * sizeof(uint64_t);
            ret = nvme_addr_read(n, prp2, (void *)prp_list, prp_trans);
            if (ret) {
                trace_pci_nvme_err_addr_read(prp2);
                return NVME_DATA_TRAS_ERROR;
            }
            while (len != 0) {
                uint64_t prp_ent = le64_to_cpu(prp_list[i]);

                if (i == n->max_prp_ents - 1 && len > n->page_size) {
                    if (unlikely(prp_ent & (n->page_size - 1))) {
                        trace_pci_nvme_err_invalid_prplist_ent(prp_ent);
                        return NVME_INVALID_PRP_OFFSET | NVME_DNR;
                    }

                    i = 0;
                    nents = (len + n->page_size - 1) >> n->page_bits;
                    prp_trans = MIN(n->max_prp_ents, nents) * sizeof(uint64_t);
                    ret = nvme_addr_read(n, prp_ent, (void *)prp_list,
                                         prp_trans);
                    if (ret) {
                        trace_pci_nvme_err_addr_read(prp_ent);
                        return NVME_DATA_TRAS_ERROR;
                    }
                    prp_ent = le64_to_cpu(prp_list[i]);
                }

                if (unlikely(prp_ent & (n->page_size - 1))) {
                    trace_pci_nvme_err_invalid_prplist_ent(prp_ent);
                    return NVME_INVALID_PRP_OFFSET | NVME_DNR;
                }

                trans_len = MIN(len, n->page_size);
                status = nvme_map_addr(n, qsg, iov, prp_ent, trans_len);
                if (status) {
                    return status;
                }

                len -= trans_len;
                i++;
            }
        } else {
            if (unlikely(prp2 & (n->page_size - 1))) {
                trace_pci_nvme_err_invalid_prp2_align(prp2);
                return NVME_INVALID_PRP_OFFSET | NVME_DNR;
            }
            status = nvme_map_addr(n, qsg, iov, prp2, len);
            if (status) {
                return status;
            }
        }
    }

    return NVME_SUCCESS;
}

/*
 * Map 'nsgld' data descriptors from 'segment'. The function will subtract the
 * number of bytes mapped in len.
 */
static uint16_t nvme_map_sgl_data(NvmeCtrl *n, QEMUSGList *qsg,
                                  QEMUIOVector *iov,
                                  NvmeSglDescriptor *segment, uint64_t nsgld,
                                  size_t *len, NvmeRequest *req)
{
    dma_addr_t addr, trans_len;
    uint32_t dlen;
    uint16_t status;

    for (int i = 0; i < nsgld; i++) {
        uint8_t type = NVME_SGL_TYPE(segment[i].type);

        switch (type) {
        case NVME_SGL_DESCR_TYPE_BIT_BUCKET:
            if (req->cmd.opcode == NVME_CMD_WRITE) {
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

            trace_pci_nvme_err_invalid_sgl_excess_length(nvme_cid(req));
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

        status = nvme_map_addr(n, qsg, iov, addr, trans_len);
        if (status) {
            return status;
        }

next:
        *len -= trans_len;
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_map_sgl(NvmeCtrl *n, QEMUSGList *qsg, QEMUIOVector *iov,
                             NvmeSglDescriptor sgl, size_t len,
                             NvmeRequest *req)
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

    trace_pci_nvme_map_sgl(nvme_cid(req), NVME_SGL_TYPE(sgl.type), len);

    /*
     * If the entire transfer can be described with a single data block it can
     * be mapped directly.
     */
    if (NVME_SGL_TYPE(sgl.type) == NVME_SGL_DESCR_TYPE_DATA_BLOCK) {
        status = nvme_map_sgl_data(n, qsg, iov, sgld, 1, &len, req);
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

            status = nvme_map_sgl_data(n, qsg, iov, segment, SEG_CHUNK_SIZE,
                                       &len, req);
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
            status = nvme_map_sgl_data(n, qsg, iov, segment, nsgld, &len, req);
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
        status = nvme_map_sgl_data(n, qsg, iov, segment, nsgld - 1, &len, req);
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
    if (iov->iov) {
        qemu_iovec_destroy(iov);
    }

    if (qsg->sg) {
        qemu_sglist_destroy(qsg);
    }

    return status;
}

static uint16_t nvme_map_dptr(NvmeCtrl *n, size_t len, NvmeRequest *req)
{
    uint64_t prp1, prp2;

    switch (NVME_CMD_FLAGS_PSDT(req->cmd.flags)) {
    case NVME_PSDT_PRP:
        prp1 = le64_to_cpu(req->cmd.dptr.prp1);
        prp2 = le64_to_cpu(req->cmd.dptr.prp2);

        return nvme_map_prp(n, prp1, prp2, len, req);
    case NVME_PSDT_SGL_MPTR_CONTIGUOUS:
    case NVME_PSDT_SGL_MPTR_SGL:
        /* SGLs shall not be used for Admin commands in NVMe over PCIe */
        if (!req->sq->sqid) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        return nvme_map_sgl(n, &req->qsg, &req->iov, req->cmd.dptr.sgl, len,
                            req);
    default:
        return NVME_INVALID_FIELD;
    }
}

static uint16_t nvme_dma(NvmeCtrl *n, uint8_t *ptr, uint32_t len,
                         DMADirection dir, NvmeRequest *req)
{
    uint16_t status = NVME_SUCCESS;

    status = nvme_map_dptr(n, len, req);
    if (status) {
        return status;
    }

    /* assert that only one of qsg and iov carries data */
    assert((req->qsg.nsg > 0) != (req->iov.niov > 0));

    if (req->qsg.nsg > 0) {
        uint64_t residual;

        if (dir == DMA_DIRECTION_TO_DEVICE) {
            residual = dma_buf_write(ptr, len, &req->qsg);
        } else {
            residual = dma_buf_read(ptr, len, &req->qsg);
        }

        if (unlikely(residual)) {
            trace_pci_nvme_err_invalid_dma();
            status = NVME_INVALID_FIELD | NVME_DNR;
        }
    } else {
        size_t bytes;

        if (dir == DMA_DIRECTION_TO_DEVICE) {
            bytes = qemu_iovec_to_buf(&req->iov, 0, ptr, len);
        } else {
            bytes = qemu_iovec_from_buf(&req->iov, 0, ptr, len);
        }

        if (unlikely(bytes != len)) {
            trace_pci_nvme_err_invalid_dma();
            status = NVME_INVALID_FIELD | NVME_DNR;
        }
    }

    return status;
}

static void nvme_post_cqes(void *opaque)
{
    NvmeCQueue *cq = opaque;
    NvmeCtrl *n = cq->ctrl;
    NvmeRequest *req, *next;
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
            n->bar.csts = NVME_CSTS_FAILED;
            break;
        }
        QTAILQ_REMOVE(&cq->req_list, req, entry);
        nvme_inc_cq_tail(cq);
        nvme_req_exit(req);
        QTAILQ_INSERT_TAIL(&sq->req_list, req, entry);
    }
    if (cq->tail != cq->head) {
        nvme_irq_assert(n, cq);
    }
}

static void nvme_enqueue_req_completion(NvmeCQueue *cq, NvmeRequest *req)
{
    assert(cq->cqid == req->sq->cqid);
    trace_pci_nvme_enqueue_req_completion(nvme_cid(req), cq->cqid,
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
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static inline uint16_t nvme_check_bounds(NvmeNamespace *ns, uint64_t slba,
                                         uint32_t nlb)
{
    uint64_t nsze = le64_to_cpu(ns->id_ns.nsze);

    if (unlikely(UINT64_MAX - slba < nlb || slba + nlb > nsze)) {
        return NVME_LBA_RANGE | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static uint16_t nvme_check_dulbe(NvmeNamespace *ns, uint64_t slba,
                                 uint32_t nlb)
{
    BlockDriverState *bs = blk_bs(ns->blkconf.blk);

    int64_t pnum = 0, bytes = nvme_l2b(ns, nlb);
    int64_t offset = nvme_l2b(ns, slba);
    bool zeroed;
    int ret;

    Error *local_err = NULL;

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
            error_setg_errno(&local_err, -ret, "unable to get block status");
            error_report_err(local_err);

            return NVME_INTERNAL_DEV_ERROR;
        }

        zeroed = !!(ret & BDRV_BLOCK_ZERO);

        trace_pci_nvme_block_status(offset, bytes, pnum, ret, zeroed);

        if (zeroed) {
            return NVME_DULB;
        }

        offset += pnum;
    } while (pnum != bytes);

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

    trace_pci_nvme_err_aio(nvme_cid(req), strerror(ret), status);

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

    assert(zone_idx < ns->num_zones);
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

static uint16_t nvme_check_zone_write(NvmeCtrl *n, NvmeNamespace *ns,
                                      NvmeZone *zone, uint64_t slba,
                                      uint32_t nlb)
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
    uint16_t status;

    switch (nvme_get_zone_state(zone)) {
    case NVME_ZONE_STATE_EMPTY:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_FULL:
    case NVME_ZONE_STATE_CLOSED:
    case NVME_ZONE_STATE_READ_ONLY:
        status = NVME_SUCCESS;
        break;
    case NVME_ZONE_STATE_OFFLINE:
        status = NVME_ZONE_OFFLINE;
        break;
    default:
        assert(false);
    }

    return status;
}

static uint16_t nvme_check_zone_read(NvmeNamespace *ns, uint64_t slba,
                                     uint32_t nlb)
{
    NvmeZone *zone = nvme_get_zone_by_slba(ns, slba);
    uint64_t bndry = nvme_zone_rd_boundary(ns, zone);
    uint64_t end = slba + nlb;
    uint16_t status;

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

static void nvme_auto_transition_zone(NvmeNamespace *ns)
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
            nvme_aor_dec_open(ns);
            nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_CLOSED);
        }
    }
}

static uint16_t nvme_auto_open_zone(NvmeNamespace *ns, NvmeZone *zone)
{
    uint16_t status = NVME_SUCCESS;
    uint8_t zs = nvme_get_zone_state(zone);

    if (zs == NVME_ZONE_STATE_EMPTY) {
        nvme_auto_transition_zone(ns);
        status = nvme_aor_check(ns, 1, 1);
    } else if (zs == NVME_ZONE_STATE_CLOSED) {
        nvme_auto_transition_zone(ns);
        status = nvme_aor_check(ns, 0, 1);
    }

    return status;
}

static void nvme_finalize_zoned_write(NvmeNamespace *ns, NvmeRequest *req,
                                      bool failed)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeZone *zone;
    NvmeZonedResult *res = (NvmeZonedResult *)&req->cqe;
    uint64_t slba;
    uint32_t nlb;

    slba = le64_to_cpu(rw->slba);
    nlb = le16_to_cpu(rw->nlb) + 1;
    zone = nvme_get_zone_by_slba(ns, slba);

    zone->d.wp += nlb;

    if (failed) {
        res->slba = 0;
    }

    if (zone->d.wp == nvme_zone_wr_boundary(zone)) {
        switch (nvme_get_zone_state(zone)) {
        case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        case NVME_ZONE_STATE_EXPLICITLY_OPEN:
            nvme_aor_dec_open(ns);
            /* fall through */
        case NVME_ZONE_STATE_CLOSED:
            nvme_aor_dec_active(ns);
            /* fall through */
        case NVME_ZONE_STATE_EMPTY:
            nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_FULL);
            /* fall through */
        case NVME_ZONE_STATE_FULL:
            break;
        default:
            assert(false);
        }
    }
}

static void nvme_advance_zone_wp(NvmeNamespace *ns, NvmeZone *zone,
                                 uint32_t nlb)
{
    uint8_t zs;

    zone->w_ptr += nlb;

    if (zone->w_ptr < nvme_zone_wr_boundary(zone)) {
        zs = nvme_get_zone_state(zone);
        switch (zs) {
        case NVME_ZONE_STATE_EMPTY:
            nvme_aor_inc_active(ns);
            /* fall through */
        case NVME_ZONE_STATE_CLOSED:
            nvme_aor_inc_open(ns);
            nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_IMPLICITLY_OPEN);
        }
    }
}

static inline bool nvme_is_write(NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;

    return rw->opcode == NVME_CMD_WRITE ||
           rw->opcode == NVME_CMD_ZONE_APPEND ||
           rw->opcode == NVME_CMD_WRITE_ZEROES;
}

static void nvme_rw_cb(void *opaque, int ret)
{
    NvmeRequest *req = opaque;
    NvmeNamespace *ns = req->ns;

    BlockBackend *blk = ns->blkconf.blk;
    BlockAcctCookie *acct = &req->acct;
    BlockAcctStats *stats = blk_get_stats(blk);

    trace_pci_nvme_rw_cb(nvme_cid(req), blk_name(blk));

    if (ns->params.zoned && nvme_is_write(req)) {
        nvme_finalize_zoned_write(ns, req, ret != 0);
    }

    if (!ret) {
        block_acct_done(stats, acct);
    } else {
        block_acct_failed(stats, acct);
        nvme_aio_err(req, ret);
    }

    nvme_enqueue_req_completion(nvme_cq(req), req);
}

static void nvme_aio_discard_cb(void *opaque, int ret)
{
    NvmeRequest *req = opaque;
    uintptr_t *discards = (uintptr_t *)&req->opaque;

    trace_pci_nvme_aio_discard_cb(nvme_cid(req));

    if (ret) {
        nvme_aio_err(req, ret);
    }

    (*discards)--;

    if (*discards) {
        return;
    }

    nvme_enqueue_req_completion(nvme_cq(req), req);
}

struct nvme_zone_reset_ctx {
    NvmeRequest *req;
    NvmeZone    *zone;
};

static void nvme_aio_zone_reset_cb(void *opaque, int ret)
{
    struct nvme_zone_reset_ctx *ctx = opaque;
    NvmeRequest *req = ctx->req;
    NvmeNamespace *ns = req->ns;
    NvmeZone *zone = ctx->zone;
    uintptr_t *resets = (uintptr_t *)&req->opaque;

    g_free(ctx);

    trace_pci_nvme_aio_zone_reset_cb(nvme_cid(req), zone->d.zslba);

    if (!ret) {
        switch (nvme_get_zone_state(zone)) {
        case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        case NVME_ZONE_STATE_IMPLICITLY_OPEN:
            nvme_aor_dec_open(ns);
            /* fall through */
        case NVME_ZONE_STATE_CLOSED:
            nvme_aor_dec_active(ns);
            /* fall through */
        case NVME_ZONE_STATE_FULL:
            zone->w_ptr = zone->d.zslba;
            zone->d.wp = zone->w_ptr;
            nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_EMPTY);
            /* fall through */
        default:
            break;
        }
    } else {
        nvme_aio_err(req, ret);
    }

    (*resets)--;

    if (*resets) {
        return;
    }

    nvme_enqueue_req_completion(nvme_cq(req), req);
}

struct nvme_compare_ctx {
    QEMUIOVector iov;
    uint8_t *bounce;
    size_t len;
};

static void nvme_compare_cb(void *opaque, int ret)
{
    NvmeRequest *req = opaque;
    NvmeNamespace *ns = req->ns;
    struct nvme_compare_ctx *ctx = req->opaque;
    g_autofree uint8_t *buf = NULL;
    uint16_t status;

    trace_pci_nvme_compare_cb(nvme_cid(req));

    if (!ret) {
        block_acct_done(blk_get_stats(ns->blkconf.blk), &req->acct);
    } else {
        block_acct_failed(blk_get_stats(ns->blkconf.blk), &req->acct);
        nvme_aio_err(req, ret);
        goto out;
    }

    buf = g_malloc(ctx->len);

    status = nvme_dma(nvme_ctrl(req), buf, ctx->len, DMA_DIRECTION_TO_DEVICE,
                      req);
    if (status) {
        req->status = status;
        goto out;
    }

    if (memcmp(buf, ctx->bounce, ctx->len)) {
        req->status = NVME_CMP_FAILURE;
    }

out:
    qemu_iovec_destroy(&ctx->iov);
    g_free(ctx->bounce);
    g_free(ctx);

    nvme_enqueue_req_completion(nvme_cq(req), req);
}

static uint16_t nvme_dsm(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns = req->ns;
    NvmeDsmCmd *dsm = (NvmeDsmCmd *) &req->cmd;

    uint32_t attr = le32_to_cpu(dsm->attributes);
    uint32_t nr = (le32_to_cpu(dsm->nr) & 0xff) + 1;

    uint16_t status = NVME_SUCCESS;

    trace_pci_nvme_dsm(nvme_cid(req), nvme_nsid(ns), nr, attr);

    if (attr & NVME_DSMGMT_AD) {
        int64_t offset;
        size_t len;
        NvmeDsmRange range[nr];
        uintptr_t *discards = (uintptr_t *)&req->opaque;

        status = nvme_dma(n, (uint8_t *)range, sizeof(range),
                          DMA_DIRECTION_TO_DEVICE, req);
        if (status) {
            return status;
        }

        /*
         * AIO callbacks may be called immediately, so initialize discards to 1
         * to make sure the the callback does not complete the request before
         * all discards have been issued.
         */
        *discards = 1;

        for (int i = 0; i < nr; i++) {
            uint64_t slba = le64_to_cpu(range[i].slba);
            uint32_t nlb = le32_to_cpu(range[i].nlb);

            if (nvme_check_bounds(ns, slba, nlb)) {
                trace_pci_nvme_err_invalid_lba_range(slba, nlb,
                                                     ns->id_ns.nsze);
                continue;
            }

            trace_pci_nvme_dsm_deallocate(nvme_cid(req), nvme_nsid(ns), slba,
                                          nlb);

            offset = nvme_l2b(ns, slba);
            len = nvme_l2b(ns, nlb);

            while (len) {
                size_t bytes = MIN(BDRV_REQUEST_MAX_BYTES, len);

                (*discards)++;

                blk_aio_pdiscard(ns->blkconf.blk, offset, bytes,
                                 nvme_aio_discard_cb, req);

                offset += bytes;
                len -= bytes;
            }
        }

        /* account for the 1-initialization */
        (*discards)--;

        if (*discards) {
            status = NVME_NO_COMPLETE;
        } else {
            status = req->status;
        }
    }

    return status;
}

static uint16_t nvme_compare(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    BlockBackend *blk = ns->blkconf.blk;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = le16_to_cpu(rw->nlb) + 1;
    size_t len = nvme_l2b(ns, nlb);
    int64_t offset = nvme_l2b(ns, slba);
    uint8_t *bounce = NULL;
    struct nvme_compare_ctx *ctx = NULL;
    uint16_t status;

    trace_pci_nvme_compare(nvme_cid(req), nvme_nsid(ns), slba, nlb);

    status = nvme_check_mdts(n, len);
    if (status) {
        trace_pci_nvme_err_mdts(nvme_cid(req), len);
        return status;
    }

    status = nvme_check_bounds(ns, slba, nlb);
    if (status) {
        trace_pci_nvme_err_invalid_lba_range(slba, nlb, ns->id_ns.nsze);
        return status;
    }

    if (NVME_ERR_REC_DULBE(ns->features.err_rec)) {
        status = nvme_check_dulbe(ns, slba, nlb);
        if (status) {
            return status;
        }
    }

    bounce = g_malloc(len);

    ctx = g_new(struct nvme_compare_ctx, 1);
    ctx->bounce = bounce;
    ctx->len = len;

    req->opaque = ctx;

    qemu_iovec_init(&ctx->iov, 1);
    qemu_iovec_add(&ctx->iov, bounce, len);

    block_acct_start(blk_get_stats(blk), &req->acct, len, BLOCK_ACCT_READ);
    blk_aio_preadv(blk, offset, &ctx->iov, 0, nvme_compare_cb, req);

    return NVME_NO_COMPLETE;
}

static uint16_t nvme_flush(NvmeCtrl *n, NvmeRequest *req)
{
    block_acct_start(blk_get_stats(req->ns->blkconf.blk), &req->acct, 0,
                     BLOCK_ACCT_FLUSH);
    req->aiocb = blk_aio_flush(req->ns->blkconf.blk, nvme_rw_cb, req);
    return NVME_NO_COMPLETE;
}

static uint16_t nvme_read(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = (uint32_t)le16_to_cpu(rw->nlb) + 1;
    uint64_t data_size = nvme_l2b(ns, nlb);
    uint64_t data_offset;
    BlockBackend *blk = ns->blkconf.blk;
    uint16_t status;

    trace_pci_nvme_read(nvme_cid(req), nvme_nsid(ns), nlb, data_size, slba);

    status = nvme_check_mdts(n, data_size);
    if (status) {
        trace_pci_nvme_err_mdts(nvme_cid(req), data_size);
        goto invalid;
    }

    status = nvme_check_bounds(ns, slba, nlb);
    if (status) {
        trace_pci_nvme_err_invalid_lba_range(slba, nlb, ns->id_ns.nsze);
        goto invalid;
    }

    if (ns->params.zoned) {
        status = nvme_check_zone_read(ns, slba, nlb);
        if (status) {
            trace_pci_nvme_err_zone_read_not_ok(slba, nlb, status);
            goto invalid;
        }
    }

    status = nvme_map_dptr(n, data_size, req);
    if (status) {
        goto invalid;
    }

    if (NVME_ERR_REC_DULBE(ns->features.err_rec)) {
        status = nvme_check_dulbe(ns, slba, nlb);
        if (status) {
            goto invalid;
        }
    }

    data_offset = nvme_l2b(ns, slba);

    block_acct_start(blk_get_stats(blk), &req->acct, data_size,
                     BLOCK_ACCT_READ);
    if (req->qsg.sg) {
        req->aiocb = dma_blk_read(blk, &req->qsg, data_offset,
                                  BDRV_SECTOR_SIZE, nvme_rw_cb, req);
    } else {
        req->aiocb = blk_aio_preadv(blk, data_offset, &req->iov, 0,
                                    nvme_rw_cb, req);
    }
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
    uint64_t data_size = nvme_l2b(ns, nlb);
    uint64_t data_offset;
    NvmeZone *zone;
    NvmeZonedResult *res = (NvmeZonedResult *)&req->cqe;
    BlockBackend *blk = ns->blkconf.blk;
    uint16_t status;

    trace_pci_nvme_write(nvme_cid(req), nvme_io_opc_str(rw->opcode),
                         nvme_nsid(ns), nlb, data_size, slba);

    if (!wrz) {
        status = nvme_check_mdts(n, data_size);
        if (status) {
            trace_pci_nvme_err_mdts(nvme_cid(req), data_size);
            goto invalid;
        }
    }

    status = nvme_check_bounds(ns, slba, nlb);
    if (status) {
        trace_pci_nvme_err_invalid_lba_range(slba, nlb, ns->id_ns.nsze);
        goto invalid;
    }

    if (ns->params.zoned) {
        zone = nvme_get_zone_by_slba(ns, slba);

        if (append) {
            if (unlikely(slba != zone->d.zslba)) {
                trace_pci_nvme_err_append_not_at_start(slba, zone->d.zslba);
                status = NVME_INVALID_FIELD;
                goto invalid;
            }

            if (nvme_l2b(ns, nlb) > (n->page_size << n->zasl)) {
                trace_pci_nvme_err_append_too_large(slba, nlb, n->zasl);
                status = NVME_INVALID_FIELD;
                goto invalid;
            }

            slba = zone->w_ptr;
            res->slba = cpu_to_le64(slba);
        }

        status = nvme_check_zone_write(n, ns, zone, slba, nlb);
        if (status) {
            goto invalid;
        }

        status = nvme_auto_open_zone(ns, zone);
        if (status) {
            goto invalid;
        }

        nvme_advance_zone_wp(ns, zone, nlb);
    }

    data_offset = nvme_l2b(ns, slba);

    if (!wrz) {
        status = nvme_map_dptr(n, data_size, req);
        if (status) {
            goto invalid;
        }

        block_acct_start(blk_get_stats(blk), &req->acct, data_size,
                         BLOCK_ACCT_WRITE);
        if (req->qsg.sg) {
            req->aiocb = dma_blk_write(blk, &req->qsg, data_offset,
                                       BDRV_SECTOR_SIZE, nvme_rw_cb, req);
        } else {
            req->aiocb = blk_aio_pwritev(blk, data_offset, &req->iov, 0,
                                         nvme_rw_cb, req);
        }
    } else {
        block_acct_start(blk_get_stats(blk), &req->acct, 0, BLOCK_ACCT_WRITE);
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
    uint16_t status;

    switch (state) {
    case NVME_ZONE_STATE_EMPTY:
        status = nvme_aor_check(ns, 1, 0);
        if (status) {
            return status;
        }
        nvme_aor_inc_active(ns);
        /* fall through */
    case NVME_ZONE_STATE_CLOSED:
        status = nvme_aor_check(ns, 0, 1);
        if (status) {
            if (state == NVME_ZONE_STATE_EMPTY) {
                nvme_aor_dec_active(ns);
            }
            return status;
        }
        nvme_aor_inc_open(ns);
        /* fall through */
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_EXPLICITLY_OPEN);
        /* fall through */
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        return NVME_SUCCESS;
    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static uint16_t nvme_close_zone(NvmeNamespace *ns, NvmeZone *zone,
                                NvmeZoneState state, NvmeRequest *req)
{
    switch (state) {
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

static uint16_t nvme_finish_zone(NvmeNamespace *ns, NvmeZone *zone,
                                 NvmeZoneState state, NvmeRequest *req)
{
    switch (state) {
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        nvme_aor_dec_open(ns);
        /* fall through */
    case NVME_ZONE_STATE_CLOSED:
        nvme_aor_dec_active(ns);
        /* fall through */
    case NVME_ZONE_STATE_EMPTY:
        zone->w_ptr = nvme_zone_wr_boundary(zone);
        zone->d.wp = zone->w_ptr;
        nvme_assign_zone_state(ns, zone, NVME_ZONE_STATE_FULL);
        /* fall through */
    case NVME_ZONE_STATE_FULL:
        return NVME_SUCCESS;
    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static uint16_t nvme_reset_zone(NvmeNamespace *ns, NvmeZone *zone,
                                NvmeZoneState state, NvmeRequest *req)
{
    uintptr_t *resets = (uintptr_t *)&req->opaque;
    struct nvme_zone_reset_ctx *ctx;

    switch (state) {
    case NVME_ZONE_STATE_EMPTY:
        return NVME_SUCCESS;
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_CLOSED:
    case NVME_ZONE_STATE_FULL:
        break;
    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }

    /*
     * The zone reset aio callback needs to know the zone that is being reset
     * in order to transition the zone on completion.
     */
    ctx = g_new(struct nvme_zone_reset_ctx, 1);
    ctx->req = req;
    ctx->zone = zone;

    (*resets)++;

    blk_aio_pwrite_zeroes(ns->blkconf.blk, nvme_l2b(ns, zone->d.zslba),
                          nvme_l2b(ns, ns->zone_size), BDRV_REQ_MAY_UNMAP,
                          nvme_aio_zone_reset_cb, ctx);

    return NVME_NO_COMPLETE;
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

static uint16_t nvme_zone_mgmt_send(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = (NvmeCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    NvmeZone *zone;
    uintptr_t *resets;
    uint8_t *zd_ext;
    uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint64_t slba = 0;
    uint32_t zone_idx = 0;
    uint16_t status;
    uint8_t action;
    bool all;
    enum NvmeZoneProcessingMask proc_mask = NVME_PROC_CURRENT_ZONE;

    action = dw13 & 0xff;
    all = dw13 & 0x100;

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
        resets = (uintptr_t *)&req->opaque;

        if (all) {
            proc_mask = NVME_PROC_OPENED_ZONES | NVME_PROC_CLOSED_ZONES |
                NVME_PROC_FULL_ZONES;
        }
        trace_pci_nvme_reset_zone(slba, zone_idx, all);

        *resets = 1;

        status = nvme_do_zone_op(ns, zone, proc_mask, nvme_reset_zone, req);

        (*resets)--;

        return *resets ? NVME_NO_COMPLETE : req->status;

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
        status = nvme_dma(n, zd_ext, ns->params.zd_extension_size,
                          DMA_DIRECTION_TO_DEVICE, req);
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
    uint64_t slba, capacity = nvme_ns_nlbas(ns);
    NvmeZoneDescr *z;
    NvmeZone *zone;
    NvmeZoneReportHeader *header;
    void *buf, *buf_p;
    size_t zone_entry_sz;

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
        trace_pci_nvme_err_mdts(nvme_cid(req), data_size);
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
    for (; slba < capacity; slba += ns->zone_size) {
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

    status = nvme_dma(n, (uint8_t *)buf, data_size,
                      DMA_DIRECTION_FROM_DEVICE, req);

    g_free(buf);

    return status;
}

static uint16_t nvme_io_cmd(NvmeCtrl *n, NvmeRequest *req)
{
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);

    trace_pci_nvme_io_cmd(nvme_cid(req), nsid, nvme_sqid(req),
                          req->cmd.opcode, nvme_io_opc_str(req->cmd.opcode));

    if (!nvme_nsid_valid(n, nsid)) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    req->ns = nvme_ns(n, nsid);
    if (unlikely(!req->ns)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (!(req->ns->iocs[req->cmd.opcode] & NVME_CMD_EFF_CSUPP)) {
        trace_pci_nvme_err_invalid_opc(req->cmd.opcode);
        return NVME_INVALID_OPCODE | NVME_DNR;
    }

    switch (req->cmd.opcode) {
    case NVME_CMD_FLUSH:
        return nvme_flush(n, req);
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
    if (unlikely(!qsize || qsize > NVME_CAP_MQES(n->bar.cap))) {
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

        for (i = 1; i <= n->num_namespaces; i++) {
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

    return nvme_dma(n, (uint8_t *) &smart + off, trans_len,
                    DMA_DIRECTION_FROM_DEVICE, req);
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

    return nvme_dma(n, (uint8_t *) &fw_log + off, trans_len,
                    DMA_DIRECTION_FROM_DEVICE, req);
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

    return nvme_dma(n, (uint8_t *)&errlog, trans_len,
                    DMA_DIRECTION_FROM_DEVICE, req);
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

    switch (NVME_CC_CSS(n->bar.cc)) {
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

    return nvme_dma(n, ((uint8_t *)&log) + off, trans_len,
                    DMA_DIRECTION_FROM_DEVICE, req);
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
        trace_pci_nvme_err_mdts(nvme_cid(req), len);
        return status;
    }

    switch (lid) {
    case NVME_LOG_ERROR_INFO:
        return nvme_error_info(n, rae, len, off, req);
    case NVME_LOG_SMART_INFO:
        return nvme_smart_info(n, rae, len, off, req);
    case NVME_LOG_FW_SLOT_INFO:
        return nvme_fw_log_info(n, len, off, req);
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
    if (unlikely(!qsize || qsize > NVME_CAP_MQES(n->bar.cap))) {
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

    return nvme_dma(n, id, sizeof(id), DMA_DIRECTION_FROM_DEVICE, req);
}

static inline bool nvme_csi_has_nvm_support(NvmeNamespace *ns)
{
    switch (ns->csi) {
    case NVME_CSI_NVM:
    case NVME_CSI_ZONED:
        return true;
    }
    return false;
}

static uint16_t nvme_identify_ctrl(NvmeCtrl *n, NvmeRequest *req)
{
    trace_pci_nvme_identify_ctrl();

    return nvme_dma(n, (uint8_t *)&n->id_ctrl, sizeof(n->id_ctrl),
                    DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_identify_ctrl_csi(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    NvmeIdCtrlZoned id = {};

    trace_pci_nvme_identify_ctrl_csi(c->csi);

    if (c->csi == NVME_CSI_NVM) {
        return nvme_rpt_empty_id_struct(n, req);
    } else if (c->csi == NVME_CSI_ZONED) {
        if (n->params.zasl_bs) {
            id.zasl = n->zasl;
        }
        return nvme_dma(n, (uint8_t *)&id, sizeof(id),
                        DMA_DIRECTION_FROM_DEVICE, req);
    }

    return NVME_INVALID_FIELD | NVME_DNR;
}

static uint16_t nvme_identify_ns(NvmeCtrl *n, NvmeRequest *req)
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
        return nvme_rpt_empty_id_struct(n, req);
    }

    if (c->csi == NVME_CSI_NVM && nvme_csi_has_nvm_support(ns)) {
        return nvme_dma(n, (uint8_t *)&ns->id_ns, sizeof(NvmeIdNs),
                        DMA_DIRECTION_FROM_DEVICE, req);
    }

    return NVME_INVALID_CMD_SET | NVME_DNR;
}

static uint16_t nvme_identify_ns_csi(NvmeCtrl *n, NvmeRequest *req)
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
        return nvme_rpt_empty_id_struct(n, req);
    }

    if (c->csi == NVME_CSI_NVM && nvme_csi_has_nvm_support(ns)) {
        return nvme_rpt_empty_id_struct(n, req);
    } else if (c->csi == NVME_CSI_ZONED && ns->csi == NVME_CSI_ZONED) {
        return nvme_dma(n, (uint8_t *)ns->id_ns_zoned, sizeof(NvmeIdNsZoned),
                        DMA_DIRECTION_FROM_DEVICE, req);
    }

    return NVME_INVALID_FIELD | NVME_DNR;
}

static uint16_t nvme_identify_nslist(NvmeCtrl *n, NvmeRequest *req)
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
     * Both 0xffffffff (NVME_NSID_BROADCAST) and 0xfffffffe are invalid values
     * since the Active Namespace ID List should return namespaces with ids
     * *higher* than the NSID specified in the command. This is also specified
     * in the spec (NVM Express v1.3d, Section 5.15.4).
     */
    if (min_nsid >= NVME_NSID_BROADCAST - 1) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    for (i = 1; i <= n->num_namespaces; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }
        if (ns->params.nsid <= min_nsid) {
            continue;
        }
        list_ptr[j++] = cpu_to_le32(ns->params.nsid);
        if (j == data_len / sizeof(uint32_t)) {
            break;
        }
    }

    return nvme_dma(n, list, data_len, DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_identify_nslist_csi(NvmeCtrl *n, NvmeRequest *req)
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
     * Same as in nvme_identify_nslist(), 0xffffffff/0xfffffffe are invalid.
     */
    if (min_nsid >= NVME_NSID_BROADCAST - 1) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    if (c->csi != NVME_CSI_NVM && c->csi != NVME_CSI_ZONED) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    for (i = 1; i <= n->num_namespaces; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }
        if (ns->params.nsid <= min_nsid || c->csi != ns->csi) {
            continue;
        }
        list_ptr[j++] = cpu_to_le32(ns->params.nsid);
        if (j == data_len / sizeof(uint32_t)) {
            break;
        }
    }

    return nvme_dma(n, list, data_len, DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_identify_ns_descr_list(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeNamespace *ns;
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;
    uint32_t nsid = le32_to_cpu(c->nsid);
    uint8_t list[NVME_IDENTIFY_DATA_SIZE] = {};

    struct data {
        struct {
            NvmeIdNsDescr hdr;
            uint8_t v[NVME_NIDL_UUID];
        } uuid;
        struct {
            NvmeIdNsDescr hdr;
            uint8_t v;
        } csi;
    };

    struct data *ns_descrs = (struct data *)list;

    trace_pci_nvme_identify_ns_descr_list(nsid);

    if (!nvme_nsid_valid(n, nsid) || nsid == NVME_NSID_BROADCAST) {
        return NVME_INVALID_NSID | NVME_DNR;
    }

    ns = nvme_ns(n, nsid);
    if (unlikely(!ns)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    /*
     * Because the NGUID and EUI64 fields are 0 in the Identify Namespace data
     * structure, a Namespace UUID (nidt = 0x3) must be reported in the
     * Namespace Identification Descriptor. Add the namespace UUID here.
     */
    ns_descrs->uuid.hdr.nidt = NVME_NIDT_UUID;
    ns_descrs->uuid.hdr.nidl = NVME_NIDL_UUID;
    memcpy(&ns_descrs->uuid.v, ns->params.uuid.data, NVME_NIDL_UUID);

    ns_descrs->csi.hdr.nidt = NVME_NIDT_CSI;
    ns_descrs->csi.hdr.nidl = NVME_NIDL_CSI;
    ns_descrs->csi.v = ns->csi;

    return nvme_dma(n, list, sizeof(list), DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_identify_cmd_set(NvmeCtrl *n, NvmeRequest *req)
{
    uint8_t list[NVME_IDENTIFY_DATA_SIZE] = {};
    static const int data_len = sizeof(list);

    trace_pci_nvme_identify_cmd_set();

    NVME_SET_CSI(*list, NVME_CSI_NVM);
    NVME_SET_CSI(*list, NVME_CSI_ZONED);

    return nvme_dma(n, list, data_len, DMA_DIRECTION_FROM_DEVICE, req);
}

static uint16_t nvme_identify(NvmeCtrl *n, NvmeRequest *req)
{
    NvmeIdentify *c = (NvmeIdentify *)&req->cmd;

    switch (le32_to_cpu(c->cns)) {
    case NVME_ID_CNS_NS:
         /* fall through */
    case NVME_ID_CNS_NS_PRESENT:
        return nvme_identify_ns(n, req);
    case NVME_ID_CNS_CS_NS:
         /* fall through */
    case NVME_ID_CNS_CS_NS_PRESENT:
        return nvme_identify_ns_csi(n, req);
    case NVME_ID_CNS_CTRL:
        return nvme_identify_ctrl(n, req);
    case NVME_ID_CNS_CS_CTRL:
        return nvme_identify_ctrl_csi(n, req);
    case NVME_ID_CNS_NS_ACTIVE_LIST:
         /* fall through */
    case NVME_ID_CNS_NS_PRESENT_LIST:
        return nvme_identify_nslist(n, req);
    case NVME_ID_CNS_CS_NS_ACTIVE_LIST:
         /* fall through */
    case NVME_ID_CNS_CS_NS_PRESENT_LIST:
        return nvme_identify_nslist_csi(n, req);
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

    return nvme_dma(n, (uint8_t *)&timestamp, sizeof(timestamp),
                    DMA_DIRECTION_FROM_DEVICE, req);
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
             * NSID is 0xFFFFFFFF. Since the device does not support those
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
        for (i = 1; i <= n->num_namespaces; i++) {
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
    case NVME_COMMAND_SET_PROFILE:
        result = 0;
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

    ret = nvme_dma(n, (uint8_t *)&timestamp, sizeof(timestamp),
                   DMA_DIRECTION_TO_DEVICE, req);
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
            for (i = 1; i <= n->num_namespaces; i++) {
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
        for (i = 1; i <= n->num_namespaces; i++) {
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
         * NVMe v1.3, Section 5.21.1.7: 0xffff is not an allowed value for NCQR
         * and NSQR.
         */
        if ((dw11 & 0xffff) == 0xffff || ((dw11 >> 16) & 0xffff) == 0xffff) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }

        trace_pci_nvme_setfeat_numq((dw11 & 0xFFFF) + 1,
                                    ((dw11 >> 16) & 0xFFFF) + 1,
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

static uint16_t nvme_admin_cmd(NvmeCtrl *n, NvmeRequest *req)
{
    trace_pci_nvme_admin_cmd(nvme_cid(req), nvme_sqid(req), req->cmd.opcode,
                             nvme_adm_opc_str(req->cmd.opcode));

    if (!(nvme_cse_acs[req->cmd.opcode] & NVME_CMD_EFF_CSUPP)) {
        trace_pci_nvme_err_invalid_admin_opc(req->cmd.opcode);
        return NVME_INVALID_OPCODE | NVME_DNR;
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
            n->bar.csts = NVME_CSTS_FAILED;
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

    for (i = 1; i <= n->num_namespaces; i++) {
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

    n->bar.cc = 0;
}

static void nvme_ctrl_shutdown(NvmeCtrl *n)
{
    NvmeNamespace *ns;
    int i;

    if (n->pmr.dev) {
        memory_region_msync(&n->pmr.dev->mr, 0, n->pmr.dev->size);
    }

    for (i = 1; i <= n->num_namespaces; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }

        nvme_ns_shutdown(ns);
    }
}

static void nvme_select_ns_iocs(NvmeCtrl *n)
{
    NvmeNamespace *ns;
    int i;

    for (i = 1; i <= n->num_namespaces; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }
        ns->iocs = nvme_cse_iocs_none;
        switch (ns->csi) {
        case NVME_CSI_NVM:
            if (NVME_CC_CSS(n->bar.cc) != NVME_CC_CSS_ADMIN_ONLY) {
                ns->iocs = nvme_cse_iocs_nvm;
            }
            break;
        case NVME_CSI_ZONED:
            if (NVME_CC_CSS(n->bar.cc) == NVME_CC_CSS_CSI) {
                ns->iocs = nvme_cse_iocs_zoned;
            } else if (NVME_CC_CSS(n->bar.cc) == NVME_CC_CSS_NVM) {
                ns->iocs = nvme_cse_iocs_nvm;
            }
            break;
        }
    }
}

static int nvme_start_ctrl(NvmeCtrl *n)
{
    uint32_t page_bits = NVME_CC_MPS(n->bar.cc) + 12;
    uint32_t page_size = 1 << page_bits;

    if (unlikely(n->cq[0])) {
        trace_pci_nvme_err_startfail_cq();
        return -1;
    }
    if (unlikely(n->sq[0])) {
        trace_pci_nvme_err_startfail_sq();
        return -1;
    }
    if (unlikely(!n->bar.asq)) {
        trace_pci_nvme_err_startfail_nbarasq();
        return -1;
    }
    if (unlikely(!n->bar.acq)) {
        trace_pci_nvme_err_startfail_nbaracq();
        return -1;
    }
    if (unlikely(n->bar.asq & (page_size - 1))) {
        trace_pci_nvme_err_startfail_asq_misaligned(n->bar.asq);
        return -1;
    }
    if (unlikely(n->bar.acq & (page_size - 1))) {
        trace_pci_nvme_err_startfail_acq_misaligned(n->bar.acq);
        return -1;
    }
    if (unlikely(!(NVME_CAP_CSS(n->bar.cap) & (1 << NVME_CC_CSS(n->bar.cc))))) {
        trace_pci_nvme_err_startfail_css(NVME_CC_CSS(n->bar.cc));
        return -1;
    }
    if (unlikely(NVME_CC_MPS(n->bar.cc) <
                 NVME_CAP_MPSMIN(n->bar.cap))) {
        trace_pci_nvme_err_startfail_page_too_small(
                    NVME_CC_MPS(n->bar.cc),
                    NVME_CAP_MPSMIN(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_MPS(n->bar.cc) >
                 NVME_CAP_MPSMAX(n->bar.cap))) {
        trace_pci_nvme_err_startfail_page_too_large(
                    NVME_CC_MPS(n->bar.cc),
                    NVME_CAP_MPSMAX(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOCQES(n->bar.cc) <
                 NVME_CTRL_CQES_MIN(n->id_ctrl.cqes))) {
        trace_pci_nvme_err_startfail_cqent_too_small(
                    NVME_CC_IOCQES(n->bar.cc),
                    NVME_CTRL_CQES_MIN(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOCQES(n->bar.cc) >
                 NVME_CTRL_CQES_MAX(n->id_ctrl.cqes))) {
        trace_pci_nvme_err_startfail_cqent_too_large(
                    NVME_CC_IOCQES(n->bar.cc),
                    NVME_CTRL_CQES_MAX(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOSQES(n->bar.cc) <
                 NVME_CTRL_SQES_MIN(n->id_ctrl.sqes))) {
        trace_pci_nvme_err_startfail_sqent_too_small(
                    NVME_CC_IOSQES(n->bar.cc),
                    NVME_CTRL_SQES_MIN(n->bar.cap));
        return -1;
    }
    if (unlikely(NVME_CC_IOSQES(n->bar.cc) >
                 NVME_CTRL_SQES_MAX(n->id_ctrl.sqes))) {
        trace_pci_nvme_err_startfail_sqent_too_large(
                    NVME_CC_IOSQES(n->bar.cc),
                    NVME_CTRL_SQES_MAX(n->bar.cap));
        return -1;
    }
    if (unlikely(!NVME_AQA_ASQS(n->bar.aqa))) {
        trace_pci_nvme_err_startfail_asqent_sz_zero();
        return -1;
    }
    if (unlikely(!NVME_AQA_ACQS(n->bar.aqa))) {
        trace_pci_nvme_err_startfail_acqent_sz_zero();
        return -1;
    }

    n->page_bits = page_bits;
    n->page_size = page_size;
    n->max_prp_ents = n->page_size / sizeof(uint64_t);
    n->cqe_size = 1 << NVME_CC_IOCQES(n->bar.cc);
    n->sqe_size = 1 << NVME_CC_IOSQES(n->bar.cc);
    nvme_init_cq(&n->admin_cq, n, n->bar.acq, 0, 0,
                 NVME_AQA_ACQS(n->bar.aqa) + 1, 1);
    nvme_init_sq(&n->admin_sq, n, n->bar.asq, 0, 0,
                 NVME_AQA_ASQS(n->bar.aqa) + 1);

    if (!n->params.zasl_bs) {
        n->zasl = n->params.mdts;
    } else {
        if (n->params.zasl_bs < n->page_size) {
            trace_pci_nvme_err_startfail_zasl_too_small(n->params.zasl_bs,
                                                        n->page_size);
            return -1;
        }
        n->zasl = 31 - clz32(n->params.zasl_bs / n->page_size);
    }

    nvme_set_timestamp(n, 0ULL);

    QTAILQ_INIT(&n->aer_queue);

    nvme_select_ns_iocs(n);

    return 0;
}

static void nvme_cmb_enable_regs(NvmeCtrl *n)
{
    NVME_CMBLOC_SET_CDPCILS(n->bar.cmbloc, 1);
    NVME_CMBLOC_SET_CDPMLS(n->bar.cmbloc, 1);
    NVME_CMBLOC_SET_BIR(n->bar.cmbloc, NVME_CMB_BIR);

    NVME_CMBSZ_SET_SQS(n->bar.cmbsz, 1);
    NVME_CMBSZ_SET_CQS(n->bar.cmbsz, 0);
    NVME_CMBSZ_SET_LISTS(n->bar.cmbsz, 1);
    NVME_CMBSZ_SET_RDS(n->bar.cmbsz, 1);
    NVME_CMBSZ_SET_WDS(n->bar.cmbsz, 1);
    NVME_CMBSZ_SET_SZU(n->bar.cmbsz, 2); /* MBs */
    NVME_CMBSZ_SET_SZ(n->bar.cmbsz, n->params.cmb_size_mb);
}

static void nvme_write_bar(NvmeCtrl *n, hwaddr offset, uint64_t data,
                           unsigned size)
{
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
    case 0xc:   /* INTMS */
        if (unlikely(msix_enabled(&(n->parent_obj)))) {
            NVME_GUEST_ERR(pci_nvme_ub_mmiowr_intmask_with_msix,
                           "undefined access to interrupt mask set"
                           " when MSI-X is enabled");
            /* should be ignored, fall through for now */
        }
        n->bar.intms |= data & 0xffffffff;
        n->bar.intmc = n->bar.intms;
        trace_pci_nvme_mmio_intm_set(data & 0xffffffff, n->bar.intmc);
        nvme_irq_check(n);
        break;
    case 0x10:  /* INTMC */
        if (unlikely(msix_enabled(&(n->parent_obj)))) {
            NVME_GUEST_ERR(pci_nvme_ub_mmiowr_intmask_with_msix,
                           "undefined access to interrupt mask clr"
                           " when MSI-X is enabled");
            /* should be ignored, fall through for now */
        }
        n->bar.intms &= ~(data & 0xffffffff);
        n->bar.intmc = n->bar.intms;
        trace_pci_nvme_mmio_intm_clr(data & 0xffffffff, n->bar.intmc);
        nvme_irq_check(n);
        break;
    case 0x14:  /* CC */
        trace_pci_nvme_mmio_cfg(data & 0xffffffff);
        /* Windows first sends data, then sends enable bit */
        if (!NVME_CC_EN(data) && !NVME_CC_EN(n->bar.cc) &&
            !NVME_CC_SHN(data) && !NVME_CC_SHN(n->bar.cc))
        {
            n->bar.cc = data;
        }

        if (NVME_CC_EN(data) && !NVME_CC_EN(n->bar.cc)) {
            n->bar.cc = data;
            if (unlikely(nvme_start_ctrl(n))) {
                trace_pci_nvme_err_startfail();
                n->bar.csts = NVME_CSTS_FAILED;
            } else {
                trace_pci_nvme_mmio_start_success();
                n->bar.csts = NVME_CSTS_READY;
            }
        } else if (!NVME_CC_EN(data) && NVME_CC_EN(n->bar.cc)) {
            trace_pci_nvme_mmio_stopped();
            nvme_ctrl_reset(n);
            n->bar.csts &= ~NVME_CSTS_READY;
        }
        if (NVME_CC_SHN(data) && !(NVME_CC_SHN(n->bar.cc))) {
            trace_pci_nvme_mmio_shutdown_set();
            nvme_ctrl_shutdown(n);
            n->bar.cc = data;
            n->bar.csts |= NVME_CSTS_SHST_COMPLETE;
        } else if (!NVME_CC_SHN(data) && NVME_CC_SHN(n->bar.cc)) {
            trace_pci_nvme_mmio_shutdown_cleared();
            n->bar.csts &= ~NVME_CSTS_SHST_COMPLETE;
            n->bar.cc = data;
        }
        break;
    case 0x1C:  /* CSTS */
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
    case 0x20:  /* NSSR */
        if (data == 0x4E564D65) {
            trace_pci_nvme_ub_mmiowr_ssreset_unsupported();
        } else {
            /* The spec says that writes of other values have no effect */
            return;
        }
        break;
    case 0x24:  /* AQA */
        n->bar.aqa = data & 0xffffffff;
        trace_pci_nvme_mmio_aqattr(data & 0xffffffff);
        break;
    case 0x28:  /* ASQ */
        n->bar.asq = size == 8 ? data :
            (n->bar.asq & ~0xffffffffULL) | (data & 0xffffffff);
        trace_pci_nvme_mmio_asqaddr(data);
        break;
    case 0x2c:  /* ASQ hi */
        n->bar.asq = (n->bar.asq & 0xffffffff) | (data << 32);
        trace_pci_nvme_mmio_asqaddr_hi(data, n->bar.asq);
        break;
    case 0x30:  /* ACQ */
        trace_pci_nvme_mmio_acqaddr(data);
        n->bar.acq = size == 8 ? data :
            (n->bar.acq & ~0xffffffffULL) | (data & 0xffffffff);
        break;
    case 0x34:  /* ACQ hi */
        n->bar.acq = (n->bar.acq & 0xffffffff) | (data << 32);
        trace_pci_nvme_mmio_acqaddr_hi(data, n->bar.acq);
        break;
    case 0x38:  /* CMBLOC */
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_cmbloc_reserved,
                       "invalid write to reserved CMBLOC"
                       " when CMBSZ is zero, ignored");
        return;
    case 0x3C:  /* CMBSZ */
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_cmbsz_readonly,
                       "invalid write to read only CMBSZ, ignored");
        return;
    case 0x50:  /* CMBMSC */
        if (!NVME_CAP_CMBS(n->bar.cap)) {
            return;
        }

        n->bar.cmbmsc = size == 8 ? data :
            (n->bar.cmbmsc & ~0xffffffff) | (data & 0xffffffff);
        n->cmb.cmse = false;

        if (NVME_CMBMSC_CRE(data)) {
            nvme_cmb_enable_regs(n);

            if (NVME_CMBMSC_CMSE(data)) {
                hwaddr cba = NVME_CMBMSC_CBA(data) << CMBMSC_CBA_SHIFT;
                if (cba + int128_get64(n->cmb.mem.size) < cba) {
                    NVME_CMBSTS_SET_CBAI(n->bar.cmbsts, 1);
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
    case 0x54:  /* CMBMSC hi */
        n->bar.cmbmsc = (n->bar.cmbmsc & 0xffffffff) | (data << 32);
        return;

    case 0xE00: /* PMRCAP */
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_pmrcap_readonly,
                       "invalid write to PMRCAP register, ignored");
        return;
    case 0xE04: /* PMRCTL */
        n->bar.pmrctl = data;
        if (NVME_PMRCTL_EN(data)) {
            memory_region_set_enabled(&n->pmr.dev->mr, true);
            n->bar.pmrsts = 0;
        } else {
            memory_region_set_enabled(&n->pmr.dev->mr, false);
            NVME_PMRSTS_SET_NRDY(n->bar.pmrsts, 1);
            n->pmr.cmse = false;
        }
        return;
    case 0xE08: /* PMRSTS */
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_pmrsts_readonly,
                       "invalid write to PMRSTS register, ignored");
        return;
    case 0xE0C: /* PMREBS */
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_pmrebs_readonly,
                       "invalid write to PMREBS register, ignored");
        return;
    case 0xE10: /* PMRSWTP */
        NVME_GUEST_ERR(pci_nvme_ub_mmiowr_pmrswtp_readonly,
                       "invalid write to PMRSWTP register, ignored");
        return;
    case 0xE14: /* PMRMSCL */
        if (!NVME_CAP_PMRS(n->bar.cap)) {
            return;
        }

        n->bar.pmrmsc = (n->bar.pmrmsc & ~0xffffffff) | (data & 0xffffffff);
        n->pmr.cmse = false;

        if (NVME_PMRMSC_CMSE(n->bar.pmrmsc)) {
            hwaddr cba = NVME_PMRMSC_CBA(n->bar.pmrmsc) << PMRMSC_CBA_SHIFT;
            if (cba + int128_get64(n->pmr.dev->mr.size) < cba) {
                NVME_PMRSTS_SET_CBAI(n->bar.pmrsts, 1);
                return;
            }

            n->pmr.cmse = true;
            n->pmr.cba = cba;
        }

        return;
    case 0xE18: /* PMRMSCU */
        if (!NVME_CAP_PMRS(n->bar.cap)) {
            return;
        }

        n->bar.pmrmsc = (n->bar.pmrmsc & 0xffffffff) | (data << 32);
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
    uint64_t val = 0;

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

    if (addr < sizeof(n->bar)) {
        /*
         * When PMRWBM bit 1 is set then read from
         * from PMRSTS should ensure prior writes
         * made it to persistent media
         */
        if (addr == 0xE08 &&
            (NVME_PMRCAP_PMRWBM(n->bar.pmrcap) & 0x02)) {
            memory_region_msync(&n->pmr.dev->mr, 0, n->pmr.dev->size);
        }
        memcpy(&val, ptr + addr, size);
    } else {
        NVME_GUEST_ERR(pci_nvme_ub_mmiord_invalid_ofs,
                       "MMIO read beyond last register,"
                       " offset=0x%"PRIx64", returning 0", addr);
    }

    return val;
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

    if (n->conf.blk) {
        warn_report("drive property is deprecated; "
                    "please use an nvme-ns device instead");
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

    if (n->params.zasl_bs) {
        if (!is_power_of_2(n->params.zasl_bs)) {
            error_setg(errp, "zone append size limit has to be a power of 2");
            return;
        }
    }
}

static void nvme_init_state(NvmeCtrl *n)
{
    n->num_namespaces = NVME_MAX_NAMESPACES;
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

int nvme_register_namespace(NvmeCtrl *n, NvmeNamespace *ns, Error **errp)
{
    uint32_t nsid = nvme_nsid(ns);

    if (nsid > NVME_MAX_NAMESPACES) {
        error_setg(errp, "invalid namespace id (must be between 0 and %d)",
                   NVME_MAX_NAMESPACES);
        return -1;
    }

    if (!nsid) {
        for (int i = 1; i <= n->num_namespaces; i++) {
            if (!nvme_ns(n, i)) {
                nsid = ns->params.nsid = i;
                break;
            }
        }

        if (!nsid) {
            error_setg(errp, "no free namespace id");
            return -1;
        }
    } else {
        if (n->namespaces[nsid - 1]) {
            error_setg(errp, "namespace id '%d' is already in use", nsid);
            return -1;
        }
    }

    trace_pci_nvme_register_namespace(nsid);

    n->namespaces[nsid - 1] = ns;

    return 0;
}

static void nvme_init_cmb(NvmeCtrl *n, PCIDevice *pci_dev)
{
    uint64_t cmb_size = n->params.cmb_size_mb * MiB;

    n->cmb.buf = g_malloc0(cmb_size);
    memory_region_init_io(&n->cmb.mem, OBJECT(n), &nvme_cmb_ops, n,
                          "nvme-cmb", cmb_size);
    pci_register_bar(pci_dev, NVME_CMB_BIR,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64 |
                     PCI_BASE_ADDRESS_MEM_PREFETCH, &n->cmb.mem);

    NVME_CAP_SET_CMBS(n->bar.cap, 1);

    if (n->params.legacy_cmb) {
        nvme_cmb_enable_regs(n);
        n->cmb.cmse = true;
    }
}

static void nvme_init_pmr(NvmeCtrl *n, PCIDevice *pci_dev)
{
    NVME_PMRCAP_SET_RDS(n->bar.pmrcap, 1);
    NVME_PMRCAP_SET_WDS(n->bar.pmrcap, 1);
    NVME_PMRCAP_SET_BIR(n->bar.pmrcap, NVME_PMR_BIR);
    /* Turn on bit 1 support */
    NVME_PMRCAP_SET_PMRWBM(n->bar.pmrcap, 0x02);
    NVME_PMRCAP_SET_CMSS(n->bar.pmrcap, 1);

    pci_register_bar(pci_dev, NVME_PMRCAP_BIR(n->bar.pmrcap),
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

static void nvme_init_ctrl(NvmeCtrl *n, PCIDevice *pci_dev)
{
    NvmeIdCtrl *id = &n->id_ctrl;
    uint8_t *pci_conf = pci_dev->config;
    char *subnqn;

    id->vid = cpu_to_le16(pci_get_word(pci_conf + PCI_VENDOR_ID));
    id->ssvid = cpu_to_le16(pci_get_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID));
    strpadcpy((char *)id->mn, sizeof(id->mn), "QEMU NVMe Ctrl", ' ');
    strpadcpy((char *)id->fr, sizeof(id->fr), "1.0", ' ');
    strpadcpy((char *)id->sn, sizeof(id->sn), n->params.serial, ' ');
    id->rab = 6;
    id->ieee[0] = 0x00;
    id->ieee[1] = 0x02;
    id->ieee[2] = 0xb3;
    id->mdts = n->params.mdts;
    id->ver = cpu_to_le32(NVME_SPEC_VER);
    id->oacs = cpu_to_le16(0);
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
    id->nn = cpu_to_le32(n->num_namespaces);
    id->oncs = cpu_to_le16(NVME_ONCS_WRITE_ZEROES | NVME_ONCS_TIMESTAMP |
                           NVME_ONCS_FEATURES | NVME_ONCS_DSM |
                           NVME_ONCS_COMPARE);

    id->vwc = (0x2 << 1) | 0x1;
    id->sgls = cpu_to_le32(NVME_CTRL_SGLS_SUPPORT_NO_ALIGN |
                           NVME_CTRL_SGLS_BITBUCKET);

    subnqn = g_strdup_printf("nqn.2019-08.org.qemu:%s", n->params.serial);
    strpadcpy((char *)id->subnqn, sizeof(id->subnqn), subnqn, '\0');
    g_free(subnqn);

    id->psd[0].mp = cpu_to_le16(0x9c4);
    id->psd[0].enlat = cpu_to_le32(0x10);
    id->psd[0].exlat = cpu_to_le32(0x4);

    NVME_CAP_SET_MQES(n->bar.cap, 0x7ff);
    NVME_CAP_SET_CQR(n->bar.cap, 1);
    NVME_CAP_SET_TO(n->bar.cap, 0xf);
    NVME_CAP_SET_CSS(n->bar.cap, NVME_CAP_CSS_NVM);
    NVME_CAP_SET_CSS(n->bar.cap, NVME_CAP_CSS_CSI_SUPP);
    NVME_CAP_SET_CSS(n->bar.cap, NVME_CAP_CSS_ADMIN_ONLY);
    NVME_CAP_SET_MPSMAX(n->bar.cap, 4);
    NVME_CAP_SET_CMBS(n->bar.cap, n->params.cmb_size_mb ? 1 : 0);
    NVME_CAP_SET_PMRS(n->bar.cap, n->pmr.dev ? 1 : 0);

    n->bar.vs = NVME_SPEC_VER;
    n->bar.intmc = n->bar.intms = 0;
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

    qbus_create_inplace(&n->bus, sizeof(NvmeBus), TYPE_NVME_BUS,
                        &pci_dev->qdev, n->parent_obj.qdev.id);

    nvme_init_state(n);
    if (nvme_init_pci(n, pci_dev, errp)) {
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

        if (nvme_register_namespace(n, ns, errp)) {
            return;
        }
    }
}

static void nvme_exit(PCIDevice *pci_dev)
{
    NvmeCtrl *n = NVME(pci_dev);
    NvmeNamespace *ns;
    int i;

    nvme_ctrl_reset(n);

    for (i = 1; i <= n->num_namespaces; i++) {
        ns = nvme_ns(n, i);
        if (!ns) {
            continue;
        }

        nvme_ns_cleanup(ns);
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
    msix_uninit_exclusive_bar(pci_dev);
}

static Property nvme_props[] = {
    DEFINE_BLOCK_PROPERTIES(NvmeCtrl, namespace.blkconf),
    DEFINE_PROP_LINK("pmrdev", NvmeCtrl, pmr.dev, TYPE_MEMORY_BACKEND,
                     HostMemoryBackend *),
    DEFINE_PROP_STRING("serial", NvmeCtrl, params.serial),
    DEFINE_PROP_UINT32("cmb_size_mb", NvmeCtrl, params.cmb_size_mb, 0),
    DEFINE_PROP_UINT32("num_queues", NvmeCtrl, params.num_queues, 0),
    DEFINE_PROP_UINT32("max_ioqpairs", NvmeCtrl, params.max_ioqpairs, 64),
    DEFINE_PROP_UINT16("msix_qsize", NvmeCtrl, params.msix_qsize, 65),
    DEFINE_PROP_UINT8("aerl", NvmeCtrl, params.aerl, 3),
    DEFINE_PROP_UINT32("aer_max_queued", NvmeCtrl, params.aer_max_queued, 64),
    DEFINE_PROP_UINT8("mdts", NvmeCtrl, params.mdts, 7),
    DEFINE_PROP_BOOL("use-intel-id", NvmeCtrl, params.use_intel_id, false),
    DEFINE_PROP_BOOL("legacy-cmb", NvmeCtrl, params.legacy_cmb, false),
    DEFINE_PROP_SIZE32("zoned.append_size_limit", NvmeCtrl, params.zasl_bs,
                       NVME_DEFAULT_MAX_ZA_SIZE),
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
    if (NVME_CAP_PMRS(n->bar.cap)) {
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

    if (n->namespace.blkconf.blk) {
        device_add_bootindex_property(obj, &n->namespace.blkconf.bootindex,
                                      "bootindex", "/namespace@1,0",
                                      DEVICE(obj));
    }

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
