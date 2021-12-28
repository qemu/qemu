/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

#include "exec/memory.h"
#include "sysemu/hostmem.h"
#include "sysemu/blockdev.h"
#include "qemu/accel.h"
#include "qapi/qapi-types-machine.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "hw/core/cpu.h"

#define TYPE_MACHINE_SUFFIX "-machine"

/* Machine class name that needs to be used for class-name-based machine
 * type lookup to work.
 */
#define MACHINE_TYPE_NAME(machinename) (machinename TYPE_MACHINE_SUFFIX)

#define TYPE_MACHINE "machine"
#undef MACHINE  /* BSD defines it and QEMU does not use it */
OBJECT_DECLARE_TYPE(MachineState, MachineClass, MACHINE)

extern MachineState *current_machine;

void machine_run_board_init(MachineState *machine);
bool machine_usb(MachineState *machine);
int machine_phandle_start(MachineState *machine);
bool machine_dump_guest_core(MachineState *machine);
bool machine_mem_merge(MachineState *machine);
HotpluggableCPUList *machine_query_hotpluggable_cpus(MachineState *machine);
void machine_set_cpu_numa_node(MachineState *machine,
                               const CpuInstanceProperties *props,
                               Error **errp);
void machine_parse_smp_config(MachineState *ms,
                              const SMPConfiguration *config, Error **errp);

/**
 * machine_class_allow_dynamic_sysbus_dev: Add type to list of valid devices
 * @mc: Machine class
 * @type: type to allow (should be a subtype of TYPE_SYS_BUS_DEVICE)
 *
 * Add the QOM type @type to the list of devices of which are subtypes
 * of TYPE_SYS_BUS_DEVICE but which are still permitted to be dynamically
 * created (eg by the user on the command line with -device).
 * By default if the user tries to create any devices on the command line
 * that are subtypes of TYPE_SYS_BUS_DEVICE they will get an error message;
 * for the special cases which are permitted for this machine model, the
 * machine model class init code must call this function to add them
 * to the list of specifically permitted devices.
 */
void machine_class_allow_dynamic_sysbus_dev(MachineClass *mc, const char *type);

/**
 * device_type_is_dynamic_sysbus: Check if type is an allowed sysbus device
 * type for the machine class.
 * @mc: Machine class
 * @type: type to check (should be a subtype of TYPE_SYS_BUS_DEVICE)
 *
 * Returns: true if @type is a type in the machine's list of
 * dynamically pluggable sysbus devices; otherwise false.
 *
 * Check if the QOM type @type is in the list of allowed sysbus device
 * types (see machine_class_allowed_dynamic_sysbus_dev()).
 * Note that if @type has a parent type in the list, it is allowed too.
 */
bool device_type_is_dynamic_sysbus(MachineClass *mc, const char *type);

/**
 * device_is_dynamic_sysbus: test whether device is a dynamic sysbus device
 * @mc: Machine class
 * @dev: device to check
 *
 * Returns: true if @dev is a sysbus device on the machine's list
 * of dynamically pluggable sysbus devices; otherwise false.
 *
 * This function checks whether @dev is a valid dynamic sysbus device,
 * by first confirming that it is a sysbus device and then checking it
 * against the list of permitted dynamic sysbus devices which has been
 * set up by the machine using machine_class_allow_dynamic_sysbus_dev().
 *
 * It is valid to call this with something that is not a subclass of
 * TYPE_SYS_BUS_DEVICE; the function will return false in this case.
 * This allows hotplug callback functions to be written as:
 *     if (device_is_dynamic_sysbus(mc, dev)) {
 *         handle dynamic sysbus case;
 *     } else if (some other kind of hotplug) {
 *         handle that;
 *     }
 */
bool device_is_dynamic_sysbus(MachineClass *mc, DeviceState *dev);

/*
 * Checks that backend isn't used, preps it for exclusive usage and
 * returns migratable MemoryRegion provided by backend.
 */
MemoryRegion *machine_consume_memdev(MachineState *machine,
                                     HostMemoryBackend *backend);

