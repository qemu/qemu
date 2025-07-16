/*
 * s390 PCI instructions
 *
 * Copyright 2014 IBM Corp.
 * Author(s): Frank Blaschka <frank.blaschka@de.ibm.com>
 *            Hong Bo Li <lihbbj@cn.ibm.com>
 *            Yi Min Zhao <zyimin@cn.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "exec/memop.h"
#include "exec/target_page.h"
#include "system/memory.h"
#include "qemu/error-report.h"
#include "qemu/bswap.h"
#include "system/hw_accel.h"
#include "hw/boards.h"
#include "hw/pci/pci_device.h"
#include "hw/s390x/s390-pci-inst.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/s390-pci-kvm.h"
#include "hw/s390x/s390-pci-vfio.h"
#include "hw/s390x/tod.h"

#include "trace.h"

static inline void inc_dma_avail(S390PCIIOMMU *iommu)
{
    if (iommu->dma_limit) {
        iommu->dma_limit->avail++;
    }
}

static inline void dec_dma_avail(S390PCIIOMMU *iommu)
{
    if (iommu->dma_limit) {
        iommu->dma_limit->avail--;
    }
}

static void s390_set_status_code(CPUS390XState *env,
                                 uint8_t r, uint64_t status_code)
{
    env->regs[r] &= ~0xff000000ULL;
    env->regs[r] |= (status_code & 0xff) << 24;
}

static int list_pci(ClpReqRspListPci *rrb, uint8_t *cc)
{
    S390PCIBusDevice *pbdev = NULL;
    S390pciState *s = s390_get_phb();
    uint32_t res_code, initial_l2, g_l2;
    int rc, i;
    uint64_t resume_token;

    rc = 0;
    if (lduw_be_p(&rrb->request.hdr.len) != 32) {
        res_code = CLP_RC_LEN;
        rc = -EINVAL;
        goto out;
    }

    if ((ldl_be_p(&rrb->request.fmt) & CLP_MASK_FMT) != 0) {
        res_code = CLP_RC_FMT;
        rc = -EINVAL;
        goto out;
    }

    if ((ldl_be_p(&rrb->request.fmt) & ~CLP_MASK_FMT) != 0 ||
        ldq_be_p(&rrb->request.reserved1) != 0) {
        res_code = CLP_RC_RESNOT0;
        rc = -EINVAL;
        goto out;
    }

    resume_token = ldq_be_p(&rrb->request.resume_token);

    if (resume_token) {
        pbdev = s390_pci_find_dev_by_idx(s, resume_token);
        if (!pbdev) {
            res_code = CLP_RC_LISTPCI_BADRT;
            rc = -EINVAL;
            goto out;
        }
    } else {
        pbdev = s390_pci_find_next_avail_dev(s, NULL);
    }

    if (lduw_be_p(&rrb->response.hdr.len) < 48) {
        res_code = CLP_RC_8K;
        rc = -EINVAL;
        goto out;
    }

    initial_l2 = lduw_be_p(&rrb->response.hdr.len);
    if ((initial_l2 - LIST_PCI_HDR_LEN) % sizeof(ClpFhListEntry)
        != 0) {
        res_code = CLP_RC_LEN;
        rc = -EINVAL;
        *cc = 3;
        goto out;
    }

    stl_be_p(&rrb->response.fmt, 0);
    stq_be_p(&rrb->response.reserved1, 0);
    stl_be_p(&rrb->response.mdd, FH_MASK_SHM);
    stw_be_p(&rrb->response.max_fn, PCI_MAX_FUNCTIONS);
    rrb->response.flags = UID_CHECKING_ENABLED;
    rrb->response.entry_size = sizeof(ClpFhListEntry);

    i = 0;
    g_l2 = LIST_PCI_HDR_LEN;
    while (g_l2 < initial_l2 && pbdev) {
        stw_be_p(&rrb->response.fh_list[i].device_id,
            pci_get_word(pbdev->pdev->config + PCI_DEVICE_ID));
        stw_be_p(&rrb->response.fh_list[i].vendor_id,
            pci_get_word(pbdev->pdev->config + PCI_VENDOR_ID));
        /* Ignore RESERVED devices. */
        stl_be_p(&rrb->response.fh_list[i].config,
            pbdev->state == ZPCI_FS_STANDBY ? 0 : 1 << 31);
        stl_be_p(&rrb->response.fh_list[i].fid, pbdev->fid);
        stl_be_p(&rrb->response.fh_list[i].fh, pbdev->fh);

        g_l2 += sizeof(ClpFhListEntry);
        /* Add endian check for DPRINTF? */
        trace_s390_pci_list_entry(g_l2,
                lduw_be_p(&rrb->response.fh_list[i].vendor_id),
                lduw_be_p(&rrb->response.fh_list[i].device_id),
                ldl_be_p(&rrb->response.fh_list[i].fid),
                ldl_be_p(&rrb->response.fh_list[i].fh));
        pbdev = s390_pci_find_next_avail_dev(s, pbdev);
        i++;
    }

    if (!pbdev) {
        resume_token = 0;
    } else {
        resume_token = pbdev->fh & FH_MASK_INDEX;
    }
    stq_be_p(&rrb->response.resume_token, resume_token);
    stw_be_p(&rrb->response.hdr.len, g_l2);
    stw_be_p(&rrb->response.hdr.rsp, CLP_RC_OK);
out:
    if (rc) {
        trace_s390_pci_list(rc);
        stw_be_p(&rrb->response.hdr.rsp, res_code);
    }
    return rc;
}

