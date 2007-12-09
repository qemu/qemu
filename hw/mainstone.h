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
#define ETHERNET_IRQ	3

extern qemu_irq
*mst_irq_init(struct pxa2xx_state_s *cpu, uint32_t base, int irq);

#endif /* __MAINSTONE_H__ */
