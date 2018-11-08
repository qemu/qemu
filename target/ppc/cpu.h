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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_CPU_H
#define PPC_CPU_H

#include "qemu-common.h"
#include "qemu/int128.h"

//#define PPC_EMULATE_32BITS_HYPV

#if defined (TARGET_PPC64)
/* PowerPC 64 definitions */
#define TARGET_LONG_BITS 64
#define TARGET_PAGE_BITS 12

#define TCG_GUEST_DEFAULT_MO 0

/* Note that the official physical address space bits is 62-M where M
   is implementation dependent.  I've not looked up M for the set of
   cpus we emulate at the system level.  */
#define TARGET_PHYS_ADDR_SPACE_BITS 62

/* Note that the PPC environment architecture talks about 80 bit virtual
   addresses, with segmentation.  Obviously that's not all visible to a
   single process, which is all we're concerned with here.  */
#ifdef TARGET_ABI32
# define TARGET_VIRT_ADDR_SPACE_BITS 32
#else
# define TARGET_VIRT_ADDR_SPACE_BITS 64
#endif

#define TARGET_PAGE_BITS_64K 16
#define TARGET_PAGE_BITS_16M 24

#else /* defined (TARGET_PPC64) */
/* PowerPC 32 definitions */
#define TARGET_LONG_BITS 32
#define TARGET_PAGE_BITS 12

#define TARGET_PHYS_ADDR_SPACE_BITS 36
#define TARGET_VIRT_ADDR_SPACE_BITS 32

#endif /* defined (TARGET_PPC64) */

#define CPUArchState struct CPUPPCState

#include "exec/cpu-defs.h"
#include "cpu-qom.h"

#if defined (TARGET_PPC64)
#define PPC_ELF_MACHINE     EM_PPC64
#else
#define PPC_ELF_MACHINE     EM_PPC
#endif

#define PPC_BIT(bit)            (0x8000000000000000UL >> (bit))
#define PPC_BIT32(bit)          (0x80000000UL >> (bit))
#define PPC_BIT8(bit)           (0x80UL >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))
#define PPC_BITMASK32(bs, be)   ((PPC_BIT32(bs) - PPC_BIT32(be)) | \
                                 PPC_BIT32(bs))
#define PPC_BITMASK8(bs, be)    ((PPC_BIT8(bs) - PPC_BIT8(be)) | PPC_BIT8(bs))

#if HOST_LONG_BITS == 32
# define MASK_TO_LSH(m)          (__builtin_ffsll(m) - 1)
#elif HOST_LONG_BITS == 64
# define MASK_TO_LSH(m)          (__builtin_ffsl(m) - 1)
#else
# error Unknown sizeof long
#endif

#define GETFIELD(m, v)          (((v) & (m)) >> MASK_TO_LSH(m))
#define SETFIELD(m, v, val)                             \
        (((v) & ~(m)) | ((((typeof(v))(val)) << MASK_TO_LSH(m)) & (m)))

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
    POWERPC_EXCP_DTLB     = 13, /* Data TLB miss                             */
    POWERPC_EXCP_ITLB     = 14, /* Instruction TLB miss                      */
    POWERPC_EXCP_DEBUG    = 15, /* Debug interrupt                           */
    /* Vectors 16 to 31 are reserved                                         */
    POWERPC_EXCP_SPEU     = 32, /* SPE/embedded floating-point unavailable   */
    POWERPC_EXCP_EFPDI    = 33, /* Embedded floating-point data interrupt    */
    POWERPC_EXCP_EFPRI    = 34, /* Embedded floating-point round interrupt   */
    POWERPC_EXCP_EPERFM   = 35, /* Embedded performance monitor interrupt    */
    POWERPC_EXCP_DOORI    = 36, /* Embedded doorbell interrupt               */
    POWERPC_EXCP_DOORCI   = 37, /* Embedded doorbell critical interrupt      */
    POWERPC_EXCP_GDOORI   = 38, /* Embedded guest doorbell interrupt         */
    POWERPC_EXCP_GDOORCI  = 39, /* Embedded guest doorbell critical interrupt*/
    POWERPC_EXCP_HYPPRIV  = 41, /* Embedded hypervisor priv instruction      */
    /* Vectors 42 to 63 are reserved                                         */
    /* Exceptions defined in the PowerPC server specification                */
    POWERPC_EXCP_RESET    = 64, /* System reset exception                    */
    POWERPC_EXCP_DSEG     = 65, /* Data segment exception                    */
    POWERPC_EXCP_ISEG     = 66, /* Instruction segment exception             */
    POWERPC_EXCP_HDECR    = 67, /* Hypervisor decrementer exception          */
    POWERPC_EXCP_TRACE    = 68, /* Trace exception                           */
    POWERPC_EXCP_HDSI     = 69, /* Hypervisor data storage exception         */
    POWERPC_EXCP_HISI     = 70, /* Hypervisor instruction storage exception  */
    POWERPC_EXCP_HDSEG    = 71, /* Hypervisor data segment exception         */
    POWERPC_EXCP_HISEG    = 72, /* Hypervisor instruction segment exception  */
    POWERPC_EXCP_VPU      = 73, /* Vector unavailable exception              */
    /* 40x specific exceptions                                               */
    POWERPC_EXCP_PIT      = 74, /* Programmable interval timer interrupt     */
    /* 601 specific exceptions                                               */
    POWERPC_EXCP_IO       = 75, /* IO error exception                        */
    POWERPC_EXCP_RUNM     = 76, /* Run mode exception                        */
    /* 602 specific exceptions                                               */
    POWERPC_EXCP_EMUL     = 77, /* Emulation trap exception                  */
    /* 602/603 specific exceptions                                           */
    POWERPC_EXCP_IFTLB    = 78, /* Instruction fetch TLB miss                */
    POWERPC_EXCP_DLTLB    = 79, /* Data load TLB miss                        */
    POWERPC_EXCP_DSTLB    = 80, /* Data store TLB miss                       */
    /* Exceptions available on most PowerPC                                  */
    POWERPC_EXCP_FPA      = 81, /* Floating-point assist exception           */
    POWERPC_EXCP_DABR     = 82, /* Data address breakpoint                   */
    POWERPC_EXCP_IABR     = 83, /* Instruction address breakpoint            */
    POWERPC_EXCP_SMI      = 84, /* System management interrupt               */
    POWERPC_EXCP_PERFM    = 85, /* Embedded performance monitor interrupt    */
    /* 7xx/74xx specific exceptions                                          */
    POWERPC_EXCP_THERM    = 86, /* Thermal interrupt                         */
    /* 74xx specific exceptions                                              */
    POWERPC_EXCP_VPUA     = 87, /* Vector assist exception                   */
    /* 970FX specific exceptions                                             */
    POWERPC_EXCP_SOFTP    = 88, /* Soft patch exception                      */
    POWERPC_EXCP_MAINT    = 89, /* Maintenance exception                     */
    /* Freescale embedded cores specific exceptions                          */
    POWERPC_EXCP_MEXTBR   = 90, /* Maskable external breakpoint              */
    POWERPC_EXCP_NMEXTBR  = 91, /* Non maskable external breakpoint          */
    POWERPC_EXCP_ITLBE    = 92, /* Instruction TLB error                     */
    POWERPC_EXCP_DTLBE    = 93, /* Data TLB error                            */
    /* VSX Unavailable (Power ISA 2.06 and later)                            */
    POWERPC_EXCP_VSXU     = 94, /* VSX Unavailable                           */
    POWERPC_EXCP_FU       = 95, /* Facility Unavailable                      */
    /* Additional ISA 2.06 and later server exceptions                       */
    POWERPC_EXCP_HV_EMU   = 96, /* HV emulation assistance                   */
    POWERPC_EXCP_HV_MAINT = 97, /* HMI                                       */
    POWERPC_EXCP_HV_FU    = 98, /* Hypervisor Facility unavailable           */
    /* Server doorbell variants */
    POWERPC_EXCP_SDOOR    = 99,
    POWERPC_EXCP_SDOOR_HV = 100,
    /* EOL                                                                   */
    POWERPC_EXCP_NB       = 101,
    /* QEMU exceptions: used internally during code translation              */
    POWERPC_EXCP_STOP         = 0x200, /* stop translation                   */
    POWERPC_EXCP_BRANCH       = 0x201, /* branch instruction                 */
    /* QEMU exceptions: special cases we want to stop translation            */
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
    POWERPC_EXCP_FP_VXSNAN     = 0x05,  /* FP invalid SNaN op                */
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

#define PPC_INPUT(env) (env->bus_model)

/*****************************************************************************/
typedef struct opc_handler_t opc_handler_t;

/*****************************************************************************/
/* Types used to describe some PowerPC registers etc. */
typedef struct DisasContext DisasContext;
typedef struct ppc_spr_t ppc_spr_t;
typedef union ppc_avr_t ppc_avr_t;
typedef union ppc_tlb_t ppc_tlb_t;
typedef struct ppc_hash_pte64 ppc_hash_pte64_t;

/* SPR access micro-ops generations callbacks */
struct ppc_spr_t {
    void (*uea_read)(DisasContext *ctx, int gpr_num, int spr_num);
    void (*uea_write)(DisasContext *ctx, int spr_num, int gpr_num);
#if !defined(CONFIG_USER_ONLY)
    void (*oea_read)(DisasContext *ctx, int gpr_num, int spr_num);
    void (*oea_write)(DisasContext *ctx, int spr_num, int gpr_num);
    void (*hea_read)(DisasContext *ctx, int gpr_num, int spr_num);
    void (*hea_write)(DisasContext *ctx, int spr_num, int gpr_num);
#endif
    const char *name;
    target_ulong default_value;
#ifdef CONFIG_KVM
    /* We (ab)use the fact that all the SPRs will have ids for the
     * ONE_REG interface will have KVM_REG_PPC to use 0 as meaning,
     * don't sync this */
    uint64_t one_reg_id;
#endif
};

/* Altivec registers (128 bits) */
union ppc_avr_t {
    float32 f[4];
    uint8_t u8[16];
    uint16_t u16[8];
    uint32_t u32[4];
    int8_t s8[16];
    int16_t s16[8];
    int32_t s32[4];
    uint64_t u64[2];
    int64_t s64[2];
#ifdef CONFIG_INT128
    __uint128_t u128;
#endif
    Int128 s128;
};

#if !defined(CONFIG_USER_ONLY)
/* Software TLB cache */
typedef struct ppc6xx_tlb_t ppc6xx_tlb_t;
struct ppc6xx_tlb_t {
    target_ulong pte0;
    target_ulong pte1;
    target_ulong EPN;
};

typedef struct ppcemb_tlb_t ppcemb_tlb_t;
struct ppcemb_tlb_t {
    uint64_t RPN;
    target_ulong EPN;
    target_ulong PID;
    target_ulong size;
    uint32_t prot;
    uint32_t attr; /* Storage attributes */
};

typedef struct ppcmas_tlb_t {
     uint32_t mas8;
     uint32_t mas1;
     uint64_t mas2;
     uint64_t mas7_3;
} ppcmas_tlb_t;

union ppc_tlb_t {
    ppc6xx_tlb_t *tlb6;
    ppcemb_tlb_t *tlbe;
    ppcmas_tlb_t *tlbm;
};

/* possible TLB variants */
#define TLB_NONE               0
#define TLB_6XX                1
#define TLB_EMB                2
#define TLB_MAS                3
#endif

typedef struct PPCHash64SegmentPageSizes PPCHash64SegmentPageSizes;

typedef struct ppc_slb_t ppc_slb_t;
struct ppc_slb_t {
    uint64_t esid;
    uint64_t vsid;
    const PPCHash64SegmentPageSizes *sps;
};

#define MAX_SLB_ENTRIES         64
#define SEGMENT_SHIFT_256M      28
#define SEGMENT_MASK_256M       (~((1ULL << SEGMENT_SHIFT_256M) - 1))

#define SEGMENT_SHIFT_1T        40
#define SEGMENT_MASK_1T         (~((1ULL << SEGMENT_SHIFT_1T) - 1))


/*****************************************************************************/
/* Machine state register bits definition                                    */
#define MSR_SF   63 /* Sixty-four-bit mode                            hflags */
#define MSR_TAG  62 /* Tag-active mode (POWERx ?)                            */
#define MSR_ISF  61 /* Sixty-four-bit interrupt mode on 630                  */
#define MSR_SHV  60 /* hypervisor state                               hflags */
#define MSR_TS0  34 /* Transactional state, 2 bits (Book3s)                  */
#define MSR_TS1  33
#define MSR_TM   32 /* Transactional Memory Available (Book3s)               */
#define MSR_CM   31 /* Computation mode for BookE                     hflags */
#define MSR_ICM  30 /* Interrupt computation mode for BookE                  */
#define MSR_THV  29 /* hypervisor state for 32 bits PowerPC           hflags */
#define MSR_GS   28 /* guest state for BookE                                 */
#define MSR_UCLE 26 /* User-mode cache lock enable for BookE                 */
#define MSR_VR   25 /* altivec available                            x hflags */
#define MSR_SPE  25 /* SPE enable for BookE                         x hflags */
#define MSR_AP   23 /* Access privilege state on 602                  hflags */
#define MSR_VSX  23 /* Vector Scalar Extension (ISA 2.06 and later) x hflags */
#define MSR_SA   22 /* Supervisor access mode on 602                  hflags */
#define MSR_KEY  19 /* key bit on 603e                                       */
#define MSR_POW  18 /* Power management                                      */
#define MSR_TGPR 17 /* TGPR usage on 602/603                        x        */
#define MSR_CE   17 /* Critical interrupt enable on embedded PowerPC x       */
#define MSR_ILE  16 /* Interrupt little-endian mode                          */
#define MSR_EE   15 /* External interrupt enable                             */
#define MSR_PR   14 /* Problem state                                  hflags */
#define MSR_FP   13 /* Floating point available                       hflags */
#define MSR_ME   12 /* Machine check interrupt enable                        */
#define MSR_FE0  11 /* Floating point exception mode 0                hflags */
#define MSR_SE   10 /* Single-step trace enable                     x hflags */
#define MSR_DWE  10 /* Debug wait enable on 405                     x        */
#define MSR_UBLE 10 /* User BTB lock enable on e500                 x        */
#define MSR_BE   9  /* Branch trace enable                          x hflags */
#define MSR_DE   9  /* Debug interrupts enable on embedded PowerPC  x        */
#define MSR_FE1  8  /* Floating point exception mode 1                hflags */
#define MSR_AL   7  /* AL bit on POWER                                       */
#define MSR_EP   6  /* Exception prefix on 601                               */
#define MSR_IR   5  /* Instruction relocate                                  */
#define MSR_DR   4  /* Data relocate                                         */
#define MSR_IS   5  /* Instruction address space (BookE)                     */
#define MSR_DS   4  /* Data address space (BookE)                            */
#define MSR_PE   3  /* Protection enable on 403                              */
#define MSR_PX   2  /* Protection exclusive on 403                  x        */
#define MSR_PMM  2  /* Performance monitor mark on POWER            x        */
#define MSR_RI   1  /* Recoverable interrupt                        1        */
#define MSR_LE   0  /* Little-endian mode                           1 hflags */

