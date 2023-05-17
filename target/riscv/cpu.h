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
#include "qemu/cpu-float.h"
#include "qom/object.h"
#include "qemu/int128.h"
#include "cpu_bits.h"
#include "qapi/qapi-types-common.h"
#include "cpu-qom.h"

#define TCG_GUEST_DEFAULT_MO 0

/*
 * RISC-V-specific extra insn start words:
 * 1: Original instruction opcode
 */
#define TARGET_INSN_START_EXTRA_WORDS 1

#define RV(x) ((target_ulong)1 << (x - 'A'))

/* Consider updating misa_ext_cfgs[] when adding new MISA bits here */
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
#define RVJ RV('J')
#define RVG RV('G')


/* Privileged specification version */
enum {
    PRIV_VERSION_1_10_0 = 0,
    PRIV_VERSION_1_11_0,
    PRIV_VERSION_1_12_0,

    PRIV_VERSION_LATEST = PRIV_VERSION_1_12_0,
};

#define VEXT_VERSION_1_00_0 0x00010000

enum {
    TRANSLATE_SUCCESS,
    TRANSLATE_FAIL,
    TRANSLATE_PMP_FAIL,
    TRANSLATE_G_STAGE_FAIL
};

/* Extension context status */
typedef enum {
    EXT_STATUS_DISABLED = 0,
    EXT_STATUS_INITIAL,
    EXT_STATUS_CLEAN,
    EXT_STATUS_DIRTY,
} RISCVExtStatus;

#define MMU_USER_IDX 3

#define MAX_RISCV_PMPS (16)

#if !defined(CONFIG_USER_ONLY)
#include "pmp.h"
#include "debug.h"
#endif

#define RV_VLEN_MAX 1024
#define RV_MAX_MHPMEVENTS 32
#define RV_MAX_MHPMCOUNTERS 32

FIELD(VTYPE, VLMUL, 0, 3)
FIELD(VTYPE, VSEW, 3, 3)
FIELD(VTYPE, VTA, 6, 1)
FIELD(VTYPE, VMA, 7, 1)
FIELD(VTYPE, VEDIV, 8, 2)
FIELD(VTYPE, RESERVED, 10, sizeof(target_ulong) * 8 - 11)

typedef struct PMUCTRState {
    /* Current value of a counter */
    target_ulong mhpmcounter_val;
    /* Current value of a counter in RV32 */
    target_ulong mhpmcounterh_val;
    /* Snapshot values of counter */
    target_ulong mhpmcounter_prev;
    /* Snapshort value of a counter in RV32 */
    target_ulong mhpmcounterh_prev;
    bool started;
    /* Value beyond UINT32_MAX/UINT64_MAX before overflow interrupt trigger */
    target_ulong irq_overflow_left;
} PMUCTRState;

struct CPUArchState {
    target_ulong gpr[32];
    target_ulong gprh[32]; /* 64 top bits of the 128-bit registers */

    /* vector coprocessor state. */
    uint64_t vreg[32 * RV_VLEN_MAX / 64] QEMU_ALIGNED(16);
    target_ulong vxrm;
    target_ulong vxsat;
    target_ulong vl;
    target_ulong vstart;
    target_ulong vtype;
    bool vill;

    target_ulong pc;
    target_ulong load_res;
    target_ulong load_val;

    /* Floating-Point state */
    uint64_t fpr[32]; /* assume both F and D extensions */
    target_ulong frm;
    float_status fp_status;

    target_ulong badaddr;
    target_ulong bins;

    target_ulong guest_phys_fault_addr;

    target_ulong priv_ver;
    target_ulong bext_ver;
    target_ulong vext_ver;

    /* RISCVMXL, but uint32_t for vmstate migration */
    uint32_t misa_mxl;      /* current mxl */
    uint32_t misa_mxl_max;  /* max mxl for this cpu */
    uint32_t misa_ext;      /* current extensions */
    uint32_t misa_ext_mask; /* max ext for this cpu */
    uint32_t xl;            /* current xlen */

    /* 128-bit helpers upper part return value */
    target_ulong retxh;