/**
 * CPUArchId:
 * @arch_id - architecture-dependent CPU ID of present or possible CPU
 * @cpu - pointer to corresponding CPU object if it's present on NULL otherwise
 * @type - QOM class name of possible @cpu object
 * @props - CPU object properties, initialized by board
 * #vcpus_count - number of threads provided by @cpu object
 */
typedef struct CPUArchId {
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
    CPUArchId cpus[];
} CPUArchIdList;

/**
 * SMPCompatProps:
 * @prefer_sockets - whether sockets are preferred over cores in smp parsing
 * @dies_supported - whether dies are supported by the machine
 * @clusters_supported - whether clusters are supported by the machine
 */
typedef struct {
    bool prefer_sockets;
    bool dies_supported;
    bool clusters_supported;
} SMPCompatProps;

/**
 * MachineClass:
 * @deprecation_reason: If set, the machine is marked as deprecated. The
 *    string should provide some clear information about what to use instead.
 * @max_cpus: maximum number of CPUs supported. Default: 1
 * @min_cpus: minimum number of CPUs supported. Default: 1
 * @default_cpus: number of CPUs instantiated if none are specified. Default: 1
 * @is_default:
 *    If true QEMU will use this machine by default if no '-M' option is given.
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
 * @kvm_type:
 *    Return the type of KVM corresponding to the kvm-type string option or
 *    computed based on other criteria such as the host kernel capabilities.
 *    kvm-type may be NULL if it is not needed.
 * @numa_mem_supported:
 *    true if '--numa node.mem' option is supported and false otherwise
 * @hotplug_allowed:
 *    If the hook is provided, then it'll be called for each device
 *    hotplug to check whether the device hotplug is allowed.  Return
 *    true to grant allowance or false to reject the hotplug.  When
 *    false is returned, an error must be set to show the reason of
 *    the rejection.  If the hook is not provided, all hotplug will be
 *    allowed.
 * @default_ram_id:
 *    Specifies inital RAM MemoryRegion name to be used for default backend
 *    creation if user explicitly hasn't specified backend with "memory-backend"
 *    property.
 *    It also will be used as a way to optin into "-m" option support.
 *    If it's not set by board, '-m' will be ignored and generic code will
 *    not create default RAM MemoryRegion.
 * @fixup_ram_size:
 *    Amends user provided ram size (with -m option) using machine
 *    specific algorithm. To be used by old machine types for compat
 *    purposes only.
 *    Applies only to default memory backend, i.e., explicit memory backend
 *    wasn't used.
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
    void (*reset)(MachineState *state);
    void (*wakeup)(MachineState *state);
    int (*kvm_type)(MachineState *machine, const char *arg);

    BlockInterfaceType block_default_type;
    int units_per_default_bus;
    int max_cpus;
    int min_cpus;
    int default_cpus;
    unsigned int no_serial:1,
        no_parallel:1,
        no_floppy:1,
        no_cdrom:1,
        no_sdcard:1,
        pci_allow_0_address:1,
        legacy_fw_cfg_order:1;
    bool is_default;
    const char *default_machine_opts;
    const char *default_boot_order;
    const char *default_display;
    GPtrArray *compat_props;
    const char *hw_version;
    ram_addr_t default_ram_size;
    const char *default_cpu_type;
    bool default_kernel_irqchip_split;
    bool option_rom_has_mr;
    bool rom_file_has_mr;
    int minimum_page_bits;
    bool has_hotpluggable_cpus;
    bool ignore_memory_transaction_failures;
    int numa_mem_align_shift;
    const char **valid_cpu_types;
    strList *allowed_dynamic_sysbus_devices;
    bool auto_enable_numa_with_memhp;
    bool auto_enable_numa_with_memdev;
    bool ignore_boot_device_suffixes;
    bool smbus_no_migration_support;
    bool nvdimm_supported;
    bool numa_mem_supported;
    bool auto_enable_numa;
    SMPCompatProps smp_props;
    const char *default_ram_id;

    HotplugHandler *(*get_hotplug_handler)(MachineState *machine,
                                           DeviceState *dev);
    bool (*hotplug_allowed)(MachineState *state, DeviceState *dev,
                            Error **errp);
    CpuInstanceProperties (*cpu_index_to_instance_props)(MachineState *machine,
                                                         unsigned cpu_index);
    const CPUArchIdList *(*possible_cpu_arch_ids)(MachineState *machine);
    int64_t (*get_default_cpu_node_id)(const MachineState *ms, int idx);
    ram_addr_t (*fixup_ram_size)(ram_addr_t size);
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
 * CpuTopology:
 * @cpus: the number of present logical processors on the machine
 * @sockets: the number of sockets on the machine
 * @dies: the number of dies in one socket
 * @clusters: the number of clusters in one die
 * @cores: the number of cores in one cluster
 * @threads: the number of threads in one core
 * @max_cpus: the maximum number of logical processors on the machine
 */
