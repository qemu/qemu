/*
 * QEMU KVM support
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "sysemu.h"
#include "kvm.h"

//#define DEBUG_KVM

#ifdef DEBUG_KVM
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

typedef struct kvm_userspace_memory_region KVMSlot;

int kvm_allowed = 0;

struct KVMState
{
    KVMSlot slots[32];
    int fd;
    int vmfd;
};

static KVMState *kvm_state;

static KVMSlot *kvm_alloc_slot(KVMState *s)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->slots); i++) {
        if (s->slots[i].memory_size == 0)
            return &s->slots[i];
    }

    return NULL;
}

static KVMSlot *kvm_lookup_slot(KVMState *s, target_phys_addr_t start_addr)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->slots); i++) {
        KVMSlot *mem = &s->slots[i];

        if (start_addr >= mem->guest_phys_addr &&
            start_addr < (mem->guest_phys_addr + mem->memory_size))
            return mem;
    }

    return NULL;
}

int kvm_init_vcpu(CPUState *env)
{
    KVMState *s = kvm_state;
    long mmap_size;
    int ret;

    dprintf("kvm_init_vcpu\n");

    ret = kvm_vm_ioctl(s, KVM_CREATE_VCPU,
                       (void *)(unsigned long)env->cpu_index);
    if (ret < 0) {
        dprintf("kvm_create_vcpu failed\n");
        goto err;
    }

    env->kvm_fd = ret;
    env->kvm_state = s;

    mmap_size = kvm_ioctl(s, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size < 0) {
        dprintf("KVM_GET_VCPU_MMAP_SIZE failed\n");
        goto err;
    }

    env->kvm_run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        env->kvm_fd, 0);
    if (env->kvm_run == MAP_FAILED) {
        ret = -errno;
        dprintf("mmap'ing vcpu state failed\n");
        goto err;
    }

    ret = kvm_arch_init_vcpu(env);

err:
    return ret;
}

int kvm_init(int smp_cpus)
{
    KVMState *s;
    int ret;
    int i;

    if (smp_cpus > 1)
        return -EINVAL;

    s = qemu_mallocz(sizeof(KVMState));
    if (s == NULL)
        return -ENOMEM;

    for (i = 0; i < ARRAY_SIZE(s->slots); i++)
        s->slots[i].slot = i;

    s->vmfd = -1;
    s->fd = open("/dev/kvm", O_RDWR);
    if (s->fd == -1) {
        fprintf(stderr, "Could not access KVM kernel module: %m\n");
        ret = -errno;
        goto err;
    }

    ret = kvm_ioctl(s, KVM_GET_API_VERSION, 0);
    if (ret < KVM_API_VERSION) {
        if (ret > 0)
            ret = -EINVAL;
        fprintf(stderr, "kvm version too old\n");
        goto err;
    }

    if (ret > KVM_API_VERSION) {
        ret = -EINVAL;
        fprintf(stderr, "kvm version not supported\n");
        goto err;
    }

    s->vmfd = kvm_ioctl(s, KVM_CREATE_VM, 0);
    if (s->vmfd < 0)
        goto err;

    /* initially, KVM allocated its own memory and we had to jump through
     * hooks to make phys_ram_base point to this.  Modern versions of KVM
     * just use a user allocated buffer so we can use phys_ram_base
     * unmodified.  Make sure we have a sufficiently modern version of KVM.
     */
    ret = kvm_ioctl(s, KVM_CHECK_EXTENSION, (void *)KVM_CAP_USER_MEMORY);
    if (ret <= 0) {
        if (ret == 0)
            ret = -EINVAL;
        fprintf(stderr, "kvm does not support KVM_CAP_USER_MEMORY\n");
        goto err;
    }

    ret = kvm_arch_init(s, smp_cpus);
    if (ret < 0)
        goto err;

    kvm_state = s;

    return 0;

err:
    if (s) {
        if (s->vmfd != -1)
            close(s->vmfd);
        if (s->fd != -1)
            close(s->fd);
    }
    qemu_free(s);

    return ret;
}

static int kvm_handle_io(CPUState *env, uint16_t port, void *data,
                         int direction, int size, uint32_t count)
{
    int i;
    uint8_t *ptr = data;

    for (i = 0; i < count; i++) {
        if (direction == KVM_EXIT_IO_IN) {
            switch (size) {
            case 1:
                stb_p(ptr, cpu_inb(env, port));
                break;
            case 2:
                stw_p(ptr, cpu_inw(env, port));
                break;
            case 4:
                stl_p(ptr, cpu_inl(env, port));
                break;
            }
        } else {
            switch (size) {
            case 1:
                cpu_outb(env, port, ldub_p(ptr));
                break;
            case 2:
                cpu_outw(env, port, lduw_p(ptr));
                break;
            case 4:
                cpu_outl(env, port, ldl_p(ptr));
                break;
            }
        }

        ptr += size;
    }

    return 1;
}

