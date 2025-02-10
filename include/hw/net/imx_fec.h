/*
 * i.MX FEC/ENET Ethernet Controller emulation.
 *
 * Copyright (c) 2013 Jean-Christophe Dubois. <jcd@tribudubois.net>
 *
 * Based on Coldfire Fast Ethernet Controller emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IMX_FEC_H
#define IMX_FEC_H
#include "qom/object.h"

#define TYPE_IMX_FEC "imx.fec"
OBJECT_DECLARE_SIMPLE_TYPE(IMXFECState, IMX_FEC)

#define TYPE_IMX_ENET "imx.enet"

#include "hw/sysbus.h"
#include "hw/net/lan9118_phy.h"
#include "hw/irq.h"
#include "net/net.h"

#define ENET_EIR               1
#define ENET_EIMR              2
#define ENET_RDAR              4
#define ENET_TDAR              5
#define ENET_ECR               9
#define ENET_MMFR              16
#define ENET_MSCR              17
#define ENET_MIBC              25
#define ENET_RCR               33
#define ENET_TCR               49
#define ENET_PALR              57
#define ENET_PAUR              58
#define ENET_OPD               59
#define ENET_IAUR              70
#define ENET_IALR              71
#define ENET_GAUR              72
#define ENET_GALR              73
#define ENET_TFWR              81
#define ENET_FRBR              83
#define ENET_FRSR              84
#define ENET_TDSR1             89
#define ENET_TDSR2             92
#define ENET_RDSR              96
#define ENET_TDSR              97
#define ENET_MRBR              98
#define ENET_RSFL              100
#define ENET_RSEM              101
#define ENET_RAEM              102
#define ENET_RAFL              103
#define ENET_TSEM              104
#define ENET_TAEM              105
#define ENET_TAFL              106
#define ENET_TIPG              107
#define ENET_FTRL              108
#define ENET_TACC              112
#define ENET_RACC              113
#define ENET_TDAR1             121
#define ENET_TDAR2             123
#define ENET_MIIGSK_CFGR       192
#define ENET_MIIGSK_ENR        194
#define ENET_ATCR              256
#define ENET_ATVR              257
#define ENET_ATOFF             258
#define ENET_ATPER             259
#define ENET_ATCOR             260
#define ENET_ATINC             261
#define ENET_ATSTMP            262
#define ENET_TGSR              385
#define ENET_TCSR0             386
#define ENET_TCCR0             387
#define ENET_TCSR1             388
#define ENET_TCCR1             389
#define ENET_TCSR2             390
#define ENET_TCCR2             391
#define ENET_TCSR3             392
#define ENET_TCCR3             393
#define ENET_MAX               400


/* EIR and EIMR */
#define ENET_INT_HB            (1 << 31)
#define ENET_INT_BABR          (1 << 30)
#define ENET_INT_BABT          (1 << 29)
#define ENET_INT_GRA           (1 << 28)
#define ENET_INT_TXF           (1 << 27)
#define ENET_INT_TXB           (1 << 26)
#define ENET_INT_RXF           (1 << 25)
#define ENET_INT_RXB           (1 << 24)
#define ENET_INT_MII           (1 << 23)
#define ENET_INT_EBERR         (1 << 22)
#define ENET_INT_LC            (1 << 21)
#define ENET_INT_RL            (1 << 20)
#define ENET_INT_UN            (1 << 19)
#define ENET_INT_PLR           (1 << 18)
#define ENET_INT_WAKEUP        (1 << 17)
#define ENET_INT_TS_AVAIL      (1 << 16)
#define ENET_INT_TS_TIMER      (1 << 15)
#define ENET_INT_TXF2          (1 <<  7)
#define ENET_INT_TXB2          (1 <<  6)
#define ENET_INT_TXF1          (1 <<  3)
#define ENET_INT_TXB1          (1 <<  2)

#define ENET_INT_MAC           (ENET_INT_HB | ENET_INT_BABR | ENET_INT_BABT | \
                                ENET_INT_GRA | ENET_INT_TXF | ENET_INT_TXB | \
                                ENET_INT_RXF | ENET_INT_RXB | ENET_INT_MII | \
                                ENET_INT_EBERR | ENET_INT_LC | ENET_INT_RL | \
                                ENET_INT_UN | ENET_INT_PLR | ENET_INT_WAKEUP | \
                                ENET_INT_TS_AVAIL | ENET_INT_TXF1 | \
                                ENET_INT_TXB1 | ENET_INT_TXF2 | ENET_INT_TXB2)

/* RDAR */
#define ENET_RDAR_RDAR         (1 << 24)

/* TDAR */
#define ENET_TDAR_TDAR         (1 << 24)

/* ECR */
#define ENET_ECR_RESET         (1 << 0)
#define ENET_ECR_ETHEREN       (1 << 1)
#define ENET_ECR_MAGICEN       (1 << 2)
#define ENET_ECR_SLEEP         (1 << 3)
#define ENET_ECR_EN1588        (1 << 4)
#define ENET_ECR_SPEED         (1 << 5)
#define ENET_ECR_DBGEN         (1 << 6)
#define ENET_ECR_STOPEN        (1 << 7)
#define ENET_ECR_DSBWP         (1 << 8)

