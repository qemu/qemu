/*
 * QEMU model of the Versal eFuse controller
 *
 * Copyright (c) 2020 Xilinx Inc.
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
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
#include "hw/nvram/xlnx-versal-efuse.h"

#include "qemu/log.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#ifndef XLNX_VERSAL_EFUSE_CTRL_ERR_DEBUG
#define XLNX_VERSAL_EFUSE_CTRL_ERR_DEBUG 0
#endif

REG32(WR_LOCK, 0x0)
    FIELD(WR_LOCK, LOCK, 0, 16)
REG32(CFG, 0x4)
    FIELD(CFG, SLVERR_ENABLE, 5, 1)
    FIELD(CFG, MARGIN_RD, 2, 1)
    FIELD(CFG, PGM_EN, 1, 1)
REG32(STATUS, 0x8)
    FIELD(STATUS, AES_USER_KEY_1_CRC_PASS, 11, 1)
    FIELD(STATUS, AES_USER_KEY_1_CRC_DONE, 10, 1)
    FIELD(STATUS, AES_USER_KEY_0_CRC_PASS, 9, 1)
    FIELD(STATUS, AES_USER_KEY_0_CRC_DONE, 8, 1)
    FIELD(STATUS, AES_CRC_PASS, 7, 1)
    FIELD(STATUS, AES_CRC_DONE, 6, 1)
    FIELD(STATUS, CACHE_DONE, 5, 1)
    FIELD(STATUS, CACHE_LOAD, 4, 1)
    FIELD(STATUS, EFUSE_2_TBIT, 2, 1)
    FIELD(STATUS, EFUSE_1_TBIT, 1, 1)
    FIELD(STATUS, EFUSE_0_TBIT, 0, 1)
REG32(EFUSE_PGM_ADDR, 0xc)
    FIELD(EFUSE_PGM_ADDR, PAGE, 13, 4)
    FIELD(EFUSE_PGM_ADDR, ROW, 5, 8)
    FIELD(EFUSE_PGM_ADDR, COLUMN, 0, 5)
REG32(EFUSE_RD_ADDR, 0x10)
    FIELD(EFUSE_RD_ADDR, PAGE, 13, 4)
    FIELD(EFUSE_RD_ADDR, ROW, 5, 8)
REG32(EFUSE_RD_DATA, 0x14)
REG32(TPGM, 0x18)
    FIELD(TPGM, VALUE, 0, 16)
REG32(TRD, 0x1c)
    FIELD(TRD, VALUE, 0, 8)
REG32(TSU_H_PS, 0x20)
    FIELD(TSU_H_PS, VALUE, 0, 8)
REG32(TSU_H_PS_CS, 0x24)
    FIELD(TSU_H_PS_CS, VALUE, 0, 8)
REG32(TRDM, 0x28)
    FIELD(TRDM, VALUE, 0, 8)
REG32(TSU_H_CS, 0x2c)
    FIELD(TSU_H_CS, VALUE, 0, 8)
REG32(EFUSE_ISR, 0x30)
    FIELD(EFUSE_ISR, APB_SLVERR, 31, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E2, 14, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E1, 13, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E0S, 12, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E0R, 11, 1)
    FIELD(EFUSE_ISR, CACHE_APB_SLVERR, 10, 1)
    FIELD(EFUSE_ISR, CACHE_REQ_ERROR, 9, 1)
    FIELD(EFUSE_ISR, MAIN_REQ_ERROR, 8, 1)
    FIELD(EFUSE_ISR, READ_ON_CACHE_LD, 7, 1)
    FIELD(EFUSE_ISR, CACHE_FSM_ERROR, 6, 1)
    FIELD(EFUSE_ISR, MAIN_FSM_ERROR, 5, 1)
    FIELD(EFUSE_ISR, CACHE_ERROR, 4, 1)
    FIELD(EFUSE_ISR, RD_ERROR, 3, 1)
    FIELD(EFUSE_ISR, RD_DONE, 2, 1)
    FIELD(EFUSE_ISR, PGM_ERROR, 1, 1)
    FIELD(EFUSE_ISR, PGM_DONE, 0, 1)
REG32(EFUSE_IMR, 0x34)
    FIELD(EFUSE_IMR, APB_SLVERR, 31, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E2, 14, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E1, 13, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E0S, 12, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E0R, 11, 1)
    FIELD(EFUSE_IMR, CACHE_APB_SLVERR, 10, 1)
    FIELD(EFUSE_IMR, CACHE_REQ_ERROR, 9, 1)
    FIELD(EFUSE_IMR, MAIN_REQ_ERROR, 8, 1)
    FIELD(EFUSE_IMR, READ_ON_CACHE_LD, 7, 1)
    FIELD(EFUSE_IMR, CACHE_FSM_ERROR, 6, 1)
    FIELD(EFUSE_IMR, MAIN_FSM_ERROR, 5, 1)
    FIELD(EFUSE_IMR, CACHE_ERROR, 4, 1)
    FIELD(EFUSE_IMR, RD_ERROR, 3, 1)
    FIELD(EFUSE_IMR, RD_DONE, 2, 1)
    FIELD(EFUSE_IMR, PGM_ERROR, 1, 1)
    FIELD(EFUSE_IMR, PGM_DONE, 0, 1)
REG32(EFUSE_IER, 0x38)
    FIELD(EFUSE_IER, APB_SLVERR, 31, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E2, 14, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E1, 13, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E0S, 12, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E0R, 11, 1)
    FIELD(EFUSE_IER, CACHE_APB_SLVERR, 10, 1)
    FIELD(EFUSE_IER, CACHE_REQ_ERROR, 9, 1)
    FIELD(EFUSE_IER, MAIN_REQ_ERROR, 8, 1)
    FIELD(EFUSE_IER, READ_ON_CACHE_LD, 7, 1)
    FIELD(EFUSE_IER, CACHE_FSM_ERROR, 6, 1)
    FIELD(EFUSE_IER, MAIN_FSM_ERROR, 5, 1)
    FIELD(EFUSE_IER, CACHE_ERROR, 4, 1)
    FIELD(EFUSE_IER, RD_ERROR, 3, 1)
    FIELD(EFUSE_IER, RD_DONE, 2, 1)
    FIELD(EFUSE_IER, PGM_ERROR, 1, 1)
    FIELD(EFUSE_IER, PGM_DONE, 0, 1)
REG32(EFUSE_IDR, 0x3c)
    FIELD(EFUSE_IDR, APB_SLVERR, 31, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E2, 14, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E1, 13, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E0S, 12, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E0R, 11, 1)
    FIELD(EFUSE_IDR, CACHE_APB_SLVERR, 10, 1)
    FIELD(EFUSE_IDR, CACHE_REQ_ERROR, 9, 1)
    FIELD(EFUSE_IDR, MAIN_REQ_ERROR, 8, 1)
    FIELD(EFUSE_IDR, READ_ON_CACHE_LD, 7, 1)
    FIELD(EFUSE_IDR, CACHE_FSM_ERROR, 6, 1)
    FIELD(EFUSE_IDR, MAIN_FSM_ERROR, 5, 1)
    FIELD(EFUSE_IDR, CACHE_ERROR, 4, 1)
    FIELD(EFUSE_IDR, RD_ERROR, 3, 1)
    FIELD(EFUSE_IDR, RD_DONE, 2, 1)
    FIELD(EFUSE_IDR, PGM_ERROR, 1, 1)
    FIELD(EFUSE_IDR, PGM_DONE, 0, 1)
REG32(EFUSE_CACHE_LOAD, 0x40)
    FIELD(EFUSE_CACHE_LOAD, LOAD, 0, 1)
REG32(EFUSE_PGM_LOCK, 0x44)
    FIELD(EFUSE_PGM_LOCK, SPK_ID_LOCK, 0, 1)
REG32(EFUSE_AES_CRC, 0x48)
REG32(EFUSE_AES_USR_KEY0_CRC, 0x4c)
REG32(EFUSE_AES_USR_KEY1_CRC, 0x50)
REG32(EFUSE_PD, 0x54)
REG32(EFUSE_ANLG_OSC_SW_1LP, 0x60)
REG32(EFUSE_TEST_CTRL, 0x100)

#define R_MAX (R_EFUSE_TEST_CTRL + 1)

#define R_WR_LOCK_UNLOCK_PASSCODE   (0xDF0D)

/*
 * eFuse layout references:
 *   https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilnvm/src/xnvm_efuse_hw.h
 */
