/*
 * QEMU sPAPR PCI host originated from Uninorth PCI host
 *
 * Copyright (c) 2011 Alexey Kardashevskiy, IBM Corporation.
 * Copyright (C) 2011 David Gibson, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_host.h"
#include "hw/ppc/spapr.h"
#include "hw/pci-host/spapr.h"
#include "exec/address-spaces.h"
#include "exec/ram_addr.h"
#include <libfdt.h>
#include "trace.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/qmp/qerror.h"
#include "hw/ppc/fdt.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_ids.h"
#include "hw/ppc/spapr_drc.h"
#include "sysemu/device_tree.h"
#include "sysemu/kvm.h"
#include "sysemu/hostmem.h"
#include "sysemu/numa.h"

/* Copied from the kernel arch/powerpc/platforms/pseries/msi.c */
#define RTAS_QUERY_FN           0
#define RTAS_CHANGE_FN          1
#define RTAS_RESET_FN           2
#define RTAS_CHANGE_MSI_FN      3
#define RTAS_CHANGE_MSIX_FN     4

/* Interrupt types to return on RTAS_CHANGE_* */
#define RTAS_TYPE_MSI           1
#define RTAS_TYPE_MSIX          2

SpaprPhbState *spapr_pci_find_phb(SpaprMachineState *spapr, uint64_t buid)
{
    SpaprPhbState *sphb;

    QLIST_FOREACH(sphb, &spapr->phbs, list) {
        if (sphb->buid != buid) {
            continue;
        }
        return sphb;
    }

    return NULL;
}

PCIDevice *spapr_pci_find_dev(SpaprMachineState *spapr, uint64_t buid,
                              uint32_t config_addr)
{
    SpaprPhbState *sphb = spapr_pci_find_phb(spapr, buid);
    PCIHostState *phb = PCI_HOST_BRIDGE(sphb);
    int bus_num = (config_addr >> 16) & 0xFF;
    int devfn = (config_addr >> 8) & 0xFF;

    if (!phb) {
        return NULL;
    }

    return pci_find_device(phb->bus, bus_num, devfn);
}

static uint32_t rtas_pci_cfgaddr(uint32_t arg)
{
    /* This handles the encoding of extended config space addresses */
    return ((arg >> 20) & 0xf00) | (arg & 0xff);
}

