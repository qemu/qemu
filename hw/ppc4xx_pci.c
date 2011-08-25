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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

/* This file implements emulation of the 32-bit PCI controller found in some
 * 4xx SoCs, such as the 440EP. */

#include "hw.h"
#include "ppc.h"
#include "ppc4xx.h"
#include "pci.h"
#include "pci_host.h"
#include "exec-memory.h"

#undef DEBUG
#ifdef DEBUG
#define DPRINTF(fmt, ...) do { printf(fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
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

static CPUReadMemoryFunc * const pci4xx_cfgaddr_read[] = {
    &pci4xx_cfgaddr_readl,
    &pci4xx_cfgaddr_readl,
    &pci4xx_cfgaddr_readl,
};

static void pci4xx_cfgaddr_writel(void *opaque, target_phys_addr_t addr,
                                  uint32_t value)
{
    PPC4xxPCIState *ppc4xx_pci = opaque;

    ppc4xx_pci->pci_state.config_reg = value & ~0x3;
}

static CPUWriteMemoryFunc * const pci4xx_cfgaddr_write[] = {
    &pci4xx_cfgaddr_writel,
    &pci4xx_cfgaddr_writel,
    &pci4xx_cfgaddr_writel,
};

static void ppc4xx_pci_reg_write4(void *opaque, target_phys_addr_t offset,
                                  uint32_t value)
{
    struct PPC4xxPCIState *pci = opaque;

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

    return value;
}

static CPUReadMemoryFunc * const pci_reg_read[] = {
    &ppc4xx_pci_reg_read4,
    &ppc4xx_pci_reg_read4,
    &ppc4xx_pci_reg_read4,
};

static CPUWriteMemoryFunc * const pci_reg_write[] = {
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

static void ppc4xx_pci_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pci_irqs = opaque;

    DPRINTF("%s: PCI irq %d\n", __func__, irq_num);
    qemu_set_irq(pci_irqs[irq_num], level);
}

static const VMStateDescription vmstate_pci_master_map = {
    .name = "pci_master_map",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(la, struct PCIMasterMap),
        VMSTATE_UINT32(ma, struct PCIMasterMap),
        VMSTATE_UINT32(pcila, struct PCIMasterMap),
        VMSTATE_UINT32(pciha, struct PCIMasterMap),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pci_target_map = {
    .name = "pci_target_map",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(ms, struct PCITargetMap),
        VMSTATE_UINT32(la, struct PCITargetMap),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ppc4xx_pci = {
    .name = "ppc4xx_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_PCI_DEVICE_POINTER(pci_dev, PPC4xxPCIState),
        VMSTATE_STRUCT_ARRAY(pmm, PPC4xxPCIState, PPC4xx_PCI_NR_PMMS, 1,
                             vmstate_pci_master_map,
                             struct PCIMasterMap),
        VMSTATE_STRUCT_ARRAY(ptm, PPC4xxPCIState, PPC4xx_PCI_NR_PTMS, 1,
                             vmstate_pci_target_map,
                             struct PCITargetMap),
        VMSTATE_END_OF_LIST()
    }
};

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
    uint8_t *pci_conf;

    controller = g_malloc0(sizeof(PPC4xxPCIState));

    controller->pci_state.bus = pci_register_bus(NULL, "pci",
                                                 ppc4xx_pci_set_irq,
                                                 ppc4xx_pci_map_irq,
                                                 pci_irqs,
                                                 get_system_memory(),
                                                 get_system_io(),
                                                 0, 4);

    controller->pci_dev = pci_register_device(controller->pci_state.bus,
                                              "host bridge", sizeof(PCIDevice),
                                              0, NULL, NULL);
    pci_conf = controller->pci_dev->config;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_IBM);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_IBM_440GX);
    pci_config_set_class(pci_conf, PCI_CLASS_BRIDGE_OTHER);

    /* CFGADDR */
    index = cpu_register_io_memory(pci4xx_cfgaddr_read,
                                   pci4xx_cfgaddr_write, controller,
                                   DEVICE_LITTLE_ENDIAN);
    if (index < 0)
        goto free;
    cpu_register_physical_memory(config_space + PCIC0_CFGADDR, 4, index);

    /* CFGDATA */
    index = pci_host_data_register_mmio(&controller->pci_state, 1);
    if (index < 0)
        goto free;
    cpu_register_physical_memory(config_space + PCIC0_CFGDATA, 4, index);

    /* Internal registers */
    index = cpu_register_io_memory(pci_reg_read, pci_reg_write, controller,
                                   DEVICE_LITTLE_ENDIAN);
    if (index < 0)
        goto free;
    cpu_register_physical_memory(registers, PCI_REG_SIZE, index);

    qemu_register_reset(ppc4xx_pci_reset, controller);

    /* XXX load/save code not tested. */
    vmstate_register(&controller->pci_dev->qdev, ppc4xx_pci_id++,
                     &vmstate_ppc4xx_pci, controller);

    return controller->pci_state.bus;

free:
    printf("%s error\n", __func__);
    g_free(controller);
    return NULL;
}
