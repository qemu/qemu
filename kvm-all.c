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
#include "qemu/atomic.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "exec/gdbstub.h"
#include "sysemu/kvm.h"
#include "qemu/bswap.h"
#include "exec/memory.h"
#include "exec/ram_addr.h"
#include "exec/address-spaces.h"
#include "qemu/event_notifier.h"
#include "trace.h"

#include "hw/boards.h"

/* This check must be after config-host.h is included */
#ifdef CONFIG_EVENTFD
#include <sys/eventfd.h>
#endif

#ifdef CONFIG_VALGRIND_H
#include <valgrind/memcheck.h>
#endif

/* KVM uses PAGE_SIZE in its definition of COALESCED_MMIO_MAX */
#define PAGE_SIZE TARGET_PAGE_SIZE

//#define DEBUG_KVM

#ifdef DEBUG_KVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#define KVM_MSI_HASHTAB_SIZE    256

typedef struct KVMSlot
{
    hwaddr start_addr;
    ram_addr_t memory_size;
    void *ram;
    int slot;
    int flags;
} KVMSlot;

typedef struct kvm_dirty_log KVMDirtyLog;

struct KVMState
{
    KVMSlot *slots;
    int nr_slots;
    int fd;
    int vmfd;
    int coalesced_mmio;
    struct kvm_coalesced_mmio_ring *coalesced_mmio_ring;
    bool coalesced_flush_in_progress;
    int broken_set_mem_region;
    int migration_log;
    int vcpu_events;
    int robust_singlestep;
    int debugregs;
#ifdef KVM_CAP_SET_GUEST_DEBUG
    struct kvm_sw_breakpoint_head kvm_sw_breakpoints;
#endif
    int pit_state2;
    int xsave, xcrs;
    int many_ioeventfds;
    int intx_set_mask;
    /* The man page (and posix) say ioctl numbers are signed int, but
     * they're not.  Linux, glibc and *BSD all treat ioctl numbers as
     * unsigned, and treating them as signed here can break things */
    unsigned irq_set_ioctl;
#ifdef KVM_CAP_IRQ_ROUTING
    struct kvm_irq_routing *irq_routes;
    int nr_allocated_irq_routes;
    uint32_t *used_gsi_bitmap;
    unsigned int gsi_count;
    QTAILQ_HEAD(msi_hashtab, KVMMSIRoute) msi_hashtab[KVM_MSI_HASHTAB_SIZE];
    bool direct_msi;
#endif
};

KVMState *kvm_state;
bool kvm_kernel_irqchip;
bool kvm_async_interrupts_allowed;
bool kvm_halt_in_kernel_allowed;
bool kvm_irqfds_allowed;
bool kvm_msi_via_irqfd_allowed;
bool kvm_gsi_routing_allowed;
bool kvm_gsi_direct_mapping;
bool kvm_allowed;
bool kvm_readonly_mem_allowed;

static const KVMCapabilityInfo kvm_required_capabilites[] = {
    KVM_CAP_INFO(USER_MEMORY),
    KVM_CAP_INFO(DESTROY_MEMORY_REGION_WORKS),
    KVM_CAP_LAST_INFO
};

static KVMSlot *kvm_alloc_slot(KVMState *s)
{
    int i;

    for (i = 0; i < s->nr_slots; i++) {
        if (s->slots[i].memory_size == 0) {
            return &s->slots[i];
        }
    }

    fprintf(stderr, "%s: no free slot available\n", __func__);
    abort();
}

static KVMSlot *kvm_lookup_matching_slot(KVMState *s,
                                         hwaddr start_addr,
                                         hwaddr end_addr)
{
    int i;

    for (i = 0; i < s->nr_slots; i++) {
        KVMSlot *mem = &s->slots[i];

        if (start_addr == mem->start_addr &&
            end_addr == mem->start_addr + mem->memory_size) {
            return mem;
        }
    }

    return NULL;
}

/*
 * Find overlapping slot with lowest start address
 */
static KVMSlot *kvm_lookup_overlapping_slot(KVMState *s,
                                            hwaddr start_addr,
                                            hwaddr end_addr)
{
    KVMSlot *found = NULL;
    int i;

    for (i = 0; i < s->nr_slots; i++) {
        KVMSlot *mem = &s->slots[i];

        if (mem->memory_size == 0 ||
            (found && found->start_addr < mem->start_addr)) {
            continue;
        }

        if (end_addr > mem->start_addr &&
            start_addr < mem->start_addr + mem->memory_size) {
            found = mem;
        }
    }

    return found;
}

int kvm_physical_memory_addr_from_host(KVMState *s, void *ram,
                                       hwaddr *phys_addr)
{
    int i;

    for (i = 0; i < s->nr_slots; i++) {
        KVMSlot *mem = &s->slots[i];

        if (ram >= mem->ram && ram < mem->ram + mem->memory_size) {
            *phys_addr = mem->start_addr + (ram - mem->ram);
            return 1;
        }
    }

    return 0;
}

static int kvm_set_user_memory_region(KVMState *s, KVMSlot *slot)
{
    struct kvm_userspace_memory_region mem;

    mem.slot = slot->slot;
    mem.guest_phys_addr = slot->start_addr;
    mem.userspace_addr = (unsigned long)slot->ram;
    mem.flags = slot->flags;
    if (s->migration_log) {
        mem.flags |= KVM_MEM_LOG_DIRTY_PAGES;
    }

    if (slot->memory_size && mem.flags & KVM_MEM_READONLY) {
        /* Set the slot size to 0 before setting the slot to the desired
         * value. This is needed based on KVM commit 75d61fbc. */
        mem.memory_size = 0;
        kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
    }
    mem.memory_size = slot->memory_size;
    return kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
}

int kvm_init_vcpu(CPUState *cpu)
{
    KVMState *s = kvm_state;
    long mmap_size;
    int ret;

    DPRINTF("kvm_init_vcpu\n");

    ret = kvm_vm_ioctl(s, KVM_CREATE_VCPU, (void *)kvm_arch_vcpu_id(cpu));
    if (ret < 0) {
        DPRINTF("kvm_create_vcpu failed\n");
        goto err;
    }

    cpu->kvm_fd = ret;
    cpu->kvm_state = s;
    cpu->kvm_vcpu_dirty = true;

    mmap_size = kvm_ioctl(s, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size < 0) {
        ret = mmap_size;
        DPRINTF("KVM_GET_VCPU_MMAP_SIZE failed\n");
        goto err;
    }

    cpu->kvm_run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        cpu->kvm_fd, 0);
    if (cpu->kvm_run == MAP_FAILED) {
        ret = -errno;
        DPRINTF("mmap'ing vcpu state failed\n");
        goto err;
    }

    if (s->coalesced_mmio && !s->coalesced_mmio_ring) {
        s->coalesced_mmio_ring =
            (void *)cpu->kvm_run + s->coalesced_mmio * PAGE_SIZE;
    }

    ret = kvm_arch_init_vcpu(cpu);
err:
    return ret;
}

/*
 * dirty pages logging control
 */

static int kvm_mem_flags(KVMState *s, bool log_dirty, bool readonly)
{
    int flags = 0;
    flags = log_dirty ? KVM_MEM_LOG_DIRTY_PAGES : 0;
    if (readonly && kvm_readonly_mem_allowed) {
        flags |= KVM_MEM_READONLY;
    }
    return flags;
}

