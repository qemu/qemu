/*
 * BCM2838 Gigabit Ethernet emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "net/eth.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "net/checksum.h"
#include "system/dma.h"
#include "hw/core/registerfields.h"
#include "hw/core/resettable.h"
#include "hw/net/bcm2838_genet.h"
#include "trace.h"

/* GENET layouts */
REG32(GENET_SYS_REV_CTRL,              0)
FIELD(GENET_SYS_REV_CTRL, GPHY_REV,    0, 16)
FIELD(GENET_SYS_REV_CTRL, MINOR_REV,   16, 4)
FIELD(GENET_SYS_REV_CTRL, RSVD_20_23,  20, 4)
FIELD(GENET_SYS_REV_CTRL, MAJOR_REV,   24, 4)
FIELD(GENET_SYS_REV_CTRL, RSVD_28_31,  28, 4)

REG32(GENET_INTRL_0,                0)
FIELD(GENET_INTRL_0, SCB,           0, 1)
FIELD(GENET_INTRL_0, EPHY,          1, 1)
FIELD(GENET_INTRL_0, PHY_DET_R,     2, 1)
FIELD(GENET_INTRL_0, PHY_DET_F,     3, 1)
FIELD(GENET_INTRL_0, LINK_UP,       4, 1)
FIELD(GENET_INTRL_0, LINK_DOWN,     5, 1)
FIELD(GENET_INTRL_0, UMAC,          6, 1)
FIELD(GENET_INTRL_0, UMAC_TSV,      7, 1)
FIELD(GENET_INTRL_0, TBUF_UNDERRUN, 8, 1)
FIELD(GENET_INTRL_0, RBUF_OVERFLOW, 9, 1)
FIELD(GENET_INTRL_0, HFB_SM,        10, 1)
FIELD(GENET_INTRL_0, HFB_MM,        11, 1)
FIELD(GENET_INTRL_0, MPD_R,         12, 1)
FIELD(GENET_INTRL_0, RXDMA_MBDONE,  13, 1)
FIELD(GENET_INTRL_0, RXDMA_PDONE,   14, 1)
FIELD(GENET_INTRL_0, RXDMA_BDONE,   15, 1)
FIELD(GENET_INTRL_0, TXDMA_MBDONE,  16, 1)
FIELD(GENET_INTRL_0, TXDMA_PDONE,   17, 1)
FIELD(GENET_INTRL_0, TXDMA_BDONE,   18, 1)
FIELD(GENET_INTRL_0, RSVD_19_22,    19, 4)
FIELD(GENET_INTRL_0, MDIO_DONE,     23, 1)
FIELD(GENET_INTRL_0, MDIO_ERROR,    24, 1)
FIELD(GENET_INTRL_0, RSVD_25_31,    25, 4)

REG32(GENET_INTRL_1,              0)
FIELD(GENET_INTRL_1, TX_INTRS,    0, 16)
FIELD(GENET_INTRL_1, RX_INTRS,    16, 16)

REG32(GENET_UMAC_CMD,                  0)
FIELD(GENET_UMAC_CMD, TX_EN,           0, 1)
FIELD(GENET_UMAC_CMD, RX_EN,           1, 1)
FIELD(GENET_UMAC_CMD, SPEED,           2, 2)
FIELD(GENET_UMAC_CMD, PROMISC,         4, 1)
FIELD(GENET_UMAC_CMD, PAD_EN,          5, 1)
FIELD(GENET_UMAC_CMD, CRC_FWD,         6, 1)
FIELD(GENET_UMAC_CMD, PAUSE_FWD,       7, 1)
FIELD(GENET_UMAC_CMD, RX_PAUSE_IGNORE, 8, 1)
FIELD(GENET_UMAC_CMD, TX_ADDR_INS,     9, 1)
FIELD(GENET_UMAC_CMD, HD_EN,           10, 1)
FIELD(GENET_UMAC_CMD, SW_RESET_OLD,    11, 1)
FIELD(GENET_UMAC_CMD, RSVD_12,         12, 1)
FIELD(GENET_UMAC_CMD, SW_RESET,        13, 1)
FIELD(GENET_UMAC_CMD, RSVD_14,         14, 1)
FIELD(GENET_UMAC_CMD, LCL_LOOP_EN,     15, 1)
FIELD(GENET_UMAC_CMD, RSVD_16_21,      16, 6)
FIELD(GENET_UMAC_CMD, AUTO_CONFIG,     22, 1)
FIELD(GENET_UMAC_CMD, CNTL_FRM_EN,     23, 1)
FIELD(GENET_UMAC_CMD, NO_LEN_CHK,      24, 1)
FIELD(GENET_UMAC_CMD, RMT_LOOP_EN,     25, 1)
FIELD(GENET_UMAC_CMD, RX_ERR_DISC,     26, 1)
FIELD(GENET_UMAC_CMD, PRBL_EN,         27, 1)
FIELD(GENET_UMAC_CMD, TX_PAUSE_IGNORE, 28, 1)
FIELD(GENET_UMAC_CMD, TX_RX_EN,        29, 1)
FIELD(GENET_UMAC_CMD, RUNT_FILTER_DIS, 30, 1)
FIELD(GENET_UMAC_CMD, RSVD_31,         31, 1)

