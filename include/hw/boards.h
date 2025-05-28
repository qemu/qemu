/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

#include "system/memory.h"
#include "system/hostmem.h"
#include "system/blockdev.h"
#include "qapi/qapi-types-machine.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "hw/core/cpu.h"
#include "hw/resettable.h"

#define TYPE_MACHINE_SUFFIX "-machine"

/* Machine class name that needs to be used for class-name-based machine
 * type lookup to work.
 */
#define MACHINE_TYPE_NAME(machinename) (machinename TYPE_MACHINE_SUFFIX)

#define TYPE_MACHINE "machine"
#undef MACHINE  /* BSD defines it and QEMU does not use it */
OBJECT_DECLARE_TYPE(MachineState, MachineClass, MACHINE)

extern MachineState *current_machine;

/**
 * machine_class_default_cpu_type: Return the machine default CPU type.
 * @mc: Machine class
 */
const char *machine_class_default_cpu_type(MachineClass *mc);

void machine_add_audiodev_property(MachineClass *mc);
void machine_run_board_init(MachineState *machine, const char *mem_path, Error **errp);
bool machine_usb(MachineState *machine);
int machine_phandle_start(MachineState *machine);
bool machine_dump_guest_core(MachineState *machine);
bool machine_mem_merge(MachineState *machine);
bool machine_require_guest_memfd(MachineState *machine);
HotpluggableCPUList *machine_query_hotpluggable_cpus(MachineState *machine);
void machine_set_cpu_numa_node(MachineState *machine,
                               const CpuInstanceProperties *props,
                               Error **errp);
void machine_parse_smp_config(MachineState *ms,
                              const SMPConfiguration *config, Error **errp);
bool machine_parse_smp_cache(MachineState *ms,
                             const SmpCachePropertiesList *caches,
                             Error **errp);
unsigned int machine_topo_get_cores_per_socket(const MachineState *ms);
unsigned int machine_topo_get_threads_per_socket(const MachineState *ms);
CpuTopologyLevel machine_get_cache_topo_level(const MachineState *ms,
                                              CacheLevelAndType cache);
void machine_set_cache_topo_level(MachineState *ms, CacheLevelAndType cache,
                                  CpuTopologyLevel level);
bool machine_check_smp_cache(const MachineState *ms, Error **errp);
void machine_memory_devices_init(MachineState *ms, hwaddr base, uint64_t size);

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
    CPUState *cpu;
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
 * @has_clusters - whether clusters are explicitly specified in the user
 *                 provided SMP configuration
 * @books_supported - whether books are supported by the machine
 * @drawers_supported - whether drawers are supported by the machine
 * @modules_supported - whether modules are supported by the machine
 * @cache_supported - whether cache (l1d, l1i, l2 and l3) configuration are
 *                    supported by the machine
 * @has_caches - whether cache properties are explicitly specified in the
 *               user provided smp-cache configuration
 */
typedef struct {
    bool prefer_sockets;
    bool dies_supported;
    bool clusters_supported;
    bool has_clusters;
    bool books_supported;
    bool drawers_supported;
    bool modules_supported;
    bool cache_supported[CACHE_LEVEL_AND_TYPE__MAX];
    bool has_caches;
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
 *    legacy code to perform mapping from cpu_index to topology properties
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
 * @hvf_get_physical_address_range:
 *    Returns the physical address range in bits to use for the HVF virtual
 *    machine based on the current boards memory map. This may be NULL if it
 *    is not needed.
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
 *    Specifies initial RAM MemoryRegion name to be used for default backend
 *    creation if user explicitly hasn't specified backend with "memory-backend"
 *    property.
 *    It also will be used as a way to option into "-m" option support.
 *    If it's not set by board, '-m' will be ignored and generic code will
 *    not create default RAM MemoryRegion.
 * @fixup_ram_size:
 *    Amends user provided ram size (with -m option) using machine
 *    specific algorithm. To be used by old machine types for compat
 *    purposes only.
 *    Applies only to default memory backend, i.e., explicit memory backend
 *    wasn't used.
 * @smbios_memory_device_size:
 *    Default size of memory device,
 *    SMBIOS 3.1.0 "7.18 Memory Device (Type 17)"
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
    void (*reset)(MachineState *state, ResetType type);
    void (*wakeup)(MachineState *state);
    int (*kvm_type)(MachineState *machine, const char *arg);
    int (*hvf_get_physical_address_range)(MachineState *machine);

    BlockInterfaceType block_default_type;
    int units_per_default_bus;
    int max_cpus;
    int min_cpus;
    int default_cpus;
    unsigned int no_serial:1,
        no_parallel:1,
        no_floppy:1,
        no_cdrom:1,
        pci_allow_0_address:1;
    bool auto_create_sdcard;
    bool is_default;
    const char *default_machine_opts;
    const char *default_boot_order;
    const char *default_display;
    const char *default_nic;
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
    const char * const *valid_cpu_types;
    strList *allowed_dynamic_sysbus_devices;
    bool auto_enable_numa_with_memhp;
    bool auto_enable_numa_with_memdev;
    bool ignore_boot_device_suffixes;
    bool smbus_no_migration_support;
    bool nvdimm_supported;
    bool numa_mem_supported;
    bool auto_enable_numa;
    bool cpu_cluster_has_numa_boundary;
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
    uint64_t smbios_memory_device_size;
    bool (*create_default_memdev)(MachineState *ms, const char *path,
                                  Error **errp);
};

