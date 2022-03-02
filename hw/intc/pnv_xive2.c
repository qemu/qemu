/*
 * QEMU PowerPC XIVE2 interrupt controller model  (POWER10)
 *
 * Copyright (c) 2019-2022, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/dma.h"
#include "monitor/monitor.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_core.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/xive2.h"
#include "hw/ppc/pnv_xive.h"
#include "hw/ppc/xive_regs.h"
#include "hw/ppc/xive2_regs.h"
#include "hw/ppc/ppc.h"
#include "hw/qdev-properties.h"
#include "sysemu/reset.h"

#include <libfdt.h>

#include "pnv_xive2_regs.h"

#undef XIVE2_DEBUG

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

    [VST_EAS]  = { "EAT",  sizeof(Xive2Eas),  16 },
    [VST_ESB]  = { "ESB",  1,                  16 },
    [VST_END]  = { "ENDT", sizeof(Xive2End),  16 },

    [VST_NVP]  = { "NVPT", sizeof(Xive2Nvp),  16 },
    [VST_NVG]  = { "NVGT", sizeof(Xive2Nvgc), 16 },
    [VST_NVC]  = { "NVCT", sizeof(Xive2Nvgc), 16 },

    [VST_IC]  =  { "IC",   1 /* ? */         , 16 }, /* Topology # */
    [VST_SYNC] = { "SYNC", 1 /* ? */         , 16 }, /* Topology # */

    /*
     * This table contains the backing store pages for the interrupt
     * fifos of the VC sub-engine in case of overflow.
     *
     * 0 - IPI,
     * 1 - HWD,
     * 2 - NxC,
     * 3 - INT,
     * 4 - OS-Queue,
     * 5 - Pool-Queue,
     * 6 - Hard-Queue
     */
    [VST_ERQ]  = { "ERQ",  1,                   VC_QUEUE_COUNT },
};

#define xive2_error(xive, fmt, ...)                                      \
    qemu_log_mask(LOG_GUEST_ERROR, "XIVE[%x] - " fmt "\n",              \
                  (xive)->chip->chip_id, ## __VA_ARGS__);

/*
 * QEMU version of the GETFIELD/SETFIELD macros
 *
 * TODO: It might be better to use the existing extract64() and
 * deposit64() but this means that all the register definitions will
 * change and become incompatible with the ones found in skiboot.
 *
 * Keep it as it is for now until we find a common ground.
 */
static inline uint64_t GETFIELD(uint64_t mask, uint64_t word)
{
    return (word & mask) >> ctz64(mask);
}

static inline uint64_t SETFIELD(uint64_t mask, uint64_t word,
                                uint64_t value)
{
    return (word & ~mask) | ((value << ctz64(mask)) & mask);
}

/*
 * TODO: Document block id override
 */
static uint32_t pnv_xive2_block_id(PnvXive2 *xive)
{
    uint8_t blk = xive->chip->chip_id;
    uint64_t cfg_val = xive->cq_regs[CQ_XIVE_CFG >> 3];

    if (cfg_val & CQ_XIVE_CFG_HYP_HARD_BLKID_OVERRIDE) {
        blk = GETFIELD(CQ_XIVE_CFG_HYP_HARD_BLOCK_ID, cfg_val);
    }

    return blk;
}

/*
 * Remote access to controllers. HW uses MMIOs. For now, a simple scan
 * of the chips is good enough.
 *
 * TODO: Block scope support
 */
static PnvXive2 *pnv_xive2_get_remote(uint8_t blk)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv10Chip *chip10 = PNV10_CHIP(pnv->chips[i]);
        PnvXive2 *xive = &chip10->xive;

        if (pnv_xive2_block_id(xive) == blk) {
            return xive;
        }
    }
    return NULL;
}

/*
 * VST accessors for ESB, EAT, ENDT, NVP
 *
 * Indirect VST tables are arrays of VSDs pointing to a page (of same
 * size). Each page is a direct VST table.
 */

#define XIVE_VSD_SIZE 8

/* Indirect page size can be 4K, 64K, 2M, 16M. */
static uint64_t pnv_xive2_vst_page_size_allowed(uint32_t page_shift)
{
     return page_shift == 12 || page_shift == 16 ||
         page_shift == 21 || page_shift == 24;
}

static uint64_t pnv_xive2_vst_addr_direct(PnvXive2 *xive, uint32_t type,
                                          uint64_t vsd, uint32_t idx)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t vst_addr = vsd & VSD_ADDRESS_MASK;
    uint64_t vst_tsize = 1ull << (GETFIELD(VSD_TSIZE, vsd) + 12);
    uint32_t idx_max;

    idx_max = vst_tsize / info->size - 1;
    if (idx > idx_max) {
#ifdef XIVE2_DEBUG
        xive2_error(xive, "VST: %s entry %x out of range [ 0 .. %x ] !?",
                   info->name, idx, idx_max);
#endif
        return 0;
    }

    return vst_addr + idx * info->size;
}

static uint64_t pnv_xive2_vst_addr_indirect(PnvXive2 *xive, uint32_t type,
                                            uint64_t vsd, uint32_t idx)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t vsd_addr;
    uint32_t vsd_idx;
    uint32_t page_shift;
    uint32_t vst_per_page;

    /* Get the page size of the indirect table. */
    vsd_addr = vsd & VSD_ADDRESS_MASK;
    ldq_be_dma(&address_space_memory, vsd_addr, &vsd, MEMTXATTRS_UNSPECIFIED);

    if (!(vsd & VSD_ADDRESS_MASK)) {
        xive2_error(xive, "VST: invalid %s entry %x !?", info->name, idx);
        return 0;
    }

    page_shift = GETFIELD(VSD_TSIZE, vsd) + 12;

    if (!pnv_xive2_vst_page_size_allowed(page_shift)) {
        xive2_error(xive, "VST: invalid %s page shift %d", info->name,
                   page_shift);
        return 0;
    }

    vst_per_page = (1ull << page_shift) / info->size;
    vsd_idx = idx / vst_per_page;

    /* Load the VSD we are looking for, if not already done */
    if (vsd_idx) {
        vsd_addr = vsd_addr + vsd_idx * XIVE_VSD_SIZE;
        ldq_be_dma(&address_space_memory, vsd_addr, &vsd,
                   MEMTXATTRS_UNSPECIFIED);

        if (!(vsd & VSD_ADDRESS_MASK)) {
            xive2_error(xive, "VST: invalid %s entry %x !?", info->name, idx);
            return 0;
        }

        /*
         * Check that the pages have a consistent size across the
         * indirect table
         */
        if (page_shift != GETFIELD(VSD_TSIZE, vsd) + 12) {
            xive2_error(xive, "VST: %s entry %x indirect page size differ !?",
                       info->name, idx);
            return 0;
        }
    }

    return pnv_xive2_vst_addr_direct(xive, type, vsd, (idx % vst_per_page));
}

static uint64_t pnv_xive2_vst_addr(PnvXive2 *xive, uint32_t type, uint8_t blk,
                                   uint32_t idx)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t vsd;

    if (blk >= info->max_blocks) {
        xive2_error(xive, "VST: invalid block id %d for VST %s %d !?",
                   blk, info->name, idx);
        return 0;
    }

    vsd = xive->vsds[type][blk];

    /* Remote VST access */
    if (GETFIELD(VSD_MODE, vsd) == VSD_MODE_FORWARD) {
        xive = pnv_xive2_get_remote(blk);

        return xive ? pnv_xive2_vst_addr(xive, type, blk, idx) : 0;
    }

    if (VSD_INDIRECT & vsd) {
        return pnv_xive2_vst_addr_indirect(xive, type, vsd, idx);
    }

    return pnv_xive2_vst_addr_direct(xive, type, vsd, idx);
}

static int pnv_xive2_vst_read(PnvXive2 *xive, uint32_t type, uint8_t blk,
                             uint32_t idx, void *data)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t addr = pnv_xive2_vst_addr(xive, type, blk, idx);

    if (!addr) {
        return -1;
    }

    cpu_physical_memory_read(addr, data, info->size);
    return 0;
}

#define XIVE_VST_WORD_ALL -1

static int pnv_xive2_vst_write(PnvXive2 *xive, uint32_t type, uint8_t blk,
                               uint32_t idx, void *data, uint32_t word_number)
{
    const XiveVstInfo *info = &vst_infos[type];
    uint64_t addr = pnv_xive2_vst_addr(xive, type, blk, idx);

    if (!addr) {
        return -1;
    }

    if (word_number == XIVE_VST_WORD_ALL) {
        cpu_physical_memory_write(addr, data, info->size);
    } else {
        cpu_physical_memory_write(addr + word_number * 4,
                                  data + word_number * 4, 4);
    }
    return 0;
}

