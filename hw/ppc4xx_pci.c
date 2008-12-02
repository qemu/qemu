/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

/* This file implements emulation of the 32-bit PCI controller found in some
 * 4xx SoCs, such as the 440EP. */

#include "hw.h"

typedef target_phys_addr_t pci_addr_t;
#include "pci.h"
#include "pci_host.h"
#include "bswap.h"

#undef DEBUG
#ifdef DEBUG
#define DPRINTF(fmt, ...) do { printf(fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif /* DEBUG */

struct PCIMasterMap {
    uint32_t la;
    uint32_t ma;
    uint32_t pcila;
    uint32_t pciha;
};

struct PCITargetMap {
    uint32_t ms;
    uint32_t la;
};

#define PPC4xx_PCI_NR_PMMS 3
#define PPC4xx_PCI_NR_PTMS 2

struct PPC4xxPCIState {
    struct PCIMasterMap pmm[PPC4xx_PCI_NR_PMMS];
    struct PCITargetMap ptm[PPC4xx_PCI_NR_PTMS];

    PCIHostState pci_state;
    PCIDevice *pci_dev;
};
typedef struct PPC4xxPCIState PPC4xxPCIState;

#define PCIC0_CFGADDR       0x0
#define PCIC0_CFGDATA       0x4

/* PLB Memory Map (PMM) registers specify which PLB addresses are translated to
 * PCI accesses. */
#define PCIL0_PMM0LA        0x0
#define PCIL0_PMM0MA        0x4
#define PCIL0_PMM0PCILA     0x8
#define PCIL0_PMM0PCIHA     0xc
#define PCIL0_PMM1LA        0x10
#define PCIL0_PMM1MA        0x14
#define PCIL0_PMM1PCILA     0x18
#define PCIL0_PMM1PCIHA     0x1c
#define PCIL0_PMM2LA        0x20
#define PCIL0_PMM2MA        0x24
#define PCIL0_PMM2PCILA     0x28
#define PCIL0_PMM2PCIHA     0x2c

/* PCI Target Map (PTM) registers specify which PCI addresses are translated to
 * PLB accesses. */
#define PCIL0_PTM1MS        0x30
#define PCIL0_PTM1LA        0x34
#define PCIL0_PTM2MS        0x38
#define PCIL0_PTM2LA        0x3c
#define PCI_REG_SIZE        0x40


static uint32_t pci4xx_cfgaddr_readl(void *opaque, target_phys_addr_t addr)
{
    PPC4xxPCIState *ppc4xx_pci = opaque;

    return ppc4xx_pci->pci_state.config_reg;
}

static CPUReadMemoryFunc *pci4xx_cfgaddr_read[] = {
    &pci4xx_cfgaddr_readl,
    &pci4xx_cfgaddr_readl,
    &pci4xx_cfgaddr_readl,
};

static void pci4xx_cfgaddr_writel(void *opaque, target_phys_addr_t addr,
                                  uint32_t value)
{
    PPC4xxPCIState *ppc4xx_pci = opaque;

#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap32(value);
#endif

    ppc4xx_pci->pci_state.config_reg = value & ~0x3;
}

static CPUWriteMemoryFunc *pci4xx_cfgaddr_write[] = {
    &pci4xx_cfgaddr_writel,
    &pci4xx_cfgaddr_writel,
    &pci4xx_cfgaddr_writel,
};

static CPUReadMemoryFunc *pci4xx_cfgdata_read[] = {
    &pci_host_data_readb,
    &pci_host_data_readw,
    &pci_host_data_readl,
};

static CPUWriteMemoryFunc *pci4xx_cfgdata_write[] = {
    &pci_host_data_writeb,
    &pci_host_data_writew,
    &pci_host_data_writel,
};

