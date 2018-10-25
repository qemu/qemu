/*
 * QEMU RISC-V CPU
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_CPU_H
#define RISCV_CPU_H

/* QEMU addressing/paging config */
#define TARGET_PAGE_BITS 12 /* 4 KiB Pages */
#if defined(TARGET_RISCV64)
#define TARGET_LONG_BITS 64
#define TARGET_PHYS_ADDR_SPACE_BITS 56 /* 44-bit PPN */
#define TARGET_VIRT_ADDR_SPACE_BITS 48 /* sv48 */
#elif defined(TARGET_RISCV32)
#define TARGET_LONG_BITS 32
#define TARGET_PHYS_ADDR_SPACE_BITS 34 /* 22-bit PPN */
#define TARGET_VIRT_ADDR_SPACE_BITS 32 /* sv32 */
#endif

#define TCG_GUEST_DEFAULT_MO 0

#define CPUArchState struct CPURISCVState

#include "qemu-common.h"
#include "qom/cpu.h"
#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"

#define TYPE_RISCV_CPU "riscv-cpu"

#define RISCV_CPU_TYPE_SUFFIX "-" TYPE_RISCV_CPU
#define RISCV_CPU_TYPE_NAME(name) (name RISCV_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_RISCV_CPU

#define TYPE_RISCV_CPU_ANY              RISCV_CPU_TYPE_NAME("any")
#define TYPE_RISCV_CPU_RV32GCSU_V1_09_1 RISCV_CPU_TYPE_NAME("rv32gcsu-v1.9.1")
#define TYPE_RISCV_CPU_RV32GCSU_V1_10_0 RISCV_CPU_TYPE_NAME("rv32gcsu-v1.10.0")
#define TYPE_RISCV_CPU_RV32IMACU_NOMMU  RISCV_CPU_TYPE_NAME("rv32imacu-nommu")
#define TYPE_RISCV_CPU_RV64GCSU_V1_09_1 RISCV_CPU_TYPE_NAME("rv64gcsu-v1.9.1")
#define TYPE_RISCV_CPU_RV64GCSU_V1_10_0 RISCV_CPU_TYPE_NAME("rv64gcsu-v1.10.0")
#define TYPE_RISCV_CPU_RV64IMACU_NOMMU  RISCV_CPU_TYPE_NAME("rv64imacu-nommu")
#define TYPE_RISCV_CPU_SIFIVE_E31       RISCV_CPU_TYPE_NAME("sifive-e31")
#define TYPE_RISCV_CPU_SIFIVE_E51       RISCV_CPU_TYPE_NAME("sifive-e51")
#define TYPE_RISCV_CPU_SIFIVE_U34       RISCV_CPU_TYPE_NAME("sifive-u34")
#define TYPE_RISCV_CPU_SIFIVE_U54       RISCV_CPU_TYPE_NAME("sifive-u54")

#define RV32 ((target_ulong)1 << (TARGET_LONG_BITS - 2))
#define RV64 ((target_ulong)2 << (TARGET_LONG_BITS - 2))

#if defined(TARGET_RISCV32)
#define RVXLEN RV32
#elif defined(TARGET_RISCV64)
#define RVXLEN RV64
#endif

#define RV(x) ((target_ulong)1 << (x - 'A'))

#define RVI RV('I')
#define RVE RV('E') /* E and I are mutually exclusive */
#define RVM RV('M')
#define RVA RV('A')
#define RVF RV('F')
#define RVD RV('D')
#define RVC RV('C')
#define RVS RV('S')
#define RVU RV('U')

/* S extension denotes that Supervisor mode exists, however it is possible
   to have a core that support S mode but does not have an MMU and there
   is currently no bit in misa to indicate whether an MMU exists or not
   so a cpu features bitfield is required */
enum {
    RISCV_FEATURE_MMU
};

#define USER_VERSION_2_02_0 0x00020200
#define PRIV_VERSION_1_09_1 0x00010901
#define PRIV_VERSION_1_10_0 0x00011000

#define TRANSLATE_FAIL 1
#define TRANSLATE_SUCCESS 0
#define NB_MMU_MODES 4
#define MMU_USER_IDX 3

#define MAX_RISCV_PMPS (16)

typedef struct CPURISCVState CPURISCVState;

#include "pmp.h"

struct CPURISCVState {
    target_ulong gpr[32];
    uint64_t fpr[32]; /* assume both F and D extensions */
    target_ulong pc;
    target_ulong load_res;
    target_ulong load_val;

    target_ulong frm;

    target_ulong badaddr;

    target_ulong user_ver;
    target_ulong priv_ver;
    target_ulong misa;

    uint32_t features;

#ifndef CONFIG_USER_ONLY
    target_ulong priv;
    target_ulong resetvec;

    target_ulong mhartid;
    target_ulong mstatus;

