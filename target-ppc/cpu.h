/*
 *  PowerPC emulation cpu definitions for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#if !defined (__CPU_PPC_H__)
#define __CPU_PPC_H__

#include "config.h"
#include <inttypes.h>

#if defined (TARGET_PPC64)
typedef uint64_t ppc_gpr_t;
#define TARGET_GPR_BITS  64
#define TARGET_LONG_BITS 64
#define REGX "%016" PRIx64
#define TARGET_PAGE_BITS 12
#elif defined(TARGET_PPCEMB)
/* BookE have 36 bits physical address space */
#define TARGET_PHYS_ADDR_BITS 64
/* GPR are 64 bits: used by vector extension */
typedef uint64_t ppc_gpr_t;
#define TARGET_GPR_BITS  64
#define TARGET_LONG_BITS 32
#define REGX "%016" PRIx64
#if defined(CONFIG_USER_ONLY)
/* It looks like a lot of Linux programs assume page size
 * is 4kB long. This is evil, but we have to deal with it...
 */
#define TARGET_PAGE_BITS 12
#else
/* Pages can be 1 kB small */
#define TARGET_PAGE_BITS 10
#endif
#else
#if (HOST_LONG_BITS >= 64)
/* When using 64 bits temporary registers,
 * we can use 64 bits GPR with no extra cost
 * It's even an optimization as it will prevent
 * the compiler to do unuseful masking in the micro-ops.
 */
typedef uint64_t ppc_gpr_t;
#define TARGET_GPR_BITS  64
#define REGX "%08" PRIx64
#else
typedef uint32_t ppc_gpr_t;
#define TARGET_GPR_BITS  32
#define REGX "%08" PRIx32
#endif
#define TARGET_LONG_BITS 32
#define TARGET_PAGE_BITS 12
#endif

#include "cpu-defs.h"

#define ADDRX TARGET_FMT_lx
#define PADDRX TARGET_FMT_plx

#include <setjmp.h>

#include "softfloat.h"

#define TARGET_HAS_ICE 1

#if defined (TARGET_PPC64)
#define ELF_MACHINE     EM_PPC64
#else
#define ELF_MACHINE     EM_PPC
#endif

/* XXX: this should be tunable: PowerPC 601 & 64 bits PowerPC
 *                              have different cache line sizes
 */
#define ICACHE_LINE_SIZE 32
#define DCACHE_LINE_SIZE 32

/*****************************************************************************/
/* MMU model                                                                 */
enum {
    POWERPC_MMU_UNKNOWN    = 0,
    /* Standard 32 bits PowerPC MMU                            */
    POWERPC_MMU_32B,
    /* Standard 64 bits PowerPC MMU                            */
    POWERPC_MMU_64B,
    /* PowerPC 601 MMU                                         */
    POWERPC_MMU_601,
    /* PowerPC 6xx MMU with software TLB                       */
    POWERPC_MMU_SOFT_6xx,
    /* PowerPC 74xx MMU with software TLB                      */
    POWERPC_MMU_SOFT_74xx,
    /* PowerPC 4xx MMU with software TLB                       */
    POWERPC_MMU_SOFT_4xx,
    /* PowerPC 4xx MMU with software TLB and zones protections */
    POWERPC_MMU_SOFT_4xx_Z,
    /* PowerPC 4xx MMU in real mode only                       */
    POWERPC_MMU_REAL_4xx,
    /* BookE MMU model                                         */
    POWERPC_MMU_BOOKE,
    /* BookE FSL MMU model                                     */
    POWERPC_MMU_BOOKE_FSL,
    /* 64 bits "bridge" PowerPC MMU                            */
    POWERPC_MMU_64BRIDGE,
};

/*****************************************************************************/
/* Exception model                                                           */
enum {
    POWERPC_EXCP_UNKNOWN   = 0,
    /* Standard PowerPC exception model */
    POWERPC_EXCP_STD,
    /* PowerPC 40x exception model      */
    POWERPC_EXCP_40x,
    /* PowerPC 601 exception model      */
    POWERPC_EXCP_601,
    /* PowerPC 602 exception model      */
    POWERPC_EXCP_602,
    /* PowerPC 603 exception model      */
    POWERPC_EXCP_603,
    /* PowerPC 603e exception model     */
    POWERPC_EXCP_603E,
    /* PowerPC G2 exception model       */
    POWERPC_EXCP_G2,
    /* PowerPC 604 exception model      */
    POWERPC_EXCP_604,
    /* PowerPC 7x0 exception model      */
    POWERPC_EXCP_7x0,
    /* PowerPC 7x5 exception model      */
    POWERPC_EXCP_7x5,
    /* PowerPC 74xx exception model     */
    POWERPC_EXCP_74xx,
    /* PowerPC 970 exception model      */
    POWERPC_EXCP_970,
    /* BookE exception model            */
    POWERPC_EXCP_BOOKE,
};

