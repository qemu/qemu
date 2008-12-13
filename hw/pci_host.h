/*
 * QEMU Common PCI Host bridge configuration data space access routines.
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

/* Worker routines for a PCI host controller that uses an {address,data}
   register pair to access PCI configuration space.  */

typedef struct {
    uint32_t config_reg;
    PCIBus *bus;
} PCIHostState;

static void pci_host_data_writeb(void* opaque, pci_addr_t addr, uint32_t val)
{
    PCIHostState *s = opaque;
    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, 1);
}

static void pci_host_data_writew(void* opaque, pci_addr_t addr, uint32_t val)
{
    PCIHostState *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, 2);
}

static void pci_host_data_writel(void* opaque, pci_addr_t addr, uint32_t val)
{
    PCIHostState *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg, val, 4);
}

static uint32_t pci_host_data_readb(void* opaque, pci_addr_t addr)
{
    PCIHostState *s = opaque;
    if (!(s->config_reg & (1 << 31)))
        return 0xff;
    return pci_data_read(s->bus, s->config_reg | (addr & 3), 1);
}

static uint32_t pci_host_data_readw(void* opaque, pci_addr_t addr)
{
    PCIHostState *s = opaque;
    uint32_t val;
    if (!(s->config_reg & (1 << 31)))
        return 0xffff;
    val = pci_data_read(s->bus, s->config_reg | (addr & 3), 2);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    return val;
}

static uint32_t pci_host_data_readl(void* opaque, pci_addr_t addr)
{
    PCIHostState *s = opaque;
    uint32_t val;
    if (!(s->config_reg & (1 << 31)))
        return 0xffffffff;
    val = pci_data_read(s->bus, s->config_reg | (addr & 3), 4);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    return val;
}
