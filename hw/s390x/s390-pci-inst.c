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
#include "qemu-common.h"
#include "cpu.h"
#include "s390-pci-inst.h"
#include "s390-pci-bus.h"
#include "exec/memory-internal.h"
#include "qemu/error-report.h"
#include "sysemu/hw_accel.h"

/* #define DEBUG_S390PCI_INST */
#ifdef DEBUG_S390PCI_INST
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "s390pci-inst: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

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
    if (lduw_p(&rrb->request.hdr.len) != 32) {
        res_code = CLP_RC_LEN;
        rc = -EINVAL;
        goto out;
    }

    if ((ldl_p(&rrb->request.fmt) & CLP_MASK_FMT) != 0) {
        res_code = CLP_RC_FMT;
        rc = -EINVAL;
        goto out;
    }

    if ((ldl_p(&rrb->request.fmt) & ~CLP_MASK_FMT) != 0 ||
        ldq_p(&rrb->request.reserved1) != 0) {
        res_code = CLP_RC_RESNOT0;
        rc = -EINVAL;
        goto out;
    }

    resume_token = ldq_p(&rrb->request.resume_token);

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

    if (lduw_p(&rrb->response.hdr.len) < 48) {
        res_code = CLP_RC_8K;
        rc = -EINVAL;
        goto out;
    }

    initial_l2 = lduw_p(&rrb->response.hdr.len);
    if ((initial_l2 - LIST_PCI_HDR_LEN) % sizeof(ClpFhListEntry)
        != 0) {
        res_code = CLP_RC_LEN;
        rc = -EINVAL;
        *cc = 3;
        goto out;
    }

    stl_p(&rrb->response.fmt, 0);
    stq_p(&rrb->response.reserved1, 0);
    stl_p(&rrb->response.mdd, FH_MASK_SHM);
    stw_p(&rrb->response.max_fn, PCI_MAX_FUNCTIONS);
    rrb->response.flags = UID_CHECKING_ENABLED;
    rrb->response.entry_size = sizeof(ClpFhListEntry);

    i = 0;
    g_l2 = LIST_PCI_HDR_LEN;
    while (g_l2 < initial_l2 && pbdev) {
        stw_p(&rrb->response.fh_list[i].device_id,
            pci_get_word(pbdev->pdev->config + PCI_DEVICE_ID));
        stw_p(&rrb->response.fh_list[i].vendor_id,
            pci_get_word(pbdev->pdev->config + PCI_VENDOR_ID));
        /* Ignore RESERVED devices. */
        stl_p(&rrb->response.fh_list[i].config,
            pbdev->state == ZPCI_FS_STANDBY ? 0 : 1 << 31);
        stl_p(&rrb->response.fh_list[i].fid, pbdev->fid);
        stl_p(&rrb->response.fh_list[i].fh, pbdev->fh);

        g_l2 += sizeof(ClpFhListEntry);
        /* Add endian check for DPRINTF? */
        DPRINTF("g_l2 %d vendor id 0x%x device id 0x%x fid 0x%x fh 0x%x\n",
                g_l2,
                lduw_p(&rrb->response.fh_list[i].vendor_id),
                lduw_p(&rrb->response.fh_list[i].device_id),
                ldl_p(&rrb->response.fh_list[i].fid),
                ldl_p(&rrb->response.fh_list[i].fh));
        pbdev = s390_pci_find_next_avail_dev(s, pbdev);
        i++;
    }

    if (!pbdev) {
        resume_token = 0;
    } else {
        resume_token = pbdev->fh & FH_MASK_INDEX;
    }
    stq_p(&rrb->response.resume_token, resume_token);
    stw_p(&rrb->response.hdr.len, g_l2);
    stw_p(&rrb->response.hdr.rsp, CLP_RC_OK);
out:
    if (rc) {
        DPRINTF("list pci failed rc 0x%x\n", rc);
        stw_p(&rrb->response.hdr.rsp, res_code);
    }
    return rc;
}

