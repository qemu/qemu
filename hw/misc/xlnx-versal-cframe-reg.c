/*
 * QEMU model of the Configuration Frame Control module
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * Written by Francisco Iglesias <francisco.iglesias@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/registerfields.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/misc/xlnx-versal-cframe-reg.h"

#ifndef XLNX_VERSAL_CFRAME_REG_ERR_DEBUG
#define XLNX_VERSAL_CFRAME_REG_ERR_DEBUG 0
#endif

#define KEYHOLE_STREAM_4K (4 * KiB)
#define N_WORDS_128BIT 4

#define MAX_BLOCKTYPE 6
#define MAX_BLOCKTYPE_FRAMES 0xFFFFF

enum {
    CFRAME_CMD_WCFG = 1,
    CFRAME_CMD_ROWON = 2,
    CFRAME_CMD_ROWOFF = 3,
    CFRAME_CMD_RCFG = 4,
    CFRAME_CMD_DLPARK = 5,
};

static gint int_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
    guint ua = GPOINTER_TO_UINT(a);
    guint ub = GPOINTER_TO_UINT(b);
    return (ua > ub) - (ua < ub);
}

static void cfrm_imr_update_irq(XlnxVersalCFrameReg *s)
{
    bool pending = s->regs[R_CFRM_ISR0] & ~s->regs[R_CFRM_IMR0];
    qemu_set_irq(s->irq_cfrm_imr, pending);
}

static void cfrm_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);
    cfrm_imr_update_irq(s);
}

static uint64_t cfrm_ier_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);

    s->regs[R_CFRM_IMR0] &= ~s->regs[R_CFRM_IER0];
    s->regs[R_CFRM_IER0] = 0;
    cfrm_imr_update_irq(s);
    return 0;
}

static uint64_t cfrm_idr_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);

    s->regs[R_CFRM_IMR0] |= s->regs[R_CFRM_IDR0];
    s->regs[R_CFRM_IDR0] = 0;
    cfrm_imr_update_irq(s);
    return 0;
}

static uint64_t cfrm_itr_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);

    s->regs[R_CFRM_ISR0] |= s->regs[R_CFRM_ITR0];
    s->regs[R_CFRM_ITR0] = 0;
    cfrm_imr_update_irq(s);
    return 0;
}

static void cframe_incr_far(XlnxVersalCFrameReg *s)
{
    uint32_t faddr = ARRAY_FIELD_EX32(s->regs, FAR0, FRAME_ADDR);
    uint32_t blktype = ARRAY_FIELD_EX32(s->regs, FAR0, BLOCKTYPE);

    assert(blktype <= MAX_BLOCKTYPE);

    faddr++;
    if (faddr > s->cfg.blktype_num_frames[blktype]) {
        /* Restart from 0 and increment block type */
        faddr = 0;
        blktype++;

        assert(blktype <= MAX_BLOCKTYPE);

        ARRAY_FIELD_DP32(s->regs, FAR0, BLOCKTYPE, blktype);
    }

    ARRAY_FIELD_DP32(s->regs, FAR0, FRAME_ADDR, faddr);
}

static void cfrm_fdri_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);

    if (s->row_configured && s->rowon && s->wcfg) {

        if (fifo32_num_free(&s->new_f_data) >= N_WORDS_128BIT) {
            fifo32_push(&s->new_f_data, s->regs[R_FDRI0]);
            fifo32_push(&s->new_f_data, s->regs[R_FDRI1]);
            fifo32_push(&s->new_f_data, s->regs[R_FDRI2]);
            fifo32_push(&s->new_f_data, s->regs[R_FDRI3]);
        }

        if (fifo32_is_full(&s->new_f_data)) {
            uint32_t addr = extract32(s->regs[R_FAR0], 0, 23);
            XlnxCFrame *f = g_new(XlnxCFrame, 1);

            for (int i = 0; i < FRAME_NUM_WORDS; i++) {
                f->data[i] = fifo32_pop(&s->new_f_data);
            }

            g_tree_replace(s->cframes, GUINT_TO_POINTER(addr), f);

            cframe_incr_far(s);

            fifo32_reset(&s->new_f_data);
        }
    }
}

