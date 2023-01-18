/*
 * Internal definitions for a target's KVM support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_KVM_INT_H
#define QEMU_KVM_INT_H

#include "exec/memory.h"
#include "qapi/qapi-types-common.h"
#include "qemu/accel.h"
#include "qemu/queue.h"
#include "sysemu/kvm.h"

typedef struct KVMSlot
{
    hwaddr start_addr;
    ram_addr_t memory_size;
    void *ram;
    int slot;
    int flags;
    int old_flags;
    /* Dirty bitmap cache for the slot */
    unsigned long *dirty_bmap;
    unsigned long dirty_bmap_size;
    /* Cache of the address space ID */
    int as_id;
    /* Cache of the offset in ram address space */
    ram_addr_t ram_start_offset;
} KVMSlot;

typedef struct KVMMemoryUpdate {
    QSIMPLEQ_ENTRY(KVMMemoryUpdate) next;
    MemoryRegionSection section;
} KVMMemoryUpdate;

typedef struct KVMMemoryListener {
    MemoryListener listener;
    KVMSlot *slots;
    int as_id;
    QSIMPLEQ_HEAD(, KVMMemoryUpdate) transaction_add;
    QSIMPLEQ_HEAD(, KVMMemoryUpdate) transaction_del;
} KVMMemoryListener;

#define KVM_MSI_HASHTAB_SIZE    256

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
    NotifyVmexitOption notify_vmexit;
    uint32_t notify_window;
    uint32_t xen_version;
    uint32_t xen_caps;
    uint16_t xen_gnttab_max_frames;
    uint16_t xen_evtchn_max_pirq;
};

void kvm_memory_listener_register(KVMState *s, KVMMemoryListener *kml,
                                  AddressSpace *as, int as_id, const char *name);

void kvm_set_max_memslot_size(hwaddr max_slot_size);

/**
 * kvm_hwpoison_page_add:
 *
 * Parameters:
 *  @ram_addr: the address in the RAM for the poisoned page
 *
 * Add a poisoned page to the list
 *
 * Return: None.
 */
void kvm_hwpoison_page_add(ram_addr_t ram_addr);
#endif
