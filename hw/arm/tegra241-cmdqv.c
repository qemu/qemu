/*
 * Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 * NVIDIA Tegra241 CMDQ-Virtualization extension for SMMUv3
 *
 * Written by Nicolin Chen, Shameer Kolothum
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"

#include "hw/arm/smmuv3.h"
#include "hw/arm/smmuv3-common.h"
#include "smmuv3-accel.h"
#include "tegra241-cmdqv.h"
#include "trace.h"

static void tegra241_cmdqv_free_vcmdq(Tegra241CMDQV *cmdqv, int index)
{
    IOMMUFDViommu *viommu = cmdqv->s_accel->viommu;
    IOMMUFDHWqueue *vcmdq = cmdqv->vcmdq[index];

    if (!vcmdq) {
        return;
    }
    iommufd_backend_free_id(viommu->iommufd, vcmdq->hw_queue_id);
    g_free(vcmdq);
    cmdqv->vcmdq[index] = NULL;
}

/*
 * A VCMDQ's HW queue can be allocated once the guest has programmed:
 *  - VCMDQ_BASE (ring buffer GPA and size). This only checks that BASE is
 *    non-zero, not that both the _L and _H halves have been written; a
 *    half-written BASE may pass here, but the write of the second half
 *    re-runs setup and reallocates with the complete address.
 *  - the VINTF mapping (CMDQ_ALLOC_MAP.ALLOC).
 *  - both the CMDQV global enable and the VINTF enable.
 */
static bool tegra241_cmdqv_vcmdq_ready_to_alloc(Tegra241CMDQV *cmdqv, int index)
{
    return cmdqv->vcmdq_base[index] &&
           (cmdqv->cmdq_alloc_map[index] & R_CMDQ_ALLOC_MAP_0_ALLOC_MASK) &&
           tegra241_cmdqv_enabled(cmdqv) && tegra241_vintf_enabled(cmdqv);
}

/*
 * Allocate a host HW VCMDQ from the current cached BASE / size for @index.
 * No-op (returns true) until the VCMDQ is ready to be allocated.
 */
static bool tegra241_cmdqv_setup_vcmdq(Tegra241CMDQV *cmdqv, int index,
                                       Error **errp)
{
    SMMUv3AccelState *accel = cmdqv->s_accel;
    uint64_t base_mask = (uint64_t)R_VCMDQ0_BASE_L_ADDR_MASK |
                         (uint64_t)R_VCMDQ0_BASE_H_ADDR_MASK << 32;
    uint64_t addr = cmdqv->vcmdq_base[index] & base_mask;
    uint64_t log2 = cmdqv->vcmdq_base[index] & R_VCMDQ0_BASE_L_LOG2SIZE_MASK;
    uint64_t size = 1ULL << (log2 + 4);
    IOMMUFDViommu *viommu = accel->viommu;
    IOMMUFDHWqueue *hw_queue;
    uint32_t hw_queue_id;

    if (!tegra241_cmdqv_vcmdq_ready_to_alloc(cmdqv, index)) {
        return true;
    }

    tegra241_cmdqv_free_vcmdq(cmdqv, index);

    if (!iommufd_backend_alloc_hw_queue(viommu->iommufd, viommu->viommu_id,
                                        IOMMU_HW_QUEUE_TYPE_TEGRA241_CMDQV,
                                        index, addr, size, &hw_queue_id,
                                        errp)) {
        /* Record the failure in the cache. */
        cmdqv->vcmdq_gerror[index] |= R_VCMDQ0_GERROR_CMDQ_INIT_ERR_MASK;
        cmdqv->vcmdq_status[index] &= ~R_VCMDQ0_STATUS_CMDQ_EN_OK_MASK;
        return false;
    }
    hw_queue = g_new(IOMMUFDHWqueue, 1);
    hw_queue->hw_queue_id = hw_queue_id;
    hw_queue->viommu = viommu;
    cmdqv->vcmdq[index] = hw_queue;

    cmdqv->vcmdq_gerror[index] &= ~R_VCMDQ0_GERROR_CMDQ_INIT_ERR_MASK;
    cmdqv->vcmdq_status[index] |= R_VCMDQ0_STATUS_CMDQ_EN_OK_MASK;

    return true;
}