/* LPCR bits */
#define LPCR_VPM0         PPC_BIT(0)
#define LPCR_VPM1         PPC_BIT(1)
#define LPCR_ISL          PPC_BIT(2)
#define LPCR_KBV          PPC_BIT(3)
#define LPCR_DPFD_SHIFT   (63 - 11)
#define LPCR_DPFD         (0x7ull << LPCR_DPFD_SHIFT)
#define LPCR_VRMASD_SHIFT (63 - 16)
#define LPCR_VRMASD       (0x1full << LPCR_VRMASD_SHIFT)
/* P9: Power-saving mode Exit Cause Enable (Upper Section) Mask */
#define LPCR_PECE_U_SHIFT (63 - 19)
#define LPCR_PECE_U_MASK  (0x7ull << LPCR_PECE_U_SHIFT)
#define LPCR_HVEE         PPC_BIT(17) /* Hypervisor Virt Exit Enable */
#define LPCR_RMLS_SHIFT   (63 - 37)
#define LPCR_RMLS         (0xfull << LPCR_RMLS_SHIFT)
#define LPCR_ILE          PPC_BIT(38)
#define LPCR_AIL_SHIFT    (63 - 40)      /* Alternate interrupt location */
#define LPCR_AIL          (3ull << LPCR_AIL_SHIFT)
#define LPCR_UPRT         PPC_BIT(41) /* Use Process Table */
#define LPCR_EVIRT        PPC_BIT(42) /* Enhanced Virtualisation */
#define LPCR_ONL          PPC_BIT(45)
#define LPCR_LD           PPC_BIT(46) /* Large Decrementer */
#define LPCR_P7_PECE0     PPC_BIT(49)
#define LPCR_P7_PECE1     PPC_BIT(50)
#define LPCR_P7_PECE2     PPC_BIT(51)
#define LPCR_P8_PECE0     PPC_BIT(47)
#define LPCR_P8_PECE1     PPC_BIT(48)
#define LPCR_P8_PECE2     PPC_BIT(49)
#define LPCR_P8_PECE3     PPC_BIT(50)
#define LPCR_P8_PECE4     PPC_BIT(51)
/* P9: Power-saving mode Exit Cause Enable (Lower Section) Mask */
#define LPCR_PECE_L_SHIFT (63 - 51)
#define LPCR_PECE_L_MASK  (0x1full << LPCR_PECE_L_SHIFT)
#define LPCR_PDEE         PPC_BIT(47) /* Privileged Doorbell Exit EN */
#define LPCR_HDEE         PPC_BIT(48) /* Hyperv Doorbell Exit Enable */
#define LPCR_EEE          PPC_BIT(49) /* External Exit Enable        */
#define LPCR_DEE          PPC_BIT(50) /* Decrementer Exit Enable     */
#define LPCR_OEE          PPC_BIT(51) /* Other Exit Enable           */
#define LPCR_MER          PPC_BIT(52)
#define LPCR_GTSE         PPC_BIT(53) /* Guest Translation Shootdown */
#define LPCR_TC           PPC_BIT(54)
#define LPCR_HEIC         PPC_BIT(59) /* HV Extern Interrupt Control */
#define LPCR_LPES0        PPC_BIT(60)
#define LPCR_LPES1        PPC_BIT(61)
#define LPCR_RMI          PPC_BIT(62)
#define LPCR_HVICE        PPC_BIT(62) /* HV Virtualisation Int Enable */
#define LPCR_HDICE        PPC_BIT(63)

#define msr_sf   ((env->msr >> MSR_SF)   & 1)
#define msr_isf  ((env->msr >> MSR_ISF)  & 1)
#define msr_shv  ((env->msr >> MSR_SHV)  & 1)
#define msr_cm   ((env->msr >> MSR_CM)   & 1)
#define msr_icm  ((env->msr >> MSR_ICM)  & 1)
#define msr_thv  ((env->msr >> MSR_THV)  & 1)
#define msr_gs   ((env->msr >> MSR_GS)   & 1)
#define msr_ucle ((env->msr >> MSR_UCLE) & 1)
#define msr_vr   ((env->msr >> MSR_VR)   & 1)
#define msr_spe  ((env->msr >> MSR_SPE)  & 1)
#define msr_ap   ((env->msr >> MSR_AP)   & 1)
#define msr_vsx  ((env->msr >> MSR_VSX)  & 1)
#define msr_sa   ((env->msr >> MSR_SA)   & 1)
#define msr_key  ((env->msr >> MSR_KEY)  & 1)
#define msr_pow  ((env->msr >> MSR_POW)  & 1)
#define msr_tgpr ((env->msr >> MSR_TGPR) & 1)
#define msr_ce   ((env->msr >> MSR_CE)   & 1)
#define msr_ile  ((env->msr >> MSR_ILE)  & 1)
#define msr_ee   ((env->msr >> MSR_EE)   & 1)
#define msr_pr   ((env->msr >> MSR_PR)   & 1)
#define msr_fp   ((env->msr >> MSR_FP)   & 1)
#define msr_me   ((env->msr >> MSR_ME)   & 1)
#define msr_fe0  ((env->msr >> MSR_FE0)  & 1)
#define msr_se   ((env->msr >> MSR_SE)   & 1)
#define msr_dwe  ((env->msr >> MSR_DWE)  & 1)
#define msr_uble ((env->msr >> MSR_UBLE) & 1)
#define msr_be   ((env->msr >> MSR_BE)   & 1)
#define msr_de   ((env->msr >> MSR_DE)   & 1)
#define msr_fe1  ((env->msr >> MSR_FE1)  & 1)
#define msr_al   ((env->msr >> MSR_AL)   & 1)
#define msr_ep   ((env->msr >> MSR_EP)   & 1)
#define msr_ir   ((env->msr >> MSR_IR)   & 1)
#define msr_dr   ((env->msr >> MSR_DR)   & 1)
#define msr_is   ((env->msr >> MSR_IS)   & 1)
#define msr_ds   ((env->msr >> MSR_DS)   & 1)
#define msr_pe   ((env->msr >> MSR_PE)   & 1)
#define msr_px   ((env->msr >> MSR_PX)   & 1)
#define msr_pmm  ((env->msr >> MSR_PMM)  & 1)
#define msr_ri   ((env->msr >> MSR_RI)   & 1)
#define msr_le   ((env->msr >> MSR_LE)   & 1)
#define msr_ts   ((env->msr >> MSR_TS1)  & 3)
#define msr_tm   ((env->msr >> MSR_TM)   & 1)

#define DBCR0_ICMP (1 << 27)
#define DBCR0_BRT (1 << 26)
#define DBSR_ICMP (1 << 27)
#define DBSR_BRT (1 << 26)

/* Hypervisor bit is more specific */
#if defined(TARGET_PPC64)
#define MSR_HVB (1ULL << MSR_SHV)
#define msr_hv  msr_shv
#else
#if defined(PPC_EMULATE_32BITS_HYPV)
#define MSR_HVB (1ULL << MSR_THV)
#define msr_hv  msr_thv
#else
#define MSR_HVB (0ULL)
#define msr_hv  (0)
#endif
#endif

/* DSISR */
#define DSISR_NOPTE              0x40000000
/* Not permitted by access authority of encoded access authority */
#define DSISR_PROTFAULT          0x08000000
#define DSISR_ISSTORE            0x02000000
/* Not permitted by virtual page class key protection */
#define DSISR_AMR                0x00200000
/* Unsupported Radix Tree Configuration */
#define DSISR_R_BADCONFIG        0x00080000

/* SRR1 error code fields */

#define SRR1_NOPTE               DSISR_NOPTE
/* Not permitted due to no-execute or guard bit set */
#define SRR1_NOEXEC_GUARD        0x10000000
#define SRR1_PROTFAULT           DSISR_PROTFAULT
#define SRR1_IAMR                DSISR_AMR

/* Facility Status and Control (FSCR) bits */
#define FSCR_EBB        (63 - 56) /* Event-Based Branch Facility */
#define FSCR_TAR        (63 - 55) /* Target Address Register */
/* Interrupt cause mask and position in FSCR. HFSCR has the same format */
#define FSCR_IC_MASK    (0xFFULL)
#define FSCR_IC_POS     (63 - 7)
#define FSCR_IC_DSCR_SPR3   2
#define FSCR_IC_PMU         3
#define FSCR_IC_BHRB        4
#define FSCR_IC_TM          5
#define FSCR_IC_EBB         7
#define FSCR_IC_TAR         8

/* Exception state register bits definition                                  */
#define ESR_PIL   PPC_BIT(36) /* Illegal Instruction                    */
#define ESR_PPR   PPC_BIT(37) /* Privileged Instruction                 */
#define ESR_PTR   PPC_BIT(38) /* Trap                                   */
#define ESR_FP    PPC_BIT(39) /* Floating-Point Operation               */
#define ESR_ST    PPC_BIT(40) /* Store Operation                        */
#define ESR_AP    PPC_BIT(44) /* Auxiliary Processor Operation          */
#define ESR_PUO   PPC_BIT(45) /* Unimplemented Operation                */
#define ESR_BO    PPC_BIT(46) /* Byte Ordering                          */
#define ESR_PIE   PPC_BIT(47) /* Imprecise exception                    */
#define ESR_DATA  PPC_BIT(53) /* Data Access (Embedded page table)      */
#define ESR_TLBI  PPC_BIT(54) /* TLB Ineligible (Embedded page table)   */
#define ESR_PT    PPC_BIT(55) /* Page Table (Embedded page table)       */
#define ESR_SPV   PPC_BIT(56) /* SPE/VMX operation                      */
#define ESR_EPID  PPC_BIT(57) /* External Process ID operation          */
#define ESR_VLEMI PPC_BIT(58) /* VLE operation                          */
#define ESR_MIF   PPC_BIT(62) /* Misaligned instruction (VLE)           */

/* Transaction EXception And Summary Register bits                           */
#define TEXASR_FAILURE_PERSISTENT                (63 - 7)
#define TEXASR_DISALLOWED                        (63 - 8)
#define TEXASR_NESTING_OVERFLOW                  (63 - 9)
#define TEXASR_FOOTPRINT_OVERFLOW                (63 - 10)
#define TEXASR_SELF_INDUCED_CONFLICT             (63 - 11)
#define TEXASR_NON_TRANSACTIONAL_CONFLICT        (63 - 12)
#define TEXASR_TRANSACTION_CONFLICT              (63 - 13)
#define TEXASR_TRANSLATION_INVALIDATION_CONFLICT (63 - 14)
#define TEXASR_IMPLEMENTATION_SPECIFIC           (63 - 15)
#define TEXASR_INSTRUCTION_FETCH_CONFLICT        (63 - 16)
#define TEXASR_ABORT                             (63 - 31)
#define TEXASR_SUSPENDED                         (63 - 32)
#define TEXASR_PRIVILEGE_HV                      (63 - 34)
#define TEXASR_PRIVILEGE_PR                      (63 - 35)
#define TEXASR_FAILURE_SUMMARY                   (63 - 36)
#define TEXASR_TFIAR_EXACT                       (63 - 37)
#define TEXASR_ROT                               (63 - 38)
#define TEXASR_TRANSACTION_LEVEL                 (63 - 52) /* 12 bits */

enum {
    POWERPC_FLAG_NONE     = 0x00000000,
    /* Flag for MSR bit 25 signification (VRE/SPE)                           */
    POWERPC_FLAG_SPE      = 0x00000001,
    POWERPC_FLAG_VRE      = 0x00000002,
    /* Flag for MSR bit 17 signification (TGPR/CE)                           */
    POWERPC_FLAG_TGPR     = 0x00000004,
    POWERPC_FLAG_CE       = 0x00000008,
    /* Flag for MSR bit 10 signification (SE/DWE/UBLE)                       */
    POWERPC_FLAG_SE       = 0x00000010,
    POWERPC_FLAG_DWE      = 0x00000020,
    POWERPC_FLAG_UBLE     = 0x00000040,
    /* Flag for MSR bit 9 signification (BE/DE)                              */
    POWERPC_FLAG_BE       = 0x00000080,
    POWERPC_FLAG_DE       = 0x00000100,
    /* Flag for MSR bit 2 signification (PX/PMM)                             */
    POWERPC_FLAG_PX       = 0x00000200,
    POWERPC_FLAG_PMM      = 0x00000400,
    /* Flag for special features                                             */
    /* Decrementer clock: RTC clock (POWER, 601) or bus clock                */
    POWERPC_FLAG_RTC_CLK  = 0x00010000,
    POWERPC_FLAG_BUS_CLK  = 0x00020000,
    /* Has CFAR                                                              */
    POWERPC_FLAG_CFAR     = 0x00040000,
    /* Has VSX                                                               */
    POWERPC_FLAG_VSX      = 0x00080000,
    /* Has Transaction Memory (ISA 2.07)                                     */
    POWERPC_FLAG_TM       = 0x00100000,
};

