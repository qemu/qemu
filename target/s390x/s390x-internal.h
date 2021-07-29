/*
 * s390x internal definitions and helpers
 *
 * Copyright (c) 2009 Ulrich Hecht
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef S390X_INTERNAL_H
#define S390X_INTERNAL_H

#include "cpu.h"

#ifndef CONFIG_USER_ONLY
typedef struct LowCore {
    /* prefix area: defined by architecture */
    uint32_t        ccw1[2];                  /* 0x000 */
    uint32_t        ccw2[4];                  /* 0x008 */
    uint8_t         pad1[0x80 - 0x18];        /* 0x018 */
    uint32_t        ext_params;               /* 0x080 */
    uint16_t        cpu_addr;                 /* 0x084 */
    uint16_t        ext_int_code;             /* 0x086 */
    uint16_t        svc_ilen;                 /* 0x088 */
    uint16_t        svc_code;                 /* 0x08a */
    uint16_t        pgm_ilen;                 /* 0x08c */
    uint16_t        pgm_code;                 /* 0x08e */
    uint32_t        data_exc_code;            /* 0x090 */
    uint16_t        mon_class_num;            /* 0x094 */
    uint16_t        per_perc_atmid;           /* 0x096 */
    uint64_t        per_address;              /* 0x098 */
    uint8_t         exc_access_id;            /* 0x0a0 */
    uint8_t         per_access_id;            /* 0x0a1 */
    uint8_t         op_access_id;             /* 0x0a2 */
    uint8_t         ar_access_id;             /* 0x0a3 */
    uint8_t         pad2[0xA8 - 0xA4];        /* 0x0a4 */
    uint64_t        trans_exc_code;           /* 0x0a8 */
    uint64_t        monitor_code;             /* 0x0b0 */
    uint16_t        subchannel_id;            /* 0x0b8 */
    uint16_t        subchannel_nr;            /* 0x0ba */
    uint32_t        io_int_parm;              /* 0x0bc */
    uint32_t        io_int_word;              /* 0x0c0 */
    uint8_t         pad3[0xc8 - 0xc4];        /* 0x0c4 */
    uint32_t        stfl_fac_list;            /* 0x0c8 */
    uint8_t         pad4[0xe8 - 0xcc];        /* 0x0cc */
    uint64_t        mcic;                     /* 0x0e8 */
    uint8_t         pad5[0xf4 - 0xf0];        /* 0x0f0 */
    uint32_t        external_damage_code;     /* 0x0f4 */
    uint64_t        failing_storage_address;  /* 0x0f8 */
    uint8_t         pad6[0x110 - 0x100];      /* 0x100 */
    uint64_t        per_breaking_event_addr;  /* 0x110 */
    uint8_t         pad7[0x120 - 0x118];      /* 0x118 */
    PSW             restart_old_psw;          /* 0x120 */
    PSW             external_old_psw;         /* 0x130 */
    PSW             svc_old_psw;              /* 0x140 */
    PSW             program_old_psw;          /* 0x150 */
    PSW             mcck_old_psw;             /* 0x160 */
    PSW             io_old_psw;               /* 0x170 */
    uint8_t         pad8[0x1a0 - 0x180];      /* 0x180 */
    PSW             restart_new_psw;          /* 0x1a0 */
    PSW             external_new_psw;         /* 0x1b0 */
    PSW             svc_new_psw;              /* 0x1c0 */
    PSW             program_new_psw;          /* 0x1d0 */
    PSW             mcck_new_psw;             /* 0x1e0 */
    PSW             io_new_psw;               /* 0x1f0 */
    uint8_t         pad13[0x11b0 - 0x200];    /* 0x200 */

    uint64_t        mcesad;                    /* 0x11B0 */

    /* 64 bit extparam used for pfault, diag 250 etc  */
    uint64_t        ext_params2;               /* 0x11B8 */

    uint8_t         pad14[0x1200 - 0x11C0];    /* 0x11C0 */

    /* System info area */

    uint64_t        floating_pt_save_area[16]; /* 0x1200 */
    uint64_t        gpregs_save_area[16];      /* 0x1280 */
    uint32_t        st_status_fixed_logout[4]; /* 0x1300 */
    uint8_t         pad15[0x1318 - 0x1310];    /* 0x1310 */
    uint32_t        prefixreg_save_area;       /* 0x1318 */
    uint32_t        fpt_creg_save_area;        /* 0x131c */
    uint8_t         pad16[0x1324 - 0x1320];    /* 0x1320 */
    uint32_t        tod_progreg_save_area;     /* 0x1324 */
    uint64_t        cpu_timer_save_area;       /* 0x1328 */
    uint64_t        clock_comp_save_area;      /* 0x1330 */
    uint8_t         pad17[0x1340 - 0x1338];    /* 0x1338 */
    uint32_t        access_regs_save_area[16]; /* 0x1340 */
    uint64_t        cregs_save_area[16];       /* 0x1380 */

    /* align to the top of the prefix area */

    uint8_t         pad18[0x2000 - 0x1400];    /* 0x1400 */
} QEMU_PACKED LowCore;
QEMU_BUILD_BUG_ON(sizeof(LowCore) != 8192);
#endif /* CONFIG_USER_ONLY */

