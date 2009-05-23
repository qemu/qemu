/*
 * QEMU Uninorth PCI host (for all Mac99 and newer machines)
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
#include "ppc_mac.h"
#include "pci.h"

/* debug UniNorth */
//#define DEBUG_UNIN

#ifdef DEBUG_UNIN
#define UNIN_DPRINTF(fmt, ...)                                  \
    do { printf("UNIN: " fmt , ## __VA_ARGS__); } while (0)
#else
#define UNIN_DPRINTF(fmt, ...)
#endif

typedef target_phys_addr_t pci_addr_t;
#include "pci_host.h"

typedef PCIHostState UNINState;

static void pci_unin_main_config_writel (void *opaque, target_phys_addr_t addr,
                                         uint32_t val)
{
    UNINState *s = opaque;

    UNIN_DPRINTF("config_writel addr " TARGET_FMT_plx " val %x\n", addr, val);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif

    s->config_reg = val;
}

static uint32_t pci_unin_main_config_readl (void *opaque,
                                            target_phys_addr_t addr)
{
    UNINState *s = opaque;
    uint32_t val;

    val = s->config_reg;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    UNIN_DPRINTF("config_readl addr " TARGET_FMT_plx " val %x\n", addr, val);

    return val;
}

static CPUWriteMemoryFunc *pci_unin_main_config_write[] = {
    &pci_unin_main_config_writel,
    &pci_unin_main_config_writel,
    &pci_unin_main_config_writel,
};

static CPUReadMemoryFunc *pci_unin_main_config_read[] = {
    &pci_unin_main_config_readl,
    &pci_unin_main_config_readl,
    &pci_unin_main_config_readl,
};

static CPUWriteMemoryFunc *pci_unin_main_write[] = {
    &pci_host_data_writeb,
    &pci_host_data_writew,
    &pci_host_data_writel,
};

static CPUReadMemoryFunc *pci_unin_main_read[] = {
    &pci_host_data_readb,
    &pci_host_data_readw,
    &pci_host_data_readl,
};

static void pci_unin_config_writel (void *opaque, target_phys_addr_t addr,
                                    uint32_t val)
{
    UNINState *s = opaque;

    s->config_reg = val;
}

static uint32_t pci_unin_config_readl (void *opaque,
                                       target_phys_addr_t addr)
{
    UNINState *s = opaque;

    return s->config_reg;
}

static CPUWriteMemoryFunc *pci_unin_config_write[] = {
    &pci_unin_config_writel,
    &pci_unin_config_writel,
    &pci_unin_config_writel,
};

static CPUReadMemoryFunc *pci_unin_config_read[] = {
    &pci_unin_config_readl,
    &pci_unin_config_readl,
    &pci_unin_config_readl,
};

#if 0
static CPUWriteMemoryFunc *pci_unin_write[] = {
    &pci_host_pci_writeb,
    &pci_host_pci_writew,
    &pci_host_pci_writel,
};

static CPUReadMemoryFunc *pci_unin_read[] = {
    &pci_host_pci_readb,
    &pci_host_pci_readw,
    &pci_host_pci_readl,
};
#endif

/* Don't know if this matches real hardware, but it agrees with OHW.  */
static int pci_unin_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 3;
}

static void pci_unin_set_irq(qemu_irq *pic, int irq_num, int level)
{
    qemu_set_irq(pic[irq_num + 8], level);
}

static void pci_unin_save(QEMUFile* f, void *opaque)
{
    PCIDevice *d = opaque;

    pci_device_save(d, f);
}

static int pci_unin_load(QEMUFile* f, void *opaque, int version_id)
{
    PCIDevice *d = opaque;

    if (version_id != 1)
        return -EINVAL;

    return pci_device_load(d, f);
}

static void pci_unin_reset(void *opaque)
{
}

PCIBus *pci_pmac_init(qemu_irq *pic)
{
    UNINState *s;
    PCIDevice *d;
    int pci_mem_config, pci_mem_data;

    /* Use values found on a real PowerMac */
    /* Uninorth main bus */
    s = qemu_mallocz(sizeof(UNINState));
    s->bus = pci_register_bus(NULL, "pci",
                              pci_unin_set_irq, pci_unin_map_irq,
                              pic, 11 << 3, 4);

    pci_mem_config = cpu_register_io_memory(0, pci_unin_main_config_read,
                                            pci_unin_main_config_write, s);
    pci_mem_data = cpu_register_io_memory(0, pci_unin_main_read,
                                          pci_unin_main_write, s);
    cpu_register_physical_memory(0xf2800000, 0x1000, pci_mem_config);
    cpu_register_physical_memory(0xf2c00000, 0x1000, pci_mem_data);
    d = pci_register_device(s->bus, "Uni-north main", sizeof(PCIDevice),
                            11 << 3, NULL, NULL);
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_UNI_N_PCI);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type
    d->config[0x34] = 0x00; // capabilities_pointer

#if 0 // XXX: not activated as PPC BIOS doesn't handle multiple buses properly
    /* pci-to-pci bridge */
    d = pci_register_device("Uni-north bridge", sizeof(PCIDevice), 0, 13 << 3,
                            NULL, NULL);
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_DEC);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_DEC_21154);
    d->config[0x08] = 0x05; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_PCI);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x20; // latency_timer
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_BRIDGE; // header_type

    d->config[0x18] = 0x01; // primary_bus
    d->config[0x19] = 0x02; // secondary_bus
    d->config[0x1A] = 0x02; // subordinate_bus
    d->config[0x1B] = 0x20; // secondary_latency_timer
    d->config[0x1C] = 0x11; // io_base
    d->config[0x1D] = 0x01; // io_limit
    d->config[0x20] = 0x00; // memory_base
    d->config[0x21] = 0x80;
    d->config[0x22] = 0x00; // memory_limit
    d->config[0x23] = 0x80;
    d->config[0x24] = 0x01; // prefetchable_memory_base
    d->config[0x25] = 0x80;
    d->config[0x26] = 0xF1; // prefectchable_memory_limit
    d->config[0x27] = 0x7F;
    // d->config[0x34] = 0xdc // capabilities_pointer
