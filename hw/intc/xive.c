/*
 * QEMU PowerPC XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
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
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "hw/irq.h"
#include "hw/ppc/xive.h"
#include "hw/ppc/xive2.h"
#include "hw/ppc/xive_regs.h"
#include "trace.h"

/*
 * XIVE Thread Interrupt Management context
 */

/*
 * Convert an Interrupt Pending Buffer (IPB) register to a Pending
 * Interrupt Priority Register (PIPR), which contains the priority of
 * the most favored pending notification.
 */
static uint8_t ipb_to_pipr(uint8_t ibp)
{
    return ibp ? clz32((uint32_t)ibp << 24) : 0xff;
}

static uint8_t exception_mask(uint8_t ring)
{
    switch (ring) {
    case TM_QW1_OS:
        return TM_QW1_NSR_EO;
    case TM_QW3_HV_PHYS:
        return TM_QW3_NSR_HE;
    default:
        g_assert_not_reached();
    }
}

static qemu_irq xive_tctx_output(XiveTCTX *tctx, uint8_t ring)
{
        switch (ring) {
        case TM_QW0_USER:
                return 0; /* Not supported */
        case TM_QW1_OS:
                return tctx->os_output;
        case TM_QW2_HV_POOL:
        case TM_QW3_HV_PHYS:
                return tctx->hv_output;
        default:
                return 0;
        }
}

static uint64_t xive_tctx_accept(XiveTCTX *tctx, uint8_t ring)
{
    uint8_t *regs = &tctx->regs[ring];
    uint8_t nsr = regs[TM_NSR];
    uint8_t mask = exception_mask(ring);

    qemu_irq_lower(xive_tctx_output(tctx, ring));

    if (regs[TM_NSR] & mask) {
        uint8_t cppr = regs[TM_PIPR];

        regs[TM_CPPR] = cppr;

        /* Reset the pending buffer bit */
        regs[TM_IPB] &= ~xive_priority_to_ipb(cppr);
        regs[TM_PIPR] = ipb_to_pipr(regs[TM_IPB]);

        /* Drop Exception bit */
        regs[TM_NSR] &= ~mask;

        trace_xive_tctx_accept(tctx->cs->cpu_index, ring,
                               regs[TM_IPB], regs[TM_PIPR],
                               regs[TM_CPPR], regs[TM_NSR]);
    }

    return (nsr << 8) | regs[TM_CPPR];
}

static void xive_tctx_notify(XiveTCTX *tctx, uint8_t ring)
{
    uint8_t *regs = &tctx->regs[ring];

    if (regs[TM_PIPR] < regs[TM_CPPR]) {
        switch (ring) {
        case TM_QW1_OS:
            regs[TM_NSR] |= TM_QW1_NSR_EO;
            break;
        case TM_QW3_HV_PHYS:
            regs[TM_NSR] |= (TM_QW3_NSR_HE_PHYS << 6);
            break;
        default:
            g_assert_not_reached();
        }
        trace_xive_tctx_notify(tctx->cs->cpu_index, ring,
                               regs[TM_IPB], regs[TM_PIPR],
                               regs[TM_CPPR], regs[TM_NSR]);
        qemu_irq_raise(xive_tctx_output(tctx, ring));
    }
}

void xive_tctx_reset_os_signal(XiveTCTX *tctx)
{
    /*
     * Lower the External interrupt. Used when pulling an OS
     * context. It is necessary to avoid catching it in the hypervisor
     * context. It should be raised again when re-pushing the OS
     * context.
     */
    qemu_irq_lower(xive_tctx_output(tctx, TM_QW1_OS));
}

static void xive_tctx_set_cppr(XiveTCTX *tctx, uint8_t ring, uint8_t cppr)
{
    uint8_t *regs = &tctx->regs[ring];

    trace_xive_tctx_set_cppr(tctx->cs->cpu_index, ring,
                             regs[TM_IPB], regs[TM_PIPR],
                             cppr, regs[TM_NSR]);

    if (cppr > XIVE_PRIORITY_MAX) {
        cppr = 0xff;
    }

    tctx->regs[ring + TM_CPPR] = cppr;

    /* CPPR has changed, check if we need to raise a pending exception */
    xive_tctx_notify(tctx, ring);
}

void xive_tctx_ipb_update(XiveTCTX *tctx, uint8_t ring, uint8_t ipb)
{
    uint8_t *regs = &tctx->regs[ring];

    regs[TM_IPB] |= ipb;
    regs[TM_PIPR] = ipb_to_pipr(regs[TM_IPB]);
    xive_tctx_notify(tctx, ring);
}

/*
 * XIVE Thread Interrupt Management Area (TIMA)
 */

static void xive_tm_set_hv_cppr(XivePresenter *xptr, XiveTCTX *tctx,
                                hwaddr offset, uint64_t value, unsigned size)
{
    xive_tctx_set_cppr(tctx, TM_QW3_HV_PHYS, value & 0xff);
}

static uint64_t xive_tm_ack_hv_reg(XivePresenter *xptr, XiveTCTX *tctx,
                                   hwaddr offset, unsigned size)
{
    return xive_tctx_accept(tctx, TM_QW3_HV_PHYS);
}

static uint64_t xive_tm_pull_pool_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                                      hwaddr offset, unsigned size)
{
    uint32_t qw2w2_prev = xive_tctx_word2(&tctx->regs[TM_QW2_HV_POOL]);
    uint32_t qw2w2;

    qw2w2 = xive_set_field32(TM_QW2W2_VP, qw2w2_prev, 0);
    memcpy(&tctx->regs[TM_QW2_HV_POOL + TM_WORD2], &qw2w2, 4);
    return qw2w2;
}

static void xive_tm_vt_push(XivePresenter *xptr, XiveTCTX *tctx, hwaddr offset,
                            uint64_t value, unsigned size)
{
    tctx->regs[TM_QW3_HV_PHYS + TM_WORD2] = value & 0xff;
}

static uint64_t xive_tm_vt_poll(XivePresenter *xptr, XiveTCTX *tctx,
                                hwaddr offset, unsigned size)
{
    return tctx->regs[TM_QW3_HV_PHYS + TM_WORD2] & 0xff;
}

/*
 * Define an access map for each page of the TIMA that we will use in
 * the memory region ops to filter values when doing loads and stores
 * of raw registers values
 *
 * Registers accessibility bits :
 *
 *    0x0 - no access
 *    0x1 - write only
 *    0x2 - read only
 *    0x3 - read/write
 */

static const uint8_t xive_tm_hw_view[] = {
    3, 0, 0, 0,   0, 0, 0, 0,   3, 3, 3, 3,   0, 0, 0, 0, /* QW-0 User */
    3, 3, 3, 3,   3, 3, 0, 2,   3, 3, 3, 3,   0, 0, 0, 0, /* QW-1 OS   */
    0, 0, 3, 3,   0, 0, 0, 0,   3, 3, 3, 3,   0, 0, 0, 0, /* QW-2 POOL */
    3, 3, 3, 3,   0, 3, 0, 2,   3, 0, 0, 3,   3, 3, 3, 0, /* QW-3 PHYS */
};

static const uint8_t xive_tm_hv_view[] = {
    3, 0, 0, 0,   0, 0, 0, 0,   3, 3, 3, 3,   0, 0, 0, 0, /* QW-0 User */
    3, 3, 3, 3,   3, 3, 0, 2,   3, 3, 3, 3,   0, 0, 0, 0, /* QW-1 OS   */
    0, 0, 3, 3,   0, 0, 0, 0,   0, 3, 3, 3,   0, 0, 0, 0, /* QW-2 POOL */
    3, 3, 3, 3,   0, 3, 0, 2,   3, 0, 0, 3,   0, 0, 0, 0, /* QW-3 PHYS */
};

static const uint8_t xive_tm_os_view[] = {
    3, 0, 0, 0,   0, 0, 0, 0,   3, 3, 3, 3,   0, 0, 0, 0, /* QW-0 User */
    2, 3, 2, 2,   2, 2, 0, 2,   0, 0, 0, 0,   0, 0, 0, 0, /* QW-1 OS   */
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0, /* QW-2 POOL */
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0, /* QW-3 PHYS */
};

static const uint8_t xive_tm_user_view[] = {
    3, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0, /* QW-0 User */
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0, /* QW-1 OS   */
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0, /* QW-2 POOL */
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0, /* QW-3 PHYS */
};

/*
 * Overall TIMA access map for the thread interrupt management context
 * registers
 */
static const uint8_t *xive_tm_views[] = {
    [XIVE_TM_HW_PAGE]   = xive_tm_hw_view,
    [XIVE_TM_HV_PAGE]   = xive_tm_hv_view,
    [XIVE_TM_OS_PAGE]   = xive_tm_os_view,
    [XIVE_TM_USER_PAGE] = xive_tm_user_view,
};

/*
 * Computes a register access mask for a given offset in the TIMA
 */