/*****************************************************************************/
/* Floating point status and control register                                */
#define FPSCR_FX     31 /* Floating-point exception summary                  */
#define FPSCR_FEX    30 /* Floating-point enabled exception summary          */
#define FPSCR_VX     29 /* Floating-point invalid operation exception summ.  */
#define FPSCR_OX     28 /* Floating-point overflow exception                 */
#define FPSCR_UX     27 /* Floating-point underflow exception                */
#define FPSCR_ZX     26 /* Floating-point zero divide exception              */
#define FPSCR_XX     25 /* Floating-point inexact exception                  */
#define FPSCR_VXSNAN 24 /* Floating-point invalid operation exception (sNan) */
#define FPSCR_VXISI  23 /* Floating-point invalid operation exception (inf)  */
#define FPSCR_VXIDI  22 /* Floating-point invalid operation exception (inf)  */
#define FPSCR_VXZDZ  21 /* Floating-point invalid operation exception (zero) */
#define FPSCR_VXIMZ  20 /* Floating-point invalid operation exception (inf)  */
#define FPSCR_VXVC   19 /* Floating-point invalid operation exception (comp) */
#define FPSCR_FR     18 /* Floating-point fraction rounded                   */
#define FPSCR_FI     17 /* Floating-point fraction inexact                   */
#define FPSCR_C      16 /* Floating-point result class descriptor            */
#define FPSCR_FL     15 /* Floating-point less than or negative              */
#define FPSCR_FG     14 /* Floating-point greater than or negative           */
#define FPSCR_FE     13 /* Floating-point equal or zero                      */
#define FPSCR_FU     12 /* Floating-point unordered or NaN                   */
#define FPSCR_FPCC   12 /* Floating-point condition code                     */
#define FPSCR_FPRF   12 /* Floating-point result flags                       */
#define FPSCR_VXSOFT 10 /* Floating-point invalid operation exception (soft) */
#define FPSCR_VXSQRT 9  /* Floating-point invalid operation exception (sqrt) */
#define FPSCR_VXCVI  8  /* Floating-point invalid operation exception (int)  */
#define FPSCR_VE     7  /* Floating-point invalid operation exception enable */
#define FPSCR_OE     6  /* Floating-point overflow exception enable          */
#define FPSCR_UE     5  /* Floating-point undeflow exception enable          */
#define FPSCR_ZE     4  /* Floating-point zero divide exception enable       */
#define FPSCR_XE     3  /* Floating-point inexact exception enable           */
#define FPSCR_NI     2  /* Floating-point non-IEEE mode                      */
#define FPSCR_RN1    1
#define FPSCR_RN     0  /* Floating-point rounding control                   */
#define fpscr_fex    (((env->fpscr) >> FPSCR_FEX)    & 0x1)
#define fpscr_vx     (((env->fpscr) >> FPSCR_VX)     & 0x1)
#define fpscr_ox     (((env->fpscr) >> FPSCR_OX)     & 0x1)
#define fpscr_ux     (((env->fpscr) >> FPSCR_UX)     & 0x1)
#define fpscr_zx     (((env->fpscr) >> FPSCR_ZX)     & 0x1)
#define fpscr_xx     (((env->fpscr) >> FPSCR_XX)     & 0x1)
#define fpscr_vxsnan (((env->fpscr) >> FPSCR_VXSNAN) & 0x1)
#define fpscr_vxisi  (((env->fpscr) >> FPSCR_VXISI)  & 0x1)
#define fpscr_vxidi  (((env->fpscr) >> FPSCR_VXIDI)  & 0x1)
#define fpscr_vxzdz  (((env->fpscr) >> FPSCR_VXZDZ)  & 0x1)
#define fpscr_vximz  (((env->fpscr) >> FPSCR_VXIMZ)  & 0x1)
#define fpscr_vxvc   (((env->fpscr) >> FPSCR_VXVC)   & 0x1)
#define fpscr_fpcc   (((env->fpscr) >> FPSCR_FPCC)   & 0xF)
#define fpscr_vxsoft (((env->fpscr) >> FPSCR_VXSOFT) & 0x1)
#define fpscr_vxsqrt (((env->fpscr) >> FPSCR_VXSQRT) & 0x1)
#define fpscr_vxcvi  (((env->fpscr) >> FPSCR_VXCVI)  & 0x1)
#define fpscr_ve     (((env->fpscr) >> FPSCR_VE)     & 0x1)
#define fpscr_oe     (((env->fpscr) >> FPSCR_OE)     & 0x1)
#define fpscr_ue     (((env->fpscr) >> FPSCR_UE)     & 0x1)
#define fpscr_ze     (((env->fpscr) >> FPSCR_ZE)     & 0x1)
#define fpscr_xe     (((env->fpscr) >> FPSCR_XE)     & 0x1)
#define fpscr_ni     (((env->fpscr) >> FPSCR_NI)     & 0x1)
#define fpscr_rn     (((env->fpscr) >> FPSCR_RN)     & 0x3)
/* Invalid operation exception summary */
#define fpscr_ix ((env->fpscr) & ((1 << FPSCR_VXSNAN) | (1 << FPSCR_VXISI)  | \
                                  (1 << FPSCR_VXIDI)  | (1 << FPSCR_VXZDZ)  | \
                                  (1 << FPSCR_VXIMZ)  | (1 << FPSCR_VXVC)   | \
                                  (1 << FPSCR_VXSOFT) | (1 << FPSCR_VXSQRT) | \
                                  (1 << FPSCR_VXCVI)))
/* exception summary */
#define fpscr_ex  (((env->fpscr) >> FPSCR_XX) & 0x1F)
/* enabled exception summary */
#define fpscr_eex (((env->fpscr) >> FPSCR_XX) & ((env->fpscr) >> FPSCR_XE) &  \
                   0x1F)

#define FP_FX		(1ull << FPSCR_FX)
#define FP_FEX		(1ull << FPSCR_FEX)
#define FP_VX		(1ull << FPSCR_VX)
#define FP_OX		(1ull << FPSCR_OX)
#define FP_UX		(1ull << FPSCR_UX)
#define FP_ZX		(1ull << FPSCR_ZX)
#define FP_XX		(1ull << FPSCR_XX)
#define FP_VXSNAN	(1ull << FPSCR_VXSNAN)
#define FP_VXISI	(1ull << FPSCR_VXISI)
#define FP_VXIDI	(1ull << FPSCR_VXIDI)
#define FP_VXZDZ	(1ull << FPSCR_VXZDZ)
#define FP_VXIMZ	(1ull << FPSCR_VXIMZ)
#define FP_VXVC		(1ull << FPSCR_VXVC)
#define FP_FR		(1ull << FSPCR_FR)
#define FP_FI		(1ull << FPSCR_FI)
#define FP_C		(1ull << FPSCR_C)
#define FP_FL		(1ull << FPSCR_FL)
#define FP_FG		(1ull << FPSCR_FG)
#define FP_FE		(1ull << FPSCR_FE)
#define FP_FU		(1ull << FPSCR_FU)
#define FP_FPCC		(FP_FL | FP_FG | FP_FE | FP_FU)
#define FP_FPRF		(FP_C  | FP_FL | FP_FG | FP_FE | FP_FU)
#define FP_VXSOFT	(1ull << FPSCR_VXSOFT)
#define FP_VXSQRT	(1ull << FPSCR_VXSQRT)
#define FP_VXCVI	(1ull << FPSCR_VXCVI)
#define FP_VE		(1ull << FPSCR_VE)
#define FP_OE		(1ull << FPSCR_OE)
#define FP_UE		(1ull << FPSCR_UE)
#define FP_ZE		(1ull << FPSCR_ZE)
#define FP_XE		(1ull << FPSCR_XE)
#define FP_NI		(1ull << FPSCR_NI)
#define FP_RN1		(1ull << FPSCR_RN1)
#define FP_RN		(1ull << FPSCR_RN)

/* the exception bits which can be cleared by mcrfs - includes FX */
#define FP_EX_CLEAR_BITS (FP_FX     | FP_OX     | FP_UX     | FP_ZX     | \
                          FP_XX     | FP_VXSNAN | FP_VXISI  | FP_VXIDI  | \
                          FP_VXZDZ  | FP_VXIMZ  | FP_VXVC   | FP_VXSOFT | \
                          FP_VXSQRT | FP_VXCVI)

/*****************************************************************************/
/* Vector status and control register */
#define VSCR_NJ		16 /* Vector non-java */
#define VSCR_SAT	0 /* Vector saturation */
#define vscr_nj		(((env->vscr) >> VSCR_NJ)	& 0x1)
#define vscr_sat	(((env->vscr) >> VSCR_SAT)	& 0x1)

/*****************************************************************************/
/* BookE e500 MMU registers */

#define MAS0_NV_SHIFT      0
#define MAS0_NV_MASK       (0xfff << MAS0_NV_SHIFT)

#define MAS0_WQ_SHIFT      12
#define MAS0_WQ_MASK       (3 << MAS0_WQ_SHIFT)
/* Write TLB entry regardless of reservation */
#define MAS0_WQ_ALWAYS     (0 << MAS0_WQ_SHIFT)
/* Write TLB entry only already in use */
#define MAS0_WQ_COND       (1 << MAS0_WQ_SHIFT)
/* Clear TLB entry */
#define MAS0_WQ_CLR_RSRV   (2 << MAS0_WQ_SHIFT)

#define MAS0_HES_SHIFT     14
#define MAS0_HES           (1 << MAS0_HES_SHIFT)

#define MAS0_ESEL_SHIFT    16
#define MAS0_ESEL_MASK     (0xfff << MAS0_ESEL_SHIFT)

#define MAS0_TLBSEL_SHIFT  28
#define MAS0_TLBSEL_MASK   (3 << MAS0_TLBSEL_SHIFT)
#define MAS0_TLBSEL_TLB0   (0 << MAS0_TLBSEL_SHIFT)
#define MAS0_TLBSEL_TLB1   (1 << MAS0_TLBSEL_SHIFT)
#define MAS0_TLBSEL_TLB2   (2 << MAS0_TLBSEL_SHIFT)
#define MAS0_TLBSEL_TLB3   (3 << MAS0_TLBSEL_SHIFT)

#define MAS0_ATSEL_SHIFT   31
#define MAS0_ATSEL         (1 << MAS0_ATSEL_SHIFT)
#define MAS0_ATSEL_TLB     0
#define MAS0_ATSEL_LRAT    MAS0_ATSEL

#define MAS1_TSIZE_SHIFT   7
#define MAS1_TSIZE_MASK    (0x1f << MAS1_TSIZE_SHIFT)

#define MAS1_TS_SHIFT      12
#define MAS1_TS            (1 << MAS1_TS_SHIFT)

#define MAS1_IND_SHIFT     13
#define MAS1_IND           (1 << MAS1_IND_SHIFT)

#define MAS1_TID_SHIFT     16
#define MAS1_TID_MASK      (0x3fff << MAS1_TID_SHIFT)

#define MAS1_IPROT_SHIFT   30
#define MAS1_IPROT         (1 << MAS1_IPROT_SHIFT)

#define MAS1_VALID_SHIFT   31
#define MAS1_VALID         0x80000000

#define MAS2_EPN_SHIFT     12
#define MAS2_EPN_MASK      (~0ULL << MAS2_EPN_SHIFT)

#define MAS2_ACM_SHIFT     6
#define MAS2_ACM           (1 << MAS2_ACM_SHIFT)

#define MAS2_VLE_SHIFT     5
#define MAS2_VLE           (1 << MAS2_VLE_SHIFT)

#define MAS2_W_SHIFT       4
#define MAS2_W             (1 << MAS2_W_SHIFT)

#define MAS2_I_SHIFT       3
#define MAS2_I             (1 << MAS2_I_SHIFT)

#define MAS2_M_SHIFT       2
#define MAS2_M             (1 << MAS2_M_SHIFT)

#define MAS2_G_SHIFT       1
#define MAS2_G             (1 << MAS2_G_SHIFT)

#define MAS2_E_SHIFT       0
#define MAS2_E             (1 << MAS2_E_SHIFT)

#define MAS3_RPN_SHIFT     12
#define MAS3_RPN_MASK      (0xfffff << MAS3_RPN_SHIFT)

#define MAS3_U0                 0x00000200
#define MAS3_U1                 0x00000100
#define MAS3_U2                 0x00000080
#define MAS3_U3                 0x00000040
#define MAS3_UX                 0x00000020
#define MAS3_SX                 0x00000010
#define MAS3_UW                 0x00000008
#define MAS3_SW                 0x00000004
#define MAS3_UR                 0x00000002
#define MAS3_SR                 0x00000001
#define MAS3_SPSIZE_SHIFT       1
#define MAS3_SPSIZE_MASK        (0x3e << MAS3_SPSIZE_SHIFT)

#define MAS4_TLBSELD_SHIFT      MAS0_TLBSEL_SHIFT
#define MAS4_TLBSELD_MASK       MAS0_TLBSEL_MASK
#define MAS4_TIDSELD_MASK       0x00030000
#define MAS4_TIDSELD_PID0       0x00000000
#define MAS4_TIDSELD_PID1       0x00010000
#define MAS4_TIDSELD_PID2       0x00020000
#define MAS4_TIDSELD_PIDZ       0x00030000
#define MAS4_INDD               0x00008000      /* Default IND */
#define MAS4_TSIZED_SHIFT       MAS1_TSIZE_SHIFT
#define MAS4_TSIZED_MASK        MAS1_TSIZE_MASK
#define MAS4_ACMD               0x00000040
#define MAS4_VLED               0x00000020
#define MAS4_WD                 0x00000010
#define MAS4_ID                 0x00000008
#define MAS4_MD                 0x00000004
#define MAS4_GD                 0x00000002
#define MAS4_ED                 0x00000001
#define MAS4_WIMGED_MASK        0x0000001f      /* Default WIMGE */
#define MAS4_WIMGED_SHIFT       0

#define MAS5_SGS                0x80000000
#define MAS5_SLPID_MASK         0x00000fff

#define MAS6_SPID0              0x3fff0000
#define MAS6_SPID1              0x00007ffe
#define MAS6_ISIZE(x)           MAS1_TSIZE(x)
#define MAS6_SAS                0x00000001
#define MAS6_SPID               MAS6_SPID0
#define MAS6_SIND               0x00000002      /* Indirect page */
#define MAS6_SIND_SHIFT         1
#define MAS6_SPID_MASK          0x3fff0000
#define MAS6_SPID_SHIFT         16
#define MAS6_ISIZE_MASK         0x00000f80
#define MAS6_ISIZE_SHIFT        7

#define MAS7_RPN                0xffffffff

#define MAS8_TGS                0x80000000
#define MAS8_VF                 0x40000000
#define MAS8_TLBPID             0x00000fff

/* Bit definitions for MMUCFG */
#define MMUCFG_MAVN     0x00000003      /* MMU Architecture Version Number */
#define MMUCFG_MAVN_V1  0x00000000      /* v1.0 */
#define MMUCFG_MAVN_V2  0x00000001      /* v2.0 */
#define MMUCFG_NTLBS    0x0000000c      /* Number of TLBs */
#define MMUCFG_PIDSIZE  0x000007c0      /* PID Reg Size */
#define MMUCFG_TWC      0x00008000      /* TLB Write Conditional (v2.0) */
#define MMUCFG_LRAT     0x00010000      /* LRAT Supported (v2.0) */
#define MMUCFG_RASIZE   0x00fe0000      /* Real Addr Size */
#define MMUCFG_LPIDSIZE 0x0f000000      /* LPID Reg Size */

/* Bit definitions for MMUCSR0 */
#define MMUCSR0_TLB1FI  0x00000002      /* TLB1 Flash invalidate */
#define MMUCSR0_TLB0FI  0x00000004      /* TLB0 Flash invalidate */
#define MMUCSR0_TLB2FI  0x00000040      /* TLB2 Flash invalidate */
#define MMUCSR0_TLB3FI  0x00000020      /* TLB3 Flash invalidate */
#define MMUCSR0_TLBFI   (MMUCSR0_TLB0FI | MMUCSR0_TLB1FI | \
                         MMUCSR0_TLB2FI | MMUCSR0_TLB3FI)
#define MMUCSR0_TLB0PS  0x00000780      /* TLB0 Page Size */
#define MMUCSR0_TLB1PS  0x00007800      /* TLB1 Page Size */
#define MMUCSR0_TLB2PS  0x00078000      /* TLB2 Page Size */
#define MMUCSR0_TLB3PS  0x00780000      /* TLB3 Page Size */

/* TLBnCFG encoding */
#define TLBnCFG_N_ENTRY         0x00000fff      /* number of entries */
#define TLBnCFG_HES             0x00002000      /* HW select supported */
#define TLBnCFG_AVAIL           0x00004000      /* variable page size */
#define TLBnCFG_IPROT           0x00008000      /* IPROT supported */
#define TLBnCFG_GTWE            0x00010000      /* Guest can write */
#define TLBnCFG_IND             0x00020000      /* IND entries supported */
#define TLBnCFG_PT              0x00040000      /* Can load from page table */
#define TLBnCFG_MINSIZE         0x00f00000      /* Minimum Page Size (v1.0) */
#define TLBnCFG_MINSIZE_SHIFT   20
#define TLBnCFG_MAXSIZE         0x000f0000      /* Maximum Page Size (v1.0) */
#define TLBnCFG_MAXSIZE_SHIFT   16
#define TLBnCFG_ASSOC           0xff000000      /* Associativity */
#define TLBnCFG_ASSOC_SHIFT     24

