#include <linux/pci_regs.h>
#include "pci.h"

static void outl(unsigned short port, unsigned val)
{
    asm volatile("outl %0, %w1" : : "a"(val), "Nd"(port));
}

static unsigned inl(unsigned short port)
{
    unsigned data;
    asm volatile("inl %w1, %0" : "=a"(data) : "Nd"(port));
    return data;
}
static uint32_t pci_config_read(pcidevaddr_t dev, uint8_t reg)
{
    uint32_t index = reg | (dev << 8) | (0x1 << 31); 
    outl(0xCF8, index);
    return inl(0xCFC);
}

/* Scan bus look for a specific device. Only bus 0 scanned for now. */
pcidevaddr_t pci_find_dev(uint16_t vendor_id, uint16_t device_id)
{
    unsigned dev;
    for (dev = 0; dev < 256; ++dev) {
    uint32_t id = pci_config_read(dev, 0);
    if ((id & 0xFFFF) == vendor_id && (id >> 16) == device_id) {
        return dev;
    }
    }
    return PCIDEVADDR_INVALID;
}

unsigned long pci_bar_addr(pcidevaddr_t dev, int bar_num)
{
    uint32_t bar = pci_config_read(dev, PCI_BASE_ADDRESS_0 + bar_num * 4);
    if (bar & PCI_BASE_ADDRESS_SPACE_IO) {
        return bar & PCI_BASE_ADDRESS_IO_MASK;
    } else {
        return bar & PCI_BASE_ADDRESS_MEM_MASK;
    }
}

bool pci_bar_is_memory(pcidevaddr_t dev, int bar_num)
{
    uint32_t bar = pci_config_read(dev, PCI_BASE_ADDRESS_0 + bar_num * 4);
    return !(bar & PCI_BASE_ADDRESS_SPACE_IO);
}

bool pci_bar_is_valid(pcidevaddr_t dev, int bar_num)
{
    uint32_t bar = pci_config_read(dev, PCI_BASE_ADDRESS_0 + bar_num * 4);
    return bar;
}
