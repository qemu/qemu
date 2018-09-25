/*
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "exec/address-spaces.h"
#include "cpu.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

#include "hw/arm/smmuv3.h"
#include "smmuv3-internal.h"

/**
 * smmuv3_trigger_irq - pulse @irq if enabled and update
 * GERROR register in case of GERROR interrupt
 *
 * @irq: irq type
 * @gerror_mask: mask of gerrors to toggle (relevant if @irq is GERROR)
 */
static void smmuv3_trigger_irq(SMMUv3State *s, SMMUIrq irq,
                               uint32_t gerror_mask)
{

    bool pulse = false;

    switch (irq) {
    case SMMU_IRQ_EVTQ:
        pulse = smmuv3_eventq_irq_enabled(s);
        break;
    case SMMU_IRQ_PRIQ:
        qemu_log_mask(LOG_UNIMP, "PRI not yet supported\n");
        break;
    case SMMU_IRQ_CMD_SYNC:
        pulse = true;
        break;
    case SMMU_IRQ_GERROR:
    {
        uint32_t pending = s->gerror ^ s->gerrorn;
        uint32_t new_gerrors = ~pending & gerror_mask;

        if (!new_gerrors) {
            /* only toggle non pending errors */
            return;
        }
        s->gerror ^= new_gerrors;
        trace_smmuv3_write_gerror(new_gerrors, s->gerror);

        pulse = smmuv3_gerror_irq_enabled(s);
        break;
    }
    }
    if (pulse) {
            trace_smmuv3_trigger_irq(irq);
            qemu_irq_pulse(s->irq[irq]);
    }
}

static void smmuv3_write_gerrorn(SMMUv3State *s, uint32_t new_gerrorn)
{
    uint32_t pending = s->gerror ^ s->gerrorn;
    uint32_t toggled = s->gerrorn ^ new_gerrorn;

    if (toggled & ~pending) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "guest toggles non pending errors = 0x%x\n",
                      toggled & ~pending);
    }

    /*
     * We do not raise any error in case guest toggles bits corresponding
     * to not active IRQs (CONSTRAINED UNPREDICTABLE)
     */
    s->gerrorn = new_gerrorn;

    trace_smmuv3_write_gerrorn(toggled & pending, s->gerrorn);
}

static inline MemTxResult queue_read(SMMUQueue *q, void *data)
{
    dma_addr_t addr = Q_CONS_ENTRY(q);

    return dma_memory_read(&address_space_memory, addr, data, q->entry_size);
}

static MemTxResult queue_write(SMMUQueue *q, void *data)
{
    dma_addr_t addr = Q_PROD_ENTRY(q);
    MemTxResult ret;

    ret = dma_memory_write(&address_space_memory, addr, data, q->entry_size);
    if (ret != MEMTX_OK) {
        return ret;
    }

    queue_prod_incr(q);
    return MEMTX_OK;
}

static MemTxResult smmuv3_write_eventq(SMMUv3State *s, Evt *evt)
{
    SMMUQueue *q = &s->eventq;
    MemTxResult r;

    if (!smmuv3_eventq_enabled(s)) {
        return MEMTX_ERROR;
    }

    if (smmuv3_q_full(q)) {
        return MEMTX_ERROR;
    }

    r = queue_write(q, evt);
    if (r != MEMTX_OK) {
        return r;
    }

    if (!smmuv3_q_empty(q)) {
        smmuv3_trigger_irq(s, SMMU_IRQ_EVTQ, 0);
    }
    return MEMTX_OK;
}

void smmuv3_record_event(SMMUv3State *s, SMMUEventInfo *info)
{
    Evt evt = {};
    MemTxResult r;

    if (!smmuv3_eventq_enabled(s)) {
        return;
    }

    EVT_SET_TYPE(&evt, info->type);
    EVT_SET_SID(&evt, info->sid);

    switch (info->type) {
    case SMMU_EVT_NONE:
        return;
    case SMMU_EVT_F_UUT:
        EVT_SET_SSID(&evt, info->u.f_uut.ssid);
        EVT_SET_SSV(&evt,  info->u.f_uut.ssv);
        EVT_SET_ADDR(&evt, info->u.f_uut.addr);
        EVT_SET_RNW(&evt,  info->u.f_uut.rnw);
        EVT_SET_PNU(&evt,  info->u.f_uut.pnu);
        EVT_SET_IND(&evt,  info->u.f_uut.ind);
        break;
    case SMMU_EVT_C_BAD_STREAMID:
        EVT_SET_SSID(&evt, info->u.c_bad_streamid.ssid);
        EVT_SET_SSV(&evt,  info->u.c_bad_streamid.ssv);
        break;
    case SMMU_EVT_F_STE_FETCH:
        EVT_SET_SSID(&evt, info->u.f_ste_fetch.ssid);
        EVT_SET_SSV(&evt,  info->u.f_ste_fetch.ssv);
        EVT_SET_ADDR(&evt, info->u.f_ste_fetch.addr);
        break;
    case SMMU_EVT_C_BAD_STE:
        EVT_SET_SSID(&evt, info->u.c_bad_ste.ssid);
        EVT_SET_SSV(&evt,  info->u.c_bad_ste.ssv);
        break;
    case SMMU_EVT_F_STREAM_DISABLED:
        break;
    case SMMU_EVT_F_TRANS_FORBIDDEN:
        EVT_SET_ADDR(&evt, info->u.f_transl_forbidden.addr);
        EVT_SET_RNW(&evt, info->u.f_transl_forbidden.rnw);
        break;
    case SMMU_EVT_C_BAD_SUBSTREAMID:
        EVT_SET_SSID(&evt, info->u.c_bad_substream.ssid);
        break;
    case SMMU_EVT_F_CD_FETCH:
        EVT_SET_SSID(&evt, info->u.f_cd_fetch.ssid);
        EVT_SET_SSV(&evt,  info->u.f_cd_fetch.ssv);
        EVT_SET_ADDR(&evt, info->u.f_cd_fetch.addr);
        break;
    case SMMU_EVT_C_BAD_CD:
        EVT_SET_SSID(&evt, info->u.c_bad_cd.ssid);
        EVT_SET_SSV(&evt,  info->u.c_bad_cd.ssv);
        break;
    case SMMU_EVT_F_WALK_EABT:
    case SMMU_EVT_F_TRANSLATION:
    case SMMU_EVT_F_ADDR_SIZE:
    case SMMU_EVT_F_ACCESS:
    case SMMU_EVT_F_PERMISSION:
        EVT_SET_STALL(&evt, info->u.f_walk_eabt.stall);
        EVT_SET_STAG(&evt, info->u.f_walk_eabt.stag);
        EVT_SET_SSID(&evt, info->u.f_walk_eabt.ssid);
        EVT_SET_SSV(&evt, info->u.f_walk_eabt.ssv);
        EVT_SET_S2(&evt, info->u.f_walk_eabt.s2);
        EVT_SET_ADDR(&evt, info->u.f_walk_eabt.addr);
        EVT_SET_RNW(&evt, info->u.f_walk_eabt.rnw);
        EVT_SET_PNU(&evt, info->u.f_walk_eabt.pnu);
        EVT_SET_IND(&evt, info->u.f_walk_eabt.ind);
        EVT_SET_CLASS(&evt, info->u.f_walk_eabt.class);
        EVT_SET_ADDR2(&evt, info->u.f_walk_eabt.addr2);
        break;
    case SMMU_EVT_F_CFG_CONFLICT:
        EVT_SET_SSID(&evt, info->u.f_cfg_conflict.ssid);
        EVT_SET_SSV(&evt,  info->u.f_cfg_conflict.ssv);
        break;
    /* rest is not implemented */
    case SMMU_EVT_F_BAD_ATS_TREQ:
    case SMMU_EVT_F_TLB_CONFLICT:
    case SMMU_EVT_E_PAGE_REQ:
    default:
        g_assert_not_reached();
    }

    trace_smmuv3_record_event(smmu_event_string(info->type), info->sid);
    r = smmuv3_write_eventq(s, &evt);
    if (r != MEMTX_OK) {
        smmuv3_trigger_irq(s, SMMU_IRQ_GERROR, R_GERROR_EVENTQ_ABT_ERR_MASK);
    }
    info->recorded = true;
}