static int pnv_xive2_get_pq(Xive2Router *xrtr, uint8_t blk, uint32_t idx,
                             uint8_t *pq)
{
    PnvXive2 *xive = PNV_XIVE2(xrtr);

    if (pnv_xive2_block_id(xive) != blk) {
        xive2_error(xive, "VST: EAS %x is remote !?", XIVE_EAS(blk, idx));
        return -1;
    }

    *pq = xive_source_esb_get(&xive->ipi_source, idx);
    return 0;
}

static int pnv_xive2_set_pq(Xive2Router *xrtr, uint8_t blk, uint32_t idx,
                             uint8_t *pq)
{
    PnvXive2 *xive = PNV_XIVE2(xrtr);

    if (pnv_xive2_block_id(xive) != blk) {
        xive2_error(xive, "VST: EAS %x is remote !?", XIVE_EAS(blk, idx));
        return -1;
    }

    *pq = xive_source_esb_set(&xive->ipi_source, idx, *pq);
    return 0;
}

static int pnv_xive2_get_end(Xive2Router *xrtr, uint8_t blk, uint32_t idx,
                             Xive2End *end)
{
    return pnv_xive2_vst_read(PNV_XIVE2(xrtr), VST_END, blk, idx, end);
}

static int pnv_xive2_write_end(Xive2Router *xrtr, uint8_t blk, uint32_t idx,
                               Xive2End *end, uint8_t word_number)
{
    return pnv_xive2_vst_write(PNV_XIVE2(xrtr), VST_END, blk, idx, end,
                              word_number);
}

static int pnv_xive2_end_update(PnvXive2 *xive)
{
    uint8_t  blk = GETFIELD(VC_ENDC_WATCH_BLOCK_ID,
                           xive->vc_regs[(VC_ENDC_WATCH0_SPEC >> 3)]);
    uint32_t idx = GETFIELD(VC_ENDC_WATCH_INDEX,
                           xive->vc_regs[(VC_ENDC_WATCH0_SPEC >> 3)]);
    int i;
    uint64_t endc_watch[4];

    for (i = 0; i < ARRAY_SIZE(endc_watch); i++) {
        endc_watch[i] =
            cpu_to_be64(xive->vc_regs[(VC_ENDC_WATCH0_DATA0 >> 3) + i]);
    }

    return pnv_xive2_vst_write(xive, VST_END, blk, idx, endc_watch,
                              XIVE_VST_WORD_ALL);
}

static void pnv_xive2_end_cache_load(PnvXive2 *xive)
{
    uint8_t  blk = GETFIELD(VC_ENDC_WATCH_BLOCK_ID,
                           xive->vc_regs[(VC_ENDC_WATCH0_SPEC >> 3)]);
    uint32_t idx = GETFIELD(VC_ENDC_WATCH_INDEX,
                           xive->vc_regs[(VC_ENDC_WATCH0_SPEC >> 3)]);
    uint64_t endc_watch[4] = { 0 };
    int i;

    if (pnv_xive2_vst_read(xive, VST_END, blk, idx, endc_watch)) {
        xive2_error(xive, "VST: no END entry %x/%x !?", blk, idx);
    }

    for (i = 0; i < ARRAY_SIZE(endc_watch); i++) {
        xive->vc_regs[(VC_ENDC_WATCH0_DATA0 >> 3) + i] =
            be64_to_cpu(endc_watch[i]);
    }
}

static int pnv_xive2_get_nvp(Xive2Router *xrtr, uint8_t blk, uint32_t idx,
                             Xive2Nvp *nvp)
{
    return pnv_xive2_vst_read(PNV_XIVE2(xrtr), VST_NVP, blk, idx, nvp);
}

static int pnv_xive2_write_nvp(Xive2Router *xrtr, uint8_t blk, uint32_t idx,
                               Xive2Nvp *nvp, uint8_t word_number)
{
    return pnv_xive2_vst_write(PNV_XIVE2(xrtr), VST_NVP, blk, idx, nvp,
                              word_number);
}

static int pnv_xive2_nvp_update(PnvXive2 *xive)
{
    uint8_t  blk = GETFIELD(PC_NXC_WATCH_BLOCK_ID,
                            xive->pc_regs[(PC_NXC_WATCH0_SPEC >> 3)]);
    uint32_t idx = GETFIELD(PC_NXC_WATCH_INDEX,
                            xive->pc_regs[(PC_NXC_WATCH0_SPEC >> 3)]);
    int i;
    uint64_t nxc_watch[4];

    for (i = 0; i < ARRAY_SIZE(nxc_watch); i++) {
        nxc_watch[i] =
            cpu_to_be64(xive->pc_regs[(PC_NXC_WATCH0_DATA0 >> 3) + i]);
    }

    return pnv_xive2_vst_write(xive, VST_NVP, blk, idx, nxc_watch,
                              XIVE_VST_WORD_ALL);
}

static void pnv_xive2_nvp_cache_load(PnvXive2 *xive)
{
    uint8_t  blk = GETFIELD(PC_NXC_WATCH_BLOCK_ID,
                           xive->pc_regs[(PC_NXC_WATCH0_SPEC >> 3)]);
    uint32_t idx = GETFIELD(PC_NXC_WATCH_INDEX,
                           xive->pc_regs[(PC_NXC_WATCH0_SPEC >> 3)]);
    uint64_t nxc_watch[4] = { 0 };
    int i;

    if (pnv_xive2_vst_read(xive, VST_NVP, blk, idx, nxc_watch)) {
        xive2_error(xive, "VST: no NVP entry %x/%x !?", blk, idx);
    }

    for (i = 0; i < ARRAY_SIZE(nxc_watch); i++) {
        xive->pc_regs[(PC_NXC_WATCH0_DATA0 >> 3) + i] =
            be64_to_cpu(nxc_watch[i]);
    }
}

static int pnv_xive2_get_eas(Xive2Router *xrtr, uint8_t blk, uint32_t idx,
                            Xive2Eas *eas)
{
    PnvXive2 *xive = PNV_XIVE2(xrtr);

    if (pnv_xive2_block_id(xive) != blk) {
        xive2_error(xive, "VST: EAS %x is remote !?", XIVE_EAS(blk, idx));
        return -1;
    }

    return pnv_xive2_vst_read(xive, VST_EAS, blk, idx, eas);
}

static uint32_t pnv_xive2_get_config(Xive2Router *xrtr)
{
    PnvXive2 *xive = PNV_XIVE2(xrtr);
    uint32_t cfg = 0;

    if (xive->cq_regs[CQ_XIVE_CFG >> 3] & CQ_XIVE_CFG_GEN1_TIMA_OS) {
        cfg |= XIVE2_GEN1_TIMA_OS;
    }

    if (xive->cq_regs[CQ_XIVE_CFG >> 3] & CQ_XIVE_CFG_EN_VP_SAVE_RESTORE) {
        cfg |= XIVE2_VP_SAVE_RESTORE;
    }

    if (GETFIELD(CQ_XIVE_CFG_HYP_HARD_RANGE,
              xive->cq_regs[CQ_XIVE_CFG >> 3]) == CQ_XIVE_CFG_THREADID_8BITS) {
        cfg |= XIVE2_THREADID_8BITS;
    }

    return cfg;
}

static bool pnv_xive2_is_cpu_enabled(PnvXive2 *xive, PowerPCCPU *cpu)
{
    int pir = ppc_cpu_pir(cpu);
    uint32_t fc = PNV10_PIR2FUSEDCORE(pir);
    uint64_t reg = fc < 8 ? TCTXT_EN0 : TCTXT_EN1;
    uint32_t bit = pir & 0x3f;

    return xive->tctxt_regs[reg >> 3] & PPC_BIT(bit);
}

static int pnv_xive2_match_nvt(XivePresenter *xptr, uint8_t format,
                               uint8_t nvt_blk, uint32_t nvt_idx,
                               bool cam_ignore, uint8_t priority,
                               uint32_t logic_serv, XiveTCTXMatch *match)
{
    PnvXive2 *xive = PNV_XIVE2(xptr);
    PnvChip *chip = xive->chip;
    int count = 0;
    int i, j;
    bool gen1_tima_os =
        xive->cq_regs[CQ_XIVE_CFG >> 3] & CQ_XIVE_CFG_GEN1_TIMA_OS;

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pc = chip->cores[i];
        CPUCore *cc = CPU_CORE(pc);

        for (j = 0; j < cc->nr_threads; j++) {
            PowerPCCPU *cpu = pc->threads[j];
            XiveTCTX *tctx;
            int ring;

            if (!pnv_xive2_is_cpu_enabled(xive, cpu)) {
                continue;
            }

            tctx = XIVE_TCTX(pnv_cpu_state(cpu)->intc);

            if (gen1_tima_os) {
                ring = xive_presenter_tctx_match(xptr, tctx, format, nvt_blk,
                                                 nvt_idx, cam_ignore,
                                                 logic_serv);
            } else {
                ring = xive2_presenter_tctx_match(xptr, tctx, format, nvt_blk,
                                                   nvt_idx, cam_ignore,
                                                   logic_serv);
            }

            /*
             * Save the context and follow on to catch duplicates,
             * that we don't support yet.
             */
            if (ring != -1) {
                if (match->tctx) {
                    qemu_log_mask(LOG_GUEST_ERROR, "XIVE: already found a "
                                  "thread context NVT %x/%x\n",
                                  nvt_blk, nvt_idx);
                    return false;
                }

                match->ring = ring;
                match->tctx = tctx;
                count++;
            }
        }
    }

    return count;
}