    /*
     * CAUTION! Unlike the rest of this struct, mip is accessed asynchonously
     * by I/O threads. It should be read with atomic_read. It should be updated
     * using riscv_cpu_update_mip with the iothread mutex held. The iothread
     * mutex must be held because mip must be consistent with the CPU inturrept
     * state. riscv_cpu_update_mip calls cpu_interrupt or cpu_reset_interrupt
     * wuth the invariant that CPU_INTERRUPT_HARD is set iff mip is non-zero.
     * mip is 32-bits to allow atomic_read on 32-bit hosts.
     */
    uint32_t mip;

    target_ulong mie;
    target_ulong mideleg;

    target_ulong sptbr;  /* until: priv-1.9.1 */
    target_ulong satp;   /* since: priv-1.10.0 */
    target_ulong sbadaddr;
    target_ulong mbadaddr;
    target_ulong medeleg;

    target_ulong stvec;
    target_ulong sepc;
    target_ulong scause;

    target_ulong mtvec;
    target_ulong mepc;
    target_ulong mcause;
    target_ulong mtval;  /* since: priv-1.10.0 */

    target_ulong scounteren;
    target_ulong mcounteren;

    target_ulong sscratch;
    target_ulong mscratch;

    /* temporary htif regs */
    uint64_t mfromhost;
    uint64_t mtohost;
    uint64_t timecmp;

    /* physical memory protection */
    pmp_table_t pmp_state;
#endif

    float_status fp_status;

    /* QEMU */
    CPU_COMMON

    /* Fields from here on are preserved across CPU reset. */
    QEMUTimer *timer; /* Internal timer */
};

#define RISCV_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(RISCVCPUClass, (klass), TYPE_RISCV_CPU)
#define RISCV_CPU(obj) \
    OBJECT_CHECK(RISCVCPU, (obj), TYPE_RISCV_CPU)
#define RISCV_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RISCVCPUClass, (obj), TYPE_RISCV_CPU)

/**
 * RISCVCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A RISCV CPU model.
 */
typedef struct RISCVCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} RISCVCPUClass;

/**
 * RISCVCPU:
 * @env: #CPURISCVState
 *
 * A RISCV CPU.
 */
typedef struct RISCVCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/
    CPURISCVState env;
} RISCVCPU;

static inline RISCVCPU *riscv_env_get_cpu(CPURISCVState *env)
{
    return container_of(env, RISCVCPU, env);
}

static inline int riscv_has_ext(CPURISCVState *env, target_ulong ext)
{
    return (env->misa & ext) != 0;
}

static inline bool riscv_feature(CPURISCVState *env, int feature)
{
    return env->features & (1ULL << feature);
}

#include "cpu_user.h"
#include "cpu_bits.h"

extern const char * const riscv_int_regnames[];
extern const char * const riscv_fpr_regnames[];
extern const char * const riscv_excp_names[];
extern const char * const riscv_intr_names[];

#define ENV_GET_CPU(e) CPU(riscv_env_get_cpu(e))
#define ENV_OFFSET offsetof(RISCVCPU, env)

void riscv_cpu_do_interrupt(CPUState *cpu);
int riscv_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int riscv_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
int riscv_cpu_mmu_index(CPURISCVState *env, bool ifetch);
hwaddr riscv_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
void  riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                    MMUAccessType access_type, int mmu_idx,
                                    uintptr_t retaddr);
int riscv_cpu_handle_mmu_fault(CPUState *cpu, vaddr address, int size,
                              int rw, int mmu_idx);
char *riscv_isa_string(RISCVCPU *cpu);
void riscv_cpu_list(FILE *f, fprintf_function cpu_fprintf);

#define cpu_signal_handler cpu_riscv_signal_handler
#define cpu_list riscv_cpu_list
#define cpu_mmu_index riscv_cpu_mmu_index

#ifndef CONFIG_USER_ONLY
uint32_t riscv_cpu_update_mip(RISCVCPU *cpu, uint32_t mask, uint32_t value);
#define BOOL_TO_MASK(x) (-!!(x)) /* helper for riscv_cpu_update_mip value */
#endif
void riscv_set_mode(CPURISCVState *env, target_ulong newpriv);

void riscv_translate_init(void);
RISCVCPU *cpu_riscv_init(const char *cpu_model);
int cpu_riscv_signal_handler(int host_signum, void *pinfo, void *puc);
void QEMU_NORETURN do_raise_exception_err(CPURISCVState *env,
                                          uint32_t exception, uintptr_t pc);

target_ulong cpu_riscv_get_fflags(CPURISCVState *env);
void cpu_riscv_set_fflags(CPURISCVState *env, target_ulong);

#define TB_FLAGS_MMU_MASK  3
#define TB_FLAGS_FP_ENABLE MSTATUS_FS

static inline void cpu_get_tb_cpu_state(CPURISCVState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->pc;
    *cs_base = 0;
#ifdef CONFIG_USER_ONLY
    *flags = TB_FLAGS_FP_ENABLE;
#else
    *flags = cpu_mmu_index(env, 0) | (env->mstatus & MSTATUS_FS);
#endif
}

void csr_write_helper(CPURISCVState *env, target_ulong val_to_write,
        target_ulong csrno);
target_ulong csr_read_helper(CPURISCVState *env, target_ulong csrno);

#include "exec/cpu-all.h"

#endif /* RISCV_CPU_H */
