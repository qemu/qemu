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

#if defined(TARGET_PPC64) || (HOST_LONG_BITS >= 64)
/* When using 64 bits temporary registers,
 * we can use 64 bits GPR with no extra cost
 */
#define TARGET_PPCSPE
#endif

#if defined (TARGET_PPC64)
typedef uint64_t ppc_gpr_t;
#define TARGET_LONG_BITS 64
#define TARGET_GPR_BITS  64
#define REGX "%016" PRIx64
#define ADDRX "%016" PRIx64
#elif defined(TARGET_PPCSPE)
/* GPR are 64 bits: used by vector extension */
typedef uint64_t ppc_gpr_t;
#define TARGET_LONG_BITS 32
#define TARGET_GPR_BITS  64
#define REGX "%016" PRIx64
#define ADDRX "%08" PRIx32
#else
typedef uint32_t ppc_gpr_t;
#define TARGET_LONG_BITS 32
#define TARGET_GPR_BITS  32
#define REGX "%08" PRIx32
#define ADDRX "%08" PRIx32
#endif

#include "cpu-defs.h"

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

/* XXX: put this in a common place */
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/*****************************************************************************/
/* PVR definitions for most known PowerPC */
enum {
    /* PowerPC 401 cores */
    CPU_PPC_401A1     = 0x00210000,
    CPU_PPC_401B2     = 0x00220000,
    CPU_PPC_401C2     = 0x00230000,
    CPU_PPC_401D2     = 0x00240000,
    CPU_PPC_401E2     = 0x00250000,
    CPU_PPC_401F2     = 0x00260000,
    CPU_PPC_401G2     = 0x00270000,
#define CPU_PPC_401 CPU_PPC_401G2
    CPU_PPC_IOP480    = 0x40100000, /* 401B2 ? */
    CPU_PPC_COBRA     = 0x10100000, /* IBM Processor for Network Resources */
    /* PowerPC 403 cores */
    CPU_PPC_403GA     = 0x00200011,
    CPU_PPC_403GB     = 0x00200100,
    CPU_PPC_403GC     = 0x00200200,
    CPU_PPC_403GCX    = 0x00201400,
#define CPU_PPC_403 CPU_PPC_403GCX
    /* PowerPC 405 cores */
    CPU_PPC_405CR     = 0x40110145,
#define CPU_PPC_405GP CPU_PPC_405CR
    CPU_PPC_405EP     = 0x51210950,
    CPU_PPC_405GPR    = 0x50910951,
    CPU_PPC_405D2     = 0x20010000,
    CPU_PPC_405D4     = 0x41810000,
#define CPU_PPC_405 CPU_PPC_405D4
    CPU_PPC_NPE405H   = 0x414100C0,
    CPU_PPC_NPE405H2  = 0x41410140,
    CPU_PPC_NPE405L   = 0x416100C0,
    /* XXX: missing 405LP, LC77700 */
    /* IBM STBxxx (PowerPC 401/403/405 core based microcontrollers) */
#if 0
    CPU_PPC_STB01000  = xxx,
#endif
#if 0
    CPU_PPC_STB01010  = xxx,
#endif
#if 0
    CPU_PPC_STB0210   = xxx,
#endif
    CPU_PPC_STB03     = 0x40310000,
#if 0
    CPU_PPC_STB043    = xxx,
#endif
#if 0
    CPU_PPC_STB045    = xxx,
#endif
    CPU_PPC_STB25     = 0x51510950,
#if 0
    CPU_PPC_STB130    = xxx,
#endif
    /* Xilinx cores */
    CPU_PPC_X2VP4     = 0x20010820,
#define CPU_PPC_X2VP7 CPU_PPC_X2VP4
    CPU_PPC_X2VP20    = 0x20010860,
#define CPU_PPC_X2VP50 CPU_PPC_X2VP20
    /* PowerPC 440 cores */
    CPU_PPC_440EP     = 0x422218D3,
#define CPU_PPC_440GR CPU_PPC_440EP
    CPU_PPC_440GP     = 0x40120481,
    CPU_PPC_440GX     = 0x51B21850,
    CPU_PPC_440GXc    = 0x51B21892,
    CPU_PPC_440GXf    = 0x51B21894,
    CPU_PPC_440SP     = 0x53221850,
    CPU_PPC_440SP2    = 0x53221891,
    CPU_PPC_440SPE    = 0x53421890,
    /* XXX: missing 440GRX */
    /* PowerPC 460 cores - TODO */
    /* PowerPC MPC 5xx cores */
    CPU_PPC_5xx       = 0x00020020,
    /* PowerPC MPC 8xx cores (aka PowerQUICC) */
    CPU_PPC_8xx       = 0x00500000,
    /* PowerPC MPC 8xxx cores (aka PowerQUICC-II) */
    CPU_PPC_82xx_HIP3 = 0x00810101,
    CPU_PPC_82xx_HIP4 = 0x80811014,
    CPU_PPC_827x      = 0x80822013,
    /* eCores */
    CPU_PPC_e200      = 0x81120000,
    CPU_PPC_e500v110  = 0x80200010,
    CPU_PPC_e500v120  = 0x80200020,
    CPU_PPC_e500v210  = 0x80210010,
    CPU_PPC_e500v220  = 0x80210020,
#define CPU_PPC_e500 CPU_PPC_e500v220
    CPU_PPC_e600      = 0x80040010,
    /* PowerPC 6xx cores */
    CPU_PPC_601       = 0x00010001,
    CPU_PPC_602       = 0x00050100,
    CPU_PPC_603       = 0x00030100,
    CPU_PPC_603E      = 0x00060101,
    CPU_PPC_603P      = 0x00070000,
    CPU_PPC_603E7v    = 0x00070100,
    CPU_PPC_603E7v2   = 0x00070201,
    CPU_PPC_603E7     = 0x00070200,
    CPU_PPC_603R      = 0x00071201,
    CPU_PPC_G2        = 0x00810011,
    CPU_PPC_G2H4      = 0x80811010,
    CPU_PPC_G2gp      = 0x80821010,
    CPU_PPC_G2ls      = 0x90810010,
    CPU_PPC_G2LE      = 0x80820010,
    CPU_PPC_G2LEgp    = 0x80822010,
    CPU_PPC_G2LEls    = 0xA0822010,
    CPU_PPC_604       = 0x00040000,
    CPU_PPC_604E      = 0x00090100, /* Also 2110 & 2120 */
    CPU_PPC_604R      = 0x000a0101,
    /* PowerPC 74x/75x cores (aka G3) */
    CPU_PPC_74x       = 0x00080000,
    CPU_PPC_740E      = 0x00080100,
    CPU_PPC_750E      = 0x00080200,
    CPU_PPC_755_10    = 0x00083100,
    CPU_PPC_755_11    = 0x00083101,
    CPU_PPC_755_20    = 0x00083200,
    CPU_PPC_755D      = 0x00083202,
    CPU_PPC_755E      = 0x00083203,
#define CPU_PPC_755 CPU_PPC_755E
    CPU_PPC_74xP      = 0x10080000,
    CPU_PPC_750CXE21  = 0x00082201,
    CPU_PPC_750CXE22  = 0x00082212,
    CPU_PPC_750CXE23  = 0x00082203,
    CPU_PPC_750CXE24  = 0x00082214,
    CPU_PPC_750CXE24b = 0x00083214,
    CPU_PPC_750CXE31  = 0x00083211,
    CPU_PPC_750CXE31b = 0x00083311,
#define CPU_PPC_750CXE CPU_PPC_750CXE31b
    CPU_PPC_750CXR    = 0x00083410,
    CPU_PPC_750FX10   = 0x70000100,
    CPU_PPC_750FX20   = 0x70000200,
    CPU_PPC_750FX21   = 0x70000201,
    CPU_PPC_750FX22   = 0x70000202,
    CPU_PPC_750FX23   = 0x70000203,
#define CPU_PPC_750FX CPU_PPC_750FX23
    CPU_PPC_750FL     = 0x700A0203,
    CPU_PPC_750GX10   = 0x70020100,
    CPU_PPC_750GX11   = 0x70020101,
    CPU_PPC_750GX12   = 0x70020102,
#define CPU_PPC_750GX CPU_PPC_750GX12
    CPU_PPC_750GL     = 0x70020102,
    CPU_PPC_750L30    = 0x00088300,
    CPU_PPC_750L32    = 0x00088302,
    CPU_PPC_750CL     = 0x00087200,
    /* PowerPC 74xx cores (aka G4) */
    CPU_PPC_7400      = 0x000C0100,
    CPU_PPC_7410C     = 0x800C1102,
    CPU_PPC_7410D     = 0x800C1103,
    CPU_PPC_7410E     = 0x800C1104,
    CPU_PPC_7441      = 0x80000210,
    CPU_PPC_7445      = 0x80010100,
    CPU_PPC_7447      = 0x80020100,
    CPU_PPC_7447A     = 0x80030101,
    CPU_PPC_7448      = 0x80040100,
    CPU_PPC_7450      = 0x80000200,
    CPU_PPC_7450b     = 0x80000201,
    CPU_PPC_7451      = 0x80000203,
    CPU_PPC_7451G     = 0x80000210,
    CPU_PPC_7455      = 0x80010201,
    CPU_PPC_7455F     = 0x80010303,
    CPU_PPC_7455G     = 0x80010304,
    CPU_PPC_7457      = 0x80020101,
    CPU_PPC_7457C     = 0x80020102,
    CPU_PPC_7457A     = 0x80030000,
    /* 64 bits PowerPC */
    CPU_PPC_620       = 0x00140000,
    CPU_PPC_630       = 0x00400000,
    CPU_PPC_631       = 0x00410000,
    CPU_PPC_POWER4    = 0x00350000,
    CPU_PPC_POWER4P   = 0x00380000,
    CPU_PPC_POWER5    = 0x003A0000,
    CPU_PPC_POWER5P   = 0x003B0000,
    CPU_PPC_970       = 0x00390000,
    CPU_PPC_970FX10   = 0x00391100,
    CPU_PPC_970FX20   = 0x003C0200,
    CPU_PPC_970FX21   = 0x003C0201,
    CPU_PPC_970FX30   = 0x003C0300,
    CPU_PPC_970FX31   = 0x003C0301,
#define CPU_PPC_970FX CPU_PPC_970FX31
    CPU_PPC_970MP10   = 0x00440100,
    CPU_PPC_970MP11   = 0x00440101,
#define CPU_PPC_970MP CPU_PPC_970MP11
    CPU_PPC_CELL10    = 0x00700100,
    CPU_PPC_CELL20    = 0x00700400,
    CPU_PPC_CELL30    = 0x00700500,
    CPU_PPC_CELL31    = 0x00700501,
#define CPU_PPC_CELL32 CPU_PPC_CELL31
#define CPU_PPC_CELL CPU_PPC_CELL32
    CPU_PPC_RS64      = 0x00330000,
    CPU_PPC_RS64II    = 0x00340000,
    CPU_PPC_RS64III   = 0x00360000,
    CPU_PPC_RS64IV    = 0x00370000,
    /* Original POWER */
    /* XXX: should be POWER (RIOS), RSC3308, RSC4608,
     * POWER2 (RIOS2) & RSC2 (P2SC) here
     */
#if 0
    CPU_POWER         = xxx,
#endif
#if 0
    CPU_POWER2        = xxx,
#endif
};