    target_ulong jvt;

#ifdef CONFIG_USER_ONLY
    uint32_t elf_flags;
#endif

#ifndef CONFIG_USER_ONLY
    target_ulong priv;
    /* This contains QEMU specific information about the virt state. */
    bool virt_enabled;
    target_ulong geilen;
    uint64_t resetvec;

    target_ulong mhartid;
    /*
     * For RV32 this is 32-bit mstatus and 32-bit mstatush.
     * For RV64 this is a 64-bit mstatus.
     */
    uint64_t mstatus;

    uint64_t mip;
    /*
     * MIP contains the software writable version of SEIP ORed with the
     * external interrupt value. The MIP register is always up-to-date.
     * To keep track of the current source, we also save booleans of the values
     * here.
     */
    bool external_seip;
    bool software_seip;

    uint64_t miclaim;

    uint64_t mie;
    uint64_t mideleg;

    target_ulong satp;   /* since: priv-1.10.0 */
    target_ulong stval;
    target_ulong medeleg;

    target_ulong stvec;
    target_ulong sepc;
    target_ulong scause;

    target_ulong mtvec;
    target_ulong mepc;
    target_ulong mcause;
    target_ulong mtval;  /* since: priv-1.10.0 */

    /* Machine and Supervisor interrupt priorities */
    uint8_t miprio[64];
    uint8_t siprio[64];

    /* AIA CSRs */
    target_ulong miselect;
    target_ulong siselect;

    /* Hypervisor CSRs */
    target_ulong hstatus;
    target_ulong hedeleg;
    uint64_t hideleg;
    target_ulong hcounteren;
    target_ulong htval;
    target_ulong htinst;
    target_ulong hgatp;
    target_ulong hgeie;
    target_ulong hgeip;
    uint64_t htimedelta;

    /* Hypervisor controlled virtual interrupt priorities */
    target_ulong hvictl;
    uint8_t hviprio[64];

    /* Upper 64-bits of 128-bit CSRs */
    uint64_t mscratchh;
    uint64_t sscratchh;

    /* Virtual CSRs */
    /*
     * For RV32 this is 32-bit vsstatus and 32-bit vsstatush.
     * For RV64 this is a 64-bit vsstatus.
     */
    uint64_t vsstatus;
    target_ulong vstvec;
    target_ulong vsscratch;
    target_ulong vsepc;
    target_ulong vscause;
    target_ulong vstval;
    target_ulong vsatp;

    /* AIA VS-mode CSRs */
    target_ulong vsiselect;

    target_ulong mtval2;
    target_ulong mtinst;

    /* HS Backup CSRs */
    target_ulong stvec_hs;
    target_ulong sscratch_hs;
    target_ulong sepc_hs;
    target_ulong scause_hs;
    target_ulong stval_hs;
    target_ulong satp_hs;
    uint64_t mstatus_hs;

    /*
     * Signals whether the current exception occurred with two-stage address
     * translation active.
     */
    bool two_stage_lookup;
    /*
     * Signals whether the current exception occurred while doing two-stage
     * address translation for the VS-stage page table walk.
     */
    bool two_stage_indirect_lookup;

    target_ulong scounteren;
    target_ulong mcounteren;

    target_ulong mcountinhibit;

    /* PMU counter state */
    PMUCTRState pmu_ctrs[RV_MAX_MHPMCOUNTERS];

    /* PMU event selector configured values. First three are unused */
    target_ulong mhpmevent_val[RV_MAX_MHPMEVENTS];

    /* PMU event selector configured values for RV32 */
    target_ulong mhpmeventh_val[RV_MAX_MHPMEVENTS];

    target_ulong sscratch;
    target_ulong mscratch;

    /* Sstc CSRs */
    uint64_t stimecmp;

    uint64_t vstimecmp;

    /* physical memory protection */
    pmp_table_t pmp_state;
    target_ulong mseccfg;

    /* trigger module */
    target_ulong trigger_cur;
    target_ulong tdata1[RV_MAX_TRIGGERS];
    target_ulong tdata2[RV_MAX_TRIGGERS];
    target_ulong tdata3[RV_MAX_TRIGGERS];
    struct CPUBreakpoint *cpu_breakpoint[RV_MAX_TRIGGERS];
    struct CPUWatchpoint *cpu_watchpoint[RV_MAX_TRIGGERS];
    QEMUTimer *itrigger_timer[RV_MAX_TRIGGERS];
    int64_t last_icount;
    bool itrigger_enabled;

