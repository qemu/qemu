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
#include "firmware_abi.h"
#include "sysemu.h"
#include "ppc_mac.h"

/* debug NVR */
//#define DEBUG_NVR

#ifdef DEBUG_NVR
#define NVR_DPRINTF(fmt, ...)                                   \
    do { printf("NVR: " fmt , ## __VA_ARGS__); } while (0)
#else
#define NVR_DPRINTF(fmt, ...)
#endif

struct MacIONVRAMState {
    uint32_t size;
    int mem_index;
    unsigned int it_shift;
    uint8_t *data;
};

#define DEF_SYSTEM_SIZE 0xc10

/* Direct access to NVRAM */
uint32_t macio_nvram_read (void *opaque, uint32_t addr)
{
    MacIONVRAMState *s = opaque;
    uint32_t ret;

    if (addr < s->size)
        ret = s->data[addr];
    else
        ret = -1;
    NVR_DPRINTF("read addr %04x val %x\n", addr, ret);

    return ret;
}

void macio_nvram_write (void *opaque, uint32_t addr, uint32_t val)
{
    MacIONVRAMState *s = opaque;

    NVR_DPRINTF("write addr %04x val %x\n", addr, val);
    if (addr < s->size)
        s->data[addr] = val;
}

/* macio style NVRAM device */
static void macio_nvram_writeb (void *opaque,
                                target_phys_addr_t addr, uint32_t value)
{
    MacIONVRAMState *s = opaque;

    addr = (addr >> s->it_shift) & (s->size - 1);
    s->data[addr] = value;
    NVR_DPRINTF("writeb addr %04x val %x\n", (int)addr, value);
}

static uint32_t macio_nvram_readb (void *opaque, target_phys_addr_t addr)
{
    MacIONVRAMState *s = opaque;
    uint32_t value;

    addr = (addr >> s->it_shift) & (s->size - 1);
    value = s->data[addr];
    NVR_DPRINTF("readb addr %04x val %x\n", (int)addr, value);

    return value;
}

static CPUWriteMemoryFunc * const nvram_write[] = {
    &macio_nvram_writeb,
    &macio_nvram_writeb,
    &macio_nvram_writeb,
};

static CPUReadMemoryFunc * const nvram_read[] = {
    &macio_nvram_readb,
    &macio_nvram_readb,
    &macio_nvram_readb,
};

static const VMStateDescription vmstate_macio_nvram = {
    .name = "macio_nvram",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(data, MacIONVRAMState, 0, NULL, 0, size),
        VMSTATE_END_OF_LIST()
    }
};


static void macio_nvram_reset(void *opaque)
{
}

MacIONVRAMState *macio_nvram_init (int *mem_index, target_phys_addr_t size,
                                   unsigned int it_shift)
{
    MacIONVRAMState *s;

    s = qemu_mallocz(sizeof(MacIONVRAMState));
    s->data = qemu_mallocz(size);
    s->size = size;
    s->it_shift = it_shift;

    s->mem_index = cpu_register_io_memory(nvram_read, nvram_write, s,
                                          DEVICE_NATIVE_ENDIAN);
    *mem_index = s->mem_index;
    vmstate_register(NULL, -1, &vmstate_macio_nvram, s);
    qemu_register_reset(macio_nvram_reset, s);

    return s;
}

void macio_nvram_map (void *opaque, target_phys_addr_t mem_base)
{
    MacIONVRAMState *s;

    s = opaque;
    cpu_register_physical_memory(mem_base, s->size << s->it_shift,
                                 s->mem_index);
}

/* Set up a system OpenBIOS NVRAM partition */
void pmac_format_nvram_partition (MacIONVRAMState *nvr, int len)
{
    unsigned int i;
    uint32_t start = 0, end;
    struct OpenBIOS_nvpart_v1 *part_header;

    // OpenBIOS nvram variables
    // Variable partition
    part_header = (struct OpenBIOS_nvpart_v1 *)nvr->data;
    part_header->signature = OPENBIOS_PART_SYSTEM;
    pstrcpy(part_header->name, sizeof(part_header->name), "system");

    end = start + sizeof(struct OpenBIOS_nvpart_v1);
    for (i = 0; i < nb_prom_envs; i++)
        end = OpenBIOS_set_var(nvr->data, end, prom_envs[i]);

    // End marker
    nvr->data[end++] = '\0';

    end = start + ((end - start + 15) & ~15);
    /* XXX: OpenBIOS is not able to grow up a partition. Leave some space for
       new variables. */
    if (end < DEF_SYSTEM_SIZE)
        end = DEF_SYSTEM_SIZE;
    OpenBIOS_finish_partition(part_header, end - start);

    // free partition
    start = end;
    part_header = (struct OpenBIOS_nvpart_v1 *)&nvr->data[start];
    part_header->signature = OPENBIOS_PART_FREE;
    pstrcpy(part_header->name, sizeof(part_header->name), "free");

    end = len;
    OpenBIOS_finish_partition(part_header, end - start);
}
