/*
 * QEMU Freescale eTSEC Emulator
 *
 * Copyright (c) 2011-2013 AdaCore
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
#include "registers.h"

const eTSEC_Register_Definition eTSEC_registers_def[] = {
{0x000, "TSEC_ID",  "Controller ID register",    ACC_RO,  0x01240000},
{0x004, "TSEC_ID2", "Controller ID register 2",  ACC_RO,  0x003000F0},
{0x010, "IEVENT",   "Interrupt event register",  ACC_W1C, 0x00000000},
{0x014, "IMASK",    "Interrupt mask register",   ACC_RW,  0x00000000},
{0x018, "EDIS",     "Error disabled register",   ACC_RW,  0x00000000},
{0x020, "ECNTRL",   "Ethernet control register", ACC_RW,  0x00000040},
{0x028, "PTV",      "Pause time value register", ACC_RW,  0x00000000},
{0x02C, "DMACTRL",  "DMA control register",      ACC_RW,  0x00000000},
{0x030, "TBIPA",    "TBI PHY address register",  ACC_RW,  0x00000000},

/* eTSEC FIFO Control and Status Registers */

{0x058, "FIFO_RX_ALARM",          "FIFO receive alarm start threshold register",    ACC_RW, 0x00000040},
{0x05C, "FIFO_RX_ALARM_SHUTOFF",  "FIFO receive alarm shut-off threshold register", ACC_RW, 0x00000080},
{0x08C, "FIFO_TX_THR",            "FIFO transmit threshold register",               ACC_RW, 0x00000080},
{0x098, "FIFO_TX_STARVE",         "FIFO transmit starve register",                  ACC_RW, 0x00000040},
{0x09C, "FIFO_TX_STARVE_SHUTOFF", "FIFO transmit starve shut-off register",         ACC_RW, 0x00000080},

/* eTSEC Transmit Control and Status Registers */

{0x100, "TCTRL",        "Transmit control register",                ACC_RW,  0x00000000},
{0x104, "TSTAT",        "Transmit status register",                 ACC_W1C, 0x00000000},
{0x108, "DFVLAN",       "Default VLAN control word",                ACC_RW,  0x81000000},
{0x110, "TXIC",         "Transmit interrupt coalescing register",   ACC_RW,  0x00000000},
{0x114, "TQUEUE",       "Transmit queue control register",          ACC_RW,  0x00008000},
{0x140, "TR03WT",       "TxBD Rings 0-3 round-robin weightings",    ACC_RW,  0x00000000},
{0x144, "TR47WT",       "TxBD Rings 4-7 round-robin weightings",    ACC_RW,  0x00000000},
{0x180, "TBDBPH",       "Tx data buffer pointer high bits",         ACC_RW,  0x00000000},
{0x184, "TBPTR0",       "TxBD pointer for ring 0",                  ACC_RW,  0x00000000},
{0x18C, "TBPTR1",       "TxBD pointer for ring 1",                  ACC_RW,  0x00000000},
{0x194, "TBPTR2",       "TxBD pointer for ring 2",                  ACC_RW,  0x00000000},
{0x19C, "TBPTR3",       "TxBD pointer for ring 3",                  ACC_RW,  0x00000000},
{0x1A4, "TBPTR4",       "TxBD pointer for ring 4",                  ACC_RW,  0x00000000},
{0x1AC, "TBPTR5",       "TxBD pointer for ring 5",                  ACC_RW,  0x00000000},
{0x1B4, "TBPTR6",       "TxBD pointer for ring 6",                  ACC_RW,  0x00000000},
{0x1BC, "TBPTR7",       "TxBD pointer for ring 7",                  ACC_RW,  0x00000000},
{0x200, "TBASEH",       "TxBD base address high bits",              ACC_RW,  0x00000000},
{0x204, "TBASE0",       "TxBD base address of ring 0",              ACC_RW,  0x00000000},
{0x20C, "TBASE1",       "TxBD base address of ring 1",              ACC_RW,  0x00000000},
{0x214, "TBASE2",       "TxBD base address of ring 2",              ACC_RW,  0x00000000},
{0x21C, "TBASE3",       "TxBD base address of ring 3",              ACC_RW,  0x00000000},
{0x224, "TBASE4",       "TxBD base address of ring 4",              ACC_RW,  0x00000000},
{0x22C, "TBASE5",       "TxBD base address of ring 5",              ACC_RW,  0x00000000},
{0x234, "TBASE6",       "TxBD base address of ring 6",              ACC_RW,  0x00000000},
{0x23C, "TBASE7",       "TxBD base address of ring 7",              ACC_RW,  0x00000000},
{0x280, "TMR_TXTS1_ID", "Tx time stamp identification tag (set 1)", ACC_RO,  0x00000000},
{0x284, "TMR_TXTS2_ID", "Tx time stamp identification tag (set 2)", ACC_RO,  0x00000000},
{0x2C0, "TMR_TXTS1_H",  "Tx time stamp high (set 1)",               ACC_RO,  0x00000000},
{0x2C4, "TMR_TXTS1_L",  "Tx time stamp high (set 1)",               ACC_RO,  0x00000000},
{0x2C8, "TMR_TXTS2_H",  "Tx time stamp high (set 2)",               ACC_RO,  0x00000000},
{0x2CC, "TMR_TXTS2_L",  "Tx time stamp high (set 2)",               ACC_RO,  0x00000000},

