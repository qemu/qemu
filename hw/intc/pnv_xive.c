/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2017-2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "monitor/monitor.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_core.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_xive.h"
#include "hw/ppc/xive_regs.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/ppc.h"
#include "trace.h"

#include <libfdt.h>

#include "pnv_xive_regs.h"

#undef XIVE_DEBUG

/*
 * Virtual structures table (VST)
 */
#define SBE_PER_BYTE   4

typedef struct XiveVstInfo {
    const char *name;
    uint32_t    size;
    uint32_t    max_blocks;
} XiveVstInfo;

static const XiveVstInfo vst_infos[] = {
    [VST_TSEL_IVT]  = { "EAT",  sizeof(XiveEAS), 16 },
    [VST_TSEL_SBE]  = { "SBE",  1,               16 },
    [VST_TSEL_EQDT] = { "ENDT", sizeof(XiveEND), 16 },
    [VST_TSEL_VPDT] = { "VPDT", sizeof(XiveNVT), 32 },

    /*
     *  Interrupt fifo backing store table (not modeled) :
     *
     * 0 - IPI,
     * 1 - HWD,
     * 2 - First escalate,
     * 3 - Second escalate,
     * 4 - Redistribution,
     * 5 - IPI cascaded queue ?
     */
    [VST_TSEL_IRQ]  = { "IRQ",  1,               6  },
};

#define xive_error(xive, fmt, ...)                                      \
    qemu_log_mask(LOG_GUEST_ERROR, "XIVE[%x] - " fmt "\n",              \
                  (xive)->chip->chip_id, ## __VA_ARGS__);

/*
 * When PC_TCTXT_CHIPID_OVERRIDE is configured, the PC_TCTXT_CHIPID
 * field overrides the hardwired chip ID in the Powerbus operations
 * and for CAM compares
 */
static uint8_t pnv_xive_block_id(PnvXive *xive)
{
    uint8_t blk = xive->chip->chip_id;
    uint64_t cfg_val = xive->regs[PC_TCTXT_CFG >> 3];

    if (cfg_val & PC_TCTXT_CHIPID_OVERRIDE) {
        blk = GETFIELD(PC_TCTXT_CHIPID, cfg_val);
    }

    return blk;
}

/*
 * VST accessors for SBE, EAT, ENDT, NVT
 *
 * Indirect VST tables are arrays of VSDs pointing to a page (of same
 * size). Each page is a direct VST table.
 */

#define XIVE_VSD_SIZE 8

/* Indirect page size can be 4K, 64K, 2M, 16M. */
static uint64_t pnv_xive_vst_page_size_allowed(uint32_t page_shift)
{
     return page_shift == 12 || page_shift == 16 ||
         page_shift == 21 || page_shift == 24;
}

static uint64_t pnv_xive_vst_addr_direct(PnvXive *xive, uint32_t type,
                                         uint64_t vsd, uint32_t idx)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t vst_addr = vsd & VSD_ADDRESS_MASK;
    uint64_t vst_tsize = 1ull << (GETFIELD(VSD_TSIZE, vsd) + 12);
    uint32_t idx_max;

    idx_max = vst_tsize / info->size - 1;
    if (idx > idx_max) {
#ifdef XIVE_DEBUG
        xive_error(xive, "VST: %s entry %x out of range [ 0 .. %x ] !?",
                   info->name, idx, idx_max);
#endif
        return 0;
    }

    return vst_addr + idx * info->size;
}

static uint64_t pnv_xive_vst_addr_indirect(PnvXive *xive, uint32_t type,
                                           uint64_t vsd, uint32_t idx)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t vsd_addr;
    uint32_t vsd_idx;
    uint32_t page_shift;
    uint32_t vst_per_page;

    /* Get the page size of the indirect table. */
    vsd_addr = vsd & VSD_ADDRESS_MASK;
    if (ldq_be_dma(&address_space_memory, vsd_addr, &vsd,
                    MEMTXATTRS_UNSPECIFIED)) {
        xive_error(xive, "VST: failed to access %s entry %x @0x%" PRIx64,
                   info->name, idx, vsd_addr);
        return 0;
    }

    if (!(vsd & VSD_ADDRESS_MASK)) {
#ifdef XIVE_DEBUG
        xive_error(xive, "VST: invalid %s entry %x !?", info->name, idx);
#endif
        return 0;
    }

    page_shift = GETFIELD(VSD_TSIZE, vsd) + 12;

    if (!pnv_xive_vst_page_size_allowed(page_shift)) {
        xive_error(xive, "VST: invalid %s page shift %d", info->name,
                   page_shift);
        return 0;
    }

    vst_per_page = (1ull << page_shift) / info->size;
    vsd_idx = idx / vst_per_page;

    /* Load the VSD we are looking for, if not already done */
    if (vsd_idx) {
        vsd_addr = vsd_addr + vsd_idx * XIVE_VSD_SIZE;
        if (ldq_be_dma(&address_space_memory, vsd_addr, &vsd,
                       MEMTXATTRS_UNSPECIFIED)) {
            xive_error(xive, "VST: failed to access %s entry %x @0x%"
                       PRIx64, info->name, vsd_idx, vsd_addr);
            return 0;
        }

        if (!(vsd & VSD_ADDRESS_MASK)) {
#ifdef XIVE_DEBUG
            xive_error(xive, "VST: invalid %s entry %x !?", info->name, idx);
#endif
            return 0;
        }

        /*
         * Check that the pages have a consistent size across the
         * indirect table
         */
        if (page_shift != GETFIELD(VSD_TSIZE, vsd) + 12) {
            xive_error(xive, "VST: %s entry %x indirect page size differ !?",
                       info->name, idx);
            return 0;
        }
    }

    return pnv_xive_vst_addr_direct(xive, type, vsd, (idx % vst_per_page));
}

/*
 * This is a simplified model of operation forwarding on a remote IC.
 *
 * A PC MMIO address is built to identify the NVT structure. The load
 * on the remote IC will return the address of the structure in RAM,
 * which will then be used by pnv_xive_vst_write/read to perform the
 * RAM operation.
 */
static uint64_t pnv_xive_vst_addr_remote(PnvXive *xive, uint32_t type,
                                         uint64_t vsd, uint8_t blk,
                                         uint32_t idx)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t remote_addr = vsd & VSD_ADDRESS_MASK;
    uint64_t vst_addr;
    MemTxResult result;

    if (type != VST_TSEL_VPDT) {
        xive_error(xive, "VST: invalid access on remote VST %s %x/%x !?",
                   info->name, blk, idx);
        return 0;
    }

    remote_addr |= ((uint64_t)idx) << xive->pc_shift;

    vst_addr = address_space_ldq_be(&address_space_memory, remote_addr,
                                    MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        xive_error(xive, "VST: read failed at @0x%"  HWADDR_PRIx
                   " for NVT %x/%x\n", remote_addr, blk, idx);
        return 0;
    }

    return vst_addr;
}

static uint64_t pnv_xive_vst_addr(PnvXive *xive, uint32_t type, uint8_t blk,
                                  uint32_t idx)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t vsd;

    if (blk >= info->max_blocks) {
        xive_error(xive, "VST: invalid block id %d for VST %s %d !?",
                   blk, info->name, idx);
        return 0;
    }

    vsd = xive->vsds[type][blk];

    /* Remote VST access */
    if (GETFIELD(VSD_MODE, vsd) == VSD_MODE_FORWARD) {
        return pnv_xive_vst_addr_remote(xive, type, vsd, blk, idx);
    }

    if (VSD_INDIRECT & vsd) {
        return pnv_xive_vst_addr_indirect(xive, type, vsd, idx);
    }

    return pnv_xive_vst_addr_direct(xive, type, vsd, idx);
}

static int pnv_xive_vst_read(PnvXive *xive, uint32_t type, uint8_t blk,
                             uint32_t idx, void *data)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t addr = pnv_xive_vst_addr(xive, type, blk, idx);
    MemTxResult result;

    if (!addr) {
        return -1;
    }

    result = address_space_read(&address_space_memory, addr,
                                MEMTXATTRS_UNSPECIFIED, data,
                                info->size);
    if (result != MEMTX_OK) {
        xive_error(xive, "VST: read failed at @0x%" HWADDR_PRIx
                   " for VST %s %x/%x\n", addr, info->name, blk, idx);
        return -1;
    }
    return 0;
}

#define XIVE_VST_WORD_ALL -1

static int pnv_xive_vst_write(PnvXive *xive, uint32_t type, uint8_t blk,
                              uint32_t idx, void *data, uint32_t word_number)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t addr = pnv_xive_vst_addr(xive, type, blk, idx);
    MemTxResult result;

    if (!addr) {
        return -1;
    }

    if (word_number == XIVE_VST_WORD_ALL) {
        result = address_space_write(&address_space_memory, addr,
                                     MEMTXATTRS_UNSPECIFIED, data,
                                     info->size);
    } else {
        result = address_space_write(&address_space_memory,
                                     addr + word_number * 4,
                                     MEMTXATTRS_UNSPECIFIED,
                                     data + word_number * 4, 4);
    }

    if (result != MEMTX_OK) {
        xive_error(xive, "VST: write failed at @0x%" HWADDR_PRIx
                    "for VST %s %x/%x\n", addr, info->name, blk, idx);
        return -1;
    }
    return 0;
}