/*****************************************************************************/
/* Exception vectors definitions                                             */
enum {
    POWERPC_EXCP_NONE    = -1,
    /* The 64 first entries are used by the PowerPC embedded specification   */
    POWERPC_EXCP_CRITICAL = 0,  /* Critical input                            */
    POWERPC_EXCP_MCHECK   = 1,  /* Machine check exception                   */
    POWERPC_EXCP_DSI      = 2,  /* Data storage exception                    */
    POWERPC_EXCP_ISI      = 3,  /* Instruction storage exception             */
    POWERPC_EXCP_EXTERNAL = 4,  /* External input                            */
    POWERPC_EXCP_ALIGN    = 5,  /* Alignment exception                       */
    POWERPC_EXCP_PROGRAM  = 6,  /* Program exception                         */
    POWERPC_EXCP_FPU      = 7,  /* Floating-point unavailable exception      */
    POWERPC_EXCP_SYSCALL  = 8,  /* System call exception                     */
    POWERPC_EXCP_APU      = 9,  /* Auxiliary processor unavailable           */
    POWERPC_EXCP_DECR     = 10, /* Decrementer exception                     */
    POWERPC_EXCP_FIT      = 11, /* Fixed-interval timer interrupt            */
    POWERPC_EXCP_WDT      = 12, /* Watchdog timer interrupt                  */
    POWERPC_EXCP_DTLB     = 13, /* Data TLB error                            */
    POWERPC_EXCP_ITLB     = 14, /* Instruction TLB error                     */
    POWERPC_EXCP_DEBUG    = 15, /* Debug interrupt                           */
    /* Vectors 16 to 31 are reserved                                         */
#if defined(TARGET_PPCEMB)
    POWERPC_EXCP_SPEU     = 32, /* SPE/embedded floating-point unavailable   */
    POWERPC_EXCP_EFPDI    = 33, /* Embedded floating-point data interrupt    */
    POWERPC_EXCP_EFPRI    = 34, /* Embedded floating-point round interrupt   */
    POWERPC_EXCP_EPERFM   = 35, /* Embedded performance monitor interrupt    */
    POWERPC_EXCP_DOORI    = 36, /* Embedded doorbell interrupt               */
    POWERPC_EXCP_DOORCI   = 37, /* Embedded doorbell critical interrupt      */
#endif /* defined(TARGET_PPCEMB) */
    /* Vectors 38 to 63 are reserved                                         */
    /* Exceptions defined in the PowerPC server specification                */
    POWERPC_EXCP_RESET    = 64, /* System reset exception                    */
#if defined(TARGET_PPC64) /* PowerPC 64 */
    POWERPC_EXCP_DSEG     = 65, /* Data segment exception                    */
    POWERPC_EXCP_ISEG     = 66, /* Instruction segment exception             */
#endif /* defined(TARGET_PPC64) */
#if defined(TARGET_PPC64H) /* PowerPC 64 with hypervisor mode support */
    POWERPC_EXCP_HDECR    = 67, /* Hypervisor decrementer exception          */
#endif /* defined(TARGET_PPC64H) */
    POWERPC_EXCP_TRACE    = 68, /* Trace exception                           */
#if defined(TARGET_PPC64H) /* PowerPC 64 with hypervisor mode support */
    POWERPC_EXCP_HDSI     = 69, /* Hypervisor data storage exception         */
    POWERPC_EXCP_HISI     = 70, /* Hypervisor instruction storage exception  */
    POWERPC_EXCP_HDSEG    = 71, /* Hypervisor data segment exception         */
    POWERPC_EXCP_HISEG    = 72, /* Hypervisor instruction segment exception  */
#endif /* defined(TARGET_PPC64H) */
    POWERPC_EXCP_VPU      = 73, /* Vector unavailable exception              */
    /* 40x specific exceptions                                               */
    POWERPC_EXCP_PIT      = 74, /* Programmable interval timer interrupt     */
    /* 601 specific exceptions                                               */
    POWERPC_EXCP_IO       = 75, /* IO error exception                        */
    POWERPC_EXCP_RUNM     = 76, /* Run mode exception                        */
    /* 602 specific exceptions                                               */
    POWERPC_EXCP_EMUL     = 77, /* Emulation trap exception                  */
    /* 602/603 specific exceptions                                           */
    POWERPC_EXCP_IFTLB    = 78, /* Instruction fetch TLB error               */
    POWERPC_EXCP_DLTLB    = 79, /* Data load TLB miss                        */
    POWERPC_EXCP_DSTLB    = 80, /* Data store TLB miss                       */
    /* Exceptions available on most PowerPC                                  */
    POWERPC_EXCP_FPA      = 81, /* Floating-point assist exception           */
    POWERPC_EXCP_IABR     = 82, /* Instruction address breakpoint            */
    POWERPC_EXCP_SMI      = 83, /* System management interrupt               */
    POWERPC_EXCP_PERFM    = 84, /* Embedded performance monitor interrupt    */
    /* 7xx/74xx specific exceptions                                          */
    POWERPC_EXCP_THERM    = 85, /* Thermal interrupt                         */
    /* 74xx specific exceptions                                              */
    POWERPC_EXCP_VPUA     = 86, /* Vector assist exception                   */
    /* 970FX specific exceptions                                             */
    POWERPC_EXCP_SOFTP    = 87, /* Soft patch exception                      */
    POWERPC_EXCP_MAINT    = 88, /* Maintenance exception                     */
    /* EOL                                                                   */
    POWERPC_EXCP_NB       = 96,
    /* Qemu exceptions: used internally during code translation              */
    POWERPC_EXCP_STOP         = 0x200, /* stop translation                   */
    POWERPC_EXCP_BRANCH       = 0x201, /* branch instruction                 */
    /* Qemu exceptions: special cases we want to stop translation            */
    POWERPC_EXCP_SYNC         = 0x202, /* context synchronizing instruction  */
    POWERPC_EXCP_SYSCALL_USER = 0x203, /* System call in user mode only      */
};


/* Exceptions error codes                                                    */
enum {
    /* Exception subtypes for POWERPC_EXCP_ALIGN                             */
    POWERPC_EXCP_ALIGN_FP      = 0x01,  /* FP alignment exception            */
    POWERPC_EXCP_ALIGN_LST     = 0x02,  /* Unaligned mult/extern load/store  */
    POWERPC_EXCP_ALIGN_LE      = 0x03,  /* Multiple little-endian access     */
    POWERPC_EXCP_ALIGN_PROT    = 0x04,  /* Access cross protection boundary  */
    POWERPC_EXCP_ALIGN_BAT     = 0x05,  /* Access cross a BAT/seg boundary   */
    POWERPC_EXCP_ALIGN_CACHE   = 0x06,  /* Impossible dcbz access            */
    /* Exception subtypes for POWERPC_EXCP_PROGRAM                           */
    /* FP exceptions                                                         */
    POWERPC_EXCP_FP            = 0x10,
    POWERPC_EXCP_FP_OX         = 0x01,  /* FP overflow                       */
    POWERPC_EXCP_FP_UX         = 0x02,  /* FP underflow                      */
    POWERPC_EXCP_FP_ZX         = 0x03,  /* FP divide by zero                 */
    POWERPC_EXCP_FP_XX         = 0x04,  /* FP inexact                        */
    POWERPC_EXCP_FP_VXNAN      = 0x05,  /* FP invalid SNaN op                */
    POWERPC_EXCP_FP_VXISI      = 0x06,  /* FP invalid infinite subtraction   */
    POWERPC_EXCP_FP_VXIDI      = 0x07,  /* FP invalid infinite divide        */
    POWERPC_EXCP_FP_VXZDZ      = 0x08,  /* FP invalid zero divide            */
    POWERPC_EXCP_FP_VXIMZ      = 0x09,  /* FP invalid infinite * zero        */
    POWERPC_EXCP_FP_VXVC       = 0x0A,  /* FP invalid compare                */
    POWERPC_EXCP_FP_VXSOFT     = 0x0B,  /* FP invalid operation              */
    POWERPC_EXCP_FP_VXSQRT     = 0x0C,  /* FP invalid square root            */
    POWERPC_EXCP_FP_VXCVI      = 0x0D,  /* FP invalid integer conversion     */
    /* Invalid instruction                                                   */
    POWERPC_EXCP_INVAL         = 0x20,
    POWERPC_EXCP_INVAL_INVAL   = 0x01,  /* Invalid instruction               */
    POWERPC_EXCP_INVAL_LSWX    = 0x02,  /* Invalid lswx instruction          */
    POWERPC_EXCP_INVAL_SPR     = 0x03,  /* Invalid SPR access                */
    POWERPC_EXCP_INVAL_FP      = 0x04,  /* Unimplemented mandatory fp instr  */
    /* Privileged instruction                                                */
    POWERPC_EXCP_PRIV          = 0x30,
    POWERPC_EXCP_PRIV_OPC      = 0x01,  /* Privileged operation exception    */
    POWERPC_EXCP_PRIV_REG      = 0x02,  /* Privileged register exception     */
    /* Trap                                                                  */
    POWERPC_EXCP_TRAP          = 0x40,
};