static uint8_t pnv_xive2_get_block_id(Xive2Router *xrtr)
{
    return pnv_xive2_block_id(PNV_XIVE2(xrtr));
}

/*
 * The TIMA MMIO space is shared among the chips and to identify the
 * chip from which the access is being done, we extract the chip id
 * from the PIR.
 */
static PnvXive2 *pnv_xive2_tm_get_xive(PowerPCCPU *cpu)
{
    int pir = ppc_cpu_pir(cpu);
    XivePresenter *xptr = XIVE_TCTX(pnv_cpu_state(cpu)->intc)->xptr;
    PnvXive2 *xive = PNV_XIVE2(xptr);

    if (!pnv_xive2_is_cpu_enabled(xive, cpu)) {
        xive2_error(xive, "IC: CPU %x is not enabled", pir);
    }
    return xive;
}

/*
 * The internal sources of the interrupt controller have no knowledge
 * of the XIVE2 chip on which they reside. Encode the block id in the
 * source interrupt number before forwarding the source event
 * notification to the Router. This is required on a multichip system.
 */
static void pnv_xive2_notify(XiveNotifier *xn, uint32_t srcno, bool pq_checked)
{
    PnvXive2 *xive = PNV_XIVE2(xn);
    uint8_t blk = pnv_xive2_block_id(xive);

    xive2_router_notify(xn, XIVE_EAS(blk, srcno), pq_checked);
}

/*
 * Set Translation Tables
 *
 * TODO add support for multiple sets
 */
static int pnv_xive2_stt_set_data(PnvXive2 *xive, uint64_t val)
{
    uint8_t tsel = GETFIELD(CQ_TAR_SELECT, xive->cq_regs[CQ_TAR >> 3]);
    uint8_t entry = GETFIELD(CQ_TAR_ENTRY_SELECT,
                                  xive->cq_regs[CQ_TAR >> 3]);

    switch (tsel) {
    case CQ_TAR_NVPG:
    case CQ_TAR_ESB:
    case CQ_TAR_END:
        xive->tables[tsel][entry] = val;
        break;
    default:
        xive2_error(xive, "IC: unsupported table %d", tsel);
        return -1;
    }

    if (xive->cq_regs[CQ_TAR >> 3] & CQ_TAR_AUTOINC) {
        xive->cq_regs[CQ_TAR >> 3] = SETFIELD(CQ_TAR_ENTRY_SELECT,
                     xive->cq_regs[CQ_TAR >> 3], ++entry);
    }

    return 0;
}
/*
 * Virtual Structure Tables (VST) configuration
 */
static void pnv_xive2_vst_set_exclusive(PnvXive2 *xive, uint8_t type,
                                        uint8_t blk, uint64_t vsd)
{
    Xive2EndSource *end_xsrc = &xive->end_source;
    XiveSource *xsrc = &xive->ipi_source;
    const XiveVstInfo *info = &vst_infos[type];
    uint32_t page_shift = GETFIELD(VSD_TSIZE, vsd) + 12;
    uint64_t vst_tsize = 1ull << page_shift;
    uint64_t vst_addr = vsd & VSD_ADDRESS_MASK;

    /* Basic checks */

    if (VSD_INDIRECT & vsd) {
        if (!pnv_xive2_vst_page_size_allowed(page_shift)) {
            xive2_error(xive, "VST: invalid %s page shift %d", info->name,
                       page_shift);
            return;
        }
    }

    if (!QEMU_IS_ALIGNED(vst_addr, 1ull << page_shift)) {
        xive2_error(xive, "VST: %s table address 0x%"PRIx64
                    " is not aligned with page shift %d",
                    info->name, vst_addr, page_shift);
        return;
    }

    /* Record the table configuration (in SRAM on HW) */
    xive->vsds[type][blk] = vsd;

    /* Now tune the models with the configuration provided by the FW */

    switch (type) {
    case VST_ESB:
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

        memory_region_add_subregion(&xive->esb_mmio, 0, &xsrc->esb_mmio);
        break;

    case VST_EAS:  /* Nothing to be done */
        break;

    case VST_END:
        /*
         * Backing store pages for the END.
         */
        if (!(VSD_INDIRECT & vsd)) {
            memory_region_set_size(&end_xsrc->esb_mmio, (vst_tsize / info->size)
                                   * (1ull << end_xsrc->esb_shift));
        }
        memory_region_add_subregion(&xive->end_mmio, 0, &end_xsrc->esb_mmio);
        break;

    case VST_NVP:  /* Not modeled */
    case VST_NVG:  /* Not modeled */
    case VST_NVC:  /* Not modeled */
    case VST_IC:   /* Not modeled */
    case VST_SYNC: /* Not modeled */
    case VST_ERQ:  /* Not modeled */
        break;

    default:
        g_assert_not_reached();
    }
}

/*
 * Both PC and VC sub-engines are configured as each use the Virtual
 * Structure Tables
 */
static void pnv_xive2_vst_set_data(PnvXive2 *xive, uint64_t vsd)
{
    uint8_t mode = GETFIELD(VSD_MODE, vsd);
    uint8_t type = GETFIELD(VC_VSD_TABLE_SELECT,
                            xive->vc_regs[VC_VSD_TABLE_ADDR >> 3]);
    uint8_t blk = GETFIELD(VC_VSD_TABLE_ADDRESS,
                           xive->vc_regs[VC_VSD_TABLE_ADDR >> 3]);
    uint64_t vst_addr = vsd & VSD_ADDRESS_MASK;

    if (type > VST_ERQ) {
        xive2_error(xive, "VST: invalid table type %d", type);
        return;
    }

    if (blk >= vst_infos[type].max_blocks) {
        xive2_error(xive, "VST: invalid block id %d for"
                      " %s table", blk, vst_infos[type].name);
        return;
    }

    if (!vst_addr) {
        xive2_error(xive, "VST: invalid %s table address",
                   vst_infos[type].name);
        return;
    }

    switch (mode) {
    case VSD_MODE_FORWARD:
        xive->vsds[type][blk] = vsd;
        break;

    case VSD_MODE_EXCLUSIVE:
        pnv_xive2_vst_set_exclusive(xive, type, blk, vsd);
        break;

    default:
        xive2_error(xive, "VST: unsupported table mode %d", mode);
        return;
    }
}

/*
 * MMIO handlers
 */


/*
 * IC BAR layout
 *
 * Page 0: Internal CQ register accesses (reads & writes)
 * Page 1: Internal PC register accesses (reads & writes)
 * Page 2: Internal VC register accesses (reads & writes)
 * Page 3: Internal TCTXT (TIMA) reg accesses (read & writes)
 * Page 4: Notify Port page (writes only, w/data),
 * Page 5: Reserved
 * Page 6: Sync Poll page (writes only, dataless)
 * Page 7: Sync Inject page (writes only, dataless)
 * Page 8: LSI Trigger page (writes only, dataless)
 * Page 9: LSI SB Management page (reads & writes dataless)
 * Pages 10-255: Reserved
 * Pages 256-383: Direct mapped Thread Context Area (reads & writes)
 *                covering the 128 threads in P10.
 * Pages 384-511: Reserved
 */
typedef struct PnvXive2Region {
    const char *name;
    uint32_t pgoff;
    uint32_t pgsize;
    const MemoryRegionOps *ops;
} PnvXive2Region;

static const MemoryRegionOps pnv_xive2_ic_cq_ops;
static const MemoryRegionOps pnv_xive2_ic_pc_ops;
static const MemoryRegionOps pnv_xive2_ic_vc_ops;
static const MemoryRegionOps pnv_xive2_ic_tctxt_ops;
static const MemoryRegionOps pnv_xive2_ic_notify_ops;
static const MemoryRegionOps pnv_xive2_ic_sync_ops;
static const MemoryRegionOps pnv_xive2_ic_lsi_ops;
static const MemoryRegionOps pnv_xive2_ic_tm_indirect_ops;