#define MAX_ILEN 6

/* While the PoO talks about ILC (a number between 1-3) what is actually
   stored in LowCore is shifted left one bit (an even between 2-6).  As
   this is the actual length of the insn and therefore more useful, that
   is what we want to pass around and manipulate.  To make sure that we
   have applied this distinction universally, rename the "ILC" to "ILEN".  */
static inline int get_ilen(uint8_t opc)
{
    switch (opc >> 6) {
    case 0:
        return 2;
    case 1:
    case 2:
        return 4;
    default:
        return 6;
    }
}

/* Compute the ATMID field that is stored in the per_perc_atmid lowcore
   entry when a PER exception is triggered.  */
static inline uint8_t get_per_atmid(CPUS390XState *env)
{
    return ((env->psw.mask & PSW_MASK_64) ?       (1 << 7) : 0) |
                                                  (1 << 6)      |
           ((env->psw.mask & PSW_MASK_32) ?       (1 << 5) : 0) |
           ((env->psw.mask & PSW_MASK_DAT) ?      (1 << 4) : 0) |
           ((env->psw.mask & PSW_ASC_SECONDARY) ? (1 << 3) : 0) |
           ((env->psw.mask & PSW_ASC_ACCREG) ?    (1 << 2) : 0);
}

static inline uint64_t wrap_address(CPUS390XState *env, uint64_t a)
{
    if (!(env->psw.mask & PSW_MASK_64)) {
        if (!(env->psw.mask & PSW_MASK_32)) {
            /* 24-Bit mode */
            a &= 0x00ffffff;
        } else {
            /* 31-Bit mode */
            a &= 0x7fffffff;
        }
    }
    return a;
}

/* CC optimization */

/* Instead of computing the condition codes after each x86 instruction,
 * QEMU just stores the result (called CC_DST), the type of operation
 * (called CC_OP) and whatever operands are needed (CC_SRC and possibly
 * CC_VR). When the condition codes are needed, the condition codes can
 * be calculated using this information. Condition codes are not generated
 * if they are only needed for conditional branches.
 */
enum cc_op {
    CC_OP_CONST0 = 0,           /* CC is 0 */
    CC_OP_CONST1,               /* CC is 1 */
    CC_OP_CONST2,               /* CC is 2 */
    CC_OP_CONST3,               /* CC is 3 */

    CC_OP_DYNAMIC,              /* CC calculation defined by env->cc_op */
    CC_OP_STATIC,               /* CC value is env->cc_op */

    CC_OP_NZ,                   /* env->cc_dst != 0 */
    CC_OP_ADDU,                 /* dst != 0, src = carry out (0,1) */
    CC_OP_SUBU,                 /* dst != 0, src = borrow out (0,-1) */

    CC_OP_LTGT_32,              /* signed less/greater than (32bit) */
    CC_OP_LTGT_64,              /* signed less/greater than (64bit) */
    CC_OP_LTUGTU_32,            /* unsigned less/greater than (32bit) */
    CC_OP_LTUGTU_64,            /* unsigned less/greater than (64bit) */
    CC_OP_LTGT0_32,             /* signed less/greater than 0 (32bit) */
    CC_OP_LTGT0_64,             /* signed less/greater than 0 (64bit) */

    CC_OP_ADD_64,               /* overflow on add (64bit) */
    CC_OP_SUB_64,               /* overflow on subtraction (64bit) */
    CC_OP_ABS_64,               /* sign eval on abs (64bit) */
    CC_OP_NABS_64,              /* sign eval on nabs (64bit) */
    CC_OP_MULS_64,              /* overflow on signed multiply (64bit) */

    CC_OP_ADD_32,               /* overflow on add (32bit) */
    CC_OP_SUB_32,               /* overflow on subtraction (32bit) */
    CC_OP_ABS_32,               /* sign eval on abs (64bit) */
    CC_OP_NABS_32,              /* sign eval on nabs (64bit) */
    CC_OP_MULS_32,              /* overflow on signed multiply (32bit) */