int clp_service_call(S390CPU *cpu, uint8_t r2)
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

    cpu_synchronize_state(CPU(cpu));

    if (env->psw.mask & PSW_MASK_PSTATE) {
        program_interrupt(env, PGM_PRIVILEGED, 4);
        return 0;
    }

    if (s390_cpu_virt_mem_read(cpu, env->regs[r2], r2, buffer, sizeof(*reqh))) {
        return 0;
    }
    reqh = (ClpReqHdr *)buffer;
    req_len = lduw_p(&reqh->len);
    if (req_len < 16 || req_len > 8184 || (req_len % 8 != 0)) {
        program_interrupt(env, PGM_OPERAND, 4);
        return 0;
    }

    if (s390_cpu_virt_mem_read(cpu, env->regs[r2], r2, buffer,
                               req_len + sizeof(*resh))) {
        return 0;
    }
    resh = (ClpRspHdr *)(buffer + req_len);
    res_len = lduw_p(&resh->len);
    if (res_len < 8 || res_len > 8176 || (res_len % 8 != 0)) {
        program_interrupt(env, PGM_OPERAND, 4);
        return 0;
    }
    if ((req_len + res_len) > 8192) {
        program_interrupt(env, PGM_OPERAND, 4);
        return 0;
    }

    if (s390_cpu_virt_mem_read(cpu, env->regs[r2], r2, buffer,
                               req_len + res_len)) {
        return 0;
    }

    if (req_len != 32) {
        stw_p(&resh->rsp, CLP_RC_LEN);
        goto out;
    }

    switch (lduw_p(&reqh->cmd)) {
    case CLP_LIST_PCI: {
        ClpReqRspListPci *rrb = (ClpReqRspListPci *)buffer;
        list_pci(rrb, &cc);
        break;
    }
    case CLP_SET_PCI_FN: {
        ClpReqSetPci *reqsetpci = (ClpReqSetPci *)reqh;
        ClpRspSetPci *ressetpci = (ClpRspSetPci *)resh;

        pbdev = s390_pci_find_dev_by_fh(s, ldl_p(&reqsetpci->fh));
        if (!pbdev) {
                stw_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_FH);
                goto out;
        }

        switch (reqsetpci->oc) {
        case CLP_SET_ENABLE_PCI_FN:
            switch (reqsetpci->ndas) {
            case 0:
                stw_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_DMAAS);
                goto out;
            case 1:
                break;
            default:
                stw_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_RES);
                goto out;
            }

            if (pbdev->fh & FH_MASK_ENABLE) {
                stw_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_FHOP);
                goto out;
            }

            pbdev->fh |= FH_MASK_ENABLE;
            pbdev->state = ZPCI_FS_ENABLED;
            stl_p(&ressetpci->fh, pbdev->fh);
            stw_p(&ressetpci->hdr.rsp, CLP_RC_OK);
            break;
        case CLP_SET_DISABLE_PCI_FN:
            if (!(pbdev->fh & FH_MASK_ENABLE)) {
                stw_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_FHOP);
                goto out;
            }
            device_reset(DEVICE(pbdev));
            pbdev->fh &= ~FH_MASK_ENABLE;
            pbdev->state = ZPCI_FS_DISABLED;
            stl_p(&ressetpci->fh, pbdev->fh);
            stw_p(&ressetpci->hdr.rsp, CLP_RC_OK);
            break;
        default:
            DPRINTF("unknown set pci command\n");
            stw_p(&ressetpci->hdr.rsp, CLP_RC_SETPCIFN_FHOP);
            break;
        }
        break;
    }
    case CLP_QUERY_PCI_FN: {
        ClpReqQueryPci *reqquery = (ClpReqQueryPci *)reqh;
        ClpRspQueryPci *resquery = (ClpRspQueryPci *)resh;

        pbdev = s390_pci_find_dev_by_fh(s, ldl_p(&reqquery->fh));
        if (!pbdev) {
            DPRINTF("query pci no pci dev\n");
            stw_p(&resquery->hdr.rsp, CLP_RC_SETPCIFN_FH);
            goto out;
        }

        for (i = 0; i < PCI_BAR_COUNT; i++) {
            uint32_t data = pci_get_long(pbdev->pdev->config +
                PCI_BASE_ADDRESS_0 + (i * 4));

            stl_p(&resquery->bar[i], data);
            resquery->bar_size[i] = pbdev->pdev->io_regions[i].size ?
                                    ctz64(pbdev->pdev->io_regions[i].size) : 0;
            DPRINTF("bar %d addr 0x%x size 0x%" PRIx64 "barsize 0x%x\n", i,
                    ldl_p(&resquery->bar[i]),
                    pbdev->pdev->io_regions[i].size,
                    resquery->bar_size[i]);
        }

        stq_p(&resquery->sdma, ZPCI_SDMA_ADDR);
        stq_p(&resquery->edma, ZPCI_EDMA_ADDR);
        stl_p(&resquery->fid, pbdev->fid);
        stw_p(&resquery->pchid, 0);
        stw_p(&resquery->ug, 1);
        stl_p(&resquery->uid, pbdev->uid);
        stw_p(&resquery->hdr.rsp, CLP_RC_OK);
        break;
    }
    case CLP_QUERY_PCI_FNGRP: {
        ClpRspQueryPciGrp *resgrp = (ClpRspQueryPciGrp *)resh;
        resgrp->fr = 1;
        stq_p(&resgrp->dasm, 0);
        stq_p(&resgrp->msia, ZPCI_MSI_ADDR);
        stw_p(&resgrp->mui, 0);
        stw_p(&resgrp->i, 128);
        resgrp->version = 0;

        stw_p(&resgrp->hdr.rsp, CLP_RC_OK);
        break;
    }
    default:
        DPRINTF("unknown clp command\n");
        stw_p(&resh->rsp, CLP_RC_CMD);
        break;
    }

