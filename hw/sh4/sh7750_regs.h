/*
 * SH-7750 memory-mapped registers
 * This file based on information provided in the following document:
 * "Hitachi SuperH (tm) RISC engine. SH7750 Series (SH7750, SH7750S)
 *  Hardware Manual"
 *  Document Number ADE-602-124C, Rev. 4.0, 4/21/00, Hitachi Ltd.
 *
 * Copyright (C) 2001 OKTET Ltd., St.-Petersburg, Russia
 * Author: Alexandra Kossovsky <sasha@oktet.ru>
 *         Victor V. Vengerov <vvv@oktet.ru>
 *
 * The license and distribution terms for this file may be
 * found in this file hereafter or at http://www.rtems.com/license/LICENSE.
 *
 *                       LICENSE INFORMATION
 *
 * RTEMS is free software; you can redistribute it and/or modify it under
 * terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.  RTEMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details. You should have received
 * a copy of the GNU General Public License along with RTEMS; see
 * file COPYING. If not, write to the Free Software Foundation, 675
 * Mass Ave, Cambridge, MA 02139, USA.
 *
 * As a special exception, including RTEMS header files in a file,
 * instantiating RTEMS generics or templates, or linking other files
 * with RTEMS objects to produce an executable application, does not
 * by itself cause the resulting executable application to be covered
 * by the GNU General Public License. This exception does not
 * however invalidate any other reasons why the executable file might be
 * covered by the GNU Public License.
 *
 * @(#) sh7750_regs.h,v 1.2.4.1 2003/09/04 18:46:00 joel Exp
 */

#ifndef SH7750_REGS_H
#define SH7750_REGS_H

/*
 * All register has 2 addresses: in 0xff000000 - 0xffffffff (P4 address)  and
 * in 0x1f000000 - 0x1fffffff (area 7 address)
 */
#define SH7750_P4_BASE       0xff000000 /* Accessible only in privileged mode */
#define SH7750_A7_BASE       0x1f000000 /* Accessible only using TLB */

#define SH7750_P4_REG32(ofs) (SH7750_P4_BASE + (ofs))
#define SH7750_A7_REG32(ofs) (SH7750_A7_BASE + (ofs))

/*
 * MMU Registers
 */

/* Page Table Entry High register - PTEH */
#define SH7750_PTEH_REGOFS    0x000000  /* offset */
#define SH7750_PTEH           SH7750_P4_REG32(SH7750_PTEH_REGOFS)
#define SH7750_PTEH_A7        SH7750_A7_REG32(SH7750_PTEH_REGOFS)
#define SH7750_PTEH_VPN       0xfffffd00 /* Virtual page number */
#define SH7750_PTEH_VPN_S     10
#define SH7750_PTEH_ASID      0x000000ff /* Address space identifier */
#define SH7750_PTEH_ASID_S    0

/* Page Table Entry Low register - PTEL */
#define SH7750_PTEL_REGOFS    0x000004  /* offset */
#define SH7750_PTEL           SH7750_P4_REG32(SH7750_PTEL_REGOFS)
#define SH7750_PTEL_A7        SH7750_A7_REG32(SH7750_PTEL_REGOFS)
#define SH7750_PTEL_PPN       0x1ffffc00 /* Physical page number */
#define SH7750_PTEL_PPN_S     10
#define SH7750_PTEL_V         0x00000100 /* Validity (0-entry is invalid) */
#define SH7750_PTEL_SZ1       0x00000080 /* Page size bit 1 */
#define SH7750_PTEL_SZ0       0x00000010 /* Page size bit 0 */
#define SH7750_PTEL_SZ_1KB    0x00000000 /*   1-kbyte page */
#define SH7750_PTEL_SZ_4KB    0x00000010 /*   4-kbyte page */
#define SH7750_PTEL_SZ_64KB   0x00000080 /*   64-kbyte page */
#define SH7750_PTEL_SZ_1MB    0x00000090 /*   1-Mbyte page */
#define SH7750_PTEL_PR        0x00000060 /* Protection Key Data */
#define SH7750_PTEL_PR_ROPO   0x00000000 /*   read-only in priv mode */
#define SH7750_PTEL_PR_RWPO   0x00000020 /*   read-write in priv mode */
#define SH7750_PTEL_PR_ROPU   0x00000040 /*   read-only in priv or user mode */
#define SH7750_PTEL_PR_RWPU   0x00000060 /*   read-write in priv or user mode */
#define SH7750_PTEL_C         0x00000008 /* Cacheability */
                                         /*   (0 - page not cacheable) */
#define SH7750_PTEL_D         0x00000004 /* Dirty bit (1 - write has been */
                                         /*   performed to a page) */
#define SH7750_PTEL_SH        0x00000002 /* Share Status bit (1 - page are */
                                         /*   shared by processes) */
#define SH7750_PTEL_WT        0x00000001 /* Write-through bit, specifies the */
                                         /*   cache write mode: */
                                         /*     0 - Copy-back mode */
                                         /*     1 - Write-through mode */

/* Page Table Entry Assistance register - PTEA */
#define SH7750_PTEA_REGOFS    0x000034 /* offset */
#define SH7750_PTEA           SH7750_P4_REG32(SH7750_PTEA_REGOFS)
#define SH7750_PTEA_A7        SH7750_A7_REG32(SH7750_PTEA_REGOFS)
#define SH7750_PTEA_TC        0x00000008 /* Timing Control bit */
                                         /*   0 - use area 5 wait states */
                                         /*   1 - use area 6 wait states */
#define SH7750_PTEA_SA        0x00000007 /* Space Attribute bits: */
#define SH7750_PTEA_SA_UNDEF  0x00000000 /*   0 - undefined */
#define SH7750_PTEA_SA_IOVAR  0x00000001 /*   1 - variable-size I/O space */
#define SH7750_PTEA_SA_IO8    0x00000002 /*   2 - 8-bit I/O space */
#define SH7750_PTEA_SA_IO16   0x00000003 /*   3 - 16-bit I/O space */
#define SH7750_PTEA_SA_CMEM8  0x00000004 /*   4 - 8-bit common memory space */
#define SH7750_PTEA_SA_CMEM16 0x00000005 /*   5 - 16-bit common memory space */
#define SH7750_PTEA_SA_AMEM8  0x00000006 /*   6 - 8-bit attr memory space */
#define SH7750_PTEA_SA_AMEM16 0x00000007 /*   7 - 16-bit attr memory space */


/* Translation table base register */
#define SH7750_TTB_REGOFS     0x000008 /* offset */
#define SH7750_TTB            SH7750_P4_REG32(SH7750_TTB_REGOFS)
#define SH7750_TTB_A7         SH7750_A7_REG32(SH7750_TTB_REGOFS)

/* TLB exeption address register - TEA */
#define SH7750_TEA_REGOFS     0x00000c /* offset */
#define SH7750_TEA            SH7750_P4_REG32(SH7750_TEA_REGOFS)
#define SH7750_TEA_A7         SH7750_A7_REG32(SH7750_TEA_REGOFS)

/* MMU control register - MMUCR */
#define SH7750_MMUCR_REGOFS   0x000010 /* offset */
#define SH7750_MMUCR          SH7750_P4_REG32(SH7750_MMUCR_REGOFS)
#define SH7750_MMUCR_A7       SH7750_A7_REG32(SH7750_MMUCR_REGOFS)
#define SH7750_MMUCR_AT       0x00000001 /* Address translation bit */
#define SH7750_MMUCR_TI       0x00000004 /* TLB invalidate */
#define SH7750_MMUCR_SV       0x00000100 /* Single Virtual Mode bit */
#define SH7750_MMUCR_SQMD     0x00000200 /* Store Queue Mode bit */
#define SH7750_MMUCR_URC      0x0000FC00 /* UTLB Replace Counter */
#define SH7750_MMUCR_URC_S    10
#define SH7750_MMUCR_URB      0x00FC0000 /* UTLB Replace Boundary */
#define SH7750_MMUCR_URB_S    18
#define SH7750_MMUCR_LRUI     0xFC000000 /* Least Recently Used ITLB */
#define SH7750_MMUCR_LRUI_S   26




/*
 * Cache registers
 *   IC -- instructions cache
 *   OC -- operand cache
 */

/* Cache Control Register - CCR */
#define SH7750_CCR_REGOFS     0x00001c /* offset */
#define SH7750_CCR            SH7750_P4_REG32(SH7750_CCR_REGOFS)
#define SH7750_CCR_A7         SH7750_A7_REG32(SH7750_CCR_REGOFS)

#define SH7750_CCR_IIX      0x00008000 /* IC index enable bit */
#define SH7750_CCR_ICI      0x00000800 /* IC invalidation bit: */
                                       /*  set it to clear IC */
#define SH7750_CCR_ICE      0x00000100 /* IC enable bit */
#define SH7750_CCR_OIX      0x00000080 /* OC index enable bit */
#define SH7750_CCR_ORA      0x00000020 /* OC RAM enable bit */
                                       /*  if you set OCE = 0, */
                                       /*  you should set ORA = 0 */
#define SH7750_CCR_OCI      0x00000008 /* OC invalidation bit */
#define SH7750_CCR_CB       0x00000004 /* Copy-back bit for P1 area */
#define SH7750_CCR_WT       0x00000002 /* Write-through bit for P0,U0,P3 area */
#define SH7750_CCR_OCE      0x00000001 /* OC enable bit */

/* Queue address control register 0 - QACR0 */
#define SH7750_QACR0_REGOFS   0x000038  /* offset */
#define SH7750_QACR0          SH7750_P4_REG32(SH7750_QACR0_REGOFS)
#define SH7750_QACR0_A7       SH7750_A7_REG32(SH7750_QACR0_REGOFS)

/* Queue address control register 1 - QACR1 */
#define SH7750_QACR1_REGOFS   0x00003c /* offset */
#define SH7750_QACR1          SH7750_P4_REG32(SH7750_QACR1_REGOFS)
#define SH7750_QACR1_A7       SH7750_A7_REG32(SH7750_QACR1_REGOFS)


/*
 * Exeption-related registers
 */

/* Immediate data for TRAPA instruction - TRA */
#define SH7750_TRA_REGOFS     0x000020 /* offset */
#define SH7750_TRA            SH7750_P4_REG32(SH7750_TRA_REGOFS)
#define SH7750_TRA_A7         SH7750_A7_REG32(SH7750_TRA_REGOFS)

#define SH7750_TRA_IMM      0x000003fd /* Immediate data operand */
#define SH7750_TRA_IMM_S    2

/* Exeption event register - EXPEVT */
#define SH7750_EXPEVT_REGOFS  0x000024
#define SH7750_EXPEVT         SH7750_P4_REG32(SH7750_EXPEVT_REGOFS)
#define SH7750_EXPEVT_A7      SH7750_A7_REG32(SH7750_EXPEVT_REGOFS)

#define SH7750_EXPEVT_EX      0x00000fff /* Exeption code */
#define SH7750_EXPEVT_EX_S    0