/* System version register (used on MPC 8xxx) */
enum {
    PPC_SVR_8540      = 0x80300000,
    PPC_SVR_8541E     = 0x807A0010,
    PPC_SVR_8543v10   = 0x80320010,
    PPC_SVR_8543v11   = 0x80320011,
    PPC_SVR_8543v20   = 0x80320020,
    PPC_SVR_8543Ev10  = 0x803A0010,
    PPC_SVR_8543Ev11  = 0x803A0011,
    PPC_SVR_8543Ev20  = 0x803A0020,
    PPC_SVR_8545      = 0x80310220,
    PPC_SVR_8545E     = 0x80390220,
    PPC_SVR_8547E     = 0x80390120,
    PPC_SCR_8548v10   = 0x80310010,
    PPC_SCR_8548v11   = 0x80310011,
    PPC_SCR_8548v20   = 0x80310020,
    PPC_SVR_8548Ev10  = 0x80390010,
    PPC_SVR_8548Ev11  = 0x80390011,
    PPC_SVR_8548Ev20  = 0x80390020,
    PPC_SVR_8555E     = 0x80790010,
    PPC_SVR_8560v10   = 0x80700010,
    PPC_SVR_8560v20   = 0x80700020,
};

/*****************************************************************************/
/* Instruction types */
enum {
    PPC_NONE        = 0x00000000,
    /* integer operations instructions             */
    /* flow control instructions                   */
    /* virtual memory instructions                 */
    /* ld/st with reservation instructions         */
    /* cache control instructions                  */
    /* spr/msr access instructions                 */
    PPC_INSNS_BASE  = 0x0000000000000001ULL,
#define PPC_INTEGER PPC_INSNS_BASE
#define PPC_FLOW    PPC_INSNS_BASE
#define PPC_MEM     PPC_INSNS_BASE
#define PPC_RES     PPC_INSNS_BASE
#define PPC_CACHE   PPC_INSNS_BASE
#define PPC_MISC    PPC_INSNS_BASE
    /* floating point operations instructions      */
    PPC_FLOAT       = 0x0000000000000002ULL,
    /* more floating point operations instructions */
    PPC_FLOAT_EXT   = 0x0000000000000004ULL,
    /* external control instructions               */
    PPC_EXTERN      = 0x0000000000000008ULL,
    /* segment register access instructions        */
    PPC_SEGMENT     = 0x0000000000000010ULL,
    /* Optional cache control instructions         */
    PPC_CACHE_OPT   = 0x0000000000000020ULL,
    /* Optional floating point op instructions     */
    PPC_FLOAT_OPT   = 0x0000000000000040ULL,
    /* Optional memory control instructions        */
    PPC_MEM_TLBIA   = 0x0000000000000080ULL,
    PPC_MEM_TLBIE   = 0x0000000000000100ULL,
    PPC_MEM_TLBSYNC = 0x0000000000000200ULL,
    /* eieio & sync                                */
    PPC_MEM_SYNC    = 0x0000000000000400ULL,
    /* PowerPC 6xx TLB management instructions     */
    PPC_6xx_TLB     = 0x0000000000000800ULL,
    /* Altivec support                             */
    PPC_ALTIVEC     = 0x0000000000001000ULL,
    /* Time base support                           */
    PPC_TB          = 0x0000000000002000ULL,
    /* Embedded PowerPC dedicated instructions     */
    PPC_EMB_COMMON  = 0x0000000000004000ULL,
    /* PowerPC 40x exception model                 */
    PPC_40x_EXCP    = 0x0000000000008000ULL,
    /* PowerPC 40x specific instructions           */
    PPC_40x_SPEC    = 0x0000000000010000ULL,
    /* PowerPC 405 Mac instructions                */
    PPC_405_MAC     = 0x0000000000020000ULL,
    /* PowerPC 440 specific instructions           */
    PPC_440_SPEC    = 0x0000000000040000ULL,
    /* Specific extensions */
    /* Power-to-PowerPC bridge (601)               */
    PPC_POWER_BR    = 0x0000000000080000ULL,
    /* PowerPC 602 specific */
    PPC_602_SPEC    = 0x0000000000100000ULL,
    /* Deprecated instructions                     */
    /* Original POWER instruction set              */
    PPC_POWER       = 0x0000000000200000ULL,
    /* POWER2 instruction set extension            */
    PPC_POWER2      = 0x0000000000400000ULL,
    /* Power RTC support */
    PPC_POWER_RTC   = 0x0000000000800000ULL,
    /* 64 bits PowerPC instructions                */
    /* 64 bits PowerPC instruction set             */
    PPC_64B         = 0x0000000001000000ULL,
    /* 64 bits hypervisor extensions               */
    PPC_64H         = 0x0000000002000000ULL,
    /* 64 bits PowerPC "bridge" features           */
    PPC_64_BRIDGE   = 0x0000000004000000ULL,
    /* BookE (embedded) PowerPC specification      */
    PPC_BOOKE       = 0x0000000008000000ULL,
    /* eieio */
    PPC_MEM_EIEIO   = 0x0000000010000000ULL,
    /* e500 vector instructions */
    PPC_E500_VECTOR = 0x0000000020000000ULL,
    /* PowerPC 4xx dedicated instructions     */
    PPC_4xx_COMMON  = 0x0000000040000000ULL,
    /* PowerPC 2.03 specification extensions */
    PPC_203         = 0x0000000080000000ULL,
    /* PowerPC 2.03 SPE extension */
    PPC_SPE         = 0x0000000100000000ULL,
    /* PowerPC 2.03 SPE floating-point extension */
    PPC_SPEFPU      = 0x0000000200000000ULL,
    /* SLB management */
    PPC_SLBI        = 0x0000000400000000ULL,
};