static void smmuv3_init_regs(SMMUv3State *s)
{
    /**
     * IDR0: stage1 only, AArch64 only, coherent access, 16b ASID,
     *       multi-level stream table
     */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, S1P, 1); /* stage 1 supported */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TTF, 2); /* AArch64 PTW only */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, COHACC, 1); /* IO coherent */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, ASID16, 1); /* 16-bit ASID */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TTENDIAN, 2); /* little endian */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, STALL_MODEL, 1); /* No stall */
    /* terminated transaction will always be aborted/error returned */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TERM_MODEL, 1);
    /* 2-level stream table supported */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, STLEVEL, 1);

    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, SIDSIZE, SMMU_IDR1_SIDSIZE);
    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, EVENTQS, SMMU_EVENTQS);
    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, CMDQS,   SMMU_CMDQS);

   /* 4K and 64K granule support */
    s->idr[5] = FIELD_DP32(s->idr[5], IDR5, GRAN4K, 1);
    s->idr[5] = FIELD_DP32(s->idr[5], IDR5, GRAN64K, 1);
    s->idr[5] = FIELD_DP32(s->idr[5], IDR5, OAS, SMMU_IDR5_OAS); /* 44 bits */

    s->cmdq.base = deposit64(s->cmdq.base, 0, 5, SMMU_CMDQS);
    s->cmdq.prod = 0;
    s->cmdq.cons = 0;
    s->cmdq.entry_size = sizeof(struct Cmd);
    s->eventq.base = deposit64(s->eventq.base, 0, 5, SMMU_EVENTQS);
    s->eventq.prod = 0;
    s->eventq.cons = 0;
    s->eventq.entry_size = sizeof(struct Evt);

    s->features = 0;
    s->sid_split = 0;
}

static int smmu_get_ste(SMMUv3State *s, dma_addr_t addr, STE *buf,
                        SMMUEventInfo *event)
{
    int ret;

    trace_smmuv3_get_ste(addr);
    /* TODO: guarantee 64-bit single-copy atomicity */
    ret = dma_memory_read(&address_space_memory, addr,
                          (void *)buf, sizeof(*buf));
    if (ret != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Cannot fetch pte at address=0x%"PRIx64"\n", addr);
        event->type = SMMU_EVT_F_STE_FETCH;
        event->u.f_ste_fetch.addr = addr;
        return -EINVAL;
    }
    return 0;

}

/* @ssid > 0 not supported yet */
static int smmu_get_cd(SMMUv3State *s, STE *ste, uint32_t ssid,
                       CD *buf, SMMUEventInfo *event)
{
    dma_addr_t addr = STE_CTXPTR(ste);
    int ret;

    trace_smmuv3_get_cd(addr);
    /* TODO: guarantee 64-bit single-copy atomicity */
    ret = dma_memory_read(&address_space_memory, addr,
                           (void *)buf, sizeof(*buf));
    if (ret != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Cannot fetch pte at address=0x%"PRIx64"\n", addr);
        event->type = SMMU_EVT_F_CD_FETCH;
        event->u.f_ste_fetch.addr = addr;
        return -EINVAL;
    }
    return 0;
}

/* Returns < 0 in case of invalid STE, 0 otherwise */
static int decode_ste(SMMUv3State *s, SMMUTransCfg *cfg,
                      STE *ste, SMMUEventInfo *event)
{
    uint32_t config;

    if (!STE_VALID(ste)) {
        goto bad_ste;
    }

    config = STE_CONFIG(ste);

    if (STE_CFG_ABORT(config)) {
        cfg->aborted = true;
        return 0;
    }

    if (STE_CFG_BYPASS(config)) {
        cfg->bypassed = true;
        return 0;
    }

    if (STE_CFG_S2_ENABLED(config)) {
        qemu_log_mask(LOG_UNIMP, "SMMUv3 does not support stage 2 yet\n");
        goto bad_ste;
    }

    if (STE_S1CDMAX(ste) != 0) {
        qemu_log_mask(LOG_UNIMP,
                      "SMMUv3 does not support multiple context descriptors yet\n");
        goto bad_ste;
    }

    if (STE_S1STALLD(ste)) {
        qemu_log_mask(LOG_UNIMP,
                      "SMMUv3 S1 stalling fault model not allowed yet\n");
        goto bad_ste;
    }
    return 0;

bad_ste:
    event->type = SMMU_EVT_C_BAD_STE;
    return -EINVAL;
}

/**
 * smmu_find_ste - Return the stream table entry associated
 * to the sid
 *
 * @s: smmuv3 handle
 * @sid: stream ID
 * @ste: returned stream table entry
 * @event: handle to an event info
 *
 * Supports linear and 2-level stream table
 * Return 0 on success, -EINVAL otherwise
 */
