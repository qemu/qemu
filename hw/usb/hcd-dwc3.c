/*
 * QEMU model of the USB DWC3 host controller emulation.
 *
 * This model defines global register space of DWC3 controller. Global
 * registers control the AXI/AHB interfaces properties, external FIFO support
 * and event count support. All of which are unimplemented at present. We are
 * only supporting core reset and read of ID register.
 *
 * Copyright (c) 2020 Xilinx Inc. Vikram Garhwal<fnu.vikram@xilinx.com>
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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/usb/hcd-dwc3.h"
#include "qapi/error.h"

#ifndef USB_DWC3_ERR_DEBUG
#define USB_DWC3_ERR_DEBUG 0
#endif

#define HOST_MODE           1
#define FIFO_LEN         0x1000

REG32(GSBUSCFG0, 0x00)
    FIELD(GSBUSCFG0, DATRDREQINFO, 28, 4)
    FIELD(GSBUSCFG0, DESRDREQINFO, 24, 4)
    FIELD(GSBUSCFG0, DATWRREQINFO, 20, 4)
    FIELD(GSBUSCFG0, DESWRREQINFO, 16, 4)
    FIELD(GSBUSCFG0, RESERVED_15_12, 12, 4)
    FIELD(GSBUSCFG0, DATBIGEND, 11, 1)
    FIELD(GSBUSCFG0, DESBIGEND, 10, 1)
    FIELD(GSBUSCFG0, RESERVED_9_8, 8, 2)
    FIELD(GSBUSCFG0, INCR256BRSTENA, 7, 1)
    FIELD(GSBUSCFG0, INCR128BRSTENA, 6, 1)
    FIELD(GSBUSCFG0, INCR64BRSTENA, 5, 1)
    FIELD(GSBUSCFG0, INCR32BRSTENA, 4, 1)
    FIELD(GSBUSCFG0, INCR16BRSTENA, 3, 1)
    FIELD(GSBUSCFG0, INCR8BRSTENA, 2, 1)
    FIELD(GSBUSCFG0, INCR4BRSTENA, 1, 1)
    FIELD(GSBUSCFG0, INCRBRSTENA, 0, 1)
REG32(GSBUSCFG1, 0x04)
    FIELD(GSBUSCFG1, RESERVED_31_13, 13, 19)
    FIELD(GSBUSCFG1, EN1KPAGE, 12, 1)
    FIELD(GSBUSCFG1, PIPETRANSLIMIT, 8, 4)
    FIELD(GSBUSCFG1, RESERVED_7_0, 0, 8)
REG32(GTXTHRCFG, 0x08)
    FIELD(GTXTHRCFG, RESERVED_31, 31, 1)
    FIELD(GTXTHRCFG, RESERVED_30, 30, 1)
    FIELD(GTXTHRCFG, USBTXPKTCNTSEL, 29, 1)
    FIELD(GTXTHRCFG, RESERVED_28, 28, 1)
    FIELD(GTXTHRCFG, USBTXPKTCNT, 24, 4)
    FIELD(GTXTHRCFG, USBMAXTXBURSTSIZE, 16, 8)
    FIELD(GTXTHRCFG, RESERVED_15, 15, 1)
    FIELD(GTXTHRCFG, RESERVED_14, 14, 1)
    FIELD(GTXTHRCFG, RESERVED_13_11, 11, 3)
    FIELD(GTXTHRCFG, RESERVED_10_0, 0, 11)
REG32(GRXTHRCFG, 0x0c)
    FIELD(GRXTHRCFG, RESERVED_31_30, 30, 2)
    FIELD(GRXTHRCFG, USBRXPKTCNTSEL, 29, 1)
    FIELD(GRXTHRCFG, RESERVED_28, 28, 1)
    FIELD(GRXTHRCFG, USBRXPKTCNT, 24, 4)
    FIELD(GRXTHRCFG, USBMAXRXBURSTSIZE, 19, 5)
    FIELD(GRXTHRCFG, RESERVED_18_16, 16, 3)
    FIELD(GRXTHRCFG, RESERVED_15, 15, 1)
    FIELD(GRXTHRCFG, RESERVED_14_13, 13, 2)
    FIELD(GRXTHRCFG, RESVISOCOUTSPC, 0, 13)
REG32(GCTL, 0x10)
    FIELD(GCTL, PWRDNSCALE, 19, 13)
    FIELD(GCTL, MASTERFILTBYPASS, 18, 1)
    FIELD(GCTL, BYPSSETADDR, 17, 1)
    FIELD(GCTL, U2RSTECN, 16, 1)
    FIELD(GCTL, FRMSCLDWN, 14, 2)
    FIELD(GCTL, PRTCAPDIR, 12, 2)
    FIELD(GCTL, CORESOFTRESET, 11, 1)
    FIELD(GCTL, U1U2TIMERSCALE, 9, 1)
    FIELD(GCTL, DEBUGATTACH, 8, 1)
    FIELD(GCTL, RAMCLKSEL, 6, 2)
    FIELD(GCTL, SCALEDOWN, 4, 2)
    FIELD(GCTL, DISSCRAMBLE, 3, 1)
    FIELD(GCTL, U2EXIT_LFPS, 2, 1)
    FIELD(GCTL, GBLHIBERNATIONEN, 1, 1)
    FIELD(GCTL, DSBLCLKGTNG, 0, 1)
REG32(GPMSTS, 0x14)
REG32(GSTS, 0x18)
    FIELD(GSTS, CBELT, 20, 12)
    FIELD(GSTS, RESERVED_19_12, 12, 8)
    FIELD(GSTS, SSIC_IP, 11, 1)
    FIELD(GSTS, OTG_IP, 10, 1)
    FIELD(GSTS, BC_IP, 9, 1)
    FIELD(GSTS, ADP_IP, 8, 1)
    FIELD(GSTS, HOST_IP, 7, 1)
    FIELD(GSTS, DEVICE_IP, 6, 1)
    FIELD(GSTS, CSRTIMEOUT, 5, 1)
    FIELD(GSTS, BUSERRADDRVLD, 4, 1)
    FIELD(GSTS, RESERVED_3_2, 2, 2)
    FIELD(GSTS, CURMOD, 0, 2)
REG32(GUCTL1, 0x1c)
    FIELD(GUCTL1, RESUME_OPMODE_HS_HOST, 10, 1)
REG32(GSNPSID, 0x20)
REG32(GGPIO, 0x24)
    FIELD(GGPIO, GPO, 16, 16)
    FIELD(GGPIO, GPI, 0, 16)
REG32(GUID, 0x28)
REG32(GUCTL, 0x2c)
    FIELD(GUCTL, REFCLKPER, 22, 10)
    FIELD(GUCTL, NOEXTRDL, 21, 1)
    FIELD(GUCTL, RESERVED_20_18, 18, 3)
    FIELD(GUCTL, SPRSCTRLTRANSEN, 17, 1)
    FIELD(GUCTL, RESBWHSEPS, 16, 1)
    FIELD(GUCTL, RESERVED_15, 15, 1)
    FIELD(GUCTL, USBHSTINAUTORETRYEN, 14, 1)
    FIELD(GUCTL, ENOVERLAPCHK, 13, 1)
    FIELD(GUCTL, EXTCAPSUPPTEN, 12, 1)
    FIELD(GUCTL, INSRTEXTRFSBODI, 11, 1)
    FIELD(GUCTL, DTCT, 9, 2)
    FIELD(GUCTL, DTFT, 0, 9)
REG32(GBUSERRADDRLO, 0x30)
REG32(GBUSERRADDRHI, 0x34)
REG32(GHWPARAMS0, 0x40)
    FIELD(GHWPARAMS0, GHWPARAMS0_31_24, 24, 8)
    FIELD(GHWPARAMS0, GHWPARAMS0_23_16, 16, 8)
    FIELD(GHWPARAMS0, GHWPARAMS0_15_8, 8, 8)
    FIELD(GHWPARAMS0, GHWPARAMS0_7_6, 6, 2)
    FIELD(GHWPARAMS0, GHWPARAMS0_5_3, 3, 3)
    FIELD(GHWPARAMS0, GHWPARAMS0_2_0, 0, 3)
REG32(GHWPARAMS1, 0x44)
    FIELD(GHWPARAMS1, GHWPARAMS1_31, 31, 1)
    FIELD(GHWPARAMS1, GHWPARAMS1_30, 30, 1)
    FIELD(GHWPARAMS1, GHWPARAMS1_29, 29, 1)
    FIELD(GHWPARAMS1, GHWPARAMS1_28, 28, 1)
    FIELD(GHWPARAMS1, GHWPARAMS1_27, 27, 1)
    FIELD(GHWPARAMS1, GHWPARAMS1_26, 26, 1)
    FIELD(GHWPARAMS1, GHWPARAMS1_25_24, 24, 2)
    FIELD(GHWPARAMS1, GHWPARAMS1_23, 23, 1)
    FIELD(GHWPARAMS1, GHWPARAMS1_22_21, 21, 2)
    FIELD(GHWPARAMS1, GHWPARAMS1_20_15, 15, 6)
    FIELD(GHWPARAMS1, GHWPARAMS1_14_12, 12, 3)
    FIELD(GHWPARAMS1, GHWPARAMS1_11_9, 9, 3)
    FIELD(GHWPARAMS1, GHWPARAMS1_8_6, 6, 3)
    FIELD(GHWPARAMS1, GHWPARAMS1_5_3, 3, 3)
    FIELD(GHWPARAMS1, GHWPARAMS1_2_0, 0, 3)
REG32(GHWPARAMS2, 0x48)
REG32(GHWPARAMS3, 0x4c)
    FIELD(GHWPARAMS3, GHWPARAMS3_31, 31, 1)
    FIELD(GHWPARAMS3, GHWPARAMS3_30_23, 23, 8)
    FIELD(GHWPARAMS3, GHWPARAMS3_22_18, 18, 5)
    FIELD(GHWPARAMS3, GHWPARAMS3_17_12, 12, 6)
    FIELD(GHWPARAMS3, GHWPARAMS3_11, 11, 1)
    FIELD(GHWPARAMS3, GHWPARAMS3_10, 10, 1)
    FIELD(GHWPARAMS3, GHWPARAMS3_9_8, 8, 2)
    FIELD(GHWPARAMS3, GHWPARAMS3_7_6, 6, 2)
    FIELD(GHWPARAMS3, GHWPARAMS3_5_4, 4, 2)
    FIELD(GHWPARAMS3, GHWPARAMS3_3_2, 2, 2)
    FIELD(GHWPARAMS3, GHWPARAMS3_1_0, 0, 2)
REG32(GHWPARAMS4, 0x50)
    FIELD(GHWPARAMS4, GHWPARAMS4_31_28, 28, 4)
    FIELD(GHWPARAMS4, GHWPARAMS4_27_24, 24, 4)
    FIELD(GHWPARAMS4, GHWPARAMS4_23, 23, 1)
    FIELD(GHWPARAMS4, GHWPARAMS4_22, 22, 1)
    FIELD(GHWPARAMS4, GHWPARAMS4_21, 21, 1)
    FIELD(GHWPARAMS4, GHWPARAMS4_20_17, 17, 4)
    FIELD(GHWPARAMS4, GHWPARAMS4_16_13, 13, 4)
    FIELD(GHWPARAMS4, GHWPARAMS4_12, 12, 1)
    FIELD(GHWPARAMS4, GHWPARAMS4_11, 11, 1)
    FIELD(GHWPARAMS4, GHWPARAMS4_10_9, 9, 2)
    FIELD(GHWPARAMS4, GHWPARAMS4_8_7, 7, 2)
    FIELD(GHWPARAMS4, GHWPARAMS4_6, 6, 1)
    FIELD(GHWPARAMS4, GHWPARAMS4_5_0, 0, 6)
REG32(GHWPARAMS5, 0x54)
    FIELD(GHWPARAMS5, GHWPARAMS5_31_28, 28, 4)
    FIELD(GHWPARAMS5, GHWPARAMS5_27_22, 22, 6)
    FIELD(GHWPARAMS5, GHWPARAMS5_21_16, 16, 6)
    FIELD(GHWPARAMS5, GHWPARAMS5_15_10, 10, 6)
    FIELD(GHWPARAMS5, GHWPARAMS5_9_4, 4, 6)
    FIELD(GHWPARAMS5, GHWPARAMS5_3_0, 0, 4)
REG32(GHWPARAMS6, 0x58)
    FIELD(GHWPARAMS6, GHWPARAMS6_31_16, 16, 16)
    FIELD(GHWPARAMS6, BUSFLTRSSUPPORT, 15, 1)
    FIELD(GHWPARAMS6, BCSUPPORT, 14, 1)
    FIELD(GHWPARAMS6, OTG_SS_SUPPORT, 13, 1)
    FIELD(GHWPARAMS6, ADPSUPPORT, 12, 1)
    FIELD(GHWPARAMS6, HNPSUPPORT, 11, 1)
    FIELD(GHWPARAMS6, SRPSUPPORT, 10, 1)
    FIELD(GHWPARAMS6, GHWPARAMS6_9_8, 8, 2)
    FIELD(GHWPARAMS6, GHWPARAMS6_7, 7, 1)
    FIELD(GHWPARAMS6, GHWPARAMS6_6, 6, 1)
    FIELD(GHWPARAMS6, GHWPARAMS6_5_0, 0, 6)
REG32(GHWPARAMS7, 0x5c)
    FIELD(GHWPARAMS7, GHWPARAMS7_31_16, 16, 16)
    FIELD(GHWPARAMS7, GHWPARAMS7_15_0, 0, 16)
REG32(GDBGFIFOSPACE, 0x60)
    FIELD(GDBGFIFOSPACE, SPACE_AVAILABLE, 16, 16)
    FIELD(GDBGFIFOSPACE, RESERVED_15_9, 9, 7)
    FIELD(GDBGFIFOSPACE, FIFO_QUEUE_SELECT, 0, 9)
REG32(GUCTL2, 0x9c)
    FIELD(GUCTL2, RESERVED_31_26, 26, 6)
    FIELD(GUCTL2, EN_HP_PM_TIMER, 19, 7)
    FIELD(GUCTL2, NOLOWPWRDUR, 15, 4)
    FIELD(GUCTL2, RST_ACTBITLATER, 14, 1)
    FIELD(GUCTL2, RESERVED_13, 13, 1)
    FIELD(GUCTL2, DISABLECFC, 11, 1)
REG32(GUSB2PHYCFG, 0x100)
    FIELD(GUSB2PHYCFG, U2_FREECLK_EXISTS, 30, 1)
    FIELD(GUSB2PHYCFG, ULPI_LPM_WITH_OPMODE_CHK, 29, 1)
    FIELD(GUSB2PHYCFG, RESERVED_25, 25, 1)
    FIELD(GUSB2PHYCFG, LSTRD, 22, 3)
    FIELD(GUSB2PHYCFG, LSIPD, 19, 3)
    FIELD(GUSB2PHYCFG, ULPIEXTVBUSINDIACTOR, 18, 1)
    FIELD(GUSB2PHYCFG, ULPIEXTVBUSDRV, 17, 1)
    FIELD(GUSB2PHYCFG, RESERVED_16, 16, 1)
    FIELD(GUSB2PHYCFG, ULPIAUTORES, 15, 1)
    FIELD(GUSB2PHYCFG, RESERVED_14, 14, 1)
    FIELD(GUSB2PHYCFG, USBTRDTIM, 10, 4)
    FIELD(GUSB2PHYCFG, XCVRDLY, 9, 1)
    FIELD(GUSB2PHYCFG, ENBLSLPM, 8, 1)
    FIELD(GUSB2PHYCFG, PHYSEL, 7, 1)
    FIELD(GUSB2PHYCFG, SUSPENDUSB20, 6, 1)
    FIELD(GUSB2PHYCFG, FSINTF, 5, 1)
    FIELD(GUSB2PHYCFG, ULPI_UTMI_SEL, 4, 1)
    FIELD(GUSB2PHYCFG, PHYIF, 3, 1)
    FIELD(GUSB2PHYCFG, TOUTCAL, 0, 3)
REG32(GUSB2I2CCTL, 0x140)
REG32(GUSB2PHYACC_ULPI, 0x180)
    FIELD(GUSB2PHYACC_ULPI, RESERVED_31_27, 27, 5)
    FIELD(GUSB2PHYACC_ULPI, DISUIPIDRVR, 26, 1)
    FIELD(GUSB2PHYACC_ULPI, NEWREGREQ, 25, 1)
    FIELD(GUSB2PHYACC_ULPI, VSTSDONE, 24, 1)
    FIELD(GUSB2PHYACC_ULPI, VSTSBSY, 23, 1)
    FIELD(GUSB2PHYACC_ULPI, REGWR, 22, 1)
    FIELD(GUSB2PHYACC_ULPI, REGADDR, 16, 6)
    FIELD(GUSB2PHYACC_ULPI, EXTREGADDR, 8, 8)
    FIELD(GUSB2PHYACC_ULPI, REGDATA, 0, 8)
REG32(GTXFIFOSIZ0, 0x200)
    FIELD(GTXFIFOSIZ0, TXFSTADDR_N, 16, 16)
    FIELD(GTXFIFOSIZ0, TXFDEP_N, 0, 16)
REG32(GTXFIFOSIZ1, 0x204)
    FIELD(GTXFIFOSIZ1, TXFSTADDR_N, 16, 16)
    FIELD(GTXFIFOSIZ1, TXFDEP_N, 0, 16)
REG32(GTXFIFOSIZ2, 0x208)
    FIELD(GTXFIFOSIZ2, TXFSTADDR_N, 16, 16)
    FIELD(GTXFIFOSIZ2, TXFDEP_N, 0, 16)
REG32(GTXFIFOSIZ3, 0x20c)
    FIELD(GTXFIFOSIZ3, TXFSTADDR_N, 16, 16)
    FIELD(GTXFIFOSIZ3, TXFDEP_N, 0, 16)
REG32(GTXFIFOSIZ4, 0x210)
    FIELD(GTXFIFOSIZ4, TXFSTADDR_N, 16, 16)
    FIELD(GTXFIFOSIZ4, TXFDEP_N, 0, 16)
REG32(GTXFIFOSIZ5, 0x214)
    FIELD(GTXFIFOSIZ5, TXFSTADDR_N, 16, 16)
    FIELD(GTXFIFOSIZ5, TXFDEP_N, 0, 16)
REG32(GRXFIFOSIZ0, 0x280)
    FIELD(GRXFIFOSIZ0, RXFSTADDR_N, 16, 16)
    FIELD(GRXFIFOSIZ0, RXFDEP_N, 0, 16)
REG32(GRXFIFOSIZ1, 0x284)
    FIELD(GRXFIFOSIZ1, RXFSTADDR_N, 16, 16)
    FIELD(GRXFIFOSIZ1, RXFDEP_N, 0, 16)
REG32(GRXFIFOSIZ2, 0x288)
    FIELD(GRXFIFOSIZ2, RXFSTADDR_N, 16, 16)
    FIELD(GRXFIFOSIZ2, RXFDEP_N, 0, 16)
REG32(GEVNTADRLO_0, 0x300)
REG32(GEVNTADRHI_0, 0x304)
REG32(GEVNTSIZ_0, 0x308)
    FIELD(GEVNTSIZ_0, EVNTINTRPTMASK, 31, 1)
    FIELD(GEVNTSIZ_0, RESERVED_30_16, 16, 15)
    FIELD(GEVNTSIZ_0, EVENTSIZ, 0, 16)
REG32(GEVNTCOUNT_0, 0x30c)
    FIELD(GEVNTCOUNT_0, EVNT_HANDLER_BUSY, 31, 1)
    FIELD(GEVNTCOUNT_0, RESERVED_30_16, 16, 15)
    FIELD(GEVNTCOUNT_0, EVNTCOUNT, 0, 16)
REG32(GEVNTADRLO_1, 0x310)
REG32(GEVNTADRHI_1, 0x314)
REG32(GEVNTSIZ_1, 0x318)
    FIELD(GEVNTSIZ_1, EVNTINTRPTMASK, 31, 1)
    FIELD(GEVNTSIZ_1, RESERVED_30_16, 16, 15)
    FIELD(GEVNTSIZ_1, EVENTSIZ, 0, 16)
REG32(GEVNTCOUNT_1, 0x31c)
    FIELD(GEVNTCOUNT_1, EVNT_HANDLER_BUSY, 31, 1)
    FIELD(GEVNTCOUNT_1, RESERVED_30_16, 16, 15)
    FIELD(GEVNTCOUNT_1, EVNTCOUNT, 0, 16)
REG32(GEVNTADRLO_2, 0x320)
REG32(GEVNTADRHI_2, 0x324)
REG32(GEVNTSIZ_2, 0x328)
    FIELD(GEVNTSIZ_2, EVNTINTRPTMASK, 31, 1)
    FIELD(GEVNTSIZ_2, RESERVED_30_16, 16, 15)
    FIELD(GEVNTSIZ_2, EVENTSIZ, 0, 16)
REG32(GEVNTCOUNT_2, 0x32c)
    FIELD(GEVNTCOUNT_2, EVNT_HANDLER_BUSY, 31, 1)
    FIELD(GEVNTCOUNT_2, RESERVED_30_16, 16, 15)
    FIELD(GEVNTCOUNT_2, EVNTCOUNT, 0, 16)
REG32(GEVNTADRLO_3, 0x330)
REG32(GEVNTADRHI_3, 0x334)
REG32(GEVNTSIZ_3, 0x338)
    FIELD(GEVNTSIZ_3, EVNTINTRPTMASK, 31, 1)
    FIELD(GEVNTSIZ_3, RESERVED_30_16, 16, 15)
    FIELD(GEVNTSIZ_3, EVENTSIZ, 0, 16)
REG32(GEVNTCOUNT_3, 0x33c)
    FIELD(GEVNTCOUNT_3, EVNT_HANDLER_BUSY, 31, 1)
    FIELD(GEVNTCOUNT_3, RESERVED_30_16, 16, 15)
    FIELD(GEVNTCOUNT_3, EVNTCOUNT, 0, 16)
REG32(GHWPARAMS8, 0x500)
REG32(GTXFIFOPRIDEV, 0x510)
    FIELD(GTXFIFOPRIDEV, RESERVED_31_N, 6, 26)
    FIELD(GTXFIFOPRIDEV, GTXFIFOPRIDEV, 0, 6)
REG32(GTXFIFOPRIHST, 0x518)
    FIELD(GTXFIFOPRIHST, RESERVED_31_16, 3, 29)
    FIELD(GTXFIFOPRIHST, GTXFIFOPRIHST, 0, 3)
REG32(GRXFIFOPRIHST, 0x51c)
    FIELD(GRXFIFOPRIHST, RESERVED_31_16, 3, 29)
    FIELD(GRXFIFOPRIHST, GRXFIFOPRIHST, 0, 3)
REG32(GDMAHLRATIO, 0x524)
    FIELD(GDMAHLRATIO, RESERVED_31_13, 13, 19)
    FIELD(GDMAHLRATIO, HSTRXFIFO, 8, 5)
    FIELD(GDMAHLRATIO, RESERVED_7_5, 5, 3)
    FIELD(GDMAHLRATIO, HSTTXFIFO, 0, 5)
REG32(GFLADJ, 0x530)
    FIELD(GFLADJ, GFLADJ_REFCLK_240MHZDECR_PLS1, 31, 1)
    FIELD(GFLADJ, GFLADJ_REFCLK_240MHZ_DECR, 24, 7)
    FIELD(GFLADJ, GFLADJ_REFCLK_LPM_SEL, 23, 1)
    FIELD(GFLADJ, RESERVED_22, 22, 1)
    FIELD(GFLADJ, GFLADJ_REFCLK_FLADJ, 8, 14)
    FIELD(GFLADJ, GFLADJ_30MHZ_SDBND_SEL, 7, 1)
    FIELD(GFLADJ, GFLADJ_30MHZ, 0, 6)
REG32(GUSB2RHBCTL, 0x540)
    FIELD(GUSB2RHBCTL, OVRD_L1TIMEOUT, 0, 4)

#define DWC3_GLOBAL_OFFSET 0xC100
static void reset_csr(USBDWC3 * s)
{
    int i = 0;
    /*
     * We reset all CSR regs except GCTL, GUCTL, GSTS, GSNPSID, GGPIO, GUID,
     * GUSB2PHYCFGn registers and GUSB3PIPECTLn registers. We will skip PHY
     * register as we don't implement them.
     */
    for (i = 0; i < USB_DWC3_R_MAX; i++) {
        switch (i) {
        case R_GCTL:
            break;
        case R_GSTS:
            break;
        case R_GSNPSID:
            break;
        case R_GGPIO:
            break;
        case R_GUID:
            break;
        case R_GUCTL:
            break;
        case R_GHWPARAMS0...R_GHWPARAMS7:
            break;
        case R_GHWPARAMS8:
            break;
        default:
            register_reset(&s->regs_info[i]);
            break;
        }
    }

    xhci_sysbus_reset(DEVICE(&s->sysbus_xhci));
}