/* Interrupt event register */
#define SH7750_INTEVT_REGOFS  0x000028
#define SH7750_INTEVT         SH7750_P4_REG32(SH7750_INTEVT_REGOFS)
#define SH7750_INTEVT_A7      SH7750_A7_REG32(SH7750_INTEVT_REGOFS)
#define SH7750_INTEVT_EX    0x00000fff /* Exeption code */
#define SH7750_INTEVT_EX_S  0

/*
 * Exception/interrupt codes
 */
#define SH7750_EVT_TO_NUM(evt)  ((evt) >> 5)

/* Reset exception category */
#define SH7750_EVT_POWER_ON_RST        0x000 /* Power-on reset */
#define SH7750_EVT_MANUAL_RST          0x020 /* Manual reset */
#define SH7750_EVT_TLB_MULT_HIT        0x140 /* TLB multiple-hit exception */

/* General exception category */
#define SH7750_EVT_USER_BREAK          0x1E0 /* User break */
#define SH7750_EVT_IADDR_ERR           0x0E0 /* Instruction address error */
#define SH7750_EVT_TLB_READ_MISS       0x040 /* ITLB miss exception / */
                                             /*    DTLB miss exception (read) */
#define SH7750_EVT_TLB_READ_PROTV      0x0A0 /* ITLB protection violation, */
                                             /* DTLB protection violation */
                                             /*    (read) */
#define SH7750_EVT_ILLEGAL_INSTR       0x180 /* General Illegal Instruction */
                                             /*    exception */
#define SH7750_EVT_SLOT_ILLEGAL_INSTR  0x1A0 /* Slot Illegal Instruction */
                                             /*    exception */
#define SH7750_EVT_FPU_DISABLE         0x800 /* General FPU disable exception */
#define SH7750_EVT_SLOT_FPU_DISABLE    0x820 /* Slot FPU disable exception */
#define SH7750_EVT_DATA_READ_ERR       0x0E0 /* Data address error (read) */
#define SH7750_EVT_DATA_WRITE_ERR      0x100 /* Data address error (write) */
#define SH7750_EVT_DTLB_WRITE_MISS     0x060 /* DTLB miss exception (write) */
#define SH7750_EVT_DTLB_WRITE_PROTV    0x0C0 /* DTLB protection violation */
                                             /*    exception (write) */
#define SH7750_EVT_FPU_EXCEPTION       0x120 /* FPU exception */
#define SH7750_EVT_INITIAL_PGWRITE     0x080 /* Initial Page Write exception */
#define SH7750_EVT_TRAPA               0x160 /* Unconditional trap (TRAPA) */

/* Interrupt exception category */
#define SH7750_EVT_NMI                 0x1C0 /* Non-maskable interrupt */
#define SH7750_EVT_IRQ0                0x200 /* External Interrupt 0 */
#define SH7750_EVT_IRQ1                0x220 /* External Interrupt 1 */
#define SH7750_EVT_IRQ2                0x240 /* External Interrupt 2 */
#define SH7750_EVT_IRQ3                0x260 /* External Interrupt 3 */
#define SH7750_EVT_IRQ4                0x280 /* External Interrupt 4 */
#define SH7750_EVT_IRQ5                0x2A0 /* External Interrupt 5 */
#define SH7750_EVT_IRQ6                0x2C0 /* External Interrupt 6 */
#define SH7750_EVT_IRQ7                0x2E0 /* External Interrupt 7 */
#define SH7750_EVT_IRQ8                0x300 /* External Interrupt 8 */
#define SH7750_EVT_IRQ9                0x320 /* External Interrupt 9 */
#define SH7750_EVT_IRQA                0x340 /* External Interrupt A */
#define SH7750_EVT_IRQB                0x360 /* External Interrupt B */
#define SH7750_EVT_IRQC                0x380 /* External Interrupt C */
#define SH7750_EVT_IRQD                0x3A0 /* External Interrupt D */
#define SH7750_EVT_IRQE                0x3C0 /* External Interrupt E */

/* Peripheral Module Interrupts - Timer Unit (TMU) */
#define SH7750_EVT_TUNI0               0x400 /* TMU Underflow Interrupt 0 */
#define SH7750_EVT_TUNI1               0x420 /* TMU Underflow Interrupt 1 */
#define SH7750_EVT_TUNI2               0x440 /* TMU Underflow Interrupt 2 */
#define SH7750_EVT_TICPI2              0x460 /* TMU Input Capture Interrupt 2 */

/* Peripheral Module Interrupts - Real-Time Clock (RTC) */
#define SH7750_EVT_RTC_ATI             0x480 /* Alarm Interrupt Request */
#define SH7750_EVT_RTC_PRI             0x4A0 /* Periodic Interrupt Request */
#define SH7750_EVT_RTC_CUI             0x4C0 /* Carry Interrupt Request */

/* Peripheral Module Interrupts - Serial Communication Interface (SCI) */
#define SH7750_EVT_SCI_ERI             0x4E0 /* Receive Error */
#define SH7750_EVT_SCI_RXI             0x500 /* Receive Data Register Full */
#define SH7750_EVT_SCI_TXI             0x520 /* Transmit Data Register Empty */
#define SH7750_EVT_SCI_TEI             0x540 /* Transmit End */

/* Peripheral Module Interrupts - Watchdog Timer (WDT) */
#define SH7750_EVT_WDT_ITI             0x560 /* Interval Timer Interrupt */
                                             /*    (used when WDT operates in */
                                             /*    interval timer mode) */

/* Peripheral Module Interrupts - Memory Refresh Unit (REF) */
#define SH7750_EVT_REF_RCMI            0x580 /* Compare-match Interrupt */
#define SH7750_EVT_REF_ROVI            0x5A0 /* Refresh Counter Overflow */
                                             /*    interrupt */

/* Peripheral Module Interrupts - Hitachi User Debug Interface (H-UDI) */
#define SH7750_EVT_HUDI                0x600 /* UDI interrupt */

/* Peripheral Module Interrupts - General-Purpose I/O (GPIO) */
#define SH7750_EVT_GPIO                0x620 /* GPIO Interrupt */

/* Peripheral Module Interrupts - DMA Controller (DMAC) */
#define SH7750_EVT_DMAC_DMTE0          0x640 /* DMAC 0 Transfer End Interrupt */
#define SH7750_EVT_DMAC_DMTE1          0x660 /* DMAC 1 Transfer End Interrupt */
#define SH7750_EVT_DMAC_DMTE2          0x680 /* DMAC 2 Transfer End Interrupt */
#define SH7750_EVT_DMAC_DMTE3          0x6A0 /* DMAC 3 Transfer End Interrupt */
#define SH7750_EVT_DMAC_DMAE           0x6C0 /* DMAC Address Error Interrupt */

/* Peripheral Module Interrupts Serial Communication Interface w/ FIFO (SCIF) */
#define SH7750_EVT_SCIF_ERI            0x700 /* Receive Error */
#define SH7750_EVT_SCIF_RXI            0x720 /* Receive FIFO Data Full or */
                                             /* Receive Data ready interrupt */
#define SH7750_EVT_SCIF_BRI            0x740 /* Break or overrun error */
#define SH7750_EVT_SCIF_TXI            0x760 /* Transmit FIFO Data Empty */

/*
 * Power Management
 */
#define SH7750_STBCR_REGOFS   0xC00004 /* offset */
#define SH7750_STBCR          SH7750_P4_REG32(SH7750_STBCR_REGOFS)
#define SH7750_STBCR_A7       SH7750_A7_REG32(SH7750_STBCR_REGOFS)

#define SH7750_STBCR_STBY     0x80 /* Specifies a transition to standby mode: */
                                   /*   0 Transition to SLEEP mode on SLEEP */
                                   /*   1 Transition to STANDBY mode on SLEEP */
#define SH7750_STBCR_PHZ      0x40 /* State of peripheral module pins in */
                                   /* standby mode: */
                                   /*   0 normal state */
                                   /*   1 high-impendance state */

#define SH7750_STBCR_PPU      0x20 /* Peripheral module pins pull-up controls */
#define SH7750_STBCR_MSTP4    0x10 /* Stopping the clock supply to DMAC */
#define SH7750_STBCR_DMAC_STP SH7750_STBCR_MSTP4
#define SH7750_STBCR_MSTP3    0x08 /* Stopping the clock supply to SCIF */
#define SH7750_STBCR_SCIF_STP SH7750_STBCR_MSTP3
#define SH7750_STBCR_MSTP2    0x04 /* Stopping the clock supply to TMU */
#define SH7750_STBCR_TMU_STP  SH7750_STBCR_MSTP2
#define SH7750_STBCR_MSTP1    0x02 /* Stopping the clock supply to RTC */
#define SH7750_STBCR_RTC_STP  SH7750_STBCR_MSTP1
#define SH7750_STBCR_MSPT0    0x01 /* Stopping the clock supply to SCI */
#define SH7750_STBCR_SCI_STP  SH7750_STBCR_MSTP0

#define SH7750_STBCR_STBY     0x80


#define SH7750_STBCR2_REGOFS  0xC00010 /* offset */
#define SH7750_STBCR2         SH7750_P4_REG32(SH7750_STBCR2_REGOFS)
#define SH7750_STBCR2_A7      SH7750_A7_REG32(SH7750_STBCR2_REGOFS)

#define SH7750_STBCR2_DSLP    0x80 /* Specifies transition to deep sleep mode */
                                   /*   0 transition to sleep or standby mode */
                                   /*     as it is specified in STBY bit */
                                   /*   1 transition to deep sleep mode on */
                                   /*     execution of SLEEP instruction */
#define SH7750_STBCR2_MSTP6   0x02 /* Stopping the clock supply to the */
                                   /*   Store Queue in the cache controller */
#define SH7750_STBCR2_SQ_STP  SH7750_STBCR2_MSTP6
#define SH7750_STBCR2_MSTP5   0x01 /* Stopping the clock supply to the  */
                                   /*   User Break Controller (UBC) */
#define SH7750_STBCR2_UBC_STP SH7750_STBCR2_MSTP5

/*
 * Clock Pulse Generator (CPG)
 */
#define SH7750_FRQCR_REGOFS   0xC00000 /* offset */
#define SH7750_FRQCR          SH7750_P4_REG32(SH7750_FRQCR_REGOFS)
#define SH7750_FRQCR_A7       SH7750_A7_REG32(SH7750_FRQCR_REGOFS)

#define SH7750_FRQCR_CKOEN    0x0800 /* Clock Output Enable */
                                     /*    0 - CKIO pin goes to HiZ/pullup */
                                     /*    1 - Clock is output from CKIO */
#define SH7750_FRQCR_PLL1EN   0x0400 /* PLL circuit 1 enable */
#define SH7750_FRQCR_PLL2EN   0x0200 /* PLL circuit 2 enable */

