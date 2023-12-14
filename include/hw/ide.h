#ifndef HW_IDE_H
#define HW_IDE_H

#include "exec/memory.h"

/* ide/core.c */
void ide_drive_get(DriveInfo **hd, int max_bus);

#endif /* HW_IDE_H */