    CC_OP_COMP_32,              /* complement */
    CC_OP_COMP_64,              /* complement */

    CC_OP_TM_32,                /* test under mask (32bit) */
    CC_OP_TM_64,                /* test under mask (64bit) */

    CC_OP_NZ_F32,               /* FP dst != 0 (32bit) */
    CC_OP_NZ_F64,               /* FP dst != 0 (64bit) */
    CC_OP_NZ_F128,              /* FP dst != 0 (128bit) */

    CC_OP_ICM,                  /* insert characters under mask */
    CC_OP_SLA_32,               /* Calculate shift left signed (32bit) */
    CC_OP_SLA_64,               /* Calculate shift left signed (64bit) */
    CC_OP_FLOGR,                /* find leftmost one */
    CC_OP_LCBB,                 /* load count to block boundary */
    CC_OP_VC,                   /* vector compare result */
    CC_OP_MAX
};

#ifndef CONFIG_USER_ONLY

static inline hwaddr decode_basedisp_s(CPUS390XState *env, uint32_t ipb,
                                       uint8_t *ar)
{
    hwaddr addr = 0;
    uint8_t reg;

    reg = ipb >> 28;
    if (reg > 0) {
        addr = env->regs[reg];
    }
    addr += (ipb >> 16) & 0xfff;
    if (ar) {
        *ar = reg;
    }

    return addr;
}

/* Base/displacement are at the same locations. */
#define decode_basedisp_rs decode_basedisp_s

#endif /* CONFIG_USER_ONLY */

/* arch_dump.c */
int s390_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                              int cpuid, void *opaque);


/* cc_helper.c */
const char *cc_name(enum cc_op cc_op);
uint32_t calc_cc(CPUS390XState *env, uint32_t cc_op, uint64_t src, uint64_t dst,
                 uint64_t vr);

/* cpu.c */
#ifndef CONFIG_USER_ONLY
unsigned int s390_cpu_halt(S390CPU *cpu);
void s390_cpu_unhalt(S390CPU *cpu);
void s390_cpu_init_sysemu(Object *obj);
bool s390_cpu_realize_sysemu(DeviceState *dev, Error **errp);
void s390_cpu_finalize(Object *obj);
void s390_cpu_class_init_sysemu(CPUClass *cc);
void s390_cpu_machine_reset_cb(void *opaque);

#else
static inline unsigned int s390_cpu_halt(S390CPU *cpu)
{
    return 0;
}

static inline void s390_cpu_unhalt(S390CPU *cpu)
{
}
#endif /* CONFIG_USER_ONLY */


/* cpu_models.c */
void s390_cpu_model_class_register_props(ObjectClass *oc);
void s390_realize_cpu_model(CPUState *cs, Error **errp);
S390CPUModel *get_max_cpu_model(Error **errp);
void apply_cpu_model(const S390CPUModel *model, Error **errp);
ObjectClass *s390_cpu_class_by_name(const char *name);


/* excp_helper.c */
void s390x_cpu_debug_excp_handler(CPUState *cs);
void s390_cpu_do_interrupt(CPUState *cpu);
bool s390_cpu_exec_interrupt(CPUState *cpu, int int_req);
bool s390_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr);
void s390x_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                   MMUAccessType access_type, int mmu_idx,
                                   uintptr_t retaddr) QEMU_NORETURN;


/* fpu_helper.c */
uint32_t set_cc_nz_f32(float32 v);
uint32_t set_cc_nz_f64(float64 v);
uint32_t set_cc_nz_f128(float128 v);
#define S390_IEEE_MASK_INVALID   0x80
#define S390_IEEE_MASK_DIVBYZERO 0x40
#define S390_IEEE_MASK_OVERFLOW  0x20
#define S390_IEEE_MASK_UNDERFLOW 0x10
#define S390_IEEE_MASK_INEXACT   0x08
#define S390_IEEE_MASK_QUANTUM   0x04
uint8_t s390_softfloat_exc_to_ieee(unsigned int exc);
int s390_swap_bfp_rounding_mode(CPUS390XState *env, int m3);
void s390_restore_bfp_rounding_mode(CPUS390XState *env, int old_mode);
int float_comp_to_cc(CPUS390XState *env, int float_compare);

#define DCMASK_ZERO             0x0c00
#define DCMASK_NORMAL           0x0300
#define DCMASK_SUBNORMAL        0x00c0
#define DCMASK_INFINITY         0x0030
#define DCMASK_QUIET_NAN        0x000c
#define DCMASK_SIGNALING_NAN    0x0003
#define DCMASK_NAN              0x000f
#define DCMASK_NEGATIVE         0x0555
uint16_t float32_dcmask(CPUS390XState *env, float32 f1);
uint16_t float64_dcmask(CPUS390XState *env, float64 f1);
uint16_t float128_dcmask(CPUS390XState *env, float128 f1);


