/*
 * QEMU ARM CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */
#ifndef QEMU_ARM_CPU_QOM_H
#define QEMU_ARM_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_ARM_CPU "arm-cpu"

#define ARM_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(ARMCPUClass, (klass), TYPE_ARM_CPU)
#define ARM_CPU(obj) \
    OBJECT_CHECK(ARMCPU, (obj), TYPE_ARM_CPU)
#define ARM_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ARMCPUClass, (obj), TYPE_ARM_CPU)

/**
 * ARMCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * An ARM CPU model.
 */
typedef struct ARMCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} ARMCPUClass;

/**
 * ARMCPU:
 * @env: #CPUARMState
 *
 * An ARM CPU core.
 */
typedef struct ARMCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUARMState env;

    /* Coprocessor information */
    GHashTable *cp_regs;
    /* For marshalling (mostly coprocessor) register state between the
     * kernel and QEMU (for KVM) and between two QEMUs (for migration),
     * we use these arrays.
     */
    /* List of register indexes managed via these arrays; (full KVM style
     * 64 bit indexes, not CPRegInfo 32 bit indexes)
     */
    uint64_t *cpreg_indexes;
    /* Values of the registers (cpreg_indexes[i]'s value is cpreg_values[i]) */
    uint64_t *cpreg_values;
    /* Length of the indexes, values, reset_values arrays */
    int32_t cpreg_array_len;
    /* These are used only for migration: incoming data arrives in
     * these fields and is sanity checked in post_load before copying
     * to the working data structures above.
     */
    uint64_t *cpreg_vmstate_indexes;
    uint64_t *cpreg_vmstate_values;
    int32_t cpreg_vmstate_array_len;

    /* Timers used by the generic (architected) timer */
    QEMUTimer *gt_timer[NUM_GTIMERS];
    /* GPIO outputs for generic timer */
    qemu_irq gt_timer_outputs[NUM_GTIMERS];

    /* MemoryRegion to use for secure physical accesses */
    MemoryRegion *secure_memory;

    /* 'compatible' string for this CPU for Linux device trees */
    const char *dtb_compatible;

    /* PSCI version for this CPU
     * Bits[31:16] = Major Version
     * Bits[15:0] = Minor Version
     */
    uint32_t psci_version;

    /* Should CPU start in PSCI powered-off state? */
    bool start_powered_off;
    /* CPU currently in PSCI powered-off state */
    bool powered_off;
    /* CPU has security extension */
    bool has_el3;

    /* CPU has memory protection unit */
    bool has_mpu;
    /* PMSAv7 MPU number of supported regions */
    uint32_t pmsav7_dregion;

    /* PSCI conduit used to invoke PSCI methods
     * 0 - disabled, 1 - smc, 2 - hvc
     */
    uint32_t psci_conduit;

    /* [QEMU_]KVM_ARM_TARGET_* constant for this CPU, or
     * QEMU_KVM_ARM_TARGET_NONE if the kernel doesn't support this CPU type.
     */
    uint32_t kvm_target;

    /* KVM init features for this CPU */
    uint32_t kvm_init_features[7];

    /* Uniprocessor system with MP extensions */
    bool mp_is_up;

    /* The instance init functions for implementation-specific subclasses
     * set these fields to specify the implementation-dependent values of
     * various constant registers and reset values of non-constant
     * registers.
     * Some of these might become QOM properties eventually.
     * Field names match the official register names as defined in the
     * ARMv7AR ARM Architecture Reference Manual. A reset_ prefix
     * is used for reset values of non-constant registers; no reset_
     * prefix means a constant register.
     */
    uint32_t midr;
    uint32_t revidr;
    uint32_t reset_fpsid;
    uint32_t mvfr0;
    uint32_t mvfr1;
    uint32_t mvfr2;
    uint32_t ctr;
    uint32_t reset_sctlr;
    uint32_t id_pfr0;
    uint32_t id_pfr1;
    uint32_t id_dfr0;
    uint32_t pmceid0;
    uint32_t pmceid1;
    uint32_t id_afr0;
    uint32_t id_mmfr0;
    uint32_t id_mmfr1;
    uint32_t id_mmfr2;
    uint32_t id_mmfr3;
    uint32_t id_mmfr4;
    uint32_t id_isar0;
    uint32_t id_isar1;
    uint32_t id_isar2;
    uint32_t id_isar3;
    uint32_t id_isar4;
    uint32_t id_isar5;
    uint64_t id_aa64pfr0;
    uint64_t id_aa64pfr1;
    uint64_t id_aa64dfr0;
    uint64_t id_aa64dfr1;
    uint64_t id_aa64afr0;
    uint64_t id_aa64afr1;
    uint64_t id_aa64isar0;
    uint64_t id_aa64isar1;
    uint64_t id_aa64mmfr0;
    uint64_t id_aa64mmfr1;
    uint32_t dbgdidr;
    uint32_t clidr;
    uint64_t mp_affinity; /* MP ID without feature bits */
    /* The elements of this array are the CCSIDR values for each cache,
     * in the order L1DCache, L1ICache, L2DCache, L2ICache, etc.
     */
    uint32_t ccsidr[16];
    uint64_t reset_cbar;
    uint32_t reset_auxcr;
    bool reset_hivecs;
    /* DCZ blocksize, in log_2(words), ie low 4 bits of DCZID_EL0 */
    uint32_t dcz_blocksize;
    uint64_t rvbar;
} ARMCPU;

