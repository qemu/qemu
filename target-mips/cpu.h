#if !defined (__MIPS_CPU_H__)
#define __MIPS_CPU_H__

#include "mips-defs.h"
#include "cpu-defs.h"
#include "config.h"
#include "softfloat.h"

typedef union fpr_t fpr_t;
union fpr_t {
    double d;
    float  f;
    uint32_t u[2];
};

#if defined(MIPS_USES_R4K_TLB)
typedef struct tlb_t tlb_t;
struct tlb_t {
    target_ulong VPN;
    target_ulong end;
    uint8_t ASID;
    uint8_t G;
    uint8_t C[2];
    uint8_t V[2];
    uint8_t D[2];
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
    /* Floating point special purpose registers */
    uint32_t fcr0;
    uint32_t fcr25;
    uint32_t fcr26;
    uint32_t fcr28;
    uint32_t fcsr;
#endif
#if defined(MIPS_USES_R4K_TLB)
    tlb_t tlb[16];
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
#if defined (USE_HOST_FLOAT_REGS) && defined(MIPS_USES_FPU)
    double ft0, ft1, ft2;
#endif
    struct QEMUTimer *timer; /* Internal timer */
    int interrupt_request;
    jmp_buf jmp_env;
    int exception_index;
    int error_code;
    int user_mode_only; /* user mode only simulation */
    uint32_t hflags;    /* CPU State */
    /* TMASK defines different execution modes */
#define MIPS_HFLAGS_TMASK 0x00FF
#define MIPS_HFLAG_MODE   0x001F /* execution modes                    */
#define MIPS_HFLAG_UM     0x0001 /* user mode                          */
#define MIPS_HFLAG_ERL    0x0002 /* Error mode                         */
#define MIPS_HFLAG_EXL    0x0004 /* Exception mode                     */
#define MIPS_HFLAG_DM     0x0008 /* Debug mode                         */
#define MIPS_HFLAG_SM     0x0010 /* Supervisor mode                    */
#define MIPS_HFLAG_RE     0x0040 /* Reversed endianness                */
#define MIPS_HFLAG_DS     0x0080 /* In / out of delay slot             */
    /* Those flags keep the branch state if the translation is interrupted
     * between the branch instruction and the delay slot
     */
#define MIPS_HFLAG_BMASK  0x0F00
#define MIPS_HFLAG_B      0x0100 /* Unconditional branch               */
#define MIPS_HFLAG_BC     0x0200 /* Conditional branch                 */
#define MIPS_HFLAG_BL     0x0400 /* Likely branch                      */
#define MIPS_HFLAG_BR     0x0800 /* branch to register (can't link TB) */
    target_ulong btarget;        /* Jump / branch target               */
    int bcond;                   /* Branch condition (if needed)       */
    struct TranslationBlock *current_tb; /* currently executing TB  */
    /* soft mmu support */
    /* in order to avoid passing too many arguments to the memory
       write helpers, we store some rarely used information in the CPU
       context) */
    target_ulong mem_write_pc; /* host pc at which the memory was
                                   written */
    unsigned long mem_write_vaddr; /* target virtual addr at which the
                                      memory was written */
    /* 0 = kernel, 1 = user (may have 2 = kernel code, 3 = user code ?) */
    CPUTLBEntry tlb_read[2][CPU_TLB_SIZE];
    CPUTLBEntry tlb_write[2][CPU_TLB_SIZE];
    /* ice debug support */
    target_ulong breakpoints[MAX_BREAKPOINTS];
    int nb_breakpoints;
    int singlestep_enabled; /* XXX: should use CPU single step mode instead */
    /* user data */
    void *opaque;
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
    EXCP_BREAK,
    EXCP_CpU, /* 16 */
    EXCP_RI,
    EXCP_OVERFLOW,
    EXCP_TRAP,
    EXCP_DDBS,
    EXCP_DWATCH,
    EXCP_LAE, /* 22 */
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

/* MIPS opcodes */
#define EXT_SPECIAL  0x100
#define EXT_SPECIAL2 0x200
#define EXT_REGIMM   0x300
#define EXT_CP0      0x400
#define EXT_CP1      0x500
#define EXT_CP2      0x600
#define EXT_CP3      0x700

enum {
    /* indirect opcode tables */
    OPC_SPECIAL  = 0x00,
    OPC_BREGIMM  = 0x01,
    OPC_CP0      = 0x10,
    OPC_CP1      = 0x11,
    OPC_CP2      = 0x12,
    OPC_CP3      = 0x13,
    OPC_SPECIAL2 = 0x1C,
    /* arithmetic with immediate */
    OPC_ADDI     = 0x08,
    OPC_ADDIU    = 0x09,
    OPC_SLTI     = 0x0A,
    OPC_SLTIU    = 0x0B,
    OPC_ANDI     = 0x0C,
    OPC_ORI      = 0x0D,
    OPC_XORI     = 0x0E,
    OPC_LUI      = 0x0F,
    /* Jump and branches */
    OPC_J        = 0x02,
    OPC_JAL      = 0x03,
    OPC_BEQ      = 0x04,  /* Unconditional if rs = rt = 0 (B) */
    OPC_BEQL     = 0x14,
    OPC_BNE      = 0x05,
    OPC_BNEL     = 0x15,
    OPC_BLEZ     = 0x06,
    OPC_BLEZL    = 0x16,
    OPC_BGTZ     = 0x07,
    OPC_BGTZL    = 0x17,
    OPC_JALX     = 0x1D,  /* MIPS 16 only */
    /* Load and stores */
    OPC_LB       = 0x20,
    OPC_LH       = 0x21,
    OPC_LWL      = 0x22,
    OPC_LW       = 0x23,
    OPC_LBU      = 0x24,
    OPC_LHU      = 0x25,
    OPC_LWR      = 0x26,
    OPC_SB       = 0x28,
    OPC_SH       = 0x29,
    OPC_SWL      = 0x2A,
    OPC_SW       = 0x2B,
    OPC_SWR      = 0x2E,
    OPC_LL       = 0x30,
    OPC_SC       = 0x38,
    /* Floating point load/store */
    OPC_LWC1     = 0x31,
    OPC_LWC2     = 0x32,
    OPC_LDC1     = 0x35,
    OPC_LDC2     = 0x36,
    OPC_SWC1     = 0x39,
    OPC_SWC2     = 0x3A,
    OPC_SDC1     = 0x3D,
    OPC_SDC2     = 0x3E,
    /* Cache and prefetch */
    OPC_CACHE    = 0x2F,
    OPC_PREF     = 0x33,
};

/* MIPS special opcodes */
enum {
    /* Shifts */
    OPC_SLL      = 0x00 | EXT_SPECIAL,
    /* NOP is SLL r0, r0, 0   */
    /* SSNOP is SLL r0, r0, 1 */
    OPC_SRL      = 0x02 | EXT_SPECIAL,
    OPC_SRA      = 0x03 | EXT_SPECIAL,
    OPC_SLLV     = 0x04 | EXT_SPECIAL,
    OPC_SRLV     = 0x06 | EXT_SPECIAL,
    OPC_SRAV     = 0x07 | EXT_SPECIAL,
    /* Multiplication / division */
    OPC_MULT     = 0x18 | EXT_SPECIAL,
    OPC_MULTU    = 0x19 | EXT_SPECIAL,
    OPC_DIV      = 0x1A | EXT_SPECIAL,
    OPC_DIVU     = 0x1B | EXT_SPECIAL,
    /* 2 registers arithmetic / logic */
    OPC_ADD      = 0x20 | EXT_SPECIAL,
    OPC_ADDU     = 0x21 | EXT_SPECIAL,
    OPC_SUB      = 0x22 | EXT_SPECIAL,
    OPC_SUBU     = 0x23 | EXT_SPECIAL,
    OPC_AND      = 0x24 | EXT_SPECIAL,
    OPC_OR       = 0x25 | EXT_SPECIAL,
    OPC_XOR      = 0x26 | EXT_SPECIAL,
    OPC_NOR      = 0x27 | EXT_SPECIAL,
    OPC_SLT      = 0x2A | EXT_SPECIAL,
    OPC_SLTU     = 0x2B | EXT_SPECIAL,
    /* Jumps */
    OPC_JR       = 0x08 | EXT_SPECIAL,
    OPC_JALR     = 0x09 | EXT_SPECIAL,
    /* Traps */
    OPC_TGE      = 0x30 | EXT_SPECIAL,
    OPC_TGEU     = 0x31 | EXT_SPECIAL,
    OPC_TLT      = 0x32 | EXT_SPECIAL,
    OPC_TLTU     = 0x33 | EXT_SPECIAL,
    OPC_TEQ      = 0x34 | EXT_SPECIAL,
    OPC_TNE      = 0x36 | EXT_SPECIAL,
    /* HI / LO registers load & stores */
    OPC_MFHI     = 0x10 | EXT_SPECIAL,
    OPC_MTHI     = 0x11 | EXT_SPECIAL,
    OPC_MFLO     = 0x12 | EXT_SPECIAL,
    OPC_MTLO     = 0x13 | EXT_SPECIAL,
    /* Conditional moves */
    OPC_MOVZ     = 0x0A | EXT_SPECIAL,
    OPC_MOVN     = 0x0B | EXT_SPECIAL,

