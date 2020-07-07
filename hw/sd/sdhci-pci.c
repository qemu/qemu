/*
 * SDHCI device on PCI
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "hw/sd/sdhci.h"
#include "sdhci-internal.h"

static Property sdhci_pci_properties[] = {
    DEFINE_SDHCI_COMMON_PROPERTIES(SDHCIState),
    DEFINE_PROP_END_OF_LIST(),
};

static void sdhci_pci_realize(PCIDevice *dev, Error **errp)
{
    ERRP_GUARD();
    SDHCIState *s = PCI_SDHCI(dev);

    sdhci_initfn(s);
    sdhci_common_realize(s, errp);
    if (*errp) {
        return;
    }

    dev->config[PCI_CLASS_PROG] = 0x01; /* Standard Host supported DMA */
    dev->config[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin A */
    s->irq = pci_allocate_irq(dev);
    s->dma_as = pci_get_address_space(dev);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->iomem);
}

static void sdhci_pci_exit(PCIDevice *dev)
{
    SDHCIState *s = PCI_SDHCI(dev);

    sdhci_common_unrealize(s);
    sdhci_uninitfn(s);
}

static void sdhci_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = sdhci_pci_realize;
    k->exit = sdhci_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_SDHCI;
    k->class_id = PCI_CLASS_SYSTEM_SDHCI;
    device_class_set_props(dc, sdhci_pci_properties);

    sdhci_common_class_init(klass, data);
}

static const TypeInfo sdhci_pci_info = {
    .name = TYPE_PCI_SDHCI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SDHCIState),
    .class_init = sdhci_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void sdhci_pci_register_type(void)
{
    type_register_static(&sdhci_pci_info);
}

type_init(sdhci_pci_register_type)
