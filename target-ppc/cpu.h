/*
 *  PPC emulation cpu definitions for qemu.
 * 
 *  Copyright (c) 2003 Jocelyn Mayer
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

#include <endian.h>
#include <asm/byteorder.h>

#include "cpu-defs.h"

/***                          Sign extend constants                        ***/
/* 8 to 32 bits */
static inline int32_t s_ext8 (uint8_t value)
{
    int8_t *tmp = &value;

    return *tmp;
}

/* 16 to 32 bits */
static inline int32_t s_ext16 (uint16_t value)
{
    int16_t *tmp = &value;

    return *tmp;
}

/* 24 to 32 bits */
static inline int32_t s_ext24 (uint32_t value)
{
    uint16_t utmp = (value >> 8) & 0xFFFF;
    int16_t *tmp = &utmp;

    return (*tmp << 8) | (value & 0xFF);
}

#include "config.h"
#include <setjmp.h>

/* Floting point status and control register */
#define FPSCR_FX     31
#define FPSCR_FEX    30
#define FPSCR_VX     29
#define FPSCR_OX     28
#define FPSCR_UX     27
#define FPSCR_ZX     26
#define FPSCR_XX     25
#define FPSCR_VXSNAN 24
#define FPSCR_VXISI  26
#define FPSCR_VXIDI  25
#define FPSCR_VXZDZ  21
#define FPSCR_VXIMZ  20

#define FPSCR_VXVC   18
#define FPSCR_FR     17
#define FPSCR_FI     16
#define FPSCR_FPRF   11
#define FPSCR_VXSOFT 9
#define FPSCR_VXSQRT 8
#define FPSCR_VXCVI  7
#define FPSCR_OE     6
#define FPSCR_UE     5
#define FPSCR_ZE     4
#define FPSCR_XE     3
#define FPSCR_NI     2
#define FPSCR_RN     0
#define fpscr_fx     env->fpscr[FPSCR_FX]
#define fpscr_fex    env->fpscr[FPSCR_FEX]
#define fpscr_vx     env->fpscr[FPSCR_VX]
#define fpscr_ox     env->fpscr[FPSCR_OX]
#define fpscr_ux     env->fpscr[FPSCR_UX]
#define fpscr_zx     env->fpscr[FPSCR_ZX]
#define fpscr_xx     env->fpscr[FPSCR_XX]
#define fpscr_vsxnan env->fpscr[FPSCR_VXSNAN]
#define fpscr_vxisi  env->fpscr[FPSCR_VXISI]
#define fpscr_vxidi  env->fpscr[FPSCR_VXIDI]
#define fpscr_vxzdz  env->fpscr[FPSCR_VXZDZ]
#define fpscr_vximz  env->fpscr[FPSCR_VXIMZ]
#define fpscr_fr     env->fpscr[FPSCR_FR]
#define fpscr_fi     env->fpscr[FPSCR_FI]
#define fpscr_fprf   env->fpscr[FPSCR_FPRF]
#define fpscr_vxsoft env->fpscr[FPSCR_VXSOFT]
#define fpscr_vxsqrt env->fpscr[FPSCR_VXSQRT]
#define fpscr_oe     env->fpscr[FPSCR_OE]
#define fpscr_ue     env->fpscr[FPSCR_UE]
#define fpscr_ze     env->fpscr[FPSCR_ZE]
#define fpscr_xe     env->fpscr[FPSCR_XE]
#define fpscr_ni     env->fpscr[FPSCR_NI]
#define fpscr_rn     env->fpscr[FPSCR_RN]

/* Supervisor mode registers */
/* Machine state register */
#define MSR_POW 18
#define MSR_ILE 16
#define MSR_EE  15
#define MSR_PR  14
#define MSR_FP  13
#define MSR_ME  12
#define MSR_FE0 11
#define MSR_SE  10
#define MSR_BE  9
#define MSR_FE1 8
#define MSR_IP 6
#define MSR_IR 5
#define MSR_DR 4
#define MSR_RI 1
#define MSR_LE 0
#define msr_pow env->msr[MSR_POW]
#define msr_ile env->msr[MSR_ILE]
#define msr_ee  env->msr[MSR_EE]
#define msr_pr  env->msr[MSR_PR]
#define msr_fp  env->msr[MSR_FP]
#define msr_me  env->msr[MSR_ME]
#define msr_fe0 env->msr[MSR_FE0]
#define msr_se  env->msr[MSR_SE]
#define msr_be  env->msr[MSR_BE]
#define msr_fe1 env->msr[MSR_FE1]
#define msr_ip  env->msr[MSR_IP]
#define msr_ir  env->msr[MSR_IR]
#define msr_dr  env->msr[MSR_DR]
#define msr_ri  env->msr[MSR_RI]
#define msr_le  env->msr[MSR_LE]