static void usb_dwc3_gctl_postw(RegisterInfo *reg, uint64_t val64)
{
    USBDWC3 *s = USB_DWC3(reg->opaque);

    if (ARRAY_FIELD_EX32(s->regs, GCTL, CORESOFTRESET)) {
        reset_csr(s);
    }
}

static void usb_dwc3_guid_postw(RegisterInfo *reg, uint64_t val64)
{
    USBDWC3 *s = USB_DWC3(reg->opaque);

    s->regs[R_GUID] = s->cfg.dwc_usb3_user;
}

static const RegisterAccessInfo usb_dwc3_regs_info[] = {
    {   .name = "GSBUSCFG0",  .addr = A_GSBUSCFG0,
        .ro = 0xf300,
        .unimp = 0xffffffff,
    },{ .name = "GSBUSCFG1",  .addr = A_GSBUSCFG1,
        .reset = 0x300,
        .ro = 0xffffe0ff,
        .unimp = 0xffffffff,
    },{ .name = "GTXTHRCFG",  .addr = A_GTXTHRCFG,
        .ro = 0xd000ffff,
        .unimp = 0xffffffff,
    },{ .name = "GRXTHRCFG",  .addr = A_GRXTHRCFG,
        .ro = 0xd007e000,
        .unimp = 0xffffffff,
    },{ .name = "GCTL",  .addr = A_GCTL,
        .reset = 0x30c13004, .post_write = usb_dwc3_gctl_postw,
    },{ .name = "GPMSTS",  .addr = A_GPMSTS,
        .ro = 0xfffffff,
        .unimp = 0xffffffff,
    },{ .name = "GSTS",  .addr = A_GSTS,
        .reset = 0x7e800000,
        .ro = 0xffffffcf,
        .w1c = 0x30,
        .unimp = 0xffffffff,
    },{ .name = "GUCTL1",  .addr = A_GUCTL1,
        .reset = 0x198a,
        .ro = 0x7800,
        .unimp = 0xffffffff,
    },{ .name = "GSNPSID",  .addr = A_GSNPSID,
        .reset = 0x5533330a,
        .ro = 0xffffffff,
    },{ .name = "GGPIO",  .addr = A_GGPIO,
        .ro = 0xffff,
        .unimp = 0xffffffff,
    },{ .name = "GUID",  .addr = A_GUID,
        .reset = 0x12345678, .post_write = usb_dwc3_guid_postw,
    },{ .name = "GUCTL",  .addr = A_GUCTL,
        .reset = 0x0c808010,
        .ro = 0x1c8000,
        .unimp = 0xffffffff,
    },{ .name = "GBUSERRADDRLO",  .addr = A_GBUSERRADDRLO,
        .ro = 0xffffffff,
    },{ .name = "GBUSERRADDRHI",  .addr = A_GBUSERRADDRHI,
        .ro = 0xffffffff,
    },{ .name = "GHWPARAMS0",  .addr = A_GHWPARAMS0,
        .ro = 0xffffffff,
    },{ .name = "GHWPARAMS1",  .addr = A_GHWPARAMS1,
        .ro = 0xffffffff,
    },{ .name = "GHWPARAMS2",  .addr = A_GHWPARAMS2,
        .ro = 0xffffffff,
    },{ .name = "GHWPARAMS3",  .addr = A_GHWPARAMS3,
        .ro = 0xffffffff,
    },{ .name = "GHWPARAMS4",  .addr = A_GHWPARAMS4,
        .ro = 0xffffffff,
    },{ .name = "GHWPARAMS5",  .addr = A_GHWPARAMS5,
        .ro = 0xffffffff,
    },{ .name = "GHWPARAMS6",  .addr = A_GHWPARAMS6,
        .ro = 0xffffffff,
    },{ .name = "GHWPARAMS7",  .addr = A_GHWPARAMS7,
        .ro = 0xffffffff,
    },{ .name = "GDBGFIFOSPACE",  .addr = A_GDBGFIFOSPACE,
        .reset = 0xa0000,
        .ro = 0xfffffe00,
        .unimp = 0xffffffff,
    },{ .name = "GUCTL2",  .addr = A_GUCTL2,
        .reset = 0x40d,
        .ro = 0x2000,
        .unimp = 0xffffffff,
    },{ .name = "GUSB2PHYCFG",  .addr = A_GUSB2PHYCFG,
        .reset = 0x40102410,
        .ro = 0x1e014030,
        .unimp = 0xffffffff,
    },{ .name = "GUSB2I2CCTL",  .addr = A_GUSB2I2CCTL,
        .ro = 0xffffffff,
        .unimp = 0xffffffff,
    },{ .name = "GUSB2PHYACC_ULPI",  .addr = A_GUSB2PHYACC_ULPI,
        .ro = 0xfd000000,
        .unimp = 0xffffffff,
    },{ .name = "GTXFIFOSIZ0",  .addr = A_GTXFIFOSIZ0,
        .reset = 0x2c7000a,
        .unimp = 0xffffffff,
    },{ .name = "GTXFIFOSIZ1",  .addr = A_GTXFIFOSIZ1,
        .reset = 0x2d10103,
        .unimp = 0xffffffff,
    },{ .name = "GTXFIFOSIZ2",  .addr = A_GTXFIFOSIZ2,
        .reset = 0x3d40103,
        .unimp = 0xffffffff,
    },{ .name = "GTXFIFOSIZ3",  .addr = A_GTXFIFOSIZ3,
        .reset = 0x4d70083,
        .unimp = 0xffffffff,
    },{ .name = "GTXFIFOSIZ4",  .addr = A_GTXFIFOSIZ4,
        .reset = 0x55a0083,
        .unimp = 0xffffffff,
    },{ .name = "GTXFIFOSIZ5",  .addr = A_GTXFIFOSIZ5,
        .reset = 0x5dd0083,
        .unimp = 0xffffffff,
    },{ .name = "GRXFIFOSIZ0",  .addr = A_GRXFIFOSIZ0,
        .reset = 0x1c20105,
        .unimp = 0xffffffff,
    },{ .name = "GRXFIFOSIZ1",  .addr = A_GRXFIFOSIZ1,
        .reset = 0x2c70000,
        .unimp = 0xffffffff,
    },{ .name = "GRXFIFOSIZ2",  .addr = A_GRXFIFOSIZ2,
        .reset = 0x2c70000,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTADRLO_0",  .addr = A_GEVNTADRLO_0,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTADRHI_0",  .addr = A_GEVNTADRHI_0,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTSIZ_0",  .addr = A_GEVNTSIZ_0,
        .ro = 0x7fff0000,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTCOUNT_0",  .addr = A_GEVNTCOUNT_0,
        .ro = 0x7fff0000,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTADRLO_1",  .addr = A_GEVNTADRLO_1,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTADRHI_1",  .addr = A_GEVNTADRHI_1,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTSIZ_1",  .addr = A_GEVNTSIZ_1,
        .ro = 0x7fff0000,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTCOUNT_1",  .addr = A_GEVNTCOUNT_1,
        .ro = 0x7fff0000,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTADRLO_2",  .addr = A_GEVNTADRLO_2,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTADRHI_2",  .addr = A_GEVNTADRHI_2,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTSIZ_2",  .addr = A_GEVNTSIZ_2,
        .ro = 0x7fff0000,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTCOUNT_2",  .addr = A_GEVNTCOUNT_2,
        .ro = 0x7fff0000,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTADRLO_3",  .addr = A_GEVNTADRLO_3,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTADRHI_3",  .addr = A_GEVNTADRHI_3,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTSIZ_3",  .addr = A_GEVNTSIZ_3,
        .ro = 0x7fff0000,
        .unimp = 0xffffffff,
    },{ .name = "GEVNTCOUNT_3",  .addr = A_GEVNTCOUNT_3,
        .ro = 0x7fff0000,
        .unimp = 0xffffffff,
    },{ .name = "GHWPARAMS8",  .addr = A_GHWPARAMS8,
        .ro = 0xffffffff,
    },{ .name = "GTXFIFOPRIDEV",  .addr = A_GTXFIFOPRIDEV,
        .ro = 0xffffffc0,
        .unimp = 0xffffffff,
    },{ .name = "GTXFIFOPRIHST",  .addr = A_GTXFIFOPRIHST,
        .ro = 0xfffffff8,
        .unimp = 0xffffffff,
    },{ .name = "GRXFIFOPRIHST",  .addr = A_GRXFIFOPRIHST,
        .ro = 0xfffffff8,
        .unimp = 0xffffffff,
    },{ .name = "GDMAHLRATIO",  .addr = A_GDMAHLRATIO,
        .ro = 0xffffe0e0,
        .unimp = 0xffffffff,
    },{ .name = "GFLADJ",  .addr = A_GFLADJ,
        .reset = 0xc83f020,
        .rsvd = 0x40,
        .ro = 0x400040,
        .unimp = 0xffffffff,
    },{ .name = "GUSB2RHBCTL",  .addr = A_GUSB2RHBCTL,
        .rsvd = 0xfffffff0,
        .unimp = 0xffffffff,
    }
};

