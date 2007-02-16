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
#define MISSING() logout("%s:%u missing, %s!!!\n", __FILE__, __LINE__, backtrace())
#define UNEXPECTED() logout("%s:%u unexpected, %s!!!\n", __FILE__, __LINE__, backtrace())
#define backtrace() ""

/* Enable or disable logging categories. */
#define LOG_PHY         1
#define LOG_RX          1       /* receive messages */
#define LOG_TX          1       /* transmit messages */

#if defined(DEBUG_TNETW1130)
# define TRACE(condition, command) ((condition) ? (command) : (void)0)
#else
# define TRACE(condition, command) ((void)0)
#endif

#define TNETW1130_MEM0_SIZE      (8 * KiB)
#define TNETW1130_MEM1_SIZE      (128 * KiB)
#define TNETW1130_FW_SIZE        (128 * KiB)
//~ #define TNETW1130_IO_SIZE      (0 * KiB)

/* Number of memory and I/O regions. */
#define  TNETW1130_REGIONS      2

static int tnetw1130_instance = 0;
static const int tnetw1130_version = 20070211;

typedef struct {
    /* Variables for QEMU interface. */

    /* handles for memory mapped I/O */
    int io_memory[TNETW1130_REGIONS];
    PCIDevice *pci_dev;

    /* PCI region addresses */
    uint32_t region[TNETW1130_REGIONS];

    VLANClientState *vc;
    //~ eeprom_t *eeprom;

    uint8_t macaddr[6];
    uint8_t mem0[TNETW1130_MEM0_SIZE];
    uint8_t mem1[TNETW1130_MEM1_SIZE];
    uint32_t fw_addr;
    uint8_t fw[TNETW1130_FW_SIZE];
    //~ uint8_t filter[1024];
    //~ uint32_t silicon_revision;
} tnetw1130_t;

typedef struct {
    PCIDevice dev;
    tnetw1130_t tnetw1130;
} pci_tnetw1130_t;

typedef enum {
    TNETW1130_SOFT_RESET = 0x0000,
    TNETW1130_SLV_MEM_ADDR = 0x0014,
    TNETW1130_SLV_MEM_DATA = 0x0018,
    TNETW1130_SLV_MEM_CTL = 0x001c,
    TNETW1130_SLV_END_CTL = 0x0020,
    TNETW1130_EE_START = 0x0100,
    TNETW1130_ECPU_CTRL = 0x0108,
    TNETW1130_EEPROM_INFORMATION = 0x390,
} tnetw1130_reg_t;

/*****************************************************************************
 *
 * Helper functions.
 *
 ****************************************************************************/

static uint16_t reg_read16(const uint8_t * reg, uint32_t addr)
{
    assert(!(addr & 1));
    return le16_to_cpu(*(uint16_t *) (&reg[addr]));
}

static void reg_write16(uint8_t * reg, uint32_t addr, uint16_t value)
{
    assert(!(addr & 1));
    *(uint16_t *) (&reg[addr]) = cpu_to_le16(value);
}

static uint32_t reg_read32(const uint8_t * reg, uint32_t addr)
{
    assert(!(addr & 3));
    return le32_to_cpu(*(uint32_t *) (&reg[addr]));
}

static void reg_write32(uint8_t * reg, uint32_t addr, uint32_t value)
{
    assert(!(addr & 3));
    *(uint32_t *) (&reg[addr]) = cpu_to_le32(value);
}