    /* machine specific rdtime callback */
    uint64_t (*rdtime_fn)(void *);
    void *rdtime_fn_arg;

    /* machine specific AIA ireg read-modify-write callback */
#define AIA_MAKE_IREG(__isel, __priv, __virt, __vgein, __xlen) \
    ((((__xlen) & 0xff) << 24) | \
     (((__vgein) & 0x3f) << 20) | \
     (((__virt) & 0x1) << 18) | \
     (((__priv) & 0x3) << 16) | \
     (__isel & 0xffff))
#define AIA_IREG_ISEL(__ireg)                  ((__ireg) & 0xffff)
#define AIA_IREG_PRIV(__ireg)                  (((__ireg) >> 16) & 0x3)
#define AIA_IREG_VIRT(__ireg)                  (((__ireg) >> 18) & 0x1)
#define AIA_IREG_VGEIN(__ireg)                 (((__ireg) >> 20) & 0x3f)
#define AIA_IREG_XLEN(__ireg)                  (((__ireg) >> 24) & 0xff)
    int (*aia_ireg_rmw_fn[4])(void *arg, target_ulong reg,
        target_ulong *val, target_ulong new_val, target_ulong write_mask);
    void *aia_ireg_rmw_fn_arg[4];

    /* True if in debugger mode.  */
    bool debugger;

    /*
     * CSRs for PointerMasking extension
     */
    target_ulong mmte;
    target_ulong mpmmask;
    target_ulong mpmbase;
    target_ulong spmmask;
    target_ulong spmbase;
    target_ulong upmmask;
    target_ulong upmbase;

    /* CSRs for execution enviornment configuration */
    uint64_t menvcfg;
    uint64_t mstateen[SMSTATEEN_MAX_COUNT];
    uint64_t hstateen[SMSTATEEN_MAX_COUNT];
    uint64_t sstateen[SMSTATEEN_MAX_COUNT];
    target_ulong senvcfg;
    uint64_t henvcfg;
#endif
    target_ulong cur_pmmask;
    target_ulong cur_pmbase;

    /* Fields from here on are preserved across CPU reset. */
    QEMUTimer *stimer; /* Internal timer for S-mode interrupt */
    QEMUTimer *vstimer; /* Internal timer for VS-mode interrupt */
    bool vstime_irq;

    hwaddr kernel_addr;
    hwaddr fdt_addr;

    /* kvm timer */
    bool kvm_timer_dirty;
    uint64_t kvm_timer_time;
    uint64_t kvm_timer_compare;
    uint64_t kvm_timer_state;
    uint64_t kvm_timer_frequency;
};

/*
 * map is a 16-bit bitmap: the most significant set bit in map is the maximum
 * satp mode that is supported. It may be chosen by the user and must respect
 * what qemu implements (valid_1_10_32/64) and what the hw is capable of
 * (supported bitmap below).
 *
 * init is a 16-bit bitmap used to make sure the user selected a correct
 * configuration as per the specification.
 *
 * supported is a 16-bit bitmap used to reflect the hw capabilities.
 */
typedef struct {
    uint16_t map, init, supported;
} RISCVSATPMap;

struct RISCVCPUConfig {
    bool ext_zba;
    bool ext_zbb;
    bool ext_zbc;
    bool ext_zbkb;
    bool ext_zbkc;
    bool ext_zbkx;
    bool ext_zbs;
    bool ext_zca;
    bool ext_zcb;
    bool ext_zcd;
    bool ext_zce;
    bool ext_zcf;
    bool ext_zcmp;
    bool ext_zcmt;
    bool ext_zk;
    bool ext_zkn;
    bool ext_zknd;
    bool ext_zkne;
    bool ext_zknh;
    bool ext_zkr;
    bool ext_zks;
    bool ext_zksed;
    bool ext_zksh;
    bool ext_zkt;
    bool ext_ifencei;
    bool ext_icsr;
    bool ext_icbom;
    bool ext_icboz;
    bool ext_zicond;
    bool ext_zihintpause;
    bool ext_smstateen;
    bool ext_sstc;
    bool ext_svadu;
    bool ext_svinval;
    bool ext_svnapot;
    bool ext_svpbmt;
    bool ext_zdinx;
    bool ext_zawrs;
    bool ext_zfh;
    bool ext_zfhmin;
    bool ext_zfinx;
    bool ext_zhinx;
    bool ext_zhinxmin;
    bool ext_zve32f;
    bool ext_zve64f;
    bool ext_zve64d;
    bool ext_zmmul;
    bool ext_zvfh;
    bool ext_zvfhmin;
    bool ext_smaia;
    bool ext_ssaia;
    bool ext_sscofpmf;
    bool rvv_ta_all_1s;
    bool rvv_ma_all_1s;