#define BIT_POS_OF(A_) \
    ((uint32_t)((A_) & (R_EFUSE_PGM_ADDR_ROW_MASK | \
                        R_EFUSE_PGM_ADDR_COLUMN_MASK)))

#define BIT_POS(R_, C_) \
        ((uint32_t)((R_EFUSE_PGM_ADDR_ROW_MASK                  \
                    & ((R_) << R_EFUSE_PGM_ADDR_ROW_SHIFT))     \
                    |                                           \
                    (R_EFUSE_PGM_ADDR_COLUMN_MASK               \
                     & ((C_) << R_EFUSE_PGM_ADDR_COLUMN_SHIFT))))

#define EFUSE_TBIT_POS(A_)          (BIT_POS_OF(A_) >= BIT_POS(0, 28))

#define EFUSE_ANCHOR_ROW            (0)
#define EFUSE_ANCHOR_3_COL          (27)
#define EFUSE_ANCHOR_1_COL          (1)

#define EFUSE_AES_KEY_START         BIT_POS(12, 0)
#define EFUSE_AES_KEY_END           BIT_POS(19, 31)
#define EFUSE_USER_KEY_0_START      BIT_POS(20, 0)
#define EFUSE_USER_KEY_0_END        BIT_POS(27, 31)
#define EFUSE_USER_KEY_1_START      BIT_POS(28, 0)
#define EFUSE_USER_KEY_1_END        BIT_POS(35, 31)

