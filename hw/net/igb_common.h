/*
 * QEMU igb emulation - shared definitions
 *
 * Copyright (c) 2020-2023 Red Hat, Inc.
 * Copyright (c) 2008 Qumranet
 *
 * Based on work done by:
 * Nir Peleg, Tutis Systems Ltd. for Qumranet Inc.
 * Copyright (c) 2007 Dan Aloni
 * Copyright (c) 2004 Antony T Curtis
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NET_IGB_COMMON_H
#define HW_NET_IGB_COMMON_H

#include "igb_regs.h"

#define defreg(x) x = (E1000_##x >> 2)
#define defreg_indexed(x, i) x##i = (E1000_##x(i) >> 2)
#define defreg_indexeda(x, i) x##i##_A = (E1000_##x##_A(i) >> 2)

#define defregd(x) defreg_indexed(x, 0),  defreg_indexed(x, 1),  \
                   defreg_indexed(x, 2),  defreg_indexed(x, 3),  \
                   defreg_indexed(x, 4),  defreg_indexed(x, 5),  \
                   defreg_indexed(x, 6),  defreg_indexed(x, 7),  \
                   defreg_indexed(x, 8),  defreg_indexed(x, 9),  \
                   defreg_indexed(x, 10), defreg_indexed(x, 11), \
                   defreg_indexed(x, 12), defreg_indexed(x, 13), \
                   defreg_indexed(x, 14), defreg_indexed(x, 15), \
                   defreg_indexeda(x, 0), defreg_indexeda(x, 1), \
                   defreg_indexeda(x, 2), defreg_indexeda(x, 3)

#define defregv(x) defreg_indexed(x, 0), defreg_indexed(x, 1),   \
                   defreg_indexed(x, 2), defreg_indexed(x, 3),   \
                   defreg_indexed(x, 4), defreg_indexed(x, 5),   \
                   defreg_indexed(x, 6), defreg_indexed(x, 7)

enum {
    defreg(CTRL),    defreg(EECD),    defreg(EERD),    defreg(GPRC),
    defreg(GPTC),    defreg(ICR),     defreg(ICS),     defreg(IMC),
    defreg(IMS),     defreg(LEDCTL),  defreg(MANC),    defreg(MDIC),
    defreg(MPC),     defreg(RCTL),
    defreg(STATUS),  defreg(SWSM),    defreg(TCTL),
    defreg(TORH),    defreg(TORL),    defreg(TOTH),
    defreg(TOTL),    defreg(TPR),     defreg(TPT),
    defreg(WUFC),    defreg(RA),      defreg(MTA),     defreg(CRCERRS),
    defreg(VFTA),    defreg(VET),
    defreg(SCC),     defreg(ECOL),
    defreg(MCC),     defreg(LATECOL), defreg(COLC),    defreg(DC),
    defreg(TNCRS),   defreg(RLEC),
    defreg(XONRXC),  defreg(XONTXC),  defreg(XOFFRXC), defreg(XOFFTXC),
    defreg(FCRUC),   defreg(TDFH),    defreg(TDFT),
    defreg(TDFHS),   defreg(TDFTS),   defreg(TDFPC),   defreg(WUC),
    defreg(WUS),     defreg(RDFH),
    defreg(RDFT),    defreg(RDFHS),   defreg(RDFTS),   defreg(RDFPC),
    defreg(IPAV),    defreg(IP4AT),   defreg(IP6AT),
    defreg(WUPM),    defreg(FFMT),
    defreg(IAM),
    defreg(GCR),     defreg(TIMINCA), defreg(EIAC),    defreg(CTRL_EXT),
    defreg(IVAR0),   defreg(MANC2H),
    defreg(MFVAL),   defreg(MDEF),    defreg(FACTPS),  defreg(FTFT),
    defreg(RUC),     defreg(ROC),     defreg(RFC),     defreg(RJC),
    defreg(PRC64),   defreg(PRC127),  defreg(PRC255),  defreg(PRC511),
    defreg(PRC1023), defreg(PRC1522), defreg(PTC64),   defreg(PTC127),
    defreg(PTC255),  defreg(PTC511),  defreg(PTC1023), defreg(PTC1522),
    defreg(GORCL),   defreg(GORCH),   defreg(GOTCL),   defreg(GOTCH),
    defreg(RNBC),    defreg(BPRC),    defreg(MPRC),    defreg(RFCTL),
    defreg(MPTC),    defreg(BPTC),
    defreg(IAC),     defreg(MGTPRC),  defreg(MGTPDC),  defreg(MGTPTC),
    defreg(TSCTC),   defreg(RXCSUM),  defreg(FUNCTAG), defreg(GSCL_1),
    defreg(GSCL_2),  defreg(GSCL_3),  defreg(GSCL_4),  defreg(GSCN_0),
    defreg(GSCN_1),  defreg(GSCN_2),  defreg(GSCN_3),
    defreg_indexed(EITR, 0),
    defreg(MRQC),    defreg(RETA),    defreg(RSSRK),
    defreg(PBACLR),  defreg(FCAL),    defreg(FCAH),    defreg(FCT),
    defreg(FCRTH),   defreg(FCRTL),   defreg(FCTTV),   defreg(FCRTV),
    defreg(FLA),     defreg(FLOP),
    defreg(MAVTV0),  defreg(MAVTV1),  defreg(MAVTV2),  defreg(MAVTV3),
    defreg(TXSTMPL), defreg(TXSTMPH), defreg(SYSTIML), defreg(SYSTIMH),
    defreg(TIMADJL), defreg(TIMADJH),
    defreg(RXSTMPH), defreg(RXSTMPL), defreg(RXSATRL), defreg(RXSATRH),
    defreg(TIPG),
    defreg(CTRL_DUP),
    defreg(EEMNGCTL),
    defreg(EEMNGDATA),
    defreg(FLMNGCTL),
    defreg(FLMNGDATA),
    defreg(FLMNGCNT),
    defreg(TSYNCRXCTL),
    defreg(TSYNCTXCTL),
    defreg(RLPML),
    defreg(UTA),

    /* Aliases */
    defreg(RDFH_A),      defreg(RDFT_A),     defreg(TDFH_A),     defreg(TDFT_A),
    defreg(RA_A),        defreg(VFTA_A),     defreg(FCRTL_A),

    /* Additional regs used by IGB */
    defreg(FWSM),        defreg(SW_FW_SYNC),

    defreg(EICS),        defreg(EIMS),        defreg(EIMC),       defreg(EIAM),
    defreg(EICR),        defreg(IVAR_MISC),   defreg(GPIE),

    defreg(RXPBS),      defregd(RDBAL),       defregd(RDBAH),     defregd(RDLEN),
    defregd(SRRCTL),    defregd(RDH),         defregd(RDT),
    defregd(RXDCTL),    defregd(RXCTL),       defregd(RQDPC),     defreg(RA2),

    defreg(TXPBS),       defreg(TCTL_EXT),    defreg(DTXCTL),     defreg(HTCBDPC),
    defregd(TDBAL),      defregd(TDBAH),      defregd(TDLEN),     defregd(TDH),
    defregd(TDT),        defregd(TXDCTL),     defregd(TXCTL),
    defregd(TDWBAL),     defregd(TDWBAH),

    defreg(VT_CTL),

    defregv(P2VMAILBOX), defregv(V2PMAILBOX), defreg(MBVFICR),    defreg(MBVFIMR),
    defreg(VFLRE),       defreg(VFRE),        defreg(VFTE),       defreg(WVBR),
    defreg(QDE),         defreg(DTXSWC),      defreg_indexed(VLVF, 0),
    defregv(VMOLR),      defreg(RPLOLR),      defregv(VMBMEM),    defregv(VMVIR),

    defregv(PVTCTRL),    defregv(PVTEICS),    defregv(PVTEIMS),   defregv(PVTEIMC),
    defregv(PVTEIAC),    defregv(PVTEIAM),    defregv(PVTEICR),   defregv(PVFGPRC),
    defregv(PVFGPTC),    defregv(PVFGORC),    defregv(PVFGOTC),   defregv(PVFMPRC),
    defregv(PVFGPRLBC),  defregv(PVFGPTLBC),  defregv(PVFGORLBC), defregv(PVFGOTLBC),

    defreg(MTA_A),

    defreg(VTIVAR), defreg(VTIVAR_MISC),
};

uint64_t igb_mmio_read(void *opaque, hwaddr addr, unsigned size);
void igb_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

#endif