static int smmu_find_ste(SMMUv3State *s, uint32_t sid, STE *ste,
                         SMMUEventInfo *event)
{
    dma_addr_t addr;
    int ret;

    trace_smmuv3_find_ste(sid, s->features, s->sid_split);
    /* Check SID range */
    if (sid > (1 << SMMU_IDR1_SIDSIZE)) {
        event->type = SMMU_EVT_C_BAD_STREAMID;
        return -EINVAL;
    }
    if (s->features & SMMU_FEATURE_2LVL_STE) {
        int l1_ste_offset, l2_ste_offset, max_l2_ste, span;
        dma_addr_t strtab_base, l1ptr, l2ptr;
        STEDesc l1std;

        strtab_base = s->strtab_base & SMMU_BASE_ADDR_MASK;
        l1_ste_offset = sid >> s->sid_split;
        l2_ste_offset = sid & ((1 << s->sid_split) - 1);
        l1ptr = (dma_addr_t)(strtab_base + l1_ste_offset * sizeof(l1std));
        /* TODO: guarantee 64-bit single-copy atomicity */
        ret = dma_memory_read(&address_space_memory, l1ptr,
                              (uint8_t *)&l1std, sizeof(l1std));
        if (ret != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Could not read L1PTR at 0X%"PRIx64"\n", l1ptr);
            event->type = SMMU_EVT_F_STE_FETCH;
            event->u.f_ste_fetch.addr = l1ptr;
            return -EINVAL;
        }

        span = L1STD_SPAN(&l1std);

        if (!span) {
            /* l2ptr is not valid */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "invalid sid=%d (L1STD span=0)\n", sid);
            event->type = SMMU_EVT_C_BAD_STREAMID;
            return -EINVAL;
        }
        max_l2_ste = (1 << span) - 1;
        l2ptr = l1std_l2ptr(&l1std);
        trace_smmuv3_find_ste_2lvl(s->strtab_base, l1ptr, l1_ste_offset,
                                   l2ptr, l2_ste_offset, max_l2_ste);
        if (l2_ste_offset > max_l2_ste) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "l2_ste_offset=%d > max_l2_ste=%d\n",
                          l2_ste_offset, max_l2_ste);
            event->type = SMMU_EVT_C_BAD_STE;
            return -EINVAL;
        }
        addr = l2ptr + l2_ste_offset * sizeof(*ste);
    } else {
        addr = s->strtab_base + sid * sizeof(*ste);
    }

    if (smmu_get_ste(s, addr, ste, event)) {
        return -EINVAL;
    }

    return 0;
}

static int decode_cd(SMMUTransCfg *cfg, CD *cd, SMMUEventInfo *event)
{
    int ret = -EINVAL;
    int i;

    if (!CD_VALID(cd) || !CD_AARCH64(cd)) {
        goto bad_cd;
    }
    if (!CD_A(cd)) {
        goto bad_cd; /* SMMU_IDR0.TERM_MODEL == 1 */
    }
    if (CD_S(cd)) {
        goto bad_cd; /* !STE_SECURE && SMMU_IDR0.STALL_MODEL == 1 */
    }
    if (CD_HA(cd) || CD_HD(cd)) {
        goto bad_cd; /* HTTU = 0 */
    }

    /* we support only those at the moment */
    cfg->aa64 = true;
    cfg->stage = 1;

    cfg->oas = oas2bits(CD_IPS(cd));
    cfg->oas = MIN(oas2bits(SMMU_IDR5_OAS), cfg->oas);
    cfg->tbi = CD_TBI(cd);
    cfg->asid = CD_ASID(cd);

    trace_smmuv3_decode_cd(cfg->oas);

    /* decode data dependent on TT */
    for (i = 0; i <= 1; i++) {
        int tg, tsz;
        SMMUTransTableInfo *tt = &cfg->tt[i];

        cfg->tt[i].disabled = CD_EPD(cd, i);
        if (cfg->tt[i].disabled) {
            continue;
        }

        tsz = CD_TSZ(cd, i);
        if (tsz < 16 || tsz > 39) {
            goto bad_cd;
        }

        tg = CD_TG(cd, i);
        tt->granule_sz = tg2granule(tg, i);
        if ((tt->granule_sz != 12 && tt->granule_sz != 16) || CD_ENDI(cd)) {
            goto bad_cd;
        }

        tt->tsz = tsz;
        tt->ttb = CD_TTB(cd, i);
        if (tt->ttb & ~(MAKE_64BIT_MASK(0, cfg->oas))) {
            goto bad_cd;
        }
        trace_smmuv3_decode_cd_tt(i, tt->tsz, tt->ttb, tt->granule_sz);
    }

    event->record_trans_faults = CD_R(cd);

    return 0;

bad_cd:
    event->type = SMMU_EVT_C_BAD_CD;
    return ret;
}

/**
 * smmuv3_decode_config - Prepare the translation configuration
 * for the @mr iommu region
 * @mr: iommu memory region the translation config must be prepared for
 * @cfg: output translation configuration which is populated through
 *       the different configuration decoding steps
 * @event: must be zero'ed by the caller
 *
 * return < 0 in case of config decoding error (@event is filled
 * accordingly). Return 0 otherwise.
 */
static int smmuv3_decode_config(IOMMUMemoryRegion *mr, SMMUTransCfg *cfg,
                                SMMUEventInfo *event)
{
    SMMUDevice *sdev = container_of(mr, SMMUDevice, iommu);
    uint32_t sid = smmu_get_sid(sdev);
    SMMUv3State *s = sdev->smmu;
    int ret;
    STE ste;
    CD cd;

    ret = smmu_find_ste(s, sid, &ste, event);
    if (ret) {
        return ret;
    }

    ret = decode_ste(s, cfg, &ste, event);
    if (ret) {
        return ret;
    }

    if (cfg->aborted || cfg->bypassed) {
        return 0;
    }

    ret = smmu_get_cd(s, &ste, 0 /* ssid */, &cd, event);
    if (ret) {
        return ret;
    }

    return decode_cd(cfg, &cd, event);
}

/**
 * smmuv3_get_config - Look up for a cached copy of configuration data for
 * @sdev and on cache miss performs a configuration structure decoding from
 * guest RAM.
 *
 * @sdev: SMMUDevice handle
 * @event: output event info
 *
 * The configuration cache contains data resulting from both STE and CD
 * decoding under the form of an SMMUTransCfg struct. The hash table is indexed
 * by the SMMUDevice handle.
 */
static SMMUTransCfg *smmuv3_get_config(SMMUDevice *sdev, SMMUEventInfo *event)
{
    SMMUv3State *s = sdev->smmu;
    SMMUState *bc = &s->smmu_state;
    SMMUTransCfg *cfg;

    cfg = g_hash_table_lookup(bc->configs, sdev);
    if (cfg) {
        sdev->cfg_cache_hits++;
        trace_smmuv3_config_cache_hit(smmu_get_sid(sdev),
                            sdev->cfg_cache_hits, sdev->cfg_cache_misses,
                            100 * sdev->cfg_cache_hits /
                            (sdev->cfg_cache_hits + sdev->cfg_cache_misses));
    } else {
        sdev->cfg_cache_misses++;
        trace_smmuv3_config_cache_miss(smmu_get_sid(sdev),
                            sdev->cfg_cache_hits, sdev->cfg_cache_misses,
                            100 * sdev->cfg_cache_hits /
                            (sdev->cfg_cache_hits + sdev->cfg_cache_misses));
        cfg = g_new0(SMMUTransCfg, 1);

        if (!smmuv3_decode_config(&sdev->iommu, cfg, event)) {
            g_hash_table_insert(bc->configs, sdev, cfg);
        } else {
            g_free(cfg);
            cfg = NULL;
        }
    }
    return cfg;
}