static uint64_t xive_tm_mask(hwaddr offset, unsigned size, bool write)
{
    uint8_t page_offset = (offset >> TM_SHIFT) & 0x3;
    uint8_t reg_offset = offset & TM_REG_OFFSET;
    uint8_t reg_mask = write ? 0x1 : 0x2;
    uint64_t mask = 0x0;
    int i;

    for (i = 0; i < size; i++) {
        if (xive_tm_views[page_offset][reg_offset + i] & reg_mask) {
            mask |= (uint64_t) 0xff << (8 * (size - i - 1));
        }
    }

    return mask;
}

static void xive_tm_raw_write(XiveTCTX *tctx, hwaddr offset, uint64_t value,
                              unsigned size)
{
    uint8_t ring_offset = offset & TM_RING_OFFSET;
    uint8_t reg_offset = offset & TM_REG_OFFSET;
    uint64_t mask = xive_tm_mask(offset, size, true);
    int i;

    /*
     * Only 4 or 8 bytes stores are allowed and the User ring is
     * excluded
     */
    if (size < 4 || !mask || ring_offset == TM_QW0_USER) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid write access at TIMA @%"
                      HWADDR_PRIx"\n", offset);
        return;
    }

    /*
     * Use the register offset for the raw values and filter out
     * reserved values
     */
    for (i = 0; i < size; i++) {
        uint8_t byte_mask = (mask >> (8 * (size - i - 1)));
        if (byte_mask) {
            tctx->regs[reg_offset + i] = (value >> (8 * (size - i - 1))) &
                byte_mask;
        }
    }
}

static uint64_t xive_tm_raw_read(XiveTCTX *tctx, hwaddr offset, unsigned size)
{
    uint8_t ring_offset = offset & TM_RING_OFFSET;
    uint8_t reg_offset = offset & TM_REG_OFFSET;
    uint64_t mask = xive_tm_mask(offset, size, false);
    uint64_t ret;
    int i;

    /*
     * Only 4 or 8 bytes loads are allowed and the User ring is
     * excluded
     */
    if (size < 4 || !mask || ring_offset == TM_QW0_USER) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid read access at TIMA @%"
                      HWADDR_PRIx"\n", offset);
        return -1;
    }

    /* Use the register offset for the raw values */
    ret = 0;
    for (i = 0; i < size; i++) {
        ret |= (uint64_t) tctx->regs[reg_offset + i] << (8 * (size - i - 1));
    }

    /* filter out reserved values */
    return ret & mask;
}

/*
 * The TM context is mapped twice within each page. Stores and loads
 * to the first mapping below 2K write and read the specified values
 * without modification. The second mapping above 2K performs specific
 * state changes (side effects) in addition to setting/returning the
 * interrupt management area context of the processor thread.
 */
static uint64_t xive_tm_ack_os_reg(XivePresenter *xptr, XiveTCTX *tctx,
                                   hwaddr offset, unsigned size)
{
    return xive_tctx_accept(tctx, TM_QW1_OS);
}

static void xive_tm_set_os_cppr(XivePresenter *xptr, XiveTCTX *tctx,
                                hwaddr offset, uint64_t value, unsigned size)
{
    xive_tctx_set_cppr(tctx, TM_QW1_OS, value & 0xff);
}

/*
 * Adjust the IPB to allow a CPU to process event queues of other
 * priorities during one physical interrupt cycle.
 */
static void xive_tm_set_os_pending(XivePresenter *xptr, XiveTCTX *tctx,
                                   hwaddr offset, uint64_t value, unsigned size)
{
    xive_tctx_ipb_update(tctx, TM_QW1_OS, xive_priority_to_ipb(value & 0xff));
}

static void xive_os_cam_decode(uint32_t cam, uint8_t *nvt_blk,
                               uint32_t *nvt_idx, bool *vo)
{
    if (nvt_blk) {
        *nvt_blk = xive_nvt_blk(cam);
    }
    if (nvt_idx) {
        *nvt_idx = xive_nvt_idx(cam);
    }
    if (vo) {
        *vo = !!(cam & TM_QW1W2_VO);
    }
}

static uint32_t xive_tctx_get_os_cam(XiveTCTX *tctx, uint8_t *nvt_blk,
                                     uint32_t *nvt_idx, bool *vo)
{
    uint32_t qw1w2 = xive_tctx_word2(&tctx->regs[TM_QW1_OS]);
    uint32_t cam = be32_to_cpu(qw1w2);

    xive_os_cam_decode(cam, nvt_blk, nvt_idx, vo);
    return qw1w2;
}

static void xive_tctx_set_os_cam(XiveTCTX *tctx, uint32_t qw1w2)
{
    memcpy(&tctx->regs[TM_QW1_OS + TM_WORD2], &qw1w2, 4);
}

static uint64_t xive_tm_pull_os_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                                    hwaddr offset, unsigned size)
{
    uint32_t qw1w2;
    uint32_t qw1w2_new;
    uint8_t nvt_blk;
    uint32_t nvt_idx;
    bool vo;

    qw1w2 = xive_tctx_get_os_cam(tctx, &nvt_blk, &nvt_idx, &vo);

    if (!vo) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: pulling invalid NVT %x/%x !?\n",
                      nvt_blk, nvt_idx);
    }

    /* Invalidate CAM line */
    qw1w2_new = xive_set_field32(TM_QW1W2_VO, qw1w2, 0);
    xive_tctx_set_os_cam(tctx, qw1w2_new);

    xive_tctx_reset_os_signal(tctx);
    return qw1w2;
}