static int kvm_slot_dirty_pages_log_change(KVMSlot *mem, bool log_dirty)
{
    KVMState *s = kvm_state;
    int flags, mask = KVM_MEM_LOG_DIRTY_PAGES;
    int old_flags;

    old_flags = mem->flags;

    flags = (mem->flags & ~mask) | kvm_mem_flags(s, log_dirty, false);
    mem->flags = flags;

    /* If nothing changed effectively, no need to issue ioctl */
    if (s->migration_log) {
        flags |= KVM_MEM_LOG_DIRTY_PAGES;
    }

    if (flags == old_flags) {
        return 0;
    }

    return kvm_set_user_memory_region(s, mem);
}

static int kvm_dirty_pages_log_change(hwaddr phys_addr,
                                      ram_addr_t size, bool log_dirty)
{
    KVMState *s = kvm_state;
    KVMSlot *mem = kvm_lookup_matching_slot(s, phys_addr, phys_addr + size);

    if (mem == NULL)  {
        fprintf(stderr, "BUG: %s: invalid parameters " TARGET_FMT_plx "-"
                TARGET_FMT_plx "\n", __func__, phys_addr,
                (hwaddr)(phys_addr + size - 1));
        return -EINVAL;
    }
    return kvm_slot_dirty_pages_log_change(mem, log_dirty);
}

static void kvm_log_start(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    int r;

    r = kvm_dirty_pages_log_change(section->offset_within_address_space,
                                   int128_get64(section->size), true);
    if (r < 0) {
        abort();
    }
}

static void kvm_log_stop(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    int r;

    r = kvm_dirty_pages_log_change(section->offset_within_address_space,
                                   int128_get64(section->size), false);
    if (r < 0) {
        abort();
    }
}

static int kvm_set_migration_log(int enable)
{
    KVMState *s = kvm_state;
    KVMSlot *mem;
    int i, err;

    s->migration_log = enable;

    for (i = 0; i < s->nr_slots; i++) {
        mem = &s->slots[i];

        if (!mem->memory_size) {
            continue;
        }
        if (!!(mem->flags & KVM_MEM_LOG_DIRTY_PAGES) == enable) {
            continue;
        }
        err = kvm_set_user_memory_region(s, mem);
        if (err) {
            return err;
        }
    }
    return 0;
}

/* get kvm's dirty pages bitmap and update qemu's */
static int kvm_get_dirty_pages_log_range(MemoryRegionSection *section,
                                         unsigned long *bitmap)
{
    ram_addr_t start = section->offset_within_region + section->mr->ram_addr;
    ram_addr_t pages = int128_get64(section->size) / getpagesize();

    cpu_physical_memory_set_dirty_lebitmap(bitmap, start, pages);
    return 0;
}

#define ALIGN(x, y)  (((x)+(y)-1) & ~((y)-1))

/**
 * kvm_physical_sync_dirty_bitmap - Grab dirty bitmap from kernel space
 * This function updates qemu's dirty bitmap using
 * memory_region_set_dirty().  This means all bits are set
 * to dirty.
 *
 * @start_add: start of logged region.
 * @end_addr: end of logged region.
 */
static int kvm_physical_sync_dirty_bitmap(MemoryRegionSection *section)
{
    KVMState *s = kvm_state;
    unsigned long size, allocated_size = 0;
    KVMDirtyLog d;
    KVMSlot *mem;
    int ret = 0;
    hwaddr start_addr = section->offset_within_address_space;
    hwaddr end_addr = start_addr + int128_get64(section->size);

    d.dirty_bitmap = NULL;
    while (start_addr < end_addr) {
        mem = kvm_lookup_overlapping_slot(s, start_addr, end_addr);
        if (mem == NULL) {
            break;
        }

        /* XXX bad kernel interface alert
         * For dirty bitmap, kernel allocates array of size aligned to
         * bits-per-long.  But for case when the kernel is 64bits and
         * the userspace is 32bits, userspace can't align to the same
         * bits-per-long, since sizeof(long) is different between kernel
         * and user space.  This way, userspace will provide buffer which
         * may be 4 bytes less than the kernel will use, resulting in
         * userspace memory corruption (which is not detectable by valgrind
         * too, in most cases).
         * So for now, let's align to 64 instead of HOST_LONG_BITS here, in
         * a hope that sizeof(long) wont become >8 any time soon.
         */
        size = ALIGN(((mem->memory_size) >> TARGET_PAGE_BITS),
                     /*HOST_LONG_BITS*/ 64) / 8;
        if (!d.dirty_bitmap) {
            d.dirty_bitmap = g_malloc(size);
        } else if (size > allocated_size) {
            d.dirty_bitmap = g_realloc(d.dirty_bitmap, size);
        }
        allocated_size = size;
        memset(d.dirty_bitmap, 0, allocated_size);

        d.slot = mem->slot;

        if (kvm_vm_ioctl(s, KVM_GET_DIRTY_LOG, &d) == -1) {
            DPRINTF("ioctl failed %d\n", errno);
            ret = -1;
            break;
        }

        kvm_get_dirty_pages_log_range(section, d.dirty_bitmap);
        start_addr = mem->start_addr + mem->memory_size;
    }
    g_free(d.dirty_bitmap);

    return ret;
}

static void kvm_coalesce_mmio_region(MemoryListener *listener,
                                     MemoryRegionSection *secion,
                                     hwaddr start, hwaddr size)
{
    KVMState *s = kvm_state;

    if (s->coalesced_mmio) {
        struct kvm_coalesced_mmio_zone zone;

        zone.addr = start;
        zone.size = size;
        zone.pad = 0;

        (void)kvm_vm_ioctl(s, KVM_REGISTER_COALESCED_MMIO, &zone);
    }
}

static void kvm_uncoalesce_mmio_region(MemoryListener *listener,
                                       MemoryRegionSection *secion,
                                       hwaddr start, hwaddr size)
{
    KVMState *s = kvm_state;

    if (s->coalesced_mmio) {
        struct kvm_coalesced_mmio_zone zone;

        zone.addr = start;
        zone.size = size;
        zone.pad = 0;

        (void)kvm_vm_ioctl(s, KVM_UNREGISTER_COALESCED_MMIO, &zone);
    }
}

int kvm_check_extension(KVMState *s, unsigned int extension)
{
    int ret;

    ret = kvm_ioctl(s, KVM_CHECK_EXTENSION, extension);
    if (ret < 0) {
        ret = 0;
    }

    return ret;
}

static int kvm_set_ioeventfd_mmio(int fd, hwaddr addr, uint32_t val,
                                  bool assign, uint32_t size, bool datamatch)
{
    int ret;
    struct kvm_ioeventfd iofd;

    iofd.datamatch = datamatch ? val : 0;
    iofd.addr = addr;
    iofd.len = size;
    iofd.flags = 0;
    iofd.fd = fd;

    if (!kvm_enabled()) {
        return -ENOSYS;
    }

    if (datamatch) {
        iofd.flags |= KVM_IOEVENTFD_FLAG_DATAMATCH;
    }
    if (!assign) {
        iofd.flags |= KVM_IOEVENTFD_FLAG_DEASSIGN;
    }

    ret = kvm_vm_ioctl(kvm_state, KVM_IOEVENTFD, &iofd);

    if (ret < 0) {
        return -errno;
    }

    return 0;
}

static int kvm_set_ioeventfd_pio(int fd, uint16_t addr, uint16_t val,
                                 bool assign, uint32_t size, bool datamatch)
{
    struct kvm_ioeventfd kick = {
        .datamatch = datamatch ? val : 0,
        .addr = addr,
        .flags = KVM_IOEVENTFD_FLAG_PIO,
        .len = size,
        .fd = fd,
    };
    int r;
    if (!kvm_enabled()) {
        return -ENOSYS;
    }
    if (datamatch) {
        kick.flags |= KVM_IOEVENTFD_FLAG_DATAMATCH;
    }
    if (!assign) {
        kick.flags |= KVM_IOEVENTFD_FLAG_DEASSIGN;
    }
    r = kvm_vm_ioctl(kvm_state, KVM_IOEVENTFD, &kick);
    if (r < 0) {
        return r;
    }
    return 0;
}