#define SH7750_FRQCR_IFC      0x01C0 /* CPU clock frequency division ratio: */
#define SH7750_FRQCR_IFCDIV1  0x0000 /*    0 - * 1 */
#define SH7750_FRQCR_IFCDIV2  0x0040 /*    1 - * 1/2 */
#define SH7750_FRQCR_IFCDIV3  0x0080 /*    2 - * 1/3 */
#define SH7750_FRQCR_IFCDIV4  0x00C0 /*    3 - * 1/4 */
#define SH7750_FRQCR_IFCDIV6  0x0100 /*    4 - * 1/6 */
#define SH7750_FRQCR_IFCDIV8  0x0140 /*    5 - * 1/8 */

#define SH7750_FRQCR_BFC      0x0038 /* Bus clock frequency division ratio: */
#define SH7750_FRQCR_BFCDIV1  0x0000 /*    0 - * 1 */
#define SH7750_FRQCR_BFCDIV2  0x0008 /*    1 - * 1/2 */
#define SH7750_FRQCR_BFCDIV3  0x0010 /*    2 - * 1/3 */
#define SH7750_FRQCR_BFCDIV4  0x0018 /*    3 - * 1/4 */
#define SH7750_FRQCR_BFCDIV6  0x0020 /*    4 - * 1/6 */
#define SH7750_FRQCR_BFCDIV8  0x0028 /*    5 - * 1/8 */

#define SH7750_FRQCR_PFC      0x0007 /* Peripheral module clock frequency */
                                     /*    division ratio: */
#define SH7750_FRQCR_PFCDIV2  0x0000 /*    0 - * 1/2 */
#define SH7750_FRQCR_PFCDIV3  0x0001 /*    1 - * 1/3 */
#define SH7750_FRQCR_PFCDIV4  0x0002 /*    2 - * 1/4 */
#define SH7750_FRQCR_PFCDIV6  0x0003 /*    3 - * 1/6 */
#define SH7750_FRQCR_PFCDIV8  0x0004 /*    4 - * 1/8 */

/*
 * Watchdog Timer (WDT)
 */

/* Watchdog Timer Counter register - WTCNT */
#define SH7750_WTCNT_REGOFS   0xC00008 /* offset */
#define SH7750_WTCNT          SH7750_P4_REG32(SH7750_WTCNT_REGOFS)
#define SH7750_WTCNT_A7       SH7750_A7_REG32(SH7750_WTCNT_REGOFS)
#define SH7750_WTCNT_KEY      0x5A00 /* When WTCNT byte register written, you */
                                     /* have to set the upper byte to 0x5A */

/* Watchdog Timer Control/Status register - WTCSR */
#define SH7750_WTCSR_REGOFS   0xC0000C /* offset */
#define SH7750_WTCSR          SH7750_P4_REG32(SH7750_WTCSR_REGOFS)
#define SH7750_WTCSR_A7       SH7750_A7_REG32(SH7750_WTCSR_REGOFS)
#define SH7750_WTCSR_KEY      0xA500 /* When WTCSR byte register written, you */
                                     /* have to set the upper byte to 0xA5 */
#define SH7750_WTCSR_TME      0x80 /* Timer enable (1-upcount start) */
#define SH7750_WTCSR_MODE     0x40 /* Timer Mode Select: */
#define SH7750_WTCSR_MODE_WT  0x40 /*    Watchdog Timer Mode */
#define SH7750_WTCSR_MODE_IT  0x00 /*    Interval Timer Mode */
#define SH7750_WTCSR_RSTS     0x20 /* Reset Select: */
#define SH7750_WTCSR_RST_MAN  0x20 /*    Manual Reset */
#define SH7750_WTCSR_RST_PWR  0x00 /*    Power-on Reset */
#define SH7750_WTCSR_WOVF     0x10 /* Watchdog Timer Overflow Flag */
#define SH7750_WTCSR_IOVF     0x08 /* Interval Timer Overflow Flag */
#define SH7750_WTCSR_CKS      0x07 /* Clock Select: */
#define SH7750_WTCSR_CKS_DIV32   0x00 /*   1/32 of frequency divider 2 input */
#define SH7750_WTCSR_CKS_DIV64   0x01 /*   1/64 */
#define SH7750_WTCSR_CKS_DIV128  0x02 /*   1/128 */
#define SH7750_WTCSR_CKS_DIV256  0x03 /*   1/256 */
#define SH7750_WTCSR_CKS_DIV512  0x04 /*   1/512 */
#define SH7750_WTCSR_CKS_DIV1024 0x05 /*   1/1024 */
#define SH7750_WTCSR_CKS_DIV2048 0x06 /*   1/2048 */
#define SH7750_WTCSR_CKS_DIV4096 0x07 /*   1/4096 */

/*
 * Real-Time Clock (RTC)
 */
/* 64-Hz Counter Register (byte, read-only) - R64CNT */
#define SH7750_R64CNT_REGOFS  0xC80000 /* offset */
#define SH7750_R64CNT         SH7750_P4_REG32(SH7750_R64CNT_REGOFS)
#define SH7750_R64CNT_A7      SH7750_A7_REG32(SH7750_R64CNT_REGOFS)

/* Second Counter Register (byte, BCD-coded) - RSECCNT */
#define SH7750_RSECCNT_REGOFS 0xC80004 /* offset */
#define SH7750_RSECCNT        SH7750_P4_REG32(SH7750_RSECCNT_REGOFS)
#define SH7750_RSECCNT_A7     SH7750_A7_REG32(SH7750_RSECCNT_REGOFS)

/* Minute Counter Register (byte, BCD-coded) - RMINCNT */
#define SH7750_RMINCNT_REGOFS 0xC80008 /* offset */
#define SH7750_RMINCNT        SH7750_P4_REG32(SH7750_RMINCNT_REGOFS)
#define SH7750_RMINCNT_A7     SH7750_A7_REG32(SH7750_RMINCNT_REGOFS)

/* Hour Counter Register (byte, BCD-coded) - RHRCNT */
#define SH7750_RHRCNT_REGOFS  0xC8000C /* offset */
#define SH7750_RHRCNT         SH7750_P4_REG32(SH7750_RHRCNT_REGOFS)
#define SH7750_RHRCNT_A7      SH7750_A7_REG32(SH7750_RHRCNT_REGOFS)

/* Day-of-Week Counter Register (byte) - RWKCNT */
#define SH7750_RWKCNT_REGOFS  0xC80010 /* offset */
#define SH7750_RWKCNT         SH7750_P4_REG32(SH7750_RWKCNT_REGOFS)
#define SH7750_RWKCNT_A7      SH7750_A7_REG32(SH7750_RWKCNT_REGOFS)

#define SH7750_RWKCNT_SUN     0 /* Sunday */
#define SH7750_RWKCNT_MON     1 /* Monday */
#define SH7750_RWKCNT_TUE     2 /* Tuesday */
#define SH7750_RWKCNT_WED     3 /* Wednesday */
#define SH7750_RWKCNT_THU     4 /* Thursday */
#define SH7750_RWKCNT_FRI     5 /* Friday */
#define SH7750_RWKCNT_SAT     6 /* Saturday */

/* Day Counter Register (byte, BCD-coded) - RDAYCNT */
#define SH7750_RDAYCNT_REGOFS 0xC80014 /* offset */
#define SH7750_RDAYCNT        SH7750_P4_REG32(SH7750_RDAYCNT_REGOFS)
#define SH7750_RDAYCNT_A7     SH7750_A7_REG32(SH7750_RDAYCNT_REGOFS)

/* Month Counter Register (byte, BCD-coded) - RMONCNT */
#define SH7750_RMONCNT_REGOFS 0xC80018 /* offset */
#define SH7750_RMONCNT        SH7750_P4_REG32(SH7750_RMONCNT_REGOFS)
#define SH7750_RMONCNT_A7     SH7750_A7_REG32(SH7750_RMONCNT_REGOFS)

/* Year Counter Register (half, BCD-coded) - RYRCNT */
#define SH7750_RYRCNT_REGOFS  0xC8001C /* offset */
#define SH7750_RYRCNT         SH7750_P4_REG32(SH7750_RYRCNT_REGOFS)
#define SH7750_RYRCNT_A7      SH7750_A7_REG32(SH7750_RYRCNT_REGOFS)

/* Second Alarm Register (byte, BCD-coded) - RSECAR */
#define SH7750_RSECAR_REGOFS  0xC80020 /* offset */
#define SH7750_RSECAR         SH7750_P4_REG32(SH7750_RSECAR_REGOFS)
#define SH7750_RSECAR_A7      SH7750_A7_REG32(SH7750_RSECAR_REGOFS)
#define SH7750_RSECAR_ENB     0x80 /* Second Alarm Enable */

/* Minute Alarm Register (byte, BCD-coded) - RMINAR */
#define SH7750_RMINAR_REGOFS  0xC80024 /* offset */
#define SH7750_RMINAR         SH7750_P4_REG32(SH7750_RMINAR_REGOFS)
#define SH7750_RMINAR_A7      SH7750_A7_REG32(SH7750_RMINAR_REGOFS)
#define SH7750_RMINAR_ENB     0x80 /* Minute Alarm Enable */

/* Hour Alarm Register (byte, BCD-coded) - RHRAR */
#define SH7750_RHRAR_REGOFS   0xC80028 /* offset */
#define SH7750_RHRAR          SH7750_P4_REG32(SH7750_RHRAR_REGOFS)
#define SH7750_RHRAR_A7       SH7750_A7_REG32(SH7750_RHRAR_REGOFS)
#define SH7750_RHRAR_ENB      0x80 /* Hour Alarm Enable */

/* Day-of-Week Alarm Register (byte) - RWKAR */
#define SH7750_RWKAR_REGOFS   0xC8002C /* offset */
#define SH7750_RWKAR          SH7750_P4_REG32(SH7750_RWKAR_REGOFS)
#define SH7750_RWKAR_A7       SH7750_A7_REG32(SH7750_RWKAR_REGOFS)
#define SH7750_RWKAR_ENB      0x80 /* Day-of-week Alarm Enable */

#define SH7750_RWKAR_SUN      0 /* Sunday */
#define SH7750_RWKAR_MON      1 /* Monday */
#define SH7750_RWKAR_TUE      2 /* Tuesday */
#define SH7750_RWKAR_WED      3 /* Wednesday */
#define SH7750_RWKAR_THU      4 /* Thursday */
#define SH7750_RWKAR_FRI      5 /* Friday */
#define SH7750_RWKAR_SAT      6 /* Saturday */

/* Day Alarm Register (byte, BCD-coded) - RDAYAR */
#define SH7750_RDAYAR_REGOFS  0xC80030 /* offset */
#define SH7750_RDAYAR         SH7750_P4_REG32(SH7750_RDAYAR_REGOFS)
#define SH7750_RDAYAR_A7      SH7750_A7_REG32(SH7750_RDAYAR_REGOFS)
#define SH7750_RDAYAR_ENB     0x80 /* Day Alarm Enable */

/* Month Counter Register (byte, BCD-coded) - RMONAR */
#define SH7750_RMONAR_REGOFS  0xC80034 /* offset */
#define SH7750_RMONAR         SH7750_P4_REG32(SH7750_RMONAR_REGOFS)
#define SH7750_RMONAR_A7      SH7750_A7_REG32(SH7750_RMONAR_REGOFS)
#define SH7750_RMONAR_ENB     0x80 /* Month Alarm Enable */