/*****************************************************************************/
/* Input pins model                                                          */
enum {
    PPC_FLAGS_INPUT_UNKNOWN = 0,
    /* PowerPC 6xx bus                  */
    PPC_FLAGS_INPUT_6xx,
    /* BookE bus                        */
    PPC_FLAGS_INPUT_BookE,
    /* PowerPC 405 bus                  */
    PPC_FLAGS_INPUT_405,
    /* PowerPC 970 bus                  */
    PPC_FLAGS_INPUT_970,
    /* PowerPC 401 bus                  */
    PPC_FLAGS_INPUT_401,
};

#define PPC_INPUT(env) (env->bus_model)

/*****************************************************************************/
typedef struct ppc_def_t ppc_def_t;
typedef struct opc_handler_t opc_handler_t;

/*****************************************************************************/
/* Types used to describe some PowerPC registers */
typedef struct CPUPPCState CPUPPCState;
typedef struct ppc_tb_t ppc_tb_t;
typedef struct ppc_spr_t ppc_spr_t;
typedef struct ppc_dcr_t ppc_dcr_t;
typedef struct ppc_avr_t ppc_avr_t;
typedef union ppc_tlb_t ppc_tlb_t;

/* SPR access micro-ops generations callbacks */
struct ppc_spr_t {
    void (*uea_read)(void *opaque, int spr_num);
    void (*uea_write)(void *opaque, int spr_num);
#if !defined(CONFIG_USER_ONLY)
    void (*oea_read)(void *opaque, int spr_num);
    void (*oea_write)(void *opaque, int spr_num);
#if defined(TARGET_PPC64H)
    void (*hea_read)(void *opaque, int spr_num);
    void (*hea_write)(void *opaque, int spr_num);
#endif
#endif
    const unsigned char *name;
};

/* Altivec registers (128 bits) */
struct ppc_avr_t {
    uint32_t u[4];
};

/* Software TLB cache */
typedef struct ppc6xx_tlb_t ppc6xx_tlb_t;
struct ppc6xx_tlb_t {
    target_ulong pte0;
    target_ulong pte1;
    target_ulong EPN;
};

typedef struct ppcemb_tlb_t ppcemb_tlb_t;
struct ppcemb_tlb_t {
    target_phys_addr_t RPN;
    target_ulong EPN;
    target_ulong PID;
    target_ulong size;
    uint32_t prot;
    uint32_t attr; /* Storage attributes */
};

union ppc_tlb_t {
    ppc6xx_tlb_t tlb6;
    ppcemb_tlb_t tlbe;
};

/*****************************************************************************/
/* Machine state register bits definition                                    */
#define MSR_SF   63 /* Sixty-four-bit mode                            hflags */
#define MSR_ISF  61 /* Sixty-four-bit interrupt mode on 630                  */
#define MSR_HV   60 /* hypervisor state                               hflags */
#define MSR_CM   31 /* Computation mode for BookE                     hflags */
#define MSR_ICM  30 /* Interrupt computation mode for BookE                  */
#define MSR_UCLE 26 /* User-mode cache lock enable for BookE                 */
#define MSR_VR   25 /* altivec available                              hflags */
#define MSR_SPE  25 /* SPE enable for BookE                           hflags */
#define MSR_AP   23 /* Access privilege state on 602                  hflags */
#define MSR_SA   22 /* Supervisor access mode on 602                  hflags */
#define MSR_KEY  19 /* key bit on 603e                                       */
#define MSR_POW  18 /* Power management                                      */
#define MSR_WE   18 /* Wait state enable on embedded PowerPC                 */
#define MSR_TGPR 17 /* TGPR usage on 602/603                                 */
#define MSR_TLB  17 /* TLB update on ?                                       */
#define MSR_CE   17 /* Critical interrupt enable on embedded PowerPC         */
#define MSR_ILE  16 /* Interrupt little-endian mode                          */
#define MSR_EE   15 /* External interrupt enable                             */
#define MSR_PR   14 /* Problem state                                  hflags */
#define MSR_FP   13 /* Floating point available                       hflags */
#define MSR_ME   12 /* Machine check interrupt enable                        */
#define MSR_FE0  11 /* Floating point exception mode 0                hflags */
#define MSR_SE   10 /* Single-step trace enable                       hflags */
#define MSR_DWE  10 /* Debug wait enable on 405                              */
#define MSR_UBLE 10 /* User BTB lock enable on e500                          */
#define MSR_BE   9  /* Branch trace enable                            hflags */
#define MSR_DE   9  /* Debug interrupts enable on embedded PowerPC           */
#define MSR_FE1  8  /* Floating point exception mode 1                hflags */
#define MSR_AL   7  /* AL bit on POWER                                       */
#define MSR_IP   6  /* Interrupt prefix                                      */
#define MSR_IR   5  /* Instruction relocate                                  */
#define MSR_IS   5  /* Instruction address space on embedded PowerPC         */
#define MSR_DR   4  /* Data relocate                                         */
#define MSR_DS   4  /* Data address space on embedded PowerPC                */
#define MSR_PE   3  /* Protection enable on 403                              */
#define MSR_EP   3  /* Exception prefix on 601                               */
#define MSR_PX   2  /* Protection exclusive on 403                           */
#define MSR_PMM  2  /* Performance monitor mark on POWER                     */
#define MSR_RI   1  /* Recoverable interrupt                                 */
#define MSR_LE   0  /* Little-endian mode                             hflags */
#define msr_sf   env->msr[MSR_SF]
#define msr_isf  env->msr[MSR_ISF]
#define msr_hv   env->msr[MSR_HV]
#define msr_cm   env->msr[MSR_CM]
#define msr_icm  env->msr[MSR_ICM]
#define msr_ucle env->msr[MSR_UCLE]
#define msr_vr   env->msr[MSR_VR]
#define msr_spe  env->msr[MSR_SPE]
#define msr_ap   env->msr[MSR_AP]
#define msr_sa   env->msr[MSR_SA]
#define msr_key  env->msr[MSR_KEY]
#define msr_pow  env->msr[MSR_POW]
#define msr_we   env->msr[MSR_WE]
#define msr_tgpr env->msr[MSR_TGPR]
#define msr_tlb  env->msr[MSR_TLB]
#define msr_ce   env->msr[MSR_CE]
#define msr_ile  env->msr[MSR_ILE]
#define msr_ee   env->msr[MSR_EE]
#define msr_pr   env->msr[MSR_PR]
#define msr_fp   env->msr[MSR_FP]
#define msr_me   env->msr[MSR_ME]
#define msr_fe0  env->msr[MSR_FE0]
#define msr_se   env->msr[MSR_SE]
#define msr_dwe  env->msr[MSR_DWE]
#define msr_uble env->msr[MSR_UBLE]
#define msr_be   env->msr[MSR_BE]
#define msr_de   env->msr[MSR_DE]
#define msr_fe1  env->msr[MSR_FE1]
#define msr_al   env->msr[MSR_AL]
#define msr_ip   env->msr[MSR_IP]
#define msr_ir   env->msr[MSR_IR]
#define msr_is   env->msr[MSR_IS]
#define msr_dr   env->msr[MSR_DR]
#define msr_ds   env->msr[MSR_DS]
#define msr_pe   env->msr[MSR_PE]
#define msr_ep   env->msr[MSR_EP]
#define msr_px   env->msr[MSR_PX]
#define msr_pmm  env->msr[MSR_PMM]
#define msr_ri   env->msr[MSR_RI]
#define msr_le   env->msr[MSR_LE]