/**
 * DeviceMemoryState:
 * @base: address in guest physical address space where the memory
 * address space for memory devices starts
 * @mr: memory region container for memory devices
 * @as: address space for memory devices
 * @listener: memory listener used to track used memslots in the address space
 * @dimm_size: the sum of plugged DIMMs' sizes
 * @used_region_size: the part of @mr already used by memory devices
 * @required_memslots: the number of memslots required by memory devices
 * @used_memslots: the number of memslots currently used by memory devices
 * @memslot_auto_decision_active: whether any plugged memory device
 *                                automatically decided to use more than
 *                                one memslot
 */
typedef struct DeviceMemoryState {
    hwaddr base;
    MemoryRegion mr;
    AddressSpace as;
    MemoryListener listener;
    uint64_t dimm_size;
    uint64_t used_region_size;
    unsigned int required_memslots;
    unsigned int used_memslots;
    unsigned int memslot_auto_decision_active;
} DeviceMemoryState;

/**
 * CpuTopology:
 * @cpus: the number of present logical processors on the machine
 * @drawers: the number of drawers on the machine
 * @books: the number of books in one drawer
 * @sockets: the number of sockets in one book
 * @dies: the number of dies in one socket
 * @clusters: the number of clusters in one die
 * @modules: the number of modules in one cluster
 * @cores: the number of cores in one cluster
 * @threads: the number of threads in one core
 * @max_cpus: the maximum number of logical processors on the machine
 */
typedef struct CpuTopology {
    unsigned int cpus;
    unsigned int drawers;
    unsigned int books;
    unsigned int sockets;
    unsigned int dies;
    unsigned int clusters;
    unsigned int modules;
    unsigned int cores;
    unsigned int threads;
    unsigned int max_cpus;
} CpuTopology;

typedef struct SmpCache {
    SmpCacheProperties props[CACHE_LEVEL_AND_TYPE__MAX];
} SmpCache;

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
    HostMemoryBackend *memdev;
    bool aux_ram_share;
    /*
     * convenience alias to ram_memdev_id backend memory region
     * or to numa container memory region
     */
    MemoryRegion *ram;
    DeviceMemoryState *device_memory;

    /*
     * Included in MachineState for simplicity, but not supported
     * unless machine_add_audiodev_property is called.  Boards
     * that have embedded audio devices can call it from the
     * machine init function and forward the property to the device.
     */
    char *audiodev;

    ram_addr_t ram_size;
    ram_addr_t maxram_size;
    uint64_t   ram_slots;
    BootConfiguration boot_config;
    char *kernel_filename;
    char *kernel_cmdline;
    char *shim_filename;
    char *initrd_filename;
    const char *cpu_type;
    AccelState *accelerator;
    CPUArchIdList *possible_cpus;
    CpuTopology smp;
    SmpCache smp_cache;
    struct NVDIMMState *nvdimms_state;
    struct NumaState *numa_state;
    bool acpi_spcr_enabled;
};