static void tegra241_cmdqv_free_all_vcmdq(Tegra241CMDQV *cmdqv)
{
    /* uapi/linux/iommufd.h: hw_queue destroy must be in descending @index. */
    for (int i = (TEGRA241_CMDQV_MAX_CMDQ - 1); i >= 0; i--) {
        tegra241_cmdqv_free_vcmdq(cmdqv, i);
    }
}

static void tegra241_cmdqv_setup_all_vcmdq(Tegra241CMDQV *cmdqv,
                                           Error **errp)
{
    for (int i = 0; i < TEGRA241_CMDQV_MAX_CMDQ; i++) {
        if (!tegra241_cmdqv_setup_vcmdq(cmdqv, i, errp)) {
            return;
        }
    }
}

/*
 * Read a VCMDQ Page 0 register (control/status) using VCMDQ0_* offsets.
 *
 * The caller normalizes the MMIO offset such that @offset0 always refers
 * to a VCMDQ0_* register, while @index selects the VCMDQ instance.
 */
static uint64_t tegra241_cmdqv_read_vcmdq_page0(Tegra241CMDQV *cmdqv,
                                                hwaddr offset0, int index,
                                                bool direct)
{
    uint64_t val = 0;

    switch (offset0) {
    case A_VCMDQ0_CONS_INDX:
        val = cmdqv->vcmdq_cons_indx[index];
        break;
    case A_VCMDQ0_PROD_INDX:
        val = cmdqv->vcmdq_prod_indx[index];
        break;
    case A_VCMDQ0_CONFIG:
        val = cmdqv->vcmdq_config[index];
        break;
    case A_VCMDQ0_STATUS:
        val = cmdqv->vcmdq_status[index];
        break;
    case A_VCMDQ0_GERROR:
        val = cmdqv->vcmdq_gerror[index];
        break;
    case A_VCMDQ0_GERRORN:
        val = cmdqv->vcmdq_gerrorn[index];
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s unhandled read access at 0x%" PRIx64 "\n",
                      __func__, offset0);
    }
    trace_tegra241_cmdqv_read_vcmdq_page0(index, direct ? "direct" : "vi",
                                          offset0, val);
    return val;
}

/*
 * Read a VCMDQ Page 1 register (base / DRAM address) using VCMDQ0_* offsets.
 */
static uint64_t tegra241_cmdqv_read_vcmdq_page1(Tegra241CMDQV *cmdqv,
                                                hwaddr offset0, int index,
                                                bool direct)
{
    uint64_t val = 0;

    switch (offset0) {
    case A_VCMDQ0_BASE_L:
        val = cmdqv->vcmdq_base[index];
        break;
    case A_VCMDQ0_BASE_H:
        val = cmdqv->vcmdq_base[index] >> 32;
        break;
    case A_VCMDQ0_CONS_INDX_BASE_DRAM_L:
        val = cmdqv->vcmdq_cons_indx_base[index];
        break;
    case A_VCMDQ0_CONS_INDX_BASE_DRAM_H:
        val = cmdqv->vcmdq_cons_indx_base[index] >> 32;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s unhandled read access at 0x%" PRIx64 "\n",
                      __func__, offset0);
    }
    trace_tegra241_cmdqv_read_vcmdq_page1(index, direct ? "direct" : "vi",
                                          offset0, val);
    return val;
}

static uint64_t tegra241_cmdqv_config_vintf_read(Tegra241CMDQV *cmdqv,
                                                 hwaddr offset)
{
    int i;

    switch (offset) {
    case A_VINTF0_CONFIG:
        return cmdqv->vintf_config;
    case A_VINTF0_STATUS:
        return cmdqv->vintf_status;
    case A_VINTF0_SID_MATCH_0 ... A_VINTF0_SID_MATCH_15:
        i = (offset - A_VINTF0_SID_MATCH_0) / 4;
        return cmdqv->vintf_sid_match[i];
    case A_VINTF0_SID_REPLACE_0 ... A_VINTF0_SID_REPLACE_15:
        i = (offset - A_VINTF0_SID_REPLACE_0) / 4;
        return cmdqv->vintf_sid_replace[i];
    case A_VINTF0_LVCMDQ_ERR_MAP_0 ... A_VINTF0_LVCMDQ_ERR_MAP_3:
        i = (offset - A_VINTF0_LVCMDQ_ERR_MAP_0) / 4;
        return cmdqv->vintf_cmdq_err_map[i];
    default:
        /*
         * GLB_FILT_CFG_0 (offset 0xC) and GLB_FILT_DATA_0 (offset 0x10) are
         * filter config and filter data registers. They are not required for
         * normal VINTF operation and are not emulated.
         */
        qemu_log_mask(LOG_UNIMP, "%s unhandled read access at 0x%" PRIx64 "\n",
                      __func__, offset);
        return 0;
    }
}