    uint32_t mvendorid;
    uint64_t marchid;
    uint64_t mimpid;

    /* Vendor-specific custom extensions */
    bool ext_xtheadba;
    bool ext_xtheadbb;
    bool ext_xtheadbs;
    bool ext_xtheadcmo;
    bool ext_xtheadcondmov;
    bool ext_xtheadfmemidx;
    bool ext_xtheadfmv;
    bool ext_xtheadmac;
    bool ext_xtheadmemidx;
    bool ext_xtheadmempair;
    bool ext_xtheadsync;
    bool ext_XVentanaCondOps;

    uint8_t pmu_num;
    char *priv_spec;
    char *user_spec;
    char *bext_spec;
    char *vext_spec;
    uint16_t vlen;
    uint16_t elen;
    uint16_t cbom_blocksize;
    uint16_t cboz_blocksize;
    bool mmu;
    bool pmp;
    bool epmp;
    bool debug;
    bool misa_w;

    bool short_isa_string;

#ifndef CONFIG_USER_ONLY
    RISCVSATPMap satp_mode;
#endif
};

typedef struct RISCVCPUConfig RISCVCPUConfig;

/*
 * RISCVCPU:
 * @env: #CPURISCVState
 *
 * A RISCV CPU.
 */
struct ArchCPU {
    /* < private > */
    CPUState parent_obj;
    /* < public > */
    CPUNegativeOffsetState neg;
    CPURISCVState env;

    char *dyn_csr_xml;
    char *dyn_vreg_xml;

    /* Configuration Settings */
    RISCVCPUConfig cfg;

    QEMUTimer *pmu_timer;
    /* A bitmask of Available programmable counters */
    uint32_t pmu_avail_ctrs;
    /* Mapping of events to counters */
    GHashTable *pmu_event_ctr_map;
};

static inline int riscv_has_ext(CPURISCVState *env, target_ulong ext)
{
    return (env->misa_ext & ext) != 0;
}

#include "cpu_user.h"

extern const char * const riscv_int_regnames[];
extern const char * const riscv_int_regnamesh[];
extern const char * const riscv_fpr_regnames[];

const char *riscv_cpu_get_trap_name(target_ulong cause, bool async);
void riscv_cpu_do_interrupt(CPUState *cpu);
int riscv_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, DumpState *s);
int riscv_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, DumpState *s);
int riscv_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int riscv_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
int riscv_cpu_hviprio_index2irq(int index, int *out_irq, int *out_rdzero);
uint8_t riscv_cpu_default_priority(int irq);
uint64_t riscv_cpu_all_pending(CPURISCVState *env);
int riscv_cpu_mirq_pending(CPURISCVState *env);
int riscv_cpu_sirq_pending(CPURISCVState *env);
int riscv_cpu_vsirq_pending(CPURISCVState *env);
bool riscv_cpu_fp_enabled(CPURISCVState *env);
target_ulong riscv_cpu_get_geilen(CPURISCVState *env);
void riscv_cpu_set_geilen(CPURISCVState *env, target_ulong geilen);
bool riscv_cpu_vector_enabled(CPURISCVState *env);
void riscv_cpu_set_virt_enabled(CPURISCVState *env, bool enable);
int riscv_cpu_mmu_index(CPURISCVState *env, bool ifetch);
G_NORETURN void  riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                               MMUAccessType access_type,
                                               int mmu_idx, uintptr_t retaddr);
bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
char *riscv_isa_string(RISCVCPU *cpu);
void riscv_cpu_list(void);

#define cpu_list riscv_cpu_list
#define cpu_mmu_index riscv_cpu_mmu_index