/*
 * The macros which follow are intended to facilitate the
 * definition of versioned machine types, using a somewhat
 * similar pattern across targets.
 *
 * For example, a macro that can be used to define versioned
 * 'virt' machine types would look like:
 *
 *  #define DEFINE_VIRT_MACHINE_IMPL(latest, ...) \
 *      static void MACHINE_VER_SYM(class_init, virt, __VA_ARGS__)( \
 *          ObjectClass *oc, \
 *          void *data) \
 *      { \
 *          MachineClass *mc = MACHINE_CLASS(oc); \
 *          MACHINE_VER_SYM(options, virt, __VA_ARGS__)(mc); \
 *          mc->desc = "QEMU " MACHINE_VER_STR(__VA_ARGS__) " Virtual Machine"; \
 *          MACHINE_VER_DEPRECATION(__VA_ARGS__); \
 *          if (latest) { \
 *              mc->alias = "virt"; \
 *          } \
 *      } \
 *      static const TypeInfo MACHINE_VER_SYM(info, virt, __VA_ARGS__) = { \
 *          .name = MACHINE_VER_TYPE_NAME("virt", __VA_ARGS__), \
 *          .parent = TYPE_VIRT_MACHINE, \
 *          .class_init = MACHINE_VER_SYM(class_init, virt, __VA_ARGS__), \
 *      }; \
 *      static void MACHINE_VER_SYM(register, virt, __VA_ARGS__)(void) \
 *      { \
 *          MACHINE_VER_DELETION(__VA_ARGS__); \
 *          type_register_static(&MACHINE_VER_SYM(info, virt, __VA_ARGS__)); \
 *      } \
 *      type_init(MACHINE_VER_SYM(register, virt, __VA_ARGS__));
 *
 * Following this, one (or more) helpers can be added for
 * whichever scenarios need to be catered for with a machine:
 *
 *  // Normal 2 digit, marked as latest e.g. 'virt-9.0'
 *  #define DEFINE_VIRT_MACHINE_LATEST(major, minor) \
 *      DEFINE_VIRT_MACHINE_IMPL(true, major, minor)
 *
 *  // Normal 2 digit e.g. 'virt-9.0'
 *  #define DEFINE_VIRT_MACHINE(major, minor) \
 *      DEFINE_VIRT_MACHINE_IMPL(false, major, minor)
 *
 *  // Bugfix 3 digit e.g. 'virt-9.0.1'
 *  #define DEFINE_VIRT_MACHINE_BUGFIX(major, minor, micro) \
 *      DEFINE_VIRT_MACHINE_IMPL(false, major, minor, micro)
 *
 *  // Tagged 2 digit e.g. 'virt-9.0-extra'
 *  #define DEFINE_VIRT_MACHINE_TAGGED(major, minor, tag) \
 *      DEFINE_VIRT_MACHINE_IMPL(false, major, minor, _, tag)
 *
 *  // Tagged bugfix 2 digit e.g. 'virt-9.0.1-extra'
 *  #define DEFINE_VIRT_MACHINE_TAGGED(major, minor, micro, tag) \
 *      DEFINE_VIRT_MACHINE_IMPL(false, major, minor, micro, _, tag)
 */

/*
 * Helper for dispatching different macros based on how
 * many __VA_ARGS__ are passed. Supports 1 to 5 variadic
 * arguments, with the called target able to be prefixed
 * with 0 or more fixed arguments too. To be called thus:
 *
 *  _MACHINE_VER_PICK(__VA_ARGS,
 *                    MACRO_MATCHING_5_ARGS,
 *                    MACRO_MATCHING_4_ARGS,
 *                    MACRO_MATCHING_3_ARGS,
 *                    MACRO_MATCHING_2_ARGS,
 *                    MACRO_MATCHING_1_ARG) (FIXED-ARG-1,
 *                                           ...,
 *                                           FIXED-ARG-N,
 *                                           __VA_ARGS__)
 */
#define _MACHINE_VER_PICK(x1, x2, x3, x4, x5, x6, ...) x6

