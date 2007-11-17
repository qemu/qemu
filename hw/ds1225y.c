/*
 * QEMU NVRAM emulation for DS1225Y chip
 * 
 * Copyright (c) 2007 Hervé Poussineau
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

typedef enum
{
    none = 0,
    readmode,
    writemode,
} nvram_open_mode;

struct ds1225y_t
{
    target_phys_addr_t mem_base;
    uint32_t capacity;
    const char *filename;
    QEMUFile *file;
    nvram_open_mode open_mode;
};

static int ds1225y_set_to_mode(ds1225y_t *NVRAM, nvram_open_mode mode, const char *filemode)
{
    if (NVRAM->open_mode != mode)
    {
        if (NVRAM->file)
            qemu_fclose(NVRAM->file);
        NVRAM->file = qemu_fopen(NVRAM->filename, filemode);
        NVRAM->open_mode = mode;
    }
    return (NVRAM->file != NULL);
}

static uint32_t nvram_readb (void *opaque, target_phys_addr_t addr)
{
    ds1225y_t *NVRAM = opaque;
    int64_t pos;

    pos = addr - NVRAM->mem_base;
    if (addr >= NVRAM->capacity)
        addr -= NVRAM->capacity;

    if (!ds1225y_set_to_mode(NVRAM, readmode, "rb"))
        return 0;
    qemu_fseek(NVRAM->file, pos, SEEK_SET);
    return (uint32_t)qemu_get_byte(NVRAM->file);
}

static void nvram_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    ds1225y_t *NVRAM = opaque;
    int64_t pos;

    pos = addr - NVRAM->mem_base;
    if (ds1225y_set_to_mode(NVRAM, writemode, "wb"))
    {
        qemu_fseek(NVRAM->file, pos, SEEK_SET);
        qemu_put_byte(NVRAM->file, (int)value);
    }
}

static CPUReadMemoryFunc *nvram_read[] = {
    &nvram_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc *nvram_write[] = {
    &nvram_writeb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc *nvram_none[] = {
    NULL,
    NULL,
    NULL,
};

/* Initialisation routine */
ds1225y_t *ds1225y_init(target_phys_addr_t mem_base, const char *filename)
{
    ds1225y_t *s;
    int mem_index1, mem_index2;

    s = qemu_mallocz(sizeof(ds1225y_t));
    if (!s)
        return NULL;
    s->mem_base = mem_base;
    s->capacity = 0x2000; /* Fixed for ds1225y chip: 8K */
    s->filename = filename;

    /* Read/write memory */
    mem_index1 = cpu_register_io_memory(0, nvram_read, nvram_write, s);
    cpu_register_physical_memory(mem_base, s->capacity, mem_index1);
    /* Read-only memory */
    mem_index2 = cpu_register_io_memory(0, nvram_read, nvram_none, s);
    cpu_register_physical_memory(mem_base + s->capacity, s->capacity, mem_index2);
    return s;
}
