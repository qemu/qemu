/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch IPI interrupt KVM support
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/loongarch_ipi.h"
#include "system/kvm.h"
#include "target/loongarch/cpu.h"

void kvm_ipi_realize(DeviceState *dev, Error **errp)
{
    LoongarchIPIState *lis = LOONGARCH_IPI(dev);
    int ret;

    ret = kvm_create_device(kvm_state, KVM_DEV_TYPE_LOONGARCH_IPI, false);
    if (ret < 0) {
        fprintf(stderr, "IPI KVM_CREATE_DEVICE failed: %s\n",
                strerror(-ret));
        abort();
    }

    lis->dev_fd = ret;
}