static void smmuv3_flush_config(SMMUDevice *sdev)
{
    SMMUv3State *s = sdev->smmu;
    SMMUState *bc = &s->smmu_state;

    trace_smmuv3_config_cache_inv(smmu_get_sid(sdev));
    g_hash_table_remove(bc->configs, sdev);
}

static IOMMUTLBEntry smmuv3_translate(IOMMUMemoryRegion *mr, hwaddr addr,
                                      IOMMUAccessFlags flag, int iommu_idx)
{
    SMMUDevice *sdev = container_of(mr, SMMUDevice, iommu);
    SMMUv3State *s = sdev->smmu;
    uint32_t sid = smmu_get_sid(sdev);
    SMMUEventInfo event = {.type = SMMU_EVT_NONE, .sid = sid};
    SMMUPTWEventInfo ptw_info = {};
    SMMUTranslationStatus status;
    SMMUState *bs = ARM_SMMU(s);
    uint64_t page_mask, aligned_addr;
    IOMMUTLBEntry *cached_entry = NULL;
    SMMUTransTableInfo *tt;
    SMMUTransCfg *cfg = NULL;
    IOMMUTLBEntry entry = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };
    SMMUIOTLBKey key, *new_key;

    qemu_mutex_lock(&s->mutex);

    if (!smmu_enabled(s)) {
        status = SMMU_TRANS_DISABLE;
        goto epilogue;
    }

    cfg = smmuv3_get_config(sdev, &event);
    if (!cfg) {
        status = SMMU_TRANS_ERROR;
        goto epilogue;
    }

    if (cfg->aborted) {
        status = SMMU_TRANS_ABORT;
        goto epilogue;
    }

    if (cfg->bypassed) {
        status = SMMU_TRANS_BYPASS;
        goto epilogue;
    }

    tt = select_tt(cfg, addr);
    if (!tt) {
        if (event.record_trans_faults) {
            event.type = SMMU_EVT_F_TRANSLATION;
            event.u.f_translation.addr = addr;
            event.u.f_translation.rnw = flag & 0x1;
        }
        status = SMMU_TRANS_ERROR;
        goto epilogue;
    }

    page_mask = (1ULL << (tt->granule_sz)) - 1;
    aligned_addr = addr & ~page_mask;

    key.asid = cfg->asid;
    key.iova = aligned_addr;

    cached_entry = g_hash_table_lookup(bs->iotlb, &key);
    if (cached_entry) {
        cfg->iotlb_hits++;
        trace_smmu_iotlb_cache_hit(cfg->asid, aligned_addr,
                                   cfg->iotlb_hits, cfg->iotlb_misses,
                                   100 * cfg->iotlb_hits /
                                   (cfg->iotlb_hits + cfg->iotlb_misses));
        if ((flag & IOMMU_WO) && !(cached_entry->perm & IOMMU_WO)) {
            status = SMMU_TRANS_ERROR;
            if (event.record_trans_faults) {
                event.type = SMMU_EVT_F_PERMISSION;
                event.u.f_permission.addr = addr;
                event.u.f_permission.rnw = flag & 0x1;
            }
        } else {
            status = SMMU_TRANS_SUCCESS;
        }
        goto epilogue;
    }

    cfg->iotlb_misses++;
    trace_smmu_iotlb_cache_miss(cfg->asid, addr & ~page_mask,
                                cfg->iotlb_hits, cfg->iotlb_misses,
                                100 * cfg->iotlb_hits /
                                (cfg->iotlb_hits + cfg->iotlb_misses));

    if (g_hash_table_size(bs->iotlb) >= SMMU_IOTLB_MAX_SIZE) {
        smmu_iotlb_inv_all(bs);
    }

    cached_entry = g_new0(IOMMUTLBEntry, 1);

    if (smmu_ptw(cfg, aligned_addr, flag, cached_entry, &ptw_info)) {
        g_free(cached_entry);
        switch (ptw_info.type) {
        case SMMU_PTW_ERR_WALK_EABT:
            event.type = SMMU_EVT_F_WALK_EABT;
            event.u.f_walk_eabt.addr = addr;
            event.u.f_walk_eabt.rnw = flag & 0x1;
            event.u.f_walk_eabt.class = 0x1;
            event.u.f_walk_eabt.addr2 = ptw_info.addr;
            break;
        case SMMU_PTW_ERR_TRANSLATION:
            if (event.record_trans_faults) {
                event.type = SMMU_EVT_F_TRANSLATION;
                event.u.f_translation.addr = addr;
                event.u.f_translation.rnw = flag & 0x1;
            }
            break;
        case SMMU_PTW_ERR_ADDR_SIZE:
            if (event.record_trans_faults) {
                event.type = SMMU_EVT_F_ADDR_SIZE;
                event.u.f_addr_size.addr = addr;
                event.u.f_addr_size.rnw = flag & 0x1;
            }
            break;
        case SMMU_PTW_ERR_ACCESS:
            if (event.record_trans_faults) {
                event.type = SMMU_EVT_F_ACCESS;
                event.u.f_access.addr = addr;
                event.u.f_access.rnw = flag & 0x1;
            }
            break;
        case SMMU_PTW_ERR_PERMISSION:
            if (event.record_trans_faults) {
                event.type = SMMU_EVT_F_PERMISSION;
                event.u.f_permission.addr = addr;
                event.u.f_permission.rnw = flag & 0x1;
            }
            break;
        default:
            g_assert_not_reached();
        }
        status = SMMU_TRANS_ERROR;
    } else {
        new_key = g_new0(SMMUIOTLBKey, 1);
        new_key->asid = cfg->asid;
        new_key->iova = aligned_addr;
        g_hash_table_insert(bs->iotlb, new_key, cached_entry);
        status = SMMU_TRANS_SUCCESS;
    }