out:
    if (s390_cpu_virt_mem_write(cpu, env->regs[r2], r2, buffer,
                                req_len + res_len)) {
        return 0;
    }
    setcc(cpu, cc);
    return 0;
}

int pcilg_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2)
{
    CPUS390XState *env = &cpu->env;
    S390PCIBusDevice *pbdev;
    uint64_t offset;
    uint64_t data;
    MemoryRegion *mr;
    MemTxResult result;
    uint8_t len;
    uint32_t fh;
    uint8_t pcias;

    cpu_synchronize_state(CPU(cpu));

    if (env->psw.mask & PSW_MASK_PSTATE) {
        program_interrupt(env, PGM_PRIVILEGED, 4);
        return 0;
    }

    if (r2 & 0x1) {
        program_interrupt(env, PGM_SPECIFICATION, 4);
        return 0;
    }

    fh = env->regs[r2] >> 32;
    pcias = (env->regs[r2] >> 16) & 0xf;
    len = env->regs[r2] & 0xf;
    offset = env->regs[r2 + 1];

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        DPRINTF("pcilg no pci dev\n");
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
        s390_set_status_code(env, r2, ZPCI_PCI_ST_BLOCKED);
        return 0;
    default:
        break;
    }

    if (pcias < 6) {
        if ((8 - (offset & 0x7)) < len) {
            program_interrupt(env, PGM_OPERAND, 4);
            return 0;
        }
        mr = pbdev->pdev->io_regions[pcias].memory;
        result = memory_region_dispatch_read(mr, offset, &data, len,
                                             MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            program_interrupt(env, PGM_OPERAND, 4);
            return 0;
        }
    } else if (pcias == 15) {
        if ((4 - (offset & 0x3)) < len) {
            program_interrupt(env, PGM_OPERAND, 4);
            return 0;
        }
        data =  pci_host_config_read_common(
                   pbdev->pdev, offset, pci_config_size(pbdev->pdev), len);

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
            program_interrupt(env, PGM_OPERAND, 4);
            return 0;
        }
    } else {
        DPRINTF("invalid space\n");
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r2, ZPCI_PCI_ST_INVAL_AS);
        return 0;
    }

    env->regs[r1] = data;
    setcc(cpu, ZPCI_PCI_LS_OK);
    return 0;
}

