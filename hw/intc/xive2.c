/*
 * QEMU PowerPC XIVE2 interrupt controller model (POWER10)
 *
 * Copyright (c) 2019-2024, IBM Corporation..
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include "trace.h"

static void xive2_router_end_notify(Xive2Router *xrtr, uint8_t end_blk,
                                    uint32_t end_idx, uint32_t end_data,
                                    bool redistribute);

static int xive2_tctx_get_nvp_indexes(XiveTCTX *tctx, uint8_t ring,
                                      uint8_t *nvp_blk, uint32_t *nvp_idx);

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
     * is stored in big endian. NVGC is 32-bytes long, so 24-bytes from
     * w2, which fits 8 priorities * 24-bits per priority.
     */
    ptr = (uint8_t *)&nvgc->w2 + priority * 3;
    for (i = 0; i < 3; i++, ptr++) {
        val = (val << 8) + *ptr;
    }
    return val;
}

static void xive2_nvgc_set_backlog(Xive2Nvgc *nvgc, uint8_t priority,
                                   uint32_t val)
{
    uint8_t *ptr, i;
    uint32_t shift;

    if (priority > 7) {
        return;
    }

    if (val > 0xFFFFFF) {
        val = 0xFFFFFF;
    }
    /*
     * The per-priority backlog counters are 24-bit and the structure
     * is stored in big endian
     */
    ptr = (uint8_t *)&nvgc->w2 + priority * 3;
    for (i = 0; i < 3; i++, ptr++) {
        shift = 8 * (2 - i);
        *ptr = (val >> shift) & 0xFF;
    }
}

static uint32_t xive2_nvgc_get_idx(uint32_t nvp_idx, uint8_t group)
{
    uint32_t nvgc_idx;

    if (group > 0) {
        nvgc_idx = (nvp_idx & (0xffffffffULL << group)) |
                   ((1 << (group - 1)) - 1);
    } else {
        nvgc_idx = nvp_idx;
    }

    return nvgc_idx;
}

static uint8_t xive2_nvgc_get_blk(uint8_t nvp_blk, uint8_t crowd)
{
    uint8_t nvgc_blk;

    if (crowd > 0) {
        crowd = (crowd == 3) ? 4 : crowd;
        nvgc_blk = (nvp_blk & (0xffffffffULL << crowd)) |
                   ((1 << (crowd - 1)) - 1);
    } else {
        nvgc_blk = nvp_blk;
    }

    return nvgc_blk;
}

uint64_t xive2_presenter_nvgc_backlog_op(XivePresenter *xptr,
                                         bool crowd,
                                         uint8_t blk, uint32_t idx,
                                         uint16_t offset, uint16_t val)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xptr);
    uint8_t priority = GETFIELD(NVx_BACKLOG_PRIO, offset);
    uint8_t op = GETFIELD(NVx_BACKLOG_OP, offset);
    Xive2Nvgc nvgc;
    uint32_t count, old_count;

    if (xive2_router_get_nvgc(xrtr, crowd, blk, idx, &nvgc)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No %s %x/%x\n",
                      crowd ? "NVC" : "NVG", blk, idx);
        return -1;
    }
    if (!xive2_nvgc_is_valid(&nvgc)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Invalid NVG %x/%x\n", blk, idx);
        return -1;
    }

    old_count = xive2_nvgc_get_backlog(&nvgc, priority);
    count = old_count;
    /*
     * op:
     * 0b00 => increment
     * 0b01 => decrement
     * 0b1- => read
     */
    if (op == 0b00 || op == 0b01) {
        if (op == 0b00) {
            count += val;
        } else {
            if (count > val) {
                count -= val;
            } else {
                count = 0;
            }
        }
        xive2_nvgc_set_backlog(&nvgc, priority, count);
        xive2_router_write_nvgc(xrtr, crowd, blk, idx, &nvgc);
    }
    trace_xive_nvgc_backlog_op(crowd, blk, idx, op, priority, old_count);
    return old_count;
}

uint64_t xive2_presenter_nvp_backlog_op(XivePresenter *xptr,
                                        uint8_t blk, uint32_t idx,
                                        uint16_t offset)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xptr);
    uint8_t priority = GETFIELD(NVx_BACKLOG_PRIO, offset);
    uint8_t op = GETFIELD(NVx_BACKLOG_OP, offset);
    Xive2Nvp nvp;
    uint8_t ipb, old_ipb, rc;

    if (xive2_router_get_nvp(xrtr, blk, idx, &nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No NVP %x/%x\n", blk, idx);
        return -1;
    }
    if (!xive2_nvp_is_valid(&nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Invalid NVP %x/%x\n", blk, idx);
        return -1;
    }

    old_ipb = xive_get_field32(NVP2_W2_IPB, nvp.w2);
    ipb = old_ipb;
    /*
     * op:
     * 0b00 => set priority bit
     * 0b01 => reset priority bit
     * 0b1- => read
     */
    if (op == 0b00 || op == 0b01) {
        if (op == 0b00) {
            ipb |= xive_priority_to_ipb(priority);
        } else {
            ipb &= ~xive_priority_to_ipb(priority);
        }
        nvp.w2 = xive_set_field32(NVP2_W2_IPB, nvp.w2, ipb);
        xive2_router_write_nvp(xrtr, blk, idx, &nvp, 2);
    }
    rc = !!(old_ipb & xive_priority_to_ipb(priority));
    trace_xive_nvp_backlog_op(blk, idx, op, priority, rc);
    return rc;
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

#define XIVE2_QSIZE_CHUNK_CL    128
#define XIVE2_QSIZE_CHUNK_4k   4096
/* Calculate max number of queue entries for an END */
static uint32_t xive2_end_get_qentries(Xive2End *end)
{
    uint32_t w3 = end->w3;
    uint32_t qsize = xive_get_field32(END2_W3_QSIZE, w3);
    if (xive_get_field32(END2_W3_CL, w3)) {
        g_assert(qsize <= 4);
        return (XIVE2_QSIZE_CHUNK_CL << qsize) / sizeof(uint32_t);
    } else {
        g_assert(qsize <= 12);
        return (XIVE2_QSIZE_CHUNK_4k << qsize) / sizeof(uint32_t);
    }
}

void xive2_end_queue_pic_print_info(Xive2End *end, uint32_t width, GString *buf)
{
    uint64_t qaddr_base = xive2_end_qaddr(end);
    uint32_t qindex = xive_get_field32(END2_W1_PAGE_OFF, end->w1);
    uint32_t qentries = xive2_end_get_qentries(end);
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
    uint32_t qentries = xive2_end_get_qentries(end);

    uint32_t nvx_blk = xive_get_field32(END2_W6_VP_BLOCK, end->w6);
    uint32_t nvx_idx = xive_get_field32(END2_W6_VP_OFFSET, end->w6);
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
                           priority, nvx_blk, nvx_idx);

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
    uint32_t qindex = xive_get_field32(END2_W1_PAGE_OFF, end->w1);
    uint32_t qgen = xive_get_field32(END2_W1_GENERATION, end->w1);

    uint64_t qaddr = qaddr_base + (qindex << 2);
    uint32_t qdata = cpu_to_be32((qgen << 31) | (data & 0x7fffffff));
    uint32_t qentries = xive2_end_get_qentries(end);

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

        /* Set gen flipped to 1, it gets reset on a cache watch operation */
        end->w1 = xive_set_field32(END2_W1_GEN_FLIPPED, end->w1, 1);
    }
    end->w1 = xive_set_field32(END2_W1_PAGE_OFF, end->w1, qindex);
}

