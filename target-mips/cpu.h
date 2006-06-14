#if !defined (__MIPS_CPU_H__)
#define __MIPS_CPU_H__

#define TARGET_HAS_ICE 1

#include "config.h"
#include "mips-defs.h"
#include "cpu-defs.h"
#include "softfloat.h"

// uint_fast8_t and uint_fast16_t not in <sys/int_types.h>
// XXX: move that elsewhere
#if defined(HOST_SOLARIS) && SOLARISREV < 10
typedef unsigned char           uint_fast8_t;
typedef unsigned int            uint_fast16_t;
#endif

typedef union fpr_t fpr_t;
union fpr_t {
    float64  fd;   /* ieee double precision */
    float32  fs[2];/* ieee single precision */
    uint64_t d;    /* binary single fixed-point */
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

#if defined(MIPS_USES_R4K_TLB)
typedef struct tlb_t tlb_t;
struct tlb_t {
    target_ulong VPN;
    target_ulong end;
    target_ulong end2;
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
#endif

typedef struct CPUMIPSState CPUMIPSState;
struct CPUMIPSState {
    /* General integer registers */
    target_ulong gpr[32];
    /* Special registers */
    target_ulong PC;
    uint32_t HI, LO;
    uint32_t DCR; /* ? */
#if defined(MIPS_USES_FPU)
    /* Floating point registers */
    fpr_t fpr[16];
#define FPR(cpu, n) ((fpr_t*)&(cpu)->fpr[(n) / 2])
#define FPR_FD(cpu, n) (FPR(cpu, n)->fd)
#define FPR_FS(cpu, n) (FPR(cpu, n)->fs[((n) & 1) ^ FP_ENDIAN_IDX])
#define FPR_D(cpu, n)  (FPR(cpu, n)->d)
#define FPR_W(cpu, n)  (FPR(cpu, n)->w[((n) & 1) ^ FP_ENDIAN_IDX])

#ifndef USE_HOST_FLOAT_REGS
    fpr_t ft0;
    fpr_t ft1;
    fpr_t ft2;
#endif
    float_status fp_status;
    /* fpu implementation/revision register */
    uint32_t fcr0;
    /* fcsr */
    uint32_t fcr31;
#define SET_FP_COND(reg)     do { (reg) |= (1<<23); } while(0)
#define CLEAR_FP_COND(reg)   do { (reg) &= ~(1<<23); } while(0)
#define IS_FP_COND_SET(reg)  (((reg) & (1<<23)) != 0)
#define GET_FP_CAUSE(reg)    (((reg) >> 12) & 0x3f)
#define GET_FP_ENABLE(reg)   (((reg) >>  7) & 0x1f)
#define GET_FP_FLAGS(reg)    (((reg) >>  2) & 0x1f)
#define SET_FP_CAUSE(reg,v)  do { (reg) = ((reg) & ~(0x3f << 12)) | ((v) << 12); } while(0)
#define SET_FP_ENABLE(reg,v) do { (reg) = ((reg) & ~(0x1f <<  7)) | ((v) << 7); } while(0)
#define SET_FP_FLAGS(reg,v)  do { (reg) = ((reg) & ~(0x1f <<  2)) | ((v) << 2); } while(0)
#define FP_INEXACT        1
#define FP_UNDERFLOW      2
#define FP_OVERFLOW       4
#define FP_DIV0           8
#define FP_INVALID        16
#define FP_UNIMPLEMENTED  32
		
#endif
#if defined(MIPS_USES_R4K_TLB)
    tlb_t tlb[MIPS_TLB_NB];
#endif
    uint32_t CP0_index;
    uint32_t CP0_random;
    uint32_t CP0_EntryLo0;
    uint32_t CP0_EntryLo1;
    uint32_t CP0_Context;
    uint32_t CP0_PageMask;
    uint32_t CP0_Wired;
    uint32_t CP0_BadVAddr;
    uint32_t CP0_Count;
    uint32_t CP0_EntryHi;
    uint32_t CP0_Compare;
    uint32_t CP0_Status;
#define CP0St_CU3   31
#define CP0St_CU2   30
#define CP0St_CU1   29
#define CP0St_CU0   28
#define CP0St_RP    27
#define CP0St_FR    26
#define CP0St_RE    25
#define CP0St_BEV   22
#define CP0St_TS    21
#define CP0St_SR    20
#define CP0St_NMI   19
#define CP0St_IM    8
#define CP0St_UM    4
#define CP0St_ERL   2
#define CP0St_EXL   1
#define CP0St_IE    0
    uint32_t CP0_Cause;
#define CP0Ca_IV   23
    uint32_t CP0_EPC;
    uint32_t CP0_PRid;
    uint32_t CP0_Config0;
#define CP0C0_M    31
#define CP0C0_K23  28
#define CP0C0_KU   25
#define CP0C0_MDU  20
#define CP0C0_MM   17
#define CP0C0_BM   16
#define CP0C0_BE   15
#define CP0C0_AT   13
#define CP0C0_AR   10
#define CP0C0_MT   7
#define CP0C0_K0   0
    uint32_t CP0_Config1;
#define CP0C1_MMU  25
#define CP0C1_IS   22
#define CP0C1_IL   19
#define CP0C1_IA   16
#define CP0C1_DS   13
#define CP0C1_DL   10
#define CP0C1_DA   7
#define CP0C1_PC   4
#define CP0C1_WR   3
#define CP0C1_CA   2
#define CP0C1_EP   1
#define CP0C1_FP   0
    uint32_t CP0_LLAddr;
    uint32_t CP0_WatchLo;
    uint32_t CP0_WatchHi;
    uint32_t CP0_Debug;
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
    uint32_t CP0_DEPC;
    uint32_t CP0_TagLo;
    uint32_t CP0_DataLo;
    uint32_t CP0_ErrorEPC;
    uint32_t CP0_DESAVE;
    /* Qemu */
    struct QEMUTimer *timer; /* Internal timer */
    int interrupt_request;
    jmp_buf jmp_env;
    int exception_index;
    int error_code;
    int user_mode_only; /* user mode only simulation */
    uint32_t hflags;    /* CPU State */
    /* TMASK defines different execution modes */
#define MIPS_HFLAG_TMASK  0x007F
#define MIPS_HFLAG_MODE   0x001F /* execution modes                    */
#define MIPS_HFLAG_UM     0x0001 /* user mode                          */
#define MIPS_HFLAG_ERL    0x0002 /* Error mode                         */
#define MIPS_HFLAG_EXL    0x0004 /* Exception mode                     */
#define MIPS_HFLAG_DM     0x0008 /* Debug mode                         */
#define MIPS_HFLAG_SM     0x0010 /* Supervisor mode                    */
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

    int halted; /* TRUE if the CPU is in suspend state */

    CPU_COMMON
};

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
    EXCP_DDBS,
    EXCP_DWATCH,
    EXCP_LAE,
    EXCP_SAE, /* 24 */
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

#endif /* !defined (__MIPS_CPU_H__) */
