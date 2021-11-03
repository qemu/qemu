#ifndef HW_PC_H
#define HW_PC_H

#include "qemu/notify.h"
#include "qapi/qapi-types-common.h"
#include "qemu/uuid.h"
#include "hw/boards.h"
#include "hw/block/fdc.h"
#include "hw/block/flash.h"
#include "hw/i386/x86.h"

#include "hw/acpi/acpi_dev_interface.h"
#include "hw/hotplug.h"
#include "qom/object.h"
#include "hw/i386/sgx-epc.h"

#define HPET_INTCAP "hpet-intcap"

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
    PCIBus *bus;
    I2CBus *smbus;
    PFlashCFI01 *flash[2];
    ISADevice *pcspk;
    DeviceState *iommu;

    /* Configuration options: */
    uint64_t max_ram_below_4g;
    OnOffAuto vmport;

    bool acpi_build_enabled;
    bool smbus_enabled;
    bool sata_enabled;
    bool pit_enabled;
    bool hpet_enabled;
    bool default_bus_bypass_iommu;
    uint64_t max_fw_size;

    /* ACPI Memory hotplug IO base address */
    hwaddr memhp_io_base;

    SGXEPCState sgx_epc;
} PCMachineState;

#define PC_MACHINE_ACPI_DEVICE_PROP "acpi-device"
#define PC_MACHINE_MAX_RAM_BELOW_4G "max-ram-below-4g"
#define PC_MACHINE_DEVMEM_REGION_SIZE "device-memory-region-size"
#define PC_MACHINE_VMPORT           "vmport"
#define PC_MACHINE_SMBUS            "smbus"
#define PC_MACHINE_SATA             "sata"
#define PC_MACHINE_PIT              "pit"
#define PC_MACHINE_MAX_FW_SIZE      "max-fw-size"
/**
 * PCMachineClass:
 *
 * Compat fields:
 *
 * @enforce_aligned_dimm: check that DIMM's address/size is aligned by
 *                        backend's alignment value if provided
 * @acpi_data_size: Size of the chunk of memory at the top of RAM
 *                  for the BIOS ACPI tables and other BIOS
 *                  datastructures.
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
    bool kvmclock_enabled;
    const char *default_nic_model;

    /* Compat options: */

    /* Default CPU model version.  See x86_cpu_set_default_version(). */
    int default_cpu_version;

    /* ACPI compat: */
    bool has_acpi_build;
    bool rsdp_in_ram;
    int legacy_acpi_table_size;
    unsigned acpi_data_size;
    bool do_not_add_smb_acpi;
    int pci_root_uid;

    /* SMBIOS compat: */
    bool smbios_defaults;
    bool smbios_legacy_mode;
    bool smbios_uuid_encoded;

    /* RAM / address space compat: */
    bool gigabyte_align;
    bool has_reserved_memory;
    bool enforce_aligned_dimm;
    bool broken_reserved_end;

    /* generate legacy CPU hotplug AML */
    bool legacy_cpu_hotplug;

    /* use PVH to load kernels that support this feature */
    bool pvh_enabled;

    /* create kvmclock device even when KVM PV features are not exposed */
    bool kvmclock_create_always;
};

#define TYPE_PC_MACHINE "generic-pc-machine"
OBJECT_DECLARE_TYPE(PCMachineState, PCMachineClass, PC_MACHINE)

/* ioapic.c */

GSIState *pc_gsi_create(qemu_irq **irqs, bool pci_enabled);

/* pc.c */
extern int fd_bootchk;

void pc_acpi_smi_interrupt(void *opaque, int irq, int level);

void pc_guest_info_init(PCMachineState *pcms);