int clp_service_call(S390CPU *cpu, uint8_t r2, uintptr_t ra)
{
    ClpReqHdr *reqh;
    ClpRspHdr *resh;
    S390PCIBusDevice *pbdev;
    uint32_t req_len;
    uint32_t res_len;
    uint8_t buffer[4096 * 2];
    uint8_t cc = 0;
    CPUS390XState *env = &cpu->env;
    S390pciState *s = s390_get_phb();
    int i;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return 0;
    }

    if (s390_cpu_virt_mem_read(cpu, env->regs[r2], r2, buffer, sizeof(*reqh))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return 0;
    }
    reqh = (ClpReqHdr *)buffer;
    req_len = lduw_be_p(&reqh->len);
    if (req_len < 16 || req_len > 8184 || (req_len % 8 != 0)) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return 0;
    }

    if (s390_cpu_virt_mem_read(cpu, env->regs[r2], r2, buffer,
                               req_len + sizeof(*resh))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return 0;
    }
    resh = (ClpRspHdr *)(buffer + req_len);
    res_len = lduw_be_p(&resh->len);
    if (res_len < 8 || res_len > 8176 || (res_len % 8 != 0)) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return 0;
    }
    if ((req_len + res_len) > 8192) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return 0;
    }

    if (s390_cpu_virt_mem_read(cpu, env->regs[r2], r2, buffer,
                               req_len + res_len)) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return 0;
    }

    if (req_len != 32) {
        stw_be_p(&resh->rsp, CLP_RC_LEN);
        goto out;
    }

    switch (lduw_be_p(&reqh->cmd)) {
    case CLP_LIST_PCI: {
        ClpReqRspListPci *rrb = (ClpReqRspListPci *)buffer;
        list_pci(rrb, &cc);
        break;
    }
    case CLP_SET_PCI_FN: {
        ClpReqSetPci *reqsetpci = (ClpReqSetPci *)reqh;
        ClpRspSetPci *ressetpci = (ClpRspSetPci *)resh;

        pbdev = s390_pci_find_dev_by_fh(s, ldl_be_p(&reqsetpci->fh));
        if (!pbdev) {
                stw_be_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_FH);
                goto out;
        }

        switch (reqsetpci->oc) {
        case CLP_SET_ENABLE_PCI_FN:
            switch (reqsetpci->ndas) {
            case 0:
                stw_be_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_DMAAS);
                goto out;
            case 1:
                break;
            default:
                stw_be_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_RES);
                goto out;
            }

            if (pbdev->fh & FH_MASK_ENABLE) {
                stw_be_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_FHOP);
                goto out;
            }

            /*
             * Take this opportunity to make sure we still have an accurate
             * host fh.  It's possible part of the handle changed while the
             * device was disabled to the guest (e.g. vfio hot reset for
             * ISM during plug)
             */
            if (pbdev->interp) {
                /* Take this opportunity to make sure we are sync'd with host */
                if (!s390_pci_get_host_fh(pbdev, &pbdev->fh) ||
                    !(pbdev->fh & FH_MASK_ENABLE)) {
                    stw_be_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_FH);
                    goto out;
                }
            }
            pbdev->fh |= FH_MASK_ENABLE;
            pbdev->state = ZPCI_FS_ENABLED;
            stl_be_p(&ressetpci->fh, pbdev->fh);
            stw_be_p(&ressetpci->hdr.rsp, CLP_RC_OK);
            break;
        case CLP_SET_DISABLE_PCI_FN:
            if (!(pbdev->fh & FH_MASK_ENABLE)) {
                stw_be_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_FHOP);
                goto out;
            }
            device_cold_reset(DEVICE(pbdev));
            pbdev->fh &= ~FH_MASK_ENABLE;
            pbdev->state = ZPCI_FS_DISABLED;
            stl_be_p(&ressetpci->fh, pbdev->fh);
            stw_be_p(&ressetpci->hdr.rsp, CLP_RC_OK);
            break;
        default:
            trace_s390_pci_unknown("set-pci", reqsetpci->oc);
            stw_be_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_FHOP);
            break;
        }
        break;
    }
    case CLP_QUERY_PCI_FN: {
        ClpReqQueryPci *reqquery = (ClpReqQueryPci *)reqh;
        ClpRspQueryPci *resquery = (ClpRspQueryPci *)resh;

        pbdev = s390_pci_find_dev_by_fh(s, ldl_be_p(&reqquery->fh));
        if (!pbdev) {
            trace_s390_pci_nodev("query", ldl_be_p(&reqquery->fh));
            stw_be_p(&resquery->hdr.rsp, CLP_RC_SETPCIFN_FH);
            goto out;
        }

        stq_be_p(&resquery->sdma, pbdev->zpci_fn.sdma);
        stq_be_p(&resquery->edma, pbdev->zpci_fn.edma);
        stw_be_p(&resquery->pchid, pbdev->zpci_fn.pchid);
        stw_be_p(&resquery->vfn, pbdev->zpci_fn.vfn);
        resquery->flags = pbdev->zpci_fn.flags;
        resquery->pfgid = pbdev->zpci_fn.pfgid;
        resquery->pft = pbdev->zpci_fn.pft;
        resquery->fmbl = pbdev->zpci_fn.fmbl;
        stl_be_p(&resquery->fid, pbdev->zpci_fn.fid);
        stl_be_p(&resquery->uid, pbdev->zpci_fn.uid);
        memcpy(resquery->pfip, pbdev->zpci_fn.pfip, CLP_PFIP_NR_SEGMENTS);
        memcpy(resquery->util_str, pbdev->zpci_fn.util_str, CLP_UTIL_STR_LEN);

        for (i = 0; i < PCI_BAR_COUNT; i++) {
            uint32_t data = pci_get_long(pbdev->pdev->config +
                PCI_BASE_ADDRESS_0 + (i * 4));

            stl_be_p(&resquery->bar[i], data);
            resquery->bar_size[i] = pbdev->pdev->io_regions[i].size ?
                                    ctz64(pbdev->pdev->io_regions[i].size) : 0;
            trace_s390_pci_bar(i,
                    ldl_be_p(&resquery->bar[i]),
                    pbdev->pdev->io_regions[i].size,
                    resquery->bar_size[i]);
        }

        stw_be_p(&resquery->hdr.rsp, CLP_RC_OK);
        break;
    }
    case CLP_QUERY_PCI_FNGRP: {
        ClpRspQueryPciGrp *resgrp = (ClpRspQueryPciGrp *)resh;

        ClpReqQueryPciGrp *reqgrp = (ClpReqQueryPciGrp *)reqh;
        S390PCIGroup *group;

        group = s390_group_find(reqgrp->g);
        if (!group) {
            /* We do not allow access to unknown groups */
            /* The group must have been obtained with a vfio device */
            stw_be_p(&resgrp->hdr.rsp, CLP_RC_QUERYPCIFG_PFGID);
            goto out;
        }
        resgrp->fr = group->zpci_group.fr;
        stq_be_p(&resgrp->dasm, group->zpci_group.dasm);
        stq_be_p(&resgrp->msia, group->zpci_group.msia);
        stw_be_p(&resgrp->mui, group->zpci_group.mui);
        stw_be_p(&resgrp->i, group->zpci_group.i);
        stw_be_p(&resgrp->maxstbl, group->zpci_group.maxstbl);
        resgrp->version = group->zpci_group.version;
        resgrp->dtsm = group->zpci_group.dtsm;
        stw_be_p(&resgrp->hdr.rsp, CLP_RC_OK);
        break;
    }
    default:
        trace_s390_pci_unknown("clp", lduw_be_p(&reqh->cmd));
        stw_be_p(&resh->rsp, CLP_RC_CMD);
        break;
    }