static int kvm_check_many_ioeventfds(void)
{
    /* Userspace can use ioeventfd for io notification.  This requires a host
     * that supports eventfd(2) and an I/O thread; since eventfd does not
     * support SIGIO it cannot interrupt the vcpu.
     *
     * Older kernels have a 6 device limit on the KVM io bus.  Find out so we
     * can avoid creating too many ioeventfds.
     */
#if defined(CONFIG_EVENTFD)
    int ioeventfds[7];
    int i, ret = 0;
    for (i = 0; i < ARRAY_SIZE(ioeventfds); i++) {
        ioeventfds[i] = eventfd(0, EFD_CLOEXEC);
        if (ioeventfds[i] < 0) {
            break;
        }
        ret = kvm_set_ioeventfd_pio(ioeventfds[i], 0, i, true, 2, true);
        if (ret < 0) {
            close(ioeventfds[i]);
            break;
        }
    }

    /* Decide whether many devices are supported or not */
    ret = i == ARRAY_SIZE(ioeventfds);

    while (i-- > 0) {
        kvm_set_ioeventfd_pio(ioeventfds[i], 0, i, false, 2, true);
        close(ioeventfds[i]);
    }
    return ret;
#else
    return 0;
#endif
}

static const KVMCapabilityInfo *
kvm_check_extension_list(KVMState *s, const KVMCapabilityInfo *list)
{
    while (list->name) {
        if (!kvm_check_extension(s, list->value)) {
            return list;
        }
        list++;
    }
    return NULL;
}

static void kvm_set_phys_mem(MemoryRegionSection *section, bool add)
{
    KVMState *s = kvm_state;
    KVMSlot *mem, old;
    int err;
    MemoryRegion *mr = section->mr;
    bool log_dirty = memory_region_is_logging(mr);
    bool writeable = !mr->readonly && !mr->rom_device;
    bool readonly_flag = mr->readonly || memory_region_is_romd(mr);
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    void *ram = NULL;
    unsigned delta;

    /* kvm works in page size chunks, but the function may be called
       with sub-page size and unaligned start address. */
    delta = TARGET_PAGE_ALIGN(size) - size;
    if (delta > size) {
        return;
    }
    start_addr += delta;
    size -= delta;
    size &= TARGET_PAGE_MASK;
    if (!size || (start_addr & ~TARGET_PAGE_MASK)) {
        return;
    }

    if (!memory_region_is_ram(mr)) {
        if (writeable || !kvm_readonly_mem_allowed) {
            return;
        } else if (!mr->romd_mode) {
            /* If the memory device is not in romd_mode, then we actually want
             * to remove the kvm memory slot so all accesses will trap. */
            add = false;
        }
    }

    ram = memory_region_get_ram_ptr(mr) + section->offset_within_region + delta;

    while (1) {
        mem = kvm_lookup_overlapping_slot(s, start_addr, start_addr + size);
        if (!mem) {
            break;
        }

        if (add && start_addr >= mem->start_addr &&
            (start_addr + size <= mem->start_addr + mem->memory_size) &&
            (ram - start_addr == mem->ram - mem->start_addr)) {
            /* The new slot fits into the existing one and comes with
             * identical parameters - update flags and done. */
            kvm_slot_dirty_pages_log_change(mem, log_dirty);
            return;
        }

        old = *mem;

        if (mem->flags & KVM_MEM_LOG_DIRTY_PAGES) {
            kvm_physical_sync_dirty_bitmap(section);
        }

        /* unregister the overlapping slot */
        mem->memory_size = 0;
        err = kvm_set_user_memory_region(s, mem);
        if (err) {
            fprintf(stderr, "%s: error unregistering overlapping slot: %s\n",
                    __func__, strerror(-err));
            abort();
        }

        /* Workaround for older KVM versions: we can't join slots, even not by
         * unregistering the previous ones and then registering the larger
         * slot. We have to maintain the existing fragmentation. Sigh.
         *
         * This workaround assumes that the new slot starts at the same
         * address as the first existing one. If not or if some overlapping
         * slot comes around later, we will fail (not seen in practice so far)
         * - and actually require a recent KVM version. */
        if (s->broken_set_mem_region &&
            old.start_addr == start_addr && old.memory_size < size && add) {
            mem = kvm_alloc_slot(s);
            mem->memory_size = old.memory_size;
            mem->start_addr = old.start_addr;
            mem->ram = old.ram;
            mem->flags = kvm_mem_flags(s, log_dirty, readonly_flag);

            err = kvm_set_user_memory_region(s, mem);
            if (err) {
                fprintf(stderr, "%s: error updating slot: %s\n", __func__,
                        strerror(-err));
                abort();
            }

            start_addr += old.memory_size;
            ram += old.memory_size;
            size -= old.memory_size;
            continue;
        }

        /* register prefix slot */
        if (old.start_addr < start_addr) {
            mem = kvm_alloc_slot(s);
            mem->memory_size = start_addr - old.start_addr;
            mem->start_addr = old.start_addr;
            mem->ram = old.ram;
            mem->flags =  kvm_mem_flags(s, log_dirty, readonly_flag);

            err = kvm_set_user_memory_region(s, mem);
            if (err) {
                fprintf(stderr, "%s: error registering prefix slot: %s\n",
                        __func__, strerror(-err));
#ifdef TARGET_PPC
                fprintf(stderr, "%s: This is probably because your kernel's " \
                                "PAGE_SIZE is too big. Please try to use 4k " \
                                "PAGE_SIZE!\n", __func__);
#endif
                abort();
            }
        }

        /* register suffix slot */
        if (old.start_addr + old.memory_size > start_addr + size) {
            ram_addr_t size_delta;

            mem = kvm_alloc_slot(s);
            mem->start_addr = start_addr + size;
            size_delta = mem->start_addr - old.start_addr;
            mem->memory_size = old.memory_size - size_delta;
            mem->ram = old.ram + size_delta;
            mem->flags = kvm_mem_flags(s, log_dirty, readonly_flag);

            err = kvm_set_user_memory_region(s, mem);
            if (err) {
                fprintf(stderr, "%s: error registering suffix slot: %s\n",
                        __func__, strerror(-err));
                abort();
            }
        }
    }

    /* in case the KVM bug workaround already "consumed" the new slot */
    if (!size) {
        return;
    }
    if (!add) {
        return;
    }
    mem = kvm_alloc_slot(s);
    mem->memory_size = size;
    mem->start_addr = start_addr;
    mem->ram = ram;
    mem->flags = kvm_mem_flags(s, log_dirty, readonly_flag);

    err = kvm_set_user_memory_region(s, mem);
    if (err) {
        fprintf(stderr, "%s: error registering slot: %s\n", __func__,
                strerror(-err));
        abort();
    }
}

static void kvm_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    memory_region_ref(section->mr);
    kvm_set_phys_mem(section, true);
}

static void kvm_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    kvm_set_phys_mem(section, false);
    memory_region_unref(section->mr);
}

static void kvm_log_sync(MemoryListener *listener,
                         MemoryRegionSection *section)
{
    int r;

    r = kvm_physical_sync_dirty_bitmap(section);
    if (r < 0) {
        abort();
    }
}

static void kvm_log_global_start(struct MemoryListener *listener)
{
    int r;

    r = kvm_set_migration_log(1);
    assert(r >= 0);
}

static void kvm_log_global_stop(struct MemoryListener *listener)
{
    int r;

    r = kvm_set_migration_log(0);
    assert(r >= 0);
}

