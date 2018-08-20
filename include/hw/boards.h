/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

#include "sysemu/blockdev.h"
#include "sysemu/accel.h"
#include "hw/qdev.h"
#include "qom/object.h"
#include "qom/cpu.h"

/**
 * memory_region_allocate_system_memory - Allocate a board's main memory
 * @mr: the #MemoryRegion to be initialized
 * @owner: the object that tracks the region's reference count
 * @name: name of the memory region
 * @ram_size: size of the region in bytes
 *
 * This function allocates the main memory for a board model, and
 * initializes @mr appropriately. It also arranges for the memory
 * to be migrated (by calling vmstate_register_ram_global()).
 *
 * Memory allocated via this function will be backed with the memory
 * backend the user provided using "-mem-path" or "-numa node,memdev=..."
 * if appropriate; this is typically used to cause host huge pages to be
 * used. This function should therefore be called by a board exactly once,
 * for the primary or largest RAM area it implements.
 *
 * For boards where the major RAM is split into two parts in the memory
 * map, you can deal with this by calling memory_region_allocate_system_memory()
 * once to get a MemoryRegion with enough RAM for both parts, and then
 * creating alias MemoryRegions via memory_region_init_alias() which
 * alias into different parts of the RAM MemoryRegion and can be mapped
 * into the memory map in the appropriate places.
 *
 * Smaller pieces of memory (display RAM, static RAMs, etc) don't need
 * to be backed via the -mem-path memory backend and can simply
 * be created via memory_region_init_ram().
 */
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

void machine_run_board_init(MachineState *machine);
bool machine_usb(MachineState *machine);
bool machine_kernel_irqchip_allowed(MachineState *machine);
bool machine_kernel_irqchip_required(MachineState *machine);
bool machine_kernel_irqchip_split(MachineState *machine);
int machine_kvm_shadow_mem(MachineState *machine);
int machine_phandle_start(MachineState *machine);
bool machine_dump_guest_core(MachineState *machine);
bool machine_mem_merge(MachineState *machine);
void machine_register_compat_props(MachineState *machine);
HotpluggableCPUList *machine_query_hotpluggable_cpus(MachineState *machine);
void machine_set_cpu_numa_node(MachineState *machine,
                               const CpuInstanceProperties *props,
                               Error **errp);

void machine_class_allow_dynamic_sysbus_dev(MachineClass *mc, const char *type);


/**
 * CPUArchId:
 * @arch_id - architecture-dependent CPU ID of present or possible CPU
 * @cpu - pointer to corresponding CPU object if it's present on NULL otherwise
 * @type - QOM class name of possible @cpu object
 * @props - CPU object properties, initialized by board
 * #vcpus_count - number of threads provided by @cpu object
 */
typedef struct {
    uint64_t arch_id;
    int64_t vcpus_count;
    CpuInstanceProperties props;
    Object *cpu;
    const char *type;
} CPUArchId;

/**
 * CPUArchIdList:
 * @len - number of @CPUArchId items in @cpus array
 * @cpus - array of present or possible CPUs for current machine configuration
 */
typedef struct {
    int len;
    CPUArchId cpus[0];
} CPUArchIdList;

/**
 * MachineClass:
 * @deprecation_reason: If set, the machine is marked as deprecated. The
 *    string should provide some clear information about what to use instead.
 * @max_cpus: maximum number of CPUs supported. Default: 1
 * @min_cpus: minimum number of CPUs supported. Default: 1
 * @default_cpus: number of CPUs instantiated if none are specified. Default: 1
 * @get_hotplug_handler: this function is called during bus-less
 *    device hotplug. If defined it returns pointer to an instance
 *    of HotplugHandler object, which handles hotplug operation
 *    for a given @dev. It may return NULL if @dev doesn't require
 *    any actions to be performed by hotplug handler.
 * @cpu_index_to_instance_props:
 *    used to provide @cpu_index to socket/core/thread number mapping, allowing
 *    legacy code to perform maping from cpu_index to topology properties
 *    Returns: tuple of socket/core/thread ids given cpu_index belongs to.
 *    used to provide @cpu_index to socket number mapping, allowing
 *    a machine to group CPU threads belonging to the same socket/package
 *    Returns: socket number given cpu_index belongs to.
 * @hw_version:
 *    Value of QEMU_VERSION when the machine was added to QEMU.
 *    Set only by old machines because they need to keep
 *    compatibility on code that exposed QEMU_VERSION to guests in
 *    the past (and now use qemu_hw_version()).
 * @possible_cpu_arch_ids:
 *    Returns an array of @CPUArchId architecture-dependent CPU IDs
 *    which includes CPU IDs for present and possible to hotplug CPUs.
 *    Caller is responsible for freeing returned list.
 * @get_default_cpu_node_id:
 *    returns default board specific node_id value for CPU slot specified by
 *    index @idx in @ms->possible_cpus[]
 * @has_hotpluggable_cpus:
 *    If true, board supports CPUs creation with -device/device_add.
 * @default_cpu_type:
 *    specifies default CPU_TYPE, which will be used for parsing target
 *    specific features and for creating CPUs if CPU name wasn't provided
 *    explicitly at CLI
 * @minimum_page_bits:
 *    If non-zero, the board promises never to create a CPU with a page size
 *    smaller than this, so QEMU can use a more efficient larger page
 *    size than the target architecture's minimum. (Attempting to create
 *    such a CPU will fail.) Note that changing this is a migration
 *    compatibility break for the machine.
 * @ignore_memory_transaction_failures:
 *    If this is flag is true then the CPU will ignore memory transaction
 *    failures which should cause the CPU to take an exception due to an
 *    access to an unassigned physical address; the transaction will instead
 *    return zero (for a read) or be ignored (for a write). This should be
 *    set only by legacy board models which rely on the old RAZ/WI behaviour
 *    for handling devices that QEMU does not yet model. New board models
 *    should instead use "unimplemented-device" for all memory ranges where
 *    the guest will attempt to probe for a device that QEMU doesn't
 *    implement and a stub device is required.
 */
