/*
 * QEMU KVM support
 *
 * Copyright IBM, Corp. 2008
 *           Red Hat, Inc. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Glauber Costa     <gcosta@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdarg.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "sysemu.h"
#include "gdbstub.h"
#include "kvm.h"

/* KVM uses PAGE_SIZE in it's definition of COALESCED_MMIO_MAX */
#define PAGE_SIZE TARGET_PAGE_SIZE

//#define DEBUG_KVM

#ifdef DEBUG_KVM
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

typedef struct KVMSlot
{
    target_phys_addr_t start_addr;
    ram_addr_t memory_size;
    ram_addr_t phys_offset;
    int slot;
    int flags;
} KVMSlot;

typedef struct kvm_dirty_log KVMDirtyLog;

int kvm_allowed = 0;

struct KVMState
{
    KVMSlot slots[32];
    int fd;
    int vmfd;
    int coalesced_mmio;
#ifdef KVM_CAP_SET_GUEST_DEBUG
    struct kvm_sw_breakpoint_head kvm_sw_breakpoints;
#endif
};

static KVMState *kvm_state;

static KVMSlot *kvm_alloc_slot(KVMState *s)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->slots); i++) {
        /* KVM private memory slots */
        if (i >= 8 && i < 12)
            continue;
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

        if (start_addr >= mem->start_addr &&
            start_addr < (mem->start_addr + mem->memory_size))
            return mem;
    }

    return NULL;
}

static int kvm_set_user_memory_region(KVMState *s, KVMSlot *slot)
{
    struct kvm_userspace_memory_region mem;

    mem.slot = slot->slot;
    mem.guest_phys_addr = slot->start_addr;
    mem.memory_size = slot->memory_size;
    mem.userspace_addr = (unsigned long)phys_ram_base + slot->phys_offset;
    mem.flags = slot->flags;

    return kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
}


int kvm_init_vcpu(CPUState *env)
{
    KVMState *s = kvm_state;
    long mmap_size;
    int ret;

    dprintf("kvm_init_vcpu\n");

    ret = kvm_vm_ioctl(s, KVM_CREATE_VCPU, env->cpu_index);
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

int kvm_sync_vcpus(void)
{
    CPUState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        int ret;

        ret = kvm_arch_put_registers(env);
        if (ret)
            return ret;
    }

    return 0;
}

/*
 * dirty pages logging control
 */
static int kvm_dirty_pages_log_change(target_phys_addr_t phys_addr, target_phys_addr_t end_addr,
                                      unsigned flags,
                                      unsigned mask)
{
    KVMState *s = kvm_state;
    KVMSlot *mem = kvm_lookup_slot(s, phys_addr);
    if (mem == NULL)  {
            dprintf("invalid parameters %llx-%llx\n", phys_addr, end_addr);
            return -EINVAL;
    }

    flags = (mem->flags & ~mask) | flags;
    /* Nothing changed, no need to issue ioctl */
    if (flags == mem->flags)
            return 0;

    mem->flags = flags;

    return kvm_set_user_memory_region(s, mem);
}

int kvm_log_start(target_phys_addr_t phys_addr, target_phys_addr_t end_addr)
{
        return kvm_dirty_pages_log_change(phys_addr, end_addr,
                                          KVM_MEM_LOG_DIRTY_PAGES,
                                          KVM_MEM_LOG_DIRTY_PAGES);
}

int kvm_log_stop(target_phys_addr_t phys_addr, target_phys_addr_t end_addr)
{
        return kvm_dirty_pages_log_change(phys_addr, end_addr,
                                          0,
                                          KVM_MEM_LOG_DIRTY_PAGES);
}

/**
 * kvm_physical_sync_dirty_bitmap - Grab dirty bitmap from kernel space
 * This function updates qemu's dirty bitmap using cpu_physical_memory_set_dirty().
 * This means all bits are set to dirty.
 *
 * @start_add: start of logged region. This is what we use to search the memslot
 * @end_addr: end of logged region.
 */