static void cfrm_readout_frames(XlnxVersalCFrameReg *s, uint32_t start_addr,
                                uint32_t end_addr)
{
    /*
     * NB: when our minimum glib version is at least 2.68 we can improve the
     * performance of the cframe traversal by using g_tree_lookup_node and
     * g_tree_node_next (instead of calling g_tree_lookup for finding each
     * cframe).
     */
    for (uint32_t addr = start_addr; addr < end_addr; addr++) {
        XlnxCFrame *f = g_tree_lookup(s->cframes, GUINT_TO_POINTER(addr));

        /* Transmit the data if a frame was found */
        if (f) {
            for (int i = 0; i < FRAME_NUM_WORDS; i += 4) {
                XlnxCfiPacket pkt = {};

                pkt.data[0] = f->data[i];
                pkt.data[1] = f->data[i + 1];
                pkt.data[2] = f->data[i + 2];
                pkt.data[3] = f->data[i + 3];

                if (s->cfg.cfu_fdro) {
                    xlnx_cfi_transfer_packet(s->cfg.cfu_fdro, &pkt);
                }
            }
        }
    }
}

static void cfrm_frcnt_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);

    if (s->row_configured && s->rowon && s->rcfg) {
        uint32_t start_addr = extract32(s->regs[R_FAR0], 0, 23);
        uint32_t end_addr = start_addr + s->regs[R_FRCNT0] / FRAME_NUM_QWORDS;

        cfrm_readout_frames(s, start_addr, end_addr);
    }
}

static void cfrm_cmd_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);

    if (s->row_configured) {
        uint8_t cmd = ARRAY_FIELD_EX32(s->regs, CMD0, CMD);

        switch (cmd) {
        case CFRAME_CMD_WCFG:
            s->wcfg = true;
            break;
        case CFRAME_CMD_ROWON:
            s->rowon = true;
            break;
        case CFRAME_CMD_ROWOFF:
            s->rowon = false;
            break;
        case CFRAME_CMD_RCFG:
            s->rcfg = true;
            break;
        case CFRAME_CMD_DLPARK:
            s->wcfg = false;
            s->rcfg = false;
            break;
        default:
            break;
        };
    }
}

static uint64_t cfrm_last_frame_bot_post_read(RegisterInfo *reg,
                                              uint64_t val64)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);
    uint64_t val = 0;

    switch (reg->access->addr) {
    case A_LAST_FRAME_BOT0:
        val = FIELD_DP32(val, LAST_FRAME_BOT0, BLOCKTYPE1_LAST_FRAME_LSB,
                         s->cfg.blktype_num_frames[1]);
        val = FIELD_DP32(val, LAST_FRAME_BOT0, BLOCKTYPE0_LAST_FRAME,
                         s->cfg.blktype_num_frames[0]);
        break;
    case A_LAST_FRAME_BOT1:
        val = FIELD_DP32(val, LAST_FRAME_BOT1, BLOCKTYPE3_LAST_FRAME_LSB,
                         s->cfg.blktype_num_frames[3]);
        val = FIELD_DP32(val, LAST_FRAME_BOT1, BLOCKTYPE2_LAST_FRAME,
                         s->cfg.blktype_num_frames[2]);
        val = FIELD_DP32(val, LAST_FRAME_BOT1, BLOCKTYPE1_LAST_FRAME_MSB,
                         (s->cfg.blktype_num_frames[1] >> 12));
        break;
    case A_LAST_FRAME_BOT2:
        val = FIELD_DP32(val, LAST_FRAME_BOT2, BLOCKTYPE3_LAST_FRAME_MSB,
                         (s->cfg.blktype_num_frames[3] >> 4));
        break;
    case A_LAST_FRAME_BOT3:
    default:
        break;
    }

    return val;
}