/* TLBnPS encoding */
#define TLBnPS_4K               0x00000004
#define TLBnPS_8K               0x00000008
#define TLBnPS_16K              0x00000010
#define TLBnPS_32K              0x00000020
#define TLBnPS_64K              0x00000040
#define TLBnPS_128K             0x00000080
#define TLBnPS_256K             0x00000100
#define TLBnPS_512K             0x00000200
#define TLBnPS_1M               0x00000400
#define TLBnPS_2M               0x00000800
#define TLBnPS_4M               0x00001000
#define TLBnPS_8M               0x00002000
#define TLBnPS_16M              0x00004000
#define TLBnPS_32M              0x00008000
#define TLBnPS_64M              0x00010000
#define TLBnPS_128M             0x00020000
#define TLBnPS_256M             0x00040000
#define TLBnPS_512M             0x00080000
#define TLBnPS_1G               0x00100000
#define TLBnPS_2G               0x00200000
#define TLBnPS_4G               0x00400000
#define TLBnPS_8G               0x00800000
#define TLBnPS_16G              0x01000000
#define TLBnPS_32G              0x02000000
#define TLBnPS_64G              0x04000000
#define TLBnPS_128G             0x08000000
#define TLBnPS_256G             0x10000000

/* tlbilx action encoding */
#define TLBILX_T_ALL                    0
#define TLBILX_T_TID                    1
#define TLBILX_T_FULLMATCH              3
#define TLBILX_T_CLASS0                 4
#define TLBILX_T_CLASS1                 5
#define TLBILX_T_CLASS2                 6
#define TLBILX_T_CLASS3                 7

/* BookE 2.06 helper defines */

#define BOOKE206_FLUSH_TLB0    (1 << 0)
#define BOOKE206_FLUSH_TLB1    (1 << 1)
#define BOOKE206_FLUSH_TLB2    (1 << 2)
#define BOOKE206_FLUSH_TLB3    (1 << 3)

/* number of possible TLBs */
#define BOOKE206_MAX_TLBN      4

#define EPID_EPID_SHIFT 0x0
#define EPID_EPID 0xFF
#define EPID_ELPID_SHIFT 0x10
#define EPID_ELPID 0x3F0000
#define EPID_EGS 0x20000000
#define EPID_EGS_SHIFT 29
#define EPID_EAS 0x40000000
#define EPID_EAS_SHIFT 30
#define EPID_EPR 0x80000000
#define EPID_EPR_SHIFT 31
/* We don't support EGS and ELPID */
#define EPID_MASK (EPID_EPID | EPID_EAS | EPID_EPR)

/*****************************************************************************/
/* Server and Embedded Processor Control */

#define DBELL_TYPE_SHIFT               27
#define DBELL_TYPE_MASK                (0x1f << DBELL_TYPE_SHIFT)
#define DBELL_TYPE_DBELL               (0x00 << DBELL_TYPE_SHIFT)
#define DBELL_TYPE_DBELL_CRIT          (0x01 << DBELL_TYPE_SHIFT)
#define DBELL_TYPE_G_DBELL             (0x02 << DBELL_TYPE_SHIFT)
#define DBELL_TYPE_G_DBELL_CRIT        (0x03 << DBELL_TYPE_SHIFT)
#define DBELL_TYPE_G_DBELL_MC          (0x04 << DBELL_TYPE_SHIFT)

#define DBELL_TYPE_DBELL_SERVER        (0x05 << DBELL_TYPE_SHIFT)

#define DBELL_BRDCAST                  PPC_BIT(37)
#define DBELL_LPIDTAG_SHIFT            14
#define DBELL_LPIDTAG_MASK             (0xfff << DBELL_LPIDTAG_SHIFT)
#define DBELL_PIRTAG_MASK              0x3fff

#define DBELL_PROCIDTAG_MASK           PPC_BITMASK(44, 63)

#define PPC_PAGE_SIZES_MAX_SZ   8

struct ppc_radix_page_info {
    uint32_t count;
    uint32_t entries[PPC_PAGE_SIZES_MAX_SZ];
};

/*****************************************************************************/
/* The whole PowerPC CPU context */

/* PowerPC needs eight modes for different hypervisor/supervisor/guest +
 * real/paged mode combinations. The other two modes are for external PID
 * load/store.
 */
#define NB_MMU_MODES    10
#define MMU_MODE8_SUFFIX _epl
#define MMU_MODE9_SUFFIX _eps
#define PPC_TLB_EPID_LOAD 8
#define PPC_TLB_EPID_STORE 9

#define PPC_CPU_OPCODES_LEN          0x40
#define PPC_CPU_INDIRECT_OPCODES_LEN 0x20

struct CPUPPCState {
    /* First are the most commonly used resources
     * during translated code execution
     */
    /* general purpose registers */
    target_ulong gpr[32];
    /* Storage for GPR MSB, used by the SPE extension */
    target_ulong gprh[32];
    /* LR */
    target_ulong lr;
    /* CTR */
    target_ulong ctr;
    /* condition register */
    uint32_t crf[8];
#if defined(TARGET_PPC64)
    /* CFAR */
    target_ulong cfar;
#endif
    /* XER (with SO, OV, CA split out) */
    target_ulong xer;
    target_ulong so;
    target_ulong ov;
    target_ulong ca;
    target_ulong ov32;
    target_ulong ca32;
    /* Reservation address */
    target_ulong reserve_addr;
    /* Reservation value */
    target_ulong reserve_val;
    target_ulong reserve_val2;

    /* Those ones are used in supervisor mode only */
    /* machine state register */
    target_ulong msr;
    /* temporary general purpose registers */
    target_ulong tgpr[4]; /* Used to speed-up TLB assist handlers */

    /* Floating point execution context */
    float_status fp_status;
    /* floating point registers */
    float64 fpr[32];
    /* floating point status and control register */
    target_ulong fpscr;

    /* Next instruction pointer */
    target_ulong nip;

    /* High part of 128-bit helper return.  */
    uint64_t retxh;

    int access_type; /* when a memory exception occurs, the access
                        type is stored here */

    CPU_COMMON

    /* MMU context - only relevant for full system emulation */
#if !defined(CONFIG_USER_ONLY)
#if defined(TARGET_PPC64)
    /* PowerPC 64 SLB area */
    ppc_slb_t slb[MAX_SLB_ENTRIES];
    /* tcg TLB needs flush (deferred slb inval instruction typically) */
#endif
    /* segment registers */
    target_ulong sr[32];
    /* BATs */
    uint32_t nb_BATs;
    target_ulong DBAT[2][8];
    target_ulong IBAT[2][8];
    /* PowerPC TLB registers (for 4xx, e500 and 60x software driven TLBs) */
    int32_t nb_tlb;      /* Total number of TLB                              */
    int tlb_per_way; /* Speed-up helper: used to avoid divisions at run time */
    int nb_ways;     /* Number of ways in the TLB set                        */
    int last_way;    /* Last used way used to allocate TLB in a LRU way      */
    int id_tlbs;     /* If 1, MMU has separated TLBs for instructions & data */
    int nb_pids;     /* Number of available PID registers                    */
    int tlb_type;    /* Type of TLB we're dealing with                       */
    ppc_tlb_t tlb;   /* TLB is optional. Allocate them only if needed        */
    /* 403 dedicated access protection registers */
    target_ulong pb[4];
    bool tlb_dirty;   /* Set to non-zero when modifying TLB                  */
    bool kvm_sw_tlb;  /* non-zero if KVM SW TLB API is active                */
    uint32_t tlb_need_flush; /* Delayed flush needed */
#define TLB_NEED_LOCAL_FLUSH   0x1
#define TLB_NEED_GLOBAL_FLUSH  0x2
#endif

    /* Other registers */
    /* Special purpose registers */
    target_ulong spr[1024];
    ppc_spr_t spr_cb[1024];
    /* Altivec registers */
    ppc_avr_t avr[32];
    uint32_t vscr;
    /* VSX registers */
    uint64_t vsr[32];
    /* SPE registers */
    uint64_t spe_acc;
    uint32_t spe_fscr;
    /* SPE and Altivec can share a status since they will never be used
     * simultaneously */
    float_status vec_status;

    /* Internal devices resources */
    /* Time base and decrementer */
    ppc_tb_t *tb_env;
    /* Device control registers */
    ppc_dcr_t *dcr_env;

    int dcache_line_size;
    int icache_line_size;

    /* Those resources are used during exception processing */
    /* CPU model definition */
    target_ulong msr_mask;
    powerpc_mmu_t mmu_model;
    powerpc_excp_t excp_model;
    powerpc_input_t bus_model;
    int bfd_mach;
    uint32_t flags;
    uint64_t insns_flags;
    uint64_t insns_flags2;
#if defined(TARGET_PPC64)
    ppc_slb_t vrma_slb;
    target_ulong rmls;
#endif

    int error_code;
    uint32_t pending_interrupts;
#if !defined(CONFIG_USER_ONLY)
    /* This is the IRQ controller, which is implementation dependent
     * and only relevant when emulating a complete machine.
     */
    uint32_t irq_input_state;
    void **irq_inputs;
    /* Exception vectors */
    target_ulong excp_vectors[POWERPC_EXCP_NB];
    target_ulong excp_prefix;
    target_ulong ivor_mask;
    target_ulong ivpr_mask;
    target_ulong hreset_vector;
    hwaddr mpic_iack;
    /* true when the external proxy facility mode is enabled */
    bool mpic_proxy;
    /* set when the processor has an HV mode, thus HV priv
     * instructions and SPRs are diallowed if MSR:HV is 0
     */
    bool has_hv_mode;
    /* On P7/P8, set when in PM state, we need to handle resume
     * in a special way (such as routing some resume causes to
     * 0x100), so flag this here.
     */
    bool in_pm_state;
#endif

    /* Those resources are used only during code translation */
    /* opcode handlers */
    opc_handler_t *opcodes[PPC_CPU_OPCODES_LEN];

    /* Those resources are used only in QEMU core */
    target_ulong hflags;      /* hflags is a MSR & HFLAGS_MASK         */
    target_ulong hflags_nmsr; /* specific hflags, not coming from MSR */
    int immu_idx;         /* precomputed MMU index to speed up insn access */
    int dmmu_idx;         /* precomputed MMU index to speed up data accesses */

    /* Power management */
    int (*check_pow)(CPUPPCState *env);

#if !defined(CONFIG_USER_ONLY)
    void *load_info;    /* Holds boot loading state.  */
#endif

    /* booke timers */

    /* Specifies bit locations of the Time Base used to signal a fixed timer
     * exception on a transition from 0 to 1. (watchdog or fixed-interval timer)
     *
     * 0 selects the least significant bit.
     * 63 selects the most significant bit.
     */
    uint8_t fit_period[4];
    uint8_t wdt_period[4];

    /* Transactional memory state */
    target_ulong tm_gpr[32];
    ppc_avr_t tm_vsr[64];
    uint64_t tm_cr;
    uint64_t tm_lr;
    uint64_t tm_ctr;
    uint64_t tm_fpscr;
    uint64_t tm_amr;
    uint64_t tm_ppr;
    uint64_t tm_vrsave;
    uint32_t tm_vscr;
    uint64_t tm_dscr;
    uint64_t tm_tar;
};

#define SET_FIT_PERIOD(a_, b_, c_, d_)          \
do {                                            \
    env->fit_period[0] = (a_);                  \
    env->fit_period[1] = (b_);                  \
    env->fit_period[2] = (c_);                  \
    env->fit_period[3] = (d_);                  \
 } while (0)

#define SET_WDT_PERIOD(a_, b_, c_, d_)          \
do {                                            \
    env->wdt_period[0] = (a_);                  \
    env->wdt_period[1] = (b_);                  \
    env->wdt_period[2] = (c_);                  \
    env->wdt_period[3] = (d_);                  \
 } while (0)

typedef struct PPCVirtualHypervisor PPCVirtualHypervisor;
typedef struct PPCVirtualHypervisorClass PPCVirtualHypervisorClass;

/**
 * PowerPCCPU:
 * @env: #CPUPPCState
 * @vcpu_id: vCPU identifier given to KVM
 * @compat_pvr: Current logical PVR, zero if in "raw" mode
 *
 * A PowerPC CPU.
 */
struct PowerPCCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUPPCState env;
    int vcpu_id;
    uint32_t compat_pvr;
    PPCVirtualHypervisor *vhyp;
    Object *intc;
    void *machine_data;
    int32_t node_id; /* NUMA node this CPU belongs to */
    PPCHash64Options *hash64_opts;

    /* Fields related to migration compatibility hacks */
    bool pre_2_8_migration;
    target_ulong mig_msr_mask;
    uint64_t mig_insns_flags;
    uint64_t mig_insns_flags2;
    uint32_t mig_nb_BATs;
    bool pre_2_10_migration;
    bool pre_3_0_migration;
    int32_t mig_slb_nr;
};

static inline PowerPCCPU *ppc_env_get_cpu(CPUPPCState *env)
{
    return container_of(env, PowerPCCPU, env);
}

#define ENV_GET_CPU(e) CPU(ppc_env_get_cpu(e))

#define ENV_OFFSET offsetof(PowerPCCPU, env)

PowerPCCPUClass *ppc_cpu_class_by_pvr(uint32_t pvr);
PowerPCCPUClass *ppc_cpu_class_by_pvr_mask(uint32_t pvr);
PowerPCCPUClass *ppc_cpu_get_family_class(PowerPCCPUClass *pcc);

struct PPCVirtualHypervisor {
    Object parent;
};

struct PPCVirtualHypervisorClass {
    InterfaceClass parent;
    void (*hypercall)(PPCVirtualHypervisor *vhyp, PowerPCCPU *cpu);
    hwaddr (*hpt_mask)(PPCVirtualHypervisor *vhyp);
    const ppc_hash_pte64_t *(*map_hptes)(PPCVirtualHypervisor *vhyp,
                                         hwaddr ptex, int n);
    void (*unmap_hptes)(PPCVirtualHypervisor *vhyp,
                        const ppc_hash_pte64_t *hptes,
                        hwaddr ptex, int n);
    void (*store_hpte)(PPCVirtualHypervisor *vhyp, hwaddr ptex,
                       uint64_t pte0, uint64_t pte1);
    uint64_t (*get_patbe)(PPCVirtualHypervisor *vhyp);
    target_ulong (*encode_hpt_for_kvm_pr)(PPCVirtualHypervisor *vhyp);
};

#define TYPE_PPC_VIRTUAL_HYPERVISOR "ppc-virtual-hypervisor"
#define PPC_VIRTUAL_HYPERVISOR(obj)                 \
    OBJECT_CHECK(PPCVirtualHypervisor, (obj), TYPE_PPC_VIRTUAL_HYPERVISOR)
#define PPC_VIRTUAL_HYPERVISOR_CLASS(klass)         \
    OBJECT_CLASS_CHECK(PPCVirtualHypervisorClass, (klass), \
                       TYPE_PPC_VIRTUAL_HYPERVISOR)
#define PPC_VIRTUAL_HYPERVISOR_GET_CLASS(obj) \
    OBJECT_GET_CLASS(PPCVirtualHypervisorClass, (obj), \
                     TYPE_PPC_VIRTUAL_HYPERVISOR)

