#ifndef HW_IDE_H
#define HW_IDE_H

#include "qdev.h"

/* ide-isa.c */
void isa_ide_init(int iobase, int iobase2, qemu_irq irq,
                  BlockDriverState *hd0, BlockDriverState *hd1);

#endif /* HW_IDE_H */