/*
 * Construct a human targeted machine version string.
 *
 * Can be invoked with various signatures
 *
 *  MACHINE_VER_STR(sym, prefix, major, minor)
 *  MACHINE_VER_STR(sym, prefix, major, minor, micro)
 *  MACHINE_VER_STR(sym, prefix, major, minor, _, tag)
 *  MACHINE_VER_STR(sym, prefix, major, minor, micro, _, tag)
 *
 * Respectively emitting symbols with the format
 *
 *   "{major}.{minor}"
 *   "{major}.{minor}-{tag}"
 *   "{major}.{minor}.{micro}"
 *   "{major}.{minor}.{micro}-{tag}"
 */
#define _MACHINE_VER_STR2(major, minor) \
    #major "." #minor

#define _MACHINE_VER_STR3(major, minor, micro) \
    #major "." #minor "." #micro

#define _MACHINE_VER_STR4(major, minor, _unused_, tag) \
    #major "." #minor "-" #tag

#define _MACHINE_VER_STR5(major, minor, micro, _unused_, tag) \
    #major "." #minor "." #micro "-" #tag

#define MACHINE_VER_STR(...) \
    _MACHINE_VER_PICK(__VA_ARGS__, \
                      _MACHINE_VER_STR5, \
                      _MACHINE_VER_STR4, \
                      _MACHINE_VER_STR3, \
                      _MACHINE_VER_STR2) (__VA_ARGS__)


/*
 * Construct a QAPI type name for a versioned machine
 * type
 *
 * Can be invoked with various signatures
 *
 *  MACHINE_VER_TYPE_NAME(prefix, major, minor)
 *  MACHINE_VER_TYPE_NAME(prefix, major, minor, micro)
 *  MACHINE_VER_TYPE_NAME(prefix, major, minor, _, tag)
 *  MACHINE_VER_TYPE_NAME(prefix, major, minor, micro, _, tag)
 *
 * Respectively emitting symbols with the format
 *
 *   "{prefix}-{major}.{minor}"
 *   "{prefix}-{major}.{minor}.{micro}"
 *   "{prefix}-{major}.{minor}-{tag}"
 *   "{prefix}-{major}.{minor}.{micro}-{tag}"
 */
#define _MACHINE_VER_TYPE_NAME2(prefix, major, minor)   \
    prefix "-" #major "." #minor TYPE_MACHINE_SUFFIX

#define _MACHINE_VER_TYPE_NAME3(prefix, major, minor, micro) \
    prefix "-" #major "." #minor "." #micro TYPE_MACHINE_SUFFIX

#define _MACHINE_VER_TYPE_NAME4(prefix, major, minor, _unused_, tag) \
    prefix "-" #major "." #minor "-" #tag TYPE_MACHINE_SUFFIX

#define _MACHINE_VER_TYPE_NAME5(prefix, major, minor, micro, _unused_, tag) \
    prefix "-" #major "." #minor "." #micro "-" #tag TYPE_MACHINE_SUFFIX

#define MACHINE_VER_TYPE_NAME(prefix, ...) \
    _MACHINE_VER_PICK(__VA_ARGS__, \
                      _MACHINE_VER_TYPE_NAME5, \
                      _MACHINE_VER_TYPE_NAME4, \
                      _MACHINE_VER_TYPE_NAME3, \
                      _MACHINE_VER_TYPE_NAME2) (prefix, __VA_ARGS__)

/*
 * Construct a name for a versioned machine type that is
 * suitable for use as a C symbol (function/variable/etc).
 *
 * Can be invoked with various signatures
 *
 *  MACHINE_VER_SYM(sym, prefix, major, minor)
 *  MACHINE_VER_SYM(sym, prefix, major, minor, micro)
 *  MACHINE_VER_SYM(sym, prefix, major, minor, _, tag)
 *  MACHINE_VER_SYM(sym, prefix, major, minor, micro, _, tag)
 *
 * Respectively emitting symbols with the format
 *
 *   {prefix}_machine_{major}_{minor}_{sym}
 *   {prefix}_machine_{major}_{minor}_{micro}_{sym}
 *   {prefix}_machine_{major}_{minor}_{tag}_{sym}
 *   {prefix}_machine_{major}_{minor}_{micro}_{tag}_{sym}
 */
#define _MACHINE_VER_SYM2(sym, prefix, major, minor) \
    prefix ## _machine_ ## major ## _ ## minor ## _ ## sym

