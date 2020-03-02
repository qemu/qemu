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

#include "qemu/osdep.h"
#include "cpu.h"
#include "hax-i386.h"

/*
 * return 0 when success, -1 when driver not loaded,
 * other negative value for other failure
 */
static int hax_open_device(hax_fd *fd)
{
    uint32_t errNum = 0;
    HANDLE hDevice;

    if (!fd) {
        return -2;
    }

    hDevice = CreateFile("\\\\.\\HAX",
                         GENERIC_READ | GENERIC_WRITE,
                         0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open the HAX device!\n");
        errNum = GetLastError();
        if (errNum == ERROR_FILE_NOT_FOUND) {
            return -1;
        }
        return -2;
    }
    *fd = hDevice;
    return 0;
}

/* hax_fd hax_mod_open */
 hax_fd hax_mod_open(void)
{
    int ret;
    hax_fd fd = NULL;

    ret = hax_open_device(&fd);
    if (ret != 0) {
        fprintf(stderr, "Open HAX device failed\n");
    }

    return fd;
}

int hax_populate_ram(uint64_t va, uint64_t size)
{
    int ret;
    HANDLE hDeviceVM;
    DWORD dSize = 0;

    if (!hax_global.vm || !hax_global.vm->fd) {
        fprintf(stderr, "Allocate memory before vm create?\n");
        return -EINVAL;
    }

    hDeviceVM = hax_global.vm->fd;
    if (hax_global.supports_64bit_ramblock) {
        struct hax_ramblock_info ramblock = {
            .start_va = va,
            .size = size,
            .reserved = 0
        };

        ret = DeviceIoControl(hDeviceVM,
                              HAX_VM_IOCTL_ADD_RAMBLOCK,
                              &ramblock, sizeof(ramblock), NULL, 0, &dSize,
                              (LPOVERLAPPED) NULL);
    } else {
        struct hax_alloc_ram_info info = {
            .size = (uint32_t) size,
            .pad = 0,
            .va = va
        };

        ret = DeviceIoControl(hDeviceVM,
                              HAX_VM_IOCTL_ALLOC_RAM,
                              &info, sizeof(info), NULL, 0, &dSize,
                              (LPOVERLAPPED) NULL);
    }

    if (!ret) {
        fprintf(stderr, "Failed to register RAM block: va=0x%" PRIx64
                ", size=0x%" PRIx64 ", method=%s\n", va, size,
                hax_global.supports_64bit_ramblock ? "new" : "legacy");
        return ret;
    }

    return 0;
}

int hax_set_ram(uint64_t start_pa, uint32_t size, uint64_t host_va, int flags)
{
    struct hax_set_ram_info info;
    HANDLE hDeviceVM = hax_global.vm->fd;
    DWORD dSize = 0;
    int ret;

    info.pa_start = start_pa;
    info.size = size;
    info.va = host_va;
    info.flags = (uint8_t) flags;

    ret = DeviceIoControl(hDeviceVM, HAX_VM_IOCTL_SET_RAM,
                          &info, sizeof(info), NULL, 0, &dSize,
                          (LPOVERLAPPED) NULL);

    if (!ret) {
        return -EFAULT;
    } else {
        return 0;
    }
}

int hax_capability(struct hax_state *hax, struct hax_capabilityinfo *cap)
{
    int ret;
    HANDLE hDevice = hax->fd;        /* handle to hax module */
    DWORD dSize = 0;
    DWORD err = 0;

    if (hax_invalid_fd(hDevice)) {
        fprintf(stderr, "Invalid fd for hax device!\n");
        return -ENODEV;
    }

    ret = DeviceIoControl(hDevice, HAX_IOCTL_CAPABILITY, NULL, 0, cap,
                          sizeof(*cap), &dSize, (LPOVERLAPPED) NULL);

    if (!ret) {
        err = GetLastError();
        if (err == ERROR_INSUFFICIENT_BUFFER || err == ERROR_MORE_DATA) {
            fprintf(stderr, "hax capability is too long to hold.\n");
        }
        fprintf(stderr, "Failed to get Hax capability:%luu\n", err);
        return -EFAULT;
    } else {
        return 0;
    }
}