static void kvm_mem_ioeventfd_add(MemoryListener *listener,
                                  MemoryRegionSection *section,
                                  bool match_data, uint64_t data,
                                  EventNotifier *e)
{
    int fd = event_notifier_get_fd(e);
    int r;

    r = kvm_set_ioeventfd_mmio(fd, section->offset_within_address_space,
                               data, true, int128_get64(section->size),
                               match_data);
    if (r < 0) {
        fprintf(stderr, "%s: error adding ioeventfd: %s\n",
                __func__, strerror(-r));
        abort();
    }
}

static void kvm_mem_ioeventfd_del(MemoryListener *listener,
                                  MemoryRegionSection *section,
                                  bool match_data, uint64_t data,
                                  EventNotifier *e)
{
    int fd = event_notifier_get_fd(e);
    int r;

    r = kvm_set_ioeventfd_mmio(fd, section->offset_within_address_space,
                               data, false, int128_get64(section->size),
                               match_data);
    if (r < 0) {
        abort();
    }
}

static void kvm_io_ioeventfd_add(MemoryListener *listener,
                                 MemoryRegionSection *section,
                                 bool match_data, uint64_t data,
                                 EventNotifier *e)
{
    int fd = event_notifier_get_fd(e);
    int r;

    r = kvm_set_ioeventfd_pio(fd, section->offset_within_address_space,
                              data, true, int128_get64(section->size),
                              match_data);
    if (r < 0) {
        fprintf(stderr, "%s: error adding ioeventfd: %s\n",
                __func__, strerror(-r));
        abort();
    }
}

static void kvm_io_ioeventfd_del(MemoryListener *listener,
                                 MemoryRegionSection *section,
                                 bool match_data, uint64_t data,
                                 EventNotifier *e)

{
    int fd = event_notifier_get_fd(e);
    int r;

    r = kvm_set_ioeventfd_pio(fd, section->offset_within_address_space,
                              data, false, int128_get64(section->size),
                              match_data);
    if (r < 0) {
        abort();
    }
}

static MemoryListener kvm_memory_listener = {
    .region_add = kvm_region_add,
    .region_del = kvm_region_del,
    .log_start = kvm_log_start,
    .log_stop = kvm_log_stop,
    .log_sync = kvm_log_sync,
    .log_global_start = kvm_log_global_start,
    .log_global_stop = kvm_log_global_stop,
    .eventfd_add = kvm_mem_ioeventfd_add,
    .eventfd_del = kvm_mem_ioeventfd_del,
    .coalesced_mmio_add = kvm_coalesce_mmio_region,
    .coalesced_mmio_del = kvm_uncoalesce_mmio_region,
    .priority = 10,
};

static MemoryListener kvm_io_listener = {
    .eventfd_add = kvm_io_ioeventfd_add,
    .eventfd_del = kvm_io_ioeventfd_del,
    .priority = 10,
};

static void kvm_handle_interrupt(CPUState *cpu, int mask)
{
    cpu->interrupt_request |= mask;

    if (!qemu_cpu_is_self(cpu)) {
        qemu_cpu_kick(cpu);
    }
}

int kvm_set_irq(KVMState *s, int irq, int level)
{
    struct kvm_irq_level event;
    int ret;

    assert(kvm_async_interrupts_enabled());

    event.level = level;
    event.irq = irq;
    ret = kvm_vm_ioctl(s, s->irq_set_ioctl, &event);
    if (ret < 0) {
        perror("kvm_set_irq");
        abort();
    }

    return (s->irq_set_ioctl == KVM_IRQ_LINE) ? 1 : event.status;
}

#ifdef KVM_CAP_IRQ_ROUTING
typedef struct KVMMSIRoute {
    struct kvm_irq_routing_entry kroute;
    QTAILQ_ENTRY(KVMMSIRoute) entry;
} KVMMSIRoute;

static void set_gsi(KVMState *s, unsigned int gsi)
{
    s->used_gsi_bitmap[gsi / 32] |= 1U << (gsi % 32);
}

static void clear_gsi(KVMState *s, unsigned int gsi)
{
    s->used_gsi_bitmap[gsi / 32] &= ~(1U << (gsi % 32));
}

void kvm_init_irq_routing(KVMState *s)
{
    int gsi_count, i;

    gsi_count = kvm_check_extension(s, KVM_CAP_IRQ_ROUTING);
    if (gsi_count > 0) {
        unsigned int gsi_bits, i;

        /* Round up so we can search ints using ffs */
        gsi_bits = ALIGN(gsi_count, 32);
        s->used_gsi_bitmap = g_malloc0(gsi_bits / 8);
        s->gsi_count = gsi_count;

        /* Mark any over-allocated bits as already in use */
        for (i = gsi_count; i < gsi_bits; i++) {
            set_gsi(s, i);
        }
    }

    s->irq_routes = g_malloc0(sizeof(*s->irq_routes));
    s->nr_allocated_irq_routes = 0;

    if (!s->direct_msi) {
        for (i = 0; i < KVM_MSI_HASHTAB_SIZE; i++) {
            QTAILQ_INIT(&s->msi_hashtab[i]);
        }
    }

    kvm_arch_init_irq_routing(s);
}

void kvm_irqchip_commit_routes(KVMState *s)
{
    int ret;

    s->irq_routes->flags = 0;
    ret = kvm_vm_ioctl(s, KVM_SET_GSI_ROUTING, s->irq_routes);
    assert(ret == 0);
}

static void kvm_add_routing_entry(KVMState *s,
                                  struct kvm_irq_routing_entry *entry)
{
    struct kvm_irq_routing_entry *new;
    int n, size;

    if (s->irq_routes->nr == s->nr_allocated_irq_routes) {
        n = s->nr_allocated_irq_routes * 2;
        if (n < 64) {
            n = 64;
        }
        size = sizeof(struct kvm_irq_routing);
        size += n * sizeof(*new);
        s->irq_routes = g_realloc(s->irq_routes, size);
        s->nr_allocated_irq_routes = n;
    }
    n = s->irq_routes->nr++;
    new = &s->irq_routes->entries[n];

    *new = *entry;

    set_gsi(s, entry->gsi);
}

static int kvm_update_routing_entry(KVMState *s,
                                    struct kvm_irq_routing_entry *new_entry)
{
    struct kvm_irq_routing_entry *entry;
    int n;

    for (n = 0; n < s->irq_routes->nr; n++) {
        entry = &s->irq_routes->entries[n];
        if (entry->gsi != new_entry->gsi) {
            continue;
        }

        if(!memcmp(entry, new_entry, sizeof *entry)) {
            return 0;
        }

        *entry = *new_entry;

        kvm_irqchip_commit_routes(s);

        return 0;
    }

    return -ESRCH;
}

void kvm_irqchip_add_irq_route(KVMState *s, int irq, int irqchip, int pin)
{
    struct kvm_irq_routing_entry e = {};

    assert(pin < s->gsi_count);

    e.gsi = irq;
    e.type = KVM_IRQ_ROUTING_IRQCHIP;
    e.flags = 0;
    e.u.irqchip.irqchip = irqchip;
    e.u.irqchip.pin = pin;
    kvm_add_routing_entry(s, &e);
}

void kvm_irqchip_release_virq(KVMState *s, int virq)
{
    struct kvm_irq_routing_entry *e;
    int i;

    if (kvm_gsi_direct_mapping()) {
        return;
    }

    for (i = 0; i < s->irq_routes->nr; i++) {
        e = &s->irq_routes->entries[i];
        if (e->gsi == virq) {
            s->irq_routes->nr--;
            *e = s->irq_routes->entries[s->irq_routes->nr];
        }
    }
    clear_gsi(s, virq);
}