/* eTSEC Receive Control and Status Registers */

{0x300, "RCTRL",      "Receive control register",                     ACC_RW,  0x00000000},
{0x304, "RSTAT",      "Receive status register",                      ACC_W1C, 0x00000000},
{0x310, "RXIC",       "Receive interrupt coalescing register",        ACC_RW,  0x00000000},
{0x314, "RQUEUE",     "Receive queue control register.",              ACC_RW,  0x00800080},
{0x330, "RBIFX",      "Receive bit field extract control register",   ACC_RW,  0x00000000},
{0x334, "RQFAR",      "Receive queue filing table address register",  ACC_RW,  0x00000000},
{0x338, "RQFCR",      "Receive queue filing table control register",  ACC_RW,  0x00000000},
{0x33C, "RQFPR",      "Receive queue filing table property register", ACC_RW,  0x00000000},
{0x340, "MRBLR",      "Maximum receive buffer length register",       ACC_RW,  0x00000000},
{0x380, "RBDBPH",     "Rx data buffer pointer high bits",             ACC_RW,  0x00000000},
{0x384, "RBPTR0",     "RxBD pointer for ring 0",                      ACC_RW,  0x00000000},
{0x38C, "RBPTR1",     "RxBD pointer for ring 1",                      ACC_RW,  0x00000000},
{0x394, "RBPTR2",     "RxBD pointer for ring 2",                      ACC_RW,  0x00000000},
{0x39C, "RBPTR3",     "RxBD pointer for ring 3",                      ACC_RW,  0x00000000},
{0x3A4, "RBPTR4",     "RxBD pointer for ring 4",                      ACC_RW,  0x00000000},
{0x3AC, "RBPTR5",     "RxBD pointer for ring 5",                      ACC_RW,  0x00000000},
{0x3B4, "RBPTR6",     "RxBD pointer for ring 6",                      ACC_RW,  0x00000000},
{0x3BC, "RBPTR7",     "RxBD pointer for ring 7",                      ACC_RW,  0x00000000},
{0x400, "RBASEH",     "RxBD base address high bits",                  ACC_RW,  0x00000000},
{0x404, "RBASE0",     "RxBD base address of ring 0",                  ACC_RW,  0x00000000},
{0x40C, "RBASE1",     "RxBD base address of ring 1",                  ACC_RW,  0x00000000},
{0x414, "RBASE2",     "RxBD base address of ring 2",                  ACC_RW,  0x00000000},
{0x41C, "RBASE3",     "RxBD base address of ring 3",                  ACC_RW,  0x00000000},
{0x424, "RBASE4",     "RxBD base address of ring 4",                  ACC_RW,  0x00000000},
{0x42C, "RBASE5",     "RxBD base address of ring 5",                  ACC_RW,  0x00000000},
{0x434, "RBASE6",     "RxBD base address of ring 6",                  ACC_RW,  0x00000000},
{0x43C, "RBASE7",     "RxBD base address of ring 7",                  ACC_RW,  0x00000000},
{0x4C0, "TMR_RXTS_H", "Rx timer time stamp register high",            ACC_RW,  0x00000000},
{0x4C4, "TMR_RXTS_L", "Rx timer time stamp register low",             ACC_RW,  0x00000000},