/* Segment registers */
typedef struct ppc_sr_t {
    uint32_t t:1;
    uint32_t ks:1;
    uint32_t kp:1;
    uint32_t n:1;
    uint32_t res:4;
    uint32_t vsid:24;
} ppc_sr_t;

typedef struct CPUPPCState {
    /* general purpose registers */
    uint32_t gpr[32];
    /* floating point registers */
    uint64_t fpr[32];
    /* segment registers */
    ppc_sr_t sr[16];
    /* special purpose registers */
    uint32_t spr[1024];
    /* XER */
    uint8_t xer[32];
    /* Reservation address */
    uint32_t reserve;
    /* machine state register */
    uint8_t msr[32];
    /* condition register */
    uint8_t crf[8];
    /* floating point status and control register */
    uint8_t fpscr[32];
    uint32_t nip;
    /* CPU exception code */
    uint32_t exception;

    /* qemu dedicated */
    uint64_t ft0; /* temporary float register */
    int interrupt_request;
    jmp_buf jmp_env;
    int exception_index;
    int error_code;
    int user_mode_only; /* user mode only simulation */
    struct TranslationBlock *current_tb; /* currently executing TB */

    /* user data */
    void *opaque;
} CPUPPCState;

CPUPPCState *cpu_ppc_init(void);
int cpu_ppc_exec(CPUPPCState *s);
void cpu_ppc_close(CPUPPCState *s);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
struct siginfo;
int cpu_ppc_signal_handler(int host_signum, struct siginfo *info, 
                           void *puc);

void cpu_ppc_dump_state(CPUPPCState *env, FILE *f, int flags);

#define TARGET_PAGE_BITS 12
#include "cpu-all.h"

#define ugpr(n) (env->gpr[n])
#define fpr(n) (env->fpr[n])

#define SPR_ENCODE(sprn)                               \
(((sprn) >> 5) | (((sprn) & 0x1F) << 5))

/* User mode SPR */
#define spr(n) env->spr[n]
//#define XER    spr[1]
#define XER env->xer
#define XER_SO 31
#define XER_OV 30
#define XER_CA 29
#define XER_BC 0
#define xer_so env->xer[XER_SO]
#define xer_ov env->xer[XER_OV]
#define xer_ca env->xer[XER_CA]
#define xer_bc env->xer[XER_BC]

