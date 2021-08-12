#include "qemu/osdep.h"
#include "hw/acpi/cpu_hotplug.h"
#include "migration/vmstate.h"


/* Following stubs are all related to ACPI cpu hotplug */
const VMStateDescription vmstate_cpu_hotplug;

void acpi_switch_to_modern_cphp(AcpiCpuHotplug *gpe_cpu,
                                CPUHotplugState *cpuhp_state,
                                uint16_t io_port)
{
    return;
}

void legacy_acpi_cpu_hotplug_init(MemoryRegion *parent, Object *owner,
                                  AcpiCpuHotplug *gpe_cpu, uint16_t base)
{
    return;
}

void acpi_cpu_ospm_status(CPUHotplugState *cpu_st, ACPIOSTInfoList ***list)
{
    return;
}

void acpi_cpu_plug_cb(HotplugHandler *hotplug_dev,
                      CPUHotplugState *cpu_st, DeviceState *dev, Error **errp)
{
    return;
}

void legacy_acpi_cpu_plug_cb(HotplugHandler *hotplug_dev,
                             AcpiCpuHotplug *g, DeviceState *dev, Error **errp)
{
    return;
}

void acpi_cpu_unplug_cb(CPUHotplugState *cpu_st,
                        DeviceState *dev, Error **errp)
{
    return;
}

void acpi_cpu_unplug_request_cb(HotplugHandler *hotplug_dev,
                                CPUHotplugState *cpu_st,
                                DeviceState *dev, Error **errp)
{
    return;
}