static uint64_t cfrm_last_frame_top_post_read(RegisterInfo *reg,
                                              uint64_t val64)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);
    uint64_t val = 0;

    switch (reg->access->addr) {
    case A_LAST_FRAME_TOP0:
        val = FIELD_DP32(val, LAST_FRAME_TOP0, BLOCKTYPE5_LAST_FRAME_LSB,
                         s->cfg.blktype_num_frames[5]);
        val = FIELD_DP32(val, LAST_FRAME_TOP0, BLOCKTYPE4_LAST_FRAME,
                         s->cfg.blktype_num_frames[4]);
        break;
    case A_LAST_FRAME_TOP1:
        val = FIELD_DP32(val, LAST_FRAME_TOP1, BLOCKTYPE6_LAST_FRAME,
                         s->cfg.blktype_num_frames[6]);
        val = FIELD_DP32(val, LAST_FRAME_TOP1, BLOCKTYPE5_LAST_FRAME_MSB,
                         (s->cfg.blktype_num_frames[5] >> 12));
        break;
    case A_LAST_FRAME_TOP2:
    case A_LAST_FRAME_BOT3:
    default:
        break;
    }

    return val;
}

static void cfrm_far_sfr_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(reg->opaque);

    if (s->row_configured && s->rowon && s->rcfg) {
        uint32_t start_addr = extract32(s->regs[R_FAR_SFR0], 0, 23);

        /* Readback 1 frame */
        cfrm_readout_frames(s, start_addr, start_addr + 1);
    }
}

