/*
 * QEMU NVM Express
 *
 * Copyright (c) 2012 Intel Corporation
 * Copyright (c) 2021 Minwoo Im
 * Copyright (c) 2021 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *   Keith Busch            <kbusch@kernel.org>
 *   Klaus Jensen           <k.jensen@samsung.com>
 *   Gollu Appalanaidu      <anaidu.gollu@samsung.com>
 *   Dmitry Fomichev        <dmitry.fomichev@wdc.com>
 *   Minwoo Im              <minwoo.im.dev@gmail.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */

#ifndef HW_NVME_NVME_H
#define HW_NVME_NVME_H

#include "qemu/uuid.h"
#include "hw/pci/pci_device.h"
#include "hw/block/block.h"

#include "block/nvme.h"

#define NVME_MAX_CONTROLLERS 256
#define NVME_MAX_NAMESPACES  256
#define NVME_EUI64_DEFAULT ((uint64_t)0x5254000000000000)

QEMU_BUILD_BUG_ON(NVME_MAX_NAMESPACES > NVME_NSID_BROADCAST - 1);

typedef struct NvmeCtrl NvmeCtrl;
typedef struct NvmeNamespace NvmeNamespace;

#define TYPE_NVME_BUS "nvme-bus"
OBJECT_DECLARE_SIMPLE_TYPE(NvmeBus, NVME_BUS)

typedef struct NvmeBus {
    BusState parent_bus;
} NvmeBus;

#define TYPE_NVME_SUBSYS "nvme-subsys"
#define NVME_SUBSYS(obj) \
    OBJECT_CHECK(NvmeSubsystem, (obj), TYPE_NVME_SUBSYS)
#define SUBSYS_SLOT_RSVD (void *)0xFFFF

typedef struct NvmeSubsystem {
    DeviceState parent_obj;
    NvmeBus     bus;
    uint8_t     subnqn[256];
    char        *serial;

    NvmeCtrl      *ctrls[NVME_MAX_CONTROLLERS];
    NvmeNamespace *namespaces[NVME_MAX_NAMESPACES + 1];

    struct {
        char *nqn;
    } params;
} NvmeSubsystem;

int nvme_subsys_register_ctrl(NvmeCtrl *n, Error **errp);
void nvme_subsys_unregister_ctrl(NvmeSubsystem *subsys, NvmeCtrl *n);

static inline NvmeCtrl *nvme_subsys_ctrl(NvmeSubsystem *subsys,
                                         uint32_t cntlid)
{
    if (!subsys || cntlid >= NVME_MAX_CONTROLLERS) {
        return NULL;
    }

    if (subsys->ctrls[cntlid] == SUBSYS_SLOT_RSVD) {
        return NULL;
    }

    return subsys->ctrls[cntlid];
}

static inline NvmeNamespace *nvme_subsys_ns(NvmeSubsystem *subsys,
                                            uint32_t nsid)
{
    if (!subsys || !nsid || nsid > NVME_MAX_NAMESPACES) {
        return NULL;
    }

    return subsys->namespaces[nsid];
}

#define TYPE_NVME_NS "nvme-ns"
#define NVME_NS(obj) \
    OBJECT_CHECK(NvmeNamespace, (obj), TYPE_NVME_NS)

typedef struct NvmeZone {
    NvmeZoneDescr   d;
    uint64_t        w_ptr;
    QTAILQ_ENTRY(NvmeZone) entry;
} NvmeZone;

typedef struct NvmeNamespaceParams {
    bool     detached;
    bool     shared;
    uint32_t nsid;
    QemuUUID uuid;
    uint64_t eui64;
    bool     eui64_default;

    uint16_t ms;
    uint8_t  mset;
    uint8_t  pi;
    uint8_t  pil;
    uint8_t  pif;

    uint16_t mssrl;
    uint32_t mcl;
    uint8_t  msrc;

    bool     zoned;
    bool     cross_zone_read;
    uint64_t zone_size_bs;
    uint64_t zone_cap_bs;
    uint32_t max_active_zones;
    uint32_t max_open_zones;
    uint32_t zd_extension_size;

    uint32_t numzrwa;
    uint64_t zrwas;
    uint64_t zrwafg;
} NvmeNamespaceParams;