/* RTC Control Register 1 (byte) - RCR1 */
#define SH7750_RCR1_REGOFS    0xC80038 /* offset */
#define SH7750_RCR1           SH7750_P4_REG32(SH7750_RCR1_REGOFS)
#define SH7750_RCR1_A7        SH7750_A7_REG32(SH7750_RCR1_REGOFS)
#define SH7750_RCR1_CF        0x80 /* Carry Flag */
#define SH7750_RCR1_CIE       0x10 /* Carry Interrupt Enable */
#define SH7750_RCR1_AIE       0x08 /* Alarm Interrupt Enable */
#define SH7750_RCR1_AF        0x01 /* Alarm Flag */

/* RTC Control Register 2 (byte) - RCR2 */
#define SH7750_RCR2_REGOFS    0xC8003C /* offset */
#define SH7750_RCR2           SH7750_P4_REG32(SH7750_RCR2_REGOFS)
#define SH7750_RCR2_A7        SH7750_A7_REG32(SH7750_RCR2_REGOFS)
#define SH7750_RCR2_PEF        0x80 /* Periodic Interrupt Flag */
#define SH7750_RCR2_PES        0x70 /* Periodic Interrupt Enable: */
#define SH7750_RCR2_PES_DIS    0x00 /*   Periodic Interrupt Disabled */
#define SH7750_RCR2_PES_DIV256 0x10 /*   Generated at 1/256 sec interval */
#define SH7750_RCR2_PES_DIV64  0x20 /*   Generated at 1/64 sec interval */
#define SH7750_RCR2_PES_DIV16  0x30 /*   Generated at 1/16 sec interval */
#define SH7750_RCR2_PES_DIV4   0x40 /*   Generated at 1/4 sec interval */
#define SH7750_RCR2_PES_DIV2   0x50 /*   Generated at 1/2 sec interval */
#define SH7750_RCR2_PES_x1     0x60 /*   Generated at 1 sec interval */
#define SH7750_RCR2_PES_x2     0x70 /*   Generated at 2 sec interval */
#define SH7750_RCR2_RTCEN      0x08 /* RTC Crystal Oscillator is Operated */
#define SH7750_RCR2_ADJ        0x04 /* 30-Second Adjastment */
#define SH7750_RCR2_RESET      0x02 /* Frequency divider circuits are reset */
#define SH7750_RCR2_START      0x01 /* 0 - sec, min, hr, day-of-week, month, */
                                    /*     year counters are stopped */
                                    /* 1 - sec, min, hr, day-of-week, month, */
                                    /*     year counters operate normally */
/*
 * Bus State Controller - BSC
 */
/* Bus Control Register 1 - BCR1 */
#define SH7750_BCR1_REGOFS    0x800000 /* offset */
#define SH7750_BCR1           SH7750_P4_REG32(SH7750_BCR1_REGOFS)
#define SH7750_BCR1_A7        SH7750_A7_REG32(SH7750_BCR1_REGOFS)
#define SH7750_BCR1_ENDIAN    0x80000000 /* Endianness (1 - little endian) */
#define SH7750_BCR1_MASTER    0x40000000 /* Master/Slave mode (1-master) */
#define SH7750_BCR1_A0MPX     0x20000000 /* Area 0 Memory Type (0-SRAM,1-MPX) */
#define SH7750_BCR1_IPUP      0x02000000 /* Input Pin Pull-up Control: */
                                         /*   0 - pull-up resistor is on for */
                                         /*       control input pins */
                                         /*   1 - pull-up resistor is off */
#define SH7750_BCR1_OPUP      0x01000000 /* Output Pin Pull-up Control: */
                                         /*   0 - pull-up resistor is on for */
                                         /*       control output pins */
                                         /*   1 - pull-up resistor is off */
#define SH7750_BCR1_A1MBC     0x00200000 /* Area 1 SRAM Byte Control Mode: */
                                         /*   0 - Area 1 SRAM is set to */
                                         /*       normal mode */
                                         /*   1 - Area 1 SRAM is set to byte */
                                         /*       control mode */
#define SH7750_BCR1_A4MBC     0x00100000 /* Area 4 SRAM Byte Control Mode: */
                                         /*   0 - Area 4 SRAM is set to */
                                         /*       normal mode */
                                         /*   1 - Area 4 SRAM is set to byte */
                                         /*       control mode */
#define SH7750_BCR1_BREQEN    0x00080000 /* BREQ Enable: */
                                         /*   0 - External requests are  not */
                                         /*       accepted */
                                         /*   1 - External requests are */
                                         /*       accepted */
#define SH7750_BCR1_PSHR      0x00040000 /* Partial Sharing Bit: */
                                         /*   0 - Master Mode */
                                         /*   1 - Partial-sharing Mode */
#define SH7750_BCR1_MEMMPX    0x00020000 /* Area 1 to 6 MPX Interface: */
                                         /*   0 - SRAM/burst ROM interface */
                                         /*   1 - MPX interface */
#define SH7750_BCR1_HIZMEM    0x00008000 /* High Impendance Control. */
                                         /*   Specifies the state of A[25:0], */
                                         /*   BS\, CSn\, RD/WR\, CE2A\, CE2B\ */
                                         /*   in standby mode and when bus is */
                                         /*   released: */
                                         /*   0 - signals go to High-Z mode */
                                         /*   1 - signals driven */
#define SH7750_BCR1_HIZCNT    0x00004000 /* High Impendance Control. */
                                         /*   Specifies the state of the */
                                         /*   RAS\, RAS2\, WEn\, CASn\, DQMn, */
                                         /*   RD\, CASS\, FRAME\, RD2\ */
                                         /*   signals in standby mode and */
                                         /* when bus is released: */
                                         /*   0 - signals go to High-Z mode */
                                         /*   1 - signals driven */
#define SH7750_BCR1_A0BST     0x00003800 /* Area 0 Burst ROM Control */
#define SH7750_BCR1_A0BST_SRAM    0x0000 /*   Area 0 accessed as SRAM i/f */
#define SH7750_BCR1_A0BST_ROM4    0x0800 /*   Area 0 accessed as burst ROM */
                                         /*   interface, 4 cosequtive access */
#define SH7750_BCR1_A0BST_ROM8    0x1000 /*   Area 0 accessed as burst ROM */
                                         /*   interface, 8 cosequtive access */
#define SH7750_BCR1_A0BST_ROM16   0x1800 /*   Area 0 accessed as burst ROM */
                                         /*   interface, 16 cosequtive access */
#define SH7750_BCR1_A0BST_ROM32   0x2000 /*   Area 0 accessed as burst ROM */
                                         /*   interface, 32 cosequtive access */

#define SH7750_BCR1_A5BST     0x00000700 /* Area 5 Burst ROM Control */
#define SH7750_BCR1_A5BST_SRAM    0x0000 /*   Area 5 accessed as SRAM i/f */
#define SH7750_BCR1_A5BST_ROM4    0x0100 /*   Area 5 accessed as burst ROM */
                                         /*   interface, 4 cosequtive access */
#define SH7750_BCR1_A5BST_ROM8    0x0200 /*   Area 5 accessed as burst ROM */
                                         /*   interface, 8 cosequtive access */
#define SH7750_BCR1_A5BST_ROM16   0x0300 /*   Area 5 accessed as burst ROM */
                                         /*   interface, 16 cosequtive access */
#define SH7750_BCR1_A5BST_ROM32   0x0400 /*   Area 5 accessed as burst ROM */
                                         /*   interface, 32 cosequtive access */

#define SH7750_BCR1_A6BST     0x000000E0 /* Area 6 Burst ROM Control */
#define SH7750_BCR1_A6BST_SRAM    0x0000 /*   Area 6 accessed as SRAM i/f */
#define SH7750_BCR1_A6BST_ROM4    0x0020 /*   Area 6 accessed as burst ROM */
                                         /*   interface, 4 cosequtive access */
#define SH7750_BCR1_A6BST_ROM8    0x0040 /*   Area 6 accessed as burst ROM */
                                         /*   interface, 8 cosequtive access */
#define SH7750_BCR1_A6BST_ROM16   0x0060 /*   Area 6 accessed as burst ROM */
                                         /*   interface, 16 cosequtive access */
#define SH7750_BCR1_A6BST_ROM32   0x0080 /*   Area 6 accessed as burst ROM */
                                         /*   interface, 32 cosequtive access */

#define SH7750_BCR1_DRAMTP        0x001C /* Area 2 and 3 Memory Type */
#define SH7750_BCR1_DRAMTP_2SRAM_3SRAM   0x0000 /* Area 2 and 3 are SRAM or */
                                                /* MPX interface. */
#define SH7750_BCR1_DRAMTP_2SRAM_3SDRAM  0x0008 /* Area 2 - SRAM/MPX, Area 3 */
                                                /* synchronous DRAM */
#define SH7750_BCR1_DRAMTP_2SDRAM_3SDRAM 0x000C /* Area 2 and 3 are */
                                                /* synchronous DRAM interface */
#define SH7750_BCR1_DRAMTP_2SRAM_3DRAM   0x0010 /* Area 2 - SRAM/MPX, Area 3 */
                                                /* DRAM interface */
#define SH7750_BCR1_DRAMTP_2DRAM_3DRAM   0x0014 /* Area 2 and 3 are DRAM */
                                                /* interface */

#define SH7750_BCR1_A56PCM    0x00000001 /* Area 5 and 6 Bus Type: */
                                         /*   0 - SRAM interface */
                                         /*   1 - PCMCIA interface */

/* Bus Control Register 2 (half) - BCR2 */
#define SH7750_BCR2_REGOFS    0x800004 /* offset */
#define SH7750_BCR2           SH7750_P4_REG32(SH7750_BCR2_REGOFS)
#define SH7750_BCR2_A7        SH7750_A7_REG32(SH7750_BCR2_REGOFS)

#define SH7750_BCR2_A0SZ      0xC000 /* Area 0 Bus Width */
#define SH7750_BCR2_A0SZ_S    14
#define SH7750_BCR2_A6SZ      0x3000 /* Area 6 Bus Width */
#define SH7750_BCR2_A6SZ_S    12
#define SH7750_BCR2_A5SZ      0x0C00 /* Area 5 Bus Width */
#define SH7750_BCR2_A5SZ_S    10
#define SH7750_BCR2_A4SZ      0x0300 /* Area 4 Bus Width */
#define SH7750_BCR2_A4SZ_S    8
#define SH7750_BCR2_A3SZ      0x00C0 /* Area 3 Bus Width */
#define SH7750_BCR2_A3SZ_S    6
#define SH7750_BCR2_A2SZ      0x0030 /* Area 2 Bus Width */
#define SH7750_BCR2_A2SZ_S    4
#define SH7750_BCR2_A1SZ      0x000C /* Area 1 Bus Width */
#define SH7750_BCR2_A1SZ_S    2
#define SH7750_BCR2_SZ_64     0 /* 64 bits */
#define SH7750_BCR2_SZ_8      1 /* 8 bits */
#define SH7750_BCR2_SZ_16     2 /* 16 bits */
#define SH7750_BCR2_SZ_32     3 /* 32 bits */
#define SH7750_BCR2_PORTEN    0x0001 /* Port Function Enable */
                                     /* 0 - D51-D32 are not used as a port */
                                     /* 1 - D51-D32 are used as a port */

