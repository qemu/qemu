/*
 * QEMU PowerPC 440 shared definitions
 *
 * Copyright (c) 2012 François Revol
 * Copyright (c) 2016-2018 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#ifndef PPC440_H
#define PPC440_H

#include "hw/ppc/ppc.h"

void ppc4xx_l2sram_init(CPUPPCState *env);
void ppc4xx_cpr_init(CPUPPCState *env);
void ppc4xx_sdr_init(CPUPPCState *env);
void ppc440_sdram_init(CPUPPCState *env, int nbanks,
                       MemoryRegion *ram_memories,
                       hwaddr *ram_bases, hwaddr *ram_sizes,
                       int do_init);
void ppc4xx_ahb_init(CPUPPCState *env);
void ppc4xx_dma_init(CPUPPCState *env, int dcr_base);
void ppc460ex_pcie_init(CPUPPCState *env);

#endif /* PPC440_H */