static void xive_tctx_need_resend(XiveRouter *xrtr, XiveTCTX *tctx,
                                  uint8_t nvt_blk, uint32_t nvt_idx)
{
    XiveNVT nvt;
    uint8_t ipb;

    /*
     * Grab the associated NVT to pull the pending bits, and merge
     * them with the IPB of the thread interrupt context registers
     */
    if (xive_router_get_nvt(xrtr, nvt_blk, nvt_idx, &nvt)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid NVT %x/%x\n",
                          nvt_blk, nvt_idx);
        return;
    }

    ipb = xive_get_field32(NVT_W4_IPB, nvt.w4);

    if (ipb) {
        /* Reset the NVT value */
        nvt.w4 = xive_set_field32(NVT_W4_IPB, nvt.w4, 0);
        xive_router_write_nvt(xrtr, nvt_blk, nvt_idx, &nvt, 4);
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
static void xive_tm_push_os_ctx(XivePresenter *xptr, XiveTCTX *tctx,
                                hwaddr offset, uint64_t value, unsigned size)
{
    uint32_t cam = value;
    uint32_t qw1w2 = cpu_to_be32(cam);
    uint8_t nvt_blk;
    uint32_t nvt_idx;
    bool vo;

    xive_os_cam_decode(cam, &nvt_blk, &nvt_idx, &vo);

    /* First update the registers */
    xive_tctx_set_os_cam(tctx, qw1w2);

    /* Check the interrupt pending bits */
    if (vo) {
        xive_tctx_need_resend(XIVE_ROUTER(xptr), tctx, nvt_blk, nvt_idx);
    }
}

static uint32_t xive_presenter_get_config(XivePresenter *xptr)
{
    XivePresenterClass *xpc = XIVE_PRESENTER_GET_CLASS(xptr);

    return xpc->get_config(xptr);
}

/*
 * Define a mapping of "special" operations depending on the TIMA page
 * offset and the size of the operation.
 */
typedef struct XiveTmOp {
    uint8_t  page_offset;
    uint32_t op_offset;
    unsigned size;
    void     (*write_handler)(XivePresenter *xptr, XiveTCTX *tctx,
                              hwaddr offset,
                              uint64_t value, unsigned size);
    uint64_t (*read_handler)(XivePresenter *xptr, XiveTCTX *tctx, hwaddr offset,
                             unsigned size);
} XiveTmOp;

static const XiveTmOp xive_tm_operations[] = {
    /*
     * MMIOs below 2K : raw values and special operations without side
     * effects
     */
    { XIVE_TM_OS_PAGE, TM_QW1_OS + TM_CPPR,   1, xive_tm_set_os_cppr, NULL },
    { XIVE_TM_HV_PAGE, TM_QW1_OS + TM_WORD2,     4, xive_tm_push_os_ctx, NULL },
    { XIVE_TM_HV_PAGE, TM_QW3_HV_PHYS + TM_CPPR, 1, xive_tm_set_hv_cppr, NULL },
    { XIVE_TM_HV_PAGE, TM_QW3_HV_PHYS + TM_WORD2, 1, xive_tm_vt_push, NULL },
    { XIVE_TM_HV_PAGE, TM_QW3_HV_PHYS + TM_WORD2, 1, NULL, xive_tm_vt_poll },

    /* MMIOs above 2K : special operations with side effects */
    { XIVE_TM_OS_PAGE, TM_SPC_ACK_OS_REG,     2, NULL, xive_tm_ack_os_reg },
    { XIVE_TM_OS_PAGE, TM_SPC_SET_OS_PENDING, 1, xive_tm_set_os_pending, NULL },
    { XIVE_TM_HV_PAGE, TM_SPC_PULL_OS_CTX,    4, NULL, xive_tm_pull_os_ctx },
    { XIVE_TM_HV_PAGE, TM_SPC_PULL_OS_CTX,    8, NULL, xive_tm_pull_os_ctx },
    { XIVE_TM_HV_PAGE, TM_SPC_ACK_HV_REG,     2, NULL, xive_tm_ack_hv_reg },
    { XIVE_TM_HV_PAGE, TM_SPC_PULL_POOL_CTX,  4, NULL, xive_tm_pull_pool_ctx },
    { XIVE_TM_HV_PAGE, TM_SPC_PULL_POOL_CTX,  8, NULL, xive_tm_pull_pool_ctx },
};

static const XiveTmOp xive2_tm_operations[] = {
    /*
     * MMIOs below 2K : raw values and special operations without side
     * effects
     */
    { XIVE_TM_OS_PAGE, TM_QW1_OS + TM_CPPR,   1, xive_tm_set_os_cppr, NULL },
    { XIVE_TM_HV_PAGE, TM_QW1_OS + TM_WORD2,  4, xive2_tm_push_os_ctx, NULL },
    { XIVE_TM_HV_PAGE, TM_QW3_HV_PHYS + TM_CPPR, 1, xive_tm_set_hv_cppr, NULL },
    { XIVE_TM_HV_PAGE, TM_QW3_HV_PHYS + TM_WORD2, 1, xive_tm_vt_push, NULL },
    { XIVE_TM_HV_PAGE, TM_QW3_HV_PHYS + TM_WORD2, 1, NULL, xive_tm_vt_poll },

    /* MMIOs above 2K : special operations with side effects */
    { XIVE_TM_OS_PAGE, TM_SPC_ACK_OS_REG,     2, NULL, xive_tm_ack_os_reg },
    { XIVE_TM_OS_PAGE, TM_SPC_SET_OS_PENDING, 1, xive_tm_set_os_pending, NULL },
    { XIVE_TM_HV_PAGE, TM_SPC_PULL_OS_CTX,    4, NULL, xive2_tm_pull_os_ctx },
    { XIVE_TM_HV_PAGE, TM_SPC_PULL_OS_CTX,    8, NULL, xive2_tm_pull_os_ctx },
    { XIVE_TM_HV_PAGE, TM_SPC_ACK_HV_REG,     2, NULL, xive_tm_ack_hv_reg },
    { XIVE_TM_HV_PAGE, TM_SPC_PULL_POOL_CTX,  4, NULL, xive_tm_pull_pool_ctx },
    { XIVE_TM_HV_PAGE, TM_SPC_PULL_POOL_CTX,  8, NULL, xive_tm_pull_pool_ctx },
};

static const XiveTmOp *xive_tm_find_op(XivePresenter *xptr, hwaddr offset,
                                       unsigned size, bool write)
{
    uint8_t page_offset = (offset >> TM_SHIFT) & 0x3;
    uint32_t op_offset = offset & TM_ADDRESS_MASK;
    const XiveTmOp *tm_ops;
    int i, tm_ops_count;
    uint32_t cfg;

    cfg = xive_presenter_get_config(xptr);
    if (cfg & XIVE_PRESENTER_GEN1_TIMA_OS) {
        tm_ops = xive_tm_operations;
        tm_ops_count = ARRAY_SIZE(xive_tm_operations);
    } else {
        tm_ops = xive2_tm_operations;
        tm_ops_count = ARRAY_SIZE(xive2_tm_operations);
    }

    for (i = 0; i < tm_ops_count; i++) {
        const XiveTmOp *xto = &tm_ops[i];

        /* Accesses done from a more privileged TIMA page is allowed */
        if (xto->page_offset >= page_offset &&
            xto->op_offset == op_offset &&
            xto->size == size &&
            ((write && xto->write_handler) || (!write && xto->read_handler))) {
            return xto;
        }
    }
    return NULL;
}

/*
 * TIMA MMIO handlers
 */
void xive_tctx_tm_write(XivePresenter *xptr, XiveTCTX *tctx, hwaddr offset,
                        uint64_t value, unsigned size)
{
    const XiveTmOp *xto;

    trace_xive_tctx_tm_write(tctx->cs->cpu_index, offset, size, value);

    /*
     * TODO: check V bit in Q[0-3]W2
     */

    /*
     * First, check for special operations in the 2K region
     */
    if (offset & TM_SPECIAL_OP) {
        xto = xive_tm_find_op(tctx->xptr, offset, size, true);
        if (!xto) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid write access at TIMA "
                          "@%"HWADDR_PRIx"\n", offset);
        } else {
            xto->write_handler(xptr, tctx, offset, value, size);
        }
        return;
    }

    /*
     * Then, for special operations in the region below 2K.
     */
    xto = xive_tm_find_op(tctx->xptr, offset, size, true);
    if (xto) {
        xto->write_handler(xptr, tctx, offset, value, size);
        return;
    }

    /*
     * Finish with raw access to the register values
     */
    xive_tm_raw_write(tctx, offset, value, size);
}

uint64_t xive_tctx_tm_read(XivePresenter *xptr, XiveTCTX *tctx, hwaddr offset,
                           unsigned size)
{
    const XiveTmOp *xto;
    uint64_t ret;

    /*
     * TODO: check V bit in Q[0-3]W2
     */

    /*
     * First, check for special operations in the 2K region
     */
    if (offset & TM_SPECIAL_OP) {
        xto = xive_tm_find_op(tctx->xptr, offset, size, false);
        if (!xto) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid read access to TIMA"
                          "@%"HWADDR_PRIx"\n", offset);
            return -1;
        }
        ret = xto->read_handler(xptr, tctx, offset, size);
        goto out;
    }

    /*
     * Then, for special operations in the region below 2K.
     */
    xto = xive_tm_find_op(tctx->xptr, offset, size, false);
    if (xto) {
        ret = xto->read_handler(xptr, tctx, offset, size);
        goto out;
    }

    /*
     * Finish with raw access to the register values
     */
    ret = xive_tm_raw_read(tctx, offset, size);
out:
    trace_xive_tctx_tm_read(tctx->cs->cpu_index, offset, size, ret);
    return ret;
}

static char *xive_tctx_ring_print(uint8_t *ring)
{
    uint32_t w2 = xive_tctx_word2(ring);

    return g_strdup_printf("%02x   %02x  %02x    %02x   %02x  "
                   "%02x  %02x   %02x  %08x",
                   ring[TM_NSR], ring[TM_CPPR], ring[TM_IPB], ring[TM_LSMFB],
                   ring[TM_ACK_CNT], ring[TM_INC], ring[TM_AGE], ring[TM_PIPR],
                   be32_to_cpu(w2));
}

static const char * const xive_tctx_ring_names[] = {
    "USER", "OS", "POOL", "PHYS",
};

/*
 * kvm_irqchip_in_kernel() will cause the compiler to turn this
 * info a nop if CONFIG_KVM isn't defined.
 */
#define xive_in_kernel(xptr)                                            \
    (kvm_irqchip_in_kernel() &&                                         \
     ({                                                                 \
         XivePresenterClass *xpc = XIVE_PRESENTER_GET_CLASS(xptr);      \
         xpc->in_kernel ? xpc->in_kernel(xptr) : false;                 \
     }))

void xive_tctx_pic_print_info(XiveTCTX *tctx, Monitor *mon)
{
    int cpu_index;
    int i;

    /* Skip partially initialized vCPUs. This can happen on sPAPR when vCPUs
     * are hot plugged or unplugged.
     */
    if (!tctx) {
        return;
    }

    cpu_index = tctx->cs ? tctx->cs->cpu_index : -1;

    if (xive_in_kernel(tctx->xptr)) {
        Error *local_err = NULL;

        kvmppc_xive_cpu_synchronize_state(tctx, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return;
        }
    }

    monitor_printf(mon, "CPU[%04x]:   QW   NSR CPPR IPB LSMFB ACK# INC AGE PIPR"
                   "  W2\n", cpu_index);

    for (i = 0; i < XIVE_TM_RING_COUNT; i++) {
        char *s = xive_tctx_ring_print(&tctx->regs[i * XIVE_TM_RING_SIZE]);
        monitor_printf(mon, "CPU[%04x]: %4s    %s\n", cpu_index,
                       xive_tctx_ring_names[i], s);
        g_free(s);
    }
}

void xive_tctx_reset(XiveTCTX *tctx)
{
    memset(tctx->regs, 0, sizeof(tctx->regs));

    /* Set some defaults */
    tctx->regs[TM_QW1_OS + TM_LSMFB] = 0xFF;
    tctx->regs[TM_QW1_OS + TM_ACK_CNT] = 0xFF;
    tctx->regs[TM_QW1_OS + TM_AGE] = 0xFF;

    /*
     * Initialize PIPR to 0xFF to avoid phantom interrupts when the
     * CPPR is first set.
     */
    tctx->regs[TM_QW1_OS + TM_PIPR] =
        ipb_to_pipr(tctx->regs[TM_QW1_OS + TM_IPB]);
    tctx->regs[TM_QW3_HV_PHYS + TM_PIPR] =
        ipb_to_pipr(tctx->regs[TM_QW3_HV_PHYS + TM_IPB]);
}

