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
#include "ppc/mac.h"
#include "pci/pci.h"
#include "escc.h"

#define TYPE_MACIO "macio"
#define MACIO(obj) OBJECT_CHECK(MacIOState, (obj), TYPE_MACIO)

typedef struct MacIOState
{
    /*< private >*/
    PCIDevice parent;
    /*< public >*/

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

static int macio_common_initfn(PCIDevice *d)
{
    MacIOState *s = MACIO(d);

    d->config[0x3d] = 0x01; // interrupt on pin 1

    macio_bar_setup(s);
    pci_register_bar(d, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar);

    return 0;
}

static int macio_oldworld_initfn(PCIDevice *d)
{
    MacIOState *s = MACIO(d);
    int ret = macio_common_initfn(d);
    if (ret < 0) {
        return ret;
    }

    if (s->pic_mem) {
        /* Heathrow PIC */
        memory_region_add_subregion(&s->bar, 0x00000, s->pic_mem);
    }

    return 0;
}

static int macio_newworld_initfn(PCIDevice *d)
{
    MacIOState *s = MACIO(d);
    int ret = macio_common_initfn(d);
    if (ret < 0) {
        return ret;
    }

    if (s->pic_mem) {
        /* OpenPIC */
        memory_region_add_subregion(&s->bar, 0x40000, s->pic_mem);
    }

    return 0;
}

static void macio_instance_init(Object *obj)
{
    MacIOState *s = MACIO(obj);

    memory_region_init(&s->bar, "macio", 0x80000);
}

static void macio_oldworld_class_init(ObjectClass *oc, void *data)
{
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(oc);

    pdc->init = macio_oldworld_initfn;
    pdc->device_id = PCI_DEVICE_ID_APPLE_343S1201;
}

static void macio_newworld_class_init(ObjectClass *oc, void *data)
{
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(oc);

    pdc->init = macio_newworld_initfn;
    pdc->device_id = PCI_DEVICE_ID_APPLE_UNI_N_KEYL;
}

static void macio_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->class_id = PCI_CLASS_OTHERS << 8;
}

static const TypeInfo macio_oldworld_type_info = {
    .name          = TYPE_OLDWORLD_MACIO,
    .parent        = TYPE_MACIO,
    .class_init    = macio_oldworld_class_init,
};

static const TypeInfo macio_newworld_type_info = {
    .name          = TYPE_NEWWORLD_MACIO,
    .parent        = TYPE_MACIO,
    .class_init    = macio_newworld_class_init,
};

static const TypeInfo macio_type_info = {
    .name          = TYPE_MACIO,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MacIOState),
    .instance_init = macio_instance_init,
    .abstract      = true,
    .class_init    = macio_class_init,
};

static void macio_register_types(void)
{
    type_register_static(&macio_type_info);
    type_register_static(&macio_oldworld_type_info);
    type_register_static(&macio_newworld_type_info);
}

type_init(macio_register_types)

void macio_init(PCIDevice *d,
                MemoryRegion *pic_mem, MemoryRegion *dbdma_mem,
                MemoryRegion *cuda_mem, void *nvram,
                int nb_ide, MemoryRegion **ide_mem,
                MemoryRegion *escc_mem)
{
    MacIOState *macio_state = MACIO(d);
    int i;

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
    /* Note: this code is strongly inspirated from the corresponding code
       in PearPC */

    qdev_init_nofail(DEVICE(d));
}