out:
    if (s390_cpu_virt_mem_write(cpu, env->regs[r2], r2, buffer,
                                req_len + res_len)) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return 0;
    }
    setcc(cpu, cc);
    return 0;
}

/**
 * Swap data contained in s390x big endian registers to little endian
 * PCI bars.
 *
 * @ptr: a pointer to a uint64_t data field
 * @len: the length of the valid data, must be 1,2,4 or 8
 */
static int zpci_endian_swap(uint64_t *ptr, uint8_t len)
{
    uint64_t data = *ptr;

    switch (len) {
    case 1:
        break;
    case 2:
        data = bswap16(data);
        break;
    case 4:
        data = bswap32(data);
        break;
    case 8:
        data = bswap64(data);
        break;
    default:
        return -EINVAL;
    }
    *ptr = data;
    return 0;
}

static MemoryRegion *s390_get_subregion(MemoryRegion *mr, uint64_t offset,
                                        uint8_t len)
{
    MemoryRegion *subregion;
    uint64_t subregion_size;

    QTAILQ_FOREACH(subregion, &mr->subregions, subregions_link) {
        subregion_size = int128_get64(subregion->size);
        if ((offset >= subregion->addr) &&
            (offset + len) <= (subregion->addr + subregion_size)) {
            mr = subregion;
            break;
        }
    }
    return mr;
}

static MemTxResult zpci_read_bar(S390PCIBusDevice *pbdev, uint8_t pcias,
                                 uint64_t offset, uint64_t *data, uint8_t len)
{
    MemoryRegion *mr;

    mr = pbdev->pdev->io_regions[pcias].memory;
    mr = s390_get_subregion(mr, offset, len);
    offset -= mr->addr;
    return memory_region_dispatch_read(mr, offset, data,
                                       size_memop(len) | MO_BE,
                                       MEMTXATTRS_UNSPECIFIED);
}

int pcilg_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2, uintptr_t ra)
{
    CPUS390XState *env = &cpu->env;
    S390PCIBusDevice *pbdev;
    uint64_t offset;
    uint64_t data;
    MemTxResult result;
    uint8_t len;
    uint32_t fh;
    uint8_t pcias;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return 0;
    }

    if (r2 & 0x1) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return 0;
    }

    fh = env->regs[r2] >> 32;
    pcias = (env->regs[r2] >> 16) & 0xf;
    len = env->regs[r2] & 0xf;
    offset = env->regs[r2 + 1];

    if (!(fh & FH_MASK_ENABLE)) {
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    }

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        trace_s390_pci_nodev("pcilg", fh);
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    }

    switch (pbdev->state) {
    case ZPCI_FS_PERMANENT_ERROR:
    case ZPCI_FS_ERROR:
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r2, ZPCI_PCI_ST_BLOCKED);
        return 0;
    default:
        break;
    }

    switch (pcias) {
    case ZPCI_IO_BAR_MIN...ZPCI_IO_BAR_MAX:
        if (!len || (len > (8 - (offset & 0x7)))) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
            return 0;
        }
        result = zpci_read_bar(pbdev, pcias, offset, &data, len);
        if (result != MEMTX_OK) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
            return 0;
        }
        break;
    case ZPCI_CONFIG_BAR:
        if (!len || (len > (4 - (offset & 0x3))) || len == 3) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
            return 0;
        }
        data =  pci_host_config_read_common(
                   pbdev->pdev, offset, pci_config_size(pbdev->pdev), len);

        if (zpci_endian_swap(&data, len)) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
            return 0;
        }
        break;
    default:
        trace_s390_pci_invalid("pcilg", fh);
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r2, ZPCI_PCI_ST_INVAL_AS);
        return 0;
    }

    pbdev->fmb.counter[ZPCI_FMB_CNT_LD]++;

    env->regs[r1] = data;
    setcc(cpu, ZPCI_PCI_LS_OK);
    return 0;
}