static const RegisterAccessInfo cframe_reg_regs_info[] = {
    {   .name = "CRC0",  .addr = A_CRC0,
        .rsvd = 0x00000000,
    },{ .name = "CRC1",  .addr = A_CRC0,
        .rsvd = 0xffffffff,
    },{ .name = "CRC2",  .addr = A_CRC0,
        .rsvd = 0xffffffff,
    },{ .name = "CRC3",  .addr = A_CRC0,
        .rsvd = 0xffffffff,
    },{ .name = "FAR0",  .addr = A_FAR0,
        .rsvd = 0xfe000000,
    },{ .name = "FAR1",  .addr = A_FAR1,
        .rsvd = 0xffffffff,
    },{ .name = "FAR2",  .addr = A_FAR2,
        .rsvd = 0xffffffff,
    },{ .name = "FAR3",  .addr = A_FAR3,
        .rsvd = 0xffffffff,
    },{ .name = "FAR_SFR0",  .addr = A_FAR_SFR0,
        .rsvd = 0xff800000,
    },{ .name = "FAR_SFR1",  .addr = A_FAR_SFR1,
        .rsvd = 0xffffffff,
    },{ .name = "FAR_SFR2",  .addr = A_FAR_SFR2,
        .rsvd = 0xffffffff,
    },{ .name = "FAR_SFR3",  .addr = A_FAR_SFR3,
        .rsvd = 0xffffffff,
        .post_write = cfrm_far_sfr_post_write,
    },{ .name = "FDRI0",  .addr = A_FDRI0,
    },{ .name = "FDRI1",  .addr = A_FDRI1,
    },{ .name = "FDRI2",  .addr = A_FDRI2,
    },{ .name = "FDRI3",  .addr = A_FDRI3,
        .post_write = cfrm_fdri_post_write,
    },{ .name = "FRCNT0",  .addr = A_FRCNT0,
        .rsvd = 0x00000000,
    },{ .name = "FRCNT1",  .addr = A_FRCNT1,
        .rsvd = 0xffffffff,
    },{ .name = "FRCNT2",  .addr = A_FRCNT2,
        .rsvd = 0xffffffff,
    },{ .name = "FRCNT3",  .addr = A_FRCNT3,
        .rsvd = 0xffffffff,
        .post_write = cfrm_frcnt_post_write
    },{ .name = "CMD0",  .addr = A_CMD0,
        .rsvd = 0xffffffe0,
    },{ .name = "CMD1",  .addr = A_CMD1,
        .rsvd = 0xffffffff,
    },{ .name = "CMD2",  .addr = A_CMD2,
        .rsvd = 0xffffffff,
    },{ .name = "CMD3",  .addr = A_CMD3,
        .rsvd = 0xffffffff,
        .post_write = cfrm_cmd_post_write
    },{ .name = "CR_MASK0",  .addr = A_CR_MASK0,
        .rsvd = 0x00000000,
    },{ .name = "CR_MASK1",  .addr = A_CR_MASK1,
        .rsvd = 0x00000000,
    },{ .name = "CR_MASK2",  .addr = A_CR_MASK2,
        .rsvd = 0x00000000,
    },{ .name = "CR_MASK3",  .addr = A_CR_MASK3,
        .rsvd = 0xffffffff,
    },{ .name = "CTL0",  .addr = A_CTL0,
        .rsvd = 0xfffffff8,
    },{ .name = "CTL1",  .addr = A_CTL1,
        .rsvd = 0xffffffff,
    },{ .name = "CTL2",  .addr = A_CTL2,
        .rsvd = 0xffffffff,
    },{ .name = "CTL3",  .addr = A_CTL3,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_ISR0",  .addr = A_CFRM_ISR0,
        .rsvd = 0xffc04000,
        .w1c = 0x3bfff,
    },{ .name = "CFRM_ISR1",  .addr = A_CFRM_ISR1,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_ISR2",  .addr = A_CFRM_ISR2,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_ISR3",  .addr = A_CFRM_ISR3,
        .rsvd = 0xffffffff,
        .post_write = cfrm_isr_postw,
    },{ .name = "CFRM_IMR0",  .addr = A_CFRM_IMR0,
        .rsvd = 0xffc04000,
        .ro = 0xfffff,
        .reset = 0x3bfff,
    },{ .name = "CFRM_IMR1",  .addr = A_CFRM_IMR1,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_IMR2",  .addr = A_CFRM_IMR2,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_IMR3",  .addr = A_CFRM_IMR3,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_IER0",  .addr = A_CFRM_IER0,
        .rsvd = 0xffc04000,
    },{ .name = "CFRM_IER1",  .addr = A_CFRM_IER1,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_IER2",  .addr = A_CFRM_IER2,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_IER3",  .addr = A_CFRM_IER3,
        .rsvd = 0xffffffff,
        .pre_write = cfrm_ier_prew,
    },{ .name = "CFRM_IDR0",  .addr = A_CFRM_IDR0,
        .rsvd = 0xffc04000,
    },{ .name = "CFRM_IDR1",  .addr = A_CFRM_IDR1,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_IDR2",  .addr = A_CFRM_IDR2,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_IDR3",  .addr = A_CFRM_IDR3,
        .rsvd = 0xffffffff,
        .pre_write = cfrm_idr_prew,
    },{ .name = "CFRM_ITR0",  .addr = A_CFRM_ITR0,
        .rsvd = 0xffc04000,
    },{ .name = "CFRM_ITR1",  .addr = A_CFRM_ITR1,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_ITR2",  .addr = A_CFRM_ITR2,
        .rsvd = 0xffffffff,
    },{ .name = "CFRM_ITR3",  .addr = A_CFRM_ITR3,
        .rsvd = 0xffffffff,
        .pre_write = cfrm_itr_prew,
    },{ .name = "SEU_SYNDRM00",  .addr = A_SEU_SYNDRM00,
    },{ .name = "SEU_SYNDRM01",  .addr = A_SEU_SYNDRM01,
    },{ .name = "SEU_SYNDRM02",  .addr = A_SEU_SYNDRM02,
    },{ .name = "SEU_SYNDRM03",  .addr = A_SEU_SYNDRM03,
    },{ .name = "SEU_SYNDRM10",  .addr = A_SEU_SYNDRM10,
    },{ .name = "SEU_SYNDRM11",  .addr = A_SEU_SYNDRM11,
    },{ .name = "SEU_SYNDRM12",  .addr = A_SEU_SYNDRM12,
    },{ .name = "SEU_SYNDRM13",  .addr = A_SEU_SYNDRM13,
    },{ .name = "SEU_SYNDRM20",  .addr = A_SEU_SYNDRM20,
    },{ .name = "SEU_SYNDRM21",  .addr = A_SEU_SYNDRM21,
    },{ .name = "SEU_SYNDRM22",  .addr = A_SEU_SYNDRM22,
    },{ .name = "SEU_SYNDRM23",  .addr = A_SEU_SYNDRM23,
    },{ .name = "SEU_SYNDRM30",  .addr = A_SEU_SYNDRM30,
    },{ .name = "SEU_SYNDRM31",  .addr = A_SEU_SYNDRM31,
    },{ .name = "SEU_SYNDRM32",  .addr = A_SEU_SYNDRM32,
    },{ .name = "SEU_SYNDRM33",  .addr = A_SEU_SYNDRM33,
    },{ .name = "SEU_VIRTUAL_SYNDRM0",  .addr = A_SEU_VIRTUAL_SYNDRM0,
    },{ .name = "SEU_VIRTUAL_SYNDRM1",  .addr = A_SEU_VIRTUAL_SYNDRM1,
    },{ .name = "SEU_VIRTUAL_SYNDRM2",  .addr = A_SEU_VIRTUAL_SYNDRM2,
    },{ .name = "SEU_VIRTUAL_SYNDRM3",  .addr = A_SEU_VIRTUAL_SYNDRM3,
    },{ .name = "SEU_CRC0",  .addr = A_SEU_CRC0,
    },{ .name = "SEU_CRC1",  .addr = A_SEU_CRC1,
    },{ .name = "SEU_CRC2",  .addr = A_SEU_CRC2,
    },{ .name = "SEU_CRC3",  .addr = A_SEU_CRC3,
    },{ .name = "CFRAME_FAR_BOT0",  .addr = A_CFRAME_FAR_BOT0,
    },{ .name = "CFRAME_FAR_BOT1",  .addr = A_CFRAME_FAR_BOT1,
    },{ .name = "CFRAME_FAR_BOT2",  .addr = A_CFRAME_FAR_BOT2,
    },{ .name = "CFRAME_FAR_BOT3",  .addr = A_CFRAME_FAR_BOT3,
    },{ .name = "CFRAME_FAR_TOP0",  .addr = A_CFRAME_FAR_TOP0,
    },{ .name = "CFRAME_FAR_TOP1",  .addr = A_CFRAME_FAR_TOP1,
    },{ .name = "CFRAME_FAR_TOP2",  .addr = A_CFRAME_FAR_TOP2,
    },{ .name = "CFRAME_FAR_TOP3",  .addr = A_CFRAME_FAR_TOP3,
    },{ .name = "LAST_FRAME_BOT0",  .addr = A_LAST_FRAME_BOT0,
        .ro = 0xffffffff,
        .post_read = cfrm_last_frame_bot_post_read,
    },{ .name = "LAST_FRAME_BOT1",  .addr = A_LAST_FRAME_BOT1,
        .ro = 0xffffffff,
        .post_read = cfrm_last_frame_bot_post_read,
    },{ .name = "LAST_FRAME_BOT2",  .addr = A_LAST_FRAME_BOT2,
        .ro = 0xffffffff,
        .post_read = cfrm_last_frame_bot_post_read,
    },{ .name = "LAST_FRAME_BOT3",  .addr = A_LAST_FRAME_BOT3,
        .ro = 0xffffffff,
        .post_read = cfrm_last_frame_bot_post_read,
    },{ .name = "LAST_FRAME_TOP0",  .addr = A_LAST_FRAME_TOP0,
        .ro = 0xffffffff,
        .post_read = cfrm_last_frame_top_post_read,
    },{ .name = "LAST_FRAME_TOP1",  .addr = A_LAST_FRAME_TOP1,
        .ro = 0xffffffff,
        .post_read = cfrm_last_frame_top_post_read,
    },{ .name = "LAST_FRAME_TOP2",  .addr = A_LAST_FRAME_TOP2,
        .ro = 0xffffffff,
        .post_read = cfrm_last_frame_top_post_read,
    },{ .name = "LAST_FRAME_TOP3",  .addr = A_LAST_FRAME_TOP3,
        .ro = 0xffffffff,
        .post_read = cfrm_last_frame_top_post_read,
    }
};

