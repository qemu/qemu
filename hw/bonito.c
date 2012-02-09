/*
 * bonito north bridge support
 *
 * Copyright (c) 2008 yajin (yajin@vm-kernel.org)
 * Copyright (c) 2010 Huacai Chen (zltjiangshi@gmail.com)
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

/*
 * fulong 2e mini pc has a bonito north bridge.
 */

/* what is the meaning of devfn in qemu and IDSEL in bonito northbridge?
 *
 * devfn   pci_slot<<3  + funno
 * one pci bus can have 32 devices and each device can have 8 functions.
 *
 * In bonito north bridge, pci slot = IDSEL bit - 12.
 * For example, PCI_IDSEL_VIA686B = 17,
 * pci slot = 17-12=5
 *
 * so
 * VT686B_FUN0's devfn = (5<<3)+0
 * VT686B_FUN1's devfn = (5<<3)+1
 *
 * qemu also uses pci address for north bridge to access pci config register.
 * bus_no   [23:16]
 * dev_no   [15:11]
 * fun_no   [10:8]
 * reg_no   [7:2]
 *
 * so function bonito_sbridge_pciaddr for the translation from
 * north bridge address to pci address.
 */

#include <assert.h>

#include "hw.h"
#include "pci.h"
#include "pc.h"
#include "mips.h"
#include "pci_host.h"
#include "sysemu.h"
#include "exec-memory.h"

//#define DEBUG_BONITO

#ifdef DEBUG_BONITO
#define DPRINTF(fmt, ...) fprintf(stderr, "%s: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#define DPRINTF(fmt, ...)
#endif

/* from linux soure code. include/asm-mips/mips-boards/bonito64.h*/
#define BONITO_BOOT_BASE        0x1fc00000
#define BONITO_BOOT_SIZE        0x00100000
#define BONITO_BOOT_TOP         (BONITO_BOOT_BASE+BONITO_BOOT_SIZE-1)
#define BONITO_FLASH_BASE       0x1c000000
#define BONITO_FLASH_SIZE       0x03000000
#define BONITO_FLASH_TOP        (BONITO_FLASH_BASE+BONITO_FLASH_SIZE-1)
#define BONITO_SOCKET_BASE      0x1f800000
#define BONITO_SOCKET_SIZE      0x00400000
#define BONITO_SOCKET_TOP       (BONITO_SOCKET_BASE+BONITO_SOCKET_SIZE-1)
#define BONITO_REG_BASE         0x1fe00000
#define BONITO_REG_SIZE         0x00040000
#define BONITO_REG_TOP          (BONITO_REG_BASE+BONITO_REG_SIZE-1)
#define BONITO_DEV_BASE         0x1ff00000
#define BONITO_DEV_SIZE         0x00100000
#define BONITO_DEV_TOP          (BONITO_DEV_BASE+BONITO_DEV_SIZE-1)
#define BONITO_PCILO_BASE       0x10000000
#define BONITO_PCILO_BASE_VA    0xb0000000
#define BONITO_PCILO_SIZE       0x0c000000
#define BONITO_PCILO_TOP        (BONITO_PCILO_BASE+BONITO_PCILO_SIZE-1)
#define BONITO_PCILO0_BASE      0x10000000
#define BONITO_PCILO1_BASE      0x14000000
#define BONITO_PCILO2_BASE      0x18000000
#define BONITO_PCIHI_BASE       0x20000000
#define BONITO_PCIHI_SIZE       0x20000000
#define BONITO_PCIHI_TOP        (BONITO_PCIHI_BASE+BONITO_PCIHI_SIZE-1)
#define BONITO_PCIIO_BASE       0x1fd00000
#define BONITO_PCIIO_BASE_VA    0xbfd00000
#define BONITO_PCIIO_SIZE       0x00010000
#define BONITO_PCIIO_TOP        (BONITO_PCIIO_BASE+BONITO_PCIIO_SIZE-1)
#define BONITO_PCICFG_BASE      0x1fe80000
#define BONITO_PCICFG_SIZE      0x00080000
#define BONITO_PCICFG_TOP       (BONITO_PCICFG_BASE+BONITO_PCICFG_SIZE-1)