static void ppc4xx_pci_reg_write4(void *opaque, target_phys_addr_t offset,
                                  uint32_t value)
{
    struct PPC4xxPCIState *pci = opaque;

#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap32(value);
#endif

    /* We ignore all target attempts at PCI configuration, effectively
     * assuming a bidirectional 1:1 mapping of PLB and PCI space. */

    switch (offset) {
    case PCIL0_PMM0LA:
        pci->pmm[0].la = value;
        break;
    case PCIL0_PMM0MA:
        pci->pmm[0].ma = value;
        break;
    case PCIL0_PMM0PCIHA:
        pci->pmm[0].pciha = value;
        break;
    case PCIL0_PMM0PCILA:
        pci->pmm[0].pcila = value;
        break;

    case PCIL0_PMM1LA:
        pci->pmm[1].la = value;
        break;
    case PCIL0_PMM1MA:
        pci->pmm[1].ma = value;
        break;
    case PCIL0_PMM1PCIHA:
        pci->pmm[1].pciha = value;
        break;
    case PCIL0_PMM1PCILA:
        pci->pmm[1].pcila = value;
        break;

    case PCIL0_PMM2LA:
        pci->pmm[2].la = value;
        break;
    case PCIL0_PMM2MA:
        pci->pmm[2].ma = value;
        break;
    case PCIL0_PMM2PCIHA:
        pci->pmm[2].pciha = value;
        break;
    case PCIL0_PMM2PCILA:
        pci->pmm[2].pcila = value;
        break;

    case PCIL0_PTM1MS:
        pci->ptm[0].ms = value;
        break;
    case PCIL0_PTM1LA:
        pci->ptm[0].la = value;
        break;
    case PCIL0_PTM2MS:
        pci->ptm[1].ms = value;
        break;
    case PCIL0_PTM2LA:
        pci->ptm[1].la = value;
        break;

    default:
        printf("%s: unhandled PCI internal register 0x%lx\n", __func__,
               (unsigned long)offset);
        break;
    }
}

static uint32_t ppc4xx_pci_reg_read4(void *opaque, target_phys_addr_t offset)
{
    struct PPC4xxPCIState *pci = opaque;
    uint32_t value;

    switch (offset) {
    case PCIL0_PMM0LA:
        value = pci->pmm[0].la;
        break;
    case PCIL0_PMM0MA:
        value = pci->pmm[0].ma;
        break;
    case PCIL0_PMM0PCIHA:
        value = pci->pmm[0].pciha;
        break;
    case PCIL0_PMM0PCILA:
        value = pci->pmm[0].pcila;
        break;

    case PCIL0_PMM1LA:
        value = pci->pmm[1].la;
        break;
    case PCIL0_PMM1MA:
        value = pci->pmm[1].ma;
        break;
    case PCIL0_PMM1PCIHA:
        value = pci->pmm[1].pciha;
        break;
    case PCIL0_PMM1PCILA:
        value = pci->pmm[1].pcila;
        break;

    case PCIL0_PMM2LA:
        value = pci->pmm[2].la;
        break;
    case PCIL0_PMM2MA:
        value = pci->pmm[2].ma;
        break;
    case PCIL0_PMM2PCIHA:
        value = pci->pmm[2].pciha;
        break;
    case PCIL0_PMM2PCILA:
        value = pci->pmm[2].pcila;
        break;

    case PCIL0_PTM1MS:
        value = pci->ptm[0].ms;
        break;
    case PCIL0_PTM1LA:
        value = pci->ptm[0].la;
        break;
    case PCIL0_PTM2MS:
        value = pci->ptm[1].ms;
        break;
    case PCIL0_PTM2LA:
        value = pci->ptm[1].la;
        break;

    default:
        printf("%s: invalid PCI internal register 0x%lx\n", __func__,
               (unsigned long)offset);
        value = 0;
    }

#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap32(value);
#endif

    return value;
}

static CPUReadMemoryFunc *pci_reg_read[] = {
    &ppc4xx_pci_reg_read4,
    &ppc4xx_pci_reg_read4,
    &ppc4xx_pci_reg_read4,
};

static CPUWriteMemoryFunc *pci_reg_write[] = {
    &ppc4xx_pci_reg_write4,
    &ppc4xx_pci_reg_write4,
    &ppc4xx_pci_reg_write4,
};

static void ppc4xx_pci_reset(void *opaque)
{
    struct PPC4xxPCIState *pci = opaque;

    memset(pci->pmm, 0, sizeof(pci->pmm));
    memset(pci->ptm, 0, sizeof(pci->ptm));
}

/* On Bamboo, all pins from each slot are tied to a single board IRQ. This
 * may need further refactoring for other boards. */
static int ppc4xx_pci_map_irq(PCIDevice *pci_dev, int irq_num)
{
    int slot = pci_dev->devfn >> 3;

    DPRINTF("%s: devfn %x irq %d -> %d\n", __func__,
            pci_dev->devfn, irq_num, slot);

    return slot - 1;
}

