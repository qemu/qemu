#ifndef PPCE500_H
#define PPCE500_H

typedef struct PPCE500Params {
    /* Standard QEMU machine init params */
    ram_addr_t ram_size;
    const char *boot_device;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    const char *cpu_model;
    int pci_first_slot;
    int pci_nr_slots;

    /* e500-specific params */

    /* required -- must at least add toplevel board compatible */
    void (*fixup_devtree)(struct PPCE500Params *params, void *fdt);

    int mpic_version;
} PPCE500Params;

void ppce500_init(PPCE500Params *params);

#endif