/* eTSEC MAC Registers */

{0x500, "MACCFG1",     "MAC configuration register 1",          ACC_RW, 0x00000000},
{0x504, "MACCFG2",     "MAC configuration register 2",          ACC_RW, 0x00007000},
{0x508, "IPGIFG",      "Inter-packet/inter-frame gap register", ACC_RW, 0x40605060},
{0x50C, "HAFDUP",      "Half-duplex control",                   ACC_RW, 0x00A1F037},
{0x510, "MAXFRM",      "Maximum frame length",                  ACC_RW, 0x00000600},
{0x520, "MIIMCFG",     "MII management configuration",          ACC_RW, 0x00000007},
{0x524, "MIIMCOM",     "MII management command",                ACC_RW, 0x00000000},
{0x528, "MIIMADD",     "MII management address",                ACC_RW, 0x00000000},
{0x52C, "MIIMCON",     "MII management control",                ACC_WO, 0x00000000},
{0x530, "MIIMSTAT",    "MII management status",                 ACC_RO, 0x00000000},
{0x534, "MIIMIND",     "MII management indicator",              ACC_RO, 0x00000000},
{0x53C, "IFSTAT",      "Interface status",                      ACC_RO, 0x00000000},
{0x540, "MACSTNADDR1", "MAC station address register 1",        ACC_RW, 0x00000000},
{0x544, "MACSTNADDR2", "MAC station address register 2",        ACC_RW, 0x00000000},
{0x548, "MAC01ADDR1",  "MAC exact match address 1, part 1",     ACC_RW, 0x00000000},
{0x54C, "MAC01ADDR2",  "MAC exact match address 1, part 2",     ACC_RW, 0x00000000},
{0x550, "MAC02ADDR1",  "MAC exact match address 2, part 1",     ACC_RW, 0x00000000},
{0x554, "MAC02ADDR2",  "MAC exact match address 2, part 2",     ACC_RW, 0x00000000},
{0x558, "MAC03ADDR1",  "MAC exact match address 3, part 1",     ACC_RW, 0x00000000},
{0x55C, "MAC03ADDR2",  "MAC exact match address 3, part 2",     ACC_RW, 0x00000000},
{0x560, "MAC04ADDR1",  "MAC exact match address 4, part 1",     ACC_RW, 0x00000000},
{0x564, "MAC04ADDR2",  "MAC exact match address 4, part 2",     ACC_RW, 0x00000000},
{0x568, "MAC05ADDR1",  "MAC exact match address 5, part 1",     ACC_RW, 0x00000000},
{0x56C, "MAC05ADDR2",  "MAC exact match address 5, part 2",     ACC_RW, 0x00000000},
{0x570, "MAC06ADDR1",  "MAC exact match address 6, part 1",     ACC_RW, 0x00000000},
{0x574, "MAC06ADDR2",  "MAC exact match address 6, part 2",     ACC_RW, 0x00000000},
{0x578, "MAC07ADDR1",  "MAC exact match address 7, part 1",     ACC_RW, 0x00000000},
{0x57C, "MAC07ADDR2",  "MAC exact match address 7, part 2",     ACC_RW, 0x00000000},
{0x580, "MAC08ADDR1",  "MAC exact match address 8, part 1",     ACC_RW, 0x00000000},
{0x584, "MAC08ADDR2",  "MAC exact match address 8, part 2",     ACC_RW, 0x00000000},
{0x588, "MAC09ADDR1",  "MAC exact match address 9, part 1",     ACC_RW, 0x00000000},
{0x58C, "MAC09ADDR2",  "MAC exact match address 9, part 2",     ACC_RW, 0x00000000},
{0x590, "MAC10ADDR1",  "MAC exact match address 10, part 1",    ACC_RW, 0x00000000},
{0x594, "MAC10ADDR2",  "MAC exact match address 10, part 2",    ACC_RW, 0x00000000},
{0x598, "MAC11ADDR1",  "MAC exact match address 11, part 1",    ACC_RW, 0x00000000},
{0x59C, "MAC11ADDR2",  "MAC exact match address 11, part 2",    ACC_RW, 0x00000000},
{0x5A0, "MAC12ADDR1",  "MAC exact match address 12, part 1",    ACC_RW, 0x00000000},
{0x5A4, "MAC12ADDR2",  "MAC exact match address 12, part 2",    ACC_RW, 0x00000000},
{0x5A8, "MAC13ADDR1",  "MAC exact match address 13, part 1",    ACC_RW, 0x00000000},
{0x5AC, "MAC13ADDR2",  "MAC exact match address 13, part 2",    ACC_RW, 0x00000000},
{0x5B0, "MAC14ADDR1",  "MAC exact match address 14, part 1",    ACC_RW, 0x00000000},
{0x5B4, "MAC14ADDR2",  "MAC exact match address 14, part 2",    ACC_RW, 0x00000000},
{0x5B8, "MAC15ADDR1",  "MAC exact match address 15, part 1",    ACC_RW, 0x00000000},
{0x5BC, "MAC15ADDR2",  "MAC exact match address 15, part 2",    ACC_RW, 0x00000000},