/* 512 pages. 4K: 2M range, 64K: 32M range */
static const PnvXive2Region pnv_xive2_ic_regions[] = {
    { "xive-ic-cq",        0,   1,   &pnv_xive2_ic_cq_ops     },
    { "xive-ic-vc",        1,   1,   &pnv_xive2_ic_vc_ops     },
    { "xive-ic-pc",        2,   1,   &pnv_xive2_ic_pc_ops     },
    { "xive-ic-tctxt",     3,   1,   &pnv_xive2_ic_tctxt_ops  },
    { "xive-ic-notify",    4,   1,   &pnv_xive2_ic_notify_ops },
    /* page 5 reserved */
    { "xive-ic-sync",      6,   2,   &pnv_xive2_ic_sync_ops   },
    { "xive-ic-lsi",       8,   2,   &pnv_xive2_ic_lsi_ops    },
    /* pages 10-255 reserved */
    { "xive-ic-tm-indirect", 256, 128, &pnv_xive2_ic_tm_indirect_ops  },
    /* pages 384-511 reserved */
};

/*
 * CQ operations
 */

static uint64_t pnv_xive2_ic_cq_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint32_t reg = offset >> 3;
    uint64_t val = 0;

    switch (offset) {
    case CQ_XIVE_CAP: /* Set at reset */
    case CQ_XIVE_CFG:
        val = xive->cq_regs[reg];
        break;
    case CQ_MSGSND: /* TODO check the #cores of the machine */
        val = 0xffffffff00000000;
        break;
    case CQ_CFG_PB_GEN:
        val = CQ_CFG_PB_GEN_PB_INIT; /* TODO: fix CQ_CFG_PB_GEN default value */
        break;
    default:
        xive2_error(xive, "CQ: invalid read @%"HWADDR_PRIx, offset);
    }

    return val;
}

static uint64_t pnv_xive2_bar_size(uint64_t val)
{
    return 1ull << (GETFIELD(CQ_BAR_RANGE, val) + 24);
}

static void pnv_xive2_ic_cq_write(void *opaque, hwaddr offset,
                                  uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    MemoryRegion *sysmem = get_system_memory();
    uint32_t reg = offset >> 3;
    int i;

    switch (offset) {
    case CQ_XIVE_CFG:
    case CQ_RST_CTL: /* TODO: reset all BARs */
        break;

    case CQ_IC_BAR:
        xive->ic_shift = val & CQ_IC_BAR_64K ? 16 : 12;
        if (!(val & CQ_IC_BAR_VALID)) {
            xive->ic_base = 0;
            if (xive->cq_regs[reg] & CQ_IC_BAR_VALID) {
                for (i = 0; i < ARRAY_SIZE(xive->ic_mmios); i++) {
                    memory_region_del_subregion(&xive->ic_mmio,
                                                &xive->ic_mmios[i]);
                }
                memory_region_del_subregion(sysmem, &xive->ic_mmio);
            }
        } else {
            xive->ic_base = val & ~(CQ_IC_BAR_VALID | CQ_IC_BAR_64K);
            if (!(xive->cq_regs[reg] & CQ_IC_BAR_VALID)) {
                for (i = 0; i < ARRAY_SIZE(xive->ic_mmios); i++) {
                    memory_region_add_subregion(&xive->ic_mmio,
                               pnv_xive2_ic_regions[i].pgoff << xive->ic_shift,
                               &xive->ic_mmios[i]);
                }
                memory_region_add_subregion(sysmem, xive->ic_base,
                                            &xive->ic_mmio);
            }
        }
        break;

    case CQ_TM_BAR:
        xive->tm_shift = val & CQ_TM_BAR_64K ? 16 : 12;
        if (!(val & CQ_TM_BAR_VALID)) {
            xive->tm_base = 0;
            if (xive->cq_regs[reg] & CQ_TM_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->tm_mmio);
            }
        } else {
            xive->tm_base = val & ~(CQ_TM_BAR_VALID | CQ_TM_BAR_64K);
            if (!(xive->cq_regs[reg] & CQ_TM_BAR_VALID)) {
                memory_region_add_subregion(sysmem, xive->tm_base,
                                            &xive->tm_mmio);
            }
        }
        break;

    case CQ_ESB_BAR:
        xive->esb_shift = val & CQ_BAR_64K ? 16 : 12;
        if (!(val & CQ_BAR_VALID)) {
            xive->esb_base = 0;
            if (xive->cq_regs[reg] & CQ_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->esb_mmio);
            }
        } else {
            xive->esb_base = val & CQ_BAR_ADDR;
            if (!(xive->cq_regs[reg] & CQ_BAR_VALID)) {
                memory_region_set_size(&xive->esb_mmio,
                                       pnv_xive2_bar_size(val));
                memory_region_add_subregion(sysmem, xive->esb_base,
                                            &xive->esb_mmio);
            }
        }
        break;

    case CQ_END_BAR:
        xive->end_shift = val & CQ_BAR_64K ? 16 : 12;
        if (!(val & CQ_BAR_VALID)) {
            xive->end_base = 0;
            if (xive->cq_regs[reg] & CQ_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->end_mmio);
            }
        } else {
            xive->end_base = val & CQ_BAR_ADDR;
            if (!(xive->cq_regs[reg] & CQ_BAR_VALID)) {
                memory_region_set_size(&xive->end_mmio,
                                       pnv_xive2_bar_size(val));
                memory_region_add_subregion(sysmem, xive->end_base,
                                            &xive->end_mmio);
            }
        }
        break;

    case CQ_NVC_BAR:
        xive->nvc_shift = val & CQ_BAR_64K ? 16 : 12;
        if (!(val & CQ_BAR_VALID)) {
            xive->nvc_base = 0;
            if (xive->cq_regs[reg] & CQ_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->nvc_mmio);
            }
        } else {
            xive->nvc_base = val & CQ_BAR_ADDR;
            if (!(xive->cq_regs[reg] & CQ_BAR_VALID)) {
                memory_region_set_size(&xive->nvc_mmio,
                                       pnv_xive2_bar_size(val));
                memory_region_add_subregion(sysmem, xive->nvc_base,
                                            &xive->nvc_mmio);
            }
        }
        break;

    case CQ_NVPG_BAR:
        xive->nvpg_shift = val & CQ_BAR_64K ? 16 : 12;
        if (!(val & CQ_BAR_VALID)) {
            xive->nvpg_base = 0;
            if (xive->cq_regs[reg] & CQ_BAR_VALID) {
                memory_region_del_subregion(sysmem, &xive->nvpg_mmio);
            }
        } else {
            xive->nvpg_base = val & CQ_BAR_ADDR;
            if (!(xive->cq_regs[reg] & CQ_BAR_VALID)) {
                memory_region_set_size(&xive->nvpg_mmio,
                                       pnv_xive2_bar_size(val));
                memory_region_add_subregion(sysmem, xive->nvpg_base,
                                            &xive->nvpg_mmio);
            }
        }
        break;

    case CQ_TAR: /* Set Translation Table Address */
        break;
    case CQ_TDR: /* Set Translation Table Data */
        pnv_xive2_stt_set_data(xive, val);
        break;
    case CQ_FIRMASK_OR: /* FIR error reporting */
        break;
    default:
        xive2_error(xive, "CQ: invalid write 0x%"HWADDR_PRIx, offset);
        return;
    }

    xive->cq_regs[reg] = val;
}

static const MemoryRegionOps pnv_xive2_ic_cq_ops = {
    .read = pnv_xive2_ic_cq_read,
    .write = pnv_xive2_ic_cq_write,
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

static uint64_t pnv_xive2_ic_vc_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint64_t val = 0;
    uint32_t reg = offset >> 3;

    switch (offset) {
    /*
     * VSD table settings.
     */
    case VC_VSD_TABLE_ADDR:
    case VC_VSD_TABLE_DATA:
        val = xive->vc_regs[reg];
        break;

    /*
     * ESB cache updates (not modeled)
     */
    case VC_ESBC_FLUSH_CTRL:
        xive->vc_regs[reg] &= ~VC_ESBC_FLUSH_CTRL_POLL_VALID;
        val = xive->vc_regs[reg];
        break;

    /*
     * EAS cache updates (not modeled)
     */
    case VC_EASC_FLUSH_CTRL:
        xive->vc_regs[reg] &= ~VC_EASC_FLUSH_CTRL_POLL_VALID;
        val = xive->vc_regs[reg];
        break;

    /*
     * END cache updates
     */
    case VC_ENDC_WATCH0_SPEC:
        xive->vc_regs[reg] &= ~(VC_ENDC_WATCH_FULL | VC_ENDC_WATCH_CONFLICT);
        val = xive->vc_regs[reg];
        break;

    case VC_ENDC_WATCH0_DATA0:
        /*
         * Load DATA registers from cache with data requested by the
         * SPEC register
         */
        pnv_xive2_end_cache_load(xive);
        val = xive->vc_regs[reg];
        break;

    case VC_ENDC_WATCH0_DATA1 ... VC_ENDC_WATCH0_DATA3:
        val = xive->vc_regs[reg];
        break;

    case VC_ENDC_FLUSH_CTRL:
        xive->vc_regs[reg] &= ~VC_ENDC_FLUSH_CTRL_POLL_VALID;
        val = xive->vc_regs[reg];
        break;

    /*
     * Indirect invalidation
     */
    case VC_AT_MACRO_KILL_MASK:
        val = xive->vc_regs[reg];
        break;

    case VC_AT_MACRO_KILL:
        xive->vc_regs[reg] &= ~VC_AT_MACRO_KILL_VALID;
        val = xive->vc_regs[reg];
        break;

    /*
     * Interrupt fifo overflow in memory backing store (Not modeled)
     */
    case VC_QUEUES_CFG_REM0 ... VC_QUEUES_CFG_REM6:
        val = xive->vc_regs[reg];
        break;

    /*
     * Synchronisation
     */
    case VC_ENDC_SYNC_DONE:
        val = VC_ENDC_SYNC_POLL_DONE;
        break;
    default:
        xive2_error(xive, "VC: invalid read @%"HWADDR_PRIx, offset);
    }

    return val;
}