REG32(GENET_UMAC_MAC_0,         0)
FIELD(GENET_UMAC_MAC_0, ADDR_3, 0,  8)
FIELD(GENET_UMAC_MAC_0, ADDR_2, 8,  8)
FIELD(GENET_UMAC_MAC_0, ADDR_1, 16, 8)
FIELD(GENET_UMAC_MAC_0, ADDR_0, 24, 8)

REG32(GENET_UMAC_MAC_1,             0)
FIELD(GENET_UMAC_MAC_1, ADDR_5,     0,  8)
FIELD(GENET_UMAC_MAC_1, ADDR_4,     8,  8)
FIELD(GENET_UMAC_MAC_1, RSVD_16_31, 16, 8)

REG32(GENET_UMAC_MDIO_CMD,             0)
FIELD(GENET_UMAC_MDIO_CMD, REG_DATA,   0, 16)
FIELD(GENET_UMAC_MDIO_CMD, REG_ID,     16, 5)
FIELD(GENET_UMAC_MDIO_CMD, PHY_ID,     21, 5)
FIELD(GENET_UMAC_MDIO_CMD, WR,         26, 1)
FIELD(GENET_UMAC_MDIO_CMD, RD,         27, 1)
FIELD(GENET_UMAC_MDIO_CMD, RD_FAIL,    28, 1)
FIELD(GENET_UMAC_MDIO_CMD, START_BUSY, 29, 1)
FIELD(GENET_UMAC_MDIO_CMD, RSVD_30_31, 30, 2)

REG32(GENET_DMA_RING_CFG,             0)
FIELD(GENET_DMA_RING_CFG, EN,         0, 17)
FIELD(GENET_DMA_RING_CFG, RSVD_17_31, 17, 14)

REG32(GENET_DMA_CTRL,              0)
FIELD(GENET_DMA_CTRL, EN,          0, 1)
FIELD(GENET_DMA_CTRL, RING_BUF_EN, 1, 17)
FIELD(GENET_DMA_CTRL, RSVD_18_19,  18, 2)
FIELD(GENET_DMA_CTRL, TSB_SWAP_EN, 20, 1)
FIELD(GENET_DMA_CTRL, RSVD_21_31,  21, 11)

REG32(GENET_DMA_PROD_INDEX,              0)
FIELD(GENET_DMA_PROD_INDEX, INDEX,       0, 16)
FIELD(GENET_DMA_PROD_INDEX, DISCARD_CNT, 16, 16)

REG32(GENET_DMA_CONS_INDEX,             0)
FIELD(GENET_DMA_CONS_INDEX, INDEX,      0, 16)
FIELD(GENET_DMA_CONS_INDEX, RSVD_16_31, 16, 16)

REG32(GENET_DMA_STATUS,                     0)
FIELD(GENET_DMA_STATUS, DISABLED,           0, 1)
FIELD(GENET_DMA_STATUS, DESC_RAM_INIT_BUSY, 1, 1)
FIELD(GENET_DMA_STATUS, RSVD_2_31,          2, 30)

#define GENET_DMA_ENABLE_MASK ((1U << 18) - 1)

REG32(GENET_RDMA_LENGTH_STATUS,             0)
FIELD(GENET_RDMA_LENGTH_STATUS, OVERRUN,    0, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, CRC_ERROR,  1, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, RXERR,      2, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, NO,         3, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, LG,         4, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, MULTICAST,  5, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, BROADCAST,  6, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, RSVD_7_11,  7, 5)
FIELD(GENET_RDMA_LENGTH_STATUS, WRAP,       12, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, SOP,        13, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, EOP,        14, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, OWN,        15, 1)
FIELD(GENET_RDMA_LENGTH_STATUS, BUFLENGTH,  16, 12)
FIELD(GENET_RDMA_LENGTH_STATUS, RSVD_28_31, 29, 4)