epilogue:
    qemu_mutex_unlock(&s->mutex);
    switch (status) {
    case SMMU_TRANS_SUCCESS:
        entry.perm = flag;
        entry.translated_addr = cached_entry->translated_addr +
                                    (addr & page_mask);
        entry.addr_mask = cached_entry->addr_mask;
        trace_smmuv3_translate_success(mr->parent_obj.name, sid, addr,
                                       entry.translated_addr, entry.perm);
        break;
    case SMMU_TRANS_DISABLE:
        entry.perm = flag;
        entry.addr_mask = ~TARGET_PAGE_MASK;
        trace_smmuv3_translate_disable(mr->parent_obj.name, sid, addr,
                                      entry.perm);
        break;
    case SMMU_TRANS_BYPASS:
        entry.perm = flag;
        entry.addr_mask = ~TARGET_PAGE_MASK;
        trace_smmuv3_translate_bypass(mr->parent_obj.name, sid, addr,
                                      entry.perm);
        break;
    case SMMU_TRANS_ABORT:
        /* no event is recorded on abort */
        trace_smmuv3_translate_abort(mr->parent_obj.name, sid, addr,
                                     entry.perm);
        break;
    case SMMU_TRANS_ERROR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s translation failed for iova=0x%"PRIx64"(%s)\n",
                      mr->parent_obj.name, addr, smmu_event_string(event.type));
        smmuv3_record_event(s, &event);
        break;
    }

    return entry;
}

/**
 * smmuv3_notify_iova - call the notifier @n for a given
 * @asid and @iova tuple.
 *
 * @mr: IOMMU mr region handle
 * @n: notifier to be called
 * @asid: address space ID or negative value if we don't care
 * @iova: iova
 */
static void smmuv3_notify_iova(IOMMUMemoryRegion *mr,
                               IOMMUNotifier *n,
                               int asid,
                               dma_addr_t iova)
{
    SMMUDevice *sdev = container_of(mr, SMMUDevice, iommu);
    SMMUEventInfo event = {};
    SMMUTransTableInfo *tt;
    SMMUTransCfg *cfg;
    IOMMUTLBEntry entry;

    cfg = smmuv3_get_config(sdev, &event);
    if (!cfg) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s error decoding the configuration for iommu mr=%s\n",
                      __func__, mr->parent_obj.name);
        return;
    }

    if (asid >= 0 && cfg->asid != asid) {
        return;
    }

    tt = select_tt(cfg, iova);
    if (!tt) {
        return;
    }

    entry.target_as = &address_space_memory;
    entry.iova = iova;
    entry.addr_mask = (1 << tt->granule_sz) - 1;
    entry.perm = IOMMU_NONE;

    memory_region_notify_one(n, &entry);
}

/* invalidate an asid/iova tuple in all mr's */
static void smmuv3_inv_notifiers_iova(SMMUState *s, int asid, dma_addr_t iova)
{
    SMMUNotifierNode *node;

    QLIST_FOREACH(node, &s->notifiers_list, next) {
        IOMMUMemoryRegion *mr = &node->sdev->iommu;
        IOMMUNotifier *n;

        trace_smmuv3_inv_notifiers_iova(mr->parent_obj.name, asid, iova);

        IOMMU_NOTIFIER_FOREACH(n, mr) {
            smmuv3_notify_iova(mr, n, asid, iova);
        }
    }
}

static int smmuv3_cmdq_consume(SMMUv3State *s)
{
    SMMUState *bs = ARM_SMMU(s);
    SMMUCmdError cmd_error = SMMU_CERROR_NONE;
    SMMUQueue *q = &s->cmdq;
    SMMUCommandType type = 0;

    if (!smmuv3_cmdq_enabled(s)) {
        return 0;
    }
    /*
     * some commands depend on register values, typically CR0. In case those
     * register values change while handling the command, spec says it
     * is UNPREDICTABLE whether the command is interpreted under the new
     * or old value.
     */

    while (!smmuv3_q_empty(q)) {
        uint32_t pending = s->gerror ^ s->gerrorn;
        Cmd cmd;

        trace_smmuv3_cmdq_consume(Q_PROD(q), Q_CONS(q),
                                  Q_PROD_WRAP(q), Q_CONS_WRAP(q));

        if (FIELD_EX32(pending, GERROR, CMDQ_ERR)) {
            break;
        }

        if (queue_read(q, &cmd) != MEMTX_OK) {
            cmd_error = SMMU_CERROR_ABT;
            break;
        }

        type = CMD_TYPE(&cmd);

        trace_smmuv3_cmdq_opcode(smmu_cmd_string(type));

        qemu_mutex_lock(&s->mutex);
        switch (type) {
        case SMMU_CMD_SYNC:
            if (CMD_SYNC_CS(&cmd) & CMD_SYNC_SIG_IRQ) {
                smmuv3_trigger_irq(s, SMMU_IRQ_CMD_SYNC, 0);
            }
            break;
        case SMMU_CMD_PREFETCH_CONFIG:
        case SMMU_CMD_PREFETCH_ADDR:
            break;
        case SMMU_CMD_CFGI_STE:
        {
            uint32_t sid = CMD_SID(&cmd);
            IOMMUMemoryRegion *mr = smmu_iommu_mr(bs, sid);
            SMMUDevice *sdev;

            if (CMD_SSEC(&cmd)) {
                cmd_error = SMMU_CERROR_ILL;
                break;
            }

            if (!mr) {
                break;
            }

            trace_smmuv3_cmdq_cfgi_ste(sid);
            sdev = container_of(mr, SMMUDevice, iommu);
            smmuv3_flush_config(sdev);

            break;
        }
        case SMMU_CMD_CFGI_STE_RANGE: /* same as SMMU_CMD_CFGI_ALL */
        {
            uint32_t start = CMD_SID(&cmd), end, i;
            uint8_t range = CMD_STE_RANGE(&cmd);

            if (CMD_SSEC(&cmd)) {
                cmd_error = SMMU_CERROR_ILL;
                break;
            }

            end = start + (1 << (range + 1)) - 1;
            trace_smmuv3_cmdq_cfgi_ste_range(start, end);

            for (i = start; i <= end; i++) {
                IOMMUMemoryRegion *mr = smmu_iommu_mr(bs, i);
                SMMUDevice *sdev;

                if (!mr) {
                    continue;
                }
                sdev = container_of(mr, SMMUDevice, iommu);
                smmuv3_flush_config(sdev);
            }
            break;
        }
        case SMMU_CMD_CFGI_CD:
        case SMMU_CMD_CFGI_CD_ALL:
        {
            uint32_t sid = CMD_SID(&cmd);
            IOMMUMemoryRegion *mr = smmu_iommu_mr(bs, sid);
            SMMUDevice *sdev;

            if (CMD_SSEC(&cmd)) {
                cmd_error = SMMU_CERROR_ILL;
                break;
            }

            if (!mr) {
                break;
            }

            trace_smmuv3_cmdq_cfgi_cd(sid);
            sdev = container_of(mr, SMMUDevice, iommu);
            smmuv3_flush_config(sdev);
            break;
        }
        case SMMU_CMD_TLBI_NH_ASID:
        {
            uint16_t asid = CMD_ASID(&cmd);

            trace_smmuv3_cmdq_tlbi_nh_asid(asid);
            smmu_inv_notifiers_all(&s->smmu_state);
            smmu_iotlb_inv_asid(bs, asid);
            break;
        }
        case SMMU_CMD_TLBI_NH_ALL:
        case SMMU_CMD_TLBI_NSNH_ALL:
            trace_smmuv3_cmdq_tlbi_nh();
            smmu_inv_notifiers_all(&s->smmu_state);
            smmu_iotlb_inv_all(bs);
            break;
        case SMMU_CMD_TLBI_NH_VAA:
        {
            dma_addr_t addr = CMD_ADDR(&cmd);
            uint16_t vmid = CMD_VMID(&cmd);

            trace_smmuv3_cmdq_tlbi_nh_vaa(vmid, addr);
            smmuv3_inv_notifiers_iova(bs, -1, addr);
            smmu_iotlb_inv_all(bs);
            break;
        }
        case SMMU_CMD_TLBI_NH_VA:
        {
            uint16_t asid = CMD_ASID(&cmd);
            uint16_t vmid = CMD_VMID(&cmd);
            dma_addr_t addr = CMD_ADDR(&cmd);
            bool leaf = CMD_LEAF(&cmd);

            trace_smmuv3_cmdq_tlbi_nh_va(vmid, asid, addr, leaf);
            smmuv3_inv_notifiers_iova(bs, asid, addr);
            smmu_iotlb_inv_iova(bs, asid, addr);
            break;
        }
        case SMMU_CMD_TLBI_EL3_ALL:
        case SMMU_CMD_TLBI_EL3_VA:
        case SMMU_CMD_TLBI_EL2_ALL:
        case SMMU_CMD_TLBI_EL2_ASID:
        case SMMU_CMD_TLBI_EL2_VA:
        case SMMU_CMD_TLBI_EL2_VAA:
        case SMMU_CMD_TLBI_S12_VMALL:
        case SMMU_CMD_TLBI_S2_IPA:
        case SMMU_CMD_ATC_INV:
        case SMMU_CMD_PRI_RESP:
        case SMMU_CMD_RESUME:
        case SMMU_CMD_STALL_TERM:
            trace_smmuv3_unhandled_cmd(type);
            break;
        default:
            cmd_error = SMMU_CERROR_ILL;
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Illegal command type: %d\n", CMD_TYPE(&cmd));
            break;
        }
        qemu_mutex_unlock(&s->mutex);
        if (cmd_error) {
            break;
        }
        /*
         * We only increment the cons index after the completion of
         * the command. We do that because the SYNC returns immediately
         * and does not check the completion of previous commands
         */
        queue_cons_incr(q);
    }

    if (cmd_error) {
        trace_smmuv3_cmdq_consume_error(smmu_cmd_string(type), cmd_error);
        smmu_write_cmdq_err(s, cmd_error);
        smmuv3_trigger_irq(s, SMMU_IRQ_GERROR, R_GERROR_CMDQ_ERR_MASK);
    }

    trace_smmuv3_cmdq_consume_out(Q_PROD(q), Q_CONS(q),
                                  Q_PROD_WRAP(q), Q_CONS_WRAP(q));

    return 0;
}