/*****************************************************************************/
/* The whole PowerPC CPU context */
struct CPUPPCState {
    /* First are the most commonly used resources
     * during translated code execution
     */
#if TARGET_GPR_BITS > HOST_LONG_BITS
    /* temporary fixed-point registers
     * used to emulate 64 bits target on 32 bits hosts
     */
    ppc_gpr_t t0, t1, t2;
#endif
    ppc_avr_t t0_avr, t1_avr, t2_avr;

    /* general purpose registers */
    ppc_gpr_t gpr[32];
    /* LR */
    target_ulong lr;
    /* CTR */
    target_ulong ctr;
    /* condition register */
    uint8_t crf[8];
    /* XER */
    /* XXX: We use only 5 fields, but we want to keep the structure aligned */
    uint8_t xer[8];
    /* Reservation address */
    target_ulong reserve;

    /* Those ones are used in supervisor mode only */
    /* machine state register */
    uint8_t msr[64];
    /* temporary general purpose registers */
    ppc_gpr_t tgpr[4]; /* Used to speed-up TLB assist handlers */

    /* Floating point execution context */
    /* temporary float registers */
    float64 ft0;
    float64 ft1;
    float64 ft2;
    float_status fp_status;
    /* floating point registers */
    float64 fpr[32];
    /* floating point status and control register */
    uint8_t fpscr[8];

    CPU_COMMON

    int halted; /* TRUE if the CPU is in suspend state */

    int access_type; /* when a memory exception occurs, the access
                        type is stored here */

    /* MMU context */
    /* Address space register */
    target_ulong asr;
    /* segment registers */
    target_ulong sdr1;
    target_ulong sr[16];
    /* BATs */
    int nb_BATs;
    target_ulong DBAT[2][8];
    target_ulong IBAT[2][8];

    /* Other registers */
    /* Special purpose registers */
    target_ulong spr[1024];
    /* Altivec registers */
    ppc_avr_t avr[32];
    uint32_t vscr;
    /* SPE registers */
    ppc_gpr_t spe_acc;
    float_status spe_status;
    uint32_t spe_fscr;

    /* Internal devices resources */
    /* Time base and decrementer */
    ppc_tb_t *tb_env;
    /* Device control registers */
    ppc_dcr_t *dcr_env;

    /* PowerPC TLB registers (for 4xx and 60x software driven TLBs) */
    int nb_tlb;      /* Total number of TLB                                  */
    int tlb_per_way; /* Speed-up helper: used to avoid divisions at run time */
    int nb_ways;     /* Number of ways in the TLB set                        */
    int last_way;    /* Last used way used to allocate TLB in a LRU way      */
    int id_tlbs;     /* If 1, MMU has separated TLBs for instructions & data */
    int nb_pids;     /* Number of available PID registers                    */
    ppc_tlb_t *tlb;  /* TLB is optional. Allocate them only if needed        */
    /* 403 dedicated access protection registers */
    target_ulong pb[4];

    /* Those resources are used during exception processing */
    /* CPU model definition */
    target_ulong msr_mask;
    uint8_t mmu_model;
    uint8_t excp_model;
    uint8_t bus_model;
    uint8_t pad;
    int bfd_mach;
    uint32_t flags;

    int exception_index;
    int error_code;
    int interrupt_request;
    uint32_t pending_interrupts;
#if !defined(CONFIG_USER_ONLY)
    /* This is the IRQ controller, which is implementation dependant
     * and only relevant when emulating a complete machine.
     */
    uint32_t irq_input_state;
    void **irq_inputs;
    /* Exception vectors */
    target_ulong excp_vectors[POWERPC_EXCP_NB];
    target_ulong excp_prefix;
    target_ulong ivor_mask;
    target_ulong ivpr_mask;
#endif

    /* Those resources are used only during code translation */
    /* Next instruction pointer */
    target_ulong nip;
    /* SPR translation callbacks */
    ppc_spr_t spr_cb[1024];
    /* opcode handlers */
    opc_handler_t *opcodes[0x40];

    /* Those resources are used only in Qemu core */
    jmp_buf jmp_env;
    int user_mode_only; /* user mode only simulation */
    target_ulong hflags; /* hflags is a MSR & HFLAGS_MASK */

    /* Power management */
    int power_mode;

    /* temporary hack to handle OSI calls (only used if non NULL) */
    int (*osi_call)(struct CPUPPCState *env);
};

/* Context used internally during MMU translations */
typedef struct mmu_ctx_t mmu_ctx_t;
struct mmu_ctx_t {
    target_phys_addr_t raddr;      /* Real address              */
    int prot;                      /* Protection bits           */
    target_phys_addr_t pg_addr[2]; /* PTE tables base addresses */
    target_ulong ptem;             /* Virtual segment ID | API  */
    int key;                       /* Access key                */
};

/*****************************************************************************/
CPUPPCState *cpu_ppc_init (void);
int cpu_ppc_exec (CPUPPCState *s);
void cpu_ppc_close (CPUPPCState *s);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_ppc_signal_handler (int host_signum, void *pinfo,
                            void *puc);

void do_interrupt (CPUPPCState *env);
void ppc_hw_interrupt (CPUPPCState *env);
void cpu_loop_exit (void);

void dump_stack (CPUPPCState *env);

#if !defined(CONFIG_USER_ONLY)
target_ulong do_load_ibatu (CPUPPCState *env, int nr);
target_ulong do_load_ibatl (CPUPPCState *env, int nr);
void do_store_ibatu (CPUPPCState *env, int nr, target_ulong value);
void do_store_ibatl (CPUPPCState *env, int nr, target_ulong value);
target_ulong do_load_dbatu (CPUPPCState *env, int nr);
target_ulong do_load_dbatl (CPUPPCState *env, int nr);
void do_store_dbatu (CPUPPCState *env, int nr, target_ulong value);
void do_store_dbatl (CPUPPCState *env, int nr, target_ulong value);
target_ulong do_load_sdr1 (CPUPPCState *env);
void do_store_sdr1 (CPUPPCState *env, target_ulong value);
#if defined(TARGET_PPC64)
target_ulong ppc_load_asr (CPUPPCState *env);
void ppc_store_asr (CPUPPCState *env, target_ulong value);
#endif
target_ulong do_load_sr (CPUPPCState *env, int srnum);
void do_store_sr (CPUPPCState *env, int srnum, target_ulong value);
#endif
target_ulong ppc_load_xer (CPUPPCState *env);
void ppc_store_xer (CPUPPCState *env, target_ulong value);
target_ulong do_load_msr (CPUPPCState *env);
void do_store_msr (CPUPPCState *env, target_ulong value);
#if defined(TARGET_PPC64)
void ppc_store_msr_32 (CPUPPCState *env, uint32_t value);
#endif