static void update_msix_table_msg_data(S390PCIBusDevice *pbdev, uint64_t offset,
                                       uint64_t *data, uint8_t len)
{
    uint32_t val;
    uint8_t *msg_data;

    if (offset % PCI_MSIX_ENTRY_SIZE != 8) {
        return;
    }

    if (len != 4) {
        DPRINTF("access msix table msg data but len is %d\n", len);
        return;
    }

    msg_data = (uint8_t *)data - offset % PCI_MSIX_ENTRY_SIZE +
               PCI_MSIX_ENTRY_VECTOR_CTRL;
    val = pci_get_long(msg_data) |
        ((pbdev->fh & FH_MASK_INDEX) << ZPCI_MSI_VEC_BITS);
    pci_set_long(msg_data, val);
    DPRINTF("update msix msg_data to 0x%" PRIx64 "\n", *data);
}

static int trap_msix(S390PCIBusDevice *pbdev, uint64_t offset, uint8_t pcias)
{
    if (pbdev->msix.available && pbdev->msix.table_bar == pcias &&
        offset >= pbdev->msix.table_offset &&
        offset <= pbdev->msix.table_offset +
                  (pbdev->msix.entries - 1) * PCI_MSIX_ENTRY_SIZE) {
        return 1;
    } else {
        return 0;
    }
}

int pcistg_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2)
{
    CPUS390XState *env = &cpu->env;
    uint64_t offset, data;
    S390PCIBusDevice *pbdev;
    MemoryRegion *mr;
    MemTxResult result;
    uint8_t len;
    uint32_t fh;
    uint8_t pcias;

    cpu_synchronize_state(CPU(cpu));

    if (env->psw.mask & PSW_MASK_PSTATE) {
        program_interrupt(env, PGM_PRIVILEGED, 4);
        return 0;
    }

    if (r2 & 0x1) {
        program_interrupt(env, PGM_SPECIFICATION, 4);
        return 0;
    }

    fh = env->regs[r2] >> 32;
    pcias = (env->regs[r2] >> 16) & 0xf;
    len = env->regs[r2] & 0xf;
    offset = env->regs[r2 + 1];

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        DPRINTF("pcistg no pci dev\n");
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
        s390_set_status_code(env, r2, ZPCI_PCI_ST_BLOCKED);
        return 0;
    default:
        break;
    }

    data = env->regs[r1];
    if (pcias < 6) {
        if ((8 - (offset & 0x7)) < len) {
            program_interrupt(env, PGM_OPERAND, 4);
            return 0;
        }

        if (trap_msix(pbdev, offset, pcias)) {
            offset = offset - pbdev->msix.table_offset;
            mr = &pbdev->pdev->msix_table_mmio;
            update_msix_table_msg_data(pbdev, offset, &data, len);
        } else {
            mr = pbdev->pdev->io_regions[pcias].memory;
        }

        result = memory_region_dispatch_write(mr, offset, data, len,
                                     MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            program_interrupt(env, PGM_OPERAND, 4);
            return 0;
        }
    } else if (pcias == 15) {
        if ((4 - (offset & 0x3)) < len) {
            program_interrupt(env, PGM_OPERAND, 4);
            return 0;
        }
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
            program_interrupt(env, PGM_OPERAND, 4);
            return 0;
        }

        pci_host_config_write_common(pbdev->pdev, offset,
                                     pci_config_size(pbdev->pdev),
                                     data, len);
    } else {
        DPRINTF("pcistg invalid space\n");
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r2, ZPCI_PCI_ST_INVAL_AS);
        return 0;
    }

    setcc(cpu, ZPCI_PCI_LS_OK);
    return 0;
}