int hax_mod_version(struct hax_state *hax, struct hax_module_version *version)
{
    int ret;
    HANDLE hDevice = hax->fd; /* handle to hax module */
    DWORD dSize = 0;
    DWORD err = 0;

    if (hax_invalid_fd(hDevice)) {
        fprintf(stderr, "Invalid fd for hax device!\n");
        return -ENODEV;
    }

    ret = DeviceIoControl(hDevice,
                          HAX_IOCTL_VERSION,
                          NULL, 0,
                          version, sizeof(*version), &dSize,
                          (LPOVERLAPPED) NULL);

    if (!ret) {
        err = GetLastError();
        if (err == ERROR_INSUFFICIENT_BUFFER || err == ERROR_MORE_DATA) {
            fprintf(stderr, "hax module verion is too long to hold.\n");
        }
        fprintf(stderr, "Failed to get Hax module version:%lu\n", err);
        return -EFAULT;
    } else {
        return 0;
    }
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
    DWORD dSize = 0;

    if (hax_invalid_fd(hax->fd)) {
        return -EINVAL;
    }

    if (hax->vm) {
        return 0;
    }

    ret = DeviceIoControl(hax->fd,
                          HAX_IOCTL_CREATE_VM,
                          NULL, 0, &vm_id, sizeof(vm_id), &dSize,
                          (LPOVERLAPPED) NULL);
    if (!ret) {
        fprintf(stderr, "Failed to create VM. Error code: %lu\n",
                GetLastError());
        return -1;
    }
    *vmid = vm_id;
    return 0;
}

hax_fd hax_host_open_vm(struct hax_state *hax, int vm_id)
{
    char *vm_name = NULL;
    hax_fd hDeviceVM;

    vm_name = hax_vm_devfs_string(vm_id);
    if (!vm_name) {
        fprintf(stderr, "Failed to open VM. VM name is null\n");
        return INVALID_HANDLE_VALUE;
    }

    hDeviceVM = CreateFile(vm_name,
                           GENERIC_READ | GENERIC_WRITE,
                           0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDeviceVM == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Open the vm device error:%s, ec:%lu\n",
                vm_name, GetLastError());
    }

    g_free(vm_name);
    return hDeviceVM;
}

int hax_notify_qemu_version(hax_fd vm_fd, struct hax_qemu_version *qversion)
{
    int ret;
    DWORD dSize = 0;
    if (hax_invalid_fd(vm_fd)) {
        return -EINVAL;
    }
    ret = DeviceIoControl(vm_fd,
                          HAX_VM_IOCTL_NOTIFY_QEMU_VERSION,
                          qversion, sizeof(struct hax_qemu_version),
                          NULL, 0, &dSize, (LPOVERLAPPED) NULL);
    if (!ret) {
        fprintf(stderr, "Failed to notify qemu API version\n");
        return -1;
    }
    return 0;
}

int hax_host_create_vcpu(hax_fd vm_fd, int vcpuid)
{
    int ret;
    DWORD dSize = 0;

    ret = DeviceIoControl(vm_fd,
                          HAX_VM_IOCTL_VCPU_CREATE,
                          &vcpuid, sizeof(vcpuid), NULL, 0, &dSize,
                          (LPOVERLAPPED) NULL);
    if (!ret) {
        fprintf(stderr, "Failed to create vcpu %x\n", vcpuid);
        return -1;
    }

    return 0;
}

hax_fd hax_host_open_vcpu(int vmid, int vcpuid)
{
    char *devfs_path = NULL;
    hax_fd hDeviceVCPU;

    devfs_path = hax_vcpu_devfs_string(vmid, vcpuid);
    if (!devfs_path) {
        fprintf(stderr, "Failed to get the devfs\n");
        return INVALID_HANDLE_VALUE;
    }

    hDeviceVCPU = CreateFile(devfs_path,
                             GENERIC_READ | GENERIC_WRITE,
                             0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                             NULL);

    if (hDeviceVCPU == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open the vcpu devfs\n");
    }
    g_free(devfs_path);
    return hDeviceVCPU;
}