static int pnv_xive_get_end(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                            XiveEND *end)
{
    PnvXive *xive = PNV_XIVE(xrtr);

    if (pnv_xive_block_id(xive) != blk) {
        xive_error(xive, "VST: END %x/%x is remote !?", blk, idx);
        return -1;
    }

    return pnv_xive_vst_read(PNV_XIVE(xrtr), VST_TSEL_EQDT, blk, idx, end);
}

static int pnv_xive_write_end(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                              XiveEND *end, uint8_t word_number)
{
    PnvXive *xive = PNV_XIVE(xrtr);

    if (pnv_xive_block_id(xive) != blk) {
        xive_error(xive, "VST: END %x/%x is remote !?", blk, idx);
        return -1;
    }

    return pnv_xive_vst_write(PNV_XIVE(xrtr), VST_TSEL_EQDT, blk, idx, end,
                              word_number);
}

static int pnv_xive_end_update(PnvXive *xive)
{
    uint8_t  blk = GETFIELD(VC_EQC_CWATCH_BLOCKID,
                           xive->regs[(VC_EQC_CWATCH_SPEC >> 3)]);
    uint32_t idx = GETFIELD(VC_EQC_CWATCH_OFFSET,
                           xive->regs[(VC_EQC_CWATCH_SPEC >> 3)]);
    int i;
    uint64_t eqc_watch[4];

    for (i = 0; i < ARRAY_SIZE(eqc_watch); i++) {
        eqc_watch[i] = cpu_to_be64(xive->regs[(VC_EQC_CWATCH_DAT0 >> 3) + i]);
    }

    return pnv_xive_vst_write(xive, VST_TSEL_EQDT, blk, idx, eqc_watch,
                              XIVE_VST_WORD_ALL);
}

static void pnv_xive_end_cache_load(PnvXive *xive)
{
    uint8_t  blk = GETFIELD(VC_EQC_CWATCH_BLOCKID,
                           xive->regs[(VC_EQC_CWATCH_SPEC >> 3)]);
    uint32_t idx = GETFIELD(VC_EQC_CWATCH_OFFSET,
                           xive->regs[(VC_EQC_CWATCH_SPEC >> 3)]);
    uint64_t eqc_watch[4] = { 0 };
    int i;

    if (pnv_xive_vst_read(xive, VST_TSEL_EQDT, blk, idx, eqc_watch)) {
        xive_error(xive, "VST: no END entry %x/%x !?", blk, idx);
    }

    for (i = 0; i < ARRAY_SIZE(eqc_watch); i++) {
        xive->regs[(VC_EQC_CWATCH_DAT0 >> 3) + i] = be64_to_cpu(eqc_watch[i]);
    }
}

static int pnv_xive_get_nvt(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                            XiveNVT *nvt)
{
    return pnv_xive_vst_read(PNV_XIVE(xrtr), VST_TSEL_VPDT, blk, idx, nvt);
}

static int pnv_xive_write_nvt(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                              XiveNVT *nvt, uint8_t word_number)
{
    return pnv_xive_vst_write(PNV_XIVE(xrtr), VST_TSEL_VPDT, blk, idx, nvt,
                              word_number);
}

static int pnv_xive_nvt_update(PnvXive *xive)
{
    uint8_t  blk = GETFIELD(PC_VPC_CWATCH_BLOCKID,
                           xive->regs[(PC_VPC_CWATCH_SPEC >> 3)]);
    uint32_t idx = GETFIELD(PC_VPC_CWATCH_OFFSET,
                           xive->regs[(PC_VPC_CWATCH_SPEC >> 3)]);
    int i;
    uint64_t vpc_watch[8];

    for (i = 0; i < ARRAY_SIZE(vpc_watch); i++) {
        vpc_watch[i] = cpu_to_be64(xive->regs[(PC_VPC_CWATCH_DAT0 >> 3) + i]);
    }

    return pnv_xive_vst_write(xive, VST_TSEL_VPDT, blk, idx, vpc_watch,
                              XIVE_VST_WORD_ALL);
}

static void pnv_xive_nvt_cache_load(PnvXive *xive)
{
    uint8_t  blk = GETFIELD(PC_VPC_CWATCH_BLOCKID,
                           xive->regs[(PC_VPC_CWATCH_SPEC >> 3)]);
    uint32_t idx = GETFIELD(PC_VPC_CWATCH_OFFSET,
                           xive->regs[(PC_VPC_CWATCH_SPEC >> 3)]);
    uint64_t vpc_watch[8] = { 0 };
    int i;

    if (pnv_xive_vst_read(xive, VST_TSEL_VPDT, blk, idx, vpc_watch)) {
        xive_error(xive, "VST: no NVT entry %x/%x !?", blk, idx);
    }

    for (i = 0; i < ARRAY_SIZE(vpc_watch); i++) {
        xive->regs[(PC_VPC_CWATCH_DAT0 >> 3) + i] = be64_to_cpu(vpc_watch[i]);
    }
}

static int pnv_xive_get_eas(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                            XiveEAS *eas)
{
    PnvXive *xive = PNV_XIVE(xrtr);

    /*
     * EAT lookups should be local to the IC
     */
    if (pnv_xive_block_id(xive) != blk) {
        xive_error(xive, "VST: EAS %x is remote !?", XIVE_EAS(blk, idx));
        return -1;
    }

    return pnv_xive_vst_read(xive, VST_TSEL_IVT, blk, idx, eas);
}

static int pnv_xive_get_pq(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                           uint8_t *pq)
{
    PnvXive *xive = PNV_XIVE(xrtr);

    if (pnv_xive_block_id(xive) != blk) {
        xive_error(xive, "VST: EAS %x is remote !?", XIVE_EAS(blk, idx));
        return -1;
    }

    *pq = xive_source_esb_get(&xive->ipi_source, idx);
    return 0;
}

static int pnv_xive_set_pq(XiveRouter *xrtr, uint8_t blk, uint32_t idx,
                           uint8_t *pq)
{
    PnvXive *xive = PNV_XIVE(xrtr);

    if (pnv_xive_block_id(xive) != blk) {
        xive_error(xive, "VST: EAS %x is remote !?", XIVE_EAS(blk, idx));
        return -1;
    }

    *pq = xive_source_esb_set(&xive->ipi_source, idx, *pq);
    return 0;
}

/*
 * One bit per thread id. The first register PC_THREAD_EN_REG0 covers
 * the first cores 0-15 (normal) of the chip or 0-7 (fused). The
 * second register covers cores 16-23 (normal) or 8-11 (fused).
 */
static bool pnv_xive_is_cpu_enabled(PnvXive *xive, PowerPCCPU *cpu)
{
    int pir = ppc_cpu_pir(cpu);
    uint32_t fc = PNV9_PIR2FUSEDCORE(pir);
    uint64_t reg = fc < 8 ? PC_THREAD_EN_REG0 : PC_THREAD_EN_REG1;
    uint32_t bit = pir & 0x3f;

    return xive->regs[reg >> 3] & PPC_BIT(bit);
}

static int pnv_xive_match_nvt(XivePresenter *xptr, uint8_t format,
                              uint8_t nvt_blk, uint32_t nvt_idx,
                              bool cam_ignore, uint8_t priority,
                              uint32_t logic_serv, XiveTCTXMatch *match)
{
    PnvXive *xive = PNV_XIVE(xptr);
    PnvChip *chip = xive->chip;
    int count = 0;
    int i, j;

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pc = chip->cores[i];
        CPUCore *cc = CPU_CORE(pc);

        for (j = 0; j < cc->nr_threads; j++) {
            PowerPCCPU *cpu = pc->threads[j];
            XiveTCTX *tctx;
            int ring;

            if (!pnv_xive_is_cpu_enabled(xive, cpu)) {
                continue;
            }

            tctx = XIVE_TCTX(pnv_cpu_state(cpu)->intc);

            /*
             * Check the thread context CAM lines and record matches.
             */
            ring = xive_presenter_tctx_match(xptr, tctx, format, nvt_blk,
                                             nvt_idx, cam_ignore, logic_serv);
            /*
             * Save the context and follow on to catch duplicates, that we
             * don't support yet.
             */
            if (ring != -1) {
                if (match->tctx) {
                    qemu_log_mask(LOG_GUEST_ERROR, "XIVE: already found a "
                                  "thread context NVT %x/%x\n",
                                  nvt_blk, nvt_idx);
                    return -1;
                }

                match->ring = ring;
                match->tctx = tctx;
                count++;
            }
        }
    }

    return count;
}

static uint32_t pnv_xive_presenter_get_config(XivePresenter *xptr)
{
    uint32_t cfg = 0;

    /* TIMA GEN1 is all P9 knows */
    cfg |= XIVE_PRESENTER_GEN1_TIMA_OS;

    return cfg;
}

static uint8_t pnv_xive_get_block_id(XiveRouter *xrtr)
{
    return pnv_xive_block_id(PNV_XIVE(xrtr));
}

/*
 * The TIMA MMIO space is shared among the chips and to identify the
 * chip from which the access is being done, we extract the chip id
 * from the PIR.
 */