static void cframe_reg_cfi_transfer_packet(XlnxCfiIf *cfi_if,
                                           XlnxCfiPacket *pkt)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(cfi_if);
    uint64_t we = MAKE_64BIT_MASK(0, 4 * 8);

    if (!s->row_configured) {
        return;
    }

    switch (pkt->reg_addr) {
    case CFRAME_FAR:
        s->regs[R_FAR0] = pkt->data[0];
        break;
    case CFRAME_SFR:
        s->regs[R_FAR_SFR0] = pkt->data[0];
        register_write(&s->regs_info[R_FAR_SFR3], 0,
                       we, object_get_typename(OBJECT(s)),
                       XLNX_VERSAL_CFRAME_REG_ERR_DEBUG);
        break;
    case CFRAME_FDRI:
        s->regs[R_FDRI0] = pkt->data[0];
        s->regs[R_FDRI1] = pkt->data[1];
        s->regs[R_FDRI2] = pkt->data[2];
        register_write(&s->regs_info[R_FDRI3], pkt->data[3],
                       we, object_get_typename(OBJECT(s)),
                       XLNX_VERSAL_CFRAME_REG_ERR_DEBUG);
        break;
    case CFRAME_CMD:
        ARRAY_FIELD_DP32(s->regs, CMD0, CMD, pkt->data[0]);

        register_write(&s->regs_info[R_CMD3], 0,
                       we, object_get_typename(OBJECT(s)),
                       XLNX_VERSAL_CFRAME_REG_ERR_DEBUG);
        break;
    default:
        break;
    }
}

