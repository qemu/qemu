/*
 * PowerMac MacIO device emulation
 *
 * Copyright (c) 2005-2007 Fabrice Bellard
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
#include "hw.h"
#include "ppc_mac.h"
#include "pci.h"
#include "escc.h"

typedef struct MacIOState
{
    PCIDevice parent;
    int is_oldworld;
    MemoryRegion bar;
    MemoryRegion *pic_mem;
    MemoryRegion *dbdma_mem;
    MemoryRegion *cuda_mem;
    MemoryRegion *escc_mem;
    void *nvram;
    int nb_ide;
    MemoryRegion *ide_mem[4];
} MacIOState;

static void macio_bar_setup(MacIOState *macio_state)
{
    int i;
    MemoryRegion *bar = &macio_state->bar;

    memory_region_init(bar, "macio", 0x80000);
    if (macio_state->pic_mem) {
        if (macio_state->is_oldworld) {
            /* Heathrow PIC */
            memory_region_add_subregion(bar, 0x00000, macio_state->pic_mem);
        } else {
            /* OpenPIC */
            memory_region_add_subregion(bar, 0x40000, macio_state->pic_mem);
        }
    }
    if (macio_state->dbdma_mem) {
        memory_region_add_subregion(bar, 0x08000, macio_state->dbdma_mem);
    }
    if (macio_state->escc_mem) {
        memory_region_add_subregion(bar, 0x13000, macio_state->escc_mem);
    }
    if (macio_state->cuda_mem) {
        memory_region_add_subregion(bar, 0x16000, macio_state->cuda_mem);
    }
    for (i = 0; i < macio_state->nb_ide; i++) {
        if (macio_state->ide_mem[i]) {
            memory_region_add_subregion(bar, 0x1f000 + (i * 0x1000),
                                        macio_state->ide_mem[i]);
        }
    }
    if (macio_state->nvram != NULL)
        macio_nvram_setup_bar(macio_state->nvram, bar, 0x60000);
}

static int macio_initfn(PCIDevice *d)
{
    d->config[0x3d] = 0x01; // interrupt on pin 1
    return 0;
}

static void macio_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = macio_initfn;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->class_id = PCI_CLASS_OTHERS << 8;
}

static TypeInfo macio_info = {
    .name          = "macio",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MacIOState),
    .class_init    = macio_class_init,
};

static void macio_register_types(void)
{
    type_register_static(&macio_info);
}

type_init(macio_register_types)

void macio_init (PCIBus *bus, int device_id, int is_oldworld,
                 MemoryRegion *pic_mem, MemoryRegion *dbdma_mem,
                 MemoryRegion *cuda_mem, void *nvram,
                 int nb_ide, MemoryRegion **ide_mem,
                 MemoryRegion *escc_mem)
{
    PCIDevice *d;
    MacIOState *macio_state;
    int i;

    d = pci_create_simple(bus, -1, "macio");

    macio_state = DO_UPCAST(MacIOState, parent, d);
    macio_state->is_oldworld = is_oldworld;
    macio_state->pic_mem = pic_mem;
    macio_state->dbdma_mem = dbdma_mem;
    macio_state->cuda_mem = cuda_mem;
    macio_state->escc_mem = escc_mem;
    macio_state->nvram = nvram;
    if (nb_ide > 4)
        nb_ide = 4;
    macio_state->nb_ide = nb_ide;
    for (i = 0; i < nb_ide; i++)
        macio_state->ide_mem[i] = ide_mem[i];
    for (; i < 4; i++)
        macio_state->ide_mem[i] = NULL;
    /* Note: this code is strongly inspirated from the corresponding code
       in PearPC */

    pci_config_set_device_id(d->config, device_id);

    macio_bar_setup(macio_state);
    pci_register_bar(d, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &macio_state->bar);
}