typedef struct NvmeNamespace {
    DeviceState  parent_obj;
    BlockConf    blkconf;
    int32_t      bootindex;
    int64_t      size;
    int64_t      moff;
    NvmeIdNs     id_ns;
    NvmeIdNsNvm  id_ns_nvm;
    NvmeLBAF     lbaf;
    unsigned int nlbaf;
    size_t       lbasz;
    const uint32_t *iocs;
    uint8_t      csi;
    uint16_t     status;
    int          attached;
    uint8_t      pif;

    struct {
        uint16_t zrwas;
        uint16_t zrwafg;
        uint32_t numzrwa;
    } zns;

    QTAILQ_ENTRY(NvmeNamespace) entry;

    NvmeIdNsZoned   *id_ns_zoned;
    NvmeZone        *zone_array;
    QTAILQ_HEAD(, NvmeZone) exp_open_zones;
    QTAILQ_HEAD(, NvmeZone) imp_open_zones;
    QTAILQ_HEAD(, NvmeZone) closed_zones;
    QTAILQ_HEAD(, NvmeZone) full_zones;
    uint32_t        num_zones;
    uint64_t        zone_size;
    uint64_t        zone_capacity;
    uint32_t        zone_size_log2;
    uint8_t         *zd_extensions;
    int32_t         nr_open_zones;
    int32_t         nr_active_zones;

    NvmeNamespaceParams params;

    struct {
        uint32_t err_rec;
    } features;
} NvmeNamespace;

static inline uint32_t nvme_nsid(NvmeNamespace *ns)
{
    if (ns) {
        return ns->params.nsid;
    }

    return 0;
}

static inline size_t nvme_l2b(NvmeNamespace *ns, uint64_t lba)
{
    return lba << ns->lbaf.ds;
}

static inline size_t nvme_m2b(NvmeNamespace *ns, uint64_t lba)
{
    return ns->lbaf.ms * lba;
}

static inline int64_t nvme_moff(NvmeNamespace *ns, uint64_t lba)
{
    return ns->moff + nvme_m2b(ns, lba);
}

static inline bool nvme_ns_ext(NvmeNamespace *ns)
{
    return !!NVME_ID_NS_FLBAS_EXTENDED(ns->id_ns.flbas);
}

static inline NvmeZoneState nvme_get_zone_state(NvmeZone *zone)
{
    return zone->d.zs >> 4;
}

static inline void nvme_set_zone_state(NvmeZone *zone, NvmeZoneState state)
{
    zone->d.zs = state << 4;
}

static inline uint64_t nvme_zone_rd_boundary(NvmeNamespace *ns, NvmeZone *zone)
{
    return zone->d.zslba + ns->zone_size;
}

static inline uint64_t nvme_zone_wr_boundary(NvmeZone *zone)
{
    return zone->d.zslba + zone->d.zcap;
}

static inline bool nvme_wp_is_valid(NvmeZone *zone)
{
    uint8_t st = nvme_get_zone_state(zone);

    return st != NVME_ZONE_STATE_FULL &&
           st != NVME_ZONE_STATE_READ_ONLY &&
           st != NVME_ZONE_STATE_OFFLINE;
}

static inline uint8_t *nvme_get_zd_extension(NvmeNamespace *ns,
                                             uint32_t zone_idx)
{
    return &ns->zd_extensions[zone_idx * ns->params.zd_extension_size];
}

static inline void nvme_aor_inc_open(NvmeNamespace *ns)
{
    assert(ns->nr_open_zones >= 0);
    if (ns->params.max_open_zones) {
        ns->nr_open_zones++;
        assert(ns->nr_open_zones <= ns->params.max_open_zones);
    }
}

static inline void nvme_aor_dec_open(NvmeNamespace *ns)
{
    if (ns->params.max_open_zones) {
        assert(ns->nr_open_zones > 0);
        ns->nr_open_zones--;
    }
    assert(ns->nr_open_zones >= 0);
}

static inline void nvme_aor_inc_active(NvmeNamespace *ns)
{
    assert(ns->nr_active_zones >= 0);
    if (ns->params.max_active_zones) {
        ns->nr_active_zones++;
        assert(ns->nr_active_zones <= ns->params.max_active_zones);
    }
}