static void ppc4xx_pci_set_irq(qemu_irq *pci_irqs, int irq_num, int level)
{
    DPRINTF("%s: PCI irq %d\n", __func__, irq_num);
    qemu_set_irq(pci_irqs[irq_num], level);
}

static void ppc4xx_pci_save(QEMUFile *f, void *opaque)
{
    PPC4xxPCIState *controller = opaque;
    int i;

    pci_device_save(&controller->pci_dev, f);

    for (i = 0; i < PPC4xx_PCI_NR_PMMS; i++) {
        qemu_put_be32s(f, &controller->pmm[i].la);
        qemu_put_be32s(f, &controller->pmm[i].ma);
        qemu_put_be32s(f, &controller->pmm[i].pcila);
        qemu_put_be32s(f, &controller->pmm[i].pciha);
    }

    for (i = 0; i < PPC4xx_PCI_NR_PTMS; i++) {
        qemu_put_be32s(f, &controller->ptm[i].ms);
        qemu_put_be32s(f, &controller->ptm[i].la);
    }
}

static int ppc4xx_pci_load(QEMUFile *f, void *opaque, int version_id)
{
    PPC4xxPCIState *controller = opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    pci_device_load(&controller->pci_dev, f);

    for (i = 0; i < PPC4xx_PCI_NR_PMMS; i++) {
        qemu_get_be32s(f, &controller->pmm[i].la);
        qemu_get_be32s(f, &controller->pmm[i].ma);
        qemu_get_be32s(f, &controller->pmm[i].pcila);
        qemu_get_be32s(f, &controller->pmm[i].pciha);
    }

    for (i = 0; i < PPC4xx_PCI_NR_PTMS; i++) {
        qemu_get_be32s(f, &controller->ptm[i].ms);
        qemu_get_be32s(f, &controller->ptm[i].la);
    }

    return 0;
}

/* XXX Interrupt acknowledge cycles not supported. */
PCIBus *ppc4xx_pci_init(CPUState *env, qemu_irq pci_irqs[4],
                        target_phys_addr_t config_space,
                        target_phys_addr_t int_ack,
                        target_phys_addr_t special_cycle,
                        target_phys_addr_t registers)
{
    PPC4xxPCIState *controller;
    int index;
    static int ppc4xx_pci_id;

    controller = qemu_mallocz(sizeof(PPC4xxPCIState));
    if (!controller)
        return NULL;

    controller->pci_state.bus = pci_register_bus(ppc4xx_pci_set_irq,
                                                 ppc4xx_pci_map_irq,
                                                 pci_irqs, 0, 4);

    controller->pci_dev = pci_register_device(controller->pci_state.bus,
                                              "host bridge", sizeof(PCIDevice),
                                              0, NULL, NULL);
    controller->pci_dev->config[0x00] = 0x14; // vendor_id
    controller->pci_dev->config[0x01] = 0x10;
    controller->pci_dev->config[0x02] = 0x7f; // device_id
    controller->pci_dev->config[0x03] = 0x02;
    controller->pci_dev->config[0x0a] = 0x80; // class_sub = other bridge type
    controller->pci_dev->config[0x0b] = 0x06; // class_base = PCI_bridge

    /* CFGADDR */
    index = cpu_register_io_memory(0, pci4xx_cfgaddr_read,
                                   pci4xx_cfgaddr_write, controller);
    if (index < 0)
        goto free;
    cpu_register_physical_memory(config_space + PCIC0_CFGADDR, 4, index);

    /* CFGDATA */
    index = cpu_register_io_memory(0, pci4xx_cfgdata_read,
                                   pci4xx_cfgdata_write,
                                   &controller->pci_state);
    if (index < 0)
        goto free;
    cpu_register_physical_memory(config_space + PCIC0_CFGDATA, 4, index);

    /* Internal registers */
    index = cpu_register_io_memory(0, pci_reg_read, pci_reg_write, controller);
    if (index < 0)
        goto free;
    cpu_register_physical_memory(registers, PCI_REG_SIZE, index);

    qemu_register_reset(ppc4xx_pci_reset, controller);

    /* XXX load/save code not tested. */
    register_savevm("ppc4xx_pci", ppc4xx_pci_id++, 1,
                    ppc4xx_pci_save, ppc4xx_pci_load, controller);

    return controller->pci_state.bus;

free:
    printf("%s error\n", __func__);
    qemu_free(controller);
    return NULL;
}
