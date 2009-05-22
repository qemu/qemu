/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

typedef void QEMUMachineInitFunc(ram_addr_t ram_size,
                                 const char *boot_device,
                                 const char *kernel_filename,
                                 const char *kernel_cmdline,
                                 const char *initrd_filename,
                                 const char *cpu_model);

typedef struct QEMUMachine {
    const char *name;
    const char *desc;
    QEMUMachineInitFunc *init;
    int use_scsi;
    int max_cpus;
    int is_default;
    struct QEMUMachine *next;
} QEMUMachine;

int qemu_register_machine(QEMUMachine *m);

extern QEMUMachine *current_machine;

#endif
