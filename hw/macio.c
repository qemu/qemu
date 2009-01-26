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

typedef struct macio_state_t macio_state_t;
struct macio_state_t {
    int is_oldworld;
    int pic_mem_index;
    int dbdma_mem_index;
    int cuda_mem_index;
    int escc_mem_index;
    void *nvram;
    int nb_ide;
    int ide_mem_index[4];
};

static void macio_map (PCIDevice *pci_dev, int region_num,
                       uint32_t addr, uint32_t size, int type)
{
    macio_state_t *macio_state;
    int i;

    macio_state = (macio_state_t *)(pci_dev + 1);
    if (macio_state->pic_mem_index >= 0) {
        if (macio_state->is_oldworld) {
            /* Heathrow PIC */
            cpu_register_physical_memory(addr + 0x00000, 0x1000,
                                         macio_state->pic_mem_index);
        } else {
            /* OpenPIC */
            cpu_register_physical_memory(addr + 0x40000, 0x40000,
                                         macio_state->pic_mem_index);
        }
    }
    if (macio_state->dbdma_mem_index >= 0) {
        cpu_register_physical_memory(addr + 0x08000, 0x1000,
                                     macio_state->dbdma_mem_index);
    }
    if (macio_state->escc_mem_index >= 0) {
        cpu_register_physical_memory(addr + 0x13000, ESCC_SIZE << 4,
                                     macio_state->escc_mem_index);
    }
    if (macio_state->cuda_mem_index >= 0) {
        cpu_register_physical_memory(addr + 0x16000, 0x2000,
                                     macio_state->cuda_mem_index);
    }
    for (i = 0; i < macio_state->nb_ide; i++) {
        if (macio_state->ide_mem_index[i] >= 0) {
            cpu_register_physical_memory(addr + 0x1f000 + (i * 0x1000), 0x1000,
                                         macio_state->ide_mem_index[i]);
        }
    }
    if (macio_state->nvram != NULL)
        macio_nvram_map(macio_state->nvram, addr + 0x60000);
}

void macio_init (PCIBus *bus, int device_id, int is_oldworld, int pic_mem_index,
                 int dbdma_mem_index, int cuda_mem_index, void *nvram,
                 int nb_ide, int *ide_mem_index, int escc_mem_index)
{
    PCIDevice *d;
    macio_state_t *macio_state;
    int i;

    d = pci_register_device(bus, "macio",
                            sizeof(PCIDevice) + sizeof(macio_state_t),
                            -1, NULL, NULL);
    macio_state = (macio_state_t *)(d + 1);
    macio_state->is_oldworld = is_oldworld;
    macio_state->pic_mem_index = pic_mem_index;
    macio_state->dbdma_mem_index = dbdma_mem_index;
    macio_state->cuda_mem_index = cuda_mem_index;
    macio_state->escc_mem_index = escc_mem_index;
    macio_state->nvram = nvram;
    if (nb_ide > 4)
        nb_ide = 4;
    macio_state->nb_ide = nb_ide;
    for (i = 0; i < nb_ide; i++)
        macio_state->ide_mem_index[i] = ide_mem_index[i];
    for (; i < 4; i++)
        macio_state->ide_mem_index[i] = -1;
    /* Note: this code is strongly inspirated from the corresponding code
       in PearPC */

    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, device_id);

    d->config[0x0a] = 0x00; // class_sub = pci2pci
    d->config[0x0b] = 0xff; // class_base = bridge
    d->config[0x0e] = 0x00; // header_type

    d->config[0x3d] = 0x01; // interrupt on pin 1

    pci_register_io_region(d, 0, 0x80000,
                           PCI_ADDRESS_SPACE_MEM, macio_map);
}