static void xive2_pgofnext(uint8_t *nvgc_blk, uint32_t *nvgc_idx,
                           uint8_t next_level)
{
    uint32_t mask, next_idx;
    uint8_t next_blk;

    /*
     * Adjust the block and index of a VP for the next group/crowd
     * size (PGofFirst/PGofNext field in the NVP and NVGC structures).
     *
     * The 6-bit group level is split into a 2-bit crowd and 4-bit
     * group levels. Encoding is similar. However, we don't support
     * crowd size of 8. So a crowd level of 0b11 is bumped to a crowd
     * size of 16.
     */
    next_blk = NVx_CROWD_LVL(next_level);
    if (next_blk == 3) {
        next_blk = 4;
    }
    mask = (1 << next_blk) - 1;
    *nvgc_blk &= ~mask;
    *nvgc_blk |= mask >> 1;

    next_idx = NVx_GROUP_LVL(next_level);
    mask = (1 << next_idx) - 1;
    *nvgc_idx &= ~mask;
    *nvgc_idx |= mask >> 1;
}

/*
 * Scan the group chain and return the highest priority and group
 * level of pending group interrupts.
 */
static uint8_t xive2_presenter_backlog_scan(XivePresenter *xptr,
                                            uint8_t nvx_blk, uint32_t nvx_idx,
                                            uint8_t first_group,
                                            uint8_t *out_level)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xptr);
    uint32_t nvgc_idx;
    uint32_t current_level, count;
    uint8_t nvgc_blk, prio;
    Xive2Nvgc nvgc;

    for (prio = 0; prio <= XIVE_PRIORITY_MAX; prio++) {
        current_level = first_group & 0x3F;
        nvgc_blk = nvx_blk;
        nvgc_idx = nvx_idx;

        while (current_level) {
            xive2_pgofnext(&nvgc_blk, &nvgc_idx, current_level);

            if (xive2_router_get_nvgc(xrtr, NVx_CROWD_LVL(current_level),
                                      nvgc_blk, nvgc_idx, &nvgc)) {
                qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No NVGC %x/%x\n",
                              nvgc_blk, nvgc_idx);
                return 0xFF;
            }
            if (!xive2_nvgc_is_valid(&nvgc)) {
                qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Invalid NVGC %x/%x\n",
                              nvgc_blk, nvgc_idx);
                return 0xFF;
            }

            count = xive2_nvgc_get_backlog(&nvgc, prio);
            if (count) {
                *out_level = current_level;
                return prio;
            }
            current_level = xive_get_field32(NVGC2_W0_PGONEXT, nvgc.w0) & 0x3F;
        }
    }
    return 0xFF;
}

static void xive2_presenter_backlog_decr(XivePresenter *xptr,
                                         uint8_t nvx_blk, uint32_t nvx_idx,
                                         uint8_t group_prio,
                                         uint8_t group_level)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xptr);
    uint32_t nvgc_idx, count;
    uint8_t nvgc_blk;
    Xive2Nvgc nvgc;

    nvgc_blk = nvx_blk;
    nvgc_idx = nvx_idx;
    xive2_pgofnext(&nvgc_blk, &nvgc_idx, group_level);

    if (xive2_router_get_nvgc(xrtr, NVx_CROWD_LVL(group_level),
                              nvgc_blk, nvgc_idx, &nvgc)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No NVGC %x/%x\n",
                      nvgc_blk, nvgc_idx);
        return;
    }
    if (!xive2_nvgc_is_valid(&nvgc)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Invalid NVGC %x/%x\n",
                      nvgc_blk, nvgc_idx);
        return;
    }
    count = xive2_nvgc_get_backlog(&nvgc, group_prio);
    if (!count) {
        return;
    }
    xive2_nvgc_set_backlog(&nvgc, group_prio, count - 1);
    xive2_router_write_nvgc(xrtr, NVx_CROWD_LVL(group_level),
                            nvgc_blk, nvgc_idx, &nvgc);
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
                                uint8_t ring,
                                uint8_t nvp_blk, uint32_t nvp_idx)
{
    CPUPPCState *env = &POWERPC_CPU(tctx->cs)->env;
    uint32_t pir = env->spr_cb[SPR_PIR].default_value;
    Xive2Nvp nvp;
    uint8_t *sig_regs = xive_tctx_signal_regs(tctx, ring);
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

    if ((nvp.w0 & NVP2_W0_P) || ring != TM_QW2_HV_POOL) {
        /*
         * Non-pool contexts always save CPPR (ignore p bit). XXX: Clarify
         * whether that is the correct behaviour.
         */
        nvp.w2 = xive_set_field32(NVP2_W2_CPPR, nvp.w2, sig_regs[TM_CPPR]);
    }
    if (nvp.w0 & NVP2_W0_L) {
        /*
         * Typically not used. If LSMFB is restored with 0, it will
         * force a backlog rescan
         */
        nvp.w2 = xive_set_field32(NVP2_W2_LSMFB, nvp.w2, regs[TM_LSMFB]);
    }
    if (nvp.w0 & NVP2_W0_G) {
        nvp.w2 = xive_set_field32(NVP2_W2_LGS, nvp.w2, regs[TM_LGS]);
    }
    if (nvp.w0 & NVP2_W0_T) {
        nvp.w2 = xive_set_field32(NVP2_W2_T, nvp.w2, regs[TM_T]);
    }
    xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, &nvp, 2);

    nvp.w1 = xive_set_field32(NVP2_W1_CO, nvp.w1, 0);
    /* NVP2_W1_CO_THRID_VALID only set once */
    nvp.w1 = xive_set_field32(NVP2_W1_CO_THRID, nvp.w1, 0xFFFF);
    xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, &nvp, 1);
}

