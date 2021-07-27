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

#ifndef HW_NVME_INTERNAL_H
#define HW_NVME_INTERNAL_H

#include "qemu/uuid.h"
#include "hw/pci/pci.h"
#include "hw/block/block.h"

#include "block/nvme.h"

#define NVME_MAX_CONTROLLERS 32
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

typedef struct NvmeSubsystem {
    DeviceState parent_obj;
    NvmeBus     bus;
    uint8_t     subnqn[256];

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
} NvmeNamespaceParams;

typedef struct NvmeNamespace {
    DeviceState  parent_obj;
    BlockConf    blkconf;
    int32_t      bootindex;
    int64_t      size;
    int64_t      moff;
    NvmeIdNs     id_ns;
    NvmeLBAF     lbaf;
    size_t       lbasz;
    const uint32_t *iocs;
    uint8_t      csi;
    uint16_t     status;
    int          attached;

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
    QEMUTimer   *timer;
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
    QEMUTimer   *timer;
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
    uint32_t    reg_size;
    uint32_t    max_q_ents;
    uint8_t     outstanding_aers;
    uint32_t    irq_status;
    int         cq_pending;
    uint64_t    host_timestamp;                 /* Timestamp sent by the host */
    uint64_t    timestamp_set_qemu_clock_ms;    /* QEMU clock time */
    uint64_t    starttime_ms;
    uint16_t    temperature;
    uint8_t     smart_critical_warning;

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
        uint32_t    async_config;
    } features;
} NvmeCtrl;

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

void nvme_attach_ns(NvmeCtrl *n, NvmeNamespace *ns);
uint16_t nvme_bounce_data(NvmeCtrl *n, uint8_t *ptr, uint32_t len,
                          NvmeTxDirection dir, NvmeRequest *req);
uint16_t nvme_bounce_mdata(NvmeCtrl *n, uint8_t *ptr, uint32_t len,
                           NvmeTxDirection dir, NvmeRequest *req);
void nvme_rw_complete_cb(void *opaque, int ret);
uint16_t nvme_map_dptr(NvmeCtrl *n, NvmeSg *sg, size_t len,
                       NvmeCmd *cmd);

