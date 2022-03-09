/*
 * QEMU PIIX South Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2018 Hervé Poussineau
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_SOUTHBRIDGE_PIIX_H
#define HW_SOUTHBRIDGE_PIIX_H

#include "hw/pci/pci.h"
#include "qom/object.h"

#define TYPE_PIIX4_PM "PIIX4_PM"

I2CBus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                      qemu_irq sci_irq, qemu_irq smi_irq,
                      int smm_enabled, DeviceState **piix4_pm);

/* PIRQRC[A:D]: PIRQx Route Control Registers */
#define PIIX_PIRQCA 0x60
#define PIIX_PIRQCB 0x61
#define PIIX_PIRQCC 0x62
#define PIIX_PIRQCD 0x63

/*
 * Reset Control Register: PCI-accessible ISA-Compatible Register at address
 * 0xcf9, provided by the PCI/ISA bridge (PIIX3 PCI function 0, 8086:7000).
 */
#define PIIX_RCR_IOPORT 0xcf9

#define PIIX_NUM_PIC_IRQS       16      /* i8259 * 2 */
#define PIIX_NUM_PIRQS          4ULL    /* PIRQ[A-D] */

struct PIIXState {
    PCIDevice dev;

    /*
     * bitmap to track pic levels.
     * The pic level is the logical OR of all the PCI irqs mapped to it
     * So one PIC level is tracked by PIIX_NUM_PIRQS bits.
     *
     * PIRQ is mapped to PIC pins, we track it by
     * PIIX_NUM_PIRQS * PIIX_NUM_PIC_IRQS = 64 bits with
     * pic_irq * PIIX_NUM_PIRQS + pirq
     */
#if PIIX_NUM_PIC_IRQS * PIIX_NUM_PIRQS > 64
#error "unable to encode pic state in 64bit in pic_levels."
#endif
    uint64_t pic_levels;

    qemu_irq *pic;

    /* This member isn't used. Just for save/load compatibility */
    int32_t pci_irq_levels_vmstate[PIIX_NUM_PIRQS];

    /* Reset Control Register contents */
    uint8_t rcr;

    /* IO memory region for Reset Control Register (PIIX_RCR_IOPORT) */
    MemoryRegion rcr_mem;
};
typedef struct PIIXState PIIX3State;

#define TYPE_PIIX3_PCI_DEVICE "pci-piix3"
DECLARE_INSTANCE_CHECKER(PIIX3State, PIIX3_PCI_DEVICE,
                         TYPE_PIIX3_PCI_DEVICE)

PIIX3State *piix3_create(PCIBus *pci_bus, ISABus **isa_bus);

DeviceState *piix4_create(PCIBus *pci_bus, ISABus **isa_bus, I2CBus **smbus);

#endif