#define TYPE_AARCH64_CPU "aarch64-cpu"
#define AARCH64_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(AArch64CPUClass, (klass), TYPE_AARCH64_CPU)
#define AARCH64_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(AArch64CPUClass, (obj), TYPE_AArch64_CPU)

typedef struct AArch64CPUClass {
    /*< private >*/
    ARMCPUClass parent_class;
    /*< public >*/
} AArch64CPUClass;

static inline ARMCPU *arm_env_get_cpu(CPUARMState *env)
{
    return container_of(env, ARMCPU, env);
}

#define ENV_GET_CPU(e) CPU(arm_env_get_cpu(e))

#define ENV_OFFSET offsetof(ARMCPU, env)

#ifndef CONFIG_USER_ONLY
extern const struct VMStateDescription vmstate_arm_cpu;
#endif

void register_cp_regs_for_features(ARMCPU *cpu);
void init_cpreg_list(ARMCPU *cpu);

void arm_cpu_do_interrupt(CPUState *cpu);
void arm_v7m_cpu_do_interrupt(CPUState *cpu);
bool arm_cpu_exec_interrupt(CPUState *cpu, int int_req);

void arm_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                        int flags);

hwaddr arm_cpu_get_phys_page_attrs_debug(CPUState *cpu, vaddr addr,
                                         MemTxAttrs *attrs);

int arm_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int arm_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

int arm_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                             int cpuid, void *opaque);
int arm_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                             int cpuid, void *opaque);

/* Callback functions for the generic timer's timers. */
void arm_gt_ptimer_cb(void *opaque);
void arm_gt_vtimer_cb(void *opaque);
void arm_gt_htimer_cb(void *opaque);
void arm_gt_stimer_cb(void *opaque);

#define ARM_AFF0_SHIFT 0
#define ARM_AFF0_MASK  (0xFFULL << ARM_AFF0_SHIFT)
#define ARM_AFF1_SHIFT 8
#define ARM_AFF1_MASK  (0xFFULL << ARM_AFF1_SHIFT)
#define ARM_AFF2_SHIFT 16
#define ARM_AFF2_MASK  (0xFFULL << ARM_AFF2_SHIFT)
#define ARM_AFF3_SHIFT 32
#define ARM_AFF3_MASK  (0xFFULL << ARM_AFF3_SHIFT)

#define ARM32_AFFINITY_MASK (ARM_AFF0_MASK|ARM_AFF1_MASK|ARM_AFF2_MASK)
#define ARM64_AFFINITY_MASK \
    (ARM_AFF0_MASK|ARM_AFF1_MASK|ARM_AFF2_MASK|ARM_AFF3_MASK)

#ifdef TARGET_AARCH64
int aarch64_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int aarch64_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
#endif

#endif
