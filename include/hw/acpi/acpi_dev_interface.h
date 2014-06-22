#ifndef ACPI_DEV_INTERFACE_H
#define ACPI_DEV_INTERFACE_H

#include "qom/object.h"
#include "qapi-types.h"

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

/**
 * AcpiDeviceIfClass:
 *
 * ospm_status: returns status of ACPI device objects, reported
 *              via _OST method if device supports it.
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
} AcpiDeviceIfClass;
#endif
