/*
 * QEMU model of the Xilinx Zynq Devcfg Interface
 *
 * (C) 2011 PetaLogix Pty Ltd
 * (C) 2014 Xilinx Inc.
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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
#include "hw/dma/xlnx-zynq-devcfg.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "system/dma.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define FREQ_HZ 900000000

#define BTT_MAX 0x400

#ifndef XLNX_ZYNQ_DEVCFG_ERR_DEBUG
#define XLNX_ZYNQ_DEVCFG_ERR_DEBUG 0
#endif

#define DB_PRINT(fmt, args...) do { \
    if (XLNX_ZYNQ_DEVCFG_ERR_DEBUG) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

REG32(CTRL, 0x00)
    FIELD(CTRL,     FORCE_RST,          31,  1) /* Not supported, wr ignored */
    FIELD(CTRL,     PCAP_PR,            27,  1) /* Forced to 0 on bad unlock */
    FIELD(CTRL,     PCAP_MODE,          26,  1)
    FIELD(CTRL,     MULTIBOOT_EN,       24,  1)
    FIELD(CTRL,     USER_MODE,          15,  1)
    FIELD(CTRL,     PCFG_AES_FUSE,      12,  1)
    FIELD(CTRL,     PCFG_AES_EN,         9,  3)
    FIELD(CTRL,     SEU_EN,              8,  1)
    FIELD(CTRL,     SEC_EN,              7,  1)
    FIELD(CTRL,     SPNIDEN,             6,  1)
    FIELD(CTRL,     SPIDEN,              5,  1)
    FIELD(CTRL,     NIDEN,               4,  1)
    FIELD(CTRL,     DBGEN,               3,  1)
    FIELD(CTRL,     DAP_EN,              0,  3)

REG32(LOCK, 0x04)
#define AES_FUSE_LOCK        4
#define AES_EN_LOCK          3
#define SEU_LOCK             2
#define SEC_LOCK             1
#define DBG_LOCK             0

/* mapping bits in R_LOCK to what they lock in R_CTRL */
static const uint32_t lock_ctrl_map[] = {
    [AES_FUSE_LOCK] = R_CTRL_PCFG_AES_FUSE_MASK,
    [AES_EN_LOCK]   = R_CTRL_PCFG_AES_EN_MASK,
    [SEU_LOCK]      = R_CTRL_SEU_EN_MASK,
    [SEC_LOCK]      = R_CTRL_SEC_EN_MASK,
    [DBG_LOCK]      = R_CTRL_SPNIDEN_MASK | R_CTRL_SPIDEN_MASK |
                      R_CTRL_NIDEN_MASK   | R_CTRL_DBGEN_MASK  |
                      R_CTRL_DAP_EN_MASK,
};

REG32(CFG, 0x08)
    FIELD(CFG,      RFIFO_TH,           10,  2)
    FIELD(CFG,      WFIFO_TH,            8,  2)
    FIELD(CFG,      RCLK_EDGE,           7,  1)
    FIELD(CFG,      WCLK_EDGE,           6,  1)
    FIELD(CFG,      DISABLE_SRC_INC,     5,  1)
    FIELD(CFG,      DISABLE_DST_INC,     4,  1)
#define R_CFG_RESET 0x50B

REG32(INT_STS, 0x0C)
    FIELD(INT_STS,  PSS_GTS_USR_B,      31,  1)
    FIELD(INT_STS,  PSS_FST_CFG_B,      30,  1)
    FIELD(INT_STS,  PSS_CFG_RESET_B,    27,  1)
    FIELD(INT_STS,  RX_FIFO_OV,         18,  1)
    FIELD(INT_STS,  WR_FIFO_LVL,        17,  1)
    FIELD(INT_STS,  RD_FIFO_LVL,        16,  1)
    FIELD(INT_STS,  DMA_CMD_ERR,        15,  1)
    FIELD(INT_STS,  DMA_Q_OV,           14,  1)
    FIELD(INT_STS,  DMA_DONE,           13,  1)
    FIELD(INT_STS,  DMA_P_DONE,         12,  1)
    FIELD(INT_STS,  P2D_LEN_ERR,        11,  1)
    FIELD(INT_STS,  PCFG_DONE,           2,  1)
#define R_INT_STS_RSVD       ((0x7 << 24) | (0x1 << 19) | (0xF < 7))