#define _MACHINE_VER_SYM3(sym, prefix, major, minor, micro) \
    prefix ## _machine_ ## major ## _ ## minor ## _ ## micro ## _ ## sym

#define _MACHINE_VER_SYM4(sym, prefix, major, minor, _unused_, tag) \
    prefix ## _machine_ ## major ## _ ## minor ## _ ## tag ## _ ## sym

#define _MACHINE_VER_SYM5(sym, prefix, major, minor, micro, _unused_, tag) \
    prefix ## _machine_ ## major ## _ ## minor ## _ ## micro ## _ ## tag ## _ ## sym

#define MACHINE_VER_SYM(sym, prefix, ...) \
    _MACHINE_VER_PICK(__VA_ARGS__, \
                      _MACHINE_VER_SYM5, \
                      _MACHINE_VER_SYM4, \
                      _MACHINE_VER_SYM3, \
                      _MACHINE_VER_SYM2) (sym, prefix, __VA_ARGS__)


/*
 * How many years/major releases for each phase
 * of the life cycle. Assumes use of versioning
 * scheme where major is bumped each year.
 *
 * These values must match the ver_machine_deprecation_version
 * and ver_machine_deletion_version logic in docs/conf.py and
 * the text in docs/about/deprecated.rst
 */
#define MACHINE_VER_DELETION_MAJOR 6
#define MACHINE_VER_DEPRECATION_MAJOR 3

/*
 * Expands to a static string containing a deprecation
 * message for a versioned machine type
 */
#define MACHINE_VER_DEPRECATION_MSG \
    "machines more than " stringify(MACHINE_VER_DEPRECATION_MAJOR) \
    " years old are subject to deletion after " \
    stringify(MACHINE_VER_DELETION_MAJOR) " years"

#define _MACHINE_VER_IS_CURRENT_EXPIRED(cutoff, major, minor) \
    (((QEMU_VERSION_MAJOR - major) > cutoff) || \
     (((QEMU_VERSION_MAJOR - major) == cutoff) && \
      (QEMU_VERSION_MINOR - minor) >= 0))

#define _MACHINE_VER_IS_NEXT_MINOR_EXPIRED(cutoff, major, minor) \
    (((QEMU_VERSION_MAJOR - major) > cutoff) || \
     (((QEMU_VERSION_MAJOR - major) == cutoff) && \
      ((QEMU_VERSION_MINOR + 1) - minor) >= 0))

#define _MACHINE_VER_IS_NEXT_MAJOR_EXPIRED(cutoff, major, minor) \
    ((((QEMU_VERSION_MAJOR + 1) - major) > cutoff) ||            \
     ((((QEMU_VERSION_MAJOR + 1) - major) == cutoff) &&          \
      (0 - minor) >= 0))

/*
 * - The first check applies to formal releases
 * - The second check applies to dev snapshots / release candidates
 *   where the next major version is the same.
 *   e.g. 9.0.50, 9.1.50, 9.0.90, 9.1.90
 * - The third check applies to dev snapshots / release candidates
 *   where the next major version will change.
 *   e.g. 9.2.50, 9.2.90
 *
 * NB: this assumes we do 3 minor releases per year, before bumping major,
 * and dev snapshots / release candidates are numbered with micro >= 50
 * If this ever changes the logic below will need modifying....
 */
#define _MACHINE_VER_IS_EXPIRED_IMPL(cutoff, major, minor) \
    ((QEMU_VERSION_MICRO < 50 && \
      _MACHINE_VER_IS_CURRENT_EXPIRED(cutoff, major, minor)) || \
     (QEMU_VERSION_MICRO >= 50 && QEMU_VERSION_MINOR < 2 && \
      _MACHINE_VER_IS_NEXT_MINOR_EXPIRED(cutoff, major, minor)) || \
     (QEMU_VERSION_MICRO >= 50 && QEMU_VERSION_MINOR == 2 && \
      _MACHINE_VER_IS_NEXT_MAJOR_EXPIRED(cutoff, major, minor)))

#define _MACHINE_VER_IS_EXPIRED2(cutoff, major, minor) \
    _MACHINE_VER_IS_EXPIRED_IMPL(cutoff, major, minor)