#endif

    /* Uninorth AGP bus */
    pci_mem_config = cpu_register_io_memory(0, pci_unin_config_read,
                                            pci_unin_config_write, s);
    pci_mem_data = cpu_register_io_memory(0, pci_unin_main_read,
                                          pci_unin_main_write, s);
    cpu_register_physical_memory(0xf0800000, 0x1000, pci_mem_config);
    cpu_register_physical_memory(0xf0c00000, 0x1000, pci_mem_data);

    d = pci_register_device(s->bus, "Uni-north AGP", sizeof(PCIDevice),
                            11 << 3, NULL, NULL);
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_UNI_N_AGP);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type
    //    d->config[0x34] = 0x80; // capabilities_pointer

#if 0 // XXX: not needed for now
    /* Uninorth internal bus */
    s = &pci_bridge[2];
    pci_mem_config = cpu_register_io_memory(0, pci_unin_config_read,
                                            pci_unin_config_write, s);
    pci_mem_data = cpu_register_io_memory(0, pci_unin_read,
                                          pci_unin_write, s);
    cpu_register_physical_memory(0xf4800000, 0x1000, pci_mem_config);
    cpu_register_physical_memory(0xf4c00000, 0x1000, pci_mem_data);

    d = pci_register_device("Uni-north internal", sizeof(PCIDevice),
                            3, 11 << 3, NULL, NULL);
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_UNI_N_I_PCI);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type
    d->config[0x34] = 0x00; // capabilities_pointer
#endif
    register_savevm("uninorth", 0, 1, pci_unin_save, pci_unin_load, d);
    qemu_register_reset(pci_unin_reset, 0, d);
    pci_unin_reset(d);

    return s->bus;
}