static void usb_dwc3_reset(DeviceState *dev)
{
    USBDWC3 *s = USB_DWC3(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        switch (i) {
        case R_GHWPARAMS0...R_GHWPARAMS7:
            break;
        case R_GHWPARAMS8:
            break;
        default:
            register_reset(&s->regs_info[i]);
        };
    }

    xhci_sysbus_reset(DEVICE(&s->sysbus_xhci));
}

static const MemoryRegionOps usb_dwc3_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void usb_dwc3_realize(DeviceState *dev, Error **errp)
{
    USBDWC3 *s = USB_DWC3(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *err = NULL;

    sysbus_realize(SYS_BUS_DEVICE(&s->sysbus_xhci), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->iomem, 0,
         sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sysbus_xhci), 0));
    sysbus_init_mmio(sbd, &s->iomem);

    /*
     * Device Configuration
     */
    s->regs[R_GHWPARAMS0] = 0x40204048 | s->cfg.mode;
    s->regs[R_GHWPARAMS1] = 0x222493b;
    s->regs[R_GHWPARAMS2] = 0x12345678;
    s->regs[R_GHWPARAMS3] = 0x618c088;
    s->regs[R_GHWPARAMS4] = 0x47822004;
    s->regs[R_GHWPARAMS5] = 0x4202088;
    s->regs[R_GHWPARAMS6] = 0x7850c20;
    s->regs[R_GHWPARAMS7] = 0x0;
    s->regs[R_GHWPARAMS8] = 0x478;
}