/* POOL cam is the same as OS cam encoding */
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

static void xive2_redistribute(Xive2Router *xrtr, XiveTCTX *tctx, uint8_t ring)
{
    uint8_t *sig_regs = xive_tctx_signal_regs(tctx, ring);
    uint8_t nsr = sig_regs[TM_NSR];
    uint8_t pipr = sig_regs[TM_PIPR];
    uint8_t crowd = NVx_CROWD_LVL(nsr);
    uint8_t group = NVx_GROUP_LVL(nsr);
    uint8_t nvgc_blk, end_blk, nvp_blk;
    uint32_t nvgc_idx, end_idx, nvp_idx;
    Xive2Nvgc nvgc;
    uint8_t prio_limit;
    uint32_t cfg;

    /* redistribution is only for group/crowd interrupts */
    if (!xive_nsr_indicates_group_exception(ring, nsr)) {
        return;
    }

    /* Don't check return code since ring is expected to be invalidated */
    xive2_tctx_get_nvp_indexes(tctx, ring, &nvp_blk, &nvp_idx);

    trace_xive_redistribute(tctx->cs->cpu_index, ring, nvp_blk, nvp_idx);

    trace_xive_redistribute(tctx->cs->cpu_index, ring, nvp_blk, nvp_idx);
    /* convert crowd/group to blk/idx */
    nvgc_idx = xive2_nvgc_get_idx(nvp_idx, group);
    nvgc_blk = xive2_nvgc_get_blk(nvp_blk, crowd);

    /* Use blk/idx to retrieve the NVGC */
    if (xive2_router_get_nvgc(xrtr, crowd, nvgc_blk, nvgc_idx, &nvgc)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: no %s %x/%x\n",
                      crowd ? "NVC" : "NVG", nvgc_blk, nvgc_idx);
        return;
    }

    /* retrieve the END blk/idx from the NVGC */
    end_blk = xive_get_field32(NVGC2_W1_END_BLK, nvgc.w1);
    end_idx = xive_get_field32(NVGC2_W1_END_IDX, nvgc.w1);

    /* determine number of priorities being used */
    cfg = xive2_router_get_config(xrtr);
    if (cfg & XIVE2_EN_VP_GRP_PRIORITY) {
        prio_limit = 1 << GETFIELD(NVGC2_W1_PSIZE, nvgc.w1);
    } else {
        prio_limit = 1 << GETFIELD(XIVE2_VP_INT_PRIO, cfg);
    }

    /* add priority offset to end index */
    end_idx += pipr % prio_limit;

    /* trigger the group END */
    xive2_router_end_notify(xrtr, end_blk, end_idx, 0, true);

    /* clear interrupt indication for the context */
    sig_regs[TM_NSR] = 0;
    sig_regs[TM_PIPR] = sig_regs[TM_CPPR];
    xive_tctx_reset_signal(tctx, ring);
}

static void xive2_tctx_process_pending(XiveTCTX *tctx, uint8_t sig_ring);

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
    uint8_t nsr;

    xive2_cam_decode(cam, &nvp_blk, &nvp_idx, &valid, &do_save);

    if (xive2_tctx_get_nvp_indexes(tctx, ring, &nvp_blk, &nvp_idx)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: pulling invalid NVP %x/%x !?\n",
                      nvp_blk, nvp_idx);
    }

    /* Invalidate CAM line of requested ring and all lower rings */
    for (cur_ring = TM_QW0_USER; cur_ring <= ring;
         cur_ring += XIVE_TM_RING_SIZE) {
        uint32_t ringw2 = xive_tctx_word2(&tctx->regs[cur_ring]);
        uint32_t ringw2_new = xive_set_field32(TM2_QW1W2_VO, ringw2, 0);
        bool is_valid = !!(xive_get_field32(TM2_QW1W2_VO, ringw2));
        uint8_t *sig_regs;

        memcpy(&tctx->regs[cur_ring + TM_WORD2], &ringw2_new, 4);

        /* Skip the rest for USER or invalid contexts */
        if ((cur_ring == TM_QW0_USER) || !is_valid) {
            continue;
        }

        /* Active group/crowd interrupts need to be redistributed */
        sig_regs = xive_tctx_signal_regs(tctx, ring);
        nsr = sig_regs[TM_NSR];
        if (xive_nsr_indicates_group_exception(cur_ring, nsr)) {
            /* Ensure ring matches NSR (for HV NSR POOL vs PHYS rings) */
            if (cur_ring == xive_nsr_exception_ring(cur_ring, nsr)) {
                xive2_redistribute(xrtr, tctx, cur_ring);
            }
        }

        /*
         * Lower external interrupt line of requested ring and below except for
         * USER, which doesn't exist.
         */
        if (xive_nsr_indicates_exception(cur_ring, nsr)) {
            if (cur_ring == xive_nsr_exception_ring(cur_ring, nsr)) {
                xive_tctx_reset_signal(tctx, cur_ring);
            }
        }
    }

    if (ring == TM_QW2_HV_POOL) {
        /* Re-check phys for interrupts if pool was disabled */
        nsr = tctx->regs[TM_QW3_HV_PHYS + TM_NSR];
        if (xive_nsr_indicates_exception(TM_QW3_HV_PHYS, nsr)) {
            /* Ring must be PHYS because POOL would have been redistributed */
            g_assert(xive_nsr_exception_ring(TM_QW3_HV_PHYS, nsr) ==
                                                           TM_QW3_HV_PHYS);
        } else {
            xive2_tctx_process_pending(tctx, TM_QW3_HV_PHYS);
        }
    }

    if (xive2_router_get_config(xrtr) & XIVE2_VP_SAVE_RESTORE && do_save) {
        xive2_tctx_save_ctx(xrtr, tctx, ring, nvp_blk, nvp_idx);
    }

    return target_ringw2;
}