int kvm_cpu_exec(CPUState *env)
{
    struct kvm_run *run = env->kvm_run;
    int ret;

    dprintf("kvm_cpu_exec()\n");

    do {
        kvm_arch_pre_run(env, run);

        if ((env->interrupt_request & CPU_INTERRUPT_EXIT)) {
            dprintf("interrupt exit requested\n");
            ret = 0;
            break;
        }

        ret = kvm_vcpu_ioctl(env, KVM_RUN, 0);
        kvm_arch_post_run(env, run);

        if (ret == -EINTR || ret == -EAGAIN) {
            dprintf("io window exit\n");
            ret = 0;
            break;
        }

        if (ret < 0) {
            dprintf("kvm run failed %s\n", strerror(-ret));
            abort();
        }

        ret = 0; /* exit loop */
        switch (run->exit_reason) {
        case KVM_EXIT_IO:
            dprintf("handle_io\n");
            ret = kvm_handle_io(env, run->io.port,
                                (uint8_t *)run + run->io.data_offset,
                                run->io.direction,
                                run->io.size,
                                run->io.count);
            break;
        case KVM_EXIT_MMIO:
            dprintf("handle_mmio\n");
            cpu_physical_memory_rw(run->mmio.phys_addr,
                                   run->mmio.data,
                                   run->mmio.len,
                                   run->mmio.is_write);
            ret = 1;
            break;
        case KVM_EXIT_IRQ_WINDOW_OPEN:
            dprintf("irq_window_open\n");
            break;
        case KVM_EXIT_SHUTDOWN:
            dprintf("shutdown\n");
            qemu_system_reset_request();
            ret = 1;
            break;
        case KVM_EXIT_UNKNOWN:
            dprintf("kvm_exit_unknown\n");
            break;
        case KVM_EXIT_FAIL_ENTRY:
            dprintf("kvm_exit_fail_entry\n");
            break;
        case KVM_EXIT_EXCEPTION:
            dprintf("kvm_exit_exception\n");
            break;
        case KVM_EXIT_DEBUG:
            dprintf("kvm_exit_debug\n");
            break;
        default:
            dprintf("kvm_arch_handle_exit\n");
            ret = kvm_arch_handle_exit(env, run);
            break;
        }
    } while (ret > 0);

    if ((env->interrupt_request & CPU_INTERRUPT_EXIT)) {
        env->interrupt_request &= ~CPU_INTERRUPT_EXIT;
        env->exception_index = EXCP_INTERRUPT;
    }

    return ret;
}

void kvm_set_phys_mem(target_phys_addr_t start_addr,
                      ram_addr_t size,
                      ram_addr_t phys_offset)
{
    KVMState *s = kvm_state;
    ram_addr_t flags = phys_offset & ~TARGET_PAGE_MASK;
    KVMSlot *mem;

    /* KVM does not support read-only slots */
    phys_offset &= ~IO_MEM_ROM;

    mem = kvm_lookup_slot(s, start_addr);
    if (mem) {
        if (flags == IO_MEM_UNASSIGNED) {
            mem->memory_size = 0;
            mem->guest_phys_addr = start_addr;
            mem->userspace_addr = 0;
            mem->flags = 0;

            kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, mem);
        } else if (start_addr >= mem->guest_phys_addr &&
                   (start_addr + size) <= (mem->guest_phys_addr + mem->memory_size))
            return;
    }

    /* KVM does not need to know about this memory */
    if (flags >= IO_MEM_UNASSIGNED)
        return;

    mem = kvm_alloc_slot(s);
    mem->memory_size = size;
    mem->guest_phys_addr = start_addr;
    mem->userspace_addr = (unsigned long)(phys_ram_base + phys_offset);
    mem->flags = 0;

    kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, mem);
    /* FIXME deal with errors */
}

int kvm_ioctl(KVMState *s, int type, void *data)
{
    int ret;

    ret = ioctl(s->fd, type, data);
    if (ret == -1)
        ret = -errno;

    return ret;
}

int kvm_vm_ioctl(KVMState *s, int type, void *data)
{
    int ret;

    ret = ioctl(s->vmfd, type, data);
    if (ret == -1)
        ret = -errno;

    return ret;
}

int kvm_vcpu_ioctl(CPUState *env, int type, void *data)
{
    int ret;

    ret = ioctl(env->kvm_fd, type, data);
    if (ret == -1)
        ret = -errno;

    return ret;
}