/* eTSEC, "Transmit", "and", Receive, Counters */

{0x680, "TR64",  "Transmit and receive 64-byte frame counter ",                   ACC_RW, 0x00000000},
{0x684, "TR127", "Transmit and receive 65- to 127-byte frame counter",            ACC_RW, 0x00000000},
{0x688, "TR255", "Transmit and receive 128- to 255-byte frame counter",           ACC_RW, 0x00000000},
{0x68C, "TR511", "Transmit and receive 256- to 511-byte frame counter",           ACC_RW, 0x00000000},
{0x690, "TR1K",  "Transmit and receive 512- to 1023-byte frame counter",          ACC_RW, 0x00000000},
{0x694, "TRMAX", "Transmit and receive 1024- to 1518-byte frame counter",         ACC_RW, 0x00000000},
{0x698, "TRMGV", "Transmit and receive 1519- to 1522-byte good VLAN frame count", ACC_RW, 0x00000000},

/* eTSEC Receive Counters */

{0x69C, "RBYT", "Receive byte counter",                  ACC_RW, 0x00000000},
{0x6A0, "RPKT", "Receive packet counter",                ACC_RW, 0x00000000},
{0x6A4, "RFCS", "Receive FCS error counter",             ACC_RW, 0x00000000},
{0x6A8, "RMCA", "Receive multicast packet counter",      ACC_RW, 0x00000000},
{0x6AC, "RBCA", "Receive broadcast packet counter",      ACC_RW, 0x00000000},
{0x6B0, "RXCF", "Receive control frame packet counter ", ACC_RW, 0x00000000},
{0x6B4, "RXPF", "Receive PAUSE frame packet counter",    ACC_RW, 0x00000000},
{0x6B8, "RXUO", "Receive unknown OP code counter ",      ACC_RW, 0x00000000},
{0x6BC, "RALN", "Receive alignment error counter ",      ACC_RW, 0x00000000},
{0x6C0, "RFLR", "Receive frame length error counter ",   ACC_RW, 0x00000000},
{0x6C4, "RCDE", "Receive code error counter ",           ACC_RW, 0x00000000},
{0x6C8, "RCSE", "Receive carrier sense error counter",   ACC_RW, 0x00000000},
{0x6CC, "RUND", "Receive undersize packet counter",      ACC_RW, 0x00000000},
{0x6D0, "ROVR", "Receive oversize packet counter ",      ACC_RW, 0x00000000},
{0x6D4, "RFRG", "Receive fragments counter",             ACC_RW, 0x00000000},
{0x6D8, "RJBR", "Receive jabber counter ",               ACC_RW, 0x00000000},
{0x6DC, "RDRP", "Receive drop counter",                  ACC_RW, 0x00000000},

/* eTSEC Transmit Counters */

