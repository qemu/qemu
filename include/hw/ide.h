#ifndef HW_IDE_H
#define HW_IDE_H

#include "hw/isa/isa.h"
#include "exec/memory.h"

/* ide-isa.c */
ISADevice *isa_ide_init(ISABus *bus, int iobase, int iobase2, int isairq,
                        DriveInfo *hd0, DriveInfo *hd1);

/* ide-pci.c */
int pci_piix3_xen_ide_unplug(DeviceState *dev, bool aux);

/* ide-mmio.c */
void mmio_ide_init_drives(DeviceState *dev, DriveInfo *hd0, DriveInfo *hd1);

int ide_get_geometry(BusState *bus, int unit,
                     int16_t *cyls, int8_t *heads, int8_t *secs);
int ide_get_bios_chs_trans(BusState *bus, int unit);

/* ide/core.c */
void ide_drive_get(DriveInfo **hd, int max_bus);

#endif /* HW_IDE_H */