static const char *tnetw1130_regname(target_phys_addr_t addr)
{
    static char buffer[32];
    const char *name = buffer;
    sprintf(buffer, "0x%08x", addr);
    switch (addr) {
        case TNETW1130_SOFT_RESET:
            name = "SOFT_RESET";
            break;
        case TNETW1130_SLV_MEM_ADDR:
            name = "SLV_MEM_ADDR";
            break;
        case TNETW1130_SLV_MEM_DATA:
            name = "SLV_MEM_DATA";
            break;
        case TNETW1130_SLV_MEM_CTL:
            name = "SLV_MEM_CTL";
            break;
        case TNETW1130_SLV_END_CTL:
            name = "SLV_END_CTL";
            break;
        case 0x0034:
            name = "FEMR";
            break;
        case 0x00b4:
            name = "INT_TRIG";
            break;
        case 0x00d4:
            name = "IRQ_MASK";
            break;
        case 0x00e4:
            name = "IRQ_STATUS_CLEAR";
            break;
        case 0x00e8:
            name = "IRQ_ACK";
            break;
        case 0x00ec:
            name = "HINT_TRIG";
            break;
        /* we do mean NON_DES (0xf0), not NON_DES_MASK which is at 0xe0: */
        case 0x00f0:
            name = "IRQ_STATUS_NON_DES";
            break;
        case TNETW1130_EE_START:
            name = "EE_START";
            break;
        case 0x0104:
            name = "SOR_CFG";
            break;
        case TNETW1130_ECPU_CTRL:
            name = "ECPU_CTRL";
            break;
        case 0x01d0:
            name = "ENABLE";
            break;
        case 0x0338:
            name = "EEPROM_CTL";
            break;
        case 0x033c:
            name = "EEPROM_ADDR";
            break;
        case 0x0340:
            name = "EEPROM_DATA";
            break;
        case 0x0344:
            name = "EEPROM_CFG";
            break;
        case 0x0350:
            name = "PHY_ADDR";
            break;
        case 0x0354:
            name = "PHY_DATA";
            break;
        case 0x0358:
            name = "PHY_CTL";
            break;
        case 0x0374:
            name = "GPIO_OE";
            break;
        case 0x037c:
            name = "GPIO_OUT";
            break;
        case 0x0388:
            name = "CMD_MAILBOX_OFFS";
            break;
        case 0x038c:
            name = "INFO_MAILBOX_OFFS";
            break;
        case 0x0390:
            name = "EEPROM_INFORMATION";
            break;
    }

    return name;
}

static void tnetw1130_reset(tnetw1130_t * s)
{
  // !!! dummy
}

static uint8_t tnetw1130_read0b(pci_tnetw1130_t * d, target_phys_addr_t addr)
{
    tnetw1130_t *s = &d->tnetw1130;
    uint8_t value = 0;
    assert(addr < TNETW1130_MEM0_SIZE);
    value = s->mem0[addr];
    //~ } else if (addr -= 0x20000, addr == TNETW1130_SOFT_RESET) {
    logout("addr %s = %02x\n", tnetw1130_regname(addr), value);
    return value;
}

/* Radio type names, found in Win98 driver's TIACXLN.INF */
#define RADIO_MAXIM_0D		0x0d
#define RADIO_RFMD_11		0x11
#define RADIO_RALINK_15		0x15
/* used in ACX111 cards (WG311v2, WL-121, ...): */
#define RADIO_RADIA_16		0x16

static uint16_t tnetw1130_read0w(pci_tnetw1130_t * d, target_phys_addr_t addr)
{
    tnetw1130_t *s = &d->tnetw1130;
    uint16_t value = 0;
    assert(addr < TNETW1130_MEM0_SIZE);
    value = reg_read16(s->mem0, addr);
    //~ } else if (addr -= 0x20000, addr == TNETW1130_SOFT_RESET) {
    //~ } else if (addr == TNETW1130_EE_START) {
    //~ } else if (addr == TNETW1130_ECPU_CTRL) {
    if (addr == TNETW1130_EEPROM_INFORMATION) {
        value = (RADIO_RADIA_16 << 8) + 0x01;
    }
    logout("addr %s = %04x\n", tnetw1130_regname(addr), value);
    return value;
}

static uint32_t tnetw1130_read0l(pci_tnetw1130_t * d, target_phys_addr_t addr)
{
    tnetw1130_t *s = &d->tnetw1130;
    uint32_t value = 0;
    assert(addr < TNETW1130_MEM0_SIZE);
    value = reg_read32(s->mem0, addr);
    if (0) {
    } else if (addr == TNETW1130_SLV_MEM_DATA) {
        value = reg_read32(s->fw, s->fw_addr);
    }
    logout("addr %s = %08x\n", tnetw1130_regname(addr), value);
    return value;
}

