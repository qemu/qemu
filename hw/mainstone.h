/*
 * PXA270-based Intel Mainstone platforms.
 *
 * Copyright (c) 2007 by Armin Kuster <akuster@kama-aina.net> or
 *                                    <akuster@mvista.com>
 *
 * This code is licensed under the GNU GPL v2.
 */

#ifndef __MAINSTONE_H__
#define __MAINSTONE_H__

/* Device addresses */
#define MST_FPGA_PHYS	0x08000000
#define MST_ETH_PHYS	0x10000300
#define MST_FLASH_0		0x00000000
#define MST_FLASH_1		0x04000000

/* IRQ definitions */
#define MMC_IRQ       0
#define USIM_IRQ      1
#define USBC_IRQ      2
#define ETHERNET_IRQ  3
#define AC97_IRQ      4
#define PEN_IRQ       5
#define MSINS_IRQ     6
#define EXBRD_IRQ     7
#define S0_CD_IRQ     9
#define S0_STSCHG_IRQ 10
#define S0_IRQ        11
#define S1_CD_IRQ     13
#define S1_STSCHG_IRQ 14
#define S1_IRQ        15

extern qemu_irq
*mst_irq_init(struct pxa2xx_state_s *cpu, uint32_t base, int irq);

#endif /* __MAINSTONE_H__ */