/* Wait Control Register 1 - WCR1 */
#define SH7750_WCR1_REGOFS    0x800008 /* offset */
#define SH7750_WCR1           SH7750_P4_REG32(SH7750_WCR1_REGOFS)
#define SH7750_WCR1_A7        SH7750_A7_REG32(SH7750_WCR1_REGOFS)
#define SH7750_WCR1_DMAIW     0x70000000 /* DACK Device Inter-Cycle Idle */
                                         /*   specification */
#define SH7750_WCR1_DMAIW_S   28
#define SH7750_WCR1_A6IW      0x07000000 /* Area 6 Inter-Cycle Idle spec. */
#define SH7750_WCR1_A6IW_S    24
#define SH7750_WCR1_A5IW      0x00700000 /* Area 5 Inter-Cycle Idle spec. */
#define SH7750_WCR1_A5IW_S    20
#define SH7750_WCR1_A4IW      0x00070000 /* Area 4 Inter-Cycle Idle spec. */
#define SH7750_WCR1_A4IW_S    16
#define SH7750_WCR1_A3IW      0x00007000 /* Area 3 Inter-Cycle Idle spec. */
#define SH7750_WCR1_A3IW_S    12
#define SH7750_WCR1_A2IW      0x00000700 /* Area 2 Inter-Cycle Idle spec. */
#define SH7750_WCR1_A2IW_S    8
#define SH7750_WCR1_A1IW      0x00000070 /* Area 1 Inter-Cycle Idle spec. */
#define SH7750_WCR1_A1IW_S    4
#define SH7750_WCR1_A0IW      0x00000007 /* Area 0 Inter-Cycle Idle spec. */
#define SH7750_WCR1_A0IW_S    0

/* Wait Control Register 2 - WCR2 */
#define SH7750_WCR2_REGOFS    0x80000C /* offset */
#define SH7750_WCR2           SH7750_P4_REG32(SH7750_WCR2_REGOFS)
#define SH7750_WCR2_A7        SH7750_A7_REG32(SH7750_WCR2_REGOFS)

#define SH7750_WCR2_A6W       0xE0000000 /* Area 6 Wait Control */
#define SH7750_WCR2_A6W_S     29
#define SH7750_WCR2_A6B       0x1C000000 /* Area 6 Burst Pitch */
#define SH7750_WCR2_A6B_S     26
#define SH7750_WCR2_A5W       0x03800000 /* Area 5 Wait Control */
#define SH7750_WCR2_A5W_S     23
#define SH7750_WCR2_A5B       0x00700000 /* Area 5 Burst Pitch */
#define SH7750_WCR2_A5B_S     20
#define SH7750_WCR2_A4W       0x000E0000 /* Area 4 Wait Control */
#define SH7750_WCR2_A4W_S     17
#define SH7750_WCR2_A3W       0x0000E000 /* Area 3 Wait Control */
#define SH7750_WCR2_A3W_S     13
#define SH7750_WCR2_A2W       0x00000E00 /* Area 2 Wait Control */
#define SH7750_WCR2_A2W_S     9
#define SH7750_WCR2_A1W       0x000001C0 /* Area 1 Wait Control */
#define SH7750_WCR2_A1W_S     6
#define SH7750_WCR2_A0W       0x00000038 /* Area 0 Wait Control */
#define SH7750_WCR2_A0W_S     3
#define SH7750_WCR2_A0B       0x00000007 /* Area 0 Burst Pitch */
#define SH7750_WCR2_A0B_S     0

#define SH7750_WCR2_WS0       0 /* 0 wait states inserted */
#define SH7750_WCR2_WS1       1 /* 1 wait states inserted */
#define SH7750_WCR2_WS2       2 /* 2 wait states inserted */
#define SH7750_WCR2_WS3       3 /* 3 wait states inserted */
#define SH7750_WCR2_WS6       4 /* 6 wait states inserted */
#define SH7750_WCR2_WS9       5 /* 9 wait states inserted */
#define SH7750_WCR2_WS12      6 /* 12 wait states inserted */
#define SH7750_WCR2_WS15      7 /* 15 wait states inserted */

#define SH7750_WCR2_BPWS0     0 /* 0 wait states inserted from 2nd access */
#define SH7750_WCR2_BPWS1     1 /* 1 wait states inserted from 2nd access */
#define SH7750_WCR2_BPWS2     2 /* 2 wait states inserted from 2nd access */
#define SH7750_WCR2_BPWS3     3 /* 3 wait states inserted from 2nd access */
#define SH7750_WCR2_BPWS4     4 /* 4 wait states inserted from 2nd access */
#define SH7750_WCR2_BPWS5     5 /* 5 wait states inserted from 2nd access */
#define SH7750_WCR2_BPWS6     6 /* 6 wait states inserted from 2nd access */
#define SH7750_WCR2_BPWS7     7 /* 7 wait states inserted from 2nd access */

/* DRAM CAS\ Assertion Delay (area 3,2) */
#define SH7750_WCR2_DRAM_CAS_ASW1   0 /* 1 cycle */
#define SH7750_WCR2_DRAM_CAS_ASW2   1 /* 2 cycles */
#define SH7750_WCR2_DRAM_CAS_ASW3   2 /* 3 cycles */
#define SH7750_WCR2_DRAM_CAS_ASW4   3 /* 4 cycles */
#define SH7750_WCR2_DRAM_CAS_ASW7   4 /* 7 cycles */
#define SH7750_WCR2_DRAM_CAS_ASW10  5 /* 10 cycles */
#define SH7750_WCR2_DRAM_CAS_ASW13  6 /* 13 cycles */
#define SH7750_WCR2_DRAM_CAS_ASW16  7 /* 16 cycles */

/* SDRAM CAS\ Latency Cycles */
#define SH7750_WCR2_SDRAM_CAS_LAT1  1 /* 1 cycle */
#define SH7750_WCR2_SDRAM_CAS_LAT2  2 /* 2 cycles */
#define SH7750_WCR2_SDRAM_CAS_LAT3  3 /* 3 cycles */
#define SH7750_WCR2_SDRAM_CAS_LAT4  4 /* 4 cycles */
#define SH7750_WCR2_SDRAM_CAS_LAT5  5 /* 5 cycles */

/* Wait Control Register 3 - WCR3 */
#define SH7750_WCR3_REGOFS    0x800010 /* offset */
#define SH7750_WCR3           SH7750_P4_REG32(SH7750_WCR3_REGOFS)
#define SH7750_WCR3_A7        SH7750_A7_REG32(SH7750_WCR3_REGOFS)

#define SH7750_WCR3_A6S       0x04000000 /* Area 6 Write Strobe Setup time */
#define SH7750_WCR3_A6H       0x03000000 /* Area 6 Data Hold Time */
#define SH7750_WCR3_A6H_S     24
#define SH7750_WCR3_A5S       0x00400000 /* Area 5 Write Strobe Setup time */
#define SH7750_WCR3_A5H       0x00300000 /* Area 5 Data Hold Time */
#define SH7750_WCR3_A5H_S     20
#define SH7750_WCR3_A4S       0x00040000 /* Area 4 Write Strobe Setup time */
#define SH7750_WCR3_A4H       0x00030000 /* Area 4 Data Hold Time */
#define SH7750_WCR3_A4H_S     16
#define SH7750_WCR3_A3S       0x00004000 /* Area 3 Write Strobe Setup time */
#define SH7750_WCR3_A3H       0x00003000 /* Area 3 Data Hold Time */
#define SH7750_WCR3_A3H_S     12
#define SH7750_WCR3_A2S       0x00000400 /* Area 2 Write Strobe Setup time */
#define SH7750_WCR3_A2H       0x00000300 /* Area 2 Data Hold Time */
#define SH7750_WCR3_A2H_S     8
#define SH7750_WCR3_A1S       0x00000040 /* Area 1 Write Strobe Setup time */
#define SH7750_WCR3_A1H       0x00000030 /* Area 1 Data Hold Time */
#define SH7750_WCR3_A1H_S     4
#define SH7750_WCR3_A0S       0x00000004 /* Area 0 Write Strobe Setup time */
#define SH7750_WCR3_A0H       0x00000003 /* Area 0 Data Hold Time */
#define SH7750_WCR3_A0H_S     0

#define SH7750_WCR3_DHWS_0    0 /* 0 wait states data hold time */
#define SH7750_WCR3_DHWS_1    1 /* 1 wait states data hold time */
#define SH7750_WCR3_DHWS_2    2 /* 2 wait states data hold time */
#define SH7750_WCR3_DHWS_3    3 /* 3 wait states data hold time */

#define SH7750_MCR_REGOFS     0x800014 /* offset */
#define SH7750_MCR            SH7750_P4_REG32(SH7750_MCR_REGOFS)
#define SH7750_MCR_A7         SH7750_A7_REG32(SH7750_MCR_REGOFS)

#define SH7750_MCR_RASD       0x80000000 /* RAS Down mode */
#define SH7750_MCR_MRSET      0x40000000 /* SDRAM Mode Register Set */
#define SH7750_MCR_PALL       0x00000000 /* SDRAM Precharge All cmd. Mode */
#define SH7750_MCR_TRC        0x38000000 /* RAS Precharge Time at End of */
                                         /* Refresh: */
#define SH7750_MCR_TRC_0      0x00000000 /*    0 */
#define SH7750_MCR_TRC_3      0x08000000 /*    3 */
#define SH7750_MCR_TRC_6      0x10000000 /*    6 */
#define SH7750_MCR_TRC_9      0x18000000 /*    9 */
#define SH7750_MCR_TRC_12     0x20000000 /*    12 */
#define SH7750_MCR_TRC_15     0x28000000 /*    15 */
#define SH7750_MCR_TRC_18     0x30000000 /*    18 */
#define SH7750_MCR_TRC_21     0x38000000 /*    21 */

#define SH7750_MCR_TCAS       0x00800000 /* CAS Negation Period */
#define SH7750_MCR_TCAS_1     0x00000000 /*    1 */
#define SH7750_MCR_TCAS_2     0x00800000 /*    2 */

#define SH7750_MCR_TPC        0x00380000 /* DRAM: RAS Precharge Period */
                                         /* SDRAM: minimum number of cycles */
                                         /* until the next bank active cmd */
                                         /* is output after precharging */
#define SH7750_MCR_TPC_S      19
#define SH7750_MCR_TPC_SDRAM_1 0x00000000 /* 1 cycle */
#define SH7750_MCR_TPC_SDRAM_2 0x00080000 /* 2 cycles */
#define SH7750_MCR_TPC_SDRAM_3 0x00100000 /* 3 cycles */
#define SH7750_MCR_TPC_SDRAM_4 0x00180000 /* 4 cycles */
#define SH7750_MCR_TPC_SDRAM_5 0x00200000 /* 5 cycles */
#define SH7750_MCR_TPC_SDRAM_6 0x00280000 /* 6 cycles */
#define SH7750_MCR_TPC_SDRAM_7 0x00300000 /* 7 cycles */
#define SH7750_MCR_TPC_SDRAM_8 0x00380000 /* 8 cycles */

