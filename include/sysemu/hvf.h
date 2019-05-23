/*
 * QEMU Hypervisor.framework (HVF) support
 *
 * Copyright Google Inc., 2017
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in non-HVF-specific code */

#ifndef HVF_H
#define HVF_H

#include "qemu/bitops.h"
#include "exec/memory.h"
#include "sysemu/accel.h"

extern bool hvf_allowed;
#ifdef CONFIG_HVF
#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>
#include <Hypervisor/hv_error.h>
#include "target/i386/cpu.h"
#include "hw/hw.h"
uint32_t hvf_get_supported_cpuid(uint32_t func, uint32_t idx,
                                 int reg);
#define hvf_enabled() (hvf_allowed)
#else
#define hvf_enabled() 0
#define hvf_get_supported_cpuid(func, idx, reg) 0
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

typedef struct HVFState {
    AccelState parent;
    hvf_slot slots[32];
    int num_slots;

    hvf_vcpu_caps *hvf_caps;
} HVFState;
extern HVFState *hvf_state;

void hvf_set_phys_mem(MemoryRegionSection *, bool);
void hvf_handle_io(CPUArchState *, uint16_t, void *,
                  int, int, int);
hvf_slot *hvf_find_overlap_slot(uint64_t, uint64_t);

/* Disable HVF if |disable| is 1, otherwise, enable it iff it is supported by
 * the host CPU. Use hvf_enabled() after this to get the result. */
void hvf_disable(int disable);

/* Returns non-0 if the host CPU supports the VMX "unrestricted guest" feature
 * which allows the virtual CPU to directly run in "real mode". If true, this
 * allows QEMU to run several vCPU threads in parallel (see cpus.c). Otherwise,
 * only a a single TCG thread can run, and it will call HVF to run the current
 * instructions, except in case of "real mode" (paging disabled, typically at
 * boot time), or MMIO operations. */

int hvf_sync_vcpus(void);

int hvf_init_vcpu(CPUState *);
int hvf_vcpu_exec(CPUState *);
int hvf_smp_cpu_exec(CPUState *);
void hvf_cpu_synchronize_state(CPUState *);
void hvf_cpu_synchronize_post_reset(CPUState *);
void hvf_cpu_synchronize_post_init(CPUState *);
void _hvf_cpu_synchronize_post_init(CPUState *, run_on_cpu_data);

void hvf_vcpu_destroy(CPUState *);
void hvf_raise_event(CPUState *);
/* void hvf_reset_vcpu_state(void *opaque); */
void hvf_reset_vcpu(CPUState *);
void vmx_update_tpr(CPUState *);
void update_apic_tpr(CPUState *);
int hvf_put_registers(CPUState *);
void vmx_clear_int_window_exiting(CPUState *cpu);

#define TYPE_HVF_ACCEL ACCEL_CLASS_NAME("hvf")

#define HVF_STATE(obj) \
    OBJECT_CHECK(HVFState, (obj), TYPE_HVF_ACCEL)

#endif