#define _MACHINE_VER_IS_EXPIRED3(cutoff, major, minor, micro) \
    _MACHINE_VER_IS_EXPIRED_IMPL(cutoff, major, minor)
#define _MACHINE_VER_IS_EXPIRED4(cutoff, major, minor, _unused, tag) \
    _MACHINE_VER_IS_EXPIRED_IMPL(cutoff, major, minor)
#define _MACHINE_VER_IS_EXPIRED5(cutoff, major, minor, micro, _unused, tag)   \
    _MACHINE_VER_IS_EXPIRED_IMPL(cutoff, major, minor)

#define _MACHINE_IS_EXPIRED(cutoff, ...) \
    _MACHINE_VER_PICK(__VA_ARGS__, \
                      _MACHINE_VER_IS_EXPIRED5, \
                      _MACHINE_VER_IS_EXPIRED4, \
                      _MACHINE_VER_IS_EXPIRED3, \
                      _MACHINE_VER_IS_EXPIRED2) (cutoff, __VA_ARGS__)

/*
 * Evaluates true when a machine type with (major, minor)
 * or (major, minor, micro) version should be considered
 * deprecated based on the current versioned machine type
 * lifecycle rules
 */
#define MACHINE_VER_IS_DEPRECATED(...) \
    _MACHINE_IS_EXPIRED(MACHINE_VER_DEPRECATION_MAJOR, __VA_ARGS__)

/*
 * Evaluates true when a machine type with (major, minor)
 * or (major, minor, micro) version should be considered
 * for deletion based on the current versioned machine type
 * lifecycle rules
 */
#define MACHINE_VER_SHOULD_DELETE(...) \
    _MACHINE_IS_EXPIRED(MACHINE_VER_DELETION_MAJOR, __VA_ARGS__)

/*
 * Sets the deprecation reason for a versioned machine based
 * on its age
 *
 * This must be unconditionally used in the _class_init
 * function for all machine types which support versioning.
 *
 * Initially it will effectively be a no-op, but after a
 * suitable period of time has passed, it will set the
 * 'deprecation_reason' field on the machine, to warn users
 * about forthcoming removal.
 */
#define MACHINE_VER_DEPRECATION(...) \
    do { \
        if (MACHINE_VER_IS_DEPRECATED(__VA_ARGS__)) { \
            mc->deprecation_reason = MACHINE_VER_DEPRECATION_MSG; \
        } \
    } while (0)

/*
 * Prevents registration of a versioned machined based on
 * its age
 *
 * This must be unconditionally used in the register
 * method for all machine types which support versioning.
 *
 * Inijtially it will effectively be a no-op, but after a
 * suitable period of time has passed, it will cause
 * execution of the method to return, avoiding registration
 * of the machine
 */
#define MACHINE_VER_DELETION(...) \
    do { \
        if (MACHINE_VER_SHOULD_DELETE(__VA_ARGS__)) { \
            return; \
        } \
    } while (0)

#define DEFINE_MACHINE(namestr, machine_initfn) \
    static void machine_initfn##_class_init(ObjectClass *oc, const void *data) \
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

extern GlobalProperty hw_compat_10_0[];
extern const size_t hw_compat_10_0_len;

extern GlobalProperty hw_compat_9_2[];
extern const size_t hw_compat_9_2_len;

extern GlobalProperty hw_compat_9_1[];
extern const size_t hw_compat_9_1_len;

extern GlobalProperty hw_compat_9_0[];
extern const size_t hw_compat_9_0_len;

extern GlobalProperty hw_compat_8_2[];
extern const size_t hw_compat_8_2_len;

extern GlobalProperty hw_compat_8_1[];
extern const size_t hw_compat_8_1_len;

extern GlobalProperty hw_compat_8_0[];
extern const size_t hw_compat_8_0_len;

extern GlobalProperty hw_compat_7_2[];
extern const size_t hw_compat_7_2_len;

extern GlobalProperty hw_compat_7_1[];
extern const size_t hw_compat_7_1_len;

extern GlobalProperty hw_compat_7_0[];
extern const size_t hw_compat_7_0_len;

extern GlobalProperty hw_compat_6_2[];
extern const size_t hw_compat_6_2_len;

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

#endif