void kvm_physical_sync_dirty_bitmap(target_phys_addr_t start_addr, target_phys_addr_t end_addr)
{
    KVMState *s = kvm_state;
    KVMDirtyLog d;
    KVMSlot *mem = kvm_lookup_slot(s, start_addr);
    unsigned long alloc_size;
    ram_addr_t addr;
    target_phys_addr_t phys_addr = start_addr;

    dprintf("sync addr: %llx into %lx\n", start_addr, mem->phys_offset);
    if (mem == NULL) {
            fprintf(stderr, "BUG: %s: invalid parameters\n", __func__);
            return;
    }

    alloc_size = mem->memory_size >> TARGET_PAGE_BITS / sizeof(d.dirty_bitmap);
    d.dirty_bitmap = qemu_mallocz(alloc_size);

    d.slot = mem->slot;
    dprintf("slot %d, phys_addr %llx, uaddr: %llx\n",
            d.slot, mem->start_addr, mem->phys_offset);

    if (kvm_vm_ioctl(s, KVM_GET_DIRTY_LOG, &d) == -1) {
        dprintf("ioctl failed %d\n", errno);
        goto out;
    }

    phys_addr = start_addr;
    for (addr = mem->phys_offset; phys_addr < end_addr; phys_addr+= TARGET_PAGE_SIZE, addr += TARGET_PAGE_SIZE) {
        unsigned long *bitmap = (unsigned long *)d.dirty_bitmap;
        unsigned nr = (phys_addr - start_addr) >> TARGET_PAGE_BITS;
        unsigned word = nr / (sizeof(*bitmap) * 8);
        unsigned bit = nr % (sizeof(*bitmap) * 8);
        if ((bitmap[word] >> bit) & 1)
            cpu_physical_memory_set_dirty(addr);
    }
out:
    qemu_free(d.dirty_bitmap);
}

int kvm_coalesce_mmio_region(target_phys_addr_t start, ram_addr_t size)
{
    int ret = -ENOSYS;
#ifdef KVM_CAP_COALESCED_MMIO
    KVMState *s = kvm_state;

    if (s->coalesced_mmio) {
        struct kvm_coalesced_mmio_zone zone;

        zone.addr = start;
        zone.size = size;

        ret = kvm_vm_ioctl(s, KVM_REGISTER_COALESCED_MMIO, &zone);
    }
#endif

    return ret;
}

