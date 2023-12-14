/*
 * QEMU Hypervisor.framework (HVF) support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in HVF-specific code */

#ifndef HVF_INT_H
#define HVF_INT_H

#ifdef __aarch64__
#include <Hypervisor/Hypervisor.h>
#else
#include <Hypervisor/hv.h>
#endif

/* hvf_slot flags */
#define HVF_SLOT_LOG (1 << 0)

typedef struct hvf_slot {
    uint64_t start;
    uint64_t size;
    uint8_t *mem;
    int slot_id;
    uint32_t flags;
    MemoryRegion *region;
} hvf_slot;

typedef struct hvf_vcpu_caps {
    uint64_t vmx_cap_pinbased;
    uint64_t vmx_cap_procbased;
    uint64_t vmx_cap_procbased2;
    uint64_t vmx_cap_entry;
    uint64_t vmx_cap_exit;
    uint64_t vmx_cap_preemption_timer;
} hvf_vcpu_caps;

struct HVFState {
    AccelState parent;
    hvf_slot slots[32];
    int num_slots;

    hvf_vcpu_caps *hvf_caps;
    uint64_t vtimer_offset;
    QTAILQ_HEAD(, hvf_sw_breakpoint) hvf_sw_breakpoints;
};
extern HVFState *hvf_state;

struct AccelCPUState {
    uint64_t fd;
    void *exit;
    bool vtimer_masked;
    sigset_t unblock_ipi_mask;
    bool guest_debug_enabled;
};

void assert_hvf_ok(hv_return_t ret);
int hvf_arch_init(void);
int hvf_arch_init_vcpu(CPUState *cpu);
void hvf_arch_vcpu_destroy(CPUState *cpu);
int hvf_vcpu_exec(CPUState *);
hvf_slot *hvf_find_overlap_slot(uint64_t, uint64_t);
int hvf_put_registers(CPUState *);
int hvf_get_registers(CPUState *);
void hvf_kick_vcpu_thread(CPUState *cpu);

#endif