/* gdbstub.c */
int s390_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int s390_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void s390_cpu_gdb_init(CPUState *cs);


/* helper.c */
void s390_cpu_dump_state(CPUState *cpu, FILE *f, int flags);
void do_restart_interrupt(CPUS390XState *env);
#ifndef CONFIG_USER_ONLY
void s390_cpu_recompute_watchpoints(CPUState *cs);
void s390x_tod_timer(void *opaque);
void s390x_cpu_timer(void *opaque);
void s390_handle_wait(S390CPU *cpu);
hwaddr s390_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
hwaddr s390_cpu_get_phys_addr_debug(CPUState *cpu, vaddr addr);
#define S390_STORE_STATUS_DEF_ADDR offsetof(LowCore, floating_pt_save_area)
int s390_store_status(S390CPU *cpu, hwaddr addr, bool store_arch);
int s390_store_adtl_status(S390CPU *cpu, hwaddr addr, hwaddr len);
LowCore *cpu_map_lowcore(CPUS390XState *env);
void cpu_unmap_lowcore(LowCore *lowcore);
#endif /* CONFIG_USER_ONLY */


/* interrupt.c */
void trigger_pgm_exception(CPUS390XState *env, uint32_t code);
void cpu_inject_clock_comparator(S390CPU *cpu);
void cpu_inject_cpu_timer(S390CPU *cpu);
void cpu_inject_emergency_signal(S390CPU *cpu, uint16_t src_cpu_addr);
int cpu_inject_external_call(S390CPU *cpu, uint16_t src_cpu_addr);
bool s390_cpu_has_io_int(S390CPU *cpu);
bool s390_cpu_has_ext_int(S390CPU *cpu);
bool s390_cpu_has_mcck_int(S390CPU *cpu);
bool s390_cpu_has_int(S390CPU *cpu);
bool s390_cpu_has_restart_int(S390CPU *cpu);
bool s390_cpu_has_stop_int(S390CPU *cpu);
void cpu_inject_restart(S390CPU *cpu);
void cpu_inject_stop(S390CPU *cpu);


/* ioinst.c */
void ioinst_handle_xsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_csch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_hsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_msch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                        uintptr_t ra);
void ioinst_handle_ssch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                        uintptr_t ra);
void ioinst_handle_stcrw(S390CPU *cpu, uint32_t ipb, uintptr_t ra);
void ioinst_handle_stsch(S390CPU *cpu, uint64_t reg1, uint32_t ipb,
                         uintptr_t ra);
int ioinst_handle_tsch(S390CPU *cpu, uint64_t reg1, uint32_t ipb, uintptr_t ra);
void ioinst_handle_chsc(S390CPU *cpu, uint32_t ipb, uintptr_t ra);
void ioinst_handle_schm(S390CPU *cpu, uint64_t reg1, uint64_t reg2,
                        uint32_t ipb, uintptr_t ra);
void ioinst_handle_rsch(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_rchp(S390CPU *cpu, uint64_t reg1, uintptr_t ra);
void ioinst_handle_sal(S390CPU *cpu, uint64_t reg1, uintptr_t ra);


/* mem_helper.c */
target_ulong mmu_real2abs(CPUS390XState *env, target_ulong raddr);
void probe_write_access(CPUS390XState *env, uint64_t addr, uint64_t len,
                        uintptr_t ra);


/* mmu_helper.c */
bool mmu_absolute_addr_valid(target_ulong addr, bool is_write);
/* Special access mode only valid for mmu_translate() */
#define MMU_S390_LRA        -1
int mmu_translate(CPUS390XState *env, target_ulong vaddr, int rw, uint64_t asc,
                  target_ulong *raddr, int *flags, uint64_t *tec);
int mmu_translate_real(CPUS390XState *env, target_ulong raddr, int rw,
                       target_ulong *addr, int *flags, uint64_t *tec);


/* misc_helper.c */
int handle_diag_288(CPUS390XState *env, uint64_t r1, uint64_t r3);
void handle_diag_308(CPUS390XState *env, uint64_t r1, uint64_t r3,
                     uintptr_t ra);


/* translate.c */
void s390x_translate_init(void);


/* sigp.c */
int handle_sigp(CPUS390XState *env, uint8_t order, uint64_t r1, uint64_t r3);
void do_stop_interrupt(CPUS390XState *env);

#endif /* S390X_INTERNAL_H */
