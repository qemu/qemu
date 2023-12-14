#include "qemu/osdep.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/pci/pci.h"
#include "vga_int.h"

void build_vga_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    int s3d = 0;
    Aml *method;

    if (object_dynamic_cast(OBJECT(adev), "qxl-vga")) {
        s3d = 3;
    }

    method = aml_method("_S1D", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_int(0)));
    aml_append(scope, method);

    method = aml_method("_S2D", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_int(0)));
    aml_append(scope, method);

    method = aml_method("_S3D", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_int(s3d)));
    aml_append(scope, method);
}