#ifndef CONFIG_USER_ONLY
void riscv_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr);
hwaddr riscv_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
void riscv_cpu_swap_hypervisor_regs(CPURISCVState *env);
int riscv_cpu_claim_interrupts(RISCVCPU *cpu, uint64_t interrupts);
uint64_t riscv_cpu_update_mip(CPURISCVState *env, uint64_t mask,
                              uint64_t value);
#define BOOL_TO_MASK(x) (-!!(x)) /* helper for riscv_cpu_update_mip value */
void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(void *),
                             void *arg);
void riscv_cpu_set_aia_ireg_rmw_fn(CPURISCVState *env, uint32_t priv,
                                   int (*rmw_fn)(void *arg,
                                                 target_ulong reg,
                                                 target_ulong *val,
                                                 target_ulong new_val,
                                                 target_ulong write_mask),
                                   void *rmw_fn_arg);

RISCVException smstateen_acc_ok(CPURISCVState *env, int index, uint64_t bit);
#endif
void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv);

void riscv_translate_init(void);
G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      uint32_t exception, uintptr_t pc);

target_ulong riscv_cpu_get_fflags(CPURISCVState *env);
void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong);

#include "exec/cpu-all.h"

FIELD(TB_FLAGS, MEM_IDX, 0, 3)
FIELD(TB_FLAGS, FS, 3, 2)
/* Vector flags */
FIELD(TB_FLAGS, VS, 5, 2)
FIELD(TB_FLAGS, LMUL, 7, 3)
FIELD(TB_FLAGS, SEW, 10, 3)
FIELD(TB_FLAGS, VL_EQ_VLMAX, 13, 1)
FIELD(TB_FLAGS, VILL, 14, 1)
FIELD(TB_FLAGS, VSTART_EQ_ZERO, 15, 1)
/* The combination of MXL/SXL/UXL that applies to the current cpu mode. */
FIELD(TB_FLAGS, XL, 16, 2)
/* If PointerMasking should be applied */
FIELD(TB_FLAGS, PM_MASK_ENABLED, 18, 1)
FIELD(TB_FLAGS, PM_BASE_ENABLED, 19, 1)
FIELD(TB_FLAGS, VTA, 20, 1)
FIELD(TB_FLAGS, VMA, 21, 1)
/* Native debug itrigger */
FIELD(TB_FLAGS, ITRIGGER, 22, 1)
/* Virtual mode enabled */
FIELD(TB_FLAGS, VIRT_ENABLED, 23, 1)
FIELD(TB_FLAGS, PRIV, 24, 2)

#ifdef TARGET_RISCV32
#define riscv_cpu_mxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_mxl(CPURISCVState *env)
{
    return env->misa_mxl;
}
#endif
#define riscv_cpu_mxl_bits(env) (1UL << (4 + riscv_cpu_mxl(env)))

static inline const RISCVCPUConfig *riscv_cpu_cfg(CPURISCVState *env)
{
    return &env_archcpu(env)->cfg;
}

#if defined(TARGET_RISCV32)
#define cpu_recompute_xl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL cpu_recompute_xl(CPURISCVState *env)
{
    RISCVMXL xl = env->misa_mxl;
#if !defined(CONFIG_USER_ONLY)
    /*
     * When emulating a 32-bit-only cpu, use RV32.
     * When emulating a 64-bit cpu, and MXL has been reduced to RV32,
     * MSTATUSH doesn't have UXL/SXL, therefore XLEN cannot be widened
     * back to RV64 for lower privs.
     */
    if (xl != MXL_RV32) {
        switch (env->priv) {
        case PRV_M:
            break;
        case PRV_U:
            xl = get_field(env->mstatus, MSTATUS64_UXL);
            break;
        default: /* PRV_S */
            xl = get_field(env->mstatus, MSTATUS64_SXL);
            break;
        }
    }
#endif
    return xl;
}
#endif

static inline int riscv_cpu_xlen(CPURISCVState *env)
{
    return 16 << env->xl;
}

#ifdef TARGET_RISCV32
#define riscv_cpu_sxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_sxl(CPURISCVState *env)
{
#ifdef CONFIG_USER_ONLY
    return env->misa_mxl;
#else
    return get_field(env->mstatus, MSTATUS64_SXL);
#endif
}
#endif