void ppc_cpu_do_interrupt(CPUState *cpu);
bool ppc_cpu_exec_interrupt(CPUState *cpu, int int_req);
void ppc_cpu_dump_state(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                        int flags);
void ppc_cpu_dump_statistics(CPUState *cpu, FILE *f,
                             fprintf_function cpu_fprintf, int flags);
hwaddr ppc_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int ppc_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int ppc_cpu_gdb_read_register_apple(CPUState *cpu, uint8_t *buf, int reg);
int ppc_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
int ppc_cpu_gdb_write_register_apple(CPUState *cpu, uint8_t *buf, int reg);
int ppc64_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, void *opaque);
int ppc32_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, void *opaque);
#ifndef CONFIG_USER_ONLY
void ppc_cpu_do_system_reset(CPUState *cs);
extern const struct VMStateDescription vmstate_ppc_cpu;
#endif

/*****************************************************************************/
void ppc_translate_init(void);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_ppc_signal_handler (int host_signum, void *pinfo,
                            void *puc);
#if defined(CONFIG_USER_ONLY)
int ppc_cpu_handle_mmu_fault(CPUState *cpu, vaddr address, int size, int rw,
                             int mmu_idx);
#endif

#if !defined(CONFIG_USER_ONLY)
void ppc_store_sdr1 (CPUPPCState *env, target_ulong value);
void ppc_store_ptcr(CPUPPCState *env, target_ulong value);
#endif /* !defined(CONFIG_USER_ONLY) */
void ppc_store_msr (CPUPPCState *env, target_ulong value);

void ppc_cpu_list (FILE *f, fprintf_function cpu_fprintf);

/* Time-base and decrementer management */
#ifndef NO_CPU_IO_DEFS
uint64_t cpu_ppc_load_tbl (CPUPPCState *env);
uint32_t cpu_ppc_load_tbu (CPUPPCState *env);
void cpu_ppc_store_tbu (CPUPPCState *env, uint32_t value);
void cpu_ppc_store_tbl (CPUPPCState *env, uint32_t value);
uint64_t cpu_ppc_load_atbl (CPUPPCState *env);
uint32_t cpu_ppc_load_atbu (CPUPPCState *env);
void cpu_ppc_store_atbl (CPUPPCState *env, uint32_t value);
void cpu_ppc_store_atbu (CPUPPCState *env, uint32_t value);
bool ppc_decr_clear_on_delivery(CPUPPCState *env);
uint32_t cpu_ppc_load_decr (CPUPPCState *env);
void cpu_ppc_store_decr (CPUPPCState *env, uint32_t value);
uint32_t cpu_ppc_load_hdecr (CPUPPCState *env);
void cpu_ppc_store_hdecr (CPUPPCState *env, uint32_t value);
uint64_t cpu_ppc_load_purr (CPUPPCState *env);
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
void cpu_ppc_set_vhyp(PowerPCCPU *cpu, PPCVirtualHypervisor *vhyp);
#endif
#endif

void store_fpscr(CPUPPCState *env, uint64_t arg, uint32_t mask);

static inline uint64_t ppc_dump_gpr(CPUPPCState *env, int gprn)
{
    uint64_t gprv;

    gprv = env->gpr[gprn];
    if (env->flags & POWERPC_FLAG_SPE) {
        /* If the CPU implements the SPE extension, we have to get the
         * high bits of the GPR from the gprh storage area
         */
        gprv &= 0xFFFFFFFFULL;
        gprv |= (uint64_t)env->gprh[gprn] << 32;
    }

    return gprv;
}

/* Device control registers */
int ppc_dcr_read (ppc_dcr_t *dcr_env, int dcrn, uint32_t *valp);
int ppc_dcr_write (ppc_dcr_t *dcr_env, int dcrn, uint32_t val);

#define POWERPC_CPU_TYPE_SUFFIX "-" TYPE_POWERPC_CPU
#define POWERPC_CPU_TYPE_NAME(model) model POWERPC_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_POWERPC_CPU

#define cpu_signal_handler cpu_ppc_signal_handler
#define cpu_list ppc_cpu_list

/* MMU modes definitions */
#define MMU_USER_IDX 0
static inline int cpu_mmu_index (CPUPPCState *env, bool ifetch)
{
    return ifetch ? env->immu_idx : env->dmmu_idx;
}

/* Compatibility modes */
#if defined(TARGET_PPC64)
bool ppc_check_compat(PowerPCCPU *cpu, uint32_t compat_pvr,
                      uint32_t min_compat_pvr, uint32_t max_compat_pvr);
bool ppc_type_check_compat(const char *cputype, uint32_t compat_pvr,
                           uint32_t min_compat_pvr, uint32_t max_compat_pvr);

void ppc_set_compat(PowerPCCPU *cpu, uint32_t compat_pvr, Error **errp);

#if !defined(CONFIG_USER_ONLY)
void ppc_set_compat_all(uint32_t compat_pvr, Error **errp);
#endif
int ppc_compat_max_vthreads(PowerPCCPU *cpu);
void ppc_compat_add_property(Object *obj, const char *name,
                             uint32_t *compat_pvr, const char *basedesc,
                             Error **errp);
#endif /* defined(TARGET_PPC64) */

#include "exec/cpu-all.h"

/*****************************************************************************/
/* CRF definitions */
#define CRF_LT_BIT    3
#define CRF_GT_BIT    2
#define CRF_EQ_BIT    1
#define CRF_SO_BIT    0
#define CRF_LT        (1 << CRF_LT_BIT)
#define CRF_GT        (1 << CRF_GT_BIT)
#define CRF_EQ        (1 << CRF_EQ_BIT)
#define CRF_SO        (1 << CRF_SO_BIT)
/* For SPE extensions */
#define CRF_CH        (1 << CRF_LT_BIT)
#define CRF_CL        (1 << CRF_GT_BIT)
#define CRF_CH_OR_CL  (1 << CRF_EQ_BIT)
#define CRF_CH_AND_CL (1 << CRF_SO_BIT)

/* XER definitions */
#define XER_SO  31
#define XER_OV  30
#define XER_CA  29
#define XER_OV32  19
#define XER_CA32  18
#define XER_CMP  8
#define XER_BC   0
#define xer_so  (env->so)
#define xer_ov  (env->ov)
#define xer_ca  (env->ca)
#define xer_ov32  (env->ov)
#define xer_ca32  (env->ca)
#define xer_cmp ((env->xer >> XER_CMP) & 0xFF)
#define xer_bc  ((env->xer >> XER_BC)  & 0x7F)

