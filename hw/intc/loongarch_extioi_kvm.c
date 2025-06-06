/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch EXTIOI interrupt kvm support
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/typedefs.h"
#include "hw/intc/loongarch_extioi.h"
#include "linux/kvm.h"
#include "qapi/error.h"
#include "system/kvm.h"

void kvm_extioi_realize(DeviceState *dev, Error **errp)
{
    LoongArchExtIOICommonState *lecs = LOONGARCH_EXTIOI_COMMON(dev);
    LoongArchExtIOIState *les = LOONGARCH_EXTIOI(dev);
    int ret;

    ret = kvm_create_device(kvm_state, KVM_DEV_TYPE_LOONGARCH_EIOINTC, false);
    if (ret < 0) {
        fprintf(stderr, "create KVM_LOONGARCH_EIOINTC failed: %s\n",
                strerror(-ret));
        abort();
    }

    les->dev_fd = ret;
    ret = kvm_device_access(les->dev_fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_CTRL,
                            KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_NUM_CPU,
                            &lecs->num_cpu, true, NULL);
    if (ret < 0) {
        fprintf(stderr, "KVM_LOONGARCH_EXTIOI_INIT_NUM_CPU failed: %s\n",
                strerror(-ret));
        abort();
    }

    ret = kvm_device_access(les->dev_fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_CTRL,
                            KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_FEATURE,
                            &lecs->features, true, NULL);
    if (ret < 0) {
        fprintf(stderr, "KVM_LOONGARCH_EXTIOI_INIT_FEATURE failed: %s\n",
                strerror(-ret));
        abort();
    }
}
