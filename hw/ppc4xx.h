/*
 * QEMU PowerPC 4xx emulation shared definitions
 *
 * Copyright (c) 2007 Jocelyn Mayer
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

#if !defined(PPC_4XX_H)
#define PPC_4XX_H

#include "pci.h"

/* PowerPC 4xx core initialization */
CPUState *ppc4xx_init (const char *cpu_model,
                       clk_setup_t *cpu_clk, clk_setup_t *tb_clk,
                       uint32_t sysclk);

/* PowerPC 4xx universal interrupt controller */
enum {
    PPCUIC_OUTPUT_INT = 0,
    PPCUIC_OUTPUT_CINT = 1,
    PPCUIC_OUTPUT_NB,
};
qemu_irq *ppcuic_init (CPUState *env, qemu_irq *irqs,
                       uint32_t dcr_base, int has_ssr, int has_vr);

ram_addr_t ppc4xx_sdram_adjust(ram_addr_t ram_size, int nr_banks,
                               target_phys_addr_t ram_bases[],
                               target_phys_addr_t ram_sizes[],
                               const unsigned int sdram_bank_sizes[]);

void ppc4xx_sdram_init (CPUState *env, qemu_irq irq, int nbanks,
                        target_phys_addr_t *ram_bases,
                        target_phys_addr_t *ram_sizes,
                        int do_init);

PCIBus *ppc4xx_pci_init(CPUState *env, qemu_irq pci_irqs[4],
                        target_phys_addr_t config_space,
                        target_phys_addr_t int_ack,
                        target_phys_addr_t special_cycle,
                        target_phys_addr_t registers);

#endif /* !defined(PPC_4XX_H) */
