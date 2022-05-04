/*
 * HP-PARISC Dino PCI chipset emulation, as in B160L and similiar machines
 *
 * (C) 2017-2019 by Helge Deller <deller@gmx.de>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 * Documentation available at:
 * https://parisc.wiki.kernel.org/images-parisc/9/91/Dino_ers.pdf
 * https://parisc.wiki.kernel.org/images-parisc/7/70/Dino_3_1_Errata.pdf
 */

#ifndef DINO_H
#define DINO_H

#include "hw/pci/pci_host.h"

#define TYPE_DINO_PCI_HOST_BRIDGE "dino-pcihost"
OBJECT_DECLARE_SIMPLE_TYPE(DinoState, DINO_PCI_HOST_BRIDGE)

#define DINO_IAR0               0x004
#define DINO_IODC               0x008
#define DINO_IRR0               0x00C  /* RO */
#define DINO_IAR1               0x010
#define DINO_IRR1               0x014  /* RO */
#define DINO_IMR                0x018
#define DINO_IPR                0x01C
#define DINO_TOC_ADDR           0x020
#define DINO_ICR                0x024
#define DINO_ILR                0x028  /* RO */
#define DINO_IO_COMMAND         0x030  /* WO */
#define DINO_IO_STATUS          0x034  /* RO */
#define DINO_IO_CONTROL         0x038
#define DINO_IO_GSC_ERR_RESP    0x040  /* RO */
#define DINO_IO_ERR_INFO        0x044  /* RO */
#define DINO_IO_PCI_ERR_RESP    0x048  /* RO */
#define DINO_IO_FBB_EN          0x05c
#define DINO_IO_ADDR_EN         0x060
#define DINO_PCI_CONFIG_ADDR    0x064
#define DINO_PCI_CONFIG_DATA    0x068
#define DINO_PCI_IO_DATA        0x06c
#define DINO_PCI_MEM_DATA       0x070  /* Dino 3.x only */
#define DINO_GSC2X_CONFIG       0x7b4  /* RO */
#define DINO_GMASK              0x800
#define DINO_PAMR               0x804
#define DINO_PAPR               0x808
#define DINO_DAMODE             0x80c
#define DINO_PCICMD             0x810
#define DINO_PCISTS             0x814  /* R/WC */
#define DINO_MLTIM              0x81c
#define DINO_BRDG_FEAT          0x820
#define DINO_PCIROR             0x824
#define DINO_PCIWOR             0x828
#define DINO_TLTIM              0x830

#define DINO_IRQS         11      /* bits 0-10 are architected */
#define DINO_IRR_MASK     0x5ff   /* only 10 bits are implemented */
#define DINO_LOCAL_IRQS   (DINO_IRQS + 1)
#define DINO_MASK_IRQ(x)  (1 << (x))

#define DINO_IRQ_PCIINTA   0
#define DINO_IRQ_PCIINTB   1
#define DINO_IRQ_PCIINTC   2
#define DINO_IRQ_PCIINTD   3
#define DINO_IRQ_PCIINTE   4
#define DINO_IRQ_PCIINTF   5
#define DINO_IRQ_GSCEXTINT 6
#define DINO_IRQ_BUSERRINT 7
#define DINO_IRQ_PS2INT    8
#define DINO_IRQ_UNUSED    9
#define DINO_IRQ_RS232INT  10

#define PCIINTA   0x001
#define PCIINTB   0x002
#define PCIINTC   0x004
#define PCIINTD   0x008
#define PCIINTE   0x010
#define PCIINTF   0x020
#define GSCEXTINT 0x040
/* #define xxx       0x080 - bit 7 is "default" */
/* #define xxx    0x100 - bit 8 not used */
/* #define xxx    0x200 - bit 9 not used */
#define RS232INT  0x400

#define DINO_MEM_CHUNK_SIZE (8 * MiB)

#define DINO800_REGS (1 + (DINO_TLTIM - DINO_GMASK) / 4)
static const uint32_t reg800_keep_bits[DINO800_REGS] = {
    MAKE_64BIT_MASK(0, 1),  /* GMASK */
    MAKE_64BIT_MASK(0, 7),  /* PAMR */
    MAKE_64BIT_MASK(0, 7),  /* PAPR */
    MAKE_64BIT_MASK(0, 8),  /* DAMODE */
    MAKE_64BIT_MASK(0, 7),  /* PCICMD */
    MAKE_64BIT_MASK(0, 9),  /* PCISTS */
    MAKE_64BIT_MASK(0, 32), /* Undefined */
    MAKE_64BIT_MASK(0, 8),  /* MLTIM */
    MAKE_64BIT_MASK(0, 30), /* BRDG_FEAT */
    MAKE_64BIT_MASK(0, 24), /* PCIROR */
    MAKE_64BIT_MASK(0, 22), /* PCIWOR */
    MAKE_64BIT_MASK(0, 32), /* Undocumented */
    MAKE_64BIT_MASK(0, 9),  /* TLTIM */
};

/* offsets to DINO HPA: */
#define DINO_PCI_ADDR           0x064
#define DINO_CONFIG_DATA        0x068
#define DINO_IO_DATA            0x06c

struct DinoState {
    PCIHostState parent_obj;

    /*
     * PCI_CONFIG_ADDR is parent_obj.config_reg, via pci_host_conf_be_ops,
     * so that we can map PCI_CONFIG_DATA to pci_host_data_be_ops.
     */
    uint32_t config_reg_dino; /* keep original copy, including 2 lowest bits */

    uint32_t iar0;
    uint32_t iar1;
    uint32_t imr;
    uint32_t ipr;
    uint32_t icr;
    uint32_t ilr;
    uint32_t io_fbb_en;
    uint32_t io_addr_en;
    uint32_t io_control;
    uint32_t toc_addr;

    uint32_t reg800[DINO800_REGS];

    MemoryRegion this_mem;
    MemoryRegion pci_mem;
    MemoryRegion pci_mem_alias[32];

    MemoryRegion *memory_as;

    AddressSpace bm_as;
    MemoryRegion bm;
    MemoryRegion bm_ram_alias;
    MemoryRegion bm_pci_alias;
    MemoryRegion bm_cpu_alias;

    qemu_irq irqs[DINO_IRQS];
};

#endif