void do_compute_hflags (CPUPPCState *env);
void cpu_ppc_reset (void *opaque);
CPUPPCState *cpu_ppc_init (void);
void cpu_ppc_close(CPUPPCState *env);

int ppc_find_by_name (const unsigned char *name, ppc_def_t **def);
int ppc_find_by_pvr (uint32_t apvr, ppc_def_t **def);
void ppc_cpu_list (FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt, ...));
int cpu_ppc_register (CPUPPCState *env, ppc_def_t *def);

/* Time-base and decrementer management */
#ifndef NO_CPU_IO_DEFS
uint32_t cpu_ppc_load_tbl (CPUPPCState *env);
uint32_t cpu_ppc_load_tbu (CPUPPCState *env);
void cpu_ppc_store_tbu (CPUPPCState *env, uint32_t value);
void cpu_ppc_store_tbl (CPUPPCState *env, uint32_t value);
uint32_t cpu_ppc_load_atbl (CPUPPCState *env);
uint32_t cpu_ppc_load_atbu (CPUPPCState *env);
void cpu_ppc_store_atbl (CPUPPCState *env, uint32_t value);
void cpu_ppc_store_atbu (CPUPPCState *env, uint32_t value);
uint32_t cpu_ppc_load_decr (CPUPPCState *env);
void cpu_ppc_store_decr (CPUPPCState *env, uint32_t value);
#if defined(TARGET_PPC64H)
uint32_t cpu_ppc_load_hdecr (CPUPPCState *env);
void cpu_ppc_store_hdecr (CPUPPCState *env, uint32_t value);
uint64_t cpu_ppc_load_purr (CPUPPCState *env);
void cpu_ppc_store_purr (CPUPPCState *env, uint64_t value);
#endif
uint32_t cpu_ppc601_load_rtcl (CPUPPCState *env);
uint32_t cpu_ppc601_load_rtcu (CPUPPCState *env);
#if !defined(CONFIG_USER_ONLY)
void cpu_ppc601_store_rtcl (CPUPPCState *env, uint32_t value);
void cpu_ppc601_store_rtcu (CPUPPCState *env, uint32_t value);
target_ulong load_40x_pit (CPUPPCState *env);
void store_40x_pit (CPUPPCState *env, target_ulong val);
void store_40x_dbcr0 (CPUPPCState *env, uint32_t val);
void store_40x_sler (CPUPPCState *env, uint32_t val);
void store_booke_tcr (CPUPPCState *env, target_ulong val);
void store_booke_tsr (CPUPPCState *env, target_ulong val);
void ppc_tlb_invalidate_all (CPUPPCState *env);
void ppc_tlb_invalidate_one (CPUPPCState *env, target_ulong addr);
#if defined(TARGET_PPC64)
void ppc_slb_invalidate_all (CPUPPCState *env);
void ppc_slb_invalidate_one (CPUPPCState *env, uint64_t T0);
#endif
int ppcemb_tlb_search (CPUPPCState *env, target_ulong address, uint32_t pid);
#endif
#endif

/* Device control registers */
int ppc_dcr_read (ppc_dcr_t *dcr_env, int dcrn, target_ulong *valp);
int ppc_dcr_write (ppc_dcr_t *dcr_env, int dcrn, target_ulong val);

#define CPUState CPUPPCState
#define cpu_init cpu_ppc_init
#define cpu_exec cpu_ppc_exec
#define cpu_gen_code cpu_ppc_gen_code
#define cpu_signal_handler cpu_ppc_signal_handler

#include "cpu-all.h"

/*****************************************************************************/
/* Registers definitions */
#define XER_SO 31
#define XER_OV 30
#define XER_CA 29
#define XER_CMP 8
#define XER_BC  0
#define xer_so  env->xer[4]
#define xer_ov  env->xer[6]
#define xer_ca  env->xer[2]
#define xer_cmp env->xer[1]
#define xer_bc  env->xer[0]