/*
 * Encode LMUL to lmul as follows:
 *     LMUL    vlmul    lmul
 *      1       000       0
 *      2       001       1
 *      4       010       2
 *      8       011       3
 *      -       100       -
 *     1/8      101      -3
 *     1/4      110      -2
 *     1/2      111      -1
 *
 * then, we can calculate VLMAX = vlen >> (vsew + 3 - lmul)
 * e.g. vlen = 256 bits, SEW = 16, LMUL = 1/8
 *      => VLMAX = vlen >> (1 + 3 - (-3))
 *               = 256 >> 7
 *               = 2
 */
static inline uint32_t vext_get_vlmax(RISCVCPU *cpu, target_ulong vtype)
{
    uint8_t sew = FIELD_EX64(vtype, VTYPE, VSEW);
    int8_t lmul = sextract32(FIELD_EX64(vtype, VTYPE, VLMUL), 0, 3);
    return cpu->cfg.vlen >> (sew + 3 - lmul);
}

void cpu_get_tb_cpu_state(CPURISCVState *env, target_ulong *pc,
                          target_ulong *cs_base, uint32_t *pflags);

void riscv_cpu_update_mask(CPURISCVState *env);

RISCVException riscv_csrrw(CPURISCVState *env, int csrno,
                           target_ulong *ret_value,
                           target_ulong new_value, target_ulong write_mask);
RISCVException riscv_csrrw_debug(CPURISCVState *env, int csrno,
                                 target_ulong *ret_value,
                                 target_ulong new_value,
                                 target_ulong write_mask);

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

typedef RISCVException (*riscv_csr_predicate_fn)(CPURISCVState *env,
                                                 int csrno);
typedef RISCVException (*riscv_csr_read_fn)(CPURISCVState *env, int csrno,
                                            target_ulong *ret_value);
typedef RISCVException (*riscv_csr_write_fn)(CPURISCVState *env, int csrno,
                                             target_ulong new_value);
typedef RISCVException (*riscv_csr_op_fn)(CPURISCVState *env, int csrno,
                                          target_ulong *ret_value,
                                          target_ulong new_value,
                                          target_ulong write_mask);

RISCVException riscv_csrrw_i128(CPURISCVState *env, int csrno,
                                Int128 *ret_value,
                                Int128 new_value, Int128 write_mask);

typedef RISCVException (*riscv_csr_read128_fn)(CPURISCVState *env, int csrno,
                                               Int128 *ret_value);
typedef RISCVException (*riscv_csr_write128_fn)(CPURISCVState *env, int csrno,
                                             Int128 new_value);

typedef struct {
    const char *name;
    riscv_csr_predicate_fn predicate;
    riscv_csr_read_fn read;
    riscv_csr_write_fn write;
    riscv_csr_op_fn op;
    riscv_csr_read128_fn read128;
    riscv_csr_write128_fn write128;
    /* The default priv spec version should be PRIV_VERSION_1_10_0 (i.e 0) */
    uint32_t min_priv_ver;
} riscv_csr_operations;

/* CSR function table constants */
enum {
    CSR_TABLE_SIZE = 0x1000
};

/*
 * The event id are encoded based on the encoding specified in the
 * SBI specification v0.3
 */

enum riscv_pmu_event_idx {
    RISCV_PMU_EVENT_HW_CPU_CYCLES = 0x01,
    RISCV_PMU_EVENT_HW_INSTRUCTIONS = 0x02,
    RISCV_PMU_EVENT_CACHE_DTLB_READ_MISS = 0x10019,
    RISCV_PMU_EVENT_CACHE_DTLB_WRITE_MISS = 0x1001B,
    RISCV_PMU_EVENT_CACHE_ITLB_PREFETCH_MISS = 0x10021,
};

/* CSR function table */
extern riscv_csr_operations csr_ops[CSR_TABLE_SIZE];

extern const bool valid_vm_1_10_32[], valid_vm_1_10_64[];

void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops);
void riscv_set_csr_ops(int csrno, riscv_csr_operations *ops);

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs);

uint8_t satp_mode_max_from_map(uint32_t map);
const char *satp_mode_str(uint8_t satp_mode, bool is_32_bit);

#endif /* RISCV_CPU_H */
