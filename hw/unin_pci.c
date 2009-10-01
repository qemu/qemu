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

typedef struct UNINState {
    SysBusDevice busdev;
    PCIHostState host_state;
} UNINState;

static void pci_unin_main_config_writel (void *opaque, target_phys_addr_t addr,
                                         uint32_t val)
{
    UNINState *s = opaque;

    UNIN_DPRINTF("config_writel addr " TARGET_FMT_plx " val %x\n", addr, val);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif

    s->host_state.config_reg = val;
}

static uint32_t pci_unin_main_config_readl (void *opaque,
                                            target_phys_addr_t addr)
{
    UNINState *s = opaque;
    uint32_t val;

    val = s->host_state.config_reg;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    UNIN_DPRINTF("config_readl addr " TARGET_FMT_plx " val %x\n", addr, val);

    return val;
}

static CPUWriteMemoryFunc * const pci_unin_main_config_write[] = {
    &pci_unin_main_config_writel,
    &pci_unin_main_config_writel,
    &pci_unin_main_config_writel,
};

static CPUReadMemoryFunc * const pci_unin_main_config_read[] = {
    &pci_unin_main_config_readl,
    &pci_unin_main_config_readl,
    &pci_unin_main_config_readl,
};

static CPUWriteMemoryFunc * const pci_unin_main_write[] = {
    &pci_host_data_writeb,
    &pci_host_data_writew,
    &pci_host_data_writel,
};

static CPUReadMemoryFunc * const pci_unin_main_read[] = {
    &pci_host_data_readb,
    &pci_host_data_readw,
    &pci_host_data_readl,
};

static void pci_unin_config_writel (void *opaque, target_phys_addr_t addr,
                                    uint32_t val)
{
    UNINState *s = opaque;

    s->host_state.config_reg = val;
}

static uint32_t pci_unin_config_readl (void *opaque,
                                       target_phys_addr_t addr)
{
    UNINState *s = opaque;

    return s->host_state.config_reg;
}

static CPUWriteMemoryFunc * const pci_unin_config_write[] = {
    &pci_unin_config_writel,
    &pci_unin_config_writel,
    &pci_unin_config_writel,
};

static CPUReadMemoryFunc * const pci_unin_config_read[] = {
    &pci_unin_config_readl,
    &pci_unin_config_readl,
    &pci_unin_config_readl,
};

static CPUWriteMemoryFunc * const pci_unin_write[] = {
    &pci_host_data_writeb,
    &pci_host_data_writew,
    &pci_host_data_writel,
};

static CPUReadMemoryFunc * const pci_unin_read[] = {
    &pci_host_data_readb,
    &pci_host_data_readw,
    &pci_host_data_readl,
};

/* Don't know if this matches real hardware, but it agrees with OHW.  */
static int pci_unin_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 3;
}

static void pci_unin_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

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

static int pci_unin_main_init_device(SysBusDevice *dev)
{
    UNINState *s;
    int pci_mem_config, pci_mem_data;

    /* Use values found on a real PowerMac */
    /* Uninorth main bus */
    s = FROM_SYSBUS(UNINState, dev);

    pci_mem_config = cpu_register_io_memory(pci_unin_main_config_read,
                                            pci_unin_main_config_write, s);
    pci_mem_data = cpu_register_io_memory(pci_unin_main_read,
                                          pci_unin_main_write, &s->host_state);

    sysbus_init_mmio(dev, 0x1000, pci_mem_config);
    sysbus_init_mmio(dev, 0x1000, pci_mem_data);

    register_savevm("uninorth", 0, 1, pci_unin_save, pci_unin_load, &s->host_state);
    qemu_register_reset(pci_unin_reset, &s->host_state);
    pci_unin_reset(&s->host_state);
    return 0;
}

static int pci_dec_21154_init_device(SysBusDevice *dev)
{
    UNINState *s;
    int pci_mem_config, pci_mem_data;

    /* Uninorth bridge */
    s = FROM_SYSBUS(UNINState, dev);

    // XXX: s = &pci_bridge[2];
    pci_mem_config = cpu_register_io_memory(pci_unin_config_read,
                                            pci_unin_config_write, s);
    pci_mem_data = cpu_register_io_memory(pci_unin_main_read,
                                          pci_unin_main_write, &s->host_state);
    sysbus_init_mmio(dev, 0x1000, pci_mem_config);
    sysbus_init_mmio(dev, 0x1000, pci_mem_data);
    return 0;
}