/* SPR definitions */
#define SPR_MQ                (0x000)
#define SPR_XER               (0x001)
#define SPR_601_VRTCU         (0x004)
#define SPR_601_VRTCL         (0x005)
#define SPR_601_UDECR         (0x006)
#define SPR_LR                (0x008)
#define SPR_CTR               (0x009)
#define SPR_UAMR              (0x00D)
#define SPR_DSCR              (0x011)
#define SPR_DSISR             (0x012)
#define SPR_DAR               (0x013) /* DAE for PowerPC 601 */
#define SPR_601_RTCU          (0x014)
#define SPR_601_RTCL          (0x015)
#define SPR_DECR              (0x016)
#define SPR_SDR1              (0x019)
#define SPR_SRR0              (0x01A)
#define SPR_SRR1              (0x01B)
#define SPR_CFAR              (0x01C)
#define SPR_AMR               (0x01D)
#define SPR_ACOP              (0x01F)
#define SPR_BOOKE_PID         (0x030)
#define SPR_BOOKS_PID         (0x030)
#define SPR_BOOKE_DECAR       (0x036)
#define SPR_BOOKE_CSRR0       (0x03A)
#define SPR_BOOKE_CSRR1       (0x03B)
#define SPR_BOOKE_DEAR        (0x03D)
#define SPR_IAMR              (0x03D)
#define SPR_BOOKE_ESR         (0x03E)
#define SPR_BOOKE_IVPR        (0x03F)
#define SPR_MPC_EIE           (0x050)
#define SPR_MPC_EID           (0x051)
#define SPR_MPC_NRI           (0x052)
#define SPR_TFHAR             (0x080)
#define SPR_TFIAR             (0x081)
#define SPR_TEXASR            (0x082)
#define SPR_TEXASRU           (0x083)
#define SPR_UCTRL             (0x088)
#define SPR_TIDR              (0x090)
#define SPR_MPC_CMPA          (0x090)
#define SPR_MPC_CMPB          (0x091)
#define SPR_MPC_CMPC          (0x092)
#define SPR_MPC_CMPD          (0x093)
#define SPR_MPC_ECR           (0x094)
#define SPR_MPC_DER           (0x095)
#define SPR_MPC_COUNTA        (0x096)
#define SPR_MPC_COUNTB        (0x097)
#define SPR_CTRL              (0x098)
#define SPR_MPC_CMPE          (0x098)
#define SPR_MPC_CMPF          (0x099)
#define SPR_FSCR              (0x099)
#define SPR_MPC_CMPG          (0x09A)
#define SPR_MPC_CMPH          (0x09B)
#define SPR_MPC_LCTRL1        (0x09C)
#define SPR_MPC_LCTRL2        (0x09D)
#define SPR_UAMOR             (0x09D)
#define SPR_MPC_ICTRL         (0x09E)
#define SPR_MPC_BAR           (0x09F)
#define SPR_PSPB              (0x09F)
#define SPR_DAWR              (0x0B4)
#define SPR_RPR               (0x0BA)
#define SPR_CIABR             (0x0BB)
#define SPR_DAWRX             (0x0BC)
#define SPR_HFSCR             (0x0BE)
#define SPR_VRSAVE            (0x100)
#define SPR_USPRG0            (0x100)
#define SPR_USPRG1            (0x101)
#define SPR_USPRG2            (0x102)
#define SPR_USPRG3            (0x103)
#define SPR_USPRG4            (0x104)
#define SPR_USPRG5            (0x105)
#define SPR_USPRG6            (0x106)
#define SPR_USPRG7            (0x107)
#define SPR_VTBL              (0x10C)
#define SPR_VTBU              (0x10D)
#define SPR_SPRG0             (0x110)
#define SPR_SPRG1             (0x111)
#define SPR_SPRG2             (0x112)
#define SPR_SPRG3             (0x113)
#define SPR_SPRG4             (0x114)
#define SPR_SCOMC             (0x114)
#define SPR_SPRG5             (0x115)
#define SPR_SCOMD             (0x115)
#define SPR_SPRG6             (0x116)
#define SPR_SPRG7             (0x117)
#define SPR_ASR               (0x118)
#define SPR_EAR               (0x11A)
#define SPR_TBL               (0x11C)
#define SPR_TBU               (0x11D)
#define SPR_TBU40             (0x11E)
#define SPR_SVR               (0x11E)
#define SPR_BOOKE_PIR         (0x11E)
#define SPR_PVR               (0x11F)
#define SPR_HSPRG0            (0x130)
#define SPR_BOOKE_DBSR        (0x130)
#define SPR_HSPRG1            (0x131)
#define SPR_HDSISR            (0x132)
#define SPR_HDAR              (0x133)
#define SPR_BOOKE_EPCR        (0x133)
#define SPR_SPURR             (0x134)
#define SPR_BOOKE_DBCR0       (0x134)
#define SPR_IBCR              (0x135)
#define SPR_PURR              (0x135)
#define SPR_BOOKE_DBCR1       (0x135)
#define SPR_DBCR              (0x136)
#define SPR_HDEC              (0x136)
#define SPR_BOOKE_DBCR2       (0x136)
#define SPR_HIOR              (0x137)
#define SPR_MBAR              (0x137)
#define SPR_RMOR              (0x138)
#define SPR_BOOKE_IAC1        (0x138)
#define SPR_HRMOR             (0x139)
#define SPR_BOOKE_IAC2        (0x139)
#define SPR_HSRR0             (0x13A)
#define SPR_BOOKE_IAC3        (0x13A)
#define SPR_HSRR1             (0x13B)
#define SPR_BOOKE_IAC4        (0x13B)
#define SPR_BOOKE_DAC1        (0x13C)
#define SPR_MMCRH             (0x13C)
#define SPR_DABR2             (0x13D)
#define SPR_BOOKE_DAC2        (0x13D)
#define SPR_TFMR              (0x13D)
#define SPR_BOOKE_DVC1        (0x13E)
#define SPR_LPCR              (0x13E)
#define SPR_BOOKE_DVC2        (0x13F)
#define SPR_LPIDR             (0x13F)
#define SPR_BOOKE_TSR         (0x150)
#define SPR_HMER              (0x150)
#define SPR_HMEER             (0x151)
#define SPR_PCR               (0x152)
#define SPR_BOOKE_LPIDR       (0x152)
#define SPR_BOOKE_TCR         (0x154)
#define SPR_BOOKE_TLB0PS      (0x158)
#define SPR_BOOKE_TLB1PS      (0x159)
#define SPR_BOOKE_TLB2PS      (0x15A)
#define SPR_BOOKE_TLB3PS      (0x15B)
#define SPR_AMOR              (0x15D)
#define SPR_BOOKE_MAS7_MAS3   (0x174)
#define SPR_BOOKE_IVOR0       (0x190)
#define SPR_BOOKE_IVOR1       (0x191)
#define SPR_BOOKE_IVOR2       (0x192)
#define SPR_BOOKE_IVOR3       (0x193)
#define SPR_BOOKE_IVOR4       (0x194)
#define SPR_BOOKE_IVOR5       (0x195)
#define SPR_BOOKE_IVOR6       (0x196)
#define SPR_BOOKE_IVOR7       (0x197)
#define SPR_BOOKE_IVOR8       (0x198)
#define SPR_BOOKE_IVOR9       (0x199)
#define SPR_BOOKE_IVOR10      (0x19A)
#define SPR_BOOKE_IVOR11      (0x19B)
#define SPR_BOOKE_IVOR12      (0x19C)
#define SPR_BOOKE_IVOR13      (0x19D)
#define SPR_BOOKE_IVOR14      (0x19E)
#define SPR_BOOKE_IVOR15      (0x19F)
#define SPR_BOOKE_IVOR38      (0x1B0)
#define SPR_BOOKE_IVOR39      (0x1B1)
#define SPR_BOOKE_IVOR40      (0x1B2)
#define SPR_BOOKE_IVOR41      (0x1B3)
#define SPR_BOOKE_IVOR42      (0x1B4)
#define SPR_BOOKE_GIVOR2      (0x1B8)
#define SPR_BOOKE_GIVOR3      (0x1B9)
#define SPR_BOOKE_GIVOR4      (0x1BA)
#define SPR_BOOKE_GIVOR8      (0x1BB)
#define SPR_BOOKE_GIVOR13     (0x1BC)
#define SPR_BOOKE_GIVOR14     (0x1BD)
#define SPR_TIR               (0x1BE)
#define SPR_PTCR              (0x1D0)
#define SPR_BOOKE_SPEFSCR     (0x200)
#define SPR_Exxx_BBEAR        (0x201)
#define SPR_Exxx_BBTAR        (0x202)
#define SPR_Exxx_L1CFG0       (0x203)
#define SPR_Exxx_L1CFG1       (0x204)
#define SPR_Exxx_NPIDR        (0x205)
#define SPR_ATBL              (0x20E)
#define SPR_ATBU              (0x20F)
#define SPR_IBAT0U            (0x210)
#define SPR_BOOKE_IVOR32      (0x210)
#define SPR_RCPU_MI_GRA       (0x210)
#define SPR_IBAT0L            (0x211)
#define SPR_BOOKE_IVOR33      (0x211)
#define SPR_IBAT1U            (0x212)
#define SPR_BOOKE_IVOR34      (0x212)
#define SPR_IBAT1L            (0x213)
#define SPR_BOOKE_IVOR35      (0x213)
#define SPR_IBAT2U            (0x214)
#define SPR_BOOKE_IVOR36      (0x214)
#define SPR_IBAT2L            (0x215)
#define SPR_BOOKE_IVOR37      (0x215)
#define SPR_IBAT3U            (0x216)
#define SPR_IBAT3L            (0x217)
#define SPR_DBAT0U            (0x218)
#define SPR_RCPU_L2U_GRA      (0x218)
#define SPR_DBAT0L            (0x219)
#define SPR_DBAT1U            (0x21A)
#define SPR_DBAT1L            (0x21B)
#define SPR_DBAT2U            (0x21C)
#define SPR_DBAT2L            (0x21D)
#define SPR_DBAT3U            (0x21E)
#define SPR_DBAT3L            (0x21F)
#define SPR_IBAT4U            (0x230)
#define SPR_RPCU_BBCMCR       (0x230)
#define SPR_MPC_IC_CST        (0x230)
#define SPR_Exxx_CTXCR        (0x230)
#define SPR_IBAT4L            (0x231)
#define SPR_MPC_IC_ADR        (0x231)
#define SPR_Exxx_DBCR3        (0x231)
#define SPR_IBAT5U            (0x232)
#define SPR_MPC_IC_DAT        (0x232)
#define SPR_Exxx_DBCNT        (0x232)
#define SPR_IBAT5L            (0x233)
#define SPR_IBAT6U            (0x234)
#define SPR_IBAT6L            (0x235)
#define SPR_IBAT7U            (0x236)
#define SPR_IBAT7L            (0x237)
#define SPR_DBAT4U            (0x238)
#define SPR_RCPU_L2U_MCR      (0x238)
#define SPR_MPC_DC_CST        (0x238)
#define SPR_Exxx_ALTCTXCR     (0x238)
#define SPR_DBAT4L            (0x239)
#define SPR_MPC_DC_ADR        (0x239)
#define SPR_DBAT5U            (0x23A)
#define SPR_BOOKE_MCSRR0      (0x23A)
#define SPR_MPC_DC_DAT        (0x23A)
#define SPR_DBAT5L            (0x23B)
#define SPR_BOOKE_MCSRR1      (0x23B)
#define SPR_DBAT6U            (0x23C)
#define SPR_BOOKE_MCSR        (0x23C)
#define SPR_DBAT6L            (0x23D)
#define SPR_Exxx_MCAR         (0x23D)
#define SPR_DBAT7U            (0x23E)
#define SPR_BOOKE_DSRR0       (0x23E)
#define SPR_DBAT7L            (0x23F)
#define SPR_BOOKE_DSRR1       (0x23F)
#define SPR_BOOKE_SPRG8       (0x25C)
#define SPR_BOOKE_SPRG9       (0x25D)
#define SPR_BOOKE_MAS0        (0x270)
#define SPR_BOOKE_MAS1        (0x271)
#define SPR_BOOKE_MAS2        (0x272)
#define SPR_BOOKE_MAS3        (0x273)
#define SPR_BOOKE_MAS4        (0x274)
#define SPR_BOOKE_MAS5        (0x275)
#define SPR_BOOKE_MAS6        (0x276)
#define SPR_BOOKE_PID1        (0x279)
#define SPR_BOOKE_PID2        (0x27A)
#define SPR_MPC_DPDR          (0x280)
#define SPR_MPC_IMMR          (0x288)
#define SPR_BOOKE_TLB0CFG     (0x2B0)
#define SPR_BOOKE_TLB1CFG     (0x2B1)
#define SPR_BOOKE_TLB2CFG     (0x2B2)
#define SPR_BOOKE_TLB3CFG     (0x2B3)
#define SPR_BOOKE_EPR         (0x2BE)
#define SPR_PERF0             (0x300)
#define SPR_RCPU_MI_RBA0      (0x300)
#define SPR_MPC_MI_CTR        (0x300)
#define SPR_POWER_USIER       (0x300)
#define SPR_PERF1             (0x301)
#define SPR_RCPU_MI_RBA1      (0x301)
#define SPR_POWER_UMMCR2      (0x301)
#define SPR_PERF2             (0x302)
#define SPR_RCPU_MI_RBA2      (0x302)
#define SPR_MPC_MI_AP         (0x302)
#define SPR_POWER_UMMCRA      (0x302)
#define SPR_PERF3             (0x303)
#define SPR_RCPU_MI_RBA3      (0x303)
#define SPR_MPC_MI_EPN        (0x303)
#define SPR_POWER_UPMC1       (0x303)
#define SPR_PERF4             (0x304)
#define SPR_POWER_UPMC2       (0x304)
#define SPR_PERF5             (0x305)
#define SPR_MPC_MI_TWC        (0x305)
#define SPR_POWER_UPMC3       (0x305)
#define SPR_PERF6             (0x306)
#define SPR_MPC_MI_RPN        (0x306)
#define SPR_POWER_UPMC4       (0x306)
#define SPR_PERF7             (0x307)
#define SPR_POWER_UPMC5       (0x307)
#define SPR_PERF8             (0x308)
#define SPR_RCPU_L2U_RBA0     (0x308)
#define SPR_MPC_MD_CTR        (0x308)
#define SPR_POWER_UPMC6       (0x308)
#define SPR_PERF9             (0x309)
#define SPR_RCPU_L2U_RBA1     (0x309)
#define SPR_MPC_MD_CASID      (0x309)
#define SPR_970_UPMC7         (0X309)
#define SPR_PERFA             (0x30A)
#define SPR_RCPU_L2U_RBA2     (0x30A)
#define SPR_MPC_MD_AP         (0x30A)
#define SPR_970_UPMC8         (0X30A)
#define SPR_PERFB             (0x30B)
#define SPR_RCPU_L2U_RBA3     (0x30B)
#define SPR_MPC_MD_EPN        (0x30B)
#define SPR_POWER_UMMCR0      (0X30B)
#define SPR_PERFC             (0x30C)
#define SPR_MPC_MD_TWB        (0x30C)
#define SPR_POWER_USIAR       (0X30C)
#define SPR_PERFD             (0x30D)
#define SPR_MPC_MD_TWC        (0x30D)
#define SPR_POWER_USDAR       (0X30D)
#define SPR_PERFE             (0x30E)
#define SPR_MPC_MD_RPN        (0x30E)
#define SPR_POWER_UMMCR1      (0X30E)
#define SPR_PERFF             (0x30F)
#define SPR_MPC_MD_TW         (0x30F)
#define SPR_UPERF0            (0x310)
#define SPR_POWER_SIER        (0x310)
#define SPR_UPERF1            (0x311)
#define SPR_POWER_MMCR2       (0x311)
#define SPR_UPERF2            (0x312)
#define SPR_POWER_MMCRA       (0X312)
#define SPR_UPERF3            (0x313)
#define SPR_POWER_PMC1        (0X313)
#define SPR_UPERF4            (0x314)
#define SPR_POWER_PMC2        (0X314)
#define SPR_UPERF5            (0x315)
#define SPR_POWER_PMC3        (0X315)
#define SPR_UPERF6            (0x316)
#define SPR_POWER_PMC4        (0X316)
#define SPR_UPERF7            (0x317)
#define SPR_POWER_PMC5        (0X317)
#define SPR_UPERF8            (0x318)
#define SPR_POWER_PMC6        (0X318)
#define SPR_UPERF9            (0x319)
#define SPR_970_PMC7          (0X319)
#define SPR_UPERFA            (0x31A)
#define SPR_970_PMC8          (0X31A)
#define SPR_UPERFB            (0x31B)
#define SPR_POWER_MMCR0       (0X31B)
#define SPR_UPERFC            (0x31C)
#define SPR_POWER_SIAR        (0X31C)
#define SPR_UPERFD            (0x31D)
#define SPR_POWER_SDAR        (0X31D)
#define SPR_UPERFE            (0x31E)
#define SPR_POWER_MMCR1       (0X31E)
#define SPR_UPERFF            (0x31F)
#define SPR_RCPU_MI_RA0       (0x320)
#define SPR_MPC_MI_DBCAM      (0x320)
#define SPR_BESCRS            (0x320)
#define SPR_RCPU_MI_RA1       (0x321)
#define SPR_MPC_MI_DBRAM0     (0x321)
#define SPR_BESCRSU           (0x321)
#define SPR_RCPU_MI_RA2       (0x322)
#define SPR_MPC_MI_DBRAM1     (0x322)
#define SPR_BESCRR            (0x322)
#define SPR_RCPU_MI_RA3       (0x323)
#define SPR_BESCRRU           (0x323)
#define SPR_EBBHR             (0x324)
#define SPR_EBBRR             (0x325)
#define SPR_BESCR             (0x326)
#define SPR_RCPU_L2U_RA0      (0x328)
#define SPR_MPC_MD_DBCAM      (0x328)
#define SPR_RCPU_L2U_RA1      (0x329)
#define SPR_MPC_MD_DBRAM0     (0x329)
#define SPR_RCPU_L2U_RA2      (0x32A)
#define SPR_MPC_MD_DBRAM1     (0x32A)
#define SPR_RCPU_L2U_RA3      (0x32B)
#define SPR_TAR               (0x32F)
#define SPR_IC                (0x350)
#define SPR_VTB               (0x351)
#define SPR_MMCRC             (0x353)
#define SPR_PSSCR             (0x357)
#define SPR_440_INV0          (0x370)
#define SPR_440_INV1          (0x371)
#define SPR_440_INV2          (0x372)
#define SPR_440_INV3          (0x373)
#define SPR_440_ITV0          (0x374)
#define SPR_440_ITV1          (0x375)
#define SPR_440_ITV2          (0x376)
#define SPR_440_ITV3          (0x377)
#define SPR_440_CCR1          (0x378)
#define SPR_TACR              (0x378)
#define SPR_TCSCR             (0x379)
#define SPR_CSIGR             (0x37a)
#define SPR_DCRIPR            (0x37B)
#define SPR_POWER_SPMC1       (0x37C)
#define SPR_POWER_SPMC2       (0x37D)
#define SPR_POWER_MMCRS       (0x37E)
#define SPR_WORT              (0x37F)
#define SPR_PPR               (0x380)
#define SPR_750_GQR0          (0x390)
#define SPR_440_DNV0          (0x390)
#define SPR_750_GQR1          (0x391)
#define SPR_440_DNV1          (0x391)
#define SPR_750_GQR2          (0x392)
#define SPR_440_DNV2          (0x392)
#define SPR_750_GQR3          (0x393)
#define SPR_440_DNV3          (0x393)
#define SPR_750_GQR4          (0x394)
#define SPR_440_DTV0          (0x394)
#define SPR_750_GQR5          (0x395)
#define SPR_440_DTV1          (0x395)
#define SPR_750_GQR6          (0x396)
#define SPR_440_DTV2          (0x396)
#define SPR_750_GQR7          (0x397)
#define SPR_440_DTV3          (0x397)
#define SPR_750_THRM4         (0x398)
#define SPR_750CL_HID2        (0x398)
#define SPR_440_DVLIM         (0x398)
#define SPR_750_WPAR          (0x399)
#define SPR_440_IVLIM         (0x399)
#define SPR_TSCR              (0x399)
#define SPR_750_DMAU          (0x39A)
#define SPR_750_DMAL          (0x39B)
#define SPR_440_RSTCFG        (0x39B)
#define SPR_BOOKE_DCDBTRL     (0x39C)
#define SPR_BOOKE_DCDBTRH     (0x39D)
#define SPR_BOOKE_ICDBTRL     (0x39E)
#define SPR_BOOKE_ICDBTRH     (0x39F)
#define SPR_74XX_UMMCR2       (0x3A0)
#define SPR_7XX_UPMC5         (0x3A1)
#define SPR_7XX_UPMC6         (0x3A2)
#define SPR_UBAMR             (0x3A7)
#define SPR_7XX_UMMCR0        (0x3A8)
#define SPR_7XX_UPMC1         (0x3A9)
#define SPR_7XX_UPMC2         (0x3AA)
#define SPR_7XX_USIAR         (0x3AB)
#define SPR_7XX_UMMCR1        (0x3AC)
#define SPR_7XX_UPMC3         (0x3AD)
#define SPR_7XX_UPMC4         (0x3AE)
#define SPR_USDA              (0x3AF)
#define SPR_40x_ZPR           (0x3B0)
#define SPR_BOOKE_MAS7        (0x3B0)
#define SPR_74XX_MMCR2        (0x3B0)
#define SPR_7XX_PMC5          (0x3B1)
#define SPR_40x_PID           (0x3B1)
#define SPR_7XX_PMC6          (0x3B2)
#define SPR_440_MMUCR         (0x3B2)
#define SPR_4xx_CCR0          (0x3B3)
#define SPR_BOOKE_EPLC        (0x3B3)
#define SPR_405_IAC3          (0x3B4)
#define SPR_BOOKE_EPSC        (0x3B4)
#define SPR_405_IAC4          (0x3B5)
#define SPR_405_DVC1          (0x3B6)
#define SPR_405_DVC2          (0x3B7)
#define SPR_BAMR              (0x3B7)
#define SPR_7XX_MMCR0         (0x3B8)
#define SPR_7XX_PMC1          (0x3B9)
#define SPR_40x_SGR           (0x3B9)
#define SPR_7XX_PMC2          (0x3BA)
#define SPR_40x_DCWR          (0x3BA)
#define SPR_7XX_SIAR          (0x3BB)
#define SPR_405_SLER          (0x3BB)
#define SPR_7XX_MMCR1         (0x3BC)
#define SPR_405_SU0R          (0x3BC)
#define SPR_401_SKR           (0x3BC)
#define SPR_7XX_PMC3          (0x3BD)
#define SPR_405_DBCR1         (0x3BD)
#define SPR_7XX_PMC4          (0x3BE)
#define SPR_SDA               (0x3BF)
#define SPR_403_VTBL          (0x3CC)
#define SPR_403_VTBU          (0x3CD)
#define SPR_DMISS             (0x3D0)
#define SPR_DCMP              (0x3D1)
#define SPR_HASH1             (0x3D2)
#define SPR_HASH2             (0x3D3)
#define SPR_BOOKE_ICDBDR      (0x3D3)
#define SPR_TLBMISS           (0x3D4)
#define SPR_IMISS             (0x3D4)
#define SPR_40x_ESR           (0x3D4)
#define SPR_PTEHI             (0x3D5)
#define SPR_ICMP              (0x3D5)
#define SPR_40x_DEAR          (0x3D5)
#define SPR_PTELO             (0x3D6)
#define SPR_RPA               (0x3D6)
#define SPR_40x_EVPR          (0x3D6)
#define SPR_L3PM              (0x3D7)
#define SPR_403_CDBCR         (0x3D7)
#define SPR_L3ITCR0           (0x3D8)
#define SPR_TCR               (0x3D8)
#define SPR_40x_TSR           (0x3D8)
#define SPR_IBR               (0x3DA)
#define SPR_40x_TCR           (0x3DA)
#define SPR_ESASRR            (0x3DB)
#define SPR_40x_PIT           (0x3DB)
#define SPR_403_TBL           (0x3DC)
#define SPR_403_TBU           (0x3DD)
#define SPR_SEBR              (0x3DE)
#define SPR_40x_SRR2          (0x3DE)
#define SPR_SER               (0x3DF)
#define SPR_40x_SRR3          (0x3DF)
#define SPR_L3OHCR            (0x3E8)
#define SPR_L3ITCR1           (0x3E9)
#define SPR_L3ITCR2           (0x3EA)
#define SPR_L3ITCR3           (0x3EB)
#define SPR_HID0              (0x3F0)
#define SPR_40x_DBSR          (0x3F0)
#define SPR_HID1              (0x3F1)
#define SPR_IABR              (0x3F2)
#define SPR_40x_DBCR0         (0x3F2)
#define SPR_601_HID2          (0x3F2)
#define SPR_Exxx_L1CSR0       (0x3F2)
#define SPR_ICTRL             (0x3F3)
#define SPR_HID2              (0x3F3)
#define SPR_750CL_HID4        (0x3F3)
#define SPR_Exxx_L1CSR1       (0x3F3)
#define SPR_440_DBDR          (0x3F3)
#define SPR_LDSTDB            (0x3F4)
#define SPR_750_TDCL          (0x3F4)
#define SPR_40x_IAC1          (0x3F4)
#define SPR_MMUCSR0           (0x3F4)
#define SPR_970_HID4          (0x3F4)
#define SPR_DABR              (0x3F5)
#define DABR_MASK (~(target_ulong)0x7)
#define SPR_Exxx_BUCSR        (0x3F5)
#define SPR_40x_IAC2          (0x3F5)
#define SPR_601_HID5          (0x3F5)
#define SPR_40x_DAC1          (0x3F6)
#define SPR_MSSCR0            (0x3F6)
#define SPR_970_HID5          (0x3F6)
#define SPR_MSSSR0            (0x3F7)
#define SPR_MSSCR1            (0x3F7)
#define SPR_DABRX             (0x3F7)
#define SPR_40x_DAC2          (0x3F7)
#define SPR_MMUCFG            (0x3F7)
#define SPR_LDSTCR            (0x3F8)
#define SPR_L2PMCR            (0x3F8)
#define SPR_750FX_HID2        (0x3F8)
#define SPR_Exxx_L1FINV0      (0x3F8)
#define SPR_L2CR              (0x3F9)
#define SPR_L3CR              (0x3FA)
#define SPR_750_TDCH          (0x3FA)
#define SPR_IABR2             (0x3FA)
#define SPR_40x_DCCR          (0x3FA)
#define SPR_ICTC              (0x3FB)
#define SPR_40x_ICCR          (0x3FB)
#define SPR_THRM1             (0x3FC)
#define SPR_403_PBL1          (0x3FC)
#define SPR_SP                (0x3FD)
#define SPR_THRM2             (0x3FD)
#define SPR_403_PBU1          (0x3FD)
#define SPR_604_HID13         (0x3FD)
#define SPR_LT                (0x3FE)
#define SPR_THRM3             (0x3FE)
#define SPR_RCPU_FPECR        (0x3FE)
#define SPR_403_PBL2          (0x3FE)
#define SPR_PIR               (0x3FF)
#define SPR_403_PBU2          (0x3FF)
#define SPR_601_HID15         (0x3FF)
#define SPR_604_HID15         (0x3FF)
#define SPR_E500_SVR          (0x3FF)