static void tnetw1130_write0b(pci_tnetw1130_t * d, target_phys_addr_t addr,
                           uint8_t val)
{
    //~ tnetw1130_t *s = &d->tnetw1130;
}

static void tnetw1130_write0w(pci_tnetw1130_t * d, target_phys_addr_t addr,
                           uint16_t value)
{
    tnetw1130_t *s = &d->tnetw1130;
    assert(addr < TNETW1130_MEM0_SIZE);
    reg_write16(s->mem0, addr, value);
    if (addr == TNETW1130_SOFT_RESET) {
        if (value & 1) {
            logout("soft reset\n");
        }
    } else if (addr == TNETW1130_EE_START) {
        if (value & 1) {
            logout("start burst read from EEPROM\n");
        }
    } else if (addr == TNETW1130_ECPU_CTRL) {
        if (value & 1) {
            logout("halt eCPU\n");
            //~ reg_write16(s->mem0, addr, value & ~1);
        }
    }
    logout("addr %s = %04x\n", tnetw1130_regname(addr), value);
}

static void tnetw1130_write0l(pci_tnetw1130_t * d, target_phys_addr_t addr,
                           uint32_t value)
{
    tnetw1130_t *s = &d->tnetw1130;
    assert(addr < TNETW1130_MEM0_SIZE);
    reg_write32(s->mem0, addr, value);
    if (addr == TNETW1130_SLV_MEM_ADDR) {
        s->fw_addr = value;
        assert(value < TNETW1130_FW_SIZE);
    } else if (addr == TNETW1130_SLV_MEM_DATA) {
        reg_write32(s->fw, s->fw_addr, value);
    } else if (addr == TNETW1130_SLV_MEM_CTL) {
        if (value == 0) {
            logout("basic mode\n");
        } else if (value == 1) {
            logout("autoincrement mode\n");
            MISSING();
        } else {
            UNEXPECTED();
        }
    } else if (addr == TNETW1130_SLV_END_CTL) {
    }
    logout("addr %s = %08x\n", tnetw1130_regname(addr), value);
}

static uint8_t tnetw1130_read1b(pci_tnetw1130_t * d, target_phys_addr_t addr)
{
    tnetw1130_t *s = &d->tnetw1130;
    uint8_t value = 0;
    assert(addr < TNETW1130_MEM1_SIZE);
    value = s->mem1[addr];
    logout("addr %s = %02x\n", tnetw1130_regname(addr), value);
    return value;
}

static uint16_t tnetw1130_read1w(pci_tnetw1130_t * d, target_phys_addr_t addr)
{
    tnetw1130_t *s = &d->tnetw1130;
    uint16_t value = 0;
    assert(addr < TNETW1130_MEM1_SIZE);
    value = reg_read16(s->mem1, addr);
    logout("addr %s = %04x\n", tnetw1130_regname(addr), value);
    return value;
}

static uint32_t tnetw1130_read1l(pci_tnetw1130_t * d, target_phys_addr_t addr)
{
    //~ tnetw1130_t *s = &d->tnetw1130;
    return 0;
}

static void tnetw1130_write1b(pci_tnetw1130_t * d, target_phys_addr_t addr,
                           uint8_t val)
{
    //~ tnetw1130_t *s = &d->tnetw1130;
}

static void tnetw1130_write1w(pci_tnetw1130_t * d, target_phys_addr_t addr,
                           uint16_t value)
{
    tnetw1130_t *s = &d->tnetw1130;
    assert(addr < TNETW1130_MEM1_SIZE);
    reg_write16(s->mem1, addr, value);
    logout("addr %s = %04x\n", tnetw1130_regname(addr), value);
}

static void tnetw1130_write1l(pci_tnetw1130_t * d, target_phys_addr_t addr,
                           uint32_t val)
{
    //~ tnetw1130_t *s = &d->tnetw1130;
}

/*****************************************************************************
 *
 * Port mapped I/O.
 *
 ****************************************************************************/

#if 0 // no port mapped i/o

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

#endif

/*****************************************************************************
 *
 * Memory mapped I/O.
 *
 ****************************************************************************/

static uint32_t tnetw1130_mem0_readb(void *opaque, target_phys_addr_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    return tnetw1130_read0b(d, addr);
}

