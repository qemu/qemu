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

/* DBDMA: currently no op - should suffice right now */

static void dbdma_writeb (void *opaque,
                          target_phys_addr_t addr, uint32_t value)
{
    printf("%s: 0x" PADDRX " <= 0x%08x\n", __func__, addr, value);
}

static void dbdma_writew (void *opaque,
                          target_phys_addr_t addr, uint32_t value)
{
}

static void dbdma_writel (void *opaque,
                          target_phys_addr_t addr, uint32_t value)
{
}

static uint32_t dbdma_readb (void *opaque, target_phys_addr_t addr)
{
    printf("%s: 0x" PADDRX " => 0x00000000\n", __func__, addr);

    return 0;
}

static uint32_t dbdma_readw (void *opaque, target_phys_addr_t addr)
{
    return 0;
}

static uint32_t dbdma_readl (void *opaque, target_phys_addr_t addr)
{
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

void dbdma_init (int *dbdma_mem_index)
{
    *dbdma_mem_index = cpu_register_io_memory(0, dbdma_read, dbdma_write, NULL);
}