static void pnv_xive2_ic_vc_write(void *opaque, hwaddr offset,
                                  uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint32_t reg = offset >> 3;

    switch (offset) {
    /*
     * VSD table settings.
     */
    case VC_VSD_TABLE_ADDR:
       break;
    case VC_VSD_TABLE_DATA:
        pnv_xive2_vst_set_data(xive, val);
        break;

    /*
     * ESB cache updates (not modeled)
     */
    /* case VC_ESBC_FLUSH_CTRL: */
    case VC_ESBC_FLUSH_POLL:
        xive->vc_regs[VC_ESBC_FLUSH_CTRL >> 3] |= VC_ESBC_FLUSH_CTRL_POLL_VALID;
        /* ESB update */
        break;

    /*
     * EAS cache updates (not modeled)
     */
    /* case VC_EASC_FLUSH_CTRL: */
    case VC_EASC_FLUSH_POLL:
        xive->vc_regs[VC_EASC_FLUSH_CTRL >> 3] |= VC_EASC_FLUSH_CTRL_POLL_VALID;
        /* EAS update */
        break;

    /*
     * END cache updates
     */
    case VC_ENDC_WATCH0_SPEC:
         val &= ~VC_ENDC_WATCH_CONFLICT; /* HW will set this bit */
        break;

    case VC_ENDC_WATCH0_DATA1 ... VC_ENDC_WATCH0_DATA3:
        break;
    case VC_ENDC_WATCH0_DATA0:
        /* writing to DATA0 triggers the cache write */
        xive->vc_regs[reg] = val;
        pnv_xive2_end_update(xive);
        break;


    /* case VC_ENDC_FLUSH_CTRL: */
    case VC_ENDC_FLUSH_POLL:
        xive->vc_regs[VC_ENDC_FLUSH_CTRL >> 3] |= VC_ENDC_FLUSH_CTRL_POLL_VALID;
        break;

    /*
     * Indirect invalidation
     */
    case VC_AT_MACRO_KILL:
    case VC_AT_MACRO_KILL_MASK:
        break;

    /*
     * Interrupt fifo overflow in memory backing store (Not modeled)
     */
    case VC_QUEUES_CFG_REM0 ... VC_QUEUES_CFG_REM6:
        break;

    /*
     * Synchronisation
     */
    case VC_ENDC_SYNC_DONE:
        break;

    default:
        xive2_error(xive, "VC: invalid write @%"HWADDR_PRIx, offset);
        return;
    }

    xive->vc_regs[reg] = val;
}

static const MemoryRegionOps pnv_xive2_ic_vc_ops = {
    .read = pnv_xive2_ic_vc_read,
    .write = pnv_xive2_ic_vc_write,
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

static uint64_t pnv_xive2_ic_pc_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint64_t val = -1;
    uint32_t reg = offset >> 3;

    switch (offset) {
    /*
     * VSD table settings.
     */
    case PC_VSD_TABLE_ADDR:
    case PC_VSD_TABLE_DATA:
        val = xive->pc_regs[reg];
        break;

    /*
     * cache updates
     */
    case PC_NXC_WATCH0_SPEC:
        xive->pc_regs[reg] &= ~(PC_NXC_WATCH_FULL | PC_NXC_WATCH_CONFLICT);
        val = xive->pc_regs[reg];
        break;

    case PC_NXC_WATCH0_DATA0:
       /*
        * Load DATA registers from cache with data requested by the
        * SPEC register
        */
        pnv_xive2_nvp_cache_load(xive);
        val = xive->pc_regs[reg];
        break;

    case PC_NXC_WATCH0_DATA1 ... PC_NXC_WATCH0_DATA3:
        val = xive->pc_regs[reg];
        break;

    case PC_NXC_FLUSH_CTRL:
        xive->pc_regs[reg] &= ~PC_NXC_FLUSH_CTRL_POLL_VALID;
        val = xive->pc_regs[reg];
        break;

    /*
     * Indirect invalidation
     */
    case PC_AT_KILL:
        xive->pc_regs[reg] &= ~PC_AT_KILL_VALID;
        val = xive->pc_regs[reg];
        break;

    default:
        xive2_error(xive, "PC: invalid read @%"HWADDR_PRIx, offset);
    }

    return val;
}

static void pnv_xive2_ic_pc_write(void *opaque, hwaddr offset,
                                  uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint32_t reg = offset >> 3;

    switch (offset) {

    /*
     * VSD table settings. Only taken into account in the VC
     * sub-engine because the Xive2Router model combines both VC and PC
     * sub-engines
     */
    case PC_VSD_TABLE_ADDR:
    case PC_VSD_TABLE_DATA:
        break;

    /*
     * cache updates
     */
    case PC_NXC_WATCH0_SPEC:
        val &= ~PC_NXC_WATCH_CONFLICT; /* HW will set this bit */
        break;

    case PC_NXC_WATCH0_DATA1 ... PC_NXC_WATCH0_DATA3:
        break;
    case PC_NXC_WATCH0_DATA0:
        /* writing to DATA0 triggers the cache write */
        xive->pc_regs[reg] = val;
        pnv_xive2_nvp_update(xive);
        break;

   /* case PC_NXC_FLUSH_CTRL: */
    case PC_NXC_FLUSH_POLL:
        xive->pc_regs[PC_NXC_FLUSH_CTRL >> 3] |= PC_NXC_FLUSH_CTRL_POLL_VALID;
        break;

    /*
     * Indirect invalidation
     */
    case PC_AT_KILL:
    case PC_AT_KILL_MASK:
        break;

    default:
        xive2_error(xive, "PC: invalid write @%"HWADDR_PRIx, offset);
        return;
    }

    xive->pc_regs[reg] = val;
}

static const MemoryRegionOps pnv_xive2_ic_pc_ops = {
    .read = pnv_xive2_ic_pc_read,
    .write = pnv_xive2_ic_pc_write,
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


static uint64_t pnv_xive2_ic_tctxt_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint64_t val = -1;
    uint32_t reg = offset >> 3;

    switch (offset) {
    /*
     * XIVE2 hardware thread enablement
     */
    case TCTXT_EN0:
    case TCTXT_EN1:
        val = xive->tctxt_regs[reg];
        break;

    case TCTXT_EN0_SET:
    case TCTXT_EN0_RESET:
        val = xive->tctxt_regs[TCTXT_EN0 >> 3];
        break;
    case TCTXT_EN1_SET:
    case TCTXT_EN1_RESET:
        val = xive->tctxt_regs[TCTXT_EN1 >> 3];
        break;
    default:
        xive2_error(xive, "TCTXT: invalid read @%"HWADDR_PRIx, offset);
    }

    return val;
}

static void pnv_xive2_ic_tctxt_write(void *opaque, hwaddr offset,
                                     uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint32_t reg = offset >> 3;

    switch (offset) {
    /*
     * XIVE2 hardware thread enablement
     */
    case TCTXT_EN0: /* Physical Thread Enable */
    case TCTXT_EN1: /* Physical Thread Enable (fused core) */
        break;

    case TCTXT_EN0_SET:
        xive->tctxt_regs[TCTXT_EN0 >> 3] |= val;
        break;
    case TCTXT_EN1_SET:
        xive->tctxt_regs[TCTXT_EN1 >> 3] |= val;
        break;
    case TCTXT_EN0_RESET:
        xive->tctxt_regs[TCTXT_EN0 >> 3] &= ~val;
        break;
    case TCTXT_EN1_RESET:
        xive->tctxt_regs[TCTXT_EN1 >> 3] &= ~val;
        break;

    default:
        xive2_error(xive, "TCTXT: invalid write @%"HWADDR_PRIx, offset);
        return;
    }

    xive->pc_regs[reg] = val;
}