int hax_host_setup_vcpu_channel(struct hax_vcpu_state *vcpu)
{
    hax_fd hDeviceVCPU = vcpu->fd;
    int ret;
    struct hax_tunnel_info info;
    DWORD dSize = 0;

    ret = DeviceIoControl(hDeviceVCPU,
                          HAX_VCPU_IOCTL_SETUP_TUNNEL,
                          NULL, 0, &info, sizeof(info), &dSize,
                          (LPOVERLAPPED) NULL);
    if (!ret) {
        fprintf(stderr, "Failed to setup the hax tunnel\n");
        return -1;
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

int hax_vcpu_run(struct hax_vcpu_state *vcpu)
{
    int ret;
    HANDLE hDeviceVCPU = vcpu->fd;
    DWORD dSize = 0;

    ret = DeviceIoControl(hDeviceVCPU,
                          HAX_VCPU_IOCTL_RUN,
                          NULL, 0, NULL, 0, &dSize, (LPOVERLAPPED) NULL);
    if (!ret) {
        return -EFAULT;
    } else {
        return 0;
    }
}

int hax_sync_fpu(CPUArchState *env, struct fx_layout *fl, int set)
{
    int ret;
    hax_fd fd;
    HANDLE hDeviceVCPU;
    DWORD dSize = 0;

    fd = hax_vcpu_get_fd(env);
    if (hax_invalid_fd(fd)) {
        return -1;
    }

    hDeviceVCPU = fd;

    if (set) {
        ret = DeviceIoControl(hDeviceVCPU,
                              HAX_VCPU_IOCTL_SET_FPU,
                              fl, sizeof(*fl), NULL, 0, &dSize,
                              (LPOVERLAPPED) NULL);
    } else {
        ret = DeviceIoControl(hDeviceVCPU,
                              HAX_VCPU_IOCTL_GET_FPU,
                              NULL, 0, fl, sizeof(*fl), &dSize,
                              (LPOVERLAPPED) NULL);
    }
    if (!ret) {
        return -EFAULT;
    } else {
        return 0;
    }
}

int hax_sync_msr(CPUArchState *env, struct hax_msr_data *msrs, int set)
{
    int ret;
    hax_fd fd;
    HANDLE hDeviceVCPU;
    DWORD dSize = 0;

    fd = hax_vcpu_get_fd(env);
    if (hax_invalid_fd(fd)) {
        return -1;
    }
    hDeviceVCPU = fd;

    if (set) {
        ret = DeviceIoControl(hDeviceVCPU,
                              HAX_VCPU_IOCTL_SET_MSRS,
                              msrs, sizeof(*msrs),
                              msrs, sizeof(*msrs), &dSize, (LPOVERLAPPED) NULL);
    } else {
        ret = DeviceIoControl(hDeviceVCPU,
                              HAX_VCPU_IOCTL_GET_MSRS,
                              msrs, sizeof(*msrs),
                              msrs, sizeof(*msrs), &dSize, (LPOVERLAPPED) NULL);
    }
    if (!ret) {
        return -EFAULT;
    } else {
        return 0;
    }
}

int hax_sync_vcpu_state(CPUArchState *env, struct vcpu_state_t *state, int set)
{
    int ret;
    hax_fd fd;
    HANDLE hDeviceVCPU;
    DWORD dSize;

    fd = hax_vcpu_get_fd(env);
    if (hax_invalid_fd(fd)) {
        return -1;
    }

    hDeviceVCPU = fd;

    if (set) {
        ret = DeviceIoControl(hDeviceVCPU,
                              HAX_VCPU_SET_REGS,
                              state, sizeof(*state),
                              NULL, 0, &dSize, (LPOVERLAPPED) NULL);
    } else {
        ret = DeviceIoControl(hDeviceVCPU,
                              HAX_VCPU_GET_REGS,
                              NULL, 0,
                              state, sizeof(*state), &dSize,
                              (LPOVERLAPPED) NULL);
    }
    if (!ret) {
        return -EFAULT;
    } else {
        return 0;
    }
}

int hax_inject_interrupt(CPUArchState *env, int vector)
{
    int ret;
    hax_fd fd;
    HANDLE hDeviceVCPU;
    DWORD dSize;

    fd = hax_vcpu_get_fd(env);
    if (hax_invalid_fd(fd)) {
        return -1;
    }

    hDeviceVCPU = fd;

    ret = DeviceIoControl(hDeviceVCPU,
                          HAX_VCPU_IOCTL_INTERRUPT,
                          &vector, sizeof(vector), NULL, 0, &dSize,
                          (LPOVERLAPPED) NULL);
    if (!ret) {
        return -EFAULT;
    } else {
        return 0;
    }
}
