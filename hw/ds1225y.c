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

#include "hw.h"
#include "mips.h"
#include "nvram.h"

//#define DEBUG_NVRAM

typedef struct ds1225y_t
{
    uint32_t chip_size;
    QEMUFile *file;
    uint8_t *contents;
    uint8_t protection;
} ds1225y_t;


static uint32_t nvram_readb (void *opaque, target_phys_addr_t addr)
{
    ds1225y_t *s = opaque;
    uint32_t val;

    val = s->contents[addr];

#ifdef DEBUG_NVRAM
    printf("nvram: read 0x%x at " TARGET_FMT_lx "\n", val, addr);
#endif
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
    ds1225y_t *s = opaque;

#ifdef DEBUG_NVRAM
    printf("nvram: write 0x%x at " TARGET_FMT_lx "\n", val, addr);
#endif

    s->contents[addr] = val & 0xff;
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

static void nvram_writeb_protected (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    ds1225y_t *s = opaque;

    if (s->protection != 7) {
#ifdef DEBUG_NVRAM
    printf("nvram: prevent write of 0x%x at " TARGET_FMT_lx "\n", val, addr);
#endif
        return;
    }

    nvram_writeb(opaque, addr, val);
}

static void nvram_writew_protected (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    nvram_writeb_protected(opaque, addr, val & 0xff);
    nvram_writeb_protected(opaque, addr + 1, (val >> 8) & 0xff);
}

static void nvram_writel_protected (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    nvram_writeb_protected(opaque, addr, val & 0xff);
    nvram_writeb_protected(opaque, addr + 1, (val >> 8) & 0xff);
    nvram_writeb_protected(opaque, addr + 2, (val >> 16) & 0xff);
    nvram_writeb_protected(opaque, addr + 3, (val >> 24) & 0xff);
}

static CPUReadMemoryFunc *nvram_read[] = {
    &nvram_readb,
    &nvram_readw,
    &nvram_readl,
};

static CPUWriteMemoryFunc *nvram_write[] = {
    &nvram_writeb,
    &nvram_writew,
    &nvram_writel,
};

static CPUWriteMemoryFunc *nvram_write_protected[] = {
    &nvram_writeb_protected,
    &nvram_writew_protected,
    &nvram_writel_protected,
};

/* Initialisation routine */
void *ds1225y_init(target_phys_addr_t mem_base, const char *filename)
{
    ds1225y_t *s;
    int mem_indexRW, mem_indexRP;
    QEMUFile *file;

    s = qemu_mallocz(sizeof(ds1225y_t));
    if (!s)
        return NULL;
    s->chip_size = 0x2000; /* Fixed for ds1225y chip: 8 KiB */
    s->contents = qemu_mallocz(s->chip_size);
    if (!s->contents) {
        return NULL;
    }
    s->protection = 7;

    /* Read current file */
    file = qemu_fopen(filename, "rb");
    if (file) {
        /* Read nvram contents */
        qemu_get_buffer(file, s->contents, s->chip_size);
        qemu_fclose(file);
    }
    s->file = qemu_fopen(filename, "wb");
    if (s->file) {
        /* Write back contents, as 'wb' mode cleaned the file */
        qemu_put_buffer(s->file, s->contents, s->chip_size);
        qemu_fflush(s->file);
    }

    /* Read/write memory */
    mem_indexRW = cpu_register_io_memory(0, nvram_read, nvram_write, s);
    cpu_register_physical_memory(mem_base, s->chip_size, mem_indexRW);
    /* Read/write protected memory */
    mem_indexRP = cpu_register_io_memory(0, nvram_read, nvram_write_protected, s);
    cpu_register_physical_memory(mem_base + s->chip_size, s->chip_size, mem_indexRP);
    return s;
}