uint64_t xive2_tm_pull_os_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                              hwaddr offset, unsigned size)
{
    return xive2_tm_pull_ctx(xptr, tctx, offset, size, TM_QW1_OS);
}

uint64_t xive2_tm_pull_pool_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                                hwaddr offset, unsigned size)
{
    return xive2_tm_pull_ctx(xptr, tctx, offset, size, TM_QW2_HV_POOL);
}

uint64_t xive2_tm_pull_phys_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                                hwaddr offset, unsigned size)
{
    return xive2_tm_pull_ctx(xptr, tctx, offset, size, TM_QW3_HV_PHYS);
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

static uint8_t xive2_tctx_restore_ctx(Xive2Router *xrtr, XiveTCTX *tctx,
                                      uint8_t ring,
                                      uint8_t nvp_blk, uint32_t nvp_idx,
                                      Xive2Nvp *nvp)
{
    CPUPPCState *env = &POWERPC_CPU(tctx->cs)->env;
    uint32_t pir = env->spr_cb[SPR_PIR].default_value;
    uint8_t *sig_regs = xive_tctx_signal_regs(tctx, ring);
    uint8_t *regs = &tctx->regs[ring];
    uint8_t cppr;

    if (!xive2_nvp_is_hw(nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: NVP %x/%x is not HW owned\n",
                      nvp_blk, nvp_idx);
        return 0;
    }

    cppr = xive_get_field32(NVP2_W2_CPPR, nvp->w2);
    nvp->w2 = xive_set_field32(NVP2_W2_CPPR, nvp->w2, 0);
    xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, nvp, 2);

    sig_regs[TM_CPPR] = cppr;
    regs[TM_LSMFB] = xive_get_field32(NVP2_W2_LSMFB, nvp->w2);
    regs[TM_LGS] = xive_get_field32(NVP2_W2_LGS, nvp->w2);
    regs[TM_T] = xive_get_field32(NVP2_W2_T, nvp->w2);

    nvp->w1 = xive_set_field32(NVP2_W1_CO, nvp->w1, 1);
    nvp->w1 = xive_set_field32(NVP2_W1_CO_THRID_VALID, nvp->w1, 1);
    nvp->w1 = xive_set_field32(NVP2_W1_CO_THRID, nvp->w1, pir);

    /*
     * Checkout privilege: 0:OS, 1:Pool, 2:Hard
     *
     * TODO: we don't support hard push/pull
     */
    switch (ring) {
    case TM_QW1_OS:
        nvp->w1 = xive_set_field32(NVP2_W1_CO_PRIV, nvp->w1, 0);
        break;
    case TM_QW2_HV_POOL:
        nvp->w1 = xive_set_field32(NVP2_W1_CO_PRIV, nvp->w1, 1);
        break;
    default:
        g_assert_not_reached();
    }

    xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, nvp, 1);

    /* return restored CPPR to generate a CPU exception if needed */
    return cppr;
}

/* Restore TIMA VP context from NVP backlog */
static void xive2_tctx_restore_nvp(Xive2Router *xrtr, XiveTCTX *tctx,
                                   uint8_t ring,
                                   uint8_t nvp_blk, uint32_t nvp_idx,
                                   bool do_restore)
{
    uint8_t *regs = &tctx->regs[ring];
    uint8_t ipb;
    Xive2Nvp nvp;

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
    if (xive2_router_get_config(xrtr) & XIVE2_VP_SAVE_RESTORE && do_restore) {
        xive2_tctx_restore_ctx(xrtr, tctx, ring, nvp_blk, nvp_idx, &nvp);
    }

    ipb = xive_get_field32(NVP2_W2_IPB, nvp.w2);
    if (ipb) {
        nvp.w2 = xive_set_field32(NVP2_W2_IPB, nvp.w2, 0);
        xive2_router_write_nvp(xrtr, nvp_blk, nvp_idx, &nvp, 2);
    }
    /* IPB bits in the backlog are merged with the TIMA IPB bits */
    regs[TM_IPB] |= ipb;
}

/*
 * Updating the ring CAM line can trigger a resend of interrupt
 */
static void xive2_tm_push_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                              hwaddr offset, uint64_t value, unsigned size,
                              uint8_t ring)
{
    uint32_t cam;
    uint32_t w2;
    uint64_t dw1;
    uint8_t nvp_blk;
    uint32_t nvp_idx;
    bool v;
    bool do_restore;

    if (xive_ring_valid(tctx, ring)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Attempt to push VP to enabled"
                                       " ring 0x%02x\n", ring);
        return;
    }

    /* First update the thead context */
    switch (size) {
    case 1:
        tctx->regs[ring + TM_WORD2] = value & 0xff;
        cam = xive2_tctx_hw_cam_line(xptr, tctx);
        cam |= ((value & 0xc0) << 24); /* V and H bits */
        break;
    case 4:
        cam = value;
        w2 = cpu_to_be32(cam);
        memcpy(&tctx->regs[ring + TM_WORD2], &w2, 4);
        break;
    case 8:
        cam = value >> 32;
        dw1 = cpu_to_be64(value);
        memcpy(&tctx->regs[ring + TM_WORD2], &dw1, 8);
        break;
    default:
        g_assert_not_reached();
    }

    xive2_cam_decode(cam, &nvp_blk, &nvp_idx, &v, &do_restore);

    /* Check the interrupt pending bits */
    if (v) {
        Xive2Router *xrtr = XIVE2_ROUTER(xptr);
        uint8_t cur_ring;

        xive2_tctx_restore_nvp(xrtr, tctx, ring,
                               nvp_blk, nvp_idx, do_restore);

        for (cur_ring = TM_QW1_OS; cur_ring <= ring;
             cur_ring += XIVE_TM_RING_SIZE) {
            uint8_t *sig_regs = xive_tctx_signal_regs(tctx, cur_ring);
            uint8_t nsr = sig_regs[TM_NSR];

            if (!xive_ring_valid(tctx, cur_ring)) {
                continue;
            }

            if (cur_ring == TM_QW2_HV_POOL) {
                if (xive_nsr_indicates_exception(cur_ring, nsr)) {
                    g_assert(xive_nsr_exception_ring(cur_ring, nsr) ==
                                                               TM_QW3_HV_PHYS);
                    xive2_redistribute(xrtr, tctx,
                                       xive_nsr_exception_ring(ring, nsr));
                }
                xive2_tctx_process_pending(tctx, TM_QW3_HV_PHYS);
                break;
            }
            xive2_tctx_process_pending(tctx, cur_ring);
        }
    }
}