static inline void nvme_aor_dec_active(NvmeNamespace *ns)
{
    if (ns->params.max_active_zones) {
        assert(ns->nr_active_zones > 0);
        ns->nr_active_zones--;
        assert(ns->nr_active_zones >= ns->nr_open_zones);
    }
    assert(ns->nr_active_zones >= 0);
}

void nvme_ns_init_format(NvmeNamespace *ns);
int nvme_ns_setup(NvmeNamespace *ns, Error **errp);
void nvme_ns_drain(NvmeNamespace *ns);
void nvme_ns_shutdown(NvmeNamespace *ns);
void nvme_ns_cleanup(NvmeNamespace *ns);

typedef struct NvmeAsyncEvent {
    QTAILQ_ENTRY(NvmeAsyncEvent) entry;
    NvmeAerResult result;
} NvmeAsyncEvent;

enum {
    NVME_SG_ALLOC = 1 << 0,
    NVME_SG_DMA   = 1 << 1,
};

typedef struct NvmeSg {
    int flags;

    union {
        QEMUSGList   qsg;
        QEMUIOVector iov;
    };
} NvmeSg;

typedef enum NvmeTxDirection {
    NVME_TX_DIRECTION_TO_DEVICE   = 0,
    NVME_TX_DIRECTION_FROM_DEVICE = 1,
} NvmeTxDirection;

typedef struct NvmeRequest {
    struct NvmeSQueue       *sq;
    struct NvmeNamespace    *ns;
    BlockAIOCB              *aiocb;
    uint16_t                status;
    void                    *opaque;
    NvmeCqe                 cqe;
    NvmeCmd                 cmd;
    BlockAcctCookie         acct;
    NvmeSg                  sg;
    QTAILQ_ENTRY(NvmeRequest)entry;
} NvmeRequest;

typedef struct NvmeBounceContext {
    NvmeRequest *req;

    struct {
        QEMUIOVector iov;
        uint8_t *bounce;
    } data, mdata;
} NvmeBounceContext;

static inline const char *nvme_adm_opc_str(uint8_t opc)
{
    switch (opc) {
    case NVME_ADM_CMD_DELETE_SQ:        return "NVME_ADM_CMD_DELETE_SQ";
    case NVME_ADM_CMD_CREATE_SQ:        return "NVME_ADM_CMD_CREATE_SQ";
    case NVME_ADM_CMD_GET_LOG_PAGE:     return "NVME_ADM_CMD_GET_LOG_PAGE";
    case NVME_ADM_CMD_DELETE_CQ:        return "NVME_ADM_CMD_DELETE_CQ";
    case NVME_ADM_CMD_CREATE_CQ:        return "NVME_ADM_CMD_CREATE_CQ";
    case NVME_ADM_CMD_IDENTIFY:         return "NVME_ADM_CMD_IDENTIFY";
    case NVME_ADM_CMD_ABORT:            return "NVME_ADM_CMD_ABORT";
    case NVME_ADM_CMD_SET_FEATURES:     return "NVME_ADM_CMD_SET_FEATURES";
    case NVME_ADM_CMD_GET_FEATURES:     return "NVME_ADM_CMD_GET_FEATURES";
    case NVME_ADM_CMD_ASYNC_EV_REQ:     return "NVME_ADM_CMD_ASYNC_EV_REQ";
    case NVME_ADM_CMD_NS_ATTACHMENT:    return "NVME_ADM_CMD_NS_ATTACHMENT";
    case NVME_ADM_CMD_VIRT_MNGMT:       return "NVME_ADM_CMD_VIRT_MNGMT";
    case NVME_ADM_CMD_DBBUF_CONFIG:     return "NVME_ADM_CMD_DBBUF_CONFIG";
    case NVME_ADM_CMD_FORMAT_NVM:       return "NVME_ADM_CMD_FORMAT_NVM";
    default:                            return "NVME_ADM_CMD_UNKNOWN";
    }
}

