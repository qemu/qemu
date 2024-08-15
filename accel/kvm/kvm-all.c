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

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <poll.h>

#include <linux/kvm.h>

#include "qemu/atomic.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/s390x/adapter.h"
#include "gdbstub/enums.h"
#include "sysemu/kvm_int.h"
#include "sysemu/runstate.h"
#include "sysemu/cpus.h"
#include "sysemu/accel-blocker.h"
#include "qemu/bswap.h"
#include "exec/memory.h"
#include "exec/ram_addr.h"
#include "qemu/event_notifier.h"
#include "qemu/main-loop.h"
#include "trace.h"
#include "hw/irq.h"
#include "qapi/visitor.h"
#include "qapi/qapi-types-common.h"
#include "qapi/qapi-visit-common.h"
#include "sysemu/reset.h"
#include "qemu/guest-random.h"
#include "sysemu/hw_accel.h"
#include "kvm-cpus.h"
#include "sysemu/dirtylimit.h"
#include "qemu/range.h"

#include "hw/boards.h"
#include "sysemu/stats.h"

/* This check must be after config-host.h is included */
#ifdef CONFIG_EVENTFD
#include <sys/eventfd.h>
#endif

/* KVM uses PAGE_SIZE in its definition of KVM_COALESCED_MMIO_MAX. We
 * need to use the real host PAGE_SIZE, as that's what KVM will use.
 */
#ifdef PAGE_SIZE
#undef PAGE_SIZE
#endif
#define PAGE_SIZE qemu_real_host_page_size()

#ifndef KVM_GUESTDBG_BLOCKIRQ
#define KVM_GUESTDBG_BLOCKIRQ 0
#endif

struct KVMParkedVcpu {
    unsigned long vcpu_id;
    int kvm_fd;
    QLIST_ENTRY(KVMParkedVcpu) node;
};

KVMState *kvm_state;
bool kvm_kernel_irqchip;
bool kvm_split_irqchip;
bool kvm_async_interrupts_allowed;
bool kvm_halt_in_kernel_allowed;
bool kvm_resamplefds_allowed;
bool kvm_msi_via_irqfd_allowed;
bool kvm_gsi_routing_allowed;
bool kvm_gsi_direct_mapping;
bool kvm_allowed;
bool kvm_readonly_mem_allowed;
bool kvm_vm_attributes_allowed;
bool kvm_msi_use_devid;
static bool kvm_has_guest_debug;
static int kvm_sstep_flags;
static bool kvm_immediate_exit;
static uint64_t kvm_supported_memory_attributes;
static bool kvm_guest_memfd_supported;
static hwaddr kvm_max_slot_size = ~0;

static const KVMCapabilityInfo kvm_required_capabilites[] = {
    KVM_CAP_INFO(USER_MEMORY),
    KVM_CAP_INFO(DESTROY_MEMORY_REGION_WORKS),
    KVM_CAP_INFO(JOIN_MEMORY_REGIONS_WORKS),
    KVM_CAP_INFO(INTERNAL_ERROR_DATA),
    KVM_CAP_INFO(IOEVENTFD),
    KVM_CAP_INFO(IOEVENTFD_ANY_LENGTH),
    KVM_CAP_LAST_INFO
};

static NotifierList kvm_irqchip_change_notifiers =
    NOTIFIER_LIST_INITIALIZER(kvm_irqchip_change_notifiers);

struct KVMResampleFd {
    int gsi;
    EventNotifier *resample_event;
    QLIST_ENTRY(KVMResampleFd) node;
};
typedef struct KVMResampleFd KVMResampleFd;

/*
 * Only used with split irqchip where we need to do the resample fd
 * kick for the kernel from userspace.
 */
static QLIST_HEAD(, KVMResampleFd) kvm_resample_fd_list =
    QLIST_HEAD_INITIALIZER(kvm_resample_fd_list);

static QemuMutex kml_slots_lock;

#define kvm_slots_lock()    qemu_mutex_lock(&kml_slots_lock)
#define kvm_slots_unlock()  qemu_mutex_unlock(&kml_slots_lock)

static void kvm_slot_init_dirty_bitmap(KVMSlot *mem);

static inline void kvm_resample_fd_remove(int gsi)
{
    KVMResampleFd *rfd;

    QLIST_FOREACH(rfd, &kvm_resample_fd_list, node) {
        if (rfd->gsi == gsi) {
            QLIST_REMOVE(rfd, node);
            g_free(rfd);
            break;
        }
    }
}

static inline void kvm_resample_fd_insert(int gsi, EventNotifier *event)
{
    KVMResampleFd *rfd = g_new0(KVMResampleFd, 1);

    rfd->gsi = gsi;
    rfd->resample_event = event;

    QLIST_INSERT_HEAD(&kvm_resample_fd_list, rfd, node);
}

void kvm_resample_fd_notify(int gsi)
{
    KVMResampleFd *rfd;

    QLIST_FOREACH(rfd, &kvm_resample_fd_list, node) {
        if (rfd->gsi == gsi) {
            event_notifier_set(rfd->resample_event);
            trace_kvm_resample_fd_notify(gsi);
            return;
        }
    }
}

unsigned int kvm_get_max_memslots(void)
{
    KVMState *s = KVM_STATE(current_accel());

    return s->nr_slots;
}

unsigned int kvm_get_free_memslots(void)
{
    unsigned int used_slots = 0;
    KVMState *s = kvm_state;
    int i;

    kvm_slots_lock();
    for (i = 0; i < s->nr_as; i++) {
        if (!s->as[i].ml) {
            continue;
        }
        used_slots = MAX(used_slots, s->as[i].ml->nr_used_slots);
    }
    kvm_slots_unlock();

    return s->nr_slots - used_slots;
}

/* Called with KVMMemoryListener.slots_lock held */
static KVMSlot *kvm_get_free_slot(KVMMemoryListener *kml)
{
    KVMState *s = kvm_state;
    int i;

    for (i = 0; i < s->nr_slots; i++) {
        if (kml->slots[i].memory_size == 0) {
            return &kml->slots[i];
        }
    }

    return NULL;
}

/* Called with KVMMemoryListener.slots_lock held */
static KVMSlot *kvm_alloc_slot(KVMMemoryListener *kml)
{
    KVMSlot *slot = kvm_get_free_slot(kml);

    if (slot) {
        return slot;
    }

    fprintf(stderr, "%s: no free slot available\n", __func__);
    abort();
}

static KVMSlot *kvm_lookup_matching_slot(KVMMemoryListener *kml,
                                         hwaddr start_addr,
                                         hwaddr size)
{
    KVMState *s = kvm_state;
    int i;

    for (i = 0; i < s->nr_slots; i++) {
        KVMSlot *mem = &kml->slots[i];

        if (start_addr == mem->start_addr && size == mem->memory_size) {
            return mem;
        }
    }

    return NULL;
}

/*
 * Calculate and align the start address and the size of the section.
 * Return the size. If the size is 0, the aligned section is empty.
 */
static hwaddr kvm_align_section(MemoryRegionSection *section,
                                hwaddr *start)
{
    hwaddr size = int128_get64(section->size);
    hwaddr delta, aligned;

    /* kvm works in page size chunks, but the function may be called
       with sub-page size and unaligned start address. Pad the start
       address to next and truncate size to previous page boundary. */
    aligned = ROUND_UP(section->offset_within_address_space,
                       qemu_real_host_page_size());
    delta = aligned - section->offset_within_address_space;
    *start = aligned;
    if (delta > size) {
        return 0;
    }

    return (size - delta) & qemu_real_host_page_mask();
}

int kvm_physical_memory_addr_from_host(KVMState *s, void *ram,
                                       hwaddr *phys_addr)
{
    KVMMemoryListener *kml = &s->memory_listener;
    int i, ret = 0;

    kvm_slots_lock();
    for (i = 0; i < s->nr_slots; i++) {
        KVMSlot *mem = &kml->slots[i];

        if (ram >= mem->ram && ram < mem->ram + mem->memory_size) {
            *phys_addr = mem->start_addr + (ram - mem->ram);
            ret = 1;
            break;
        }
    }
    kvm_slots_unlock();

    return ret;
}

static int kvm_set_user_memory_region(KVMMemoryListener *kml, KVMSlot *slot, bool new)
{
    KVMState *s = kvm_state;
    struct kvm_userspace_memory_region2 mem;
    int ret;

    mem.slot = slot->slot | (kml->as_id << 16);
    mem.guest_phys_addr = slot->start_addr;
    mem.userspace_addr = (unsigned long)slot->ram;
    mem.flags = slot->flags;
    mem.guest_memfd = slot->guest_memfd;
    mem.guest_memfd_offset = slot->guest_memfd_offset;

    if (slot->memory_size && !new && (mem.flags ^ slot->old_flags) & KVM_MEM_READONLY) {
        /* Set the slot size to 0 before setting the slot to the desired
         * value. This is needed based on KVM commit 75d61fbc. */
        mem.memory_size = 0;

        if (kvm_guest_memfd_supported) {
            ret = kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION2, &mem);
        } else {
            ret = kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
        }
        if (ret < 0) {
            goto err;
        }
    }
    mem.memory_size = slot->memory_size;
    if (kvm_guest_memfd_supported) {
        ret = kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION2, &mem);
    } else {
        ret = kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
    }
    slot->old_flags = mem.flags;
err:
    trace_kvm_set_user_memory(mem.slot >> 16, (uint16_t)mem.slot, mem.flags,
                              mem.guest_phys_addr, mem.memory_size,
                              mem.userspace_addr, mem.guest_memfd,
                              mem.guest_memfd_offset, ret);
    if (ret < 0) {
        if (kvm_guest_memfd_supported) {
                error_report("%s: KVM_SET_USER_MEMORY_REGION2 failed, slot=%d,"
                        " start=0x%" PRIx64 ", size=0x%" PRIx64 ","
                        " flags=0x%" PRIx32 ", guest_memfd=%" PRId32 ","
                        " guest_memfd_offset=0x%" PRIx64 ": %s",
                        __func__, mem.slot, slot->start_addr,
                        (uint64_t)mem.memory_size, mem.flags,
                        mem.guest_memfd, (uint64_t)mem.guest_memfd_offset,
                        strerror(errno));
        } else {
                error_report("%s: KVM_SET_USER_MEMORY_REGION failed, slot=%d,"
                            " start=0x%" PRIx64 ", size=0x%" PRIx64 ": %s",
                            __func__, mem.slot, slot->start_addr,
                            (uint64_t)mem.memory_size, strerror(errno));
        }
    }
    return ret;
}

void kvm_park_vcpu(CPUState *cpu)
{
    struct KVMParkedVcpu *vcpu;

    trace_kvm_park_vcpu(cpu->cpu_index, kvm_arch_vcpu_id(cpu));

    vcpu = g_malloc0(sizeof(*vcpu));
    vcpu->vcpu_id = kvm_arch_vcpu_id(cpu);
    vcpu->kvm_fd = cpu->kvm_fd;
    QLIST_INSERT_HEAD(&kvm_state->kvm_parked_vcpus, vcpu, node);
}

int kvm_unpark_vcpu(KVMState *s, unsigned long vcpu_id)
{
    struct KVMParkedVcpu *cpu;
    int kvm_fd = -ENOENT;

    QLIST_FOREACH(cpu, &s->kvm_parked_vcpus, node) {
        if (cpu->vcpu_id == vcpu_id) {
            QLIST_REMOVE(cpu, node);
            kvm_fd = cpu->kvm_fd;
            g_free(cpu);
            break;
        }
    }

    trace_kvm_unpark_vcpu(vcpu_id, kvm_fd > 0 ? "unparked" : "!found parked");

    return kvm_fd;
}

int kvm_create_vcpu(CPUState *cpu)
{
    unsigned long vcpu_id = kvm_arch_vcpu_id(cpu);
    KVMState *s = kvm_state;
    int kvm_fd;

    /* check if the KVM vCPU already exist but is parked */
    kvm_fd = kvm_unpark_vcpu(s, vcpu_id);
    if (kvm_fd < 0) {
        /* vCPU not parked: create a new KVM vCPU */
        kvm_fd = kvm_vm_ioctl(s, KVM_CREATE_VCPU, vcpu_id);
        if (kvm_fd < 0) {
            error_report("KVM_CREATE_VCPU IOCTL failed for vCPU %lu", vcpu_id);
            return kvm_fd;
        }
    }

    cpu->kvm_fd = kvm_fd;
    cpu->kvm_state = s;
    cpu->vcpu_dirty = true;
    cpu->dirty_pages = 0;
    cpu->throttle_us_per_full = 0;

    trace_kvm_create_vcpu(cpu->cpu_index, vcpu_id, kvm_fd);

    return 0;
}

int kvm_create_and_park_vcpu(CPUState *cpu)
{
    int ret = 0;

    ret = kvm_create_vcpu(cpu);
    if (!ret) {
        kvm_park_vcpu(cpu);
    }

    return ret;
}

static int do_kvm_destroy_vcpu(CPUState *cpu)
{
    KVMState *s = kvm_state;
    long mmap_size;
    int ret = 0;

    trace_kvm_destroy_vcpu(cpu->cpu_index, kvm_arch_vcpu_id(cpu));

    ret = kvm_arch_destroy_vcpu(cpu);
    if (ret < 0) {
        goto err;
    }

    mmap_size = kvm_ioctl(s, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size < 0) {
        ret = mmap_size;
        trace_kvm_failed_get_vcpu_mmap_size();
        goto err;
    }

    ret = munmap(cpu->kvm_run, mmap_size);
    if (ret < 0) {
        goto err;
    }

    if (cpu->kvm_dirty_gfns) {
        ret = munmap(cpu->kvm_dirty_gfns, s->kvm_dirty_ring_bytes);
        if (ret < 0) {
            goto err;
        }
    }

    kvm_park_vcpu(cpu);
err:
    return ret;
}

void kvm_destroy_vcpu(CPUState *cpu)
{
    if (do_kvm_destroy_vcpu(cpu) < 0) {
        error_report("kvm_destroy_vcpu failed");
        exit(EXIT_FAILURE);
    }
}