#define EFUSE_RD_BLOCKED_START      EFUSE_AES_KEY_START
#define EFUSE_RD_BLOCKED_END        EFUSE_USER_KEY_1_END

#define EFUSE_GLITCH_DET_WR_LK      BIT_POS(4, 31)
#define EFUSE_PPK0_WR_LK            BIT_POS(43, 6)
#define EFUSE_PPK1_WR_LK            BIT_POS(43, 7)
#define EFUSE_PPK2_WR_LK            BIT_POS(43, 8)
#define EFUSE_AES_WR_LK             BIT_POS(43, 11)
#define EFUSE_USER_KEY_0_WR_LK      BIT_POS(43, 13)
#define EFUSE_USER_KEY_1_WR_LK      BIT_POS(43, 15)
#define EFUSE_PUF_SYN_LK            BIT_POS(43, 16)
#define EFUSE_DNA_WR_LK             BIT_POS(43, 27)
#define EFUSE_BOOT_ENV_WR_LK        BIT_POS(43, 28)

#define EFUSE_PGM_LOCKED_START      BIT_POS(44, 0)
#define EFUSE_PGM_LOCKED_END        BIT_POS(51, 31)

#define EFUSE_PUF_PAGE              (2)
#define EFUSE_PUF_SYN_START         BIT_POS(129, 0)
#define EFUSE_PUF_SYN_END           BIT_POS(255, 27)

#define EFUSE_KEY_CRC_LK_ROW           (43)
#define EFUSE_AES_KEY_CRC_LK_MASK      ((1U << 9) | (1U << 10))
#define EFUSE_USER_KEY_0_CRC_LK_MASK   (1U << 12)
#define EFUSE_USER_KEY_1_CRC_LK_MASK   (1U << 14)

/*
 * A handy macro to return value of an array element,
 * or a specific default if given index is out of bound.
 */
#define ARRAY_GET(A_, I_, D_) \
    ((unsigned int)(I_) < ARRAY_SIZE(A_) ? (A_)[I_] : (D_))

QEMU_BUILD_BUG_ON(R_MAX != ARRAY_SIZE(((XlnxVersalEFuseCtrl *)0)->regs));

typedef struct XlnxEFuseLkSpec {
    uint16_t row;
    uint16_t lk_bit;
} XlnxEFuseLkSpec;