#define LR     spr[SPR_ENCODE(8)]
#define CTR    spr[SPR_ENCODE(9)]
/* VEA mode SPR */
#define V_TBL  spr[SPR_ENCODE(268)]
#define V_TBU  spr[SPR_ENCODE(269)]
/* supervisor mode SPR */
#define DSISR  spr[SPR_ENCODE(18)]
#define DAR    spr[SPR_ENCODE(19)]
#define DEC    spr[SPR_ENCODE(22)]
#define SDR1   spr[SPR_ENCODE(25)]
typedef struct ppc_sdr1_t {
    uint32_t htaborg:16;
    uint32_t res:7;
    uint32_t htabmask:9;
} ppc_sdr1_t;
#define SRR0   spr[SPR_ENCODE(26)]
#define SRR0_MASK 0xFFFFFFFC
#define SRR1   spr[SPR_ENCODE(27)]
#define SPRG0  spr[SPR_ENCODE(272)]
#define SPRG1  spr[SPR_ENCODE(273)]
#define SPRG2  spr[SPR_ENCODE(274)]
#define SPRG3  spr[SPR_ENCODE(275)]
#define EAR    spr[SPR_ENCODE(282)]
typedef struct ppc_ear_t {
    uint32_t e:1;
    uint32_t res:25;
    uint32_t rid:6;
} ppc_ear_t;
#define TBL    spr[SPR_ENCODE(284)]
#define TBU    spr[SPR_ENCODE(285)]
#define PVR    spr[SPR_ENCODE(287)]
typedef struct ppc_pvr_t {
    uint32_t version:16;
    uint32_t revision:16;
} ppc_pvr_t;
#define IBAT0U spr[SPR_ENCODE(528)]
#define IBAT0L spr[SPR_ENCODE(529)]
#define IBAT1U spr[SPR_ENCODE(530)]
#define IBAT1L spr[SPR_ENCODE(531)]
#define IBAT2U spr[SPR_ENCODE(532)]
#define IBAT2L spr[SPR_ENCODE(533)]
#define IBAT3U spr[SPR_ENCODE(534)]
#define IBAT3L spr[SPR_ENCODE(535)]
#define DBAT0U spr[SPR_ENCODE(536)]
#define DBAT0L spr[SPR_ENCODE(537)]
#define DBAT1U spr[SPR_ENCODE(538)]
#define DBAT1L spr[SPR_ENCODE(539)]
#define DBAT2U spr[SPR_ENCODE(540)]
#define DBAT2L spr[SPR_ENCODE(541)]
#define DBAT3U spr[SPR_ENCODE(542)]
#define DBAT3L spr[SPR_ENCODE(543)]
typedef struct ppc_ubat_t {
    uint32_t bepi:15;
    uint32_t res:4;
    uint32_t bl:11;
    uint32_t vs:1;
    uint32_t vp:1;
} ppc_ubat_t;
typedef struct ppc_lbat_t {
    uint32_t brpn:15;
    uint32_t res0:10;
    uint32_t w:1;
    uint32_t i:1;
    uint32_t m:1;
    uint32_t g:1;
    uint32_t res1:1;
    uint32_t pp:2;
} ppc_lbat_t;
#define DABR   spr[SPR_ENCODE(1013)]
#define DABR_MASK 0xFFFFFFF8
typedef struct ppc_dabr_t {
    uint32_t dab:29;
    uint32_t bt:1;
    uint32_t dw:1;
    uint32_t dr:1;
} ppc_dabr_t;
#define FPECR  spr[SPR_ENCODE(1022)]
#define PIR    spr[SPR_ENCODE(1023)]

#define TARGET_PAGE_BITS 12
#include "cpu-all.h"

CPUPPCState *cpu_ppc_init(void);
int cpu_ppc_exec(CPUPPCState *s);
void cpu_ppc_close(CPUPPCState *s);
void cpu_ppc_dump_state(CPUPPCState *env, FILE *f, int flags);

