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
#include "exec/gdbstub.h"
#include "sysemu/kvm_int.h"
#include "sysemu/runstate.h"
#include "sysemu/cpus.h"
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

#include "hw/boards.h"

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
#define PAGE_SIZE qemu_real_host_page_size

#ifndef KVM_GUESTDBG_BLOCKIRQ
#define KVM_GUESTDBG_BLOCKIRQ 0
#endif

//#define DEBUG_KVM

#ifdef DEBUG_KVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#define KVM_MSI_HASHTAB_SIZE    256

struct KVMParkedVcpu {
    unsigned long vcpu_id;
    int kvm_fd;
    QLIST_ENTRY(KVMParkedVcpu) node;
};

enum KVMDirtyRingReaperState {
    KVM_DIRTY_RING_REAPER_NONE = 0,
    /* The reaper is sleeping */
    KVM_DIRTY_RING_REAPER_WAIT,
    /* The reaper is reaping for dirty pages */
    KVM_DIRTY_RING_REAPER_REAPING,
};

/*
 * KVM reaper instance, responsible for collecting the KVM dirty bits
 * via the dirty ring.
 */
struct KVMDirtyRingReaper {
    /* The reaper thread */
    QemuThread reaper_thr;
    volatile uint64_t reaper_iteration; /* iteration number of reaper thr */
    volatile enum KVMDirtyRingReaperState reaper_state; /* reap thr state */
};

struct KVMState
{
    AccelState parent_obj;

    int nr_slots;
    int fd;
    int vmfd;
    int coalesced_mmio;
    int coalesced_pio;
    struct kvm_coalesced_mmio_ring *coalesced_mmio_ring;
    bool coalesced_flush_in_progress;
    int vcpu_events;
    int robust_singlestep;
    int debugregs;
#ifdef KVM_CAP_SET_GUEST_DEBUG
    QTAILQ_HEAD(, kvm_sw_breakpoint) kvm_sw_breakpoints;
#endif
    int max_nested_state_len;
    int many_ioeventfds;
    int intx_set_mask;
    int kvm_shadow_mem;
    bool kernel_irqchip_allowed;
    bool kernel_irqchip_required;
    OnOffAuto kernel_irqchip_split;
    bool sync_mmu;
    uint64_t manual_dirty_log_protect;
    /* The man page (and posix) say ioctl numbers are signed int, but
     * they're not.  Linux, glibc and *BSD all treat ioctl numbers as
     * unsigned, and treating them as signed here can break things */
    unsigned irq_set_ioctl;
    unsigned int sigmask_len;
    GHashTable *gsimap;
#ifdef KVM_CAP_IRQ_ROUTING
    struct kvm_irq_routing *irq_routes;
    int nr_allocated_irq_routes;
    unsigned long *used_gsi_bitmap;
    unsigned int gsi_count;
    QTAILQ_HEAD(, KVMMSIRoute) msi_hashtab[KVM_MSI_HASHTAB_SIZE];
#endif
    KVMMemoryListener memory_listener;
    QLIST_HEAD(, KVMParkedVcpu) kvm_parked_vcpus;

    /* For "info mtree -f" to tell if an MR is registered in KVM */
    int nr_as;
    struct KVMAs {
        KVMMemoryListener *ml;
        AddressSpace *as;
    } *as;
    uint64_t kvm_dirty_ring_bytes;  /* Size of the per-vcpu dirty ring */
    uint32_t kvm_dirty_ring_size;   /* Number of dirty GFNs per ring */
    struct KVMDirtyRingReaper reaper;
};