#define SH7750_MCR_RCD         0x00030000 /* DRAM: RAS-CAS Assertion Delay */
                                          /*   time */
                                          /* SDRAM: bank active-read/write */
                                          /*   command delay time */
#define SH7750_MCR_RCD_DRAM_2  0x00000000 /* DRAM delay 2 clocks */
#define SH7750_MCR_RCD_DRAM_3  0x00010000 /* DRAM delay 3 clocks */
#define SH7750_MCR_RCD_DRAM_4  0x00020000 /* DRAM delay 4 clocks */
#define SH7750_MCR_RCD_DRAM_5  0x00030000 /* DRAM delay 5 clocks */
#define SH7750_MCR_RCD_SDRAM_2 0x00010000 /* DRAM delay 2 clocks */
#define SH7750_MCR_RCD_SDRAM_3 0x00020000 /* DRAM delay 3 clocks */
#define SH7750_MCR_RCD_SDRAM_4 0x00030000 /* DRAM delay 4 clocks */

#define SH7750_MCR_TRWL       0x0000E000 /* SDRAM Write Precharge Delay */
#define SH7750_MCR_TRWL_1     0x00000000 /* 1 */
#define SH7750_MCR_TRWL_2     0x00002000 /* 2 */
#define SH7750_MCR_TRWL_3     0x00004000 /* 3 */
#define SH7750_MCR_TRWL_4     0x00006000 /* 4 */
#define SH7750_MCR_TRWL_5     0x00008000 /* 5 */

#define SH7750_MCR_TRAS       0x00001C00 /* DRAM: CAS-Before-RAS Refresh RAS */
                                         /* asserting period */
                                         /* SDRAM: Command interval after */
                                         /* synchronous DRAM refresh */
#define SH7750_MCR_TRAS_DRAM_2         0x00000000 /* 2 */
#define SH7750_MCR_TRAS_DRAM_3         0x00000400 /* 3 */
#define SH7750_MCR_TRAS_DRAM_4         0x00000800 /* 4 */
#define SH7750_MCR_TRAS_DRAM_5         0x00000C00 /* 5 */
#define SH7750_MCR_TRAS_DRAM_6         0x00001000 /* 6 */
#define SH7750_MCR_TRAS_DRAM_7         0x00001400 /* 7 */
#define SH7750_MCR_TRAS_DRAM_8         0x00001800 /* 8 */
#define SH7750_MCR_TRAS_DRAM_9         0x00001C00 /* 9 */

#define SH7750_MCR_TRAS_SDRAM_TRC_4    0x00000000 /* 4 + TRC */
#define SH7750_MCR_TRAS_SDRAM_TRC_5    0x00000400 /* 5 + TRC */
#define SH7750_MCR_TRAS_SDRAM_TRC_6    0x00000800 /* 6 + TRC */
#define SH7750_MCR_TRAS_SDRAM_TRC_7    0x00000C00 /* 7 + TRC */
#define SH7750_MCR_TRAS_SDRAM_TRC_8    0x00001000 /* 8 + TRC */
#define SH7750_MCR_TRAS_SDRAM_TRC_9    0x00001400 /* 9 + TRC */
#define SH7750_MCR_TRAS_SDRAM_TRC_10   0x00001800 /* 10 + TRC */
#define SH7750_MCR_TRAS_SDRAM_TRC_11   0x00001C00 /* 11 + TRC */

#define SH7750_MCR_BE         0x00000200 /* Burst Enable */
#define SH7750_MCR_SZ         0x00000180 /* Memory Data Size */
#define SH7750_MCR_SZ_64      0x00000000 /* 64 bits */
#define SH7750_MCR_SZ_16      0x00000100 /* 16 bits */
#define SH7750_MCR_SZ_32      0x00000180 /* 32 bits */

#define SH7750_MCR_AMX        0x00000078 /* Address Multiplexing */
#define SH7750_MCR_AMX_S      3
#define SH7750_MCR_AMX_DRAM_8BIT_COL    0x00000000 /* 8-bit column addr */
#define SH7750_MCR_AMX_DRAM_9BIT_COL    0x00000008 /* 9-bit column addr */
#define SH7750_MCR_AMX_DRAM_10BIT_COL   0x00000010 /* 10-bit column addr */
#define SH7750_MCR_AMX_DRAM_11BIT_COL   0x00000018 /* 11-bit column addr */
#define SH7750_MCR_AMX_DRAM_12BIT_COL   0x00000020 /* 12-bit column addr */
/* See SH7750 Hardware Manual for SDRAM address multiplexor selection */

#define SH7750_MCR_RFSH       0x00000004 /* Refresh Control */
#define SH7750_MCR_RMODE      0x00000002 /* Refresh Mode: */
#define SH7750_MCR_RMODE_NORMAL 0x00000000 /* Normal Refresh Mode */
#define SH7750_MCR_RMODE_SELF   0x00000002 /* Self-Refresh Mode */
#define SH7750_MCR_RMODE_EDO    0x00000001 /* EDO Mode */

/* SDRAM Mode Set address */
#define SH7750_SDRAM_MODE_A2_BASE  0xFF900000
#define SH7750_SDRAM_MODE_A3_BASE  0xFF940000
#define SH7750_SDRAM_MODE_A2_32BIT(x) (SH7750_SDRAM_MODE_A2_BASE + ((x) << 2))
#define SH7750_SDRAM_MODE_A3_32BIT(x) (SH7750_SDRAM_MODE_A3_BASE + ((x) << 2))
#define SH7750_SDRAM_MODE_A2_64BIT(x) (SH7750_SDRAM_MODE_A2_BASE + ((x) << 3))
#define SH7750_SDRAM_MODE_A3_64BIT(x) (SH7750_SDRAM_MODE_A3_BASE + ((x) << 3))


/* PCMCIA Control Register (half) - PCR */
#define SH7750_PCR_REGOFS     0x800018 /* offset */
#define SH7750_PCR            SH7750_P4_REG32(SH7750_PCR_REGOFS)
#define SH7750_PCR_A7         SH7750_A7_REG32(SH7750_PCR_REGOFS)

#define SH7750_PCR_A5PCW      0xC000 /* Area 5 PCMCIA Wait - Number of wait */
                                     /* states to be added to the number of */
                                     /* waits specified by WCR2 in a */
                                     /* low-speed PCMCIA wait cycle */
#define SH7750_PCR_A5PCW_0    0x0000 /*    0 waits inserted */
#define SH7750_PCR_A5PCW_15   0x4000 /*    15 waits inserted */
#define SH7750_PCR_A5PCW_30   0x8000 /*    30 waits inserted */
#define SH7750_PCR_A5PCW_50   0xC000 /*    50 waits inserted */

#define SH7750_PCR_A6PCW      0x3000 /* Area 6 PCMCIA Wait - Number of wait */
                                     /* states to be added to the number of */
                                     /* waits specified by WCR2 in a */
                                     /* low-speed PCMCIA wait cycle */
#define SH7750_PCR_A6PCW_0    0x0000 /*    0 waits inserted */
#define SH7750_PCR_A6PCW_15   0x1000 /*    15 waits inserted */
#define SH7750_PCR_A6PCW_30   0x2000 /*    30 waits inserted */
#define SH7750_PCR_A6PCW_50   0x3000 /*    50 waits inserted */

#define SH7750_PCR_A5TED      0x0E00 /* Area 5 Addr-OE\/WE\ Assertion Delay */
                                     /* delay time from address output to */
                                     /* OE\/WE\ assertion on the connected */
                                     /* PCMCIA interface */
#define SH7750_PCR_A5TED_S    9
#define SH7750_PCR_A6TED      0x01C0 /* Area 6 Addr-OE\/WE\ Assertion Delay */
#define SH7750_PCR_A6TED_S    6

#define SH7750_PCR_TED_0WS    0 /* 0 Waits inserted */
#define SH7750_PCR_TED_1WS    1 /* 1 Waits inserted */
#define SH7750_PCR_TED_2WS    2 /* 2 Waits inserted */
#define SH7750_PCR_TED_3WS    3 /* 3 Waits inserted */
#define SH7750_PCR_TED_6WS    4 /* 6 Waits inserted */
#define SH7750_PCR_TED_9WS    5 /* 9 Waits inserted */
#define SH7750_PCR_TED_12WS   6 /* 12 Waits inserted */
#define SH7750_PCR_TED_15WS   7 /* 15 Waits inserted */

#define SH7750_PCR_A5TEH      0x0038 /* Area 5 OE\/WE\ Negation Addr delay, */
                                     /* address hold delay time from OE\/WE\ */
                                     /* negation in a write on the connected */
                                     /* PCMCIA interface */
#define SH7750_PCR_A5TEH_S    3

#define SH7750_PCR_A6TEH      0x0007 /* Area 6 OE\/WE\ Negation Address delay */
#define SH7750_PCR_A6TEH_S    0

#define SH7750_PCR_TEH_0WS    0 /* 0 Waits inserted */
#define SH7750_PCR_TEH_1WS    1 /* 1 Waits inserted */
#define SH7750_PCR_TEH_2WS    2 /* 2 Waits inserted */
#define SH7750_PCR_TEH_3WS    3 /* 3 Waits inserted */
#define SH7750_PCR_TEH_6WS    4 /* 6 Waits inserted */
#define SH7750_PCR_TEH_9WS    5 /* 9 Waits inserted */
#define SH7750_PCR_TEH_12WS   6 /* 12 Waits inserted */
#define SH7750_PCR_TEH_15WS   7 /* 15 Waits inserted */

/* Refresh Timer Control/Status Register (half) - RTSCR */
#define SH7750_RTCSR_REGOFS   0x80001C /* offset */
#define SH7750_RTCSR          SH7750_P4_REG32(SH7750_RTCSR_REGOFS)
#define SH7750_RTCSR_A7       SH7750_A7_REG32(SH7750_RTCSR_REGOFS)

#define SH7750_RTCSR_KEY      0xA500 /* RTCSR write key */
#define SH7750_RTCSR_CMF      0x0080 /* Compare-Match Flag (indicates a */
                                     /* match between the refresh timer */
                                     /* counter and refresh time constant) */
#define SH7750_RTCSR_CMIE     0x0040 /* Compare-Match Interrupt Enable */
#define SH7750_RTCSR_CKS      0x0038 /* Refresh Counter Clock Selects */
#define SH7750_RTCSR_CKS_DIS          0x0000 /* Clock Input Disabled */
#define SH7750_RTCSR_CKS_CKIO_DIV4    0x0008 /* Bus Clock / 4 */
#define SH7750_RTCSR_CKS_CKIO_DIV16   0x0010 /* Bus Clock / 16 */
#define SH7750_RTCSR_CKS_CKIO_DIV64   0x0018 /* Bus Clock / 64 */
#define SH7750_RTCSR_CKS_CKIO_DIV256  0x0020 /* Bus Clock / 256 */
#define SH7750_RTCSR_CKS_CKIO_DIV1024 0x0028 /* Bus Clock / 1024 */
#define SH7750_RTCSR_CKS_CKIO_DIV2048 0x0030 /* Bus Clock / 2048 */
#define SH7750_RTCSR_CKS_CKIO_DIV4096 0x0038 /* Bus Clock / 4096 */

