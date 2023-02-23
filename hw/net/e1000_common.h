/*
 * QEMU e1000(e) emulation - shared definitions
 *
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

#ifndef HW_NET_E1000_COMMON_H
#define HW_NET_E1000_COMMON_H

#include "e1000_regs.h"

#define defreg(x)   x = (E1000_##x >> 2)
enum {
    defreg(CTRL),    defreg(EECD),    defreg(EERD),    defreg(GPRC),
    defreg(GPTC),    defreg(ICR),     defreg(ICS),     defreg(IMC),
    defreg(IMS),     defreg(LEDCTL),  defreg(MANC),    defreg(MDIC),
    defreg(MPC),     defreg(PBA),     defreg(RCTL),    defreg(RDBAH0),
    defreg(RDBAL0),  defreg(RDH0),    defreg(RDLEN0),  defreg(RDT0),
    defreg(STATUS),  defreg(SWSM),    defreg(TCTL),    defreg(TDBAH),
    defreg(TDBAL),   defreg(TDH),     defreg(TDLEN),   defreg(TDT),
    defreg(TDLEN1),  defreg(TDBAL1),  defreg(TDBAH1),  defreg(TDH1),
    defreg(TDT1),    defreg(TORH),    defreg(TORL),    defreg(TOTH),
    defreg(TOTL),    defreg(TPR),     defreg(TPT),     defreg(TXDCTL),
    defreg(WUFC),    defreg(RA),      defreg(MTA),     defreg(CRCERRS),
    defreg(VFTA),    defreg(VET),     defreg(RDTR),    defreg(RADV),
    defreg(TADV),    defreg(ITR),     defreg(SCC),     defreg(ECOL),
    defreg(MCC),     defreg(LATECOL), defreg(COLC),    defreg(DC),
    defreg(TNCRS),   defreg(SEQEC),   defreg(CEXTERR), defreg(RLEC),
    defreg(XONRXC),  defreg(XONTXC),  defreg(XOFFRXC), defreg(XOFFTXC),
    defreg(FCRUC),   defreg(AIT),     defreg(TDFH),    defreg(TDFT),
    defreg(TDFHS),   defreg(TDFTS),   defreg(TDFPC),   defreg(WUC),
    defreg(WUS),     defreg(POEMB),   defreg(PBS),     defreg(RDFH),
    defreg(RDFT),    defreg(RDFHS),   defreg(RDFTS),   defreg(RDFPC),
    defreg(PBM),     defreg(IPAV),    defreg(IP4AT),   defreg(IP6AT),
    defreg(WUPM),    defreg(FFLT),    defreg(FFMT),    defreg(FFVT),
    defreg(TARC0),   defreg(TARC1),   defreg(IAM),     defreg(EXTCNF_CTRL),
    defreg(GCR),     defreg(TIMINCA), defreg(EIAC),    defreg(CTRL_EXT),
    defreg(IVAR),    defreg(MFUTP01), defreg(MFUTP23), defreg(MANC2H),
    defreg(MFVAL),   defreg(MDEF),    defreg(FACTPS),  defreg(FTFT),
    defreg(RUC),     defreg(ROC),     defreg(RFC),     defreg(RJC),
    defreg(PRC64),   defreg(PRC127),  defreg(PRC255),  defreg(PRC511),
    defreg(PRC1023), defreg(PRC1522), defreg(PTC64),   defreg(PTC127),
    defreg(PTC255),  defreg(PTC511),  defreg(PTC1023), defreg(PTC1522),
    defreg(GORCL),   defreg(GORCH),   defreg(GOTCL),   defreg(GOTCH),
    defreg(RNBC),    defreg(BPRC),    defreg(MPRC),    defreg(RFCTL),
    defreg(PSRCTL),  defreg(MPTC),    defreg(BPTC),    defreg(TSCTFC),
    defreg(IAC),     defreg(MGTPRC),  defreg(MGTPDC),  defreg(MGTPTC),
    defreg(TSCTC),   defreg(RXCSUM),  defreg(FUNCTAG), defreg(GSCL_1),
    defreg(GSCL_2),  defreg(GSCL_3),  defreg(GSCL_4),  defreg(GSCN_0),
    defreg(GSCN_1),  defreg(GSCN_2),  defreg(GSCN_3),  defreg(GCR2),
    defreg(RAID),    defreg(RSRPD),   defreg(TIDV),    defreg(EITR),
    defreg(MRQC),    defreg(RETA),    defreg(RSSRK),   defreg(RDBAH1),
    defreg(RDBAL1),  defreg(RDLEN1),  defreg(RDH1),    defreg(RDT1),
    defreg(PBACLR),  defreg(FCAL),    defreg(FCAH),    defreg(FCT),
    defreg(FCRTH),   defreg(FCRTL),   defreg(FCTTV),   defreg(FCRTV),
    defreg(FLA),     defreg(EEWR),    defreg(FLOP),    defreg(FLOL),
    defreg(FLSWCTL), defreg(FLSWCNT), defreg(RXDCTL),  defreg(RXDCTL1),
    defreg(MAVTV0),  defreg(MAVTV1),  defreg(MAVTV2),  defreg(MAVTV3),
    defreg(TXSTMPL), defreg(TXSTMPH), defreg(SYSTIML), defreg(SYSTIMH),
    defreg(RXCFGL),  defreg(RXUDP),   defreg(TIMADJL), defreg(TIMADJH),
    defreg(RXSTMPH), defreg(RXSTMPL), defreg(RXSATRL), defreg(RXSATRH),
    defreg(FLASHT),  defreg(TIPG),    defreg(RDH),     defreg(RDT),
    defreg(RDLEN),   defreg(RDBAH),   defreg(RDBAL),
    defreg(TXDCTL1),
    defreg(FLSWDATA),
    defreg(CTRL_DUP),
    defreg(EXTCNF_SIZE),
    defreg(EEMNGCTL),
    defreg(EEMNGDATA),
    defreg(FLMNGCTL),
    defreg(FLMNGDATA),
    defreg(FLMNGCNT),
    defreg(TSYNCRXCTL),
    defreg(TSYNCTXCTL),

    /* Aliases */
    defreg(RDH0_A),  defreg(RDT0_A),  defreg(RDTR_A),  defreg(RDFH_A),
    defreg(RDFT_A),  defreg(TDH_A),   defreg(TDT_A),   defreg(TIDV_A),
    defreg(TDFH_A),  defreg(TDFT_A),  defreg(RA_A),    defreg(RDBAL0_A),
    defreg(TDBAL_A), defreg(TDLEN_A), defreg(VFTA_A),  defreg(RDLEN0_A),
    defreg(FCRTL_A), defreg(FCRTH_A)
};

#endif