/*
 * Write a VCMDQ Page 0 register (control/status) using VCMDQ0_* offsets.
 *
 * The caller normalizes the MMIO offset such that @offset0 always refers
 * to a VCMDQ0_* register, while @index selects the VCMDQ instance.
 *
 * Page 0 registers are all 32-bit; this helper is only called for 4-byte
 * writes.
 */
static void tegra241_cmdqv_write_vcmdq_page0(Tegra241CMDQV *cmdqv,
                                             hwaddr offset0, int index,
                                             uint32_t value, bool direct)
{
    switch (offset0) {
    case A_VCMDQ0_CONS_INDX:
        cmdqv->vcmdq_cons_indx[index] = value;
        break;
    case A_VCMDQ0_PROD_INDX:
        /* VCMDQ is functional only once allocated to a VINTF; cache only. */
        cmdqv->vcmdq_prod_indx[index] = value;
        break;
    case A_VCMDQ0_CONFIG:
        if (value & R_VCMDQ0_CONFIG_CMDQ_EN_MASK) {
            /* Report init error if any. */
            if (!(cmdqv->vcmdq_gerror[index] &
                  R_VCMDQ0_GERROR_CMDQ_INIT_ERR_MASK)) {
                cmdqv->vcmdq_status[index] |=
                    R_VCMDQ0_STATUS_CMDQ_EN_OK_MASK;
            }
        } else {
            cmdqv->vcmdq_status[index] &= ~R_VCMDQ0_STATUS_CMDQ_EN_OK_MASK;
        }
        cmdqv->vcmdq_config[index] = value;
        break;
    case A_VCMDQ0_GERRORN:
        /* VCMDQ is functional only once allocated to a VINTF; cache only. */
        cmdqv->vcmdq_gerrorn[index] = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s unhandled write access at 0x%" PRIx64 "\n",
                      __func__, offset0);
    }
    trace_tegra241_cmdqv_write_vcmdq_page0(index, direct ? "direct" : "vi",
                                           offset0, value);
}

/*
 * Write a VCMDQ Page 1 register (base / DRAM address) - 4-byte access.
 */
static void tegra241_cmdqv_write_vcmdq_page1(Tegra241CMDQV *cmdqv,
                                             hwaddr offset0, int index,
                                             uint32_t value, bool direct,
                                             Error **errp)
{
    switch (offset0) {
    case A_VCMDQ0_BASE_L:
        cmdqv->vcmdq_base[index] =
            deposit64(cmdqv->vcmdq_base[index], 0, 32, value);
        tegra241_cmdqv_setup_vcmdq(cmdqv, index, errp);
        break;
    case A_VCMDQ0_BASE_H:
        cmdqv->vcmdq_base[index] =
            deposit64(cmdqv->vcmdq_base[index], 32, 32, value);
        tegra241_cmdqv_setup_vcmdq(cmdqv, index, errp);
        break;
    case A_VCMDQ0_CONS_INDX_BASE_DRAM_L:
        cmdqv->vcmdq_cons_indx_base[index] =
            deposit64(cmdqv->vcmdq_cons_indx_base[index], 0, 32, value);
        break;
    case A_VCMDQ0_CONS_INDX_BASE_DRAM_H:
        cmdqv->vcmdq_cons_indx_base[index] =
            deposit64(cmdqv->vcmdq_cons_indx_base[index], 32, 32, value);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s unhandled write access at 0x%" PRIx64 "\n",
                      __func__, offset0);
    }
    trace_tegra241_cmdqv_write_vcmdq_page1(index, direct ? "direct" : "vi",
                                           offset0, value);
}

