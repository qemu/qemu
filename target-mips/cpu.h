#if !defined (__MIPS_CPU_H__)
#define __MIPS_CPU_H__

#define TARGET_HAS_ICE 1

#define ELF_MACHINE	EM_MIPS

#include "config.h"
#include "mips-defs.h"
#include "cpu-defs.h"
#include "softfloat.h"

// uint_fast8_t and uint_fast16_t not in <sys/int_types.h>
// XXX: move that elsewhere
#if defined(HOST_SOLARIS) && HOST_SOLARIS < 10
typedef unsigned char           uint_fast8_t;
typedef unsigned int            uint_fast16_t;
#endif

typedef union fpr_t fpr_t;
union fpr_t {
    float64  fd;   /* ieee double precision */
    float32  fs[2];/* ieee single precision */
    uint64_t d;    /* binary double fixed-point */
    uint32_t w[2]; /* binary single fixed-point */
};
/* define FP_ENDIAN_IDX to access the same location
 * in the fpr_t union regardless of the host endianess
 */
#if defined(WORDS_BIGENDIAN)
#  define FP_ENDIAN_IDX 1
#else
#  define FP_ENDIAN_IDX 0
#endif

typedef struct r4k_tlb_t r4k_tlb_t;
struct r4k_tlb_t {
    target_ulong VPN;
    uint32_t PageMask;
    uint_fast8_t ASID;
    uint_fast16_t G:1;
    uint_fast16_t C0:3;
    uint_fast16_t C1:3;
    uint_fast16_t V0:1;
    uint_fast16_t V1:1;
    uint_fast16_t D0:1;
    uint_fast16_t D1:1;
    target_ulong PFN[2];
};

typedef struct mips_def_t mips_def_t;

typedef struct CPUMIPSState CPUMIPSState;
struct CPUMIPSState {
    /* General integer registers */
    target_ulong gpr[32];
    /* Special registers */
    target_ulong PC;
#if TARGET_LONG_BITS > HOST_LONG_BITS
    target_ulong t0;
    target_ulong t1;
    target_ulong t2;
#endif
    target_ulong HI, LO;
    /* Floating point registers */
    fpr_t fpr[32];
#ifndef USE_HOST_FLOAT_REGS
    fpr_t ft0;
    fpr_t ft1;
    fpr_t ft2;
#endif
    float_status fp_status;
    /* fpu implementation/revision register (fir) */
    uint32_t fcr0;
#define FCR0_F64 22
#define FCR0_L 21
#define FCR0_W 20
#define FCR0_3D 19
#define FCR0_PS 18
#define FCR0_D 17
#define FCR0_S 16
#define FCR0_PRID 8
#define FCR0_REV 0
    /* fcsr */
    uint32_t fcr31;
#define SET_FP_COND(num,env)     do { ((env)->fcr31) |= ((num) ? (1 << ((num) + 24)) : (1 << 23)); } while(0)
#define CLEAR_FP_COND(num,env)   do { ((env)->fcr31) &= ~((num) ? (1 << ((num) + 24)) : (1 << 23)); } while(0)
#define GET_FP_COND(env)         ((((env)->fcr31 >> 24) & 0xfe) | (((env)->fcr31 >> 23) & 0x1))
#define GET_FP_CAUSE(reg)        (((reg) >> 12) & 0x3f)
#define GET_FP_ENABLE(reg)       (((reg) >>  7) & 0x1f)
#define GET_FP_FLAGS(reg)        (((reg) >>  2) & 0x1f)
#define SET_FP_CAUSE(reg,v)      do { (reg) = ((reg) & ~(0x3f << 12)) | ((v & 0x3f) << 12); } while(0)
#define SET_FP_ENABLE(reg,v)     do { (reg) = ((reg) & ~(0x1f <<  7)) | ((v & 0x1f) << 7); } while(0)
#define SET_FP_FLAGS(reg,v)      do { (reg) = ((reg) & ~(0x1f <<  2)) | ((v & 0x1f) << 2); } while(0)
#define UPDATE_FP_FLAGS(reg,v)   do { (reg) |= ((v & 0x1f) << 2); } while(0)
#define FP_INEXACT        1
#define FP_UNDERFLOW      2
#define FP_OVERFLOW       4
#define FP_DIV0           8
#define FP_INVALID        16
#define FP_UNIMPLEMENTED  32