static PnvXive *pnv_xive_tm_get_xive(PowerPCCPU *cpu)
{
    int pir = ppc_cpu_pir(cpu);
    XivePresenter *xptr = XIVE_TCTX(pnv_cpu_state(cpu)->intc)->xptr;
    PnvXive *xive = PNV_XIVE(xptr);

    if (!pnv_xive_is_cpu_enabled(xive, cpu)) {
        xive_error(xive, "IC: CPU %x is not enabled", pir);
    }
    return xive;
}

/*
 * The internal sources (IPIs) of the interrupt controller have no
 * knowledge of the XIVE chip on which they reside. Encode the block
 * id in the source interrupt number before forwarding the source
 * event notification to the Router. This is required on a multichip
 * system.
 */
static void pnv_xive_notify(XiveNotifier *xn, uint32_t srcno, bool pq_checked)
{
    PnvXive *xive = PNV_XIVE(xn);
    uint8_t blk = pnv_xive_block_id(xive);

    xive_router_notify(xn, XIVE_EAS(blk, srcno), pq_checked);
}

/*
 * XIVE helpers
 */

static uint64_t pnv_xive_vc_size(PnvXive *xive)
{
    return (~xive->regs[CQ_VC_BARM >> 3] + 1) & CQ_VC_BARM_MASK;
}

static uint64_t pnv_xive_edt_shift(PnvXive *xive)
{
    return ctz64(pnv_xive_vc_size(xive) / XIVE_TABLE_EDT_MAX);
}

static uint64_t pnv_xive_pc_size(PnvXive *xive)
{
    return (~xive->regs[CQ_PC_BARM >> 3] + 1) & CQ_PC_BARM_MASK;
}

static uint32_t pnv_xive_nr_ipis(PnvXive *xive, uint8_t blk)
{
    uint64_t vsd = xive->vsds[VST_TSEL_SBE][blk];
    uint64_t vst_tsize = 1ull << (GETFIELD(VSD_TSIZE, vsd) + 12);

    return VSD_INDIRECT & vsd ? 0 : vst_tsize * SBE_PER_BYTE;
}

/*
 * Compute the number of entries per indirect subpage.
 */
static uint64_t pnv_xive_vst_per_subpage(PnvXive *xive, uint32_t type)
{
    uint8_t blk = pnv_xive_block_id(xive);
    uint64_t vsd = xive->vsds[type][blk];
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t vsd_addr;
    uint32_t page_shift;

    /* For direct tables, fake a valid value */
    if (!(VSD_INDIRECT & vsd)) {
        return 1;
    }

    /* Get the page size of the indirect table. */
    vsd_addr = vsd & VSD_ADDRESS_MASK;
    if (ldq_be_dma(&address_space_memory, vsd_addr, &vsd,
                   MEMTXATTRS_UNSPECIFIED)) {
        xive_error(xive, "VST: failed to access %s entry @0x%" PRIx64,
                   info->name, vsd_addr);
        return 0;
    }

    if (!(vsd & VSD_ADDRESS_MASK)) {
#ifdef XIVE_DEBUG
        xive_error(xive, "VST: invalid %s entry %x !?", info->name, idx);
#endif
        return 0;
    }

    page_shift = GETFIELD(VSD_TSIZE, vsd) + 12;

    if (!pnv_xive_vst_page_size_allowed(page_shift)) {
        xive_error(xive, "VST: invalid %s page shift %d", info->name,
                   page_shift);
        return 0;
    }

    return (1ull << page_shift) / info->size;
}

/*
 * EDT Table
 *
 * The Virtualization Controller MMIO region containing the IPI ESB
 * pages and END ESB pages is sub-divided into "sets" which map
 * portions of the VC region to the different ESB pages. It is
 * configured at runtime through the EDT "Domain Table" to let the
 * firmware decide how to split the VC address space between IPI ESB
 * pages and END ESB pages.
 */

/*
 * Computes the overall size of the IPI or the END ESB pages
 */
static uint64_t pnv_xive_edt_size(PnvXive *xive, uint64_t type)
{
    uint64_t edt_size = 1ull << pnv_xive_edt_shift(xive);
    uint64_t size = 0;
    int i;

    for (i = 0; i < XIVE_TABLE_EDT_MAX; i++) {
        uint64_t edt_type = GETFIELD(CQ_TDR_EDT_TYPE, xive->edt[i]);

        if (edt_type == type) {
            size += edt_size;
        }
    }

    return size;
}

/*
 * Maps an offset of the VC region in the IPI or END region using the
 * layout defined by the EDT "Domaine Table"
 */
static uint64_t pnv_xive_edt_offset(PnvXive *xive, uint64_t vc_offset,
                                              uint64_t type)
{
    int i;
    uint64_t edt_size = 1ull << pnv_xive_edt_shift(xive);
    uint64_t edt_offset = vc_offset;

    for (i = 0; i < XIVE_TABLE_EDT_MAX && (i * edt_size) < vc_offset; i++) {
        uint64_t edt_type = GETFIELD(CQ_TDR_EDT_TYPE, xive->edt[i]);

        if (edt_type != type) {
            edt_offset -= edt_size;
        }
    }

    return edt_offset;
}

static void pnv_xive_edt_resize(PnvXive *xive)
{
    uint64_t ipi_edt_size = pnv_xive_edt_size(xive, CQ_TDR_EDT_IPI);
    uint64_t end_edt_size = pnv_xive_edt_size(xive, CQ_TDR_EDT_EQ);

    memory_region_set_size(&xive->ipi_edt_mmio, ipi_edt_size);
    memory_region_add_subregion(&xive->ipi_mmio, 0, &xive->ipi_edt_mmio);

    memory_region_set_size(&xive->end_edt_mmio, end_edt_size);
    memory_region_add_subregion(&xive->end_mmio, 0, &xive->end_edt_mmio);
}

/*
 * XIVE Table configuration. Only EDT is supported.
 */
static int pnv_xive_table_set_data(PnvXive *xive, uint64_t val)
{
    uint64_t tsel = xive->regs[CQ_TAR >> 3] & CQ_TAR_TSEL;
    uint8_t tsel_index = GETFIELD(CQ_TAR_TSEL_INDEX, xive->regs[CQ_TAR >> 3]);
    uint64_t *xive_table;
    uint8_t max_index;

    switch (tsel) {
    case CQ_TAR_TSEL_BLK:
        max_index = ARRAY_SIZE(xive->blk);
        xive_table = xive->blk;
        break;
    case CQ_TAR_TSEL_MIG:
        max_index = ARRAY_SIZE(xive->mig);
        xive_table = xive->mig;
        break;
    case CQ_TAR_TSEL_EDT:
        max_index = ARRAY_SIZE(xive->edt);
        xive_table = xive->edt;
        break;
    case CQ_TAR_TSEL_VDT:
        max_index = ARRAY_SIZE(xive->vdt);
        xive_table = xive->vdt;
        break;
    default:
        xive_error(xive, "IC: invalid table %d", (int) tsel);
        return -1;
    }

    if (tsel_index >= max_index) {
        xive_error(xive, "IC: invalid index %d", (int) tsel_index);
        return -1;
    }

    xive_table[tsel_index] = val;

    if (xive->regs[CQ_TAR >> 3] & CQ_TAR_TBL_AUTOINC) {
        xive->regs[CQ_TAR >> 3] =
            SETFIELD(CQ_TAR_TSEL_INDEX, xive->regs[CQ_TAR >> 3], ++tsel_index);
    }

    /*
     * EDT configuration is complete. Resize the MMIO windows exposing
     * the IPI and the END ESBs in the VC region.
     */
    if (tsel == CQ_TAR_TSEL_EDT && tsel_index == ARRAY_SIZE(xive->edt)) {
        pnv_xive_edt_resize(xive);
    }

    return 0;
}

/*
 * Virtual Structure Tables (VST) configuration
 */