{0x6E0, "TBYT", "Transmit byte counter",                       ACC_RW, 0x00000000},
{0x6E4, "TPKT", "Transmit packet counter",                     ACC_RW, 0x00000000},
{0x6E8, "TMCA", "Transmit multicast packet counter ",          ACC_RW, 0x00000000},
{0x6EC, "TBCA", "Transmit broadcast packet counter ",          ACC_RW, 0x00000000},
{0x6F0, "TXPF", "Transmit PAUSE control frame counter ",       ACC_RW, 0x00000000},
{0x6F4, "TDFR", "Transmit deferral packet counter ",           ACC_RW, 0x00000000},
{0x6F8, "TEDF", "Transmit excessive deferral packet counter ", ACC_RW, 0x00000000},
{0x6FC, "TSCL", "Transmit single collision packet counter",    ACC_RW, 0x00000000},
{0x700, "TMCL", "Transmit multiple collision packet counter",  ACC_RW, 0x00000000},
{0x704, "TLCL", "Transmit late collision packet counter",      ACC_RW, 0x00000000},
{0x708, "TXCL", "Transmit excessive collision packet counter", ACC_RW, 0x00000000},
{0x70C, "TNCL", "Transmit total collision counter ",           ACC_RW, 0x00000000},
{0x714, "TDRP", "Transmit drop frame counter",                 ACC_RW, 0x00000000},
{0x718, "TJBR", "Transmit jabber frame counter ",              ACC_RW, 0x00000000},
{0x71C, "TFCS", "Transmit FCS error counter",                  ACC_RW, 0x00000000},
{0x720, "TXCF", "Transmit control frame counter ",             ACC_RW, 0x00000000},
{0x724, "TOVR", "Transmit oversize frame counter",             ACC_RW, 0x00000000},
{0x728, "TUND", "Transmit undersize frame counter ",           ACC_RW, 0x00000000},
{0x72C, "TFRG", "Transmit fragments frame counter ",           ACC_RW, 0x00000000},

/* eTSEC Counter Control and TOE Statistics Registers */

{0x730, "CAR1", "Carry register one register",           ACC_W1C, 0x00000000},
{0x734, "CAR2", "Carry register two register ",          ACC_W1C, 0x00000000},
{0x738, "CAM1", "Carry register one mask register ",     ACC_RW,  0xFE03FFFF},
{0x73C, "CAM2", "Carry register two mask register ",     ACC_RW,  0x000FFFFD},
{0x740, "RREJ", "Receive filer rejected packet counter", ACC_RW,  0x00000000},

/* Hash Function Registers */

{0x800, "IGADDR0", "Individual/group address register 0", ACC_RW, 0x00000000},
{0x804, "IGADDR1", "Individual/group address register 1", ACC_RW, 0x00000000},
{0x808, "IGADDR2", "Individual/group address register 2", ACC_RW, 0x00000000},
{0x80C, "IGADDR3", "Individual/group address register 3", ACC_RW, 0x00000000},
{0x810, "IGADDR4", "Individual/group address register 4", ACC_RW, 0x00000000},
{0x814, "IGADDR5", "Individual/group address register 5", ACC_RW, 0x00000000},
{0x818, "IGADDR6", "Individual/group address register 6", ACC_RW, 0x00000000},
{0x81C, "IGADDR7", "Individual/group address register 7", ACC_RW, 0x00000000},
{0x880, "GADDR0",  "Group address register 0",            ACC_RW, 0x00000000},
{0x884, "GADDR1",  "Group address register 1",            ACC_RW, 0x00000000},
{0x888, "GADDR2",  "Group address register 2",            ACC_RW, 0x00000000},
{0x88C, "GADDR3",  "Group address register 3",            ACC_RW, 0x00000000},
{0x890, "GADDR4",  "Group address register 4",            ACC_RW, 0x00000000},
{0x894, "GADDR5",  "Group address register 5",            ACC_RW, 0x00000000},
{0x898, "GADDR6",  "Group address register 6",            ACC_RW, 0x00000000},
{0x89C, "GADDR7",  "Group address register 7",            ACC_RW, 0x00000000},

/* eTSEC DMA Attribute Registers */

{0xBF8, "ATTR",    "Attribute register",                                  ACC_RW, 0x00000000},
{0xBFC, "ATTRELI", "Attribute extract length and extract index register", ACC_RW, 0x00000000},


/* eTSEC Lossless Flow Control Registers */