static uint32_t tnetw1130_mem0_readw(void *opaque, target_phys_addr_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    return tnetw1130_read0w(d, addr);
}

static uint32_t tnetw1130_mem0_readl(void *opaque, target_phys_addr_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    return tnetw1130_read0l(d, addr);
}

static void tnetw1130_mem0_writeb(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    logout("addr %s\n", tnetw1130_regname(addr));
    tnetw1130_write0b(d, addr, val);
}

static void tnetw1130_mem0_writew(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    tnetw1130_write0w(d, addr, val);
}

static void tnetw1130_mem0_writel(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[0];
    tnetw1130_write0l(d, addr, val);
}

static CPUReadMemoryFunc *tnetw1130_region0_read[] = {
    tnetw1130_mem0_readb,
    tnetw1130_mem0_readw,
    tnetw1130_mem0_readl
};

static CPUWriteMemoryFunc *tnetw1130_region0_write[] = {
    tnetw1130_mem0_writeb,
    tnetw1130_mem0_writew,
    tnetw1130_mem0_writel
};

static uint32_t tnetw1130_mem1_readb(void *opaque, target_phys_addr_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    return tnetw1130_read1b(d, addr);
}

static uint32_t tnetw1130_mem1_readw(void *opaque, target_phys_addr_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    return tnetw1130_read1w(d, addr);
}

static uint32_t tnetw1130_mem1_readl(void *opaque, target_phys_addr_t addr)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    logout("addr %s\n", tnetw1130_regname(addr));
    return tnetw1130_read1l(d, addr);
}

static void tnetw1130_mem1_writeb(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    logout("addr %s\n", tnetw1130_regname(addr));
    tnetw1130_write1b(d, addr, val);
}

static void tnetw1130_mem1_writew(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    tnetw1130_write1w(d, addr, val);
}

static void tnetw1130_mem1_writel(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) opaque;
    tnetw1130_t *s = &d->tnetw1130;
    addr -= s->region[1];
    logout("addr %s\n", tnetw1130_regname(addr));
    tnetw1130_write1l(d, addr, val);
}

static void tnetw1130_mem_map(PCIDevice * pci_dev, int region_num,
                            uint32_t addr, uint32_t size, int type)
{
    pci_tnetw1130_t *d = (pci_tnetw1130_t *) pci_dev;
    tnetw1130_t *s = &d->tnetw1130;

    logout("region %d, addr 0x%08x, size 0x%08x\n", region_num, addr, size);
    assert((unsigned)region_num < TNETW1130_REGIONS);
    s->region[region_num] = addr;

    cpu_register_physical_memory(addr, size, s->io_memory[region_num]);
}

static CPUReadMemoryFunc *tnetw1130_region1_read[] = {
    tnetw1130_mem1_readb,
    tnetw1130_mem1_readw,
    tnetw1130_mem1_readl
};

static CPUWriteMemoryFunc *tnetw1130_region1_write[] = {
    tnetw1130_mem1_writeb,
    tnetw1130_mem1_writew,
    tnetw1130_mem1_writel
};

/*****************************************************************************
 *
 * Other functions.
 *
 ****************************************************************************/

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
    //~ tnetw1130_t *s = opaque;

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