static void pnv_xive_vst_set_exclusive(PnvXive *xive, uint8_t type,
                                       uint8_t blk, uint64_t vsd)
{
    XiveENDSource *end_xsrc = &xive->end_source;
    XiveSource *xsrc = &xive->ipi_source;
    const XiveVstInfo *info = &vst_infos[type];
    uint32_t page_shift = GETFIELD(VSD_TSIZE, vsd) + 12;
    uint64_t vst_tsize = 1ull << page_shift;
    uint64_t vst_addr = vsd & VSD_ADDRESS_MASK;

    /* Basic checks */

    if (VSD_INDIRECT & vsd) {
        if (!(xive->regs[VC_GLOBAL_CONFIG >> 3] & VC_GCONF_INDIRECT)) {
            xive_error(xive, "VST: %s indirect tables are not enabled",
                       info->name);
            return;
        }

        if (!pnv_xive_vst_page_size_allowed(page_shift)) {
            xive_error(xive, "VST: invalid %s page shift %d", info->name,
                       page_shift);
            return;
        }
    }

    if (!QEMU_IS_ALIGNED(vst_addr, 1ull << page_shift)) {
        xive_error(xive, "VST: %s table address 0x%"PRIx64" is not aligned with"
                   " page shift %d", info->name, vst_addr, page_shift);
        return;
    }

    /* Record the table configuration (in SRAM on HW) */
    xive->vsds[type][blk] = vsd;

    /* Now tune the models with the configuration provided by the FW */

    switch (type) {
    case VST_TSEL_IVT:  /* Nothing to be done */
        break;

    case VST_TSEL_EQDT:
        /*
         * Backing store pages for the END.
         *
         * If the table is direct, we can compute the number of PQ
         * entries provisioned by FW (such as skiboot) and resize the
         * END ESB window accordingly.
         */
        if (!(VSD_INDIRECT & vsd)) {
            memory_region_set_size(&end_xsrc->esb_mmio, (vst_tsize / info->size)
                                   * (1ull << xsrc->esb_shift));
        }
        memory_region_add_subregion(&xive->end_edt_mmio, 0,
                                    &end_xsrc->esb_mmio);
        break;

    case VST_TSEL_SBE:
        /*
         * Backing store pages for the source PQ bits. The model does
         * not use these PQ bits backed in RAM because the XiveSource
         * model has its own.
         *
         * If the table is direct, we can compute the number of PQ
         * entries provisioned by FW (such as skiboot) and resize the
         * ESB window accordingly.
         */
        if (!(VSD_INDIRECT & vsd)) {
            memory_region_set_size(&xsrc->esb_mmio, vst_tsize * SBE_PER_BYTE
                                   * (1ull << xsrc->esb_shift));
        }
        memory_region_add_subregion(&xive->ipi_edt_mmio, 0, &xsrc->esb_mmio);
        break;

    case VST_TSEL_VPDT: /* Not modeled */
    case VST_TSEL_IRQ:  /* Not modeled */
        /*
         * These tables contains the backing store pages for the
         * interrupt fifos of the VC sub-engine in case of overflow.
         */
        break;

    default:
        g_assert_not_reached();
    }
}

/*
 * Both PC and VC sub-engines are configured as each use the Virtual
 * Structure Tables : SBE, EAS, END and NVT.
 */
static void pnv_xive_vst_set_data(PnvXive *xive, uint64_t vsd, bool pc_engine)
{
    uint8_t mode = GETFIELD(VSD_MODE, vsd);
    uint8_t type = GETFIELD(VST_TABLE_SELECT,
                            xive->regs[VC_VSD_TABLE_ADDR >> 3]);
    uint8_t blk = GETFIELD(VST_TABLE_BLOCK,
                           xive->regs[VC_VSD_TABLE_ADDR >> 3]);
    uint64_t vst_addr = vsd & VSD_ADDRESS_MASK;

    if (type > VST_TSEL_IRQ) {
        xive_error(xive, "VST: invalid table type %d", type);
        return;
    }

    if (blk >= vst_infos[type].max_blocks) {
        xive_error(xive, "VST: invalid block id %d for"
                      " %s table", blk, vst_infos[type].name);
        return;
    }

    /*
     * Only take the VC sub-engine configuration into account because
     * the XiveRouter model combines both VC and PC sub-engines
     */
    if (pc_engine) {
        return;
    }

    if (!vst_addr) {
        xive_error(xive, "VST: invalid %s table address", vst_infos[type].name);
        return;
    }

    switch (mode) {
    case VSD_MODE_FORWARD:
        xive->vsds[type][blk] = vsd;
        break;

    case VSD_MODE_EXCLUSIVE:
        pnv_xive_vst_set_exclusive(xive, type, blk, vsd);
        break;

    default:
        xive_error(xive, "VST: unsupported table mode %d", mode);
        return;
    }
}

/*
 * Interrupt controller MMIO region. The layout is compatible between
 * 4K and 64K pages :
 *
 * Page 0           sub-engine BARs
 *  0x000 - 0x3FF   IC registers
 *  0x400 - 0x7FF   PC registers
 *  0x800 - 0xFFF   VC registers
 *
 * Page 1           Notify page (writes only)
 *  0x000 - 0x7FF   HW interrupt triggers (PSI, PHB)
 *  0x800 - 0xFFF   forwards and syncs
 *
 * Page 2           LSI Trigger page (writes only) (not modeled)
 * Page 3           LSI SB EOI page (reads only) (not modeled)
 *
 * Page 4-7         indirect TIMA
 */

/*
 * IC - registers MMIO
 */
