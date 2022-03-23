/*
 * QEMU Hypervisor.framework support
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/hvf.h"
#include "sysemu/hvf_int.h"

void assert_hvf_ok(hv_return_t ret)
{
    if (ret == HV_SUCCESS) {
        return;
    }

    switch (ret) {
    case HV_ERROR:
        error_report("Error: HV_ERROR");
        break;
    case HV_BUSY:
        error_report("Error: HV_BUSY");
        break;
    case HV_BAD_ARGUMENT:
        error_report("Error: HV_BAD_ARGUMENT");
        break;
    case HV_NO_RESOURCES:
        error_report("Error: HV_NO_RESOURCES");
        break;
    case HV_NO_DEVICE:
        error_report("Error: HV_NO_DEVICE");
        break;
    case HV_UNSUPPORTED:
        error_report("Error: HV_UNSUPPORTED");
        break;
    default:
        error_report("Unknown Error");
    }

    abort();
}