{0xC00, "RQPRM0",  "Receive Queue Parameters register 0 ", ACC_RW, 0x00000000},
{0xC04, "RQPRM1",  "Receive Queue Parameters register 1 ", ACC_RW, 0x00000000},
{0xC08, "RQPRM2",  "Receive Queue Parameters register 2 ", ACC_RW, 0x00000000},
{0xC0C, "RQPRM3",  "Receive Queue Parameters register 3 ", ACC_RW, 0x00000000},
{0xC10, "RQPRM4",  "Receive Queue Parameters register 4 ", ACC_RW, 0x00000000},
{0xC14, "RQPRM5",  "Receive Queue Parameters register 5 ", ACC_RW, 0x00000000},
{0xC18, "RQPRM6",  "Receive Queue Parameters register 6 ", ACC_RW, 0x00000000},
{0xC1C, "RQPRM7",  "Receive Queue Parameters register 7 ", ACC_RW, 0x00000000},
{0xC44, "RFBPTR0", "Last Free RxBD pointer for ring 0",    ACC_RW, 0x00000000},
{0xC4C, "RFBPTR1", "Last Free RxBD pointer for ring 1",    ACC_RW, 0x00000000},
{0xC54, "RFBPTR2", "Last Free RxBD pointer for ring 2",    ACC_RW, 0x00000000},
{0xC5C, "RFBPTR3", "Last Free RxBD pointer for ring 3",    ACC_RW, 0x00000000},
{0xC64, "RFBPTR4", "Last Free RxBD pointer for ring 4",    ACC_RW, 0x00000000},
{0xC6C, "RFBPTR5", "Last Free RxBD pointer for ring 5",    ACC_RW, 0x00000000},
{0xC74, "RFBPTR6", "Last Free RxBD pointer for ring 6",    ACC_RW, 0x00000000},
{0xC7C, "RFBPTR7", "Last Free RxBD pointer for ring 7",    ACC_RW, 0x00000000},

/* eTSEC Future Expansion Space */

/* Reserved*/

/* eTSEC IEEE 1588 Registers */

{0xE00, "TMR_CTRL",     "Timer control register",                          ACC_RW,  0x00010001},
{0xE04, "TMR_TEVENT",   "time stamp event register",                       ACC_W1C, 0x00000000},
{0xE08, "TMR_TEMASK",   "Timer event mask register",                       ACC_RW,  0x00000000},
{0xE0C, "TMR_PEVENT",   "time stamp event register",                       ACC_RW,  0x00000000},
{0xE10, "TMR_PEMASK",   "Timer event mask register",                       ACC_RW,  0x00000000},
{0xE14, "TMR_STAT",     "time stamp status register",                      ACC_RW,  0x00000000},
{0xE18, "TMR_CNT_H",    "timer counter high register",                     ACC_RW,  0x00000000},
{0xE1C, "TMR_CNT_L",    "timer counter low register",                      ACC_RW,  0x00000000},
{0xE20, "TMR_ADD",      "Timer drift compensation addend register",        ACC_RW,  0x00000000},
{0xE24, "TMR_ACC",      "Timer accumulator register",                      ACC_RW,  0x00000000},
{0xE28, "TMR_PRSC",     "Timer prescale",                                  ACC_RW,  0x00000002},
{0xE30, "TMROFF_H",     "Timer offset high",                               ACC_RW,  0x00000000},
{0xE34, "TMROFF_L",     "Timer offset low",                                ACC_RW,  0x00000000},
{0xE40, "TMR_ALARM1_H", "Timer alarm 1 high register",                     ACC_RW,  0xFFFFFFFF},
{0xE44, "TMR_ALARM1_L", "Timer alarm 1 high register",                     ACC_RW,  0xFFFFFFFF},
{0xE48, "TMR_ALARM2_H", "Timer alarm 2 high register",                     ACC_RW,  0xFFFFFFFF},
{0xE4C, "TMR_ALARM2_L", "Timer alarm 2 high register",                     ACC_RW,  0xFFFFFFFF},
{0xE80, "TMR_FIPER1",   "Timer fixed period interval",                     ACC_RW,  0xFFFFFFFF},
{0xE84, "TMR_FIPER2",   "Timer fixed period interval",                     ACC_RW,  0xFFFFFFFF},
{0xE88, "TMR_FIPER3",   "Timer fixed period interval",                     ACC_RW,  0xFFFFFFFF},
{0xEA0, "TMR_ETTS1_H",  "Time stamp of general purpose external trigger ", ACC_RW,  0x00000000},
{0xEA4, "TMR_ETTS1_L",  "Time stamp of general purpose external trigger",  ACC_RW,  0x00000000},
{0xEA8, "TMR_ETTS2_H",  "Time stamp of general purpose external trigger ", ACC_RW,  0x00000000},
{0xEAC, "TMR_ETTS2_L",  "Time stamp of general purpose external trigger",  ACC_RW,  0x00000000},

/* End Of Table */
{0x0, 0x0, 0x0, 0x0, 0x0}
};