int kvm_init_vcpu(CPUState *cpu, Error **errp)
{
    KVMState *s = kvm_state;
    long mmap_size;
    int ret;

    trace_kvm_init_vcpu(cpu->cpu_index, kvm_arch_vcpu_id(cpu));

    ret = kvm_create_vcpu(cpu);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "kvm_init_vcpu: kvm_create_vcpu failed (%lu)",
                         kvm_arch_vcpu_id(cpu));
        goto err;
    }

    mmap_size = kvm_ioctl(s, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size < 0) {
        ret = mmap_size;
        error_setg_errno(errp, -mmap_size,
                         "kvm_init_vcpu: KVM_GET_VCPU_MMAP_SIZE failed");
        goto err;
    }

    cpu->kvm_run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        cpu->kvm_fd, 0);
    if (cpu->kvm_run == MAP_FAILED) {
        ret = -errno;
        error_setg_errno(errp, ret,
                         "kvm_init_vcpu: mmap'ing vcpu state failed (%lu)",
                         kvm_arch_vcpu_id(cpu));
        goto err;
    }

    if (s->coalesced_mmio && !s->coalesced_mmio_ring) {
        s->coalesced_mmio_ring =
            (void *)cpu->kvm_run + s->coalesced_mmio * PAGE_SIZE;
    }

    if (s->kvm_dirty_ring_size) {
        /* Use MAP_SHARED to share pages with the kernel */
        cpu->kvm_dirty_gfns = mmap(NULL, s->kvm_dirty_ring_bytes,
                                   PROT_READ | PROT_WRITE, MAP_SHARED,
                                   cpu->kvm_fd,
                                   PAGE_SIZE * KVM_DIRTY_LOG_PAGE_OFFSET);
        if (cpu->kvm_dirty_gfns == MAP_FAILED) {
            ret = -errno;
            goto err;
        }
    }

    ret = kvm_arch_init_vcpu(cpu);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "kvm_init_vcpu: kvm_arch_init_vcpu failed (%lu)",
                         kvm_arch_vcpu_id(cpu));
    }
    cpu->kvm_vcpu_stats_fd = kvm_vcpu_ioctl(cpu, KVM_GET_STATS_FD, NULL);

err:
    return ret;
}

/*
 * dirty pages logging control
 */

static int kvm_mem_flags(MemoryRegion *mr)
{
    bool readonly = mr->readonly || memory_region_is_romd(mr);
    int flags = 0;

    if (memory_region_get_dirty_log_mask(mr) != 0) {
        flags |= KVM_MEM_LOG_DIRTY_PAGES;
    }
    if (readonly && kvm_readonly_mem_allowed) {
        flags |= KVM_MEM_READONLY;
    }
    if (memory_region_has_guest_memfd(mr)) {
        assert(kvm_guest_memfd_supported);
        flags |= KVM_MEM_GUEST_MEMFD;
    }
    return flags;
}

/* Called with KVMMemoryListener.slots_lock held */
static int kvm_slot_update_flags(KVMMemoryListener *kml, KVMSlot *mem,
                                 MemoryRegion *mr)
{
    mem->flags = kvm_mem_flags(mr);

    /* If nothing changed effectively, no need to issue ioctl */
    if (mem->flags == mem->old_flags) {
        return 0;
    }

    kvm_slot_init_dirty_bitmap(mem);
    return kvm_set_user_memory_region(kml, mem, false);
}

static int kvm_section_update_flags(KVMMemoryListener *kml,
                                    MemoryRegionSection *section)
{
    hwaddr start_addr, size, slot_size;
    KVMSlot *mem;
    int ret = 0;

    size = kvm_align_section(section, &start_addr);
    if (!size) {
        return 0;
    }

    kvm_slots_lock();

    while (size && !ret) {
        slot_size = MIN(kvm_max_slot_size, size);
        mem = kvm_lookup_matching_slot(kml, start_addr, slot_size);
        if (!mem) {
            /* We don't have a slot if we want to trap every access. */
            goto out;
        }

        ret = kvm_slot_update_flags(kml, mem, section->mr);
        start_addr += slot_size;
        size -= slot_size;
    }

out:
    kvm_slots_unlock();
    return ret;
}

static void kvm_log_start(MemoryListener *listener,
                          MemoryRegionSection *section,
                          int old, int new)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);
    int r;

    if (old != 0) {
        return;
    }

    r = kvm_section_update_flags(kml, section);
    if (r < 0) {
        abort();
    }
}

static void kvm_log_stop(MemoryListener *listener,
                          MemoryRegionSection *section,
                          int old, int new)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);
    int r;

    if (new != 0) {
        return;
    }

    r = kvm_section_update_flags(kml, section);
    if (r < 0) {
        abort();
    }
}

/* get kvm's dirty pages bitmap and update qemu's */
static void kvm_slot_sync_dirty_pages(KVMSlot *slot)
{
    ram_addr_t start = slot->ram_start_offset;
    ram_addr_t pages = slot->memory_size / qemu_real_host_page_size();

    cpu_physical_memory_set_dirty_lebitmap(slot->dirty_bmap, start, pages);
}

static void kvm_slot_reset_dirty_pages(KVMSlot *slot)
{
    memset(slot->dirty_bmap, 0, slot->dirty_bmap_size);
}

#define ALIGN(x, y)  (((x)+(y)-1) & ~((y)-1))

/* Allocate the dirty bitmap for a slot  */
static void kvm_slot_init_dirty_bitmap(KVMSlot *mem)
{
    if (!(mem->flags & KVM_MEM_LOG_DIRTY_PAGES) || mem->dirty_bmap) {
        return;
    }

    /*
     * XXX bad kernel interface alert
     * For dirty bitmap, kernel allocates array of size aligned to
     * bits-per-long.  But for case when the kernel is 64bits and
     * the userspace is 32bits, userspace can't align to the same
     * bits-per-long, since sizeof(long) is different between kernel
     * and user space.  This way, userspace will provide buffer which
     * may be 4 bytes less than the kernel will use, resulting in
     * userspace memory corruption (which is not detectable by valgrind
     * too, in most cases).
     * So for now, let's align to 64 instead of HOST_LONG_BITS here, in
     * a hope that sizeof(long) won't become >8 any time soon.
     *
     * Note: the granule of kvm dirty log is qemu_real_host_page_size.
     * And mem->memory_size is aligned to it (otherwise this mem can't
     * be registered to KVM).
     */
    hwaddr bitmap_size = ALIGN(mem->memory_size / qemu_real_host_page_size(),
                                        /*HOST_LONG_BITS*/ 64) / 8;
    mem->dirty_bmap = g_malloc0(bitmap_size);
    mem->dirty_bmap_size = bitmap_size;
}

/*
 * Sync dirty bitmap from kernel to KVMSlot.dirty_bmap, return true if
 * succeeded, false otherwise
 */
static bool kvm_slot_get_dirty_log(KVMState *s, KVMSlot *slot)
{
    struct kvm_dirty_log d = {};
    int ret;

    d.dirty_bitmap = slot->dirty_bmap;
    d.slot = slot->slot | (slot->as_id << 16);
    ret = kvm_vm_ioctl(s, KVM_GET_DIRTY_LOG, &d);

    if (ret == -ENOENT) {
        /* kernel does not have dirty bitmap in this slot */
        ret = 0;
    }
    if (ret) {
        error_report_once("%s: KVM_GET_DIRTY_LOG failed with %d",
                          __func__, ret);
    }
    return ret == 0;
}

/* Should be with all slots_lock held for the address spaces. */
static void kvm_dirty_ring_mark_page(KVMState *s, uint32_t as_id,
                                     uint32_t slot_id, uint64_t offset)
{
    KVMMemoryListener *kml;
    KVMSlot *mem;

    if (as_id >= s->nr_as) {
        return;
    }

    kml = s->as[as_id].ml;
    mem = &kml->slots[slot_id];

    if (!mem->memory_size || offset >=
        (mem->memory_size / qemu_real_host_page_size())) {
        return;
    }

    set_bit(offset, mem->dirty_bmap);
}

static bool dirty_gfn_is_dirtied(struct kvm_dirty_gfn *gfn)
{
    /*
     * Read the flags before the value.  Pairs with barrier in
     * KVM's kvm_dirty_ring_push() function.
     */
    return qatomic_load_acquire(&gfn->flags) == KVM_DIRTY_GFN_F_DIRTY;
}

static void dirty_gfn_set_collected(struct kvm_dirty_gfn *gfn)
{
    /*
     * Use a store-release so that the CPU that executes KVM_RESET_DIRTY_RINGS
     * sees the full content of the ring:
     *
     * CPU0                     CPU1                         CPU2
     * ------------------------------------------------------------------------------
     *                                                       fill gfn0
     *                                                       store-rel flags for gfn0
     * load-acq flags for gfn0
     * store-rel RESET for gfn0
     *                          ioctl(RESET_RINGS)
     *                            load-acq flags for gfn0
     *                            check if flags have RESET
     *
     * The synchronization goes from CPU2 to CPU0 to CPU1.
     */
    qatomic_store_release(&gfn->flags, KVM_DIRTY_GFN_F_RESET);
}

/*
 * Should be with all slots_lock held for the address spaces.  It returns the
 * dirty page we've collected on this dirty ring.
 */
static uint32_t kvm_dirty_ring_reap_one(KVMState *s, CPUState *cpu)
{
    struct kvm_dirty_gfn *dirty_gfns = cpu->kvm_dirty_gfns, *cur;
    uint32_t ring_size = s->kvm_dirty_ring_size;
    uint32_t count = 0, fetch = cpu->kvm_fetch_index;

    /*
     * It's possible that we race with vcpu creation code where the vcpu is
     * put onto the vcpus list but not yet initialized the dirty ring
     * structures.  If so, skip it.
     */
    if (!cpu->created) {
        return 0;
    }

    assert(dirty_gfns && ring_size);
    trace_kvm_dirty_ring_reap_vcpu(cpu->cpu_index);

    while (true) {
        cur = &dirty_gfns[fetch % ring_size];
        if (!dirty_gfn_is_dirtied(cur)) {
            break;
        }
        kvm_dirty_ring_mark_page(s, cur->slot >> 16, cur->slot & 0xffff,
                                 cur->offset);
        dirty_gfn_set_collected(cur);
        trace_kvm_dirty_ring_page(cpu->cpu_index, fetch, cur->offset);
        fetch++;
        count++;
    }
    cpu->kvm_fetch_index = fetch;
    cpu->dirty_pages += count;

    return count;
}

/* Must be with slots_lock held */
static uint64_t kvm_dirty_ring_reap_locked(KVMState *s, CPUState* cpu)
{
    int ret;
    uint64_t total = 0;
    int64_t stamp;

    stamp = get_clock();

    if (cpu) {
        total = kvm_dirty_ring_reap_one(s, cpu);
    } else {
        CPU_FOREACH(cpu) {
            total += kvm_dirty_ring_reap_one(s, cpu);
        }
    }

    if (total) {
        ret = kvm_vm_ioctl(s, KVM_RESET_DIRTY_RINGS);
        assert(ret == total);
    }

    stamp = get_clock() - stamp;

    if (total) {
        trace_kvm_dirty_ring_reap(total, stamp / 1000);
    }

    return total;
}

/*
 * Currently for simplicity, we must hold BQL before calling this.  We can
 * consider to drop the BQL if we're clear with all the race conditions.
 */
static uint64_t kvm_dirty_ring_reap(KVMState *s, CPUState *cpu)
{
    uint64_t total;

    /*
     * We need to lock all kvm slots for all address spaces here,
     * because:
     *
     * (1) We need to mark dirty for dirty bitmaps in multiple slots
     *     and for tons of pages, so it's better to take the lock here
     *     once rather than once per page.  And more importantly,
     *
     * (2) We must _NOT_ publish dirty bits to the other threads
     *     (e.g., the migration thread) via the kvm memory slot dirty
     *     bitmaps before correctly re-protect those dirtied pages.
     *     Otherwise we can have potential risk of data corruption if
     *     the page data is read in the other thread before we do
     *     reset below.
     */
    kvm_slots_lock();
    total = kvm_dirty_ring_reap_locked(s, cpu);
    kvm_slots_unlock();

    return total;
}

static void do_kvm_cpu_synchronize_kick(CPUState *cpu, run_on_cpu_data arg)
{
    /* No need to do anything */
}

/*
 * Kick all vcpus out in a synchronized way.  When returned, we
 * guarantee that every vcpu has been kicked and at least returned to
 * userspace once.
 */
static void kvm_cpu_synchronize_kick_all(void)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        run_on_cpu(cpu, do_kvm_cpu_synchronize_kick, RUN_ON_CPU_NULL);
    }
}

/*
 * Flush all the existing dirty pages to the KVM slot buffers.  When
 * this call returns, we guarantee that all the touched dirty pages
 * before calling this function have been put into the per-kvmslot
 * dirty bitmap.
 *
 * This function must be called with BQL held.
 */
static void kvm_dirty_ring_flush(void)
{
    trace_kvm_dirty_ring_flush(0);
    /*
     * The function needs to be serialized.  Since this function
     * should always be with BQL held, serialization is guaranteed.
     * However, let's be sure of it.
     */
    assert(bql_locked());
    /*
     * First make sure to flush the hardware buffers by kicking all
     * vcpus out in a synchronous way.
     */
    kvm_cpu_synchronize_kick_all();
    kvm_dirty_ring_reap(kvm_state, NULL);
    trace_kvm_dirty_ring_flush(1);
}

/**
 * kvm_physical_sync_dirty_bitmap - Sync dirty bitmap from kernel space
 *
 * This function will first try to fetch dirty bitmap from the kernel,
 * and then updates qemu's dirty bitmap.
 *
 * NOTE: caller must be with kml->slots_lock held.
 *
 * @kml: the KVM memory listener object
 * @section: the memory section to sync the dirty bitmap with
 */
static void kvm_physical_sync_dirty_bitmap(KVMMemoryListener *kml,
                                           MemoryRegionSection *section)
{
    KVMState *s = kvm_state;
    KVMSlot *mem;
    hwaddr start_addr, size;
    hwaddr slot_size;

    size = kvm_align_section(section, &start_addr);
    while (size) {
        slot_size = MIN(kvm_max_slot_size, size);
        mem = kvm_lookup_matching_slot(kml, start_addr, slot_size);
        if (!mem) {
            /* We don't have a slot if we want to trap every access. */
            return;
        }
        if (kvm_slot_get_dirty_log(s, mem)) {
            kvm_slot_sync_dirty_pages(mem);
        }
        start_addr += slot_size;
        size -= slot_size;
    }
}

/* Alignment requirement for KVM_CLEAR_DIRTY_LOG - 64 pages */
#define KVM_CLEAR_LOG_SHIFT  6
#define KVM_CLEAR_LOG_ALIGN  (qemu_real_host_page_size() << KVM_CLEAR_LOG_SHIFT)
#define KVM_CLEAR_LOG_MASK   (-KVM_CLEAR_LOG_ALIGN)