int kvm_uncoalesce_mmio_region(target_phys_addr_t start, ram_addr_t size)
{
    int ret = -ENOSYS;
#ifdef KVM_CAP_COALESCED_MMIO
    KVMState *s = kvm_state;

    if (s->coalesced_mmio) {
        struct kvm_coalesced_mmio_zone zone;

        zone.addr = start;
        zone.size = size;

        ret = kvm_vm_ioctl(s, KVM_UNREGISTER_COALESCED_MMIO, &zone);
    }
#endif

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

#ifdef KVM_CAP_SET_GUEST_DEBUG
    TAILQ_INIT(&s->kvm_sw_breakpoints);
#endif
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
    ret = kvm_ioctl(s, KVM_CHECK_EXTENSION, KVM_CAP_USER_MEMORY);
    if (ret <= 0) {
        if (ret == 0)
            ret = -EINVAL;
        fprintf(stderr, "kvm does not support KVM_CAP_USER_MEMORY\n");
        goto err;
    }

    /* There was a nasty bug in < kvm-80 that prevents memory slots from being
     * destroyed properly.  Since we rely on this capability, refuse to work
     * with any kernel without this capability. */
    ret = kvm_ioctl(s, KVM_CHECK_EXTENSION,
                    KVM_CAP_DESTROY_MEMORY_REGION_WORKS);
    if (ret <= 0) {
        if (ret == 0)
            ret = -EINVAL;

        fprintf(stderr,
                "KVM kernel module broken (DESTROY_MEMORY_REGION)\n"
                "Please upgrade to at least kvm-81.\n");
        goto err;
    }

    s->coalesced_mmio = 0;
#ifdef KVM_CAP_COALESCED_MMIO
    ret = kvm_ioctl(s, KVM_CHECK_EXTENSION, KVM_CAP_COALESCED_MMIO);
    if (ret > 0)
        s->coalesced_mmio = ret;
#endif

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

static void kvm_run_coalesced_mmio(CPUState *env, struct kvm_run *run)
{
#ifdef KVM_CAP_COALESCED_MMIO
    KVMState *s = kvm_state;
    if (s->coalesced_mmio) {
        struct kvm_coalesced_mmio_ring *ring;

        ring = (void *)run + (s->coalesced_mmio * TARGET_PAGE_SIZE);
        while (ring->first != ring->last) {
            struct kvm_coalesced_mmio *ent;

            ent = &ring->coalesced_mmio[ring->first];

            cpu_physical_memory_write(ent->phys_addr, ent->data, ent->len);
            /* FIXME smp_wmb() */
            ring->first = (ring->first + 1) % KVM_COALESCED_MMIO_MAX;
        }
    }
#endif
}

int kvm_cpu_exec(CPUState *env)
{
    struct kvm_run *run = env->kvm_run;
    int ret;

    dprintf("kvm_cpu_exec()\n");

    do {
        kvm_arch_pre_run(env, run);

        if (env->exit_request) {
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

        kvm_run_coalesced_mmio(env, run);

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
#ifdef KVM_CAP_SET_GUEST_DEBUG
            if (kvm_arch_debug(&run->debug.arch)) {
                gdb_set_stop_cpu(env);
                vm_stop(EXCP_DEBUG);
                env->exception_index = EXCP_DEBUG;
                return 0;
            }
            /* re-enter, this exception was guest-internal */
            ret = 1;
#endif /* KVM_CAP_SET_GUEST_DEBUG */
            break;
        default:
            dprintf("kvm_arch_handle_exit\n");
            ret = kvm_arch_handle_exit(env, run);
            break;
        }
    } while (ret > 0);

    if (env->exit_request) {
        env->exit_request = 0;
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
        if ((flags == IO_MEM_UNASSIGNED) || (flags >= TLB_MMIO)) {
            mem->memory_size = 0;
            mem->start_addr = start_addr;
            mem->phys_offset = 0;
            mem->flags = 0;

            kvm_set_user_memory_region(s, mem);
        } else if (start_addr >= mem->start_addr &&
                   (start_addr + size) <= (mem->start_addr +
                                           mem->memory_size)) {
            KVMSlot slot;
            target_phys_addr_t mem_start;
            ram_addr_t mem_size, mem_offset;

            /* Not splitting */
            if ((phys_offset - (start_addr - mem->start_addr)) == 
                mem->phys_offset)
                return;

            /* unregister whole slot */
            memcpy(&slot, mem, sizeof(slot));
            mem->memory_size = 0;
            kvm_set_user_memory_region(s, mem);

            /* register prefix slot */
            mem_start = slot.start_addr;
            mem_size = start_addr - slot.start_addr;
            mem_offset = slot.phys_offset;
            if (mem_size)
                kvm_set_phys_mem(mem_start, mem_size, mem_offset);

            /* register new slot */
            kvm_set_phys_mem(start_addr, size, phys_offset);

            /* register suffix slot */
            mem_start = start_addr + size;
            mem_offset += mem_size + size;
            mem_size = slot.memory_size - mem_size - size;
            if (mem_size)
                kvm_set_phys_mem(mem_start, mem_size, mem_offset);

            return;
        } else {
            printf("Registering overlapping slot\n");
            abort();
        }
    }
    /* KVM does not need to know about this memory */
    if (flags >= IO_MEM_UNASSIGNED)
        return;

    mem = kvm_alloc_slot(s);
    mem->memory_size = size;
    mem->start_addr = start_addr;
    mem->phys_offset = phys_offset;
    mem->flags = 0;

    kvm_set_user_memory_region(s, mem);
    /* FIXME deal with errors */
}

int kvm_ioctl(KVMState *s, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    ret = ioctl(s->fd, type, arg);
    if (ret == -1)
        ret = -errno;

    return ret;
}

int kvm_vm_ioctl(KVMState *s, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    ret = ioctl(s->vmfd, type, arg);
    if (ret == -1)
        ret = -errno;

    return ret;
}

int kvm_vcpu_ioctl(CPUState *env, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    ret = ioctl(env->kvm_fd, type, arg);
    if (ret == -1)
        ret = -errno;

    return ret;
}

int kvm_has_sync_mmu(void)
{
#ifdef KVM_CAP_SYNC_MMU
    KVMState *s = kvm_state;

    if (kvm_ioctl(s, KVM_CHECK_EXTENSION, KVM_CAP_SYNC_MMU) > 0)
        return 1;
#endif

    return 0;
}

#ifdef KVM_CAP_SET_GUEST_DEBUG
struct kvm_sw_breakpoint *kvm_find_sw_breakpoint(CPUState *env,
                                                 target_ulong pc)
{
    struct kvm_sw_breakpoint *bp;

    TAILQ_FOREACH(bp, &env->kvm_state->kvm_sw_breakpoints, entry) {
        if (bp->pc == pc)
            return bp;
    }
    return NULL;
}

int kvm_sw_breakpoints_active(CPUState *env)
{
    return !TAILQ_EMPTY(&env->kvm_state->kvm_sw_breakpoints);
}

int kvm_update_guest_debug(CPUState *env, unsigned long reinject_trap)
{
    struct kvm_guest_debug dbg;

    dbg.control = 0;
    if (env->singlestep_enabled)
        dbg.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;

    kvm_arch_update_guest_debug(env, &dbg);
    dbg.control |= reinject_trap;

    return kvm_vcpu_ioctl(env, KVM_SET_GUEST_DEBUG, &dbg);
}

int kvm_insert_breakpoint(CPUState *current_env, target_ulong addr,
                          target_ulong len, int type)
{
    struct kvm_sw_breakpoint *bp;
    CPUState *env;
    int err;

    if (type == GDB_BREAKPOINT_SW) {
        bp = kvm_find_sw_breakpoint(current_env, addr);
        if (bp) {
            bp->use_count++;
            return 0;
        }

        bp = qemu_malloc(sizeof(struct kvm_sw_breakpoint));
        if (!bp)
            return -ENOMEM;

        bp->pc = addr;
        bp->use_count = 1;
        err = kvm_arch_insert_sw_breakpoint(current_env, bp);
        if (err) {
            free(bp);
            return err;
        }

        TAILQ_INSERT_HEAD(&current_env->kvm_state->kvm_sw_breakpoints,
                          bp, entry);
    } else {
        err = kvm_arch_insert_hw_breakpoint(addr, len, type);
        if (err)
            return err;
    }

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        err = kvm_update_guest_debug(env, 0);
        if (err)
            return err;
    }
    return 0;
}