    uint32_t nb_tlb;
    uint32_t tlb_in_use;
    int (*map_address) (CPUMIPSState *env, target_ulong *physical, int *prot, target_ulong address, int rw, int access_type);
    void (*do_tlbwi) (void);
    void (*do_tlbwr) (void);
    void (*do_tlbp) (void);
    void (*do_tlbr) (void);
    union {
        struct {
            r4k_tlb_t tlb[MIPS_TLB_MAX];
        } r4k;
    } mmu;

    int32_t CP0_Index;
    int32_t CP0_Random;
    target_ulong CP0_EntryLo0;
    target_ulong CP0_EntryLo1;
    target_ulong CP0_Context;
    int32_t CP0_PageMask;
    int32_t CP0_PageGrain;
    int32_t CP0_Wired;
    int32_t CP0_HWREna;
    target_ulong CP0_BadVAddr;
    int32_t CP0_Count;
    target_ulong CP0_EntryHi;
    int32_t CP0_Compare;
    int32_t CP0_Status;
#define CP0St_CU3   31
#define CP0St_CU2   30
#define CP0St_CU1   29
#define CP0St_CU0   28
#define CP0St_RP    27
#define CP0St_FR    26
#define CP0St_RE    25
#define CP0St_MX    24
#define CP0St_PX    23
#define CP0St_BEV   22
#define CP0St_TS    21
#define CP0St_SR    20
#define CP0St_NMI   19
#define CP0St_IM    8
#define CP0St_KX    7
#define CP0St_SX    6
#define CP0St_UX    5
#define CP0St_UM    4
#define CP0St_R0    3
#define CP0St_ERL   2
#define CP0St_EXL   1
#define CP0St_IE    0
    int32_t CP0_IntCtl;
    int32_t CP0_SRSCtl;
    int32_t CP0_SRSMap;
    int32_t CP0_Cause;
#define CP0Ca_BD   31
#define CP0Ca_TI   30
#define CP0Ca_CE   28
#define CP0Ca_DC   27
#define CP0Ca_PCI  26
#define CP0Ca_IV   23
#define CP0Ca_WP   22
#define CP0Ca_IP    8
#define CP0Ca_IP_mask 0x0000FF00
#define CP0Ca_EC    2
    target_ulong CP0_EPC;
    int32_t CP0_PRid;
    int32_t CP0_EBase;
    int32_t CP0_Config0;
#define CP0C0_M    31
#define CP0C0_K23  28
#define CP0C0_KU   25
#define CP0C0_SB   21
#define CP0C0_MDU  20
#define CP0C0_MM   17
#define CP0C0_BM   16
#define CP0C0_BE   15
#define CP0C0_AT   13
#define CP0C0_AR   10
#define CP0C0_MT   7
#define CP0C0_VI   3
#define CP0C0_K0   0
    int32_t CP0_Config1;
#define CP0C1_M    31
#define CP0C1_MMU  25
#define CP0C1_IS   22
#define CP0C1_IL   19
#define CP0C1_IA   16
#define CP0C1_DS   13
#define CP0C1_DL   10
#define CP0C1_DA   7
#define CP0C1_C2   6
#define CP0C1_MD   5
#define CP0C1_PC   4
#define CP0C1_WR   3
#define CP0C1_CA   2
#define CP0C1_EP   1
#define CP0C1_FP   0
    int32_t CP0_Config2;
#define CP0C2_M    31
#define CP0C2_TU   28
#define CP0C2_TS   24
#define CP0C2_TL   20
#define CP0C2_TA   16
#define CP0C2_SU   12
#define CP0C2_SS   8
#define CP0C2_SL   4
#define CP0C2_SA   0
    int32_t CP0_Config3;
#define CP0C3_M    31
#define CP0C3_DSPP 10
#define CP0C3_LPA  7
#define CP0C3_VEIC 6
#define CP0C3_VInt 5
#define CP0C3_SP   4
#define CP0C3_MT   2
#define CP0C3_SM   1
#define CP0C3_TL   0
    int32_t CP0_Config6;
    int32_t CP0_Config7;
    target_ulong CP0_LLAddr;
    target_ulong CP0_WatchLo[8];
    int32_t CP0_WatchHi[8];
    target_ulong CP0_XContext;
    int32_t CP0_Framemask;
    int32_t CP0_Debug;
#define CPDB_DBD   31
#define CP0DB_DM   30
#define CP0DB_LSNM 28
#define CP0DB_Doze 27
#define CP0DB_Halt 26
#define CP0DB_CNT  25
#define CP0DB_IBEP 24
#define CP0DB_DBEP 21
#define CP0DB_IEXI 20
#define CP0DB_VER  15
#define CP0DB_DEC  10
#define CP0DB_SSt  8
#define CP0DB_DINT 5
#define CP0DB_DIB  4
#define CP0DB_DDBS 3
#define CP0DB_DDBL 2
#define CP0DB_DBp  1
#define CP0DB_DSS  0
    uint32_t CP0_TraceControl;
    uint32_t CP0_TraceControl2;
    uint32_t CP0_UserTraceData;
    uint32_t CP0_TraceBPC;
    target_ulong CP0_DEPC;
    //~ uint32_t CP0_ErrCtl;
    int32_t CP0_Performance0;
    int32_t CP0_TagLo;
    int32_t CP0_DataLo;
    int32_t CP0_TagHi;
    int32_t CP0_DataHi;
    target_ulong CP0_ErrorEPC;
    int32_t CP0_DESAVE;
    /* Qemu */
    int interrupt_request;
    jmp_buf jmp_env;
    int exception_index;
    int error_code;
    int user_mode_only; /* user mode only simulation */
    uint32_t hflags;    /* CPU State */
    /* TMASK defines different execution modes */
#define MIPS_HFLAG_TMASK  0x007F
#define MIPS_HFLAG_MODE   0x0007 /* execution modes                    */
#define MIPS_HFLAG_UM     0x0001 /* user mode                          */
#define MIPS_HFLAG_DM     0x0002 /* Debug mode                         */
#define MIPS_HFLAG_SM     0x0004 /* Supervisor mode                    */
#define MIPS_HFLAG_64     0x0008 /* 64-bit instructions enabled        */
#define MIPS_HFLAG_FPU    0x0010 /* FPU enabled                        */
#define MIPS_HFLAG_F64    0x0020 /* 64-bit FPU enabled                 */
#define MIPS_HFLAG_RE     0x0040 /* Reversed endianness                */
    /* If translation is interrupted between the branch instruction and
     * the delay slot, record what type of branch it is so that we can
     * resume translation properly.  It might be possible to reduce
     * this from three bits to two.  */
#define MIPS_HFLAG_BMASK  0x0380
#define MIPS_HFLAG_B      0x0080 /* Unconditional branch               */
#define MIPS_HFLAG_BC     0x0100 /* Conditional branch                 */
#define MIPS_HFLAG_BL     0x0180 /* Likely branch                      */
#define MIPS_HFLAG_BR     0x0200 /* branch to register (can't link TB) */
    target_ulong btarget;        /* Jump / branch target               */
    int bcond;                   /* Branch condition (if needed)       */