static void pnv_xive_ic_reg_write(void *opaque, hwaddr offset,
                                  uint64_t val, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);
    MemoryRegion *sysmem = get_system_memory();
    uint32_t reg = offset >> 3;
    bool is_chip0 = xive->chip->chip_id == 0;

    switch (offset) {

    /*
     * XIVE CQ (PowerBus bridge) settings
     */
    case CQ_MSGSND:     /* msgsnd for doorbells */
    case CQ_FIRMASK_OR: /* FIR error reporting */
        break;
    case CQ_PBI_CTL:
        if (val & CQ_PBI_PC_64K) {
            xive->pc_shift = 16;
        }
        if (val & CQ_PBI_VC_64K) {
            xive->vc_shift = 16;
        }
        break;
    case CQ_CFG_PB_GEN: /* PowerBus General Configuration */
        /*
         * TODO: CQ_INT_ADDR_OPT for 1-block-per-chip mode
         */
        break;

    /*
     * XIVE Virtualization Controller settings
     */
    case VC_GLOBAL_CONFIG:
        break;

    /*
     * XIVE Presenter Controller settings
     */
    case PC_GLOBAL_CONFIG:
        /*
         * PC_GCONF_CHIPID_OVR
         *   Overrides Int command Chip ID with the Chip ID field (DEBUG)
         */
        break;
    case PC_TCTXT_CFG:
        /*
         * TODO: block group support
         */
        break;
    case PC_TCTXT_TRACK:
        /*
         * PC_TCTXT_TRACK_EN:
         *   enable block tracking and exchange of block ownership
         *   information between Interrupt controllers
         */
        break;

    /*
     * Misc settings
     */
    case VC_SBC_CONFIG: /* Store EOI configuration */
        /*
         * Configure store EOI if required by firmware (skiboot has removed
         * support recently though)
         */
        if (val & (VC_SBC_CONF_CPLX_CIST | VC_SBC_CONF_CIST_BOTH)) {
            xive->ipi_source.esb_flags |= XIVE_SRC_STORE_EOI;
        }
        break;

    case VC_EQC_CONFIG: /* TODO: silent escalation */
    case VC_AIB_TX_ORDER_TAG2: /* relax ordering */
        break;

    /*
     * XIVE BAR settings (XSCOM only)
     */
    case CQ_RST_CTL:
        /* bit4: resets all BAR registers */
        break;

    case CQ_IC_BAR: /* IC BAR. 8 pages */
        xive->ic_shift = val & CQ_IC_BAR_64K ? 16 : 12;
        if (!(val & CQ_IC_BAR_VALID)) {
            xive->ic_base = 0;
            if (xive->regs[reg] & CQ_IC_BAR_VALID) {
                memory_region_del_subregion(&xive->ic_mmio,
                                            &xive->ic_reg_mmio);
                memory_region_del_subregion(&xive->ic_mmio,
                                            &xive->ic_notify_mmio);
                memory_region_del_subregion(&xive->ic_mmio,
                                            &xive->ic_lsi_mmio);
                memory_region_del_subregion(&xive->ic_mmio,
                                            &xive->tm_indirect_mmio);

                memory_region_del_subregion(sysmem, &xive->ic_mmio);
            }
        } else {
            xive->ic_base = val & ~(CQ_IC_BAR_VALID | CQ_IC_BAR_64K);
            if (!(xive->regs[reg] & CQ_IC_BAR_VALID)) {
                memory_region_add_subregion(sysmem, xive->ic_base,
                                            &xive->ic_mmio);

                memory_region_add_subregion(&xive->ic_mmio,  0,
                                            &xive->ic_reg_mmio);
                memory_region_add_subregion(&xive->ic_mmio,
                                            1ul << xive->ic_shift,
                                            &xive->ic_notify_mmio);
                memory_region_add_subregion(&xive->ic_mmio,
                                            2ul << xive->ic_shift,
                                            &xive->ic_lsi_mmio);
                memory_region_add_subregion(&xive->ic_mmio,
                                            4ull << xive->ic_shift,
                                            &xive->tm_indirect_mmio);
            }
        }
        break;

    case CQ_TM1_BAR: /* TM BAR. 4 pages. Map only once */
    case CQ_TM2_BAR: /* second TM BAR. for hotplug. Not modeled */
        xive->tm_shift = val & CQ_TM_BAR_64K ? 16 : 12;
        if (!(val & CQ_TM_BAR_VALID)) {
            xive->tm_base = 0;
            if (xive->regs[reg] & CQ_TM_BAR_VALID && is_chip0) {
                memory_region_del_subregion(sysmem, &xive->tm_mmio);
            }
        } else {
            xive->tm_base = val & ~(CQ_TM_BAR_VALID | CQ_TM_BAR_64K);
            if (!(xive->regs[reg] & CQ_TM_BAR_VALID) && is_chip0) {
                memory_region_add_subregion(sysmem, xive->tm_base,
                                            &xive->tm_mmio);
            }
        }
        break;

    case CQ_PC_BARM:
        xive->regs[reg] = val;
        memory_region_set_size(&xive->pc_mmio, pnv_xive_pc_size(xive));
        break;
    case CQ_PC_BAR: /* From 32M to 512G */
        if (!(val & CQ_PC_BAR_VALID)) {
            xive->pc_base = 0;
            if (xive->regs[reg] & CQ_PC_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->pc_mmio);
            }
        } else {
            xive->pc_base = val & ~(CQ_PC_BAR_VALID);
            if (!(xive->regs[reg] & CQ_PC_BAR_VALID)) {
                memory_region_add_subregion(sysmem, xive->pc_base,
                                            &xive->pc_mmio);
            }
        }
        break;

    case CQ_VC_BARM:
        xive->regs[reg] = val;
        memory_region_set_size(&xive->vc_mmio, pnv_xive_vc_size(xive));
        break;
    case CQ_VC_BAR: /* From 64M to 4TB */
        if (!(val & CQ_VC_BAR_VALID)) {
            xive->vc_base = 0;
            if (xive->regs[reg] & CQ_VC_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->vc_mmio);
            }
        } else {
            xive->vc_base = val & ~(CQ_VC_BAR_VALID);
            if (!(xive->regs[reg] & CQ_VC_BAR_VALID)) {
                memory_region_add_subregion(sysmem, xive->vc_base,
                                            &xive->vc_mmio);
            }
        }
        break;

    /*
     * XIVE Table settings.
     */
    case CQ_TAR: /* Table Address */
        break;
    case CQ_TDR: /* Table Data */
        pnv_xive_table_set_data(xive, val);
        break;

    /*
     * XIVE VC & PC Virtual Structure Table settings
     */
    case VC_VSD_TABLE_ADDR:
    case PC_VSD_TABLE_ADDR: /* Virtual table selector */
        break;
    case VC_VSD_TABLE_DATA: /* Virtual table setting */
    case PC_VSD_TABLE_DATA:
        pnv_xive_vst_set_data(xive, val, offset == PC_VSD_TABLE_DATA);
        break;

    /*
     * Interrupt fifo overflow in memory backing store (Not modeled)
     */
    case VC_IRQ_CONFIG_IPI:
    case VC_IRQ_CONFIG_HW:
    case VC_IRQ_CONFIG_CASCADE1:
    case VC_IRQ_CONFIG_CASCADE2:
    case VC_IRQ_CONFIG_REDIST:
    case VC_IRQ_CONFIG_IPI_CASC:
        break;

    /*
     * XIVE hardware thread enablement
     */
    case PC_THREAD_EN_REG0: /* Physical Thread Enable */
    case PC_THREAD_EN_REG1: /* Physical Thread Enable (fused core) */
        break;

    case PC_THREAD_EN_REG0_SET:
        xive->regs[PC_THREAD_EN_REG0 >> 3] |= val;
        break;
    case PC_THREAD_EN_REG1_SET:
        xive->regs[PC_THREAD_EN_REG1 >> 3] |= val;
        break;
    case PC_THREAD_EN_REG0_CLR:
        xive->regs[PC_THREAD_EN_REG0 >> 3] &= ~val;
        break;
    case PC_THREAD_EN_REG1_CLR:
        xive->regs[PC_THREAD_EN_REG1 >> 3] &= ~val;
        break;

    /*
     * Indirect TIMA access set up. Defines the PIR of the HW thread
     * to use.
     */
    case PC_TCTXT_INDIR0 ... PC_TCTXT_INDIR3:
        break;

    /*
     * XIVE PC & VC cache updates for EAS, NVT and END
     */
    case VC_IVC_SCRUB_MASK:
    case VC_IVC_SCRUB_TRIG:
        break;

    case VC_EQC_CWATCH_SPEC:
        val &= ~VC_EQC_CWATCH_CONFLICT; /* HW resets this bit */
        break;
    case VC_EQC_CWATCH_DAT1 ... VC_EQC_CWATCH_DAT3:
        break;
    case VC_EQC_CWATCH_DAT0:
        /* writing to DATA0 triggers the cache write */
        xive->regs[reg] = val;
        pnv_xive_end_update(xive);
        break;
    case VC_EQC_SCRUB_MASK:
    case VC_EQC_SCRUB_TRIG:
        /*
         * The scrubbing registers flush the cache in RAM and can also
         * invalidate.
         */
        break;

    case PC_VPC_CWATCH_SPEC:
        val &= ~PC_VPC_CWATCH_CONFLICT; /* HW resets this bit */
        break;
    case PC_VPC_CWATCH_DAT1 ... PC_VPC_CWATCH_DAT7:
        break;
    case PC_VPC_CWATCH_DAT0:
        /* writing to DATA0 triggers the cache write */
        xive->regs[reg] = val;
        pnv_xive_nvt_update(xive);
        break;
    case PC_VPC_SCRUB_MASK:
    case PC_VPC_SCRUB_TRIG:
        /*
         * The scrubbing registers flush the cache in RAM and can also
         * invalidate.
         */
        break;


    /*
     * XIVE PC & VC cache invalidation
     */
    case PC_AT_KILL:
        break;
    case VC_AT_MACRO_KILL:
        break;
    case PC_AT_KILL_MASK:
    case VC_AT_MACRO_KILL_MASK:
        break;

    default:
        xive_error(xive, "IC: invalid write to reg=0x%"HWADDR_PRIx, offset);
        return;
    }

    xive->regs[reg] = val;
}

static uint64_t pnv_xive_ic_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);
    uint64_t val = 0;
    uint32_t reg = offset >> 3;

    switch (offset) {
    case CQ_CFG_PB_GEN:
    case CQ_IC_BAR:
    case CQ_TM1_BAR:
    case CQ_TM2_BAR:
    case CQ_PC_BAR:
    case CQ_PC_BARM:
    case CQ_VC_BAR:
    case CQ_VC_BARM:
    case CQ_TAR:
    case CQ_TDR:
    case CQ_PBI_CTL:

    case PC_TCTXT_CFG:
    case PC_TCTXT_TRACK:
    case PC_TCTXT_INDIR0:
    case PC_TCTXT_INDIR1:
    case PC_TCTXT_INDIR2:
    case PC_TCTXT_INDIR3:
    case PC_GLOBAL_CONFIG:

    case PC_VPC_SCRUB_MASK:

    case VC_GLOBAL_CONFIG:
    case VC_AIB_TX_ORDER_TAG2:

    case VC_IRQ_CONFIG_IPI:
    case VC_IRQ_CONFIG_HW:
    case VC_IRQ_CONFIG_CASCADE1:
    case VC_IRQ_CONFIG_CASCADE2:
    case VC_IRQ_CONFIG_REDIST:
    case VC_IRQ_CONFIG_IPI_CASC:

    case VC_EQC_SCRUB_MASK:
    case VC_IVC_SCRUB_MASK:
    case VC_SBC_CONFIG:
    case VC_AT_MACRO_KILL_MASK:
    case VC_VSD_TABLE_ADDR:
    case PC_VSD_TABLE_ADDR:
    case VC_VSD_TABLE_DATA:
    case PC_VSD_TABLE_DATA:
    case PC_THREAD_EN_REG0:
    case PC_THREAD_EN_REG1:
        val = xive->regs[reg];
        break;

    /*
     * XIVE hardware thread enablement
     */
    case PC_THREAD_EN_REG0_SET:
    case PC_THREAD_EN_REG0_CLR:
        val = xive->regs[PC_THREAD_EN_REG0 >> 3];
        break;
    case PC_THREAD_EN_REG1_SET:
    case PC_THREAD_EN_REG1_CLR:
        val = xive->regs[PC_THREAD_EN_REG1 >> 3];
        break;

    case CQ_MSGSND: /* Identifies which cores have msgsnd enabled. */
        val = 0xffffff0000000000;
        break;

    /*
     * XIVE PC & VC cache updates for EAS, NVT and END
     */
    case VC_EQC_CWATCH_SPEC:
        xive->regs[reg] = ~(VC_EQC_CWATCH_FULL | VC_EQC_CWATCH_CONFLICT);
        val = xive->regs[reg];
        break;
    case VC_EQC_CWATCH_DAT0:
        /*
         * Load DATA registers from cache with data requested by the
         * SPEC register
         */
        pnv_xive_end_cache_load(xive);
        val = xive->regs[reg];
        break;
    case VC_EQC_CWATCH_DAT1 ... VC_EQC_CWATCH_DAT3:
        val = xive->regs[reg];
        break;

    case PC_VPC_CWATCH_SPEC:
        xive->regs[reg] = ~(PC_VPC_CWATCH_FULL | PC_VPC_CWATCH_CONFLICT);
        val = xive->regs[reg];
        break;
    case PC_VPC_CWATCH_DAT0:
        /*
         * Load DATA registers from cache with data requested by the
         * SPEC register
         */
        pnv_xive_nvt_cache_load(xive);
        val = xive->regs[reg];
        break;
    case PC_VPC_CWATCH_DAT1 ... PC_VPC_CWATCH_DAT7:
        val = xive->regs[reg];
        break;

    case PC_VPC_SCRUB_TRIG:
    case VC_IVC_SCRUB_TRIG:
    case VC_EQC_SCRUB_TRIG:
        xive->regs[reg] &= ~VC_SCRUB_VALID;
        val = xive->regs[reg];
        break;

    /*
     * XIVE PC & VC cache invalidation
     */
    case PC_AT_KILL:
        xive->regs[reg] &= ~PC_AT_KILL_VALID;
        val = xive->regs[reg];
        break;
    case VC_AT_MACRO_KILL:
        xive->regs[reg] &= ~VC_KILL_VALID;
        val = xive->regs[reg];
        break;

    /*
     * XIVE synchronisation
     */
    case VC_EQC_CONFIG:
        val = VC_EQC_SYNC_MASK;
        break;

    default:
        xive_error(xive, "IC: invalid read reg=0x%"HWADDR_PRIx, offset);
    }

    return val;
}

