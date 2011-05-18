/*
 * QEMU ICH Emulation
 *
 * Copyright (c) 2010 Sebastian Herbszt <herbszt@gmx.de>
 * Copyright (c) 2010 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * lspci dump of a ICH-9 real device
 *
 * 00:1f.2 SATA controller [0106]: Intel Corporation 82801IR/IO/IH (ICH9R/DO/DH) 6 port SATA AHCI Controller [8086:2922] (rev 02) (prog-if 01 [AHCI 1.0])
 *         Subsystem: Intel Corporation 82801IR/IO/IH (ICH9R/DO/DH) 6 port SATA AHCI Controller [8086:2922]
 *         Control: I/O+ Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+
 *         Status: Cap+ 66MHz+ UDF- FastB2B+ ParErr- DEVSEL=medium >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
 *         Latency: 0
 *         Interrupt: pin B routed to IRQ 222
 *         Region 0: I/O ports at d000 [size=8]
 *         Region 1: I/O ports at cc00 [size=4]
 *         Region 2: I/O ports at c880 [size=8]
 *         Region 3: I/O ports at c800 [size=4]
 *         Region 4: I/O ports at c480 [size=32]
 *         Region 5: Memory at febf9000 (32-bit, non-prefetchable) [size=2K]
 *         Capabilities: [80] Message Signalled Interrupts: Mask- 64bit- Count=1/16 Enable+
 *                 Address: fee0f00c  Data: 41d9
 *         Capabilities: [70] Power Management version 3
 *                 Flags: PMEClk- DSI- D1- D2- AuxCurrent=0mA PME(D0-,D1-,D2-,D3hot+,D3cold-)
 *                 Status: D0 PME-Enable- DSel=0 DScale=0 PME-
 *         Capabilities: [a8] SATA HBA <?>
 *         Capabilities: [b0] Vendor Specific Information <?>
 *         Kernel driver in use: ahci
 *         Kernel modules: ahci
 * 00: 86 80 22 29 07 04 b0 02 02 01 06 01 00 00 00 00
 * 10: 01 d0 00 00 01 cc 00 00 81 c8 00 00 01 c8 00 00
 * 20: 81 c4 00 00 00 90 bf fe 00 00 00 00 86 80 22 29
 * 30: 00 00 00 00 80 00 00 00 00 00 00 00 0f 02 00 00
 * 40: 00 80 00 80 00 00 00 00 00 00 00 00 00 00 00 00
 * 50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * 70: 01 a8 03 40 08 00 00 00 00 00 00 00 00 00 00 00
 * 80: 05 70 09 00 0c f0 e0 fe d9 41 00 00 00 00 00 00
 * 90: 40 00 0f 82 93 01 00 00 00 00 00 00 00 00 00 00
 * a0: ac 00 00 00 0a 00 12 00 12 b0 10 00 48 00 00 00
 * b0: 09 00 06 20 00 00 00 00 00 00 00 00 00 00 00 00
 * c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 * f0: 00 00 00 00 00 00 00 00 86 0f 02 00 00 00 00 00
 *
 */

#include <hw/hw.h>
#include <hw/msi.h>
#include <hw/pc.h>
#include <hw/pci.h>
#include <hw/isa.h>
#include "block.h"
#include "block_int.h"
#include "dma.h"

#include <hw/ide/pci.h>
#include <hw/ide/ahci.h>

static int pci_ich9_ahci_init(PCIDevice *dev)
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

    msi_init(dev, 0x50, 1, true, false);

    ahci_init(&d->ahci, &dev->qdev, 6);
    d->ahci.irq = d->card.irq[0];

    /* XXX BAR size should be 1k, but that breaks, so bump it to 4k for now */
    pci_register_bar_simple(&d->card, 5, 0x1000, 0, d->ahci.mem);

    return 0;
}

static int pci_ich9_uninit(PCIDevice *dev)
{
    struct AHCIPCIState *d;
    d = DO_UPCAST(struct AHCIPCIState, card, dev);

    msi_uninit(dev);
    qemu_unregister_reset(ahci_reset, d);
    ahci_uninit(&d->ahci);

    return 0;
}

static void pci_ich9_write_config(PCIDevice *pci, uint32_t addr,
                                  uint32_t val, int len)
{
    pci_default_write_config(pci, addr, val, len);
    msi_write_config(pci, addr, val, len);
}

static PCIDeviceInfo ich_ahci_info[] = {
    {
        .qdev.name    = "ich9-ahci",
        .qdev.alias   = "ahci",
        .qdev.size    = sizeof(AHCIPCIState),
        .init         = pci_ich9_ahci_init,
        .exit         = pci_ich9_uninit,
        .config_write = pci_ich9_write_config,
    },{
        /* end of list */
    }
};

static void ich_ahci_register(void)
{
    pci_qdev_register_many(ich_ahci_info);
}
device_init(ich_ahci_register);