#define BONITO_PCICONFIGBASE    0x00
#define BONITO_REGBASE          0x100

#define BONITO_PCICONFIG_BASE   (BONITO_PCICONFIGBASE+BONITO_REG_BASE)
#define BONITO_PCICONFIG_SIZE   (0x100)

#define BONITO_INTERNAL_REG_BASE  (BONITO_REGBASE+BONITO_REG_BASE)
#define BONITO_INTERNAL_REG_SIZE  (0x70)

#define BONITO_SPCICONFIG_BASE  (BONITO_PCICFG_BASE)
#define BONITO_SPCICONFIG_SIZE  (BONITO_PCICFG_SIZE)



/* 1. Bonito h/w Configuration */
/* Power on register */

#define BONITO_BONPONCFG        (0x00 >> 2)      /* 0x100 */
#define BONITO_BONGENCFG_OFFSET 0x4
#define BONITO_BONGENCFG        (BONITO_BONGENCFG_OFFSET>>2)   /*0x104 */

/* 2. IO & IDE configuration */
#define BONITO_IODEVCFG         (0x08 >> 2)      /* 0x108 */

/* 3. IO & IDE configuration */
#define BONITO_SDCFG            (0x0c >> 2)      /* 0x10c */

/* 4. PCI address map control */
#define BONITO_PCIMAP           (0x10 >> 2)      /* 0x110 */
#define BONITO_PCIMEMBASECFG    (0x14 >> 2)      /* 0x114 */
#define BONITO_PCIMAP_CFG       (0x18 >> 2)      /* 0x118 */

/* 5. ICU & GPIO regs */
/* GPIO Regs - r/w */
#define BONITO_GPIODATA_OFFSET  0x1c
#define BONITO_GPIODATA         (BONITO_GPIODATA_OFFSET >> 2)   /* 0x11c */
#define BONITO_GPIOIE           (0x20 >> 2)      /* 0x120 */

/* ICU Configuration Regs - r/w */
#define BONITO_INTEDGE          (0x24 >> 2)      /* 0x124 */
#define BONITO_INTSTEER         (0x28 >> 2)      /* 0x128 */
#define BONITO_INTPOL           (0x2c >> 2)      /* 0x12c */

/* ICU Enable Regs - IntEn & IntISR are r/o. */
#define BONITO_INTENSET         (0x30 >> 2)      /* 0x130 */
#define BONITO_INTENCLR         (0x34 >> 2)      /* 0x134 */
#define BONITO_INTEN            (0x38 >> 2)      /* 0x138 */
#define BONITO_INTISR           (0x3c >> 2)      /* 0x13c */

/* PCI mail boxes */
#define BONITO_PCIMAIL0_OFFSET    0x40
#define BONITO_PCIMAIL1_OFFSET    0x44
#define BONITO_PCIMAIL2_OFFSET    0x48
#define BONITO_PCIMAIL3_OFFSET    0x4c
#define BONITO_PCIMAIL0         (0x40 >> 2)      /* 0x140 */
#define BONITO_PCIMAIL1         (0x44 >> 2)      /* 0x144 */
#define BONITO_PCIMAIL2         (0x48 >> 2)      /* 0x148 */
#define BONITO_PCIMAIL3         (0x4c >> 2)      /* 0x14c */

/* 6. PCI cache */
#define BONITO_PCICACHECTRL     (0x50 >> 2)      /* 0x150 */
#define BONITO_PCICACHETAG      (0x54 >> 2)      /* 0x154 */
#define BONITO_PCIBADADDR       (0x58 >> 2)      /* 0x158 */
#define BONITO_PCIMSTAT         (0x5c >> 2)      /* 0x15c */

