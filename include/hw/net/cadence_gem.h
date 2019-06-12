/*
 * QEMU Cadence GEM emulation
 *
 * Copyright (c) 2011 Xilinx, Inc.
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

#ifndef CADENCE_GEM_H
#define CADENCE_GEM_H

#define TYPE_CADENCE_GEM "cadence_gem"
#define CADENCE_GEM(obj) OBJECT_CHECK(CadenceGEMState, (obj), TYPE_CADENCE_GEM)

#include "net/net.h"
#include "hw/sysbus.h"

#define CADENCE_GEM_MAXREG        (0x00000800 / 4) /* Last valid GEM address */

/* Max number of words in a DMA descriptor.  */
#define DESC_MAX_NUM_WORDS              6

#define MAX_PRIORITY_QUEUES             8
#define MAX_TYPE1_SCREENERS             16
#define MAX_TYPE2_SCREENERS             16

typedef struct CadenceGEMState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    NICState *nic;
    NICConf conf;
    qemu_irq irq[MAX_PRIORITY_QUEUES];

    /* Static properties */
    uint8_t num_priority_queues;
    uint8_t num_type1_screeners;
    uint8_t num_type2_screeners;
    uint32_t revision;

    /* GEM registers backing store */
    uint32_t regs[CADENCE_GEM_MAXREG];
    /* Mask of register bits which are write only */
    uint32_t regs_wo[CADENCE_GEM_MAXREG];
    /* Mask of register bits which are read only */
    uint32_t regs_ro[CADENCE_GEM_MAXREG];
    /* Mask of register bits which are clear on read */
    uint32_t regs_rtc[CADENCE_GEM_MAXREG];
    /* Mask of register bits which are write 1 to clear */
    uint32_t regs_w1c[CADENCE_GEM_MAXREG];

    /* PHY registers backing store */
    uint16_t phy_regs[32];

    uint8_t phy_loop; /* Are we in phy loopback? */

    /* The current DMA descriptor pointers */
    uint32_t rx_desc_addr[MAX_PRIORITY_QUEUES];
    uint32_t tx_desc_addr[MAX_PRIORITY_QUEUES];

    uint8_t can_rx_state; /* Debug only */

    uint32_t rx_desc[MAX_PRIORITY_QUEUES][DESC_MAX_NUM_WORDS];

    bool sar_active[4];
} CadenceGEMState;

#endif
