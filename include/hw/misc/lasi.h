/*
 * HP-PARISC Lasi chipset emulation.
 *
 * (C) 2019 by Helge Deller <deller@gmx.de>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 * Documentation available at:
 * https://parisc.wiki.kernel.org/images-parisc/7/79/Lasi_ers.pdf
 */

#ifndef LASI_H
#define LASI_H

#include "system/address-spaces.h"
#include "hw/boards.h"
#include "hw/sysbus.h"

#define TYPE_LASI_CHIP "lasi-chip"
OBJECT_DECLARE_SIMPLE_TYPE(LasiState, LASI_CHIP)

#define LASI_IRR        0x00    /* RO */
#define LASI_IMR        0x04
#define LASI_IPR        0x08
#define LASI_ICR        0x0c
#define LASI_IAR        0x10

#define LASI_LPT        0x02000
#define LASI_AUDIO      0x04000
#define LASI_UART       0x05000
#define LASI_SCSI       0x06000
#define LASI_LAN        0x07000
#define LASI_PS2        0x08000
#define LASI_RTC        0x09000
#define LASI_FDC        0x0A000

#define LASI_PCR        0x0C000 /* LASI Power Control register */
#define LASI_ERRLOG     0x0C004 /* LASI Error Logging register */
#define LASI_VER        0x0C008 /* LASI Version Control register */
#define LASI_IORESET    0x0C00C /* LASI I/O Reset register */
#define LASI_AMR        0x0C010 /* LASI Arbitration Mask register */
#define LASI_IO_CONF    0x7FFFE /* LASI primary configuration register */
#define LASI_IO_CONF2   0x7FFFF /* LASI secondary configuration register */

#define LASI_BIT(x)     (1ul << (x))
#define LASI_IRQ_BITS   (LASI_BIT(5) | LASI_BIT(7) | LASI_BIT(8) | LASI_BIT(9) \
            | LASI_BIT(13) | LASI_BIT(14) | LASI_BIT(16) | LASI_BIT(17) \
            | LASI_BIT(18) | LASI_BIT(19) | LASI_BIT(20) | LASI_BIT(21) \
            | LASI_BIT(26))

#define ICR_BUS_ERROR_BIT  LASI_BIT(8)  /* bit 8 in ICR */
#define ICR_TOC_BIT        LASI_BIT(1)  /* bit 1 in ICR */

#define LASI_IRQS           27

#define LASI_IRQ_HPA        14
#define LASI_IRQ_UART_HPA   5
#define LASI_IRQ_LPT_HPA    7
#define LASI_IRQ_LAN_HPA    8
#define LASI_IRQ_SCSI_HPA   9
#define LASI_IRQ_AUDIO_HPA  13
#define LASI_IRQ_PS2KBD_HPA 26
#define LASI_IRQ_PS2MOU_HPA 26

struct LasiState {
    SysBusDevice parent_obj;

    uint32_t irr;
    uint32_t imr;
    uint32_t ipr;
    uint32_t icr;
    uint32_t iar;

    uint32_t errlog;
    uint32_t amr;
    uint32_t rtc_ref;

    MemoryRegion this_mem;
};

#endif