/* 7. other*/
#define BONITO_TIMECFG          (0x60 >> 2)      /* 0x160 */
#define BONITO_CPUCFG           (0x64 >> 2)      /* 0x164 */
#define BONITO_DQCFG            (0x68 >> 2)      /* 0x168 */
#define BONITO_MEMSIZE          (0x6C >> 2)      /* 0x16c */

#define BONITO_REGS             (0x70 >> 2)

/* PCI config for south bridge. type 0 */
#define BONITO_PCICONF_IDSEL_MASK      0xfffff800     /* [31:11] */
#define BONITO_PCICONF_IDSEL_OFFSET    11
#define BONITO_PCICONF_FUN_MASK        0x700    /* [10:8] */
#define BONITO_PCICONF_FUN_OFFSET      8
#define BONITO_PCICONF_REG_MASK        0xFC
#define BONITO_PCICONF_REG_OFFSET      0


/* idsel BIT = pci slot number +12 */
#define PCI_SLOT_BASE              12
#define PCI_IDSEL_VIA686B_BIT      (17)
#define PCI_IDSEL_VIA686B          (1<<PCI_IDSEL_VIA686B_BIT)

#define PCI_ADDR(busno,devno,funno,regno)  \
    ((((busno)<<16)&0xff0000) + (((devno)<<11)&0xf800) + (((funno)<<8)&0x700) + (regno))

typedef PCIHostState BonitoState;

typedef struct PCIBonitoState
{
    PCIDevice dev;
    BonitoState *pcihost;
    uint32_t regs[BONITO_REGS];

    struct bonldma {
        uint32_t ldmactrl;
        uint32_t ldmastat;
        uint32_t ldmaaddr;
        uint32_t ldmago;
    } bonldma;

    /* Based at 1fe00300, bonito Copier */
    struct boncop {
        uint32_t copctrl;
        uint32_t copstat;
        uint32_t coppaddr;
        uint32_t copgo;
    } boncop;

    /* Bonito registers */
    MemoryRegion iomem;
    MemoryRegion iomem_ldma;
    MemoryRegion iomem_cop;

    target_phys_addr_t bonito_pciio_start;
    target_phys_addr_t bonito_pciio_length;
    int bonito_pciio_handle;

    target_phys_addr_t bonito_localio_start;
    target_phys_addr_t bonito_localio_length;
    int bonito_localio_handle;

} PCIBonitoState;

PCIBonitoState * bonito_state;

static void bonito_writel(void *opaque, target_phys_addr_t addr,
                          uint64_t val, unsigned size)
{
    PCIBonitoState *s = opaque;
    uint32_t saddr;
    int reset = 0;

    saddr = (addr - BONITO_REGBASE) >> 2;

    DPRINTF("bonito_writel "TARGET_FMT_plx" val %x saddr %x\n", addr, val, saddr);
    switch (saddr) {
    case BONITO_BONPONCFG:
    case BONITO_IODEVCFG:
    case BONITO_SDCFG:
    case BONITO_PCIMAP:
    case BONITO_PCIMEMBASECFG:
    case BONITO_PCIMAP_CFG:
    case BONITO_GPIODATA:
    case BONITO_GPIOIE:
    case BONITO_INTEDGE:
    case BONITO_INTSTEER:
    case BONITO_INTPOL:
    case BONITO_PCIMAIL0:
    case BONITO_PCIMAIL1:
    case BONITO_PCIMAIL2:
    case BONITO_PCIMAIL3:
    case BONITO_PCICACHECTRL:
    case BONITO_PCICACHETAG:
    case BONITO_PCIBADADDR:
    case BONITO_PCIMSTAT:
    case BONITO_TIMECFG:
    case BONITO_CPUCFG:
    case BONITO_DQCFG:
    case BONITO_MEMSIZE:
        s->regs[saddr] = val;
        break;
    case BONITO_BONGENCFG:
        if (!(s->regs[saddr] & 0x04) && (val & 0x04)) {
            reset = 1; /* bit 2 jump from 0 to 1 cause reset */
        }
        s->regs[saddr] = val;
        if (reset) {
            qemu_system_reset_request();
        }
        break;
    case BONITO_INTENSET:
        s->regs[BONITO_INTENSET] = val;
        s->regs[BONITO_INTEN] |= val;
        break;
    case BONITO_INTENCLR:
        s->regs[BONITO_INTENCLR] = val;
        s->regs[BONITO_INTEN] &= ~val;
        break;
    case BONITO_INTEN:
    case BONITO_INTISR:
        DPRINTF("write to readonly bonito register %x\n", saddr);
        break;
    default:
        DPRINTF("write to unknown bonito register %x\n", saddr);
        break;
    }
}

