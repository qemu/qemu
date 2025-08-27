#ifndef HW_PC_H
#define HW_PC_H

#include "qemu/notify.h"
#include "qapi/qapi-types-common.h"
#include "qemu/uuid.h"
#include "hw/boards.h"
#include "hw/block/fdc.h"
#include "hw/block/flash.h"
#include "hw/i386/x86.h"

#include "hw/hotplug.h"
#include "qom/object.h"
#include "hw/i386/sgx-epc.h"
#include "hw/cxl/cxl.h"

#define MAX_IDE_BUS 2

/**
 * PCMachineState:
 * @acpi_dev: link to ACPI PM device that performs ACPI hotplug handling
 * @boot_cpus: number of present VCPUs
 */
typedef struct PCMachineState {
    /*< private >*/
    X86MachineState parent_obj;

    /* <public> */

    /* State for other subsystems/APIs: */
    Notifier machine_done;

    /* Pointers to devices and objects: */
    PCIBus *pcibus;
    I2CBus *smbus;
    PFlashCFI01 *flash[2];
    ISADevice *pcspk;
    DeviceState *iommu;
    BusState *idebus[MAX_IDE_BUS];

    /* Configuration options: */
    uint64_t max_ram_below_4g;
    OnOffAuto vmport;
    SmbiosEntryPointType smbios_entry_point_type;
    const char *south_bridge;

    bool acpi_build_enabled;
    bool smbus_enabled;
    bool sata_enabled;
    bool hpet_enabled;
    bool i8042_enabled;
    bool default_bus_bypass_iommu;
    bool fd_bootchk;
    uint64_t max_fw_size;

    /* ACPI Memory hotplug IO base address */
    hwaddr memhp_io_base;

    SGXEPCState sgx_epc;
    CXLState cxl_devices_state;
} PCMachineState;

#define PC_MACHINE_ACPI_DEVICE_PROP "acpi-device"
#define PC_MACHINE_MAX_RAM_BELOW_4G "max-ram-below-4g"
#define PC_MACHINE_VMPORT           "vmport"
#define PC_MACHINE_SMBUS            "smbus"
#define PC_MACHINE_SATA             "sata"
#define PC_MACHINE_I8042            "i8042"
#define PC_MACHINE_MAX_FW_SIZE      "max-fw-size"
#define PC_MACHINE_SMBIOS_EP        "smbios-entry-point-type"

/**
 * PCMachineClass:
 *
 * Compat fields:
 *
 * @gigabyte_align: Make sure that guest addresses aligned at
 *                  1Gbyte boundaries get mapped to host
 *                  addresses aligned at 1Gbyte boundaries. This
 *                  way we can use 1GByte pages in the host.
 *
 */
struct PCMachineClass {
    /*< private >*/
    X86MachineClass parent_class;

    /*< public >*/

    /* Device configuration: */
    bool pci_enabled;
    const char *default_south_bridge;

    /* Compat options: */

    /* Default CPU model version.  See x86_cpu_set_default_version(). */
    int default_cpu_version;

    /* ACPI compat: */
    bool has_acpi_build;
    int pci_root_uid;

    /* SMBIOS compat: */
    bool smbios_defaults;
    bool smbios_legacy_mode;
    SmbiosEntryPointType default_smbios_ep_type;

    /* RAM / address space compat: */
    bool gigabyte_align;
    bool has_reserved_memory;
    bool enforce_amd_1tb_hole;
    bool isa_bios_alias;

    /* generate legacy CPU hotplug AML */
    bool legacy_cpu_hotplug;

    /* use PVH to load kernels that support this feature */
    bool pvh_enabled;

    /* create kvmclock device even when KVM PV features are not exposed */
    bool kvmclock_create_always;

    /*
     * whether the machine type implements broken 32-bit address space bound
     * check for memory.
     */
    bool broken_32bit_mem_addr_check;
};

#define TYPE_PC_MACHINE "generic-pc-machine"
OBJECT_DECLARE_TYPE(PCMachineState, PCMachineClass, PC_MACHINE)

/* ioapic.c */

GSIState *pc_gsi_create(qemu_irq **irqs, bool pci_enabled);

/* pc.c */

void pc_acpi_smi_interrupt(void *opaque, int irq, int level);