    int bigendian;               /* TRUE if the CPU is in big endian mode */
    int halted; /* TRUE if the CPU is in suspend state */

    int SYNCI_Step; /* Address step size for SYNCI */
    int CCRes; /* Cycle count resolution/divisor */
    int Status_rw_bitmask; /* Read/write bits in CP0_Status */

#ifdef CONFIG_USER_ONLY
    target_ulong tls_value;
#endif

    CPU_COMMON

    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;

    mips_def_t *cpu_model;
#ifndef CONFIG_USER_ONLY
    void *irq[8];
#endif

    struct QEMUTimer *timer; /* Internal timer */
};

int no_mmu_map_address (CPUMIPSState *env, target_ulong *physical, int *prot,
                        target_ulong address, int rw, int access_type);
int fixed_mmu_map_address (CPUMIPSState *env, target_ulong *physical, int *prot,
                           target_ulong address, int rw, int access_type);
int r4k_map_address (CPUMIPSState *env, target_ulong *physical, int *prot,
                     target_ulong address, int rw, int access_type);
void r4k_do_tlbwi (void);
void r4k_do_tlbwr (void);
void r4k_do_tlbp (void);
void r4k_do_tlbr (void);
int mips_find_by_name (const unsigned char *name, mips_def_t **def);
void mips_cpu_list (FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt, ...));
int cpu_mips_register (CPUMIPSState *env, mips_def_t *def);

