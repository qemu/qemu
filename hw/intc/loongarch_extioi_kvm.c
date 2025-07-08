/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch EXTIOI interrupt kvm support
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/intc/loongarch_extioi.h"
#include "linux/kvm.h"
#include "qapi/error.h"
#include "system/kvm.h"

static void kvm_extioi_access_reg(int fd, uint64_t addr, void *val, bool write)
{
    kvm_device_access(fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_REGS,
                      addr, val, write, &error_abort);
}

static void kvm_extioi_access_sw_state(int fd, uint64_t addr,
                                       void *val, bool write)
{
    kvm_device_access(fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_SW_STATUS,
                      addr, val, write, &error_abort);
}

static void kvm_extioi_access_sw_status(void *opaque, bool write)
{
    LoongArchExtIOICommonState *lecs = LOONGARCH_EXTIOI_COMMON(opaque);
    LoongArchExtIOIState *les = LOONGARCH_EXTIOI(opaque);
    int addr;

    addr = KVM_DEV_LOONGARCH_EXTIOI_SW_STATUS_STATE;
    kvm_extioi_access_sw_state(les->dev_fd, addr, &lecs->status, write);
}

static void kvm_extioi_access_regs(void *opaque, bool write)
{
    LoongArchExtIOICommonState *lecs = LOONGARCH_EXTIOI_COMMON(opaque);
    LoongArchExtIOIState *les = LOONGARCH_EXTIOI(opaque);
    int fd = les->dev_fd;
    int addr, offset, cpu;

    for (addr = EXTIOI_NODETYPE_START; addr < EXTIOI_NODETYPE_END; addr += 4) {
        offset = (addr - EXTIOI_NODETYPE_START) / 4;
        kvm_extioi_access_reg(fd, addr, &lecs->nodetype[offset], write);
    }

    for (addr = EXTIOI_IPMAP_START; addr < EXTIOI_IPMAP_END; addr += 4) {
        offset = (addr - EXTIOI_IPMAP_START) / 4;
        kvm_extioi_access_reg(fd, addr, &lecs->ipmap[offset], write);
    }

    for (addr = EXTIOI_ENABLE_START; addr < EXTIOI_ENABLE_END; addr += 4) {
        offset = (addr - EXTIOI_ENABLE_START) / 4;
        kvm_extioi_access_reg(fd, addr, &lecs->enable[offset], write);
    }

    for (addr = EXTIOI_BOUNCE_START; addr < EXTIOI_BOUNCE_END; addr += 4) {
        offset = (addr - EXTIOI_BOUNCE_START) / 4;
        kvm_extioi_access_reg(fd, addr, &lecs->bounce[offset], write);
    }

    for (addr = EXTIOI_ISR_START; addr < EXTIOI_ISR_END; addr += 4) {
        offset = (addr - EXTIOI_ISR_START) / 4;
        kvm_extioi_access_reg(fd, addr, &lecs->isr[offset], write);
    }

    for (addr = EXTIOI_COREMAP_START; addr < EXTIOI_COREMAP_END; addr += 4) {
        offset = (addr - EXTIOI_COREMAP_START) / 4;
        kvm_extioi_access_reg(fd, addr, &lecs->coremap[offset], write);
    }

    for (cpu = 0; cpu < lecs->num_cpu; cpu++) {
        for (addr = EXTIOI_COREISR_START;
             addr < EXTIOI_COREISR_END; addr += 4) {
            offset = (addr - EXTIOI_COREISR_START) / 4;
            kvm_extioi_access_reg(fd, (cpu << 16) | addr,
                                  &lecs->cpu[cpu].coreisr[offset], write);
        }
    }
}

int kvm_extioi_get(void *opaque)
{
    kvm_extioi_access_regs(opaque, false);
    kvm_extioi_access_sw_status(opaque, false);
    return 0;
}

int kvm_extioi_put(void *opaque, int version_id)
{
    LoongArchExtIOIState *les = LOONGARCH_EXTIOI(opaque);
    int fd = les->dev_fd;

    if (fd == 0) {
        return 0;
    }

    kvm_extioi_access_regs(opaque, true);
    kvm_extioi_access_sw_status(opaque, true);
    kvm_device_access(fd, KVM_DEV_LOONGARCH_EXTIOI_GRP_CTRL,
                      KVM_DEV_LOONGARCH_EXTIOI_CTRL_LOAD_FINISHED,
                      NULL, true, &error_abort);
    return 0;
}

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
