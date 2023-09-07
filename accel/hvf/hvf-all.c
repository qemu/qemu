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
#if defined(MAC_OS_VERSION_11_0) && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0
    case HV_DENIED:
        error_report("Error: HV_DENIED");
        break;
#endif
    default:
        error_report("Unknown Error");
    }

    abort();
}

struct hvf_sw_breakpoint *hvf_find_sw_breakpoint(CPUState *cpu, vaddr pc)
{
    struct hvf_sw_breakpoint *bp;

    QTAILQ_FOREACH(bp, &hvf_state->hvf_sw_breakpoints, entry) {
        if (bp->pc == pc) {
            return bp;
        }
    }
    return NULL;
}

int hvf_sw_breakpoints_active(CPUState *cpu)
{
    return !QTAILQ_EMPTY(&hvf_state->hvf_sw_breakpoints);
}

int hvf_update_guest_debug(CPUState *cpu)
{
    hvf_arch_update_guest_debug(cpu);
    return 0;
}
