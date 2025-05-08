/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"

#include "tdx.h"

int tdx_pre_create_vcpu(CPUState *cpu, Error **errp)
{
    return -EINVAL;
}

int tdx_parse_tdvf(void *flash_ptr, int size)
{
    return -EINVAL;
}