static MemTxResult smmu_writell(SMMUv3State *s, hwaddr offset,
                               uint64_t data, MemTxAttrs attrs)
{
    switch (offset) {
    case A_GERROR_IRQ_CFG0:
        s->gerror_irq_cfg0 = data;
        return MEMTX_OK;
    case A_STRTAB_BASE:
        s->strtab_base = data;
        return MEMTX_OK;
    case A_CMDQ_BASE:
        s->cmdq.base = data;
        s->cmdq.log2size = extract64(s->cmdq.base, 0, 5);
        if (s->cmdq.log2size > SMMU_CMDQS) {
            s->cmdq.log2size = SMMU_CMDQS;
        }
        return MEMTX_OK;
    case A_EVENTQ_BASE:
        s->eventq.base = data;
        s->eventq.log2size = extract64(s->eventq.base, 0, 5);
        if (s->eventq.log2size > SMMU_EVENTQS) {
            s->eventq.log2size = SMMU_EVENTQS;
        }
        return MEMTX_OK;
    case A_EVENTQ_IRQ_CFG0:
        s->eventq_irq_cfg0 = data;
        return MEMTX_OK;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s Unexpected 64-bit access to 0x%"PRIx64" (WI)\n",
                      __func__, offset);
        return MEMTX_OK;
    }
}

static MemTxResult smmu_writel(SMMUv3State *s, hwaddr offset,
                               uint64_t data, MemTxAttrs attrs)
{
    switch (offset) {
    case A_CR0:
        s->cr[0] = data;
        s->cr0ack = data & ~SMMU_CR0_RESERVED;
        /* in case the command queue has been enabled */
        smmuv3_cmdq_consume(s);
        return MEMTX_OK;
    case A_CR1:
        s->cr[1] = data;
        return MEMTX_OK;
    case A_CR2:
        s->cr[2] = data;
        return MEMTX_OK;
    case A_IRQ_CTRL:
        s->irq_ctrl = data;
        return MEMTX_OK;
    case A_GERRORN:
        smmuv3_write_gerrorn(s, data);
        /*
         * By acknowledging the CMDQ_ERR, SW may notify cmds can
         * be processed again
         */
        smmuv3_cmdq_consume(s);
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG0: /* 64b */
        s->gerror_irq_cfg0 = deposit64(s->gerror_irq_cfg0, 0, 32, data);
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG0 + 4:
        s->gerror_irq_cfg0 = deposit64(s->gerror_irq_cfg0, 32, 32, data);
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG1:
        s->gerror_irq_cfg1 = data;
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG2:
        s->gerror_irq_cfg2 = data;
        return MEMTX_OK;
    case A_STRTAB_BASE: /* 64b */
        s->strtab_base = deposit64(s->strtab_base, 0, 32, data);
        return MEMTX_OK;
    case A_STRTAB_BASE + 4:
        s->strtab_base = deposit64(s->strtab_base, 32, 32, data);
        return MEMTX_OK;
    case A_STRTAB_BASE_CFG:
        s->strtab_base_cfg = data;
        if (FIELD_EX32(data, STRTAB_BASE_CFG, FMT) == 1) {
            s->sid_split = FIELD_EX32(data, STRTAB_BASE_CFG, SPLIT);
            s->features |= SMMU_FEATURE_2LVL_STE;
        }
        return MEMTX_OK;
    case A_CMDQ_BASE: /* 64b */
        s->cmdq.base = deposit64(s->cmdq.base, 0, 32, data);
        s->cmdq.log2size = extract64(s->cmdq.base, 0, 5);
        if (s->cmdq.log2size > SMMU_CMDQS) {
            s->cmdq.log2size = SMMU_CMDQS;
        }
        return MEMTX_OK;
    case A_CMDQ_BASE + 4: /* 64b */
        s->cmdq.base = deposit64(s->cmdq.base, 32, 32, data);
        return MEMTX_OK;
    case A_CMDQ_PROD:
        s->cmdq.prod = data;
        smmuv3_cmdq_consume(s);
        return MEMTX_OK;
    case A_CMDQ_CONS:
        s->cmdq.cons = data;
        return MEMTX_OK;
    case A_EVENTQ_BASE: /* 64b */
        s->eventq.base = deposit64(s->eventq.base, 0, 32, data);
        s->eventq.log2size = extract64(s->eventq.base, 0, 5);
        if (s->eventq.log2size > SMMU_EVENTQS) {
            s->eventq.log2size = SMMU_EVENTQS;
        }
        return MEMTX_OK;
    case A_EVENTQ_BASE + 4:
        s->eventq.base = deposit64(s->eventq.base, 32, 32, data);
        return MEMTX_OK;
    case A_EVENTQ_PROD:
        s->eventq.prod = data;
        return MEMTX_OK;
    case A_EVENTQ_CONS:
        s->eventq.cons = data;
        return MEMTX_OK;
    case A_EVENTQ_IRQ_CFG0: /* 64b */
        s->eventq_irq_cfg0 = deposit64(s->eventq_irq_cfg0, 0, 32, data);
        return MEMTX_OK;
    case A_EVENTQ_IRQ_CFG0 + 4:
        s->eventq_irq_cfg0 = deposit64(s->eventq_irq_cfg0, 32, 32, data);
        return MEMTX_OK;
    case A_EVENTQ_IRQ_CFG1:
        s->eventq_irq_cfg1 = data;
        return MEMTX_OK;
    case A_EVENTQ_IRQ_CFG2:
        s->eventq_irq_cfg2 = data;
        return MEMTX_OK;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s Unexpected 32-bit access to 0x%"PRIx64" (WI)\n",
                      __func__, offset);
        return MEMTX_OK;
    }
}

