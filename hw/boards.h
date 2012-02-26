/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

#include "qdev.h"

typedef void QEMUMachineInitFunc(ram_addr_t ram_size,
                                 const char *boot_device,
                                 const char *kernel_filename,
                                 const char *kernel_cmdline,
                                 const char *initrd_filename,
                                 const char *cpu_model);

typedef struct QEMUMachine {
    const char *name;
    const char *alias;
    const char *desc;
    QEMUMachineInitFunc *init;
    int use_scsi;
    int max_cpus;
    unsigned int no_serial:1,
        no_parallel:1,
        use_virtcon:1,
        no_floppy:1,
        no_cdrom:1,
        no_sdcard:1;
    int is_default;
    const char *default_machine_opts;
    GlobalProperty *compat_props;
    struct QEMUMachine *next;
} QEMUMachine;

int qemu_register_machine(QEMUMachine *m);
QEMUMachine *find_default_machine(void);

extern QEMUMachine *current_machine;

#endif
