/*
 * QEMU emulation for Texas Instruments TNETW1130 (ACX111) wireless.
 * 
 * Copyright (c) 2007 Stefan Weil
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Texas Instruments does not provide any datasheets.
 */

#include <assert.h>             /* assert */
#include "vl.h"

/*****************************************************************************
 *
 * Common declarations for all PCI devices.
 *
 ****************************************************************************/

#define PCI_VENDOR_ID           0x00    /* 16 bits */
#define PCI_DEVICE_ID           0x02    /* 16 bits */
#define PCI_COMMAND             0x04    /* 16 bits */
#define PCI_STATUS              0x06    /* 16 bits */

#define PCI_REVISION_ID         0x08    /* 8 bits  */
#define PCI_CLASS_CODE          0x0b    /* 8 bits */
#define PCI_SUBCLASS_CODE       0x0a    /* 8 bits */
#define PCI_HEADER_TYPE         0x0e    /* 8 bits */

#define PCI_BASE_ADDRESS_0      0x10    /* 32 bits */
#define PCI_BASE_ADDRESS_1      0x14    /* 32 bits */
#define PCI_BASE_ADDRESS_2      0x18    /* 32 bits */
#define PCI_BASE_ADDRESS_3      0x1c    /* 32 bits */
#define PCI_BASE_ADDRESS_4      0x20    /* 32 bits */
#define PCI_BASE_ADDRESS_5      0x24    /* 32 bits */

#define PCI_CONFIG_8(offset, value) \
    (pci_conf[offset] = (value))
#define PCI_CONFIG_16(offset, value) \
    (*(uint16_t *)&pci_conf[offset] = cpu_to_le16(value))
#define PCI_CONFIG_32(offset, value) \
    (*(uint32_t *)&pci_conf[offset] = cpu_to_le32(value))

#define KiB 1024

/*****************************************************************************
 *
 * Declarations for emulation options and debugging.
 *
 ****************************************************************************/

/* Debug TNETW1130 card. */
#define DEBUG_TNETW1130

#if defined(DEBUG_TNETW1130)
# define logout(fmt, args...) fprintf(stderr, "TNETW1130 %-24s" fmt, __func__, ##args)
#else
# define logout(fmt, args...) ((void)0)
#endif

#define missing(text)       assert(!"feature is missing in this emulation: " text)

/* Enable or disable logging categories. */
#define LOG_PHY         1
#define LOG_RX          1       /* receive messages */
#define LOG_TX          1       /* transmit messages */

#if defined(DEBUG_TNETW1130)
# define TRACE(condition, command) ((condition) ? (command) : (void)0)
#else
# define TRACE(condition, command) ((void)0)
#endif

#define TNETW1130_MEM_SIZE      (4 * KiB)
#define TNETW1130_IO_SIZE      (0 * KiB)

static int tnetw1130_instance = 0;
static const int tnetw1130_version = 20070211;

typedef struct {
    /* Variables for QEMU interface. */
    int io_memory;              /* handle for memory mapped I/O */
    PCIDevice *pci_dev;
    uint32_t region[2];         /* PCI region addresses */
    VLANClientState *vc;
    //~ eeprom_t *eeprom;

    uint8_t macaddr[6];
    //~ uint8_t mem[DP8381X_IO_SIZE];
    //~ uint8_t filter[1024];
    //~ uint32_t silicon_revision;
} tnetw1130_t;

typedef struct {
    PCIDevice dev;
    tnetw1130_t tnetw1130;
} pci_tnetw1130_t;

static const char *tnetw1130_regname(target_phys_addr_t addr)
{
  // !!! dummy
  return "";
}

static void tnetw1130_reset(tnetw1130_t * s)
{
  // !!! dummy
}

static uint8_t tnetw1130_readb(pci_tnetw1130_t * d, target_phys_addr_t addr)
{
  // !!! dummy
  return 0;
}

static uint16_t tnetw1130_readw(pci_tnetw1130_t * d, target_phys_addr_t addr)
{
  // !!! dummy
  return 0;
}

static uint32_t tnetw1130_readl(pci_tnetw1130_t * d, target_phys_addr_t addr)
{
  // !!! dummy
  return 0;
}

static void tnetw1130_writeb(pci_tnetw1130_t * d, target_phys_addr_t addr,
                           uint8_t val)
{
  // !!! dummy
}