static MemTxResult zpci_write_bar(S390PCIBusDevice *pbdev, uint8_t pcias,
                                  uint64_t offset, uint64_t data, uint8_t len)
{
    MemoryRegion *mr;

    mr = pbdev->pdev->io_regions[pcias].memory;
    mr = s390_get_subregion(mr, offset, len);
    offset -= mr->addr;
    return memory_region_dispatch_write(mr, offset, data,
                                        size_memop(len) | MO_BE,
                                        MEMTXATTRS_UNSPECIFIED);
}

int pcistg_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2, uintptr_t ra)
{
    CPUS390XState *env = &cpu->env;
    uint64_t offset, data;
    S390PCIBusDevice *pbdev;
    MemTxResult result;
    uint8_t len;
    uint32_t fh;
    uint8_t pcias;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return 0;
    }

    if (r2 & 0x1) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return 0;
    }

    fh = env->regs[r2] >> 32;
    pcias = (env->regs[r2] >> 16) & 0xf;
    len = env->regs[r2] & 0xf;
    offset = env->regs[r2 + 1];
    data = env->regs[r1];

    if (!(fh & FH_MASK_ENABLE)) {
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    }

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        trace_s390_pci_nodev("pcistg", fh);
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    }

    switch (pbdev->state) {
    /* ZPCI_FS_RESERVED, ZPCI_FS_STANDBY and ZPCI_FS_DISABLED
     * are already covered by the FH_MASK_ENABLE check above
     */
    case ZPCI_FS_PERMANENT_ERROR:
    case ZPCI_FS_ERROR:
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r2, ZPCI_PCI_ST_BLOCKED);
        return 0;
    default:
        break;
    }

    switch (pcias) {
        /* A ZPCI PCI card may use any BAR from BAR 0 to BAR 5 */
    case ZPCI_IO_BAR_MIN...ZPCI_IO_BAR_MAX:
        /* Check length:
         * A length of 0 is invalid and length should not cross a double word
         */
        if (!len || (len > (8 - (offset & 0x7)))) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
            return 0;
        }

        result = zpci_write_bar(pbdev, pcias, offset, data, len);
        if (result != MEMTX_OK) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
            return 0;
        }
        break;
    case ZPCI_CONFIG_BAR:
        /* ZPCI uses the pseudo BAR number 15 as configuration space */
        /* possible access lengths are 1,2,4 and must not cross a word */
        if (!len || (len > (4 - (offset & 0x3))) || len == 3) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
            return 0;
        }
        /* len = 1,2,4 so we do not need to test */
        zpci_endian_swap(&data, len);
        pci_host_config_write_common(pbdev->pdev, offset,
                                     pci_config_size(pbdev->pdev),
                                     data, len);
        break;
    default:
        trace_s390_pci_invalid("pcistg", fh);
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r2, ZPCI_PCI_ST_INVAL_AS);
        return 0;
    }

    pbdev->fmb.counter[ZPCI_FMB_CNT_ST]++;

    setcc(cpu, ZPCI_PCI_LS_OK);
    return 0;
}

static uint32_t s390_pci_update_iotlb(S390PCIIOMMU *iommu,
                                      S390IOTLBEntry *entry)
{
    S390IOTLBEntry *cache = g_hash_table_lookup(iommu->iotlb, &entry->iova);
    IOMMUTLBEvent event = {
        .type = entry->perm ? IOMMU_NOTIFIER_MAP : IOMMU_NOTIFIER_UNMAP,
        .entry = {
            .target_as = &address_space_memory,
            .iova = entry->iova,
            .translated_addr = entry->translated_addr,
            .perm = entry->perm,
            .addr_mask = ~TARGET_PAGE_MASK,
        },
    };

    if (event.type == IOMMU_NOTIFIER_UNMAP) {
        if (!cache) {
            goto out;
        }
        g_hash_table_remove(iommu->iotlb, &entry->iova);
        inc_dma_avail(iommu);
        /* Don't notify the iommu yet, maybe we can bundle contiguous unmaps */
        goto out;
    } else {
        if (cache) {
            if (cache->perm == entry->perm &&
                cache->translated_addr == entry->translated_addr) {
                goto out;
            }

            event.type = IOMMU_NOTIFIER_UNMAP;
            event.entry.perm = IOMMU_NONE;
            memory_region_notify_iommu(&iommu->iommu_mr, 0, event);
            event.type = IOMMU_NOTIFIER_MAP;
            event.entry.perm = entry->perm;
        }

        cache = g_new(S390IOTLBEntry, 1);
        cache->iova = entry->iova;
        cache->translated_addr = entry->translated_addr;
        cache->len = TARGET_PAGE_SIZE;
        cache->perm = entry->perm;
        g_hash_table_replace(iommu->iotlb, &cache->iova, cache);
        dec_dma_avail(iommu);
    }

    /*
     * All associated iotlb entries have already been cleared, trigger the
     * unmaps.
     */
    memory_region_notify_iommu(&iommu->iommu_mr, 0, event);

out:
    return iommu->dma_limit ? iommu->dma_limit->avail : 1;
}

static void s390_pci_batch_unmap(S390PCIIOMMU *iommu, uint64_t iova,
                                 uint64_t len)
{
    uint64_t remain = len, start = iova, end = start + len - 1, mask, size;
    IOMMUTLBEvent event = {
        .type = IOMMU_NOTIFIER_UNMAP,
        .entry = {
            .target_as = &address_space_memory,
            .translated_addr = 0,
            .perm = IOMMU_NONE,
        },
    };

    while (remain >= TARGET_PAGE_SIZE) {
        mask = dma_aligned_pow2_mask(start, end, 64);
        size = mask + 1;
        event.entry.iova = start;
        event.entry.addr_mask = mask;
        memory_region_notify_iommu(&iommu->iommu_mr, 0, event);
        start += size;
        remain -= size;
    }
}

