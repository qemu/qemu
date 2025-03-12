#ifndef HW_IDE_BUS_H
#define HW_IDE_BUS_H

#include "system/ioport.h"
#include "hw/ide/ide-dev.h"
#include "hw/ide/ide-dma.h"

struct IDEBus {
    BusState qbus;
    IDEDevice *master;
    IDEDevice *slave;
    IDEState ifs[2];
    QEMUBH *bh;

    int bus_id;
    int max_units;
    IDEDMA *dma;
    uint8_t unit;
    uint8_t cmd;
    qemu_irq irq; /* bus output */

    int error_status;
    uint8_t retry_unit;
    int64_t retry_sector_num;
    uint32_t retry_nsector;
    PortioList portio_list;
    PortioList portio2_list;
    VMChangeStateEntry *vmstate;
};

#define TYPE_IDE_BUS "IDE"
OBJECT_DECLARE_SIMPLE_TYPE(IDEBus, IDE_BUS)

void ide_bus_init(IDEBus *idebus, size_t idebus_size, DeviceState *dev,
                  int bus_id, int max_units);
IDEDevice *ide_bus_create_drive(IDEBus *bus, int unit, DriveInfo *drive);

int ide_get_geometry(BusState *bus, int unit,
                     int16_t *cyls, int8_t *heads, int8_t *secs);
int ide_get_bios_chs_trans(BusState *bus, int unit);

#endif
