/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

#include "qemu/typedefs.h"
#include "sysemu/blockdev.h"
#include "hw/qdev.h"
#include "qom/object.h"


typedef struct MachineState MachineState;

typedef void QEMUMachineInitFunc(MachineState *ms);

typedef void QEMUMachineResetFunc(void);

typedef void QEMUMachineHotAddCPUFunc(const int64_t id, Error **errp);

typedef int QEMUMachineGetKvmtypeFunc(const char *arg);

struct QEMUMachine {
    const char *name;
    const char *alias;
    const char *desc;
    QEMUMachineInitFunc *init;
    QEMUMachineResetFunc *reset;
    QEMUMachineHotAddCPUFunc *hot_add_cpu;
    QEMUMachineGetKvmtypeFunc *kvm_type;
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
    const char *default_boot_order;
    GlobalProperty *compat_props;
    const char *hw_version;
};

#define TYPE_MACHINE_SUFFIX "-machine"
int qemu_register_machine(QEMUMachine *m);

#define TYPE_MACHINE "machine"
#undef MACHINE  /* BSD defines it and QEMU does not use it */
#define MACHINE(obj) \
    OBJECT_CHECK(MachineState, (obj), TYPE_MACHINE)
#define MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MachineClass, (obj), TYPE_MACHINE)
#define MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(MachineClass, (klass), TYPE_MACHINE)

MachineClass *find_default_machine(void);
extern MachineState *current_machine;

/**
 * MachineClass:
 * @qemu_machine: #QEMUMachine
 */
struct MachineClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    const char *name;
    const char *alias;
    const char *desc;

    void (*init)(MachineState *state);
    void (*reset)(void);
    void (*hot_add_cpu)(const int64_t id, Error **errp);
    int (*kvm_type)(const char *arg);

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
    const char *default_boot_order;
    GlobalProperty *compat_props;
    const char *hw_version;
};

/**
 * MachineState:
 */
struct MachineState {
    /*< private >*/
    Object parent_obj;
    /*< public >*/

    char *accel;
    bool kernel_irqchip;
    int kvm_shadow_mem;
    char *dtb;
    char *dumpdtb;
    int phandle_start;
    char *dt_compatible;
    bool dump_guest_core;
    bool mem_merge;
    bool usb;
    char *firmware;

    ram_addr_t ram_size;
    const char *boot_order;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    const char *cpu_model;
};

#endif