/* SPR definitions */
#define SPR_MQ           (0x000)
#define SPR_XER          (0x001)
#define SPR_601_VRTCU    (0x004)
#define SPR_601_VRTCL    (0x005)
#define SPR_601_UDECR    (0x006)
#define SPR_LR           (0x008)
#define SPR_CTR          (0x009)
#define SPR_DSISR        (0x012)
#define SPR_DAR          (0x013) /* DAE for PowerPC 601 */
#define SPR_601_RTCU     (0x014)
#define SPR_601_RTCL     (0x015)
#define SPR_DECR         (0x016)
#define SPR_SDR1         (0x019)
#define SPR_SRR0         (0x01A)
#define SPR_SRR1         (0x01B)
#define SPR_AMR          (0x01D)
#define SPR_BOOKE_PID    (0x030)
#define SPR_BOOKE_DECAR  (0x036)
#define SPR_BOOKE_CSRR0  (0x03A)
#define SPR_BOOKE_CSRR1  (0x03B)
#define SPR_BOOKE_DEAR   (0x03D)
#define SPR_BOOKE_ESR    (0x03E)
#define SPR_BOOKE_IVPR   (0x03F)
#define SPR_8xx_EIE      (0x050)
#define SPR_8xx_EID      (0x051)
#define SPR_8xx_NRE      (0x052)
#define SPR_CTRL         (0x088)
#define SPR_58x_CMPA     (0x090)
#define SPR_58x_CMPB     (0x091)
#define SPR_58x_CMPC     (0x092)
#define SPR_58x_CMPD     (0x093)
#define SPR_58x_ICR      (0x094)
#define SPR_58x_DER      (0x094)
#define SPR_58x_COUNTA   (0x096)
#define SPR_58x_COUNTB   (0x097)
#define SPR_UCTRL        (0x098)
#define SPR_58x_CMPE     (0x098)
#define SPR_58x_CMPF     (0x099)
#define SPR_58x_CMPG     (0x09A)
#define SPR_58x_CMPH     (0x09B)
#define SPR_58x_LCTRL1   (0x09C)
#define SPR_58x_LCTRL2   (0x09D)
#define SPR_58x_ICTRL    (0x09E)
#define SPR_58x_BAR      (0x09F)
#define SPR_VRSAVE       (0x100)
#define SPR_USPRG0       (0x100)
#define SPR_USPRG1       (0x101)
#define SPR_USPRG2       (0x102)
#define SPR_USPRG3       (0x103)
#define SPR_USPRG4       (0x104)
#define SPR_USPRG5       (0x105)
#define SPR_USPRG6       (0x106)
#define SPR_USPRG7       (0x107)
#define SPR_VTBL         (0x10C)
#define SPR_VTBU         (0x10D)
#define SPR_SPRG0        (0x110)
#define SPR_SPRG1        (0x111)
#define SPR_SPRG2        (0x112)
#define SPR_SPRG3        (0x113)
#define SPR_SPRG4        (0x114)
#define SPR_SCOMC        (0x114)
#define SPR_SPRG5        (0x115)
#define SPR_SCOMD        (0x115)
#define SPR_SPRG6        (0x116)
#define SPR_SPRG7        (0x117)
#define SPR_ASR          (0x118)
#define SPR_EAR          (0x11A)
#define SPR_TBL          (0x11C)
#define SPR_TBU          (0x11D)
#define SPR_TBU40        (0x11E)
#define SPR_SVR          (0x11E)
#define SPR_BOOKE_PIR    (0x11E)
#define SPR_PVR          (0x11F)
#define SPR_HSPRG0       (0x130)
#define SPR_BOOKE_DBSR   (0x130)
#define SPR_HSPRG1       (0x131)
#define SPR_HDSISR       (0x132)
#define SPR_HDAR         (0x133)
#define SPR_BOOKE_DBCR0  (0x134)
#define SPR_IBCR         (0x135)
#define SPR_PURR         (0x135)
#define SPR_BOOKE_DBCR1  (0x135)
#define SPR_DBCR         (0x136)
#define SPR_HDEC         (0x136)
#define SPR_BOOKE_DBCR2  (0x136)
#define SPR_HIOR         (0x137)
#define SPR_MBAR         (0x137)
#define SPR_RMOR         (0x138)
#define SPR_BOOKE_IAC1   (0x138)
#define SPR_HRMOR        (0x139)
#define SPR_BOOKE_IAC2   (0x139)
#define SPR_HSRR0        (0x13A)
#define SPR_BOOKE_IAC3   (0x13A)
#define SPR_HSRR1        (0x13B)
#define SPR_BOOKE_IAC4   (0x13B)
#define SPR_LPCR         (0x13C)
#define SPR_BOOKE_DAC1   (0x13C)
#define SPR_LPIDR        (0x13D)
#define SPR_DABR2        (0x13D)
#define SPR_BOOKE_DAC2   (0x13D)
#define SPR_BOOKE_DVC1   (0x13E)
#define SPR_BOOKE_DVC2   (0x13F)
#define SPR_BOOKE_TSR    (0x150)
#define SPR_BOOKE_TCR    (0x154)
#define SPR_BOOKE_IVOR0  (0x190)
#define SPR_BOOKE_IVOR1  (0x191)
#define SPR_BOOKE_IVOR2  (0x192)
#define SPR_BOOKE_IVOR3  (0x193)
#define SPR_BOOKE_IVOR4  (0x194)
#define SPR_BOOKE_IVOR5  (0x195)
#define SPR_BOOKE_IVOR6  (0x196)
#define SPR_BOOKE_IVOR7  (0x197)
#define SPR_BOOKE_IVOR8  (0x198)
#define SPR_BOOKE_IVOR9  (0x199)
#define SPR_BOOKE_IVOR10 (0x19A)
#define SPR_BOOKE_IVOR11 (0x19B)
#define SPR_BOOKE_IVOR12 (0x19C)
#define SPR_BOOKE_IVOR13 (0x19D)
#define SPR_BOOKE_IVOR14 (0x19E)
#define SPR_BOOKE_IVOR15 (0x19F)
#define SPR_BOOKE_SPEFSCR (0x200)
#define SPR_E500_BBEAR   (0x201)
#define SPR_E500_BBTAR   (0x202)
#define SPR_ATBL         (0x20E)
#define SPR_ATBU         (0x20F)
#define SPR_IBAT0U       (0x210)
#define SPR_BOOKE_IVOR32 (0x210)
#define SPR_IBAT0L       (0x211)
#define SPR_BOOKE_IVOR33 (0x211)
#define SPR_IBAT1U       (0x212)
#define SPR_BOOKE_IVOR34 (0x212)
#define SPR_IBAT1L       (0x213)
#define SPR_BOOKE_IVOR35 (0x213)
#define SPR_IBAT2U       (0x214)
#define SPR_BOOKE_IVOR36 (0x214)
#define SPR_IBAT2L       (0x215)
#define SPR_E500_L1CFG0  (0x215)
#define SPR_BOOKE_IVOR37 (0x215)
#define SPR_IBAT3U       (0x216)
#define SPR_E500_L1CFG1  (0x216)
#define SPR_IBAT3L       (0x217)
#define SPR_DBAT0U       (0x218)
#define SPR_DBAT0L       (0x219)
#define SPR_DBAT1U       (0x21A)
#define SPR_DBAT1L       (0x21B)
#define SPR_DBAT2U       (0x21C)
#define SPR_DBAT2L       (0x21D)
#define SPR_DBAT3U       (0x21E)
#define SPR_DBAT3L       (0x21F)
#define SPR_IBAT4U       (0x230)
#define SPR_IBAT4L       (0x231)
#define SPR_IBAT5U       (0x232)
#define SPR_IBAT5L       (0x233)
#define SPR_IBAT6U       (0x234)
#define SPR_IBAT6L       (0x235)
#define SPR_IBAT7U       (0x236)
#define SPR_IBAT7L       (0x237)
#define SPR_DBAT4U       (0x238)
#define SPR_DBAT4L       (0x239)
#define SPR_DBAT5U       (0x23A)
#define SPR_BOOKE_MCSRR0 (0x23A)
#define SPR_DBAT5L       (0x23B)
#define SPR_BOOKE_MCSRR1 (0x23B)
#define SPR_DBAT6U       (0x23C)
#define SPR_BOOKE_MCSR   (0x23C)
#define SPR_DBAT6L       (0x23D)
#define SPR_E500_MCAR    (0x23D)
#define SPR_DBAT7U       (0x23E)
#define SPR_BOOKE_DSRR0  (0x23E)
#define SPR_DBAT7L       (0x23F)
#define SPR_BOOKE_DSRR1  (0x23F)
#define SPR_BOOKE_SPRG8  (0x25C)
#define SPR_BOOKE_SPRG9  (0x25D)
#define SPR_BOOKE_MAS0   (0x270)
#define SPR_BOOKE_MAS1   (0x271)
#define SPR_BOOKE_MAS2   (0x272)
#define SPR_BOOKE_MAS3   (0x273)
#define SPR_BOOKE_MAS4   (0x274)
#define SPR_BOOKE_MAS6   (0x276)
#define SPR_BOOKE_PID1   (0x279)
#define SPR_BOOKE_PID2   (0x27A)
#define SPR_BOOKE_TLB0CFG (0x2B0)
#define SPR_BOOKE_TLB1CFG (0x2B1)
#define SPR_BOOKE_TLB2CFG (0x2B2)
#define SPR_BOOKE_TLB3CFG (0x2B3)
#define SPR_BOOKE_EPR    (0x2BE)
#define SPR_PERF0        (0x300)
#define SPR_PERF1        (0x301)
#define SPR_PERF2        (0x302)
#define SPR_PERF3        (0x303)
#define SPR_PERF4        (0x304)
#define SPR_PERF5        (0x305)
#define SPR_PERF6        (0x306)
#define SPR_PERF7        (0x307)
#define SPR_PERF8        (0x308)
#define SPR_PERF9        (0x309)
#define SPR_PERFA        (0x30A)
#define SPR_PERFB        (0x30B)
#define SPR_PERFC        (0x30C)
#define SPR_PERFD        (0x30D)
#define SPR_PERFE        (0x30E)
#define SPR_PERFF        (0x30F)
#define SPR_UPERF0       (0x310)
#define SPR_UPERF1       (0x311)
#define SPR_UPERF2       (0x312)
#define SPR_UPERF3       (0x313)
#define SPR_UPERF4       (0x314)
#define SPR_UPERF5       (0x315)
#define SPR_UPERF6       (0x316)
#define SPR_UPERF7       (0x317)
#define SPR_UPERF8       (0x318)
#define SPR_UPERF9       (0x319)
#define SPR_UPERFA       (0x31A)
#define SPR_UPERFB       (0x31B)
#define SPR_UPERFC       (0x31C)
#define SPR_UPERFD       (0x31D)
#define SPR_UPERFE       (0x31E)
#define SPR_UPERFF       (0x31F)
#define SPR_440_INV0     (0x370)
#define SPR_440_INV1     (0x371)
#define SPR_440_INV2     (0x372)
#define SPR_440_INV3     (0x373)
#define SPR_440_ITV0     (0x374)
#define SPR_440_ITV1     (0x375)
#define SPR_440_ITV2     (0x376)
#define SPR_440_ITV3     (0x377)
#define SPR_440_CCR1     (0x378)
#define SPR_DCRIPR       (0x37B)
#define SPR_PPR          (0x380)
#define SPR_440_DNV0     (0x390)
#define SPR_440_DNV1     (0x391)
#define SPR_440_DNV2     (0x392)
#define SPR_440_DNV3     (0x393)
#define SPR_440_DTV0     (0x394)
#define SPR_440_DTV1     (0x395)
#define SPR_440_DTV2     (0x396)
#define SPR_440_DTV3     (0x397)
#define SPR_440_DVLIM    (0x398)
#define SPR_440_IVLIM    (0x399)
#define SPR_440_RSTCFG   (0x39B)
#define SPR_BOOKE_DCDBTRL (0x39C)
#define SPR_BOOKE_DCDBTRH (0x39D)
#define SPR_BOOKE_ICDBTRL (0x39E)
#define SPR_BOOKE_ICDBTRH (0x39F)
#define SPR_UMMCR2       (0x3A0)
#define SPR_UPMC5        (0x3A1)
#define SPR_UPMC6        (0x3A2)
#define SPR_UBAMR        (0x3A7)
#define SPR_UMMCR0       (0x3A8)
#define SPR_UPMC1        (0x3A9)
#define SPR_UPMC2        (0x3AA)
#define SPR_USIAR        (0x3AB)
#define SPR_UMMCR1       (0x3AC)
#define SPR_UPMC3        (0x3AD)
#define SPR_UPMC4        (0x3AE)
#define SPR_USDA         (0x3AF)
#define SPR_40x_ZPR      (0x3B0)
#define SPR_BOOKE_MAS7   (0x3B0)
#define SPR_620_PMR0     (0x3B0)
#define SPR_MMCR2        (0x3B0)
#define SPR_PMC5         (0x3B1)
#define SPR_40x_PID      (0x3B1)
#define SPR_620_PMR1     (0x3B1)
#define SPR_PMC6         (0x3B2)
#define SPR_440_MMUCR    (0x3B2)
#define SPR_620_PMR2     (0x3B2)
#define SPR_4xx_CCR0     (0x3B3)
#define SPR_BOOKE_EPLC   (0x3B3)
#define SPR_620_PMR3     (0x3B3)
#define SPR_405_IAC3     (0x3B4)
#define SPR_BOOKE_EPSC   (0x3B4)
#define SPR_620_PMR4     (0x3B4)
#define SPR_405_IAC4     (0x3B5)
#define SPR_620_PMR5     (0x3B5)
#define SPR_405_DVC1     (0x3B6)
#define SPR_620_PMR6     (0x3B6)
#define SPR_405_DVC2     (0x3B7)
#define SPR_620_PMR7     (0x3B7)
#define SPR_BAMR         (0x3B7)
#define SPR_MMCR0        (0x3B8)
#define SPR_620_PMR8     (0x3B8)
#define SPR_PMC1         (0x3B9)
#define SPR_40x_SGR      (0x3B9)
#define SPR_620_PMR9     (0x3B9)
#define SPR_PMC2         (0x3BA)
#define SPR_40x_DCWR     (0x3BA)
#define SPR_620_PMRA     (0x3BA)
#define SPR_SIAR         (0x3BB)
#define SPR_405_SLER     (0x3BB)
#define SPR_620_PMRB     (0x3BB)
#define SPR_MMCR1        (0x3BC)
#define SPR_405_SU0R     (0x3BC)
#define SPR_620_PMRC     (0x3BC)
#define SPR_401_SKR      (0x3BC)
#define SPR_PMC3         (0x3BD)
#define SPR_405_DBCR1    (0x3BD)
#define SPR_620_PMRD     (0x3BD)
#define SPR_PMC4         (0x3BE)
#define SPR_620_PMRE     (0x3BE)
#define SPR_SDA          (0x3BF)
#define SPR_620_PMRF     (0x3BF)
#define SPR_403_VTBL     (0x3CC)
#define SPR_403_VTBU     (0x3CD)
#define SPR_DMISS        (0x3D0)
#define SPR_DCMP         (0x3D1)
#define SPR_HASH1        (0x3D2)
#define SPR_HASH2        (0x3D3)
#define SPR_BOOKE_ICDBDR (0x3D3)
#define SPR_TLBMISS      (0x3D4)
#define SPR_IMISS        (0x3D4)
#define SPR_40x_ESR      (0x3D4)
#define SPR_PTEHI        (0x3D5)
#define SPR_ICMP         (0x3D5)
#define SPR_40x_DEAR     (0x3D5)
#define SPR_PTELO        (0x3D6)
#define SPR_RPA          (0x3D6)
#define SPR_40x_EVPR     (0x3D6)
#define SPR_L3PM         (0x3D7)
#define SPR_403_CDBCR    (0x3D7)
#define SPR_L3OHCR       (0x3D8)
#define SPR_TCR          (0x3D8)
#define SPR_40x_TSR      (0x3D8)
#define SPR_IBR          (0x3DA)
#define SPR_40x_TCR      (0x3DA)
#define SPR_ESASRR       (0x3DB)
#define SPR_40x_PIT      (0x3DB)
#define SPR_403_TBL      (0x3DC)
#define SPR_403_TBU      (0x3DD)
#define SPR_SEBR         (0x3DE)
#define SPR_40x_SRR2     (0x3DE)
#define SPR_SER          (0x3DF)
#define SPR_40x_SRR3     (0x3DF)
#define SPR_L3ITCR0      (0x3E8)
#define SPR_L3ITCR1      (0x3E9)
#define SPR_L3ITCR2      (0x3EA)
#define SPR_L3ITCR3      (0x3EB)
#define SPR_HID0         (0x3F0)
#define SPR_40x_DBSR     (0x3F0)
#define SPR_HID1         (0x3F1)
#define SPR_IABR         (0x3F2)
#define SPR_40x_DBCR0    (0x3F2)
#define SPR_601_HID2     (0x3F2)
#define SPR_E500_L1CSR0  (0x3F2)
#define SPR_ICTRL        (0x3F3)
#define SPR_HID2         (0x3F3)
#define SPR_E500_L1CSR1  (0x3F3)
#define SPR_440_DBDR     (0x3F3)
#define SPR_LDSTDB       (0x3F4)
#define SPR_40x_IAC1     (0x3F4)
#define SPR_BOOKE_MMUCSR0 (0x3F4)
#define SPR_DABR         (0x3F5)
#define DABR_MASK (~(target_ulong)0x7)
#define SPR_E500_BUCSR   (0x3F5)
#define SPR_40x_IAC2     (0x3F5)
#define SPR_601_HID5     (0x3F5)
#define SPR_40x_DAC1     (0x3F6)
#define SPR_MSSCR0       (0x3F6)
#define SPR_MSSSR0       (0x3F7)
#define SPR_DABRX        (0x3F7)
#define SPR_40x_DAC2     (0x3F7)
#define SPR_BOOKE_MMUCFG (0x3F7)
#define SPR_LDSTCR       (0x3F8)
#define SPR_L2PMCR       (0x3F8)
#define SPR_750_HID2     (0x3F8)
#define SPR_620_HID8     (0x3F8)
#define SPR_L2CR         (0x3F9)
#define SPR_620_HID9     (0x3F9)
#define SPR_L3CR         (0x3FA)
#define SPR_IABR2        (0x3FA)
#define SPR_40x_DCCR     (0x3FA)
#define SPR_ICTC         (0x3FB)
#define SPR_40x_ICCR     (0x3FB)
#define SPR_THRM1        (0x3FC)
#define SPR_403_PBL1     (0x3FC)
#define SPR_SP           (0x3FD)
#define SPR_THRM2        (0x3FD)
#define SPR_403_PBU1     (0x3FD)
#define SPR_604_HID13    (0x3FD)
#define SPR_LT           (0x3FE)
#define SPR_THRM3        (0x3FE)
#define SPR_FPECR        (0x3FE)
#define SPR_403_PBL2     (0x3FE)
#define SPR_PIR          (0x3FF)
#define SPR_403_PBU2     (0x3FF)
#define SPR_601_HID15    (0x3FF)
#define SPR_604_HID15    (0x3FF)
#define SPR_E500_SVR     (0x3FF)

