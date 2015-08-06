/*
 * Copyright (c) 2009 Laurent Vivier
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
#ifndef HW_MAC_DBDMA_H
#define HW_MAC_DBDMA_H 1

#include "exec/memory.h"

typedef struct DBDMA_io DBDMA_io;

typedef void (*DBDMA_flush)(DBDMA_io *io);
typedef void (*DBDMA_rw)(DBDMA_io *io);
typedef void (*DBDMA_end)(DBDMA_io *io);
struct DBDMA_io {
    void *opaque;
    void *channel;
    hwaddr addr;
    int len;
    int is_last;
    int is_dma_out;
    DBDMA_end dma_end;
    /* DMA is in progress, don't start another one */
    bool processing;
    /* unaligned last sector of a request */
    uint8_t head_remainder[0x200];
    uint8_t tail_remainder[0x200];
    QEMUIOVector iov;
};

/*
 * DBDMA control/status registers.  All little-endian.
 */

#define DBDMA_CONTROL         0x00
#define DBDMA_STATUS          0x01
#define DBDMA_CMDPTR_HI       0x02
#define DBDMA_CMDPTR_LO       0x03
#define DBDMA_INTR_SEL        0x04
#define DBDMA_BRANCH_SEL      0x05
#define DBDMA_WAIT_SEL        0x06
#define DBDMA_XFER_MODE       0x07
#define DBDMA_DATA2PTR_HI     0x08
#define DBDMA_DATA2PTR_LO     0x09
#define DBDMA_RES1            0x0A
#define DBDMA_ADDRESS_HI      0x0B
#define DBDMA_BRANCH_ADDR_HI  0x0C
#define DBDMA_RES2            0x0D
#define DBDMA_RES3            0x0E
#define DBDMA_RES4            0x0F

#define DBDMA_REGS            16
#define DBDMA_SIZE            (DBDMA_REGS * sizeof(uint32_t))

#define DBDMA_CHANNEL_SHIFT   7
#define DBDMA_CHANNEL_SIZE    (1 << DBDMA_CHANNEL_SHIFT)

#define DBDMA_CHANNELS        (0x1000 >> DBDMA_CHANNEL_SHIFT)

/* Bits in control and status registers */

#define RUN        0x8000
#define PAUSE      0x4000
#define FLUSH      0x2000
#define WAKE       0x1000
#define DEAD       0x0800
#define ACTIVE     0x0400
#define BT         0x0100
#define DEVSTAT    0x00ff

/*
 * DBDMA command structure.  These fields are all little-endian!
 */

typedef struct dbdma_cmd {
    uint16_t req_count;          /* requested byte transfer count */
    uint16_t command;            /* command word (has bit-fields) */
    uint32_t phy_addr;           /* physical data address */
    uint32_t cmd_dep;            /* command-dependent field */
    uint16_t res_count;          /* residual count after completion */
    uint16_t xfer_status;        /* transfer status */
} dbdma_cmd;

/* DBDMA command values in command field */

#define COMMAND_MASK    0xf000
#define OUTPUT_MORE     0x0000        /* transfer memory data to stream */
#define OUTPUT_LAST     0x1000        /* ditto followed by end marker */
#define INPUT_MORE      0x2000        /* transfer stream data to memory */
#define INPUT_LAST      0x3000        /* ditto, expect end marker */
#define STORE_WORD      0x4000        /* write word (4 bytes) to device reg */
#define LOAD_WORD       0x5000        /* read word (4 bytes) from device reg */
#define DBDMA_NOP       0x6000        /* do nothing */
#define DBDMA_STOP      0x7000        /* suspend processing */

/* Key values in command field */

#define KEY_MASK        0x0700
#define KEY_STREAM0     0x0000        /* usual data stream */
#define KEY_STREAM1     0x0100        /* control/status stream */
#define KEY_STREAM2     0x0200        /* device-dependent stream */
#define KEY_STREAM3     0x0300        /* device-dependent stream */
#define KEY_STREAM4     0x0400        /* reserved */
#define KEY_REGS        0x0500        /* device register space */
#define KEY_SYSTEM      0x0600        /* system memory-mapped space */
#define KEY_DEVICE      0x0700        /* device memory-mapped space */

/* Interrupt control values in command field */

#define INTR_MASK       0x0030
#define INTR_NEVER      0x0000        /* don't interrupt */
#define INTR_IFSET      0x0010        /* intr if condition bit is 1 */
#define INTR_IFCLR      0x0020        /* intr if condition bit is 0 */
#define INTR_ALWAYS     0x0030        /* always interrupt */

/* Branch control values in command field */

#define BR_MASK         0x000c
#define BR_NEVER        0x0000        /* don't branch */
#define BR_IFSET        0x0004        /* branch if condition bit is 1 */
#define BR_IFCLR        0x0008        /* branch if condition bit is 0 */
#define BR_ALWAYS       0x000c        /* always branch */

/* Wait control values in command field */

#define WAIT_MASK       0x0003
#define WAIT_NEVER      0x0000        /* don't wait */
#define WAIT_IFSET      0x0001        /* wait if condition bit is 1 */
#define WAIT_IFCLR      0x0002        /* wait if condition bit is 0 */
#define WAIT_ALWAYS     0x0003        /* always wait */

typedef struct DBDMA_channel {
    int channel;
    uint32_t regs[DBDMA_REGS];
    qemu_irq irq;
    DBDMA_io io;
    DBDMA_rw rw;
    DBDMA_flush flush;
    dbdma_cmd current;
} DBDMA_channel;

typedef struct {
    MemoryRegion mem;
    DBDMA_channel channels[DBDMA_CHANNELS];
    QEMUBH *bh;
} DBDMAState;

/* Externally callable functions */

void DBDMA_register_channel(void *dbdma, int nchan, qemu_irq irq,
                            DBDMA_rw rw, DBDMA_flush flush,
                            void *opaque);
void DBDMA_kick(DBDMAState *dbdma);
void* DBDMA_init (MemoryRegion **dbdma_mem);

#endif