/* CPU run-time flags (MMU and exception model) */
enum {
    /* MMU model */
    PPC_FLAGS_MMU_MASK     = 0x0000000F,
    /* Standard 32 bits PowerPC MMU */
    PPC_FLAGS_MMU_32B      = 0x00000000,
    /* Standard 64 bits PowerPC MMU */
    PPC_FLAGS_MMU_64B      = 0x00000001,
    /* PowerPC 601 MMU */
    PPC_FLAGS_MMU_601      = 0x00000002,
    /* PowerPC 6xx MMU with software TLB */
    PPC_FLAGS_MMU_SOFT_6xx = 0x00000003,
    /* PowerPC 4xx MMU with software TLB */
    PPC_FLAGS_MMU_SOFT_4xx = 0x00000004,
    /* PowerPC 403 MMU */
    PPC_FLAGS_MMU_403      = 0x00000005,
    /* Freescale e500 MMU model */
    PPC_FLAGS_MMU_e500     = 0x00000006,
    /* BookE MMU model */
    PPC_FLAGS_MMU_BOOKE    = 0x00000007,
    /* Exception model */
    PPC_FLAGS_EXCP_MASK    = 0x000000F0,
    /* Standard PowerPC exception model */
    PPC_FLAGS_EXCP_STD     = 0x00000000,
    /* PowerPC 40x exception model */
    PPC_FLAGS_EXCP_40x     = 0x00000010,
    /* PowerPC 601 exception model */
    PPC_FLAGS_EXCP_601     = 0x00000020,
    /* PowerPC 602 exception model */
    PPC_FLAGS_EXCP_602     = 0x00000030,
    /* PowerPC 603 exception model */
    PPC_FLAGS_EXCP_603     = 0x00000040,
    /* PowerPC 604 exception model */
    PPC_FLAGS_EXCP_604     = 0x00000050,
    /* PowerPC 7x0 exception model */
    PPC_FLAGS_EXCP_7x0     = 0x00000060,
    /* PowerPC 7x5 exception model */
    PPC_FLAGS_EXCP_7x5     = 0x00000070,
    /* PowerPC 74xx exception model */
    PPC_FLAGS_EXCP_74xx    = 0x00000080,
    /* PowerPC 970 exception model */
    PPC_FLAGS_EXCP_970     = 0x00000090,
    /* BookE exception model */
    PPC_FLAGS_EXCP_BOOKE   = 0x000000A0,
};