static void tnetw1130_writew(pci_tnetw1130_t * d, target_phys_addr_t addr,
                           uint16_t val)
{
  // !!! dummy
}

static void tnetw1130_writel(pci_tnetw1130_t * d, target_phys_addr_t addr,
                           uint32_t val)
{
  // !!! dummy
}

/*****************************************************************************
 *
 * Port mapped I/O.
 *
 ****************************************************************************/

static uint32_t tnetw1130_ioport_readb(void *opaque, uint32_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    logout("addr=%s\n", tnetw1130_regname(addr));
    return tnetw1130_readb(d, addr);
}

static uint32_t tnetw1130_ioport_readw(void *opaque, uint32_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    logout("addr=%s\n", tnetw1130_regname(addr));
    return tnetw1130_readw(d, addr);
}

static uint32_t tnetw1130_ioport_readl(void *opaque, uint32_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    logout("addr=%s\n", tnetw1130_regname(addr));
    return tnetw1130_readl(d, addr);
}

static void tnetw1130_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    logout("addr=%s val=0x%02x\n", tnetw1130_regname(addr), val);
    tnetw1130_writeb(d, addr, val);
}

static void tnetw1130_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    logout("addr=%s val=0x%04x\n", tnetw1130_regname(addr), val);
    tnetw1130_writew(d, addr, val);
}

static void tnetw1130_ioport_writel(void *opaque, uint32_t addr, uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    logout("addr=%s val=0x%08x\n", tnetw1130_regname(addr), val);
    tnetw1130_writel(d, addr, val);
}

static void tnetw1130_io_map(PCIDevice * pci_dev, int region_num,
                           uint32_t addr, uint32_t size, int type)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) pci_dev;
    tnetw1130_t *s = &d->tnetw1130;

    logout("region %d, addr 0x%08x, size 0x%08x\n", region_num, addr, size);
    assert(region_num == 0);
    s->region[region_num] = addr;

    register_ioport_read(addr, size, 1, tnetw1130_ioport_readb, d);
    register_ioport_read(addr, size, 2, tnetw1130_ioport_readw, d);
    register_ioport_read(addr, size, 4, tnetw1130_ioport_readl, d);
    register_ioport_write(addr, size, 1, tnetw1130_ioport_writeb, d);
    register_ioport_write(addr, size, 2, tnetw1130_ioport_writew, d);
    register_ioport_write(addr, size, 4, tnetw1130_ioport_writel, d);
}

/*****************************************************************************
 *
 * Memory mapped I/O.
 *
 ****************************************************************************/

static uint32_t tnetw1130_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    logout("addr 0x%08x\n", addr);
    return tnetw1130_readb(d, addr);
}

static uint32_t tnetw1130_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    logout("addr 0x%08x\n", addr);
    return tnetw1130_readw(d, addr);
}

static uint32_t tnetw1130_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    logout("addr 0x%08x\n", addr);
    return tnetw1130_readl(d, addr);
}

static void tnetw1130_mmio_writeb(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    logout("addr 0x%08x\n", addr);
    tnetw1130_writeb(d, addr, val);
}

static void tnetw1130_mmio_writew(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    logout("addr 0x%08x\n", addr);
    tnetw1130_writew(d, addr, val);
}

static void tnetw1130_mmio_writel(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    logout("addr 0x%08x\n", addr);
    tnetw1130_writel(d, addr, val);
}

static void tnetw1130_mem_map(PCIDevice * pci_dev, int region_num,
                            uint32_t addr, uint32_t size, int type)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) pci_dev;
    tnetw1130_t *s = &d->tnetw1130;

    logout("region %d, addr 0x%08x, size 0x%08x\n", region_num, addr, size);
    assert(region_num == 1);
    s->region[region_num] = addr;

    cpu_register_physical_memory(addr, TNETW1130_MEM_SIZE, s->io_memory);
}

static CPUReadMemoryFunc *tnetw1130_mmio_read[] = {
    tnetw1130_mmio_readb,
    tnetw1130_mmio_readw,
    tnetw1130_mmio_readl
};

static CPUWriteMemoryFunc *tnetw1130_mmio_write[] = {
    tnetw1130_mmio_writeb,
    tnetw1130_mmio_writew,
    tnetw1130_mmio_writel
};

