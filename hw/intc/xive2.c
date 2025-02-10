/*
 * QEMU PowerPC XIVE2 interrupt controller model (POWER10)
 *
 * Copyright (c) 2019-2022, IBM Corporation..
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "system/cpus.h"
#include "system/dma.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/xive.h"
#include "hw/ppc/xive2.h"
#include "hw/ppc/xive2_regs.h"

uint32_t xive2_router_get_config(Xive2Router *xrtr)
{
    Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

    return xrc->get_config(xrtr);
}

static int xive2_router_get_block_id(Xive2Router *xrtr)
{
   Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

   return xrc->get_block_id(xrtr);
}

static uint64_t xive2_nvp_reporting_addr(Xive2Nvp *nvp)
{
    uint64_t cache_addr;

    cache_addr = xive_get_field32(NVP2_W6_REPORTING_LINE, nvp->w6) << 24 |
        xive_get_field32(NVP2_W7_REPORTING_LINE, nvp->w7);
    cache_addr <<= 8; /* aligned on a cache line pair */
    return cache_addr;
}

static uint32_t xive2_nvgc_get_backlog(Xive2Nvgc *nvgc, uint8_t priority)
{
    uint32_t val = 0;
    uint8_t *ptr, i;

    if (priority > 7) {
        return 0;
    }

    /*
     * The per-priority backlog counters are 24-bit and the structure
     * is stored in big endian
     */
    ptr = (uint8_t *)&nvgc->w2 + priority * 3;
    for (i = 0; i < 3; i++, ptr++) {
        val = (val << 8) + *ptr;
    }
    return val;
}

void xive2_eas_pic_print_info(Xive2Eas *eas, uint32_t lisn, GString *buf)
{
    if (!xive2_eas_is_valid(eas)) {
        return;
    }

    g_string_append_printf(buf, "  %08x %s end:%02x/%04x data:%08x\n",
                           lisn, xive2_eas_is_masked(eas) ? "M" : " ",
                           (uint8_t)  xive_get_field64(EAS2_END_BLOCK, eas->w),
                           (uint32_t) xive_get_field64(EAS2_END_INDEX, eas->w),
                           (uint32_t) xive_get_field64(EAS2_END_DATA, eas->w));
}

void xive2_end_queue_pic_print_info(Xive2End *end, uint32_t width, GString *buf)
{
    uint64_t qaddr_base = xive2_end_qaddr(end);
    uint32_t qsize = xive_get_field32(END2_W3_QSIZE, end->w3);
    uint32_t qindex = xive_get_field32(END2_W1_PAGE_OFF, end->w1);
    uint32_t qentries = 1 << (qsize + 10);
    int i;

    /*
     * print out the [ (qindex - (width - 1)) .. (qindex + 1)] window
     */
    g_string_append_printf(buf, " [ ");
    qindex = (qindex - (width - 1)) & (qentries - 1);
    for (i = 0; i < width; i++) {
        uint64_t qaddr = qaddr_base + (qindex << 2);
        uint32_t qdata = -1;

        if (dma_memory_read(&address_space_memory, qaddr, &qdata,
                            sizeof(qdata), MEMTXATTRS_UNSPECIFIED)) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: failed to read EQ @0x%"
                          HWADDR_PRIx "\n", qaddr);
            return;
        }
        g_string_append_printf(buf, "%s%08x ", i == width - 1 ? "^" : "",
                               be32_to_cpu(qdata));
        qindex = (qindex + 1) & (qentries - 1);
    }
    g_string_append_printf(buf, "]");
}

void xive2_end_pic_print_info(Xive2End *end, uint32_t end_idx, GString *buf)
{
    uint64_t qaddr_base = xive2_end_qaddr(end);
    uint32_t qindex = xive_get_field32(END2_W1_PAGE_OFF, end->w1);
    uint32_t qgen = xive_get_field32(END2_W1_GENERATION, end->w1);
    uint32_t qsize = xive_get_field32(END2_W3_QSIZE, end->w3);
    uint32_t qentries = 1 << (qsize + 10);

    uint32_t nvp_blk = xive_get_field32(END2_W6_VP_BLOCK, end->w6);
    uint32_t nvp_idx = xive_get_field32(END2_W6_VP_OFFSET, end->w6);
    uint8_t priority = xive_get_field32(END2_W7_F0_PRIORITY, end->w7);
    uint8_t pq;

    if (!xive2_end_is_valid(end)) {
        return;
    }

    pq = xive_get_field32(END2_W1_ESn, end->w1);

    g_string_append_printf(buf,
                           "  %08x %c%c %c%c%c%c%c%c%c%c%c%c%c %c%c "
                           "prio:%d nvp:%02x/%04x",
                           end_idx,
                           pq & XIVE_ESB_VAL_P ? 'P' : '-',
                           pq & XIVE_ESB_VAL_Q ? 'Q' : '-',
                           xive2_end_is_valid(end)    ? 'v' : '-',
                           xive2_end_is_enqueue(end)  ? 'q' : '-',
                           xive2_end_is_notify(end)   ? 'n' : '-',
                           xive2_end_is_backlog(end)  ? 'b' : '-',
                           xive2_end_is_precluded_escalation(end) ? 'p' : '-',
                           xive2_end_is_escalate(end) ? 'e' : '-',
                           xive2_end_is_escalate_end(end) ? 'N' : '-',
                           xive2_end_is_uncond_escalation(end)   ? 'u' : '-',
                           xive2_end_is_silent_escalation(end)   ? 's' : '-',
                           xive2_end_is_firmware1(end)   ? 'f' : '-',
                           xive2_end_is_firmware2(end)   ? 'F' : '-',
                           xive2_end_is_ignore(end) ? 'i' : '-',
                           xive2_end_is_crowd(end)  ? 'c' : '-',
                           priority, nvp_blk, nvp_idx);

    if (qaddr_base) {
        g_string_append_printf(buf, " eq:@%08"PRIx64"% 6d/%5d ^%d",
                               qaddr_base, qindex, qentries, qgen);
        xive2_end_queue_pic_print_info(end, 6, buf);
    }
    g_string_append_c(buf, '\n');
}