REG32(GENET_TDMA_LENGTH_STATUS,             0)
FIELD(GENET_TDMA_LENGTH_STATUS, RSVD_0_3,   0, 4)
FIELD(GENET_TDMA_LENGTH_STATUS, DO_CSUM,    4, 1)
FIELD(GENET_TDMA_LENGTH_STATUS, OW_CRC,     5, 1)
FIELD(GENET_TDMA_LENGTH_STATUS, APPEND_CRC, 6, 1)
FIELD(GENET_TDMA_LENGTH_STATUS, RSVD_7_8,   7, 2)
FIELD(GENET_TDMA_LENGTH_STATUS, UNDERRUN,   9, 1)
FIELD(GENET_TDMA_LENGTH_STATUS, RSVD_10_11, 10, 2)
FIELD(GENET_TDMA_LENGTH_STATUS, WRAP,       12, 1)
FIELD(GENET_TDMA_LENGTH_STATUS, SOP,        13, 1)
FIELD(GENET_TDMA_LENGTH_STATUS, EOP,        14, 1)
FIELD(GENET_TDMA_LENGTH_STATUS, OWN,        15, 1)
FIELD(GENET_TDMA_LENGTH_STATUS, BUFLENGTH,  16, 12)
FIELD(GENET_TDMA_LENGTH_STATUS, RSVD_28_31, 29, 4)

REG16(GENET_PHY_BMCR,            0)
FIELD(GENET_PHY_BMCR, RSVD_0_5,  0, 6)
FIELD(GENET_PHY_BMCR, SPEED1000, 6, 1)
FIELD(GENET_PHY_BMCR, CTST,      7, 1)
FIELD(GENET_PHY_BMCR, FULLDPLX,  8, 1)
FIELD(GENET_PHY_BMCR, ANRESTART, 9, 1)
FIELD(GENET_PHY_BMCR, ISOLATE,   10, 1)
FIELD(GENET_PHY_BMCR, PDOWN,     11, 1)
FIELD(GENET_PHY_BMCR, AENABLE,   12, 1)
FIELD(GENET_PHY_BMCR, SPEED100,  13, 1)
FIELD(GENET_PHY_BMCR, LOOPBACK,  14, 1)
FIELD(GENET_PHY_BMCR, RESET,     15, 1)

REG16(GENET_PHY_BMSR,               0)
FIELD(GENET_PHY_BMSR, ERCAP,        0, 1)
FIELD(GENET_PHY_BMSR, JCD,          1, 1)
FIELD(GENET_PHY_BMSR, LSTATUS,      2, 1)
FIELD(GENET_PHY_BMSR, ANEGCAPABLE,  3, 1)
FIELD(GENET_PHY_BMSR, RFAULT,       4, 1)
FIELD(GENET_PHY_BMSR, ANEGCOMPLETE, 5, 1)
FIELD(GENET_PHY_BMSR, RSVD_6_7,     6, 2)
FIELD(GENET_PHY_BMSR, ESTATEN,      8, 1)
FIELD(GENET_PHY_BMSR, _100HALF2,    9, 1)
FIELD(GENET_PHY_BMSR, _100FULL2,    10, 1)
FIELD(GENET_PHY_BMSR, _10HALF,      11, 1)
FIELD(GENET_PHY_BMSR, _10FULL,      12, 1)
FIELD(GENET_PHY_BMSR, _100HALF,     13, 1)
FIELD(GENET_PHY_BMSR, _100FULL,     14, 1)
FIELD(GENET_PHY_BMSR, _10BASE4,     15, 1)

REG16(GENET_PHY_LPA,                           0)
FIELD(GENET_PHY_LPA, SLCT,                     0, 5)
FIELD(GENET_PHY_LPA, _10HALF_1000XFULL,        5, 1)
FIELD(GENET_PHY_LPA, _10FULL_1000XHALF,        6, 1)
FIELD(GENET_PHY_LPA, _100HALF_1000XPAUSE,      7, 1)
FIELD(GENET_PHY_LPA, _100FULL_1000XPAUSE_ASYM, 8, 1)
FIELD(GENET_PHY_LPA, _100BASE4,                9, 1)
FIELD(GENET_PHY_LPA, PAUSE_CAP,                10, 1)
FIELD(GENET_PHY_LPA, PAUSE_ASYM,               11, 1)
FIELD(GENET_PHY_LPA, RSVD_12,                  12, 1)
FIELD(GENET_PHY_LPA, RFAULT,                   13, 1)
FIELD(GENET_PHY_LPA, LPACK,                    14, 1)
FIELD(GENET_PHY_LPA, NPAGE,                    15, 1)

