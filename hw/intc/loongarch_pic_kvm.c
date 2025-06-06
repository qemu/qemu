/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch kvm pch pic interrupt support
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "hw/loongarch/virt.h"
#include "hw/pci-host/ls7a.h"
#include "system/kvm.h"

void kvm_pic_realize(DeviceState *dev, Error **errp)
{
    LoongarchPICState *lps = LOONGARCH_PIC(dev);
    uint64_t pch_pic_base = VIRT_PCH_REG_BASE;
    int ret;

    ret = kvm_create_device(kvm_state, KVM_DEV_TYPE_LOONGARCH_PCHPIC, false);
    if (ret < 0) {
        fprintf(stderr, "Create KVM_LOONGARCH_PCHPIC failed: %s\n",
                strerror(-ret));
        abort();
    }

    lps->dev_fd = ret;
    ret = kvm_device_access(lps->dev_fd, KVM_DEV_LOONGARCH_PCH_PIC_GRP_CTRL,
                            KVM_DEV_LOONGARCH_PCH_PIC_CTRL_INIT,
                            &pch_pic_base, true, NULL);
    if (ret < 0) {
        fprintf(stderr, "KVM_LOONGARCH_PCH_PIC_INIT failed: %s\n",
                strerror(-ret));
        abort();
    }
}
