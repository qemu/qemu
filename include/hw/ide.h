#ifndef HW_IDE_H
#define HW_IDE_H

#include "exec/memory.h"

int ide_get_geometry(BusState *bus, int unit,
                     int16_t *cyls, int8_t *heads, int8_t *secs);
int ide_get_bios_chs_trans(BusState *bus, int unit);

/* ide/core.c */
void ide_drive_get(DriveInfo **hd, int max_bus);

#endif /* HW_IDE_H */