int rpcit_service_call(S390CPU *cpu, uint8_t r1, uint8_t r2)
{
    CPUS390XState *env = &cpu->env;
    uint32_t fh;
    S390PCIBusDevice *pbdev;
    S390PCIIOMMU *iommu;
    hwaddr start, end;
    IOMMUTLBEntry entry;
    MemoryRegion *mr;

    cpu_synchronize_state(CPU(cpu));

    if (env->psw.mask & PSW_MASK_PSTATE) {
        program_interrupt(env, PGM_PRIVILEGED, 4);
        goto out;
    }

    if (r2 & 0x1) {
        program_interrupt(env, PGM_SPECIFICATION, 4);
        goto out;
    }

    fh = env->regs[r1] >> 32;
    start = env->regs[r2];
    end = start + env->regs[r2 + 1];

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        DPRINTF("rpcit no pci dev\n");
        setcc(cpu, ZPCI_PCI_LS_INVAL_HANDLE);
        goto out;
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
    if (!iommu->g_iota) {
        pbdev->state = ZPCI_FS_ERROR;
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r1, ZPCI_PCI_ST_INSUF_RES);
        s390_pci_generate_error_event(ERR_EVENT_INVALAS, pbdev->fh, pbdev->fid,
                                      start, 0);
        goto out;
    }

    if (end < iommu->pba || start > iommu->pal) {
        pbdev->state = ZPCI_FS_ERROR;
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r1, ZPCI_PCI_ST_INSUF_RES);
        s390_pci_generate_error_event(ERR_EVENT_OORANGE, pbdev->fh, pbdev->fid,
                                      start, 0);
        goto out;
    }

    mr = &iommu->iommu_mr;
    while (start < end) {
        entry = mr->iommu_ops->translate(mr, start, 0);

        if (!entry.translated_addr) {
            pbdev->state = ZPCI_FS_ERROR;
            setcc(cpu, ZPCI_PCI_LS_ERR);
            s390_set_status_code(env, r1, ZPCI_PCI_ST_INSUF_RES);
            s390_pci_generate_error_event(ERR_EVENT_SERR, pbdev->fh, pbdev->fid,
                                          start, ERR_EVENT_Q_BIT);
            goto out;
        }

        memory_region_notify_iommu(mr, entry);
        start += entry.addr_mask + 1;
    }

    setcc(cpu, ZPCI_PCI_LS_OK);
out:
    return 0;
}

int pcistb_service_call(S390CPU *cpu, uint8_t r1, uint8_t r3, uint64_t gaddr,
                        uint8_t ar)
{
    CPUS390XState *env = &cpu->env;
    S390PCIBusDevice *pbdev;
    MemoryRegion *mr;
    MemTxResult result;
    int i;
    uint32_t fh;
    uint8_t pcias;
    uint8_t len;
    uint8_t buffer[128];

    if (env->psw.mask & PSW_MASK_PSTATE) {
        program_interrupt(env, PGM_PRIVILEGED, 6);
        return 0;
    }

    fh = env->regs[r1] >> 32;
    pcias = (env->regs[r1] >> 16) & 0xf;
    len = env->regs[r1] & 0xff;

    if (pcias > 5) {
        DPRINTF("pcistb invalid space\n");
        setcc(cpu, ZPCI_PCI_LS_ERR);
        s390_set_status_code(env, r1, ZPCI_PCI_ST_INVAL_AS);
        return 0;
    }

    switch (len) {
    case 16:
    case 32:
    case 64:
    case 128:
        break;
    default:
        program_interrupt(env, PGM_SPECIFICATION, 6);
        return 0;
    }

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        DPRINTF("pcistb no pci dev fh 0x%x\n", fh);
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
        s390_set_status_code(env, r1, ZPCI_PCI_ST_BLOCKED);
        return 0;
    default:
        break;
    }

    mr = pbdev->pdev->io_regions[pcias].memory;
    if (!memory_region_access_valid(mr, env->regs[r3], len, true)) {
        program_interrupt(env, PGM_OPERAND, 6);
        return 0;
    }

    if (s390_cpu_virt_mem_read(cpu, gaddr, ar, buffer, len)) {
        return 0;
    }

    for (i = 0; i < len / 8; i++) {
        result = memory_region_dispatch_write(mr, env->regs[r3] + i * 8,
                                     ldq_p(buffer + i * 8), 8,
                                     MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            program_interrupt(env, PGM_OPERAND, 6);
            return 0;
        }
    }

    setcc(cpu, ZPCI_PCI_LS_OK);
    return 0;
}

