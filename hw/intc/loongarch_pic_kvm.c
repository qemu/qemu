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
#include "system/kvm.h"

static void kvm_pch_pic_access_reg(int fd, uint64_t addr, void *val, bool write)
{
    kvm_device_access(fd, KVM_DEV_LOONGARCH_PCH_PIC_GRP_REGS,
                      addr, val, write, &error_abort);
}

static void kvm_pch_pic_access(void *opaque, bool write)
{
    LoongArchPICCommonState *s = LOONGARCH_PIC_COMMON(opaque);
    LoongarchPICState *lps = LOONGARCH_PIC(opaque);
    int fd = lps->dev_fd;
    int addr, offset;

    if (fd == 0) {
        return;
    }

    kvm_pch_pic_access_reg(fd, PCH_PIC_INT_MASK, &s->int_mask, write);
    kvm_pch_pic_access_reg(fd, PCH_PIC_HTMSI_EN, &s->htmsi_en, write);
    kvm_pch_pic_access_reg(fd, PCH_PIC_INT_EDGE, &s->intedge, write);
    kvm_pch_pic_access_reg(fd, PCH_PIC_AUTO_CTRL0, &s->auto_crtl0, write);
    kvm_pch_pic_access_reg(fd, PCH_PIC_AUTO_CTRL1, &s->auto_crtl1, write);

    for (addr = PCH_PIC_ROUTE_ENTRY;
        addr < PCH_PIC_ROUTE_ENTRY_END; addr++) {
        offset = addr - PCH_PIC_ROUTE_ENTRY;
        kvm_pch_pic_access_reg(fd, addr, &s->route_entry[offset], write);
    }

    for (addr = PCH_PIC_HTMSI_VEC; addr < PCH_PIC_HTMSI_VEC_END; addr++) {
        offset = addr - PCH_PIC_HTMSI_VEC;
        kvm_pch_pic_access_reg(fd, addr, &s->htmsi_vector[offset], write);
    }

    kvm_pch_pic_access_reg(fd, PCH_PIC_INT_REQUEST, &s->intirr, write);
    kvm_pch_pic_access_reg(fd, PCH_PIC_INT_STATUS, &s->intisr, write);
    kvm_pch_pic_access_reg(fd, PCH_PIC_INT_POL, &s->int_polarity, write);
}

int kvm_pic_get(void *opaque)
{
    kvm_pch_pic_access(opaque, false);
    return 0;
}

int kvm_pic_put(void *opaque, int version_id)
{
    kvm_pch_pic_access(opaque, true);
    return 0;
}

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