static const MemoryRegionOps pnv_xive_ic_reg_ops = {
    .read = pnv_xive_ic_reg_read,
    .write = pnv_xive_ic_reg_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/*
 * IC - Notify MMIO port page (write only)
 */
#define PNV_XIVE_FORWARD_IPI        0x800 /* Forward IPI */
#define PNV_XIVE_FORWARD_HW         0x880 /* Forward HW */
#define PNV_XIVE_FORWARD_OS_ESC     0x900 /* Forward OS escalation */
#define PNV_XIVE_FORWARD_HW_ESC     0x980 /* Forward Hyp escalation */
#define PNV_XIVE_FORWARD_REDIS      0xa00 /* Forward Redistribution */
#define PNV_XIVE_RESERVED5          0xa80 /* Cache line 5 PowerBUS operation */
#define PNV_XIVE_RESERVED6          0xb00 /* Cache line 6 PowerBUS operation */
#define PNV_XIVE_RESERVED7          0xb80 /* Cache line 7 PowerBUS operation */

/* VC synchronisation */
#define PNV_XIVE_SYNC_IPI           0xc00 /* Sync IPI */
#define PNV_XIVE_SYNC_HW            0xc80 /* Sync HW */
#define PNV_XIVE_SYNC_OS_ESC        0xd00 /* Sync OS escalation */
#define PNV_XIVE_SYNC_HW_ESC        0xd80 /* Sync Hyp escalation */
#define PNV_XIVE_SYNC_REDIS         0xe00 /* Sync Redistribution */

/* PC synchronisation */
#define PNV_XIVE_SYNC_PULL          0xe80 /* Sync pull context */
#define PNV_XIVE_SYNC_PUSH          0xf00 /* Sync push context */
#define PNV_XIVE_SYNC_VPC           0xf80 /* Sync remove VPC store */

static void pnv_xive_end_notify(XiveRouter *xrtr, XiveEAS *eas)
{
    PnvXive *xive = PNV_XIVE(xrtr);
    uint8_t end_blk = xive_get_field64(EAS_END_BLOCK, eas->w);
    uint32_t end_idx = xive_get_field64(EAS_END_INDEX, eas->w);
    uint32_t end_data = xive_get_field64(EAS_END_DATA, eas->w);
    uint64_t end_vsd = xive->vsds[VST_TSEL_EQDT][end_blk];

    switch (GETFIELD(VSD_MODE, end_vsd)) {
    case VSD_MODE_EXCLUSIVE:
        /* Perform the END notification on the local IC. */
        xive_router_end_notify(xrtr, eas);
        break;

    case VSD_MODE_FORWARD: {
        MemTxResult result;
        uint64_t notif_port = end_vsd & VSD_ADDRESS_MASK;
        uint64_t data = XIVE_TRIGGER_END | XIVE_TRIGGER_PQ |
            be64_to_cpu(eas->w);

        /* Forward the store on the remote IC notify page. */
        address_space_stq_be(&address_space_memory, notif_port, data,
                             MEMTXATTRS_UNSPECIFIED, &result);
        if (result != MEMTX_OK) {
            xive_error(xive, "IC: Forward notif END %x/%x [%x] failed @%"
                       HWADDR_PRIx, end_blk, end_idx, end_data, notif_port);
            return;
        }
        break;
    }

    case VSD_MODE_INVALID:
    default:
        /* Set FIR */
        xive_error(xive, "IC: Invalid END VSD for block %x", end_blk);
        return;
    }
}

/*
 * The notify page can either be used to receive trigger events from
 * the HW controllers (PHB, PSI) or to reroute interrupts between
 * Interrupt controllers.
 */
static void pnv_xive_ic_hw_trigger(PnvXive *xive, hwaddr addr, uint64_t val)
{
    uint8_t blk;
    uint32_t idx;

    trace_pnv_xive_ic_hw_trigger(addr, val);

    if (val & XIVE_TRIGGER_END) {
        val = cpu_to_be64(val);
        pnv_xive_end_notify(XIVE_ROUTER(xive), (XiveEAS *) &val);
        return;
    }

    /*
     * Forward the source event notification directly to the Router.
     * The source interrupt number should already be correctly encoded
     * with the chip block id by the sending device (PHB, PSI).
     */
    blk = XIVE_EAS_BLOCK(val);
    idx = XIVE_EAS_INDEX(val);

    xive_router_notify(XIVE_NOTIFIER(xive), XIVE_EAS(blk, idx),
                       !!(val & XIVE_TRIGGER_PQ));
}

static void pnv_xive_ic_notify_write(void *opaque, hwaddr addr, uint64_t val,
                                     unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    /* VC: HW triggers */
    switch (addr) {
    case 0x000 ... 0x7FF:
        pnv_xive_ic_hw_trigger(opaque, addr, val);
        break;

    /* VC: Forwarded IRQs */
    case PNV_XIVE_FORWARD_IPI:
    case PNV_XIVE_FORWARD_HW:
    case PNV_XIVE_FORWARD_OS_ESC:
    case PNV_XIVE_FORWARD_HW_ESC:
    case PNV_XIVE_FORWARD_REDIS:
        /* TODO: forwarded IRQs. Should be like HW triggers */
        xive_error(xive, "IC: forwarded at @0x%"HWADDR_PRIx" IRQ 0x%"PRIx64,
                   addr, val);
        break;

    /* VC syncs */
    case PNV_XIVE_SYNC_IPI:
    case PNV_XIVE_SYNC_HW:
    case PNV_XIVE_SYNC_OS_ESC:
    case PNV_XIVE_SYNC_HW_ESC:
    case PNV_XIVE_SYNC_REDIS:
        break;

    /* PC syncs */
    case PNV_XIVE_SYNC_PULL:
    case PNV_XIVE_SYNC_PUSH:
    case PNV_XIVE_SYNC_VPC:
        break;

    default:
        xive_error(xive, "IC: invalid notify write @%"HWADDR_PRIx, addr);
    }
}

static uint64_t pnv_xive_ic_notify_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    /* loads are invalid */
    xive_error(xive, "IC: invalid notify read @%"HWADDR_PRIx, addr);
    return -1;
}

static const MemoryRegionOps pnv_xive_ic_notify_ops = {
    .read = pnv_xive_ic_notify_read,
    .write = pnv_xive_ic_notify_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/*
 * IC - LSI MMIO handlers (not modeled)
 */

static void pnv_xive_ic_lsi_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    xive_error(xive, "IC: LSI invalid write @%"HWADDR_PRIx, addr);
}

static uint64_t pnv_xive_ic_lsi_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    xive_error(xive, "IC: LSI invalid read @%"HWADDR_PRIx, addr);
    return -1;
}

static const MemoryRegionOps pnv_xive_ic_lsi_ops = {
    .read = pnv_xive_ic_lsi_read,
    .write = pnv_xive_ic_lsi_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/*
 * IC - Indirect TIMA MMIO handlers
 */

/*
 * When the TIMA is accessed from the indirect page, the thread id of
 * the target CPU is configured in the PC_TCTXT_INDIR0 register before
 * use. This is used for resets and for debug purpose also.
 */
static XiveTCTX *pnv_xive_get_indirect_tctx(PnvXive *xive)
{
    PnvChip *chip = xive->chip;
    uint64_t tctxt_indir = xive->regs[PC_TCTXT_INDIR0 >> 3];
    PowerPCCPU *cpu = NULL;
    int pir;

    if (!(tctxt_indir & PC_TCTXT_INDIR_VALID)) {
        xive_error(xive, "IC: no indirect TIMA access in progress");
        return NULL;
    }

    pir = (chip->chip_id << 8) | GETFIELD(PC_TCTXT_INDIR_THRDID, tctxt_indir);
    cpu = pnv_chip_find_cpu(chip, pir);
    if (!cpu) {
        xive_error(xive, "IC: invalid PIR %x for indirect access", pir);
        return NULL;
    }

    /* Check that HW thread is XIVE enabled */
    if (!pnv_xive_is_cpu_enabled(xive, cpu)) {
        xive_error(xive, "IC: CPU %x is not enabled", pir);
    }

    return XIVE_TCTX(pnv_cpu_state(cpu)->intc);
}

static void xive_tm_indirect_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    XiveTCTX *tctx = pnv_xive_get_indirect_tctx(PNV_XIVE(opaque));

    xive_tctx_tm_write(XIVE_PRESENTER(opaque), tctx, offset, value, size);
}