static const MemoryRegionOps pnv_xive2_ic_tctxt_ops = {
    .read = pnv_xive2_ic_tctxt_read,
    .write = pnv_xive2_ic_tctxt_write,
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
 * Redirect XSCOM to MMIO handlers
 */
static uint64_t pnv_xive2_xscom_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint64_t val = -1;
    uint32_t xscom_reg = offset >> 3;
    uint32_t mmio_offset = (xscom_reg & 0xFF) << 3;

    switch (xscom_reg) {
    case 0x000 ... 0x0FF:
        val = pnv_xive2_ic_cq_read(opaque, mmio_offset, size);
        break;
    case 0x100 ... 0x1FF:
        val = pnv_xive2_ic_vc_read(opaque, mmio_offset, size);
        break;
    case 0x200 ... 0x2FF:
        val = pnv_xive2_ic_pc_read(opaque, mmio_offset, size);
        break;
    case 0x300 ... 0x3FF:
        val = pnv_xive2_ic_tctxt_read(opaque, mmio_offset, size);
        break;
    default:
        xive2_error(xive, "XSCOM: invalid read @%"HWADDR_PRIx, offset);
    }

    return val;
}

static void pnv_xive2_xscom_write(void *opaque, hwaddr offset,
                                  uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint32_t xscom_reg = offset >> 3;
    uint32_t mmio_offset = (xscom_reg & 0xFF) << 3;

    switch (xscom_reg) {
    case 0x000 ... 0x0FF:
        pnv_xive2_ic_cq_write(opaque, mmio_offset, val, size);
        break;
    case 0x100 ... 0x1FF:
        pnv_xive2_ic_vc_write(opaque, mmio_offset, val, size);
        break;
    case 0x200 ... 0x2FF:
        pnv_xive2_ic_pc_write(opaque, mmio_offset, val, size);
        break;
    case 0x300 ... 0x3FF:
        pnv_xive2_ic_tctxt_write(opaque, mmio_offset, val, size);
        break;
    default:
        xive2_error(xive, "XSCOM: invalid write @%"HWADDR_PRIx, offset);
    }
}

static const MemoryRegionOps pnv_xive2_xscom_ops = {
    .read = pnv_xive2_xscom_read,
    .write = pnv_xive2_xscom_write,
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
 * Notify port page. The layout is compatible between 4K and 64K pages :
 *
 * Page 1           Notify page (writes only)
 *  0x000 - 0x7FF   IPI interrupt (NPU)
 *  0x800 - 0xFFF   HW interrupt triggers (PSI, PHB)
 */

static void pnv_xive2_ic_hw_trigger(PnvXive2 *xive, hwaddr addr,
                                    uint64_t val)
{
    uint8_t blk;
    uint32_t idx;

    if (val & XIVE_TRIGGER_END) {
        xive2_error(xive, "IC: END trigger at @0x%"HWADDR_PRIx" data 0x%"PRIx64,
                   addr, val);
        return;
    }

    /*
     * Forward the source event notification directly to the Router.
     * The source interrupt number should already be correctly encoded
     * with the chip block id by the sending device (PHB, PSI).
     */
    blk = XIVE_EAS_BLOCK(val);
    idx = XIVE_EAS_INDEX(val);

    xive2_router_notify(XIVE_NOTIFIER(xive), XIVE_EAS(blk, idx),
                         !!(val & XIVE_TRIGGER_PQ));
}

static void pnv_xive2_ic_notify_write(void *opaque, hwaddr offset,
                                      uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

    /* VC: IPI triggers */
    switch (offset) {
    case 0x000 ... 0x7FF:
        /* TODO: check IPI notify sub-page routing */
        pnv_xive2_ic_hw_trigger(opaque, offset, val);
        break;

    /* VC: HW triggers */
    case 0x800 ... 0xFFF:
        pnv_xive2_ic_hw_trigger(opaque, offset, val);
        break;

    default:
        xive2_error(xive, "NOTIFY: invalid write @%"HWADDR_PRIx, offset);
    }
}

static uint64_t pnv_xive2_ic_notify_read(void *opaque, hwaddr offset,
                                         unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

   /* loads are invalid */
    xive2_error(xive, "NOTIFY: invalid read @%"HWADDR_PRIx, offset);
    return -1;
}

static const MemoryRegionOps pnv_xive2_ic_notify_ops = {
    .read = pnv_xive2_ic_notify_read,
    .write = pnv_xive2_ic_notify_write,
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

static uint64_t pnv_xive2_ic_lsi_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

    xive2_error(xive, "LSI: invalid read @%"HWADDR_PRIx, offset);
    return -1;
}

static void pnv_xive2_ic_lsi_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

    xive2_error(xive, "LSI: invalid write @%"HWADDR_PRIx, offset);
}

static const MemoryRegionOps pnv_xive2_ic_lsi_ops = {
    .read = pnv_xive2_ic_lsi_read,
    .write = pnv_xive2_ic_lsi_write,
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
 * Sync MMIO page (write only)
 */
#define PNV_XIVE2_SYNC_IPI      0x000
#define PNV_XIVE2_SYNC_HW       0x080
#define PNV_XIVE2_SYNC_NxC      0x100
#define PNV_XIVE2_SYNC_INT      0x180
#define PNV_XIVE2_SYNC_OS_ESC   0x200
#define PNV_XIVE2_SYNC_POOL_ESC 0x280
#define PNV_XIVE2_SYNC_HARD_ESC 0x300

static uint64_t pnv_xive2_ic_sync_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

    /* loads are invalid */
    xive2_error(xive, "SYNC: invalid read @%"HWADDR_PRIx, offset);
    return -1;
}

static void pnv_xive2_ic_sync_write(void *opaque, hwaddr offset,
                                    uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

    switch (offset) {
    case PNV_XIVE2_SYNC_IPI:
    case PNV_XIVE2_SYNC_HW:
    case PNV_XIVE2_SYNC_NxC:
    case PNV_XIVE2_SYNC_INT:
    case PNV_XIVE2_SYNC_OS_ESC:
    case PNV_XIVE2_SYNC_POOL_ESC:
    case PNV_XIVE2_SYNC_HARD_ESC:
        break;
    default:
        xive2_error(xive, "SYNC: invalid write @%"HWADDR_PRIx, offset);
    }
}

static const MemoryRegionOps pnv_xive2_ic_sync_ops = {
    .read = pnv_xive2_ic_sync_read,
    .write = pnv_xive2_ic_sync_write,
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
 * When the TM direct pages of the IC controller are accessed, the
 * target HW thread is deduced from the page offset.
 */
static XiveTCTX *pnv_xive2_get_indirect_tctx(PnvXive2 *xive, uint32_t pir)
{
    PnvChip *chip = xive->chip;
    PowerPCCPU *cpu = NULL;

    cpu = pnv_chip_find_cpu(chip, pir);
    if (!cpu) {
        xive2_error(xive, "IC: invalid PIR %x for indirect access", pir);
        return NULL;
    }

    if (!pnv_xive2_is_cpu_enabled(xive, cpu)) {
        xive2_error(xive, "IC: CPU %x is not enabled", pir);
    }

    return XIVE_TCTX(pnv_cpu_state(cpu)->intc);
}

static uint64_t pnv_xive2_ic_tm_indirect_read(void *opaque, hwaddr offset,
                                              unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint32_t pir = offset >> xive->ic_shift;
    XiveTCTX *tctx = pnv_xive2_get_indirect_tctx(xive, pir);
    uint64_t val = -1;

    if (tctx) {
        val = xive_tctx_tm_read(NULL, tctx, offset, size);
    }

    return val;
}

static void pnv_xive2_ic_tm_indirect_write(void *opaque, hwaddr offset,
                                           uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);
    uint32_t pir = offset >> xive->ic_shift;
    XiveTCTX *tctx = pnv_xive2_get_indirect_tctx(xive, pir);

    if (tctx) {
        xive_tctx_tm_write(NULL, tctx, offset, val, size);
    }
}

static const MemoryRegionOps pnv_xive2_ic_tm_indirect_ops = {
    .read = pnv_xive2_ic_tm_indirect_read,
    .write = pnv_xive2_ic_tm_indirect_write,
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
 * TIMA ops
 */

/*
 * Special TIMA offsets to handle accesses in a POWER10 way.
 *
 * Only the CAM line updates done by the hypervisor should be handled
 * specifically.
 */
#define HV_PAGE_OFFSET         (XIVE_TM_HV_PAGE << TM_SHIFT)
#define HV_PUSH_OS_CTX_OFFSET  (HV_PAGE_OFFSET | (TM_QW1_OS + TM_WORD2))
#define HV_PULL_OS_CTX_OFFSET  (HV_PAGE_OFFSET | TM_SPC_PULL_OS_CTX)

static void pnv_xive2_tm_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    PnvXive2 *xive = pnv_xive2_tm_get_xive(cpu);
    XiveTCTX *tctx = XIVE_TCTX(pnv_cpu_state(cpu)->intc);
    XivePresenter *xptr = XIVE_PRESENTER(xive);
    bool gen1_tima_os =
        xive->cq_regs[CQ_XIVE_CFG >> 3] & CQ_XIVE_CFG_GEN1_TIMA_OS;

    /* TODO: should we switch the TM ops table instead ? */
    if (!gen1_tima_os && offset == HV_PUSH_OS_CTX_OFFSET) {
        xive2_tm_push_os_ctx(xptr, tctx, offset, value, size);
        return;
    }

    /* Other TM ops are the same as XIVE1 */
    xive_tctx_tm_write(xptr, tctx, offset, value, size);
}