//~ lspci -x: 00: 4c 10 66 90 07 01 10 02 00 00 80 02 04 40 00 00
//~ lspci -x: 10: 00 c0 af fe 00 00 ac fe 00 00 00 00 00 00 00 00
//~ lspci -x: 20: 00 00 00 00 00 00 00 00 02 1c 00 00 86 11 04 3b
//~ lspci -x: 30: 00 00 00 00 40 00 00 00 00 00 00 00 0a 01 00 00

    /* TI TNETW1130 */
    PCI_CONFIG_32(PCI_VENDOR_ID, 0x9066104c);
    PCI_CONFIG_32(PCI_COMMAND, 0x02100000);
    /* ethernet network controller */
    PCI_CONFIG_32(PCI_REVISION_ID, 0x02800000);
    //~ PCI_CONFIG_32(PCI_BASE_ADDRESS_0,
                  //~ PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_MEM_PREFETCH);
    //~ PCI_CONFIG_32(PCI_BASE_ADDRESS_1,
                  //~ PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_MEM_PREFETCH);
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
    s->io_memory[0] =
        cpu_register_io_memory(0, tnetw1130_region0_read, tnetw1130_region0_write, d);
    s->io_memory[1] =
        cpu_register_io_memory(0, tnetw1130_region1_read, tnetw1130_region1_write, d);

    logout("io_memory = 0x%08x, 0x%08x\n", s->io_memory[0], s->io_memory[1]);

    pci_register_io_region(&d->dev, 0, TNETW1130_MEM0_SIZE,
                           PCI_ADDRESS_SPACE_MEM, tnetw1130_mem_map);
    pci_register_io_region(&d->dev, 1, TNETW1130_MEM1_SIZE,
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

/*
00:0a.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
	Subsystem: Abocom Systems Inc: Unknown device ab90
	Flags: bus master, medium devsel, latency 32, IRQ 10
	Memory at dffdc000 (32-bit, non-prefetchable) [size=8K]
	Memory at dffa0000 (32-bit, non-prefetchable) [size=128K]
	Capabilities: [40] Power Management version 2

04:08.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: Texas Instruments Unknown device 9067
        Flags: medium devsel, IRQ 50
        Memory at faafe000 (32-bit, non-prefetchable) [size=8K]
        Memory at faac0000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2

01:08.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: Netgear Unknown device 4c00
        Control: I/O+ Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B-
        Status: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR-
        Latency: 32, Cache Line Size: 32 bytes
        Interrupt: pin A routed to IRQ 201
        Region 0: Memory at eb020000 (32-bit, non-prefetchable) [size=8K]
        Region 1: Memory at eb000000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2
                Flags: PMEClk- DSI- D1+ D2+ AuxCurrent=0mA PME(D0+,D1+,D2+,D3hot+,D3cold-)
                Status: D0 PME-Enable- DSel=0 DScale=0 PME-

00:09.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: D-Link System Inc: Unknown device 3b04
        Flags: medium devsel, IRQ 11
        Memory at de020000 (32-bit, non-prefetchable) [size=8K]
        Memory at de000000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2

0000:02:08.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: Abocom Systems Inc: Unknown device ab90
        Flags: bus master, medium devsel, latency 32, IRQ 10
        Memory at f8140000 (32-bit, non-prefetchable) [size=8K]
        Memory at f8100000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2

02:0d.0 Network controller: Texas Instruments ACX 111 54Mbps Wireless Interface
        Subsystem: D-Link System Inc Unknown device 3b04
        Flags: bus master, medium devsel, latency 64, IRQ 10
        Memory at feafc000 (32-bit, non-prefetchable) [size=8K]
        Memory at feac0000 (32-bit, non-prefetchable) [size=128K]
        Capabilities: [40] Power Management version 2 

TNETW1130 tnetw1130_mem_map       region 0, addr 0x12020000, size 0x00002000
QEMU    cpu_register_physical_memory 0x12020000...0x12022000, size 0x00002000, phys_offset 0x000000b0
TNETW1130 tnetw1130_mem_map       region 1, addr 0x12000000, size 0x00020000
QEMU    cpu_register_physical_memory 0x12000000...0x12020000, size 0x00020000, phys_offset 0x000000b0

TNETW1130 tnetw1130_mem0_readw    addr ECPU_CTRL
TNETW1130 tnetw1130_mem0_writew   addr ECPU_CTRL
TNETW1130 tnetw1130_mem0_readw    addr SOFT_RESET
TNETW1130 tnetw1130_mem0_writew   addr SOFT_RESET
TNETW1130 tnetw1130_mem0_readb    addr SOFT_RESET
TNETW1130 tnetw1130_mem0_writew   addr SOFT_RESET
TNETW1130 tnetw1130_mem0_readw    addr EE_START
TNETW1130 tnetw1130_mem0_writew   addr EE_START
TNETW1130 tnetw1130_mem0_readb    addr SOFT_RESET
TNETW1130 tnetw1130_mem0_readw    addr ECPU_CTRL

*/

/* eof */