int rpcit_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2, uintptr_t ra)
{
    CPUS390XState *env = &cpu->env;
    uint64_t iova, coalesce = 0;
    uint32_t fh;
    uint16_t error = 0;
    S390PCIBusDevice *pbdev;
    S390PCIIOMMU *iommu;
    S390IOTLBEntry entry;
    hwaddr start, end, sstart;
    uint32_t dma_avail;
    bool again;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return 0;
    }

    if (r2 & 0x1) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return 0;
    }

    fh = env->regs[r1] >> 32;
    sstart = start = env->regs[r2];
    end = start + env->regs[r2 + 1];

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        trace_s390_pci_nodev("rpcit", fh);
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    }

    switch (pbdev->state) {
    case ZPCI_FS_RESERVED:
    case ZPCI_FS_STANDBY:
    case ZPCI_FS_DISABLED:
    case ZPCI_FS_PERMANENT_ERROR:
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    case ZPCI_FS_ERROR:
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r1, ZPCI_MOD_ST_ERROR_RECOVER);
        return 0;
    default:
        break;
    }

    iommu = pbdev->iommu;
    if (iommu->dma_limit) {
        dma_avail = iommu->dma_limit->avail;
    } else {
        dma_avail = 1;
    }
    if (!iommu->g_iota) {
        error = ERR_EVENT_INVALAS;
        goto err;
    }

    if (end < iommu->pba || start > iommu->pal) {
        error = ERR_EVENT_OORANGE;
        goto err;
    }

 retry:
    start = sstart;
    again = false;
    while (start < end) {
        error = s390_guest_io_table_walk(iommu->g_iota, start, &entry);
        if (error) {
            break;
        }

        /*
         * If this is an unmap of a PTE, let's try to coalesce multiple unmaps
         * into as few notifier events as possible.
         */
        if (entry.perm == IOMMU_NONE && entry.len == TARGET_PAGE_SIZE) {
            if (coalesce == 0) {
                iova = entry.iova;
            }
            coalesce += entry.len;
        } else if (coalesce > 0) {
            /* Unleash the coalesced unmap before processing a new map */
            s390_pci_batch_unmap(iommu, iova, coalesce);
            coalesce = 0;
        }

        start += entry.len;
        while (entry.iova < start && entry.iova < end) {
            if (dma_avail > 0 || entry.perm == IOMMU_NONE) {
                dma_avail = s390_pci_update_iotlb(iommu, &entry);
                entry.iova += TARGET_PAGE_SIZE;
                entry.translated_addr += TARGET_PAGE_SIZE;
            } else {
                /*
                 * We are unable to make a new mapping at this time, continue
                 * on and hopefully free up more space.  Then attempt another
                 * pass.
                 */
                again = true;
                break;
            }
        }
    }
    if (coalesce) {
            /* Unleash the coalesced unmap before finishing rpcit */
            s390_pci_batch_unmap(iommu, iova, coalesce);
            coalesce = 0;
    }
    if (again && dma_avail > 0)
        goto retry;
err:
    if (error) {
        pbdev->state = ZPCI_FS_ERROR;
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r1, ZPCI_PCI_ST_FUNC_IN_ERR);
        s390_pci_generate_error_event(error, pbdev->fh, pbdev->fid, start, 0);
    } else {
        pbdev->fmb.counter[ZPCI_FMB_CNT_RPCIT]++;
        if (dma_avail > 0) {
            setcc(cpu, ZPCI_PCI_LS_OK);
        } else {
            /* vfio DMA mappings are exhausted, trigger a RPCIT */
            setcc(cpu, ZPCI_PCI_LS_ERR);
            s390_set_status_code(env, r1, ZPCI_RPCIT_ST_INSUFF_RES);
        }
    }
    return 0;
}

int pcistb_service_call(S390CPU *cpu, uint8_t r1, uint8_t r3, uint64_t gaddr,
                        uint8_t ar, uintptr_t ra)
{
    CPUS390XState *env = &cpu->env;
    S390PCIBusDevice *pbdev;
    MemoryRegion *mr;
    MemTxResult result;
    uint64_t offset;
    int i;
    uint32_t fh;
    uint8_t pcias;
    uint16_t len;
    uint8_t buffer[128];

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return 0;
    }

    fh = env->regs[r1] >> 32;
    pcias = (env->regs[r1] >> 16) & 0xf;
    len = env->regs[r1] & 0x1fff;
    offset = env->regs[r3];

    if (!(fh & FH_MASK_ENABLE)) {
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    }

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        trace_s390_pci_nodev("pcistb", fh);
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    }

    switch (pbdev->state) {
    case ZPCI_FS_PERMANENT_ERROR:
    case ZPCI_FS_ERROR:
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r1, ZPCI_PCI_ST_BLOCKED);
        return 0;
    default:
        break;
    }

    if (pcias > ZPCI_IO_BAR_MAX) {
        trace_s390_pci_invalid("pcistb", fh);
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r1, ZPCI_PCI_ST_INVAL_AS);
        return 0;
    }

    /* Verify the address, offset and length */
    /* offset must be a multiple of 8 */
    if (offset % 8) {
        goto specification_error;
    }
    /* Length must be greater than 8, a multiple of 8 */
    /* and not greater than maxstbl */
    if ((len <= 8) || (len % 8) ||
        (len > pbdev->pci_group->zpci_group.maxstbl)) {
        goto specification_error;
    }
    /* Do not cross a 4K-byte boundary */
    if (((offset & 0xfff) + len) > 0x1000) {
        goto specification_error;
    }
    /* Guest address must be double word aligned */
    if (gaddr & 0x07UL) {
        goto specification_error;
    }

    mr = pbdev->pdev->io_regions[pcias].memory;
    mr = s390_get_subregion(mr, offset, len);
    offset -= mr->addr;

    for (i = 0; i < len; i += 8) {
        if (!memory_region_access_valid(mr, offset + i, 8, true,
                                        MEMTXATTRS_UNSPECIFIED)) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
            return 0;
        }
    }

    if (s390_cpu_virt_mem_read(cpu, gaddr, ar, buffer, len)) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return 0;
    }

    for (i = 0; i < len / 8; i++) {
        result = memory_region_dispatch_write(mr, offset + i * 8,
                                              ldq_be_p(buffer + i * 8),
                                              MO_64, MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            s390_program_interrupt(env, PGM_OPERAND, ra);
            return 0;
        }
    }

    pbdev->fmb.counter[ZPCI_FMB_CNT_STB]++;

    setcc(cpu, ZPCI_PCI_LS_OK);
    return 0;