static uint64_t pnv_xive2_tm_read(void *opaque, hwaddr offset, unsigned size)
{
    PowerPCCPU *cpu = POWERPC_CPU(current_cpu);
    PnvXive2 *xive = pnv_xive2_tm_get_xive(cpu);
    XiveTCTX *tctx = XIVE_TCTX(pnv_cpu_state(cpu)->intc);
    XivePresenter *xptr = XIVE_PRESENTER(xive);
    bool gen1_tima_os =
        xive->cq_regs[CQ_XIVE_CFG >> 3] & CQ_XIVE_CFG_GEN1_TIMA_OS;

    /* TODO: should we switch the TM ops table instead ? */
    if (!gen1_tima_os && offset == HV_PULL_OS_CTX_OFFSET) {
        return xive2_tm_pull_os_ctx(xptr, tctx, offset, size);
    }

    /* Other TM ops are the same as XIVE1 */
    return xive_tctx_tm_read(xptr, tctx, offset, size);
}

static const MemoryRegionOps pnv_xive2_tm_ops = {
    .read = pnv_xive2_tm_read,
    .write = pnv_xive2_tm_write,
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

static uint64_t pnv_xive2_nvc_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

    xive2_error(xive, "NVC: invalid read @%"HWADDR_PRIx, offset);
    return -1;
}

static void pnv_xive2_nvc_write(void *opaque, hwaddr offset,
                                uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

    xive2_error(xive, "NVC: invalid write @%"HWADDR_PRIx, offset);
}

static const MemoryRegionOps pnv_xive2_nvc_ops = {
    .read = pnv_xive2_nvc_read,
    .write = pnv_xive2_nvc_write,
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

static uint64_t pnv_xive2_nvpg_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

    xive2_error(xive, "NVPG: invalid read @%"HWADDR_PRIx, offset);
    return -1;
}

static void pnv_xive2_nvpg_write(void *opaque, hwaddr offset,
                                 uint64_t val, unsigned size)
{
    PnvXive2 *xive = PNV_XIVE2(opaque);

    xive2_error(xive, "NVPG: invalid write @%"HWADDR_PRIx, offset);
}