#define SH7750_RTCSR_OVF      0x0004 /* Refresh Count Overflow Flag */
#define SH7750_RTCSR_OVIE     0x0002 /* Refresh Count Overflow Interrupt */
                                     /*   Enable */
#define SH7750_RTCSR_LMTS     0x0001 /* Refresh Count Overflow Limit Select */
#define SH7750_RTCSR_LMTS_1024 0x0000 /* Count Limit is 1024 */
#define SH7750_RTCSR_LMTS_512  0x0001 /* Count Limit is 512 */

/* Refresh Timer Counter (half) - RTCNT */
#define SH7750_RTCNT_REGOFS   0x800020 /* offset */
#define SH7750_RTCNT          SH7750_P4_REG32(SH7750_RTCNT_REGOFS)
#define SH7750_RTCNT_A7       SH7750_A7_REG32(SH7750_RTCNT_REGOFS)

#define SH7750_RTCNT_KEY      0xA500 /* RTCNT write key */

/* Refresh Time Constant Register (half) - RTCOR */
#define SH7750_RTCOR_REGOFS   0x800024 /* offset */
#define SH7750_RTCOR          SH7750_P4_REG32(SH7750_RTCOR_REGOFS)
#define SH7750_RTCOR_A7       SH7750_A7_REG32(SH7750_RTCOR_REGOFS)

#define SH7750_RTCOR_KEY      0xA500 /* RTCOR write key */

/* Refresh Count Register (half) - RFCR */
#define SH7750_RFCR_REGOFS    0x800028 /* offset */
#define SH7750_RFCR           SH7750_P4_REG32(SH7750_RFCR_REGOFS)
#define SH7750_RFCR_A7        SH7750_A7_REG32(SH7750_RFCR_REGOFS)

#define SH7750_RFCR_KEY       0xA400 /* RFCR write key */

/* Synchronous DRAM mode registers - SDMR */
#define SH7750_SDMR2_REGOFS   0x900000 /* base offset */
#define SH7750_SDMR2_REGNB    0x0FFC /* nb of register */
#define SH7750_SDMR2          SH7750_P4_REG32(SH7750_SDMR2_REGOFS)
#define SH7750_SDMR2_A7       SH7750_A7_REG32(SH7750_SDMR2_REGOFS)

#define SH7750_SDMR3_REGOFS   0x940000 /* offset */
#define SH7750_SDMR3_REGNB    0x0FFC /* nb of register */
#define SH7750_SDMR3          SH7750_P4_REG32(SH7750_SDMR3_REGOFS)
#define SH7750_SDMR3_A7       SH7750_A7_REG32(SH7750_SDMR3_REGOFS)

/*
 * Direct Memory Access Controller (DMAC)
 */

/* DMA Source Address Register - SAR0, SAR1, SAR2, SAR3 */
#define SH7750_SAR_REGOFS(n)  (0xA00000 + ((n) * 16)) /* offset */
#define SH7750_SAR(n)         SH7750_P4_REG32(SH7750_SAR_REGOFS(n))
#define SH7750_SAR_A7(n)      SH7750_A7_REG32(SH7750_SAR_REGOFS(n))
#define SH7750_SAR0           SH7750_SAR(0)
#define SH7750_SAR1           SH7750_SAR(1)
#define SH7750_SAR2           SH7750_SAR(2)
#define SH7750_SAR3           SH7750_SAR(3)
#define SH7750_SAR0_A7        SH7750_SAR_A7(0)
#define SH7750_SAR1_A7        SH7750_SAR_A7(1)
#define SH7750_SAR2_A7        SH7750_SAR_A7(2)
#define SH7750_SAR3_A7        SH7750_SAR_A7(3)

/* DMA Destination Address Register - DAR0, DAR1, DAR2, DAR3 */
#define SH7750_DAR_REGOFS(n)  (0xA00004 + ((n) * 16)) /* offset */
#define SH7750_DAR(n)         SH7750_P4_REG32(SH7750_DAR_REGOFS(n))
#define SH7750_DAR_A7(n)      SH7750_A7_REG32(SH7750_DAR_REGOFS(n))
#define SH7750_DAR0           SH7750_DAR(0)
#define SH7750_DAR1           SH7750_DAR(1)
#define SH7750_DAR2           SH7750_DAR(2)
#define SH7750_DAR3           SH7750_DAR(3)
#define SH7750_DAR0_A7        SH7750_DAR_A7(0)
#define SH7750_DAR1_A7        SH7750_DAR_A7(1)
#define SH7750_DAR2_A7        SH7750_DAR_A7(2)
#define SH7750_DAR3_A7        SH7750_DAR_A7(3)

/* DMA Transfer Count Register - DMATCR0, DMATCR1, DMATCR2, DMATCR3 */
#define SH7750_DMATCR_REGOFS(n)  (0xA00008 + ((n) * 16)) /* offset */
#define SH7750_DMATCR(n)      SH7750_P4_REG32(SH7750_DMATCR_REGOFS(n))
#define SH7750_DMATCR_A7(n)   SH7750_A7_REG32(SH7750_DMATCR_REGOFS(n))
#define SH7750_DMATCR0_P4     SH7750_DMATCR(0)
#define SH7750_DMATCR1_P4     SH7750_DMATCR(1)
#define SH7750_DMATCR2_P4     SH7750_DMATCR(2)
#define SH7750_DMATCR3_P4     SH7750_DMATCR(3)
#define SH7750_DMATCR0_A7     SH7750_DMATCR_A7(0)
#define SH7750_DMATCR1_A7     SH7750_DMATCR_A7(1)
#define SH7750_DMATCR2_A7     SH7750_DMATCR_A7(2)
#define SH7750_DMATCR3_A7     SH7750_DMATCR_A7(3)

/* DMA Channel Control Register - CHCR0, CHCR1, CHCR2, CHCR3 */
#define SH7750_CHCR_REGOFS(n)  (0xA0000C + ((n) * 16)) /* offset */
#define SH7750_CHCR(n)        SH7750_P4_REG32(SH7750_CHCR_REGOFS(n))
#define SH7750_CHCR_A7(n)     SH7750_A7_REG32(SH7750_CHCR_REGOFS(n))
#define SH7750_CHCR0          SH7750_CHCR(0)
#define SH7750_CHCR1          SH7750_CHCR(1)
#define SH7750_CHCR2          SH7750_CHCR(2)
#define SH7750_CHCR3          SH7750_CHCR(3)
#define SH7750_CHCR0_A7       SH7750_CHCR_A7(0)
#define SH7750_CHCR1_A7       SH7750_CHCR_A7(1)
#define SH7750_CHCR2_A7       SH7750_CHCR_A7(2)
#define SH7750_CHCR3_A7       SH7750_CHCR_A7(3)

#define SH7750_CHCR_SSA       0xE0000000 /* Source Address Space Attribute */
#define SH7750_CHCR_SSA_PCMCIA  0x00000000 /* Reserved in PCMCIA access */
#define SH7750_CHCR_SSA_DYNBSZ  0x20000000 /* Dynamic Bus Sizing I/O space */
#define SH7750_CHCR_SSA_IO8     0x40000000 /* 8-bit I/O space */
#define SH7750_CHCR_SSA_IO16    0x60000000 /* 16-bit I/O space */
#define SH7750_CHCR_SSA_CMEM8   0x80000000 /* 8-bit common memory space */
#define SH7750_CHCR_SSA_CMEM16  0xA0000000 /* 16-bit common memory space */
#define SH7750_CHCR_SSA_AMEM8   0xC0000000 /* 8-bit attribute memory space */
#define SH7750_CHCR_SSA_AMEM16  0xE0000000 /* 16-bit attribute memory space */

#define SH7750_CHCR_STC       0x10000000 /* Source Addr Wait Control Select */
                                         /*   specifies CS5 or CS6 space wait */
                                         /*   control for PCMCIA access */

#define SH7750_CHCR_DSA       0x0E000000 /* Source Address Space Attribute */
#define SH7750_CHCR_DSA_PCMCIA  0x00000000 /* Reserved in PCMCIA access */
#define SH7750_CHCR_DSA_DYNBSZ  0x02000000 /* Dynamic Bus Sizing I/O space */
#define SH7750_CHCR_DSA_IO8     0x04000000 /* 8-bit I/O space */
#define SH7750_CHCR_DSA_IO16    0x06000000 /* 16-bit I/O space */
#define SH7750_CHCR_DSA_CMEM8   0x08000000 /* 8-bit common memory space */
#define SH7750_CHCR_DSA_CMEM16  0x0A000000 /* 16-bit common memory space */
#define SH7750_CHCR_DSA_AMEM8   0x0C000000 /* 8-bit attribute memory space */
#define SH7750_CHCR_DSA_AMEM16  0x0E000000 /* 16-bit attribute memory space */

#define SH7750_CHCR_DTC       0x01000000 /* Destination Address Wait Control */
                                         /*   Select, specifies CS5 or CS6 */
                                         /*   space wait control for PCMCIA */
                                         /*   access */

#define SH7750_CHCR_DS        0x00080000 /* DREQ\ Select : */
#define SH7750_CHCR_DS_LOWLVL 0x00000000 /*   Low Level Detection */
#define SH7750_CHCR_DS_FALL   0x00080000 /*   Falling Edge Detection */

#define SH7750_CHCR_RL        0x00040000 /* Request Check Level: */
#define SH7750_CHCR_RL_ACTH   0x00000000 /*   DRAK is an active high out */
#define SH7750_CHCR_RL_ACTL   0x00040000 /*   DRAK is an active low out */

#define SH7750_CHCR_AM        0x00020000 /* Acknowledge Mode: */
#define SH7750_CHCR_AM_RD     0x00000000 /*   DACK is output in read cycle */
#define SH7750_CHCR_AM_WR     0x00020000 /*   DACK is output in write cycle */

#define SH7750_CHCR_AL        0x00010000 /* Acknowledge Level: */
#define SH7750_CHCR_AL_ACTH   0x00000000 /*   DACK is an active high out */
#define SH7750_CHCR_AL_ACTL   0x00010000 /*   DACK is an active low out */

#define SH7750_CHCR_DM        0x0000C000 /* Destination Address Mode: */
#define SH7750_CHCR_DM_FIX    0x00000000 /*   Destination Addr Fixed */
#define SH7750_CHCR_DM_INC    0x00004000 /*   Destination Addr Incremented */
#define SH7750_CHCR_DM_DEC    0x00008000 /*   Destination Addr Decremented */