void xive2_end_eas_pic_print_info(Xive2End *end, uint32_t end_idx,
                                  GString *buf)
{
    Xive2Eas *eas = (Xive2Eas *) &end->w4;
    uint8_t pq;

    if (!xive2_end_is_escalate(end)) {
        return;
    }

    pq = xive_get_field32(END2_W1_ESe, end->w1);

    g_string_append_printf(buf, "  %08x %c%c %c%c end:%02x/%04x data:%08x\n",
                           end_idx,
                           pq & XIVE_ESB_VAL_P ? 'P' : '-',
                           pq & XIVE_ESB_VAL_Q ? 'Q' : '-',
                           xive2_eas_is_valid(eas) ? 'v' : ' ',
                           xive2_eas_is_masked(eas) ? 'M' : ' ',
                           (uint8_t)  xive_get_field64(EAS2_END_BLOCK, eas->w),
                           (uint32_t) xive_get_field64(EAS2_END_INDEX, eas->w),
                           (uint32_t) xive_get_field64(EAS2_END_DATA, eas->w));
}

void xive2_nvp_pic_print_info(Xive2Nvp *nvp, uint32_t nvp_idx, GString *buf)
{
    uint8_t  eq_blk = xive_get_field32(NVP2_W5_VP_END_BLOCK, nvp->w5);
    uint32_t eq_idx = xive_get_field32(NVP2_W5_VP_END_INDEX, nvp->w5);
    uint64_t cache_line = xive2_nvp_reporting_addr(nvp);

    if (!xive2_nvp_is_valid(nvp)) {
        return;
    }

    g_string_append_printf(buf, "  %08x end:%02x/%04x IPB:%02x PGoFirst:%02x",
                           nvp_idx, eq_blk, eq_idx,
                           xive_get_field32(NVP2_W2_IPB, nvp->w2),
                           xive_get_field32(NVP2_W0_PGOFIRST, nvp->w0));
    if (cache_line) {
        g_string_append_printf(buf, "  reporting CL:%016"PRIx64, cache_line);
    }

    /*
     * When the NVP is HW controlled, more fields are updated
     */
    if (xive2_nvp_is_hw(nvp)) {
        g_string_append_printf(buf, " CPPR:%02x",
                               xive_get_field32(NVP2_W2_CPPR, nvp->w2));
        if (xive2_nvp_is_co(nvp)) {
            g_string_append_printf(buf, " CO:%04x",
                                   xive_get_field32(NVP2_W1_CO_THRID, nvp->w1));
        }
    }
    g_string_append_c(buf, '\n');
}

void xive2_nvgc_pic_print_info(Xive2Nvgc *nvgc, uint32_t nvgc_idx, GString *buf)
{
    uint8_t i;

    if (!xive2_nvgc_is_valid(nvgc)) {
        return;
    }

    g_string_append_printf(buf, "  %08x PGoNext:%02x bklog: ", nvgc_idx,
                           xive_get_field32(NVGC2_W0_PGONEXT, nvgc->w0));
    for (i = 0; i <= XIVE_PRIORITY_MAX; i++) {
        g_string_append_printf(buf, "[%d]=0x%x ",
                               i, xive2_nvgc_get_backlog(nvgc, i));
    }
    g_string_append_printf(buf, "\n");
}

static void xive2_end_enqueue(Xive2End *end, uint32_t data)
{
    uint64_t qaddr_base = xive2_end_qaddr(end);
    uint32_t qsize = xive_get_field32(END2_W3_QSIZE, end->w3);
    uint32_t qindex = xive_get_field32(END2_W1_PAGE_OFF, end->w1);
    uint32_t qgen = xive_get_field32(END2_W1_GENERATION, end->w1);

    uint64_t qaddr = qaddr_base + (qindex << 2);
    uint32_t qdata = cpu_to_be32((qgen << 31) | (data & 0x7fffffff));
    uint32_t qentries = 1 << (qsize + 10);

    if (dma_memory_write(&address_space_memory, qaddr, &qdata, sizeof(qdata),
                         MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: failed to write END data @0x%"
                      HWADDR_PRIx "\n", qaddr);
        return;
    }

    qindex = (qindex + 1) & (qentries - 1);
    if (qindex == 0) {
        qgen ^= 1;
        end->w1 = xive_set_field32(END2_W1_GENERATION, end->w1, qgen);

        /* TODO(PowerNV): reset GF bit on a cache watch operation */
        end->w1 = xive_set_field32(END2_W1_GEN_FLIPPED, end->w1, qgen);
    }
    end->w1 = xive_set_field32(END2_W1_PAGE_OFF, end->w1, qindex);
}

/*
 * XIVE Thread Interrupt Management Area (TIMA) - Gen2 mode
 *
 * TIMA Gen2 VP “save & restore” (S&R) indicated by H bit next to V bit
 *
 *   - if a context is enabled with the H bit set, the VP context
 *     information is retrieved from the NVP structure (“check out”)
 *     and stored back on a context pull (“check in”), the SW receives
 *     the same context pull information as on P9
 *
 *   - the H bit cannot be changed while the V bit is set, i.e. a
 *     context cannot be set up in the TIMA and then be “pushed” into
 *     the NVP by changing the H bit while the context is enabled
 */