KVMState *kvm_state;
bool kvm_kernel_irqchip;
bool kvm_split_irqchip;
bool kvm_async_interrupts_allowed;
bool kvm_halt_in_kernel_allowed;
bool kvm_eventfds_allowed;
bool kvm_irqfds_allowed;
bool kvm_resamplefds_allowed;
bool kvm_msi_via_irqfd_allowed;
bool kvm_gsi_routing_allowed;
bool kvm_gsi_direct_mapping;
bool kvm_allowed;
bool kvm_readonly_mem_allowed;
bool kvm_vm_attributes_allowed;
bool kvm_direct_msi_allowed;
bool kvm_ioeventfd_any_length_allowed;
bool kvm_msi_use_devid;
bool kvm_has_guest_debug;
int kvm_sstep_flags;
static bool kvm_immediate_exit;
static hwaddr kvm_max_slot_size = ~0;

static const KVMCapabilityInfo kvm_required_capabilites[] = {
    KVM_CAP_INFO(USER_MEMORY),
    KVM_CAP_INFO(DESTROY_MEMORY_REGION_WORKS),
    KVM_CAP_INFO(JOIN_MEMORY_REGIONS_WORKS),
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

int kvm_get_max_memslots(void)
{
    KVMState *s = KVM_STATE(current_accel());

    return s->nr_slots;
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

bool kvm_has_free_slot(MachineState *ms)
{
    KVMState *s = KVM_STATE(ms->accelerator);
    bool result;
    KVMMemoryListener *kml = &s->memory_listener;

    kvm_slots_lock();
    result = !!kvm_get_free_slot(kml);
    kvm_slots_unlock();

    return result;
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
                       qemu_real_host_page_size);
    delta = aligned - section->offset_within_address_space;
    *start = aligned;
    if (delta > size) {
        return 0;
    }

    return (size - delta) & qemu_real_host_page_mask;
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
    struct kvm_userspace_memory_region mem;
    int ret;

    mem.slot = slot->slot | (kml->as_id << 16);
    mem.guest_phys_addr = slot->start_addr;
    mem.userspace_addr = (unsigned long)slot->ram;
    mem.flags = slot->flags;

    if (slot->memory_size && !new && (mem.flags ^ slot->old_flags) & KVM_MEM_READONLY) {
        /* Set the slot size to 0 before setting the slot to the desired
         * value. This is needed based on KVM commit 75d61fbc. */
        mem.memory_size = 0;
        ret = kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
        if (ret < 0) {
            goto err;
        }
    }
    mem.memory_size = slot->memory_size;
    ret = kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
    slot->old_flags = mem.flags;
err:
    trace_kvm_set_user_memory(mem.slot, mem.flags, mem.guest_phys_addr,
                              mem.memory_size, mem.userspace_addr, ret);
    if (ret < 0) {
        error_report("%s: KVM_SET_USER_MEMORY_REGION failed, slot=%d,"
                     " start=0x%" PRIx64 ", size=0x%" PRIx64 ": %s",
                     __func__, mem.slot, slot->start_addr,
                     (uint64_t)mem.memory_size, strerror(errno));
    }
    return ret;
}

static int do_kvm_destroy_vcpu(CPUState *cpu)
{
    KVMState *s = kvm_state;
    long mmap_size;
    struct KVMParkedVcpu *vcpu = NULL;
    int ret = 0;

    DPRINTF("kvm_destroy_vcpu\n");

    ret = kvm_arch_destroy_vcpu(cpu);
    if (ret < 0) {
        goto err;
    }

    mmap_size = kvm_ioctl(s, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size < 0) {
        ret = mmap_size;
        DPRINTF("KVM_GET_VCPU_MMAP_SIZE failed\n");
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

    vcpu = g_malloc0(sizeof(*vcpu));
    vcpu->vcpu_id = kvm_arch_vcpu_id(cpu);
    vcpu->kvm_fd = cpu->kvm_fd;
    QLIST_INSERT_HEAD(&kvm_state->kvm_parked_vcpus, vcpu, node);
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

static int kvm_get_vcpu(KVMState *s, unsigned long vcpu_id)
{
    struct KVMParkedVcpu *cpu;

    QLIST_FOREACH(cpu, &s->kvm_parked_vcpus, node) {
        if (cpu->vcpu_id == vcpu_id) {
            int kvm_fd;

            QLIST_REMOVE(cpu, node);
            kvm_fd = cpu->kvm_fd;
            g_free(cpu);
            return kvm_fd;
        }
    }

    return kvm_vm_ioctl(s, KVM_CREATE_VCPU, (void *)vcpu_id);
}

int kvm_init_vcpu(CPUState *cpu, Error **errp)
{
    KVMState *s = kvm_state;
    long mmap_size;
    int ret;

    trace_kvm_init_vcpu(cpu->cpu_index, kvm_arch_vcpu_id(cpu));

    ret = kvm_get_vcpu(s, kvm_arch_vcpu_id(cpu));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "kvm_init_vcpu: kvm_get_vcpu failed (%lu)",
                         kvm_arch_vcpu_id(cpu));
        goto err;
    }

    cpu->kvm_fd = ret;
    cpu->kvm_state = s;
    cpu->vcpu_dirty = true;
    cpu->dirty_pages = 0;

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
            DPRINTF("mmap'ing vcpu dirty gfns failed: %d\n", ret);
            goto err;
        }
    }

    ret = kvm_arch_init_vcpu(cpu);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "kvm_init_vcpu: kvm_arch_init_vcpu failed (%lu)",
                         kvm_arch_vcpu_id(cpu));
    }
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
    ram_addr_t pages = slot->memory_size / qemu_real_host_page_size;

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
    hwaddr bitmap_size = ALIGN(mem->memory_size / qemu_real_host_page_size,
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
        (mem->memory_size / qemu_real_host_page_size)) {
        return;
    }

    set_bit(offset, mem->dirty_bmap);
}

