/*
 * QEMU HAXM support
 *
 * Copyright (c) 2011 Intel Corporation
 *  Written by:
 *  Jiang Yunhong<yunhong.jiang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* HAX module interface - darwin version */
#include "qemu/osdep.h"
#include <sys/ioctl.h>

#include "sysemu/cpus.h"
#include "hax-accel-ops.h"

hax_fd hax_mod_open(void)
{
    int fd = open("/dev/HAX", O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open the hax module\n");
    }

    qemu_set_cloexec(fd);

    return fd;
}

int hax_populate_ram(uint64_t va, uint64_t size)
{
    int ret;

    if (!hax_global.vm || !hax_global.vm->fd) {
        fprintf(stderr, "Allocate memory before vm create?\n");
        return -EINVAL;
    }

    if (hax_global.supports_64bit_ramblock) {
        struct hax_ramblock_info ramblock = {
            .start_va = va,
            .size = size,
            .reserved = 0
        };

        ret = ioctl(hax_global.vm->fd, HAX_VM_IOCTL_ADD_RAMBLOCK, &ramblock);
    } else {
        struct hax_alloc_ram_info info = {
            .size = (uint32_t)size,
            .pad = 0,
            .va = va
        };

        ret = ioctl(hax_global.vm->fd, HAX_VM_IOCTL_ALLOC_RAM, &info);
    }
    if (ret < 0) {
        fprintf(stderr, "Failed to register RAM block: ret=%d, va=0x%" PRIx64
                ", size=0x%" PRIx64 ", method=%s\n", ret, va, size,
                hax_global.supports_64bit_ramblock ? "new" : "legacy");
        return ret;
    }
    return 0;
}

int hax_set_ram(uint64_t start_pa, uint32_t size, uint64_t host_va, int flags)
{
    struct hax_set_ram_info info;
    int ret;

    info.pa_start = start_pa;
    info.size = size;
    info.va = host_va;
    info.flags = (uint8_t) flags;

    ret = ioctl(hax_global.vm->fd, HAX_VM_IOCTL_SET_RAM, &info);
    if (ret < 0) {
        return -errno;
    }
    return 0;
}

int hax_capability(struct hax_state *hax, struct hax_capabilityinfo *cap)
{
    int ret;

    ret = ioctl(hax->fd, HAX_IOCTL_CAPABILITY, cap);
    if (ret == -1) {
        fprintf(stderr, "Failed to get HAX capability\n");
        return -errno;
    }

    return 0;
}

int hax_mod_version(struct hax_state *hax, struct hax_module_version *version)
{
    int ret;

    ret = ioctl(hax->fd, HAX_IOCTL_VERSION, version);
    if (ret == -1) {
        fprintf(stderr, "Failed to get HAX version\n");
        return -errno;
    }

    return 0;
}

static char *hax_vm_devfs_string(int vm_id)
{
    return g_strdup_printf("/dev/hax_vm/vm%02d", vm_id);
}

static char *hax_vcpu_devfs_string(int vm_id, int vcpu_id)
{
    return g_strdup_printf("/dev/hax_vm%02d/vcpu%02d", vm_id, vcpu_id);
}

int hax_host_create_vm(struct hax_state *hax, int *vmid)
{
    int ret;
    int vm_id = 0;

    if (hax_invalid_fd(hax->fd)) {
        return -EINVAL;
    }

    if (hax->vm) {
        return 0;
    }

    ret = ioctl(hax->fd, HAX_IOCTL_CREATE_VM, &vm_id);
    *vmid = vm_id;
    return ret;
}

hax_fd hax_host_open_vm(struct hax_state *hax, int vm_id)
{
    hax_fd fd;
    char *vm_name = NULL;

    vm_name = hax_vm_devfs_string(vm_id);
    if (!vm_name) {
        return -1;
    }

    fd = open(vm_name, O_RDWR);
    g_free(vm_name);

    qemu_set_cloexec(fd);

    return fd;
}

int hax_notify_qemu_version(hax_fd vm_fd, struct hax_qemu_version *qversion)
{
    int ret;

    if (hax_invalid_fd(vm_fd)) {
        return -EINVAL;
    }

    ret = ioctl(vm_fd, HAX_VM_IOCTL_NOTIFY_QEMU_VERSION, qversion);

    if (ret < 0) {
        fprintf(stderr, "Failed to notify qemu API version\n");
        return ret;
    }
    return 0;
}