static void xive2_tctx_save_ctx(Xive2Router *xrtr, XiveTCTX *tctx,
                                uint8_t nvp_blk, uint32_t nvp_idx,
                                uint8_t ring)
{
    CPUPPCState *env = &POWERPC_CPU(tctx->cs)->env;
    uint32_t pir = env->spr_cb[SPR_PIR].default_value;
    Xive2Nvp nvp;
    uint8_t *regs = &tctx->regs[ring];

    if (xive2_router_get_nvp(xrtr, nvp_blk, nvp_idx, &nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No NVP %x/%x\n",
                          nvp_blk, nvp_idx);
        return;
    }

    if (!xive2_nvp_is_valid(&nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid NVP %x/%x\n",
                      nvp_blk, nvp_idx);
        return;
    }

    if (!xive2_nvp_is_hw(&nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: NVP %x/%x is not HW owned\n",
                      nvp_blk, nvp_idx);
        return;
    }

    if (!xive2_nvp_is_co(&nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: NVP %x/%x is not checkout\n",
                      nvp_blk, nvp_idx);
        return;
    }

    if (xive_get_field32(NVP2_W1_CO_THRID_VALID, nvp.w1) &&
        xive_get_field32(NVP2_W1_CO_THRID, nvp.w1) != pir) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "XIVE: NVP %x/%x invalid checkout Thread %x\n",
                      nvp_blk, nvp_idx, pir);
        return;
    }

    nvp.w2 = xive_set_field32(NVP2_W2_IPB, nvp.w2, regs[TM_IPB]);
    nvp.w2 = xive_set_field32(NVP2_W2_CPPR, nvp.w2, regs[TM_CPPR]);
    nvp.w2 = xive_set_field32(NVP2_W2_LSMFB, nvp.w2, regs[TM_LSMFB]);
    xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, &nvp, 2);

    nvp.w1 = xive_set_field32(NVP2_W1_CO, nvp.w1, 0);
    /* NVP2_W1_CO_THRID_VALID only set once */
    nvp.w1 = xive_set_field32(NVP2_W1_CO_THRID, nvp.w1, 0xFFFF);
    xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, &nvp, 1);
}

static void xive2_cam_decode(uint32_t cam, uint8_t *nvp_blk,
                             uint32_t *nvp_idx, bool *valid, bool *hw)
{
    *nvp_blk = xive2_nvp_blk(cam);
    *nvp_idx = xive2_nvp_idx(cam);
    *valid = !!(cam & TM2_W2_VALID);
    *hw = !!(cam & TM2_W2_HW);
}

/*
 * Encode the HW CAM line with 7bit or 8bit thread id. The thread id
 * width and block id width is configurable at the IC level.
 *
 *    chipid << 24 | 0000 0000 0000 0000 1 threadid (7Bit)
 *    chipid << 24 | 0000 0000 0000 0001 threadid   (8Bit)
 */
static uint32_t xive2_tctx_hw_cam_line(XivePresenter *xptr, XiveTCTX *tctx)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xptr);
    CPUPPCState *env = &POWERPC_CPU(tctx->cs)->env;
    uint32_t pir = env->spr_cb[SPR_PIR].default_value;
    uint8_t blk = xive2_router_get_block_id(xrtr);
    uint8_t tid_shift =
        xive2_router_get_config(xrtr) & XIVE2_THREADID_8BITS ? 8 : 7;
    uint8_t tid_mask = (1 << tid_shift) - 1;

    return xive2_nvp_cam_line(blk, 1 << tid_shift | (pir & tid_mask));
}

static uint64_t xive2_tm_pull_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                                  hwaddr offset, unsigned size, uint8_t ring)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xptr);
    uint32_t target_ringw2 = xive_tctx_word2(&tctx->regs[ring]);
    uint32_t cam = be32_to_cpu(target_ringw2);
    uint8_t nvp_blk;
    uint32_t nvp_idx;
    uint8_t cur_ring;
    bool valid;
    bool do_save;

    xive2_cam_decode(cam, &nvp_blk, &nvp_idx, &valid, &do_save);

    if (!valid) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: pulling invalid NVP %x/%x !?\n",
                      nvp_blk, nvp_idx);
    }

    /* Invalidate CAM line of requested ring and all lower rings */
    for (cur_ring = TM_QW0_USER; cur_ring <= ring;
         cur_ring += XIVE_TM_RING_SIZE) {
        uint32_t ringw2 = xive_tctx_word2(&tctx->regs[cur_ring]);
        uint32_t ringw2_new = xive_set_field32(TM2_QW1W2_VO, ringw2, 0);
        memcpy(&tctx->regs[cur_ring + TM_WORD2], &ringw2_new, 4);
    }

    if (xive2_router_get_config(xrtr) & XIVE2_VP_SAVE_RESTORE && do_save) {
        xive2_tctx_save_ctx(xrtr, tctx, nvp_blk, nvp_idx, ring);
    }

    /*
     * Lower external interrupt line of requested ring and below except for
     * USER, which doesn't exist.
     */
    for (cur_ring = TM_QW1_OS; cur_ring <= ring;
         cur_ring += XIVE_TM_RING_SIZE) {
        xive_tctx_reset_signal(tctx, cur_ring);
    }
    return target_ringw2;
}

uint64_t xive2_tm_pull_os_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                              hwaddr offset, unsigned size)
{
    return xive2_tm_pull_ctx(xptr, tctx, offset, size, TM_QW1_OS);
}

#define REPORT_LINE_GEN1_SIZE       16

