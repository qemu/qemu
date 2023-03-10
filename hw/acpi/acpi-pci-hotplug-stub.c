#include "qemu/osdep.h"
#include "hw/acpi/pcihp.h"
#include "migration/vmstate.h"

const VMStateDescription vmstate_acpi_pcihp_pci_status;

void acpi_pcihp_init(Object *owner, AcpiPciHpState *s, PCIBus *root_bus,
                     MemoryRegion *address_space_io, uint16_t io_base)
{
    return;
}

void acpi_pcihp_device_plug_cb(HotplugHandler *hotplug_dev, AcpiPciHpState *s,
                               DeviceState *dev, Error **errp)
{
    return;
}

void acpi_pcihp_device_pre_plug_cb(HotplugHandler *hotplug_dev,
                                   DeviceState *dev, Error **errp)
{
    return;
}

void acpi_pcihp_device_unplug_cb(HotplugHandler *hotplug_dev, AcpiPciHpState *s,
                                 DeviceState *dev, Error **errp)
{
    return;
}

void acpi_pcihp_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                         AcpiPciHpState *s, DeviceState *dev,
                                         Error **errp)
{
    return;
}

void acpi_pcihp_reset(AcpiPciHpState *s)
{
    return;
}

bool acpi_pcihp_is_hotpluggbale_bus(AcpiPciHpState *s, BusState *bus)
{
    return true;
}
