/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

#include "sysemu/blockdev.h"
#include "hw/qdev.h"

#define DEFAULT_MACHINE_OPTIONS \
    .boot_order = "cad"

typedef struct QEMUMachineInitArgs {
    ram_addr_t ram_size;
    const char *boot_device;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    const char *cpu_model;
} QEMUMachineInitArgs;

typedef void QEMUMachineInitFunc(QEMUMachineInitArgs *args);

typedef void QEMUMachineResetFunc(void);

typedef struct QEMUMachine {
    const char *name;
    const char *alias;
    const char *desc;
    QEMUMachineInitFunc *init;
    QEMUMachineResetFunc *reset;
    BlockInterfaceType block_default_type;
    int max_cpus;
    unsigned int no_serial:1,
        no_parallel:1,
        use_virtcon:1,
        use_sclp:1,
        no_floppy:1,
        no_cdrom:1,
        no_sdcard:1;
    int is_default;
    const char *default_machine_opts;
    const char *boot_order;
    GlobalProperty *compat_props;
    struct QEMUMachine *next;
    const char *hw_version;
} QEMUMachine;

int qemu_register_machine(QEMUMachine *m);
QEMUMachine *find_default_machine(void);

extern QEMUMachine *current_machine;

#endif