/*****************************************************************************/
/* Memory access type :
 * may be needed for precise access rights control and precise exceptions.
 */
enum {
    /* 1 bit to define user level / supervisor access */
    ACCESS_USER  = 0x00,
    ACCESS_SUPER = 0x01,
    /* Type of instruction that generated the access */
    ACCESS_CODE  = 0x10, /* Code fetch access                */
    ACCESS_INT   = 0x20, /* Integer load/store access        */
    ACCESS_FLOAT = 0x30, /* floating point load/store access */
    ACCESS_RES   = 0x40, /* load/store with reservation      */
    ACCESS_EXT   = 0x50, /* external access                  */
    ACCESS_CACHE = 0x60, /* Cache manipulation               */
};

/* Hardware interruption sources:
 * all those exception can be raised simulteaneously
 */
/* Input pins definitions */
enum {
    /* 6xx bus input pins */
    PPC6xx_INPUT_HRESET     = 0,
    PPC6xx_INPUT_SRESET     = 1,
    PPC6xx_INPUT_CKSTP_IN   = 2,
    PPC6xx_INPUT_MCP        = 3,
    PPC6xx_INPUT_SMI        = 4,
    PPC6xx_INPUT_INT        = 5,
};

enum {
    /* Embedded PowerPC input pins */
    PPCBookE_INPUT_HRESET     = 0,
    PPCBookE_INPUT_SRESET     = 1,
    PPCBookE_INPUT_CKSTP_IN   = 2,
    PPCBookE_INPUT_MCP        = 3,
    PPCBookE_INPUT_SMI        = 4,
    PPCBookE_INPUT_INT        = 5,
    PPCBookE_INPUT_CINT       = 6,
};

