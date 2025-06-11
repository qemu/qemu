/*
 * VFIO based AP matrix device assignment
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Rorie Reyes <rreyes@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/s390x/ap-bridge.h"

int ap_chsc_sei_nt0_get_event(void *res)
{
    return EVENT_INFORMATION_NOT_STORED;
}

bool ap_chsc_sei_nt0_have_event(void)
{
    return false;
}