static void xive2_tm_report_line_gen1(XiveTCTX *tctx, uint8_t *data,
                                      uint8_t size)
{
    uint8_t *regs = tctx->regs;

    g_assert(size == REPORT_LINE_GEN1_SIZE);
    memset(data, 0, size);
    /*
     * See xive architecture for description of what is saved. It is
     * hand-picked information to fit in 16 bytes.
     */
    data[0x0] = regs[TM_QW3_HV_PHYS + TM_NSR];
    data[0x1] = regs[TM_QW3_HV_PHYS + TM_CPPR];
    data[0x2] = regs[TM_QW3_HV_PHYS + TM_IPB];
    data[0x3] = regs[TM_QW2_HV_POOL + TM_IPB];
    data[0x4] = regs[TM_QW1_OS + TM_ACK_CNT];
    data[0x5] = regs[TM_QW3_HV_PHYS + TM_LGS];
    data[0x6] = 0xFF;
    data[0x7] = regs[TM_QW3_HV_PHYS + TM_WORD2] & 0x80;
    data[0x7] |= (regs[TM_QW2_HV_POOL + TM_WORD2] & 0x80) >> 1;
    data[0x7] |= (regs[TM_QW1_OS + TM_WORD2] & 0x80) >> 2;
    data[0x7] |= (regs[TM_QW3_HV_PHYS + TM_WORD2] & 0x3);
    data[0x8] = regs[TM_QW1_OS + TM_NSR];
    data[0x9] = regs[TM_QW1_OS + TM_CPPR];
    data[0xA] = regs[TM_QW1_OS + TM_IPB];
    data[0xB] = regs[TM_QW1_OS + TM_LGS];
    if (regs[TM_QW0_USER + TM_WORD2] & 0x80) {
        /*
         * Logical server extension, except VU bit replaced by EB bit
         * from NSR
         */
        data[0xC] = regs[TM_QW0_USER + TM_WORD2];
        data[0xC] &= ~0x80;
        data[0xC] |= regs[TM_QW0_USER + TM_NSR] & 0x80;
        data[0xD] = regs[TM_QW0_USER + TM_WORD2 + 1];
        data[0xE] = regs[TM_QW0_USER + TM_WORD2 + 2];
        data[0xF] = regs[TM_QW0_USER + TM_WORD2 + 3];
    }
}

static void xive2_tm_pull_ctx_ol(XivePresenter *xptr, XiveTCTX *tctx,
                                 hwaddr offset, uint64_t value,
                                 unsigned size, uint8_t ring)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xptr);
    uint32_t hw_cam, nvp_idx, xive2_cfg, reserved;
    uint8_t nvp_blk;
    Xive2Nvp nvp;
    uint64_t phys_addr;
    MemTxResult result;

    hw_cam = xive2_tctx_hw_cam_line(xptr, tctx);
    nvp_blk = xive2_nvp_blk(hw_cam);
    nvp_idx = xive2_nvp_idx(hw_cam);

    if (xive2_router_get_nvp(xrtr, nvp_blk, nvp_idx, &nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No NVP %x/%x\n",
                      nvp_blk, nvp_idx);
        return;
    }

    if (!xive2_nvp_is_valid(&nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid NVP %x/%x\n",
                      nvp_blk, nvp_idx);
        return;
    }

    xive2_cfg = xive2_router_get_config(xrtr);

    phys_addr = xive2_nvp_reporting_addr(&nvp) + 0x80; /* odd line */
    if (xive2_cfg & XIVE2_GEN1_TIMA_OS) {
        uint8_t pull_ctxt[REPORT_LINE_GEN1_SIZE];

        xive2_tm_report_line_gen1(tctx, pull_ctxt, REPORT_LINE_GEN1_SIZE);
        result = dma_memory_write(&address_space_memory, phys_addr,
                                  pull_ctxt, REPORT_LINE_GEN1_SIZE,
                                  MEMTXATTRS_UNSPECIFIED);
        assert(result == MEMTX_OK);
    } else {
        result = dma_memory_write(&address_space_memory, phys_addr,
                                  &tctx->regs, sizeof(tctx->regs),
                                  MEMTXATTRS_UNSPECIFIED);
        assert(result == MEMTX_OK);
        reserved = 0xFFFFFFFF;
        result = dma_memory_write(&address_space_memory, phys_addr + 12,
                                  &reserved, sizeof(reserved),
                                  MEMTXATTRS_UNSPECIFIED);
        assert(result == MEMTX_OK);
    }

    /* the rest is similar to pull context to registers */
    xive2_tm_pull_ctx(xptr, tctx, offset, size, ring);
}

void xive2_tm_pull_os_ctx_ol(XivePresenter *xptr, XiveTCTX *tctx,
                             hwaddr offset, uint64_t value, unsigned size)
{
    xive2_tm_pull_ctx_ol(xptr, tctx, offset, value, size, TM_QW1_OS);
}


void xive2_tm_pull_phys_ctx_ol(XivePresenter *xptr, XiveTCTX *tctx,
                               hwaddr offset, uint64_t value, unsigned size)
{
    xive2_tm_pull_ctx_ol(xptr, tctx, offset, value, size, TM_QW3_HV_PHYS);
}

static uint8_t xive2_tctx_restore_os_ctx(Xive2Router *xrtr, XiveTCTX *tctx,
                                        uint8_t nvp_blk, uint32_t nvp_idx,
                                        Xive2Nvp *nvp)
{
    CPUPPCState *env = &POWERPC_CPU(tctx->cs)->env;
    uint32_t pir = env->spr_cb[SPR_PIR].default_value;
    uint8_t cppr;

    if (!xive2_nvp_is_hw(nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: NVP %x/%x is not HW owned\n",
                      nvp_blk, nvp_idx);
        return 0;
    }

    cppr = xive_get_field32(NVP2_W2_CPPR, nvp->w2);
    nvp->w2 = xive_set_field32(NVP2_W2_CPPR, nvp->w2, 0);
    xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, nvp, 2);

    tctx->regs[TM_QW1_OS + TM_CPPR] = cppr;
    /* we don't model LSMFB */

    nvp->w1 = xive_set_field32(NVP2_W1_CO, nvp->w1, 1);
    nvp->w1 = xive_set_field32(NVP2_W1_CO_THRID_VALID, nvp->w1, 1);
    nvp->w1 = xive_set_field32(NVP2_W1_CO_THRID, nvp->w1, pir);

    /*
     * Checkout privilege: 0:OS, 1:Pool, 2:Hard
     *
     * TODO: we only support OS push/pull
     */
    nvp->w1 = xive_set_field32(NVP2_W1_CO_PRIV, nvp->w1, 0);

    xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, nvp, 1);

    /* return restored CPPR to generate a CPU exception if needed */
    return cppr;
}

