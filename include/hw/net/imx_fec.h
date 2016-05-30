/*
 * i.MX Fast Ethernet Controller emulation.
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

#define TYPE_IMX_FEC "imx.fec"
#define IMX_FEC(obj) OBJECT_CHECK(IMXFECState, (obj), TYPE_IMX_FEC)

#include "hw/sysbus.h"
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
#define ENET_RDSR              96
#define ENET_TDSR              97
#define ENET_MRBR              98
#define ENET_MIIGSK_CFGR       192
#define ENET_MIIGSK_ENR        194
#define ENET_MAX               400

#define ENET_MAX_FRAME_SIZE    2032

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

/* RDAR */
#define ENET_RDAR_RDAR         (1 << 24)

/* TDAR */
#define ENET_TDAR_TDAR         (1 << 24)

#define ENET_ECR_RESET         (1 << 0)
#define ENET_ECR_ETHEREN       (1 << 1)

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

typedef struct IMXFECState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    NICState *nic;
    NICConf conf;
    qemu_irq irq;
    MemoryRegion iomem;

    uint32_t regs[ENET_MAX];
    uint32_t rx_descriptor;
    uint32_t tx_descriptor;

    uint32_t phy_status;
    uint32_t phy_control;
    uint32_t phy_advertise;
    uint32_t phy_int;
    uint32_t phy_int_mask;
} IMXFECState;

#endif