    OPC_MOVCI    = 0x01 | EXT_SPECIAL,

    /* Special */
    OPC_PMON     = 0x05 | EXT_SPECIAL,
    OPC_SYSCALL  = 0x0C | EXT_SPECIAL,
    OPC_BREAK    = 0x0D | EXT_SPECIAL,
    OPC_SYNC     = 0x0F | EXT_SPECIAL,
};

enum {
    /* Mutiply & xxx operations */
    OPC_MADD     = 0x00 | EXT_SPECIAL2,
    OPC_MADDU    = 0x01 | EXT_SPECIAL2,
    OPC_MUL      = 0x02 | EXT_SPECIAL2,
    OPC_MSUB     = 0x04 | EXT_SPECIAL2,
    OPC_MSUBU    = 0x05 | EXT_SPECIAL2,
    /* Misc */
    OPC_CLZ      = 0x20 | EXT_SPECIAL2,
    OPC_CLO      = 0x21 | EXT_SPECIAL2,
    /* Special */
    OPC_SDBBP    = 0x3F | EXT_SPECIAL2,
};

/* Branch REGIMM */
enum {
    OPC_BLTZ     = 0x00 | EXT_REGIMM,
    OPC_BLTZL    = 0x02 | EXT_REGIMM,
    OPC_BGEZ     = 0x01 | EXT_REGIMM,
    OPC_BGEZL    = 0x03 | EXT_REGIMM,
    OPC_BLTZAL   = 0x10 | EXT_REGIMM,
    OPC_BLTZALL  = 0x12 | EXT_REGIMM,
    OPC_BGEZAL   = 0x11 | EXT_REGIMM,
    OPC_BGEZALL  = 0x13 | EXT_REGIMM,
    OPC_TGEI     = 0x08 | EXT_REGIMM,
    OPC_TGEIU    = 0x09 | EXT_REGIMM,
    OPC_TLTI     = 0x0A | EXT_REGIMM,
    OPC_TLTIU    = 0x0B | EXT_REGIMM,
    OPC_TEQI     = 0x0C | EXT_REGIMM,
    OPC_TNEI     = 0x0E | EXT_REGIMM,
};

enum {
    /* Coprocessor 0 (MMU) */
    OPC_MFC0     = 0x00 | EXT_CP0,
    OPC_MTC0     = 0x04 | EXT_CP0,
    OPC_TLBR     = 0x01 | EXT_CP0,
    OPC_TLBWI    = 0x02 | EXT_CP0,
    OPC_TLBWR    = 0x06 | EXT_CP0,
    OPC_TLBP     = 0x08 | EXT_CP0,
    OPC_ERET     = 0x18 | EXT_CP0,
    OPC_DERET    = 0x1F | EXT_CP0,
    OPC_WAIT     = 0x20 | EXT_CP0,
};

int cpu_mips_exec(CPUMIPSState *s);
CPUMIPSState *cpu_mips_init(void);
uint32_t cpu_mips_get_clock (void);

#endif /* !defined (__MIPS_CPU_H__) */