static void xive2_tctx_need_resend(Xive2Router *xrtr, XiveTCTX *tctx,
                                   uint8_t nvp_blk, uint32_t nvp_idx,
                                   bool do_restore)
{
    Xive2Nvp nvp;
    uint8_t ipb;

    /*
     * Grab the associated thread interrupt context registers in the
     * associated NVP
     */
    if (xive2_router_get_nvp(xrtr, nvp_blk, nvp_idx, &nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No NVP %x/%x\n",
                      nvp_blk, nvp_idx);
        return;
    }

    if (!xive2_nvp_is_valid(&nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid NVP %x/%x\n",
                      nvp_blk, nvp_idx);
        return;
    }

    /* Automatically restore thread context registers */
    if (xive2_router_get_config(xrtr) & XIVE2_VP_SAVE_RESTORE &&
        do_restore) {
        xive2_tctx_restore_os_ctx(xrtr, tctx, nvp_blk, nvp_idx, &nvp);
    }

    ipb = xive_get_field32(NVP2_W2_IPB, nvp.w2);
    if (ipb) {
        nvp.w2 = xive_set_field32(NVP2_W2_IPB, nvp.w2, 0);
        xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, &nvp, 2);
    }
    /*
     * Always call xive_tctx_ipb_update(). Even if there were no
     * escalation triggered, there could be a pending interrupt which
     * was saved when the context was pulled and that we need to take
     * into account by recalculating the PIPR (which is not
     * saved/restored).
     * It will also raise the External interrupt signal if needed.
     */
    xive_tctx_ipb_update(tctx, TM_QW1_OS, ipb);
}

/*
 * Updating the OS CAM line can trigger a resend of interrupt
 */
void xive2_tm_push_os_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                          hwaddr offset, uint64_t value, unsigned size)
{
    uint32_t cam;
    uint32_t qw1w2;
    uint64_t qw1dw1;
    uint8_t nvp_blk;
    uint32_t nvp_idx;
    bool vo;
    bool do_restore;

    /* First update the thead context */
    switch (size) {
    case 4:
        cam = value;
        qw1w2 = cpu_to_be32(cam);
        memcpy(&tctx->regs[TM_QW1_OS + TM_WORD2], &qw1w2, 4);
        break;
    case 8:
        cam = value >> 32;
        qw1dw1 = cpu_to_be64(value);
        memcpy(&tctx->regs[TM_QW1_OS + TM_WORD2], &qw1dw1, 8);
        break;
    default:
        g_assert_not_reached();
    }

    xive2_cam_decode(cam, &nvp_blk, &nvp_idx, &vo, &do_restore);

    /* Check the interrupt pending bits */
    if (vo) {
        xive2_tctx_need_resend(XIVE2_ROUTER(xptr), tctx, nvp_blk, nvp_idx,
                               do_restore);
    }
}

static void xive2_tctx_set_target(XiveTCTX *tctx, uint8_t ring, uint8_t target)
{
    uint8_t *regs = &tctx->regs[ring];

    regs[TM_T] = target;
}

void xive2_tm_set_hv_target(XivePresenter *xptr, XiveTCTX *tctx,
                            hwaddr offset, uint64_t value, unsigned size)
{
    xive2_tctx_set_target(tctx, TM_QW3_HV_PHYS, value & 0xff);
}

/*
 * XIVE Router (aka. Virtualization Controller or IVRE)
 */

int xive2_router_get_eas(Xive2Router *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                         Xive2Eas *eas)
{
    Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

    return xrc->get_eas(xrtr, eas_blk, eas_idx, eas);
}

static
int xive2_router_get_pq(Xive2Router *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                       uint8_t *pq)
{
    Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

    return xrc->get_pq(xrtr, eas_blk, eas_idx, pq);
}

static
int xive2_router_set_pq(Xive2Router *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                       uint8_t *pq)
{
    Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

    return xrc->set_pq(xrtr, eas_blk, eas_idx, pq);
}

int xive2_router_get_end(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                         Xive2End *end)
{
   Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

   return xrc->get_end(xrtr, end_blk, end_idx, end);
}

int xive2_router_write_end(Xive2Router *xrtr, uint8_t end_blk, uint32_t end_idx,
                           Xive2End *end, uint8_t word_number)
{
   Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

   return xrc->write_end(xrtr, end_blk, end_idx, end, word_number);
}

int xive2_router_get_nvp(Xive2Router *xrtr, uint8_t nvp_blk, uint32_t nvp_idx,
                         Xive2Nvp *nvp)
{
   Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

   return xrc->get_nvp(xrtr, nvp_blk, nvp_idx, nvp);
}

int xive2_router_write_nvp(Xive2Router *xrtr, uint8_t nvp_blk, uint32_t nvp_idx,
                           Xive2Nvp *nvp, uint8_t word_number)
{
   Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

   return xrc->write_nvp(xrtr, nvp_blk, nvp_idx, nvp, word_number);
}

int xive2_router_get_nvgc(Xive2Router *xrtr, bool crowd,
                          uint8_t nvgc_blk, uint32_t nvgc_idx,
                          Xive2Nvgc *nvgc)
{
   Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

   return xrc->get_nvgc(xrtr, crowd, nvgc_blk, nvgc_idx, nvgc);
}

int xive2_router_write_nvgc(Xive2Router *xrtr, bool crowd,
                            uint8_t nvgc_blk, uint32_t nvgc_idx,
                            Xive2Nvgc *nvgc)
{
   Xive2RouterClass *xrc = XIVE2_ROUTER_GET_CLASS(xrtr);

   return xrc->write_nvgc(xrtr, crowd, nvgc_blk, nvgc_idx, nvgc);
}

/*
 * The thread context register words are in big-endian format.
 */
