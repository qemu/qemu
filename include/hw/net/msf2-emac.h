/*
 * QEMU model of the Smartfusion2 Ethernet MAC.
 *
 * Copyright (c) 2020 Subbaraya Sundeep <sundeep.lkml@gmail.com>.
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

#include "hw/sysbus.h"
#include "system/memory.h"
#include "net/net.h"
#include "net/eth.h"
#include "qom/object.h"

#define TYPE_MSS_EMAC "msf2-emac"
OBJECT_DECLARE_SIMPLE_TYPE(MSF2EmacState, MSS_EMAC)

#define R_MAX         (0x1a0 / 4)
#define PHY_MAX_REGS  32

struct MSF2EmacState {
    SysBusDevice parent;

    MemoryRegion mmio;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;

    qemu_irq irq;
    NICState *nic;
    NICConf conf;

    uint8_t mac_addr[ETH_ALEN];
    uint32_t rx_desc;
    uint16_t phy_regs[PHY_MAX_REGS];

    uint32_t regs[R_MAX];
};