static int tnetw1130_load(QEMUFile * f, void *opaque, int version_id)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
#if 0
    tnetw1130_t *s = &d->tnetw1130;
#endif
    int result = 0;
    logout("\n");
    if (version_id == tnetw1130_version) {
        result = pci_device_load(&d->dev, f);
    } else {
        result = -EINVAL;
    }
    return result;
}

static void nic_reset(void *opaque)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    logout("%p\n", d);
}

static int tnetw1130_can_receive(void *opaque)
{
    tnetw1130_t *s = opaque;

    logout("\n");

    /* TODO: handle queued receive data. */
    return 0;
}

static void tnetw1130_save(QEMUFile * f, void *opaque)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
#if 0
    tnetw1130_t *s = &d->tnetw1130;
#endif
    logout("\n");
    pci_device_save(&d->dev, f);
    /* TODO: support different endianess */
    qemu_put_buffer(f, (uint8_t *) d, sizeof(*d));
}

static void tnetw1130_receive(void *opaque, const uint8_t * buf, int size)
{
}

static void tnetw1130_init(PCIBus * bus, NICInfo * nd)
{
    pci_tnetw1130_t *d;
    tnetw1130_t *s;
    uint8_t *pci_conf;

    d = (pci_tnetw1130_t *) pci_register_device(bus, "TNETW1130",
                                              sizeof(pci_tnetw1130_t),
                                              -1, NULL, NULL);
    pci_conf = d->dev.config;

    /* TI TNETW1130 */
    PCI_CONFIG_32(PCI_VENDOR_ID, 0x9066104c);
    PCI_CONFIG_32(PCI_COMMAND, 0x02100000);
    /* ethernet network controller */
    PCI_CONFIG_32(PCI_REVISION_ID, 0x02800000);
    PCI_CONFIG_32(0x28, 0x00001c02);
    PCI_CONFIG_32(0x28, 0x9067104c);
    /* Address registers are set by pci_register_io_region. */
    /* Capabilities Pointer, CLOFS */
    PCI_CONFIG_32(0x34, 0x00000040);
    /* 0x38 reserved, returns 0 */
    /* MNGNT = 11, MXLAT = 52, IPIN = 0 */
    PCI_CONFIG_32(0x3c, 0x00000100);
    /* Power Management Capabilities */
    PCI_CONFIG_32(0x40, 0x7e020001);
    /* Power Management Control and Status */
    //~ PCI_CONFIG_32(0x44, 0x00000000);
    /* 0x48...0xff reserved, returns 0 */

    s = &d->tnetw1130;

    /* Handler for memory-mapped I/O */
    s->io_memory =
        cpu_register_io_memory(0, tnetw1130_mmio_read, tnetw1130_mmio_write, d);

    logout("io_memory = 0x%08x\n", s->io_memory);

    pci_register_io_region(&d->dev, 0, TNETW1130_IO_SIZE,
                           PCI_ADDRESS_SPACE_IO, tnetw1130_io_map);
    pci_register_io_region(&d->dev, 1, TNETW1130_MEM_SIZE,
                           PCI_ADDRESS_SPACE_MEM, tnetw1130_mem_map);

    s->pci_dev = &d->dev;
    static const char macaddr[6] = {
        0x00, 0x60, 0x65, 0x02, 0x4a, 0x8e
    };
    memcpy(s->macaddr, macaddr, 6);
    //~ memcpy(s->macaddr, nd->macaddr, 6);
    tnetw1130_reset(s);

    s->vc = qemu_new_vlan_client(nd->vlan, tnetw1130_receive,
                                 tnetw1130_can_receive, s);

    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "tnetw1130 pci macaddr=%02x:%02x:%02x:%02x:%02x:%02x",
             nd->macaddr[0],
             nd->macaddr[1],
             nd->macaddr[2], nd->macaddr[3], nd->macaddr[4], nd->macaddr[5]);

    qemu_register_reset(nic_reset, d);

    register_savevm("tnetw1130", tnetw1130_instance, tnetw1130_version,
                    tnetw1130_save, tnetw1130_load, d);
}

void pci_tnetw1130_init(PCIBus * bus, NICInfo * nd, int devfn)
{
    logout("\n");
    tnetw1130_init(bus, nd);
}

/* eof */