static inline const char *nvme_io_opc_str(uint8_t opc)
{
    switch (opc) {
    case NVME_CMD_FLUSH:            return "NVME_NVM_CMD_FLUSH";
    case NVME_CMD_WRITE:            return "NVME_NVM_CMD_WRITE";
    case NVME_CMD_READ:             return "NVME_NVM_CMD_READ";
    case NVME_CMD_COMPARE:          return "NVME_NVM_CMD_COMPARE";
    case NVME_CMD_WRITE_ZEROES:     return "NVME_NVM_CMD_WRITE_ZEROES";
    case NVME_CMD_DSM:              return "NVME_NVM_CMD_DSM";
    case NVME_CMD_VERIFY:           return "NVME_NVM_CMD_VERIFY";
    case NVME_CMD_COPY:             return "NVME_NVM_CMD_COPY";
    case NVME_CMD_ZONE_MGMT_SEND:   return "NVME_ZONED_CMD_MGMT_SEND";
    case NVME_CMD_ZONE_MGMT_RECV:   return "NVME_ZONED_CMD_MGMT_RECV";
    case NVME_CMD_ZONE_APPEND:      return "NVME_ZONED_CMD_ZONE_APPEND";
    default:                        return "NVME_NVM_CMD_UNKNOWN";
    }
}

typedef struct NvmeSQueue {
    struct NvmeCtrl *ctrl;
    uint16_t    sqid;
    uint16_t    cqid;
    uint32_t    head;
    uint32_t    tail;
    uint32_t    size;
    uint64_t    dma_addr;
    uint64_t    db_addr;
    uint64_t    ei_addr;
    QEMUBH      *bh;
    EventNotifier notifier;
    bool        ioeventfd_enabled;
    NvmeRequest *io_req;
    QTAILQ_HEAD(, NvmeRequest) req_list;
    QTAILQ_HEAD(, NvmeRequest) out_req_list;
    QTAILQ_ENTRY(NvmeSQueue) entry;
} NvmeSQueue;

typedef struct NvmeCQueue {
    struct NvmeCtrl *ctrl;
    uint8_t     phase;
    uint16_t    cqid;
    uint16_t    irq_enabled;
    uint32_t    head;
    uint32_t    tail;
    uint32_t    vector;
    uint32_t    size;
    uint64_t    dma_addr;
    uint64_t    db_addr;
    uint64_t    ei_addr;
    QEMUBH      *bh;
    EventNotifier notifier;
    bool        ioeventfd_enabled;
    QTAILQ_HEAD(, NvmeSQueue) sq_list;
    QTAILQ_HEAD(, NvmeRequest) req_list;
} NvmeCQueue;

#define TYPE_NVME "nvme"
#define NVME(obj) \
        OBJECT_CHECK(NvmeCtrl, (obj), TYPE_NVME)

typedef struct NvmeParams {
    char     *serial;
    uint32_t num_queues; /* deprecated since 5.1 */
    uint32_t max_ioqpairs;
    uint16_t msix_qsize;
    uint32_t cmb_size_mb;
    uint8_t  aerl;
    uint32_t aer_max_queued;
    uint8_t  mdts;
    uint8_t  vsl;
    bool     use_intel_id;
    uint8_t  zasl;
    bool     auto_transition_zones;
    bool     legacy_cmb;
    bool     ioeventfd;
    uint8_t  sriov_max_vfs;
    uint16_t sriov_vq_flexible;
    uint16_t sriov_vi_flexible;
    uint8_t  sriov_max_vq_per_vf;
    uint8_t  sriov_max_vi_per_vf;
} NvmeParams;

