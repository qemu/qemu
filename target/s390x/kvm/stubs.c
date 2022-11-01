/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "kvm_s390x.h"

int kvm_s390_get_protected_dump(void)
{
    return false;
}
