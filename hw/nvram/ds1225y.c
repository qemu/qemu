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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

typedef struct {
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
    s->file = s->filename ? fopen(s->filename, "wb") : NULL;
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
    .post_load = nvram_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_VARRAY_UINT32(contents, NvRamState, chip_size, 0,
                              vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

#define TYPE_DS1225Y "ds1225y"
#define DS1225Y(obj) OBJECT_CHECK(SysBusNvRamState, (obj), TYPE_DS1225Y)

typedef struct {
    SysBusDevice parent_obj;

    NvRamState nvram;
} SysBusNvRamState;

static void nvram_sysbus_realize(DeviceState *dev, Error **errp)
{
    SysBusNvRamState *sys = DS1225Y(dev);
    NvRamState *s = &sys->nvram;
    FILE *file;

    s->contents = g_malloc0(s->chip_size);

    memory_region_init_io(&s->iomem, OBJECT(s), &nvram_ops, s,
                          "nvram", s->chip_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    /* Read current file */
    file = s->filename ? fopen(s->filename, "rb") : NULL;
    if (file) {
        /* Read nvram contents */
        if (fread(s->contents, s->chip_size, 1, file) != 1) {
            error_report("nvram_sysbus_realize: short read");
        }
        fclose(file);
    }
    nvram_post_load(s, 0);
}

static Property nvram_sysbus_properties[] = {
    DEFINE_PROP_UINT32("size", SysBusNvRamState, nvram.chip_size, 0x2000),
    DEFINE_PROP_STRING("filename", SysBusNvRamState, nvram.filename),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvram_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nvram_sysbus_realize;
    dc->vmsd = &vmstate_nvram;
    dc->props = nvram_sysbus_properties;
}

static const TypeInfo nvram_sysbus_info = {
    .name          = TYPE_DS1225Y,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusNvRamState),
    .class_init    = nvram_sysbus_class_init,
};

static void nvram_register_types(void)
{
    type_register_static(&nvram_sysbus_info);
}

type_init(nvram_register_types)
