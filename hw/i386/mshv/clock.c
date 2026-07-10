/*
 * MSHV partition reference clock
 *
 * Copyright Microsoft, Corp. 2026
 *
 * Authors: Magnus Kulke <magnuskulke@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#include "system/runstate.h"
#include "hw/hyperv/hvhdk.h"
#include "hw/hyperv/hvhdk_mini.h"
#include "hw/hyperv/hvgdk.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "linux/mshv.h"
#include "system/mshv.h"
#include "system/mshv_int.h"

/*
 * Partition reference clock (HV_PARTITION_PROPERTY_REFERENCE_TIME), captured
 * when the VM is stopped and re-applied when it resumes.
 *
 * Mirrors hw/i386/kvm/clock.c.
 */
typedef struct MshvClockState {
    uint64_t ref_time;
    bool ref_time_pending;
} MshvClockState;

static MshvClockState mshv_clock;

static int mshv_get_reference_time(int vm_fd, uint64_t *ref_time)
{
    struct hv_input_get_partition_property in = { 0 };
    struct hv_output_get_partition_property out = { 0 };
    struct mshv_root_hvcall args = { 0 };
    int ret;

    in.property_code = HV_PARTITION_PROPERTY_REFERENCE_TIME;

    args.code        = HVCALL_GET_PARTITION_PROPERTY;
    args.in_sz       = sizeof(in);
    args.in_ptr      = (uint64_t)&in;
    args.out_sz      = sizeof(out);
    args.out_ptr     = (uint64_t)&out;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to get reference time");
        return -1;
    }

    *ref_time = out.property_value;
    return 0;
}

static int mshv_set_reference_time(int vm_fd, uint64_t ref_time)
{
    struct hv_input_set_partition_property in = { 0 };
    struct mshv_root_hvcall args = { 0 };
    int ret;

    in.property_code  = HV_PARTITION_PROPERTY_REFERENCE_TIME;
    in.property_value = ref_time;

    args.code         = HVCALL_SET_PARTITION_PROPERTY;
    args.in_sz        = sizeof(in);
    args.in_ptr       = (uint64_t)&in;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to set reference time");
        return -1;
    }

    return 0;
}

/*
 * Freeze (freeze=1) or unfreeze (freeze=0) time for a partition. This will not
 * pause/eject vCPU execution. It is assumed that the caller already has stopped
 * the partition's vCPUs.
 *
 * NB: a partition's reference clock can only be written while the time is
 * frozen.
 */
static int mshv_set_time_freeze(int vm_fd, int freeze)
{
    struct hv_input_set_partition_property in = { 0 };
    struct mshv_root_hvcall args = { 0 };
    int ret;

    in.property_code  = HV_PARTITION_PROPERTY_TIME_FREEZE;
    in.property_value = freeze;

    args.code         = HVCALL_SET_PARTITION_PROPERTY;
    args.in_sz        = sizeof(in);
    args.in_ptr       = (uint64_t)&in;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to set time freeze");
        return -1;
    }

    return 0;
}

static void mshv_clock_vm_state_change(void *opaque, bool running,
                                       RunState state)
{
    MshvClockState *s = opaque;
    int vm_fd = mshv_state->vm;
    int ret;

    if (running) {
        /* Skip if we have nothing to restore, e.g. on initial boot. */
        if (!s->ref_time_pending) {
            return;
        }

        ret = mshv_set_time_freeze(vm_fd, 1);
        if (ret < 0) {
            error_report("Failed to freeze partition time on resume");
            abort();
        }

        /* INVARIANT: reference time can only be written if time is frozen. */
        ret = mshv_set_reference_time(vm_fd, s->ref_time);
        if (ret < 0) {
            error_report("Failed to restore reference time on resume");
            abort();
        }

        if (mshv_set_time_freeze(vm_fd, 0) < 0) {
            error_report("Failed to unfreeze partition time on resume");
            abort();
        }

        s->ref_time_pending = false;
    } else {
        /* Skip if we already have a to-be-set ref time */
        if (s->ref_time_pending) {
            return;
        }

        ret = mshv_get_reference_time(vm_fd, &s->ref_time);
        if (ret < 0) {
            error_report("Failed to capture reference time on stop");
            abort();
        }

        s->ref_time_pending = true;
    }
}

/*
 * The incoming reference time should be applied on the next resume before
 * vCPUs start executing.
 */
static int mshv_clock_post_load(void *opaque, int version_id)
{
    MshvClockState *s = opaque;

    s->ref_time_pending = true;

    return 0;
}

static const VMStateDescription vmstate_mshv_clock = {
    .name = "mshv-clock",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = mshv_clock_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(ref_time, MshvClockState),
        VMSTATE_END_OF_LIST()
    },
};

void mshv_clock_init(void)
{
    vmstate_register(NULL, 0, &vmstate_mshv_clock, &mshv_clock);
    qemu_add_vm_change_state_handler(mshv_clock_vm_state_change, &mshv_clock);
}