/*
 * Write a VCMDQ Page 1 register - 8-byte access at BASE_L or DRAM_L.
 */
static void tegra241_cmdqv_write_vcmdq_page1_64(Tegra241CMDQV *cmdqv,
                                                hwaddr offset0, int index,
                                                uint64_t value, bool direct,
                                                Error **errp)
{
    switch (offset0) {
    case A_VCMDQ0_BASE_L:
        cmdqv->vcmdq_base[index] = value;
        tegra241_cmdqv_setup_vcmdq(cmdqv, index, errp);
        break;
    case A_VCMDQ0_CONS_INDX_BASE_DRAM_L:
        cmdqv->vcmdq_cons_indx_base[index] = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s unhandled 64-bit write at 0x%" PRIx64 "\n",
                      __func__, offset0);
    }
    trace_tegra241_cmdqv_write_vcmdq_page1(index, direct ? "direct" : "vi",
                                           offset0, value);
}

static void tegra241_cmdqv_config_vintf_write(Tegra241CMDQV *cmdqv,
                                              hwaddr offset, uint64_t value,
                                              Error **errp)
{
    int i;

    switch (offset) {
    case A_VINTF0_CONFIG:
        /*
         * Mask out HYP_OWN on guest writes. This bit selects Hypervisor (1) vs
         * Guest (0) ownership of the CMDQ. Force it to 0 so the VINTF always
         * remains guest-owned.
         */
        value &= ~R_VINTF0_CONFIG_HYP_OWN_MASK;

        cmdqv->vintf_config = value;
        if (value & R_VINTF0_CONFIG_ENABLE_MASK) {
            cmdqv->vintf_status |= R_VINTF0_STATUS_ENABLE_OK_MASK;
            /*
             * VCMDQs whose BASE was programmed before VINTF was
             * enabled need their hw_queue allocated now.
             */
            tegra241_cmdqv_setup_all_vcmdq(cmdqv, errp);
        } else {
            tegra241_cmdqv_free_all_vcmdq(cmdqv);
            cmdqv->vintf_status &= ~R_VINTF0_STATUS_ENABLE_OK_MASK;
        }
        break;
    case A_VINTF0_SID_MATCH_0 ... A_VINTF0_SID_MATCH_15:
        i = (offset - A_VINTF0_SID_MATCH_0) / 4;
        cmdqv->vintf_sid_match[i] = value;
        break;
    case A_VINTF0_SID_REPLACE_0 ... A_VINTF0_SID_REPLACE_15:
        i = (offset - A_VINTF0_SID_REPLACE_0) / 4;
        cmdqv->vintf_sid_replace[i] = value;
        break;
    default:
        /*
         * GLB_FILT_CFG_0 (offset 0xC) and GLB_FILT_DATA_0 (offset 0x10) are
         * filter config and filter data registers. They are not required for
         * normal VINTF operation and are not emulated.
         */
        qemu_log_mask(LOG_UNIMP, "%s unhandled write access at 0x%" PRIx64 "\n",
                      __func__, offset);
        return;
    }
}

