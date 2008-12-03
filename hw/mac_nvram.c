/*
 * PowerMac NVRAM emulation
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

struct MacIONVRAMState {
    target_phys_addr_t size;
    int mem_index;
    uint8_t data[0x2000];
};

/* Direct access to NVRAM */
uint32_t macio_nvram_read (void *opaque, uint32_t addr)
{
    MacIONVRAMState *s = opaque;
    uint32_t ret;

    //    printf("%s: %p addr %04x\n", __func__, s, addr);
    if (addr < 0x2000)
        ret = s->data[addr];
    else
        ret = -1;

    return ret;
}

void macio_nvram_write (void *opaque, uint32_t addr, uint32_t val)
{
    MacIONVRAMState *s = opaque;

    //    printf("%s: %p addr %04x val %02x\n", __func__, s, addr, val);
    if (addr < 0x2000)
        s->data[addr] = val;
}

/* macio style NVRAM device */
static void macio_nvram_writeb (void *opaque,
                                target_phys_addr_t addr, uint32_t value)
{
    MacIONVRAMState *s = opaque;

    addr = (addr >> 4) & 0x1fff;
    s->data[addr] = value;
    //    printf("macio_nvram_writeb %04x = %02x\n", addr, value);
}

static uint32_t macio_nvram_readb (void *opaque, target_phys_addr_t addr)
{
    MacIONVRAMState *s = opaque;
    uint32_t value;

    addr = (addr >> 4) & 0x1fff;
    value = s->data[addr];
    //    printf("macio_nvram_readb %04x = %02x\n", addr, value);

    return value;
}

static CPUWriteMemoryFunc *nvram_write[] = {
    &macio_nvram_writeb,
    &macio_nvram_writeb,
    &macio_nvram_writeb,
};

static CPUReadMemoryFunc *nvram_read[] = {
    &macio_nvram_readb,
    &macio_nvram_readb,
    &macio_nvram_readb,
};

MacIONVRAMState *macio_nvram_init (int *mem_index, target_phys_addr_t size)
{
    MacIONVRAMState *s;

    s = qemu_mallocz(sizeof(MacIONVRAMState));
    if (!s)
        return NULL;
    s->size = size;
    s->mem_index = cpu_register_io_memory(0, nvram_read, nvram_write, s);
    *mem_index = s->mem_index;

    return s;
}

void macio_nvram_map (void *opaque, target_phys_addr_t mem_base)
{
    MacIONVRAMState *s;

    s = opaque;
    cpu_register_physical_memory(mem_base, s->size, s->mem_index);
}

static uint8_t nvram_chksum (const uint8_t *buf, int n)
{
    int sum, i;
    sum = 0;
    for(i = 0; i < n; i++)
        sum += buf[i];
    return (sum & 0xff) + (sum >> 8);
}

/* set a free Mac OS NVRAM partition */
void pmac_format_nvram_partition (MacIONVRAMState *nvr, int len)
{
    uint8_t *buf;
    char partition_name[12] = "wwwwwwwwwwww";

    buf = nvr->data;
    buf[0] = 0x7f; /* free partition magic */
    buf[1] = 0; /* checksum */
    buf[2] = len >> 8;
    buf[3] = len;
    memcpy(buf + 4, partition_name, 12);
    buf[1] = nvram_chksum(buf, 16);
}