#define PCI_HOST_PROP_RAM_MEM          "ram-mem"
#define PCI_HOST_PROP_PCI_MEM          "pci-mem"
#define PCI_HOST_PROP_SYSTEM_MEM       "system-mem"
#define PCI_HOST_PROP_IO_MEM           "io-mem"
#define PCI_HOST_PROP_PCI_HOLE_START   "pci-hole-start"
#define PCI_HOST_PROP_PCI_HOLE_END     "pci-hole-end"
#define PCI_HOST_PROP_PCI_HOLE64_START "pci-hole64-start"
#define PCI_HOST_PROP_PCI_HOLE64_END   "pci-hole64-end"
#define PCI_HOST_PROP_PCI_HOLE64_SIZE  "pci-hole64-size"
#define PCI_HOST_BELOW_4G_MEM_SIZE     "below-4g-mem-size"
#define PCI_HOST_ABOVE_4G_MEM_SIZE     "above-4g-mem-size"
#define PCI_HOST_PROP_SMM_RANGES       "smm-ranges"

typedef enum {
    SEV_DESC_TYPE_UNDEF,
    /* The section contains the region that must be validated by the VMM. */
    SEV_DESC_TYPE_SNP_SEC_MEM,
    /* The section contains the SNP secrets page */
    SEV_DESC_TYPE_SNP_SECRETS,
    /* The section contains address that can be used as a CPUID page */
    SEV_DESC_TYPE_CPUID,
    /* The section contains the region for kernel hashes for measured direct boot */
    SEV_DESC_TYPE_SNP_KERNEL_HASHES = 0x10,

} ovmf_sev_metadata_desc_type;

typedef struct __attribute__((__packed__)) OvmfSevMetadataDesc {
    uint32_t base;
    uint32_t len;
    ovmf_sev_metadata_desc_type type;
} OvmfSevMetadataDesc;

typedef struct __attribute__((__packed__)) OvmfSevMetadata {
    uint8_t signature[4];
    uint32_t len;
    uint32_t version;
    uint32_t num_desc;
    OvmfSevMetadataDesc descs[];
} OvmfSevMetadata;

OvmfSevMetadata *pc_system_get_ovmf_sev_metadata_ptr(void);

void pc_pci_as_mapping_init(MemoryRegion *system_memory,
                            MemoryRegion *pci_address_space);

void xen_load_linux(PCMachineState *pcms);
void pc_memory_init(PCMachineState *pcms,
                    MemoryRegion *system_memory,
                    MemoryRegion *rom_memory,
                    uint64_t pci_hole64_size);
uint64_t pc_pci_hole64_start(void);
DeviceState *pc_vga_init(ISABus *isa_bus, PCIBus *pci_bus);
void pc_basic_device_init(struct PCMachineState *pcms,
                          ISABus *isa_bus, qemu_irq *gsi,
                          ISADevice *rtc_state,
                          bool create_fdctrl,
                          uint32_t hpet_irqs);
void pc_nic_init(PCMachineClass *pcmc, ISABus *isa_bus, PCIBus *pci_bus);

void pc_i8259_create(ISABus *isa_bus, qemu_irq *i8259_irqs);

/* port92.c */
#define PORT92_A20_LINE "a20"

#define TYPE_PORT92 "port92"

/* pc_sysfw.c */
void pc_system_flash_create(PCMachineState *pcms);
void pc_system_flash_cleanup_unused(PCMachineState *pcms);
void pc_system_firmware_init(PCMachineState *pcms, MemoryRegion *rom_memory);
bool pc_system_ovmf_table_find(const char *entry, uint8_t **data,
                               int *data_len);
void pc_system_parse_ovmf_flash(uint8_t *flash_ptr, size_t flash_size);

/* sgx.c */
void pc_machine_init_sgx_epc(PCMachineState *pcms);

extern GlobalProperty pc_compat_10_1[];
extern const size_t pc_compat_10_1_len;

extern GlobalProperty pc_compat_10_0[];
extern const size_t pc_compat_10_0_len;

extern GlobalProperty pc_compat_9_2[];
extern const size_t pc_compat_9_2_len;

extern GlobalProperty pc_compat_9_1[];
extern const size_t pc_compat_9_1_len;

extern GlobalProperty pc_compat_9_0[];
extern const size_t pc_compat_9_0_len;

extern GlobalProperty pc_compat_8_2[];
extern const size_t pc_compat_8_2_len;

extern GlobalProperty pc_compat_8_1[];
extern const size_t pc_compat_8_1_len;

extern GlobalProperty pc_compat_8_0[];
extern const size_t pc_compat_8_0_len;