static uint64_t tegra241_cmdqv_read_mmio(void *opaque, hwaddr offset,
                                         unsigned size)
{
    Tegra241CMDQV *cmdqv = (Tegra241CMDQV *)opaque;
    uint64_t val = 0;
    int index;

    if (offset >= TEGRA241_CMDQV_IO_LEN) {
        qemu_log_mask(LOG_UNIMP,
                      "%s offset 0x%" PRIx64 " off limit (0x%x)\n", __func__,
                      offset, TEGRA241_CMDQV_IO_LEN);
        goto out;
    }

    switch (offset) {
    case A_CONFIG:
        val = cmdqv->config;
        break;
    case A_PARAM:
        val = cmdqv->param;
        break;
    case A_STATUS:
        val = cmdqv->status;
        break;
    case A_VI_ERR_MAP_0 ... A_VI_ERR_MAP_1:
        val = cmdqv->vi_err_map[(offset - A_VI_ERR_MAP_0) / 4];
        break;
    case A_VI_INT_MASK_0 ... A_VI_INT_MASK_1:
        val = cmdqv->vi_int_mask[(offset - A_VI_INT_MASK_0) / 4];
        break;
    case A_CMDQ_ERR_MAP_0 ... A_CMDQ_ERR_MAP_3:
        val = cmdqv->cmdq_err_map[(offset - A_CMDQ_ERR_MAP_0) / 4];
        break;
    case A_CMDQ_ALLOC_MAP_0 ... A_CMDQ_ALLOC_MAP_1:
        val = cmdqv->cmdq_alloc_map[(offset - A_CMDQ_ALLOC_MAP_0) / 4];
        break;
    case A_VINTF0_CONFIG ... A_VINTF0_LVCMDQ_ERR_MAP_3:
        val = tegra241_cmdqv_config_vintf_read(cmdqv, offset);
        break;
    case A_VI_VCMDQ0_CONS_INDX ... A_VI_VCMDQ1_GERRORN:
        /*
         * VINTF Page0 registers are hardware aliases of VCMDQ Page0 registers.
         * Translate the VINTF aperture offset to its VCMDQ Page0 equivalent
         * before dispatching to the Page 0 helper.
         */
        offset -= CMDQV_VINTF_PAGE0_BASE - CMDQV_VCMDQ_PAGE0_BASE;
        index = (offset - CMDQV_VCMDQ_PAGE0_BASE) / CMDQV_VCMDQ_STRIDE;
        return tegra241_cmdqv_read_vcmdq_page0(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, false);
    case A_VCMDQ0_CONS_INDX ... A_VCMDQ1_GERRORN:
        /*
         * Decode a per-VCMDQ Page 0 access. Each VCMDQ occupies a
         * CMDQV_VCMDQ_STRIDE-byte window; extract the index and normalize
         * to the VCMDQ0_* offset before calling the Page 0 helper.
         */
        index = (offset - CMDQV_VCMDQ_PAGE0_BASE) / CMDQV_VCMDQ_STRIDE;
        return tegra241_cmdqv_read_vcmdq_page0(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, true);
    case A_VI_VCMDQ0_BASE_L ... A_VI_VCMDQ1_CONS_INDX_BASE_DRAM_H:
        /* Same VINTF-to-VCMDQ translation as VINTF Page0 case above. */
        offset -= CMDQV_VINTF_PAGE1_BASE - CMDQV_VCMDQ_PAGE1_BASE;
        index = (offset - CMDQV_VCMDQ_PAGE1_BASE) / CMDQV_VCMDQ_STRIDE;
        return tegra241_cmdqv_read_vcmdq_page1(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, false);
    case A_VCMDQ0_BASE_L ... A_VCMDQ1_CONS_INDX_BASE_DRAM_H:
        index = (offset - CMDQV_VCMDQ_PAGE1_BASE) / CMDQV_VCMDQ_STRIDE;
        return tegra241_cmdqv_read_vcmdq_page1(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, true);
    default:
        qemu_log_mask(LOG_UNIMP, "%s unhandled read access at 0x%" PRIx64 "\n",
                      __func__, offset);
    }

out:
    trace_tegra241_cmdqv_read_mmio(offset, val, size);
    return val;
}