/* Disable MAS Interrupt Updates for Hypervisor */
#define EPCR_DMIUH            (1 << 22)
/* Disable Guest TLB Management Instructions */
#define EPCR_DGTMI            (1 << 23)
/* Guest Interrupt Computation Mode */
#define EPCR_GICM             (1 << 24)
/* Interrupt Computation Mode */
#define EPCR_ICM              (1 << 25)
/* Disable Embedded Hypervisor Debug */
#define EPCR_DUVD             (1 << 26)
/* Instruction Storage Interrupt Directed to Guest State */
#define EPCR_ISIGS            (1 << 27)
/* Data Storage Interrupt Directed to Guest State */
#define EPCR_DSIGS            (1 << 28)
/* Instruction TLB Error Interrupt Directed to Guest State */
#define EPCR_ITLBGS           (1 << 29)
/* Data TLB Error Interrupt Directed to Guest State */
#define EPCR_DTLBGS           (1 << 30)
/* External Input Interrupt Directed to Guest State */
#define EPCR_EXTGS            (1 << 31)

#define   L1CSR0_CPE		0x00010000	/* Data Cache Parity Enable */
#define   L1CSR0_CUL		0x00000400	/* (D-)Cache Unable to Lock */
#define   L1CSR0_DCLFR		0x00000100	/* D-Cache Lock Flash Reset */
#define   L1CSR0_DCFI		0x00000002	/* Data Cache Flash Invalidate */
#define   L1CSR0_DCE		0x00000001	/* Data Cache Enable */

#define   L1CSR1_CPE		0x00010000	/* Instruction Cache Parity Enable */
#define   L1CSR1_ICUL		0x00000400	/* I-Cache Unable to Lock */
#define   L1CSR1_ICLFR		0x00000100	/* I-Cache Lock Flash Reset */
#define   L1CSR1_ICFI		0x00000002	/* Instruction Cache Flash Invalidate */
#define   L1CSR1_ICE		0x00000001	/* Instruction Cache Enable */

/* HID0 bits */
#define HID0_DEEPNAP        (1 << 24)           /* pre-2.06 */
#define HID0_DOZE           (1 << 23)           /* pre-2.06 */
#define HID0_NAP            (1 << 22)           /* pre-2.06 */
#define HID0_HILE           PPC_BIT(19) /* POWER8 */
#define HID0_POWER9_HILE    PPC_BIT(4)

/*****************************************************************************/
/* PowerPC Instructions types definitions                                    */
enum {
    PPC_NONE           = 0x0000000000000000ULL,
    /* PowerPC base instructions set                                         */
    PPC_INSNS_BASE     = 0x0000000000000001ULL,
    /*   integer operations instructions                                     */
#define PPC_INTEGER PPC_INSNS_BASE
    /*   flow control instructions                                           */
#define PPC_FLOW    PPC_INSNS_BASE
    /*   virtual memory instructions                                         */
#define PPC_MEM     PPC_INSNS_BASE
    /*   ld/st with reservation instructions                                 */
#define PPC_RES     PPC_INSNS_BASE
    /*   spr/msr access instructions                                         */
#define PPC_MISC    PPC_INSNS_BASE
    /* Deprecated instruction sets                                           */
    /*   Original POWER instruction set                                      */
    PPC_POWER          = 0x0000000000000002ULL,
    /*   POWER2 instruction set extension                                    */
    PPC_POWER2         = 0x0000000000000004ULL,
    /*   Power RTC support                                                   */
    PPC_POWER_RTC      = 0x0000000000000008ULL,
    /*   Power-to-PowerPC bridge (601)                                       */
    PPC_POWER_BR       = 0x0000000000000010ULL,
    /* 64 bits PowerPC instruction set                                       */
    PPC_64B            = 0x0000000000000020ULL,
    /*   New 64 bits extensions (PowerPC 2.0x)                               */
    PPC_64BX           = 0x0000000000000040ULL,
    /*   64 bits hypervisor extensions                                       */
    PPC_64H            = 0x0000000000000080ULL,
    /*   New wait instruction (PowerPC 2.0x)                                 */
    PPC_WAIT           = 0x0000000000000100ULL,
    /*   Time base mftb instruction                                          */
    PPC_MFTB           = 0x0000000000000200ULL,

    /* Fixed-point unit extensions                                           */
    /*   PowerPC 602 specific                                                */
    PPC_602_SPEC       = 0x0000000000000400ULL,
    /*   isel instruction                                                    */
    PPC_ISEL           = 0x0000000000000800ULL,
    /*   popcntb instruction                                                 */
    PPC_POPCNTB        = 0x0000000000001000ULL,
    /*   string load / store                                                 */
    PPC_STRING         = 0x0000000000002000ULL,
    /*   real mode cache inhibited load / store                              */
    PPC_CILDST         = 0x0000000000004000ULL,

    /* Floating-point unit extensions                                        */
    /*   Optional floating point instructions                                */
    PPC_FLOAT          = 0x0000000000010000ULL,
    /* New floating-point extensions (PowerPC 2.0x)                          */
    PPC_FLOAT_EXT      = 0x0000000000020000ULL,
    PPC_FLOAT_FSQRT    = 0x0000000000040000ULL,
    PPC_FLOAT_FRES     = 0x0000000000080000ULL,
    PPC_FLOAT_FRSQRTE  = 0x0000000000100000ULL,
    PPC_FLOAT_FRSQRTES = 0x0000000000200000ULL,
    PPC_FLOAT_FSEL     = 0x0000000000400000ULL,
    PPC_FLOAT_STFIWX   = 0x0000000000800000ULL,

    /* Vector/SIMD extensions                                                */
    /*   Altivec support                                                     */
    PPC_ALTIVEC        = 0x0000000001000000ULL,
    /*   PowerPC 2.03 SPE extension                                          */
    PPC_SPE            = 0x0000000002000000ULL,
    /*   PowerPC 2.03 SPE single-precision floating-point extension          */
    PPC_SPE_SINGLE     = 0x0000000004000000ULL,
    /*   PowerPC 2.03 SPE double-precision floating-point extension          */
    PPC_SPE_DOUBLE     = 0x0000000008000000ULL,

    /* Optional memory control instructions                                  */
    PPC_MEM_TLBIA      = 0x0000000010000000ULL,
    PPC_MEM_TLBIE      = 0x0000000020000000ULL,
    PPC_MEM_TLBSYNC    = 0x0000000040000000ULL,
    /*   sync instruction                                                    */
    PPC_MEM_SYNC       = 0x0000000080000000ULL,
    /*   eieio instruction                                                   */
    PPC_MEM_EIEIO      = 0x0000000100000000ULL,

    /* Cache control instructions                                            */
    PPC_CACHE          = 0x0000000200000000ULL,
    /*   icbi instruction                                                    */
    PPC_CACHE_ICBI     = 0x0000000400000000ULL,
    /*   dcbz instruction                                                    */
    PPC_CACHE_DCBZ     = 0x0000000800000000ULL,
    /*   dcba instruction                                                    */
    PPC_CACHE_DCBA     = 0x0000002000000000ULL,
    /*   Freescale cache locking instructions                                */
    PPC_CACHE_LOCK     = 0x0000004000000000ULL,

    /* MMU related extensions                                                */
    /*   external control instructions                                       */
    PPC_EXTERN         = 0x0000010000000000ULL,
    /*   segment register access instructions                                */
    PPC_SEGMENT        = 0x0000020000000000ULL,
    /*   PowerPC 6xx TLB management instructions                             */
    PPC_6xx_TLB        = 0x0000040000000000ULL,
    /* PowerPC 74xx TLB management instructions                              */
    PPC_74xx_TLB       = 0x0000080000000000ULL,
    /*   PowerPC 40x TLB management instructions                             */
    PPC_40x_TLB        = 0x0000100000000000ULL,
    /*   segment register access instructions for PowerPC 64 "bridge"        */
    PPC_SEGMENT_64B    = 0x0000200000000000ULL,
    /*   SLB management                                                      */
    PPC_SLBI           = 0x0000400000000000ULL,

    /* Embedded PowerPC dedicated instructions                               */
    PPC_WRTEE          = 0x0001000000000000ULL,
    /* PowerPC 40x exception model                                           */
    PPC_40x_EXCP       = 0x0002000000000000ULL,
    /* PowerPC 405 Mac instructions                                          */
    PPC_405_MAC        = 0x0004000000000000ULL,
    /* PowerPC 440 specific instructions                                     */
    PPC_440_SPEC       = 0x0008000000000000ULL,
    /* BookE (embedded) PowerPC specification                                */
    PPC_BOOKE          = 0x0010000000000000ULL,
    /* mfapidi instruction                                                   */
    PPC_MFAPIDI        = 0x0020000000000000ULL,
    /* tlbiva instruction                                                    */
    PPC_TLBIVA         = 0x0040000000000000ULL,
    /* tlbivax instruction                                                   */
    PPC_TLBIVAX        = 0x0080000000000000ULL,
    /* PowerPC 4xx dedicated instructions                                    */
    PPC_4xx_COMMON     = 0x0100000000000000ULL,
    /* PowerPC 40x ibct instructions                                         */
    PPC_40x_ICBT       = 0x0200000000000000ULL,
    /* rfmci is not implemented in all BookE PowerPC                         */
    PPC_RFMCI          = 0x0400000000000000ULL,
    /* rfdi instruction                                                      */
    PPC_RFDI           = 0x0800000000000000ULL,
    /* DCR accesses                                                          */
    PPC_DCR            = 0x1000000000000000ULL,
    /* DCR extended accesse                                                  */
    PPC_DCRX           = 0x2000000000000000ULL,
    /* user-mode DCR access, implemented in PowerPC 460                      */
    PPC_DCRUX          = 0x4000000000000000ULL,
    /* popcntw and popcntd instructions                                      */
    PPC_POPCNTWD       = 0x8000000000000000ULL,

#define PPC_TCG_INSNS  (PPC_INSNS_BASE | PPC_POWER | PPC_POWER2 \
                        | PPC_POWER_RTC | PPC_POWER_BR | PPC_64B \
                        | PPC_64BX | PPC_64H | PPC_WAIT | PPC_MFTB \
                        | PPC_602_SPEC | PPC_ISEL | PPC_POPCNTB \
                        | PPC_STRING | PPC_FLOAT | PPC_FLOAT_EXT \
                        | PPC_FLOAT_FSQRT | PPC_FLOAT_FRES \
                        | PPC_FLOAT_FRSQRTE | PPC_FLOAT_FRSQRTES \
                        | PPC_FLOAT_FSEL | PPC_FLOAT_STFIWX \
                        | PPC_ALTIVEC | PPC_SPE | PPC_SPE_SINGLE \
                        | PPC_SPE_DOUBLE | PPC_MEM_TLBIA \
                        | PPC_MEM_TLBIE | PPC_MEM_TLBSYNC \
                        | PPC_MEM_SYNC | PPC_MEM_EIEIO \
                        | PPC_CACHE | PPC_CACHE_ICBI \
                        | PPC_CACHE_DCBZ \
                        | PPC_CACHE_DCBA | PPC_CACHE_LOCK \
                        | PPC_EXTERN | PPC_SEGMENT | PPC_6xx_TLB \
                        | PPC_74xx_TLB | PPC_40x_TLB | PPC_SEGMENT_64B \
                        | PPC_SLBI | PPC_WRTEE | PPC_40x_EXCP \
                        | PPC_405_MAC | PPC_440_SPEC | PPC_BOOKE \
                        | PPC_MFAPIDI | PPC_TLBIVA | PPC_TLBIVAX \
                        | PPC_4xx_COMMON | PPC_40x_ICBT | PPC_RFMCI \
                        | PPC_RFDI | PPC_DCR | PPC_DCRX | PPC_DCRUX \
                        | PPC_POPCNTWD | PPC_CILDST)

    /* extended type values */

    /* BookE 2.06 PowerPC specification                                      */
    PPC2_BOOKE206      = 0x0000000000000001ULL,
    /* VSX (extensions to Altivec / VMX)                                     */
    PPC2_VSX           = 0x0000000000000002ULL,
    /* Decimal Floating Point (DFP)                                          */
    PPC2_DFP           = 0x0000000000000004ULL,
    /* Embedded.Processor Control                                            */
    PPC2_PRCNTL        = 0x0000000000000008ULL,
    /* Byte-reversed, indexed, double-word load and store                    */
    PPC2_DBRX          = 0x0000000000000010ULL,
    /* Book I 2.05 PowerPC specification                                     */
    PPC2_ISA205        = 0x0000000000000020ULL,
    /* VSX additions in ISA 2.07                                             */
    PPC2_VSX207        = 0x0000000000000040ULL,
    /* ISA 2.06B bpermd                                                      */
    PPC2_PERM_ISA206   = 0x0000000000000080ULL,
    /* ISA 2.06B divide extended variants                                    */
    PPC2_DIVE_ISA206   = 0x0000000000000100ULL,
    /* ISA 2.06B larx/stcx. instructions                                     */
    PPC2_ATOMIC_ISA206 = 0x0000000000000200ULL,
    /* ISA 2.06B floating point integer conversion                           */
    PPC2_FP_CVT_ISA206 = 0x0000000000000400ULL,
    /* ISA 2.06B floating point test instructions                            */
    PPC2_FP_TST_ISA206 = 0x0000000000000800ULL,
    /* ISA 2.07 bctar instruction                                            */
    PPC2_BCTAR_ISA207  = 0x0000000000001000ULL,
    /* ISA 2.07 load/store quadword                                          */
    PPC2_LSQ_ISA207    = 0x0000000000002000ULL,
    /* ISA 2.07 Altivec                                                      */
    PPC2_ALTIVEC_207   = 0x0000000000004000ULL,
    /* PowerISA 2.07 Book3s specification                                    */
    PPC2_ISA207S       = 0x0000000000008000ULL,
    /* Double precision floating point conversion for signed integer 64      */
    PPC2_FP_CVT_S64    = 0x0000000000010000ULL,
    /* Transactional Memory (ISA 2.07, Book II)                              */
    PPC2_TM            = 0x0000000000020000ULL,
    /* Server PM instructgions (ISA 2.06, Book III)                          */
    PPC2_PM_ISA206     = 0x0000000000040000ULL,
    /* POWER ISA 3.0                                                         */
    PPC2_ISA300        = 0x0000000000080000ULL,