static const MemoryRegionOps pnv_xive2_nvpg_ops = {
    .read = pnv_xive2_nvpg_read,
    .write = pnv_xive2_nvpg_write,
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
 * POWER10 default capabilities: 0x2000120076f000FC
 */
#define PNV_XIVE2_CAPABILITIES  0x2000120076f000FC

/*
 * POWER10 default configuration: 0x0030000033000000
 *
 * 8bits thread id was dropped for P10
 */
#define PNV_XIVE2_CONFIGURATION 0x0030000033000000

static void pnv_xive2_reset(void *dev)
{
    PnvXive2 *xive = PNV_XIVE2(dev);
    XiveSource *xsrc = &xive->ipi_source;
    Xive2EndSource *end_xsrc = &xive->end_source;

    xive->cq_regs[CQ_XIVE_CAP >> 3] = xive->capabilities;
    xive->cq_regs[CQ_XIVE_CFG >> 3] = xive->config;

    /* HW hardwires the #Topology of the chip in the block field */
    xive->cq_regs[CQ_XIVE_CFG >> 3] |=
        SETFIELD(CQ_XIVE_CFG_HYP_HARD_BLOCK_ID, 0ull, xive->chip->chip_id);

    /* Set default page size to 64k */
    xive->ic_shift = xive->esb_shift = xive->end_shift = 16;
    xive->nvc_shift = xive->nvpg_shift = xive->tm_shift = 16;

    /* Clear source MMIOs */
    if (memory_region_is_mapped(&xsrc->esb_mmio)) {
        memory_region_del_subregion(&xive->esb_mmio, &xsrc->esb_mmio);
    }

    if (memory_region_is_mapped(&end_xsrc->esb_mmio)) {
        memory_region_del_subregion(&xive->end_mmio, &end_xsrc->esb_mmio);
    }
}

/*
 *  Maximum number of IRQs and ENDs supported by HW. Will be tuned by
 *  software.
 */
#define PNV_XIVE2_NR_IRQS (PNV10_XIVE2_ESB_SIZE / (1ull << XIVE_ESB_64K_2PAGE))
#define PNV_XIVE2_NR_ENDS (PNV10_XIVE2_END_SIZE / (1ull << XIVE_ESB_64K_2PAGE))

static void pnv_xive2_realize(DeviceState *dev, Error **errp)
{
    PnvXive2 *xive = PNV_XIVE2(dev);
    PnvXive2Class *pxc = PNV_XIVE2_GET_CLASS(dev);
    XiveSource *xsrc = &xive->ipi_source;
    Xive2EndSource *end_xsrc = &xive->end_source;
    Error *local_err = NULL;
    int i;

    pxc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    assert(xive->chip);

    /*
     * The XiveSource and Xive2EndSource objects are realized with the
     * maximum allowed HW configuration. The ESB MMIO regions will be
     * resized dynamically when the controller is configured by the FW
     * to limit accesses to resources not provisioned.
     */
    object_property_set_int(OBJECT(xsrc), "flags", XIVE_SRC_STORE_EOI,
                            &error_fatal);
    object_property_set_int(OBJECT(xsrc), "nr-irqs", PNV_XIVE2_NR_IRQS,
                            &error_fatal);
    object_property_set_link(OBJECT(xsrc), "xive", OBJECT(xive),
                             &error_fatal);
    qdev_realize(DEVICE(xsrc), NULL, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    object_property_set_int(OBJECT(end_xsrc), "nr-ends", PNV_XIVE2_NR_ENDS,
                            &error_fatal);
    object_property_set_link(OBJECT(end_xsrc), "xive", OBJECT(xive),
                             &error_abort);
    qdev_realize(DEVICE(end_xsrc), NULL, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* XSCOM region, used for initial configuration of the BARs */
    memory_region_init_io(&xive->xscom_regs, OBJECT(dev),
                          &pnv_xive2_xscom_ops, xive, "xscom-xive",
                          PNV10_XSCOM_XIVE2_SIZE << 3);

    /* Interrupt controller MMIO regions */
    xive->ic_shift = 16;
    memory_region_init(&xive->ic_mmio, OBJECT(dev), "xive-ic",
                       PNV10_XIVE2_IC_SIZE);

    for (i = 0; i < ARRAY_SIZE(xive->ic_mmios); i++) {
        memory_region_init_io(&xive->ic_mmios[i], OBJECT(dev),
                         pnv_xive2_ic_regions[i].ops, xive,
                         pnv_xive2_ic_regions[i].name,
                         pnv_xive2_ic_regions[i].pgsize << xive->ic_shift);
    }

    /*
     * VC MMIO regions.
     */
    xive->esb_shift = 16;
    xive->end_shift = 16;
    memory_region_init(&xive->esb_mmio, OBJECT(xive), "xive-esb",
                       PNV10_XIVE2_ESB_SIZE);
    memory_region_init(&xive->end_mmio, OBJECT(xive), "xive-end",
                       PNV10_XIVE2_END_SIZE);

    /* Presenter Controller MMIO region (not modeled) */
    xive->nvc_shift = 16;
    xive->nvpg_shift = 16;
    memory_region_init_io(&xive->nvc_mmio, OBJECT(dev),
                          &pnv_xive2_nvc_ops, xive,
                          "xive-nvc", PNV10_XIVE2_NVC_SIZE);

    memory_region_init_io(&xive->nvpg_mmio, OBJECT(dev),
                          &pnv_xive2_nvpg_ops, xive,
                          "xive-nvpg", PNV10_XIVE2_NVPG_SIZE);

    /* Thread Interrupt Management Area (Direct) */
    xive->tm_shift = 16;
    memory_region_init_io(&xive->tm_mmio, OBJECT(dev), &pnv_xive2_tm_ops,
                          xive, "xive-tima", PNV10_XIVE2_TM_SIZE);

    qemu_register_reset(pnv_xive2_reset, dev);
}

static Property pnv_xive2_properties[] = {
    DEFINE_PROP_UINT64("ic-bar", PnvXive2, ic_base, 0),
    DEFINE_PROP_UINT64("esb-bar", PnvXive2, esb_base, 0),
    DEFINE_PROP_UINT64("end-bar", PnvXive2, end_base, 0),
    DEFINE_PROP_UINT64("nvc-bar", PnvXive2, nvc_base, 0),
    DEFINE_PROP_UINT64("nvpg-bar", PnvXive2, nvpg_base, 0),
    DEFINE_PROP_UINT64("tm-bar", PnvXive2, tm_base, 0),
    DEFINE_PROP_UINT64("capabilities", PnvXive2, capabilities,
                       PNV_XIVE2_CAPABILITIES),
    DEFINE_PROP_UINT64("config", PnvXive2, config,
                       PNV_XIVE2_CONFIGURATION),
    DEFINE_PROP_LINK("chip", PnvXive2, chip, TYPE_PNV_CHIP, PnvChip *),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_xive2_instance_init(Object *obj)
{
    PnvXive2 *xive = PNV_XIVE2(obj);

    object_initialize_child(obj, "ipi_source", &xive->ipi_source,
                            TYPE_XIVE_SOURCE);
    object_initialize_child(obj, "end_source", &xive->end_source,
                            TYPE_XIVE2_END_SOURCE);
}

static int pnv_xive2_dt_xscom(PnvXScomInterface *dev, void *fdt,
                              int xscom_offset)
{
    const char compat_p10[] = "ibm,power10-xive-x";
    char *name;
    int offset;
    uint32_t reg[] = {
        cpu_to_be32(PNV10_XSCOM_XIVE2_BASE),
        cpu_to_be32(PNV10_XSCOM_XIVE2_SIZE)
    };

    name = g_strdup_printf("xive@%x", PNV10_XSCOM_XIVE2_BASE);
    offset = fdt_add_subnode(fdt, xscom_offset, name);
    _FDT(offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));
    _FDT(fdt_setprop(fdt, offset, "compatible", compat_p10,
                     sizeof(compat_p10)));
    return 0;
}

static void pnv_xive2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);
    Xive2RouterClass *xrc = XIVE2_ROUTER_CLASS(klass);
    XiveNotifierClass *xnc = XIVE_NOTIFIER_CLASS(klass);
    XivePresenterClass *xpc = XIVE_PRESENTER_CLASS(klass);
    PnvXive2Class *pxc = PNV_XIVE2_CLASS(klass);

    xdc->dt_xscom  = pnv_xive2_dt_xscom;

    dc->desc       = "PowerNV XIVE2 Interrupt Controller (POWER10)";
    device_class_set_parent_realize(dc, pnv_xive2_realize,
                                    &pxc->parent_realize);
    device_class_set_props(dc, pnv_xive2_properties);

    xrc->get_eas   = pnv_xive2_get_eas;
    xrc->get_pq    = pnv_xive2_get_pq;
    xrc->set_pq    = pnv_xive2_set_pq;
    xrc->get_end   = pnv_xive2_get_end;
    xrc->write_end = pnv_xive2_write_end;
    xrc->get_nvp   = pnv_xive2_get_nvp;
    xrc->write_nvp = pnv_xive2_write_nvp;
    xrc->get_config  = pnv_xive2_get_config;
    xrc->get_block_id = pnv_xive2_get_block_id;

    xnc->notify    = pnv_xive2_notify;

    xpc->match_nvt  = pnv_xive2_match_nvt;
};

static const TypeInfo pnv_xive2_info = {
    .name          = TYPE_PNV_XIVE2,
    .parent        = TYPE_XIVE2_ROUTER,
    .instance_init = pnv_xive2_instance_init,
    .instance_size = sizeof(PnvXive2),
    .class_init    = pnv_xive2_class_init,
    .class_size    = sizeof(PnvXive2Class),
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_xive2_register_types(void)
{
    type_register_static(&pnv_xive2_info);
}

type_init(pnv_xive2_register_types)

static void xive2_nvp_pic_print_info(Xive2Nvp *nvp, uint32_t nvp_idx,
                                     Monitor *mon)
{
    uint8_t  eq_blk = xive_get_field32(NVP2_W5_VP_END_BLOCK, nvp->w5);
    uint32_t eq_idx = xive_get_field32(NVP2_W5_VP_END_INDEX, nvp->w5);

    if (!xive2_nvp_is_valid(nvp)) {
        return;
    }

    monitor_printf(mon, "  %08x end:%02x/%04x IPB:%02x",
                   nvp_idx, eq_blk, eq_idx,
                   xive_get_field32(NVP2_W2_IPB, nvp->w2));
    /*
     * When the NVP is HW controlled, more fields are updated
     */
    if (xive2_nvp_is_hw(nvp)) {
        monitor_printf(mon, " CPPR:%02x",
                       xive_get_field32(NVP2_W2_CPPR, nvp->w2));
        if (xive2_nvp_is_co(nvp)) {
            monitor_printf(mon, " CO:%04x",
                           xive_get_field32(NVP2_W1_CO_THRID, nvp->w1));
        }
    }
    monitor_printf(mon, "\n");
}

/*
 * If the table is direct, we can compute the number of PQ entries
 * provisioned by FW.
 */
static uint32_t pnv_xive2_nr_esbs(PnvXive2 *xive)
{
    uint8_t blk = pnv_xive2_block_id(xive);
    uint64_t vsd = xive->vsds[VST_ESB][blk];
    uint64_t vst_tsize = 1ull << (GETFIELD(VSD_TSIZE, vsd) + 12);

    return VSD_INDIRECT & vsd ? 0 : vst_tsize * SBE_PER_BYTE;
}

/*
 * Compute the number of entries per indirect subpage.
 */
static uint64_t pnv_xive2_vst_per_subpage(PnvXive2 *xive, uint32_t type)
{
    uint8_t blk = pnv_xive2_block_id(xive);
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
    ldq_be_dma(&address_space_memory, vsd_addr, &vsd, MEMTXATTRS_UNSPECIFIED);

    if (!(vsd & VSD_ADDRESS_MASK)) {
#ifdef XIVE2_DEBUG
        xive2_error(xive, "VST: invalid %s entry!?", info->name);
#endif
        return 0;
    }

    page_shift = GETFIELD(VSD_TSIZE, vsd) + 12;

    if (!pnv_xive2_vst_page_size_allowed(page_shift)) {
        xive2_error(xive, "VST: invalid %s page shift %d", info->name,
                   page_shift);
        return 0;
    }

    return (1ull << page_shift) / info->size;
}

void pnv_xive2_pic_print_info(PnvXive2 *xive, Monitor *mon)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xive);
    uint8_t blk = pnv_xive2_block_id(xive);
    uint8_t chip_id = xive->chip->chip_id;
    uint32_t srcno0 = XIVE_EAS(blk, 0);
    uint32_t nr_esbs = pnv_xive2_nr_esbs(xive);
    Xive2Eas eas;
    Xive2End end;
    Xive2Nvp nvp;
    int i;
    uint64_t xive_nvp_per_subpage;

    monitor_printf(mon, "XIVE[%x] Source %08x .. %08x\n", blk, srcno0,
                   srcno0 + nr_esbs - 1);
    xive_source_pic_print_info(&xive->ipi_source, srcno0, mon);

    monitor_printf(mon, "XIVE[%x] EAT %08x .. %08x\n", blk, srcno0,
                   srcno0 + nr_esbs - 1);
    for (i = 0; i < nr_esbs; i++) {
        if (xive2_router_get_eas(xrtr, blk, i, &eas)) {
            break;
        }
        if (!xive2_eas_is_masked(&eas)) {
            xive2_eas_pic_print_info(&eas, i, mon);
        }
    }

    monitor_printf(mon, "XIVE[%x] #%d END Escalation EAT\n", chip_id, blk);
    i = 0;
    while (!xive2_router_get_end(xrtr, blk, i, &end)) {
        xive2_end_eas_pic_print_info(&end, i++, mon);
    }

    monitor_printf(mon, "XIVE[%x] #%d ENDT\n", chip_id, blk);
    i = 0;
    while (!xive2_router_get_end(xrtr, blk, i, &end)) {
        xive2_end_pic_print_info(&end, i++, mon);
    }

    monitor_printf(mon, "XIVE[%x] #%d NVPT %08x .. %08x\n", chip_id, blk,
                   0, XIVE2_NVP_COUNT - 1);
    xive_nvp_per_subpage = pnv_xive2_vst_per_subpage(xive, VST_NVP);
    for (i = 0; i < XIVE2_NVP_COUNT; i += xive_nvp_per_subpage) {
        while (!xive2_router_get_nvp(xrtr, blk, i, &nvp)) {
            xive2_nvp_pic_print_info(&nvp, i++, mon);
        }
    }
}
