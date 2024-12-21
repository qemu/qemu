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
#include "system/hvf.h"
#include "system/hvf_int.h"

const char *hvf_return_string(hv_return_t ret)
{
    switch (ret) {
    case HV_SUCCESS:      return "HV_SUCCESS";
    case HV_ERROR:        return "HV_ERROR";
    case HV_BUSY:         return "HV_BUSY";
    case HV_BAD_ARGUMENT: return "HV_BAD_ARGUMENT";
    case HV_NO_RESOURCES: return "HV_NO_RESOURCES";
    case HV_NO_DEVICE:    return "HV_NO_DEVICE";
    case HV_UNSUPPORTED:  return "HV_UNSUPPORTED";
    case HV_DENIED:       return "HV_DENIED";
    default:              return "[unknown hv_return value]";
    }
}

void assert_hvf_ok_impl(hv_return_t ret, const char *file, unsigned int line,
                        const char *exp)
{
    if (ret == HV_SUCCESS) {
        return;
    }

    error_report("Error: %s = %s (0x%x, at %s:%u)",
        exp, hvf_return_string(ret), ret, file, line);

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
