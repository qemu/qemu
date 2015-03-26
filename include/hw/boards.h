/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

#include "qemu/typedefs.h"
#include "sysemu/blockdev.h"
#include "sysemu/accel.h"
#include "hw/qdev.h"
#include "qom/object.h"


typedef void QEMUMachineInitFunc(MachineState *ms);

typedef void QEMUMachineResetFunc(void);

typedef void QEMUMachineHotAddCPUFunc(const int64_t id, Error **errp);

typedef int QEMUMachineGetKvmtypeFunc(const char *arg);

struct QEMUMachine {
    const char *family; /* NULL iff @name identifies a standalone machtype */
    const char *name;
    const char *alias;
    const char *desc;
    QEMUMachineInitFunc *init;
    QEMUMachineResetFunc *reset;
    QEMUMachineHotAddCPUFunc *hot_add_cpu;
    QEMUMachineGetKvmtypeFunc *kvm_type;
    BlockInterfaceType block_default_type;
    int units_per_default_bus;
    int max_cpus;
    unsigned int no_serial:1,
        no_parallel:1,
        use_virtcon:1,
        use_sclp:1,
        no_floppy:1,
        no_cdrom:1,
        no_sdcard:1,
        has_dynamic_sysbus:1;
    int is_default;
    const char *default_machine_opts;
    const char *default_boot_order;
    const char *default_display;
    GlobalProperty *compat_props;
    const char *hw_version;
};

void memory_region_allocate_system_memory(MemoryRegion *mr, Object *owner,
                                          const char *name,
                                          uint64_t ram_size);

int qemu_register_machine(QEMUMachine *m);

#define TYPE_MACHINE_SUFFIX "-machine"
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

bool machine_usb(MachineState *machine);
bool machine_iommu(MachineState *machine);
bool machine_kernel_irqchip_allowed(MachineState *machine);
bool machine_kernel_irqchip_required(MachineState *machine);
int machine_kvm_shadow_mem(MachineState *machine);
int machine_phandle_start(MachineState *machine);
bool machine_dump_guest_core(MachineState *machine);
bool machine_mem_merge(MachineState *machine);

/**
 * MachineClass:
 * @qemu_machine: #QEMUMachine
 * @get_hotplug_handler: this function is called during bus-less
 *    device hotplug. If defined it returns pointer to an instance
 *    of HotplugHandler object, which handles hotplug operation
 *    for a given @dev. It may return NULL if @dev doesn't require
 *    any actions to be performed by hotplug handler.
 * @cpu_index_to_socket_id:
 *    used to provide @cpu_index to socket number mapping, allowing
 *    a machine to group CPU threads belonging to the same socket/package
 *    Returns: socket number given cpu_index belongs to.
 */
struct MachineClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    const char *family; /* NULL iff @name identifies a standalone machtype */
    const char *name;
    const char *alias;
    const char *desc;

    void (*init)(MachineState *state);
    void (*reset)(void);
    void (*hot_add_cpu)(const int64_t id, Error **errp);
    int (*kvm_type)(const char *arg);

    BlockInterfaceType block_default_type;
    int units_per_default_bus;
    int max_cpus;
    unsigned int no_serial:1,
        no_parallel:1,
        use_virtcon:1,
        use_sclp:1,
        no_floppy:1,
        no_cdrom:1,
        no_sdcard:1,
        has_dynamic_sysbus:1;
    int is_default;
    const char *default_machine_opts;
    const char *default_boot_order;
    const char *default_display;
    GlobalProperty *compat_props;
    const char *hw_version;

    HotplugHandler *(*get_hotplug_handler)(MachineState *machine,
                                           DeviceState *dev);
    unsigned (*cpu_index_to_socket_id)(unsigned cpu_index);
};

/**
 * MachineState:
 */
struct MachineState {
    /*< private >*/
    Object parent_obj;
    Notifier sysbus_notifier;

    /*< public >*/

    char *accel;
    bool kernel_irqchip_allowed;
    bool kernel_irqchip_required;
    int kvm_shadow_mem;
    char *dtb;
    char *dumpdtb;
    int phandle_start;
    char *dt_compatible;
    bool dump_guest_core;
    bool mem_merge;
    bool usb;
    bool usb_disabled;
    char *firmware;
    bool iommu;
    bool suppress_vmdesc;

    ram_addr_t ram_size;
    ram_addr_t maxram_size;
    uint64_t   ram_slots;
    const char *boot_order;
    char *kernel_filename;
    char *kernel_cmdline;
    char *initrd_filename;
    const char *cpu_model;
    AccelState *accelerator;
};

#endif
