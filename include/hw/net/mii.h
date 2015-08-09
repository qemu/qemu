/*
 * Common network MII address and register definitions.
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * Allwinner EMAC register definitions from Linux kernel are:
 *   Copyright 2012 Stefan Roese <sr@denx.de>
 *   Copyright 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 *   Copyright 1997 Sten Wang
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#ifndef MII_H
#define MII_H

/* PHY registers */
#define MII_BMCR            0
#define MII_BMSR            1
#define MII_PHYID1          2
#define MII_PHYID2          3
#define MII_ANAR            4
#define MII_ANLPAR          5
#define MII_ANER            6
#define MII_NSR             16
#define MII_LBREMR          17
#define MII_REC             18
#define MII_SNRDR           19
#define MII_TEST            25

/* PHY registers fields */
#define MII_BMCR_RESET      (1 << 15)
#define MII_BMCR_LOOPBACK   (1 << 14)
#define MII_BMCR_SPEED      (1 << 13)
#define MII_BMCR_AUTOEN     (1 << 12)
#define MII_BMCR_FD         (1 << 8)

#define MII_BMSR_100TX_FD   (1 << 14)
#define MII_BMSR_100TX_HD   (1 << 13)
#define MII_BMSR_10T_FD     (1 << 12)
#define MII_BMSR_10T_HD     (1 << 11)
#define MII_BMSR_MFPS       (1 << 6)
#define MII_BMSR_AN_COMP    (1 << 5)
#define MII_BMSR_AUTONEG    (1 << 3)
#define MII_BMSR_LINK_ST    (1 << 2)

#define MII_ANAR_TXFD       (1 << 8)
#define MII_ANAR_TX         (1 << 7)
#define MII_ANAR_10FD       (1 << 6)
#define MII_ANAR_10         (1 << 5)
#define MII_ANAR_CSMACD     (1 << 0)

#define MII_ANLPAR_ACK      (1 << 14)
#define MII_ANLPAR_TXFD     (1 << 8)
#define MII_ANLPAR_TX       (1 << 7)
#define MII_ANLPAR_10FD     (1 << 6)
#define MII_ANLPAR_10       (1 << 5)
#define MII_ANLPAR_CSMACD   (1 << 0)

/* List of vendor identifiers */
/* RealTek 8201 */
#define RTL8201CP_PHYID1    0x0000
#define RTL8201CP_PHYID2    0x8201

/* National Semiconductor DP83848 */
#define DP83848_PHYID1      0x2000
#define DP83848_PHYID2      0x5c90

#endif /* MII_H */