static void efuse_imr_update_irq(XlnxVersalEFuseCtrl *s)
{
    bool pending = s->regs[R_EFUSE_ISR] & ~s->regs[R_EFUSE_IMR];
    qemu_set_irq(s->irq_efuse_imr, pending);
}

static void efuse_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(reg->opaque);
    efuse_imr_update_irq(s);
}

static uint64_t efuse_ier_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_EFUSE_IMR] &= ~val;
    efuse_imr_update_irq(s);
    return 0;
}

static uint64_t efuse_idr_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_EFUSE_IMR] |= val;
    efuse_imr_update_irq(s);
    return 0;
}

static void efuse_status_tbits_sync(XlnxVersalEFuseCtrl *s)
{
    uint32_t check = xlnx_efuse_tbits_check(s->efuse);
    uint32_t val = s->regs[R_STATUS];

    val = FIELD_DP32(val, STATUS, EFUSE_0_TBIT, !!(check & (1 << 0)));
    val = FIELD_DP32(val, STATUS, EFUSE_1_TBIT, !!(check & (1 << 1)));
    val = FIELD_DP32(val, STATUS, EFUSE_2_TBIT, !!(check & (1 << 2)));

    s->regs[R_STATUS] = val;
}

static void efuse_anchor_bits_check(XlnxVersalEFuseCtrl *s)
{
    unsigned page;

    if (!s->efuse || !s->efuse->init_tbits) {
        return;
    }

    for (page = 0; page < s->efuse->efuse_nr; page++) {
        uint32_t row = 0, bit;

        row = FIELD_DP32(row, EFUSE_PGM_ADDR, PAGE, page);
        row = FIELD_DP32(row, EFUSE_PGM_ADDR, ROW, EFUSE_ANCHOR_ROW);

        bit = FIELD_DP32(row, EFUSE_PGM_ADDR, COLUMN, EFUSE_ANCHOR_3_COL);
        if (!xlnx_efuse_get_bit(s->efuse, bit)) {
            xlnx_efuse_set_bit(s->efuse, bit);
        }

        bit = FIELD_DP32(row, EFUSE_PGM_ADDR, COLUMN, EFUSE_ANCHOR_1_COL);
        if (!xlnx_efuse_get_bit(s->efuse, bit)) {
            xlnx_efuse_set_bit(s->efuse, bit);
        }
    }
}

static void efuse_key_crc_check(RegisterInfo *reg, uint32_t crc,
                                uint32_t pass_mask, uint32_t done_mask,
                                unsigned first, uint32_t lk_mask)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(reg->opaque);
    uint32_t r, lk_bits;

    /*
     * To start, assume both DONE and PASS, and clear PASS by xor
     * if CRC-check fails or CRC-check disabled by lock fuse.
     */
    r = s->regs[R_STATUS] | done_mask | pass_mask;

    lk_bits = xlnx_efuse_get_row(s->efuse, EFUSE_KEY_CRC_LK_ROW) & lk_mask;
    if (lk_bits == 0 && xlnx_efuse_k256_check(s->efuse, crc, first)) {
        pass_mask = 0;
    }

    s->regs[R_STATUS] = r ^ pass_mask;
}

static void efuse_data_sync(XlnxVersalEFuseCtrl *s)
{
    efuse_status_tbits_sync(s);
}

static int efuse_lk_spec_cmp(const void *a, const void *b)
{
    uint16_t r1 = ((const XlnxEFuseLkSpec *)a)->row;
    uint16_t r2 = ((const XlnxEFuseLkSpec *)b)->row;

    return (r1 > r2) - (r1 < r2);
}

static void efuse_lk_spec_sort(XlnxVersalEFuseCtrl *s)
{
    XlnxEFuseLkSpec *ary = s->extra_pg0_lock_spec;
    const uint32_t n8 = s->extra_pg0_lock_n16 * 2;
    const uint32_t sz  = sizeof(ary[0]);
    const uint32_t cnt = n8 / sz;

    if (ary && cnt) {
        qsort(ary, cnt, sz, efuse_lk_spec_cmp);
    }
}