enum {
    /* PowerPC 40x input pins */
    PPC40x_INPUT_RESET_CORE = 0,
    PPC40x_INPUT_RESET_CHIP = 1,
    PPC40x_INPUT_RESET_SYS  = 2,
    PPC40x_INPUT_CINT       = 3,
    PPC40x_INPUT_INT        = 4,
    PPC40x_INPUT_HALT       = 5,
    PPC40x_INPUT_DEBUG      = 6,
    PPC40x_INPUT_NB,
};

enum {
    /* PowerPC 620 (and probably others) input pins */
    PPC620_INPUT_HRESET     = 0,
    PPC620_INPUT_SRESET     = 1,
    PPC620_INPUT_CKSTP      = 2,
    PPC620_INPUT_TBEN       = 3,
    PPC620_INPUT_WAKEUP     = 4,
    PPC620_INPUT_MCP        = 5,
    PPC620_INPUT_SMI        = 6,
    PPC620_INPUT_INT        = 7,
};

enum {
    /* PowerPC 970 input pins */
    PPC970_INPUT_HRESET     = 0,
    PPC970_INPUT_SRESET     = 1,
    PPC970_INPUT_CKSTP      = 2,
    PPC970_INPUT_TBEN       = 3,
    PPC970_INPUT_MCP        = 4,
    PPC970_INPUT_INT        = 5,
    PPC970_INPUT_THINT      = 6,
};

/* Hardware exceptions definitions */
enum {
    /* External hardware exception sources */
    PPC_INTERRUPT_RESET     = 0,  /* Reset exception                      */
    PPC_INTERRUPT_MCK       = 1,  /* Machine check exception              */
    PPC_INTERRUPT_EXT       = 2,  /* External interrupt                   */
    PPC_INTERRUPT_SMI       = 3,  /* System management interrupt          */
    PPC_INTERRUPT_CEXT      = 4,  /* Critical external interrupt          */
    PPC_INTERRUPT_DEBUG     = 5,  /* External debug exception             */
    PPC_INTERRUPT_THERM     = 6,  /* Thermal exception                    */
    /* Internal hardware exception sources */
    PPC_INTERRUPT_DECR      = 7,  /* Decrementer exception                */
    PPC_INTERRUPT_HDECR     = 8,  /* Hypervisor decrementer exception     */
    PPC_INTERRUPT_PIT       = 9,  /* Programmable inteval timer interrupt */
    PPC_INTERRUPT_FIT       = 10, /* Fixed interval timer interrupt       */
    PPC_INTERRUPT_WDT       = 11, /* Watchdog timer interrupt             */
    PPC_INTERRUPT_CDOORBELL = 12, /* Critical doorbell interrupt          */
    PPC_INTERRUPT_DOORBELL  = 13, /* Doorbell interrupt                   */
    PPC_INTERRUPT_PERFM     = 14, /* Performance monitor interrupt        */
};

/*****************************************************************************/

#endif /* !defined (__CPU_PPC_H__) */