extern GlobalProperty pc_compat_7_2[];
extern const size_t pc_compat_7_2_len;

extern GlobalProperty pc_compat_7_1[];
extern const size_t pc_compat_7_1_len;

extern GlobalProperty pc_compat_7_0[];
extern const size_t pc_compat_7_0_len;

extern GlobalProperty pc_compat_6_2[];
extern const size_t pc_compat_6_2_len;

extern GlobalProperty pc_compat_6_1[];
extern const size_t pc_compat_6_1_len;

extern GlobalProperty pc_compat_6_0[];
extern const size_t pc_compat_6_0_len;

extern GlobalProperty pc_compat_5_2[];
extern const size_t pc_compat_5_2_len;

extern GlobalProperty pc_compat_5_1[];
extern const size_t pc_compat_5_1_len;

extern GlobalProperty pc_compat_5_0[];
extern const size_t pc_compat_5_0_len;

extern GlobalProperty pc_compat_4_2[];
extern const size_t pc_compat_4_2_len;

extern GlobalProperty pc_compat_4_1[];
extern const size_t pc_compat_4_1_len;

extern GlobalProperty pc_compat_4_0[];
extern const size_t pc_compat_4_0_len;

extern GlobalProperty pc_compat_3_1[];
extern const size_t pc_compat_3_1_len;

extern GlobalProperty pc_compat_3_0[];
extern const size_t pc_compat_3_0_len;

extern GlobalProperty pc_compat_2_12[];
extern const size_t pc_compat_2_12_len;

extern GlobalProperty pc_compat_2_11[];
extern const size_t pc_compat_2_11_len;

extern GlobalProperty pc_compat_2_10[];
extern const size_t pc_compat_2_10_len;

extern GlobalProperty pc_compat_2_9[];
extern const size_t pc_compat_2_9_len;

extern GlobalProperty pc_compat_2_8[];
extern const size_t pc_compat_2_8_len;

extern GlobalProperty pc_compat_2_7[];
extern const size_t pc_compat_2_7_len;

extern GlobalProperty pc_compat_2_6[];
extern const size_t pc_compat_2_6_len;

#define DEFINE_PC_MACHINE(suffix, namestr, initfn, optsfn) \
    static void pc_machine_##suffix##_class_init(ObjectClass *oc, \
                                                 const void *data) \
    { \
        MachineClass *mc = MACHINE_CLASS(oc); \
        optsfn(mc); \
        mc->init = initfn; \
    } \
    static const TypeInfo pc_machine_type_##suffix = { \
        .name       = namestr TYPE_MACHINE_SUFFIX, \
        .parent     = TYPE_PC_MACHINE, \
        .class_init = pc_machine_##suffix##_class_init, \
    }; \
    static void pc_machine_init_##suffix(void) \
    { \
        type_register_static(&pc_machine_type_##suffix); \
    } \
    type_init(pc_machine_init_##suffix)

#define DEFINE_PC_VER_MACHINE(namesym, namestr, initfn, isdefault, malias, ...) \
    static void MACHINE_VER_SYM(init, namesym, __VA_ARGS__)( \
        MachineState *machine) \
    { \
        initfn(machine); \
    } \
    static void MACHINE_VER_SYM(class_init, namesym, __VA_ARGS__)( \
        ObjectClass *oc, \
        const void *data) \
    { \
        MachineClass *mc = MACHINE_CLASS(oc); \
        MACHINE_VER_SYM(options, namesym, __VA_ARGS__)(mc); \
        mc->init = MACHINE_VER_SYM(init, namesym, __VA_ARGS__); \
        MACHINE_VER_DEPRECATION(__VA_ARGS__); \
        mc->is_default = isdefault; \
        mc->alias = malias; \
    } \
    static const TypeInfo MACHINE_VER_SYM(info, namesym, __VA_ARGS__) = \
    { \
        .name       = MACHINE_VER_TYPE_NAME(namestr, __VA_ARGS__), \
        .parent     = TYPE_PC_MACHINE, \
        .class_init = MACHINE_VER_SYM(class_init, namesym, __VA_ARGS__), \
    }; \
    static void MACHINE_VER_SYM(register, namesym, __VA_ARGS__)(void) \
    { \
        MACHINE_VER_DELETION(__VA_ARGS__); \
        type_register_static(&MACHINE_VER_SYM(info, namesym, __VA_ARGS__)); \
    } \
    type_init(MACHINE_VER_SYM(register, namesym, __VA_ARGS__));

#endif