static int kvm_log_clear_one_slot(KVMSlot *mem, int as_id, uint64_t start,
                                  uint64_t size)
{
    KVMState *s = kvm_state;
    uint64_t end, bmap_start, start_delta, bmap_npages;
    struct kvm_clear_dirty_log d;
    unsigned long *bmap_clear = NULL, psize = qemu_real_host_page_size();
    int ret;

    /*
     * We need to extend either the start or the size or both to
     * satisfy the KVM interface requirement.  Firstly, do the start
     * page alignment on 64 host pages
     */
    bmap_start = start & KVM_CLEAR_LOG_MASK;
    start_delta = start - bmap_start;
    bmap_start /= psize;

    /*
     * The kernel interface has restriction on the size too, that either:
     *
     * (1) the size is 64 host pages aligned (just like the start), or
     * (2) the size fills up until the end of the KVM memslot.
     */
    bmap_npages = DIV_ROUND_UP(size + start_delta, KVM_CLEAR_LOG_ALIGN)
        << KVM_CLEAR_LOG_SHIFT;
    end = mem->memory_size / psize;
    if (bmap_npages > end - bmap_start) {
        bmap_npages = end - bmap_start;
    }
    start_delta /= psize;

    /*
     * Prepare the bitmap to clear dirty bits.  Here we must guarantee
     * that we won't clear any unknown dirty bits otherwise we might
     * accidentally clear some set bits which are not yet synced from
     * the kernel into QEMU's bitmap, then we'll lose track of the
     * guest modifications upon those pages (which can directly lead
     * to guest data loss or panic after migration).
     *
     * Layout of the KVMSlot.dirty_bmap:
     *
     *                   |<-------- bmap_npages -----------..>|
     *                                                     [1]
     *                     start_delta         size
     *  |----------------|-------------|------------------|------------|
     *  ^                ^             ^                               ^
     *  |                |             |                               |
     * start          bmap_start     (start)                         end
     * of memslot                                             of memslot
     *
     * [1] bmap_npages can be aligned to either 64 pages or the end of slot
     */

    assert(bmap_start % BITS_PER_LONG == 0);
    /* We should never do log_clear before log_sync */
    assert(mem->dirty_bmap);
    if (start_delta || bmap_npages - size / psize) {
        /* Slow path - we need to manipulate a temp bitmap */
        bmap_clear = bitmap_new(bmap_npages);
        bitmap_copy_with_src_offset(bmap_clear, mem->dirty_bmap,
                                    bmap_start, start_delta + size / psize);
        /*
         * We need to fill the holes at start because that was not
         * specified by the caller and we extended the bitmap only for
         * 64 pages alignment
         */
        bitmap_clear(bmap_clear, 0, start_delta);
        d.dirty_bitmap = bmap_clear;
    } else {
        /*
         * Fast path - both start and size align well with BITS_PER_LONG
         * (or the end of memory slot)
         */
        d.dirty_bitmap = mem->dirty_bmap + BIT_WORD(bmap_start);
    }

    d.first_page = bmap_start;
    /* It should never overflow.  If it happens, say something */
    assert(bmap_npages <= UINT32_MAX);
    d.num_pages = bmap_npages;
    d.slot = mem->slot | (as_id << 16);

    ret = kvm_vm_ioctl(s, KVM_CLEAR_DIRTY_LOG, &d);
    if (ret < 0 && ret != -ENOENT) {
        error_report("%s: KVM_CLEAR_DIRTY_LOG failed, slot=%d, "
                     "start=0x%"PRIx64", size=0x%"PRIx32", errno=%d",
                     __func__, d.slot, (uint64_t)d.first_page,
                     (uint32_t)d.num_pages, ret);
    } else {
        ret = 0;
        trace_kvm_clear_dirty_log(d.slot, d.first_page, d.num_pages);
    }

    /*
     * After we have updated the remote dirty bitmap, we update the
     * cached bitmap as well for the memslot, then if another user
     * clears the same region we know we shouldn't clear it again on
     * the remote otherwise it's data loss as well.
     */
    bitmap_clear(mem->dirty_bmap, bmap_start + start_delta,
                 size / psize);
    /* This handles the NULL case well */
    g_free(bmap_clear);
    return ret;
}


/**
 * kvm_physical_log_clear - Clear the kernel's dirty bitmap for range
 *
 * NOTE: this will be a no-op if we haven't enabled manual dirty log
 * protection in the host kernel because in that case this operation
 * will be done within log_sync().
 *
 * @kml:     the kvm memory listener
 * @section: the memory range to clear dirty bitmap
 */
static int kvm_physical_log_clear(KVMMemoryListener *kml,
                                  MemoryRegionSection *section)
{
    KVMState *s = kvm_state;
    uint64_t start, size, offset, count;
    KVMSlot *mem;
    int ret = 0, i;

    if (!s->manual_dirty_log_protect) {
        /* No need to do explicit clear */
        return ret;
    }

    start = section->offset_within_address_space;
    size = int128_get64(section->size);

    if (!size) {
        /* Nothing more we can do... */
        return ret;
    }

    kvm_slots_lock();

    for (i = 0; i < s->nr_slots; i++) {
        mem = &kml->slots[i];
        /* Discard slots that are empty or do not overlap the section */
        if (!mem->memory_size ||
            mem->start_addr > start + size - 1 ||
            start > mem->start_addr + mem->memory_size - 1) {
            continue;
        }

        if (start >= mem->start_addr) {
            /* The slot starts before section or is aligned to it.  */
            offset = start - mem->start_addr;
            count = MIN(mem->memory_size - offset, size);
        } else {
            /* The slot starts after section.  */
            offset = 0;
            count = MIN(mem->memory_size, size - (mem->start_addr - start));
        }
        ret = kvm_log_clear_one_slot(mem, kml->as_id, offset, count);
        if (ret < 0) {
            break;
        }
    }

    kvm_slots_unlock();

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

static void kvm_coalesce_pio_add(MemoryListener *listener,
                                MemoryRegionSection *section,
                                hwaddr start, hwaddr size)
{
    KVMState *s = kvm_state;

    if (s->coalesced_pio) {
        struct kvm_coalesced_mmio_zone zone;

        zone.addr = start;
        zone.size = size;
        zone.pio = 1;

        (void)kvm_vm_ioctl(s, KVM_REGISTER_COALESCED_MMIO, &zone);
    }
}

static void kvm_coalesce_pio_del(MemoryListener *listener,
                                MemoryRegionSection *section,
                                hwaddr start, hwaddr size)
{
    KVMState *s = kvm_state;

    if (s->coalesced_pio) {
        struct kvm_coalesced_mmio_zone zone;

        zone.addr = start;
        zone.size = size;
        zone.pio = 1;

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

int kvm_vm_check_extension(KVMState *s, unsigned int extension)
{
    int ret;

    ret = kvm_vm_ioctl(s, KVM_CHECK_EXTENSION, extension);
    if (ret < 0) {
        /* VM wide version not implemented, use global one instead */
        ret = kvm_check_extension(s, extension);
    }

    return ret;
}

/*
 * We track the poisoned pages to be able to:
 * - replace them on VM reset
 * - block a migration for a VM with a poisoned page
 */
typedef struct HWPoisonPage {
    ram_addr_t ram_addr;
    QLIST_ENTRY(HWPoisonPage) list;
} HWPoisonPage;

static QLIST_HEAD(, HWPoisonPage) hwpoison_page_list =
    QLIST_HEAD_INITIALIZER(hwpoison_page_list);

static void kvm_unpoison_all(void *param)
{
    HWPoisonPage *page, *next_page;

    QLIST_FOREACH_SAFE(page, &hwpoison_page_list, list, next_page) {
        QLIST_REMOVE(page, list);
        qemu_ram_remap(page->ram_addr, TARGET_PAGE_SIZE);
        g_free(page);
    }
}

void kvm_hwpoison_page_add(ram_addr_t ram_addr)
{
    HWPoisonPage *page;

    QLIST_FOREACH(page, &hwpoison_page_list, list) {
        if (page->ram_addr == ram_addr) {
            return;
        }
    }
    page = g_new(HWPoisonPage, 1);
    page->ram_addr = ram_addr;
    QLIST_INSERT_HEAD(&hwpoison_page_list, page, list);
}

bool kvm_hwpoisoned_mem(void)
{
    return !QLIST_EMPTY(&hwpoison_page_list);
}

static uint32_t adjust_ioeventfd_endianness(uint32_t val, uint32_t size)
{
#if HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN
    /* The kernel expects ioeventfd values in HOST_BIG_ENDIAN
     * endianness, but the memory core hands them in target endianness.
     * For example, PPC is always treated as big-endian even if running
     * on KVM and on PPC64LE.  Correct here.
     */
    switch (size) {
    case 2:
        val = bswap16(val);
        break;
    case 4:
        val = bswap32(val);
        break;
    }
#endif
    return val;
}

static int kvm_set_ioeventfd_mmio(int fd, hwaddr addr, uint32_t val,
                                  bool assign, uint32_t size, bool datamatch)
{
    int ret;
    struct kvm_ioeventfd iofd = {
        .datamatch = datamatch ? adjust_ioeventfd_endianness(val, size) : 0,
        .addr = addr,
        .len = size,
        .flags = 0,
        .fd = fd,
    };

    trace_kvm_set_ioeventfd_mmio(fd, (uint64_t)addr, val, assign, size,
                                 datamatch);
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
        .datamatch = datamatch ? adjust_ioeventfd_endianness(val, size) : 0,
        .addr = addr,
        .flags = KVM_IOEVENTFD_FLAG_PIO,
        .len = size,
        .fd = fd,
    };
    int r;
    trace_kvm_set_ioeventfd_pio(fd, addr, val, assign, size, datamatch);
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

void kvm_set_max_memslot_size(hwaddr max_slot_size)
{
    g_assert(
        ROUND_UP(max_slot_size, qemu_real_host_page_size()) == max_slot_size
    );
    kvm_max_slot_size = max_slot_size;
}

static int kvm_set_memory_attributes(hwaddr start, uint64_t size, uint64_t attr)
{
    struct kvm_memory_attributes attrs;
    int r;

    assert((attr & kvm_supported_memory_attributes) == attr);
    attrs.attributes = attr;
    attrs.address = start;
    attrs.size = size;
    attrs.flags = 0;

    r = kvm_vm_ioctl(kvm_state, KVM_SET_MEMORY_ATTRIBUTES, &attrs);
    if (r) {
        error_report("failed to set memory (0x%" HWADDR_PRIx "+0x%" PRIx64 ") "
                     "with attr 0x%" PRIx64 " error '%s'",
                     start, size, attr, strerror(errno));
    }
    return r;
}

int kvm_set_memory_attributes_private(hwaddr start, uint64_t size)
{
    return kvm_set_memory_attributes(start, size, KVM_MEMORY_ATTRIBUTE_PRIVATE);
}

int kvm_set_memory_attributes_shared(hwaddr start, uint64_t size)
{
    return kvm_set_memory_attributes(start, size, 0);
}

/* Called with KVMMemoryListener.slots_lock held */
static void kvm_set_phys_mem(KVMMemoryListener *kml,
                             MemoryRegionSection *section, bool add)
{
    KVMSlot *mem;
    int err;
    MemoryRegion *mr = section->mr;
    bool writable = !mr->readonly && !mr->rom_device;
    hwaddr start_addr, size, slot_size, mr_offset;
    ram_addr_t ram_start_offset;
    void *ram;

    if (!memory_region_is_ram(mr)) {
        if (writable || !kvm_readonly_mem_allowed) {
            return;
        } else if (!mr->romd_mode) {
            /* If the memory device is not in romd_mode, then we actually want
             * to remove the kvm memory slot so all accesses will trap. */
            add = false;
        }
    }

    size = kvm_align_section(section, &start_addr);
    if (!size) {
        return;
    }

    /* The offset of the kvmslot within the memory region */
    mr_offset = section->offset_within_region + start_addr -
        section->offset_within_address_space;

    /* use aligned delta to align the ram address and offset */
    ram = memory_region_get_ram_ptr(mr) + mr_offset;
    ram_start_offset = memory_region_get_ram_addr(mr) + mr_offset;

    if (!add) {
        do {
            slot_size = MIN(kvm_max_slot_size, size);
            mem = kvm_lookup_matching_slot(kml, start_addr, slot_size);
            if (!mem) {
                return;
            }
            if (mem->flags & KVM_MEM_LOG_DIRTY_PAGES) {
                /*
                 * NOTE: We should be aware of the fact that here we're only
                 * doing a best effort to sync dirty bits.  No matter whether
                 * we're using dirty log or dirty ring, we ignored two facts:
                 *
                 * (1) dirty bits can reside in hardware buffers (PML)
                 *
                 * (2) after we collected dirty bits here, pages can be dirtied
                 * again before we do the final KVM_SET_USER_MEMORY_REGION to
                 * remove the slot.
                 *
                 * Not easy.  Let's cross the fingers until it's fixed.
                 */
                if (kvm_state->kvm_dirty_ring_size) {
                    kvm_dirty_ring_reap_locked(kvm_state, NULL);
                    if (kvm_state->kvm_dirty_ring_with_bitmap) {
                        kvm_slot_sync_dirty_pages(mem);
                        kvm_slot_get_dirty_log(kvm_state, mem);
                    }
                } else {
                    kvm_slot_get_dirty_log(kvm_state, mem);
                }
                kvm_slot_sync_dirty_pages(mem);
            }

            /* unregister the slot */
            g_free(mem->dirty_bmap);
            mem->dirty_bmap = NULL;
            mem->memory_size = 0;
            mem->flags = 0;
            err = kvm_set_user_memory_region(kml, mem, false);
            if (err) {
                fprintf(stderr, "%s: error unregistering slot: %s\n",
                        __func__, strerror(-err));
                abort();
            }
            start_addr += slot_size;
            size -= slot_size;
            kml->nr_used_slots--;
        } while (size);
        return;
    }

    /* register the new slot */
    do {
        slot_size = MIN(kvm_max_slot_size, size);
        mem = kvm_alloc_slot(kml);
        mem->as_id = kml->as_id;
        mem->memory_size = slot_size;
        mem->start_addr = start_addr;
        mem->ram_start_offset = ram_start_offset;
        mem->ram = ram;
        mem->flags = kvm_mem_flags(mr);
        mem->guest_memfd = mr->ram_block->guest_memfd;
        mem->guest_memfd_offset = (uint8_t*)ram - mr->ram_block->host;

        kvm_slot_init_dirty_bitmap(mem);
        err = kvm_set_user_memory_region(kml, mem, true);
        if (err) {
            fprintf(stderr, "%s: error registering slot: %s\n", __func__,
                    strerror(-err));
            abort();
        }

        if (memory_region_has_guest_memfd(mr)) {
            err = kvm_set_memory_attributes_private(start_addr, slot_size);
            if (err) {
                error_report("%s: failed to set memory attribute private: %s",
                             __func__, strerror(-err));
                exit(1);
            }
        }

        start_addr += slot_size;
        ram_start_offset += slot_size;
        ram += slot_size;
        size -= slot_size;
        kml->nr_used_slots++;
    } while (size);
}

static void *kvm_dirty_ring_reaper_thread(void *data)
{
    KVMState *s = data;
    struct KVMDirtyRingReaper *r = &s->reaper;

    rcu_register_thread();

    trace_kvm_dirty_ring_reaper("init");

    while (true) {
        r->reaper_state = KVM_DIRTY_RING_REAPER_WAIT;
        trace_kvm_dirty_ring_reaper("wait");
        /*
         * TODO: provide a smarter timeout rather than a constant?
         */
        sleep(1);

        /* keep sleeping so that dirtylimit not be interfered by reaper */
        if (dirtylimit_in_service()) {
            continue;
        }

        trace_kvm_dirty_ring_reaper("wakeup");
        r->reaper_state = KVM_DIRTY_RING_REAPER_REAPING;

        bql_lock();
        kvm_dirty_ring_reap(s, NULL);
        bql_unlock();

        r->reaper_iteration++;
    }

    trace_kvm_dirty_ring_reaper("exit");

    rcu_unregister_thread();

    return NULL;
}

static void kvm_dirty_ring_reaper_init(KVMState *s)
{
    struct KVMDirtyRingReaper *r = &s->reaper;

    qemu_thread_create(&r->reaper_thr, "kvm-reaper",
                       kvm_dirty_ring_reaper_thread,
                       s, QEMU_THREAD_JOINABLE);
}

static int kvm_dirty_ring_init(KVMState *s)
{
    uint32_t ring_size = s->kvm_dirty_ring_size;
    uint64_t ring_bytes = ring_size * sizeof(struct kvm_dirty_gfn);
    unsigned int capability = KVM_CAP_DIRTY_LOG_RING;
    int ret;

    s->kvm_dirty_ring_size = 0;
    s->kvm_dirty_ring_bytes = 0;

    /* Bail if the dirty ring size isn't specified */
    if (!ring_size) {
        return 0;
    }

    /*
     * Read the max supported pages. Fall back to dirty logging mode
     * if the dirty ring isn't supported.
     */
    ret = kvm_vm_check_extension(s, capability);
    if (ret <= 0) {
        capability = KVM_CAP_DIRTY_LOG_RING_ACQ_REL;
        ret = kvm_vm_check_extension(s, capability);
    }

    if (ret <= 0) {
        warn_report("KVM dirty ring not available, using bitmap method");
        return 0;
    }

    if (ring_bytes > ret) {
        error_report("KVM dirty ring size %" PRIu32 " too big "
                     "(maximum is %ld).  Please use a smaller value.",
                     ring_size, (long)ret / sizeof(struct kvm_dirty_gfn));
        return -EINVAL;
    }

    ret = kvm_vm_enable_cap(s, capability, 0, ring_bytes);
    if (ret) {
        error_report("Enabling of KVM dirty ring failed: %s. "
                     "Suggested minimum value is 1024.", strerror(-ret));
        return -EIO;
    }

    /* Enable the backup bitmap if it is supported */
    ret = kvm_vm_check_extension(s, KVM_CAP_DIRTY_LOG_RING_WITH_BITMAP);
    if (ret > 0) {
        ret = kvm_vm_enable_cap(s, KVM_CAP_DIRTY_LOG_RING_WITH_BITMAP, 0);
        if (ret) {
            error_report("Enabling of KVM dirty ring's backup bitmap failed: "
                         "%s. ", strerror(-ret));
            return -EIO;
        }

        s->kvm_dirty_ring_with_bitmap = true;
    }

    s->kvm_dirty_ring_size = ring_size;
    s->kvm_dirty_ring_bytes = ring_bytes;

    return 0;
}

static void kvm_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);
    KVMMemoryUpdate *update;

    update = g_new0(KVMMemoryUpdate, 1);
    update->section = *section;

    QSIMPLEQ_INSERT_TAIL(&kml->transaction_add, update, next);
}

static void kvm_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);
    KVMMemoryUpdate *update;

    update = g_new0(KVMMemoryUpdate, 1);
    update->section = *section;

    QSIMPLEQ_INSERT_TAIL(&kml->transaction_del, update, next);
}