static void xive_tctx_realize(DeviceState *dev, Error **errp)
{
    XiveTCTX *tctx = XIVE_TCTX(dev);
    PowerPCCPU *cpu;
    CPUPPCState *env;

    assert(tctx->cs);
    assert(tctx->xptr);

    cpu = POWERPC_CPU(tctx->cs);
    env = &cpu->env;
    switch (PPC_INPUT(env)) {
    case PPC_FLAGS_INPUT_POWER9:
        tctx->hv_output = qdev_get_gpio_in(DEVICE(cpu), POWER9_INPUT_HINT);
        tctx->os_output = qdev_get_gpio_in(DEVICE(cpu), POWER9_INPUT_INT);
        break;

    default:
        error_setg(errp, "XIVE interrupt controller does not support "
                   "this CPU bus model");
        return;
    }

    /* Connect the presenter to the VCPU (required for CPU hotplug) */
    if (xive_in_kernel(tctx->xptr)) {
        if (kvmppc_xive_cpu_connect(tctx, errp) < 0) {
            return;
        }
    }
}

static int vmstate_xive_tctx_pre_save(void *opaque)
{
    XiveTCTX *tctx = XIVE_TCTX(opaque);
    Error *local_err = NULL;
    int ret;

    if (xive_in_kernel(tctx->xptr)) {
        ret = kvmppc_xive_cpu_get_state(tctx, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            return ret;
        }
    }

    return 0;
}

static int vmstate_xive_tctx_post_load(void *opaque, int version_id)
{
    XiveTCTX *tctx = XIVE_TCTX(opaque);
    Error *local_err = NULL;
    int ret;

    if (xive_in_kernel(tctx->xptr)) {
        /*
         * Required for hotplugged CPU, for which the state comes
         * after all states of the machine.
         */
        ret = kvmppc_xive_cpu_set_state(tctx, &local_err);
        if (ret < 0) {
            error_report_err(local_err);
            return ret;
        }
    }

    return 0;
}

static const VMStateDescription vmstate_xive_tctx = {
    .name = TYPE_XIVE_TCTX,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = vmstate_xive_tctx_pre_save,
    .post_load = vmstate_xive_tctx_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_BUFFER(regs, XiveTCTX),
        VMSTATE_END_OF_LIST()
    },
};

static Property xive_tctx_properties[] = {
    DEFINE_PROP_LINK("cpu", XiveTCTX, cs, TYPE_CPU, CPUState *),
    DEFINE_PROP_LINK("presenter", XiveTCTX, xptr, TYPE_XIVE_PRESENTER,
                     XivePresenter *),
    DEFINE_PROP_END_OF_LIST(),
};

static void xive_tctx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "XIVE Interrupt Thread Context";
    dc->realize = xive_tctx_realize;
    dc->vmsd = &vmstate_xive_tctx;
    device_class_set_props(dc, xive_tctx_properties);
    /*
     * Reason: part of XIVE interrupt controller, needs to be wired up
     * by xive_tctx_create().
     */
    dc->user_creatable = false;
}

static const TypeInfo xive_tctx_info = {
    .name          = TYPE_XIVE_TCTX,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XiveTCTX),
    .class_init    = xive_tctx_class_init,
};

Object *xive_tctx_create(Object *cpu, XivePresenter *xptr, Error **errp)
{
    Object *obj;

    obj = object_new(TYPE_XIVE_TCTX);
    object_property_add_child(cpu, TYPE_XIVE_TCTX, obj);
    object_unref(obj);
    object_property_set_link(obj, "cpu", cpu, &error_abort);
    object_property_set_link(obj, "presenter", OBJECT(xptr), &error_abort);
    if (!qdev_realize(DEVICE(obj), NULL, errp)) {
        object_unparent(obj);
        return NULL;
    }
    return obj;
}

void xive_tctx_destroy(XiveTCTX *tctx)
{
    Object *obj = OBJECT(tctx);

    object_unparent(obj);
}

/*
 * XIVE ESB helpers
 */

uint8_t xive_esb_set(uint8_t *pq, uint8_t value)
{
    uint8_t old_pq = *pq & 0x3;

    *pq &= ~0x3;
    *pq |= value & 0x3;

    return old_pq;
}

bool xive_esb_trigger(uint8_t *pq)
{
    uint8_t old_pq = *pq & 0x3;

    switch (old_pq) {
    case XIVE_ESB_RESET:
        xive_esb_set(pq, XIVE_ESB_PENDING);
        return true;
    case XIVE_ESB_PENDING:
    case XIVE_ESB_QUEUED:
        xive_esb_set(pq, XIVE_ESB_QUEUED);
        return false;
    case XIVE_ESB_OFF:
        xive_esb_set(pq, XIVE_ESB_OFF);
        return false;
    default:
         g_assert_not_reached();
    }
}

bool xive_esb_eoi(uint8_t *pq)
{
    uint8_t old_pq = *pq & 0x3;

    switch (old_pq) {
    case XIVE_ESB_RESET:
    case XIVE_ESB_PENDING:
        xive_esb_set(pq, XIVE_ESB_RESET);
        return false;
    case XIVE_ESB_QUEUED:
        xive_esb_set(pq, XIVE_ESB_PENDING);
        return true;
    case XIVE_ESB_OFF:
        xive_esb_set(pq, XIVE_ESB_OFF);
        return false;
    default:
         g_assert_not_reached();
    }
}

/*
 * XIVE Interrupt Source (or IVSE)
 */

uint8_t xive_source_esb_get(XiveSource *xsrc, uint32_t srcno)
{
    assert(srcno < xsrc->nr_irqs);

    return xsrc->status[srcno] & 0x3;
}

uint8_t xive_source_esb_set(XiveSource *xsrc, uint32_t srcno, uint8_t pq)
{
    assert(srcno < xsrc->nr_irqs);

    return xive_esb_set(&xsrc->status[srcno], pq);
}

/*
 * Returns whether the event notification should be forwarded.
 */
static bool xive_source_lsi_trigger(XiveSource *xsrc, uint32_t srcno)
{
    uint8_t old_pq = xive_source_esb_get(xsrc, srcno);

    xive_source_set_asserted(xsrc, srcno, true);

    switch (old_pq) {
    case XIVE_ESB_RESET:
        xive_source_esb_set(xsrc, srcno, XIVE_ESB_PENDING);
        return true;
    default:
        return false;
    }
}

/*
 * Sources can be configured with PQ offloading in which case the check
 * on the PQ state bits of MSIs is disabled
 */
static bool xive_source_esb_disabled(XiveSource *xsrc, uint32_t srcno)
{
    return (xsrc->esb_flags & XIVE_SRC_PQ_DISABLE) &&
        !xive_source_irq_is_lsi(xsrc, srcno);
}

/*
 * Returns whether the event notification should be forwarded.
 */
static bool xive_source_esb_trigger(XiveSource *xsrc, uint32_t srcno)
{
    bool ret;

    assert(srcno < xsrc->nr_irqs);

    if (xive_source_esb_disabled(xsrc, srcno)) {
        return true;
    }

    ret = xive_esb_trigger(&xsrc->status[srcno]);

    if (xive_source_irq_is_lsi(xsrc, srcno) &&
        xive_source_esb_get(xsrc, srcno) == XIVE_ESB_QUEUED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "XIVE: queued an event on LSI IRQ %d\n", srcno);
    }

    return ret;
}

/*
 * Returns whether the event notification should be forwarded.
 */
static bool xive_source_esb_eoi(XiveSource *xsrc, uint32_t srcno)
{
    bool ret;

    assert(srcno < xsrc->nr_irqs);

    if (xive_source_esb_disabled(xsrc, srcno)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid EOI for IRQ %d\n", srcno);
        return false;
    }

    ret = xive_esb_eoi(&xsrc->status[srcno]);

    /*
     * LSI sources do not set the Q bit but they can still be
     * asserted, in which case we should forward a new event
     * notification
     */
    if (xive_source_irq_is_lsi(xsrc, srcno) &&
        xive_source_is_asserted(xsrc, srcno)) {
        ret = xive_source_lsi_trigger(xsrc, srcno);
    }

    return ret;
}

/*
 * Forward the source event notification to the Router
 */
static void xive_source_notify(XiveSource *xsrc, int srcno)
{
    XiveNotifierClass *xnc = XIVE_NOTIFIER_GET_CLASS(xsrc->xive);
    bool pq_checked = !xive_source_esb_disabled(xsrc, srcno);

    if (xnc->notify) {
        xnc->notify(xsrc->xive, srcno, pq_checked);
    }
}

/*
 * In a two pages ESB MMIO setting, even page is the trigger page, odd
 * page is for management
 */