static int reg_irqs(CPUS390XState *env, S390PCIBusDevice *pbdev, ZpciFib fib)
{
    int ret, len;

    ret = css_register_io_adapter(S390_PCIPT_ADAPTER,
                                  FIB_DATA_ISC(ldl_p(&fib.data)), true, false,
                                  &pbdev->routes.adapter.adapter_id);
    assert(ret == 0);

    pbdev->summary_ind = get_indicator(ldq_p(&fib.aisb), sizeof(uint64_t));
    len = BITS_TO_LONGS(FIB_DATA_NOI(ldl_p(&fib.data))) * sizeof(unsigned long);
    pbdev->indicator = get_indicator(ldq_p(&fib.aibv), len);

    ret = map_indicator(&pbdev->routes.adapter, pbdev->summary_ind);
    if (ret) {
        goto out;
    }

    ret = map_indicator(&pbdev->routes.adapter, pbdev->indicator);
    if (ret) {
        goto out;
    }

    pbdev->routes.adapter.summary_addr = ldq_p(&fib.aisb);
    pbdev->routes.adapter.summary_offset = FIB_DATA_AISBO(ldl_p(&fib.data));
    pbdev->routes.adapter.ind_addr = ldq_p(&fib.aibv);
    pbdev->routes.adapter.ind_offset = FIB_DATA_AIBVO(ldl_p(&fib.data));
    pbdev->isc = FIB_DATA_ISC(ldl_p(&fib.data));
    pbdev->noi = FIB_DATA_NOI(ldl_p(&fib.data));
    pbdev->sum = FIB_DATA_SUM(ldl_p(&fib.data));

    DPRINTF("reg_irqs adapter id %d\n", pbdev->routes.adapter.adapter_id);
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

    DPRINTF("dereg_irqs adapter id %d\n", pbdev->routes.adapter.adapter_id);
    return 0;
}

static int reg_ioat(CPUS390XState *env, S390PCIIOMMU *iommu, ZpciFib fib)
{
    uint64_t pba = ldq_p(&fib.pba);
    uint64_t pal = ldq_p(&fib.pal);
    uint64_t g_iota = ldq_p(&fib.iota);
    uint8_t dt = (g_iota >> 2) & 0x7;
    uint8_t t = (g_iota >> 11) & 0x1;

    if (pba > pal || pba < ZPCI_SDMA_ADDR || pal > ZPCI_EDMA_ADDR) {
        program_interrupt(env, PGM_OPERAND, 6);
        return -EINVAL;
    }

    /* currently we only support designation type 1 with translation */
    if (!(dt == ZPCI_IOTA_RTTO && t)) {
        error_report("unsupported ioat dt %d t %d", dt, t);
        program_interrupt(env, PGM_OPERAND, 6);
        return -EINVAL;
    }

    iommu->pba = pba;
    iommu->pal = pal;
    iommu->g_iota = g_iota;

    s390_pci_iommu_enable(iommu);

    return 0;
}

