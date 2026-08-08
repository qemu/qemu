/*
 * K230 GSDMA
 *
 * Copyright (c) 2026 Tao Ding <dingtao0430@163.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DMA_K230_GSDMA_H
#define HW_DMA_K230_GSDMA_H

#include "hw/core/sysbus.h"

#define TYPE_K230_GSDMA "riscv.k230.gsdma"
OBJECT_DECLARE_SIMPLE_TYPE(K230GSDMAState, K230_GSDMA)

#define K230_GSDMA_MMIO_SIZE          0x4000
#define K230_GSDMA_NUM_SDMA_CHANNELS  4

#define K230_GSDMA_CH_STRIDE          0x30
#define K230_GSDMA_CH_BASE            0x50

/* K230 SDMA request input */
enum {
    K230_GSDMA_GPIO_DECOMP_CTRL_EN,
    K230_GSDMA_GPIO_DMA_WRITE_REQ,
    K230_GSDMA_GPIO_DMA_READ_REQ,
    K230_GSDMA_NUM_GPIOS_IN,
};

/* K230 SDMA ACK output */
enum {
    K230_GSDMA_GPIO_DMA_WRITE_ACK,
    K230_GSDMA_GPIO_DMA_READ_ACK,
    K230_GSDMA_NUM_GPIOS_OUT,
};

/* K230 GSDMA Registers Map */
#define K230_GSDMA_DMA_CH_EN          0x00
#define K230_GSDMA_DMA_INT_MASK       0x04
#define K230_GSDMA_DMA_INT_STAT       0x08
#define K230_GSDMA_DMA_CFG            0x0c
#define K230_GSDMA_GDMA_CTRL          0x10
#define K230_GSDMA_GDMA_LLI_BASE      0x14
#define K230_GSDMA_GDMA_CH_CNT0       0x18
#define K230_GSDMA_GDMA_CH_CNT_LAST   0x34
#define K230_GSDMA_GDMA_CURRENT_LLT   0x38
#define K230_GSDMA_DMA_WEIGHT         0x48

/* K230 GSDMA SDMA Channel Register Map */
#define K230_GSDMA_CH_CTL             0x00
#define K230_GSDMA_CH_STATUS          0x04
#define K230_GSDMA_CH_CFG             0x08
#define K230_GSDMA_CH_USR_DATA        0x0c
#define K230_GSDMA_CH_LLT_SADDR       0x10
#define K230_GSDMA_CH_CURRENT_LLT     0x14

/* K230 SDMA Control bit */
#define K230_GSDMA_CTL_START          BIT(0)
#define K230_GSDMA_CTL_STOP           BIT(1)
#define K230_GSDMA_CTL_RESUME         BIT(2)

/* K230 SDMA States bit */
#define K230_GSDMA_SDMA_STATUS_BUSY   BIT(0)
#define K230_GSDMA_SDMA_STATUS_PAUSE  BIT(1)

/* K230 SDMA Interrupt bit */
#define K230_GSDMA_SDMA_DONE_INT(ch)  BIT(ch)
#define K230_GSDMA_SDMA_ITEM_INT(ch)  BIT((ch) + 4)
#define K230_GSDMA_SDMA_PAUSE_INT(ch) BIT((ch) + 8)


#define K230_GSDMA_DMA_CH_EN_MASK     MAKE_64BIT_MASK(0, 5)
#define K230_GSDMA_DMA_CFG_RESET      0x000007ff

/* K230 SDMA Linked List bit */
#define K230_GSDMA_LLT_2D_MODE        BIT(28)
#define K230_GSDMA_LLT_PAUSE          BIT(29)
#define K230_GSDMA_LLT_NODE_INTR      BIT(30)
#define K230_GSDMA_GDMA_LLT_GCH_SHIFT 16
#define K230_GSDMA_GDMA_LLT_GCH_MASK  0x7
#define K230_GSDMA_CH_CFG_DAT_MODE    BIT(0)
#define K230_GSDMA_CH_CFG_USR_DATA_SIZE_SHIFT 1
#define K230_GSDMA_CH_CFG_USR_DATA_SIZE_MASK  0x3
#define K230_GSDMA_CH_CFG_DAT_ENDIAN_SHIFT 4
#define K230_GSDMA_CH_CFG_DAT_ENDIAN_MASK  0x3
#define K230_GSDMA_CH_CFG_SRC_FIXED   BIT(8)
#define K230_GSDMA_CH_CFG_DST_FIXED   BIT(9)
#define K230_GSDMA_CH0_CFG_DECOMP_CTRL_EN BIT(10)

/* K230 SDMA channel state */
typedef struct K230GSDMAChannel {
    uint32_t ctl;
    uint32_t status;
    uint32_t cfg;
    uint32_t usr_data;
    uint32_t llt_saddr;
    uint32_t current_llt;
    uint32_t next_llt;
    bool started;
} K230GSDMAChannel;

/* K230 SDMA Linked List */
typedef struct K230GSDMALLT {
    uint32_t cfg;
    uint32_t src_addr;
    uint32_t line_size;
    uint32_t line_cfg;
    uint32_t dst_addr;
    uint32_t next_llt_addr;
} K230GSDMALLT;

/* K230 GSDMA state */
struct K230GSDMAState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq handshake_out[K230_GSDMA_NUM_GPIOS_OUT];

    uint32_t dma_ch_en;
    uint32_t dma_int_mask;
    uint32_t dma_int_stat;
    uint32_t dma_cfg;
    uint32_t dma_weight;

    K230GSDMAChannel channels[K230_GSDMA_NUM_SDMA_CHANNELS];
};

#endif