void xive2_tm_push_os_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                          hwaddr offset, uint64_t value, unsigned size)
{
    xive2_tm_push_ctx(xptr, tctx, offset, value, size, TM_QW1_OS);
}

void xive2_tm_push_pool_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                            hwaddr offset, uint64_t value, unsigned size)
{
    xive2_tm_push_ctx(xptr, tctx, offset, value, size, TM_QW2_HV_POOL);
}

void xive2_tm_push_phys_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                            hwaddr offset, uint64_t value, unsigned size)
{
    xive2_tm_push_ctx(xptr, tctx, offset, value, size, TM_QW3_HV_PHYS);
}

/* returns -1 if ring is invalid, but still populates block and index */
static int xive2_tctx_get_nvp_indexes(XiveTCTX *tctx, uint8_t ring,
                                      uint8_t *nvp_blk, uint32_t *nvp_idx)
{
    uint32_t w2;
    uint32_t cam = 0;
    int rc = 0;

    w2 = xive_tctx_word2(&tctx->regs[ring]);
    switch (ring) {
    case TM_QW1_OS:
        if (!(be32_to_cpu(w2) & TM2_QW1W2_VO)) {
            rc = -1;
        }
        cam = xive_get_field32(TM2_QW1W2_OS_CAM, w2);
        break;
    case TM_QW2_HV_POOL:
        if (!(be32_to_cpu(w2) & TM2_QW2W2_VP)) {
            rc = -1;
        }
        cam = xive_get_field32(TM2_QW2W2_POOL_CAM, w2);
        break;
    case TM_QW3_HV_PHYS:
        if (!(be32_to_cpu(w2) & TM2_QW3W2_VT)) {
            rc = -1;
        }
        cam = xive2_tctx_hw_cam_line(tctx->xptr, tctx);
        break;
    default:
        rc = -1;
    }
    *nvp_blk = xive2_nvp_blk(cam);
    *nvp_idx = xive2_nvp_idx(cam);
    return rc;
}

static void xive2_tctx_accept_el(XivePresenter *xptr, XiveTCTX *tctx,
                                 uint8_t ring, uint8_t cl_ring)
{
    uint64_t rd;
    Xive2Router *xrtr = XIVE2_ROUTER(xptr);
    uint32_t nvp_idx, xive2_cfg;
    uint8_t nvp_blk;
    Xive2Nvp nvp;
    uint64_t phys_addr;
    uint8_t OGen = 0;

    xive2_tctx_get_nvp_indexes(tctx, cl_ring, &nvp_blk, &nvp_idx);

    if (xive2_router_get_nvp(xrtr, (uint8_t)nvp_blk, nvp_idx, &nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No NVP %x/%x\n",
                      nvp_blk, nvp_idx);
        return;
    }

    if (!xive2_nvp_is_valid(&nvp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid NVP %x/%x\n",
                      nvp_blk, nvp_idx);
        return;
    }


    rd = xive_tctx_accept(tctx, ring);

    if (ring == TM_QW1_OS) {
        OGen = tctx->regs[ring + TM_OGEN];
    }
    xive2_cfg = xive2_router_get_config(xrtr);
    phys_addr = xive2_nvp_reporting_addr(&nvp);
    uint8_t report_data[REPORT_LINE_GEN1_SIZE];
    memset(report_data, 0xff, sizeof(report_data));
    if ((OGen == 1) || (xive2_cfg & XIVE2_GEN1_TIMA_OS)) {
        report_data[8] = (rd >> 8) & 0xff;
        report_data[9] = rd & 0xff;
    } else {
        report_data[0] = (rd >> 8) & 0xff;
        report_data[1] = rd & 0xff;
    }
    cpu_physical_memory_write(phys_addr, report_data, REPORT_LINE_GEN1_SIZE);
}

void xive2_tm_ack_os_el(XivePresenter *xptr, XiveTCTX *tctx,
                        hwaddr offset, uint64_t value, unsigned size)
{
    xive2_tctx_accept_el(xptr, tctx, TM_QW1_OS, TM_QW1_OS);
}