static unsigned int kvm_hash_msi(uint32_t data)
{
    /* This is optimized for IA32 MSI layout. However, no other arch shall
     * repeat the mistake of not providing a direct MSI injection API. */
    return data & 0xff;
}

static void kvm_flush_dynamic_msi_routes(KVMState *s)
{
    KVMMSIRoute *route, *next;
    unsigned int hash;

    for (hash = 0; hash < KVM_MSI_HASHTAB_SIZE; hash++) {
        QTAILQ_FOREACH_SAFE(route, &s->msi_hashtab[hash], entry, next) {
            kvm_irqchip_release_virq(s, route->kroute.gsi);
            QTAILQ_REMOVE(&s->msi_hashtab[hash], route, entry);
            g_free(route);
        }
    }
}

static int kvm_irqchip_get_virq(KVMState *s)
{
    uint32_t *word = s->used_gsi_bitmap;
    int max_words = ALIGN(s->gsi_count, 32) / 32;
    int i, bit;
    bool retry = true;

again:
    /* Return the lowest unused GSI in the bitmap */
    for (i = 0; i < max_words; i++) {
        bit = ffs(~word[i]);
        if (!bit) {
            continue;
        }

        return bit - 1 + i * 32;
    }
    if (!s->direct_msi && retry) {
        retry = false;
        kvm_flush_dynamic_msi_routes(s);
        goto again;
    }
    return -ENOSPC;

}

static KVMMSIRoute *kvm_lookup_msi_route(KVMState *s, MSIMessage msg)
{
    unsigned int hash = kvm_hash_msi(msg.data);
    KVMMSIRoute *route;

    QTAILQ_FOREACH(route, &s->msi_hashtab[hash], entry) {
        if (route->kroute.u.msi.address_lo == (uint32_t)msg.address &&
            route->kroute.u.msi.address_hi == (msg.address >> 32) &&
            route->kroute.u.msi.data == le32_to_cpu(msg.data)) {
            return route;
        }
    }
    return NULL;
}

int kvm_irqchip_send_msi(KVMState *s, MSIMessage msg)
{
    struct kvm_msi msi;
    KVMMSIRoute *route;

    if (s->direct_msi) {
        msi.address_lo = (uint32_t)msg.address;
        msi.address_hi = msg.address >> 32;
        msi.data = le32_to_cpu(msg.data);
        msi.flags = 0;
        memset(msi.pad, 0, sizeof(msi.pad));

        return kvm_vm_ioctl(s, KVM_SIGNAL_MSI, &msi);
    }

    route = kvm_lookup_msi_route(s, msg);
    if (!route) {
        int virq;

        virq = kvm_irqchip_get_virq(s);
        if (virq < 0) {
            return virq;
        }

        route = g_malloc0(sizeof(KVMMSIRoute));
        route->kroute.gsi = virq;
        route->kroute.type = KVM_IRQ_ROUTING_MSI;
        route->kroute.flags = 0;
        route->kroute.u.msi.address_lo = (uint32_t)msg.address;
        route->kroute.u.msi.address_hi = msg.address >> 32;
        route->kroute.u.msi.data = le32_to_cpu(msg.data);

        kvm_add_routing_entry(s, &route->kroute);
        kvm_irqchip_commit_routes(s);

        QTAILQ_INSERT_TAIL(&s->msi_hashtab[kvm_hash_msi(msg.data)], route,
                           entry);
    }

    assert(route->kroute.type == KVM_IRQ_ROUTING_MSI);

    return kvm_set_irq(s, route->kroute.gsi, 1);
}

int kvm_irqchip_add_msi_route(KVMState *s, MSIMessage msg)
{
    struct kvm_irq_routing_entry kroute = {};
    int virq;

    if (kvm_gsi_direct_mapping()) {
        return msg.data & 0xffff;
    }

    if (!kvm_gsi_routing_enabled()) {
        return -ENOSYS;
    }

    virq = kvm_irqchip_get_virq(s);
    if (virq < 0) {
        return virq;
    }

    kroute.gsi = virq;
    kroute.type = KVM_IRQ_ROUTING_MSI;
    kroute.flags = 0;
    kroute.u.msi.address_lo = (uint32_t)msg.address;
    kroute.u.msi.address_hi = msg.address >> 32;
    kroute.u.msi.data = le32_to_cpu(msg.data);

    kvm_add_routing_entry(s, &kroute);
    kvm_irqchip_commit_routes(s);

    return virq;
}

int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg)
{
    struct kvm_irq_routing_entry kroute = {};

    if (kvm_gsi_direct_mapping()) {
        return 0;
    }

    if (!kvm_irqchip_in_kernel()) {
        return -ENOSYS;
    }

    kroute.gsi = virq;
    kroute.type = KVM_IRQ_ROUTING_MSI;
    kroute.flags = 0;
    kroute.u.msi.address_lo = (uint32_t)msg.address;
    kroute.u.msi.address_hi = msg.address >> 32;
    kroute.u.msi.data = le32_to_cpu(msg.data);

    return kvm_update_routing_entry(s, &kroute);
}

static int kvm_irqchip_assign_irqfd(KVMState *s, int fd, int rfd, int virq,
                                    bool assign)
{
    struct kvm_irqfd irqfd = {
        .fd = fd,
        .gsi = virq,
        .flags = assign ? 0 : KVM_IRQFD_FLAG_DEASSIGN,
    };

    if (rfd != -1) {
        irqfd.flags |= KVM_IRQFD_FLAG_RESAMPLE;
        irqfd.resamplefd = rfd;
    }

    if (!kvm_irqfds_enabled()) {
        return -ENOSYS;
    }

    return kvm_vm_ioctl(s, KVM_IRQFD, &irqfd);
}

#else /* !KVM_CAP_IRQ_ROUTING */

void kvm_init_irq_routing(KVMState *s)
{
}

void kvm_irqchip_release_virq(KVMState *s, int virq)
{
}

int kvm_irqchip_send_msi(KVMState *s, MSIMessage msg)
{
    abort();
}

int kvm_irqchip_add_msi_route(KVMState *s, MSIMessage msg)
{
    return -ENOSYS;
}

static int kvm_irqchip_assign_irqfd(KVMState *s, int fd, int virq, bool assign)
{
    abort();
}

int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg)
{
    return -ENOSYS;
}
#endif /* !KVM_CAP_IRQ_ROUTING */

int kvm_irqchip_add_irqfd_notifier(KVMState *s, EventNotifier *n,
                                   EventNotifier *rn, int virq)
{
    return kvm_irqchip_assign_irqfd(s, event_notifier_get_fd(n),
           rn ? event_notifier_get_fd(rn) : -1, virq, true);
}

int kvm_irqchip_remove_irqfd_notifier(KVMState *s, EventNotifier *n, int virq)
{
    return kvm_irqchip_assign_irqfd(s, event_notifier_get_fd(n), -1, virq,
           false);
}

static int kvm_irqchip_create(KVMState *s)
{
    int ret;

    if (!qemu_opt_get_bool(qemu_get_machine_opts(), "kernel_irqchip", true) ||
        !kvm_check_extension(s, KVM_CAP_IRQCHIP)) {
        return 0;
    }

    /* First probe and see if there's a arch-specific hook to create the
     * in-kernel irqchip for us */
    ret = kvm_arch_irqchip_create(s);
    if (ret < 0) {
        return ret;
    } else if (ret == 0) {
        ret = kvm_vm_ioctl(s, KVM_CREATE_IRQCHIP);
        if (ret < 0) {
            fprintf(stderr, "Create kernel irqchip failed\n");
            return ret;
        }
    }

    kvm_kernel_irqchip = true;
    /* If we have an in-kernel IRQ chip then we must have asynchronous
     * interrupt delivery (though the reverse is not necessarily true)
     */
    kvm_async_interrupts_allowed = true;
    kvm_halt_in_kernel_allowed = true;

    kvm_init_irq_routing(s);

    return 0;
}