static void kvm_region_commit(MemoryListener *listener)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener,
                                          listener);
    KVMMemoryUpdate *u1, *u2;
    bool need_inhibit = false;

    if (QSIMPLEQ_EMPTY(&kml->transaction_add) &&
        QSIMPLEQ_EMPTY(&kml->transaction_del)) {
        return;
    }

    /*
     * We have to be careful when regions to add overlap with ranges to remove.
     * We have to simulate atomic KVM memslot updates by making sure no ioctl()
     * is currently active.
     *
     * The lists are order by addresses, so it's easy to find overlaps.
     */
    u1 = QSIMPLEQ_FIRST(&kml->transaction_del);
    u2 = QSIMPLEQ_FIRST(&kml->transaction_add);
    while (u1 && u2) {
        Range r1, r2;

        range_init_nofail(&r1, u1->section.offset_within_address_space,
                          int128_get64(u1->section.size));
        range_init_nofail(&r2, u2->section.offset_within_address_space,
                          int128_get64(u2->section.size));

        if (range_overlaps_range(&r1, &r2)) {
            need_inhibit = true;
            break;
        }
        if (range_lob(&r1) < range_lob(&r2)) {
            u1 = QSIMPLEQ_NEXT(u1, next);
        } else {
            u2 = QSIMPLEQ_NEXT(u2, next);
        }
    }

    kvm_slots_lock();
    if (need_inhibit) {
        accel_ioctl_inhibit_begin();
    }

    /* Remove all memslots before adding the new ones. */
    while (!QSIMPLEQ_EMPTY(&kml->transaction_del)) {
        u1 = QSIMPLEQ_FIRST(&kml->transaction_del);
        QSIMPLEQ_REMOVE_HEAD(&kml->transaction_del, next);

        kvm_set_phys_mem(kml, &u1->section, false);
        memory_region_unref(u1->section.mr);

        g_free(u1);
    }
    while (!QSIMPLEQ_EMPTY(&kml->transaction_add)) {
        u1 = QSIMPLEQ_FIRST(&kml->transaction_add);
        QSIMPLEQ_REMOVE_HEAD(&kml->transaction_add, next);

        memory_region_ref(u1->section.mr);
        kvm_set_phys_mem(kml, &u1->section, true);

        g_free(u1);
    }

    if (need_inhibit) {
        accel_ioctl_inhibit_end();
    }
    kvm_slots_unlock();
}

static void kvm_log_sync(MemoryListener *listener,
                         MemoryRegionSection *section)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);

    kvm_slots_lock();
    kvm_physical_sync_dirty_bitmap(kml, section);
    kvm_slots_unlock();
}

static void kvm_log_sync_global(MemoryListener *l, bool last_stage)
{
    KVMMemoryListener *kml = container_of(l, KVMMemoryListener, listener);
    KVMState *s = kvm_state;
    KVMSlot *mem;
    int i;

    /* Flush all kernel dirty addresses into KVMSlot dirty bitmap */
    kvm_dirty_ring_flush();

    /*
     * TODO: make this faster when nr_slots is big while there are
     * only a few used slots (small VMs).
     */
    kvm_slots_lock();
    for (i = 0; i < s->nr_slots; i++) {
        mem = &kml->slots[i];
        if (mem->memory_size && mem->flags & KVM_MEM_LOG_DIRTY_PAGES) {
            kvm_slot_sync_dirty_pages(mem);

            if (s->kvm_dirty_ring_with_bitmap && last_stage &&
                kvm_slot_get_dirty_log(s, mem)) {
                kvm_slot_sync_dirty_pages(mem);
            }

            /*
             * This is not needed by KVM_GET_DIRTY_LOG because the
             * ioctl will unconditionally overwrite the whole region.
             * However kvm dirty ring has no such side effect.
             */
            kvm_slot_reset_dirty_pages(mem);
        }
    }
    kvm_slots_unlock();
}

static void kvm_log_clear(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);
    int r;

    r = kvm_physical_log_clear(kml, section);
    if (r < 0) {
        error_report_once("%s: kvm log clear failed: mr=%s "
                          "offset=%"HWADDR_PRIx" size=%"PRIx64, __func__,
                          section->mr->name, section->offset_within_region,
                          int128_get64(section->size));
        abort();
    }
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
        fprintf(stderr, "%s: error adding ioeventfd: %s (%d)\n",
                __func__, strerror(-r), -r);
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
        fprintf(stderr, "%s: error deleting ioeventfd: %s (%d)\n",
                __func__, strerror(-r), -r);
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
        fprintf(stderr, "%s: error adding ioeventfd: %s (%d)\n",
                __func__, strerror(-r), -r);
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
        fprintf(stderr, "%s: error deleting ioeventfd: %s (%d)\n",
                __func__, strerror(-r), -r);
        abort();
    }
}

void kvm_memory_listener_register(KVMState *s, KVMMemoryListener *kml,
                                  AddressSpace *as, int as_id, const char *name)
{
    int i;

    kml->slots = g_new0(KVMSlot, s->nr_slots);
    kml->as_id = as_id;

    for (i = 0; i < s->nr_slots; i++) {
        kml->slots[i].slot = i;
    }

    QSIMPLEQ_INIT(&kml->transaction_add);
    QSIMPLEQ_INIT(&kml->transaction_del);

    kml->listener.region_add = kvm_region_add;
    kml->listener.region_del = kvm_region_del;
    kml->listener.commit = kvm_region_commit;
    kml->listener.log_start = kvm_log_start;
    kml->listener.log_stop = kvm_log_stop;
    kml->listener.priority = MEMORY_LISTENER_PRIORITY_ACCEL;
    kml->listener.name = name;

    if (s->kvm_dirty_ring_size) {
        kml->listener.log_sync_global = kvm_log_sync_global;
    } else {
        kml->listener.log_sync = kvm_log_sync;
        kml->listener.log_clear = kvm_log_clear;
    }

    memory_listener_register(&kml->listener, as);

    for (i = 0; i < s->nr_as; ++i) {
        if (!s->as[i].as) {
            s->as[i].as = as;
            s->as[i].ml = kml;
            break;
        }
    }
}

static MemoryListener kvm_io_listener = {
    .name = "kvm-io",
    .coalesced_io_add = kvm_coalesce_pio_add,
    .coalesced_io_del = kvm_coalesce_pio_del,
    .eventfd_add = kvm_io_ioeventfd_add,
    .eventfd_del = kvm_io_ioeventfd_del,
    .priority = MEMORY_LISTENER_PRIORITY_DEV_BACKEND,
};

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
    set_bit(gsi, s->used_gsi_bitmap);
}

static void clear_gsi(KVMState *s, unsigned int gsi)
{
    clear_bit(gsi, s->used_gsi_bitmap);
}

void kvm_init_irq_routing(KVMState *s)
{
    int gsi_count;

    gsi_count = kvm_check_extension(s, KVM_CAP_IRQ_ROUTING) - 1;
    if (gsi_count > 0) {
        /* Round up so we can search ints using ffs */
        s->used_gsi_bitmap = bitmap_new(gsi_count);
        s->gsi_count = gsi_count;
    }

    s->irq_routes = g_malloc0(sizeof(*s->irq_routes));
    s->nr_allocated_irq_routes = 0;

    kvm_arch_init_irq_routing(s);
}

void kvm_irqchip_commit_routes(KVMState *s)
{
    int ret;

    if (kvm_gsi_direct_mapping()) {
        return;
    }

    if (!kvm_gsi_routing_enabled()) {
        return;
    }

    s->irq_routes->flags = 0;
    trace_kvm_irqchip_commit_routes();
    ret = kvm_vm_ioctl(s, KVM_SET_GSI_ROUTING, s->irq_routes);
    assert(ret == 0);
}

void kvm_add_routing_entry(KVMState *s,
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
    kvm_arch_release_virq_post(virq);
    trace_kvm_irqchip_release_virq(virq);
}

void kvm_irqchip_add_change_notifier(Notifier *n)
{
    notifier_list_add(&kvm_irqchip_change_notifiers, n);
}

void kvm_irqchip_remove_change_notifier(Notifier *n)
{
    notifier_remove(n);
}

void kvm_irqchip_change_notify(void)
{
    notifier_list_notify(&kvm_irqchip_change_notifiers, NULL);
}

int kvm_irqchip_get_virq(KVMState *s)
{
    int next_virq;

    /* Return the lowest unused GSI in the bitmap */
    next_virq = find_first_zero_bit(s->used_gsi_bitmap, s->gsi_count);
    if (next_virq >= s->gsi_count) {
        return -ENOSPC;
    } else {
        return next_virq;
    }
}

int kvm_irqchip_send_msi(KVMState *s, MSIMessage msg)
{
    struct kvm_msi msi;

    msi.address_lo = (uint32_t)msg.address;
    msi.address_hi = msg.address >> 32;
    msi.data = le32_to_cpu(msg.data);
    msi.flags = 0;
    memset(msi.pad, 0, sizeof(msi.pad));

    return kvm_vm_ioctl(s, KVM_SIGNAL_MSI, &msi);
}

int kvm_irqchip_add_msi_route(KVMRouteChange *c, int vector, PCIDevice *dev)
{
    struct kvm_irq_routing_entry kroute = {};
    int virq;
    KVMState *s = c->s;
    MSIMessage msg = {0, 0};

    if (pci_available && dev) {
        msg = pci_get_msi_message(dev, vector);
    }

    if (kvm_gsi_direct_mapping()) {
        return kvm_arch_msi_data_to_gsi(msg.data);
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
    if (pci_available && kvm_msi_devid_required()) {
        kroute.flags = KVM_MSI_VALID_DEVID;
        kroute.u.msi.devid = pci_requester_id(dev);
    }
    if (kvm_arch_fixup_msi_route(&kroute, msg.address, msg.data, dev)) {
        kvm_irqchip_release_virq(s, virq);
        return -EINVAL;
    }

    if (s->irq_routes->nr < s->gsi_count) {
        trace_kvm_irqchip_add_msi_route(dev ? dev->name : (char *)"N/A",
                                        vector, virq);

        kvm_add_routing_entry(s, &kroute);
        kvm_arch_add_msi_route_post(&kroute, vector, dev);
        c->changes++;
    } else {
        kvm_irqchip_release_virq(s, virq);
        return -ENOSPC;
    }

    return virq;
}

int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg,
                                 PCIDevice *dev)
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
    if (pci_available && kvm_msi_devid_required()) {
        kroute.flags = KVM_MSI_VALID_DEVID;
        kroute.u.msi.devid = pci_requester_id(dev);
    }
    if (kvm_arch_fixup_msi_route(&kroute, msg.address, msg.data, dev)) {
        return -EINVAL;
    }

    trace_kvm_irqchip_update_msi_route(virq);

    return kvm_update_routing_entry(s, &kroute);
}