static uint32_t efuse_lk_spec_find(XlnxVersalEFuseCtrl *s, uint32_t row)
{
    const XlnxEFuseLkSpec *ary = s->extra_pg0_lock_spec;
    const uint32_t n8  = s->extra_pg0_lock_n16 * 2;
    const uint32_t sz  = sizeof(ary[0]);
    const uint32_t cnt = n8 / sz;
    const XlnxEFuseLkSpec *item = NULL;

    if (ary && cnt) {
        XlnxEFuseLkSpec k = { .row = row, };

        item = bsearch(&k, ary, cnt, sz, efuse_lk_spec_cmp);
    }

    return item ? item->lk_bit : 0;
}

static uint32_t efuse_bit_locked(XlnxVersalEFuseCtrl *s, uint32_t bit)
{
    /* Hard-coded locks */
    static const uint16_t pg0_hard_lock[] = {
        [4] = EFUSE_GLITCH_DET_WR_LK,
        [37] = EFUSE_BOOT_ENV_WR_LK,

        [8 ... 11]  = EFUSE_DNA_WR_LK,
        [12 ... 19] = EFUSE_AES_WR_LK,
        [20 ... 27] = EFUSE_USER_KEY_0_WR_LK,
        [28 ... 35] = EFUSE_USER_KEY_1_WR_LK,
        [64 ... 71] = EFUSE_PPK0_WR_LK,
        [72 ... 79] = EFUSE_PPK1_WR_LK,
        [80 ... 87] = EFUSE_PPK2_WR_LK,
    };

    uint32_t row = FIELD_EX32(bit, EFUSE_PGM_ADDR, ROW);
    uint32_t lk_bit = ARRAY_GET(pg0_hard_lock, row, 0);

    return lk_bit ? lk_bit : efuse_lk_spec_find(s, row);
}

static bool efuse_pgm_locked(XlnxVersalEFuseCtrl *s, unsigned int bit)
{

    unsigned int lock = 1;

    /* Global lock */
    if (!ARRAY_FIELD_EX32(s->regs, CFG, PGM_EN)) {
        goto ret_lock;
    }

    /* Row lock */
    switch (FIELD_EX32(bit, EFUSE_PGM_ADDR, PAGE)) {
    case 0:
        if (ARRAY_FIELD_EX32(s->regs, EFUSE_PGM_LOCK, SPK_ID_LOCK) &&
            bit >= EFUSE_PGM_LOCKED_START && bit <= EFUSE_PGM_LOCKED_END) {
            goto ret_lock;
        }

        lock = efuse_bit_locked(s, bit);
        break;
    case EFUSE_PUF_PAGE:
        if (bit < EFUSE_PUF_SYN_START || bit > EFUSE_PUF_SYN_END) {
            lock = 0;
            goto ret_lock;
        }

        lock = EFUSE_PUF_SYN_LK;
        break;
    default:
        lock = 0;
        goto ret_lock;
    }

    /* Row lock by an efuse bit */
    if (lock) {
        lock = xlnx_efuse_get_bit(s->efuse, lock);
    }

 ret_lock:
    return lock != 0;
}

static void efuse_pgm_addr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(reg->opaque);
    unsigned bit = val64;
    bool ok = false;

    /* Always zero out PGM_ADDR because it is write-only */
    s->regs[R_EFUSE_PGM_ADDR] = 0;

    /*
     * Indicate error if bit is write-protected (or read-only
     * as guarded by efuse_set_bit()).
     *
     * Keep it simple by not modeling program timing.
     *
     * Note: model must NEVER clear the PGM_ERROR bit; it is
     *       up to guest to do so (or by reset).
     */
    if (efuse_pgm_locked(s, bit)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));

        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Denied setting of efuse<%u, %u, %u>\n",
                      path,
                      FIELD_EX32(bit, EFUSE_PGM_ADDR, PAGE),
                      FIELD_EX32(bit, EFUSE_PGM_ADDR, ROW),
                      FIELD_EX32(bit, EFUSE_PGM_ADDR, COLUMN));
    } else if (xlnx_efuse_set_bit(s->efuse, bit)) {
        ok = true;
        if (EFUSE_TBIT_POS(bit)) {
            efuse_status_tbits_sync(s);
        }
    }

    if (!ok) {
        ARRAY_FIELD_DP32(s->regs, EFUSE_ISR, PGM_ERROR, 1);
    }

    ARRAY_FIELD_DP32(s->regs, EFUSE_ISR, PGM_DONE, 1);
    efuse_imr_update_irq(s);
}

