/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou  <ziqiaozhou@microsoft.com>
 *          Magnus Kulke <magnuskulke@microsoft.com>
 *          Jinank Jain  <jinankjain@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef QEMU_MSHV_INT_H
#define QEMU_MSHV_INT_H

#define MSHV_MSR_ENTRIES_COUNT 64

#define MSHV_MAX_MEM_SLOTS 32

typedef struct hyperv_message hv_message;

typedef struct MshvHvCallArgs {
    void *base;
    void *input_page;
    void *output_page;
} MshvHvCallArgs;

struct AccelCPUState {
    int cpufd;
    bool dirty;
    MshvHvCallArgs hvcall_args;
};

typedef struct MshvMemoryListener {
    MemoryListener listener;
    int as_id;
} MshvMemoryListener;

typedef struct MshvAddressSpace {
    MshvMemoryListener *ml;
    AddressSpace *as;
} MshvAddressSpace;

typedef struct MshvMemorySlotManager {
    size_t n_slots;
    GList *slots;
    QemuMutex mutex;
} MshvMemorySlotManager;

struct MshvState {
    AccelState parent_obj;
    int vm;
    MshvMemoryListener memory_listener;
    /* number of listeners */
    int nr_as;
    MshvAddressSpace *as;
    int fd;
    MshvMemorySlotManager msm;
};

typedef struct MshvMsiControl {
    bool updated;
    GHashTable *gsi_routes;
} MshvMsiControl;

#define mshv_vcpufd(cpu) (cpu->accel->cpufd)

/* cpu */
typedef struct MshvFPU {
    uint8_t fpr[8][16];
    uint16_t fcw;
    uint16_t fsw;
    uint8_t ftwx;
    uint8_t pad1;
    uint16_t last_opcode;
    uint64_t last_ip;
    uint64_t last_dp;
    uint8_t xmm[16][16];
    uint32_t mxcsr;
    uint32_t pad2;
} MshvFPU;

typedef enum MshvVmExit {
    MshvVmExitIgnore   = 0,
    MshvVmExitShutdown = 1,
    MshvVmExitSpecial  = 2,
} MshvVmExit;

typedef enum MshvRemapResult {
    MshvRemapOk = 0,
    MshvRemapNoMapping = 1,
    MshvRemapNoOverlap = 2,
} MshvRemapResult;

void mshv_init_mmio_emu(void);
int mshv_create_vcpu(int vm_fd, uint8_t vp_index, int *cpu_fd);
void mshv_remove_vcpu(int vm_fd, int cpu_fd);
int mshv_configure_vcpu(const CPUState *cpu, const MshvFPU *fpu, uint64_t xcr0);
int mshv_get_standard_regs(CPUState *cpu);
int mshv_get_special_regs(CPUState *cpu);
int mshv_run_vcpu(int vm_fd, CPUState *cpu, hv_message *msg, MshvVmExit *exit);
int mshv_load_regs(CPUState *cpu);
int mshv_store_regs(CPUState *cpu);
int mshv_set_generic_regs(const CPUState *cpu, const hv_register_assoc *assocs,
                          size_t n_regs);
int mshv_arch_put_registers(const CPUState *cpu);
void mshv_arch_init_vcpu(CPUState *cpu);
void mshv_arch_destroy_vcpu(CPUState *cpu);
void mshv_arch_amend_proc_features(
    union hv_partition_synthetic_processor_features *features);
int mshv_arch_post_init_vm(int vm_fd);

#if defined COMPILING_PER_TARGET && defined CONFIG_MSHV_IS_POSSIBLE
int mshv_hvcall(int fd, const struct mshv_root_hvcall *args);
#endif

/* memory */
typedef struct MshvMemorySlot {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    bool readonly;
    bool mapped;
} MshvMemorySlot;

MshvRemapResult mshv_remap_overlap_region(int vm_fd, uint64_t gpa);
int mshv_guest_mem_read(uint64_t gpa, uint8_t *data, uintptr_t size,
                        bool is_secure_mode, bool instruction_fetch);
int mshv_guest_mem_write(uint64_t gpa, const uint8_t *data, uintptr_t size,
                         bool is_secure_mode);
void mshv_set_phys_mem(MshvMemoryListener *mml, MemoryRegionSection *section,
                       bool add);
void mshv_init_memory_slot_manager(MshvState *mshv_state);

/* msr */
typedef struct MshvMsrEntry {
  uint32_t index;
  uint32_t reserved;
  uint64_t data;
} MshvMsrEntry;

typedef struct MshvMsrEntries {
    MshvMsrEntry entries[MSHV_MSR_ENTRIES_COUNT];
    uint32_t nmsrs;
} MshvMsrEntries;

int mshv_configure_msr(const CPUState *cpu, const MshvMsrEntry *msrs,
                       size_t n_msrs);

/* interrupt */
void mshv_init_msicontrol(void);
int mshv_reserve_ioapic_msi_routes(int vm_fd);

#endif
