/*
 * Memory mapped access to ISA IO space.
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include "isa.h"

static void isa_mmio_writeb (void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    cpu_outb(addr & IOPORTS_MASK, val);
}

static void isa_mmio_writew(void *opaque, target_phys_addr_t addr,
                               uint32_t val)
{
    cpu_outw(addr & IOPORTS_MASK, val);
}

static void isa_mmio_writel(void *opaque, target_phys_addr_t addr,
                               uint32_t val)
{
    cpu_outl(addr & IOPORTS_MASK, val);
}

static uint32_t isa_mmio_readb (void *opaque, target_phys_addr_t addr)
{
    return cpu_inb(addr & IOPORTS_MASK);
}

static uint32_t isa_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    return cpu_inw(addr & IOPORTS_MASK);
}

static uint32_t isa_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    return cpu_inl(addr & IOPORTS_MASK);
}

static CPUWriteMemoryFunc * const isa_mmio_write[] = {
    &isa_mmio_writeb,
    &isa_mmio_writew,
    &isa_mmio_writel,
};

static CPUReadMemoryFunc * const isa_mmio_read[] = {
    &isa_mmio_readb,
    &isa_mmio_readw,
    &isa_mmio_readl,
};

void isa_mmio_init(target_phys_addr_t base, target_phys_addr_t size)
{
    int isa_mmio_iomemtype;

    isa_mmio_iomemtype = cpu_register_io_memory(isa_mmio_read,
                                                isa_mmio_write,
                                                NULL,
                                                DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, size, isa_mmio_iomemtype);
}