static void efuse_rd_addr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(reg->opaque);
    unsigned bit = val64;
    bool denied;

    /* Always zero out RD_ADDR because it is write-only */
    s->regs[R_EFUSE_RD_ADDR] = 0;

    /*
     * Indicate error if row is read-blocked.
     *
     * Note: model must NEVER clear the RD_ERROR bit; it is
     *       up to guest to do so (or by reset).
     */
    s->regs[R_EFUSE_RD_DATA] = xlnx_versal_efuse_read_row(s->efuse,
                                                          bit, &denied);
    if (denied) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));

        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Denied reading of efuse<%u, %u>\n",
                      path,
                      FIELD_EX32(bit, EFUSE_RD_ADDR, PAGE),
                      FIELD_EX32(bit, EFUSE_RD_ADDR, ROW));

        ARRAY_FIELD_DP32(s->regs, EFUSE_ISR, RD_ERROR, 1);
    }

    ARRAY_FIELD_DP32(s->regs, EFUSE_ISR, RD_DONE, 1);
    efuse_imr_update_irq(s);
}

static uint64_t efuse_cache_load_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(reg->opaque);

    if (val64 & R_EFUSE_CACHE_LOAD_LOAD_MASK) {
        efuse_data_sync(s);

        ARRAY_FIELD_DP32(s->regs, STATUS, CACHE_DONE, 1);
        efuse_imr_update_irq(s);
    }

    return 0;
}

static uint64_t efuse_pgm_lock_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(reg->opaque);

    /* Ignore all other bits */
    val64 = FIELD_EX32(val64, EFUSE_PGM_LOCK, SPK_ID_LOCK);

    /* Once the bit is written 1, only reset will clear it to 0 */
    val64 |= ARRAY_FIELD_EX32(s->regs, EFUSE_PGM_LOCK, SPK_ID_LOCK);

    return val64;
}

static void efuse_aes_crc_postw(RegisterInfo *reg, uint64_t val64)
{
    efuse_key_crc_check(reg, val64,
                        R_STATUS_AES_CRC_PASS_MASK,
                        R_STATUS_AES_CRC_DONE_MASK,
                        EFUSE_AES_KEY_START,
                        EFUSE_AES_KEY_CRC_LK_MASK);
}

static void efuse_aes_u0_crc_postw(RegisterInfo *reg, uint64_t val64)
{
    efuse_key_crc_check(reg, val64,
                        R_STATUS_AES_USER_KEY_0_CRC_PASS_MASK,
                        R_STATUS_AES_USER_KEY_0_CRC_DONE_MASK,
                        EFUSE_USER_KEY_0_START,
                        EFUSE_USER_KEY_0_CRC_LK_MASK);
}

static void efuse_aes_u1_crc_postw(RegisterInfo *reg, uint64_t val64)
{
    efuse_key_crc_check(reg, val64,
                        R_STATUS_AES_USER_KEY_1_CRC_PASS_MASK,
                        R_STATUS_AES_USER_KEY_1_CRC_DONE_MASK,
                        EFUSE_USER_KEY_1_START,
                        EFUSE_USER_KEY_1_CRC_LK_MASK);
}

static uint64_t efuse_wr_lock_prew(RegisterInfo *reg, uint64_t val)
{
    return val != R_WR_LOCK_UNLOCK_PASSCODE;
}