static int pci_unin_agp_init_device(SysBusDevice *dev)
{
    UNINState *s;
    int pci_mem_config, pci_mem_data;

    /* Uninorth AGP bus */
    s = FROM_SYSBUS(UNINState, dev);

    pci_mem_config = cpu_register_io_memory(pci_unin_config_read,
                                            pci_unin_config_write, s);
    pci_mem_data = cpu_register_io_memory(pci_unin_main_read,
                                          pci_unin_main_write, &s->host_state);
    sysbus_init_mmio(dev, 0x1000, pci_mem_config);
    sysbus_init_mmio(dev, 0x1000, pci_mem_data);
    return 0;
}

static int pci_unin_internal_init_device(SysBusDevice *dev)
{
    UNINState *s;
    int pci_mem_config, pci_mem_data;

    /* Uninorth internal bus */
    s = FROM_SYSBUS(UNINState, dev);

    pci_mem_config = cpu_register_io_memory(pci_unin_config_read,
                                            pci_unin_config_write, s);
    pci_mem_data = cpu_register_io_memory(pci_unin_read,
                                          pci_unin_write, s);
    sysbus_init_mmio(dev, 0x1000, pci_mem_config);
    sysbus_init_mmio(dev, 0x1000, pci_mem_data);
    return 0;
}

PCIBus *pci_pmac_init(qemu_irq *pic)
{
    DeviceState *dev;
    SysBusDevice *s;
    UNINState *d;

    /* Use values found on a real PowerMac */
    /* Uninorth main bus */
    dev = qdev_create(NULL, "Uni-north main");
    qdev_init(dev);
    s = sysbus_from_qdev(dev);
    d = FROM_SYSBUS(UNINState, s);
    d->host_state.bus = pci_register_bus(&d->busdev.qdev, "pci",
                                         pci_unin_set_irq, pci_unin_map_irq,
                                         pic, 11 << 3, 4);

    pci_create_simple(d->host_state.bus, 11 << 3, "Uni-north main");

    sysbus_mmio_map(s, 0, 0xf2800000);
    sysbus_mmio_map(s, 1, 0xf2c00000);

    /* DEC 21154 bridge */
#if 0
    /* XXX: not activated as PPC BIOS doesn't handle multiple buses properly */
    pci_create_simple(d->host_state.bus, 12 << 3, "DEC 21154");
#endif

    /* Uninorth AGP bus */
    pci_create_simple(d->host_state.bus, 13 << 3, "Uni-north AGP");

    /* Uninorth internal bus */
#if 0
    /* XXX: not needed for now */
    pci_create_simple(d->host_state.bus, 14 << 3, "Uni-north internal");
#endif

    return d->host_state.bus;
}

static int unin_main_pci_host_init(PCIDevice *d)
{
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_UNI_N_PCI);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type
    d->config[0x34] = 0x00; // capabilities_pointer
    return 0;
}

static int dec_21154_pci_host_init(PCIDevice *d)
{
    /* pci-to-pci bridge */
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
    return 0;
}

static int unin_agp_pci_host_init(PCIDevice *d)
{
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_UNI_N_AGP);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type
    //    d->config[0x34] = 0x80; // capabilities_pointer
    return 0;
}

static int unin_internal_pci_host_init(PCIDevice *d)
{
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_UNI_N_I_PCI);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type
    d->config[0x34] = 0x00; // capabilities_pointer
    return 0;
}

static PCIDeviceInfo unin_main_pci_host_info = {
    .qdev.name = "Uni-north main",
    .qdev.size = sizeof(PCIDevice),
    .init      = unin_main_pci_host_init,
};

static PCIDeviceInfo dec_21154_pci_host_info = {
    .qdev.name = "DEC 21154",
    .qdev.size = sizeof(PCIDevice),
    .init      = dec_21154_pci_host_init,
};

static PCIDeviceInfo unin_agp_pci_host_info = {
    .qdev.name = "Uni-north AGP",
    .qdev.size = sizeof(PCIDevice),
    .init      = unin_agp_pci_host_init,
};

static PCIDeviceInfo unin_internal_pci_host_info = {
    .qdev.name = "Uni-north internal",
    .qdev.size = sizeof(PCIDevice),
    .init      = unin_internal_pci_host_init,
};

static void unin_register_devices(void)
{
    sysbus_register_dev("Uni-north main", sizeof(UNINState),
                        pci_unin_main_init_device);
    pci_qdev_register(&unin_main_pci_host_info);
    sysbus_register_dev("DEC 21154", sizeof(UNINState),
                        pci_dec_21154_init_device);
    pci_qdev_register(&dec_21154_pci_host_info);
    sysbus_register_dev("Uni-north AGP", sizeof(UNINState),
                        pci_unin_agp_init_device);
    pci_qdev_register(&unin_agp_pci_host_info);
    sysbus_register_dev("Uni-north internal", sizeof(UNINState),
                        pci_unin_internal_init_device);
    pci_qdev_register(&unin_internal_pci_host_info);
}

device_init(unin_register_devices)
