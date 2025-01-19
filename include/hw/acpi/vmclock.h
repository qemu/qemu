#ifndef ACPI_VMCLOCK_H
#define ACPI_VMCLOCK_H

#include "hw/acpi/bios-linker-loader.h"
#include "hw/qdev-core.h"
#include "qemu/uuid.h"
#include "qom/object.h"

#define TYPE_VMCLOCK    "vmclock"

#define VMCLOCK_ADDR    0xfeffb000
#define VMCLOCK_SIZE    0x1000

OBJECT_DECLARE_SIMPLE_TYPE(VmclockState, VMCLOCK)

struct vmclock_abi;

struct VmclockState {
    DeviceState parent_obj;
    MemoryRegion clk_page;
    uint64_t physaddr;
    struct vmclock_abi *clk;
};

/* returns NULL unless there is exactly one device */
static inline Object *find_vmclock_dev(void)
{
    return object_resolve_path_type("", TYPE_VMCLOCK, NULL);
}

void vmclock_build_acpi(VmclockState *vms, GArray *table_data,
                        BIOSLinker *linker, const char *oem_id);

#endif
