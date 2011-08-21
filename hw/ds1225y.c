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

#include "sysbus.h"
#include "trace.h"

typedef struct {
    DeviceState qdev;
    uint32_t chip_size;
    char *filename;
    QEMUFile *file;
    uint8_t *contents;
} NvRamState;

static uint32_t nvram_readb (void *opaque, target_phys_addr_t addr)
{
    NvRamState *s = opaque;
    uint32_t val;

    val = s->contents[addr];
    trace_nvram_read(addr, val);
    return val;
}

static uint32_t nvram_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = nvram_readb(opaque, addr);
    v |= nvram_readb(opaque, addr + 1) << 8;
    return v;
}

static uint32_t nvram_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = nvram_readb(opaque, addr);
    v |= nvram_readb(opaque, addr + 1) << 8;
    v |= nvram_readb(opaque, addr + 2) << 16;
    v |= nvram_readb(opaque, addr + 3) << 24;
    return v;
}

static void nvram_writeb (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    NvRamState *s = opaque;

    val &= 0xff;
    trace_nvram_write(addr, s->contents[addr], val);

    s->contents[addr] = val;
    if (s->file) {
        qemu_fseek(s->file, addr, SEEK_SET);
        qemu_put_byte(s->file, (int)val);
        qemu_fflush(s->file);
    }
}

static void nvram_writew (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    nvram_writeb(opaque, addr, val & 0xff);
    nvram_writeb(opaque, addr + 1, (val >> 8) & 0xff);
}

static void nvram_writel (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    nvram_writeb(opaque, addr, val & 0xff);
    nvram_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    nvram_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    nvram_writeb(opaque, addr + 3, (val >> 24) & 0xff);
}

static CPUReadMemoryFunc * const nvram_read[] = {
    &nvram_readb,
    &nvram_readw,
    &nvram_readl,
};

static CPUWriteMemoryFunc * const nvram_write[] = {
    &nvram_writeb,
    &nvram_writew,
    &nvram_writel,
};

static int nvram_post_load(void *opaque, int version_id)
{
    NvRamState *s = opaque;

    /* Close file, as filename may has changed in load/store process */
    if (s->file) {
        qemu_fclose(s->file);
    }

    /* Write back nvram contents */
    s->file = qemu_fopen(s->filename, "wb");
    if (s->file) {
        /* Write back contents, as 'wb' mode cleaned the file */
        qemu_put_buffer(s->file, s->contents, s->chip_size);
        qemu_fflush(s->file);
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
    QEMUFile *file;
    int s_io;

    s->contents = g_malloc0(s->chip_size);

    s_io = cpu_register_io_memory(nvram_read, nvram_write, s,
                                  DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, s->chip_size, s_io);

    /* Read current file */
    file = qemu_fopen(s->filename, "rb");
    if (file) {
        /* Read nvram contents */
        qemu_get_buffer(file, s->contents, s->chip_size);
        qemu_fclose(file);
    }
    nvram_post_load(s, 0);

    return 0;
}

static SysBusDeviceInfo nvram_sysbus_info = {
    .qdev.name  = "ds1225y",
    .qdev.size  = sizeof(SysBusNvRamState),
    .qdev.vmsd  = &vmstate_nvram,
    .init       = nvram_sysbus_initfn,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("size", SysBusNvRamState, nvram.chip_size, 0x2000),
        DEFINE_PROP_STRING("filename", SysBusNvRamState, nvram.filename),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void nvram_register(void)
{
    sysbus_register_withprop(&nvram_sysbus_info);
}

device_init(nvram_register)