static const RegisterAccessInfo efuse_ctrl_regs_info[] = {
    {   .name = "WR_LOCK",  .addr = A_WR_LOCK,
        .reset = 0x1,
        .pre_write = efuse_wr_lock_prew,
    },{ .name = "CFG",  .addr = A_CFG,
        .rsvd = 0x9,
    },{ .name = "STATUS",  .addr = A_STATUS,
        .rsvd = 0x8,
        .ro = 0xfff,
    },{ .name = "EFUSE_PGM_ADDR",  .addr = A_EFUSE_PGM_ADDR,
        .post_write = efuse_pgm_addr_postw,
    },{ .name = "EFUSE_RD_ADDR",  .addr = A_EFUSE_RD_ADDR,
        .rsvd = 0x1f,
        .post_write = efuse_rd_addr_postw,
    },{ .name = "EFUSE_RD_DATA",  .addr = A_EFUSE_RD_DATA,
        .ro = 0xffffffff,
    },{ .name = "TPGM",  .addr = A_TPGM,
    },{ .name = "TRD",  .addr = A_TRD,
        .reset = 0x19,
    },{ .name = "TSU_H_PS",  .addr = A_TSU_H_PS,
        .reset = 0xff,
    },{ .name = "TSU_H_PS_CS",  .addr = A_TSU_H_PS_CS,
        .reset = 0x11,
    },{ .name = "TRDM",  .addr = A_TRDM,
        .reset = 0x3a,
    },{ .name = "TSU_H_CS",  .addr = A_TSU_H_CS,
        .reset = 0x16,
    },{ .name = "EFUSE_ISR",  .addr = A_EFUSE_ISR,
        .rsvd = 0x7fff8000,
        .w1c = 0x80007fff,
        .post_write = efuse_isr_postw,
    },{ .name = "EFUSE_IMR",  .addr = A_EFUSE_IMR,
        .reset = 0x80007fff,
        .rsvd = 0x7fff8000,
        .ro = 0xffffffff,
    },{ .name = "EFUSE_IER",  .addr = A_EFUSE_IER,
        .rsvd = 0x7fff8000,
        .pre_write = efuse_ier_prew,
    },{ .name = "EFUSE_IDR",  .addr = A_EFUSE_IDR,
        .rsvd = 0x7fff8000,
        .pre_write = efuse_idr_prew,
    },{ .name = "EFUSE_CACHE_LOAD",  .addr = A_EFUSE_CACHE_LOAD,
        .pre_write = efuse_cache_load_prew,
    },{ .name = "EFUSE_PGM_LOCK",  .addr = A_EFUSE_PGM_LOCK,
        .pre_write = efuse_pgm_lock_prew,
    },{ .name = "EFUSE_AES_CRC",  .addr = A_EFUSE_AES_CRC,
        .post_write = efuse_aes_crc_postw,
    },{ .name = "EFUSE_AES_USR_KEY0_CRC",  .addr = A_EFUSE_AES_USR_KEY0_CRC,
        .post_write = efuse_aes_u0_crc_postw,
    },{ .name = "EFUSE_AES_USR_KEY1_CRC",  .addr = A_EFUSE_AES_USR_KEY1_CRC,
        .post_write = efuse_aes_u1_crc_postw,
    },{ .name = "EFUSE_PD",  .addr = A_EFUSE_PD,
        .ro = 0xfffffffe,
    },{ .name = "EFUSE_ANLG_OSC_SW_1LP",  .addr = A_EFUSE_ANLG_OSC_SW_1LP,
    },{ .name = "EFUSE_TEST_CTRL",  .addr = A_EFUSE_TEST_CTRL,
        .reset = 0x8,
    }
};

static void efuse_ctrl_reg_write(void *opaque, hwaddr addr,
                                 uint64_t data, unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    XlnxVersalEFuseCtrl *s;
    Object *dev;

    assert(reg_array != NULL);

    dev = reg_array->mem.owner;
    assert(dev);

    s = XLNX_VERSAL_EFUSE_CTRL(dev);

    if (addr != A_WR_LOCK && s->regs[R_WR_LOCK]) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));

        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s[reg_0x%02lx]: Attempt to write locked register.\n",
                      path, (long)addr);
    } else {
        register_write_memory(opaque, addr, data, size);
    }
}