static int kvm_irqchip_assign_irqfd(KVMState *s, EventNotifier *event,
                                    EventNotifier *resample, int virq,
                                    bool assign)
{
    int fd = event_notifier_get_fd(event);
    int rfd = resample ? event_notifier_get_fd(resample) : -1;

    struct kvm_irqfd irqfd = {
        .fd = fd,
        .gsi = virq,
        .flags = assign ? 0 : KVM_IRQFD_FLAG_DEASSIGN,
    };

    if (rfd != -1) {
        assert(assign);
        if (kvm_irqchip_is_split()) {
            /*
             * When the slow irqchip (e.g. IOAPIC) is in the
             * userspace, KVM kernel resamplefd will not work because
             * the EOI of the interrupt will be delivered to userspace
             * instead, so the KVM kernel resamplefd kick will be
             * skipped.  The userspace here mimics what the kernel
             * provides with resamplefd, remember the resamplefd and
             * kick it when we receive EOI of this IRQ.
             *
             * This is hackery because IOAPIC is mostly bypassed
             * (except EOI broadcasts) when irqfd is used.  However
             * this can bring much performance back for split irqchip
             * with INTx IRQs (for VFIO, this gives 93% perf of the
             * full fast path, which is 46% perf boost comparing to
             * the INTx slow path).
             */
            kvm_resample_fd_insert(virq, resample);
        } else {
            irqfd.flags |= KVM_IRQFD_FLAG_RESAMPLE;
            irqfd.resamplefd = rfd;
        }
    } else if (!assign) {
        if (kvm_irqchip_is_split()) {
            kvm_resample_fd_remove(virq);
        }
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

int kvm_irqchip_add_msi_route(KVMRouteChange *c, int vector, PCIDevice *dev)
{
    return -ENOSYS;
}

int kvm_irqchip_add_adapter_route(KVMState *s, AdapterInfo *adapter)
{
    return -ENOSYS;
}

int kvm_irqchip_add_hv_sint_route(KVMState *s, uint32_t vcpu, uint32_t sint)
{
    return -ENOSYS;
}

static int kvm_irqchip_assign_irqfd(KVMState *s, EventNotifier *event,
                                    EventNotifier *resample, int virq,
                                    bool assign)
{
    abort();
}

int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg)
{
    return -ENOSYS;
}
#endif /* !KVM_CAP_IRQ_ROUTING */

int kvm_irqchip_add_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                       EventNotifier *rn, int virq)
{
    return kvm_irqchip_assign_irqfd(s, n, rn, virq, true);
}

int kvm_irqchip_remove_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                          int virq)
{
    return kvm_irqchip_assign_irqfd(s, n, NULL, virq, false);
}

int kvm_irqchip_add_irqfd_notifier(KVMState *s, EventNotifier *n,
                                   EventNotifier *rn, qemu_irq irq)
{
    gpointer key, gsi;
    gboolean found = g_hash_table_lookup_extended(s->gsimap, irq, &key, &gsi);

    if (!found) {
        return -ENXIO;
    }
    return kvm_irqchip_add_irqfd_notifier_gsi(s, n, rn, GPOINTER_TO_INT(gsi));
}

int kvm_irqchip_remove_irqfd_notifier(KVMState *s, EventNotifier *n,
                                      qemu_irq irq)
{
    gpointer key, gsi;
    gboolean found = g_hash_table_lookup_extended(s->gsimap, irq, &key, &gsi);

    if (!found) {
        return -ENXIO;
    }
    return kvm_irqchip_remove_irqfd_notifier_gsi(s, n, GPOINTER_TO_INT(gsi));
}

void kvm_irqchip_set_qemuirq_gsi(KVMState *s, qemu_irq irq, int gsi)
{
    g_hash_table_insert(s->gsimap, irq, GINT_TO_POINTER(gsi));
}

static void kvm_irqchip_create(KVMState *s)
{
    int ret;

    assert(s->kernel_irqchip_split != ON_OFF_AUTO_AUTO);
    if (kvm_check_extension(s, KVM_CAP_IRQCHIP)) {
        ;
    } else if (kvm_check_extension(s, KVM_CAP_S390_IRQCHIP)) {
        ret = kvm_vm_enable_cap(s, KVM_CAP_S390_IRQCHIP, 0);
        if (ret < 0) {
            fprintf(stderr, "Enable kernel irqchip failed: %s\n", strerror(-ret));
            exit(1);
        }
    } else {
        return;
    }

    if (kvm_check_extension(s, KVM_CAP_IRQFD) <= 0) {
        fprintf(stderr, "kvm: irqfd not implemented\n");
        exit(1);
    }

    /* First probe and see if there's a arch-specific hook to create the
     * in-kernel irqchip for us */
    ret = kvm_arch_irqchip_create(s);
    if (ret == 0) {
        if (s->kernel_irqchip_split == ON_OFF_AUTO_ON) {
            error_report("Split IRQ chip mode not supported.");
            exit(1);
        } else {
            ret = kvm_vm_ioctl(s, KVM_CREATE_IRQCHIP);
        }
    }
    if (ret < 0) {
        fprintf(stderr, "Create kernel irqchip failed: %s\n", strerror(-ret));
        exit(1);
    }

    kvm_kernel_irqchip = true;
    /* If we have an in-kernel IRQ chip then we must have asynchronous
     * interrupt delivery (though the reverse is not necessarily true)
     */
    kvm_async_interrupts_allowed = true;
    kvm_halt_in_kernel_allowed = true;

    kvm_init_irq_routing(s);

    s->gsimap = g_hash_table_new(g_direct_hash, g_direct_equal);
}

/* Find number of supported CPUs using the recommended
 * procedure from the kernel API documentation to cope with
 * older kernels that may be missing capabilities.
 */
static int kvm_recommended_vcpus(KVMState *s)
{
    int ret = kvm_vm_check_extension(s, KVM_CAP_NR_VCPUS);
    return (ret) ? ret : 4;
}

static int kvm_max_vcpus(KVMState *s)
{
    int ret = kvm_check_extension(s, KVM_CAP_MAX_VCPUS);
    return (ret) ? ret : kvm_recommended_vcpus(s);
}

static int kvm_max_vcpu_id(KVMState *s)
{
    int ret = kvm_check_extension(s, KVM_CAP_MAX_VCPU_ID);
    return (ret) ? ret : kvm_max_vcpus(s);
}

bool kvm_vcpu_id_is_valid(int vcpu_id)
{
    KVMState *s = KVM_STATE(current_accel());
    return vcpu_id >= 0 && vcpu_id < kvm_max_vcpu_id(s);
}

bool kvm_dirty_ring_enabled(void)
{
    return kvm_state && kvm_state->kvm_dirty_ring_size;
}

static void query_stats_cb(StatsResultList **result, StatsTarget target,
                           strList *names, strList *targets, Error **errp);
static void query_stats_schemas_cb(StatsSchemaList **result, Error **errp);

uint32_t kvm_dirty_ring_size(void)
{
    return kvm_state->kvm_dirty_ring_size;
}