static uint64_t xive_tm_indirect_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    XiveTCTX *tctx = pnv_xive_get_indirect_tctx(PNV_XIVE(opaque));

    return xive_tctx_tm_read(XIVE_PRESENTER(opaque), tctx, offset, size);
}

static const MemoryRegionOps xive_tm_indirect_ops = {
    .read = xive_tm_indirect_read,
    .write = xive_tm_indirect_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void pnv_xive_tm_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    PnvXive *xive = pnv_xive_tm_get_xive(cpu);
    XiveTCTX *tctx = XIVE_TCTX(pnv_cpu_state(cpu)->intc);

    xive_tctx_tm_write(XIVE_PRESENTER(xive), tctx, offset, value, size);
}

static uint64_t pnv_xive_tm_read(void *opaque, hwaddr offset, unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    PnvXive *xive = pnv_xive_tm_get_xive(cpu);
    XiveTCTX *tctx = XIVE_TCTX(pnv_cpu_state(cpu)->intc);

    return xive_tctx_tm_read(XIVE_PRESENTER(xive), tctx, offset, size);
}

const MemoryRegionOps pnv_xive_tm_ops = {
    .read = pnv_xive_tm_read,
    .write = pnv_xive_tm_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

/*
 * Interrupt controller XSCOM region.
 */
static uint64_t pnv_xive_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    switch (addr >> 3) {
    case X_VC_EQC_CONFIG:
        /* FIXME (skiboot): This is the only XSCOM load. Bizarre. */
        return VC_EQC_SYNC_MASK;
    default:
        return pnv_xive_ic_reg_read(opaque, addr, size);
    }
}

static void pnv_xive_xscom_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    pnv_xive_ic_reg_write(opaque, addr, val, size);
}

static const MemoryRegionOps pnv_xive_xscom_ops = {
    .read = pnv_xive_xscom_read,
    .write = pnv_xive_xscom_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    }
};

/*
 * Virtualization Controller MMIO region containing the IPI and END ESB pages
 */
static uint64_t pnv_xive_vc_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);
    uint64_t edt_index = offset >> pnv_xive_edt_shift(xive);
    uint64_t edt_type = 0;
    uint64_t edt_offset;
    MemTxResult result;
    AddressSpace *edt_as = NULL;
    uint64_t ret = -1;

    if (edt_index < XIVE_TABLE_EDT_MAX) {
        edt_type = GETFIELD(CQ_TDR_EDT_TYPE, xive->edt[edt_index]);
    }

    switch (edt_type) {
    case CQ_TDR_EDT_IPI:
        edt_as = &xive->ipi_as;
        break;
    case CQ_TDR_EDT_EQ:
        edt_as = &xive->end_as;
        break;
    default:
        xive_error(xive, "VC: invalid EDT type for read @%"HWADDR_PRIx, offset);
        return -1;
    }

    /* Remap the offset for the targeted address space */
    edt_offset = pnv_xive_edt_offset(xive, offset, edt_type);

    ret = address_space_ldq(edt_as, edt_offset, MEMTXATTRS_UNSPECIFIED,
                            &result);

    if (result != MEMTX_OK) {
        xive_error(xive, "VC: %s read failed at @0x%"HWADDR_PRIx " -> @0x%"
                   HWADDR_PRIx, edt_type == CQ_TDR_EDT_IPI ? "IPI" : "END",
                   offset, edt_offset);
        return -1;
    }

    return ret;
}

static void pnv_xive_vc_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);
    uint64_t edt_index = offset >> pnv_xive_edt_shift(xive);
    uint64_t edt_type = 0;
    uint64_t edt_offset;
    MemTxResult result;
    AddressSpace *edt_as = NULL;

    if (edt_index < XIVE_TABLE_EDT_MAX) {
        edt_type = GETFIELD(CQ_TDR_EDT_TYPE, xive->edt[edt_index]);
    }

    switch (edt_type) {
    case CQ_TDR_EDT_IPI:
        edt_as = &xive->ipi_as;
        break;
    case CQ_TDR_EDT_EQ:
        edt_as = &xive->end_as;
        break;
    default:
        xive_error(xive, "VC: invalid EDT type for write @%"HWADDR_PRIx,
                   offset);
        return;
    }

    /* Remap the offset for the targeted address space */
    edt_offset = pnv_xive_edt_offset(xive, offset, edt_type);

    address_space_stq(edt_as, edt_offset, val, MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        xive_error(xive, "VC: write failed at @0x%"HWADDR_PRIx, edt_offset);
    }
}

static const MemoryRegionOps pnv_xive_vc_ops = {
    .read = pnv_xive_vc_read,
    .write = pnv_xive_vc_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

/*
 * Presenter Controller MMIO region. Points to the NVT sets.
 *
 * HW implements all possible mem ops to the underlying NVT structure
 * but QEMU does not need to be so precise. The model implementation
 * simply returns the RAM address of the NVT structure which is then
 * used by pnv_xive_vst_write/read to perform the RAM operation.
 */
static uint64_t pnv_xive_pc_read(void *opaque, hwaddr offset, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);
    uint32_t nvt_idx = offset >> xive->pc_shift;
    uint8_t blk = pnv_xive_block_id(xive); /* TODO: VDT -> block xlate */

    return pnv_xive_vst_addr(xive, VST_TSEL_VPDT, blk, nvt_idx);
}

static void pnv_xive_pc_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    PnvXive *xive = PNV_XIVE(opaque);

    xive_error(xive, "PC: invalid write to VC @%"HWADDR_PRIx, addr);
}