#define PCI_HOST_PROP_PCI_HOLE_START   "pci-hole-start"
#define PCI_HOST_PROP_PCI_HOLE_END     "pci-hole-end"
#define PCI_HOST_PROP_PCI_HOLE64_START "pci-hole64-start"
#define PCI_HOST_PROP_PCI_HOLE64_END   "pci-hole64-end"
#define PCI_HOST_PROP_PCI_HOLE64_SIZE  "pci-hole64-size"
#define PCI_HOST_BELOW_4G_MEM_SIZE     "below-4g-mem-size"
#define PCI_HOST_ABOVE_4G_MEM_SIZE     "above-4g-mem-size"


void pc_pci_as_mapping_init(Object *owner, MemoryRegion *system_memory,
                            MemoryRegion *pci_address_space);

void xen_load_linux(PCMachineState *pcms);
void pc_memory_init(PCMachineState *pcms,
                    MemoryRegion *system_memory,
                    MemoryRegion *rom_memory,
                    MemoryRegion **ram_memory);
uint64_t pc_pci_hole64_start(void);
DeviceState *pc_vga_init(ISABus *isa_bus, PCIBus *pci_bus);
void pc_basic_device_init(struct PCMachineState *pcms,
                          ISABus *isa_bus, qemu_irq *gsi,
                          ISADevice **rtc_state,
                          bool create_fdctrl,
                          uint32_t hpet_irqs);
void pc_init_ne2k_isa(ISABus *bus, NICInfo *nd);
void pc_cmos_init(PCMachineState *pcms,
                  BusState *ide0, BusState *ide1,
                  ISADevice *s);
void pc_nic_init(PCMachineClass *pcmc, ISABus *isa_bus, PCIBus *pci_bus);
void pc_pci_device_init(PCIBus *pci_bus);

typedef void (*cpu_set_smm_t)(int smm, void *arg);

void pc_i8259_create(ISABus *isa_bus, qemu_irq *i8259_irqs);

ISADevice *pc_find_fdc0(void);

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

/* hw/i386/acpi-common.c */
void pc_madt_cpu_entry(AcpiDeviceIf *adev, int uid,
                       const CPUArchIdList *apic_ids, GArray *entry,
                       bool force_enabled);

/* sgx.c */
void pc_machine_init_sgx_epc(PCMachineState *pcms);

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

extern GlobalProperty pc_compat_2_5[];
extern const size_t pc_compat_2_5_len;

extern GlobalProperty pc_compat_2_4[];
extern const size_t pc_compat_2_4_len;

extern GlobalProperty pc_compat_2_3[];
extern const size_t pc_compat_2_3_len;

extern GlobalProperty pc_compat_2_2[];
extern const size_t pc_compat_2_2_len;

extern GlobalProperty pc_compat_2_1[];
extern const size_t pc_compat_2_1_len;

extern GlobalProperty pc_compat_2_0[];
extern const size_t pc_compat_2_0_len;

extern GlobalProperty pc_compat_1_7[];
extern const size_t pc_compat_1_7_len;

extern GlobalProperty pc_compat_1_6[];
extern const size_t pc_compat_1_6_len;

extern GlobalProperty pc_compat_1_5[];
extern const size_t pc_compat_1_5_len;

extern GlobalProperty pc_compat_1_4[];
extern const size_t pc_compat_1_4_len;

/* Helper for setting model-id for CPU models that changed model-id
 * depending on QEMU versions up to QEMU 2.4.
 */
#define PC_CPU_MODEL_IDS(v) \
    { "qemu32-" TYPE_X86_CPU, "model-id", "QEMU Virtual CPU version " v, },\
    { "qemu64-" TYPE_X86_CPU, "model-id", "QEMU Virtual CPU version " v, },\
    { "athlon-" TYPE_X86_CPU, "model-id", "QEMU Virtual CPU version " v, },

#define DEFINE_PC_MACHINE(suffix, namestr, initfn, optsfn) \
    static void pc_machine_##suffix##_class_init(ObjectClass *oc, void *data) \
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
        type_register(&pc_machine_type_##suffix); \
    } \
    type_init(pc_machine_init_##suffix)

extern void igd_passthrough_isa_bridge_create(PCIBus *bus, uint16_t gpu_dev_id);
#endif