REG32(INT_MASK, 0x10)

REG32(STATUS, 0x14)
    FIELD(STATUS,   DMA_CMD_Q_F,        31,  1)
    FIELD(STATUS,   DMA_CMD_Q_E,        30,  1)
    FIELD(STATUS,   DMA_DONE_CNT,       28,  2)
    FIELD(STATUS,   RX_FIFO_LVL,        20,  5)
    FIELD(STATUS,   TX_FIFO_LVL,        12,  7)
    FIELD(STATUS,   PSS_GTS_USR_B,      11,  1)
    FIELD(STATUS,   PSS_FST_CFG_B,      10,  1)
    FIELD(STATUS,   PSS_CFG_RESET_B,     5,  1)

REG32(DMA_SRC_ADDR, 0x18)
REG32(DMA_DST_ADDR, 0x1C)
REG32(DMA_SRC_LEN, 0x20)
REG32(DMA_DST_LEN, 0x24)
REG32(ROM_SHADOW, 0x28)
REG32(SW_ID, 0x30)
REG32(UNLOCK, 0x34)

#define R_UNLOCK_MAGIC 0x757BDF0D

REG32(MCTRL, 0x80)
    FIELD(MCTRL,    PS_VERSION,         28,  4)
    FIELD(MCTRL,    PCFG_POR_B,          8,  1)
    FIELD(MCTRL,    INT_PCAP_LPBK,       4,  1)
    FIELD(MCTRL,    QEMU,                3,  1)

static void xlnx_zynq_devcfg_update_ixr(XlnxZynqDevcfg *s)
{
    qemu_set_irq(s->irq, ~s->regs[R_INT_MASK] & s->regs[R_INT_STS]);
}

static void xlnx_zynq_devcfg_reset(DeviceState *dev)
{
    XlnxZynqDevcfg *s = XLNX_ZYNQ_DEVCFG(dev);
    int i;

    for (i = 0; i < XLNX_ZYNQ_DEVCFG_R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void xlnx_zynq_devcfg_dma_go(XlnxZynqDevcfg *s)
{
    do {
        uint8_t buf[BTT_MAX];
        XlnxZynqDevcfgDMACmd *dmah = s->dma_cmd_fifo;
        uint32_t btt = BTT_MAX;
        bool loopback = s->regs[R_MCTRL] & R_MCTRL_INT_PCAP_LPBK_MASK;

        btt = MIN(btt, dmah->src_len);
        if (loopback) {
            btt = MIN(btt, dmah->dest_len);
        }
        DB_PRINT("reading %x bytes from %x\n", btt, dmah->src_addr);
        dma_memory_read(&address_space_memory, dmah->src_addr, buf, btt,
                        MEMTXATTRS_UNSPECIFIED);
        dmah->src_len -= btt;
        dmah->src_addr += btt;
        if (loopback && (dmah->src_len || dmah->dest_len)) {
            DB_PRINT("writing %x bytes from %x\n", btt, dmah->dest_addr);
            dma_memory_write(&address_space_memory, dmah->dest_addr, buf, btt,
                             MEMTXATTRS_UNSPECIFIED);
            dmah->dest_len -= btt;
            dmah->dest_addr += btt;
        }
        if (!dmah->src_len && !dmah->dest_len) {
            DB_PRINT("dma operation finished\n");
            s->regs[R_INT_STS] |= R_INT_STS_DMA_DONE_MASK |
                                  R_INT_STS_DMA_P_DONE_MASK;
            s->dma_cmd_fifo_num--;
            memmove(s->dma_cmd_fifo, &s->dma_cmd_fifo[1],
                    sizeof(s->dma_cmd_fifo) - sizeof(s->dma_cmd_fifo[0]));
        }
        xlnx_zynq_devcfg_update_ixr(s);
    } while (s->dma_cmd_fifo_num);
}

static void r_ixr_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxZynqDevcfg *s = XLNX_ZYNQ_DEVCFG(reg->opaque);

    xlnx_zynq_devcfg_update_ixr(s);
}

static uint64_t r_ctrl_pre_write(RegisterInfo *reg, uint64_t val)
{
    XlnxZynqDevcfg *s = XLNX_ZYNQ_DEVCFG(reg->opaque);
    int i;

    for (i = 0; i < ARRAY_SIZE(lock_ctrl_map); ++i) {
        if (s->regs[R_LOCK] & 1 << i) {
            val &= ~lock_ctrl_map[i];
            val |= lock_ctrl_map[i] & s->regs[R_CTRL];
        }
    }
    return val;
}

static void r_ctrl_post_write(RegisterInfo *reg, uint64_t val)
{
    const char *device_prefix = object_get_typename(OBJECT(reg->opaque));
    uint32_t aes_en = FIELD_EX32(val, CTRL, PCFG_AES_EN);

    if (aes_en != 0 && aes_en != 7) {
        qemu_log_mask(LOG_UNIMP, "%s: warning, aes-en bits inconsistent,"
                      "unimplemented security reset should happen!\n",
                      device_prefix);
    }
}

static void r_unlock_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxZynqDevcfg *s = XLNX_ZYNQ_DEVCFG(reg->opaque);
    const char *device_prefix = object_get_typename(OBJECT(s));

    if (val == R_UNLOCK_MAGIC) {
        DB_PRINT("successful unlock\n");
        s->regs[R_CTRL] |= R_CTRL_PCAP_PR_MASK;
        s->regs[R_CTRL] |= R_CTRL_PCFG_AES_EN_MASK;
        memory_region_set_enabled(&s->iomem, true);
    } else { /* bad unlock attempt */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: failed unlock\n", device_prefix);
        s->regs[R_CTRL] &= ~R_CTRL_PCAP_PR_MASK;
        s->regs[R_CTRL] &= ~R_CTRL_PCFG_AES_EN_MASK;
        /* core becomes inaccessible */
        memory_region_set_enabled(&s->iomem, false);
    }
}