int xive2_presenter_tctx_match(XivePresenter *xptr, XiveTCTX *tctx,
                               uint8_t format,
                               uint8_t nvt_blk, uint32_t nvt_idx,
                               bool cam_ignore, uint32_t logic_serv)
{
    uint32_t cam =   xive2_nvp_cam_line(nvt_blk, nvt_idx);
    uint32_t qw3w2 = xive_tctx_word2(&tctx->regs[TM_QW3_HV_PHYS]);
    uint32_t qw2w2 = xive_tctx_word2(&tctx->regs[TM_QW2_HV_POOL]);
    uint32_t qw1w2 = xive_tctx_word2(&tctx->regs[TM_QW1_OS]);
    uint32_t qw0w2 = xive_tctx_word2(&tctx->regs[TM_QW0_USER]);

    /*
     * TODO (PowerNV): ignore mode. The low order bits of the NVT
     * identifier are ignored in the "CAM" match.
     */

    if (format == 0) {
        if (cam_ignore == true) {
            /*
             * F=0 & i=1: Logical server notification (bits ignored at
             * the end of the NVT identifier)
             */
            qemu_log_mask(LOG_UNIMP, "XIVE: no support for LS NVT %x/%x\n",
                          nvt_blk, nvt_idx);
            return -1;
        }

        /* F=0 & i=0: Specific NVT notification */

        /* PHYS ring */
        if ((be32_to_cpu(qw3w2) & TM2_QW3W2_VT) &&
            cam == xive2_tctx_hw_cam_line(xptr, tctx)) {
            return TM_QW3_HV_PHYS;
        }

        /* HV POOL ring */
        if ((be32_to_cpu(qw2w2) & TM2_QW2W2_VP) &&
            cam == xive_get_field32(TM2_QW2W2_POOL_CAM, qw2w2)) {
            return TM_QW2_HV_POOL;
        }

        /* OS ring */
        if ((be32_to_cpu(qw1w2) & TM2_QW1W2_VO) &&
            cam == xive_get_field32(TM2_QW1W2_OS_CAM, qw1w2)) {
            return TM_QW1_OS;
        }
    } else {
        /* F=1 : User level Event-Based Branch (EBB) notification */

        /* USER ring */
        if  ((be32_to_cpu(qw1w2) & TM2_QW1W2_VO) &&
             (cam == xive_get_field32(TM2_QW1W2_OS_CAM, qw1w2)) &&
             (be32_to_cpu(qw0w2) & TM2_QW0W2_VU) &&
             (logic_serv == xive_get_field32(TM2_QW0W2_LOGIC_SERV, qw0w2))) {
            return TM_QW0_USER;
        }
    }
    return -1;
}

static void xive2_router_realize(DeviceState *dev, Error **errp)
{
    Xive2Router *xrtr = XIVE2_ROUTER(dev);

    assert(xrtr->xfb);
}

/*
 * Notification using the END ESe/ESn bit (Event State Buffer for
 * escalation and notification). Profide further coalescing in the
 * Router.
 */
static bool xive2_router_end_es_notify(Xive2Router *xrtr, uint8_t end_blk,
                                       uint32_t end_idx, Xive2End *end,
                                       uint32_t end_esmask)
{
    uint8_t pq = xive_get_field32(end_esmask, end->w1);
    bool notify = xive_esb_trigger(&pq);

    if (pq != xive_get_field32(end_esmask, end->w1)) {
        end->w1 = xive_set_field32(end_esmask, end->w1, pq);
        xive2_router_write_end(xrtr, end_blk, end_idx, end, 1);
    }

    /* ESe/n[Q]=1 : end of notification */
    return notify;
}

/*
 * An END trigger can come from an event trigger (IPI or HW) or from
 * another chip. We don't model the PowerBus but the END trigger
 * message has the same parameters than in the function below.
 */
static void xive2_router_end_notify(Xive2Router *xrtr, uint8_t end_blk,
                                    uint32_t end_idx, uint32_t end_data)
{
    Xive2End end;
    uint8_t priority;
    uint8_t format;
    bool found;
    Xive2Nvp nvp;
    uint8_t nvp_blk;
    uint32_t nvp_idx;

    /* END cache lookup */
    if (xive2_router_get_end(xrtr, end_blk, end_idx, &end)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No END %x/%x\n", end_blk,
                      end_idx);
        return;
    }

    if (!xive2_end_is_valid(&end)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: END %x/%x is invalid\n",
                      end_blk, end_idx);
        return;
    }

    if (xive2_end_is_enqueue(&end)) {
        xive2_end_enqueue(&end, end_data);
        /* Enqueuing event data modifies the EQ toggle and index */
        xive2_router_write_end(xrtr, end_blk, end_idx, &end, 1);
    }

    /*
     * When the END is silent, we skip the notification part.
     */
    if (xive2_end_is_silent_escalation(&end)) {
        goto do_escalation;
    }

    /*
     * The W7 format depends on the F bit in W6. It defines the type
     * of the notification :
     *
     *   F=0 : single or multiple NVP notification
     *   F=1 : User level Event-Based Branch (EBB) notification, no
     *         priority
     */
    format = xive_get_field32(END2_W6_FORMAT_BIT, end.w6);
    priority = xive_get_field32(END2_W7_F0_PRIORITY, end.w7);

    /* The END is masked */
    if (format == 0 && priority == 0xff) {
        return;
    }

    /*
     * Check the END ESn (Event State Buffer for notification) for
     * even further coalescing in the Router
     */
    if (!xive2_end_is_notify(&end)) {
        /* ESn[Q]=1 : end of notification */
        if (!xive2_router_end_es_notify(xrtr, end_blk, end_idx,
                                       &end, END2_W1_ESn)) {
            return;
        }
    }

    /*
     * Follows IVPE notification
     */
    nvp_blk = xive_get_field32(END2_W6_VP_BLOCK, end.w6);
    nvp_idx = xive_get_field32(END2_W6_VP_OFFSET, end.w6);

    /* NVP cache lookup */
    if (xive2_router_get_nvp(xrtr, nvp_blk, nvp_idx, &nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: no NVP %x/%x\n",
                      nvp_blk, nvp_idx);
        return;
    }

    if (!xive2_nvp_is_valid(&nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: NVP %x/%x is invalid\n",
                      nvp_blk, nvp_idx);
        return;
    }

    found = xive_presenter_notify(xrtr->xfb, format, nvp_blk, nvp_idx,
                          xive2_end_is_ignore(&end),
                          priority,
                          xive_get_field32(END2_W7_F1_LOG_SERVER_ID, end.w7));

    /* TODO: Auto EOI. */

    if (found) {
        return;
    }

    /*
     * If no matching NVP is dispatched on a HW thread :
     * - specific VP: update the NVP structure if backlog is activated
     * - logical server : forward request to IVPE (not supported)
     */
    if (xive2_end_is_backlog(&end)) {
        uint8_t ipb;

        if (format == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "XIVE: END %x/%x invalid config: F1 & backlog\n",
                          end_blk, end_idx);
            return;
        }

        /*
         * Record the IPB in the associated NVP structure for later
         * use. The presenter will resend the interrupt when the vCPU
         * is dispatched again on a HW thread.
         */
        ipb = xive_get_field32(NVP2_W2_IPB, nvp.w2) |
            xive_priority_to_ipb(priority);
        nvp.w2 = xive_set_field32(NVP2_W2_IPB, nvp.w2, ipb);
        xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, &nvp, 2);

        /*
         * On HW, follows a "Broadcast Backlog" to IVPEs
         */
    }

