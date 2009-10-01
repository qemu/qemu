/*
 * QEMU PowerPC E500 emulation shared definitions
 *
 * Copyright (C) 2009 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu,     <yu.liu@freescale.com>
 *
 * This file is derived from hw/ppc440.h
 * the copyright for that material belongs to the original owners.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 */

#if !defined(PPC_E500_H)
#define PPC_E500_H

PCIBus *ppce500_pci_init(qemu_irq *pic, target_phys_addr_t registers);

#endif /* !defined(PPC_E500_H) */