static void finish_read_pci_config(SpaprMachineState *spapr, uint64_t buid,
                                   uint32_t addr, uint32_t size,
                                   target_ulong rets)
{
    PCIDevice *pci_dev;
    uint32_t val;

    if ((size != 1) && (size != 2) && (size != 4)) {
        /* access must be 1, 2 or 4 bytes */
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    pci_dev = spapr_pci_find_dev(spapr, buid, addr);
    addr = rtas_pci_cfgaddr(addr);

    if (!pci_dev || (addr % size) || (addr >= pci_config_size(pci_dev))) {
        /* Access must be to a valid device, within bounds and
         * naturally aligned */
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    val = pci_host_config_read_common(pci_dev, addr,
                                      pci_config_size(pci_dev), size);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    rtas_st(rets, 1, val);
}

static void rtas_ibm_read_pci_config(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                     uint32_t token, uint32_t nargs,
                                     target_ulong args,
                                     uint32_t nret, target_ulong rets)
{
    uint64_t buid;
    uint32_t size, addr;

    if ((nargs != 4) || (nret != 2)) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    buid = rtas_ldq(args, 1);
    size = rtas_ld(args, 3);
    addr = rtas_ld(args, 0);

    finish_read_pci_config(spapr, buid, addr, size, rets);
}

static void rtas_read_pci_config(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                 uint32_t token, uint32_t nargs,
                                 target_ulong args,
                                 uint32_t nret, target_ulong rets)
{
    uint32_t size, addr;

    if ((nargs != 2) || (nret != 2)) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    size = rtas_ld(args, 1);
    addr = rtas_ld(args, 0);

    finish_read_pci_config(spapr, 0, addr, size, rets);
}

static void finish_write_pci_config(SpaprMachineState *spapr, uint64_t buid,
                                    uint32_t addr, uint32_t size,
                                    uint32_t val, target_ulong rets)
{
    PCIDevice *pci_dev;

    if ((size != 1) && (size != 2) && (size != 4)) {
        /* access must be 1, 2 or 4 bytes */
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    pci_dev = spapr_pci_find_dev(spapr, buid, addr);
    addr = rtas_pci_cfgaddr(addr);

    if (!pci_dev || (addr % size) || (addr >= pci_config_size(pci_dev))) {
        /* Access must be to a valid device, within bounds and
         * naturally aligned */
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    pci_host_config_write_common(pci_dev, addr, pci_config_size(pci_dev),
                                 val, size);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_ibm_write_pci_config(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                      uint32_t token, uint32_t nargs,
                                      target_ulong args,
                                      uint32_t nret, target_ulong rets)
{
    uint64_t buid;
    uint32_t val, size, addr;

    if ((nargs != 5) || (nret != 1)) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    buid = rtas_ldq(args, 1);
    val = rtas_ld(args, 4);
    size = rtas_ld(args, 3);
    addr = rtas_ld(args, 0);

    finish_write_pci_config(spapr, buid, addr, size, val, rets);
}

static void rtas_write_pci_config(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args,
                                  uint32_t nret, target_ulong rets)
{
    uint32_t val, size, addr;

    if ((nargs != 3) || (nret != 1)) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }


    val = rtas_ld(args, 2);
    size = rtas_ld(args, 1);
    addr = rtas_ld(args, 0);

    finish_write_pci_config(spapr, 0, addr, size, val, rets);
}

/*
 * Set MSI/MSIX message data.
 * This is required for msi_notify()/msix_notify() which
 * will write at the addresses via spapr_msi_write().
 *
 * If hwaddr == 0, all entries will have .data == first_irq i.e.
 * table will be reset.
 */
static void spapr_msi_setmsg(PCIDevice *pdev, hwaddr addr, bool msix,
                             unsigned first_irq, unsigned req_num)
{
    unsigned i;
    MSIMessage msg = { .address = addr, .data = first_irq };

    if (!msix) {
        msi_set_message(pdev, msg);
        trace_spapr_pci_msi_setup(pdev->name, 0, msg.address);
        return;
    }

    for (i = 0; i < req_num; ++i) {
        msix_set_message(pdev, i, msg);
        trace_spapr_pci_msi_setup(pdev->name, i, msg.address);
        if (addr) {
            ++msg.data;
        }
    }
}

static void rtas_ibm_change_msi(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                uint32_t token, uint32_t nargs,
                                target_ulong args, uint32_t nret,
                                target_ulong rets)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    uint32_t config_addr = rtas_ld(args, 0);
    uint64_t buid = rtas_ldq(args, 1);
    unsigned int func = rtas_ld(args, 3);
    unsigned int req_num = rtas_ld(args, 4); /* 0 == remove all */
    unsigned int seq_num = rtas_ld(args, 5);
    unsigned int ret_intr_type;
    unsigned int irq, max_irqs = 0;
    SpaprPhbState *phb = NULL;
    PCIDevice *pdev = NULL;
    spapr_pci_msi *msi;
    int *config_addr_key;
    Error *err = NULL;
    int i;

    /* Fins SpaprPhbState */
    phb = spapr_pci_find_phb(spapr, buid);
    if (phb) {
        pdev = spapr_pci_find_dev(spapr, buid, config_addr);
    }
    if (!phb || !pdev) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    switch (func) {
    case RTAS_CHANGE_FN:
        if (msi_present(pdev)) {
            ret_intr_type = RTAS_TYPE_MSI;
        } else if (msix_present(pdev)) {
            ret_intr_type = RTAS_TYPE_MSIX;
        } else {
            rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
            return;
        }
        break;
    case RTAS_CHANGE_MSI_FN:
        if (msi_present(pdev)) {
            ret_intr_type = RTAS_TYPE_MSI;
        } else {
            rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
            return;
        }
        break;
    case RTAS_CHANGE_MSIX_FN:
        if (msix_present(pdev)) {
            ret_intr_type = RTAS_TYPE_MSIX;
        } else {
            rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
            return;
        }
        break;
    default:
        error_report("rtas_ibm_change_msi(%u) is not implemented", func);
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    msi = (spapr_pci_msi *) g_hash_table_lookup(phb->msi, &config_addr);

    /* Releasing MSIs */
    if (!req_num) {
        if (!msi) {
            trace_spapr_pci_msi("Releasing wrong config", config_addr);
            rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
            return;
        }

        if (!smc->legacy_irq_allocation) {
            spapr_irq_msi_free(spapr, msi->first_irq, msi->num);
        }
        spapr_irq_free(spapr, msi->first_irq, msi->num);
        if (msi_present(pdev)) {
            spapr_msi_setmsg(pdev, 0, false, 0, 0);
        }
        if (msix_present(pdev)) {
            spapr_msi_setmsg(pdev, 0, true, 0, 0);
        }
        g_hash_table_remove(phb->msi, &config_addr);

        trace_spapr_pci_msi("Released MSIs", config_addr);
        rtas_st(rets, 0, RTAS_OUT_SUCCESS);
        rtas_st(rets, 1, 0);
        return;
    }

    /* Enabling MSI */

    /* Check if the device supports as many IRQs as requested */
    if (ret_intr_type == RTAS_TYPE_MSI) {
        max_irqs = msi_nr_vectors_allocated(pdev);
    } else if (ret_intr_type == RTAS_TYPE_MSIX) {
        max_irqs = pdev->msix_entries_nr;
    }
    if (!max_irqs) {
        error_report("Requested interrupt type %d is not enabled for device %x",
                     ret_intr_type, config_addr);
        rtas_st(rets, 0, -1); /* Hardware error */
        return;
    }
    /* Correct the number if the guest asked for too many */
    if (req_num > max_irqs) {
        trace_spapr_pci_msi_retry(config_addr, req_num, max_irqs);
        req_num = max_irqs;
        irq = 0; /* to avoid misleading trace */
        goto out;
    }

    /* Allocate MSIs */
    if (smc->legacy_irq_allocation) {
        irq = spapr_irq_find(spapr, req_num, ret_intr_type == RTAS_TYPE_MSI,
                             &err);
    } else {
        irq = spapr_irq_msi_alloc(spapr, req_num,
                                  ret_intr_type == RTAS_TYPE_MSI, &err);
    }
    if (err) {
        error_reportf_err(err, "Can't allocate MSIs for device %x: ",
                          config_addr);
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    for (i = 0; i < req_num; i++) {
        spapr_irq_claim(spapr, irq + i, false, &err);
        if (err) {
            if (i) {
                spapr_irq_free(spapr, irq, i);
            }
            if (!smc->legacy_irq_allocation) {
                spapr_irq_msi_free(spapr, irq, req_num);
            }
            error_reportf_err(err, "Can't allocate MSIs for device %x: ",
                              config_addr);
            rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
            return;
        }
    }

    /* Release previous MSIs */
    if (msi) {
        if (!smc->legacy_irq_allocation) {
            spapr_irq_msi_free(spapr, msi->first_irq, msi->num);
        }
        spapr_irq_free(spapr, msi->first_irq, msi->num);
        g_hash_table_remove(phb->msi, &config_addr);
    }

    /* Setup MSI/MSIX vectors in the device (via cfgspace or MSIX BAR) */
    spapr_msi_setmsg(pdev, SPAPR_PCI_MSI_WINDOW, ret_intr_type == RTAS_TYPE_MSIX,
                     irq, req_num);

    /* Add MSI device to cache */
    msi = g_new(spapr_pci_msi, 1);
    msi->first_irq = irq;
    msi->num = req_num;
    config_addr_key = g_new(int, 1);
    *config_addr_key = config_addr;
    g_hash_table_insert(phb->msi, config_addr_key, msi);

out:
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    rtas_st(rets, 1, req_num);
    rtas_st(rets, 2, ++seq_num);
    if (nret > 3) {
        rtas_st(rets, 3, ret_intr_type);
    }

    trace_spapr_pci_rtas_ibm_change_msi(config_addr, func, req_num, irq);
}

static void rtas_ibm_query_interrupt_source_number(PowerPCCPU *cpu,
                                                   SpaprMachineState *spapr,
                                                   uint32_t token,
                                                   uint32_t nargs,
                                                   target_ulong args,
                                                   uint32_t nret,
                                                   target_ulong rets)
{
    uint32_t config_addr = rtas_ld(args, 0);
    uint64_t buid = rtas_ldq(args, 1);
    unsigned int intr_src_num = -1, ioa_intr_num = rtas_ld(args, 3);
    SpaprPhbState *phb = NULL;
    PCIDevice *pdev = NULL;
    spapr_pci_msi *msi;

    /* Find SpaprPhbState */
    phb = spapr_pci_find_phb(spapr, buid);
    if (phb) {
        pdev = spapr_pci_find_dev(spapr, buid, config_addr);
    }
    if (!phb || !pdev) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    /* Find device descriptor and start IRQ */
    msi = (spapr_pci_msi *) g_hash_table_lookup(phb->msi, &config_addr);
    if (!msi || !msi->first_irq || !msi->num || (ioa_intr_num >= msi->num)) {
        trace_spapr_pci_msi("Failed to return vector", config_addr);
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }
    intr_src_num = msi->first_irq + ioa_intr_num;
    trace_spapr_pci_rtas_ibm_query_interrupt_source_number(ioa_intr_num,
                                                           intr_src_num);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    rtas_st(rets, 1, intr_src_num);
    rtas_st(rets, 2, 1);/* 0 == level; 1 == edge */
}

static void rtas_ibm_set_eeh_option(PowerPCCPU *cpu,
                                    SpaprMachineState *spapr,
                                    uint32_t token, uint32_t nargs,
                                    target_ulong args, uint32_t nret,
                                    target_ulong rets)
{
    SpaprPhbState *sphb;
    uint32_t addr, option;
    uint64_t buid;
    int ret;

    if ((nargs != 4) || (nret != 1)) {
        goto param_error_exit;
    }

    buid = rtas_ldq(args, 1);
    addr = rtas_ld(args, 0);
    option = rtas_ld(args, 3);

    sphb = spapr_pci_find_phb(spapr, buid);
    if (!sphb) {
        goto param_error_exit;
    }

    if (!spapr_phb_eeh_available(sphb)) {
        goto param_error_exit;
    }

    ret = spapr_phb_vfio_eeh_set_option(sphb, addr, option);
    rtas_st(rets, 0, ret);
    return;

param_error_exit:
    rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
}

static void rtas_ibm_get_config_addr_info2(PowerPCCPU *cpu,
                                           SpaprMachineState *spapr,
                                           uint32_t token, uint32_t nargs,
                                           target_ulong args, uint32_t nret,
                                           target_ulong rets)
{
    SpaprPhbState *sphb;
    PCIDevice *pdev;
    uint32_t addr, option;
    uint64_t buid;

    if ((nargs != 4) || (nret != 2)) {
        goto param_error_exit;
    }

    buid = rtas_ldq(args, 1);
    sphb = spapr_pci_find_phb(spapr, buid);
    if (!sphb) {
        goto param_error_exit;
    }

    if (!spapr_phb_eeh_available(sphb)) {
        goto param_error_exit;
    }

    /*
     * We always have PE address of form "00BB0001". "BB"
     * represents the bus number of PE's primary bus.
     */
    option = rtas_ld(args, 3);
    switch (option) {
    case RTAS_GET_PE_ADDR:
        addr = rtas_ld(args, 0);
        pdev = spapr_pci_find_dev(spapr, buid, addr);
        if (!pdev) {
            goto param_error_exit;
        }

        rtas_st(rets, 1, (pci_bus_num(pci_get_bus(pdev)) << 16) + 1);
        break;
    case RTAS_GET_PE_MODE:
        rtas_st(rets, 1, RTAS_PE_MODE_SHARED);
        break;
    default:
        goto param_error_exit;
    }

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    return;

param_error_exit:
    rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
}

static void rtas_ibm_read_slot_reset_state2(PowerPCCPU *cpu,
                                            SpaprMachineState *spapr,
                                            uint32_t token, uint32_t nargs,
                                            target_ulong args, uint32_t nret,
                                            target_ulong rets)
{
    SpaprPhbState *sphb;
    uint64_t buid;
    int state, ret;

    if ((nargs != 3) || (nret != 4 && nret != 5)) {
        goto param_error_exit;
    }

    buid = rtas_ldq(args, 1);
    sphb = spapr_pci_find_phb(spapr, buid);
    if (!sphb) {
        goto param_error_exit;
    }

    if (!spapr_phb_eeh_available(sphb)) {
        goto param_error_exit;
    }

    ret = spapr_phb_vfio_eeh_get_state(sphb, &state);
    rtas_st(rets, 0, ret);
    if (ret != RTAS_OUT_SUCCESS) {
        return;
    }

    rtas_st(rets, 1, state);
    rtas_st(rets, 2, RTAS_EEH_SUPPORT);
    rtas_st(rets, 3, RTAS_EEH_PE_UNAVAIL_INFO);
    if (nret >= 5) {
        rtas_st(rets, 4, RTAS_EEH_PE_RECOVER_INFO);
    }
    return;

param_error_exit:
    rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
}

static void rtas_ibm_set_slot_reset(PowerPCCPU *cpu,
                                    SpaprMachineState *spapr,
                                    uint32_t token, uint32_t nargs,
                                    target_ulong args, uint32_t nret,
                                    target_ulong rets)
{
    SpaprPhbState *sphb;
    uint32_t option;
    uint64_t buid;
    int ret;

    if ((nargs != 4) || (nret != 1)) {
        goto param_error_exit;
    }

    buid = rtas_ldq(args, 1);
    option = rtas_ld(args, 3);
    sphb = spapr_pci_find_phb(spapr, buid);
    if (!sphb) {
        goto param_error_exit;
    }

    if (!spapr_phb_eeh_available(sphb)) {
        goto param_error_exit;
    }

    ret = spapr_phb_vfio_eeh_reset(sphb, option);
    rtas_st(rets, 0, ret);
    return;

param_error_exit:
    rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
}

static void rtas_ibm_configure_pe(PowerPCCPU *cpu,
                                  SpaprMachineState *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args, uint32_t nret,
                                  target_ulong rets)
{
    SpaprPhbState *sphb;
    uint64_t buid;
    int ret;

    if ((nargs != 3) || (nret != 1)) {
        goto param_error_exit;
    }

    buid = rtas_ldq(args, 1);
    sphb = spapr_pci_find_phb(spapr, buid);
    if (!sphb) {
        goto param_error_exit;
    }

    if (!spapr_phb_eeh_available(sphb)) {
        goto param_error_exit;
    }

    ret = spapr_phb_vfio_eeh_configure(sphb);
    rtas_st(rets, 0, ret);
    return;

param_error_exit:
    rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
}

/* To support it later */
static void rtas_ibm_slot_error_detail(PowerPCCPU *cpu,
                                       SpaprMachineState *spapr,
                                       uint32_t token, uint32_t nargs,
                                       target_ulong args, uint32_t nret,
                                       target_ulong rets)
{
    SpaprPhbState *sphb;
    int option;
    uint64_t buid;

    if ((nargs != 8) || (nret != 1)) {
        goto param_error_exit;
    }

    buid = rtas_ldq(args, 1);
    sphb = spapr_pci_find_phb(spapr, buid);
    if (!sphb) {
        goto param_error_exit;
    }

    if (!spapr_phb_eeh_available(sphb)) {
        goto param_error_exit;
    }

    option = rtas_ld(args, 7);
    switch (option) {
    case RTAS_SLOT_TEMP_ERR_LOG:
    case RTAS_SLOT_PERM_ERR_LOG:
        break;
    default:
        goto param_error_exit;
    }

    /* We don't have error log yet */
    rtas_st(rets, 0, RTAS_OUT_NO_ERRORS_FOUND);
    return;

param_error_exit:
    rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
}

static void pci_spapr_set_irq(void *opaque, int irq_num, int level)
{
    /*
     * Here we use the number returned by pci_swizzle_map_irq_fn to find a
     * corresponding qemu_irq.
     */
    SpaprPhbState *phb = opaque;

    trace_spapr_pci_lsi_set(phb->dtbusname, irq_num, phb->lsi_table[irq_num].irq);
    qemu_set_irq(spapr_phb_lsi_qirq(phb, irq_num), level);
}

static PCIINTxRoute spapr_route_intx_pin_to_irq(void *opaque, int pin)
{
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(opaque);
    PCIINTxRoute route;

    route.mode = PCI_INTX_ENABLED;
    route.irq = sphb->lsi_table[pin].irq;

    return route;
}

/*
 * MSI/MSIX memory region implementation.
 * The handler handles both MSI and MSIX.
 * The vector number is encoded in least bits in data.
 */
static void spapr_msi_write(void *opaque, hwaddr addr,
                            uint64_t data, unsigned size)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    uint32_t irq = data;

    trace_spapr_pci_msi_write(addr, data, irq);

    qemu_irq_pulse(spapr_qirq(spapr, irq));
}

static const MemoryRegionOps spapr_msi_ops = {
    /* There is no .read as the read result is undefined by PCI spec */
    .read = NULL,
    .write = spapr_msi_write,
    .endianness = DEVICE_LITTLE_ENDIAN
};

/*
 * PHB PCI device
 */
static AddressSpace *spapr_pci_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    SpaprPhbState *phb = opaque;

    return &phb->iommu_as;
}

static char *spapr_phb_vfio_get_loc_code(SpaprPhbState *sphb,  PCIDevice *pdev)
{
    char *path = NULL, *buf = NULL, *host = NULL;

    /* Get the PCI VFIO host id */
    host = object_property_get_str(OBJECT(pdev), "host", NULL);
    if (!host) {
        goto err_out;
    }

    /* Construct the path of the file that will give us the DT location */
    path = g_strdup_printf("/sys/bus/pci/devices/%s/devspec", host);
    g_free(host);
    if (!g_file_get_contents(path, &buf, NULL, NULL)) {
        goto err_out;
    }
    g_free(path);

    /* Construct and read from host device tree the loc-code */
    path = g_strdup_printf("/proc/device-tree%s/ibm,loc-code", buf);
    g_free(buf);
    if (!g_file_get_contents(path, &buf, NULL, NULL)) {
        goto err_out;
    }
    return buf;

err_out:
    g_free(path);
    return NULL;
}

static char *spapr_phb_get_loc_code(SpaprPhbState *sphb, PCIDevice *pdev)
{
    char *buf;
    const char *devtype = "qemu";
    uint32_t busnr = pci_bus_num(PCI_BUS(qdev_get_parent_bus(DEVICE(pdev))));

    if (object_dynamic_cast(OBJECT(pdev), "vfio-pci")) {
        buf = spapr_phb_vfio_get_loc_code(sphb, pdev);
        if (buf) {
            return buf;
        }
        devtype = "vfio";
    }
    /*
     * For emulated devices and VFIO-failure case, make up
     * the loc-code.
     */
    buf = g_strdup_printf("%s_%s:%04x:%02x:%02x.%x",
                          devtype, pdev->name, sphb->index, busnr,
                          PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
    return buf;
}

/* Macros to operate with address in OF binding to PCI */
#define b_x(x, p, l)    (((x) & ((1<<(l))-1)) << (p))
#define b_n(x)          b_x((x), 31, 1) /* 0 if relocatable */
#define b_p(x)          b_x((x), 30, 1) /* 1 if prefetchable */
#define b_t(x)          b_x((x), 29, 1) /* 1 if the address is aliased */
#define b_ss(x)         b_x((x), 24, 2) /* the space code */
#define b_bbbbbbbb(x)   b_x((x), 16, 8) /* bus number */
#define b_ddddd(x)      b_x((x), 11, 5) /* device number */
#define b_fff(x)        b_x((x), 8, 3)  /* function number */
#define b_rrrrrrrr(x)   b_x((x), 0, 8)  /* register number */

/* for 'reg'/'assigned-addresses' OF properties */
#define RESOURCE_CELLS_SIZE 2
#define RESOURCE_CELLS_ADDRESS 3

typedef struct ResourceFields {
    uint32_t phys_hi;
    uint32_t phys_mid;
    uint32_t phys_lo;
    uint32_t size_hi;
    uint32_t size_lo;
} QEMU_PACKED ResourceFields;

typedef struct ResourceProps {
    ResourceFields reg[8];
    ResourceFields assigned[7];
    uint32_t reg_len;
    uint32_t assigned_len;
} ResourceProps;

/* fill in the 'reg'/'assigned-resources' OF properties for
 * a PCI device. 'reg' describes resource requirements for a
 * device's IO/MEM regions, 'assigned-addresses' describes the
 * actual resource assignments.
 *
 * the properties are arrays of ('phys-addr', 'size') pairs describing
 * the addressable regions of the PCI device, where 'phys-addr' is a
 * RESOURCE_CELLS_ADDRESS-tuple of 32-bit integers corresponding to
 * (phys.hi, phys.mid, phys.lo), and 'size' is a
 * RESOURCE_CELLS_SIZE-tuple corresponding to (size.hi, size.lo).
 *
 * phys.hi = 0xYYXXXXZZ, where:
 *   0xYY = npt000ss
 *          |||   |
 *          |||   +-- space code
 *          |||               |
 *          |||               +  00 if configuration space
 *          |||               +  01 if IO region,
 *          |||               +  10 if 32-bit MEM region
 *          |||               +  11 if 64-bit MEM region
 *          |||
 *          ||+------ for non-relocatable IO: 1 if aliased
 *          ||        for relocatable IO: 1 if below 64KB
 *          ||        for MEM: 1 if below 1MB
 *          |+------- 1 if region is prefetchable
 *          +-------- 1 if region is non-relocatable
 *   0xXXXX = bbbbbbbb dddddfff, encoding bus, slot, and function
 *            bits respectively
 *   0xZZ = rrrrrrrr, the register number of the BAR corresponding
 *          to the region
 *
 * phys.mid and phys.lo correspond respectively to the hi/lo portions
 * of the actual address of the region.
 *
 * how the phys-addr/size values are used differ slightly between
 * 'reg' and 'assigned-addresses' properties. namely, 'reg' has
 * an additional description for the config space region of the
 * device, and in the case of QEMU has n=0 and phys.mid=phys.lo=0
 * to describe the region as relocatable, with an address-mapping
 * that corresponds directly to the PHB's address space for the
 * resource. 'assigned-addresses' always has n=1 set with an absolute
 * address assigned for the resource. in general, 'assigned-addresses'
 * won't be populated, since addresses for PCI devices are generally
 * unmapped initially and left to the guest to assign.
 *
 * note also that addresses defined in these properties are, at least
 * for PAPR guests, relative to the PHBs IO/MEM windows, and
 * correspond directly to the addresses in the BARs.
 *
 * in accordance with PCI Bus Binding to Open Firmware,
 * IEEE Std 1275-1994, section 4.1.1, as implemented by PAPR+ v2.7,
 * Appendix C.
 */
static void populate_resource_props(PCIDevice *d, ResourceProps *rp)
{
    int bus_num = pci_bus_num(PCI_BUS(qdev_get_parent_bus(DEVICE(d))));
    uint32_t dev_id = (b_bbbbbbbb(bus_num) |
                       b_ddddd(PCI_SLOT(d->devfn)) |
                       b_fff(PCI_FUNC(d->devfn)));
    ResourceFields *reg, *assigned;
    int i, reg_idx = 0, assigned_idx = 0;

    /* config space region */
    reg = &rp->reg[reg_idx++];
    reg->phys_hi = cpu_to_be32(dev_id);
    reg->phys_mid = 0;
    reg->phys_lo = 0;
    reg->size_hi = 0;
    reg->size_lo = 0;

    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        if (!d->io_regions[i].size) {
            continue;
        }

        reg = &rp->reg[reg_idx++];

        reg->phys_hi = cpu_to_be32(dev_id | b_rrrrrrrr(pci_bar(d, i)));
        if (d->io_regions[i].type & PCI_BASE_ADDRESS_SPACE_IO) {
            reg->phys_hi |= cpu_to_be32(b_ss(1));
        } else if (d->io_regions[i].type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
            reg->phys_hi |= cpu_to_be32(b_ss(3));
        } else {
            reg->phys_hi |= cpu_to_be32(b_ss(2));
        }
        reg->phys_mid = 0;
        reg->phys_lo = 0;
        reg->size_hi = cpu_to_be32(d->io_regions[i].size >> 32);
        reg->size_lo = cpu_to_be32(d->io_regions[i].size);

        if (d->io_regions[i].addr == PCI_BAR_UNMAPPED) {
            continue;
        }

        assigned = &rp->assigned[assigned_idx++];
        assigned->phys_hi = cpu_to_be32(be32_to_cpu(reg->phys_hi) | b_n(1));
        assigned->phys_mid = cpu_to_be32(d->io_regions[i].addr >> 32);
        assigned->phys_lo = cpu_to_be32(d->io_regions[i].addr);
        assigned->size_hi = reg->size_hi;
        assigned->size_lo = reg->size_lo;
    }

    rp->reg_len = reg_idx * sizeof(ResourceFields);
    rp->assigned_len = assigned_idx * sizeof(ResourceFields);
}

typedef struct PCIClass PCIClass;
typedef struct PCISubClass PCISubClass;
typedef struct PCIIFace PCIIFace;

struct PCIIFace {
    int iface;
    const char *name;
};

struct PCISubClass {
    int subclass;
    const char *name;
    const PCIIFace *iface;
};

struct PCIClass {
    const char *name;
    const PCISubClass *subc;
};

static const PCISubClass undef_subclass[] = {
    { PCI_CLASS_NOT_DEFINED_VGA, "display", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass mass_subclass[] = {
    { PCI_CLASS_STORAGE_SCSI, "scsi", NULL },
    { PCI_CLASS_STORAGE_IDE, "ide", NULL },
    { PCI_CLASS_STORAGE_FLOPPY, "fdc", NULL },
    { PCI_CLASS_STORAGE_IPI, "ipi", NULL },
    { PCI_CLASS_STORAGE_RAID, "raid", NULL },
    { PCI_CLASS_STORAGE_ATA, "ata", NULL },
    { PCI_CLASS_STORAGE_SATA, "sata", NULL },
    { PCI_CLASS_STORAGE_SAS, "sas", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass net_subclass[] = {
    { PCI_CLASS_NETWORK_ETHERNET, "ethernet", NULL },
    { PCI_CLASS_NETWORK_TOKEN_RING, "token-ring", NULL },
    { PCI_CLASS_NETWORK_FDDI, "fddi", NULL },
    { PCI_CLASS_NETWORK_ATM, "atm", NULL },
    { PCI_CLASS_NETWORK_ISDN, "isdn", NULL },
    { PCI_CLASS_NETWORK_WORLDFIP, "worldfip", NULL },
    { PCI_CLASS_NETWORK_PICMG214, "picmg", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass displ_subclass[] = {
    { PCI_CLASS_DISPLAY_VGA, "vga", NULL },
    { PCI_CLASS_DISPLAY_XGA, "xga", NULL },
    { PCI_CLASS_DISPLAY_3D, "3d-controller", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass media_subclass[] = {
    { PCI_CLASS_MULTIMEDIA_VIDEO, "video", NULL },
    { PCI_CLASS_MULTIMEDIA_AUDIO, "sound", NULL },
    { PCI_CLASS_MULTIMEDIA_PHONE, "telephony", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass mem_subclass[] = {
    { PCI_CLASS_MEMORY_RAM, "memory", NULL },
    { PCI_CLASS_MEMORY_FLASH, "flash", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass bridg_subclass[] = {
    { PCI_CLASS_BRIDGE_HOST, "host", NULL },
    { PCI_CLASS_BRIDGE_ISA, "isa", NULL },
    { PCI_CLASS_BRIDGE_EISA, "eisa", NULL },
    { PCI_CLASS_BRIDGE_MC, "mca", NULL },
    { PCI_CLASS_BRIDGE_PCI, "pci", NULL },
    { PCI_CLASS_BRIDGE_PCMCIA, "pcmcia", NULL },
    { PCI_CLASS_BRIDGE_NUBUS, "nubus", NULL },
    { PCI_CLASS_BRIDGE_CARDBUS, "cardbus", NULL },
    { PCI_CLASS_BRIDGE_RACEWAY, "raceway", NULL },
    { PCI_CLASS_BRIDGE_PCI_SEMITP, "semi-transparent-pci", NULL },
    { PCI_CLASS_BRIDGE_IB_PCI, "infiniband", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass comm_subclass[] = {
    { PCI_CLASS_COMMUNICATION_SERIAL, "serial", NULL },
    { PCI_CLASS_COMMUNICATION_PARALLEL, "parallel", NULL },
    { PCI_CLASS_COMMUNICATION_MULTISERIAL, "multiport-serial", NULL },
    { PCI_CLASS_COMMUNICATION_MODEM, "modem", NULL },
    { PCI_CLASS_COMMUNICATION_GPIB, "gpib", NULL },
    { PCI_CLASS_COMMUNICATION_SC, "smart-card", NULL },
    { 0xFF, NULL, NULL, },
};

static const PCIIFace pic_iface[] = {
    { PCI_CLASS_SYSTEM_PIC_IOAPIC, "io-apic" },
    { PCI_CLASS_SYSTEM_PIC_IOXAPIC, "io-xapic" },
    { 0xFF, NULL },
};

static const PCISubClass sys_subclass[] = {
    { PCI_CLASS_SYSTEM_PIC, "interrupt-controller", pic_iface },
    { PCI_CLASS_SYSTEM_DMA, "dma-controller", NULL },
    { PCI_CLASS_SYSTEM_TIMER, "timer", NULL },
    { PCI_CLASS_SYSTEM_RTC, "rtc", NULL },
    { PCI_CLASS_SYSTEM_PCI_HOTPLUG, "hot-plug-controller", NULL },
    { PCI_CLASS_SYSTEM_SDHCI, "sd-host-controller", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass inp_subclass[] = {
    { PCI_CLASS_INPUT_KEYBOARD, "keyboard", NULL },
    { PCI_CLASS_INPUT_PEN, "pen", NULL },
    { PCI_CLASS_INPUT_MOUSE, "mouse", NULL },
    { PCI_CLASS_INPUT_SCANNER, "scanner", NULL },
    { PCI_CLASS_INPUT_GAMEPORT, "gameport", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass dock_subclass[] = {
    { PCI_CLASS_DOCKING_GENERIC, "dock", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass cpu_subclass[] = {
    { PCI_CLASS_PROCESSOR_PENTIUM, "pentium", NULL },
    { PCI_CLASS_PROCESSOR_POWERPC, "powerpc", NULL },
    { PCI_CLASS_PROCESSOR_MIPS, "mips", NULL },
    { PCI_CLASS_PROCESSOR_CO, "co-processor", NULL },
    { 0xFF, NULL, NULL },
};

static const PCIIFace usb_iface[] = {
    { PCI_CLASS_SERIAL_USB_UHCI, "usb-uhci" },
    { PCI_CLASS_SERIAL_USB_OHCI, "usb-ohci", },
    { PCI_CLASS_SERIAL_USB_EHCI, "usb-ehci" },
    { PCI_CLASS_SERIAL_USB_XHCI, "usb-xhci" },
    { PCI_CLASS_SERIAL_USB_UNKNOWN, "usb-unknown" },
    { PCI_CLASS_SERIAL_USB_DEVICE, "usb-device" },
    { 0xFF, NULL },
};

static const PCISubClass ser_subclass[] = {
    { PCI_CLASS_SERIAL_FIREWIRE, "firewire", NULL },
    { PCI_CLASS_SERIAL_ACCESS, "access-bus", NULL },
    { PCI_CLASS_SERIAL_SSA, "ssa", NULL },
    { PCI_CLASS_SERIAL_USB, "usb", usb_iface },
    { PCI_CLASS_SERIAL_FIBER, "fibre-channel", NULL },
    { PCI_CLASS_SERIAL_SMBUS, "smb", NULL },
    { PCI_CLASS_SERIAL_IB, "infiniband", NULL },
    { PCI_CLASS_SERIAL_IPMI, "ipmi", NULL },
    { PCI_CLASS_SERIAL_SERCOS, "sercos", NULL },
    { PCI_CLASS_SERIAL_CANBUS, "canbus", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass wrl_subclass[] = {
    { PCI_CLASS_WIRELESS_IRDA, "irda", NULL },
    { PCI_CLASS_WIRELESS_CIR, "consumer-ir", NULL },
    { PCI_CLASS_WIRELESS_RF_CONTROLLER, "rf-controller", NULL },
    { PCI_CLASS_WIRELESS_BLUETOOTH, "bluetooth", NULL },
    { PCI_CLASS_WIRELESS_BROADBAND, "broadband", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass sat_subclass[] = {
    { PCI_CLASS_SATELLITE_TV, "satellite-tv", NULL },
    { PCI_CLASS_SATELLITE_AUDIO, "satellite-audio", NULL },
    { PCI_CLASS_SATELLITE_VOICE, "satellite-voice", NULL },
    { PCI_CLASS_SATELLITE_DATA, "satellite-data", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass crypt_subclass[] = {
    { PCI_CLASS_CRYPT_NETWORK, "network-encryption", NULL },
    { PCI_CLASS_CRYPT_ENTERTAINMENT,
      "entertainment-encryption", NULL },
    { 0xFF, NULL, NULL },
};

static const PCISubClass spc_subclass[] = {
    { PCI_CLASS_SP_DPIO, "dpio", NULL },
    { PCI_CLASS_SP_PERF, "counter", NULL },
    { PCI_CLASS_SP_SYNCH, "measurement", NULL },
    { PCI_CLASS_SP_MANAGEMENT, "management-card", NULL },
    { 0xFF, NULL, NULL },
};

static const PCIClass pci_classes[] = {
    { "legacy-device", undef_subclass },
    { "mass-storage",  mass_subclass },
    { "network", net_subclass },
    { "display", displ_subclass, },
    { "multimedia-device", media_subclass },
    { "memory-controller", mem_subclass },
    { "unknown-bridge", bridg_subclass },
    { "communication-controller", comm_subclass},
    { "system-peripheral", sys_subclass },
    { "input-controller", inp_subclass },
    { "docking-station", dock_subclass },
    { "cpu", cpu_subclass },
    { "serial-bus", ser_subclass },
    { "wireless-controller", wrl_subclass },
    { "intelligent-io", NULL },
    { "satellite-device", sat_subclass },
    { "encryption", crypt_subclass },
    { "data-processing-controller", spc_subclass },
};

static const char *dt_name_from_class(uint8_t class, uint8_t subclass,
                                      uint8_t iface)
{
    const PCIClass *pclass;
    const PCISubClass *psubclass;
    const PCIIFace *piface;
    const char *name;

    if (class >= ARRAY_SIZE(pci_classes)) {
        return "pci";
    }

    pclass = pci_classes + class;
    name = pclass->name;

    if (pclass->subc == NULL) {
        return name;
    }

    psubclass = pclass->subc;
    while ((psubclass->subclass & 0xff) != 0xff) {
        if ((psubclass->subclass & 0xff) == subclass) {
            name = psubclass->name;
            break;
        }
        psubclass++;
    }

    piface = psubclass->iface;
    if (piface == NULL) {
        return name;
    }
    while ((piface->iface & 0xff) != 0xff) {
        if ((piface->iface & 0xff) == iface) {
            name = piface->name;
            break;
        }
        piface++;
    }

    return name;
}

/*
 * DRC helper functions
 */

static uint32_t drc_id_from_devfn(SpaprPhbState *phb,
                                  uint8_t chassis, int32_t devfn)
{
    return (phb->index << 16) | (chassis << 8) | devfn;
}

static SpaprDrc *drc_from_devfn(SpaprPhbState *phb,
                                uint8_t chassis, int32_t devfn)
{
    return spapr_drc_by_id(TYPE_SPAPR_DRC_PCI,
                           drc_id_from_devfn(phb, chassis, devfn));
}

static uint8_t chassis_from_bus(PCIBus *bus, Error **errp)
{
    if (pci_bus_is_root(bus)) {
        return 0;
    } else {
        PCIDevice *bridge = pci_bridge_get_device(bus);

        return object_property_get_uint(OBJECT(bridge), "chassis_nr", errp);
    }
}

static SpaprDrc *drc_from_dev(SpaprPhbState *phb, PCIDevice *dev)
{
    Error *local_err = NULL;
    uint8_t chassis = chassis_from_bus(pci_get_bus(dev), &local_err);

    if (local_err) {
        error_report_err(local_err);
        return NULL;
    }

    return drc_from_devfn(phb, chassis, dev->devfn);
}

static void add_drcs(SpaprPhbState *phb, PCIBus *bus, Error **errp)
{
    Object *owner;
    int i;
    uint8_t chassis;
    Error *local_err = NULL;

    if (!phb->dr_enabled) {
        return;
    }

    chassis = chassis_from_bus(bus, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (pci_bus_is_root(bus)) {
        owner = OBJECT(phb);
    } else {
        owner = OBJECT(pci_bridge_get_device(bus));
    }

    for (i = 0; i < PCI_SLOT_MAX * PCI_FUNC_MAX; i++) {
        spapr_dr_connector_new(owner, TYPE_SPAPR_DRC_PCI,
                               drc_id_from_devfn(phb, chassis, i));
    }
}

static void remove_drcs(SpaprPhbState *phb, PCIBus *bus, Error **errp)
{
    int i;
    uint8_t chassis;
    Error *local_err = NULL;

    if (!phb->dr_enabled) {
        return;
    }

    chassis = chassis_from_bus(bus, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    for (i = PCI_SLOT_MAX * PCI_FUNC_MAX - 1; i >= 0; i--) {
        SpaprDrc *drc = drc_from_devfn(phb, chassis, i);

        if (drc) {
            object_unparent(OBJECT(drc));
        }
    }
}

typedef struct PciWalkFdt {
    void *fdt;
    int offset;
    SpaprPhbState *sphb;
    int err;
} PciWalkFdt;

static int spapr_dt_pci_device(SpaprPhbState *sphb, PCIDevice *dev,
                               void *fdt, int parent_offset);

static void spapr_dt_pci_device_cb(PCIBus *bus, PCIDevice *pdev,
                                   void *opaque)
{
    PciWalkFdt *p = opaque;
    int err;

    if (p->err) {
        /* Something's already broken, don't keep going */
        return;
    }

    err = spapr_dt_pci_device(p->sphb, pdev, p->fdt, p->offset);
    if (err < 0) {
        p->err = err;
    }
}

/* Augment PCI device node with bridge specific information */
static int spapr_dt_pci_bus(SpaprPhbState *sphb, PCIBus *bus,
                               void *fdt, int offset)
{
    Object *owner;
    PciWalkFdt cbinfo = {
        .fdt = fdt,
        .offset = offset,
        .sphb = sphb,
        .err = 0,
    };
    int ret;

    _FDT(fdt_setprop_cell(fdt, offset, "#address-cells",
                          RESOURCE_CELLS_ADDRESS));
    _FDT(fdt_setprop_cell(fdt, offset, "#size-cells",
                          RESOURCE_CELLS_SIZE));

    assert(bus);
    pci_for_each_device_reverse(bus, pci_bus_num(bus),
                                spapr_dt_pci_device_cb, &cbinfo);
    if (cbinfo.err) {
        return cbinfo.err;
    }

    if (pci_bus_is_root(bus)) {
        owner = OBJECT(sphb);
    } else {
        owner = OBJECT(pci_bridge_get_device(bus));
    }

    ret = spapr_dt_drc(fdt, offset, owner,
                       SPAPR_DR_CONNECTOR_TYPE_PCI);
    if (ret) {
        return ret;
    }

    return offset;
}

/* create OF node for pci device and required OF DT properties */
static int spapr_dt_pci_device(SpaprPhbState *sphb, PCIDevice *dev,
                               void *fdt, int parent_offset)
{
    int offset;
    const gchar *basename;
    gchar *nodename;
    int slot = PCI_SLOT(dev->devfn);
    int func = PCI_FUNC(dev->devfn);
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
    ResourceProps rp;
    SpaprDrc *drc = drc_from_dev(sphb, dev);
    uint32_t vendor_id = pci_default_read_config(dev, PCI_VENDOR_ID, 2);
    uint32_t device_id = pci_default_read_config(dev, PCI_DEVICE_ID, 2);
    uint32_t revision_id = pci_default_read_config(dev, PCI_REVISION_ID, 1);
    uint32_t ccode = pci_default_read_config(dev, PCI_CLASS_PROG, 3);
    uint32_t irq_pin = pci_default_read_config(dev, PCI_INTERRUPT_PIN, 1);
    uint32_t subsystem_id = pci_default_read_config(dev, PCI_SUBSYSTEM_ID, 2);
    uint32_t subsystem_vendor_id =
        pci_default_read_config(dev, PCI_SUBSYSTEM_VENDOR_ID, 2);
    uint32_t cache_line_size =
        pci_default_read_config(dev, PCI_CACHE_LINE_SIZE, 1);
    uint32_t pci_status = pci_default_read_config(dev, PCI_STATUS, 2);
    gchar *loc_code;

    basename = dt_name_from_class((ccode >> 16) & 0xff, (ccode >> 8) & 0xff,
                                  ccode & 0xff);

    if (func != 0) {
        nodename = g_strdup_printf("%s@%x,%x", basename, slot, func);
    } else {
        nodename = g_strdup_printf("%s@%x", basename, slot);
    }

    _FDT(offset = fdt_add_subnode(fdt, parent_offset, nodename));

    g_free(nodename);

    /* in accordance with PAPR+ v2.7 13.6.3, Table 181 */
    _FDT(fdt_setprop_cell(fdt, offset, "vendor-id", vendor_id));
    _FDT(fdt_setprop_cell(fdt, offset, "device-id", device_id));
    _FDT(fdt_setprop_cell(fdt, offset, "revision-id", revision_id));

    _FDT(fdt_setprop_cell(fdt, offset, "class-code", ccode));
    if (irq_pin) {
        _FDT(fdt_setprop_cell(fdt, offset, "interrupts", irq_pin));
    }

    if (subsystem_id) {
        _FDT(fdt_setprop_cell(fdt, offset, "subsystem-id", subsystem_id));
    }

    if (subsystem_vendor_id) {
        _FDT(fdt_setprop_cell(fdt, offset, "subsystem-vendor-id",
                              subsystem_vendor_id));
    }

    _FDT(fdt_setprop_cell(fdt, offset, "cache-line-size", cache_line_size));


    /* the following fdt cells are masked off the pci status register */
    _FDT(fdt_setprop_cell(fdt, offset, "devsel-speed",
                          PCI_STATUS_DEVSEL_MASK & pci_status));

    if (pci_status & PCI_STATUS_FAST_BACK) {
        _FDT(fdt_setprop(fdt, offset, "fast-back-to-back", NULL, 0));
    }
    if (pci_status & PCI_STATUS_66MHZ) {
        _FDT(fdt_setprop(fdt, offset, "66mhz-capable", NULL, 0));
    }
    if (pci_status & PCI_STATUS_UDF) {
        _FDT(fdt_setprop(fdt, offset, "udf-supported", NULL, 0));
    }

    loc_code = spapr_phb_get_loc_code(sphb, dev);
    _FDT(fdt_setprop_string(fdt, offset, "ibm,loc-code", loc_code));
    g_free(loc_code);

    if (drc) {
        _FDT(fdt_setprop_cell(fdt, offset, "ibm,my-drc-index",
                              spapr_drc_index(drc)));
    }

    if (msi_present(dev)) {
        uint32_t max_msi = msi_nr_vectors_allocated(dev);
        if (max_msi) {
            _FDT(fdt_setprop_cell(fdt, offset, "ibm,req#msi", max_msi));
        }
    }
    if (msix_present(dev)) {
        uint32_t max_msix = dev->msix_entries_nr;
        if (max_msix) {
            _FDT(fdt_setprop_cell(fdt, offset, "ibm,req#msi-x", max_msix));
        }
    }

    populate_resource_props(dev, &rp);
    _FDT(fdt_setprop(fdt, offset, "reg", (uint8_t *)rp.reg, rp.reg_len));
    _FDT(fdt_setprop(fdt, offset, "assigned-addresses",
                     (uint8_t *)rp.assigned, rp.assigned_len));

    if (sphb->pcie_ecs && pci_is_express(dev)) {
        _FDT(fdt_setprop_cell(fdt, offset, "ibm,pci-config-space-type", 0x1));
    }

    spapr_phb_nvgpu_populate_pcidev_dt(dev, fdt, offset, sphb);

    if (!pc->is_bridge) {
        /* Properties only for non-bridges */
        uint32_t min_grant = pci_default_read_config(dev, PCI_MIN_GNT, 1);
        uint32_t max_latency = pci_default_read_config(dev, PCI_MAX_LAT, 1);
        _FDT(fdt_setprop_cell(fdt, offset, "min-grant", min_grant));
        _FDT(fdt_setprop_cell(fdt, offset, "max-latency", max_latency));
        return offset;
    } else {
        PCIBus *sec_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(dev));

        return spapr_dt_pci_bus(sphb, sec_bus, fdt, offset);
    }
}

/* Callback to be called during DRC release. */
void spapr_phb_remove_pci_device_cb(DeviceState *dev)
{
    HotplugHandler *hotplug_ctrl = qdev_get_hotplug_handler(dev);

    hotplug_handler_unplug(hotplug_ctrl, dev, &error_abort);
    object_unparent(OBJECT(dev));
}

int spapr_pci_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                          void *fdt, int *fdt_start_offset, Error **errp)
{
    HotplugHandler *plug_handler = qdev_get_hotplug_handler(drc->dev);
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(plug_handler);
    PCIDevice *pdev = PCI_DEVICE(drc->dev);

    *fdt_start_offset = spapr_dt_pci_device(sphb, pdev, fdt, 0);
    return 0;
}

static void spapr_pci_bridge_plug(SpaprPhbState *phb,
                                  PCIBridge *bridge,
                                  Error **errp)
{
    Error *local_err = NULL;
    PCIBus *bus = pci_bridge_get_sec_bus(bridge);

    add_drcs(phb, bus, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void spapr_pci_plug(HotplugHandler *plug_handler,
                           DeviceState *plugged_dev, Error **errp)
{
    SpaprPhbState *phb = SPAPR_PCI_HOST_BRIDGE(DEVICE(plug_handler));
    PCIDevice *pdev = PCI_DEVICE(plugged_dev);
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(plugged_dev);
    SpaprDrc *drc = drc_from_dev(phb, pdev);
    Error *local_err = NULL;
    PCIBus *bus = PCI_BUS(qdev_get_parent_bus(DEVICE(pdev)));
    uint32_t slotnr = PCI_SLOT(pdev->devfn);

    /* if DR is disabled we don't need to do anything in the case of
     * hotplug or coldplug callbacks
     */
    if (!phb->dr_enabled) {
        /* if this is a hotplug operation initiated by the user
         * we need to let them know it's not enabled
         */
        if (plugged_dev->hotplugged) {
            error_setg(&local_err, QERR_BUS_NO_HOTPLUG,
                       object_get_typename(OBJECT(phb)));
        }
        goto out;
    }

    g_assert(drc);

    if (pc->is_bridge) {
        spapr_pci_bridge_plug(phb, PCI_BRIDGE(plugged_dev), &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /* Following the QEMU convention used for PCIe multifunction
     * hotplug, we do not allow functions to be hotplugged to a
     * slot that already has function 0 present
     */
    if (plugged_dev->hotplugged && bus->devices[PCI_DEVFN(slotnr, 0)] &&
        PCI_FUNC(pdev->devfn) != 0) {
        error_setg(&local_err, "PCI: slot %d function 0 already ocuppied by %s,"
                   " additional functions can no longer be exposed to guest.",
                   slotnr, bus->devices[PCI_DEVFN(slotnr, 0)]->name);
        goto out;
    }

    spapr_drc_attach(drc, DEVICE(pdev), &local_err);
    if (local_err) {
        goto out;
    }

    /* If this is function 0, signal hotplug for all the device functions.
     * Otherwise defer sending the hotplug event.
     */
    if (!spapr_drc_hotplugged(plugged_dev)) {
        spapr_drc_reset(drc);
    } else if (PCI_FUNC(pdev->devfn) == 0) {
        int i;
        uint8_t chassis = chassis_from_bus(pci_get_bus(pdev), &local_err);

        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        for (i = 0; i < 8; i++) {
            SpaprDrc *func_drc;
            SpaprDrcClass *func_drck;
            SpaprDREntitySense state;

            func_drc = drc_from_devfn(phb, chassis, PCI_DEVFN(slotnr, i));
            func_drck = SPAPR_DR_CONNECTOR_GET_CLASS(func_drc);
            state = func_drck->dr_entity_sense(func_drc);

            if (state == SPAPR_DR_ENTITY_SENSE_PRESENT) {
                spapr_hotplug_req_add_by_index(func_drc);
            }
        }
    }

out:
    error_propagate(errp, local_err);
}

static void spapr_pci_bridge_unplug(SpaprPhbState *phb,
                                    PCIBridge *bridge,
                                    Error **errp)
{
    Error *local_err = NULL;
    PCIBus *bus = pci_bridge_get_sec_bus(bridge);

    remove_drcs(phb, bus, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void spapr_pci_unplug(HotplugHandler *plug_handler,
                             DeviceState *plugged_dev, Error **errp)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(plugged_dev);
    SpaprPhbState *phb = SPAPR_PCI_HOST_BRIDGE(DEVICE(plug_handler));

    /* some version guests do not wait for completion of a device
     * cleanup (generally done asynchronously by the kernel) before
     * signaling to QEMU that the device is safe, but instead sleep
     * for some 'safe' period of time. unfortunately on a busy host
     * this sleep isn't guaranteed to be long enough, resulting in
     * bad things like IRQ lines being left asserted during final
     * device removal. to deal with this we call reset just prior
     * to finalizing the device, which will put the device back into
     * an 'idle' state, as the device cleanup code expects.
     */
    pci_device_reset(PCI_DEVICE(plugged_dev));

    if (pc->is_bridge) {
        Error *local_err = NULL;
        spapr_pci_bridge_unplug(phb, PCI_BRIDGE(plugged_dev), &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
        }
        return;
    }

    object_property_set_bool(OBJECT(plugged_dev), false, "realized", NULL);
}

static void spapr_pci_unplug_request(HotplugHandler *plug_handler,
                                     DeviceState *plugged_dev, Error **errp)
{
    SpaprPhbState *phb = SPAPR_PCI_HOST_BRIDGE(DEVICE(plug_handler));
    PCIDevice *pdev = PCI_DEVICE(plugged_dev);
    SpaprDrc *drc = drc_from_dev(phb, pdev);

    if (!phb->dr_enabled) {
        error_setg(errp, QERR_BUS_NO_HOTPLUG,
                   object_get_typename(OBJECT(phb)));
        return;
    }

    g_assert(drc);
    g_assert(drc->dev == plugged_dev);

    if (!spapr_drc_unplug_requested(drc)) {
        PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(plugged_dev);
        uint32_t slotnr = PCI_SLOT(pdev->devfn);
        SpaprDrc *func_drc;
        SpaprDrcClass *func_drck;
        SpaprDREntitySense state;
        int i;
        Error *local_err = NULL;
        uint8_t chassis = chassis_from_bus(pci_get_bus(pdev), &local_err);

        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        if (pc->is_bridge) {
            error_setg(errp, "PCI: Hot unplug of PCI bridges not supported");
        }

        /* ensure any other present functions are pending unplug */
        if (PCI_FUNC(pdev->devfn) == 0) {
            for (i = 1; i < 8; i++) {
                func_drc = drc_from_devfn(phb, chassis, PCI_DEVFN(slotnr, i));
                func_drck = SPAPR_DR_CONNECTOR_GET_CLASS(func_drc);
                state = func_drck->dr_entity_sense(func_drc);
                if (state == SPAPR_DR_ENTITY_SENSE_PRESENT
                    && !spapr_drc_unplug_requested(func_drc)) {
                    error_setg(errp,
                               "PCI: slot %d, function %d still present. "
                               "Must unplug all non-0 functions first.",
                               slotnr, i);
                    return;
                }
            }
        }

        spapr_drc_detach(drc);

        /* if this isn't func 0, defer unplug event. otherwise signal removal
         * for all present functions
         */
        if (PCI_FUNC(pdev->devfn) == 0) {
            for (i = 7; i >= 0; i--) {
                func_drc = drc_from_devfn(phb, chassis, PCI_DEVFN(slotnr, i));
                func_drck = SPAPR_DR_CONNECTOR_GET_CLASS(func_drc);
                state = func_drck->dr_entity_sense(func_drc);
                if (state == SPAPR_DR_ENTITY_SENSE_PRESENT) {
                    spapr_hotplug_req_remove_by_index(func_drc);
                }
            }
        }
    }
}

static void spapr_phb_finalizefn(Object *obj)
{
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(obj);

    g_free(sphb->dtbusname);
    sphb->dtbusname = NULL;
}

static void spapr_phb_unrealize(DeviceState *dev, Error **errp)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(phb);
    SpaprTceTable *tcet;
    int i;
    const unsigned windows_supported = spapr_phb_windows_supported(sphb);
    Error *local_err = NULL;

    spapr_phb_nvgpu_free(sphb);

    if (sphb->msi) {
        g_hash_table_unref(sphb->msi);
        sphb->msi = NULL;
    }

    /*
     * Remove IO/MMIO subregions and aliases, rest should get cleaned
     * via PHB's unrealize->object_finalize
     */
    for (i = windows_supported - 1; i >= 0; i--) {
        tcet = spapr_tce_find_by_liobn(sphb->dma_liobn[i]);
        if (tcet) {
            memory_region_del_subregion(&sphb->iommu_root,
                                        spapr_tce_get_iommu(tcet));
        }
    }

    remove_drcs(sphb, phb->bus, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    for (i = PCI_NUM_PINS - 1; i >= 0; i--) {
        if (sphb->lsi_table[i].irq) {
            spapr_irq_free(spapr, sphb->lsi_table[i].irq, 1);
            sphb->lsi_table[i].irq = 0;
        }
    }

    QLIST_REMOVE(sphb, list);

    memory_region_del_subregion(&sphb->iommu_root, &sphb->msiwindow);

    /*
     * An attached PCI device may have memory listeners, eg. VFIO PCI. We have
     * unmapped all sections. Remove the listeners now, before destroying the
     * address space.
     */
    address_space_remove_listeners(&sphb->iommu_as);
    address_space_destroy(&sphb->iommu_as);

    qbus_set_hotplug_handler(BUS(phb->bus), NULL, &error_abort);
    pci_unregister_root_bus(phb->bus);

    memory_region_del_subregion(get_system_memory(), &sphb->iowindow);
    if (sphb->mem64_win_pciaddr != (hwaddr)-1) {
        memory_region_del_subregion(get_system_memory(), &sphb->mem64window);
    }
    memory_region_del_subregion(get_system_memory(), &sphb->mem32window);
}

static void spapr_phb_realize(DeviceState *dev, Error **errp)
{
    /* We don't use SPAPR_MACHINE() in order to exit gracefully if the user
     * tries to add a sPAPR PHB to a non-pseries machine.
     */
    SpaprMachineState *spapr =
        (SpaprMachineState *) object_dynamic_cast(qdev_get_machine(),
                                                  TYPE_SPAPR_MACHINE);
    SpaprMachineClass *smc = spapr ? SPAPR_MACHINE_GET_CLASS(spapr) : NULL;
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(s);
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    char *namebuf;
    int i;
    PCIBus *bus;
    uint64_t msi_window_size = 4096;
    SpaprTceTable *tcet;
    const unsigned windows_supported = spapr_phb_windows_supported(sphb);
    Error *local_err = NULL;

    if (!spapr) {
        error_setg(errp, TYPE_SPAPR_PCI_HOST_BRIDGE " needs a pseries machine");
        return;
    }

    assert(sphb->index != (uint32_t)-1); /* checked in spapr_phb_pre_plug() */

    if (sphb->mem64_win_size != 0) {
        if (sphb->mem_win_size > SPAPR_PCI_MEM32_WIN_SIZE) {
            error_setg(errp, "32-bit memory window of size 0x%"HWADDR_PRIx
                       " (max 2 GiB)", sphb->mem_win_size);
            return;
        }

        /* 64-bit window defaults to identity mapping */
        sphb->mem64_win_pciaddr = sphb->mem64_win_addr;
    } else if (sphb->mem_win_size > SPAPR_PCI_MEM32_WIN_SIZE) {
        /*
         * For compatibility with old configuration, if no 64-bit MMIO
         * window is specified, but the ordinary (32-bit) memory
         * window is specified as > 2GiB, we treat it as a 2GiB 32-bit
         * window, with a 64-bit MMIO window following on immediately
         * afterwards
         */
        sphb->mem64_win_size = sphb->mem_win_size - SPAPR_PCI_MEM32_WIN_SIZE;
        sphb->mem64_win_addr = sphb->mem_win_addr + SPAPR_PCI_MEM32_WIN_SIZE;
        sphb->mem64_win_pciaddr =
            SPAPR_PCI_MEM_WIN_BUS_OFFSET + SPAPR_PCI_MEM32_WIN_SIZE;
        sphb->mem_win_size = SPAPR_PCI_MEM32_WIN_SIZE;
    }

    if (spapr_pci_find_phb(spapr, sphb->buid)) {
        SpaprPhbState *s;

        error_setg(errp, "PCI host bridges must have unique indexes");
        error_append_hint(errp, "The following indexes are already in use:");
        QLIST_FOREACH(s, &spapr->phbs, list) {
            error_append_hint(errp, " %d", s->index);
        }
        error_append_hint(errp, "\nTry another value for the index property\n");
        return;
    }

    if (sphb->numa_node != -1 &&
        (sphb->numa_node >= MAX_NODES || !numa_info[sphb->numa_node].present)) {
        error_setg(errp, "Invalid NUMA node ID for PCI host bridge");
        return;
    }

    sphb->dtbusname = g_strdup_printf("pci@%" PRIx64, sphb->buid);

    /* Initialize memory regions */
    namebuf = g_strdup_printf("%s.mmio", sphb->dtbusname);
    memory_region_init(&sphb->memspace, OBJECT(sphb), namebuf, UINT64_MAX);
    g_free(namebuf);

    namebuf = g_strdup_printf("%s.mmio32-alias", sphb->dtbusname);
    memory_region_init_alias(&sphb->mem32window, OBJECT(sphb),
                             namebuf, &sphb->memspace,
                             SPAPR_PCI_MEM_WIN_BUS_OFFSET, sphb->mem_win_size);
    g_free(namebuf);
    memory_region_add_subregion(get_system_memory(), sphb->mem_win_addr,
                                &sphb->mem32window);

    if (sphb->mem64_win_size != 0) {
        namebuf = g_strdup_printf("%s.mmio64-alias", sphb->dtbusname);
        memory_region_init_alias(&sphb->mem64window, OBJECT(sphb),
                                 namebuf, &sphb->memspace,
                                 sphb->mem64_win_pciaddr, sphb->mem64_win_size);
        g_free(namebuf);

        memory_region_add_subregion(get_system_memory(),
                                    sphb->mem64_win_addr,
                                    &sphb->mem64window);
    }

    /* Initialize IO regions */
    namebuf = g_strdup_printf("%s.io", sphb->dtbusname);
    memory_region_init(&sphb->iospace, OBJECT(sphb),
                       namebuf, SPAPR_PCI_IO_WIN_SIZE);
    g_free(namebuf);

    namebuf = g_strdup_printf("%s.io-alias", sphb->dtbusname);
    memory_region_init_alias(&sphb->iowindow, OBJECT(sphb), namebuf,
                             &sphb->iospace, 0, SPAPR_PCI_IO_WIN_SIZE);
    g_free(namebuf);
    memory_region_add_subregion(get_system_memory(), sphb->io_win_addr,
                                &sphb->iowindow);

    bus = pci_register_root_bus(dev, NULL,
                                pci_spapr_set_irq, pci_swizzle_map_irq_fn, sphb,
                                &sphb->memspace, &sphb->iospace,
                                PCI_DEVFN(0, 0), PCI_NUM_PINS,
                                TYPE_PCI_BUS);

    /*
     * Despite resembling a vanilla PCI bus in most ways, the PAPR
     * para-virtualized PCI bus *does* permit PCI-E extended config
     * space access
     */
    if (sphb->pcie_ecs) {
        bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;
    }
    phb->bus = bus;
    qbus_set_hotplug_handler(BUS(phb->bus), OBJECT(sphb), NULL);

    /*
     * Initialize PHB address space.
     * By default there will be at least one subregion for default
     * 32bit DMA window.
     * Later the guest might want to create another DMA window
     * which will become another memory subregion.
     */
    namebuf = g_strdup_printf("%s.iommu-root", sphb->dtbusname);
    memory_region_init(&sphb->iommu_root, OBJECT(sphb),
                       namebuf, UINT64_MAX);
    g_free(namebuf);
    address_space_init(&sphb->iommu_as, &sphb->iommu_root,
                       sphb->dtbusname);

    /*
     * As MSI/MSIX interrupts trigger by writing at MSI/MSIX vectors,
     * we need to allocate some memory to catch those writes coming
     * from msi_notify()/msix_notify().
     * As MSIMessage:addr is going to be the same and MSIMessage:data
     * is going to be a VIRQ number, 4 bytes of the MSI MR will only
     * be used.
     *
     * For KVM we want to ensure that this memory is a full page so that
     * our memory slot is of page size granularity.
     */
    if (kvm_enabled()) {
        msi_window_size = getpagesize();
    }

    memory_region_init_io(&sphb->msiwindow, OBJECT(sphb), &spapr_msi_ops, spapr,
                          "msi", msi_window_size);
    memory_region_add_subregion(&sphb->iommu_root, SPAPR_PCI_MSI_WINDOW,
                                &sphb->msiwindow);

    pci_setup_iommu(bus, spapr_pci_dma_iommu, sphb);

    pci_bus_set_route_irq_fn(bus, spapr_route_intx_pin_to_irq);

    QLIST_INSERT_HEAD(&spapr->phbs, sphb, list);

    /* Initialize the LSI table */
    for (i = 0; i < PCI_NUM_PINS; i++) {
        uint32_t irq = SPAPR_IRQ_PCI_LSI + sphb->index * PCI_NUM_PINS + i;

        if (smc->legacy_irq_allocation) {
            irq = spapr_irq_findone(spapr, &local_err);
            if (local_err) {
                error_propagate_prepend(errp, local_err,
                                        "can't allocate LSIs: ");
                /*
                 * Older machines will never support PHB hotplug, ie, this is an
                 * init only path and QEMU will terminate. No need to rollback.
                 */
                return;
            }
        }

        spapr_irq_claim(spapr, irq, true, &local_err);
        if (local_err) {
            error_propagate_prepend(errp, local_err, "can't allocate LSIs: ");
            goto unrealize;
        }

        sphb->lsi_table[i].irq = irq;
    }

    /* allocate connectors for child PCI devices */
    add_drcs(sphb, phb->bus, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto unrealize;
    }

    /* DMA setup */
    for (i = 0; i < windows_supported; ++i) {
        tcet = spapr_tce_new_table(DEVICE(sphb), sphb->dma_liobn[i]);
        if (!tcet) {
            error_setg(errp, "Creating window#%d failed for %s",
                       i, sphb->dtbusname);
            goto unrealize;
        }
        memory_region_add_subregion(&sphb->iommu_root, 0,
                                    spapr_tce_get_iommu(tcet));
    }

    sphb->msi = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, g_free);
    return;

unrealize:
    spapr_phb_unrealize(dev, NULL);
}

static int spapr_phb_children_reset(Object *child, void *opaque)
{
    DeviceState *dev = (DeviceState *) object_dynamic_cast(child, TYPE_DEVICE);

    if (dev) {
        device_reset(dev);
    }

    return 0;
}

void spapr_phb_dma_reset(SpaprPhbState *sphb)
{
    int i;
    SpaprTceTable *tcet;

    for (i = 0; i < SPAPR_PCI_DMA_MAX_WINDOWS; ++i) {
        tcet = spapr_tce_find_by_liobn(sphb->dma_liobn[i]);

        if (tcet && tcet->nb_table) {
            spapr_tce_table_disable(tcet);
        }
    }

    /* Register default 32bit DMA window */
    tcet = spapr_tce_find_by_liobn(sphb->dma_liobn[0]);
    spapr_tce_table_enable(tcet, SPAPR_TCE_PAGE_SHIFT, sphb->dma_win_addr,
                           sphb->dma_win_size >> SPAPR_TCE_PAGE_SHIFT);
}

static void spapr_phb_reset(DeviceState *qdev)
{
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(qdev);
    Error *errp = NULL;

    spapr_phb_dma_reset(sphb);
    spapr_phb_nvgpu_free(sphb);
    spapr_phb_nvgpu_setup(sphb, &errp);
    if (errp) {
        error_report_err(errp);
    }

    /* Reset the IOMMU state */
    object_child_foreach(OBJECT(qdev), spapr_phb_children_reset, NULL);

    if (spapr_phb_eeh_available(SPAPR_PCI_HOST_BRIDGE(qdev))) {
        spapr_phb_vfio_reset(qdev);
    }
}

static Property spapr_phb_properties[] = {
    DEFINE_PROP_UINT32("index", SpaprPhbState, index, -1),
    DEFINE_PROP_UINT64("mem_win_size", SpaprPhbState, mem_win_size,
                       SPAPR_PCI_MEM32_WIN_SIZE),
    DEFINE_PROP_UINT64("mem64_win_size", SpaprPhbState, mem64_win_size,
                       SPAPR_PCI_MEM64_WIN_SIZE),
    DEFINE_PROP_UINT64("io_win_size", SpaprPhbState, io_win_size,
                       SPAPR_PCI_IO_WIN_SIZE),
    DEFINE_PROP_BOOL("dynamic-reconfiguration", SpaprPhbState, dr_enabled,
                     true),
    /* Default DMA window is 0..1GB */
    DEFINE_PROP_UINT64("dma_win_addr", SpaprPhbState, dma_win_addr, 0),
    DEFINE_PROP_UINT64("dma_win_size", SpaprPhbState, dma_win_size, 0x40000000),
    DEFINE_PROP_UINT64("dma64_win_addr", SpaprPhbState, dma64_win_addr,
                       0x800000000000000ULL),
    DEFINE_PROP_BOOL("ddw", SpaprPhbState, ddw_enabled, true),
    DEFINE_PROP_UINT64("pgsz", SpaprPhbState, page_size_mask,
                       (1ULL << 12) | (1ULL << 16)),
    DEFINE_PROP_UINT32("numa_node", SpaprPhbState, numa_node, -1),
    DEFINE_PROP_BOOL("pre-2.8-migration", SpaprPhbState,
                     pre_2_8_migration, false),
    DEFINE_PROP_BOOL("pcie-extended-configuration-space", SpaprPhbState,
                     pcie_ecs, true),
    DEFINE_PROP_UINT64("gpa", SpaprPhbState, nv2_gpa_win_addr, 0),
    DEFINE_PROP_UINT64("atsd", SpaprPhbState, nv2_atsd_win_addr, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_spapr_pci_lsi = {
    .name = "spapr_pci/lsi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_EQUAL(irq, struct spapr_pci_lsi, NULL),

        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_spapr_pci_msi = {
    .name = "spapr_pci/msi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT32(key, spapr_pci_msi_mig),
        VMSTATE_UINT32(value.first_irq, spapr_pci_msi_mig),
        VMSTATE_UINT32(value.num, spapr_pci_msi_mig),
        VMSTATE_END_OF_LIST()
    },
};

static int spapr_pci_pre_save(void *opaque)
{
    SpaprPhbState *sphb = opaque;
    GHashTableIter iter;
    gpointer key, value;
    int i;

    if (sphb->pre_2_8_migration) {
        sphb->mig_liobn = sphb->dma_liobn[0];
        sphb->mig_mem_win_addr = sphb->mem_win_addr;
        sphb->mig_mem_win_size = sphb->mem_win_size;
        sphb->mig_io_win_addr = sphb->io_win_addr;
        sphb->mig_io_win_size = sphb->io_win_size;

        if ((sphb->mem64_win_size != 0)
            && (sphb->mem64_win_addr
                == (sphb->mem_win_addr + sphb->mem_win_size))) {
            sphb->mig_mem_win_size += sphb->mem64_win_size;
        }
    }

    g_free(sphb->msi_devs);
    sphb->msi_devs = NULL;
    sphb->msi_devs_num = g_hash_table_size(sphb->msi);
    if (!sphb->msi_devs_num) {
        return 0;
    }
    sphb->msi_devs = g_new(spapr_pci_msi_mig, sphb->msi_devs_num);

    g_hash_table_iter_init(&iter, sphb->msi);
    for (i = 0; g_hash_table_iter_next(&iter, &key, &value); ++i) {
        sphb->msi_devs[i].key = *(uint32_t *) key;
        sphb->msi_devs[i].value = *(spapr_pci_msi *) value;
    }

    return 0;
}

static int spapr_pci_post_load(void *opaque, int version_id)
{
    SpaprPhbState *sphb = opaque;
    gpointer key, value;
    int i;

    for (i = 0; i < sphb->msi_devs_num; ++i) {
        key = g_memdup(&sphb->msi_devs[i].key,
                       sizeof(sphb->msi_devs[i].key));
        value = g_memdup(&sphb->msi_devs[i].value,
                         sizeof(sphb->msi_devs[i].value));
        g_hash_table_insert(sphb->msi, key, value);
    }
    g_free(sphb->msi_devs);
    sphb->msi_devs = NULL;
    sphb->msi_devs_num = 0;

    return 0;
}

static bool pre_2_8_migration(void *opaque, int version_id)
{
    SpaprPhbState *sphb = opaque;

    return sphb->pre_2_8_migration;
}

static const VMStateDescription vmstate_spapr_pci = {
    .name = "spapr_pci",
    .version_id = 2,
    .minimum_version_id = 2,
    .pre_save = spapr_pci_pre_save,
    .post_load = spapr_pci_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_EQUAL(buid, SpaprPhbState, NULL),
        VMSTATE_UINT32_TEST(mig_liobn, SpaprPhbState, pre_2_8_migration),
        VMSTATE_UINT64_TEST(mig_mem_win_addr, SpaprPhbState, pre_2_8_migration),
        VMSTATE_UINT64_TEST(mig_mem_win_size, SpaprPhbState, pre_2_8_migration),
        VMSTATE_UINT64_TEST(mig_io_win_addr, SpaprPhbState, pre_2_8_migration),
        VMSTATE_UINT64_TEST(mig_io_win_size, SpaprPhbState, pre_2_8_migration),
        VMSTATE_STRUCT_ARRAY(lsi_table, SpaprPhbState, PCI_NUM_PINS, 0,
                             vmstate_spapr_pci_lsi, struct spapr_pci_lsi),
        VMSTATE_INT32(msi_devs_num, SpaprPhbState),
        VMSTATE_STRUCT_VARRAY_ALLOC(msi_devs, SpaprPhbState, msi_devs_num, 0,
                                    vmstate_spapr_pci_msi, spapr_pci_msi_mig),
        VMSTATE_END_OF_LIST()
    },
};

static const char *spapr_phb_root_bus_path(PCIHostState *host_bridge,
                                           PCIBus *rootbus)
{
    SpaprPhbState *sphb = SPAPR_PCI_HOST_BRIDGE(host_bridge);

    return sphb->dtbusname;
}

static void spapr_phb_class_init(ObjectClass *klass, void *data)
{
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    HotplugHandlerClass *hp = HOTPLUG_HANDLER_CLASS(klass);

    hc->root_bus_path = spapr_phb_root_bus_path;
    dc->realize = spapr_phb_realize;
    dc->unrealize = spapr_phb_unrealize;
    dc->props = spapr_phb_properties;
    dc->reset = spapr_phb_reset;
    dc->vmsd = &vmstate_spapr_pci;
    /* Supported by TYPE_SPAPR_MACHINE */
    dc->user_creatable = true;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    hp->plug = spapr_pci_plug;
    hp->unplug = spapr_pci_unplug;
    hp->unplug_request = spapr_pci_unplug_request;
}

static const TypeInfo spapr_phb_info = {
    .name          = TYPE_SPAPR_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(SpaprPhbState),
    .instance_finalize = spapr_phb_finalizefn,
    .class_init    = spapr_phb_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void spapr_phb_pci_enumerate_bridge(PCIBus *bus, PCIDevice *pdev,
                                           void *opaque)
{
    unsigned int *bus_no = opaque;
    PCIBus *sec_bus = NULL;

    if ((pci_default_read_config(pdev, PCI_HEADER_TYPE, 1) !=
         PCI_HEADER_TYPE_BRIDGE)) {
        return;
    }

    (*bus_no)++;
    pci_default_write_config(pdev, PCI_PRIMARY_BUS, pci_dev_bus_num(pdev), 1);
    pci_default_write_config(pdev, PCI_SECONDARY_BUS, *bus_no, 1);
    pci_default_write_config(pdev, PCI_SUBORDINATE_BUS, *bus_no, 1);

    sec_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));
    if (!sec_bus) {
        return;
    }

    pci_for_each_device(sec_bus, pci_bus_num(sec_bus),
                        spapr_phb_pci_enumerate_bridge, bus_no);
    pci_default_write_config(pdev, PCI_SUBORDINATE_BUS, *bus_no, 1);
}

static void spapr_phb_pci_enumerate(SpaprPhbState *phb)
{
    PCIBus *bus = PCI_HOST_BRIDGE(phb)->bus;
    unsigned int bus_no = 0;

    pci_for_each_device(bus, pci_bus_num(bus),
                        spapr_phb_pci_enumerate_bridge,
                        &bus_no);

}

int spapr_dt_phb(SpaprPhbState *phb, uint32_t intc_phandle, void *fdt,
                 uint32_t nr_msis, int *node_offset)
{
    int bus_off, i, j, ret;
    uint32_t bus_range[] = { cpu_to_be32(0), cpu_to_be32(0xff) };
    struct {
        uint32_t hi;
        uint64_t child;
        uint64_t parent;
        uint64_t size;
    } QEMU_PACKED ranges[] = {
        {
            cpu_to_be32(b_ss(1)), cpu_to_be64(0),
            cpu_to_be64(phb->io_win_addr),
            cpu_to_be64(memory_region_size(&phb->iospace)),
        },
        {
            cpu_to_be32(b_ss(2)), cpu_to_be64(SPAPR_PCI_MEM_WIN_BUS_OFFSET),
            cpu_to_be64(phb->mem_win_addr),
            cpu_to_be64(phb->mem_win_size),
        },
        {
            cpu_to_be32(b_ss(3)), cpu_to_be64(phb->mem64_win_pciaddr),
            cpu_to_be64(phb->mem64_win_addr),
            cpu_to_be64(phb->mem64_win_size),
        },
    };
    const unsigned sizeof_ranges =
        (phb->mem64_win_size ? 3 : 2) * sizeof(ranges[0]);
    uint64_t bus_reg[] = { cpu_to_be64(phb->buid), 0 };
    uint32_t interrupt_map_mask[] = {
        cpu_to_be32(b_ddddd(-1)|b_fff(0)), 0x0, 0x0, cpu_to_be32(-1)};
    uint32_t interrupt_map[PCI_SLOT_MAX * PCI_NUM_PINS][7];
    uint32_t ddw_applicable[] = {
        cpu_to_be32(RTAS_IBM_QUERY_PE_DMA_WINDOW),
        cpu_to_be32(RTAS_IBM_CREATE_PE_DMA_WINDOW),
        cpu_to_be32(RTAS_IBM_REMOVE_PE_DMA_WINDOW)
    };
    uint32_t ddw_extensions[] = {
        cpu_to_be32(1),
        cpu_to_be32(RTAS_IBM_RESET_PE_DMA_WINDOW)
    };
    uint32_t associativity[] = {cpu_to_be32(0x4),
                                cpu_to_be32(0x0),
                                cpu_to_be32(0x0),
                                cpu_to_be32(0x0),
                                cpu_to_be32(phb->numa_node)};
    SpaprTceTable *tcet;
    SpaprDrc *drc;
    Error *errp = NULL;

    /* Start populating the FDT */
    _FDT(bus_off = fdt_add_subnode(fdt, 0, phb->dtbusname));
    if (node_offset) {
        *node_offset = bus_off;
    }

    /* Write PHB properties */
    _FDT(fdt_setprop_string(fdt, bus_off, "device_type", "pci"));
    _FDT(fdt_setprop_string(fdt, bus_off, "compatible", "IBM,Logical_PHB"));
    _FDT(fdt_setprop_cell(fdt, bus_off, "#interrupt-cells", 0x1));
    _FDT(fdt_setprop(fdt, bus_off, "used-by-rtas", NULL, 0));
    _FDT(fdt_setprop(fdt, bus_off, "bus-range", &bus_range, sizeof(bus_range)));
    _FDT(fdt_setprop(fdt, bus_off, "ranges", &ranges, sizeof_ranges));
    _FDT(fdt_setprop(fdt, bus_off, "reg", &bus_reg, sizeof(bus_reg)));
    _FDT(fdt_setprop_cell(fdt, bus_off, "ibm,pci-config-space-type", 0x1));
    _FDT(fdt_setprop_cell(fdt, bus_off, "ibm,pe-total-#msi", nr_msis));

    /* Dynamic DMA window */
    if (phb->ddw_enabled) {
        _FDT(fdt_setprop(fdt, bus_off, "ibm,ddw-applicable", &ddw_applicable,
                         sizeof(ddw_applicable)));
        _FDT(fdt_setprop(fdt, bus_off, "ibm,ddw-extensions",
                         &ddw_extensions, sizeof(ddw_extensions)));
    }

    /* Advertise NUMA via ibm,associativity */
    if (phb->numa_node != -1) {
        _FDT(fdt_setprop(fdt, bus_off, "ibm,associativity", associativity,
                         sizeof(associativity)));
    }

    /* Build the interrupt-map, this must matches what is done
     * in pci_swizzle_map_irq_fn
     */
    _FDT(fdt_setprop(fdt, bus_off, "interrupt-map-mask",
                     &interrupt_map_mask, sizeof(interrupt_map_mask)));
    for (i = 0; i < PCI_SLOT_MAX; i++) {
        for (j = 0; j < PCI_NUM_PINS; j++) {
            uint32_t *irqmap = interrupt_map[i*PCI_NUM_PINS + j];
            int lsi_num = pci_swizzle(i, j);

            irqmap[0] = cpu_to_be32(b_ddddd(i)|b_fff(0));
            irqmap[1] = 0;
            irqmap[2] = 0;
            irqmap[3] = cpu_to_be32(j+1);
            irqmap[4] = cpu_to_be32(intc_phandle);
            spapr_dt_irq(&irqmap[5], phb->lsi_table[lsi_num].irq, true);
        }
    }
    /* Write interrupt map */
    _FDT(fdt_setprop(fdt, bus_off, "interrupt-map", &interrupt_map,
                     sizeof(interrupt_map)));

    tcet = spapr_tce_find_by_liobn(phb->dma_liobn[0]);
    if (!tcet) {
        return -1;
    }
    spapr_dma_dt(fdt, bus_off, "ibm,dma-window",
                 tcet->liobn, tcet->bus_offset,
                 tcet->nb_table << tcet->page_shift);

    drc = spapr_drc_by_id(TYPE_SPAPR_DRC_PHB, phb->index);
    if (drc) {
        uint32_t drc_index = cpu_to_be32(spapr_drc_index(drc));

        _FDT(fdt_setprop(fdt, bus_off, "ibm,my-drc-index", &drc_index,
                         sizeof(drc_index)));
    }

    /* Walk the bridges and program the bus numbers*/
    spapr_phb_pci_enumerate(phb);
    _FDT(fdt_setprop_cell(fdt, bus_off, "qemu,phb-enumerated", 0x1));

    /* Walk the bridge and subordinate buses */
    ret = spapr_dt_pci_bus(phb, PCI_HOST_BRIDGE(phb)->bus, fdt, bus_off);
    if (ret < 0) {
        return ret;
    }

    spapr_phb_nvgpu_populate_dt(phb, fdt, bus_off, &errp);
    if (errp) {
        error_report_err(errp);
    }
    spapr_phb_nvgpu_ram_populate_dt(phb, fdt);

    return 0;
}

void spapr_pci_rtas_init(void)
{
    spapr_rtas_register(RTAS_READ_PCI_CONFIG, "read-pci-config",
                        rtas_read_pci_config);
    spapr_rtas_register(RTAS_WRITE_PCI_CONFIG, "write-pci-config",
                        rtas_write_pci_config);
    spapr_rtas_register(RTAS_IBM_READ_PCI_CONFIG, "ibm,read-pci-config",
                        rtas_ibm_read_pci_config);
    spapr_rtas_register(RTAS_IBM_WRITE_PCI_CONFIG, "ibm,write-pci-config",
                        rtas_ibm_write_pci_config);
    if (msi_nonbroken) {
        spapr_rtas_register(RTAS_IBM_QUERY_INTERRUPT_SOURCE_NUMBER,
                            "ibm,query-interrupt-source-number",
                            rtas_ibm_query_interrupt_source_number);
        spapr_rtas_register(RTAS_IBM_CHANGE_MSI, "ibm,change-msi",
                            rtas_ibm_change_msi);
    }

    spapr_rtas_register(RTAS_IBM_SET_EEH_OPTION,
                        "ibm,set-eeh-option",
                        rtas_ibm_set_eeh_option);
    spapr_rtas_register(RTAS_IBM_GET_CONFIG_ADDR_INFO2,
                        "ibm,get-config-addr-info2",
                        rtas_ibm_get_config_addr_info2);
    spapr_rtas_register(RTAS_IBM_READ_SLOT_RESET_STATE2,
                        "ibm,read-slot-reset-state2",
                        rtas_ibm_read_slot_reset_state2);
    spapr_rtas_register(RTAS_IBM_SET_SLOT_RESET,
                        "ibm,set-slot-reset",
                        rtas_ibm_set_slot_reset);
    spapr_rtas_register(RTAS_IBM_CONFIGURE_PE,
                        "ibm,configure-pe",
                        rtas_ibm_configure_pe);
    spapr_rtas_register(RTAS_IBM_SLOT_ERROR_DETAIL,
                        "ibm,slot-error-detail",
                        rtas_ibm_slot_error_detail);
}

static void spapr_pci_register_types(void)
{
    type_register_static(&spapr_phb_info);
}

type_init(spapr_pci_register_types)

static int spapr_switch_one_vga(DeviceState *dev, void *opaque)
{
    bool be = *(bool *)opaque;

    if (object_dynamic_cast(OBJECT(dev), "VGA")
        || object_dynamic_cast(OBJECT(dev), "secondary-vga")) {
        object_property_set_bool(OBJECT(dev), be, "big-endian-framebuffer",
                                 &error_abort);
    }
    return 0;
}

void spapr_pci_switch_vga(bool big_endian)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    SpaprPhbState *sphb;

    /*
     * For backward compatibility with existing guests, we switch
     * the endianness of the VGA controller when changing the guest
     * interrupt mode
     */
    QLIST_FOREACH(sphb, &spapr->phbs, list) {
        BusState *bus = &PCI_HOST_BRIDGE(sphb)->bus->qbus;
        qbus_walk_children(bus, spapr_switch_one_vga, NULL, NULL, NULL,
                           &big_endian);
    }
}