#define PPC_MMU(env) (env->flags & PPC_FLAGS_MMU_MASK)
#define PPC_EXCP(env) (env->flags & PPC_FLAGS_EXCP_MASK)

/*****************************************************************************/
/* Supported instruction set definitions */
/* This generates an empty opcode table... */
#define PPC_INSNS_TODO (PPC_NONE)
#define PPC_FLAGS_TODO (0x00000000)

/* PowerPC 40x instruction set */
#define PPC_INSNS_EMB (PPC_INSNS_BASE | PPC_MEM_TLBSYNC | PPC_EMB_COMMON)
/* PowerPC 401 */
#define PPC_INSNS_401 (PPC_INSNS_TODO)
#define PPC_FLAGS_401 (PPC_FLAGS_TODO)
/* PowerPC 403 */
#define PPC_INSNS_403 (PPC_INSNS_EMB | PPC_MEM_SYNC | PPC_MEM_EIEIO |         \
                       PPC_MEM_TLBIA | PPC_4xx_COMMON | PPC_40x_EXCP |        \
                       PPC_40x_SPEC)
#define PPC_FLAGS_403 (PPC_FLAGS_MMU_403 | PPC_FLAGS_EXCP_40x)
/* PowerPC 405 */
#define PPC_INSNS_405 (PPC_INSNS_EMB | PPC_MEM_SYNC | PPC_MEM_EIEIO |         \
                       PPC_CACHE_OPT | PPC_MEM_TLBIA | PPC_TB |               \
                       PPC_4xx_COMMON | PPC_40x_SPEC |  PPC_40x_EXCP |        \
                       PPC_405_MAC)
#define PPC_FLAGS_405 (PPC_FLAGS_MMU_SOFT_4xx | PPC_FLAGS_EXCP_40x)
/* PowerPC 440 */
#define PPC_INSNS_440 (PPC_INSNS_EMB | PPC_CACHE_OPT | PPC_BOOKE |            \
                       PPC_4xx_COMMON | PPC_405_MAC | PPC_440_SPEC)
#define PPC_FLAGS_440 (PPC_FLAGS_MMU_BOOKE | PPC_FLAGS_EXCP_BOOKE)
/* Generic BookE PowerPC */
#define PPC_INSNS_BOOKE (PPC_INSNS_EMB | PPC_BOOKE | PPC_MEM_EIEIO |          \
                         PPC_FLOAT | PPC_FLOAT_OPT | PPC_CACHE_OPT)
#define PPC_FLAGS_BOOKE (PPC_FLAGS_MMU_BOOKE | PPC_FLAGS_EXCP_BOOKE)
/* e500 core */
#define PPC_INSNS_E500 (PPC_INSNS_EMB | PPC_BOOKE | PPC_MEM_EIEIO |           \
                        PPC_CACHE_OPT | PPC_E500_VECTOR)
#define PPC_FLAGS_E500 (PPC_FLAGS_MMU_SOFT_4xx | PPC_FLAGS_EXCP_40x)
/* Non-embedded PowerPC */
#define PPC_INSNS_COMMON  (PPC_INSNS_BASE | PPC_FLOAT | PPC_MEM_SYNC |        \
                            PPC_MEM_EIEIO | PPC_SEGMENT | PPC_MEM_TLBIE)
/* PowerPC 601 */
#define PPC_INSNS_601 (PPC_INSNS_COMMON | PPC_EXTERN | PPC_POWER_BR)
#define PPC_FLAGS_601 (PPC_FLAGS_MMU_601 | PPC_FLAGS_EXCP_601)
/* PowerPC 602 */
#define PPC_INSNS_602 (PPC_INSNS_COMMON | PPC_FLOAT_EXT | PPC_6xx_TLB |       \
                       PPC_MEM_TLBSYNC | PPC_TB | PPC_602_SPEC)
#define PPC_FLAGS_602 (PPC_FLAGS_MMU_SOFT_6xx | PPC_FLAGS_EXCP_602)
/* PowerPC 603 */
#define PPC_INSNS_603 (PPC_INSNS_COMMON | PPC_FLOAT_EXT | PPC_6xx_TLB |       \
                       PPC_MEM_TLBSYNC | PPC_EXTERN | PPC_TB)
