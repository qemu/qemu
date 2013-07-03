/*
 * QEMU sPAPR NVRAM emulation
 *
 * Copyright (C) 2012 David Gibson, IBM Corporation.
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

#include <libfdt.h>

#include "sysemu/device_tree.h"
#include "hw/sysbus.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"

typedef struct sPAPRNVRAM {
    VIOsPAPRDevice sdev;
    uint32_t size;
    uint8_t *buf;
    BlockDriverState *drive;
} sPAPRNVRAM;

#define TYPE_VIO_SPAPR_NVRAM "spapr-nvram"
#define VIO_SPAPR_NVRAM(obj) \
     OBJECT_CHECK(sPAPRNVRAM, (obj), TYPE_VIO_SPAPR_NVRAM)

#define MIN_NVRAM_SIZE 8192
#define DEFAULT_NVRAM_SIZE 65536
#define MAX_NVRAM_SIZE (UINT16_MAX * 16)

static void rtas_nvram_fetch(sPAPREnvironment *spapr,
                             uint32_t token, uint32_t nargs,
                             target_ulong args,
                             uint32_t nret, target_ulong rets)
{
    sPAPRNVRAM *nvram = spapr->nvram;
    hwaddr offset, buffer, len;
    int alen;
    void *membuf;

    if ((nargs != 3) || (nret != 2)) {
        rtas_st(rets, 0, -3);
        return;
    }

    if (!nvram) {
        rtas_st(rets, 0, -1);
        rtas_st(rets, 1, 0);
        return;
    }

    offset = rtas_ld(args, 0);
    buffer = rtas_ld(args, 1);
    len = rtas_ld(args, 2);

    if (((offset + len) < offset)
        || ((offset + len) > nvram->size)) {
        rtas_st(rets, 0, -3);
        rtas_st(rets, 1, 0);
        return;
    }

    membuf = cpu_physical_memory_map(buffer, &len, 1);
    if (nvram->drive) {
        alen = bdrv_pread(nvram->drive, offset, membuf, len);
    } else {
        assert(nvram->buf);

        memcpy(membuf, nvram->buf + offset, len);
        alen = len;
    }
    cpu_physical_memory_unmap(membuf, len, 1, len);

    rtas_st(rets, 0, (alen < len) ? -1 : 0);
    rtas_st(rets, 1, (alen < 0) ? 0 : alen);
}

static void rtas_nvram_store(sPAPREnvironment *spapr,
                             uint32_t token, uint32_t nargs,
                             target_ulong args,
                             uint32_t nret, target_ulong rets)
{
    sPAPRNVRAM *nvram = spapr->nvram;
    hwaddr offset, buffer, len;
    int alen;
    void *membuf;

    if ((nargs != 3) || (nret != 2)) {
        rtas_st(rets, 0, -3);
        return;
    }

    if (!nvram) {
        rtas_st(rets, 0, -1);
        return;
    }

    offset = rtas_ld(args, 0);
    buffer = rtas_ld(args, 1);
    len = rtas_ld(args, 2);

    if (((offset + len) < offset)
        || ((offset + len) > nvram->size)) {
        rtas_st(rets, 0, -3);
        return;
    }

    membuf = cpu_physical_memory_map(buffer, &len, 0);
    if (nvram->drive) {
        alen = bdrv_pwrite(nvram->drive, offset, membuf, len);
    } else {
        assert(nvram->buf);

        memcpy(nvram->buf + offset, membuf, len);
        alen = len;
    }
    cpu_physical_memory_unmap(membuf, len, 0, len);

    rtas_st(rets, 0, (alen < len) ? -1 : 0);
    rtas_st(rets, 1, (alen < 0) ? 0 : alen);
}

static int spapr_nvram_init(VIOsPAPRDevice *dev)
{
    sPAPRNVRAM *nvram = VIO_SPAPR_NVRAM(dev);

    if (nvram->drive) {
        nvram->size = bdrv_getlength(nvram->drive);
    } else {
        nvram->size = DEFAULT_NVRAM_SIZE;
        nvram->buf = g_malloc0(nvram->size);
    }

    if ((nvram->size < MIN_NVRAM_SIZE) || (nvram->size > MAX_NVRAM_SIZE)) {
        fprintf(stderr, "spapr-nvram must be between %d and %d bytes in size\n",
                MIN_NVRAM_SIZE, MAX_NVRAM_SIZE);
        return -1;
    }

    spapr_rtas_register("nvram-fetch", rtas_nvram_fetch);
    spapr_rtas_register("nvram-store", rtas_nvram_store);

    return 0;
}

static int spapr_nvram_devnode(VIOsPAPRDevice *dev, void *fdt, int node_off)
{
    sPAPRNVRAM *nvram = VIO_SPAPR_NVRAM(dev);

    return fdt_setprop_cell(fdt, node_off, "#bytes", nvram->size);
}

static Property spapr_nvram_properties[] = {
    DEFINE_SPAPR_PROPERTIES(sPAPRNVRAM, sdev),
    DEFINE_PROP_DRIVE("drive", sPAPRNVRAM, drive),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_nvram_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VIOsPAPRDeviceClass *k = VIO_SPAPR_DEVICE_CLASS(klass);

    k->init = spapr_nvram_init;
    k->devnode = spapr_nvram_devnode;
    k->dt_name = "nvram";
    k->dt_type = "nvram";
    k->dt_compatible = "qemu,spapr-nvram";
    dc->props = spapr_nvram_properties;
}

static const TypeInfo spapr_nvram_type_info = {
    .name          = TYPE_VIO_SPAPR_NVRAM,
    .parent        = TYPE_VIO_SPAPR_DEVICE,
    .instance_size = sizeof(sPAPRNVRAM),
    .class_init    = spapr_nvram_class_init,
};

static void spapr_nvram_register_types(void)
{
    type_register_static(&spapr_nvram_type_info);
}

type_init(spapr_nvram_register_types)