/* 4-byte MMIO write handler. */
static void tegra241_cmdqv_writel_mmio(Tegra241CMDQV *cmdqv, hwaddr offset,
                                       uint32_t value)
{
    Error *local_err = NULL;
    int index;

    switch (offset) {
    case A_CONFIG:
        cmdqv->config = value;
        if (value & R_CONFIG_CMDQV_EN_MASK) {
            cmdqv->status |= R_STATUS_CMDQV_ENABLED_MASK;
            /*
             * VCMDQs whose BASE was programmed before CMDQV was enabled
             * need their hw_queue allocated now.
             */
            tegra241_cmdqv_setup_all_vcmdq(cmdqv, &local_err);
        } else {
            tegra241_cmdqv_free_all_vcmdq(cmdqv);
            cmdqv->status &= ~R_STATUS_CMDQV_ENABLED_MASK;
        }
        break;
    case A_VI_INT_MASK_0 ... A_VI_INT_MASK_1:
        cmdqv->vi_int_mask[(offset - A_VI_INT_MASK_0) / 4] = value;
        break;
    case A_CMDQ_ALLOC_MAP_0 ... A_CMDQ_ALLOC_MAP_1: {
        int idx = (offset - A_CMDQ_ALLOC_MAP_0) / 4;
        bool was_alloc = cmdqv->cmdq_alloc_map[idx] &
                         R_CMDQ_ALLOC_MAP_0_ALLOC_MASK;
        bool now_alloc = value & R_CMDQ_ALLOC_MAP_0_ALLOC_MASK;

        cmdqv->cmdq_alloc_map[idx] = value;
        /*
         * If the VCMDQ was already programmed (BASE) before mapping, fire
         * setup on the ALLOC 0->1 transition; tear down on 1->0.
         */
        if (!was_alloc && now_alloc) {
            tegra241_cmdqv_setup_vcmdq(cmdqv, idx, &local_err);
        } else if (was_alloc && !now_alloc) {
            tegra241_cmdqv_free_vcmdq(cmdqv, idx);
        }
        break;
    }
    case A_VINTF0_CONFIG ... A_VINTF0_LVCMDQ_ERR_MAP_3:
        tegra241_cmdqv_config_vintf_write(cmdqv, offset, value, &local_err);
        break;
    case A_VI_VCMDQ0_CONS_INDX ... A_VI_VCMDQ1_GERRORN:
        /*
         * VINTF Page0 registers are hardware aliases of VCMDQ Page0 registers.
         * Translate the VINTF aperture offset to its VCMDQ Page0 equivalent
         * before dispatching to the Page 0 helper.
         */
        offset -= CMDQV_VINTF_PAGE0_BASE - CMDQV_VCMDQ_PAGE0_BASE;
        index = (offset - CMDQV_VCMDQ_PAGE0_BASE) / CMDQV_VCMDQ_STRIDE;
        tegra241_cmdqv_write_vcmdq_page0(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, value, false);
        break;
    case A_VCMDQ0_CONS_INDX ... A_VCMDQ1_GERRORN:
        /*
         * Decode a per-VCMDQ Page 0 access. Each VCMDQ occupies a
         * CMDQV_VCMDQ_STRIDE-byte window; extract the index and normalize
         * to the VCMDQ0_* offset before calling the Page 0 helper.
         */
        index = (offset - CMDQV_VCMDQ_PAGE0_BASE) / CMDQV_VCMDQ_STRIDE;
        tegra241_cmdqv_write_vcmdq_page0(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, value, true);
        break;
    case A_VI_VCMDQ0_BASE_L ... A_VI_VCMDQ1_CONS_INDX_BASE_DRAM_H:
        /* Same VINTF-to-VCMDQ translation as VINTF Page0 case above. */
        offset -= CMDQV_VINTF_PAGE1_BASE - CMDQV_VCMDQ_PAGE1_BASE;
        index = (offset - CMDQV_VCMDQ_PAGE1_BASE) / CMDQV_VCMDQ_STRIDE;
        tegra241_cmdqv_write_vcmdq_page1(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, value, false,
                &local_err);
        break;
    case A_VCMDQ0_BASE_L ... A_VCMDQ1_CONS_INDX_BASE_DRAM_H:
        index = (offset - CMDQV_VCMDQ_PAGE1_BASE) / CMDQV_VCMDQ_STRIDE;
        tegra241_cmdqv_write_vcmdq_page1(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, value, true,
                &local_err);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s unhandled write access at 0x%" PRIx64 "\n",
                      __func__, offset);
    }

    if (local_err) {
        error_report_err(local_err);
    }
}

/*
 * 8-byte MMIO write handler. Only Page 1 BASE / CONS_INDX_BASE_DRAM accept
 * full 64-bit writes; other offsets are write-ignored.
 */