static const MemoryRegionOps pnv_xive_pc_ops = {
    .read = pnv_xive_pc_read,
    .write = pnv_xive_pc_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static void xive_nvt_pic_print_info(XiveNVT *nvt, uint32_t nvt_idx,
                                    Monitor *mon)
{
    uint8_t  eq_blk = xive_get_field32(NVT_W1_EQ_BLOCK, nvt->w1);
    uint32_t eq_idx = xive_get_field32(NVT_W1_EQ_INDEX, nvt->w1);

    if (!xive_nvt_is_valid(nvt)) {
        return;
    }

    monitor_printf(mon, "  %08x end:%02x/%04x IPB:%02x\n", nvt_idx,
                   eq_blk, eq_idx,
                   xive_get_field32(NVT_W4_IPB, nvt->w4));
}

void pnv_xive_pic_print_info(PnvXive *xive, Monitor *mon)
{
    XiveRouter *xrtr = XIVE_ROUTER(xive);
    uint8_t blk = pnv_xive_block_id(xive);
    uint8_t chip_id = xive->chip->chip_id;
    uint32_t srcno0 = XIVE_EAS(blk, 0);
    uint32_t nr_ipis = pnv_xive_nr_ipis(xive, blk);
    XiveEAS eas;
    XiveEND end;
    XiveNVT nvt;
    int i;
    uint64_t xive_nvt_per_subpage;

    monitor_printf(mon, "XIVE[%x] #%d Source %08x .. %08x\n", chip_id, blk,
                   srcno0, srcno0 + nr_ipis - 1);
    xive_source_pic_print_info(&xive->ipi_source, srcno0, mon);

    monitor_printf(mon, "XIVE[%x] #%d EAT %08x .. %08x\n", chip_id, blk,
                   srcno0, srcno0 + nr_ipis - 1);
    for (i = 0; i < nr_ipis; i++) {
        if (xive_router_get_eas(xrtr, blk, i, &eas)) {
            break;
        }
        if (!xive_eas_is_masked(&eas)) {
            xive_eas_pic_print_info(&eas, i, mon);
        }
    }

    monitor_printf(mon, "XIVE[%x] #%d ENDT\n", chip_id, blk);
    i = 0;
    while (!xive_router_get_end(xrtr, blk, i, &end)) {
        xive_end_pic_print_info(&end, i++, mon);
    }

    monitor_printf(mon, "XIVE[%x] #%d END Escalation EAT\n", chip_id, blk);
    i = 0;
    while (!xive_router_get_end(xrtr, blk, i, &end)) {
        xive_end_eas_pic_print_info(&end, i++, mon);
    }

    monitor_printf(mon, "XIVE[%x] #%d NVTT %08x .. %08x\n", chip_id, blk,
                   0, XIVE_NVT_COUNT - 1);
    xive_nvt_per_subpage = pnv_xive_vst_per_subpage(xive, VST_TSEL_VPDT);
    for (i = 0; i < XIVE_NVT_COUNT; i += xive_nvt_per_subpage) {
        while (!xive_router_get_nvt(xrtr, blk, i, &nvt)) {
            xive_nvt_pic_print_info(&nvt, i++, mon);
        }
    }
}

static void pnv_xive_reset(void *dev)
{
    PnvXive *xive = PNV_XIVE(dev);
    XiveSource *xsrc = &xive->ipi_source;
    XiveENDSource *end_xsrc = &xive->end_source;

    /* Default page size (Should be changed at runtime to 64k) */
    xive->ic_shift = xive->vc_shift = xive->pc_shift = 12;

    /* Clear subregions */
    if (memory_region_is_mapped(&xsrc->esb_mmio)) {
        memory_region_del_subregion(&xive->ipi_edt_mmio, &xsrc->esb_mmio);
    }

    if (memory_region_is_mapped(&xive->ipi_edt_mmio)) {
        memory_region_del_subregion(&xive->ipi_mmio, &xive->ipi_edt_mmio);
    }

    if (memory_region_is_mapped(&end_xsrc->esb_mmio)) {
        memory_region_del_subregion(&xive->end_edt_mmio, &end_xsrc->esb_mmio);
    }

    if (memory_region_is_mapped(&xive->end_edt_mmio)) {
        memory_region_del_subregion(&xive->end_mmio, &xive->end_edt_mmio);
    }
}

static void pnv_xive_init(Object *obj)
{
    PnvXive *xive = PNV_XIVE(obj);

    object_initialize_child(obj, "ipi_source", &xive->ipi_source,
                            TYPE_XIVE_SOURCE);
    object_initialize_child(obj, "end_source", &xive->end_source,
                            TYPE_XIVE_END_SOURCE);
}

/*
 *  Maximum number of IRQs and ENDs supported by HW
 */
#define PNV_XIVE_NR_IRQS (PNV9_XIVE_VC_SIZE / (1ull << XIVE_ESB_64K_2PAGE))
#define PNV_XIVE_NR_ENDS (PNV9_XIVE_VC_SIZE / (1ull << XIVE_ESB_64K_2PAGE))

static void pnv_xive_realize(DeviceState *dev, Error **errp)
{
    PnvXive *xive = PNV_XIVE(dev);
    PnvXiveClass *pxc = PNV_XIVE_GET_CLASS(dev);
    XiveSource *xsrc = &xive->ipi_source;
    XiveENDSource *end_xsrc = &xive->end_source;
    Error *local_err = NULL;

    pxc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    assert(xive->chip);

    /*
     * The XiveSource and XiveENDSource objects are realized with the
     * maximum allowed HW configuration. The ESB MMIO regions will be
     * resized dynamically when the controller is configured by the FW
     * to limit accesses to resources not provisioned.
     */
    object_property_set_int(OBJECT(xsrc), "nr-irqs", PNV_XIVE_NR_IRQS,
                            &error_fatal);
    object_property_set_link(OBJECT(xsrc), "xive", OBJECT(xive), &error_abort);
    if (!qdev_realize(DEVICE(xsrc), NULL, errp)) {
        return;
    }

    object_property_set_int(OBJECT(end_xsrc), "nr-ends", PNV_XIVE_NR_ENDS,
                            &error_fatal);
    object_property_set_link(OBJECT(end_xsrc), "xive", OBJECT(xive),
                             &error_abort);
    if (!qdev_realize(DEVICE(end_xsrc), NULL, errp)) {
        return;
    }

    /* Default page size. Generally changed at runtime to 64k */
    xive->ic_shift = xive->vc_shift = xive->pc_shift = 12;

    /* XSCOM region, used for initial configuration of the BARs */
    memory_region_init_io(&xive->xscom_regs, OBJECT(dev), &pnv_xive_xscom_ops,
                          xive, "xscom-xive", PNV9_XSCOM_XIVE_SIZE << 3);

    /* Interrupt controller MMIO regions */
    memory_region_init(&xive->ic_mmio, OBJECT(dev), "xive-ic",
                       PNV9_XIVE_IC_SIZE);

    memory_region_init_io(&xive->ic_reg_mmio, OBJECT(dev), &pnv_xive_ic_reg_ops,
                          xive, "xive-ic-reg", 1 << xive->ic_shift);
    memory_region_init_io(&xive->ic_notify_mmio, OBJECT(dev),
                          &pnv_xive_ic_notify_ops,
                          xive, "xive-ic-notify", 1 << xive->ic_shift);
    xive->ic_notify_mmio.disable_reentrancy_guard = true;

    /* The Pervasive LSI trigger and EOI pages (not modeled) */
    memory_region_init_io(&xive->ic_lsi_mmio, OBJECT(dev), &pnv_xive_ic_lsi_ops,
                          xive, "xive-ic-lsi", 2 << xive->ic_shift);

    /* Thread Interrupt Management Area (Indirect) */
    memory_region_init_io(&xive->tm_indirect_mmio, OBJECT(dev),
                          &xive_tm_indirect_ops,
                          xive, "xive-tima-indirect", PNV9_XIVE_TM_SIZE);
    /*
     * Overall Virtualization Controller MMIO region containing the
     * IPI ESB pages and END ESB pages. The layout is defined by the
     * EDT "Domain table" and the accesses are dispatched using
     * address spaces for each.
     */
    memory_region_init_io(&xive->vc_mmio, OBJECT(xive), &pnv_xive_vc_ops, xive,
                          "xive-vc", PNV9_XIVE_VC_SIZE);

    memory_region_init(&xive->ipi_mmio, OBJECT(xive), "xive-vc-ipi",
                       PNV9_XIVE_VC_SIZE);
    address_space_init(&xive->ipi_as, &xive->ipi_mmio, "xive-vc-ipi");
    memory_region_init(&xive->end_mmio, OBJECT(xive), "xive-vc-end",
                       PNV9_XIVE_VC_SIZE);
    address_space_init(&xive->end_as, &xive->end_mmio, "xive-vc-end");

    /*
     * The MMIO windows exposing the IPI ESBs and the END ESBs in the
     * VC region. Their size is configured by the FW in the EDT table.
     */
    memory_region_init(&xive->ipi_edt_mmio, OBJECT(xive), "xive-vc-ipi-edt", 0);
    memory_region_init(&xive->end_edt_mmio, OBJECT(xive), "xive-vc-end-edt", 0);

    /* Presenter Controller MMIO region (not modeled) */
    memory_region_init_io(&xive->pc_mmio, OBJECT(xive), &pnv_xive_pc_ops, xive,
                          "xive-pc", PNV9_XIVE_PC_SIZE);
    xive->pc_mmio.disable_reentrancy_guard = true;

    /* Thread Interrupt Management Area (Direct) */
    memory_region_init_io(&xive->tm_mmio, OBJECT(xive), &pnv_xive_tm_ops,
                          xive, "xive-tima", PNV9_XIVE_TM_SIZE);

    qemu_register_reset(pnv_xive_reset, dev);
}

static int pnv_xive_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int xscom_offset)
{
    const char compat[] = "ibm,power9-xive-x";
    char *name;
    int offset;
    uint32_t lpc_pcba = PNV9_XSCOM_XIVE_BASE;
    uint32_t reg[] = {
        cpu_to_be32(lpc_pcba),
        cpu_to_be32(PNV9_XSCOM_XIVE_SIZE)
    };

    name = g_strdup_printf("xive@%x", lpc_pcba);
    offset = fdt_add_subnode(fdt, xscom_offset, name);
    _FDT(offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop(fdt, offset, "compatible", compat,
                      sizeof(compat))));
    return 0;
}

static Property pnv_xive_properties[] = {
    DEFINE_PROP_UINT64("ic-bar", PnvXive, ic_base, 0),
    DEFINE_PROP_UINT64("vc-bar", PnvXive, vc_base, 0),
    DEFINE_PROP_UINT64("pc-bar", PnvXive, pc_base, 0),
    DEFINE_PROP_UINT64("tm-bar", PnvXive, tm_base, 0),
    /* The PnvChip id identifies the XIVE interrupt controller. */
    DEFINE_PROP_LINK("chip", PnvXive, chip, TYPE_PNV_CHIP, PnvChip *),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_xive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);
    XiveRouterClass *xrc = XIVE_ROUTER_CLASS(klass);
    XiveNotifierClass *xnc = XIVE_NOTIFIER_CLASS(klass);
    XivePresenterClass *xpc = XIVE_PRESENTER_CLASS(klass);
    PnvXiveClass *pxc = PNV_XIVE_CLASS(klass);

    xdc->dt_xscom = pnv_xive_dt_xscom;

    dc->desc = "PowerNV XIVE Interrupt Controller";
    device_class_set_parent_realize(dc, pnv_xive_realize, &pxc->parent_realize);
    dc->realize = pnv_xive_realize;
    device_class_set_props(dc, pnv_xive_properties);

    xrc->get_eas = pnv_xive_get_eas;
    xrc->get_pq = pnv_xive_get_pq;
    xrc->set_pq = pnv_xive_set_pq;
    xrc->get_end = pnv_xive_get_end;
    xrc->write_end = pnv_xive_write_end;
    xrc->get_nvt = pnv_xive_get_nvt;
    xrc->write_nvt = pnv_xive_write_nvt;
    xrc->get_block_id = pnv_xive_get_block_id;
    xrc->end_notify = pnv_xive_end_notify;

    xnc->notify = pnv_xive_notify;
    xpc->match_nvt  = pnv_xive_match_nvt;
    xpc->get_config = pnv_xive_presenter_get_config;
};

static const TypeInfo pnv_xive_info = {
    .name          = TYPE_PNV_XIVE,
    .parent        = TYPE_XIVE_ROUTER,
    .instance_init = pnv_xive_init,
    .instance_size = sizeof(PnvXive),
    .class_init    = pnv_xive_class_init,
    .class_size    = sizeof(PnvXiveClass),
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_xive_register_types(void)
{
    type_register_static(&pnv_xive_info);
}

type_init(pnv_xive_register_types)