#define PPC_FLAGS_603 (PPC_FLAGS_MMU_SOFT_6xx | PPC_FLAGS_EXCP_603)
/* PowerPC G2 */
#define PPC_INSNS_G2 (PPC_INSNS_COMMON | PPC_FLOAT_EXT | PPC_6xx_TLB |        \
                      PPC_MEM_TLBSYNC | PPC_EXTERN | PPC_TB)
#define PPC_FLAGS_G2 (PPC_FLAGS_MMU_SOFT_6xx | PPC_FLAGS_EXCP_603)
/* PowerPC 604 */
#define PPC_INSNS_604 (PPC_INSNS_COMMON | PPC_FLOAT_EXT | PPC_EXTERN |        \
                       PPC_MEM_TLBSYNC | PPC_TB)
#define PPC_FLAGS_604 (PPC_FLAGS_MMU_32B | PPC_FLAGS_EXCP_604)
/* PowerPC 740/750 (aka G3) */
#define PPC_INSNS_7x0 (PPC_INSNS_COMMON | PPC_FLOAT_EXT | PPC_EXTERN |        \
                       PPC_MEM_TLBSYNC | PPC_TB)
#define PPC_FLAGS_7x0 (PPC_FLAGS_MMU_32B | PPC_FLAGS_EXCP_7x0)
/* PowerPC 745/755 */
#define PPC_INSNS_7x5 (PPC_INSNS_COMMON | PPC_FLOAT_EXT | PPC_EXTERN |        \
                       PPC_MEM_TLBSYNC | PPC_TB | PPC_6xx_TLB)
#define PPC_FLAGS_7x5 (PPC_FLAGS_MMU_SOFT_6xx | PPC_FLAGS_EXCP_7x5)
/* PowerPC 74xx (aka G4) */
#define PPC_INSNS_74xx (PPC_INSNS_COMMON | PPC_FLOAT_EXT | PPC_ALTIVEC |      \
                        PPC_MEM_TLBSYNC | PPC_TB)
#define PPC_FLAGS_74xx (PPC_FLAGS_MMU_32B | PPC_FLAGS_EXCP_74xx)
/* PowerPC 970 (aka G5) */
#define PPC_INSNS_970  (PPC_INSNS_COMMON | PPC_FLOAT_EXT | PPC_FLOAT_OPT |    \
                        PPC_ALTIVEC | PPC_MEM_TLBSYNC | PPC_TB |              \
                        PPC_64B | PPC_64_BRIDGE | PPC_SLBI)
#define PPC_FLAGS_970  (PPC_FLAGS_MMU_64B | PPC_FLAGS_EXCP_970)

/* Default PowerPC will be 604/970 */
#define PPC_INSNS_PPC32 PPC_INSNS_604
#define PPC_FLAGS_PPC32 PPC_FLAGS_604
#define PPC_INSNS_PPC64 PPC_INSNS_970
#define PPC_FLAGS_PPC64 PPC_FLAGS_970
#define PPC_INSNS_DEFAULT PPC_INSNS_604
#define PPC_FLAGS_DEFAULT PPC_FLAGS_604
typedef struct ppc_def_t ppc_def_t;

/*****************************************************************************/
/* Types used to describe some PowerPC registers */
typedef struct CPUPPCState CPUPPCState;
typedef struct opc_handler_t opc_handler_t;
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
    target_ulong RPN;
    target_ulong EPN;
    target_ulong PID;
    int size;
    int prot;
    int attr; /* Storage attributes */
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
    target_ulong t0, t1, t2;
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
    int (*dcr_read)(ppc_dcr_t *dcr_env, int dcr_num, target_ulong *val);
    int (*dcr_write)(ppc_dcr_t *dcr_env, int dcr_num, target_ulong val);
    ppc_dcr_t *dcr_env;

    /* PowerPC TLB registers (for 4xx and 60x software driven TLBs) */
    int nb_tlb;      /* Total number of TLB                                  */
    int tlb_per_way; /* Speed-up helper: used to avoid divisions at run time */
    int nb_ways;     /* Number of ways in the TLB set                        */
    int last_way;    /* Last used way used to allocate TLB in a LRU way      */
    int id_tlbs;     /* If 1, MMU has separated TLBs for instructions & data */
    int nb_pids;     /* Number of available PID registers                    */
    ppc_tlb_t *tlb;  /* TLB is optional. Allocate them only if needed        */
    /* Callbacks for specific checks on some implementations */
    int (*tlb_check_more)(CPUPPCState *env, ppc_tlb_t *tlb, int *prot,
                          target_ulong vaddr, int rw, int acc_type,
                          int is_user);
    /* 403 dedicated access protection registers */
    target_ulong pb[4];

    /* Those resources are used during exception processing */
    /* CPU model definition */
    uint64_t msr_mask;
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
    uint32_t hflags;

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
CPUPPCState *cpu_ppc_init(void);
int cpu_ppc_exec(CPUPPCState *s);
void cpu_ppc_close(CPUPPCState *s);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
int cpu_ppc_signal_handler(int host_signum, void *pinfo, 
                           void *puc);

void do_interrupt (CPUPPCState *env);
void ppc_hw_interrupt (CPUPPCState *env);
void cpu_loop_exit(void);

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
uint32_t ppc_load_xer (CPUPPCState *env);
void ppc_store_xer (CPUPPCState *env, uint32_t value);
target_ulong do_load_msr (CPUPPCState *env);
void do_store_msr (CPUPPCState *env, target_ulong value);
void ppc_store_msr_32 (CPUPPCState *env, uint32_t value);

void do_compute_hflags (CPUPPCState *env);

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
uint32_t cpu_ppc_load_decr (CPUPPCState *env);
void cpu_ppc_store_decr (CPUPPCState *env, uint32_t value);
uint32_t cpu_ppc601_load_rtcl (CPUPPCState *env);
uint32_t cpu_ppc601_load_rtcu (CPUPPCState *env);
#if !defined(CONFIG_USER_ONLY)
void cpu_ppc601_store_rtcl (CPUPPCState *env, uint32_t value);
void cpu_ppc601_store_rtcu (CPUPPCState *env, uint32_t value);
target_ulong load_40x_pit (CPUPPCState *env);
void store_40x_pit (CPUPPCState *env, target_ulong val);
void store_booke_tcr (CPUPPCState *env, target_ulong val);
void store_booke_tsr (CPUPPCState *env, target_ulong val);
#endif
#endif