static uint64_t r_lock_pre_write(RegisterInfo *reg, uint64_t val)
{
    XlnxZynqDevcfg *s = XLNX_ZYNQ_DEVCFG(reg->opaque);

    /* once bits are locked they stay locked */
    return s->regs[R_LOCK] | val;
}

static void r_dma_dst_len_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxZynqDevcfg *s = XLNX_ZYNQ_DEVCFG(reg->opaque);

    s->dma_cmd_fifo[s->dma_cmd_fifo_num] = (XlnxZynqDevcfgDMACmd) {
            .src_addr = s->regs[R_DMA_SRC_ADDR] & ~0x3UL,
            .dest_addr = s->regs[R_DMA_DST_ADDR] & ~0x3UL,
            .src_len = s->regs[R_DMA_SRC_LEN] << 2,
            .dest_len = s->regs[R_DMA_DST_LEN] << 2,
    };
    s->dma_cmd_fifo_num++;
    DB_PRINT("dma transfer started; %d total transfers pending\n",
             s->dma_cmd_fifo_num);
    xlnx_zynq_devcfg_dma_go(s);
}

static const RegisterAccessInfo xlnx_zynq_devcfg_regs_info[] = {
    {   .name = "CTRL",                 .addr = A_CTRL,
        .reset = R_CTRL_PCAP_PR_MASK | R_CTRL_PCAP_MODE_MASK | 0x3 << 13,
        .rsvd = 0x1 << 28 | 0x3ff << 13 | 0x3 << 13,
        .pre_write = r_ctrl_pre_write,
        .post_write = r_ctrl_post_write,
    },
    {   .name = "LOCK",                 .addr = A_LOCK,
        .rsvd = MAKE_64BIT_MASK(5, 64 - 5),
        .pre_write = r_lock_pre_write,
    },
    {   .name = "CFG",                  .addr = A_CFG,
        .reset = R_CFG_RESET,
        .rsvd = 0xfffff00f,
    },
    {   .name = "INT_STS",              .addr = A_INT_STS,
        .w1c = ~R_INT_STS_RSVD,
        .reset = R_INT_STS_PSS_GTS_USR_B_MASK   |
                 R_INT_STS_PSS_CFG_RESET_B_MASK |
                 R_INT_STS_WR_FIFO_LVL_MASK,
        .rsvd = R_INT_STS_RSVD,
        .post_write = r_ixr_post_write,
    },
    {   .name = "INT_MASK",            .addr = A_INT_MASK,
        .reset = ~0,
        .rsvd = R_INT_STS_RSVD,
        .post_write = r_ixr_post_write,
    },
    {   .name = "STATUS",               .addr = A_STATUS,
        .reset = R_STATUS_DMA_CMD_Q_E_MASK      |
                 R_STATUS_PSS_GTS_USR_B_MASK    |
                 R_STATUS_PSS_CFG_RESET_B_MASK,
        .ro = ~0,
    },
    {   .name = "DMA_SRC_ADDR",         .addr = A_DMA_SRC_ADDR, },
    {   .name = "DMA_DST_ADDR",         .addr = A_DMA_DST_ADDR, },
    {   .name = "DMA_SRC_LEN",          .addr = A_DMA_SRC_LEN,
        .ro = MAKE_64BIT_MASK(27, 64 - 27) },
    {   .name = "DMA_DST_LEN",          .addr = A_DMA_DST_LEN,
        .ro = MAKE_64BIT_MASK(27, 64 - 27),
        .post_write = r_dma_dst_len_post_write,
    },
    {   .name = "ROM_SHADOW",           .addr = A_ROM_SHADOW,
        .rsvd = ~0ull,
    },
    {   .name = "SW_ID",                .addr = A_SW_ID, },
    {   .name = "UNLOCK",               .addr = A_UNLOCK,
        .post_write = r_unlock_post_write,
    },
    {   .name = "MCTRL",                .addr = R_MCTRL * 4,
       /* Silicon 3.0 for version field, the mysterious reserved bit 23
        * and QEMU platform identifier.
        */
       .reset = 0x2 << R_MCTRL_PS_VERSION_SHIFT | 1 << 23 | R_MCTRL_QEMU_MASK,
       .ro = ~R_MCTRL_INT_PCAP_LPBK_MASK,
       .rsvd = 0x00f00303,
    },
};

