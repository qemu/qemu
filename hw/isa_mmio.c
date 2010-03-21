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

static void isa_mmio_writew_be(void *opaque, target_phys_addr_t addr,
                               uint32_t val)
{
    val = bswap16(val);
    cpu_outw(addr & IOPORTS_MASK, val);
}

static void isa_mmio_writew_le(void *opaque, target_phys_addr_t addr,
                               uint32_t val)
{
    cpu_outw(addr & IOPORTS_MASK, val);
}

static void isa_mmio_writel_be(void *opaque, target_phys_addr_t addr,
                               uint32_t val)
{
    val = bswap32(val);
    cpu_outl(addr & IOPORTS_MASK, val);
}

static void isa_mmio_writel_le(void *opaque, target_phys_addr_t addr,
                               uint32_t val)
{
    cpu_outl(addr & IOPORTS_MASK, val);
}

static uint32_t isa_mmio_readb (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = cpu_inb(addr & IOPORTS_MASK);
    return val;
}

static uint32_t isa_mmio_readw_be(void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = cpu_inw(addr & IOPORTS_MASK);
    val = bswap16(val);
    return val;
}

static uint32_t isa_mmio_readw_le(void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = cpu_inw(addr & IOPORTS_MASK);
    return val;
}

static uint32_t isa_mmio_readl_be(void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = cpu_inl(addr & IOPORTS_MASK);
    val = bswap32(val);
    return val;
}

static uint32_t isa_mmio_readl_le(void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = cpu_inl(addr & IOPORTS_MASK);
    return val;
}

static CPUWriteMemoryFunc * const isa_mmio_write_be[] = {
    &isa_mmio_writeb,
    &isa_mmio_writew_be,
    &isa_mmio_writel_be,
};

static CPUReadMemoryFunc * const isa_mmio_read_be[] = {
    &isa_mmio_readb,
    &isa_mmio_readw_be,
    &isa_mmio_readl_be,
};

static CPUWriteMemoryFunc * const isa_mmio_write_le[] = {
    &isa_mmio_writeb,
    &isa_mmio_writew_le,
    &isa_mmio_writel_le,
};

static CPUReadMemoryFunc * const isa_mmio_read_le[] = {
    &isa_mmio_readb,
    &isa_mmio_readw_le,
    &isa_mmio_readl_le,
};

static int isa_mmio_iomemtype = 0;

void isa_mmio_init(target_phys_addr_t base, target_phys_addr_t size, int be)
{
    if (!isa_mmio_iomemtype) {
        if (be) {
            isa_mmio_iomemtype = cpu_register_io_memory(isa_mmio_read_be,
                                                        isa_mmio_write_be,
                                                        NULL);
        } else {
            isa_mmio_iomemtype = cpu_register_io_memory(isa_mmio_read_le,
                                                        isa_mmio_write_le,
                                                        NULL);
        }
    }
    cpu_register_physical_memory(base, size, isa_mmio_iomemtype);
}