/* MIBC */
#define ENET_MIBC_MIB_DIS      (1 << 31)
#define ENET_MIBC_MIB_IDLE     (1 << 30)
#define ENET_MIBC_MIB_CLEAR    (1 << 29)

/* RCR */
#define ENET_RCR_LOOP          (1 << 0)
#define ENET_RCR_DRT           (1 << 1)
#define ENET_RCR_MII_MODE      (1 << 2)
#define ENET_RCR_PROM          (1 << 3)
#define ENET_RCR_BC_REJ        (1 << 4)
#define ENET_RCR_FCE           (1 << 5)
#define ENET_RCR_RGMII_EN      (1 << 6)
#define ENET_RCR_RMII_MODE     (1 << 8)
#define ENET_RCR_RMII_10T      (1 << 9)
#define ENET_RCR_PADEN         (1 << 12)
#define ENET_RCR_PAUFWD        (1 << 13)
#define ENET_RCR_CRCFWD        (1 << 14)
#define ENET_RCR_CFEN          (1 << 15)
#define ENET_RCR_MAX_FL_SHIFT  (16)
#define ENET_RCR_MAX_FL_LENGTH (14)
#define ENET_RCR_NLC           (1 << 30)
#define ENET_RCR_GRS           (1 << 31)

#define ENET_MAX_FRAME_SIZE    (1 << ENET_RCR_MAX_FL_LENGTH)

/* TCR */
#define ENET_TCR_GTS           (1 << 0)
#define ENET_TCR_FDEN          (1 << 2)
#define ENET_TCR_TFC_PAUSE     (1 << 3)
#define ENET_TCR_RFC_PAUSE     (1 << 4)
#define ENET_TCR_ADDSEL_SHIFT  (5)
#define ENET_TCR_ADDSEL_LENGTH (3)
#define ENET_TCR_CRCFWD        (1 << 9)

/* RDSR */
#define ENET_TWFR_TFWR_SHIFT   (0)
#define ENET_TWFR_TFWR_LENGTH  (6)
#define ENET_TWFR_STRFWD       (1 << 8)

#define ENET_RACC_SHIFT16      BIT(7)

/* Buffer Descriptor.  */
typedef struct {
    uint16_t length;
    uint16_t flags;
    uint32_t data;
} IMXFECBufDesc;

#define ENET_BD_R              (1 << 15)
#define ENET_BD_E              (1 << 15)
#define ENET_BD_O1             (1 << 14)
#define ENET_BD_W              (1 << 13)
#define ENET_BD_O2             (1 << 12)
#define ENET_BD_L              (1 << 11)
#define ENET_BD_TC             (1 << 10)
#define ENET_BD_ABC            (1 << 9)
#define ENET_BD_M              (1 << 8)
#define ENET_BD_BC             (1 << 7)
#define ENET_BD_MC             (1 << 6)
#define ENET_BD_LG             (1 << 5)
#define ENET_BD_NO             (1 << 4)
#define ENET_BD_CR             (1 << 2)
#define ENET_BD_OV             (1 << 1)
#define ENET_BD_TR             (1 << 0)

typedef struct {
    uint16_t length;
    uint16_t flags;
    uint32_t data;
    uint16_t status;
    uint16_t option;
    uint16_t checksum;
    uint16_t head_proto;
    uint32_t last_buffer;
    uint32_t timestamp;
    uint32_t reserved[2];
} IMXENETBufDesc;

#define ENET_BD_ME             (1 << 15)
#define ENET_BD_TX_INT         (1 << 14)
#define ENET_BD_TS             (1 << 13)
#define ENET_BD_PINS           (1 << 12)
#define ENET_BD_IINS           (1 << 11)
#define ENET_BD_PE             (1 << 10)
#define ENET_BD_CE             (1 << 9)
#define ENET_BD_UC             (1 << 8)
#define ENET_BD_RX_INT         (1 << 7)

#define ENET_BD_TXE            (1 << 15)
#define ENET_BD_UE             (1 << 13)
#define ENET_BD_EE             (1 << 12)
#define ENET_BD_FE             (1 << 11)
#define ENET_BD_LCE            (1 << 10)
#define ENET_BD_OE             (1 << 9)
#define ENET_BD_TSE            (1 << 8)
#define ENET_BD_ICE            (1 << 5)
#define ENET_BD_PCR            (1 << 4)
#define ENET_BD_VLAN           (1 << 2)
#define ENET_BD_IPV6           (1 << 1)
#define ENET_BD_FRAG           (1 << 0)

#define ENET_BD_BDU            (1 << 31)

#define ENET_TX_RING_NUM       3

#define FSL_IMX25_FEC_SIZE      0x4000

struct IMXFECState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    NICState *nic;
    NICConf conf;
    qemu_irq irq[2];
    MemoryRegion iomem;

    uint32_t regs[ENET_MAX];
    uint32_t rx_descriptor;

    uint32_t tx_descriptor[ENET_TX_RING_NUM];
    uint32_t tx_ring_num;

    Lan9118PhyState mii;
    IRQState mii_irq;
    uint32_t phy_num;
    bool phy_connected;
    struct IMXFECState *phy_consumer;

    bool is_fec;

    /* Buffer used to assemble a Tx frame */
    uint8_t frame[ENET_MAX_FRAME_SIZE];
};

#endif