specification_error:
    s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    return 0;
}

static int reg_irqs(CPUS390XState *env, S390PCIBusDevice *pbdev, ZpciFib fib)
{
    int ret, len;
    uint8_t isc = FIB_DATA_ISC(ldl_be_p(&fib.data));

    pbdev->routes.adapter.adapter_id = css_get_adapter_id(
                                       CSS_IO_ADAPTER_PCI, isc);
    pbdev->summary_ind = get_indicator(ldq_be_p(&fib.aisb), sizeof(uint64_t));
    len = BITS_TO_LONGS(FIB_DATA_NOI(ldl_be_p(&fib.data))) * sizeof(unsigned long);
    pbdev->indicator = get_indicator(ldq_be_p(&fib.aibv), len);

    ret = map_indicator(&pbdev->routes.adapter, pbdev->summary_ind);
    if (ret) {
        goto out;
    }

    ret = map_indicator(&pbdev->routes.adapter, pbdev->indicator);
    if (ret) {
        goto out;
    }

    pbdev->routes.adapter.summary_addr = ldq_be_p(&fib.aisb);
    pbdev->routes.adapter.summary_offset = FIB_DATA_AISBO(ldl_be_p(&fib.data));
    pbdev->routes.adapter.ind_addr = ldq_be_p(&fib.aibv);
    pbdev->routes.adapter.ind_offset = FIB_DATA_AIBVO(ldl_be_p(&fib.data));
    pbdev->isc = isc;
    pbdev->noi = FIB_DATA_NOI(ldl_be_p(&fib.data));
    pbdev->sum = FIB_DATA_SUM(ldl_be_p(&fib.data));

    trace_s390_pci_irqs("register", pbdev->routes.adapter.adapter_id);
    return 0;
out:
    release_indicator(&pbdev->routes.adapter, pbdev->summary_ind);
    release_indicator(&pbdev->routes.adapter, pbdev->indicator);
    pbdev->summary_ind = NULL;
    pbdev->indicator = NULL;
    return ret;
}

int pci_dereg_irqs(S390PCIBusDevice *pbdev)
{
    release_indicator(&pbdev->routes.adapter, pbdev->summary_ind);
    release_indicator(&pbdev->routes.adapter, pbdev->indicator);

    pbdev->summary_ind = NULL;
    pbdev->indicator = NULL;
    pbdev->routes.adapter.summary_addr = 0;
    pbdev->routes.adapter.summary_offset = 0;
    pbdev->routes.adapter.ind_addr = 0;
    pbdev->routes.adapter.ind_offset = 0;
    pbdev->isc = 0;
    pbdev->noi = 0;
    pbdev->sum = 0;

    trace_s390_pci_irqs("unregister", pbdev->routes.adapter.adapter_id);
    return 0;
}

static int reg_ioat(CPUS390XState *env, S390PCIBusDevice *pbdev, ZpciFib fib,
                    uintptr_t ra)
{
    S390PCIIOMMU *iommu = pbdev->iommu;
    uint64_t pba = ldq_be_p(&fib.pba);
    uint64_t pal = ldq_be_p(&fib.pal);
    uint64_t g_iota = ldq_be_p(&fib.iota);
    uint8_t dt = (g_iota >> 2) & 0x7;
    uint8_t t = (g_iota >> 11) & 0x1;

    pba &= ~0xfff;
    pal |= 0xfff;
    if (pba > pal || pba < pbdev->zpci_fn.sdma || pal > pbdev->zpci_fn.edma) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return -EINVAL;
    }

    /* currently we only support designation type 1 with translation */
    if (t && dt != ZPCI_IOTA_RTTO) {
        error_report("unsupported ioat dt %d t %d", dt, t);
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return -EINVAL;
    } else if (!t && !pbdev->rtr_avail) {
        error_report("relaxed translation not allowed");
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return -EINVAL;
    }

    iommu->pba = pba;
    iommu->pal = pal;
    iommu->g_iota = g_iota;

    if (t) {
        s390_pci_iommu_enable(iommu);
    } else {
        s390_pci_iommu_direct_map_enable(iommu);
    }

    return 0;
}

void pci_dereg_ioat(S390PCIIOMMU *iommu)
{
    s390_pci_iommu_disable(iommu);
    iommu->pba = 0;
    iommu->pal = 0;
    iommu->g_iota = 0;
}

void fmb_timer_free(S390PCIBusDevice *pbdev)
{
    if (pbdev->fmb_timer) {
        timer_free(pbdev->fmb_timer);
        pbdev->fmb_timer = NULL;
    }
    pbdev->fmb_addr = 0;
    memset(&pbdev->fmb, 0, sizeof(ZpciFmb));
}