void pci_dereg_ioat(S390PCIIOMMU *iommu)
{
    s390_pci_iommu_disable(iommu);
    iommu->pba = 0;
    iommu->pal = 0;
    iommu->g_iota = 0;
}

int mpcifc_service_call(S390CPU *cpu, uint8_t r1, uint64_t fiba, uint8_t ar)
{
    CPUS390XState *env = &cpu->env;
    uint8_t oc, dmaas;
    uint32_t fh;
    ZpciFib fib;
    S390PCIBusDevice *pbdev;
    uint64_t cc = ZPCI_PCI_LS_OK;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        program_interrupt(env, PGM_PRIVILEGED, 6);
        return 0;
    }

    oc = env->regs[r1] & 0xff;
    dmaas = (env->regs[r1] >> 16) & 0xff;
    fh = env->regs[r1] >> 32;

    if (fiba & 0x7) {
        program_interrupt(env, PGM_SPECIFICATION, 6);
        return 0;
    }

    pbdev = s390_pci_find_dev_by_fh(s390_get_phb(), fh);
    if (!pbdev) {
        DPRINTF("mpcifc no pci dev fh 0x%x\n", fh);
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
        return 0;
    }

    if (fib.fmt != 0) {
        program_interrupt(env, PGM_OPERAND, 6);
        return 0;
    }

    switch (oc) {
    case ZPCI_MOD_FC_REG_INT:
        if (pbdev->summary_ind) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_SEQUENCE);
        } else if (reg_irqs(env, pbdev, fib)) {
            cc = ZPCI_PCI_LS_ERR;
            s390_set_status_code(env, r1, ZPCI_MOD_ST_RES_NOT_AVAIL);
        }
        break;
    case ZPCI_MOD_FC_DEREG_INT:
        if (!pbdev->summary_ind) {
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
        } else if (reg_ioat(env, pbdev->iommu, fib)) {
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
            if (reg_ioat(env, pbdev->iommu, fib)) {
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
    case ZPCI_MOD_FC_SET_MEASURE:
        pbdev->fmb_addr = ldq_p(&fib.fmb_addr);
        break;
    default:
        program_interrupt(&cpu->env, PGM_OPERAND, 6);
        cc = ZPCI_PCI_LS_ERR;
    }

    setcc(cpu, cc);
    return 0;
}

int stpcifc_service_call(S390CPU *cpu, uint8_t r1, uint64_t fiba, uint8_t ar)
{
    CPUS390XState *env = &cpu->env;
    uint8_t dmaas;
    uint32_t fh;
    ZpciFib fib;
    S390PCIBusDevice *pbdev;
    uint32_t data;
    uint64_t cc = ZPCI_PCI_LS_OK;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        program_interrupt(env, PGM_PRIVILEGED, 6);
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
        program_interrupt(env, PGM_SPECIFICATION, 6);
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
    case ZPCI_FS_BLOCKED:
        fib.fc |= 0x40;
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

    stq_p(&fib.pba, pbdev->iommu->pba);
    stq_p(&fib.pal, pbdev->iommu->pal);
    stq_p(&fib.iota, pbdev->iommu->g_iota);
    stq_p(&fib.aibv, pbdev->routes.adapter.ind_addr);
    stq_p(&fib.aisb, pbdev->routes.adapter.summary_addr);
    stq_p(&fib.fmb_addr, pbdev->fmb_addr);

    data = ((uint32_t)pbdev->isc << 28) | ((uint32_t)pbdev->noi << 16) |
           ((uint32_t)pbdev->routes.adapter.ind_offset << 8) |
           ((uint32_t)pbdev->sum << 7) | pbdev->routes.adapter.summary_offset;
    stl_p(&fib.data, data);

out:
    if (s390_cpu_virt_mem_write(cpu, fiba, ar, (uint8_t *)&fib, sizeof(fib))) {
        return 0;
    }

    setcc(cpu, cc);
    return 0;
}
