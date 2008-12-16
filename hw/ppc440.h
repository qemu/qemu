/*
 * Qemu PowerPC 440 board emualtion
 *
 * Copyright 2007 IBM Corporation.
 * Authors: Jerone Young <jyoung5@us.ibm.com>
 * 	    Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 *
 * This work is licensed under the GNU GPL licence version 2 or later
 *
 */

#ifndef QEMU_PPC440_H
#define QEMU_PPC440_H

#include "hw.h"

CPUState *ppc440ep_init(ram_addr_t *ram_size, PCIBus **pcip,
                        const unsigned int pci_irq_nrs[4], int do_init);

#endif