static uint64_t cframe_reg_fdri_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported read from addr=%"
                  HWADDR_PRIx "\n", __func__, addr);
    return 0;
}

static void cframe_reg_fdri_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(opaque);
    uint32_t wfifo[WFIFO_SZ];

    if (update_wfifo(addr, value, s->wfifo, wfifo)) {
        uint64_t we = MAKE_64BIT_MASK(0, 4 * 8);

        s->regs[R_FDRI0] = wfifo[0];
        s->regs[R_FDRI1] = wfifo[1];
        s->regs[R_FDRI2] = wfifo[2];
        register_write(&s->regs_info[R_FDRI3], wfifo[3],
                       we, object_get_typename(OBJECT(s)),
                       XLNX_VERSAL_CFRAME_REG_ERR_DEBUG);
    }
}

static void cframe_reg_reset_enter(Object *obj, ResetType type)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
    memset(s->wfifo, 0, WFIFO_SZ * sizeof(uint32_t));
    fifo32_reset(&s->new_f_data);

    if (g_tree_nnodes(s->cframes)) {
        /*
         * Take a reference so when g_tree_destroy() unrefs it we keep the
         * GTree and only destroy its contents. NB: when our minimum
         * glib version is at least 2.70 we could use g_tree_remove_all().
         */
        g_tree_ref(s->cframes);
        g_tree_destroy(s->cframes);
    }
}

static void cframe_reg_reset_hold(Object *obj)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(obj);

    cfrm_imr_update_irq(s);
}

static const MemoryRegionOps cframe_reg_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps cframe_reg_fdri_ops = {
    .read = cframe_reg_fdri_read,
    .write = cframe_reg_fdri_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t cframes_bcast_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported read from addr=%"
                  HWADDR_PRIx "\n", __func__, addr);
    return 0;
}

static void cframes_bcast_write(XlnxVersalCFrameBcastReg *s, uint8_t reg_addr,
                                uint32_t *wfifo)
{
    XlnxCfiPacket pkt = {
        .reg_addr = reg_addr,
        .data[0] = wfifo[0],
        .data[1] = wfifo[1],
        .data[2] = wfifo[2],
        .data[3] = wfifo[3]
    };

    for (int i = 0; i < ARRAY_SIZE(s->cfg.cframe); i++) {
        if (s->cfg.cframe[i]) {
            xlnx_cfi_transfer_packet(s->cfg.cframe[i], &pkt);
        }
    }
}

static void cframes_bcast_reg_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    XlnxVersalCFrameBcastReg *s = XLNX_VERSAL_CFRAME_BCAST_REG(opaque);
    uint32_t wfifo[WFIFO_SZ];

    if (update_wfifo(addr, value, s->wfifo, wfifo)) {
        uint8_t reg_addr = extract32(addr, 4, 6);

        cframes_bcast_write(s, reg_addr, wfifo);
    }
}

static uint64_t cframes_bcast_fdri_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported read from addr=%"
                  HWADDR_PRIx "\n", __func__, addr);
    return 0;
}

static void cframes_bcast_fdri_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    XlnxVersalCFrameBcastReg *s = XLNX_VERSAL_CFRAME_BCAST_REG(opaque);
    uint32_t wfifo[WFIFO_SZ];

    if (update_wfifo(addr, value, s->wfifo, wfifo)) {
        cframes_bcast_write(s, CFRAME_FDRI, wfifo);
    }
}

