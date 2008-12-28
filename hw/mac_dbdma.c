/*
 * PowerMac descriptor-based DMA emulation
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

/* debug DBDMA */
//#define DEBUG_DBDMA

#ifdef DEBUG_DBDMA
#define DBDMA_DPRINTF(fmt, args...) \
do { printf("DBDMA: " fmt , ##args); } while (0)
#else
#define DBDMA_DPRINTF(fmt, args...)
#endif

/* DBDMA: currently no op - should suffice right now */

static void dbdma_writeb (void *opaque,
                          target_phys_addr_t addr, uint32_t value)
{
    DBDMA_DPRINTF("writeb 0x" TARGET_FMT_plx " <= 0x%08x\n", addr, value);
}

static void dbdma_writew (void *opaque,
                          target_phys_addr_t addr, uint32_t value)
{
    DBDMA_DPRINTF("writew 0x" TARGET_FMT_plx " <= 0x%08x\n", addr, value);
}

static void dbdma_writel (void *opaque,
                          target_phys_addr_t addr, uint32_t value)
{
    DBDMA_DPRINTF("writel 0x" TARGET_FMT_plx " <= 0x%08x\n", addr, value);
}

static uint32_t dbdma_readb (void *opaque, target_phys_addr_t addr)
{
    DBDMA_DPRINTF("readb 0x" TARGET_FMT_plx " => 0\n", addr);

    return 0;
}

static uint32_t dbdma_readw (void *opaque, target_phys_addr_t addr)
{
    DBDMA_DPRINTF("readw 0x" TARGET_FMT_plx " => 0\n", addr);

    return 0;
}

static uint32_t dbdma_readl (void *opaque, target_phys_addr_t addr)
{
    DBDMA_DPRINTF("readl 0x" TARGET_FMT_plx " => 0\n", addr);

    return 0;
}

static CPUWriteMemoryFunc *dbdma_write[] = {
    &dbdma_writeb,
    &dbdma_writew,
    &dbdma_writel,
};

static CPUReadMemoryFunc *dbdma_read[] = {
    &dbdma_readb,
    &dbdma_readw,
    &dbdma_readl,
};

static void dbdma_reset(void *opaque)
{
}

void dbdma_init (int *dbdma_mem_index)
{
    *dbdma_mem_index = cpu_register_io_memory(0, dbdma_read, dbdma_write, NULL);
    qemu_register_reset(dbdma_reset, NULL);
    dbdma_reset(NULL);
}