static int fmb_do_update(S390PCIBusDevice *pbdev, int offset, uint64_t val,
                         int len)
{
    MemTxResult ret;
    uint64_t dst = pbdev->fmb_addr + offset;

    switch (len) {
    case 8:
        address_space_stq_be(&address_space_memory, dst, val,
                             MEMTXATTRS_UNSPECIFIED,
                             &ret);
        break;
    case 4:
        address_space_stl_be(&address_space_memory, dst, val,
                             MEMTXATTRS_UNSPECIFIED,
                             &ret);
        break;
    case 2:
        address_space_stw_be(&address_space_memory, dst, val,
                             MEMTXATTRS_UNSPECIFIED,
                             &ret);
        break;
    case 1:
        address_space_stb(&address_space_memory, dst, val,
                          MEMTXATTRS_UNSPECIFIED,
                          &ret);
        break;
    default:
        ret = MEMTX_ERROR;
        break;
    }
    if (ret != MEMTX_OK) {
        s390_pci_generate_error_event(ERR_EVENT_FMBA, pbdev->fh, pbdev->fid,
                                      pbdev->fmb_addr, 0);
        fmb_timer_free(pbdev);
    }

    return ret;
}

static void fmb_update(void *opaque)
{
    S390PCIBusDevice *pbdev = opaque;
    int64_t t = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    int i;

    /* Update U bit */
    pbdev->fmb.last_update *= 2;
    pbdev->fmb.last_update |= UPDATE_U_BIT;
    if (fmb_do_update(pbdev, offsetof(ZpciFmb, last_update),
                      pbdev->fmb.last_update,
                      sizeof(pbdev->fmb.last_update))) {
        return;
    }

    /* Update FMB sample count */
    if (fmb_do_update(pbdev, offsetof(ZpciFmb, sample),
                      pbdev->fmb.sample++,
                      sizeof(pbdev->fmb.sample))) {
        return;
    }

    /* Update FMB counters */
    for (i = 0; i < ZPCI_FMB_CNT_MAX; i++) {
        if (fmb_do_update(pbdev, offsetof(ZpciFmb, counter[i]),
                          pbdev->fmb.counter[i],
                          sizeof(pbdev->fmb.counter[0]))) {
            return;
        }
    }

    /* Clear U bit and update the time */
    pbdev->fmb.last_update = time2tod(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    pbdev->fmb.last_update *= 2;
    if (fmb_do_update(pbdev, offsetof(ZpciFmb, last_update),
                      pbdev->fmb.last_update,
                      sizeof(pbdev->fmb.last_update))) {
        return;
    }
    timer_mod(pbdev->fmb_timer, t + pbdev->pci_group->zpci_group.mui);
}

static int mpcifc_reg_int_interp(S390PCIBusDevice *pbdev, ZpciFib *fib)
{
    int rc;

    rc = s390_pci_kvm_aif_enable(pbdev, fib, pbdev->forwarding_assist);
    if (rc) {
        trace_s390_pci_kvm_aif("enable");
        return rc;
    }

    return 0;
}

static int mpcifc_dereg_int_interp(S390PCIBusDevice *pbdev, ZpciFib *fib)
{
    int rc;

    rc = s390_pci_kvm_aif_disable(pbdev);
    if (rc) {
        trace_s390_pci_kvm_aif("disable");
        return rc;
    }

    return 0;
}

int mpcifc_service_call(S390CPU *cpu, uint8_t r1, uint64_t fiba, uint8_t ar,
                        uintptr_t ra)
{
    CPUS390XState *env = &cpu->env;
    uint8_t oc, dmaas;
    uint32_t fh;
    ZpciFib fib;
    S390PCIBusDevice *pbdev;
    uint64_t cc = ZPCI_PCI_LS_OK;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return 0;
    }

    oc = env->regs[r1] & 0xff;
    dmaas = (env->regs[r1] >> 16) & 0xff;
    fh = env->regs[r1] >> 32;

    if (fiba & 0x7) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return 0;
    }

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        trace_s390_pci_nodev("mpcifc", fh);
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    }

    switch (pbdev->state) {
    case ZPCI_FS_RESERVED:
    case ZPCI_FS_STANDBY:
    case ZPCI_FS_DISABLED:
    case ZPCI_FS_PERMANENT_ERROR:
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    default:
        break;
    }

    if (s390_cpu_virt_mem_read(cpu, fiba, ar, (uint8_t *)&fib, sizeof(fib))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return 0;
    }

    if (fib.fmt != 0) {
        s390_program_interrupt(env, PGM_OPERAND, ra);
        return 0;
    }

    switch (oc) {
    case ZPCI_MOD_FC_REG_INT:
        if (pbdev->interp) {
            if (mpcifc_reg_int_interp(pbdev, &fib)) {
                cc = ZPCI_PCI_LS_ERR;
                s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
            }
        } else if (pbdev->summary_ind) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
        } else if (reg_irqs(env, pbdev, fib)) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_RES_NOT_AVAIL);
        }
        break;
    case ZPCI_MOD_FC_DEREG_INT:
        if (pbdev->interp) {
            if (mpcifc_dereg_int_interp(pbdev, &fib)) {
                cc = ZPCI_PCI_LS_ERR;
                s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
            }
        } else if (!pbdev->summary_ind) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
        } else {
            pci_dereg_irqs(pbdev);
        }
        break;
    case ZPCI_MOD_FC_REG_IOAT:
        if (dmaas != 0) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_DMAAS_INVAL);
        } else if (pbdev->iommu->enabled) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
        } else if (reg_ioat(env, pbdev, fib, ra)) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_INSUF_RES);
        }
        break;
    case ZPCI_MOD_FC_DEREG_IOAT:
        if (dmaas != 0) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_DMAAS_INVAL);
        } else if (!pbdev->iommu->enabled) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
        } else {
            pci_dereg_ioat(pbdev->iommu);
        }
        break;
    case ZPCI_MOD_FC_REREG_IOAT:
        if (dmaas != 0) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_DMAAS_INVAL);
        } else if (!pbdev->iommu->enabled) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
        } else {
            pci_dereg_ioat(pbdev->iommu);
            if (reg_ioat(env, pbdev, fib, ra)) {
                cc = ZPCI_PCI_LS_ERR;
                s390_set_status_code(env, r1, ZPCI_MOD_ST_INSUF_RES);
            }
        }
        break;
    case ZPCI_MOD_FC_RESET_ERROR:
        switch (pbdev->state) {
        case ZPCI_FS_BLOCKED:
        case ZPCI_FS_ERROR:
            pbdev->state = ZPCI_FS_ENABLED;
            break;
        default:
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
        }
        break;
    case ZPCI_MOD_FC_RESET_BLOCK:
        switch (pbdev->state) {
        case ZPCI_FS_ERROR:
            pbdev->state = ZPCI_FS_BLOCKED;
            break;
        default:
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
        }
        break;
    case ZPCI_MOD_FC_SET_MEASURE: {
        uint64_t fmb_addr = ldq_be_p(&fib.fmb_addr);

        if (fmb_addr & FMBK_MASK) {
            cc = ZPCI_PCI_LS_ERR;
            s390_pci_generate_error_event(ERR_EVENT_FMBPRO, pbdev->fh,
                                          pbdev->fid, fmb_addr, 0);
            fmb_timer_free(pbdev);
            break;
        }

        if (!fmb_addr) {
            /* Stop updating FMB. */
            fmb_timer_free(pbdev);
            break;
        }

        if (!pbdev->fmb_timer) {
            pbdev->fmb_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                            fmb_update, pbdev);
        } else if (timer_pending(pbdev->fmb_timer)) {
            /* Remove pending timer to update FMB address. */
            timer_del(pbdev->fmb_timer);
        }
        pbdev->fmb_addr = fmb_addr;
        timer_mod(pbdev->fmb_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                                    pbdev->pci_group->zpci_group.mui);
        break;
    }
    default:
        s390_program_interrupt(&cpu->env, PGM_OPERAND, ra);
        cc = ZPCI_PCI_LS_ERR;
    }

    setcc(cpu, cc);
    return 0;
}