static inline bool addr_is_even(hwaddr addr, uint32_t shift)
{
    return !((addr >> shift) & 1);
}

static inline bool xive_source_is_trigger_page(XiveSource *xsrc, hwaddr addr)
{
    return xive_source_esb_has_2page(xsrc) &&
        addr_is_even(addr, xsrc->esb_shift - 1);
}

/*
 * ESB MMIO loads
 *                      Trigger page    Management/EOI page
 *
 * ESB MMIO setting     2 pages         1 or 2 pages
 *
 * 0x000 .. 0x3FF       -1              EOI and return 0|1
 * 0x400 .. 0x7FF       -1              EOI and return 0|1
 * 0x800 .. 0xBFF       -1              return PQ
 * 0xC00 .. 0xCFF       -1              return PQ and atomically PQ=00
 * 0xD00 .. 0xDFF       -1              return PQ and atomically PQ=01
 * 0xE00 .. 0xDFF       -1              return PQ and atomically PQ=10
 * 0xF00 .. 0xDFF       -1              return PQ and atomically PQ=11
 */
static uint64_t xive_source_esb_read(void *opaque, hwaddr addr, unsigned size)
{
    XiveSource *xsrc = XIVE_SOURCE(opaque);
    uint32_t offset = addr & 0xFFF;
    uint32_t srcno = addr >> xsrc->esb_shift;
    uint64_t ret = -1;

    /* In a two pages ESB MMIO setting, trigger page should not be read */
    if (xive_source_is_trigger_page(xsrc, addr)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "XIVE: invalid load on IRQ %d trigger page at "
                      "0x%"HWADDR_PRIx"\n", srcno, addr);
        return -1;
    }

    switch (offset) {
    case XIVE_ESB_LOAD_EOI ... XIVE_ESB_LOAD_EOI + 0x7FF:
        ret = xive_source_esb_eoi(xsrc, srcno);

        /* Forward the source event notification for routing */
        if (ret) {
            xive_source_notify(xsrc, srcno);
        }
        break;

    case XIVE_ESB_GET ... XIVE_ESB_GET + 0x3FF:
        ret = xive_source_esb_get(xsrc, srcno);
        break;

    case XIVE_ESB_SET_PQ_00 ... XIVE_ESB_SET_PQ_00 + 0x0FF:
    case XIVE_ESB_SET_PQ_01 ... XIVE_ESB_SET_PQ_01 + 0x0FF:
    case XIVE_ESB_SET_PQ_10 ... XIVE_ESB_SET_PQ_10 + 0x0FF:
    case XIVE_ESB_SET_PQ_11 ... XIVE_ESB_SET_PQ_11 + 0x0FF:
        ret = xive_source_esb_set(xsrc, srcno, (offset >> 8) & 0x3);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid ESB load addr %x\n",
                      offset);
    }

    trace_xive_source_esb_read(addr, srcno, ret);

    return ret;
}

/*
 * ESB MMIO stores
 *                      Trigger page    Management/EOI page
 *
 * ESB MMIO setting     2 pages         1 or 2 pages
 *
 * 0x000 .. 0x3FF       Trigger         Trigger
 * 0x400 .. 0x7FF       Trigger         EOI
 * 0x800 .. 0xBFF       Trigger         undefined
 * 0xC00 .. 0xCFF       Trigger         PQ=00
 * 0xD00 .. 0xDFF       Trigger         PQ=01
 * 0xE00 .. 0xDFF       Trigger         PQ=10
 * 0xF00 .. 0xDFF       Trigger         PQ=11
 */
static void xive_source_esb_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    XiveSource *xsrc = XIVE_SOURCE(opaque);
    uint32_t offset = addr & 0xFFF;
    uint32_t srcno = addr >> xsrc->esb_shift;
    bool notify = false;

    trace_xive_source_esb_write(addr, srcno, value);

    /* In a two pages ESB MMIO setting, trigger page only triggers */
    if (xive_source_is_trigger_page(xsrc, addr)) {
        notify = xive_source_esb_trigger(xsrc, srcno);
        goto out;
    }

    switch (offset) {
    case 0 ... 0x3FF:
        notify = xive_source_esb_trigger(xsrc, srcno);
        break;

    case XIVE_ESB_STORE_EOI ... XIVE_ESB_STORE_EOI + 0x3FF:
        if (!(xsrc->esb_flags & XIVE_SRC_STORE_EOI)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "XIVE: invalid Store EOI for IRQ %d\n", srcno);
            return;
        }

        notify = xive_source_esb_eoi(xsrc, srcno);
        break;

    /*
     * This is an internal offset used to inject triggers when the PQ
     * state bits are not controlled locally. Such as for LSIs when
     * under ABT mode.
     */
    case XIVE_ESB_INJECT ... XIVE_ESB_INJECT + 0x3FF:
        notify = true;
        break;

    case XIVE_ESB_SET_PQ_00 ... XIVE_ESB_SET_PQ_00 + 0x0FF:
    case XIVE_ESB_SET_PQ_01 ... XIVE_ESB_SET_PQ_01 + 0x0FF:
    case XIVE_ESB_SET_PQ_10 ... XIVE_ESB_SET_PQ_10 + 0x0FF:
    case XIVE_ESB_SET_PQ_11 ... XIVE_ESB_SET_PQ_11 + 0x0FF:
        xive_source_esb_set(xsrc, srcno, (offset >> 8) & 0x3);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid ESB write addr %x\n",
                      offset);
        return;
    }

out:
    /* Forward the source event notification for routing */
    if (notify) {
        xive_source_notify(xsrc, srcno);
    }
}

static const MemoryRegionOps xive_source_esb_ops = {
    .read = xive_source_esb_read,
    .write = xive_source_esb_write,
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

void xive_source_set_irq(void *opaque, int srcno, int val)
{
    XiveSource *xsrc = XIVE_SOURCE(opaque);
    bool notify = false;

    if (xive_source_irq_is_lsi(xsrc, srcno)) {
        if (val) {
            notify = xive_source_lsi_trigger(xsrc, srcno);
        } else {
            xive_source_set_asserted(xsrc, srcno, false);
        }
    } else {
        if (val) {
            notify = xive_source_esb_trigger(xsrc, srcno);
        }
    }

    /* Forward the source event notification for routing */
    if (notify) {
        xive_source_notify(xsrc, srcno);
    }
}

void xive_source_pic_print_info(XiveSource *xsrc, uint32_t offset, Monitor *mon)
{
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        uint8_t pq = xive_source_esb_get(xsrc, i);

        if (pq == XIVE_ESB_OFF) {
            continue;
        }

        monitor_printf(mon, "  %08x %s %c%c%c\n", i + offset,
                       xive_source_irq_is_lsi(xsrc, i) ? "LSI" : "MSI",
                       pq & XIVE_ESB_VAL_P ? 'P' : '-',
                       pq & XIVE_ESB_VAL_Q ? 'Q' : '-',
                       xive_source_is_asserted(xsrc, i) ? 'A' : ' ');
    }
}

static void xive_source_reset(void *dev)
{
    XiveSource *xsrc = XIVE_SOURCE(dev);

    /* Do not clear the LSI bitmap */

    memset(xsrc->status, xsrc->reset_pq, xsrc->nr_irqs);
}

static void xive_source_realize(DeviceState *dev, Error **errp)
{
    XiveSource *xsrc = XIVE_SOURCE(dev);
    size_t esb_len = xive_source_esb_len(xsrc);

    assert(xsrc->xive);

    if (!xsrc->nr_irqs) {
        error_setg(errp, "Number of interrupt needs to be greater than 0");
        return;
    }

    if (xsrc->esb_shift != XIVE_ESB_4K &&
        xsrc->esb_shift != XIVE_ESB_4K_2PAGE &&
        xsrc->esb_shift != XIVE_ESB_64K &&
        xsrc->esb_shift != XIVE_ESB_64K_2PAGE) {
        error_setg(errp, "Invalid ESB shift setting");
        return;
    }

    xsrc->status = g_malloc0(xsrc->nr_irqs);
    xsrc->lsi_map = bitmap_new(xsrc->nr_irqs);

    memory_region_init(&xsrc->esb_mmio, OBJECT(xsrc), "xive.esb", esb_len);
    memory_region_init_io(&xsrc->esb_mmio_emulated, OBJECT(xsrc),
                          &xive_source_esb_ops, xsrc, "xive.esb-emulated",
                          esb_len);
    memory_region_add_subregion(&xsrc->esb_mmio, 0, &xsrc->esb_mmio_emulated);

    qemu_register_reset(xive_source_reset, dev);
}

static const VMStateDescription vmstate_xive_source = {
    .name = TYPE_XIVE_SOURCE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_EQUAL(nr_irqs, XiveSource, NULL),
        VMSTATE_VBUFFER_UINT32(status, XiveSource, 1, NULL, nr_irqs),
        VMSTATE_END_OF_LIST()
    },
};

/*
 * The default XIVE interrupt source setting for the ESB MMIOs is two
 * 64k pages without Store EOI, to be in sync with KVM.
 */