typedef struct NvmeCtrl {
    PCIDevice    parent_obj;
    MemoryRegion bar0;
    MemoryRegion iomem;
    NvmeBar      bar;
    NvmeParams   params;
    NvmeBus      bus;

    uint16_t    cntlid;
    bool        qs_created;
    uint32_t    page_size;
    uint16_t    page_bits;
    uint16_t    max_prp_ents;
    uint16_t    cqe_size;
    uint16_t    sqe_size;
    uint32_t    max_q_ents;
    uint8_t     outstanding_aers;
    uint32_t    irq_status;
    int         cq_pending;
    uint64_t    host_timestamp;                 /* Timestamp sent by the host */
    uint64_t    timestamp_set_qemu_clock_ms;    /* QEMU clock time */
    uint64_t    starttime_ms;
    uint16_t    temperature;
    uint8_t     smart_critical_warning;
    uint32_t    conf_msix_qsize;
    uint32_t    conf_ioqpairs;
    uint64_t    dbbuf_dbs;
    uint64_t    dbbuf_eis;
    bool        dbbuf_enabled;

    struct {
        MemoryRegion mem;
        uint8_t      *buf;
        bool         cmse;
        hwaddr       cba;
    } cmb;

    struct {
        HostMemoryBackend *dev;
        bool              cmse;
        hwaddr            cba;
    } pmr;

    uint8_t     aer_mask;
    NvmeRequest **aer_reqs;
    QTAILQ_HEAD(, NvmeAsyncEvent) aer_queue;
    int         aer_queued;

    uint32_t    dmrsl;

    /* Namespace ID is started with 1 so bitmap should be 1-based */
#define NVME_CHANGED_NSID_SIZE  (NVME_MAX_NAMESPACES + 1)
    DECLARE_BITMAP(changed_nsids, NVME_CHANGED_NSID_SIZE);

    NvmeSubsystem   *subsys;

    NvmeNamespace   namespace;
    NvmeNamespace   *namespaces[NVME_MAX_NAMESPACES + 1];
    NvmeSQueue      **sq;
    NvmeCQueue      **cq;
    NvmeSQueue      admin_sq;
    NvmeCQueue      admin_cq;
    NvmeIdCtrl      id_ctrl;

    struct {
        struct {
            uint16_t temp_thresh_hi;
            uint16_t temp_thresh_low;
        };

        uint32_t                async_config;
        NvmeHostBehaviorSupport hbs;
    } features;

    NvmePriCtrlCap  pri_ctrl_cap;
    NvmeSecCtrlList sec_ctrl_list;
    struct {
        uint16_t    vqrfap;
        uint16_t    virfap;
    } next_pri_ctrl_cap;    /* These override pri_ctrl_cap after reset */
} NvmeCtrl;

typedef enum NvmeResetType {
    NVME_RESET_FUNCTION   = 0,
    NVME_RESET_CONTROLLER = 1,
} NvmeResetType;

static inline NvmeNamespace *nvme_ns(NvmeCtrl *n, uint32_t nsid)
{
    if (!nsid || nsid > NVME_MAX_NAMESPACES) {
        return NULL;
    }

    return n->namespaces[nsid];
}

static inline NvmeCQueue *nvme_cq(NvmeRequest *req)
{
    NvmeSQueue *sq = req->sq;
    NvmeCtrl *n = sq->ctrl;

    return n->cq[sq->cqid];
}

static inline NvmeCtrl *nvme_ctrl(NvmeRequest *req)
{
    NvmeSQueue *sq = req->sq;
    return sq->ctrl;
}

static inline uint16_t nvme_cid(NvmeRequest *req)
{
    if (!req) {
        return 0xffff;
    }

    return le16_to_cpu(req->cqe.cid);
}

static inline NvmeSecCtrlEntry *nvme_sctrl(NvmeCtrl *n)
{
    PCIDevice *pci_dev = &n->parent_obj;
    NvmeCtrl *pf = NVME(pcie_sriov_get_pf(pci_dev));

    if (pci_is_vf(pci_dev)) {
        return &pf->sec_ctrl_list.sec[pcie_sriov_vf_number(pci_dev)];
    }

    return NULL;
}

static inline NvmeSecCtrlEntry *nvme_sctrl_for_cntlid(NvmeCtrl *n,
                                                      uint16_t cntlid)
{
    NvmeSecCtrlList *list = &n->sec_ctrl_list;
    uint8_t i;

    for (i = 0; i < list->numcntl; i++) {
        if (le16_to_cpu(list->sec[i].scid) == cntlid) {
            return &list->sec[i];
        }
    }

    return NULL;
}

void nvme_attach_ns(NvmeCtrl *n, NvmeNamespace *ns);
uint16_t nvme_bounce_data(NvmeCtrl *n, void *ptr, uint32_t len,
                          NvmeTxDirection dir, NvmeRequest *req);
uint16_t nvme_bounce_mdata(NvmeCtrl *n, void *ptr, uint32_t len,
                           NvmeTxDirection dir, NvmeRequest *req);
void nvme_rw_complete_cb(void *opaque, int ret);
uint16_t nvme_map_dptr(NvmeCtrl *n, NvmeSg *sg, size_t len,
                       NvmeCmd *cmd);

#endif /* HW_NVME_NVME_H */