int kvm_remove_breakpoint(CPUState *current_env, target_ulong addr,
                          target_ulong len, int type)
{
    struct kvm_sw_breakpoint *bp;
    CPUState *env;
    int err;

    if (type == GDB_BREAKPOINT_SW) {
        bp = kvm_find_sw_breakpoint(current_env, addr);
        if (!bp)
            return -ENOENT;

        if (bp->use_count > 1) {
            bp->use_count--;
            return 0;
        }

        err = kvm_arch_remove_sw_breakpoint(current_env, bp);
        if (err)
            return err;

        TAILQ_REMOVE(&current_env->kvm_state->kvm_sw_breakpoints, bp, entry);
        qemu_free(bp);
    } else {
        err = kvm_arch_remove_hw_breakpoint(addr, len, type);
        if (err)
            return err;
    }

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        err = kvm_update_guest_debug(env, 0);
        if (err)
            return err;
    }
    return 0;
}

void kvm_remove_all_breakpoints(CPUState *current_env)
{
    struct kvm_sw_breakpoint *bp, *next;
    KVMState *s = current_env->kvm_state;
    CPUState *env;

    TAILQ_FOREACH_SAFE(bp, &s->kvm_sw_breakpoints, entry, next) {
        if (kvm_arch_remove_sw_breakpoint(current_env, bp) != 0) {
            /* Try harder to find a CPU that currently sees the breakpoint. */
            for (env = first_cpu; env != NULL; env = env->next_cpu) {
                if (kvm_arch_remove_sw_breakpoint(env, bp) == 0)
                    break;
            }
        }
    }
    kvm_arch_remove_all_hw_breakpoints();

    for (env = first_cpu; env != NULL; env = env->next_cpu)
        kvm_update_guest_debug(env, 0);
}

#else /* !KVM_CAP_SET_GUEST_DEBUG */

int kvm_update_guest_debug(CPUState *env, unsigned long reinject_trap)
{
    return -EINVAL;
}

int kvm_insert_breakpoint(CPUState *current_env, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

int kvm_remove_breakpoint(CPUState *current_env, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

void kvm_remove_all_breakpoints(CPUState *current_env)
{
}
#endif /* !KVM_CAP_SET_GUEST_DEBUG */
