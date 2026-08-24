/*
 * K230 Decompress Engine
 *
 * Copyright (c) 2026 Tao Ding <dingtao0430@163.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_K230_DECOMP_GZIP_H
#define HW_MISC_K230_DECOMP_GZIP_H

#include <zlib.h>
#include "hw/core/sysbus.h"

#define TYPE_K230_DECOMP_GZIP "riscv.k230.decomp-gzip"
OBJECT_DECLARE_SIMPLE_TYPE(K230DecompGzipState, K230_DECOMP_GZIP)

/* K230 DECOMP GZIP ACK input */
enum {
    K230_DECOMP_GZIP_GPIO_DMA_WRITE_ACK,
    K230_DECOMP_GZIP_GPIO_DMA_READ_ACK,
    K230_DECOMP_GZIP_NUM_GPIOS_IN,
};

/* K230 DECOMP GZIP REQ output */
enum {
    K230_DECOMP_GZIP_GPIO_DECOMP_CTRL_EN,
    K230_DECOMP_GZIP_GPIO_DMA_WRITE_REQ,
    K230_DECOMP_GZIP_GPIO_DMA_READ_REQ,
    K230_DECOMP_GZIP_NUM_GPIOS_OUT,
};

#define K230_DECOMP_GZIP_MMIO_SIZE       0x4000

/* K230 DECOMP GZIP Registers map */
/* Start decompression controller */
#define K230_DECOMP_GZIP_DECOMP_START    0x00
/* Source data length register */
#define K230_DECOMP_GZIP_GZIP_SRC_SIZE   0x04
/* Output data length register */
#define K230_DECOMP_GZIP_GZIP_OUT_SIZE   0x08
/* Decompress status register */
#define K230_DECOMP_GZIP_DECOMP_STAT     0x0c

/* Start decompression controller */
#define K230_DECOMP_GZIP_START           (1U << 0)

#define K230_DECOMP_GZIP_CTRL_EN         (1U << 31)
#define K230_DECOMP_GZIP_DMA_IN_MASK     0x7fffffffU

#define K230_DECOMP_GZIP_STAT_CRC_OK     (1U << 10)
#define K230_DECOMP_GZIP_STAT_STATE_MASK 0xfU

#define K230_DECOMP_GZIP_BLOCK_SIZE      0x00020000
#define K230_DECOMP_GZIP_SRAM_IN_BASE    0x80000
#define K230_DECOMP_GZIP_SRAM_OUT_BASE   0

typedef struct K230DecompGzipSlotState {
    uint32_t slot;  /* Slot index */
    /*
     * Data offset in current slot.
     * First valid data in input buffer. Last valid data in output buffer.
     */
    uint32_t current_offset;
} K230DecompGzipSlotState;

struct K230DecompGzipState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    hwaddr sram_base;
    qemu_irq signal_out[K230_DECOMP_GZIP_NUM_GPIOS_OUT];
    uint32_t decomp_start;
    uint32_t gzip_src_size;
    uint32_t gzip_out_size;
    uint32_t decomp_stat;
    K230DecompGzipSlotState input;  /* decomp gzip ring input */
    K230DecompGzipSlotState output; /* decomp gzip ring output */
    uint32_t total_requested;
    uint32_t total_produced;
    bool active;
    bool in_kick;
    bool zstream_inited;
    bool stream_end;
    /* Read from ring input. */
    uint8_t input_buf[K230_DECOMP_GZIP_BLOCK_SIZE];
    /* Write to ring output. */
    uint8_t output_buf[K230_DECOMP_GZIP_BLOCK_SIZE];
    z_stream zs;
};

#endif