/* Re-calculate and present pending interrupts */
static void xive2_tctx_process_pending(XiveTCTX *tctx, uint8_t sig_ring)
{
    uint8_t *sig_regs = &tctx->regs[sig_ring];
    Xive2Router *xrtr = XIVE2_ROUTER(tctx->xptr);
    uint8_t backlog_prio;
    uint8_t first_group;
    uint8_t group_level;
    uint8_t pipr_min;
    uint8_t lsmfb_min;
    uint8_t ring_min;
    uint8_t cppr = sig_regs[TM_CPPR];
    bool group_enabled;
    Xive2Nvp nvp;
    int rc;

    g_assert(sig_ring == TM_QW3_HV_PHYS || sig_ring == TM_QW1_OS);
    g_assert(sig_regs[TM_WORD2] & 0x80);
    g_assert(!xive_nsr_indicates_group_exception(sig_ring, sig_regs[TM_NSR]));

    /*
     * Recompute the PIPR based on local pending interrupts. It will
     * be adjusted below if needed in case of pending group interrupts.
     */
again:
    pipr_min = xive_ipb_to_pipr(sig_regs[TM_IPB]);
    group_enabled = !!sig_regs[TM_LGS];
    lsmfb_min = group_enabled ? sig_regs[TM_LSMFB] : 0xff;
    ring_min = sig_ring;
    group_level = 0;

    /* PHYS updates also depend on POOL values */
    if (sig_ring == TM_QW3_HV_PHYS) {
        uint8_t *pool_regs = &tctx->regs[TM_QW2_HV_POOL];

        /* POOL values only matter if POOL ctx is valid */
        if (pool_regs[TM_WORD2] & 0x80) {
            uint8_t pool_pipr = xive_ipb_to_pipr(pool_regs[TM_IPB]);
            uint8_t pool_lsmfb = pool_regs[TM_LSMFB];

            /*
             * Determine highest priority interrupt and
             * remember which ring has it.
             */
            if (pool_pipr < pipr_min) {
                pipr_min = pool_pipr;
                if (pool_pipr < lsmfb_min) {
                    ring_min = TM_QW2_HV_POOL;
                }
            }

            /* Values needed for group priority calculation */
            if (pool_regs[TM_LGS] && (pool_lsmfb < lsmfb_min)) {
                group_enabled = true;
                lsmfb_min = pool_lsmfb;
                if (lsmfb_min < pipr_min) {
                    ring_min = TM_QW2_HV_POOL;
                }
            }
        }
    }

    if (group_enabled &&
        lsmfb_min < cppr &&
        lsmfb_min < pipr_min) {

        uint8_t nvp_blk;
        uint32_t nvp_idx;

        /*
         * Thread has seen a group interrupt with a higher priority
         * than the new cppr or pending local interrupt. Check the
         * backlog
         */
        rc = xive2_tctx_get_nvp_indexes(tctx, ring_min, &nvp_blk, &nvp_idx);
        if (rc) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: set CPPR on invalid "
                                           "context\n");
            return;
        }

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

        first_group = xive_get_field32(NVP2_W0_PGOFIRST, nvp.w0);
        if (!first_group) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid NVP %x/%x\n",
                          nvp_blk, nvp_idx);
            return;
        }

        backlog_prio = xive2_presenter_backlog_scan(tctx->xptr,
                                                    nvp_blk, nvp_idx,
                                                    first_group, &group_level);
        tctx->regs[ring_min + TM_LSMFB] = backlog_prio;
        if (backlog_prio != lsmfb_min) {
            /*
             * If the group backlog scan finds a less favored or no interrupt,
             * then re-do the processing which may turn up a more favored
             * interrupt from IPB or the other pool. Backlog should not
             * find a priority < LSMFB.
             */
            g_assert(backlog_prio >= lsmfb_min);
            goto again;
        }

        xive2_presenter_backlog_decr(tctx->xptr, nvp_blk, nvp_idx,
                                     backlog_prio, group_level);
        pipr_min = backlog_prio;
    }

    if (pipr_min > cppr) {
        pipr_min = cppr;
    }
    xive_tctx_pipr_set(tctx, ring_min, pipr_min, group_level);
}

/* NOTE: CPPR only exists for TM_QW1_OS and TM_QW3_HV_PHYS */
static void xive2_tctx_set_cppr(XiveTCTX *tctx, uint8_t sig_ring, uint8_t cppr)
{
    uint8_t *sig_regs = &tctx->regs[sig_ring];
    Xive2Router *xrtr = XIVE2_ROUTER(tctx->xptr);
    uint8_t old_cppr;
    uint8_t nsr = sig_regs[TM_NSR];

    g_assert(sig_ring == TM_QW1_OS || sig_ring == TM_QW3_HV_PHYS);

    g_assert(tctx->regs[TM_QW2_HV_POOL + TM_NSR] == 0);
    g_assert(tctx->regs[TM_QW2_HV_POOL + TM_PIPR] == 0);
    g_assert(tctx->regs[TM_QW2_HV_POOL + TM_CPPR] == 0);

    /* XXX: should show pool IPB for PHYS ring */
    trace_xive_tctx_set_cppr(tctx->cs->cpu_index, sig_ring,
                             sig_regs[TM_IPB], sig_regs[TM_PIPR],
                             cppr, nsr);

    if (cppr > XIVE_PRIORITY_MAX) {
        cppr = 0xff;
    }

    old_cppr = sig_regs[TM_CPPR];
    sig_regs[TM_CPPR] = cppr;

    /* Handle increased CPPR priority (lower value) */
    if (cppr < old_cppr) {
        if (cppr <= sig_regs[TM_PIPR]) {
            /* CPPR lowered below PIPR, must un-present interrupt */
            if (xive_nsr_indicates_exception(sig_ring, nsr)) {
                if (xive_nsr_indicates_group_exception(sig_ring, nsr)) {
                    /* redistribute precluded active grp interrupt */
                    xive2_redistribute(xrtr, tctx,
                                       xive_nsr_exception_ring(sig_ring, nsr));
                    return;
                }
            }

            /* interrupt is VP directed, pending in IPB */
            xive_tctx_pipr_set(tctx, sig_ring, cppr, 0);
            return;
        } else {
            /* CPPR was lowered, but still above PIPR. No action needed. */
            return;
        }
    }

    /* CPPR didn't change, nothing needs to be done */
    if (cppr == old_cppr) {
        return;
    }

    /* CPPR priority decreased (higher value) */
    if (!xive_nsr_indicates_exception(sig_ring, nsr)) {
        xive2_tctx_process_pending(tctx, sig_ring);
    }
}

void xive2_tm_set_hv_cppr(XivePresenter *xptr, XiveTCTX *tctx,
                          hwaddr offset, uint64_t value, unsigned size)
{
    xive2_tctx_set_cppr(tctx, TM_QW3_HV_PHYS, value & 0xff);
}

void xive2_tm_set_os_cppr(XivePresenter *xptr, XiveTCTX *tctx,
                          hwaddr offset, uint64_t value, unsigned size)
{
    xive2_tctx_set_cppr(tctx, TM_QW1_OS, value & 0xff);
}

/*
 * Adjust the IPB to allow a CPU to process event queues of other
 * priorities during one physical interrupt cycle.
 */
void xive2_tm_set_os_pending(XivePresenter *xptr, XiveTCTX *tctx,
                             hwaddr offset, uint64_t value, unsigned size)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xptr);
    uint8_t ring = TM_QW1_OS;
    uint8_t *regs = &tctx->regs[ring];
    uint8_t priority = value & 0xff;

    /*
     * XXX: should this simply set a bit in IPB and wait for it to be picked
     * up next cycle, or is it supposed to present it now? We implement the
     * latter here.
     */
    regs[TM_IPB] |= xive_priority_to_ipb(priority);
    if (xive_ipb_to_pipr(regs[TM_IPB]) >= regs[TM_PIPR]) {
        return;
    }
    if (xive_nsr_indicates_group_exception(ring, regs[TM_NSR])) {
        xive2_redistribute(xrtr, tctx, ring);
    }

    xive_tctx_pipr_present(tctx, ring, priority, 0);
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

