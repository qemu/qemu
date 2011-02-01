#include <hw/hw.h>
#include <hw/msi.h>
#include <hw/pc.h>
#include <hw/pci.h>
#include <hw/isa.h>
#include "block.h"
#include "block_int.h"
#include "sysemu.h"
#include "dma.h"

#include <hw/ide/pci.h>
#include <hw/ide/ahci.h>

static int pci_ich9_ahci_initfn(PCIDevice *dev)
{
    struct AHCIPCIState *d;
    d = DO_UPCAST(struct AHCIPCIState, card, dev);

    pci_config_set_vendor_id(d->card.config, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(d->card.config, PCI_DEVICE_ID_INTEL_82801IR);

    pci_config_set_class(d->card.config, PCI_CLASS_STORAGE_SATA);
    pci_config_set_revision(d->card.config, 0x02);
    pci_config_set_prog_interface(d->card.config, AHCI_PROGMODE_MAJOR_REV_1);

    d->card.config[PCI_CACHE_LINE_SIZE] = 0x08;  /* Cache line size */
    d->card.config[PCI_LATENCY_TIMER]   = 0x00;  /* Latency timer */
    pci_config_set_interrupt_pin(d->card.config, 1);

    /* XXX Software should program this register */
    d->card.config[0x90]   = 1 << 6; /* Address Map Register - AHCI mode */

    qemu_register_reset(ahci_reset, d);

    /* XXX BAR size should be 1k, but that breaks, so bump it to 4k for now */
    pci_register_bar(&d->card, 5, 0x1000, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     ahci_pci_map);

    msi_init(dev, 0x50, 1, true, false);

    ahci_init(&d->ahci, &dev->qdev);
    d->ahci.irq = d->card.irq[0];

    return 0;
}

static PCIDeviceInfo ich_ahci_info[] = {
    {
        .qdev.name    = "ich9-ahci",
        .qdev.size    = sizeof(AHCIPCIState),
        .init         = pci_ich9_ahci_initfn,
    },{
        /* end of list */
    }
};

static void ich_ahci_register(void)
{
    pci_qdev_register_many(ich_ahci_info);
}
device_init(ich_ahci_register);