struct MachineClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    const char *family; /* NULL iff @name identifies a standalone machtype */
    char *name;
    const char *alias;
    const char *desc;
    const char *deprecation_reason;

    void (*init)(MachineState *state);
    void (*reset)(void);
    void (*hot_add_cpu)(const int64_t id, Error **errp);
    int (*kvm_type)(const char *arg);

    BlockInterfaceType block_default_type;
    int units_per_default_bus;
    int max_cpus;
    int min_cpus;
    int default_cpus;
    unsigned int no_serial:1,
        no_parallel:1,
        use_virtcon:1,
        no_floppy:1,
        no_cdrom:1,
        no_sdcard:1,
        pci_allow_0_address:1,
        legacy_fw_cfg_order:1;
    int is_default;
    const char *default_machine_opts;
    const char *default_boot_order;
    const char *default_display;
    GArray *compat_props;
    const char *hw_version;
    ram_addr_t default_ram_size;
    const char *default_cpu_type;
    bool option_rom_has_mr;
    bool rom_file_has_mr;
    int minimum_page_bits;
    bool has_hotpluggable_cpus;
    bool ignore_memory_transaction_failures;
    int numa_mem_align_shift;
    const char **valid_cpu_types;
    strList *allowed_dynamic_sysbus_devices;
    bool auto_enable_numa_with_memhp;
    void (*numa_auto_assign_ram)(MachineClass *mc, NodeInfo *nodes,
                                 int nb_nodes, ram_addr_t size);
    bool ignore_boot_device_suffixes;

    HotplugHandler *(*get_hotplug_handler)(MachineState *machine,
                                           DeviceState *dev);
    CpuInstanceProperties (*cpu_index_to_instance_props)(MachineState *machine,
                                                         unsigned cpu_index);
    const CPUArchIdList *(*possible_cpu_arch_ids)(MachineState *machine);
    int64_t (*get_default_cpu_node_id)(const MachineState *ms, int idx);
};

/**
 * DeviceMemoryState:
 * @base: address in guest physical address space where the memory
 * address space for memory devices starts
 * @mr: address space container for memory devices
 */
typedef struct DeviceMemoryState {
    hwaddr base;
    MemoryRegion mr;
} DeviceMemoryState;

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
    bool enable_graphics;
    char *memory_encryption;
    DeviceMemoryState *device_memory;

    ram_addr_t ram_size;
    ram_addr_t maxram_size;
    uint64_t   ram_slots;
    const char *boot_order;
    char *kernel_filename;
    char *kernel_cmdline;
    char *initrd_filename;
    const char *cpu_type;
    AccelState *accelerator;
    CPUArchIdList *possible_cpus;
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
    type_init(machine_initfn##_register_types)

#define SET_MACHINE_COMPAT(m, COMPAT) \
    do {                              \
        int i;                        \
        static GlobalProperty props[] = {       \
            COMPAT                              \
            { /* end of list */ }               \
        };                                      \
        if (!m->compat_props) { \
            m->compat_props = g_array_new(false, false, sizeof(void *)); \
        } \
        for (i = 0; props[i].driver != NULL; i++) {    \
            GlobalProperty *prop = &props[i];          \
            g_array_append_val(m->compat_props, prop); \
        }                                              \
    } while (0)

#endif