/* Find number of supported CPUs using the recommended
 * procedure from the kernel API documentation to cope with
 * older kernels that may be missing capabilities.
 */
static int kvm_recommended_vcpus(KVMState *s)
{
    int ret = kvm_check_extension(s, KVM_CAP_NR_VCPUS);
    return (ret) ? ret : 4;
}

static int kvm_max_vcpus(KVMState *s)
{
    int ret = kvm_check_extension(s, KVM_CAP_MAX_VCPUS);
    return (ret) ? ret : kvm_recommended_vcpus(s);
}

int kvm_init(MachineClass *mc)
{
    static const char upgrade_note[] =
        "Please upgrade to at least kernel 2.6.29 or recent kvm-kmod\n"
        "(see http://sourceforge.net/projects/kvm).\n";
    struct {
        const char *name;
        int num;
    } num_cpus[] = {
        { "SMP",          smp_cpus },
        { "hotpluggable", max_cpus },
        { NULL, }
    }, *nc = num_cpus;
    int soft_vcpus_limit, hard_vcpus_limit;
    KVMState *s;
    const KVMCapabilityInfo *missing_cap;
    int ret;
    int i, type = 0;
    const char *kvm_type;

    s = g_malloc0(sizeof(KVMState));

    /*
     * On systems where the kernel can support different base page
     * sizes, host page size may be different from TARGET_PAGE_SIZE,
     * even with KVM.  TARGET_PAGE_SIZE is assumed to be the minimum
     * page size for the system though.
     */
    assert(TARGET_PAGE_SIZE <= getpagesize());
    page_size_init();

#ifdef KVM_CAP_SET_GUEST_DEBUG
    QTAILQ_INIT(&s->kvm_sw_breakpoints);
#endif
    s->vmfd = -1;
    s->fd = qemu_open("/dev/kvm", O_RDWR);
    if (s->fd == -1) {
        fprintf(stderr, "Could not access KVM kernel module: %m\n");
        ret = -errno;
        goto err;
    }

    ret = kvm_ioctl(s, KVM_GET_API_VERSION, 0);
    if (ret < KVM_API_VERSION) {
        if (ret >= 0) {
            ret = -EINVAL;
        }
        fprintf(stderr, "kvm version too old\n");
        goto err;
    }

    if (ret > KVM_API_VERSION) {
        ret = -EINVAL;
        fprintf(stderr, "kvm version not supported\n");
        goto err;
    }

    s->nr_slots = kvm_check_extension(s, KVM_CAP_NR_MEMSLOTS);

    /* If unspecified, use the default value */
    if (!s->nr_slots) {
        s->nr_slots = 32;
    }

    s->slots = g_malloc0(s->nr_slots * sizeof(KVMSlot));

    for (i = 0; i < s->nr_slots; i++) {
        s->slots[i].slot = i;
    }

    /* check the vcpu limits */
    soft_vcpus_limit = kvm_recommended_vcpus(s);
    hard_vcpus_limit = kvm_max_vcpus(s);

    while (nc->name) {
        if (nc->num > soft_vcpus_limit) {
            fprintf(stderr,
                    "Warning: Number of %s cpus requested (%d) exceeds "
                    "the recommended cpus supported by KVM (%d)\n",
                    nc->name, nc->num, soft_vcpus_limit);

            if (nc->num > hard_vcpus_limit) {
                fprintf(stderr, "Number of %s cpus requested (%d) exceeds "
                        "the maximum cpus supported by KVM (%d)\n",
                        nc->name, nc->num, hard_vcpus_limit);
                exit(1);
            }
        }
        nc++;
    }

    kvm_type = qemu_opt_get(qemu_get_machine_opts(), "kvm-type");
    if (mc->kvm_type) {
        type = mc->kvm_type(kvm_type);
    } else if (kvm_type) {
        ret = -EINVAL;
        fprintf(stderr, "Invalid argument kvm-type=%s\n", kvm_type);
        goto err;
    }

    do {
        ret = kvm_ioctl(s, KVM_CREATE_VM, type);
    } while (ret == -EINTR);

    if (ret < 0) {
        fprintf(stderr, "ioctl(KVM_CREATE_VM) failed: %d %s\n", -ret,
                strerror(-ret));

#ifdef TARGET_S390X
        fprintf(stderr, "Please add the 'switch_amode' kernel parameter to "
                        "your host kernel command line\n");
#endif
        goto err;
    }

    s->vmfd = ret;
    missing_cap = kvm_check_extension_list(s, kvm_required_capabilites);
    if (!missing_cap) {
        missing_cap =
            kvm_check_extension_list(s, kvm_arch_required_capabilities);
    }
    if (missing_cap) {
        ret = -EINVAL;
        fprintf(stderr, "kvm does not support %s\n%s",
                missing_cap->name, upgrade_note);
        goto err;
    }

    s->coalesced_mmio = kvm_check_extension(s, KVM_CAP_COALESCED_MMIO);

    s->broken_set_mem_region = 1;
    ret = kvm_check_extension(s, KVM_CAP_JOIN_MEMORY_REGIONS_WORKS);
    if (ret > 0) {
        s->broken_set_mem_region = 0;
    }

#ifdef KVM_CAP_VCPU_EVENTS
    s->vcpu_events = kvm_check_extension(s, KVM_CAP_VCPU_EVENTS);
#endif

    s->robust_singlestep =
        kvm_check_extension(s, KVM_CAP_X86_ROBUST_SINGLESTEP);

#ifdef KVM_CAP_DEBUGREGS
    s->debugregs = kvm_check_extension(s, KVM_CAP_DEBUGREGS);
#endif

#ifdef KVM_CAP_XSAVE
    s->xsave = kvm_check_extension(s, KVM_CAP_XSAVE);
#endif

#ifdef KVM_CAP_XCRS
    s->xcrs = kvm_check_extension(s, KVM_CAP_XCRS);
#endif

#ifdef KVM_CAP_PIT_STATE2
    s->pit_state2 = kvm_check_extension(s, KVM_CAP_PIT_STATE2);
#endif

#ifdef KVM_CAP_IRQ_ROUTING
    s->direct_msi = (kvm_check_extension(s, KVM_CAP_SIGNAL_MSI) > 0);
#endif

    s->intx_set_mask = kvm_check_extension(s, KVM_CAP_PCI_2_3);

    s->irq_set_ioctl = KVM_IRQ_LINE;
    if (kvm_check_extension(s, KVM_CAP_IRQ_INJECT_STATUS)) {
        s->irq_set_ioctl = KVM_IRQ_LINE_STATUS;
    }

#ifdef KVM_CAP_READONLY_MEM
    kvm_readonly_mem_allowed =
        (kvm_check_extension(s, KVM_CAP_READONLY_MEM) > 0);
#endif

    ret = kvm_arch_init(s);
    if (ret < 0) {
        goto err;
    }

    ret = kvm_irqchip_create(s);
    if (ret < 0) {
        goto err;
    }

    kvm_state = s;
    memory_listener_register(&kvm_memory_listener, &address_space_memory);
    memory_listener_register(&kvm_io_listener, &address_space_io);

    s->many_ioeventfds = kvm_check_many_ioeventfds();

    cpu_interrupt_handler = kvm_handle_interrupt;

    return 0;

err:
    assert(ret < 0);
    if (s->vmfd >= 0) {
        close(s->vmfd);
    }
    if (s->fd != -1) {
        close(s->fd);
    }
    g_free(s->slots);
    g_free(s);

    return ret;
}