static bool dirty_gfn_is_dirtied(struct kvm_dirty_gfn *gfn)
{
    return gfn->flags == KVM_DIRTY_GFN_F_DIRTY;
}

static void dirty_gfn_set_collected(struct kvm_dirty_gfn *gfn)
{
    gfn->flags = KVM_DIRTY_GFN_F_RESET;
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
static uint64_t kvm_dirty_ring_reap_locked(KVMState *s)
{
    int ret;
    CPUState *cpu;
    uint64_t total = 0;
    int64_t stamp;

    stamp = get_clock();

    CPU_FOREACH(cpu) {
        total += kvm_dirty_ring_reap_one(s, cpu);
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
static uint64_t kvm_dirty_ring_reap(KVMState *s)
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
    total = kvm_dirty_ring_reap_locked(s);
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
    assert(qemu_mutex_iothread_locked());
    /*
     * First make sure to flush the hardware buffers by kicking all
     * vcpus out in a synchronous way.
     */
    kvm_cpu_synchronize_kick_all();
    kvm_dirty_ring_reap(kvm_state);
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
#define KVM_CLEAR_LOG_ALIGN  (qemu_real_host_page_size << KVM_CLEAR_LOG_SHIFT)
#define KVM_CLEAR_LOG_MASK   (-KVM_CLEAR_LOG_ALIGN)

static int kvm_log_clear_one_slot(KVMSlot *mem, int as_id, uint64_t start,
                                  uint64_t size)
{
    KVMState *s = kvm_state;
    uint64_t end, bmap_start, start_delta, bmap_npages;
    struct kvm_clear_dirty_log d;
    unsigned long *bmap_clear = NULL, psize = qemu_real_host_page_size;
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

static MemoryListener kvm_coalesced_pio_listener = {
    .name = "kvm-coalesced-pio",
    .coalesced_io_add = kvm_coalesce_pio_add,
    .coalesced_io_del = kvm_coalesce_pio_del,
};

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

static uint32_t adjust_ioeventfd_endianness(uint32_t val, uint32_t size)
{
#if defined(HOST_WORDS_BIGENDIAN) != defined(TARGET_WORDS_BIGENDIAN)
    /* The kernel expects ioeventfd values in HOST_WORDS_BIGENDIAN
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

void kvm_set_max_memslot_size(hwaddr max_slot_size)
{
    g_assert(
        ROUND_UP(max_slot_size, qemu_real_host_page_size) == max_slot_size
    );
    kvm_max_slot_size = max_slot_size;
}

static void kvm_set_phys_mem(KVMMemoryListener *kml,
                             MemoryRegionSection *section, bool add)
{
    KVMSlot *mem;
    int err;
    MemoryRegion *mr = section->mr;
    bool writeable = !mr->readonly && !mr->rom_device;
    hwaddr start_addr, size, slot_size, mr_offset;
    ram_addr_t ram_start_offset;
    void *ram;

    if (!memory_region_is_ram(mr)) {
        if (writeable || !kvm_readonly_mem_allowed) {
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

    kvm_slots_lock();

    if (!add) {
        do {
            slot_size = MIN(kvm_max_slot_size, size);
            mem = kvm_lookup_matching_slot(kml, start_addr, slot_size);
            if (!mem) {
                goto out;
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
                    kvm_dirty_ring_reap_locked(kvm_state);
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
        } while (size);
        goto out;
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
        kvm_slot_init_dirty_bitmap(mem);
        err = kvm_set_user_memory_region(kml, mem, true);
        if (err) {
            fprintf(stderr, "%s: error registering slot: %s\n", __func__,
                    strerror(-err));
            abort();
        }
        start_addr += slot_size;
        ram_start_offset += slot_size;
        ram += slot_size;
        size -= slot_size;
    } while (size);

out:
    kvm_slots_unlock();
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

        trace_kvm_dirty_ring_reaper("wakeup");
        r->reaper_state = KVM_DIRTY_RING_REAPER_REAPING;

        qemu_mutex_lock_iothread();
        kvm_dirty_ring_reap(s);
        qemu_mutex_unlock_iothread();

        r->reaper_iteration++;
    }

    trace_kvm_dirty_ring_reaper("exit");

    rcu_unregister_thread();

    return NULL;
}

static int kvm_dirty_ring_reaper_init(KVMState *s)
{
    struct KVMDirtyRingReaper *r = &s->reaper;

    qemu_thread_create(&r->reaper_thr, "kvm-reaper",
                       kvm_dirty_ring_reaper_thread,
                       s, QEMU_THREAD_JOINABLE);

    return 0;
}

static void kvm_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);

    memory_region_ref(section->mr);
    kvm_set_phys_mem(kml, section, true);
}

static void kvm_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);

    kvm_set_phys_mem(kml, section, false);
    memory_region_unref(section->mr);
}

static void kvm_log_sync(MemoryListener *listener,
                         MemoryRegionSection *section)
{
    KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);

    kvm_slots_lock();
    kvm_physical_sync_dirty_bitmap(kml, section);
    kvm_slots_unlock();
}

static void kvm_log_sync_global(MemoryListener *l)
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

    kml->slots = g_malloc0(s->nr_slots * sizeof(KVMSlot));
    kml->as_id = as_id;

    for (i = 0; i < s->nr_slots; i++) {
        kml->slots[i].slot = i;
    }

    kml->listener.region_add = kvm_region_add;
    kml->listener.region_del = kvm_region_del;
    kml->listener.log_start = kvm_log_start;
    kml->listener.log_stop = kvm_log_stop;
    kml->listener.priority = 10;
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
    .eventfd_add = kvm_io_ioeventfd_add,
    .eventfd_del = kvm_io_ioeventfd_del,
    .priority = 10,
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
    int gsi_count, i;

    gsi_count = kvm_check_extension(s, KVM_CAP_IRQ_ROUTING) - 1;
    if (gsi_count > 0) {
        /* Round up so we can search ints using ffs */
        s->used_gsi_bitmap = bitmap_new(gsi_count);
        s->gsi_count = gsi_count;
    }

    s->irq_routes = g_malloc0(sizeof(*s->irq_routes));
    s->nr_allocated_irq_routes = 0;

    if (!kvm_direct_msi_allowed) {
        for (i = 0; i < KVM_MSI_HASHTAB_SIZE; i++) {
            QTAILQ_INIT(&s->msi_hashtab[i]);
        }
    }

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
    int next_virq;

    /*
     * PIC and IOAPIC share the first 16 GSI numbers, thus the available
     * GSI numbers are more than the number of IRQ route. Allocating a GSI
     * number can succeed even though a new route entry cannot be added.
     * When this happens, flush dynamic MSI entries to free IRQ route entries.
     */
    if (!kvm_direct_msi_allowed && s->irq_routes->nr == s->gsi_count) {
        kvm_flush_dynamic_msi_routes(s);
    }

    /* Return the lowest unused GSI in the bitmap */
    next_virq = find_first_zero_bit(s->used_gsi_bitmap, s->gsi_count);
    if (next_virq >= s->gsi_count) {
        return -ENOSPC;
    } else {
        return next_virq;
    }
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

    if (kvm_direct_msi_allowed) {
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

int kvm_irqchip_add_msi_route(KVMState *s, int vector, PCIDevice *dev)
{
    struct kvm_irq_routing_entry kroute = {};
    int virq;
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

    trace_kvm_irqchip_add_msi_route(dev ? dev->name : (char *)"N/A",
                                    vector, virq);

    kvm_add_routing_entry(s, &kroute);
    kvm_arch_add_msi_route_post(&kroute, vector, dev);
    kvm_irqchip_commit_routes(s);

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

    if (!kvm_irqfds_enabled()) {
        return -ENOSYS;
    }

    return kvm_vm_ioctl(s, KVM_IRQFD, &irqfd);
}

int kvm_irqchip_add_adapter_route(KVMState *s, AdapterInfo *adapter)
{
    struct kvm_irq_routing_entry kroute = {};
    int virq;

    if (!kvm_gsi_routing_enabled()) {
        return -ENOSYS;
    }

    virq = kvm_irqchip_get_virq(s);
    if (virq < 0) {
        return virq;
    }

    kroute.gsi = virq;
    kroute.type = KVM_IRQ_ROUTING_S390_ADAPTER;
    kroute.flags = 0;
    kroute.u.adapter.summary_addr = adapter->summary_addr;
    kroute.u.adapter.ind_addr = adapter->ind_addr;
    kroute.u.adapter.summary_offset = adapter->summary_offset;
    kroute.u.adapter.ind_offset = adapter->ind_offset;
    kroute.u.adapter.adapter_id = adapter->adapter_id;

    kvm_add_routing_entry(s, &kroute);

    return virq;
}

int kvm_irqchip_add_hv_sint_route(KVMState *s, uint32_t vcpu, uint32_t sint)
{
    struct kvm_irq_routing_entry kroute = {};
    int virq;

    if (!kvm_gsi_routing_enabled()) {
        return -ENOSYS;
    }
    if (!kvm_check_extension(s, KVM_CAP_HYPERV_SYNIC)) {
        return -ENOSYS;
    }
    virq = kvm_irqchip_get_virq(s);
    if (virq < 0) {
        return virq;
    }

    kroute.gsi = virq;
    kroute.type = KVM_IRQ_ROUTING_HV_SINT;
    kroute.flags = 0;
    kroute.u.hv_sint.vcpu = vcpu;
    kroute.u.hv_sint.sint = sint;

    kvm_add_routing_entry(s, &kroute);
    kvm_irqchip_commit_routes(s);

    return virq;
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

int kvm_irqchip_add_msi_route(KVMState *s, int vector, PCIDevice *dev)
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

    /* First probe and see if there's a arch-specific hook to create the
     * in-kernel irqchip for us */
    ret = kvm_arch_irqchip_create(s);
    if (ret == 0) {
        if (s->kernel_irqchip_split == ON_OFF_AUTO_ON) {
            perror("Split IRQ chip mode not supported.");
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
    return kvm_state->kvm_dirty_ring_size ? true : false;
}

static int kvm_init(MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    static const char upgrade_note[] =
        "Please upgrade to at least kernel 2.6.29 or recent kvm-kmod\n"
        "(see http://sourceforge.net/projects/kvm).\n";
    struct {
        const char *name;
        int num;
    } num_cpus[] = {
        { "SMP",          ms->smp.cpus },
        { "hotpluggable", ms->smp.max_cpus },
        { NULL, }
    }, *nc = num_cpus;
    int soft_vcpus_limit, hard_vcpus_limit;
    KVMState *s;
    const KVMCapabilityInfo *missing_cap;
    int ret;
    int type = 0;
    uint64_t dirty_log_manual_caps;

    qemu_mutex_init(&kml_slots_lock);

    s = KVM_STATE(ms->accelerator);

    /*
     * On systems where the kernel can support different base page
     * sizes, host page size may be different from TARGET_PAGE_SIZE,
     * even with KVM.  TARGET_PAGE_SIZE is assumed to be the minimum
     * page size for the system though.
     */
    assert(TARGET_PAGE_SIZE <= qemu_real_host_page_size);

    s->sigmask_len = 8;

#ifdef KVM_CAP_SET_GUEST_DEBUG
    QTAILQ_INIT(&s->kvm_sw_breakpoints);
#endif
    QLIST_INIT(&s->kvm_parked_vcpus);
    s->fd = qemu_open_old("/dev/kvm", O_RDWR);
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
    if (s->kvm_dirty_ring_size > 0) {
        uint64_t ring_bytes;

        ring_bytes = s->kvm_dirty_ring_size * sizeof(struct kvm_dirty_gfn);

        /* Read the max supported pages */
        ret = kvm_vm_check_extension(s, KVM_CAP_DIRTY_LOG_RING);
        if (ret > 0) {
            if (ring_bytes > ret) {
                error_report("KVM dirty ring size %" PRIu32 " too big "
                             "(maximum is %ld).  Please use a smaller value.",
                             s->kvm_dirty_ring_size,
                             (long)ret / sizeof(struct kvm_dirty_gfn));
                ret = -EINVAL;
                goto err;
            }

            ret = kvm_vm_enable_cap(s, KVM_CAP_DIRTY_LOG_RING, 0, ring_bytes);
            if (ret) {
                error_report("Enabling of KVM dirty ring failed: %s. "
                             "Suggested minimum value is 1024.", strerror(-ret));
                goto err;
            }

            s->kvm_dirty_ring_bytes = ring_bytes;
         } else {
             warn_report("KVM dirty ring not available, using bitmap method");
             s->kvm_dirty_ring_size = 0;
        }
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

    s->robust_singlestep =
        kvm_check_extension(s, KVM_CAP_X86_ROBUST_SINGLESTEP);

#ifdef KVM_CAP_DEBUGREGS
    s->debugregs = kvm_check_extension(s, KVM_CAP_DEBUGREGS);
#endif

    s->max_nested_state_len = kvm_check_extension(s, KVM_CAP_NESTED_STATE);

#ifdef KVM_CAP_IRQ_ROUTING
    kvm_direct_msi_allowed = (kvm_check_extension(s, KVM_CAP_SIGNAL_MSI) > 0);
#endif

    s->intx_set_mask = kvm_check_extension(s, KVM_CAP_PCI_2_3);

    s->irq_set_ioctl = KVM_IRQ_LINE;
    if (kvm_check_extension(s, KVM_CAP_IRQ_INJECT_STATUS)) {
        s->irq_set_ioctl = KVM_IRQ_LINE_STATUS;
    }

    kvm_readonly_mem_allowed =
        (kvm_check_extension(s, KVM_CAP_READONLY_MEM) > 0);

    kvm_eventfds_allowed =
        (kvm_check_extension(s, KVM_CAP_IOEVENTFD) > 0);

    kvm_irqfds_allowed =
        (kvm_check_extension(s, KVM_CAP_IRQFD) > 0);

    kvm_resamplefds_allowed =
        (kvm_check_extension(s, KVM_CAP_IRQFD_RESAMPLE) > 0);

    kvm_vm_attributes_allowed =
        (kvm_check_extension(s, KVM_CAP_VM_ATTRIBUTES) > 0);

    kvm_ioeventfd_any_length_allowed =
        (kvm_check_extension(s, KVM_CAP_IOEVENTFD_ANY_LENGTH) > 0);

#ifdef KVM_CAP_SET_GUEST_DEBUG
    kvm_has_guest_debug =
        (kvm_check_extension(s, KVM_CAP_SET_GUEST_DEBUG) > 0);
#endif

    kvm_sstep_flags = 0;
    if (kvm_has_guest_debug) {
        kvm_sstep_flags = SSTEP_ENABLE;

#if defined KVM_CAP_SET_GUEST_DEBUG2
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

    if (kvm_eventfds_allowed) {
        s->memory_listener.listener.eventfd_add = kvm_mem_ioeventfd_add;
        s->memory_listener.listener.eventfd_del = kvm_mem_ioeventfd_del;
    }
    s->memory_listener.listener.coalesced_io_add = kvm_coalesce_mmio_region;
    s->memory_listener.listener.coalesced_io_del = kvm_uncoalesce_mmio_region;

    kvm_memory_listener_register(s, &s->memory_listener,
                                 &address_space_memory, 0, "kvm-memory");
    if (kvm_eventfds_allowed) {
        memory_listener_register(&kvm_io_listener,
                                 &address_space_io);
    }
    memory_listener_register(&kvm_coalesced_pio_listener,
                             &address_space_io);

    s->many_ioeventfds = kvm_check_many_ioeventfds();

    s->sync_mmu = !!kvm_vm_check_extension(kvm_state, KVM_CAP_SYNC_MMU);
    if (!s->sync_mmu) {
        ret = ram_block_discard_disable(true);
        assert(!ret);
    }

    if (s->kvm_dirty_ring_size) {
        ret = kvm_dirty_ring_reaper_init(s);
        if (ret) {
            goto err;
        }
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
    fprintf(stderr, "KVM internal error. Suberror: %d\n",
            run->internal.suberror);

    if (kvm_check_extension(kvm_state, KVM_CAP_INTERNAL_ERROR_DATA)) {
        int i;

        for (i = 0; i < run->internal.ndata; ++i) {
            fprintf(stderr, "extra data[%d]: 0x%016"PRIx64"\n",
                    i, (uint64_t)run->internal.data[i]);
        }
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

    if (s->coalesced_flush_in_progress) {
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

bool kvm_cpu_check_are_resettable(void)
{
    return kvm_arch_cpu_check_are_resettable();
}

static void do_kvm_cpu_synchronize_state(CPUState *cpu, run_on_cpu_data arg)
{
    if (!cpu->vcpu_dirty) {
        kvm_arch_get_registers(cpu);
        cpu->vcpu_dirty = true;
    }
}

void kvm_cpu_synchronize_state(CPUState *cpu)
{
    if (!cpu->vcpu_dirty) {
        run_on_cpu(cpu, do_kvm_cpu_synchronize_state, RUN_ON_CPU_NULL);
    }
}

static void do_kvm_cpu_synchronize_post_reset(CPUState *cpu, run_on_cpu_data arg)
{
    kvm_arch_put_registers(cpu, KVM_PUT_RESET_STATE);
    cpu->vcpu_dirty = false;
}

void kvm_cpu_synchronize_post_reset(CPUState *cpu)
{
    run_on_cpu(cpu, do_kvm_cpu_synchronize_post_reset, RUN_ON_CPU_NULL);
}

static void do_kvm_cpu_synchronize_post_init(CPUState *cpu, run_on_cpu_data arg)
{
    kvm_arch_put_registers(cpu, KVM_PUT_FULL_STATE);
    cpu->vcpu_dirty = false;
}

void kvm_cpu_synchronize_post_init(CPUState *cpu)
{
    run_on_cpu(cpu, do_kvm_cpu_synchronize_post_init, RUN_ON_CPU_NULL);
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

int kvm_cpu_exec(CPUState *cpu)
{
    struct kvm_run *run = cpu->kvm_run;
    int ret, run_ret;

    DPRINTF("kvm_cpu_exec()\n");

    if (kvm_arch_process_async_events(cpu)) {
        qatomic_set(&cpu->exit_request, 0);
        return EXCP_HLT;
    }

    qemu_mutex_unlock_iothread();
    cpu_exec_start(cpu);

    do {
        MemTxAttrs attrs;

        if (cpu->vcpu_dirty) {
            kvm_arch_put_registers(cpu, KVM_PUT_RUNTIME_STATE);
            cpu->vcpu_dirty = false;
        }

        kvm_arch_pre_run(cpu, run);
        if (qatomic_read(&cpu->exit_request)) {
            DPRINTF("interrupt exit requested\n");
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
            qemu_mutex_lock_iothread();
            kvm_arch_on_sigbus_vcpu(cpu, pending_sigbus_code,
                                    pending_sigbus_addr);
            have_sigbus_pending = false;
            qemu_mutex_unlock_iothread();
        }
#endif

        if (run_ret < 0) {
            if (run_ret == -EINTR || run_ret == -EAGAIN) {
                DPRINTF("io window exit\n");
                kvm_eat_signals(cpu);
                ret = EXCP_INTERRUPT;
                break;
            }
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

        trace_kvm_run_exit(cpu->cpu_index, run->exit_reason);
        switch (run->exit_reason) {
        case KVM_EXIT_IO:
            DPRINTF("handle_io\n");
            /* Called outside BQL */
            kvm_handle_io(run->io.port, attrs,
                          (uint8_t *)run + run->io.data_offset,
                          run->io.direction,
                          run->io.size,
                          run->io.count);
            ret = 0;
            break;
        case KVM_EXIT_MMIO:
            DPRINTF("handle_mmio\n");
            /* Called outside BQL */
            address_space_rw(&address_space_memory,
                             run->mmio.phys_addr, attrs,
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
            qemu_mutex_lock_iothread();
            kvm_dirty_ring_reap(kvm_state);
            qemu_mutex_unlock_iothread();
            ret = 0;
            break;
        case KVM_EXIT_SYSTEM_EVENT:
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
                qemu_mutex_lock_iothread();
                qemu_system_guest_panicked(cpu_get_crash_info(cpu));
                qemu_mutex_unlock_iothread();
                ret = 0;
                break;
            default:
                DPRINTF("kvm_arch_handle_exit\n");
                ret = kvm_arch_handle_exit(cpu, run);
                break;
            }
            break;
        default:
            DPRINTF("kvm_arch_handle_exit\n");
            ret = kvm_arch_handle_exit(cpu, run);
            break;
        }
    } while (ret == 0);

    cpu_exec_end(cpu);
    qemu_mutex_lock_iothread();

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

int kvm_has_robust_singlestep(void)
{
    return kvm_state->robust_singlestep;
}

int kvm_has_debugregs(void)
{
    return kvm_state->debugregs;
}

int kvm_max_nested_state_length(void)
{
    return kvm_state->max_nested_state_len;
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

bool kvm_arm_supports_user_irq(void)
{
    return kvm_check_extension(kvm_state, KVM_CAP_ARM_USER_IRQ);
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
    Error *error = NULL;
    uint32_t value;

    if (s->fd != -1) {
        error_setg(errp, "Cannot set properties after the accelerator has been initialized");
        return;
    }

    visit_type_uint32(v, name, &value, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
    if (value & (value - 1)) {
        error_setg(errp, "dirty-ring-size must be a power of two.");
        return;
    }

    s->kvm_dirty_ring_size = value;
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
}

static void kvm_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "KVM";
    ac->init_machine = kvm_init;
    ac->has_memory = kvm_accel_has_memory;
    ac->allowed = &kvm_allowed;

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
