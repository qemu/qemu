#ifndef PPCE500_H
#define PPCE500_H

#include "hw/boards.h"

typedef struct PPCE500Params {
    int pci_first_slot;
    int pci_nr_slots;

    /* required -- must at least add toplevel board compatible */
    void (*fixup_devtree)(struct PPCE500Params *params, void *fdt);

    int mpic_version;
} PPCE500Params;

void ppce500_init(QEMUMachineInitArgs *args, PPCE500Params *params);

#endif