static void tegra241_cmdqv_writell_mmio(Tegra241CMDQV *cmdqv, hwaddr offset,
                                        uint64_t value)
{
    Error *local_err = NULL;
    int index;

    switch (offset) {
    case A_VI_VCMDQ0_BASE_L ... A_VI_VCMDQ1_CONS_INDX_BASE_DRAM_H:
        /*
         * VINTF Page1 registers are hardware aliases of VCMDQ Page1 registers.
         * Translate the VINTF aperture offset to its VCMDQ Page1 equivalent
         * before dispatching to the Page 1 helper.
         */
        offset -= CMDQV_VINTF_PAGE1_BASE - CMDQV_VCMDQ_PAGE1_BASE;
        index = (offset - CMDQV_VCMDQ_PAGE1_BASE) / CMDQV_VCMDQ_STRIDE;
        tegra241_cmdqv_write_vcmdq_page1_64(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, value, false,
                &local_err);
        break;
    case A_VCMDQ0_BASE_L ... A_VCMDQ1_CONS_INDX_BASE_DRAM_H:
        index = (offset - CMDQV_VCMDQ_PAGE1_BASE) / CMDQV_VCMDQ_STRIDE;
        tegra241_cmdqv_write_vcmdq_page1_64(cmdqv,
                offset - index * CMDQV_VCMDQ_STRIDE, index, value, true,
                &local_err);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s unhandled 64-bit write at 0x%" PRIx64 " (WI)\n",
                      __func__, offset);
    }

    if (local_err) {
        error_report_err(local_err);
    }
}

static void tegra241_cmdqv_write_mmio(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size)
{
    Tegra241CMDQV *cmdqv = (Tegra241CMDQV *)opaque;

    if (offset >= TEGRA241_CMDQV_IO_LEN) {
        qemu_log_mask(LOG_UNIMP,
                      "%s offset 0x%" PRIx64 " off limit (0x%x)\n", __func__,
                      offset, TEGRA241_CMDQV_IO_LEN);
        goto out;
    }

    switch (size) {
    case 4:
        tegra241_cmdqv_writel_mmio(cmdqv, offset, value);
        break;
    case 8:
        tegra241_cmdqv_writell_mmio(cmdqv, offset, value);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s bad write size %u at 0x%" PRIx64 "\n",
                      __func__, size, offset);
    }

out:
    trace_tegra241_cmdqv_write_mmio(offset, value, size);
}

static void tegra241_cmdqv_free_viommu(SMMUv3State *s)
{
    SMMUv3AccelState *accel = s->s_accel;
    IOMMUFDViommu *viommu = accel->viommu;
    Tegra241CMDQV *cmdqv = accel->cmdqv;
    IOMMUFDVeventq *veventq = cmdqv->veventq;

    if (!viommu) {
        return;
    }
    if (veventq) {
        close(veventq->veventq_fd);
        iommufd_backend_free_id(viommu->iommufd, veventq->veventq_id);
        g_free(veventq);
        cmdqv->veventq = NULL;
    }
    if (cmdqv->vintf_page0) {
        munmap(cmdqv->vintf_page0, VINTF_PAGE_SIZE);
        cmdqv->vintf_page0 = NULL;
    }
    iommufd_backend_free_id(viommu->iommufd, viommu->viommu_id);
}

static bool
tegra241_cmdqv_alloc_viommu(SMMUv3State *s, HostIOMMUDeviceIOMMUFD *idev,
                            uint32_t *out_viommu_id, Error **errp)
{
    Tegra241CMDQV *cmdqv = s->s_accel->cmdqv;
    uint32_t viommu_id, veventq_id, veventq_fd;
    IOMMUFDVeventq *veventq;

    if (!iommufd_backend_alloc_viommu(idev->iommufd, idev->devid,
                                      IOMMU_VIOMMU_TYPE_TEGRA241_CMDQV,
                                      idev->hwpt_id, cmdqv->cmdqv_data,
                                      sizeof(*cmdqv->cmdqv_data), &viommu_id,
                                      errp)) {
        return false;
    }

    if (!iommufd_backend_viommu_mmap(idev->iommufd, viommu_id, VINTF_PAGE_SIZE,
                                     cmdqv->cmdqv_data->out_vintf_mmap_offset,
                                     &cmdqv->vintf_page0, errp)) {
        error_append_hint(errp, "Tegra241 CMDQV: failed to mmap VINTF page0");
        goto free_viommu;
    }

    if (!iommufd_backend_alloc_veventq(idev->iommufd, viommu_id,
                                       IOMMU_VEVENTQ_TYPE_TEGRA241_CMDQV,
                                       1 << SMMU_EVENTQS, &veventq_id,
                                       &veventq_fd,
                                       errp)) {
        error_append_hint(errp, "Tegra241 CMDQV: failed to alloc veventq");
        goto munmap_page0;
    }

    veventq = g_new(IOMMUFDVeventq, 1);
    veventq->veventq_id = veventq_id;
    veventq->veventq_fd = veventq_fd;
    cmdqv->veventq = veventq;

    *out_viommu_id = viommu_id;
    return true;

munmap_page0:
    munmap(cmdqv->vintf_page0, VINTF_PAGE_SIZE);
    cmdqv->vintf_page0 = NULL;
free_viommu:
    iommufd_backend_free_id(idev->iommufd, viommu_id);
    return false;
}