static const MemoryRegionOps cframes_bcast_reg_reg_ops = {
    .read = cframes_bcast_reg_read,
    .write = cframes_bcast_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps cframes_bcast_reg_fdri_ops = {
    .read = cframes_bcast_fdri_read,
    .write = cframes_bcast_fdri_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void cframe_reg_realize(DeviceState *dev, Error **errp)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(dev);

    for (int i = 0; i < ARRAY_SIZE(s->cfg.blktype_num_frames); i++) {
        if (s->cfg.blktype_num_frames[i] > MAX_BLOCKTYPE_FRAMES) {
            error_setg(errp,
                       "blktype-frames%d > 0xFFFFF (max frame per block)",
                       i);
            return;
        }
        if (s->cfg.blktype_num_frames[i]) {
            s->row_configured = true;
        }
    }
}

static void cframe_reg_init(Object *obj)
{
    XlnxVersalCFrameReg *s = XLNX_VERSAL_CFRAME_REG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XLNX_VERSAL_CFRAME_REG,
                       CFRAME_REG_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), cframe_reg_regs_info,
                              ARRAY_SIZE(cframe_reg_regs_info),
                              s->regs_info, s->regs,
                              &cframe_reg_ops,
                              XLNX_VERSAL_CFRAME_REG_ERR_DEBUG,
                              CFRAME_REG_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    memory_region_init_io(&s->iomem_fdri, obj, &cframe_reg_fdri_ops, s,
                          TYPE_XLNX_VERSAL_CFRAME_REG "-fdri",
                          KEYHOLE_STREAM_4K);
    sysbus_init_mmio(sbd, &s->iomem_fdri);
    sysbus_init_irq(sbd, &s->irq_cfrm_imr);

    s->cframes = g_tree_new_full((GCompareDataFunc)int_cmp, NULL,
                                  NULL, (GDestroyNotify)g_free);
    fifo32_create(&s->new_f_data, FRAME_NUM_WORDS);
}

static const VMStateDescription vmstate_cframe = {
    .name = "cframe",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(data, XlnxCFrame, FRAME_NUM_WORDS),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_cframe_reg = {
    .name = TYPE_XLNX_VERSAL_CFRAME_REG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(wfifo, XlnxVersalCFrameReg, 4),
        VMSTATE_UINT32_ARRAY(regs, XlnxVersalCFrameReg, CFRAME_REG_R_MAX),
        VMSTATE_BOOL(rowon, XlnxVersalCFrameReg),
        VMSTATE_BOOL(wcfg, XlnxVersalCFrameReg),
        VMSTATE_BOOL(rcfg, XlnxVersalCFrameReg),
        VMSTATE_GTREE_DIRECT_KEY_V(cframes, XlnxVersalCFrameReg, 1,
                                   &vmstate_cframe, XlnxCFrame),
        VMSTATE_FIFO32(new_f_data, XlnxVersalCFrameReg),
        VMSTATE_END_OF_LIST(),
    }
};