do_escalation:
    /*
     * If activated, escalate notification using the ESe PQ bits and
     * the EAS in w4-5
     */
    if (!xive2_end_is_escalate(&end)) {
        return;
    }

    /*
     * Check the END ESe (Event State Buffer for escalation) for even
     * further coalescing in the Router
     */
    if (!xive2_end_is_uncond_escalation(&end)) {
        /* ESe[Q]=1 : end of escalation notification */
        if (!xive2_router_end_es_notify(xrtr, end_blk, end_idx,
                                       &end, END2_W1_ESe)) {
            return;
        }
    }

    /*
     * The END trigger becomes an Escalation trigger
     */
    xive2_router_end_notify(xrtr,
                           xive_get_field32(END2_W4_END_BLOCK,     end.w4),
                           xive_get_field32(END2_W4_ESC_END_INDEX, end.w4),
                           xive_get_field32(END2_W5_ESC_END_DATA,  end.w5));
}

void xive2_router_notify(XiveNotifier *xn, uint32_t lisn, bool pq_checked)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xn);
    uint8_t eas_blk = XIVE_EAS_BLOCK(lisn);
    uint32_t eas_idx = XIVE_EAS_INDEX(lisn);
    Xive2Eas eas;

    /* EAS cache lookup */
    if (xive2_router_get_eas(xrtr, eas_blk, eas_idx, &eas)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Unknown LISN %x\n", lisn);
        return;
    }

    if (!pq_checked) {
        bool notify;
        uint8_t pq;

        /* PQ cache lookup */
        if (xive2_router_get_pq(xrtr, eas_blk, eas_idx, &pq)) {
            /* Set FIR */
            g_assert_not_reached();
        }

        notify = xive_esb_trigger(&pq);

        if (xive2_router_set_pq(xrtr, eas_blk, eas_idx, &pq)) {
            /* Set FIR */
            g_assert_not_reached();
        }

        if (!notify) {
            return;
        }
    }

    if (!xive2_eas_is_valid(&eas)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Invalid LISN %x\n", lisn);
        return;
    }

    if (xive2_eas_is_masked(&eas)) {
        /* Notification completed */
        return;
    }

    /*
     * The event trigger becomes an END trigger
     */
    xive2_router_end_notify(xrtr,
                             xive_get_field64(EAS2_END_BLOCK, eas.w),
                             xive_get_field64(EAS2_END_INDEX, eas.w),
                             xive_get_field64(EAS2_END_DATA,  eas.w));
}

static const Property xive2_router_properties[] = {
    DEFINE_PROP_LINK("xive-fabric", Xive2Router, xfb,
                     TYPE_XIVE_FABRIC, XiveFabric *),
};

static void xive2_router_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveNotifierClass *xnc = XIVE_NOTIFIER_CLASS(klass);

    dc->desc    = "XIVE2 Router Engine";
    device_class_set_props(dc, xive2_router_properties);
    /* Parent is SysBusDeviceClass. No need to call its realize hook */
    dc->realize = xive2_router_realize;
    xnc->notify = xive2_router_notify;
}

static const TypeInfo xive2_router_info = {
    .name          = TYPE_XIVE2_ROUTER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .abstract      = true,
    .instance_size = sizeof(Xive2Router),
    .class_size    = sizeof(Xive2RouterClass),
    .class_init    = xive2_router_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_XIVE_NOTIFIER },
        { TYPE_XIVE_PRESENTER },
        { }
    }
};

static inline bool addr_is_even(hwaddr addr, uint32_t shift)
{
    return !((addr >> shift) & 1);
}

