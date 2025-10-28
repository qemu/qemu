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

#include "qemu/queue.h"
#include "exec/vaddr.h"
#include "qom/object.h"
#include "accel/accel-ops.h"

#ifdef __aarch64__
#include <Hypervisor/Hypervisor.h>
typedef hv_vcpu_t hvf_vcpuid;
#else
#include <Hypervisor/hv.h>
typedef hv_vcpuid_t hvf_vcpuid;
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
    AccelState parent_obj;

    hvf_slot slots[32];
    int num_slots;

    hvf_vcpu_caps *hvf_caps;
    uint64_t vtimer_offset;
    QTAILQ_HEAD(, hvf_sw_breakpoint) hvf_sw_breakpoints;
};
extern HVFState *hvf_state;

struct AccelCPUState {
    hvf_vcpuid fd;
#ifdef __aarch64__
    hv_vcpu_exit_t *exit;
    bool vtimer_masked;
    sigset_t unblock_ipi_mask;
    bool guest_debug_enabled;
#endif
};

void assert_hvf_ok_impl(hv_return_t ret, const char *file, unsigned int line,
                        const char *exp);
#define assert_hvf_ok(EX) assert_hvf_ok_impl((EX), __FILE__, __LINE__, #EX)
const char *hvf_return_string(hv_return_t ret);
int hvf_arch_init(void);
hv_return_t hvf_arch_vm_create(MachineState *ms, uint32_t pa_range);
hvf_slot *hvf_find_overlap_slot(uint64_t, uint64_t);
void hvf_kick_vcpu_thread(CPUState *cpu);

/* Must be called by the owning thread */
int hvf_arch_init_vcpu(CPUState *cpu);
/* Must be called by the owning thread */
void hvf_arch_vcpu_destroy(CPUState *cpu);
/* Must be called by the owning thread */
int hvf_arch_vcpu_exec(CPUState *);
/* Must be called by the owning thread */
int hvf_arch_put_registers(CPUState *);
/* Must be called by the owning thread */
int hvf_arch_get_registers(CPUState *);
/* Must be called by the owning thread */
void hvf_arch_update_guest_debug(CPUState *cpu);

struct hvf_sw_breakpoint {
    vaddr pc;
    vaddr saved_insn;
    int use_count;
    QTAILQ_ENTRY(hvf_sw_breakpoint) entry;
};

struct hvf_sw_breakpoint *hvf_find_sw_breakpoint(CPUState *cpu,
                                                 vaddr pc);
int hvf_sw_breakpoints_active(CPUState *cpu);

int hvf_arch_insert_sw_breakpoint(CPUState *cpu, struct hvf_sw_breakpoint *bp);
int hvf_arch_remove_sw_breakpoint(CPUState *cpu, struct hvf_sw_breakpoint *bp);
int hvf_arch_insert_hw_breakpoint(vaddr addr, vaddr len, int type);
int hvf_arch_remove_hw_breakpoint(vaddr addr, vaddr len, int type);
void hvf_arch_remove_all_hw_breakpoints(void);

/*
 * hvf_update_guest_debug:
 * @cs: CPUState for the CPU to update
 *
 * Update guest to enable or disable debugging. Per-arch specifics will be
 * handled by calling down to hvf_arch_update_guest_debug.
 */
int hvf_update_guest_debug(CPUState *cpu);

/*
 * Return whether the guest supports debugging.
 */
bool hvf_arch_supports_guest_debug(void);

#endif