static MemTxResult smmu_write_mmio(void *opaque, hwaddr offset, uint64_t data,
                                   unsigned size, MemTxAttrs attrs)
{
    SMMUState *sys = opaque;
    SMMUv3State *s = ARM_SMMUV3(sys);
    MemTxResult r;

    /* CONSTRAINED UNPREDICTABLE choice to have page0/1 be exact aliases */
    offset &= ~0x10000;

    switch (size) {
    case 8:
        r = smmu_writell(s, offset, data, attrs);
        break;
    case 4:
        r = smmu_writel(s, offset, data, attrs);
        break;
    default:
        r = MEMTX_ERROR;
        break;
    }

    trace_smmuv3_write_mmio(offset, data, size, r);
    return r;
}

static MemTxResult smmu_readll(SMMUv3State *s, hwaddr offset,
                               uint64_t *data, MemTxAttrs attrs)
{
    switch (offset) {
    case A_GERROR_IRQ_CFG0:
        *data = s->gerror_irq_cfg0;
        return MEMTX_OK;
    case A_STRTAB_BASE:
        *data = s->strtab_base;
        return MEMTX_OK;
    case A_CMDQ_BASE:
        *data = s->cmdq.base;
        return MEMTX_OK;
    case A_EVENTQ_BASE:
        *data = s->eventq.base;
        return MEMTX_OK;
    default:
        *data = 0;
        qemu_log_mask(LOG_UNIMP,
                      "%s Unexpected 64-bit access to 0x%"PRIx64" (RAZ)\n",
                      __func__, offset);
        return MEMTX_OK;
    }
}

static MemTxResult smmu_readl(SMMUv3State *s, hwaddr offset,
                              uint64_t *data, MemTxAttrs attrs)
{
    switch (offset) {
    case A_IDREGS ... A_IDREGS + 0x1f:
        *data = smmuv3_idreg(offset - A_IDREGS);
        return MEMTX_OK;
    case A_IDR0 ... A_IDR5:
        *data = s->idr[(offset - A_IDR0) / 4];
        return MEMTX_OK;
    case A_IIDR:
        *data = s->iidr;
        return MEMTX_OK;
    case A_CR0:
        *data = s->cr[0];
        return MEMTX_OK;
    case A_CR0ACK:
        *data = s->cr0ack;
        return MEMTX_OK;
    case A_CR1:
        *data = s->cr[1];
        return MEMTX_OK;
    case A_CR2:
        *data = s->cr[2];
        return MEMTX_OK;
    case A_STATUSR:
        *data = s->statusr;
        return MEMTX_OK;
    case A_IRQ_CTRL:
    case A_IRQ_CTRL_ACK:
        *data = s->irq_ctrl;
        return MEMTX_OK;
    case A_GERROR:
        *data = s->gerror;
        return MEMTX_OK;
    case A_GERRORN:
        *data = s->gerrorn;
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG0: /* 64b */
        *data = extract64(s->gerror_irq_cfg0, 0, 32);
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG0 + 4:
        *data = extract64(s->gerror_irq_cfg0, 32, 32);
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG1:
        *data = s->gerror_irq_cfg1;
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG2:
        *data = s->gerror_irq_cfg2;
        return MEMTX_OK;
    case A_STRTAB_BASE: /* 64b */
        *data = extract64(s->strtab_base, 0, 32);
        return MEMTX_OK;
    case A_STRTAB_BASE + 4: /* 64b */
        *data = extract64(s->strtab_base, 32, 32);
        return MEMTX_OK;
    case A_STRTAB_BASE_CFG:
        *data = s->strtab_base_cfg;
        return MEMTX_OK;
    case A_CMDQ_BASE: /* 64b */
        *data = extract64(s->cmdq.base, 0, 32);
        return MEMTX_OK;
    case A_CMDQ_BASE + 4:
        *data = extract64(s->cmdq.base, 32, 32);
        return MEMTX_OK;
    case A_CMDQ_PROD:
        *data = s->cmdq.prod;
        return MEMTX_OK;
    case A_CMDQ_CONS:
        *data = s->cmdq.cons;
        return MEMTX_OK;
    case A_EVENTQ_BASE: /* 64b */
        *data = extract64(s->eventq.base, 0, 32);
        return MEMTX_OK;
    case A_EVENTQ_BASE + 4: /* 64b */
        *data = extract64(s->eventq.base, 32, 32);
        return MEMTX_OK;
    case A_EVENTQ_PROD:
        *data = s->eventq.prod;
        return MEMTX_OK;
    case A_EVENTQ_CONS:
        *data = s->eventq.cons;
        return MEMTX_OK;
    default:
        *data = 0;
        qemu_log_mask(LOG_UNIMP,
                      "%s unhandled 32-bit access at 0x%"PRIx64" (RAZ)\n",
                      __func__, offset);
        return MEMTX_OK;
    }
}