/* Simply assume the size should be bigger than the hax_tunnel,
 * since the hax_tunnel can be extended later with compatibility considered
 */
int hax_host_create_vcpu(hax_fd vm_fd, int vcpuid)
{
    int ret;

    ret = ioctl(vm_fd, HAX_VM_IOCTL_VCPU_CREATE, &vcpuid);
    if (ret < 0) {
        fprintf(stderr, "Failed to create vcpu %x\n", vcpuid);
    }

    return ret;
}

hax_fd hax_host_open_vcpu(int vmid, int vcpuid)
{
    char *devfs_path = NULL;
    hax_fd fd;

    devfs_path = hax_vcpu_devfs_string(vmid, vcpuid);
    if (!devfs_path) {
        fprintf(stderr, "Failed to get the devfs\n");
        return -EINVAL;
    }

    fd = open(devfs_path, O_RDWR);
    g_free(devfs_path);
    if (fd < 0) {
        fprintf(stderr, "Failed to open the vcpu devfs\n");
    }
    qemu_set_cloexec(fd);
    return fd;
}

int hax_host_setup_vcpu_channel(AccelCPUState *vcpu)
{
    int ret;
    struct hax_tunnel_info info;

    ret = ioctl(vcpu->fd, HAX_VCPU_IOCTL_SETUP_TUNNEL, &info);
    if (ret) {
        fprintf(stderr, "Failed to setup the hax tunnel\n");
        return ret;
    }

    if (!valid_hax_tunnel_size(info.size)) {
        fprintf(stderr, "Invalid hax tunnel size %x\n", info.size);
        ret = -EINVAL;
        return ret;
    }

    vcpu->tunnel = (struct hax_tunnel *) (intptr_t) (info.va);
    vcpu->iobuf = (unsigned char *) (intptr_t) (info.io_va);
    return 0;
}

int hax_vcpu_run(AccelCPUState *vcpu)
{
    return ioctl(vcpu->fd, HAX_VCPU_IOCTL_RUN, NULL);
}

int hax_sync_fpu(CPUArchState *env, struct fx_layout *fl, int set)
{
    int ret, fd;

    fd = hax_vcpu_get_fd(env);
    if (fd <= 0) {
        return -1;
    }

    if (set) {
        ret = ioctl(fd, HAX_VCPU_IOCTL_SET_FPU, fl);
    } else {
        ret = ioctl(fd, HAX_VCPU_IOCTL_GET_FPU, fl);
    }
    return ret;
}

int hax_sync_msr(CPUArchState *env, struct hax_msr_data *msrs, int set)
{
    int ret, fd;

    fd = hax_vcpu_get_fd(env);
    if (fd <= 0) {
        return -1;
    }
    if (set) {
        ret = ioctl(fd, HAX_VCPU_IOCTL_SET_MSRS, msrs);
    } else {
        ret = ioctl(fd, HAX_VCPU_IOCTL_GET_MSRS, msrs);
    }
    return ret;
}

int hax_sync_vcpu_state(CPUArchState *env, struct vcpu_state_t *state, int set)
{
    int ret, fd;

    fd = hax_vcpu_get_fd(env);
    if (fd <= 0) {
        return -1;
    }

    if (set) {
        ret = ioctl(fd, HAX_VCPU_SET_REGS, state);
    } else {
        ret = ioctl(fd, HAX_VCPU_GET_REGS, state);
    }
    return ret;
}

int hax_inject_interrupt(CPUArchState *env, int vector)
{
    int fd;

    fd = hax_vcpu_get_fd(env);
    if (fd <= 0) {
        return -1;
    }

    return ioctl(fd, HAX_VCPU_IOCTL_INTERRUPT, &vector);
}

void hax_kick_vcpu_thread(CPUState *cpu)
{
    /*
     * FIXME: race condition with the exit_request check in
     * hax_vcpu_hax_exec
     */
    cpu->exit_request = 1;
    cpus_kick_thread(cpu);
}