#define PPC_TCG_INSNS2 (PPC2_BOOKE206 | PPC2_VSX | PPC2_PRCNTL | PPC2_DBRX | \
                        PPC2_ISA205 | PPC2_VSX207 | PPC2_PERM_ISA206 | \
                        PPC2_DIVE_ISA206 | PPC2_ATOMIC_ISA206 | \
                        PPC2_FP_CVT_ISA206 | PPC2_FP_TST_ISA206 | \
                        PPC2_BCTAR_ISA207 | PPC2_LSQ_ISA207 | \
                        PPC2_ALTIVEC_207 | PPC2_ISA207S | PPC2_DFP | \
                        PPC2_FP_CVT_S64 | PPC2_TM | PPC2_PM_ISA206 | \
                        PPC2_ISA300)
};

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
    PPC6xx_INPUT_TBEN       = 6,
    PPC6xx_INPUT_WAKEUP     = 7,
    PPC6xx_INPUT_NB,
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
    PPCBookE_INPUT_NB,
};

enum {
    /* PowerPC E500 input pins */
    PPCE500_INPUT_RESET_CORE = 0,
    PPCE500_INPUT_MCK        = 1,
    PPCE500_INPUT_CINT       = 3,
    PPCE500_INPUT_INT        = 4,
    PPCE500_INPUT_DEBUG      = 6,
    PPCE500_INPUT_NB,
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
    /* RCPU input pins */
    PPCRCPU_INPUT_PORESET   = 0,
    PPCRCPU_INPUT_HRESET    = 1,
    PPCRCPU_INPUT_SRESET    = 2,
    PPCRCPU_INPUT_IRQ0      = 3,
    PPCRCPU_INPUT_IRQ1      = 4,
    PPCRCPU_INPUT_IRQ2      = 5,
    PPCRCPU_INPUT_IRQ3      = 6,
    PPCRCPU_INPUT_IRQ4      = 7,
    PPCRCPU_INPUT_IRQ5      = 8,
    PPCRCPU_INPUT_IRQ6      = 9,
    PPCRCPU_INPUT_IRQ7      = 10,
    PPCRCPU_INPUT_NB,
};

#if defined(TARGET_PPC64)
enum {
    /* PowerPC 970 input pins */
    PPC970_INPUT_HRESET     = 0,
    PPC970_INPUT_SRESET     = 1,
    PPC970_INPUT_CKSTP      = 2,
    PPC970_INPUT_TBEN       = 3,
    PPC970_INPUT_MCP        = 4,
    PPC970_INPUT_INT        = 5,
    PPC970_INPUT_THINT      = 6,
    PPC970_INPUT_NB,
};

enum {
    /* POWER7 input pins */
    POWER7_INPUT_INT        = 0,
    /* POWER7 probably has other inputs, but we don't care about them
     * for any existing machine.  We can wire these up when we need
     * them */
    POWER7_INPUT_NB,
};
#endif

/* Hardware exceptions definitions */
enum {
    /* External hardware exception sources */
    PPC_INTERRUPT_RESET     = 0,  /* Reset exception                      */
    PPC_INTERRUPT_WAKEUP,         /* Wakeup exception                     */
    PPC_INTERRUPT_MCK,            /* Machine check exception              */
    PPC_INTERRUPT_EXT,            /* External interrupt                   */
    PPC_INTERRUPT_SMI,            /* System management interrupt          */
    PPC_INTERRUPT_CEXT,           /* Critical external interrupt          */
    PPC_INTERRUPT_DEBUG,          /* External debug exception             */
    PPC_INTERRUPT_THERM,          /* Thermal exception                    */
    /* Internal hardware exception sources */
    PPC_INTERRUPT_DECR,           /* Decrementer exception                */
    PPC_INTERRUPT_HDECR,          /* Hypervisor decrementer exception     */
    PPC_INTERRUPT_PIT,            /* Programmable inteval timer interrupt */
    PPC_INTERRUPT_FIT,            /* Fixed interval timer interrupt       */
    PPC_INTERRUPT_WDT,            /* Watchdog timer interrupt             */
    PPC_INTERRUPT_CDOORBELL,      /* Critical doorbell interrupt          */
    PPC_INTERRUPT_DOORBELL,       /* Doorbell interrupt                   */
    PPC_INTERRUPT_PERFM,          /* Performance monitor interrupt        */
    PPC_INTERRUPT_HMI,            /* Hypervisor Maintainance interrupt    */
    PPC_INTERRUPT_HDOORBELL,      /* Hypervisor Doorbell interrupt        */
};

/* Processor Compatibility mask (PCR) */
enum {
    PCR_COMPAT_2_05     = PPC_BIT(62),
    PCR_COMPAT_2_06     = PPC_BIT(61),
    PCR_COMPAT_2_07     = PPC_BIT(60),
    PCR_COMPAT_3_00     = PPC_BIT(59),
    PCR_VEC_DIS         = PPC_BIT(0), /* Vec. disable (bit NA since POWER8) */
    PCR_VSX_DIS         = PPC_BIT(1), /* VSX disable (bit NA since POWER8) */
    PCR_TM_DIS          = PPC_BIT(2), /* Trans. memory disable (POWER8) */
};

/* HMER/HMEER */
enum {
    HMER_MALFUNCTION_ALERT      = PPC_BIT(0),
    HMER_PROC_RECV_DONE         = PPC_BIT(2),
    HMER_PROC_RECV_ERROR_MASKED = PPC_BIT(3),
    HMER_TFAC_ERROR             = PPC_BIT(4),
    HMER_TFMR_PARITY_ERROR      = PPC_BIT(5),
    HMER_XSCOM_FAIL             = PPC_BIT(8),
    HMER_XSCOM_DONE             = PPC_BIT(9),
    HMER_PROC_RECV_AGAIN        = PPC_BIT(11),
    HMER_WARN_RISE              = PPC_BIT(14),
    HMER_WARN_FALL              = PPC_BIT(15),
    HMER_SCOM_FIR_HMI           = PPC_BIT(16),
    HMER_TRIG_FIR_HMI           = PPC_BIT(17),
    HMER_HYP_RESOURCE_ERR       = PPC_BIT(20),
    HMER_XSCOM_STATUS_MASK      = PPC_BITMASK(21, 23),
};

/* Alternate Interrupt Location (AIL) */
enum {
    AIL_NONE                = 0,
    AIL_RESERVED            = 1,
    AIL_0001_8000           = 2,
    AIL_C000_0000_0000_4000 = 3,
};

/*****************************************************************************/

#define is_isa300(ctx) (!!(ctx->insns_flags2 & PPC2_ISA300))
target_ulong cpu_read_xer(CPUPPCState *env);
void cpu_write_xer(CPUPPCState *env, target_ulong xer);

static inline void cpu_get_tb_cpu_state(CPUPPCState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->nip;
    *cs_base = 0;
    *flags = env->hflags;
}

void QEMU_NORETURN raise_exception(CPUPPCState *env, uint32_t exception);
void QEMU_NORETURN raise_exception_ra(CPUPPCState *env, uint32_t exception,
                                      uintptr_t raddr);
void QEMU_NORETURN raise_exception_err(CPUPPCState *env, uint32_t exception,
                                       uint32_t error_code);
void QEMU_NORETURN raise_exception_err_ra(CPUPPCState *env, uint32_t exception,
                                          uint32_t error_code, uintptr_t raddr);

#if !defined(CONFIG_USER_ONLY)
static inline int booke206_tlbm_id(CPUPPCState *env, ppcmas_tlb_t *tlbm)
{
    uintptr_t tlbml = (uintptr_t)tlbm;
    uintptr_t tlbl = (uintptr_t)env->tlb.tlbm;

    return (tlbml - tlbl) / sizeof(env->tlb.tlbm[0]);
}

static inline int booke206_tlb_size(CPUPPCState *env, int tlbn)
{
    uint32_t tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlbn];
    int r = tlbncfg & TLBnCFG_N_ENTRY;
    return r;
}

static inline int booke206_tlb_ways(CPUPPCState *env, int tlbn)
{
    uint32_t tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlbn];
    int r = tlbncfg >> TLBnCFG_ASSOC_SHIFT;
    return r;
}

static inline int booke206_tlbm_to_tlbn(CPUPPCState *env, ppcmas_tlb_t *tlbm)
{
    int id = booke206_tlbm_id(env, tlbm);
    int end = 0;
    int i;

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        end += booke206_tlb_size(env, i);
        if (id < end) {
            return i;
        }
    }

    cpu_abort(CPU(ppc_env_get_cpu(env)), "Unknown TLBe: %d\n", id);
    return 0;
}

static inline int booke206_tlbm_to_way(CPUPPCState *env, ppcmas_tlb_t *tlb)
{
    int tlbn = booke206_tlbm_to_tlbn(env, tlb);
    int tlbid = booke206_tlbm_id(env, tlb);
    return tlbid & (booke206_tlb_ways(env, tlbn) - 1);
}

static inline ppcmas_tlb_t *booke206_get_tlbm(CPUPPCState *env, const int tlbn,
                                              target_ulong ea, int way)
{
    int r;
    uint32_t ways = booke206_tlb_ways(env, tlbn);
    int ways_bits = ctz32(ways);
    int tlb_bits = ctz32(booke206_tlb_size(env, tlbn));
    int i;

    way &= ways - 1;
    ea >>= MAS2_EPN_SHIFT;
    ea &= (1 << (tlb_bits - ways_bits)) - 1;
    r = (ea << ways_bits) | way;

    if (r >= booke206_tlb_size(env, tlbn)) {
        return NULL;
    }

    /* bump up to tlbn index */
    for (i = 0; i < tlbn; i++) {
        r += booke206_tlb_size(env, i);
    }

    return &env->tlb.tlbm[r];
}

/* returns bitmap of supported page sizes for a given TLB */
static inline uint32_t booke206_tlbnps(CPUPPCState *env, const int tlbn)
{
    uint32_t ret = 0;

    if ((env->spr[SPR_MMUCFG] & MMUCFG_MAVN) == MMUCFG_MAVN_V2) {
        /* MAV2 */
        ret = env->spr[SPR_BOOKE_TLB0PS + tlbn];
    } else {
        uint32_t tlbncfg = env->spr[SPR_BOOKE_TLB0CFG + tlbn];
        uint32_t min = (tlbncfg & TLBnCFG_MINSIZE) >> TLBnCFG_MINSIZE_SHIFT;
        uint32_t max = (tlbncfg & TLBnCFG_MAXSIZE) >> TLBnCFG_MAXSIZE_SHIFT;
        int i;
        for (i = min; i <= max; i++) {
            ret |= (1 << (i << 1));
        }
    }

    return ret;
}

static inline void booke206_fixed_size_tlbn(CPUPPCState *env, const int tlbn,
                                            ppcmas_tlb_t *tlb)
{
    uint8_t i;
    int32_t tsize = -1;

    for (i = 0; i < 32; i++) {
        if ((env->spr[SPR_BOOKE_TLB0PS + tlbn]) & (1ULL << i)) {
            if (tsize == -1) {
                tsize = i;
            } else {
                return;
            }
        }
    }

    /* TLBnPS unimplemented? Odd.. */
    assert(tsize != -1);
    tlb->mas1 &= ~MAS1_TSIZE_MASK;
    tlb->mas1 |= ((uint32_t)tsize) << MAS1_TSIZE_SHIFT;
}

#endif

static inline bool msr_is_64bit(CPUPPCState *env, target_ulong msr)
{
    if (env->mmu_model == POWERPC_MMU_BOOKE206) {
        return msr & (1ULL << MSR_CM);
    }

    return msr & (1ULL << MSR_SF);
}

/**
 * Check whether register rx is in the range between start and
 * start + nregs (as needed by the LSWX and LSWI instructions)
 */
static inline bool lsw_reg_in_range(int start, int nregs, int rx)
{
    return (start + nregs <= 32 && rx >= start && rx < start + nregs) ||
           (start + nregs > 32 && (rx >= start || rx < start + nregs - 32));
}

void dump_mmu(FILE *f, fprintf_function cpu_fprintf, CPUPPCState *env);

void ppc_maybe_bswap_register(CPUPPCState *env, uint8_t *mem_buf, int len);
#endif /* PPC_CPU_H */
