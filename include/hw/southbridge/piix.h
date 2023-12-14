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

#include "hw/pci/pci_device.h"
#include "hw/acpi/piix4.h"
#include "hw/ide/pci.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/usb/hcd-uhci.h"

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

#define PIIX_NUM_PIRQS          4ULL    /* PIRQ[A-D] */

struct PIIXState {
    PCIDevice dev;

    /*
     * bitmap to track pic levels.
     * The pic level is the logical OR of all the PCI irqs mapped to it
     * So one PIC level is tracked by PIIX_NUM_PIRQS bits.
     *
     * PIRQ is mapped to PIC pins, we track it by
     * PIIX_NUM_PIRQS * ISA_NUM_IRQS = 64 bits with
     * pic_irq * PIIX_NUM_PIRQS + pirq
     */
#if ISA_NUM_IRQS * PIIX_NUM_PIRQS > 64
#error "unable to encode pic state in 64bit in pic_levels."
#endif
    uint64_t pic_levels;

    qemu_irq cpu_intr;
    qemu_irq isa_irqs_in[ISA_NUM_IRQS];

    /* This member isn't used. Just for save/load compatibility */
    int32_t pci_irq_levels_vmstate[PIIX_NUM_PIRQS];

    MC146818RtcState rtc;
    PCIIDEState ide;
    UHCIState uhci;
    PIIX4PMState pm;

    uint32_t smb_io_base;

    /* Reset Control Register contents */
    uint8_t rcr;

    /* IO memory region for Reset Control Register (PIIX_RCR_IOPORT) */
    MemoryRegion rcr_mem;

    bool has_acpi;
    bool has_pic;
    bool has_pit;
    bool has_usb;
    bool smm_enabled;
};

#define TYPE_PIIX_PCI_DEVICE "pci-piix"
OBJECT_DECLARE_SIMPLE_TYPE(PIIXState, PIIX_PCI_DEVICE)

#define TYPE_PIIX3_DEVICE "PIIX3"
#define TYPE_PIIX4_PCI_DEVICE "piix4-isa"

#endif