#define TARGET_PAGE_BITS 12
#include "cpu-all.h"

/*****************************************************************************/
/* Registers definitions */
#define ugpr(n) (env->gpr[n])

#define XER_SO 31
#define XER_OV 30
#define XER_CA 29
#define XER_CMP 8
#define XER_BC 0
#define xer_so  env->xer[4]
#define xer_ov  env->xer[6]
#define xer_ca  env->xer[2]
#define xer_cmp env->xer[1]
#define xer_bc env->xer[0]

/* SPR definitions */
#define SPR_MQ           (0x000)
#define SPR_XER          (0x001)
#define SPR_601_VRTCU    (0x004)
#define SPR_601_VRTCL    (0x005)
#define SPR_601_UDECR    (0x006)
#define SPR_LR           (0x008)
#define SPR_CTR          (0x009)
#define SPR_DSISR        (0x012)
#define SPR_DAR          (0x013)
#define SPR_601_RTCU     (0x014)
#define SPR_601_RTCL     (0x015)
#define SPR_DECR         (0x016)
#define SPR_SDR1         (0x019)
#define SPR_SRR0         (0x01A)
#define SPR_SRR1         (0x01B)
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
#define SPR_58x_CMPA     (0x090)
#define SPR_58x_CMPB     (0x091)
#define SPR_58x_CMPC     (0x092)
#define SPR_58x_CMPD     (0x093)
#define SPR_58x_ICR      (0x094)
#define SPR_58x_DER      (0x094)
#define SPR_58x_COUNTA   (0x096)
#define SPR_58x_COUNTB   (0x097)
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
#define SPR_SVR          (0x11E)
#define SPR_BOOKE_PIR    (0x11E)
#define SPR_PVR          (0x11F)
#define SPR_HSPRG0       (0x130)
#define SPR_BOOKE_DBSR   (0x130)
#define SPR_HSPRG1       (0x131)
#define SPR_BOOKE_DBCR0  (0x134)
#define SPR_IBCR         (0x135)
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
#define SPR_HSSR0        (0x13A)
#define SPR_BOOKE_IAC3   (0x13A)
#define SPR_HSSR1        (0x13B)
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
#define SPR_E500_SPEFSCR (0x200)
#define SPR_E500_BBEAR   (0x201)
#define SPR_E500_BBTAR   (0x202)
#define SPR_BOOKE_ATBL   (0x20E)
#define SPR_BOOKE_ATBU   (0x20F)
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
#define SPR_440_INV0     (0x370)
#define SPR_440_INV1     (0x371)
#define SPR_440_INV2     (0x372)
#define SPR_440_INV3     (0x373)
#define SPR_440_IVT0     (0x374)
#define SPR_440_IVT1     (0x375)
#define SPR_440_IVT2     (0x376)
#define SPR_440_IVT3     (0x377)
#define SPR_440_DNV0     (0x390)
#define SPR_440_DNV1     (0x391)
#define SPR_440_DNV2     (0x392)
#define SPR_440_DNV3     (0x393)
#define SPR_440_DVT0     (0x394)
#define SPR_440_DVT1     (0x395)
#define SPR_440_DVT2     (0x396)
#define SPR_440_DVT3     (0x397)
#define SPR_440_DVLIM    (0x398)
#define SPR_440_IVLIM    (0x399)
#define SPR_440_RSTCFG   (0x39B)
#define SPR_BOOKE_DCBTRL (0x39C)
#define SPR_BOOKE_DCBTRH (0x39D)
#define SPR_BOOKE_ICBTRL (0x39E)
#define SPR_BOOKE_ICBTRH (0x39F)
#define SPR_UMMCR0       (0x3A8)
#define SPR_UPMC1        (0x3A9)
#define SPR_UPMC2        (0x3AA)
#define SPR_USIA         (0x3AB)
#define SPR_UMMCR1       (0x3AC)
#define SPR_UPMC3        (0x3AD)
#define SPR_UPMC4        (0x3AE)
#define SPR_USDA         (0x3AF)
#define SPR_40x_ZPR      (0x3B0)
#define SPR_BOOKE_MAS7   (0x3B0)
#define SPR_40x_PID      (0x3B1)
#define SPR_440_MMUCR    (0x3B2)
#define SPR_4xx_CCR0     (0x3B3)
#define SPR_BOOKE_EPLC   (0x3B3)
#define SPR_405_IAC3     (0x3B4)
#define SPR_BOOKE_EPSC   (0x3B4)
#define SPR_405_IAC4     (0x3B5)
#define SPR_405_DVC1     (0x3B6)
#define SPR_405_DVC2     (0x3B7)
#define SPR_MMCR0        (0x3B8)
#define SPR_PMC1         (0x3B9)
#define SPR_40x_SGR      (0x3B9)
#define SPR_PMC2         (0x3BA)
#define SPR_40x_DCWR     (0x3BA)
#define SPR_SIA          (0x3BB)
#define SPR_405_SLER     (0x3BB)
#define SPR_MMCR1        (0x3BC)
#define SPR_405_SU0R     (0x3BC)
#define SPR_PMC3         (0x3BD)
#define SPR_405_DBCR1    (0x3BD)
#define SPR_PMC4         (0x3BE)
#define SPR_SDA          (0x3BF)
#define SPR_403_VTBL     (0x3CC)
#define SPR_403_VTBU     (0x3CD)
#define SPR_DMISS        (0x3D0)
#define SPR_DCMP         (0x3D1)
#define SPR_HASH1        (0x3D2)
#define SPR_HASH2        (0x3D3)
#define SPR_BOOKE_ICBDR  (0x3D3)
#define SPR_IMISS        (0x3D4)
#define SPR_40x_ESR      (0x3D4)
#define SPR_ICMP         (0x3D5)
#define SPR_40x_DEAR     (0x3D5)
#define SPR_RPA          (0x3D6)
#define SPR_40x_EVPR     (0x3D6)
#define SPR_403_CDBCR    (0x3D7)
#define SPR_TCR          (0x3D8)
#define SPR_40x_TSR      (0x3D8)
#define SPR_IBR          (0x3DA)
#define SPR_40x_TCR      (0x3DA)
#define SPR_ESASR        (0x3DB)
#define SPR_40x_PIT      (0x3DB)
#define SPR_403_TBL      (0x3DC)
#define SPR_403_TBU      (0x3DD)
#define SPR_SEBR         (0x3DE)
#define SPR_40x_SRR2     (0x3DE)
#define SPR_SER          (0x3DF)
#define SPR_40x_SRR3     (0x3DF)
#define SPR_HID0         (0x3F0)
#define SPR_40x_DBSR     (0x3F0)
#define SPR_HID1         (0x3F1)
#define SPR_IABR         (0x3F2)
#define SPR_40x_DBCR0    (0x3F2)
#define SPR_601_HID2     (0x3F2)
#define SPR_E500_L1CSR0  (0x3F2)
#define SPR_HID2         (0x3F3)
#define SPR_E500_L1CSR1  (0x3F3)
#define SPR_440_DBDR     (0x3F3)
#define SPR_40x_IAC1     (0x3F4)
#define SPR_BOOKE_MMUCSR0 (0x3F4)
#define SPR_DABR         (0x3F5)
#define DABR_MASK (~(target_ulong)0x7)
#define SPR_E500_BUCSR   (0x3F5)
#define SPR_40x_IAC2     (0x3F5)
#define SPR_601_HID5     (0x3F5)
#define SPR_40x_DAC1     (0x3F6)
#define SPR_40x_DAC2     (0x3F7)
#define SPR_BOOKE_MMUCFG (0x3F7)
#define SPR_L2PM         (0x3F8)
#define SPR_750_HID2     (0x3F8)
#define SPR_L2CR         (0x3F9)
#define SPR_IABR2        (0x3FA)
#define SPR_40x_DCCR     (0x3FA)
#define SPR_ICTC         (0x3FB)
#define SPR_40x_ICCR     (0x3FB)
#define SPR_THRM1        (0x3FC)
#define SPR_403_PBL1     (0x3FC)
#define SPR_SP           (0x3FD)
#define SPR_THRM2        (0x3FD)
#define SPR_403_PBU1     (0x3FD)
#define SPR_LT           (0x3FE)
#define SPR_THRM3        (0x3FE)
#define SPR_FPECR        (0x3FE)
#define SPR_403_PBL2     (0x3FE)
#define SPR_PIR          (0x3FF)
#define SPR_403_PBU2     (0x3FF)
#define SPR_601_HID15    (0x3FF)
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