static uint64_t bonito_readl(void *opaque, target_phys_addr_t addr,
                             unsigned size)
{
    PCIBonitoState *s = opaque;
    uint32_t saddr;

    saddr = (addr - BONITO_REGBASE) >> 2;

    DPRINTF("bonito_readl "TARGET_FMT_plx"\n", addr);
    switch (saddr) {
    case BONITO_INTISR:
        return s->regs[saddr];
    default:
        return s->regs[saddr];
    }
}

static const MemoryRegionOps bonito_ops = {
    .read = bonito_readl,
    .write = bonito_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void bonito_pciconf_writel(void *opaque, target_phys_addr_t addr,
                                  uint64_t val, unsigned size)
{
    PCIBonitoState *s = opaque;

    DPRINTF("bonito_pciconf_writel "TARGET_FMT_plx" val %x\n", addr, val);
    s->dev.config_write(&s->dev, addr, val, 4);
}

static uint64_t bonito_pciconf_readl(void *opaque, target_phys_addr_t addr,
                                     unsigned size)
{

    PCIBonitoState *s = opaque;

    DPRINTF("bonito_pciconf_readl "TARGET_FMT_plx"\n", addr);
    return s->dev.config_read(&s->dev, addr, 4);
}

/* north bridge PCI configure space. 0x1fe0 0000 - 0x1fe0 00ff */

static const MemoryRegionOps bonito_pciconf_ops = {
    .read = bonito_pciconf_readl,
    .write = bonito_pciconf_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t bonito_ldma_readl(void *opaque, target_phys_addr_t addr,
                                  unsigned size)
{
    uint32_t val;
    PCIBonitoState *s = opaque;

    val = ((uint32_t *)(&s->bonldma))[addr/sizeof(uint32_t)];

    return val;
}

static void bonito_ldma_writel(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned size)
{
    PCIBonitoState *s = opaque;

    ((uint32_t *)(&s->bonldma))[addr/sizeof(uint32_t)] = val & 0xffffffff;
}

static const MemoryRegionOps bonito_ldma_ops = {
    .read = bonito_ldma_readl,
    .write = bonito_ldma_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t bonito_cop_readl(void *opaque, target_phys_addr_t addr,
                                 unsigned size)
{
    uint32_t val;
    PCIBonitoState *s = opaque;

    val = ((uint32_t *)(&s->boncop))[addr/sizeof(uint32_t)];

    return val;
}

static void bonito_cop_writel(void *opaque, target_phys_addr_t addr,
                              uint64_t val, unsigned size)
{
    PCIBonitoState *s = opaque;

    ((uint32_t *)(&s->boncop))[addr/sizeof(uint32_t)] = val & 0xffffffff;
}

static const MemoryRegionOps bonito_cop_ops = {
    .read = bonito_cop_readl,
    .write = bonito_cop_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint32_t bonito_sbridge_pciaddr(void *opaque, target_phys_addr_t addr)
{
    PCIBonitoState *s = opaque;
    uint32_t cfgaddr;
    uint32_t idsel;
    uint32_t devno;
    uint32_t funno;
    uint32_t regno;
    uint32_t pciaddr;

    /* support type0 pci config */
    if ((s->regs[BONITO_PCIMAP_CFG] & 0x10000) != 0x0) {
        return 0xffffffff;
    }

    cfgaddr = addr & 0xffff;
    cfgaddr |= (s->regs[BONITO_PCIMAP_CFG] & 0xffff) << 16;

    idsel = (cfgaddr & BONITO_PCICONF_IDSEL_MASK) >> BONITO_PCICONF_IDSEL_OFFSET;
    devno = ffs(idsel) - 1;
    funno = (cfgaddr & BONITO_PCICONF_FUN_MASK) >> BONITO_PCICONF_FUN_OFFSET;
    regno = (cfgaddr & BONITO_PCICONF_REG_MASK) >> BONITO_PCICONF_REG_OFFSET;

    if (idsel == 0) {
        fprintf(stderr, "error in bonito pci config address" TARGET_FMT_plx
            ",pcimap_cfg=%x\n", addr, s->regs[BONITO_PCIMAP_CFG]);
        exit(1);
    }
    pciaddr = PCI_ADDR(pci_bus_num(s->pcihost->bus), devno, funno, regno);
    DPRINTF("cfgaddr %x pciaddr %x busno %x devno %d funno %d regno %d\n",
        cfgaddr, pciaddr, pci_bus_num(s->pcihost->bus), devno, funno, regno);

    return pciaddr;
}

static void bonito_spciconf_writeb(void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
    PCIBonitoState *s = opaque;
    uint32_t pciaddr;
    uint16_t status;

    DPRINTF("bonito_spciconf_writeb "TARGET_FMT_plx" val %x\n", addr, val);
    pciaddr = bonito_sbridge_pciaddr(s, addr);

    if (pciaddr == 0xffffffff) {
        return;
    }

    /* set the pci address in s->config_reg */
    s->pcihost->config_reg = (pciaddr) | (1u << 31);
    pci_data_write(s->pcihost->bus, s->pcihost->config_reg, val & 0xff, 1);

    /* clear PCI_STATUS_REC_MASTER_ABORT and PCI_STATUS_REC_TARGET_ABORT */
    status = pci_get_word(s->dev.config + PCI_STATUS);
    status &= ~(PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT);
    pci_set_word(s->dev.config + PCI_STATUS, status);
}

static void bonito_spciconf_writew(void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
    PCIBonitoState *s = opaque;
    uint32_t pciaddr;
    uint16_t status;

    DPRINTF("bonito_spciconf_writew "TARGET_FMT_plx" val %x\n", addr, val);
    assert((addr&0x1)==0);

    pciaddr = bonito_sbridge_pciaddr(s, addr);

    if (pciaddr == 0xffffffff) {
        return;
    }

    /* set the pci address in s->config_reg */
    s->pcihost->config_reg = (pciaddr) | (1u << 31);
    pci_data_write(s->pcihost->bus, s->pcihost->config_reg, val, 2);

    /* clear PCI_STATUS_REC_MASTER_ABORT and PCI_STATUS_REC_TARGET_ABORT */
    status = pci_get_word(s->dev.config + PCI_STATUS);
    status &= ~(PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT);
    pci_set_word(s->dev.config + PCI_STATUS, status);
}

static void bonito_spciconf_writel(void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
    PCIBonitoState *s = opaque;
    uint32_t pciaddr;
    uint16_t status;

    DPRINTF("bonito_spciconf_writel "TARGET_FMT_plx" val %x\n", addr, val);
    assert((addr&0x3)==0);

    pciaddr = bonito_sbridge_pciaddr(s, addr);

    if (pciaddr == 0xffffffff) {
        return;
    }

    /* set the pci address in s->config_reg */
    s->pcihost->config_reg = (pciaddr) | (1u << 31);
    pci_data_write(s->pcihost->bus, s->pcihost->config_reg, val, 4);

    /* clear PCI_STATUS_REC_MASTER_ABORT and PCI_STATUS_REC_TARGET_ABORT */
    status = pci_get_word(s->dev.config + PCI_STATUS);
    status &= ~(PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT);
    pci_set_word(s->dev.config + PCI_STATUS, status);
}

static uint32_t bonito_spciconf_readb(void *opaque, target_phys_addr_t addr)
{
    PCIBonitoState *s = opaque;
    uint32_t pciaddr;
    uint16_t status;

    DPRINTF("bonito_spciconf_readb "TARGET_FMT_plx"\n", addr);
    pciaddr = bonito_sbridge_pciaddr(s, addr);

    if (pciaddr == 0xffffffff) {
        return 0xff;
    }

    /* set the pci address in s->config_reg */
    s->pcihost->config_reg = (pciaddr) | (1u << 31);

    /* clear PCI_STATUS_REC_MASTER_ABORT and PCI_STATUS_REC_TARGET_ABORT */
    status = pci_get_word(s->dev.config + PCI_STATUS);
    status &= ~(PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT);
    pci_set_word(s->dev.config + PCI_STATUS, status);

    return pci_data_read(s->pcihost->bus, s->pcihost->config_reg, 1);
}

static uint32_t bonito_spciconf_readw(void *opaque, target_phys_addr_t addr)
{
    PCIBonitoState *s = opaque;
    uint32_t pciaddr;
    uint16_t status;

    DPRINTF("bonito_spciconf_readw "TARGET_FMT_plx"\n", addr);
    assert((addr&0x1)==0);

    pciaddr = bonito_sbridge_pciaddr(s, addr);

    if (pciaddr == 0xffffffff) {
        return 0xffff;
    }

    /* set the pci address in s->config_reg */
    s->pcihost->config_reg = (pciaddr) | (1u << 31);

    /* clear PCI_STATUS_REC_MASTER_ABORT and PCI_STATUS_REC_TARGET_ABORT */
    status = pci_get_word(s->dev.config + PCI_STATUS);
    status &= ~(PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT);
    pci_set_word(s->dev.config + PCI_STATUS, status);

    return pci_data_read(s->pcihost->bus, s->pcihost->config_reg, 2);
}

static uint32_t bonito_spciconf_readl(void *opaque, target_phys_addr_t addr)
{
    PCIBonitoState *s = opaque;
    uint32_t pciaddr;
    uint16_t status;

    DPRINTF("bonito_spciconf_readl "TARGET_FMT_plx"\n", addr);
    assert((addr&0x3) == 0);

    pciaddr = bonito_sbridge_pciaddr(s, addr);

    if (pciaddr == 0xffffffff) {
        return 0xffffffff;
    }

    /* set the pci address in s->config_reg */
    s->pcihost->config_reg = (pciaddr) | (1u << 31);

    /* clear PCI_STATUS_REC_MASTER_ABORT and PCI_STATUS_REC_TARGET_ABORT */
    status = pci_get_word(s->dev.config + PCI_STATUS);
    status &= ~(PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT);
    pci_set_word(s->dev.config + PCI_STATUS, status);

    return pci_data_read(s->pcihost->bus, s->pcihost->config_reg, 4);
}

/* south bridge PCI configure space. 0x1fe8 0000 - 0x1fef ffff */
static const MemoryRegionOps bonito_spciconf_ops = {
    .old_mmio = {
        .read = {
            bonito_spciconf_readb,
            bonito_spciconf_readw,
            bonito_spciconf_readl,
        },
        .write = {
            bonito_spciconf_writeb,
            bonito_spciconf_writew,
            bonito_spciconf_writel,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define BONITO_IRQ_BASE 32

static void pci_bonito_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;
    int internal_irq = irq_num - BONITO_IRQ_BASE;

    if (bonito_state->regs[BONITO_INTEDGE] & (1<<internal_irq)) {
        qemu_irq_pulse(*pic);
    } else {   /* level triggered */
        if (bonito_state->regs[BONITO_INTPOL] & (1<<internal_irq)) {
            qemu_irq_raise(*pic);
        } else {
            qemu_irq_lower(*pic);
        }
    }
}

/* map the original irq (0~3) to bonito irq (16~47, but 16~31 are unused) */
static int pci_bonito_map_irq(PCIDevice * pci_dev, int irq_num)
{
    int slot;

    slot = (pci_dev->devfn >> 3);

    switch (slot) {
    case 5:   /* FULONG2E_VIA_SLOT, SouthBridge, IDE, USB, ACPI, AC97, MC97 */
        return irq_num % 4 + BONITO_IRQ_BASE;
    case 6:   /* FULONG2E_ATI_SLOT, VGA */
        return 4 + BONITO_IRQ_BASE;
    case 7:   /* FULONG2E_RTL_SLOT, RTL8139 */
        return 5 + BONITO_IRQ_BASE;
    case 8 ... 12: /* PCI slot 1 to 4 */
        return (slot - 8 + irq_num) + 6 + BONITO_IRQ_BASE;
    default:  /* Unknown device, don't do any translation */
        return irq_num;
    }
}

static void bonito_reset(void *opaque)
{
    PCIBonitoState *s = opaque;

    /* set the default value of north bridge registers */

    s->regs[BONITO_BONPONCFG] = 0xc40;
    s->regs[BONITO_BONGENCFG] = 0x1384;
    s->regs[BONITO_IODEVCFG] = 0x2bff8010;
    s->regs[BONITO_SDCFG] = 0x255e0091;

    s->regs[BONITO_GPIODATA] = 0x1ff;
    s->regs[BONITO_GPIOIE] = 0x1ff;
    s->regs[BONITO_DQCFG] = 0x8;
    s->regs[BONITO_MEMSIZE] = 0x10000000;
    s->regs[BONITO_PCIMAP] = 0x6140;
}

static const VMStateDescription vmstate_bonito = {
    .name = "Bonito",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, PCIBonitoState),
        VMSTATE_END_OF_LIST()
    }
};

static int bonito_pcihost_initfn(SysBusDevice *dev)
{
    return 0;
}

static int bonito_initfn(PCIDevice *dev)
{
    PCIBonitoState *s = DO_UPCAST(PCIBonitoState, dev, dev);
    SysBusDevice *sysbus = &s->pcihost->busdev;

    /* Bonito North Bridge, built on FPGA, VENDOR_ID/DEVICE_ID are "undefined" */
    pci_config_set_prog_interface(dev->config, 0x00);

    /* set the north bridge register mapping */
    memory_region_init_io(&s->iomem, &bonito_ops, s,
                          "north-bridge-register", BONITO_INTERNAL_REG_SIZE);
    sysbus_init_mmio(sysbus, &s->iomem);
    sysbus_mmio_map(sysbus, 0, BONITO_INTERNAL_REG_BASE);

    /* set the north bridge pci configure  mapping */
    memory_region_init_io(&s->pcihost->conf_mem, &bonito_pciconf_ops, s,
                          "north-bridge-pci-config", BONITO_PCICONFIG_SIZE);
    sysbus_init_mmio(sysbus, &s->pcihost->conf_mem);
    sysbus_mmio_map(sysbus, 1, BONITO_PCICONFIG_BASE);

    /* set the south bridge pci configure  mapping */
    memory_region_init_io(&s->pcihost->data_mem, &bonito_spciconf_ops, s,
                          "south-bridge-pci-config", BONITO_SPCICONFIG_SIZE);
    sysbus_init_mmio(sysbus, &s->pcihost->data_mem);
    sysbus_mmio_map(sysbus, 2, BONITO_SPCICONFIG_BASE);

    memory_region_init_io(&s->iomem_ldma, &bonito_ldma_ops, s,
                          "ldma", 0x100);
    sysbus_init_mmio(sysbus, &s->iomem_ldma);
    sysbus_mmio_map(sysbus, 3, 0xbfe00200);

    memory_region_init_io(&s->iomem_cop, &bonito_cop_ops, s,
                          "cop", 0x100);
    sysbus_init_mmio(sysbus, &s->iomem_cop);
    sysbus_mmio_map(sysbus, 4, 0xbfe00300);

    /* Map PCI IO Space  0x1fd0 0000 - 0x1fd1 0000 */
    s->bonito_pciio_start = BONITO_PCIIO_BASE;
    s->bonito_pciio_length = BONITO_PCIIO_SIZE;
    isa_mem_base = s->bonito_pciio_start;
    isa_mmio_init(s->bonito_pciio_start, s->bonito_pciio_length);

    /* add pci local io mapping */
    s->bonito_localio_start = BONITO_DEV_BASE;
    s->bonito_localio_length = BONITO_DEV_SIZE;
    isa_mmio_init(s->bonito_localio_start, s->bonito_localio_length);

    /* set the default value of north bridge pci config */
    pci_set_word(dev->config + PCI_COMMAND, 0x0000);
    pci_set_word(dev->config + PCI_STATUS, 0x0000);
    pci_set_word(dev->config + PCI_SUBSYSTEM_VENDOR_ID, 0x0000);
    pci_set_word(dev->config + PCI_SUBSYSTEM_ID, 0x0000);

    pci_set_byte(dev->config + PCI_INTERRUPT_LINE, 0x00);
    pci_set_byte(dev->config + PCI_INTERRUPT_PIN, 0x01);
    pci_set_byte(dev->config + PCI_MIN_GNT, 0x3c);
    pci_set_byte(dev->config + PCI_MAX_LAT, 0x00);

    qemu_register_reset(bonito_reset, s);

    return 0;
}

PCIBus *bonito_init(qemu_irq *pic)
{
    DeviceState *dev;
    PCIBus *b;
    BonitoState *pcihost;
    PCIBonitoState *s;
    PCIDevice *d;

    dev = qdev_create(NULL, "Bonito-pcihost");
    pcihost = FROM_SYSBUS(BonitoState, sysbus_from_qdev(dev));
    b = pci_register_bus(&pcihost->busdev.qdev, "pci", pci_bonito_set_irq,
                         pci_bonito_map_irq, pic, get_system_memory(),
                         get_system_io(),
                         0x28, 32);
    pcihost->bus = b;
    qdev_init_nofail(dev);

    /* set the pcihost pointer before bonito_initfn is called */
    d = pci_create(b, PCI_DEVFN(0, 0), "Bonito");
    s = DO_UPCAST(PCIBonitoState, dev, d);
    s->pcihost = pcihost;
    bonito_state = s;
    qdev_init_nofail(&d->qdev);

    return b;
}

static void bonito_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = bonito_initfn;
    k->vendor_id = 0xdf53;
    k->device_id = 0x00d5;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    dc->desc = "Host bridge";
    dc->no_user = 1;
    dc->vmsd = &vmstate_bonito;
}

static TypeInfo bonito_info = {
    .name          = "Bonito",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBonitoState),
    .class_init    = bonito_class_init,
};

static void bonito_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = bonito_pcihost_initfn;
    dc->no_user = 1;
}

static TypeInfo bonito_pcihost_info = {
    .name          = "Bonito-pcihost",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BonitoState),
    .class_init    = bonito_pcihost_class_init,
};

static void bonito_register_types(void)
{
    type_register_static(&bonito_pcihost_info);
    type_register_static(&bonito_info);
}

type_init(bonito_register_types)