static int kvm_init(MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    static const char upgrade_note[] =
        "Please upgrade to at least kernel 2.6.29 or recent kvm-kmod\n"
        "(see http://sourceforge.net/projects/kvm).\n";
    const struct {
        const char *name;
        int num;
    } num_cpus[] = {
        { "SMP",          ms->smp.cpus },
        { "hotpluggable", ms->smp.max_cpus },
        { /* end of list */ }
    }, *nc = num_cpus;
    int soft_vcpus_limit, hard_vcpus_limit;
    KVMState *s;
    const KVMCapabilityInfo *missing_cap;
    int ret;
    int type;
    uint64_t dirty_log_manual_caps;

    qemu_mutex_init(&kml_slots_lock);

    s = KVM_STATE(ms->accelerator);

    /*
     * On systems where the kernel can support different base page
     * sizes, host page size may be different from TARGET_PAGE_SIZE,
     * even with KVM.  TARGET_PAGE_SIZE is assumed to be the minimum
     * page size for the system though.
     */
    assert(TARGET_PAGE_SIZE <= qemu_real_host_page_size());

    s->sigmask_len = 8;
    accel_blocker_init();

#ifdef TARGET_KVM_HAVE_GUEST_DEBUG
    QTAILQ_INIT(&s->kvm_sw_breakpoints);
#endif
    QLIST_INIT(&s->kvm_parked_vcpus);
    s->fd = qemu_open_old(s->device ?: "/dev/kvm", O_RDWR);
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

    kvm_supported_memory_attributes = kvm_check_extension(s, KVM_CAP_MEMORY_ATTRIBUTES);
    kvm_guest_memfd_supported =
        kvm_check_extension(s, KVM_CAP_GUEST_MEMFD) &&
        kvm_check_extension(s, KVM_CAP_USER_MEMORY2) &&
        (kvm_supported_memory_attributes & KVM_MEMORY_ATTRIBUTE_PRIVATE);

    kvm_immediate_exit = kvm_check_extension(s, KVM_CAP_IMMEDIATE_EXIT);
    s->nr_slots = kvm_check_extension(s, KVM_CAP_NR_MEMSLOTS);

    /* If unspecified, use the default value */
    if (!s->nr_slots) {
        s->nr_slots = 32;
    }

    s->nr_as = kvm_check_extension(s, KVM_CAP_MULTI_ADDRESS_SPACE);
    if (s->nr_as <= 1) {
        s->nr_as = 1;
    }
    s->as = g_new0(struct KVMAs, s->nr_as);

    if (object_property_find(OBJECT(current_machine), "kvm-type")) {
        g_autofree char *kvm_type = object_property_get_str(OBJECT(current_machine),
                                                            "kvm-type",
                                                            &error_abort);
        type = mc->kvm_type(ms, kvm_type);
    } else if (mc->kvm_type) {
        type = mc->kvm_type(ms, NULL);
    } else {
        type = kvm_arch_get_default_type(ms);
    }

    if (type < 0) {
        ret = -EINVAL;
        goto err;
    }

    do {
        ret = kvm_ioctl(s, KVM_CREATE_VM, type);
    } while (ret == -EINTR);

    if (ret < 0) {
        fprintf(stderr, "ioctl(KVM_CREATE_VM) failed: %d %s\n", -ret,
                strerror(-ret));

#ifdef TARGET_S390X
        if (ret == -EINVAL) {
            fprintf(stderr,
                    "Host kernel setup problem detected. Please verify:\n");
            fprintf(stderr, "- for kernels supporting the switch_amode or"
                    " user_mode parameters, whether\n");
            fprintf(stderr,
                    "  user space is running in primary address space\n");
            fprintf(stderr,
                    "- for kernels supporting the vm.allocate_pgste sysctl, "
                    "whether it is enabled\n");
        }
#elif defined(TARGET_PPC)
        if (ret == -EINVAL) {
            fprintf(stderr,
                    "PPC KVM module is not loaded. Try modprobe kvm_%s.\n",
                    (type == 2) ? "pr" : "hv");
        }
#endif
        goto err;
    }

    s->vmfd = ret;

    /* check the vcpu limits */
    soft_vcpus_limit = kvm_recommended_vcpus(s);
    hard_vcpus_limit = kvm_max_vcpus(s);

    while (nc->name) {
        if (nc->num > soft_vcpus_limit) {
            warn_report("Number of %s cpus requested (%d) exceeds "
                        "the recommended cpus supported by KVM (%d)",
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
    s->coalesced_pio = s->coalesced_mmio &&
                       kvm_check_extension(s, KVM_CAP_COALESCED_PIO);

    /*
     * Enable KVM dirty ring if supported, otherwise fall back to
     * dirty logging mode
     */
    ret = kvm_dirty_ring_init(s);
    if (ret < 0) {
        goto err;
    }

    /*
     * KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2 is not needed when dirty ring is
     * enabled.  More importantly, KVM_DIRTY_LOG_INITIALLY_SET will assume no
     * page is wr-protected initially, which is against how kvm dirty ring is
     * usage - kvm dirty ring requires all pages are wr-protected at the very
     * beginning.  Enabling this feature for dirty ring causes data corruption.
     *
     * TODO: Without KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2 and kvm clear dirty log,
     * we may expect a higher stall time when starting the migration.  In the
     * future we can enable KVM_CLEAR_DIRTY_LOG to work with dirty ring too:
     * instead of clearing dirty bit, it can be a way to explicitly wr-protect
     * guest pages.
     */
    if (!s->kvm_dirty_ring_size) {
        dirty_log_manual_caps =
            kvm_check_extension(s, KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2);
        dirty_log_manual_caps &= (KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE |
                                  KVM_DIRTY_LOG_INITIALLY_SET);
        s->manual_dirty_log_protect = dirty_log_manual_caps;
        if (dirty_log_manual_caps) {
            ret = kvm_vm_enable_cap(s, KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2, 0,
                                    dirty_log_manual_caps);
            if (ret) {
                warn_report("Trying to enable capability %"PRIu64" of "
                            "KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2 but failed. "
                            "Falling back to the legacy mode. ",
                            dirty_log_manual_caps);
                s->manual_dirty_log_protect = 0;
            }
        }
    }

#ifdef KVM_CAP_VCPU_EVENTS
    s->vcpu_events = kvm_check_extension(s, KVM_CAP_VCPU_EVENTS);
#endif
    s->max_nested_state_len = kvm_check_extension(s, KVM_CAP_NESTED_STATE);

    s->irq_set_ioctl = KVM_IRQ_LINE;
    if (kvm_check_extension(s, KVM_CAP_IRQ_INJECT_STATUS)) {
        s->irq_set_ioctl = KVM_IRQ_LINE_STATUS;
    }

    kvm_readonly_mem_allowed =
        (kvm_check_extension(s, KVM_CAP_READONLY_MEM) > 0);

    kvm_resamplefds_allowed =
        (kvm_check_extension(s, KVM_CAP_IRQFD_RESAMPLE) > 0);

    kvm_vm_attributes_allowed =
        (kvm_check_extension(s, KVM_CAP_VM_ATTRIBUTES) > 0);

#ifdef TARGET_KVM_HAVE_GUEST_DEBUG
    kvm_has_guest_debug =
        (kvm_check_extension(s, KVM_CAP_SET_GUEST_DEBUG) > 0);
#endif

    kvm_sstep_flags = 0;
    if (kvm_has_guest_debug) {
        kvm_sstep_flags = SSTEP_ENABLE;

#if defined TARGET_KVM_HAVE_GUEST_DEBUG
        int guest_debug_flags =
            kvm_check_extension(s, KVM_CAP_SET_GUEST_DEBUG2);

        if (guest_debug_flags & KVM_GUESTDBG_BLOCKIRQ) {
            kvm_sstep_flags |= SSTEP_NOIRQ;
        }
#endif
    }

    kvm_state = s;

    ret = kvm_arch_init(ms, s);
    if (ret < 0) {
        goto err;
    }

    if (s->kernel_irqchip_split == ON_OFF_AUTO_AUTO) {
        s->kernel_irqchip_split = mc->default_kernel_irqchip_split ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
    }

    qemu_register_reset(kvm_unpoison_all, NULL);

    if (s->kernel_irqchip_allowed) {
        kvm_irqchip_create(s);
    }

    s->memory_listener.listener.eventfd_add = kvm_mem_ioeventfd_add;
    s->memory_listener.listener.eventfd_del = kvm_mem_ioeventfd_del;
    s->memory_listener.listener.coalesced_io_add = kvm_coalesce_mmio_region;
    s->memory_listener.listener.coalesced_io_del = kvm_uncoalesce_mmio_region;

    kvm_memory_listener_register(s, &s->memory_listener,
                                 &address_space_memory, 0, "kvm-memory");
    memory_listener_register(&kvm_io_listener,
                             &address_space_io);

    s->sync_mmu = !!kvm_vm_check_extension(kvm_state, KVM_CAP_SYNC_MMU);
    if (!s->sync_mmu) {
        ret = ram_block_discard_disable(true);
        assert(!ret);
    }

    if (s->kvm_dirty_ring_size) {
        kvm_dirty_ring_reaper_init(s);
    }

    if (kvm_check_extension(kvm_state, KVM_CAP_BINARY_STATS_FD)) {
        add_stats_callbacks(STATS_PROVIDER_KVM, query_stats_cb,
                            query_stats_schemas_cb);
    }

    return 0;

err:
    assert(ret < 0);
    if (s->vmfd >= 0) {
        close(s->vmfd);
    }
    if (s->fd != -1) {
        close(s->fd);
    }
    g_free(s->as);
    g_free(s->memory_listener.slots);

    return ret;
}

void kvm_set_sigmask_len(KVMState *s, unsigned int sigmask_len)
{
    s->sigmask_len = sigmask_len;
}

static void kvm_handle_io(uint16_t port, MemTxAttrs attrs, void *data, int direction,
                          int size, uint32_t count)
{
    int i;
    uint8_t *ptr = data;

    for (i = 0; i < count; i++) {
        address_space_rw(&address_space_io, port, attrs,
                         ptr, size,
                         direction == KVM_EXIT_IO_OUT);
        ptr += size;
    }
}

static int kvm_handle_internal_error(CPUState *cpu, struct kvm_run *run)
{
    int i;

    fprintf(stderr, "KVM internal error. Suberror: %d\n",
            run->internal.suberror);

    for (i = 0; i < run->internal.ndata; ++i) {
        fprintf(stderr, "extra data[%d]: 0x%016"PRIx64"\n",
                i, (uint64_t)run->internal.data[i]);
    }
    if (run->internal.suberror == KVM_INTERNAL_ERROR_EMULATION) {
        fprintf(stderr, "emulation failure\n");
        if (!kvm_arch_stop_on_emulation_error(cpu)) {
            cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
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

    if (!s || s->coalesced_flush_in_progress) {
        return;
    }

    s->coalesced_flush_in_progress = true;

    if (s->coalesced_mmio_ring) {
        struct kvm_coalesced_mmio_ring *ring = s->coalesced_mmio_ring;
        while (ring->first != ring->last) {
            struct kvm_coalesced_mmio *ent;

            ent = &ring->coalesced_mmio[ring->first];

            if (ent->pio == 1) {
                address_space_write(&address_space_io, ent->phys_addr,
                                    MEMTXATTRS_UNSPECIFIED, ent->data,
                                    ent->len);
            } else {
                cpu_physical_memory_write(ent->phys_addr, ent->data, ent->len);
            }
            smp_wmb();
            ring->first = (ring->first + 1) % KVM_COALESCED_MMIO_MAX;
        }
    }

    s->coalesced_flush_in_progress = false;
}

static void do_kvm_cpu_synchronize_state(CPUState *cpu, run_on_cpu_data arg)
{
    if (!cpu->vcpu_dirty && !kvm_state->guest_state_protected) {
        int ret = kvm_arch_get_registers(cpu);
        if (ret) {
            error_report("Failed to get registers: %s", strerror(-ret));
            cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
            vm_stop(RUN_STATE_INTERNAL_ERROR);
        }

        cpu->vcpu_dirty = true;
    }
}

void kvm_cpu_synchronize_state(CPUState *cpu)
{
    if (!cpu->vcpu_dirty && !kvm_state->guest_state_protected) {
        run_on_cpu(cpu, do_kvm_cpu_synchronize_state, RUN_ON_CPU_NULL);
    }
}

static void do_kvm_cpu_synchronize_post_reset(CPUState *cpu, run_on_cpu_data arg)
{
    int ret = kvm_arch_put_registers(cpu, KVM_PUT_RESET_STATE);
    if (ret) {
        error_report("Failed to put registers after reset: %s", strerror(-ret));
        cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
        vm_stop(RUN_STATE_INTERNAL_ERROR);
    }

    cpu->vcpu_dirty = false;
}

void kvm_cpu_synchronize_post_reset(CPUState *cpu)
{
    run_on_cpu(cpu, do_kvm_cpu_synchronize_post_reset, RUN_ON_CPU_NULL);
}

static void do_kvm_cpu_synchronize_post_init(CPUState *cpu, run_on_cpu_data arg)
{
    int ret = kvm_arch_put_registers(cpu, KVM_PUT_FULL_STATE);
    if (ret) {
        error_report("Failed to put registers after init: %s", strerror(-ret));
        exit(1);
    }

    cpu->vcpu_dirty = false;
}

void kvm_cpu_synchronize_post_init(CPUState *cpu)
{
    if (!kvm_state->guest_state_protected) {
        /*
         * This runs before the machine_init_done notifiers, and is the last
         * opportunity to synchronize the state of confidential guests.
         */
        run_on_cpu(cpu, do_kvm_cpu_synchronize_post_init, RUN_ON_CPU_NULL);
    }
}

static void do_kvm_cpu_synchronize_pre_loadvm(CPUState *cpu, run_on_cpu_data arg)
{
    cpu->vcpu_dirty = true;
}

void kvm_cpu_synchronize_pre_loadvm(CPUState *cpu)
{
    run_on_cpu(cpu, do_kvm_cpu_synchronize_pre_loadvm, RUN_ON_CPU_NULL);
}

#ifdef KVM_HAVE_MCE_INJECTION
static __thread void *pending_sigbus_addr;
static __thread int pending_sigbus_code;
static __thread bool have_sigbus_pending;
#endif

static void kvm_cpu_kick(CPUState *cpu)
{
    qatomic_set(&cpu->kvm_run->immediate_exit, 1);
}

static void kvm_cpu_kick_self(void)
{
    if (kvm_immediate_exit) {
        kvm_cpu_kick(current_cpu);
    } else {
        qemu_cpu_kick_self();
    }
}

static void kvm_eat_signals(CPUState *cpu)
{
    struct timespec ts = { 0, 0 };
    siginfo_t siginfo;
    sigset_t waitset;
    sigset_t chkset;
    int r;

    if (kvm_immediate_exit) {
        qatomic_set(&cpu->kvm_run->immediate_exit, 0);
        /* Write kvm_run->immediate_exit before the cpu->exit_request
         * write in kvm_cpu_exec.
         */
        smp_wmb();
        return;
    }

    sigemptyset(&waitset);
    sigaddset(&waitset, SIG_IPI);

    do {
        r = sigtimedwait(&waitset, &siginfo, &ts);
        if (r == -1 && !(errno == EAGAIN || errno == EINTR)) {
            perror("sigtimedwait");
            exit(1);
        }

        r = sigpending(&chkset);
        if (r == -1) {
            perror("sigpending");
            exit(1);
        }
    } while (sigismember(&chkset, SIG_IPI));
}

int kvm_convert_memory(hwaddr start, hwaddr size, bool to_private)
{
    MemoryRegionSection section;
    ram_addr_t offset;
    MemoryRegion *mr;
    RAMBlock *rb;
    void *addr;
    int ret = -1;

    trace_kvm_convert_memory(start, size, to_private ? "shared_to_private" : "private_to_shared");

    if (!QEMU_PTR_IS_ALIGNED(start, qemu_real_host_page_size()) ||
        !QEMU_PTR_IS_ALIGNED(size, qemu_real_host_page_size())) {
        return -1;
    }

    if (!size) {
        return -1;
    }

    section = memory_region_find(get_system_memory(), start, size);
    mr = section.mr;
    if (!mr) {
        /*
         * Ignore converting non-assigned region to shared.
         *
         * TDX requires vMMIO region to be shared to inject #VE to guest.
         * OVMF issues conservatively MapGPA(shared) on 32bit PCI MMIO region,
         * and vIO-APIC 0xFEC00000 4K page.
         * OVMF assigns 32bit PCI MMIO region to
         * [top of low memory: typically 2GB=0xC000000,  0xFC00000)
         */
        if (!to_private) {
            return 0;
        }
        return -1;
    }

    if (!memory_region_has_guest_memfd(mr)) {
        /*
         * Because vMMIO region must be shared, guest TD may convert vMMIO
         * region to shared explicitly.  Don't complain such case.  See
         * memory_region_type() for checking if the region is MMIO region.
         */
        if (!to_private &&
            !memory_region_is_ram(mr) &&
            !memory_region_is_ram_device(mr) &&
            !memory_region_is_rom(mr) &&
            !memory_region_is_romd(mr)) {
            ret = 0;
        } else {
            error_report("Convert non guest_memfd backed memory region "
                        "(0x%"HWADDR_PRIx" ,+ 0x%"HWADDR_PRIx") to %s",
                        start, size, to_private ? "private" : "shared");
        }
        goto out_unref;
    }

    if (to_private) {
        ret = kvm_set_memory_attributes_private(start, size);
    } else {
        ret = kvm_set_memory_attributes_shared(start, size);
    }
    if (ret) {
        goto out_unref;
    }

    addr = memory_region_get_ram_ptr(mr) + section.offset_within_region;
    rb = qemu_ram_block_from_host(addr, false, &offset);

    if (to_private) {
        if (rb->page_size != qemu_real_host_page_size()) {
            /*
             * shared memory is backed by hugetlb, which is supposed to be
             * pre-allocated and doesn't need to be discarded
             */
            goto out_unref;
        }
        ret = ram_block_discard_range(rb, offset, size);
    } else {
        ret = ram_block_discard_guest_memfd_range(rb, offset, size);
    }

out_unref:
    memory_region_unref(mr);
    return ret;
}

int kvm_cpu_exec(CPUState *cpu)
{
    struct kvm_run *run = cpu->kvm_run;
    int ret, run_ret;

    trace_kvm_cpu_exec();

    if (kvm_arch_process_async_events(cpu)) {
        qatomic_set(&cpu->exit_request, 0);
        return EXCP_HLT;
    }

    bql_unlock();
    cpu_exec_start(cpu);

    do {
        MemTxAttrs attrs;

        if (cpu->vcpu_dirty) {
            ret = kvm_arch_put_registers(cpu, KVM_PUT_RUNTIME_STATE);
            if (ret) {
                error_report("Failed to put registers after init: %s",
                             strerror(-ret));
                ret = -1;
                break;
            }

            cpu->vcpu_dirty = false;
        }

        kvm_arch_pre_run(cpu, run);
        if (qatomic_read(&cpu->exit_request)) {
            trace_kvm_interrupt_exit_request();
            /*
             * KVM requires us to reenter the kernel after IO exits to complete
             * instruction emulation. This self-signal will ensure that we
             * leave ASAP again.
             */
            kvm_cpu_kick_self();
        }

        /* Read cpu->exit_request before KVM_RUN reads run->immediate_exit.
         * Matching barrier in kvm_eat_signals.
         */
        smp_rmb();

        run_ret = kvm_vcpu_ioctl(cpu, KVM_RUN, 0);

        attrs = kvm_arch_post_run(cpu, run);

#ifdef KVM_HAVE_MCE_INJECTION
        if (unlikely(have_sigbus_pending)) {
            bql_lock();
            kvm_arch_on_sigbus_vcpu(cpu, pending_sigbus_code,
                                    pending_sigbus_addr);
            have_sigbus_pending = false;
            bql_unlock();
        }
#endif

        if (run_ret < 0) {
            if (run_ret == -EINTR || run_ret == -EAGAIN) {
                trace_kvm_io_window_exit();
                kvm_eat_signals(cpu);
                ret = EXCP_INTERRUPT;
                break;
            }
            if (!(run_ret == -EFAULT && run->exit_reason == KVM_EXIT_MEMORY_FAULT)) {
                fprintf(stderr, "error: kvm run failed %s\n",
                        strerror(-run_ret));
#ifdef TARGET_PPC
                if (run_ret == -EBUSY) {
                    fprintf(stderr,
                            "This is probably because your SMT is enabled.\n"
                            "VCPU can only run on primary threads with all "
                            "secondary threads offline.\n");
                }
#endif
                ret = -1;
                break;
            }
        }

        trace_kvm_run_exit(cpu->cpu_index, run->exit_reason);
        switch (run->exit_reason) {
        case KVM_EXIT_IO:
            /* Called outside BQL */
            kvm_handle_io(run->io.port, attrs,
                          (uint8_t *)run + run->io.data_offset,
                          run->io.direction,
                          run->io.size,
                          run->io.count);
            ret = 0;
            break;
        case KVM_EXIT_MMIO:
            /* Called outside BQL */
            address_space_rw(&address_space_memory,
                             run->mmio.phys_addr, attrs,
                             run->mmio.data,
                             run->mmio.len,
                             run->mmio.is_write);
            ret = 0;
            break;
        case KVM_EXIT_IRQ_WINDOW_OPEN:
            ret = EXCP_INTERRUPT;
            break;
        case KVM_EXIT_SHUTDOWN:
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
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
        case KVM_EXIT_DIRTY_RING_FULL:
            /*
             * We shouldn't continue if the dirty ring of this vcpu is
             * still full.  Got kicked by KVM_RESET_DIRTY_RINGS.
             */
            trace_kvm_dirty_ring_full(cpu->cpu_index);
            bql_lock();
            /*
             * We throttle vCPU by making it sleep once it exit from kernel
             * due to dirty ring full. In the dirtylimit scenario, reaping
             * all vCPUs after a single vCPU dirty ring get full result in
             * the miss of sleep, so just reap the ring-fulled vCPU.
             */
            if (dirtylimit_in_service()) {
                kvm_dirty_ring_reap(kvm_state, cpu);
            } else {
                kvm_dirty_ring_reap(kvm_state, NULL);
            }
            bql_unlock();
            dirtylimit_vcpu_execute(cpu);
            ret = 0;
            break;
        case KVM_EXIT_SYSTEM_EVENT:
            trace_kvm_run_exit_system_event(cpu->cpu_index, run->system_event.type);
            switch (run->system_event.type) {
            case KVM_SYSTEM_EVENT_SHUTDOWN:
                qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
                ret = EXCP_INTERRUPT;
                break;
            case KVM_SYSTEM_EVENT_RESET:
                qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
                ret = EXCP_INTERRUPT;
                break;
            case KVM_SYSTEM_EVENT_CRASH:
                kvm_cpu_synchronize_state(cpu);
                bql_lock();
                qemu_system_guest_panicked(cpu_get_crash_info(cpu));
                bql_unlock();
                ret = 0;
                break;
            default:
                ret = kvm_arch_handle_exit(cpu, run);
                break;
            }
            break;
        case KVM_EXIT_MEMORY_FAULT:
            trace_kvm_memory_fault(run->memory_fault.gpa,
                                   run->memory_fault.size,
                                   run->memory_fault.flags);
            if (run->memory_fault.flags & ~KVM_MEMORY_EXIT_FLAG_PRIVATE) {
                error_report("KVM_EXIT_MEMORY_FAULT: Unknown flag 0x%" PRIx64,
                             (uint64_t)run->memory_fault.flags);
                ret = -1;
                break;
            }
            ret = kvm_convert_memory(run->memory_fault.gpa, run->memory_fault.size,
                                     run->memory_fault.flags & KVM_MEMORY_EXIT_FLAG_PRIVATE);
            break;
        default:
            ret = kvm_arch_handle_exit(cpu, run);
            break;
        }
    } while (ret == 0);

    cpu_exec_end(cpu);
    bql_lock();

    if (ret < 0) {
        cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
        vm_stop(RUN_STATE_INTERNAL_ERROR);
    }

    qatomic_set(&cpu->exit_request, 0);
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
    accel_ioctl_begin();
    ret = ioctl(s->vmfd, type, arg);
    accel_ioctl_end();
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
    accel_cpu_ioctl_begin(cpu);
    ret = ioctl(cpu->kvm_fd, type, arg);
    accel_cpu_ioctl_end(cpu);
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
    accel_ioctl_begin();
    ret = ioctl(fd, type, arg);
    accel_ioctl_end();
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}

int kvm_vm_check_attr(KVMState *s, uint32_t group, uint64_t attr)
{
    int ret;
    struct kvm_device_attr attribute = {
        .group = group,
        .attr = attr,
    };

    if (!kvm_vm_attributes_allowed) {
        return 0;
    }

    ret = kvm_vm_ioctl(s, KVM_HAS_DEVICE_ATTR, &attribute);
    /* kvm returns 0 on success for HAS_DEVICE_ATTR */
    return ret ? 0 : 1;
}

int kvm_device_check_attr(int dev_fd, uint32_t group, uint64_t attr)
{
    struct kvm_device_attr attribute = {
        .group = group,
        .attr = attr,
        .flags = 0,
    };

    return kvm_device_ioctl(dev_fd, KVM_HAS_DEVICE_ATTR, &attribute) ? 0 : 1;
}

int kvm_device_access(int fd, int group, uint64_t attr,
                      void *val, bool write, Error **errp)
{
    struct kvm_device_attr kvmattr;
    int err;

    kvmattr.flags = 0;
    kvmattr.group = group;
    kvmattr.attr = attr;
    kvmattr.addr = (uintptr_t)val;

    err = kvm_device_ioctl(fd,
                           write ? KVM_SET_DEVICE_ATTR : KVM_GET_DEVICE_ATTR,
                           &kvmattr);
    if (err < 0) {
        error_setg_errno(errp, -err,
                         "KVM_%s_DEVICE_ATTR failed: Group %d "
                         "attr 0x%016" PRIx64,
                         write ? "SET" : "GET", group, attr);
    }
    return err;
}

bool kvm_has_sync_mmu(void)
{
    return kvm_state->sync_mmu;
}

int kvm_has_vcpu_events(void)
{
    return kvm_state->vcpu_events;
}

int kvm_max_nested_state_length(void)
{
    return kvm_state->max_nested_state_len;
}

int kvm_has_gsi_routing(void)
{
#ifdef KVM_CAP_IRQ_ROUTING
    return kvm_check_extension(kvm_state, KVM_CAP_IRQ_ROUTING);
#else
    return false;
#endif
}

bool kvm_arm_supports_user_irq(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_ARM_USER_IRQ);
}

#ifdef TARGET_KVM_HAVE_GUEST_DEBUG
struct kvm_sw_breakpoint *kvm_find_sw_breakpoint(CPUState *cpu, vaddr pc)
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
    int err;
};

static void kvm_invoke_set_guest_debug(CPUState *cpu, run_on_cpu_data data)
{
    struct kvm_set_guest_debug_data *dbg_data =
        (struct kvm_set_guest_debug_data *) data.host_ptr;

    dbg_data->err = kvm_vcpu_ioctl(cpu, KVM_SET_GUEST_DEBUG,
                                   &dbg_data->dbg);
}

int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap)
{
    struct kvm_set_guest_debug_data data;

    data.dbg.control = reinject_trap;

    if (cpu->singlestep_enabled) {
        data.dbg.control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;

        if (cpu->singlestep_enabled & SSTEP_NOIRQ) {
            data.dbg.control |= KVM_GUESTDBG_BLOCKIRQ;
        }
    }
    kvm_arch_update_guest_debug(cpu, &data.dbg);

    run_on_cpu(cpu, kvm_invoke_set_guest_debug,
               RUN_ON_CPU_HOST_PTR(&data));
    return data.err;
}

bool kvm_supports_guest_debug(void)
{
    /* probed during kvm_init() */
    return kvm_has_guest_debug;
}

int kvm_insert_breakpoint(CPUState *cpu, int type, vaddr addr, vaddr len)
{
    struct kvm_sw_breakpoint *bp;
    int err;

    if (type == GDB_BREAKPOINT_SW) {
        bp = kvm_find_sw_breakpoint(cpu, addr);
        if (bp) {
            bp->use_count++;
            return 0;
        }

        bp = g_new(struct kvm_sw_breakpoint, 1);
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

int kvm_remove_breakpoint(CPUState *cpu, int type, vaddr addr, vaddr len)
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
    CPUState *tmpcpu;

    QTAILQ_FOREACH_SAFE(bp, &s->kvm_sw_breakpoints, entry, next) {
        if (kvm_arch_remove_sw_breakpoint(cpu, bp) != 0) {
            /* Try harder to find a CPU that currently sees the breakpoint. */
            CPU_FOREACH(tmpcpu) {
                if (kvm_arch_remove_sw_breakpoint(tmpcpu, bp) == 0) {
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

#endif /* !TARGET_KVM_HAVE_GUEST_DEBUG */

static int kvm_set_signal_mask(CPUState *cpu, const sigset_t *sigset)
{
    KVMState *s = kvm_state;
    struct kvm_signal_mask *sigmask;
    int r;

    sigmask = g_malloc(sizeof(*sigmask) + sizeof(*sigset));

    sigmask->len = s->sigmask_len;
    memcpy(sigmask->sigset, sigset, sizeof(*sigset));
    r = kvm_vcpu_ioctl(cpu, KVM_SET_SIGNAL_MASK, sigmask);
    g_free(sigmask);

    return r;
}

static void kvm_ipi_signal(int sig)
{
    if (current_cpu) {
        assert(kvm_immediate_exit);
        kvm_cpu_kick(current_cpu);
    }
}

void kvm_init_cpu_signals(CPUState *cpu)
{
    int r;
    sigset_t set;
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = kvm_ipi_signal;
    sigaction(SIG_IPI, &sigact, NULL);

    pthread_sigmask(SIG_BLOCK, NULL, &set);
#if defined KVM_HAVE_MCE_INJECTION
    sigdelset(&set, SIGBUS);
    pthread_sigmask(SIG_SETMASK, &set, NULL);
#endif
    sigdelset(&set, SIG_IPI);
    if (kvm_immediate_exit) {
        r = pthread_sigmask(SIG_SETMASK, &set, NULL);
    } else {
        r = kvm_set_signal_mask(cpu, &set);
    }
    if (r) {
        fprintf(stderr, "kvm_set_signal_mask: %s\n", strerror(-r));
        exit(1);
    }
}

/* Called asynchronously in VCPU thread.  */
int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr)
{
#ifdef KVM_HAVE_MCE_INJECTION
    if (have_sigbus_pending) {
        return 1;
    }
    have_sigbus_pending = true;
    pending_sigbus_addr = addr;
    pending_sigbus_code = code;
    qatomic_set(&cpu->exit_request, 1);
    return 0;
#else
    return 1;
#endif
}

/* Called synchronously (via signalfd) in main thread.  */
int kvm_on_sigbus(int code, void *addr)
{
#ifdef KVM_HAVE_MCE_INJECTION
    /* Action required MCE kills the process if SIGBUS is blocked.  Because
     * that's what happens in the I/O thread, where we handle MCE via signalfd,
     * we can only get action optional here.
     */
    assert(code != BUS_MCEERR_AR);
    kvm_arch_on_sigbus_vcpu(first_cpu, code, addr);
    return 0;
#else
    return 1;
#endif
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

bool kvm_device_supported(int vmfd, uint64_t type)
{
    struct kvm_create_device create_dev = {
        .type = type,
        .fd = -1,
        .flags = KVM_CREATE_DEVICE_TEST,
    };

    if (ioctl(vmfd, KVM_CHECK_EXTENSION, KVM_CAP_DEVICE_CTRL) <= 0) {
        return false;
    }

    return (ioctl(vmfd, KVM_CREATE_DEVICE, &create_dev) >= 0);
}

int kvm_set_one_reg(CPUState *cs, uint64_t id, void *source)
{
    struct kvm_one_reg reg;
    int r;

    reg.id = id;
    reg.addr = (uintptr_t) source;
    r = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (r) {
        trace_kvm_failed_reg_set(id, strerror(-r));
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
        trace_kvm_failed_reg_get(id, strerror(-r));
    }
    return r;
}

static bool kvm_accel_has_memory(MachineState *ms, AddressSpace *as,
                                 hwaddr start_addr, hwaddr size)
{
    KVMState *kvm = KVM_STATE(ms->accelerator);
    int i;

    for (i = 0; i < kvm->nr_as; ++i) {
        if (kvm->as[i].as == as && kvm->as[i].ml) {
            size = MIN(kvm_max_slot_size, size);
            return NULL != kvm_lookup_matching_slot(kvm->as[i].ml,
                                                    start_addr, size);
        }
    }

    return false;
}

static void kvm_get_kvm_shadow_mem(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    int64_t value = s->kvm_shadow_mem;

    visit_type_int(v, name, &value, errp);
}

static void kvm_set_kvm_shadow_mem(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    int64_t value;

    if (s->fd != -1) {
        error_setg(errp, "Cannot set properties after the accelerator has been initialized");
        return;
    }

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }

    s->kvm_shadow_mem = value;
}

static void kvm_set_kernel_irqchip(Object *obj, Visitor *v,
                                   const char *name, void *opaque,
                                   Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    OnOffSplit mode;

    if (s->fd != -1) {
        error_setg(errp, "Cannot set properties after the accelerator has been initialized");
        return;
    }

    if (!visit_type_OnOffSplit(v, name, &mode, errp)) {
        return;
    }
    switch (mode) {
    case ON_OFF_SPLIT_ON:
        s->kernel_irqchip_allowed = true;
        s->kernel_irqchip_required = true;
        s->kernel_irqchip_split = ON_OFF_AUTO_OFF;
        break;
    case ON_OFF_SPLIT_OFF:
        s->kernel_irqchip_allowed = false;
        s->kernel_irqchip_required = false;
        s->kernel_irqchip_split = ON_OFF_AUTO_OFF;
        break;
    case ON_OFF_SPLIT_SPLIT:
        s->kernel_irqchip_allowed = true;
        s->kernel_irqchip_required = true;
        s->kernel_irqchip_split = ON_OFF_AUTO_ON;
        break;
    default:
        /* The value was checked in visit_type_OnOffSplit() above. If
         * we get here, then something is wrong in QEMU.
         */
        abort();
    }
}

bool kvm_kernel_irqchip_allowed(void)
{
    return kvm_state->kernel_irqchip_allowed;
}

bool kvm_kernel_irqchip_required(void)
{
    return kvm_state->kernel_irqchip_required;
}

bool kvm_kernel_irqchip_split(void)
{
    return kvm_state->kernel_irqchip_split == ON_OFF_AUTO_ON;
}

static void kvm_get_dirty_ring_size(Object *obj, Visitor *v,
                                    const char *name, void *opaque,
                                    Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    uint32_t value = s->kvm_dirty_ring_size;

    visit_type_uint32(v, name, &value, errp);
}

static void kvm_set_dirty_ring_size(Object *obj, Visitor *v,
                                    const char *name, void *opaque,
                                    Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    uint32_t value;

    if (s->fd != -1) {
        error_setg(errp, "Cannot set properties after the accelerator has been initialized");
        return;
    }

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (value & (value - 1)) {
        error_setg(errp, "dirty-ring-size must be a power of two.");
        return;
    }

    s->kvm_dirty_ring_size = value;
}

static char *kvm_get_device(Object *obj,
                            Error **errp G_GNUC_UNUSED)
{
    KVMState *s = KVM_STATE(obj);

    return g_strdup(s->device);
}

static void kvm_set_device(Object *obj,
                           const char *value,
                           Error **errp G_GNUC_UNUSED)
{
    KVMState *s = KVM_STATE(obj);

    g_free(s->device);
    s->device = g_strdup(value);
}

static void kvm_set_kvm_rapl(Object *obj, bool value, Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    s->msr_energy.enable = value;
}

static void kvm_set_kvm_rapl_socket_path(Object *obj,
                                         const char *str,
                                         Error **errp)
{
    KVMState *s = KVM_STATE(obj);
    g_free(s->msr_energy.socket_path);
    s->msr_energy.socket_path = g_strdup(str);
}

static void kvm_accel_instance_init(Object *obj)
{
    KVMState *s = KVM_STATE(obj);

    s->fd = -1;
    s->vmfd = -1;
    s->kvm_shadow_mem = -1;
    s->kernel_irqchip_allowed = true;
    s->kernel_irqchip_split = ON_OFF_AUTO_AUTO;
    /* KVM dirty ring is by default off */
    s->kvm_dirty_ring_size = 0;
    s->kvm_dirty_ring_with_bitmap = false;
    s->kvm_eager_split_size = 0;
    s->notify_vmexit = NOTIFY_VMEXIT_OPTION_RUN;
    s->notify_window = 0;
    s->xen_version = 0;
    s->xen_gnttab_max_frames = 64;
    s->xen_evtchn_max_pirq = 256;
    s->device = NULL;
    s->msr_energy.enable = false;
}

/**
 * kvm_gdbstub_sstep_flags():
 *
 * Returns: SSTEP_* flags that KVM supports for guest debug. The
 * support is probed during kvm_init()
 */
static int kvm_gdbstub_sstep_flags(void)
{
    return kvm_sstep_flags;
}

static void kvm_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "KVM";
    ac->init_machine = kvm_init;
    ac->has_memory = kvm_accel_has_memory;
    ac->allowed = &kvm_allowed;
    ac->gdbstub_supported_sstep_flags = kvm_gdbstub_sstep_flags;

    object_class_property_add(oc, "kernel-irqchip", "on|off|split",
        NULL, kvm_set_kernel_irqchip,
        NULL, NULL);
    object_class_property_set_description(oc, "kernel-irqchip",
        "Configure KVM in-kernel irqchip");

    object_class_property_add(oc, "kvm-shadow-mem", "int",
        kvm_get_kvm_shadow_mem, kvm_set_kvm_shadow_mem,
        NULL, NULL);
    object_class_property_set_description(oc, "kvm-shadow-mem",
        "KVM shadow MMU size");

    object_class_property_add(oc, "dirty-ring-size", "uint32",
        kvm_get_dirty_ring_size, kvm_set_dirty_ring_size,
        NULL, NULL);
    object_class_property_set_description(oc, "dirty-ring-size",
        "Size of KVM dirty page ring buffer (default: 0, i.e. use bitmap)");

    object_class_property_add_str(oc, "device", kvm_get_device, kvm_set_device);
    object_class_property_set_description(oc, "device",
        "Path to the device node to use (default: /dev/kvm)");

    object_class_property_add_bool(oc, "rapl",
                                   NULL,
                                   kvm_set_kvm_rapl);
    object_class_property_set_description(oc, "rapl",
        "Allow energy related MSRs for RAPL interface in Guest");

    object_class_property_add_str(oc, "rapl-helper-socket", NULL,
                                  kvm_set_kvm_rapl_socket_path);
    object_class_property_set_description(oc, "rapl-helper-socket",
        "Socket Path for comminucating with the Virtual MSR helper daemon");

    kvm_arch_accel_class_init(oc);
}

static const TypeInfo kvm_accel_type = {
    .name = TYPE_KVM_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_init = kvm_accel_instance_init,
    .class_init = kvm_accel_class_init,
    .instance_size = sizeof(KVMState),
};

static void kvm_type_init(void)
{
    type_register_static(&kvm_accel_type);
}

type_init(kvm_type_init);

typedef struct StatsArgs {
    union StatsResultsType {
        StatsResultList **stats;
        StatsSchemaList **schema;
    } result;
    strList *names;
    Error **errp;
} StatsArgs;

static StatsList *add_kvmstat_entry(struct kvm_stats_desc *pdesc,
                                    uint64_t *stats_data,
                                    StatsList *stats_list,
                                    Error **errp)
{

    Stats *stats;
    uint64List *val_list = NULL;

    /* Only add stats that we understand.  */
    switch (pdesc->flags & KVM_STATS_TYPE_MASK) {
    case KVM_STATS_TYPE_CUMULATIVE:
    case KVM_STATS_TYPE_INSTANT:
    case KVM_STATS_TYPE_PEAK:
    case KVM_STATS_TYPE_LINEAR_HIST:
    case KVM_STATS_TYPE_LOG_HIST:
        break;
    default:
        return stats_list;
    }

    switch (pdesc->flags & KVM_STATS_UNIT_MASK) {
    case KVM_STATS_UNIT_NONE:
    case KVM_STATS_UNIT_BYTES:
    case KVM_STATS_UNIT_CYCLES:
    case KVM_STATS_UNIT_SECONDS:
    case KVM_STATS_UNIT_BOOLEAN:
        break;
    default:
        return stats_list;
    }

    switch (pdesc->flags & KVM_STATS_BASE_MASK) {
    case KVM_STATS_BASE_POW10:
    case KVM_STATS_BASE_POW2:
        break;
    default:
        return stats_list;
    }

    /* Alloc and populate data list */
    stats = g_new0(Stats, 1);
    stats->name = g_strdup(pdesc->name);
    stats->value = g_new0(StatsValue, 1);

    if ((pdesc->flags & KVM_STATS_UNIT_MASK) == KVM_STATS_UNIT_BOOLEAN) {
        stats->value->u.boolean = *stats_data;
        stats->value->type = QTYPE_QBOOL;
    } else if (pdesc->size == 1) {
        stats->value->u.scalar = *stats_data;
        stats->value->type = QTYPE_QNUM;
    } else {
        int i;
        for (i = 0; i < pdesc->size; i++) {
            QAPI_LIST_PREPEND(val_list, stats_data[i]);
        }
        stats->value->u.list = val_list;
        stats->value->type = QTYPE_QLIST;
    }

    QAPI_LIST_PREPEND(stats_list, stats);
    return stats_list;
}

static StatsSchemaValueList *add_kvmschema_entry(struct kvm_stats_desc *pdesc,
                                                 StatsSchemaValueList *list,
                                                 Error **errp)
{
    StatsSchemaValueList *schema_entry = g_new0(StatsSchemaValueList, 1);
    schema_entry->value = g_new0(StatsSchemaValue, 1);

    switch (pdesc->flags & KVM_STATS_TYPE_MASK) {
    case KVM_STATS_TYPE_CUMULATIVE:
        schema_entry->value->type = STATS_TYPE_CUMULATIVE;
        break;
    case KVM_STATS_TYPE_INSTANT:
        schema_entry->value->type = STATS_TYPE_INSTANT;
        break;
    case KVM_STATS_TYPE_PEAK:
        schema_entry->value->type = STATS_TYPE_PEAK;
        break;
    case KVM_STATS_TYPE_LINEAR_HIST:
        schema_entry->value->type = STATS_TYPE_LINEAR_HISTOGRAM;
        schema_entry->value->bucket_size = pdesc->bucket_size;
        schema_entry->value->has_bucket_size = true;
        break;
    case KVM_STATS_TYPE_LOG_HIST:
        schema_entry->value->type = STATS_TYPE_LOG2_HISTOGRAM;
        break;
    default:
        goto exit;
    }

    switch (pdesc->flags & KVM_STATS_UNIT_MASK) {
    case KVM_STATS_UNIT_NONE:
        break;
    case KVM_STATS_UNIT_BOOLEAN:
        schema_entry->value->has_unit = true;
        schema_entry->value->unit = STATS_UNIT_BOOLEAN;
        break;
    case KVM_STATS_UNIT_BYTES:
        schema_entry->value->has_unit = true;
        schema_entry->value->unit = STATS_UNIT_BYTES;
        break;
    case KVM_STATS_UNIT_CYCLES:
        schema_entry->value->has_unit = true;
        schema_entry->value->unit = STATS_UNIT_CYCLES;
        break;
    case KVM_STATS_UNIT_SECONDS:
        schema_entry->value->has_unit = true;
        schema_entry->value->unit = STATS_UNIT_SECONDS;
        break;
    default:
        goto exit;
    }

    schema_entry->value->exponent = pdesc->exponent;
    if (pdesc->exponent) {
        switch (pdesc->flags & KVM_STATS_BASE_MASK) {
        case KVM_STATS_BASE_POW10:
            schema_entry->value->has_base = true;
            schema_entry->value->base = 10;
            break;
        case KVM_STATS_BASE_POW2:
            schema_entry->value->has_base = true;
            schema_entry->value->base = 2;
            break;
        default:
            goto exit;
        }
    }

    schema_entry->value->name = g_strdup(pdesc->name);
    schema_entry->next = list;
    return schema_entry;
exit:
    g_free(schema_entry->value);
    g_free(schema_entry);
    return list;
}

/* Cached stats descriptors */
typedef struct StatsDescriptors {
    const char *ident; /* cache key, currently the StatsTarget */
    struct kvm_stats_desc *kvm_stats_desc;
    struct kvm_stats_header kvm_stats_header;
    QTAILQ_ENTRY(StatsDescriptors) next;
} StatsDescriptors;

static QTAILQ_HEAD(, StatsDescriptors) stats_descriptors =
    QTAILQ_HEAD_INITIALIZER(stats_descriptors);

/*
 * Return the descriptors for 'target', that either have already been read
 * or are retrieved from 'stats_fd'.
 */
static StatsDescriptors *find_stats_descriptors(StatsTarget target, int stats_fd,
                                                Error **errp)
{
    StatsDescriptors *descriptors;
    const char *ident;
    struct kvm_stats_desc *kvm_stats_desc;
    struct kvm_stats_header *kvm_stats_header;
    size_t size_desc;
    ssize_t ret;

    ident = StatsTarget_str(target);
    QTAILQ_FOREACH(descriptors, &stats_descriptors, next) {
        if (g_str_equal(descriptors->ident, ident)) {
            return descriptors;
        }
    }

    descriptors = g_new0(StatsDescriptors, 1);

    /* Read stats header */
    kvm_stats_header = &descriptors->kvm_stats_header;
    ret = pread(stats_fd, kvm_stats_header, sizeof(*kvm_stats_header), 0);
    if (ret != sizeof(*kvm_stats_header)) {
        error_setg(errp, "KVM stats: failed to read stats header: "
                   "expected %zu actual %zu",
                   sizeof(*kvm_stats_header), ret);
        g_free(descriptors);
        return NULL;
    }
    size_desc = sizeof(*kvm_stats_desc) + kvm_stats_header->name_size;

    /* Read stats descriptors */
    kvm_stats_desc = g_malloc0_n(kvm_stats_header->num_desc, size_desc);
    ret = pread(stats_fd, kvm_stats_desc,
                size_desc * kvm_stats_header->num_desc,
                kvm_stats_header->desc_offset);

    if (ret != size_desc * kvm_stats_header->num_desc) {
        error_setg(errp, "KVM stats: failed to read stats descriptors: "
                   "expected %zu actual %zu",
                   size_desc * kvm_stats_header->num_desc, ret);
        g_free(descriptors);
        g_free(kvm_stats_desc);
        return NULL;
    }
    descriptors->kvm_stats_desc = kvm_stats_desc;
    descriptors->ident = ident;
    QTAILQ_INSERT_TAIL(&stats_descriptors, descriptors, next);
    return descriptors;
}

static void query_stats(StatsResultList **result, StatsTarget target,
                        strList *names, int stats_fd, CPUState *cpu,
                        Error **errp)
{
    struct kvm_stats_desc *kvm_stats_desc;
    struct kvm_stats_header *kvm_stats_header;
    StatsDescriptors *descriptors;
    g_autofree uint64_t *stats_data = NULL;
    struct kvm_stats_desc *pdesc;
    StatsList *stats_list = NULL;
    size_t size_desc, size_data = 0;
    ssize_t ret;
    int i;

    descriptors = find_stats_descriptors(target, stats_fd, errp);
    if (!descriptors) {
        return;
    }

    kvm_stats_header = &descriptors->kvm_stats_header;
    kvm_stats_desc = descriptors->kvm_stats_desc;
    size_desc = sizeof(*kvm_stats_desc) + kvm_stats_header->name_size;

    /* Tally the total data size; read schema data */
    for (i = 0; i < kvm_stats_header->num_desc; ++i) {
        pdesc = (void *)kvm_stats_desc + i * size_desc;
        size_data += pdesc->size * sizeof(*stats_data);
    }

    stats_data = g_malloc0(size_data);
    ret = pread(stats_fd, stats_data, size_data, kvm_stats_header->data_offset);

    if (ret != size_data) {
        error_setg(errp, "KVM stats: failed to read data: "
                   "expected %zu actual %zu", size_data, ret);
        return;
    }

    for (i = 0; i < kvm_stats_header->num_desc; ++i) {
        uint64_t *stats;
        pdesc = (void *)kvm_stats_desc + i * size_desc;

        /* Add entry to the list */
        stats = (void *)stats_data + pdesc->offset;
        if (!apply_str_list_filter(pdesc->name, names)) {
            continue;
        }
        stats_list = add_kvmstat_entry(pdesc, stats, stats_list, errp);
    }

    if (!stats_list) {
        return;
    }

    switch (target) {
    case STATS_TARGET_VM:
        add_stats_entry(result, STATS_PROVIDER_KVM, NULL, stats_list);
        break;
    case STATS_TARGET_VCPU:
        add_stats_entry(result, STATS_PROVIDER_KVM,
                        cpu->parent_obj.canonical_path,
                        stats_list);
        break;
    default:
        g_assert_not_reached();
    }
}

static void query_stats_schema(StatsSchemaList **result, StatsTarget target,
                               int stats_fd, Error **errp)
{
    struct kvm_stats_desc *kvm_stats_desc;
    struct kvm_stats_header *kvm_stats_header;
    StatsDescriptors *descriptors;
    struct kvm_stats_desc *pdesc;
    StatsSchemaValueList *stats_list = NULL;
    size_t size_desc;
    int i;

    descriptors = find_stats_descriptors(target, stats_fd, errp);
    if (!descriptors) {
        return;
    }

    kvm_stats_header = &descriptors->kvm_stats_header;
    kvm_stats_desc = descriptors->kvm_stats_desc;
    size_desc = sizeof(*kvm_stats_desc) + kvm_stats_header->name_size;

    /* Tally the total data size; read schema data */
    for (i = 0; i < kvm_stats_header->num_desc; ++i) {
        pdesc = (void *)kvm_stats_desc + i * size_desc;
        stats_list = add_kvmschema_entry(pdesc, stats_list, errp);
    }

    add_stats_schema(result, STATS_PROVIDER_KVM, target, stats_list);
}

static void query_stats_vcpu(CPUState *cpu, StatsArgs *kvm_stats_args)
{
    int stats_fd = cpu->kvm_vcpu_stats_fd;
    Error *local_err = NULL;

    if (stats_fd == -1) {
        error_setg_errno(&local_err, errno, "KVM stats: ioctl failed");
        error_propagate(kvm_stats_args->errp, local_err);
        return;
    }
    query_stats(kvm_stats_args->result.stats, STATS_TARGET_VCPU,
                kvm_stats_args->names, stats_fd, cpu,
                kvm_stats_args->errp);
}

static void query_stats_schema_vcpu(CPUState *cpu, StatsArgs *kvm_stats_args)
{
    int stats_fd = cpu->kvm_vcpu_stats_fd;
    Error *local_err = NULL;

    if (stats_fd == -1) {
        error_setg_errno(&local_err, errno, "KVM stats: ioctl failed");
        error_propagate(kvm_stats_args->errp, local_err);
        return;
    }
    query_stats_schema(kvm_stats_args->result.schema, STATS_TARGET_VCPU, stats_fd,
                       kvm_stats_args->errp);
}

static void query_stats_cb(StatsResultList **result, StatsTarget target,
                           strList *names, strList *targets, Error **errp)
{
    KVMState *s = kvm_state;
    CPUState *cpu;
    int stats_fd;

    switch (target) {
    case STATS_TARGET_VM:
    {
        stats_fd = kvm_vm_ioctl(s, KVM_GET_STATS_FD, NULL);
        if (stats_fd == -1) {
            error_setg_errno(errp, errno, "KVM stats: ioctl failed");
            return;
        }
        query_stats(result, target, names, stats_fd, NULL, errp);
        close(stats_fd);
        break;
    }
    case STATS_TARGET_VCPU:
    {
        StatsArgs stats_args;
        stats_args.result.stats = result;
        stats_args.names = names;
        stats_args.errp = errp;
        CPU_FOREACH(cpu) {
            if (!apply_str_list_filter(cpu->parent_obj.canonical_path, targets)) {
                continue;
            }
            query_stats_vcpu(cpu, &stats_args);
        }
        break;
    }
    default:
        break;
    }
}

void query_stats_schemas_cb(StatsSchemaList **result, Error **errp)
{
    StatsArgs stats_args;
    KVMState *s = kvm_state;
    int stats_fd;

    stats_fd = kvm_vm_ioctl(s, KVM_GET_STATS_FD, NULL);
    if (stats_fd == -1) {
        error_setg_errno(errp, errno, "KVM stats: ioctl failed");
        return;
    }
    query_stats_schema(result, STATS_TARGET_VM, stats_fd, errp);
    close(stats_fd);

    if (first_cpu) {
        stats_args.result.schema = result;
        stats_args.errp = errp;
        query_stats_schema_vcpu(first_cpu, &stats_args);
    }
}

void kvm_mark_guest_state_protected(void)
{
    kvm_state->guest_state_protected = true;
}

int kvm_create_guest_memfd(uint64_t size, uint64_t flags, Error **errp)
{
    int fd;
    struct kvm_create_guest_memfd guest_memfd = {
        .size = size,
        .flags = flags,
    };

    if (!kvm_guest_memfd_supported) {
        error_setg(errp, "KVM does not support guest_memfd");
        return -1;
    }

    fd = kvm_vm_ioctl(kvm_state, KVM_CREATE_GUEST_MEMFD, &guest_memfd);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Error creating KVM guest_memfd");
        return -1;
    }

    return fd;
}
