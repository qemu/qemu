#ifndef ACPI_DEV_INTERFACE_H
#define ACPI_DEV_INTERFACE_H

#include "qom/object.h"
#include "qapi-types.h"

/* These values are part of guest ABI, and can not be changed */
typedef enum {
    ACPI_PCI_HOTPLUG_STATUS = 2,
    ACPI_CPU_HOTPLUG_STATUS = 4,
    ACPI_MEMORY_HOTPLUG_STATUS = 8,
} AcpiEventStatusBits;

#define TYPE_ACPI_DEVICE_IF "acpi-device-interface"

#define ACPI_DEVICE_IF_CLASS(klass) \
     OBJECT_CLASS_CHECK(AcpiDeviceIfClass, (klass), \
                        TYPE_ACPI_DEVICE_IF)
#define ACPI_DEVICE_IF_GET_CLASS(obj) \
     OBJECT_GET_CLASS(AcpiDeviceIfClass, (obj), \
                      TYPE_ACPI_DEVICE_IF)
#define ACPI_DEVICE_IF(obj) \
     INTERFACE_CHECK(AcpiDeviceIf, (obj), \
                     TYPE_ACPI_DEVICE_IF)


typedef struct AcpiDeviceIf {
    /* <private> */
    Object Parent;
} AcpiDeviceIf;

void acpi_send_event(DeviceState *dev, AcpiEventStatusBits event);

/**
 * AcpiDeviceIfClass:
 *
 * ospm_status: returns status of ACPI device objects, reported
 *              via _OST method if device supports it.
 * send_event: inject a specified event into guest
 *
 * Interface is designed for providing unified interface
 * to generic ACPI functionality that could be used without
 * knowledge about internals of actual device that implements
 * ACPI interface.
 */
typedef struct AcpiDeviceIfClass {
    /* <private> */
    InterfaceClass parent_class;

    /* <public> */
    void (*ospm_status)(AcpiDeviceIf *adev, ACPIOSTInfoList ***list);
    void (*send_event)(AcpiDeviceIf *adev, AcpiEventStatusBits ev);
} AcpiDeviceIfClass;
#endif
