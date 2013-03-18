/*
 * QEMU NVRAM emulation for DS1225Y chip
 *
 * Copyright (c) 2007-2008 Hervé Poussineau
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

#include "hw/sysbus.h"
#include "trace.h"

typedef struct {
    DeviceState qdev;
    MemoryRegion iomem;
    uint32_t chip_size;
    char *filename;
    FILE *file;
    uint8_t *contents;
} NvRamState;

static uint64_t nvram_read(void *opaque, hwaddr addr, unsigned size)
{
    NvRamState *s = opaque;
    uint32_t val;

    val = s->contents[addr];
    trace_nvram_read(addr, val);
    return val;
}

static void nvram_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned size)
{
    NvRamState *s = opaque;

    val &= 0xff;
    trace_nvram_write(addr, s->contents[addr], val);

    s->contents[addr] = val;
    if (s->file) {
        fseek(s->file, addr, SEEK_SET);
        fputc(val, s->file);
        fflush(s->file);
    }
}

static const MemoryRegionOps nvram_ops = {
    .read = nvram_read,
    .write = nvram_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int nvram_post_load(void *opaque, int version_id)
{
    NvRamState *s = opaque;

    /* Close file, as filename may has changed in load/store process */
    if (s->file) {
        fclose(s->file);
    }

    /* Write back nvram contents */
    s->file = fopen(s->filename, "wb");
    if (s->file) {
        /* Write back contents, as 'wb' mode cleaned the file */
        if (fwrite(s->contents, s->chip_size, 1, s->file) != 1) {
            printf("nvram_post_load: short write\n");
        }
        fflush(s->file);
    }

    return 0;
}

static const VMStateDescription vmstate_nvram = {
    .name = "nvram",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .post_load = nvram_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_VARRAY_UINT32(contents, NvRamState, chip_size, 0,
                              vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct {
    SysBusDevice busdev;
    NvRamState nvram;
} SysBusNvRamState;

static int nvram_sysbus_initfn(SysBusDevice *dev)
{
    NvRamState *s = &FROM_SYSBUS(SysBusNvRamState, dev)->nvram;
    FILE *file;

    s->contents = g_malloc0(s->chip_size);

    memory_region_init_io(&s->iomem, &nvram_ops, s, "nvram", s->chip_size);
    sysbus_init_mmio(dev, &s->iomem);

    /* Read current file */
    file = fopen(s->filename, "rb");
    if (file) {
        /* Read nvram contents */
        if (fread(s->contents, s->chip_size, 1, file) != 1) {
            printf("nvram_sysbus_initfn: short read\n");
        }
        fclose(file);
    }
    nvram_post_load(s, 0);

    return 0;
}

static Property nvram_sysbus_properties[] = {
    DEFINE_PROP_UINT32("size", SysBusNvRamState, nvram.chip_size, 0x2000),
    DEFINE_PROP_STRING("filename", SysBusNvRamState, nvram.filename),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvram_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = nvram_sysbus_initfn;
    dc->vmsd = &vmstate_nvram;
    dc->props = nvram_sysbus_properties;
}

static const TypeInfo nvram_sysbus_info = {
    .name          = "ds1225y",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusNvRamState),
    .class_init    = nvram_sysbus_class_init,
};

static void nvram_register_types(void)
{
    type_register_static(&nvram_sysbus_info);
}

type_init(nvram_register_types)
