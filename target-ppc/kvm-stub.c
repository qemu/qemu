/*
 * QEMU KVM PPC specific function stubs
 *
 * Copyright Freescale Inc. 2013
 *
 * Author: Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/ppc/openpic.h"

int kvm_openpic_connect_vcpu(DeviceState *d, CPUState *cs)
{
    return -EINVAL;
}