static void efuse_ctrl_register_reset(RegisterInfo *reg)
{
    if (!reg->data || !reg->access) {
        return;
    }

    /* Reset must not trigger some registers' writers */
    switch (reg->access->addr) {
    case A_EFUSE_AES_CRC:
    case A_EFUSE_AES_USR_KEY0_CRC:
    case A_EFUSE_AES_USR_KEY1_CRC:
        *(uint32_t *)reg->data = reg->access->reset;
        return;
    }

    register_reset(reg);
}

static void efuse_ctrl_reset_hold(Object *obj, ResetType type)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        efuse_ctrl_register_reset(&s->regs_info[i]);
    }

    efuse_anchor_bits_check(s);
    efuse_data_sync(s);
    efuse_imr_update_irq(s);
}

static const MemoryRegionOps efuse_ctrl_ops = {
    .read = register_read_memory,
    .write = efuse_ctrl_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void efuse_ctrl_realize(DeviceState *dev, Error **errp)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(dev);
    const uint32_t lks_sz = sizeof(XlnxEFuseLkSpec) / 2;

    if (!s->efuse) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));

        error_setg(errp, "%s.efuse: link property not connected to XLNX-EFUSE",
                   path);
        return;
    }

    /* Sort property-defined pgm-locks for bsearch lookup */
    if ((s->extra_pg0_lock_n16 % lks_sz) != 0) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));

        error_setg(errp,
                   "%s.pg0-lock: array property item-count not multiple of %u",
                   path, lks_sz);
        return;
    }

    efuse_lk_spec_sort(s);
}

static void efuse_ctrl_init(Object *obj)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->reg_array =
        register_init_block32(DEVICE(obj), efuse_ctrl_regs_info,
                              ARRAY_SIZE(efuse_ctrl_regs_info),
                              s->regs_info, s->regs,
                              &efuse_ctrl_ops,
                              XLNX_VERSAL_EFUSE_CTRL_ERR_DEBUG,
                              R_MAX * 4);

    sysbus_init_mmio(sbd, &s->reg_array->mem);
    sysbus_init_irq(sbd, &s->irq_efuse_imr);
}

static void efuse_ctrl_finalize(Object *obj)
{
    XlnxVersalEFuseCtrl *s = XLNX_VERSAL_EFUSE_CTRL(obj);

    g_free(s->extra_pg0_lock_spec);
}

static const VMStateDescription vmstate_efuse_ctrl = {
    .name = TYPE_XLNX_VERSAL_EFUSE_CTRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxVersalEFuseCtrl, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property efuse_ctrl_props[] = {
    DEFINE_PROP_LINK("efuse",
                     XlnxVersalEFuseCtrl, efuse,
                     TYPE_XLNX_EFUSE, XlnxEFuse *),
    DEFINE_PROP_ARRAY("pg0-lock",
                      XlnxVersalEFuseCtrl, extra_pg0_lock_n16,
                      extra_pg0_lock_spec, qdev_prop_uint16, uint16_t),
};

static void efuse_ctrl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = efuse_ctrl_reset_hold;
    dc->realize = efuse_ctrl_realize;
    dc->vmsd = &vmstate_efuse_ctrl;
    device_class_set_props(dc, efuse_ctrl_props);
}

static const TypeInfo efuse_ctrl_info = {
    .name          = TYPE_XLNX_VERSAL_EFUSE_CTRL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalEFuseCtrl),
    .class_init    = efuse_ctrl_class_init,
    .instance_init = efuse_ctrl_init,
    .instance_finalize = efuse_ctrl_finalize,
};

static void efuse_ctrl_register_types(void)
{
    type_register_static(&efuse_ctrl_info);
}

type_init(efuse_ctrl_register_types)

/*
 * Retrieve a row, with unreadable bits returned as 0.
 */
uint32_t xlnx_versal_efuse_read_row(XlnxEFuse *efuse,
                                    uint32_t bit, bool *denied)
{
    bool dummy;

    if (!denied) {
        denied = &dummy;
    }

    if (bit >= EFUSE_RD_BLOCKED_START && bit <= EFUSE_RD_BLOCKED_END) {
        *denied = true;
        return 0;
    }

    *denied = false;
    return xlnx_efuse_get_row(efuse, bit);
}