static void kvm_handle_io(uint16_t port, void *data, int direction, int size,
                          uint32_t count)
{
    int i;
    uint8_t *ptr = data;

    for (i = 0; i < count; i++) {
        address_space_rw(&address_space_io, port, ptr, size,
                         direction == KVM_EXIT_IO_OUT);
        ptr += size;
    }
}

static int kvm_handle_internal_error(CPUState *cpu, struct kvm_run *run)
{
    fprintf(stderr, "KVM internal error. Suberror: %d\n",
            run->internal.suberror);

    if (kvm_check_extension(kvm_state, KVM_CAP_INTERNAL_ERROR_DATA)) {
        int i;

        for (i = 0; i < run->internal.ndata; ++i) {
            fprintf(stderr, "extra data[%d]: %"PRIx64"\n",
                    i, (uint64_t)run->internal.data[i]);
        }
    }
    if (run->internal.suberror == KVM_INTERNAL_ERROR_EMULATION) {
        fprintf(stderr, "emulation failure\n");
        if (!kvm_arch_stop_on_emulation_error(cpu)) {
            cpu_dump_state(cpu, stderr, fprintf, CPU_DUMP_CODE);
            return EXCP_INTERRUPT;
        }
    }
    /* FIXME: Should trigger a qmp message to let management know
     * something went wrong.
     */
    return -1;
}

void kvm_flush_coalesced_mmio_buffer(void)
{
    KVMState *s = kvm_state;

    if (s->coalesced_flush_in_progress) {
        return;
    }

    s->coalesced_flush_in_progress = true;

    if (s->coalesced_mmio_ring) {
        struct kvm_coalesced_mmio_ring *ring = s->coalesced_mmio_ring;
        while (ring->first != ring->last) {
            struct kvm_coalesced_mmio *ent;

            ent = &ring->coalesced_mmio[ring->first];

            cpu_physical_memory_write(ent->phys_addr, ent->data, ent->len);
            smp_wmb();
            ring->first = (ring->first + 1) % KVM_COALESCED_MMIO_MAX;
        }
    }

    s->coalesced_flush_in_progress = false;
}

static void do_kvm_cpu_synchronize_state(void *arg)
{
    CPUState *cpu = arg;

    if (!cpu->kvm_vcpu_dirty) {
        kvm_arch_get_registers(cpu);
        cpu->kvm_vcpu_dirty = true;
    }
}

void kvm_cpu_synchronize_state(CPUState *cpu)
{
    if (!cpu->kvm_vcpu_dirty) {
        run_on_cpu(cpu, do_kvm_cpu_synchronize_state, cpu);
    }
}

void kvm_cpu_synchronize_post_reset(CPUState *cpu)
{
    kvm_arch_put_registers(cpu, KVM_PUT_RESET_STATE);
    cpu->kvm_vcpu_dirty = false;
}

void kvm_cpu_synchronize_post_init(CPUState *cpu)
{
    kvm_arch_put_registers(cpu, KVM_PUT_FULL_STATE);
    cpu->kvm_vcpu_dirty = false;
}

int kvm_cpu_exec(CPUState *cpu)
{
    struct kvm_run *run = cpu->kvm_run;
    int ret, run_ret;

    DPRINTF("kvm_cpu_exec()\n");

    if (kvm_arch_process_async_events(cpu)) {
        cpu->exit_request = 0;
        return EXCP_HLT;
    }

    do {
        if (cpu->kvm_vcpu_dirty) {
            kvm_arch_put_registers(cpu, KVM_PUT_RUNTIME_STATE);
            cpu->kvm_vcpu_dirty = false;
        }

        kvm_arch_pre_run(cpu, run);
        if (cpu->exit_request) {
            DPRINTF("interrupt exit requested\n");
            /*
             * KVM requires us to reenter the kernel after IO exits to complete
             * instruction emulation. This self-signal will ensure that we
             * leave ASAP again.
             */
            qemu_cpu_kick_self();
        }
        qemu_mutex_unlock_iothread();

        run_ret = kvm_vcpu_ioctl(cpu, KVM_RUN, 0);

        qemu_mutex_lock_iothread();
        kvm_arch_post_run(cpu, run);

        if (run_ret < 0) {
            if (run_ret == -EINTR || run_ret == -EAGAIN) {
                DPRINTF("io window exit\n");
                ret = EXCP_INTERRUPT;
                break;
            }
            fprintf(stderr, "error: kvm run failed %s\n",
                    strerror(-run_ret));
            abort();
        }

        trace_kvm_run_exit(cpu->cpu_index, run->exit_reason);
        switch (run->exit_reason) {
        case KVM_EXIT_IO:
            DPRINTF("handle_io\n");
            kvm_handle_io(run->io.port,
                          (uint8_t *)run + run->io.data_offset,
                          run->io.direction,
                          run->io.size,
                          run->io.count);
            ret = 0;
            break;
        case KVM_EXIT_MMIO:
            DPRINTF("handle_mmio\n");
            cpu_physical_memory_rw(run->mmio.phys_addr,
                                   run->mmio.data,
                                   run->mmio.len,
                                   run->mmio.is_write);
            ret = 0;
            break;
        case KVM_EXIT_IRQ_WINDOW_OPEN:
            DPRINTF("irq_window_open\n");
            ret = EXCP_INTERRUPT;
            break;
        case KVM_EXIT_SHUTDOWN:
            DPRINTF("shutdown\n");
            qemu_system_reset_request();
            ret = EXCP_INTERRUPT;
            break;
        case KVM_EXIT_UNKNOWN:
            fprintf(stderr, "KVM: unknown exit, hardware reason %" PRIx64 "\n",
                    (uint64_t)run->hw.hardware_exit_reason);
            ret = -1;
            break;
        case KVM_EXIT_INTERNAL_ERROR:
            ret = kvm_handle_internal_error(cpu, run);
            break;
        default:
            DPRINTF("kvm_arch_handle_exit\n");
            ret = kvm_arch_handle_exit(cpu, run);
            break;
        }
    } while (ret == 0);

    if (ret < 0) {
        cpu_dump_state(cpu, stderr, fprintf, CPU_DUMP_CODE);
        vm_stop(RUN_STATE_INTERNAL_ERROR);
    }

    cpu->exit_request = 0;
    return ret;
}

