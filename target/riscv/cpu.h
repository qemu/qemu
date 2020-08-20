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

#include "hw/core/cpu.h"
#include "hw/registerfields.h"
#include "exec/cpu-defs.h"
#include "fpu/softfloat-types.h"

#define TCG_GUEST_DEFAULT_MO 0

#define TYPE_RISCV_CPU "riscv-cpu"

#define RISCV_CPU_TYPE_SUFFIX "-" TYPE_RISCV_CPU
#define RISCV_CPU_TYPE_NAME(name) (name RISCV_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_RISCV_CPU

#define TYPE_RISCV_CPU_ANY              RISCV_CPU_TYPE_NAME("any")
#define TYPE_RISCV_CPU_BASE32           RISCV_CPU_TYPE_NAME("rv32")
#define TYPE_RISCV_CPU_BASE64           RISCV_CPU_TYPE_NAME("rv64")
#define TYPE_RISCV_CPU_IBEX             RISCV_CPU_TYPE_NAME("lowrisc-ibex")
#define TYPE_RISCV_CPU_SIFIVE_E31       RISCV_CPU_TYPE_NAME("sifive-e31")
#define TYPE_RISCV_CPU_SIFIVE_E34       RISCV_CPU_TYPE_NAME("sifive-e34")
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
#define RVV RV('V')
#define RVC RV('C')
#define RVS RV('S')
#define RVU RV('U')
#define RVH RV('H')

/* S extension denotes that Supervisor mode exists, however it is possible
   to have a core that support S mode but does not have an MMU and there
   is currently no bit in misa to indicate whether an MMU exists or not
   so a cpu features bitfield is required, likewise for optional PMP support */
enum {
    RISCV_FEATURE_MMU,
    RISCV_FEATURE_PMP,
    RISCV_FEATURE_MISA
};

#define PRIV_VERSION_1_10_0 0x00011000
#define PRIV_VERSION_1_11_0 0x00011100

#define VEXT_VERSION_0_07_1 0x00000701

#define TRANSLATE_PMP_FAIL 2
#define TRANSLATE_FAIL 1
#define TRANSLATE_SUCCESS 0
#define MMU_USER_IDX 3

#define MAX_RISCV_PMPS (16)

typedef struct CPURISCVState CPURISCVState;

#include "pmp.h"

#define RV_VLEN_MAX 256

FIELD(VTYPE, VLMUL, 0, 2)
FIELD(VTYPE, VSEW, 2, 3)
FIELD(VTYPE, VEDIV, 5, 2)
FIELD(VTYPE, RESERVED, 7, sizeof(target_ulong) * 8 - 9)
FIELD(VTYPE, VILL, sizeof(target_ulong) * 8 - 1, 1)

struct CPURISCVState {
    target_ulong gpr[32];
    uint64_t fpr[32]; /* assume both F and D extensions */

    /* vector coprocessor state. */
    uint64_t vreg[32 * RV_VLEN_MAX / 64] QEMU_ALIGNED(16);
    target_ulong vxrm;
    target_ulong vxsat;
    target_ulong vl;
    target_ulong vstart;
    target_ulong vtype;

    target_ulong pc;
    target_ulong load_res;
    target_ulong load_val;

    target_ulong frm;

    target_ulong badaddr;
    target_ulong guest_phys_fault_addr;

    target_ulong priv_ver;
    target_ulong vext_ver;
    target_ulong misa;
    target_ulong misa_mask;

    uint32_t features;

#ifdef CONFIG_USER_ONLY
    uint32_t elf_flags;
#endif

#ifndef CONFIG_USER_ONLY
    target_ulong priv;
    /* This contains QEMU specific information about the virt state. */
    target_ulong virt;
    target_ulong resetvec;

    target_ulong mhartid;
    target_ulong mstatus;

    target_ulong mip;

#ifdef TARGET_RISCV32
    target_ulong mstatush;
#endif

    uint32_t miclaim;

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