static bool xive2_vp_match_mask(uint32_t cam1, uint32_t cam2,
                                uint32_t vp_mask)
{
    return (cam1 & vp_mask) == (cam2 & vp_mask);
}

static uint8_t xive2_get_vp_block_mask(uint32_t nvt_blk, bool crowd)
{
    uint8_t block_mask = 0b1111;

    /* 3 supported crowd sizes: 2, 4, 16 */
    if (crowd) {
        uint32_t size = xive_get_vpgroup_size(nvt_blk);

        if (size != 2 && size != 4 && size != 16) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Invalid crowd size of %d",
                                           size);
            return block_mask;
        }
        block_mask &= ~(size - 1);
    }
    return block_mask;
}

static uint32_t xive2_get_vp_index_mask(uint32_t nvt_index, bool cam_ignore)
{
    uint32_t index_mask = 0xFFFFFF; /* 24 bits */

    if (cam_ignore) {
        uint32_t size = xive_get_vpgroup_size(nvt_index);

        if (size < 2) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Invalid group size of %d",
                                           size);
            return index_mask;
        }
        index_mask &= ~(size - 1);
    }
    return index_mask;
}

/*
 * The thread context register words are in big-endian format.
 */
int xive2_presenter_tctx_match(XivePresenter *xptr, XiveTCTX *tctx,
                               uint8_t format,
                               uint8_t nvt_blk, uint32_t nvt_idx,
                               bool crowd, bool cam_ignore,
                               uint32_t logic_serv)
{
    uint32_t cam =   xive2_nvp_cam_line(nvt_blk, nvt_idx);
    uint32_t qw3w2 = xive_tctx_word2(&tctx->regs[TM_QW3_HV_PHYS]);
    uint32_t qw2w2 = xive_tctx_word2(&tctx->regs[TM_QW2_HV_POOL]);
    uint32_t qw1w2 = xive_tctx_word2(&tctx->regs[TM_QW1_OS]);
    uint32_t qw0w2 = xive_tctx_word2(&tctx->regs[TM_QW0_USER]);

    uint32_t index_mask, vp_mask;
    uint8_t block_mask;

    if (format == 0) {
        /*
         * i=0: Specific NVT notification
         * i=1: VP-group notification (bits ignored at the end of the
         *      NVT identifier)
         */
        block_mask = xive2_get_vp_block_mask(nvt_blk, crowd);
        index_mask = xive2_get_vp_index_mask(nvt_idx, cam_ignore);
        vp_mask = xive2_nvp_cam_line(block_mask, index_mask);

        /* For VP-group notifications, threads with LGS=0 are excluded */

        /* PHYS ring */
        if ((be32_to_cpu(qw3w2) & TM2_QW3W2_VT) &&
            !(cam_ignore && tctx->regs[TM_QW3_HV_PHYS + TM_LGS] == 0) &&
            xive2_vp_match_mask(cam,
                                xive2_tctx_hw_cam_line(xptr, tctx),
                                vp_mask)) {
            return TM_QW3_HV_PHYS;
        }

        /* HV POOL ring */
        if ((be32_to_cpu(qw2w2) & TM2_QW2W2_VP) &&
            !(cam_ignore && tctx->regs[TM_QW2_HV_POOL + TM_LGS] == 0) &&
            xive2_vp_match_mask(cam,
                                xive_get_field32(TM2_QW2W2_POOL_CAM, qw2w2),
                                vp_mask)) {
            return TM_QW2_HV_POOL;
        }

        /* OS ring */
        if ((be32_to_cpu(qw1w2) & TM2_QW1W2_VO) &&
            !(cam_ignore && tctx->regs[TM_QW1_OS + TM_LGS] == 0) &&
            xive2_vp_match_mask(cam,
                                xive_get_field32(TM2_QW1W2_OS_CAM, qw1w2),
                                vp_mask)) {
            return TM_QW1_OS;
        }
    } else {
        /* F=1 : User level Event-Based Branch (EBB) notification */

        /* FIXME: what if cam_ignore and LGS = 0 ? */
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

bool xive2_tm_irq_precluded(XiveTCTX *tctx, int ring, uint8_t priority)
{
    uint8_t *sig_regs = xive_tctx_signal_regs(tctx, ring);

    /*
     * The xive2_presenter_tctx_match() above tells if there's a match
     * but for VP-group notification, we still need to look at the
     * priority to know if the thread can take the interrupt now or if
     * it is precluded.
     */
    if (priority < sig_regs[TM_PIPR]) {
        return false;
    }
    return true;
}

void xive2_tm_set_lsmfb(XiveTCTX *tctx, int ring, uint8_t priority)
{
    uint8_t *regs = &tctx->regs[ring];

    /*
     * Called by the router during a VP-group notification when the
     * thread matches but can't take the interrupt because it's
     * already running at a more favored priority. It then stores the
     * new interrupt priority in the LSMFB field.
     */
    regs[TM_LSMFB] = priority;
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
                                    uint32_t end_idx, uint32_t end_data,
                                    bool redistribute)
{
    Xive2End end;
    uint8_t priority;
    uint8_t format;
    XiveTCTXMatch match;
    bool crowd, cam_ignore;
    uint8_t nvx_blk;
    uint32_t nvx_idx;

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

    if (xive2_end_is_crowd(&end) && !xive2_end_is_ignore(&end)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "XIVE: invalid END, 'crowd' bit requires 'ignore' bit\n");
        return;
    }

    if (!redistribute && xive2_end_is_enqueue(&end)) {
        trace_xive_end_enqueue(end_blk, end_idx, end_data);
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
    nvx_blk = xive_get_field32(END2_W6_VP_BLOCK, end.w6);
    nvx_idx = xive_get_field32(END2_W6_VP_OFFSET, end.w6);
    crowd = xive2_end_is_crowd(&end);
    cam_ignore = xive2_end_is_ignore(&end);

    /* TODO: Auto EOI. */
    if (xive_presenter_match(xrtr->xfb, format, nvx_blk, nvx_idx,
                             crowd, cam_ignore, priority,
                             xive_get_field32(END2_W7_F1_LOG_SERVER_ID, end.w7),
                             &match)) {
        XiveTCTX *tctx = match.tctx;
        uint8_t ring = match.ring;
        uint8_t *sig_regs = xive_tctx_signal_regs(tctx, ring);
        uint8_t nsr = sig_regs[TM_NSR];
        uint8_t group_level;

        if (priority < sig_regs[TM_PIPR] &&
            xive_nsr_indicates_group_exception(ring, nsr)) {
            xive2_redistribute(xrtr, tctx, xive_nsr_exception_ring(ring, nsr));
        }

        group_level = xive_get_group_level(crowd, cam_ignore, nvx_blk, nvx_idx);
        trace_xive_presenter_notify(nvx_blk, nvx_idx, ring, group_level);
        xive_tctx_pipr_present(tctx, ring, priority, group_level);
        return;
    }

    /*
     * If no matching NVP is dispatched on a HW thread :
     * - specific VP: update the NVP structure if backlog is activated
     * - VP-group: update the backlog counter for that priority in the NVG
     */
    if (xive2_end_is_backlog(&end)) {

        if (format == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "XIVE: END %x/%x invalid config: F1 & backlog\n",
                          end_blk, end_idx);
            return;
        }

        if (!cam_ignore) {
            uint8_t ipb;
            Xive2Nvp nvp;

            /* NVP cache lookup */
            if (xive2_router_get_nvp(xrtr, nvx_blk, nvx_idx, &nvp)) {
                qemu_log_mask(LOG_GUEST_ERROR, "XIVE: no NVP %x/%x\n",
                              nvx_blk, nvx_idx);
                return;
            }

            if (!xive2_nvp_is_valid(&nvp)) {
                qemu_log_mask(LOG_GUEST_ERROR, "XIVE: NVP %x/%x is invalid\n",
                              nvx_blk, nvx_idx);
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
            xive2_router_write_nvp(xrtr, nvx_blk, nvx_idx, &nvp, 2);
        } else {
            Xive2Nvgc nvgc;
            uint32_t backlog;

            /*
             * For groups and crowds, the per-priority backlog
             * counters are stored in the NVG/NVC structures
             */
            if (xive2_router_get_nvgc(xrtr, crowd,
                                      nvx_blk, nvx_idx, &nvgc)) {
                qemu_log_mask(LOG_GUEST_ERROR, "XIVE: no %s %x/%x\n",
                              crowd ? "NVC" : "NVG", nvx_blk, nvx_idx);
                return;
            }

            if (!xive2_nvgc_is_valid(&nvgc)) {
                qemu_log_mask(LOG_GUEST_ERROR, "XIVE: NVG %x/%x is invalid\n",
                              nvx_blk, nvx_idx);
                return;
            }

            /*
             * Increment the backlog counter for that priority.
             * We only call broadcast the first time the counter is
             * incremented. broadcast will set the LSMFB field of the TIMA of
             * relevant threads so that they know an interrupt is pending.
             */
            backlog = xive2_nvgc_get_backlog(&nvgc, priority) + 1;
            xive2_nvgc_set_backlog(&nvgc, priority, backlog);
            xive2_router_write_nvgc(xrtr, crowd, nvx_blk, nvx_idx, &nvgc);

            if (backlog == 1) {
                XiveFabricClass *xfc = XIVE_FABRIC_GET_CLASS(xrtr->xfb);
                xfc->broadcast(xrtr->xfb, nvx_blk, nvx_idx,
                               crowd, cam_ignore, priority);

                if (!xive2_end_is_precluded_escalation(&end)) {
                    /*
                     * The interrupt will be picked up when the
                     * matching thread lowers its priority level
                     */
                    return;
                }
            }
        }
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

    if (xive2_end_is_escalate_end(&end)) {
        /*
         * Perform END Adaptive escalation processing
         * The END trigger becomes an Escalation trigger
         */
        uint8_t esc_blk = xive_get_field32(END2_W4_END_BLOCK, end.w4);
        uint32_t esc_idx = xive_get_field32(END2_W4_ESC_END_INDEX, end.w4);
        uint32_t esc_data = xive_get_field32(END2_W5_ESC_END_DATA, end.w5);
        trace_xive_escalate_end(end_blk, end_idx, esc_blk, esc_idx, esc_data);
        xive2_router_end_notify(xrtr, esc_blk, esc_idx, esc_data, false);
    } /* end END adaptive escalation */

    else {
        uint32_t lisn;              /* Logical Interrupt Source Number */

        /*
         *  Perform ESB escalation processing
         *      E[N] == 1 --> N
         *      Req[Block] <- E[ESB_Block]
         *      Req[Index] <- E[ESB_Index]
         *      Req[Offset] <- 0x000
         *      Execute <ESB Store> Req command
         */
        lisn = XIVE_EAS(xive_get_field32(END2_W4_END_BLOCK,     end.w4),
                        xive_get_field32(END2_W4_ESC_END_INDEX, end.w4));

        trace_xive_escalate_esb(end_blk, end_idx, lisn);
        xive2_notify(xrtr, lisn, true /* pq_checked */);
    }

    return;
}

void xive2_notify(Xive2Router *xrtr , uint32_t lisn, bool pq_checked)
{
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

    /* TODO: add support for EAS resume */
    if (xive2_eas_is_resume(&eas)) {
        qemu_log_mask(LOG_UNIMP,
                      "XIVE: EAS resume processing unimplemented - LISN %x\n",
                      lisn);
        return;
    }

    /*
     * The event trigger becomes an END trigger
     */
    xive2_router_end_notify(xrtr,
                            xive_get_field64(EAS2_END_BLOCK, eas.w),
                            xive_get_field64(EAS2_END_INDEX, eas.w),
                            xive_get_field64(EAS2_END_DATA,  eas.w),
                            false);
    return;
}

void xive2_router_notify(XiveNotifier *xn, uint32_t lisn, bool pq_checked)
{
    Xive2Router *xrtr = XIVE2_ROUTER(xn);

    xive2_notify(xrtr, lisn, pq_checked);
    return;
}

static const Property xive2_router_properties[] = {
    DEFINE_PROP_LINK("xive-fabric", Xive2Router, xfb,
                     TYPE_XIVE_FABRIC, XiveFabric *),
};

static void xive2_router_class_init(ObjectClass *klass, const void *data)
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
    .interfaces    = (const InterfaceInfo[]) {
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

static void xive2_end_source_class_init(ObjectClass *klass, const void *data)
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