typedef struct CpuTopology {
    unsigned int cpus;
    unsigned int sockets;
    unsigned int dies;
    unsigned int clusters;
    unsigned int cores;
    unsigned int threads;
    unsigned int max_cpus;
} CpuTopology;

/**
 * MachineState:
 */
struct MachineState {
    /*< private >*/
    Object parent_obj;

    /*< public >*/

    void *fdt;
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
    bool enable_graphics;
    ConfidentialGuestSupport *cgs;
    char *ram_memdev_id;
    /*
     * convenience alias to ram_memdev_id backend memory region
     * or to numa container memory region
     */
    MemoryRegion *ram;
    DeviceMemoryState *device_memory;

    ram_addr_t ram_size;
    ram_addr_t maxram_size;
    uint64_t   ram_slots;
    const char *boot_order;
    const char *boot_once;
    char *kernel_filename;
    char *kernel_cmdline;
    char *initrd_filename;
    const char *cpu_type;
    AccelState *accelerator;
    CPUArchIdList *possible_cpus;
    CpuTopology smp;
    struct NVDIMMState *nvdimms_state;
    struct NumaState *numa_state;
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

extern GlobalProperty hw_compat_6_1[];
extern const size_t hw_compat_6_1_len;

extern GlobalProperty hw_compat_6_0[];
extern const size_t hw_compat_6_0_len;

extern GlobalProperty hw_compat_5_2[];
extern const size_t hw_compat_5_2_len;

extern GlobalProperty hw_compat_5_1[];
extern const size_t hw_compat_5_1_len;

extern GlobalProperty hw_compat_5_0[];
extern const size_t hw_compat_5_0_len;

extern GlobalProperty hw_compat_4_2[];
extern const size_t hw_compat_4_2_len;

extern GlobalProperty hw_compat_4_1[];
extern const size_t hw_compat_4_1_len;

extern GlobalProperty hw_compat_4_0[];
extern const size_t hw_compat_4_0_len;

extern GlobalProperty hw_compat_3_1[];
extern const size_t hw_compat_3_1_len;

extern GlobalProperty hw_compat_3_0[];
extern const size_t hw_compat_3_0_len;

extern GlobalProperty hw_compat_2_12[];
extern const size_t hw_compat_2_12_len;

extern GlobalProperty hw_compat_2_11[];
extern const size_t hw_compat_2_11_len;

extern GlobalProperty hw_compat_2_10[];
extern const size_t hw_compat_2_10_len;

extern GlobalProperty hw_compat_2_9[];
extern const size_t hw_compat_2_9_len;

extern GlobalProperty hw_compat_2_8[];
extern const size_t hw_compat_2_8_len;

extern GlobalProperty hw_compat_2_7[];
extern const size_t hw_compat_2_7_len;

extern GlobalProperty hw_compat_2_6[];
extern const size_t hw_compat_2_6_len;

extern GlobalProperty hw_compat_2_5[];
extern const size_t hw_compat_2_5_len;

extern GlobalProperty hw_compat_2_4[];
extern const size_t hw_compat_2_4_len;

extern GlobalProperty hw_compat_2_3[];
extern const size_t hw_compat_2_3_len;

extern GlobalProperty hw_compat_2_2[];
extern const size_t hw_compat_2_2_len;

extern GlobalProperty hw_compat_2_1[];
extern const size_t hw_compat_2_1_len;

#endif
