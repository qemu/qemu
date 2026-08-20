/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch DINTC interrupt kvm support
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/intc/loongarch_dintc.h"
#include "linux/kvm.h"
#include "qapi/error.h"
#include "system/kvm.h"

void kvm_dintc_realize(DeviceState *dev, Error **errp)
{
    LoongArchDINTCState *lds = LOONGARCH_DINTC(dev);
    int ret;

    ret = kvm_create_device(kvm_state, KVM_DEV_TYPE_LOONGARCH_DMSINTC, false);
    if (ret < 0) {
        fprintf(stderr, "create KVM_DEV_TYPE_LOONGARCH_AVEC failed: %s\n",
               strerror(-ret));
        abort();
    }
    lds->dev_fd = ret;

    /* init dintc config */
    lds->msg_addr_base = VIRT_DINTC_BASE;
    lds->msg_addr_size = VIRT_DINTC_SIZE;

    ret = kvm_device_access(lds->dev_fd, KVM_DEV_LOONGARCH_DMSINTC_GRP_CTRL,
                            KVM_DEV_LOONGARCH_DMSINTC_MSG_ADDR_BASE,
                            &lds->msg_addr_base, true, NULL);
    if (ret < 0) {
        fprintf(stderr, "KVM_DEV_LOONGARCH_DINTC_MSG_ADDR_BASE failed: %s\n",
                strerror(ret));
        abort();
    }

    ret = kvm_device_access(lds->dev_fd, KVM_DEV_LOONGARCH_DMSINTC_GRP_CTRL,
                            KVM_DEV_LOONGARCH_DMSINTC_MSG_ADDR_SIZE,
                            &lds->msg_addr_size, true, NULL);
    if (ret < 0) {
        fprintf(stderr, "KVM_DEV_LOONGARCH_DINTC_MSG_ADDR_SIZE failed: %s\n",
                strerror(ret));
        abort();
    }
}