/*****************************************************************************/
/* Exceptions */
#define EXCP_NONE          -1
/* PowerPC hardware exceptions : exception vectors defined in PowerPC book 3 */
#define EXCP_RESET         0x0100 /* System reset                            */
#define EXCP_MACHINE_CHECK 0x0200 /* Machine check exception                 */
#define EXCP_DSI           0x0300 /* Data storage exception                  */
#define EXCP_DSEG          0x0380 /* Data segment exception                  */
#define EXCP_ISI           0x0400 /* Instruction storage exception           */
#define EXCP_ISEG          0x0480 /* Instruction segment exception           */
#define EXCP_EXTERNAL      0x0500 /* External interruption                   */
#define EXCP_ALIGN         0x0600 /* Alignment exception                     */
#define EXCP_PROGRAM       0x0700 /* Program exception                       */
#define EXCP_NO_FP         0x0800 /* Floating point unavailable exception    */
#define EXCP_DECR          0x0900 /* Decrementer exception                   */
#define EXCP_HDECR         0x0980 /* Hypervisor decrementer exception        */
#define EXCP_SYSCALL       0x0C00 /* System call                             */
#define EXCP_TRACE         0x0D00 /* Trace exception                         */
#define EXCP_PERF          0x0F00 /* Performance monitor exception           */
/* Exceptions defined in PowerPC 32 bits programming environment manual      */
#define EXCP_FP_ASSIST     0x0E00 /* Floating-point assist                   */
/* Implementation specific exceptions                                        */
/* 40x exceptions                                                            */
#define EXCP_40x_PIT       0x1000 /* Programmable interval timer interrupt   */
#define EXCP_40x_FIT       0x1010 /* Fixed interval timer interrupt          */
#define EXCP_40x_WATCHDOG  0x1020 /* Watchdog timer exception                */
#define EXCP_40x_DTLBMISS  0x1100 /* Data TLB miss exception                 */
#define EXCP_40x_ITLBMISS  0x1200 /* Instruction TLB miss exception          */
#define EXCP_40x_DEBUG     0x2000 /* Debug exception                         */
/* 405 specific exceptions                                                   */
#define EXCP_405_APU       0x0F20 /* APU unavailable exception               */
/* TLB assist exceptions (602/603)                                           */
#define EXCP_I_TLBMISS     0x1000 /* Instruction TLB miss                    */
#define EXCP_DL_TLBMISS    0x1100 /* Data load TLB miss                      */
#define EXCP_DS_TLBMISS    0x1200 /* Data store TLB miss                     */
/* Breakpoint exceptions (602/603/604/620/740/745/750/755...)                */
#define EXCP_IABR          0x1300 /* Instruction address breakpoint          */
#define EXCP_SMI           0x1400 /* System management interrupt             */
/* Altivec related exceptions                                                */
#define EXCP_VPU           0x0F20 /* VPU unavailable exception               */
/* 601 specific exceptions                                                   */
#define EXCP_601_IO        0x0600 /* IO error exception                      */
#define EXCP_601_RUNM      0x2000 /* Run mode exception                      */
/* 602 specific exceptions                                                   */
#define EXCP_602_WATCHDOG  0x1500 /* Watchdog exception                      */
#define EXCP_602_EMUL      0x1600 /* Emulation trap exception                */
/* G2 specific exceptions                                                    */
#define EXCP_G2_CRIT       0x0A00 /* Critical interrupt                      */
/* MPC740/745/750 & IBM 750 specific exceptions                              */
#define EXCP_THRM          0x1700 /* Thermal management interrupt            */
/* 74xx specific exceptions                                                  */
#define EXCP_74xx_VPUA     0x1600 /* VPU assist exception                    */
/* 970FX specific exceptions                                                 */
#define EXCP_970_SOFTP     0x1500 /* Soft patch exception                    */
#define EXCP_970_MAINT     0x1600 /* Maintenance exception                   */
#define EXCP_970_THRM      0x1800 /* Thermal exception                       */
#define EXCP_970_VPUA      0x1700 /* VPU assist exception                    */
/* SPE related exceptions                                                    */
#define EXCP_NO_SPE        0x0F20 /* SPE unavailable exception               */
/* End of exception vectors area                                             */
#define EXCP_PPC_MAX       0x4000
/* Qemu exceptions: special cases we want to stop translation                */
#define EXCP_MTMSR         0x11000 /* mtmsr instruction:                     */
                                   /* may change privilege level             */
