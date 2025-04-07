#include "qemu/osdep.h"
#include "hw/acpi/memory_hotplug.h"
#include "migration/vmstate.h"

const VMStateDescription vmstate_memory_hotplug;

void acpi_memory_hotplug_init(MemoryRegion *as, Object *owner,
                              MemHotplugState *state, hwaddr io_base)
{
}

void acpi_memory_ospm_status(MemHotplugState *mem_st, ACPIOSTInfoList ***list)
{
}

void acpi_memory_plug_cb(HotplugHandler *hotplug_dev, MemHotplugState *mem_st,
                         DeviceState *dev, Error **errp)
{
}

void acpi_memory_unplug_cb(MemHotplugState *mem_st,
                           DeviceState *dev, Error **errp)
{
}

void acpi_memory_unplug_request_cb(HotplugHandler *hotplug_dev,
                                   MemHotplugState *mem_st,
                                   DeviceState *dev, Error **errp)
{
}
