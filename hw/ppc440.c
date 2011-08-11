/*
 * Qemu PowerPC 440 chip emulation
 *
 * Copyright 2007 IBM Corporation.
 * Authors:
 * 	Jerone Young <jyoung5@us.ibm.com>
 * 	Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 * 	Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "hw.h"
#include "pc.h"
#include "isa.h"
#include "ppc.h"
#include "ppc4xx.h"
#include "ppc440.h"
#include "ppc405.h"
#include "sysemu.h"
#include "kvm.h"
#include "exec-memory.h"

#define PPC440EP_PCI_CONFIG     0xeec00000
#define PPC440EP_PCI_INTACK     0xeed00000
#define PPC440EP_PCI_SPECIAL    0xeed00000
#define PPC440EP_PCI_REGS       0xef400000
#define PPC440EP_PCI_IO         0xe8000000
#define PPC440EP_PCI_IOLEN      0x00010000

#define PPC440EP_SDRAM_NR_BANKS 4

static const unsigned int ppc440ep_sdram_bank_sizes[] = {
    256<<20, 128<<20, 64<<20, 32<<20, 16<<20, 8<<20, 0
};

CPUState *ppc440ep_init(ram_addr_t *ram_size, PCIBus **pcip,
                        const unsigned int pci_irq_nrs[4], int do_init,
                        const char *cpu_model)
{
    MemoryRegion *ram_memories
        = g_malloc(PPC440EP_SDRAM_NR_BANKS * sizeof(*ram_memories));
    target_phys_addr_t ram_bases[PPC440EP_SDRAM_NR_BANKS];
    target_phys_addr_t ram_sizes[PPC440EP_SDRAM_NR_BANKS];
    CPUState *env;
    qemu_irq *pic;
    qemu_irq *irqs;
    qemu_irq *pci_irqs;

    if (cpu_model == NULL) {
        cpu_model = "440-Xilinx"; // XXX: should be 440EP
    }
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to initialize CPU!\n");
        exit(1);
    }

    ppc_dcr_init(env, NULL, NULL);

    /* interrupt controller */
    irqs = g_malloc0(sizeof(qemu_irq) * PPCUIC_OUTPUT_NB);
    irqs[PPCUIC_OUTPUT_INT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_INT];
    irqs[PPCUIC_OUTPUT_CINT] = ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_CINT];
    pic = ppcuic_init(env, irqs, 0x0C0, 0, 1);

    /* SDRAM controller */
    memset(ram_bases, 0, sizeof(ram_bases));
    memset(ram_sizes, 0, sizeof(ram_sizes));
    *ram_size = ppc4xx_sdram_adjust(*ram_size, PPC440EP_SDRAM_NR_BANKS,
                                    ram_memories,
                                    ram_bases, ram_sizes,
                                    ppc440ep_sdram_bank_sizes);
    /* XXX 440EP's ECC interrupts are on UIC1, but we've only created UIC0. */
    ppc4xx_sdram_init(env, pic[14], PPC440EP_SDRAM_NR_BANKS, ram_memories,
                      ram_bases, ram_sizes, do_init);

    /* PCI */
    pci_irqs = g_malloc(sizeof(qemu_irq) * 4);
    pci_irqs[0] = pic[pci_irq_nrs[0]];
    pci_irqs[1] = pic[pci_irq_nrs[1]];
    pci_irqs[2] = pic[pci_irq_nrs[2]];
    pci_irqs[3] = pic[pci_irq_nrs[3]];
    *pcip = ppc4xx_pci_init(env, pci_irqs,
                            PPC440EP_PCI_CONFIG,
                            PPC440EP_PCI_INTACK,
                            PPC440EP_PCI_SPECIAL,
                            PPC440EP_PCI_REGS);
    if (!*pcip)
        printf("couldn't create PCI controller!\n");

    isa_mmio_init(PPC440EP_PCI_IO, PPC440EP_PCI_IOLEN);

    if (serial_hds[0] != NULL) {
        serial_mm_init(get_system_memory(), 0xef600300, 0, pic[0],
                       PPC_SERIAL_MM_BAUDBASE, serial_hds[0],
                       DEVICE_BIG_ENDIAN);
    }
    if (serial_hds[1] != NULL) {
        serial_mm_init(get_system_memory(), 0xef600400, 0, pic[1],
                       PPC_SERIAL_MM_BAUDBASE, serial_hds[1],
                       DEVICE_BIG_ENDIAN);
    }

    return env;
}