#define EXCP_BRANCH        0x11001 /* branch instruction                     */
#define EXCP_SYSCALL_USER  0x12000 /* System call in user mode only          */
#define EXCP_INTERRUPT_CRITICAL 0x13000 /* critical IRQ                      */

/* Error codes */
enum {
    /* Exception subtypes for EXCP_ALIGN                            */
    EXCP_ALIGN_FP      = 0x01,  /* FP alignment exception           */
    EXCP_ALIGN_LST     = 0x02,  /* Unaligned mult/extern load/store */
    EXCP_ALIGN_LE      = 0x03,  /* Multiple little-endian access    */
    EXCP_ALIGN_PROT    = 0x04,  /* Access cross protection boundary */
    EXCP_ALIGN_BAT     = 0x05,  /* Access cross a BAT/seg boundary  */
    EXCP_ALIGN_CACHE   = 0x06,  /* Impossible dcbz access           */
    /* Exception subtypes for EXCP_PROGRAM                          */
    /* FP exceptions */
    EXCP_FP            = 0x10,
    EXCP_FP_OX         = 0x01,  /* FP overflow                      */
    EXCP_FP_UX         = 0x02,  /* FP underflow                     */
    EXCP_FP_ZX         = 0x03,  /* FP divide by zero                */
    EXCP_FP_XX         = 0x04,  /* FP inexact                       */
    EXCP_FP_VXNAN      = 0x05,  /* FP invalid SNaN op               */
    EXCP_FP_VXISI      = 0x06,  /* FP invalid infinite substraction */
    EXCP_FP_VXIDI      = 0x07,  /* FP invalid infinite divide       */
    EXCP_FP_VXZDZ      = 0x08,  /* FP invalid zero divide           */
    EXCP_FP_VXIMZ      = 0x09,  /* FP invalid infinite * zero       */
    EXCP_FP_VXVC       = 0x0A,  /* FP invalid compare               */
    EXCP_FP_VXSOFT     = 0x0B,  /* FP invalid operation             */
    EXCP_FP_VXSQRT     = 0x0C,  /* FP invalid square root           */
    EXCP_FP_VXCVI      = 0x0D,  /* FP invalid integer conversion    */
    /* Invalid instruction */
    EXCP_INVAL         = 0x20,
    EXCP_INVAL_INVAL   = 0x01,  /* Invalid instruction              */
    EXCP_INVAL_LSWX    = 0x02,  /* Invalid lswx instruction         */
    EXCP_INVAL_SPR     = 0x03,  /* Invalid SPR access               */
    EXCP_INVAL_FP      = 0x04,  /* Unimplemented mandatory fp instr */
    /* Privileged instruction */
    EXCP_PRIV          = 0x30,
    EXCP_PRIV_OPC      = 0x01,
    EXCP_PRIV_REG      = 0x02,
    /* Trap */
    EXCP_TRAP          = 0x40,
};

/* Hardware interruption sources:
 * all those exception can be raised simulteaneously
 */
/* Input pins definitions */
enum {
    /* 6xx bus input pins */
    PPC_INPUT_HRESET     = 0,
    PPC_INPUT_SRESET     = 1,
    PPC_INPUT_CKSTP_IN   = 2,
    PPC_INPUT_MCP        = 3,
    PPC_INPUT_SMI        = 4,
    PPC_INPUT_INT        = 5,
    /* Embedded PowerPC input pins */
    PPC_INPUT_CINT       = 6,
    PPC_INPUT_NB,
};

/* Hardware exceptions definitions */
enum {
    /* External hardware exception sources */
    PPC_INTERRUPT_RESET  = 0,  /* Reset exception                      */
    PPC_INTERRUPT_MCK    = 1,  /* Machine check exception              */
    PPC_INTERRUPT_EXT    = 2,  /* External interrupt                   */
    PPC_INTERRUPT_SMI    = 3,  /* System management interrupt          */
    PPC_INTERRUPT_CEXT   = 4,  /* Critical external interrupt          */
    PPC_INTERRUPT_DEBUG  = 5,  /* External debug exception             */
    /* Internal hardware exception sources */
    PPC_INTERRUPT_DECR   = 6,  /* Decrementer exception                */
    PPC_INTERRUPT_HDECR  = 7,  /* Hypervisor decrementer exception     */
    PPC_INTERRUPT_PIT    = 8,  /* Programmable inteval timer interrupt */
    PPC_INTERRUPT_FIT    = 9,  /* Fixed interval timer interrupt       */
    PPC_INTERRUPT_WDT    = 10, /* Watchdog timer interrupt             */
};

/*****************************************************************************/

#endif /* !defined (__CPU_PPC_H__) */