#define CPUState CPUMIPSState
#define cpu_init cpu_mips_init
#define cpu_exec cpu_mips_exec
#define cpu_gen_code cpu_mips_gen_code
#define cpu_signal_handler cpu_mips_signal_handler

#include "cpu-all.h"

/* Memory access type :
 * may be needed for precise access rights control and precise exceptions.
 */
enum {
    /* 1 bit to define user level / supervisor access */
    ACCESS_USER  = 0x00,
    ACCESS_SUPER = 0x01,
    /* 1 bit to indicate direction */
    ACCESS_STORE = 0x02,
    /* Type of instruction that generated the access */
    ACCESS_CODE  = 0x10, /* Code fetch access                */
    ACCESS_INT   = 0x20, /* Integer load/store access        */
    ACCESS_FLOAT = 0x30, /* floating point load/store access */
};

/* Exceptions */
enum {
    EXCP_NONE          = -1,
    EXCP_RESET         = 0,
    EXCP_SRESET,
    EXCP_DSS,
    EXCP_DINT,
    EXCP_NMI,
    EXCP_MCHECK,
    EXCP_EXT_INTERRUPT,
    EXCP_DFWATCH,
    EXCP_DIB, /* 8 */
    EXCP_IWATCH,
    EXCP_AdEL,
    EXCP_AdES,
    EXCP_TLBF,
    EXCP_IBE,
    EXCP_DBp,
    EXCP_SYSCALL,
    EXCP_BREAK, /* 16 */
    EXCP_CpU,
    EXCP_RI,
    EXCP_OVERFLOW,
    EXCP_TRAP,
    EXCP_FPE,
    EXCP_DDBS,
    EXCP_DWATCH,
    EXCP_LAE, /* 24 */
    EXCP_SAE,
    EXCP_LTLBL,
    EXCP_TLBL,
    EXCP_TLBS,
    EXCP_DBE,
    EXCP_DDBL,
    EXCP_MTCP0         = 0x104, /* mtmsr instruction:               */
                                /* may change privilege level       */
    EXCP_BRANCH        = 0x108, /* branch instruction               */
    EXCP_ERET          = 0x10C, /* return from interrupt            */
    EXCP_SYSCALL_USER  = 0x110, /* System call in user mode only    */
    EXCP_FLUSH         = 0x109,
};

int cpu_mips_exec(CPUMIPSState *s);
CPUMIPSState *cpu_mips_init(void);
uint32_t cpu_mips_get_clock (void);
int cpu_mips_signal_handler(int host_signum, void *pinfo, void *puc);

#endif /* !defined (__MIPS_CPU_H__) */