#define SH7750_CHCR_SM        0x00003000 /* Source Address Mode: */
#define SH7750_CHCR_SM_FIX    0x00000000 /*   Source Addr Fixed */
#define SH7750_CHCR_SM_INC    0x00001000 /*   Source Addr Incremented */
#define SH7750_CHCR_SM_DEC    0x00002000 /*   Source Addr Decremented */

#define SH7750_CHCR_RS        0x00000F00 /* Request Source Select: */
#define SH7750_CHCR_RS_ER_DA_EA_TO_EA   0x000 /* External Request, Dual Addr */
                                              /*   Mode, External Addr Space */
                                              /*   -> External Addr Space) */
#define SH7750_CHCR_RS_ER_SA_EA_TO_ED   0x200 /* External Request, Single */
                                              /*   Address Mode (Ext. Addr */
                                              /*   Space -> External Device) */
#define SH7750_CHCR_RS_ER_SA_ED_TO_EA   0x300 /* External Request, Single */
                                              /*   Address Mode, (External */
                                              /*   Device -> External Addr */
                                              /*   Space) */
#define SH7750_CHCR_RS_AR_EA_TO_EA      0x400 /* Auto-Request (External Addr */
                                              /*   Space -> Ext. Addr Space) */

#define SH7750_CHCR_RS_AR_EA_TO_OCP     0x500 /* Auto-Request (External Addr */
                                              /*   Space -> On-chip */
                                              /*   Peripheral Module) */
#define SH7750_CHCR_RS_AR_OCP_TO_EA     0x600 /* Auto-Request (On-chip */
                                              /*   Peripheral Module -> */
                                              /*   External Addr Space */
#define SH7750_CHCR_RS_SCITX_EA_TO_SC   0x800 /* SCI Transmit-Data-Empty intr */
                                              /*   transfer request (external */
                                              /*   address space -> SCTDR1) */
#define SH7750_CHCR_RS_SCIRX_SC_TO_EA   0x900 /* SCI Receive-Data-Full intr */
                                              /*   transfer request (SCRDR1 */
                                              /*   -> External Addr Space) */
#define SH7750_CHCR_RS_SCIFTX_EA_TO_SC  0xA00 /* SCIF TX-Data-Empty intr */
                                              /*   transfer request (external */
                                              /*   address space -> SCFTDR1) */
#define SH7750_CHCR_RS_SCIFRX_SC_TO_EA  0xB00 /* SCIF Receive-Data-Full intr */
                                              /*   transfer request (SCFRDR2 */
                                              /*   -> External Addr Space) */
#define SH7750_CHCR_RS_TMU2_EA_TO_EA    0xC00 /* TMU Channel 2 (input capture */
                                              /*   interrupt), (external */
                                              /*   address space -> external */
                                              /*   address space) */
#define SH7750_CHCR_RS_TMU2_EA_TO_OCP   0xD00 /* TMU Channel 2 (input capture */
                                              /*   interrupt), (external */
                                              /*   address space -> on-chip */
                                              /*   peripheral module) */
#define SH7750_CHCR_RS_TMU2_OCP_TO_EA   0xE00 /* TMU Channel 2 (input capture */
                                              /*   interrupt), (on-chip */
                                              /*   peripheral module -> */
                                              /*   external address space) */

#define SH7750_CHCR_TM        0x00000080 /* Transmit mode: */
#define SH7750_CHCR_TM_CSTEAL 0x00000000 /*     Cycle Steal Mode */
#define SH7750_CHCR_TM_BURST  0x00000080 /*     Burst Mode */

#define SH7750_CHCR_TS        0x00000070 /* Transmit Size: */
#define SH7750_CHCR_TS_QUAD   0x00000000 /*     Quadword Size (64 bits) */
#define SH7750_CHCR_TS_BYTE   0x00000010 /*     Byte Size (8 bit) */
#define SH7750_CHCR_TS_WORD   0x00000020 /*     Word Size (16 bit) */
#define SH7750_CHCR_TS_LONG   0x00000030 /*     Longword Size (32 bit) */
#define SH7750_CHCR_TS_BLOCK  0x00000040 /*     32-byte block transfer */

#define SH7750_CHCR_IE        0x00000004 /* Interrupt Enable */
#define SH7750_CHCR_TE        0x00000002 /* Transfer End */
#define SH7750_CHCR_DE        0x00000001 /* DMAC Enable */

/* DMA Operation Register - DMAOR */
#define SH7750_DMAOR_REGOFS   0xA00040 /* offset */
#define SH7750_DMAOR          SH7750_P4_REG32(SH7750_DMAOR_REGOFS)
#define SH7750_DMAOR_A7       SH7750_A7_REG32(SH7750_DMAOR_REGOFS)

#define SH7750_DMAOR_DDT      0x00008000 /* On-Demand Data Transfer Mode */

#define SH7750_DMAOR_PR       0x00000300 /* Priority Mode: */
#define SH7750_DMAOR_PR_0123  0x00000000 /*     CH0 > CH1 > CH2 > CH3 */
#define SH7750_DMAOR_PR_0231  0x00000100 /*     CH0 > CH2 > CH3 > CH1 */
#define SH7750_DMAOR_PR_2013  0x00000200 /*     CH2 > CH0 > CH1 > CH3 */
#define SH7750_DMAOR_PR_RR    0x00000300 /*     Round-robin mode */

#define SH7750_DMAOR_COD      0x00000010 /* Check Overrun for DREQ\ */
#define SH7750_DMAOR_AE       0x00000004 /* Address Error flag */
#define SH7750_DMAOR_NMIF     0x00000002 /* NMI Flag */
#define SH7750_DMAOR_DME      0x00000001 /* DMAC Master Enable */

/*
 * I/O Ports
 */
/* Port Control Register A - PCTRA */
#define SH7750_PCTRA_REGOFS   0x80002C /* offset */
#define SH7750_PCTRA          SH7750_P4_REG32(SH7750_PCTRA_REGOFS)
#define SH7750_PCTRA_A7       SH7750_A7_REG32(SH7750_PCTRA_REGOFS)

#define SH7750_PCTRA_PBPUP(n) 0 /* Bit n is pulled up */
#define SH7750_PCTRA_PBNPUP(n) (1 << ((n) * 2 + 1)) /* Bit n is not pulled up */
#define SH7750_PCTRA_PBINP(n) 0 /* Bit n is an input */
#define SH7750_PCTRA_PBOUT(n) (1 << ((n) * 2)) /* Bit n is an output */

/* Port Data Register A - PDTRA(half) */
#define SH7750_PDTRA_REGOFS   0x800030 /* offset */
#define SH7750_PDTRA          SH7750_P4_REG32(SH7750_PDTRA_REGOFS)
#define SH7750_PDTRA_A7       SH7750_A7_REG32(SH7750_PDTRA_REGOFS)

#define SH7750_PDTRA_BIT(n) (1 << (n))

/* Port Control Register B - PCTRB */
#define SH7750_PCTRB_REGOFS   0x800040 /* offset */
#define SH7750_PCTRB          SH7750_P4_REG32(SH7750_PCTRB_REGOFS)
#define SH7750_PCTRB_A7       SH7750_A7_REG32(SH7750_PCTRB_REGOFS)

#define SH7750_PCTRB_PBPUP(n) 0 /* Bit n is pulled up */
#define SH7750_PCTRB_PBNPUP(n) (1 << ((n - 16) * 2 + 1)) /* Bit n is not pulled up */
#define SH7750_PCTRB_PBINP(n) 0 /* Bit n is an input */
#define SH7750_PCTRB_PBOUT(n) (1 << ((n - 16) * 2)) /* Bit n is an output */

/* Port Data Register B - PDTRB(half) */
#define SH7750_PDTRB_REGOFS   0x800044 /* offset */
#define SH7750_PDTRB          SH7750_P4_REG32(SH7750_PDTRB_REGOFS)
#define SH7750_PDTRB_A7       SH7750_A7_REG32(SH7750_PDTRB_REGOFS)

#define SH7750_PDTRB_BIT(n) (1 << ((n) - 16))

/* GPIO Interrupt Control Register - GPIOIC(half) */
#define SH7750_GPIOIC_REGOFS  0x800048 /* offset */
#define SH7750_GPIOIC         SH7750_P4_REG32(SH7750_GPIOIC_REGOFS)
#define SH7750_GPIOIC_A7      SH7750_A7_REG32(SH7750_GPIOIC_REGOFS)

#define SH7750_GPIOIC_PTIREN(n) (1 << (n)) /* Port n is used as a GPIO int */

/*
 * Interrupt Controller - INTC
 */
/* Interrupt Control Register - ICR (half) */
#define SH7750_ICR_REGOFS     0xD00000 /* offset */
#define SH7750_ICR            SH7750_P4_REG32(SH7750_ICR_REGOFS)
#define SH7750_ICR_A7         SH7750_A7_REG32(SH7750_ICR_REGOFS)

#define SH7750_ICR_NMIL       0x8000 /* NMI Input Level */
#define SH7750_ICR_MAI        0x4000 /* NMI Interrupt Mask */

#define SH7750_ICR_NMIB       0x0200 /* NMI Block Mode: */
#define SH7750_ICR_NMIB_BLK   0x0000 /*   NMI requests held pending while */
                                     /*     SR.BL bit is set to 1 */
#define SH7750_ICR_NMIB_NBLK  0x0200 /*   NMI requests detected when SR.BL */
                                     /*     bit set to 1 */

#define SH7750_ICR_NMIE       0x0100 /* NMI Edge Select: */
#define SH7750_ICR_NMIE_FALL  0x0000 /*   Interrupt request detected on */
                                     /*     falling edge of NMI input */
#define SH7750_ICR_NMIE_RISE  0x0100 /*   Interrupt request detected on */
                                     /*     rising edge of NMI input */

#define SH7750_ICR_IRLM       0x0080 /* IRL Pin Mode: */
#define SH7750_ICR_IRLM_ENC   0x0000 /*   IRL\ pins used as a level-encoded */
                                     /*     interrupt requests */
#define SH7750_ICR_IRLM_RAW   0x0080 /*   IRL\ pins used as a four */
                                     /*     independent interrupt requests */

/*
 * User Break Controller registers
 */
#define SH7750_BARA           0x200000 /* Break address regiser A */
#define SH7750_BAMRA          0x200004 /* Break address mask regiser A */
#define SH7750_BBRA           0x200008 /* Break bus cycle regiser A */
#define SH7750_BARB           0x20000c /* Break address regiser B */
#define SH7750_BAMRB          0x200010 /* Break address mask regiser B */
#define SH7750_BBRB           0x200014 /* Break bus cycle regiser B */
#define SH7750_BASRB          0x000018 /* Break ASID regiser B */
#define SH7750_BDRB           0x200018 /* Break data regiser B */
#define SH7750_BDMRB          0x20001c /* Break data mask regiser B */
#define SH7750_BRCR           0x200020 /* Break control register */

#define SH7750_BRCR_UDBE        0x0001 /* User break debug enable bit */

/*
 * Missing in RTEMS, added for QEMU
 */
#define SH7750_BCR3_A7       0x1f800050
#define SH7750_BCR4_A7       0x1e0a00f0

#endif