static void tegra241_cmdqv_reset(SMMUv3State *s)
{
    Tegra241CMDQV *cmdqv = s->s_accel->cmdqv;

    if (!cmdqv) {
        return;
    }

    tegra241_cmdqv_free_all_vcmdq(cmdqv);
}

static const MemoryRegionOps mmio_cmdqv_ops = {
    .read = tegra241_cmdqv_read_mmio,
    .write = tegra241_cmdqv_write_mmio,
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

static bool tegra241_cmdqv_init(SMMUv3State *s, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(OBJECT(s));
    SMMUv3AccelState *accel = s->s_accel;
    Tegra241CMDQV *cmdqv;

    cmdqv = g_new0(Tegra241CMDQV, 1);
    cmdqv->cmdqv_data = g_new0(struct iommu_viommu_tegra241_cmdqv, 1);
    memory_region_init_io(&cmdqv->mmio_cmdqv, OBJECT(s), &mmio_cmdqv_ops, cmdqv,
                          "tegra241-cmdqv", TEGRA241_CMDQV_IO_LEN);
    sysbus_init_mmio(sbd, &cmdqv->mmio_cmdqv);
    sysbus_init_irq(sbd, &cmdqv->irq);
    cmdqv->s_accel = accel;
    accel->cmdqv = cmdqv;
    return true;
}

static bool tegra241_cmdqv_probe(SMMUv3State *s, HostIOMMUDeviceIOMMUFD *idev,
                                 Error **errp)
{
    uint32_t data_type = IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV;
    struct iommu_hw_info_tegra241_cmdqv cmdqv_info;
    uint64_t caps;

    if (!iommufd_backend_get_device_info(idev->iommufd, idev->devid, &data_type,
                                         &cmdqv_info, sizeof(cmdqv_info), &caps,
                                         NULL, errp)) {
        return false;
    }
    if (data_type != IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV) {
        error_setg(errp, "Host CMDQV: unexpected data type %u (expected %u)",
                   data_type, IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV);
        return false;
    }
    if (cmdqv_info.version != CMDQV_VER) {
        error_setg(errp, "Host CMDQV: unsupported version %u (expected %u)",
                   cmdqv_info.version, CMDQV_VER);
        return false;
    }
    if (cmdqv_info.log2vcmdqs < CMDQV_NUM_CMDQ_LOG2) {
        error_setg(errp, "Host CMDQV: insufficient vCMDQs log2=%u (need >= %u)",
                   cmdqv_info.log2vcmdqs, CMDQV_NUM_CMDQ_LOG2);
        return false;
    }
    if (cmdqv_info.log2vsids < CMDQV_NUM_SID_PER_VI_LOG2) {
        error_setg(errp, "Host CMDQV: insufficient SIDs log2=%u (need >= %u)",
                   cmdqv_info.log2vsids, CMDQV_NUM_SID_PER_VI_LOG2);
        return false;
    }
    return true;
}

static const SMMUv3AccelCmdqvOps tegra241_cmdqv_ops = {
    .probe = tegra241_cmdqv_probe,
    .init = tegra241_cmdqv_init,
    .alloc_viommu = tegra241_cmdqv_alloc_viommu,
    .free_viommu = tegra241_cmdqv_free_viommu,
    .reset = tegra241_cmdqv_reset,
};

const SMMUv3AccelCmdqvOps *tegra241_cmdqv_get_ops(void)
{
    return &tegra241_cmdqv_ops;
}