static const MemoryRegionOps xlnx_zynq_devcfg_reg_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const VMStateDescription vmstate_xlnx_zynq_devcfg_dma_cmd = {
    .name = "xlnx_zynq_devcfg_dma_cmd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(src_addr, XlnxZynqDevcfgDMACmd),
        VMSTATE_UINT32(dest_addr, XlnxZynqDevcfgDMACmd),
        VMSTATE_UINT32(src_len, XlnxZynqDevcfgDMACmd),
        VMSTATE_UINT32(dest_len, XlnxZynqDevcfgDMACmd),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_xlnx_zynq_devcfg = {
    .name = "xlnx_zynq_devcfg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(dma_cmd_fifo, XlnxZynqDevcfg,
                             XLNX_ZYNQ_DEVCFG_DMA_CMD_FIFO_LEN, 0,
                             vmstate_xlnx_zynq_devcfg_dma_cmd,
                             XlnxZynqDevcfgDMACmd),
        VMSTATE_UINT8(dma_cmd_fifo_num, XlnxZynqDevcfg),
        VMSTATE_UINT32_ARRAY(regs, XlnxZynqDevcfg, XLNX_ZYNQ_DEVCFG_R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void xlnx_zynq_devcfg_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    XlnxZynqDevcfg *s = XLNX_ZYNQ_DEVCFG(obj);
    RegisterInfoArray *reg_array;

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init(&s->iomem, obj, "devcfg", XLNX_ZYNQ_DEVCFG_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), xlnx_zynq_devcfg_regs_info,
                              ARRAY_SIZE(xlnx_zynq_devcfg_regs_info),
                              s->regs_info, s->regs,
                              &xlnx_zynq_devcfg_reg_ops,
                              XLNX_ZYNQ_DEVCFG_ERR_DEBUG,
                              XLNX_ZYNQ_DEVCFG_R_MAX);
    memory_region_add_subregion(&s->iomem,
                                A_CTRL,
                                &reg_array->mem);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void xlnx_zynq_devcfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, xlnx_zynq_devcfg_reset);
    dc->vmsd = &vmstate_xlnx_zynq_devcfg;
}

static const TypeInfo xlnx_zynq_devcfg_info = {
    .name           = TYPE_XLNX_ZYNQ_DEVCFG,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(XlnxZynqDevcfg),
    .instance_init  = xlnx_zynq_devcfg_init,
    .class_init     = xlnx_zynq_devcfg_class_init,
};

static void xlnx_zynq_devcfg_register_types(void)
{
    type_register_static(&xlnx_zynq_devcfg_info);
}

type_init(xlnx_zynq_devcfg_register_types)