/* from Linux kernel (crypto/crct10dif_common.c) */
static const uint16_t t10_dif_crc_table[256] = {
    0x0000, 0x8BB7, 0x9CD9, 0x176E, 0xB205, 0x39B2, 0x2EDC, 0xA56B,
    0xEFBD, 0x640A, 0x7364, 0xF8D3, 0x5DB8, 0xD60F, 0xC161, 0x4AD6,
    0x54CD, 0xDF7A, 0xC814, 0x43A3, 0xE6C8, 0x6D7F, 0x7A11, 0xF1A6,
    0xBB70, 0x30C7, 0x27A9, 0xAC1E, 0x0975, 0x82C2, 0x95AC, 0x1E1B,
    0xA99A, 0x222D, 0x3543, 0xBEF4, 0x1B9F, 0x9028, 0x8746, 0x0CF1,
    0x4627, 0xCD90, 0xDAFE, 0x5149, 0xF422, 0x7F95, 0x68FB, 0xE34C,
    0xFD57, 0x76E0, 0x618E, 0xEA39, 0x4F52, 0xC4E5, 0xD38B, 0x583C,
    0x12EA, 0x995D, 0x8E33, 0x0584, 0xA0EF, 0x2B58, 0x3C36, 0xB781,
    0xD883, 0x5334, 0x445A, 0xCFED, 0x6A86, 0xE131, 0xF65F, 0x7DE8,
    0x373E, 0xBC89, 0xABE7, 0x2050, 0x853B, 0x0E8C, 0x19E2, 0x9255,
    0x8C4E, 0x07F9, 0x1097, 0x9B20, 0x3E4B, 0xB5FC, 0xA292, 0x2925,
    0x63F3, 0xE844, 0xFF2A, 0x749D, 0xD1F6, 0x5A41, 0x4D2F, 0xC698,
    0x7119, 0xFAAE, 0xEDC0, 0x6677, 0xC31C, 0x48AB, 0x5FC5, 0xD472,
    0x9EA4, 0x1513, 0x027D, 0x89CA, 0x2CA1, 0xA716, 0xB078, 0x3BCF,
    0x25D4, 0xAE63, 0xB90D, 0x32BA, 0x97D1, 0x1C66, 0x0B08, 0x80BF,
    0xCA69, 0x41DE, 0x56B0, 0xDD07, 0x786C, 0xF3DB, 0xE4B5, 0x6F02,
    0x3AB1, 0xB106, 0xA668, 0x2DDF, 0x88B4, 0x0303, 0x146D, 0x9FDA,
    0xD50C, 0x5EBB, 0x49D5, 0xC262, 0x6709, 0xECBE, 0xFBD0, 0x7067,
    0x6E7C, 0xE5CB, 0xF2A5, 0x7912, 0xDC79, 0x57CE, 0x40A0, 0xCB17,
    0x81C1, 0x0A76, 0x1D18, 0x96AF, 0x33C4, 0xB873, 0xAF1D, 0x24AA,
    0x932B, 0x189C, 0x0FF2, 0x8445, 0x212E, 0xAA99, 0xBDF7, 0x3640,
    0x7C96, 0xF721, 0xE04F, 0x6BF8, 0xCE93, 0x4524, 0x524A, 0xD9FD,
    0xC7E6, 0x4C51, 0x5B3F, 0xD088, 0x75E3, 0xFE54, 0xE93A, 0x628D,
    0x285B, 0xA3EC, 0xB482, 0x3F35, 0x9A5E, 0x11E9, 0x0687, 0x8D30,
    0xE232, 0x6985, 0x7EEB, 0xF55C, 0x5037, 0xDB80, 0xCCEE, 0x4759,
    0x0D8F, 0x8638, 0x9156, 0x1AE1, 0xBF8A, 0x343D, 0x2353, 0xA8E4,
    0xB6FF, 0x3D48, 0x2A26, 0xA191, 0x04FA, 0x8F4D, 0x9823, 0x1394,
    0x5942, 0xD2F5, 0xC59B, 0x4E2C, 0xEB47, 0x60F0, 0x779E, 0xFC29,
    0x4BA8, 0xC01F, 0xD771, 0x5CC6, 0xF9AD, 0x721A, 0x6574, 0xEEC3,
    0xA415, 0x2FA2, 0x38CC, 0xB37B, 0x1610, 0x9DA7, 0x8AC9, 0x017E,
    0x1F65, 0x94D2, 0x83BC, 0x080B, 0xAD60, 0x26D7, 0x31B9, 0xBA0E,
    0xF0D8, 0x7B6F, 0x6C01, 0xE7B6, 0x42DD, 0xC96A, 0xDE04, 0x55B3
};

uint16_t nvme_check_prinfo(NvmeNamespace *ns, uint8_t prinfo, uint64_t slba,
                           uint32_t reftag);
uint16_t nvme_dif_mangle_mdata(NvmeNamespace *ns, uint8_t *mbuf, size_t mlen,
                               uint64_t slba);
void nvme_dif_pract_generate_dif(NvmeNamespace *ns, uint8_t *buf, size_t len,
                                 uint8_t *mbuf, size_t mlen, uint16_t apptag,
                                 uint32_t *reftag);
uint16_t nvme_dif_check(NvmeNamespace *ns, uint8_t *buf, size_t len,
                        uint8_t *mbuf, size_t mlen, uint8_t prinfo,
                        uint64_t slba, uint16_t apptag,
                        uint16_t appmask, uint32_t *reftag);
uint16_t nvme_dif_rw(NvmeCtrl *n, NvmeRequest *req);


#endif /* HW_NVME_INTERNAL_H */
