/*
 * QEMU Hyper-V support
 *
 * Copyright Red Hat, Inc. 2011
 *
 * Author: Vadim Rozenfeld     <vrozenfe@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "hyperv.h"

static bool hyperv_vapic;
static bool hyperv_relaxed_timing;
static int hyperv_spinlock_attempts = HYPERV_SPINLOCK_NEVER_RETRY;

void hyperv_enable_vapic_recommended(bool val)
{
    hyperv_vapic = val;
}

void hyperv_enable_relaxed_timing(bool val)
{
    hyperv_relaxed_timing = val;
}

void hyperv_set_spinlock_retries(int val)
{
    hyperv_spinlock_attempts = val;
    if (hyperv_spinlock_attempts < 0xFFF) {
        hyperv_spinlock_attempts = 0xFFF;
    }
}

bool hyperv_enabled(void)
{
    return hyperv_hypercall_available() || hyperv_relaxed_timing_enabled();
}

bool hyperv_hypercall_available(void)
{
    if (hyperv_vapic ||
        (hyperv_spinlock_attempts != HYPERV_SPINLOCK_NEVER_RETRY)) {
      return true;
    }
    return false;
}

bool hyperv_vapic_recommended(void)
{
    return hyperv_vapic;
}

bool hyperv_relaxed_timing_enabled(void)
{
    return hyperv_relaxed_timing;
}

int hyperv_get_spinlock_retries(void)
{
    return hyperv_spinlock_attempts;
}