REG16(GENET_PHY_STAT_1000,            0)
FIELD(GENET_PHY_STAT_1000, RSVD_0_9,  0, 10)
FIELD(GENET_PHY_STAT_1000, HALF,      10, 1)
FIELD(GENET_PHY_STAT_1000, FULL,      11, 1)
FIELD(GENET_PHY_STAT_1000, REMRXOK,   12, 1)
FIELD(GENET_PHY_STAT_1000, LOCALRXOK, 13, 1)
FIELD(GENET_PHY_STAT_1000, MSRES,     14, 1)
FIELD(GENET_PHY_STAT_1000, MSFAIL,    15, 1)

REG16(GENET_PHY_AUX_CTRL_0,              0)
FIELD(GENET_PHY_AUX_CTRL_0, REG_ID_MASK, 0, 3)
FIELD(GENET_PHY_AUX_CTRL_0, RSVD_3,      3, 1)
FIELD(GENET_PHY_AUX_CTRL_0, REG_DATA,    4, 8)
FIELD(GENET_PHY_AUX_CTRL_0, REG_ID,      12, 3)
FIELD(GENET_PHY_AUX_CTRL_0, MISC_WREN,   15, 1)

REG16(GENET_PHY_AUX_CTRL_1,           0)
FIELD(GENET_PHY_AUX_CTRL_1, RSVD_0_3, 0, 4)
FIELD(GENET_PHY_AUX_CTRL_1, REG_DATA, 4, 12)

REG16(GENET_PHY_SHADOW,           0)
FIELD(GENET_PHY_SHADOW, REG_DATA, 0, 10)
FIELD(GENET_PHY_SHADOW, REG_ID,   10, 5)
FIELD(GENET_PHY_SHADOW, WR,       15, 1)

REG16(GENET_PHY_EXP_SEL,           0)
FIELD(GENET_PHY_EXP_SEL, REG_ID,   0, 8)
FIELD(GENET_PHY_EXP_SEL, BLOCK_ID, 8, 8)

static uint64_t bcm2838_genet_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t value = ~0;

    qemu_log_mask(
        LOG_GUEST_ERROR,
        "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
        __func__, size, offset);

    trace_bcm2838_genet_read(size, offset, value);
    return value;
}

static void bcm2838_genet_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size) {
    qemu_log_mask(
        LOG_GUEST_ERROR,
        "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
        __func__, size, offset);
}

static const MemoryRegionOps bcm2838_genet_ops = {
    .read = bcm2838_genet_read,
    .write = bcm2838_genet_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {.min_access_size = 4, .max_access_size = 4},
    .valid = {.min_access_size = 4, .max_access_size = 4},
};


static void bcm2838_genet_realize(DeviceState *dev, Error **errp)
{
    BCM2838GenetState *s = BCM2838_GENET(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Controller registers */
    memory_region_init_io(&s->regs_mr, OBJECT(s), &bcm2838_genet_ops, s,
                          "bcm2838_genet_regs", sizeof(s->regs));
    sysbus_init_mmio(sbd, &s->regs_mr);
}

static void bcm2838_genet_phy_reset(BCM2838GenetState *s)
{
    trace_bcm2838_genet_phy_reset("done");
}

static void bcm2838_genet_reset(Object *obj, ResetType type)
{
    BCM2838GenetState *s = BCM2838_GENET(obj);

    memset(&s->regs, 0x00, sizeof(s->regs));

    trace_bcm2838_genet_reset("done");

    bcm2838_genet_phy_reset(s);
}

static void bcm2838_genet_class_init(ObjectClass *class, const void *data)
{
    static ResettablePhases unused_parent_phases;
    DeviceClass *dc = DEVICE_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);

    dc->realize = bcm2838_genet_realize;
    resettable_class_set_parent_phases(rc, NULL, bcm2838_genet_reset, NULL,
                                       &unused_parent_phases);
}

static const TypeInfo bcm2838_genet_info = {
    .name       = TYPE_BCM2838_GENET,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2838GenetState),
    .class_init = bcm2838_genet_class_init,
};

static void bcm2838_genet_register(void)
{
    type_register_static(&bcm2838_genet_info);
}

type_init(bcm2838_genet_register)