int stpcifc_service_call(S390CPU *cpu, uint8_t r1, uint64_t fiba, uint8_t ar,
                         uintptr_t ra)
{
    CPUS390XState *env = &cpu->env;
    uint8_t dmaas;
    uint32_t fh;
    ZpciFib fib;
    S390PCIBusDevice *pbdev;
    uint32_t data;
    uint64_t cc = ZPCI_PCI_LS_OK;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        s390_program_interrupt(env, PGM_PRIVILEGED, ra);
        return 0;
    }

    fh = env->regs[r1] >> 32;
    dmaas = (env->regs[r1] >> 16) & 0xff;

    if (dmaas) {
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r1, ZPCI_STPCIFC_ST_INVAL_DMAAS);
        return 0;
    }

    if (fiba & 0x7) {
        s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return 0;
    }

    pbdev = s390_pci_find_dev_by_idx(s390_get_phb(), fh & FH_MASK_INDEX);
    if (!pbdev) {
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    }

    memset(&fib, 0, sizeof(fib));

    switch (pbdev->state) {
    case ZPCI_FS_RESERVED:
    case ZPCI_FS_STANDBY:
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        return 0;
    case ZPCI_FS_DISABLED:
        if (fh & FH_MASK_ENABLE) {
            setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
            return 0;
        }
        goto out;
    /* BLOCKED bit is set to one coincident with the setting of ERROR bit.
     * FH Enabled bit is set to one in states of ENABLED, BLOCKED or ERROR. */
    case ZPCI_FS_ERROR:
        fib.fc |= 0x20;
        /* fallthrough */
    case ZPCI_FS_BLOCKED:
        fib.fc |= 0x40;
        /* fallthrough */
    case ZPCI_FS_ENABLED:
        fib.fc |= 0x80;
        if (pbdev->iommu->enabled) {
            fib.fc |= 0x10;
        }
        if (!(fh & FH_MASK_ENABLE)) {
            env->regs[r1] |= 1ULL << 63;
        }
        break;
    case ZPCI_FS_PERMANENT_ERROR:
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r1, ZPCI_STPCIFC_ST_PERM_ERROR);
        return 0;
    }

    stq_be_p(&fib.pba, pbdev->iommu->pba);
    stq_be_p(&fib.pal, pbdev->iommu->pal);
    stq_be_p(&fib.iota, pbdev->iommu->g_iota);
    stq_be_p(&fib.aibv, pbdev->routes.adapter.ind_addr);
    stq_be_p(&fib.aisb, pbdev->routes.adapter.summary_addr);
    stq_be_p(&fib.fmb_addr, pbdev->fmb_addr);

    data = ((uint32_t)pbdev->isc << 28) | ((uint32_t)pbdev->noi << 16) |
           ((uint32_t)pbdev->routes.adapter.ind_offset << 8) |
           ((uint32_t)pbdev->sum << 7) | pbdev->routes.adapter.summary_offset;
    stl_be_p(&fib.data, data);

out:
    if (s390_cpu_virt_mem_write(cpu, fiba, ar, (uint8_t *)&fib, sizeof(fib))) {
        s390_cpu_virt_mem_handle_exc(cpu, ra);
        return 0;
    }

    setcc(cpu, cc);
    return 0;
}
