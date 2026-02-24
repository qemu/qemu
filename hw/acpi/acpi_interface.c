#include "qemu/osdep.h"
#include "hw/acpi/acpi_dev_interface.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "qemu/module.h"

static void register_types(void)
{
    static const TypeInfo acpi_dev_if_info = {
        .name          = TYPE_ACPI_DEVICE_IF,
        .parent        = TYPE_INTERFACE,
        .class_size = sizeof(AcpiDeviceIfClass),
    };
    static const TypeInfo acpi_dev_aml_if_info = {
        .name          = TYPE_ACPI_DEV_AML_IF,
        .parent        = TYPE_INTERFACE,
        .class_size = sizeof(AcpiDevAmlIfClass),
    };


    type_register_static(&acpi_dev_if_info);
    type_register_static(&acpi_dev_aml_if_info);
}

type_init(register_types)
