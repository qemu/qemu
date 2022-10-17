#ifndef ACPI_AML_INTERFACE_H
#define ACPI_AML_INTERFACE_H

#include "qom/object.h"
#include "hw/acpi/aml-build.h"

#define TYPE_ACPI_DEV_AML_IF "acpi-dev-aml-interface"
typedef struct AcpiDevAmlIfClass AcpiDevAmlIfClass;
DECLARE_CLASS_CHECKERS(AcpiDevAmlIfClass, ACPI_DEV_AML_IF, TYPE_ACPI_DEV_AML_IF)
#define ACPI_DEV_AML_IF(obj) \
     INTERFACE_CHECK(AcpiDevAmlIf, (obj), TYPE_ACPI_DEV_AML_IF)

typedef struct AcpiDevAmlIf AcpiDevAmlIf;
typedef void (*dev_aml_fn)(AcpiDevAmlIf *adev, Aml *scope);

/**
 * AcpiDevAmlIfClass:
 *
 * build_dev_aml: adds device specific AML blob to provided scope
 *
 * Interface is designed for providing generic callback that builds device
 * specific AML blob.
 */
struct AcpiDevAmlIfClass {
    /* <private> */
    InterfaceClass parent_class;

    /* <public> */
    dev_aml_fn build_dev_aml;
};

static inline dev_aml_fn get_dev_aml_func(DeviceState *dev)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_ACPI_DEV_AML_IF)) {
        AcpiDevAmlIfClass *klass = ACPI_DEV_AML_IF_GET_CLASS(dev);
        return klass->build_dev_aml;
    }
    return NULL;
}

static inline void call_dev_aml_func(DeviceState *dev, Aml *scope)
{
    dev_aml_fn fn = get_dev_aml_func(dev);
    if (fn) {
        fn(ACPI_DEV_AML_IF(dev), scope);
    }
}

#endif