static MemTxResult smmu_read_mmio(void *opaque, hwaddr offset, uint64_t *data,
                                  unsigned size, MemTxAttrs attrs)
{
    SMMUState *sys = opaque;
    SMMUv3State *s = ARM_SMMUV3(sys);
    MemTxResult r;

    /* CONSTRAINED UNPREDICTABLE choice to have page0/1 be exact aliases */
    offset &= ~0x10000;

    switch (size) {
    case 8:
        r = smmu_readll(s, offset, data, attrs);
        break;
    case 4:
        r = smmu_readl(s, offset, data, attrs);
        break;
    default:
        r = MEMTX_ERROR;
        break;
    }

    trace_smmuv3_read_mmio(offset, *data, size, r);
    return r;
}

static const MemoryRegionOps smmu_mem_ops = {
    .read_with_attrs = smmu_read_mmio,
    .write_with_attrs = smmu_write_mmio,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void smmu_init_irq(SMMUv3State *s, SysBusDevice *dev)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->irq); i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }
}

static void smmu_reset(DeviceState *dev)
{
    SMMUv3State *s = ARM_SMMUV3(dev);
    SMMUv3Class *c = ARM_SMMUV3_GET_CLASS(s);

    c->parent_reset(dev);

    smmuv3_init_regs(s);
}

static void smmu_realize(DeviceState *d, Error **errp)
{
    SMMUState *sys = ARM_SMMU(d);
    SMMUv3State *s = ARM_SMMUV3(sys);
    SMMUv3Class *c = ARM_SMMUV3_GET_CLASS(s);
    SysBusDevice *dev = SYS_BUS_DEVICE(d);
    Error *local_err = NULL;

    c->parent_realize(d, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_mutex_init(&s->mutex);

    memory_region_init_io(&sys->iomem, OBJECT(s),
                          &smmu_mem_ops, sys, TYPE_ARM_SMMUV3, 0x20000);

    sys->mrtypename = TYPE_SMMUV3_IOMMU_MEMORY_REGION;

    sysbus_init_mmio(dev, &sys->iomem);

    smmu_init_irq(s, dev);
}

static const VMStateDescription vmstate_smmuv3_queue = {
    .name = "smmuv3_queue",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(base, SMMUQueue),
        VMSTATE_UINT32(prod, SMMUQueue),
        VMSTATE_UINT32(cons, SMMUQueue),
        VMSTATE_UINT8(log2size, SMMUQueue),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_smmuv3 = {
    .name = "smmuv3",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(features, SMMUv3State),
        VMSTATE_UINT8(sid_size, SMMUv3State),
        VMSTATE_UINT8(sid_split, SMMUv3State),

        VMSTATE_UINT32_ARRAY(cr, SMMUv3State, 3),
        VMSTATE_UINT32(cr0ack, SMMUv3State),
        VMSTATE_UINT32(statusr, SMMUv3State),
        VMSTATE_UINT32(irq_ctrl, SMMUv3State),
        VMSTATE_UINT32(gerror, SMMUv3State),
        VMSTATE_UINT32(gerrorn, SMMUv3State),
        VMSTATE_UINT64(gerror_irq_cfg0, SMMUv3State),
        VMSTATE_UINT32(gerror_irq_cfg1, SMMUv3State),
        VMSTATE_UINT32(gerror_irq_cfg2, SMMUv3State),
        VMSTATE_UINT64(strtab_base, SMMUv3State),
        VMSTATE_UINT32(strtab_base_cfg, SMMUv3State),
        VMSTATE_UINT64(eventq_irq_cfg0, SMMUv3State),
        VMSTATE_UINT32(eventq_irq_cfg1, SMMUv3State),
        VMSTATE_UINT32(eventq_irq_cfg2, SMMUv3State),

        VMSTATE_STRUCT(cmdq, SMMUv3State, 0, vmstate_smmuv3_queue, SMMUQueue),
        VMSTATE_STRUCT(eventq, SMMUv3State, 0, vmstate_smmuv3_queue, SMMUQueue),

        VMSTATE_END_OF_LIST(),
    },
};

static void smmuv3_instance_init(Object *obj)
{
    /* Nothing much to do here as of now */
}

static void smmuv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMMUv3Class *c = ARM_SMMUV3_CLASS(klass);

    dc->vmsd = &vmstate_smmuv3;
    device_class_set_parent_reset(dc, smmu_reset, &c->parent_reset);
    c->parent_realize = dc->realize;
    dc->realize = smmu_realize;
}

static void smmuv3_notify_flag_changed(IOMMUMemoryRegion *iommu,
                                       IOMMUNotifierFlag old,
                                       IOMMUNotifierFlag new)
{
    SMMUDevice *sdev = container_of(iommu, SMMUDevice, iommu);
    SMMUv3State *s3 = sdev->smmu;
    SMMUState *s = &(s3->smmu_state);
    SMMUNotifierNode *node = NULL;
    SMMUNotifierNode *next_node = NULL;

    if (new & IOMMU_NOTIFIER_MAP) {
        int bus_num = pci_bus_num(sdev->bus);
        PCIDevice *pcidev = pci_find_device(sdev->bus, bus_num, sdev->devfn);

        warn_report("SMMUv3 does not support notification on MAP: "
                     "device %s will not function properly", pcidev->name);
    }

    if (old == IOMMU_NOTIFIER_NONE) {
        trace_smmuv3_notify_flag_add(iommu->parent_obj.name);
        node = g_malloc0(sizeof(*node));
        node->sdev = sdev;
        QLIST_INSERT_HEAD(&s->notifiers_list, node, next);
        return;
    }

    /* update notifier node with new flags */
    QLIST_FOREACH_SAFE(node, &s->notifiers_list, next, next_node) {
        if (node->sdev == sdev) {
            if (new == IOMMU_NOTIFIER_NONE) {
                trace_smmuv3_notify_flag_del(iommu->parent_obj.name);
                QLIST_REMOVE(node, next);
                g_free(node);
            }
            return;
        }
    }
}

static void smmuv3_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = smmuv3_translate;
    imrc->notify_flag_changed = smmuv3_notify_flag_changed;
}

static const TypeInfo smmuv3_type_info = {
    .name          = TYPE_ARM_SMMUV3,
    .parent        = TYPE_ARM_SMMU,
    .instance_size = sizeof(SMMUv3State),
    .instance_init = smmuv3_instance_init,
    .class_size    = sizeof(SMMUv3Class),
    .class_init    = smmuv3_class_init,
};

static const TypeInfo smmuv3_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_SMMUV3_IOMMU_MEMORY_REGION,
    .class_init = smmuv3_iommu_memory_region_class_init,
};

static void smmuv3_register_types(void)
{
    type_register(&smmuv3_type_info);
    type_register(&smmuv3_iommu_memory_region_info);
}

type_init(smmuv3_register_types)