static Property xive_source_properties[] = {
    DEFINE_PROP_UINT64("flags", XiveSource, esb_flags, 0),
    DEFINE_PROP_UINT32("nr-irqs", XiveSource, nr_irqs, 0),
    DEFINE_PROP_UINT32("shift", XiveSource, esb_shift, XIVE_ESB_64K_2PAGE),
    /*
     * By default, PQs are initialized to 0b01 (Q=1) which corresponds
     * to "ints off"
     */
    DEFINE_PROP_UINT8("reset-pq", XiveSource, reset_pq, XIVE_ESB_OFF),
    DEFINE_PROP_LINK("xive", XiveSource, xive, TYPE_XIVE_NOTIFIER,
                     XiveNotifier *),
    DEFINE_PROP_END_OF_LIST(),
};

static void xive_source_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc    = "XIVE Interrupt Source";
    device_class_set_props(dc, xive_source_properties);
    dc->realize = xive_source_realize;
    dc->vmsd    = &vmstate_xive_source;
    /*
     * Reason: part of XIVE interrupt controller, needs to be wired up,
     * e.g. by spapr_xive_instance_init().
     */
    dc->user_creatable = false;
}

static const TypeInfo xive_source_info = {
    .name          = TYPE_XIVE_SOURCE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XiveSource),
    .class_init    = xive_source_class_init,
};

/*
 * XiveEND helpers
 */

void xive_end_queue_pic_print_info(XiveEND *end, uint32_t width, Monitor *mon)
{
    uint64_t qaddr_base = xive_end_qaddr(end);
    uint32_t qsize = xive_get_field32(END_W0_QSIZE, end->w0);
    uint32_t qindex = xive_get_field32(END_W1_PAGE_OFF, end->w1);
    uint32_t qentries = 1 << (qsize + 10);
    int i;

    /*
     * print out the [ (qindex - (width - 1)) .. (qindex + 1)] window
     */
    monitor_printf(mon, " [ ");
    qindex = (qindex - (width - 1)) & (qentries - 1);
    for (i = 0; i < width; i++) {
        uint64_t qaddr = qaddr_base + (qindex << 2);
        uint32_t qdata = -1;

        if (dma_memory_read(&address_space_memory, qaddr,
                            &qdata, sizeof(qdata), MEMTXATTRS_UNSPECIFIED)) {
            qemu_log_mask(LOG_GUEST_ERROR, "XIVE: failed to read EQ @0x%"
                          HWADDR_PRIx "\n", qaddr);
            return;
        }
        monitor_printf(mon, "%s%08x ", i == width - 1 ? "^" : "",
                       be32_to_cpu(qdata));
        qindex = (qindex + 1) & (qentries - 1);
    }
    monitor_printf(mon, "]");
}

void xive_end_pic_print_info(XiveEND *end, uint32_t end_idx, Monitor *mon)
{
    uint64_t qaddr_base = xive_end_qaddr(end);
    uint32_t qindex = xive_get_field32(END_W1_PAGE_OFF, end->w1);
    uint32_t qgen = xive_get_field32(END_W1_GENERATION, end->w1);
    uint32_t qsize = xive_get_field32(END_W0_QSIZE, end->w0);
    uint32_t qentries = 1 << (qsize + 10);

    uint32_t nvt_blk = xive_get_field32(END_W6_NVT_BLOCK, end->w6);
    uint32_t nvt_idx = xive_get_field32(END_W6_NVT_INDEX, end->w6);
    uint8_t priority = xive_get_field32(END_W7_F0_PRIORITY, end->w7);
    uint8_t pq;

    if (!xive_end_is_valid(end)) {
        return;
    }

    pq = xive_get_field32(END_W1_ESn, end->w1);

    monitor_printf(mon, "  %08x %c%c %c%c%c%c%c%c%c%c prio:%d nvt:%02x/%04x",
                   end_idx,
                   pq & XIVE_ESB_VAL_P ? 'P' : '-',
                   pq & XIVE_ESB_VAL_Q ? 'Q' : '-',
                   xive_end_is_valid(end)    ? 'v' : '-',
                   xive_end_is_enqueue(end)  ? 'q' : '-',
                   xive_end_is_notify(end)   ? 'n' : '-',
                   xive_end_is_backlog(end)  ? 'b' : '-',
                   xive_end_is_escalate(end) ? 'e' : '-',
                   xive_end_is_uncond_escalation(end)   ? 'u' : '-',
                   xive_end_is_silent_escalation(end)   ? 's' : '-',
                   xive_end_is_firmware(end)   ? 'f' : '-',
                   priority, nvt_blk, nvt_idx);

    if (qaddr_base) {
        monitor_printf(mon, " eq:@%08"PRIx64"% 6d/%5d ^%d",
                       qaddr_base, qindex, qentries, qgen);
        xive_end_queue_pic_print_info(end, 6, mon);
    }
    monitor_printf(mon, "\n");
}

static void xive_end_enqueue(XiveEND *end, uint32_t data)
{
    uint64_t qaddr_base = xive_end_qaddr(end);
    uint32_t qsize = xive_get_field32(END_W0_QSIZE, end->w0);
    uint32_t qindex = xive_get_field32(END_W1_PAGE_OFF, end->w1);
    uint32_t qgen = xive_get_field32(END_W1_GENERATION, end->w1);

    uint64_t qaddr = qaddr_base + (qindex << 2);
    uint32_t qdata = cpu_to_be32((qgen << 31) | (data & 0x7fffffff));
    uint32_t qentries = 1 << (qsize + 10);

    if (dma_memory_write(&address_space_memory, qaddr,
                         &qdata, sizeof(qdata), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: failed to write END data @0x%"
                      HWADDR_PRIx "\n", qaddr);
        return;
    }

    qindex = (qindex + 1) & (qentries - 1);
    if (qindex == 0) {
        qgen ^= 1;
        end->w1 = xive_set_field32(END_W1_GENERATION, end->w1, qgen);
    }
    end->w1 = xive_set_field32(END_W1_PAGE_OFF, end->w1, qindex);
}

void xive_end_eas_pic_print_info(XiveEND *end, uint32_t end_idx,
                                   Monitor *mon)
{
    XiveEAS *eas = (XiveEAS *) &end->w4;
    uint8_t pq;

    if (!xive_end_is_escalate(end)) {
        return;
    }

    pq = xive_get_field32(END_W1_ESe, end->w1);

    monitor_printf(mon, "  %08x %c%c %c%c end:%02x/%04x data:%08x\n",
                   end_idx,
                   pq & XIVE_ESB_VAL_P ? 'P' : '-',
                   pq & XIVE_ESB_VAL_Q ? 'Q' : '-',
                   xive_eas_is_valid(eas) ? 'V' : ' ',
                   xive_eas_is_masked(eas) ? 'M' : ' ',
                   (uint8_t)  xive_get_field64(EAS_END_BLOCK, eas->w),
                   (uint32_t) xive_get_field64(EAS_END_INDEX, eas->w),
                   (uint32_t) xive_get_field64(EAS_END_DATA, eas->w));
}

/*
 * XIVE Router (aka. Virtualization Controller or IVRE)
 */

int xive_router_get_eas(XiveRouter *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                        XiveEAS *eas)
{
    XiveRouterClass *xrc = XIVE_ROUTER_GET_CLASS(xrtr);

    return xrc->get_eas(xrtr, eas_blk, eas_idx, eas);
}

static
int xive_router_get_pq(XiveRouter *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                       uint8_t *pq)
{
    XiveRouterClass *xrc = XIVE_ROUTER_GET_CLASS(xrtr);

    return xrc->get_pq(xrtr, eas_blk, eas_idx, pq);
}

static
int xive_router_set_pq(XiveRouter *xrtr, uint8_t eas_blk, uint32_t eas_idx,
                       uint8_t *pq)
{
    XiveRouterClass *xrc = XIVE_ROUTER_GET_CLASS(xrtr);

    return xrc->set_pq(xrtr, eas_blk, eas_idx, pq);
}

int xive_router_get_end(XiveRouter *xrtr, uint8_t end_blk, uint32_t end_idx,
                        XiveEND *end)
{
   XiveRouterClass *xrc = XIVE_ROUTER_GET_CLASS(xrtr);

   return xrc->get_end(xrtr, end_blk, end_idx, end);
}

int xive_router_write_end(XiveRouter *xrtr, uint8_t end_blk, uint32_t end_idx,
                          XiveEND *end, uint8_t word_number)
{
   XiveRouterClass *xrc = XIVE_ROUTER_GET_CLASS(xrtr);

   return xrc->write_end(xrtr, end_blk, end_idx, end, word_number);
}

int xive_router_get_nvt(XiveRouter *xrtr, uint8_t nvt_blk, uint32_t nvt_idx,
                        XiveNVT *nvt)
{
   XiveRouterClass *xrc = XIVE_ROUTER_GET_CLASS(xrtr);

   return xrc->get_nvt(xrtr, nvt_blk, nvt_idx, nvt);
}

