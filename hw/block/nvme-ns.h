/*
 * QEMU NVM Express Virtual Namespace
 *
 * Copyright (c) 2019 CNEX Labs
 * Copyright (c) 2020 Samsung Electronics
 *
 * Authors:
 *  Klaus Jensen      <k.jensen@samsung.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 *
 */

#ifndef NVME_NS_H
#define NVME_NS_H

#define TYPE_NVME_NS "nvme-ns"
#define NVME_NS(obj) \
    OBJECT_CHECK(NvmeNamespace, (obj), TYPE_NVME_NS)

typedef struct NvmeZone {
    NvmeZoneDescr   d;
    uint64_t        w_ptr;
    QTAILQ_ENTRY(NvmeZone) entry;
} NvmeZone;

typedef struct NvmeNamespaceParams {
    uint32_t nsid;
    QemuUUID uuid;

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
    NvmeIdNs     id_ns;
    const uint32_t *iocs;
    uint8_t      csi;

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

    return -1;
}

static inline NvmeLBAF *nvme_ns_lbaf(NvmeNamespace *ns)
{
    NvmeIdNs *id_ns = &ns->id_ns;
    return &id_ns->lbaf[NVME_ID_NS_FLBAS_INDEX(id_ns->flbas)];
}

static inline uint8_t nvme_ns_lbads(NvmeNamespace *ns)
{
    return nvme_ns_lbaf(ns)->ds;
}

/* calculate the number of LBAs that the namespace can accomodate */
static inline uint64_t nvme_ns_nlbas(NvmeNamespace *ns)
{
    return ns->size >> nvme_ns_lbads(ns);
}

/* convert an LBA to the equivalent in bytes */
static inline size_t nvme_l2b(NvmeNamespace *ns, uint64_t lba)
{
    return lba << nvme_ns_lbads(ns);
}

typedef struct NvmeCtrl NvmeCtrl;

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

int nvme_ns_setup(NvmeNamespace *ns, Error **errp);
void nvme_ns_drain(NvmeNamespace *ns);
void nvme_ns_shutdown(NvmeNamespace *ns);
void nvme_ns_cleanup(NvmeNamespace *ns);

#endif /* NVME_NS_H */