/* Exeptions */
enum {
    EXCP_NONE          = 0x00,
    /* PPC hardware exceptions : exception vector / 0x100 */
    EXCP_RESET         = 0x01, /* System reset                     */
    EXCP_MACHINE_CHECK = 0x02, /* Machine check exception          */
    EXCP_DSI           = 0x03, /* Impossible memory access         */
    EXCP_ISI           = 0x04, /* Impossible instruction fetch     */
    EXCP_EXTERNAL      = 0x05, /* External interruption            */
    EXCP_ALIGN         = 0x06, /* Alignment exception              */
    EXCP_PROGRAM       = 0x07, /* Program exception                */
    EXCP_NO_FP         = 0x08, /* No floating point                */
    EXCP_DECR          = 0x09, /* Decrementer exception            */
    EXCP_RESA          = 0x0A, /* Implementation specific          */
    EXCP_RESB          = 0x0B, /* Implementation specific          */
    EXCP_SYSCALL       = 0x0C, /* System call                      */
    EXCP_TRACE         = 0x0D, /* Trace exception (optional)       */
    EXCP_FP_ASSIST     = 0x0E, /* Floating-point assist (optional) */
#if 0
    /* Exeption subtypes for EXCP_DSI */
    EXCP_DSI_TRANSLATE = 0x10301, /* Data address can't be translated */
    EXCP_DSI_NOTSUP    = 0x10302, /* Access type not supported        */
    EXCP_DSI_PROT      = 0x10303, /* Memory protection violation      */
    EXCP_DSI_EXTERNAL  = 0x10304, /* External access disabled         */
    EXCP_DSI_DABR      = 0x10305, /* Data address breakpoint          */
    /* Exeption subtypes for EXCP_ISI */
    EXCP_ISI_TRANSLATE = 0x10401, /* Code address can't be translated */
    EXCP_ISI_NOTSUP    = 0x10402, /* Access type not supported        */
    EXCP_ISI_PROT      = 0x10403, /* Memory protection violation      */
    EXCP_ISI_GUARD     = 0x10404, /* Fetch into guarded memory        */
    /* Exeption subtypes for EXCP_ALIGN */
    EXCP_ALIGN_FP      = 0x10601, /* FP alignment exception           */
    EXCP_ALIGN_LST     = 0x10602, /* Unaligned memory load/store      */
    EXCP_ALIGN_LE      = 0x10603, /* Unaligned little-endian access   */
    EXCP_ALIGN_PROT    = 0x10604, /* Access cross protection boundary */
    EXCP_ALIGN_BAT     = 0x10605, /* Access cross a BAT/seg boundary  */
    EXCP_ALIGN_CACHE   = 0x10606, /* Impossible dcbz access           */
    /* Exeption subtypes for EXCP_PROGRAM */
    /* FP exceptions */
    EXCP_FP_OX         = 0x10701, /* FP overflow                      */
    EXCP_FP_UX         = 0x10702, /* FP underflow                     */
    EXCP_FP_ZX         = 0x10703, /* FP divide by zero                */
    EXCP_FP_XX         = 0x10704, /* FP inexact                       */
    EXCP_FP_VXNAN      = 0x10705, /* FP invalid SNaN op               */
    EXCP_FP_VXISI      = 0x10706, /* FP invalid infinite substraction */
    EXCP_FP_VXIDI      = 0x10707, /* FP invalid infinite divide       */
    EXCP_FP_VXZDZ      = 0x10708, /* FP invalid zero divide           */
    EXCP_FP_VXIMZ      = 0x10709, /* FP invalid infinite * zero       */
    EXCP_FP_VXVC       = 0x1070A, /* FP invalid compare               */
    EXCP_FP_VXSOFT     = 0x1070B, /* FP invalid operation             */
    EXCP_FP_VXSQRT     = 0x1070C, /* FP invalid square root           */
    EXCP_FP_VXCVI      = 0x1070D, /* FP invalid integer conversion    */
    /* Invalid instruction */
    EXCP_INVAL_INVAL   = 0x10711, /* Invalid instruction              */
    EXCP_INVAL_LSWX    = 0x10712, /* Invalid lswx instruction         */
    EXCP_INVAL_SPR     = 0x10713, /* Invalid SPR access               */
    EXCP_INVAL_FP      = 0x10714, /* Unimplemented mandatory fp instr */
#endif
    EXCP_INVAL         = 0x70,    /* Invalid instruction              */
    /* Privileged instruction */
    EXCP_PRIV          = 0x71,    /* Privileged instruction           */
    /* Trap */
    EXCP_TRAP          = 0x72,    /* Trap                             */
    /* Special cases where we want to stop translation */
    EXCP_MTMSR         = 0x103,   /* mtmsr instruction:               */
                                  /* may change privilege level       */
    EXCP_BRANCH        = 0x104,   /* branch instruction               */
};

/*
 * We need to put in some extra aux table entries to tell glibc what
 * the cache block size is, so it can use the dcbz instruction safely.
 */
#define AT_DCACHEBSIZE          19
#define AT_ICACHEBSIZE          20
#define AT_UCACHEBSIZE          21
/* A special ignored type value for PPC, for glibc compatibility.  */
#define AT_IGNOREPPC            22
/*
 * The requirements here are:
 * - keep the final alignment of sp (sp & 0xf)
 * - make sure the 32-bit value at the first 16 byte aligned position of
 *   AUXV is greater than 16 for glibc compatibility.
 *   AT_IGNOREPPC is used for that.
 * - for compatibility with glibc ARCH_DLINFO must always be defined on PPC,
 *   even if DLINFO_ARCH_ITEMS goes to zero or is undefined.
 */
#define DLINFO_ARCH_ITEMS       3
#define ARCH_DLINFO                                                     \
do {                                                                    \
        /*                                                              \
         * Now handle glibc compatibility.                              \
         */                                                             \
        NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);                        \
        NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);                        \
                                                                        \
        NEW_AUX_ENT(AT_DCACHEBSIZE, 0x20);                              \
        NEW_AUX_ENT(AT_ICACHEBSIZE, 0x20);                              \
        NEW_AUX_ENT(AT_UCACHEBSIZE, 0);                                 \
 } while (0)
#endif /* !defined (__CPU_PPC_H__) */