int kvm_ioctl(KVMState *s, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    trace_kvm_ioctl(type, arg);
    ret = ioctl(s->fd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
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

    trace_kvm_vm_ioctl(type, arg);
    ret = ioctl(s->vmfd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}

int kvm_vcpu_ioctl(CPUState *cpu, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    trace_kvm_vcpu_ioctl(cpu->cpu_index, type, arg);
    ret = ioctl(cpu->kvm_fd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}

int kvm_device_ioctl(int fd, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    trace_kvm_device_ioctl(fd, type, arg);
    ret = ioctl(fd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}

int kvm_has_sync_mmu(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_SYNC_MMU);
}

int kvm_has_vcpu_events(void)
{
    return kvm_state->vcpu_events;
}

int kvm_has_robust_singlestep(void)
{
    return kvm_state->robust_singlestep;
}

int kvm_has_debugregs(void)
{
    return kvm_state->debugregs;
}

int kvm_has_xsave(void)
{
    return kvm_state->xsave;
}

int kvm_has_xcrs(void)
{
    return kvm_state->xcrs;
}

int kvm_has_pit_state2(void)
{
    return kvm_state->pit_state2;
}

int kvm_has_many_ioeventfds(void)
{
    if (!kvm_enabled()) {
        return 0;
    }
    return kvm_state->many_ioeventfds;
}

int kvm_has_gsi_routing(void)
{
#ifdef KVM_CAP_IRQ_ROUTING
    return kvm_check_extension(kvm_state, KVM_CAP_IRQ_ROUTING);
#else
    return false;
#endif
}

int kvm_has_intx_set_mask(void)
{
    return kvm_state->intx_set_mask;
}

void kvm_setup_guest_memory(void *start, size_t size)
{
#ifdef CONFIG_VALGRIND_H
    VALGRIND_MAKE_MEM_DEFINED(start, size);
#endif
    if (!kvm_has_sync_mmu()) {
        int ret = qemu_madvise(start, size, QEMU_MADV_DONTFORK);

        if (ret) {
            perror("qemu_madvise");
            fprintf(stderr,
                    "Need MADV_DONTFORK in absence of synchronous KVM MMU\n");
            exit(1);
        }
    }
}

#ifdef KVM_CAP_SET_GUEST_DEBUG
struct kvm_sw_breakpoint *kvm_find_sw_breakpoint(CPUState *cpu,
                                                 target_ulong pc)
{
    struct kvm_sw_breakpoint *bp;

    QTAILQ_FOREACH(bp, &cpu->kvm_state->kvm_sw_breakpoints, entry) {
        if (bp->pc == pc) {
            return bp;
        }
    }
    return NULL;
}

int kvm_sw_breakpoints_active(CPUState *cpu)
{
    return !QTAILQ_EMPTY(&cpu->kvm_state->kvm_sw_breakpoints);
}

struct kvm_set_guest_debug_data {
    struct kvm_guest_debug dbg;
    CPUState *cpu;
    int err;
};

static void kvm_invoke_set_guest_debug(void *data)
{
    struct kvm_set_guest_debug_data *dbg_data = data;

    dbg_data->err = kvm_vcpu_ioctl(dbg_data->cpu, KVM_SET_GUEST_DEBUG,
                                   &dbg_data->dbg);
}

int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap)
{
    struct kvm_set_guest_debug_data data;

    data.dbg.control = reinject_trap;

    if (cpu->singlestep_enabled) {
        data.dbg.control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;
    }
    kvm_arch_update_guest_debug(cpu, &data.dbg);
    data.cpu = cpu;

    run_on_cpu(cpu, kvm_invoke_set_guest_debug, &data);
    return data.err;
}

int kvm_insert_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type)
{
    struct kvm_sw_breakpoint *bp;
    int err;

    if (type == GDB_BREAKPOINT_SW) {
        bp = kvm_find_sw_breakpoint(cpu, addr);
        if (bp) {
            bp->use_count++;
            return 0;
        }

        bp = g_malloc(sizeof(struct kvm_sw_breakpoint));
        if (!bp) {
            return -ENOMEM;
        }

        bp->pc = addr;
        bp->use_count = 1;
        err = kvm_arch_insert_sw_breakpoint(cpu, bp);
        if (err) {
            g_free(bp);
            return err;
        }

        QTAILQ_INSERT_HEAD(&cpu->kvm_state->kvm_sw_breakpoints, bp, entry);
    } else {
        err = kvm_arch_insert_hw_breakpoint(addr, len, type);
        if (err) {
            return err;
        }
    }

    CPU_FOREACH(cpu) {
        err = kvm_update_guest_debug(cpu, 0);
        if (err) {
            return err;
        }
    }
    return 0;
}

int kvm_remove_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type)
{
    struct kvm_sw_breakpoint *bp;
    int err;

    if (type == GDB_BREAKPOINT_SW) {
        bp = kvm_find_sw_breakpoint(cpu, addr);
        if (!bp) {
            return -ENOENT;
        }

        if (bp->use_count > 1) {
            bp->use_count--;
            return 0;
        }

        err = kvm_arch_remove_sw_breakpoint(cpu, bp);
        if (err) {
            return err;
        }

        QTAILQ_REMOVE(&cpu->kvm_state->kvm_sw_breakpoints, bp, entry);
        g_free(bp);
    } else {
        err = kvm_arch_remove_hw_breakpoint(addr, len, type);
        if (err) {
            return err;
        }
    }

    CPU_FOREACH(cpu) {
        err = kvm_update_guest_debug(cpu, 0);
        if (err) {
            return err;
        }
    }
    return 0;
}

void kvm_remove_all_breakpoints(CPUState *cpu)
{
    struct kvm_sw_breakpoint *bp, *next;
    KVMState *s = cpu->kvm_state;

    QTAILQ_FOREACH_SAFE(bp, &s->kvm_sw_breakpoints, entry, next) {
        if (kvm_arch_remove_sw_breakpoint(cpu, bp) != 0) {
            /* Try harder to find a CPU that currently sees the breakpoint. */
            CPU_FOREACH(cpu) {
                if (kvm_arch_remove_sw_breakpoint(cpu, bp) == 0) {
                    break;
                }
            }
        }
        QTAILQ_REMOVE(&s->kvm_sw_breakpoints, bp, entry);
        g_free(bp);
    }
    kvm_arch_remove_all_hw_breakpoints();

    CPU_FOREACH(cpu) {
        kvm_update_guest_debug(cpu, 0);
    }
}

#else /* !KVM_CAP_SET_GUEST_DEBUG */

int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap)
{
    return -EINVAL;
}

int kvm_insert_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

int kvm_remove_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

void kvm_remove_all_breakpoints(CPUState *cpu)
{
}
#endif /* !KVM_CAP_SET_GUEST_DEBUG */

int kvm_set_signal_mask(CPUState *cpu, const sigset_t *sigset)
{
    struct kvm_signal_mask *sigmask;
    int r;

    if (!sigset) {
        return kvm_vcpu_ioctl(cpu, KVM_SET_SIGNAL_MASK, NULL);
    }

    sigmask = g_malloc(sizeof(*sigmask) + sizeof(*sigset));

    sigmask->len = 8;
    memcpy(sigmask->sigset, sigset, sizeof(*sigset));
    r = kvm_vcpu_ioctl(cpu, KVM_SET_SIGNAL_MASK, sigmask);
    g_free(sigmask);

    return r;
}
int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr)
{
    return kvm_arch_on_sigbus_vcpu(cpu, code, addr);
}

int kvm_on_sigbus(int code, void *addr)
{
    return kvm_arch_on_sigbus(code, addr);
}

int kvm_create_device(KVMState *s, uint64_t type, bool test)
{
    int ret;
    struct kvm_create_device create_dev;

    create_dev.type = type;
    create_dev.fd = -1;
    create_dev.flags = test ? KVM_CREATE_DEVICE_TEST : 0;

    if (!kvm_check_extension(s, KVM_CAP_DEVICE_CTRL)) {
        return -ENOTSUP;
    }

    ret = kvm_vm_ioctl(s, KVM_CREATE_DEVICE, &create_dev);
    if (ret) {
        return ret;
    }

    return test ? 0 : create_dev.fd;
}

int kvm_set_one_reg(CPUState *cs, uint64_t id, void *source)
{
    struct kvm_one_reg reg;
    int r;

    reg.id = id;
    reg.addr = (uintptr_t) source;
    r = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (r) {
        trace_kvm_failed_reg_set(id, strerror(r));
    }
    return r;
}

int kvm_get_one_reg(CPUState *cs, uint64_t id, void *target)
{
    struct kvm_one_reg reg;
    int r;

    reg.id = id;
    reg.addr = (uintptr_t) target;
    r = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (r) {
        trace_kvm_failed_reg_get(id, strerror(r));
    }
    return r;
}