    /* Hypervisor CSRs */
    target_ulong hstatus;
    target_ulong hedeleg;
    target_ulong hideleg;
    target_ulong hcounteren;
    target_ulong htval;
    target_ulong htinst;
    target_ulong hgatp;
    uint64_t htimedelta;

    /* Virtual CSRs */
    target_ulong vsstatus;
    target_ulong vstvec;
    target_ulong vsscratch;
    target_ulong vsepc;
    target_ulong vscause;
    target_ulong vstval;
    target_ulong vsatp;
#ifdef TARGET_RISCV32
    target_ulong vsstatush;
#endif

    target_ulong mtval2;
    target_ulong mtinst;

    /* HS Backup CSRs */
    target_ulong stvec_hs;
    target_ulong sscratch_hs;
    target_ulong sepc_hs;
    target_ulong scause_hs;
    target_ulong stval_hs;
    target_ulong satp_hs;
    target_ulong mstatus_hs;
#ifdef TARGET_RISCV32
    target_ulong mstatush_hs;
#endif

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

    /* machine specific rdtime callback */
    uint64_t (*rdtime_fn)(void);

    /* True if in debugger mode.  */
    bool debugger;
#endif

    float_status fp_status;

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
    DeviceReset parent_reset;
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
    CPUNegativeOffsetState neg;
    CPURISCVState env;

    /* Configuration Settings */
    struct {
        bool ext_i;
        bool ext_e;
        bool ext_g;
        bool ext_m;
        bool ext_a;
        bool ext_f;
        bool ext_d;
        bool ext_c;
        bool ext_s;
        bool ext_u;
        bool ext_h;
        bool ext_v;
        bool ext_counters;
        bool ext_ifencei;
        bool ext_icsr;

        char *priv_spec;
        char *user_spec;
        char *vext_spec;
        uint16_t vlen;
        uint16_t elen;
        bool mmu;
        bool pmp;
    } cfg;
} RISCVCPU;

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

void riscv_cpu_do_interrupt(CPUState *cpu);
int riscv_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int riscv_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
bool riscv_cpu_fp_enabled(CPURISCVState *env);
bool riscv_cpu_virt_enabled(CPURISCVState *env);
void riscv_cpu_set_virt_enabled(CPURISCVState *env, bool enable);
bool riscv_cpu_force_hs_excep_enabled(CPURISCVState *env);
void riscv_cpu_set_force_hs_excep(CPURISCVState *env, bool enable);
int riscv_cpu_mmu_index(CPURISCVState *env, bool ifetch);
hwaddr riscv_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
void  riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                    MMUAccessType access_type, int mmu_idx,
                                    uintptr_t retaddr);
bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
void riscv_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr);
char *riscv_isa_string(RISCVCPU *cpu);
void riscv_cpu_list(void);

#define cpu_signal_handler riscv_cpu_signal_handler
#define cpu_list riscv_cpu_list
#define cpu_mmu_index riscv_cpu_mmu_index

#ifndef CONFIG_USER_ONLY
void riscv_cpu_swap_hypervisor_regs(CPURISCVState *env);
int riscv_cpu_claim_interrupts(RISCVCPU *cpu, uint32_t interrupts);
uint32_t riscv_cpu_update_mip(RISCVCPU *cpu, uint32_t mask, uint32_t value);
#define BOOL_TO_MASK(x) (-!!(x)) /* helper for riscv_cpu_update_mip value */
void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(void));
#endif
void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv);

void riscv_translate_init(void);
int riscv_cpu_signal_handler(int host_signum, void *pinfo, void *puc);
void QEMU_NORETURN riscv_raise_exception(CPURISCVState *env,
                                         uint32_t exception, uintptr_t pc);

target_ulong riscv_cpu_get_fflags(CPURISCVState *env);
void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong);

#define TB_FLAGS_MMU_MASK   3
#define TB_FLAGS_MSTATUS_FS MSTATUS_FS

typedef CPURISCVState CPUArchState;
typedef RISCVCPU ArchCPU;
#include "exec/cpu-all.h"

