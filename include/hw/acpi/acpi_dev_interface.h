#ifndef ACPI_DEV_INTERFACE_H
#define ACPI_DEV_INTERFACE_H

#include "qapi/qapi-types-acpi.h"
#include "qom/object.h"
#include "hw/boards.h"
#include "hw/qdev-core.h"

/* These values are part of guest ABI, and can not be changed */
typedef enum {
    ACPI_PCI_HOTPLUG_STATUS = 2,
    ACPI_CPU_HOTPLUG_STATUS = 4,
    ACPI_MEMORY_HOTPLUG_STATUS = 8,
    ACPI_NVDIMM_HOTPLUG_STATUS = 16,
    ACPI_VMGENID_CHANGE_STATUS = 32,
    ACPI_POWER_DOWN_STATUS = 64,
} AcpiEventStatusBits;

#define TYPE_ACPI_DEVICE_IF "acpi-device-interface"

typedef struct AcpiDeviceIfClass AcpiDeviceIfClass;
DECLARE_CLASS_CHECKERS(AcpiDeviceIfClass, ACPI_DEVICE_IF,
                       TYPE_ACPI_DEVICE_IF)
#define ACPI_DEVICE_IF(obj) \
     INTERFACE_CHECK(AcpiDeviceIf, (obj), \
                     TYPE_ACPI_DEVICE_IF)

typedef struct AcpiDeviceIf AcpiDeviceIf;

void acpi_send_event(DeviceState *dev, AcpiEventStatusBits event);

/**
 * AcpiDeviceIfClass:
 *
 * ospm_status: returns status of ACPI device objects, reported
 *              via _OST method if device supports it.
 * send_event: inject a specified event into guest
 * madt_cpu: fills @entry with Interrupt Controller Structure
 *           for CPU indexed by @uid in @apic_ids array,
 *           returned structure types are:
 *           0 - Local APIC, 9 - Local x2APIC, 0xB - GICC
 *
 * Interface is designed for providing unified interface
 * to generic ACPI functionality that could be used without
 * knowledge about internals of actual device that implements
 * ACPI interface.
 */
struct AcpiDeviceIfClass {
    /* <private> */
    InterfaceClass parent_class;

    /* <public> */
    void (*ospm_status)(AcpiDeviceIf *adev, ACPIOSTInfoList ***list);
    void (*send_event)(AcpiDeviceIf *adev, AcpiEventStatusBits ev);
    void (*madt_cpu)(int uid, const CPUArchIdList *apic_ids, GArray *entry,
                     bool force_enabled);
};
#endif
