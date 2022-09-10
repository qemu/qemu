/*
 * QEMU XDMA PCI device
 *
 * Copyright (c) 2022 Daedalean AG
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "qom/object.h"
#include "qemu/module.h"

#define TYPE_PCI_FWD_FPGA_DEVICE "fwdfpga"

typedef struct FwdFPGAState FwdFPGAState;
DECLARE_INSTANCE_CHECKER(FwdFPGAState, FWD_FPGA, TYPE_PCI_FWD_FPGA_DEVICE)

// See Xilinx PG195 for the layout of the following structs, in particular
// tables 5, 6 for the descriptors and the "PCIe to DMA Address Map" section
// for the other structures, including tables, 40, 41, 42, 45, 48, 96, 97,
// 108-115.

typedef struct [[gnu::packed]] XdmaDescriptor {
    uint8_t control;
    uint8_t nextAdj;
    uint16_t magic;  // 0xad4b;
    uint32_t length;
    uint64_t srcAddress;
    uint64_t dstAddress;
    uint64_t nxtAddress;
} XdmaDescriptor;

typedef struct [[gnu::packed]] XdmaChannel {
    uint32_t identifier;  // 0x1fc0 or 0x1fc1
    uint32_t control;
    uint32_t unused1[0x0e];
    uint32_t status;
    uint32_t unused2[0x02];
    uint32_t alignment;
    uint32_t unused3[0x2c];
} XdmaChannel;

typedef struct [[gnu::packed]] XdmaSgdma {
    uint32_t identifier;  // 0x1fc4 or 0x1fc5
    uint32_t unused1[31];

    XdmaDescriptor *descriptorAddress;
    uint32_t descriptorAdjacent;
    uint32_t descriptorCredits;
    uint32_t unused2[0x1c];
} XdmaSgdma;

typedef struct [[gnu::packed]] XdmaBar {
    XdmaChannel h2cChannel0;
    XdmaChannel h2cChannel1;
    uint8_t padding1[0x0e00];
    XdmaChannel c2hChannel0;
    XdmaChannel c2hChannel1;
    uint8_t padding2[0x1e00];
    uint32_t configIdentifier;
    uint8_t padding3[0x0ffc];
    XdmaSgdma h2cSgdma0;
    XdmaSgdma h2cSgdma1;
    uint8_t padding4[0x0e00];
    XdmaSgdma c2hSgdma0;
    XdmaSgdma c2hSgdma1;
    uint8_t padding5[0x2e00];
} XdmaBar;

struct FwdFPGAState {
    PCIDevice pdev;
    MemoryRegion mmio;
    XdmaBar bar;
};

static uint64_t fwdfpga_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    FwdFPGAState *fwdfpga = opaque;
    uint64_t val = ~0ULL;
    memcpy(&val, (uint8_t*)&fwdfpga->bar + addr, size);
    return val;
}

static void fwdfpga_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    FwdFPGAState *fwdfpga = opaque;
    memcpy((uint8_t*)&fwdfpga->bar + addr, &val, size);
}

static const MemoryRegionOps fwdfpga_mmio_ops = {
    .read = fwdfpga_mmio_read,
    .write = fwdfpga_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void pci_fwdfpga_realize(PCIDevice *pdev, Error **errp)
{
    FwdFPGAState *fwdfpga = FWD_FPGA(pdev);

    const XdmaBar bar = {
            .h2cChannel0 = {.identifier = 0x1fc00006, .alignment = 0x00010106},
            .h2cChannel1 = {.identifier = 0x1fc00106, .alignment = 0x00010106},
            .c2hChannel0 = {.identifier = 0x1fc10006, .alignment = 0x00010106},
            .c2hChannel1 = {.identifier = 0x1fc10106, .alignment = 0x00010106},
            .configIdentifier = 0x1fc30000,
            .h2cSgdma0 = {.identifier = 0x1fc40006},
            .h2cSgdma1 = {.identifier = 0x1fc40106},
            .c2hSgdma0 = {.identifier = 0x1fc50006},
            .c2hSgdma1 = {.identifier = 0x1fc50106},
    };
    
    fwdfpga->bar = bar;

    memory_region_init_io(&fwdfpga->mmio, OBJECT(fwdfpga), &fwdfpga_mmio_ops, fwdfpga,
            "fwdfpga-mmio", sizeof(XdmaBar));
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &fwdfpga->mmio);
}

static void pci_fwdfpga_uninit(PCIDevice *pdev)
{
}

static void fwdfpga_instance_init(Object *obj)
{
}

static void fwdfpga_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_fwdfpga_realize;
    k->exit = pci_fwdfpga_uninit;
    k->vendor_id = 0x10ee; // Xilinx
    k->device_id = 0xdd01;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_OTHERS;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_fwdfpga_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo fwdfpga_info = {
        .name          = TYPE_PCI_FWD_FPGA_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(FwdFPGAState),
        .instance_init = fwdfpga_instance_init,
        .class_init    = fwdfpga_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&fwdfpga_info);
}
type_init(pci_fwdfpga_register_types)