int xive_router_write_nvt(XiveRouter *xrtr, uint8_t nvt_blk, uint32_t nvt_idx,
                        XiveNVT *nvt, uint8_t word_number)
{
   XiveRouterClass *xrc = XIVE_ROUTER_GET_CLASS(xrtr);

   return xrc->write_nvt(xrtr, nvt_blk, nvt_idx, nvt, word_number);
}

static int xive_router_get_block_id(XiveRouter *xrtr)
{
   XiveRouterClass *xrc = XIVE_ROUTER_GET_CLASS(xrtr);

   return xrc->get_block_id(xrtr);
}

static void xive_router_realize(DeviceState *dev, Error **errp)
{
    XiveRouter *xrtr = XIVE_ROUTER(dev);

    assert(xrtr->xfb);
}

static void xive_router_end_notify_handler(XiveRouter *xrtr, XiveEAS *eas)
{
    XiveRouterClass *xrc = XIVE_ROUTER_GET_CLASS(xrtr);

    return xrc->end_notify(xrtr, eas);
}

/*
 * Encode the HW CAM line in the block group mode format :
 *
 *   chip << 19 | 0000000 0 0001 thread (7Bit)
 */
static uint32_t xive_tctx_hw_cam_line(XivePresenter *xptr, XiveTCTX *tctx)
{
    CPUPPCState *env = &POWERPC_CPU(tctx->cs)->env;
    uint32_t pir = env->spr_cb[SPR_PIR].default_value;
    uint8_t blk = xive_router_get_block_id(XIVE_ROUTER(xptr));

    return xive_nvt_cam_line(blk, 1 << 7 | (pir & 0x7f));
}

/*
 * The thread context register words are in big-endian format.
 */
int xive_presenter_tctx_match(XivePresenter *xptr, XiveTCTX *tctx,
                              uint8_t format,
                              uint8_t nvt_blk, uint32_t nvt_idx,
                              bool cam_ignore, uint32_t logic_serv)
{
    uint32_t cam = xive_nvt_cam_line(nvt_blk, nvt_idx);
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
        if ((be32_to_cpu(qw3w2) & TM_QW3W2_VT) &&
            cam == xive_tctx_hw_cam_line(xptr, tctx)) {
            return TM_QW3_HV_PHYS;
        }

        /* HV POOL ring */
        if ((be32_to_cpu(qw2w2) & TM_QW2W2_VP) &&
            cam == xive_get_field32(TM_QW2W2_POOL_CAM, qw2w2)) {
            return TM_QW2_HV_POOL;
        }

        /* OS ring */
        if ((be32_to_cpu(qw1w2) & TM_QW1W2_VO) &&
            cam == xive_get_field32(TM_QW1W2_OS_CAM, qw1w2)) {
            return TM_QW1_OS;
        }
    } else {
        /* F=1 : User level Event-Based Branch (EBB) notification */

        /* USER ring */
        if  ((be32_to_cpu(qw1w2) & TM_QW1W2_VO) &&
             (cam == xive_get_field32(TM_QW1W2_OS_CAM, qw1w2)) &&
             (be32_to_cpu(qw0w2) & TM_QW0W2_VU) &&
             (logic_serv == xive_get_field32(TM_QW0W2_LOGIC_SERV, qw0w2))) {
            return TM_QW0_USER;
        }
    }
    return -1;
}

/*
 * This is our simple Xive Presenter Engine model. It is merged in the
 * Router as it does not require an extra object.
 *
 * It receives notification requests sent by the IVRE to find one
 * matching NVT (or more) dispatched on the processor threads. In case
 * of a single NVT notification, the process is abbreviated and the
 * thread is signaled if a match is found. In case of a logical server
 * notification (bits ignored at the end of the NVT identifier), the
 * IVPE and IVRE select a winning thread using different filters. This
 * involves 2 or 3 exchanges on the PowerBus that the model does not
 * support.
 *
 * The parameters represent what is sent on the PowerBus
 */
bool xive_presenter_notify(XiveFabric *xfb, uint8_t format,
                           uint8_t nvt_blk, uint32_t nvt_idx,
                           bool cam_ignore, uint8_t priority,
                           uint32_t logic_serv)
{
    XiveFabricClass *xfc = XIVE_FABRIC_GET_CLASS(xfb);
    XiveTCTXMatch match = { .tctx = NULL, .ring = 0 };
    int count;

    /*
     * Ask the machine to scan the interrupt controllers for a match
     */
    count = xfc->match_nvt(xfb, format, nvt_blk, nvt_idx, cam_ignore,
                           priority, logic_serv, &match);
    if (count < 0) {
        return false;
    }

    /* handle CPU exception delivery */
    if (count) {
        trace_xive_presenter_notify(nvt_blk, nvt_idx, match.ring);
        xive_tctx_ipb_update(match.tctx, match.ring,
                             xive_priority_to_ipb(priority));
    }

    return !!count;
}

/*
 * Notification using the END ESe/ESn bit (Event State Buffer for
 * escalation and notification). Provide further coalescing in the
 * Router.
 */
static bool xive_router_end_es_notify(XiveRouter *xrtr, uint8_t end_blk,
                                      uint32_t end_idx, XiveEND *end,
                                      uint32_t end_esmask)
{
    uint8_t pq = xive_get_field32(end_esmask, end->w1);
    bool notify = xive_esb_trigger(&pq);

    if (pq != xive_get_field32(end_esmask, end->w1)) {
        end->w1 = xive_set_field32(end_esmask, end->w1, pq);
        xive_router_write_end(xrtr, end_blk, end_idx, end, 1);
    }

    /* ESe/n[Q]=1 : end of notification */
    return notify;
}

/*
 * An END trigger can come from an event trigger (IPI or HW) or from
 * another chip. We don't model the PowerBus but the END trigger
 * message has the same parameters than in the function below.
 */
void xive_router_end_notify(XiveRouter *xrtr, XiveEAS *eas)
{
    XiveEND end;
    uint8_t priority;
    uint8_t format;
    uint8_t nvt_blk;
    uint32_t nvt_idx;
    XiveNVT nvt;
    bool found;

    uint8_t end_blk = xive_get_field64(EAS_END_BLOCK, eas->w);
    uint32_t end_idx = xive_get_field64(EAS_END_INDEX, eas->w);
    uint32_t end_data = xive_get_field64(EAS_END_DATA,  eas->w);

    /* END cache lookup */
    if (xive_router_get_end(xrtr, end_blk, end_idx, &end)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No END %x/%x\n", end_blk,
                      end_idx);
        return;
    }

    if (!xive_end_is_valid(&end)) {
        trace_xive_router_end_notify(end_blk, end_idx, end_data);
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: END %x/%x is invalid\n",
                      end_blk, end_idx);
        return;
    }

    if (xive_end_is_enqueue(&end)) {
        xive_end_enqueue(&end, end_data);
        /* Enqueuing event data modifies the EQ toggle and index */
        xive_router_write_end(xrtr, end_blk, end_idx, &end, 1);
    }

    /*
     * When the END is silent, we skip the notification part.
     */
    if (xive_end_is_silent_escalation(&end)) {
        goto do_escalation;
    }

    /*
     * The W7 format depends on the F bit in W6. It defines the type
     * of the notification :
     *
     *   F=0 : single or multiple NVT notification
     *   F=1 : User level Event-Based Branch (EBB) notification, no
     *         priority
     */
    format = xive_get_field32(END_W6_FORMAT_BIT, end.w6);
    priority = xive_get_field32(END_W7_F0_PRIORITY, end.w7);

    /* The END is masked */
    if (format == 0 && priority == 0xff) {
        return;
    }

    /*
     * Check the END ESn (Event State Buffer for notification) for
     * even further coalescing in the Router
     */
    if (!xive_end_is_notify(&end)) {
        /* ESn[Q]=1 : end of notification */
        if (!xive_router_end_es_notify(xrtr, end_blk, end_idx,
                                       &end, END_W1_ESn)) {
            return;
        }
    }

    /*
     * Follows IVPE notification
     */
    nvt_blk = xive_get_field32(END_W6_NVT_BLOCK, end.w6);
    nvt_idx = xive_get_field32(END_W6_NVT_INDEX, end.w6);

    /* NVT cache lookup */
    if (xive_router_get_nvt(xrtr, nvt_blk, nvt_idx, &nvt)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: no NVT %x/%x\n",
                      nvt_blk, nvt_idx);
        return;
    }

    if (!xive_nvt_is_valid(&nvt)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: NVT %x/%x is invalid\n",
                      nvt_blk, nvt_idx);
        return;
    }

    found = xive_presenter_notify(xrtr->xfb, format, nvt_blk, nvt_idx,
                          xive_get_field32(END_W7_F0_IGNORE, end.w7),
                          priority,
                          xive_get_field32(END_W7_F1_LOG_SERVER_ID, end.w7));

    /* TODO: Auto EOI. */

    if (found) {
        return;
    }

    /*
     * If no matching NVT is dispatched on a HW thread :
     * - specific VP: update the NVT structure if backlog is activated
     * - logical server : forward request to IVPE (not supported)
     */
    if (xive_end_is_backlog(&end)) {
        uint8_t ipb;

        if (format == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "XIVE: END %x/%x invalid config: F1 & backlog\n",
                          end_blk, end_idx);
            return;
        }
        /*
         * Record the IPB in the associated NVT structure for later
         * use. The presenter will resend the interrupt when the vCPU
         * is dispatched again on a HW thread.
         */
        ipb = xive_get_field32(NVT_W4_IPB, nvt.w4) |
            xive_priority_to_ipb(priority);
        nvt.w4 = xive_set_field32(NVT_W4_IPB, nvt.w4, ipb);
        xive_router_write_nvt(xrtr, nvt_blk, nvt_idx, &nvt, 4);

        /*
         * On HW, follows a "Broadcast Backlog" to IVPEs
         */
    }