static uint64_t xive2_end_source_read(void *opaque, hwaddr addr, unsigned size)
{
    Xive2EndSource *xsrc = XIVE2_END_SOURCE(opaque);
    uint32_t offset = addr & 0xFFF;
    uint8_t end_blk;
    uint32_t end_idx;
    Xive2End end;
    uint32_t end_esmask;
    uint8_t pq;
    uint64_t ret;

    /*
     * The block id should be deduced from the load address on the END
     * ESB MMIO but our model only supports a single block per XIVE chip.
     */
    end_blk = xive2_router_get_block_id(xsrc->xrtr);
    end_idx = addr >> (xsrc->esb_shift + 1);

    if (xive2_router_get_end(xsrc->xrtr, end_blk, end_idx, &end)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No END %x/%x\n", end_blk,
                      end_idx);
        return -1;
    }

    if (!xive2_end_is_valid(&end)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: END %x/%x is invalid\n",
                      end_blk, end_idx);
        return -1;
    }

    end_esmask = addr_is_even(addr, xsrc->esb_shift) ? END2_W1_ESn :
        END2_W1_ESe;
    pq = xive_get_field32(end_esmask, end.w1);

    switch (offset) {
    case XIVE_ESB_LOAD_EOI ... XIVE_ESB_LOAD_EOI + 0x7FF:
        ret = xive_esb_eoi(&pq);

        /* Forward the source event notification for routing ?? */
        break;

    case XIVE_ESB_GET ... XIVE_ESB_GET + 0x3FF:
        ret = pq;
        break;

    case XIVE_ESB_SET_PQ_00 ... XIVE_ESB_SET_PQ_00 + 0x0FF:
    case XIVE_ESB_SET_PQ_01 ... XIVE_ESB_SET_PQ_01 + 0x0FF:
    case XIVE_ESB_SET_PQ_10 ... XIVE_ESB_SET_PQ_10 + 0x0FF:
    case XIVE_ESB_SET_PQ_11 ... XIVE_ESB_SET_PQ_11 + 0x0FF:
        ret = xive_esb_set(&pq, (offset >> 8) & 0x3);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid END ESB load addr %d\n",
                      offset);
        return -1;
    }

    if (pq != xive_get_field32(end_esmask, end.w1)) {
        end.w1 = xive_set_field32(end_esmask, end.w1, pq);
        xive2_router_write_end(xsrc->xrtr, end_blk, end_idx, &end, 1);
    }

    return ret;
}

static void xive2_end_source_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size)
{
    Xive2EndSource *xsrc = XIVE2_END_SOURCE(opaque);
    uint32_t offset = addr & 0xFFF;
    uint8_t end_blk;
    uint32_t end_idx;
    Xive2End end;
    uint32_t end_esmask;
    uint8_t pq;
    bool notify = false;

    /*
     * The block id should be deduced from the load address on the END
     * ESB MMIO but our model only supports a single block per XIVE chip.
     */
    end_blk = xive2_router_get_block_id(xsrc->xrtr);
    end_idx = addr >> (xsrc->esb_shift + 1);

    if (xive2_router_get_end(xsrc->xrtr, end_blk, end_idx, &end)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No END %x/%x\n", end_blk,
                      end_idx);
        return;
    }

    if (!xive2_end_is_valid(&end)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: END %x/%x is invalid\n",
                      end_blk, end_idx);
        return;
    }

    end_esmask = addr_is_even(addr, xsrc->esb_shift) ? END2_W1_ESn :
        END2_W1_ESe;
    pq = xive_get_field32(end_esmask, end.w1);

    switch (offset) {
    case 0 ... 0x3FF:
        notify = xive_esb_trigger(&pq);
        break;

    case XIVE_ESB_STORE_EOI ... XIVE_ESB_STORE_EOI + 0x3FF:
        /* TODO: can we check StoreEOI availability from the router ? */
        notify = xive_esb_eoi(&pq);
        break;

    case XIVE_ESB_INJECT ... XIVE_ESB_INJECT + 0x3FF:
        if (end_esmask == END2_W1_ESe) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "XIVE: END %x/%x can not EQ inject on ESe\n",
                           end_blk, end_idx);
            return;
        }
        notify = true;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid END ESB write addr %d\n",
                      offset);
        return;
    }

    if (pq != xive_get_field32(end_esmask, end.w1)) {
        end.w1 = xive_set_field32(end_esmask, end.w1, pq);
        xive2_router_write_end(xsrc->xrtr, end_blk, end_idx, &end, 1);
    }

    /* TODO: Forward the source event notification for routing */
    if (notify) {
        ;
    }
}

static const MemoryRegionOps xive2_end_source_ops = {
    .read = xive2_end_source_read,
    .write = xive2_end_source_write,
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

static void xive2_end_source_realize(DeviceState *dev, Error **errp)
{
    Xive2EndSource *xsrc = XIVE2_END_SOURCE(dev);

    assert(xsrc->xrtr);

    if (!xsrc->nr_ends) {
        error_setg(errp, "Number of interrupt needs to be greater than 0");
        return;
    }

    if (xsrc->esb_shift != XIVE_ESB_4K &&
        xsrc->esb_shift != XIVE_ESB_64K) {
        error_setg(errp, "Invalid ESB shift setting");
        return;
    }

    /*
     * Each END is assigned an even/odd pair of MMIO pages, the even page
     * manages the ESn field while the odd page manages the ESe field.
     */
    memory_region_init_io(&xsrc->esb_mmio, OBJECT(xsrc),
                          &xive2_end_source_ops, xsrc, "xive.end",
                          (1ull << (xsrc->esb_shift + 1)) * xsrc->nr_ends);
}

static const Property xive2_end_source_properties[] = {
    DEFINE_PROP_UINT32("nr-ends", Xive2EndSource, nr_ends, 0),
    DEFINE_PROP_UINT32("shift", Xive2EndSource, esb_shift, XIVE_ESB_64K),
    DEFINE_PROP_LINK("xive", Xive2EndSource, xrtr, TYPE_XIVE2_ROUTER,
                     Xive2Router *),
};

static void xive2_end_source_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc    = "XIVE END Source";
    device_class_set_props(dc, xive2_end_source_properties);
    dc->realize = xive2_end_source_realize;
    dc->user_creatable = false;
}

static const TypeInfo xive2_end_source_info = {
    .name          = TYPE_XIVE2_END_SOURCE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(Xive2EndSource),
    .class_init    = xive2_end_source_class_init,
};

static void xive2_register_types(void)
{
    type_register_static(&xive2_router_info);
    type_register_static(&xive2_end_source_info);
}

type_init(xive2_register_types)
