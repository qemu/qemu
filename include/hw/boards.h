/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

#include "qemu/typedefs.h"
#include "sysemu/blockdev.h"
#include "sysemu/accel.h"
#include "hw/qdev.h"
#include "qom/object.h"

void memory_region_allocate_system_memory(MemoryRegion *mr, Object *owner,
                                          const char *name,
                                          uint64_t ram_size);

#define TYPE_MACHINE_SUFFIX "-machine"

/* Machine class name that needs to be used for class-name-based machine
 * type lookup to work.
 */
#define MACHINE_TYPE_NAME(machinename) (machinename TYPE_MACHINE_SUFFIX)

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
bool machine_kernel_irqchip_allowed(MachineState *machine);
bool machine_kernel_irqchip_required(MachineState *machine);
bool machine_kernel_irqchip_split(MachineState *machine);
int machine_kvm_shadow_mem(MachineState *machine);
int machine_phandle_start(MachineState *machine);
bool machine_dump_guest_core(MachineState *machine);
bool machine_mem_merge(MachineState *machine);

/**
 * MachineClass:
 * @get_hotplug_handler: this function is called during bus-less
 *    device hotplug. If defined it returns pointer to an instance
 *    of HotplugHandler object, which handles hotplug operation
 *    for a given @dev. It may return NULL if @dev doesn't require
 *    any actions to be performed by hotplug handler.
 * @cpu_index_to_socket_id:
 *    used to provide @cpu_index to socket number mapping, allowing
 *    a machine to group CPU threads belonging to the same socket/package
 *    Returns: socket number given cpu_index belongs to.
 * @hw_version:
 *    Value of QEMU_VERSION when the machine was added to QEMU.
 *    Set only by old machines because they need to keep
 *    compatibility on code that exposed QEMU_VERSION to guests in
 *    the past (and now use qemu_hw_version()).
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
        has_dynamic_sysbus:1,
        pci_allow_0_address:1;
    int is_default;
    const char *default_machine_opts;
    const char *default_boot_order;
    const char *default_display;
    GlobalProperty *compat_props;
    const char *hw_version;
    ram_addr_t default_ram_size;
    bool option_rom_has_mr;
    bool rom_file_has_mr;

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
    bool kernel_irqchip_split;
    int kvm_shadow_mem;
    char *dtb;
    char *dumpdtb;
    int phandle_start;
    char *dt_compatible;
    bool dump_guest_core;
    bool mem_merge;
    bool usb;
    bool usb_disabled;
    bool igd_gfx_passthru;
    char *firmware;
    bool iommu;
    bool suppress_vmdesc;
    bool enforce_config_section;

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

#define DEFINE_MACHINE(namestr, machine_initfn) \
    static void machine_initfn##_class_init(ObjectClass *oc, void *data) \
    { \
        MachineClass *mc = MACHINE_CLASS(oc); \
        machine_initfn(mc); \
    } \
    static const TypeInfo machine_initfn##_typeinfo = { \
        .name       = MACHINE_TYPE_NAME(namestr), \
        .parent     = TYPE_MACHINE, \
        .class_init = machine_initfn##_class_init, \
    }; \
    static void machine_initfn##_register_types(void) \
    { \
        type_register_static(&machine_initfn##_typeinfo); \
    } \
    machine_init(machine_initfn##_register_types)

#define SET_MACHINE_COMPAT(m, COMPAT) \
    do {                              \
        static GlobalProperty props[] = {       \
            COMPAT                              \
            { /* end of list */ }               \
        };                                      \
        (m)->compat_props = props;              \
    } while (0)

#endif