do_escalation:
    /*
     * If activated, escalate notification using the ESe PQ bits and
     * the EAS in w4-5
     */
    if (!xive_end_is_escalate(&end)) {
        return;
    }

    /*
     * Check the END ESe (Event State Buffer for escalation) for even
     * further coalescing in the Router
     */
    if (!xive_end_is_uncond_escalation(&end)) {
        /* ESe[Q]=1 : end of notification */
        if (!xive_router_end_es_notify(xrtr, end_blk, end_idx,
                                       &end, END_W1_ESe)) {
            return;
        }
    }

    trace_xive_router_end_escalate(end_blk, end_idx,
           (uint8_t) xive_get_field32(END_W4_ESC_END_BLOCK, end.w4),
           (uint32_t) xive_get_field32(END_W4_ESC_END_INDEX, end.w4),
           (uint32_t) xive_get_field32(END_W5_ESC_END_DATA,  end.w5));
    /*
     * The END trigger becomes an Escalation trigger
     */
    xive_router_end_notify_handler(xrtr, (XiveEAS *) &end.w4);
}

void xive_router_notify(XiveNotifier *xn, uint32_t lisn, bool pq_checked)
{
    XiveRouter *xrtr = XIVE_ROUTER(xn);
    uint8_t eas_blk = XIVE_EAS_BLOCK(lisn);
    uint32_t eas_idx = XIVE_EAS_INDEX(lisn);
    XiveEAS eas;

    /* EAS cache lookup */
    if (xive_router_get_eas(xrtr, eas_blk, eas_idx, &eas)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: Unknown LISN %x\n", lisn);
        return;
    }

    if (!pq_checked) {
        bool notify;
        uint8_t pq;

        /* PQ cache lookup */
        if (xive_router_get_pq(xrtr, eas_blk, eas_idx, &pq)) {
            /* Set FIR */
            g_assert_not_reached();
        }

        notify = xive_esb_trigger(&pq);

        if (xive_router_set_pq(xrtr, eas_blk, eas_idx, &pq)) {
            /* Set FIR */
            g_assert_not_reached();
        }

        if (!notify) {
            return;
        }
    }

    if (!xive_eas_is_valid(&eas)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %x\n", lisn);
        return;
    }

    if (xive_eas_is_masked(&eas)) {
        /* Notification completed */
        return;
    }

    /*
     * The event trigger becomes an END trigger
     */
    xive_router_end_notify_handler(xrtr, &eas);
}

static Property xive_router_properties[] = {
    DEFINE_PROP_LINK("xive-fabric", XiveRouter, xfb,
                     TYPE_XIVE_FABRIC, XiveFabric *),
    DEFINE_PROP_END_OF_LIST(),
};

static void xive_router_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveNotifierClass *xnc = XIVE_NOTIFIER_CLASS(klass);
    XiveRouterClass *xrc = XIVE_ROUTER_CLASS(klass);

    dc->desc    = "XIVE Router Engine";
    device_class_set_props(dc, xive_router_properties);
    /* Parent is SysBusDeviceClass. No need to call its realize hook */
    dc->realize = xive_router_realize;
    xnc->notify = xive_router_notify;

    /* By default, the router handles END triggers locally */
    xrc->end_notify = xive_router_end_notify;
}

static const TypeInfo xive_router_info = {
    .name          = TYPE_XIVE_ROUTER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .abstract      = true,
    .instance_size = sizeof(XiveRouter),
    .class_size    = sizeof(XiveRouterClass),
    .class_init    = xive_router_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_XIVE_NOTIFIER },
        { TYPE_XIVE_PRESENTER },
        { }
    }
};

void xive_eas_pic_print_info(XiveEAS *eas, uint32_t lisn, Monitor *mon)
{
    if (!xive_eas_is_valid(eas)) {
        return;
    }

    monitor_printf(mon, "  %08x %s end:%02x/%04x data:%08x\n",
                   lisn, xive_eas_is_masked(eas) ? "M" : " ",
                   (uint8_t)  xive_get_field64(EAS_END_BLOCK, eas->w),
                   (uint32_t) xive_get_field64(EAS_END_INDEX, eas->w),
                   (uint32_t) xive_get_field64(EAS_END_DATA, eas->w));
}

/*
 * END ESB MMIO loads
 */
static uint64_t xive_end_source_read(void *opaque, hwaddr addr, unsigned size)
{
    XiveENDSource *xsrc = XIVE_END_SOURCE(opaque);
    uint32_t offset = addr & 0xFFF;
    uint8_t end_blk;
    uint32_t end_idx;
    XiveEND end;
    uint32_t end_esmask;
    uint8_t pq;
    uint64_t ret = -1;

    /*
     * The block id should be deduced from the load address on the END
     * ESB MMIO but our model only supports a single block per XIVE chip.
     */
    end_blk = xive_router_get_block_id(xsrc->xrtr);
    end_idx = addr >> (xsrc->esb_shift + 1);

    trace_xive_end_source_read(end_blk, end_idx, addr);

    if (xive_router_get_end(xsrc->xrtr, end_blk, end_idx, &end)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: No END %x/%x\n", end_blk,
                      end_idx);
        return -1;
    }

    if (!xive_end_is_valid(&end)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: END %x/%x is invalid\n",
                      end_blk, end_idx);
        return -1;
    }

    end_esmask = addr_is_even(addr, xsrc->esb_shift) ? END_W1_ESn : END_W1_ESe;
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
        xive_router_write_end(xsrc->xrtr, end_blk, end_idx, &end, 1);
    }

    return ret;
}

/*
 * END ESB MMIO stores are invalid
 */
static void xive_end_source_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid ESB write addr 0x%"
                  HWADDR_PRIx"\n", addr);
}

static const MemoryRegionOps xive_end_source_ops = {
    .read = xive_end_source_read,
    .write = xive_end_source_write,
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

static void xive_end_source_realize(DeviceState *dev, Error **errp)
{
    XiveENDSource *xsrc = XIVE_END_SOURCE(dev);

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
                          &xive_end_source_ops, xsrc, "xive.end",
                          (1ull << (xsrc->esb_shift + 1)) * xsrc->nr_ends);
}

static Property xive_end_source_properties[] = {
    DEFINE_PROP_UINT32("nr-ends", XiveENDSource, nr_ends, 0),
    DEFINE_PROP_UINT32("shift", XiveENDSource, esb_shift, XIVE_ESB_64K),
    DEFINE_PROP_LINK("xive", XiveENDSource, xrtr, TYPE_XIVE_ROUTER,
                     XiveRouter *),
    DEFINE_PROP_END_OF_LIST(),
};

static void xive_end_source_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc    = "XIVE END Source";
    device_class_set_props(dc, xive_end_source_properties);
    dc->realize = xive_end_source_realize;
    /*
     * Reason: part of XIVE interrupt controller, needs to be wired up,
     * e.g. by spapr_xive_instance_init().
     */
    dc->user_creatable = false;
}

static const TypeInfo xive_end_source_info = {
    .name          = TYPE_XIVE_END_SOURCE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XiveENDSource),
    .class_init    = xive_end_source_class_init,
};

/*
 * XIVE Notifier
 */
static const TypeInfo xive_notifier_info = {
    .name = TYPE_XIVE_NOTIFIER,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(XiveNotifierClass),
};

/*
 * XIVE Presenter
 */
static const TypeInfo xive_presenter_info = {
    .name = TYPE_XIVE_PRESENTER,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(XivePresenterClass),
};

/*
 * XIVE Fabric
 */
static const TypeInfo xive_fabric_info = {
    .name = TYPE_XIVE_FABRIC,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(XiveFabricClass),
};

static void xive_register_types(void)
{
    type_register_static(&xive_fabric_info);
    type_register_static(&xive_source_info);
    type_register_static(&xive_notifier_info);
    type_register_static(&xive_presenter_info);
    type_register_static(&xive_router_info);
    type_register_static(&xive_end_source_info);
    type_register_static(&xive_tctx_info);
}

type_init(xive_register_types)