static Property cframe_regs_props[] = {
    DEFINE_PROP_LINK("cfu-fdro", XlnxVersalCFrameReg, cfg.cfu_fdro,
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_UINT32("blktype0-frames", XlnxVersalCFrameReg,
                       cfg.blktype_num_frames[0], 0),
    DEFINE_PROP_UINT32("blktype1-frames", XlnxVersalCFrameReg,
                       cfg.blktype_num_frames[1], 0),
    DEFINE_PROP_UINT32("blktype2-frames", XlnxVersalCFrameReg,
                       cfg.blktype_num_frames[2], 0),
    DEFINE_PROP_UINT32("blktype3-frames", XlnxVersalCFrameReg,
                       cfg.blktype_num_frames[3], 0),
    DEFINE_PROP_UINT32("blktype4-frames", XlnxVersalCFrameReg,
                       cfg.blktype_num_frames[4], 0),
    DEFINE_PROP_UINT32("blktype5-frames", XlnxVersalCFrameReg,
                       cfg.blktype_num_frames[5], 0),
    DEFINE_PROP_UINT32("blktype6-frames", XlnxVersalCFrameReg,
                       cfg.blktype_num_frames[6], 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void cframe_bcast_reg_init(Object *obj)
{
    XlnxVersalCFrameBcastReg *s = XLNX_VERSAL_CFRAME_BCAST_REG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem_reg, obj, &cframes_bcast_reg_reg_ops, s,
                          TYPE_XLNX_VERSAL_CFRAME_BCAST_REG, KEYHOLE_STREAM_4K);
    memory_region_init_io(&s->iomem_fdri, obj, &cframes_bcast_reg_fdri_ops, s,
                          TYPE_XLNX_VERSAL_CFRAME_BCAST_REG "-fdri",
                          KEYHOLE_STREAM_4K);
    sysbus_init_mmio(sbd, &s->iomem_reg);
    sysbus_init_mmio(sbd, &s->iomem_fdri);
}

static void cframe_bcast_reg_reset_enter(Object *obj, ResetType type)
{
    XlnxVersalCFrameBcastReg *s = XLNX_VERSAL_CFRAME_BCAST_REG(obj);

    memset(s->wfifo, 0, WFIFO_SZ * sizeof(uint32_t));
}

static const VMStateDescription vmstate_cframe_bcast_reg = {
    .name = TYPE_XLNX_VERSAL_CFRAME_BCAST_REG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(wfifo, XlnxVersalCFrameBcastReg, 4),
        VMSTATE_END_OF_LIST(),
    }
};

static Property cframe_bcast_regs_props[] = {
    DEFINE_PROP_LINK("cframe0", XlnxVersalCFrameBcastReg, cfg.cframe[0],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe1", XlnxVersalCFrameBcastReg, cfg.cframe[1],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe2", XlnxVersalCFrameBcastReg, cfg.cframe[2],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe3", XlnxVersalCFrameBcastReg, cfg.cframe[3],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe4", XlnxVersalCFrameBcastReg, cfg.cframe[4],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe5", XlnxVersalCFrameBcastReg, cfg.cframe[5],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe6", XlnxVersalCFrameBcastReg, cfg.cframe[6],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe7", XlnxVersalCFrameBcastReg, cfg.cframe[7],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe8", XlnxVersalCFrameBcastReg, cfg.cframe[8],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe9", XlnxVersalCFrameBcastReg, cfg.cframe[9],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe10", XlnxVersalCFrameBcastReg, cfg.cframe[10],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe11", XlnxVersalCFrameBcastReg, cfg.cframe[11],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe12", XlnxVersalCFrameBcastReg, cfg.cframe[12],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe13", XlnxVersalCFrameBcastReg, cfg.cframe[13],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_LINK("cframe14", XlnxVersalCFrameBcastReg, cfg.cframe[14],
                     TYPE_XLNX_CFI_IF, XlnxCfiIf *),
    DEFINE_PROP_END_OF_LIST(),
};

static void cframe_reg_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    XlnxCfiIfClass *xcic = XLNX_CFI_IF_CLASS(klass);

    dc->vmsd = &vmstate_cframe_reg;
    dc->realize = cframe_reg_realize;
    rc->phases.enter = cframe_reg_reset_enter;
    rc->phases.hold = cframe_reg_reset_hold;
    device_class_set_props(dc, cframe_regs_props);
    xcic->cfi_transfer_packet = cframe_reg_cfi_transfer_packet;
}

static void cframe_bcast_reg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_cframe_bcast_reg;
    device_class_set_props(dc, cframe_bcast_regs_props);
    rc->phases.enter = cframe_bcast_reg_reset_enter;
}

static const TypeInfo cframe_reg_info = {
    .name          = TYPE_XLNX_VERSAL_CFRAME_REG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalCFrameReg),
    .class_init    = cframe_reg_class_init,
    .instance_init = cframe_reg_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_XLNX_CFI_IF },
        { }
    }
};

static const TypeInfo cframe_bcast_reg_info = {
    .name          = TYPE_XLNX_VERSAL_CFRAME_BCAST_REG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalCFrameBcastReg),
    .class_init    = cframe_bcast_reg_class_init,
    .instance_init = cframe_bcast_reg_init,
};

static void cframe_reg_register_types(void)
{
    type_register_static(&cframe_reg_info);
    type_register_static(&cframe_bcast_reg_info);
}

type_init(cframe_reg_register_types)