FIELD(TB_FLAGS, VL_EQ_VLMAX, 2, 1)
FIELD(TB_FLAGS, LMUL, 3, 2)
FIELD(TB_FLAGS, SEW, 5, 3)
FIELD(TB_FLAGS, VILL, 8, 1)

/*
 * A simplification for VLMAX
 * = (1 << LMUL) * VLEN / (8 * (1 << SEW))
 * = (VLEN << LMUL) / (8 << SEW)
 * = (VLEN << LMUL) >> (SEW + 3)
 * = VLEN >> (SEW + 3 - LMUL)
 */
static inline uint32_t vext_get_vlmax(RISCVCPU *cpu, target_ulong vtype)
{
    uint8_t sew, lmul;

    sew = FIELD_EX64(vtype, VTYPE, VSEW);
    lmul = FIELD_EX64(vtype, VTYPE, VLMUL);
    return cpu->cfg.vlen >> (sew + 3 - lmul);
}

static inline void cpu_get_tb_cpu_state(CPURISCVState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *pflags)
{
    uint32_t flags = 0;

    *pc = env->pc;
    *cs_base = 0;

    if (riscv_has_ext(env, RVV)) {
        uint32_t vlmax = vext_get_vlmax(env_archcpu(env), env->vtype);
        bool vl_eq_vlmax = (env->vstart == 0) && (vlmax == env->vl);
        flags = FIELD_DP32(flags, TB_FLAGS, VILL,
                    FIELD_EX64(env->vtype, VTYPE, VILL));
        flags = FIELD_DP32(flags, TB_FLAGS, SEW,
                    FIELD_EX64(env->vtype, VTYPE, VSEW));
        flags = FIELD_DP32(flags, TB_FLAGS, LMUL,
                    FIELD_EX64(env->vtype, VTYPE, VLMUL));
        flags = FIELD_DP32(flags, TB_FLAGS, VL_EQ_VLMAX, vl_eq_vlmax);
    } else {
        flags = FIELD_DP32(flags, TB_FLAGS, VILL, 1);
    }

#ifdef CONFIG_USER_ONLY
    flags |= TB_FLAGS_MSTATUS_FS;
#else
    flags |= cpu_mmu_index(env, 0);
    if (riscv_cpu_fp_enabled(env)) {
        flags |= env->mstatus & MSTATUS_FS;
    }
#endif
    *pflags = flags;
}

int riscv_csrrw(CPURISCVState *env, int csrno, target_ulong *ret_value,
                target_ulong new_value, target_ulong write_mask);
int riscv_csrrw_debug(CPURISCVState *env, int csrno, target_ulong *ret_value,
                      target_ulong new_value, target_ulong write_mask);

static inline void riscv_csr_write(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    riscv_csrrw(env, csrno, NULL, val, MAKE_64BIT_MASK(0, TARGET_LONG_BITS));
}

static inline target_ulong riscv_csr_read(CPURISCVState *env, int csrno)
{
    target_ulong val = 0;
    riscv_csrrw(env, csrno, &val, 0, 0);
    return val;
}

typedef int (*riscv_csr_predicate_fn)(CPURISCVState *env, int csrno);
typedef int (*riscv_csr_read_fn)(CPURISCVState *env, int csrno,
    target_ulong *ret_value);
typedef int (*riscv_csr_write_fn)(CPURISCVState *env, int csrno,
    target_ulong new_value);
typedef int (*riscv_csr_op_fn)(CPURISCVState *env, int csrno,
    target_ulong *ret_value, target_ulong new_value, target_ulong write_mask);

typedef struct {
    riscv_csr_predicate_fn predicate;
    riscv_csr_read_fn read;
    riscv_csr_write_fn write;
    riscv_csr_op_fn op;
} riscv_csr_operations;

void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops);
void riscv_set_csr_ops(int csrno, riscv_csr_operations *ops);

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs);

#endif /* RISCV_CPU_H */