static void usb_dwc3_init(Object *obj)
{
    USBDWC3 *s = USB_DWC3(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_USB_DWC3, DWC3_SIZE);
    reg_array =
        register_init_block32(DEVICE(obj), usb_dwc3_regs_info,
                              ARRAY_SIZE(usb_dwc3_regs_info),
                              s->regs_info, s->regs,
                              &usb_dwc3_ops,
                              USB_DWC3_ERR_DEBUG,
                              USB_DWC3_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                DWC3_GLOBAL_OFFSET,
                                &reg_array->mem);
    object_initialize_child(obj, "dwc3-xhci", &s->sysbus_xhci,
                            TYPE_XHCI_SYSBUS);
    qdev_alias_all_properties(DEVICE(&s->sysbus_xhci), obj);

    s->cfg.mode = HOST_MODE;
}

static const VMStateDescription vmstate_usb_dwc3 = {
    .name = "usb-dwc3",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, USBDWC3, USB_DWC3_R_MAX),
        VMSTATE_UINT8(cfg.mode, USBDWC3),
        VMSTATE_UINT32(cfg.dwc_usb3_user, USBDWC3),
        VMSTATE_END_OF_LIST()
    }
};

static const Property usb_dwc3_properties[] = {
    DEFINE_PROP_UINT32("DWC_USB3_USERID", USBDWC3, cfg.dwc_usb3_user,
                       0x12345678),
};

static void usb_dwc3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, usb_dwc3_reset);
    dc->realize = usb_dwc3_realize;
    dc->vmsd = &vmstate_usb_dwc3;
    device_class_set_props(dc, usb_dwc3_properties);
}

static const TypeInfo usb_dwc3_info = {
    .name          = TYPE_USB_DWC3,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(USBDWC3),
    .class_init    = usb_dwc3_class_init,
    .instance_init = usb_dwc3_init,
};

static void usb_dwc3_register_types(void)
{
    type_register_static(&usb_dwc3_info);
}

type_init(usb_dwc3_register_types)
