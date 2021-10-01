/*
 * i386 virtual CPU header
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef I386_CPU_H
#define I386_CPU_H

#include "sysemu/tcg.h"
#include "cpu-qom.h"
#include "kvm/hyperv-proto.h"
#include "exec/cpu-defs.h"
#include "qapi/qapi-types-common.h"

/* The x86 has a strong memory model with some store-after-load re-ordering */
#define TCG_GUEST_DEFAULT_MO      (TCG_MO_ALL & ~TCG_MO_ST_LD)

#define KVM_HAVE_MCE_INJECTION 1

/* support for self modifying code even if the modified instruction is
   close to the modifying instruction */
#define TARGET_HAS_PRECISE_SMC

#ifdef TARGET_X86_64
#define I386_ELF_MACHINE  EM_X86_64
#define ELF_MACHINE_UNAME "x86_64"
#else
#define I386_ELF_MACHINE  EM_386
#define ELF_MACHINE_UNAME "i686"
#endif

enum {
    R_EAX = 0,
    R_ECX = 1,
    R_EDX = 2,
    R_EBX = 3,
    R_ESP = 4,
    R_EBP = 5,
    R_ESI = 6,
    R_EDI = 7,
    R_R8 = 8,
    R_R9 = 9,
    R_R10 = 10,
    R_R11 = 11,
    R_R12 = 12,
    R_R13 = 13,
    R_R14 = 14,
    R_R15 = 15,

    R_AL = 0,
    R_CL = 1,
    R_DL = 2,
    R_BL = 3,
    R_AH = 4,
    R_CH = 5,
    R_DH = 6,
    R_BH = 7,
};

typedef enum X86Seg {
    R_ES = 0,
    R_CS = 1,
    R_SS = 2,
    R_DS = 3,
    R_FS = 4,
    R_GS = 5,
    R_LDTR = 6,
    R_TR = 7,
} X86Seg;

/* segment descriptor fields */
#define DESC_G_SHIFT    23
#define DESC_G_MASK     (1 << DESC_G_SHIFT)
#define DESC_B_SHIFT    22
#define DESC_B_MASK     (1 << DESC_B_SHIFT)
#define DESC_L_SHIFT    21 /* x86_64 only : 64 bit code segment */
#define DESC_L_MASK     (1 << DESC_L_SHIFT)
#define DESC_AVL_SHIFT  20
#define DESC_AVL_MASK   (1 << DESC_AVL_SHIFT)
#define DESC_P_SHIFT    15
#define DESC_P_MASK     (1 << DESC_P_SHIFT)
#define DESC_DPL_SHIFT  13
#define DESC_DPL_MASK   (3 << DESC_DPL_SHIFT)
#define DESC_S_SHIFT    12
#define DESC_S_MASK     (1 << DESC_S_SHIFT)
#define DESC_TYPE_SHIFT 8
#define DESC_TYPE_MASK  (15 << DESC_TYPE_SHIFT)
#define DESC_A_MASK     (1 << 8)

#define DESC_CS_MASK    (1 << 11) /* 1=code segment 0=data segment */
#define DESC_C_MASK     (1 << 10) /* code: conforming */
#define DESC_R_MASK     (1 << 9)  /* code: readable */

#define DESC_E_MASK     (1 << 10) /* data: expansion direction */
#define DESC_W_MASK     (1 << 9)  /* data: writable */

#define DESC_TSS_BUSY_MASK (1 << 9)

/* eflags masks */
#define CC_C    0x0001
#define CC_P    0x0004
#define CC_A    0x0010
#define CC_Z    0x0040
#define CC_S    0x0080
#define CC_O    0x0800

#define TF_SHIFT   8
#define IOPL_SHIFT 12
#define VM_SHIFT   17

#define TF_MASK                 0x00000100
#define IF_MASK                 0x00000200
#define DF_MASK                 0x00000400
#define IOPL_MASK               0x00003000
#define NT_MASK                 0x00004000
#define RF_MASK                 0x00010000
#define VM_MASK                 0x00020000
#define AC_MASK                 0x00040000
#define VIF_MASK                0x00080000
#define VIP_MASK                0x00100000
#define ID_MASK                 0x00200000

/* hidden flags - used internally by qemu to represent additional cpu
   states. Only the INHIBIT_IRQ, SMM and SVMI are not redundant. We
   avoid using the IOPL_MASK, TF_MASK, VM_MASK and AC_MASK bit
   positions to ease oring with eflags. */
/* current cpl */
#define HF_CPL_SHIFT         0
/* true if hardware interrupts must be disabled for next instruction */
#define HF_INHIBIT_IRQ_SHIFT 3
/* 16 or 32 segments */
#define HF_CS32_SHIFT        4
#define HF_SS32_SHIFT        5
/* zero base for DS, ES and SS : can be '0' only in 32 bit CS segment */
#define HF_ADDSEG_SHIFT      6
/* copy of CR0.PE (protected mode) */
#define HF_PE_SHIFT          7
#define HF_TF_SHIFT          8 /* must be same as eflags */
#define HF_MP_SHIFT          9 /* the order must be MP, EM, TS */
#define HF_EM_SHIFT         10
#define HF_TS_SHIFT         11
#define HF_IOPL_SHIFT       12 /* must be same as eflags */
#define HF_LMA_SHIFT        14 /* only used on x86_64: long mode active */
#define HF_CS64_SHIFT       15 /* only used on x86_64: 64 bit code segment  */
#define HF_RF_SHIFT         16 /* must be same as eflags */
#define HF_VM_SHIFT         17 /* must be same as eflags */
#define HF_AC_SHIFT         18 /* must be same as eflags */
#define HF_SMM_SHIFT        19 /* CPU in SMM mode */
#define HF_SVME_SHIFT       20 /* SVME enabled (copy of EFER.SVME) */
#define HF_GUEST_SHIFT      21 /* SVM intercepts are active */
#define HF_OSFXSR_SHIFT     22 /* CR4.OSFXSR */
#define HF_SMAP_SHIFT       23 /* CR4.SMAP */
#define HF_IOBPT_SHIFT      24 /* an io breakpoint enabled */
#define HF_MPX_EN_SHIFT     25 /* MPX Enabled (CR4+XCR0+BNDCFGx) */
#define HF_MPX_IU_SHIFT     26 /* BND registers in-use */

#define HF_CPL_MASK          (3 << HF_CPL_SHIFT)
#define HF_INHIBIT_IRQ_MASK  (1 << HF_INHIBIT_IRQ_SHIFT)
#define HF_CS32_MASK         (1 << HF_CS32_SHIFT)
#define HF_SS32_MASK         (1 << HF_SS32_SHIFT)
#define HF_ADDSEG_MASK       (1 << HF_ADDSEG_SHIFT)
#define HF_PE_MASK           (1 << HF_PE_SHIFT)
#define HF_TF_MASK           (1 << HF_TF_SHIFT)
#define HF_MP_MASK           (1 << HF_MP_SHIFT)
#define HF_EM_MASK           (1 << HF_EM_SHIFT)
#define HF_TS_MASK           (1 << HF_TS_SHIFT)
#define HF_IOPL_MASK         (3 << HF_IOPL_SHIFT)
#define HF_LMA_MASK          (1 << HF_LMA_SHIFT)
#define HF_CS64_MASK         (1 << HF_CS64_SHIFT)
#define HF_RF_MASK           (1 << HF_RF_SHIFT)
#define HF_VM_MASK           (1 << HF_VM_SHIFT)
#define HF_AC_MASK           (1 << HF_AC_SHIFT)
#define HF_SMM_MASK          (1 << HF_SMM_SHIFT)
#define HF_SVME_MASK         (1 << HF_SVME_SHIFT)
#define HF_GUEST_MASK        (1 << HF_GUEST_SHIFT)
#define HF_OSFXSR_MASK       (1 << HF_OSFXSR_SHIFT)
#define HF_SMAP_MASK         (1 << HF_SMAP_SHIFT)
#define HF_IOBPT_MASK        (1 << HF_IOBPT_SHIFT)
#define HF_MPX_EN_MASK       (1 << HF_MPX_EN_SHIFT)
#define HF_MPX_IU_MASK       (1 << HF_MPX_IU_SHIFT)

/* hflags2 */

#define HF2_GIF_SHIFT            0 /* if set CPU takes interrupts */
#define HF2_HIF_SHIFT            1 /* value of IF_MASK when entering SVM */
#define HF2_NMI_SHIFT            2 /* CPU serving NMI */
#define HF2_VINTR_SHIFT          3 /* value of V_INTR_MASKING bit */
#define HF2_SMM_INSIDE_NMI_SHIFT 4 /* CPU serving SMI nested inside NMI */
#define HF2_MPX_PR_SHIFT         5 /* BNDCFGx.BNDPRESERVE */
#define HF2_NPT_SHIFT            6 /* Nested Paging enabled */
#define HF2_IGNNE_SHIFT          7 /* Ignore CR0.NE=0 */
#define HF2_VGIF_SHIFT           8 /* Can take VIRQ*/

#define HF2_GIF_MASK            (1 << HF2_GIF_SHIFT)
#define HF2_HIF_MASK            (1 << HF2_HIF_SHIFT)
#define HF2_NMI_MASK            (1 << HF2_NMI_SHIFT)
#define HF2_VINTR_MASK          (1 << HF2_VINTR_SHIFT)
#define HF2_SMM_INSIDE_NMI_MASK (1 << HF2_SMM_INSIDE_NMI_SHIFT)
#define HF2_MPX_PR_MASK         (1 << HF2_MPX_PR_SHIFT)
#define HF2_NPT_MASK            (1 << HF2_NPT_SHIFT)
#define HF2_IGNNE_MASK          (1 << HF2_IGNNE_SHIFT)
#define HF2_VGIF_MASK           (1 << HF2_VGIF_SHIFT)

#define CR0_PE_SHIFT 0
#define CR0_MP_SHIFT 1

#define CR0_PE_MASK  (1U << 0)
#define CR0_MP_MASK  (1U << 1)
#define CR0_EM_MASK  (1U << 2)
#define CR0_TS_MASK  (1U << 3)
#define CR0_ET_MASK  (1U << 4)
#define CR0_NE_MASK  (1U << 5)
#define CR0_WP_MASK  (1U << 16)
#define CR0_AM_MASK  (1U << 18)
#define CR0_NW_MASK  (1U << 29)
#define CR0_CD_MASK  (1U << 30)
#define CR0_PG_MASK  (1U << 31)

#define CR4_VME_MASK  (1U << 0)
#define CR4_PVI_MASK  (1U << 1)
#define CR4_TSD_MASK  (1U << 2)
#define CR4_DE_MASK   (1U << 3)
#define CR4_PSE_MASK  (1U << 4)
#define CR4_PAE_MASK  (1U << 5)
#define CR4_MCE_MASK  (1U << 6)
#define CR4_PGE_MASK  (1U << 7)
#define CR4_PCE_MASK  (1U << 8)
#define CR4_OSFXSR_SHIFT 9
#define CR4_OSFXSR_MASK (1U << CR4_OSFXSR_SHIFT)
#define CR4_OSXMMEXCPT_MASK  (1U << 10)
#define CR4_UMIP_MASK   (1U << 11)
#define CR4_LA57_MASK   (1U << 12)
#define CR4_VMXE_MASK   (1U << 13)
#define CR4_SMXE_MASK   (1U << 14)
#define CR4_FSGSBASE_MASK (1U << 16)
#define CR4_PCIDE_MASK  (1U << 17)
#define CR4_OSXSAVE_MASK (1U << 18)
#define CR4_SMEP_MASK   (1U << 20)
#define CR4_SMAP_MASK   (1U << 21)
#define CR4_PKE_MASK   (1U << 22)
#define CR4_PKS_MASK   (1U << 24)

#define CR4_RESERVED_MASK \
(~(target_ulong)(CR4_VME_MASK | CR4_PVI_MASK | CR4_TSD_MASK \
                | CR4_DE_MASK | CR4_PSE_MASK | CR4_PAE_MASK \
                | CR4_MCE_MASK | CR4_PGE_MASK | CR4_PCE_MASK \
                | CR4_OSFXSR_MASK | CR4_OSXMMEXCPT_MASK |CR4_UMIP_MASK \
                | CR4_LA57_MASK \
                | CR4_FSGSBASE_MASK | CR4_PCIDE_MASK | CR4_OSXSAVE_MASK \
                | CR4_SMEP_MASK | CR4_SMAP_MASK | CR4_PKE_MASK | CR4_PKS_MASK))

#define DR6_BD          (1 << 13)
#define DR6_BS          (1 << 14)
#define DR6_BT          (1 << 15)
#define DR6_FIXED_1     0xffff0ff0

#define DR7_GD          (1 << 13)
#define DR7_TYPE_SHIFT  16
#define DR7_LEN_SHIFT   18
#define DR7_FIXED_1     0x00000400
#define DR7_GLOBAL_BP_MASK   0xaa
#define DR7_LOCAL_BP_MASK    0x55
#define DR7_MAX_BP           4
#define DR7_TYPE_BP_INST     0x0
#define DR7_TYPE_DATA_WR     0x1
#define DR7_TYPE_IO_RW       0x2
#define DR7_TYPE_DATA_RW     0x3

#define DR_RESERVED_MASK 0xffffffff00000000ULL

#define PG_PRESENT_BIT  0
#define PG_RW_BIT       1
#define PG_USER_BIT     2
#define PG_PWT_BIT      3
#define PG_PCD_BIT      4
#define PG_ACCESSED_BIT 5
#define PG_DIRTY_BIT    6
#define PG_PSE_BIT      7
#define PG_GLOBAL_BIT   8
#define PG_PSE_PAT_BIT  12
#define PG_PKRU_BIT     59
#define PG_NX_BIT       63

#define PG_PRESENT_MASK  (1 << PG_PRESENT_BIT)
#define PG_RW_MASK       (1 << PG_RW_BIT)
#define PG_USER_MASK     (1 << PG_USER_BIT)
#define PG_PWT_MASK      (1 << PG_PWT_BIT)
#define PG_PCD_MASK      (1 << PG_PCD_BIT)
#define PG_ACCESSED_MASK (1 << PG_ACCESSED_BIT)
#define PG_DIRTY_MASK    (1 << PG_DIRTY_BIT)
#define PG_PSE_MASK      (1 << PG_PSE_BIT)
#define PG_GLOBAL_MASK   (1 << PG_GLOBAL_BIT)
#define PG_PSE_PAT_MASK  (1 << PG_PSE_PAT_BIT)
#define PG_ADDRESS_MASK  0x000ffffffffff000LL
#define PG_HI_USER_MASK  0x7ff0000000000000LL
#define PG_PKRU_MASK     (15ULL << PG_PKRU_BIT)
#define PG_NX_MASK       (1ULL << PG_NX_BIT)

#define PG_ERROR_W_BIT     1

#define PG_ERROR_P_MASK    0x01
#define PG_ERROR_W_MASK    (1 << PG_ERROR_W_BIT)
#define PG_ERROR_U_MASK    0x04
#define PG_ERROR_RSVD_MASK 0x08
#define PG_ERROR_I_D_MASK  0x10
#define PG_ERROR_PK_MASK   0x20

#define PG_MODE_PAE      (1 << 0)
#define PG_MODE_LMA      (1 << 1)
#define PG_MODE_NXE      (1 << 2)
#define PG_MODE_PSE      (1 << 3)
#define PG_MODE_LA57     (1 << 4)
#define PG_MODE_SVM_MASK MAKE_64BIT_MASK(0, 15)

/* Bits of CR4 that do not affect the NPT page format.  */
#define PG_MODE_WP       (1 << 16)
#define PG_MODE_PKE      (1 << 17)
#define PG_MODE_PKS      (1 << 18)
#define PG_MODE_SMEP     (1 << 19)

#define MCG_CTL_P       (1ULL<<8)   /* MCG_CAP register available */
#define MCG_SER_P       (1ULL<<24) /* MCA recovery/new status bits */
#define MCG_LMCE_P      (1ULL<<27) /* Local Machine Check Supported */

#define MCE_CAP_DEF     (MCG_CTL_P|MCG_SER_P)
#define MCE_BANKS_DEF   10

#define MCG_CAP_BANKS_MASK 0xff

#define MCG_STATUS_RIPV (1ULL<<0)   /* restart ip valid */
#define MCG_STATUS_EIPV (1ULL<<1)   /* ip points to correct instruction */
#define MCG_STATUS_MCIP (1ULL<<2)   /* machine check in progress */
#define MCG_STATUS_LMCE (1ULL<<3)   /* Local MCE signaled */

#define MCG_EXT_CTL_LMCE_EN (1ULL<<0) /* Local MCE enabled */

#define MCI_STATUS_VAL   (1ULL<<63)  /* valid error */
#define MCI_STATUS_OVER  (1ULL<<62)  /* previous errors lost */
#define MCI_STATUS_UC    (1ULL<<61)  /* uncorrected error */
#define MCI_STATUS_EN    (1ULL<<60)  /* error enabled */
#define MCI_STATUS_MISCV (1ULL<<59)  /* misc error reg. valid */
#define MCI_STATUS_ADDRV (1ULL<<58)  /* addr reg. valid */
#define MCI_STATUS_PCC   (1ULL<<57)  /* processor context corrupt */
#define MCI_STATUS_S     (1ULL<<56)  /* Signaled machine check */
#define MCI_STATUS_AR    (1ULL<<55)  /* Action required */

/* MISC register defines */
#define MCM_ADDR_SEGOFF  0      /* segment offset */
#define MCM_ADDR_LINEAR  1      /* linear address */
#define MCM_ADDR_PHYS    2      /* physical address */
#define MCM_ADDR_MEM     3      /* memory address */
#define MCM_ADDR_GENERIC 7      /* generic */

#define MSR_IA32_TSC                    0x10
#define MSR_IA32_APICBASE               0x1b
#define MSR_IA32_APICBASE_BSP           (1<<8)
#define MSR_IA32_APICBASE_ENABLE        (1<<11)
#define MSR_IA32_APICBASE_EXTD          (1 << 10)
#define MSR_IA32_APICBASE_BASE          (0xfffffU<<12)
#define MSR_IA32_FEATURE_CONTROL        0x0000003a
#define MSR_TSC_ADJUST                  0x0000003b
#define MSR_IA32_SPEC_CTRL              0x48
#define MSR_VIRT_SSBD                   0xc001011f
#define MSR_IA32_PRED_CMD               0x49
#define MSR_IA32_UCODE_REV              0x8b
#define MSR_IA32_CORE_CAPABILITY        0xcf

#define MSR_IA32_ARCH_CAPABILITIES      0x10a
#define ARCH_CAP_TSX_CTRL_MSR		(1<<7)

#define MSR_IA32_PERF_CAPABILITIES      0x345

#define MSR_IA32_TSX_CTRL		0x122
#define MSR_IA32_TSCDEADLINE            0x6e0
#define MSR_IA32_PKRS                   0x6e1

#define FEATURE_CONTROL_LOCKED                    (1<<0)
#define FEATURE_CONTROL_VMXON_ENABLED_INSIDE_SMX  (1ULL << 1)
#define FEATURE_CONTROL_VMXON_ENABLED_OUTSIDE_SMX (1<<2)
#define FEATURE_CONTROL_SGX_LC                    (1ULL << 17)
#define FEATURE_CONTROL_SGX                       (1ULL << 18)
#define FEATURE_CONTROL_LMCE                      (1<<20)

#define MSR_IA32_SGXLEPUBKEYHASH0       0x8c
#define MSR_IA32_SGXLEPUBKEYHASH1       0x8d
#define MSR_IA32_SGXLEPUBKEYHASH2       0x8e
#define MSR_IA32_SGXLEPUBKEYHASH3       0x8f

#define MSR_P6_PERFCTR0                 0xc1

#define MSR_IA32_SMBASE                 0x9e
#define MSR_SMI_COUNT                   0x34
#define MSR_CORE_THREAD_COUNT           0x35
#define MSR_MTRRcap                     0xfe
#define MSR_MTRRcap_VCNT                8
#define MSR_MTRRcap_FIXRANGE_SUPPORT    (1 << 8)
#define MSR_MTRRcap_WC_SUPPORTED        (1 << 10)

#define MSR_IA32_SYSENTER_CS            0x174
#define MSR_IA32_SYSENTER_ESP           0x175
#define MSR_IA32_SYSENTER_EIP           0x176

#define MSR_MCG_CAP                     0x179
#define MSR_MCG_STATUS                  0x17a
#define MSR_MCG_CTL                     0x17b
#define MSR_MCG_EXT_CTL                 0x4d0

#define MSR_P6_EVNTSEL0                 0x186

#define MSR_IA32_PERF_STATUS            0x198

#define MSR_IA32_MISC_ENABLE            0x1a0
/* Indicates good rep/movs microcode on some processors: */
#define MSR_IA32_MISC_ENABLE_DEFAULT    1
#define MSR_IA32_MISC_ENABLE_MWAIT      (1ULL << 18)

#define MSR_MTRRphysBase(reg)           (0x200 + 2 * (reg))
#define MSR_MTRRphysMask(reg)           (0x200 + 2 * (reg) + 1)

#define MSR_MTRRphysIndex(addr)         ((((addr) & ~1u) - 0x200) / 2)

#define MSR_MTRRfix64K_00000            0x250
#define MSR_MTRRfix16K_80000            0x258
#define MSR_MTRRfix16K_A0000            0x259
#define MSR_MTRRfix4K_C0000             0x268
#define MSR_MTRRfix4K_C8000             0x269
#define MSR_MTRRfix4K_D0000             0x26a
#define MSR_MTRRfix4K_D8000             0x26b
#define MSR_MTRRfix4K_E0000             0x26c
#define MSR_MTRRfix4K_E8000             0x26d
#define MSR_MTRRfix4K_F0000             0x26e
#define MSR_MTRRfix4K_F8000             0x26f

#define MSR_PAT                         0x277

#define MSR_MTRRdefType                 0x2ff

#define MSR_CORE_PERF_FIXED_CTR0        0x309
#define MSR_CORE_PERF_FIXED_CTR1        0x30a
#define MSR_CORE_PERF_FIXED_CTR2        0x30b
#define MSR_CORE_PERF_FIXED_CTR_CTRL    0x38d
#define MSR_CORE_PERF_GLOBAL_STATUS     0x38e
#define MSR_CORE_PERF_GLOBAL_CTRL       0x38f
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL   0x390

#define MSR_MC0_CTL                     0x400
#define MSR_MC0_STATUS                  0x401
#define MSR_MC0_ADDR                    0x402
#define MSR_MC0_MISC                    0x403

#define MSR_IA32_RTIT_OUTPUT_BASE       0x560
#define MSR_IA32_RTIT_OUTPUT_MASK       0x561
#define MSR_IA32_RTIT_CTL               0x570
#define MSR_IA32_RTIT_STATUS            0x571
#define MSR_IA32_RTIT_CR3_MATCH         0x572
#define MSR_IA32_RTIT_ADDR0_A           0x580
#define MSR_IA32_RTIT_ADDR0_B           0x581
#define MSR_IA32_RTIT_ADDR1_A           0x582
#define MSR_IA32_RTIT_ADDR1_B           0x583
#define MSR_IA32_RTIT_ADDR2_A           0x584
#define MSR_IA32_RTIT_ADDR2_B           0x585
#define MSR_IA32_RTIT_ADDR3_A           0x586
#define MSR_IA32_RTIT_ADDR3_B           0x587
#define MAX_RTIT_ADDRS                  8

#define MSR_EFER                        0xc0000080

#define MSR_EFER_SCE   (1 << 0)
#define MSR_EFER_LME   (1 << 8)
#define MSR_EFER_LMA   (1 << 10)
#define MSR_EFER_NXE   (1 << 11)
#define MSR_EFER_SVME  (1 << 12)
#define MSR_EFER_FFXSR (1 << 14)

#define MSR_EFER_RESERVED\
        (~(target_ulong)(MSR_EFER_SCE | MSR_EFER_LME\
            | MSR_EFER_LMA | MSR_EFER_NXE | MSR_EFER_SVME\
            | MSR_EFER_FFXSR))

#define MSR_STAR                        0xc0000081
#define MSR_LSTAR                       0xc0000082
#define MSR_CSTAR                       0xc0000083
#define MSR_FMASK                       0xc0000084
#define MSR_FSBASE                      0xc0000100
#define MSR_GSBASE                      0xc0000101
#define MSR_KERNELGSBASE                0xc0000102
#define MSR_TSC_AUX                     0xc0000103

#define MSR_VM_HSAVE_PA                 0xc0010117

#define MSR_IA32_BNDCFGS                0x00000d90
#define MSR_IA32_XSS                    0x00000da0
#define MSR_IA32_UMWAIT_CONTROL         0xe1

#define MSR_IA32_VMX_BASIC              0x00000480
#define MSR_IA32_VMX_PINBASED_CTLS      0x00000481
#define MSR_IA32_VMX_PROCBASED_CTLS     0x00000482
#define MSR_IA32_VMX_EXIT_CTLS          0x00000483
#define MSR_IA32_VMX_ENTRY_CTLS         0x00000484
#define MSR_IA32_VMX_MISC               0x00000485
#define MSR_IA32_VMX_CR0_FIXED0         0x00000486
#define MSR_IA32_VMX_CR0_FIXED1         0x00000487
#define MSR_IA32_VMX_CR4_FIXED0         0x00000488
#define MSR_IA32_VMX_CR4_FIXED1         0x00000489
#define MSR_IA32_VMX_VMCS_ENUM          0x0000048a
#define MSR_IA32_VMX_PROCBASED_CTLS2    0x0000048b
#define MSR_IA32_VMX_EPT_VPID_CAP       0x0000048c
#define MSR_IA32_VMX_TRUE_PINBASED_CTLS  0x0000048d
#define MSR_IA32_VMX_TRUE_PROCBASED_CTLS 0x0000048e
#define MSR_IA32_VMX_TRUE_EXIT_CTLS      0x0000048f
#define MSR_IA32_VMX_TRUE_ENTRY_CTLS     0x00000490
#define MSR_IA32_VMX_VMFUNC             0x00000491

#define XSTATE_FP_BIT                   0
#define XSTATE_SSE_BIT                  1
#define XSTATE_YMM_BIT                  2
#define XSTATE_BNDREGS_BIT              3
#define XSTATE_BNDCSR_BIT               4
#define XSTATE_OPMASK_BIT               5
#define XSTATE_ZMM_Hi256_BIT            6
#define XSTATE_Hi16_ZMM_BIT             7
#define XSTATE_PKRU_BIT                 9

#define XSTATE_FP_MASK                  (1ULL << XSTATE_FP_BIT)
#define XSTATE_SSE_MASK                 (1ULL << XSTATE_SSE_BIT)
#define XSTATE_YMM_MASK                 (1ULL << XSTATE_YMM_BIT)
#define XSTATE_BNDREGS_MASK             (1ULL << XSTATE_BNDREGS_BIT)
#define XSTATE_BNDCSR_MASK              (1ULL << XSTATE_BNDCSR_BIT)
#define XSTATE_OPMASK_MASK              (1ULL << XSTATE_OPMASK_BIT)
#define XSTATE_ZMM_Hi256_MASK           (1ULL << XSTATE_ZMM_Hi256_BIT)
#define XSTATE_Hi16_ZMM_MASK            (1ULL << XSTATE_Hi16_ZMM_BIT)
#define XSTATE_PKRU_MASK                (1ULL << XSTATE_PKRU_BIT)

/* CPUID feature words */
typedef enum FeatureWord {
    FEAT_1_EDX,         /* CPUID[1].EDX */
    FEAT_1_ECX,         /* CPUID[1].ECX */
    FEAT_7_0_EBX,       /* CPUID[EAX=7,ECX=0].EBX */
    FEAT_7_0_ECX,       /* CPUID[EAX=7,ECX=0].ECX */
    FEAT_7_0_EDX,       /* CPUID[EAX=7,ECX=0].EDX */
    FEAT_7_1_EAX,       /* CPUID[EAX=7,ECX=1].EAX */
    FEAT_8000_0001_EDX, /* CPUID[8000_0001].EDX */
    FEAT_8000_0001_ECX, /* CPUID[8000_0001].ECX */
    FEAT_8000_0007_EDX, /* CPUID[8000_0007].EDX */
    FEAT_8000_0008_EBX, /* CPUID[8000_0008].EBX */
    FEAT_C000_0001_EDX, /* CPUID[C000_0001].EDX */
    FEAT_KVM,           /* CPUID[4000_0001].EAX (KVM_CPUID_FEATURES) */
    FEAT_KVM_HINTS,     /* CPUID[4000_0001].EDX */
    FEAT_SVM,           /* CPUID[8000_000A].EDX */
    FEAT_XSAVE,         /* CPUID[EAX=0xd,ECX=1].EAX */
    FEAT_6_EAX,         /* CPUID[6].EAX */
    FEAT_XSAVE_COMP_LO, /* CPUID[EAX=0xd,ECX=0].EAX */
    FEAT_XSAVE_COMP_HI, /* CPUID[EAX=0xd,ECX=0].EDX */
    FEAT_ARCH_CAPABILITIES,
    FEAT_CORE_CAPABILITY,
    FEAT_PERF_CAPABILITIES,
    FEAT_VMX_PROCBASED_CTLS,
    FEAT_VMX_SECONDARY_CTLS,
    FEAT_VMX_PINBASED_CTLS,
    FEAT_VMX_EXIT_CTLS,
    FEAT_VMX_ENTRY_CTLS,
    FEAT_VMX_MISC,
    FEAT_VMX_EPT_VPID_CAPS,
    FEAT_VMX_BASIC,
    FEAT_VMX_VMFUNC,
    FEAT_14_0_ECX,
    FEAT_SGX_12_0_EAX,  /* CPUID[EAX=0x12,ECX=0].EAX (SGX) */
    FEAT_SGX_12_0_EBX,  /* CPUID[EAX=0x12,ECX=0].EBX (SGX MISCSELECT[31:0]) */
    FEAT_SGX_12_1_EAX,  /* CPUID[EAX=0x12,ECX=1].EAX (SGX ATTRIBUTES[31:0]) */
    FEATURE_WORDS,
} FeatureWord;

typedef uint64_t FeatureWordArray[FEATURE_WORDS];

/* cpuid_features bits */
#define CPUID_FP87 (1U << 0)
#define CPUID_VME  (1U << 1)
#define CPUID_DE   (1U << 2)
#define CPUID_PSE  (1U << 3)
#define CPUID_TSC  (1U << 4)
#define CPUID_MSR  (1U << 5)
#define CPUID_PAE  (1U << 6)
#define CPUID_MCE  (1U << 7)
#define CPUID_CX8  (1U << 8)
#define CPUID_APIC (1U << 9)
#define CPUID_SEP  (1U << 11) /* sysenter/sysexit */
#define CPUID_MTRR (1U << 12)
#define CPUID_PGE  (1U << 13)
#define CPUID_MCA  (1U << 14)
#define CPUID_CMOV (1U << 15)
#define CPUID_PAT  (1U << 16)
#define CPUID_PSE36   (1U << 17)
#define CPUID_PN   (1U << 18)
#define CPUID_CLFLUSH (1U << 19)
#define CPUID_DTS (1U << 21)
#define CPUID_ACPI (1U << 22)
#define CPUID_MMX  (1U << 23)
#define CPUID_FXSR (1U << 24)
#define CPUID_SSE  (1U << 25)
#define CPUID_SSE2 (1U << 26)
#define CPUID_SS (1U << 27)
#define CPUID_HT (1U << 28)
#define CPUID_TM (1U << 29)
#define CPUID_IA64 (1U << 30)
#define CPUID_PBE (1U << 31)

#define CPUID_EXT_SSE3     (1U << 0)
#define CPUID_EXT_PCLMULQDQ (1U << 1)
#define CPUID_EXT_DTES64   (1U << 2)
#define CPUID_EXT_MONITOR  (1U << 3)
#define CPUID_EXT_DSCPL    (1U << 4)
#define CPUID_EXT_VMX      (1U << 5)
#define CPUID_EXT_SMX      (1U << 6)
#define CPUID_EXT_EST      (1U << 7)
#define CPUID_EXT_TM2      (1U << 8)
#define CPUID_EXT_SSSE3    (1U << 9)
#define CPUID_EXT_CID      (1U << 10)
#define CPUID_EXT_FMA      (1U << 12)
#define CPUID_EXT_CX16     (1U << 13)
#define CPUID_EXT_XTPR     (1U << 14)
#define CPUID_EXT_PDCM     (1U << 15)
#define CPUID_EXT_PCID     (1U << 17)
#define CPUID_EXT_DCA      (1U << 18)
#define CPUID_EXT_SSE41    (1U << 19)
#define CPUID_EXT_SSE42    (1U << 20)
#define CPUID_EXT_X2APIC   (1U << 21)
#define CPUID_EXT_MOVBE    (1U << 22)
#define CPUID_EXT_POPCNT   (1U << 23)
#define CPUID_EXT_TSC_DEADLINE_TIMER (1U << 24)
#define CPUID_EXT_AES      (1U << 25)
#define CPUID_EXT_XSAVE    (1U << 26)
#define CPUID_EXT_OSXSAVE  (1U << 27)
#define CPUID_EXT_AVX      (1U << 28)
#define CPUID_EXT_F16C     (1U << 29)
#define CPUID_EXT_RDRAND   (1U << 30)
#define CPUID_EXT_HYPERVISOR  (1U << 31)

#define CPUID_EXT2_FPU     (1U << 0)
#define CPUID_EXT2_VME     (1U << 1)
#define CPUID_EXT2_DE      (1U << 2)
#define CPUID_EXT2_PSE     (1U << 3)
#define CPUID_EXT2_TSC     (1U << 4)
#define CPUID_EXT2_MSR     (1U << 5)
#define CPUID_EXT2_PAE     (1U << 6)
#define CPUID_EXT2_MCE     (1U << 7)
#define CPUID_EXT2_CX8     (1U << 8)
#define CPUID_EXT2_APIC    (1U << 9)
#define CPUID_EXT2_SYSCALL (1U << 11)
#define CPUID_EXT2_MTRR    (1U << 12)
#define CPUID_EXT2_PGE     (1U << 13)
#define CPUID_EXT2_MCA     (1U << 14)
#define CPUID_EXT2_CMOV    (1U << 15)
#define CPUID_EXT2_PAT     (1U << 16)
#define CPUID_EXT2_PSE36   (1U << 17)
#define CPUID_EXT2_MP      (1U << 19)
#define CPUID_EXT2_NX      (1U << 20)
#define CPUID_EXT2_MMXEXT  (1U << 22)
#define CPUID_EXT2_MMX     (1U << 23)
#define CPUID_EXT2_FXSR    (1U << 24)
#define CPUID_EXT2_FFXSR   (1U << 25)
#define CPUID_EXT2_PDPE1GB (1U << 26)
#define CPUID_EXT2_RDTSCP  (1U << 27)
#define CPUID_EXT2_LM      (1U << 29)
#define CPUID_EXT2_3DNOWEXT (1U << 30)
#define CPUID_EXT2_3DNOW   (1U << 31)

/* CPUID[8000_0001].EDX bits that are aliase of CPUID[1].EDX bits on AMD CPUs */
#define CPUID_EXT2_AMD_ALIASES (CPUID_EXT2_FPU | CPUID_EXT2_VME | \
                                CPUID_EXT2_DE | CPUID_EXT2_PSE | \
                                CPUID_EXT2_TSC | CPUID_EXT2_MSR | \
                                CPUID_EXT2_PAE | CPUID_EXT2_MCE | \
                                CPUID_EXT2_CX8 | CPUID_EXT2_APIC | \
                                CPUID_EXT2_MTRR | CPUID_EXT2_PGE | \
                                CPUID_EXT2_MCA | CPUID_EXT2_CMOV | \
                                CPUID_EXT2_PAT | CPUID_EXT2_PSE36 | \
                                CPUID_EXT2_MMX | CPUID_EXT2_FXSR)

#define CPUID_EXT3_LAHF_LM (1U << 0)
#define CPUID_EXT3_CMP_LEG (1U << 1)
#define CPUID_EXT3_SVM     (1U << 2)
#define CPUID_EXT3_EXTAPIC (1U << 3)
#define CPUID_EXT3_CR8LEG  (1U << 4)
#define CPUID_EXT3_ABM     (1U << 5)
#define CPUID_EXT3_SSE4A   (1U << 6)
#define CPUID_EXT3_MISALIGNSSE (1U << 7)
#define CPUID_EXT3_3DNOWPREFETCH (1U << 8)
#define CPUID_EXT3_OSVW    (1U << 9)
#define CPUID_EXT3_IBS     (1U << 10)
#define CPUID_EXT3_XOP     (1U << 11)
#define CPUID_EXT3_SKINIT  (1U << 12)
#define CPUID_EXT3_WDT     (1U << 13)
#define CPUID_EXT3_LWP     (1U << 15)
#define CPUID_EXT3_FMA4    (1U << 16)
#define CPUID_EXT3_TCE     (1U << 17)
#define CPUID_EXT3_NODEID  (1U << 19)
#define CPUID_EXT3_TBM     (1U << 21)
#define CPUID_EXT3_TOPOEXT (1U << 22)
#define CPUID_EXT3_PERFCORE (1U << 23)
#define CPUID_EXT3_PERFNB  (1U << 24)

#define CPUID_SVM_NPT             (1U << 0)
#define CPUID_SVM_LBRV            (1U << 1)
#define CPUID_SVM_SVMLOCK         (1U << 2)
#define CPUID_SVM_NRIPSAVE        (1U << 3)
#define CPUID_SVM_TSCSCALE        (1U << 4)
#define CPUID_SVM_VMCBCLEAN       (1U << 5)
#define CPUID_SVM_FLUSHASID       (1U << 6)
#define CPUID_SVM_DECODEASSIST    (1U << 7)
#define CPUID_SVM_PAUSEFILTER     (1U << 10)
#define CPUID_SVM_PFTHRESHOLD     (1U << 12)
#define CPUID_SVM_AVIC            (1U << 13)
#define CPUID_SVM_V_VMSAVE_VMLOAD (1U << 15)
#define CPUID_SVM_VGIF            (1U << 16)
#define CPUID_SVM_SVME_ADDR_CHK   (1U << 28)

/* Support RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE */
#define CPUID_7_0_EBX_FSGSBASE          (1U << 0)
/* Support SGX */
#define CPUID_7_0_EBX_SGX               (1U << 2)
/* 1st Group of Advanced Bit Manipulation Extensions */
#define CPUID_7_0_EBX_BMI1              (1U << 3)
/* Hardware Lock Elision */
#define CPUID_7_0_EBX_HLE               (1U << 4)
/* Intel Advanced Vector Extensions 2 */
#define CPUID_7_0_EBX_AVX2              (1U << 5)
/* Supervisor-mode Execution Prevention */
#define CPUID_7_0_EBX_SMEP              (1U << 7)
/* 2nd Group of Advanced Bit Manipulation Extensions */
#define CPUID_7_0_EBX_BMI2              (1U << 8)
/* Enhanced REP MOVSB/STOSB */
#define CPUID_7_0_EBX_ERMS              (1U << 9)
/* Invalidate Process-Context Identifier */
#define CPUID_7_0_EBX_INVPCID           (1U << 10)
/* Restricted Transactional Memory */
#define CPUID_7_0_EBX_RTM               (1U << 11)
/* Memory Protection Extension */
#define CPUID_7_0_EBX_MPX               (1U << 14)
/* AVX-512 Foundation */
#define CPUID_7_0_EBX_AVX512F           (1U << 16)
/* AVX-512 Doubleword & Quadword Instruction */
#define CPUID_7_0_EBX_AVX512DQ          (1U << 17)
/* Read Random SEED */
#define CPUID_7_0_EBX_RDSEED            (1U << 18)
/* ADCX and ADOX instructions */
#define CPUID_7_0_EBX_ADX               (1U << 19)
/* Supervisor Mode Access Prevention */
#define CPUID_7_0_EBX_SMAP              (1U << 20)
/* AVX-512 Integer Fused Multiply Add */
#define CPUID_7_0_EBX_AVX512IFMA        (1U << 21)
/* Persistent Commit */
#define CPUID_7_0_EBX_PCOMMIT           (1U << 22)
/* Flush a Cache Line Optimized */
#define CPUID_7_0_EBX_CLFLUSHOPT        (1U << 23)
/* Cache Line Write Back */
#define CPUID_7_0_EBX_CLWB              (1U << 24)
/* Intel Processor Trace */
#define CPUID_7_0_EBX_INTEL_PT          (1U << 25)
/* AVX-512 Prefetch */
#define CPUID_7_0_EBX_AVX512PF          (1U << 26)
/* AVX-512 Exponential and Reciprocal */
#define CPUID_7_0_EBX_AVX512ER          (1U << 27)
/* AVX-512 Conflict Detection */
#define CPUID_7_0_EBX_AVX512CD          (1U << 28)
/* SHA1/SHA256 Instruction Extensions */
#define CPUID_7_0_EBX_SHA_NI            (1U << 29)
/* AVX-512 Byte and Word Instructions */
#define CPUID_7_0_EBX_AVX512BW          (1U << 30)
/* AVX-512 Vector Length Extensions */
#define CPUID_7_0_EBX_AVX512VL          (1U << 31)

/* AVX-512 Vector Byte Manipulation Instruction */
#define CPUID_7_0_ECX_AVX512_VBMI       (1U << 1)
/* User-Mode Instruction Prevention */
#define CPUID_7_0_ECX_UMIP              (1U << 2)
/* Protection Keys for User-mode Pages */
#define CPUID_7_0_ECX_PKU               (1U << 3)
/* OS Enable Protection Keys */
#define CPUID_7_0_ECX_OSPKE             (1U << 4)
/* UMONITOR/UMWAIT/TPAUSE Instructions */
#define CPUID_7_0_ECX_WAITPKG           (1U << 5)
/* Additional AVX-512 Vector Byte Manipulation Instruction */
#define CPUID_7_0_ECX_AVX512_VBMI2      (1U << 6)
/* Galois Field New Instructions */
#define CPUID_7_0_ECX_GFNI              (1U << 8)
/* Vector AES Instructions */
#define CPUID_7_0_ECX_VAES              (1U << 9)
/* Carry-Less Multiplication Quadword */
#define CPUID_7_0_ECX_VPCLMULQDQ        (1U << 10)
/* Vector Neural Network Instructions */
#define CPUID_7_0_ECX_AVX512VNNI        (1U << 11)
/* Support for VPOPCNT[B,W] and VPSHUFBITQMB */
#define CPUID_7_0_ECX_AVX512BITALG      (1U << 12)
/* POPCNT for vectors of DW/QW */
#define CPUID_7_0_ECX_AVX512_VPOPCNTDQ  (1U << 14)
/* 5-level Page Tables */
#define CPUID_7_0_ECX_LA57              (1U << 16)
/* Read Processor ID */
#define CPUID_7_0_ECX_RDPID             (1U << 22)
/* Bus Lock Debug Exception */
#define CPUID_7_0_ECX_BUS_LOCK_DETECT   (1U << 24)
/* Cache Line Demote Instruction */
#define CPUID_7_0_ECX_CLDEMOTE          (1U << 25)
/* Move Doubleword as Direct Store Instruction */
#define CPUID_7_0_ECX_MOVDIRI           (1U << 27)
/* Move 64 Bytes as Direct Store Instruction */
#define CPUID_7_0_ECX_MOVDIR64B         (1U << 28)
/* Support SGX Launch Control */
#define CPUID_7_0_ECX_SGX_LC            (1U << 30)
/* Protection Keys for Supervisor-mode Pages */
#define CPUID_7_0_ECX_PKS               (1U << 31)

/* AVX512 Neural Network Instructions */
#define CPUID_7_0_EDX_AVX512_4VNNIW     (1U << 2)
/* AVX512 Multiply Accumulation Single Precision */
#define CPUID_7_0_EDX_AVX512_4FMAPS     (1U << 3)
/* Fast Short Rep Mov */
#define CPUID_7_0_EDX_FSRM              (1U << 4)
/* AVX512 Vector Pair Intersection to a Pair of Mask Registers */
#define CPUID_7_0_EDX_AVX512_VP2INTERSECT (1U << 8)
/* SERIALIZE instruction */
#define CPUID_7_0_EDX_SERIALIZE         (1U << 14)
/* TSX Suspend Load Address Tracking instruction */
#define CPUID_7_0_EDX_TSX_LDTRK         (1U << 16)
/* AVX512_FP16 instruction */
#define CPUID_7_0_EDX_AVX512_FP16       (1U << 23)
/* Speculation Control */
#define CPUID_7_0_EDX_SPEC_CTRL         (1U << 26)
/* Single Thread Indirect Branch Predictors */
#define CPUID_7_0_EDX_STIBP             (1U << 27)
/* Arch Capabilities */
#define CPUID_7_0_EDX_ARCH_CAPABILITIES (1U << 29)
/* Core Capability */
#define CPUID_7_0_EDX_CORE_CAPABILITY   (1U << 30)
/* Speculative Store Bypass Disable */
#define CPUID_7_0_EDX_SPEC_CTRL_SSBD    (1U << 31)

/* AVX VNNI Instruction */
#define CPUID_7_1_EAX_AVX_VNNI          (1U << 4)
/* AVX512 BFloat16 Instruction */
#define CPUID_7_1_EAX_AVX512_BF16       (1U << 5)

/* Packets which contain IP payload have LIP values */
#define CPUID_14_0_ECX_LIP              (1U << 31)

/* CLZERO instruction */
#define CPUID_8000_0008_EBX_CLZERO      (1U << 0)
/* Always save/restore FP error pointers */
#define CPUID_8000_0008_EBX_XSAVEERPTR  (1U << 2)
/* Write back and do not invalidate cache */
#define CPUID_8000_0008_EBX_WBNOINVD    (1U << 9)
/* Indirect Branch Prediction Barrier */
#define CPUID_8000_0008_EBX_IBPB        (1U << 12)
/* Indirect Branch Restricted Speculation */
#define CPUID_8000_0008_EBX_IBRS        (1U << 14)
/* Single Thread Indirect Branch Predictors */
#define CPUID_8000_0008_EBX_STIBP       (1U << 15)
/* Speculative Store Bypass Disable */
#define CPUID_8000_0008_EBX_AMD_SSBD    (1U << 24)

#define CPUID_XSAVE_XSAVEOPT   (1U << 0)
#define CPUID_XSAVE_XSAVEC     (1U << 1)
#define CPUID_XSAVE_XGETBV1    (1U << 2)
#define CPUID_XSAVE_XSAVES     (1U << 3)

#define CPUID_6_EAX_ARAT       (1U << 2)

/* CPUID[0x80000007].EDX flags: */
#define CPUID_APM_INVTSC       (1U << 8)

#define CPUID_VENDOR_SZ      12

#define CPUID_VENDOR_INTEL_1 0x756e6547 /* "Genu" */
#define CPUID_VENDOR_INTEL_2 0x49656e69 /* "ineI" */
#define CPUID_VENDOR_INTEL_3 0x6c65746e /* "ntel" */
#define CPUID_VENDOR_INTEL "GenuineIntel"

#define CPUID_VENDOR_AMD_1   0x68747541 /* "Auth" */
#define CPUID_VENDOR_AMD_2   0x69746e65 /* "enti" */
#define CPUID_VENDOR_AMD_3   0x444d4163 /* "cAMD" */
#define CPUID_VENDOR_AMD   "AuthenticAMD"

#define CPUID_VENDOR_VIA   "CentaurHauls"

#define CPUID_VENDOR_HYGON    "HygonGenuine"

#define IS_INTEL_CPU(env) ((env)->cpuid_vendor1 == CPUID_VENDOR_INTEL_1 && \
                           (env)->cpuid_vendor2 == CPUID_VENDOR_INTEL_2 && \
                           (env)->cpuid_vendor3 == CPUID_VENDOR_INTEL_3)
#define IS_AMD_CPU(env) ((env)->cpuid_vendor1 == CPUID_VENDOR_AMD_1 && \
                         (env)->cpuid_vendor2 == CPUID_VENDOR_AMD_2 && \
                         (env)->cpuid_vendor3 == CPUID_VENDOR_AMD_3)

#define CPUID_MWAIT_IBE     (1U << 1) /* Interrupts can exit capability */
#define CPUID_MWAIT_EMX     (1U << 0) /* enumeration supported */

/* CPUID[0xB].ECX level types */
#define CPUID_TOPOLOGY_LEVEL_INVALID  (0U << 8)
#define CPUID_TOPOLOGY_LEVEL_SMT      (1U << 8)
#define CPUID_TOPOLOGY_LEVEL_CORE     (2U << 8)
#define CPUID_TOPOLOGY_LEVEL_DIE      (5U << 8)

/* MSR Feature Bits */
#define MSR_ARCH_CAP_RDCL_NO            (1U << 0)
#define MSR_ARCH_CAP_IBRS_ALL           (1U << 1)
#define MSR_ARCH_CAP_RSBA               (1U << 2)
#define MSR_ARCH_CAP_SKIP_L1DFL_VMENTRY (1U << 3)
#define MSR_ARCH_CAP_SSB_NO             (1U << 4)
#define MSR_ARCH_CAP_MDS_NO             (1U << 5)
#define MSR_ARCH_CAP_PSCHANGE_MC_NO     (1U << 6)
#define MSR_ARCH_CAP_TSX_CTRL_MSR       (1U << 7)
#define MSR_ARCH_CAP_TAA_NO             (1U << 8)

#define MSR_CORE_CAP_SPLIT_LOCK_DETECT  (1U << 5)

/* VMX MSR features */
#define MSR_VMX_BASIC_VMCS_REVISION_MASK             0x7FFFFFFFull
#define MSR_VMX_BASIC_VMXON_REGION_SIZE_MASK         (0x00001FFFull << 32)
#define MSR_VMX_BASIC_VMCS_MEM_TYPE_MASK             (0x003C0000ull << 32)
#define MSR_VMX_BASIC_DUAL_MONITOR                   (1ULL << 49)
#define MSR_VMX_BASIC_INS_OUTS                       (1ULL << 54)
#define MSR_VMX_BASIC_TRUE_CTLS                      (1ULL << 55)

#define MSR_VMX_MISC_PREEMPTION_TIMER_SHIFT_MASK     0x1Full
#define MSR_VMX_MISC_STORE_LMA                       (1ULL << 5)
#define MSR_VMX_MISC_ACTIVITY_HLT                    (1ULL << 6)
#define MSR_VMX_MISC_ACTIVITY_SHUTDOWN               (1ULL << 7)
#define MSR_VMX_MISC_ACTIVITY_WAIT_SIPI              (1ULL << 8)
#define MSR_VMX_MISC_MAX_MSR_LIST_SIZE_MASK          0x0E000000ull
#define MSR_VMX_MISC_VMWRITE_VMEXIT                  (1ULL << 29)
#define MSR_VMX_MISC_ZERO_LEN_INJECT                 (1ULL << 30)

#define MSR_VMX_EPT_EXECONLY                         (1ULL << 0)
#define MSR_VMX_EPT_PAGE_WALK_LENGTH_4               (1ULL << 6)
#define MSR_VMX_EPT_PAGE_WALK_LENGTH_5               (1ULL << 7)
#define MSR_VMX_EPT_UC                               (1ULL << 8)
#define MSR_VMX_EPT_WB                               (1ULL << 14)
#define MSR_VMX_EPT_2MB                              (1ULL << 16)
#define MSR_VMX_EPT_1GB                              (1ULL << 17)
#define MSR_VMX_EPT_INVEPT                           (1ULL << 20)
#define MSR_VMX_EPT_AD_BITS                          (1ULL << 21)
#define MSR_VMX_EPT_ADVANCED_VMEXIT_INFO             (1ULL << 22)
#define MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT            (1ULL << 25)
#define MSR_VMX_EPT_INVEPT_ALL_CONTEXT               (1ULL << 26)
#define MSR_VMX_EPT_INVVPID                          (1ULL << 32)
#define MSR_VMX_EPT_INVVPID_SINGLE_ADDR              (1ULL << 40)
#define MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT           (1ULL << 41)
#define MSR_VMX_EPT_INVVPID_ALL_CONTEXT              (1ULL << 42)
#define MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS (1ULL << 43)

#define MSR_VMX_VMFUNC_EPT_SWITCHING                 (1ULL << 0)


/* VMX controls */
#define VMX_CPU_BASED_VIRTUAL_INTR_PENDING          0x00000004
#define VMX_CPU_BASED_USE_TSC_OFFSETING             0x00000008
#define VMX_CPU_BASED_HLT_EXITING                   0x00000080
#define VMX_CPU_BASED_INVLPG_EXITING                0x00000200
#define VMX_CPU_BASED_MWAIT_EXITING                 0x00000400
#define VMX_CPU_BASED_RDPMC_EXITING                 0x00000800
#define VMX_CPU_BASED_RDTSC_EXITING                 0x00001000
#define VMX_CPU_BASED_CR3_LOAD_EXITING              0x00008000
#define VMX_CPU_BASED_CR3_STORE_EXITING             0x00010000
#define VMX_CPU_BASED_CR8_LOAD_EXITING              0x00080000
#define VMX_CPU_BASED_CR8_STORE_EXITING             0x00100000
#define VMX_CPU_BASED_TPR_SHADOW                    0x00200000
#define VMX_CPU_BASED_VIRTUAL_NMI_PENDING           0x00400000
#define VMX_CPU_BASED_MOV_DR_EXITING                0x00800000
#define VMX_CPU_BASED_UNCOND_IO_EXITING             0x01000000
#define VMX_CPU_BASED_USE_IO_BITMAPS                0x02000000
#define VMX_CPU_BASED_MONITOR_TRAP_FLAG             0x08000000
#define VMX_CPU_BASED_USE_MSR_BITMAPS               0x10000000
#define VMX_CPU_BASED_MONITOR_EXITING               0x20000000
#define VMX_CPU_BASED_PAUSE_EXITING                 0x40000000
#define VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS   0x80000000

#define VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES 0x00000001
#define VMX_SECONDARY_EXEC_ENABLE_EPT               0x00000002
#define VMX_SECONDARY_EXEC_DESC                     0x00000004
#define VMX_SECONDARY_EXEC_RDTSCP                   0x00000008
#define VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE   0x00000010
#define VMX_SECONDARY_EXEC_ENABLE_VPID              0x00000020
#define VMX_SECONDARY_EXEC_WBINVD_EXITING           0x00000040
#define VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST       0x00000080
#define VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT       0x00000100
#define VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY    0x00000200
#define VMX_SECONDARY_EXEC_PAUSE_LOOP_EXITING       0x00000400
#define VMX_SECONDARY_EXEC_RDRAND_EXITING           0x00000800
#define VMX_SECONDARY_EXEC_ENABLE_INVPCID           0x00001000
#define VMX_SECONDARY_EXEC_ENABLE_VMFUNC            0x00002000
#define VMX_SECONDARY_EXEC_SHADOW_VMCS              0x00004000
#define VMX_SECONDARY_EXEC_ENCLS_EXITING            0x00008000
#define VMX_SECONDARY_EXEC_RDSEED_EXITING           0x00010000
#define VMX_SECONDARY_EXEC_ENABLE_PML               0x00020000
#define VMX_SECONDARY_EXEC_XSAVES                   0x00100000
#define VMX_SECONDARY_EXEC_TSC_SCALING              0x02000000

#define VMX_PIN_BASED_EXT_INTR_MASK                 0x00000001
#define VMX_PIN_BASED_NMI_EXITING                   0x00000008
#define VMX_PIN_BASED_VIRTUAL_NMIS                  0x00000020
#define VMX_PIN_BASED_VMX_PREEMPTION_TIMER          0x00000040
#define VMX_PIN_BASED_POSTED_INTR                   0x00000080

#define VMX_VM_EXIT_SAVE_DEBUG_CONTROLS             0x00000004
#define VMX_VM_EXIT_HOST_ADDR_SPACE_SIZE            0x00000200
#define VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL      0x00001000
#define VMX_VM_EXIT_ACK_INTR_ON_EXIT                0x00008000
#define VMX_VM_EXIT_SAVE_IA32_PAT                   0x00040000
#define VMX_VM_EXIT_LOAD_IA32_PAT                   0x00080000
#define VMX_VM_EXIT_SAVE_IA32_EFER                  0x00100000
#define VMX_VM_EXIT_LOAD_IA32_EFER                  0x00200000
#define VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER       0x00400000
#define VMX_VM_EXIT_CLEAR_BNDCFGS                   0x00800000
#define VMX_VM_EXIT_PT_CONCEAL_PIP                  0x01000000
#define VMX_VM_EXIT_CLEAR_IA32_RTIT_CTL             0x02000000
#define VMX_VM_EXIT_LOAD_IA32_PKRS                  0x20000000

#define VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS            0x00000004
#define VMX_VM_ENTRY_IA32E_MODE                     0x00000200
#define VMX_VM_ENTRY_SMM                            0x00000400
#define VMX_VM_ENTRY_DEACT_DUAL_MONITOR             0x00000800
#define VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL     0x00002000
#define VMX_VM_ENTRY_LOAD_IA32_PAT                  0x00004000
#define VMX_VM_ENTRY_LOAD_IA32_EFER                 0x00008000
#define VMX_VM_ENTRY_LOAD_BNDCFGS                   0x00010000
#define VMX_VM_ENTRY_PT_CONCEAL_PIP                 0x00020000
#define VMX_VM_ENTRY_LOAD_IA32_RTIT_CTL             0x00040000
#define VMX_VM_ENTRY_LOAD_IA32_PKRS                 0x00400000

/* Supported Hyper-V Enlightenments */
#define HYPERV_FEAT_RELAXED             0
#define HYPERV_FEAT_VAPIC               1
#define HYPERV_FEAT_TIME                2
#define HYPERV_FEAT_CRASH               3
#define HYPERV_FEAT_RESET               4
#define HYPERV_FEAT_VPINDEX             5
#define HYPERV_FEAT_RUNTIME             6
#define HYPERV_FEAT_SYNIC               7
#define HYPERV_FEAT_STIMER              8
#define HYPERV_FEAT_FREQUENCIES         9
#define HYPERV_FEAT_REENLIGHTENMENT     10
#define HYPERV_FEAT_TLBFLUSH            11
#define HYPERV_FEAT_EVMCS               12
#define HYPERV_FEAT_IPI                 13
#define HYPERV_FEAT_STIMER_DIRECT       14
#define HYPERV_FEAT_AVIC                15

#ifndef HYPERV_SPINLOCK_NEVER_NOTIFY
#define HYPERV_SPINLOCK_NEVER_NOTIFY             0xFFFFFFFF
#endif

#define EXCP00_DIVZ	0
#define EXCP01_DB	1
#define EXCP02_NMI	2
#define EXCP03_INT3	3
#define EXCP04_INTO	4
#define EXCP05_BOUND	5
#define EXCP06_ILLOP	6
#define EXCP07_PREX	7
#define EXCP08_DBLE	8
#define EXCP09_XERR	9
#define EXCP0A_TSS	10
#define EXCP0B_NOSEG	11
#define EXCP0C_STACK	12
#define EXCP0D_GPF	13
#define EXCP0E_PAGE	14
#define EXCP10_COPR	16
#define EXCP11_ALGN	17
#define EXCP12_MCHK	18

#define EXCP_VMEXIT     0x100 /* only for system emulation */
#define EXCP_SYSCALL    0x101 /* only for user emulation */
#define EXCP_VSYSCALL   0x102 /* only for user emulation */

/* i386-specific interrupt pending bits.  */
#define CPU_INTERRUPT_POLL      CPU_INTERRUPT_TGT_EXT_1
#define CPU_INTERRUPT_SMI       CPU_INTERRUPT_TGT_EXT_2
#define CPU_INTERRUPT_NMI       CPU_INTERRUPT_TGT_EXT_3
#define CPU_INTERRUPT_MCE       CPU_INTERRUPT_TGT_EXT_4
#define CPU_INTERRUPT_VIRQ      CPU_INTERRUPT_TGT_INT_0
#define CPU_INTERRUPT_SIPI      CPU_INTERRUPT_TGT_INT_1
#define CPU_INTERRUPT_TPR       CPU_INTERRUPT_TGT_INT_2

/* Use a clearer name for this.  */
#define CPU_INTERRUPT_INIT      CPU_INTERRUPT_RESET

/* Instead of computing the condition codes after each x86 instruction,
 * QEMU just stores one operand (called CC_SRC), the result
 * (called CC_DST) and the type of operation (called CC_OP). When the
 * condition codes are needed, the condition codes can be calculated
 * using this information. Condition codes are not generated if they
 * are only needed for conditional branches.
 */
typedef enum {
    CC_OP_DYNAMIC, /* must use dynamic code to get cc_op */
    CC_OP_EFLAGS,  /* all cc are explicitly computed, CC_SRC = flags */

    CC_OP_MULB, /* modify all flags, C, O = (CC_SRC != 0) */
    CC_OP_MULW,
    CC_OP_MULL,
    CC_OP_MULQ,

    CC_OP_ADDB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_ADDW,
    CC_OP_ADDL,
    CC_OP_ADDQ,

    CC_OP_ADCB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_ADCW,
    CC_OP_ADCL,
    CC_OP_ADCQ,

    CC_OP_SUBB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_SUBW,
    CC_OP_SUBL,
    CC_OP_SUBQ,

    CC_OP_SBBB, /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_SBBW,
    CC_OP_SBBL,
    CC_OP_SBBQ,

    CC_OP_LOGICB, /* modify all flags, CC_DST = res */
    CC_OP_LOGICW,
    CC_OP_LOGICL,
    CC_OP_LOGICQ,

    CC_OP_INCB, /* modify all flags except, CC_DST = res, CC_SRC = C */
    CC_OP_INCW,
    CC_OP_INCL,
    CC_OP_INCQ,

    CC_OP_DECB, /* modify all flags except, CC_DST = res, CC_SRC = C  */
    CC_OP_DECW,
    CC_OP_DECL,
    CC_OP_DECQ,

    CC_OP_SHLB, /* modify all flags, CC_DST = res, CC_SRC.msb = C */
    CC_OP_SHLW,
    CC_OP_SHLL,
    CC_OP_SHLQ,

    CC_OP_SARB, /* modify all flags, CC_DST = res, CC_SRC.lsb = C */
    CC_OP_SARW,
    CC_OP_SARL,
    CC_OP_SARQ,

    CC_OP_BMILGB, /* Z,S via CC_DST, C = SRC==0; O=0; P,A undefined */
    CC_OP_BMILGW,
    CC_OP_BMILGL,
    CC_OP_BMILGQ,

    CC_OP_ADCX, /* CC_DST = C, CC_SRC = rest.  */
    CC_OP_ADOX, /* CC_DST = O, CC_SRC = rest.  */
    CC_OP_ADCOX, /* CC_DST = C, CC_SRC2 = O, CC_SRC = rest.  */

    CC_OP_CLR, /* Z set, all other flags clear.  */
    CC_OP_POPCNT, /* Z via CC_SRC, all other flags clear.  */

    CC_OP_NB,
} CCOp;

typedef struct SegmentCache {
    uint32_t selector;
    target_ulong base;
    uint32_t limit;
    uint32_t flags;
} SegmentCache;

#define MMREG_UNION(n, bits)        \
    union n {                       \
        uint8_t  _b_##n[(bits)/8];  \
        uint16_t _w_##n[(bits)/16]; \
        uint32_t _l_##n[(bits)/32]; \
        uint64_t _q_##n[(bits)/64]; \
        float32  _s_##n[(bits)/32]; \
        float64  _d_##n[(bits)/64]; \
    }

typedef union {
    uint8_t _b[16];
    uint16_t _w[8];
    uint32_t _l[4];
    uint64_t _q[2];
} XMMReg;

typedef union {
    uint8_t _b[32];
    uint16_t _w[16];
    uint32_t _l[8];
    uint64_t _q[4];
} YMMReg;

typedef MMREG_UNION(ZMMReg, 512) ZMMReg;
typedef MMREG_UNION(MMXReg, 64)  MMXReg;

typedef struct BNDReg {
    uint64_t lb;
    uint64_t ub;
} BNDReg;

typedef struct BNDCSReg {
    uint64_t cfgu;
    uint64_t sts;
} BNDCSReg;

#define BNDCFG_ENABLE       1ULL
#define BNDCFG_BNDPRESERVE  2ULL
#define BNDCFG_BDIR_MASK    TARGET_PAGE_MASK

#ifdef HOST_WORDS_BIGENDIAN
#define ZMM_B(n) _b_ZMMReg[63 - (n)]
#define ZMM_W(n) _w_ZMMReg[31 - (n)]
#define ZMM_L(n) _l_ZMMReg[15 - (n)]
#define ZMM_S(n) _s_ZMMReg[15 - (n)]
#define ZMM_Q(n) _q_ZMMReg[7 - (n)]
#define ZMM_D(n) _d_ZMMReg[7 - (n)]

#define MMX_B(n) _b_MMXReg[7 - (n)]
#define MMX_W(n) _w_MMXReg[3 - (n)]
#define MMX_L(n) _l_MMXReg[1 - (n)]
#define MMX_S(n) _s_MMXReg[1 - (n)]
#else
#define ZMM_B(n) _b_ZMMReg[n]
#define ZMM_W(n) _w_ZMMReg[n]
#define ZMM_L(n) _l_ZMMReg[n]
#define ZMM_S(n) _s_ZMMReg[n]
#define ZMM_Q(n) _q_ZMMReg[n]
#define ZMM_D(n) _d_ZMMReg[n]

#define MMX_B(n) _b_MMXReg[n]
#define MMX_W(n) _w_MMXReg[n]
#define MMX_L(n) _l_MMXReg[n]
#define MMX_S(n) _s_MMXReg[n]
#endif
#define MMX_Q(n) _q_MMXReg[n]

typedef union {
    floatx80 d __attribute__((aligned(16)));
    MMXReg mmx;
} FPReg;

typedef struct {
    uint64_t base;
    uint64_t mask;
} MTRRVar;

#define CPU_NB_REGS64 16
#define CPU_NB_REGS32 8

#ifdef TARGET_X86_64
#define CPU_NB_REGS CPU_NB_REGS64
#else
#define CPU_NB_REGS CPU_NB_REGS32
#endif

#define MAX_FIXED_COUNTERS 3
#define MAX_GP_COUNTERS    (MSR_IA32_PERF_STATUS - MSR_P6_EVNTSEL0)

#define TARGET_INSN_START_EXTRA_WORDS 1

#define NB_OPMASK_REGS 8

/* CPU can't have 0xFFFFFFFF APIC ID, use that value to distinguish
 * that APIC ID hasn't been set yet
 */
#define UNASSIGNED_APIC_ID 0xFFFFFFFF

typedef union X86LegacyXSaveArea {
    struct {
        uint16_t fcw;
        uint16_t fsw;
        uint8_t ftw;
        uint8_t reserved;
        uint16_t fpop;
        uint64_t fpip;
        uint64_t fpdp;
        uint32_t mxcsr;
        uint32_t mxcsr_mask;
        FPReg fpregs[8];
        uint8_t xmm_regs[16][16];
    };
    uint8_t data[512];
} X86LegacyXSaveArea;

typedef struct X86XSaveHeader {
    uint64_t xstate_bv;
    uint64_t xcomp_bv;
    uint64_t reserve0;
    uint8_t reserved[40];
} X86XSaveHeader;

/* Ext. save area 2: AVX State */
typedef struct XSaveAVX {
    uint8_t ymmh[16][16];
} XSaveAVX;

/* Ext. save area 3: BNDREG */
typedef struct XSaveBNDREG {
    BNDReg bnd_regs[4];
} XSaveBNDREG;

/* Ext. save area 4: BNDCSR */
typedef union XSaveBNDCSR {
    BNDCSReg bndcsr;
    uint8_t data[64];
} XSaveBNDCSR;

/* Ext. save area 5: Opmask */
typedef struct XSaveOpmask {
    uint64_t opmask_regs[NB_OPMASK_REGS];
} XSaveOpmask;

/* Ext. save area 6: ZMM_Hi256 */
typedef struct XSaveZMM_Hi256 {
    uint8_t zmm_hi256[16][32];
} XSaveZMM_Hi256;

/* Ext. save area 7: Hi16_ZMM */
typedef struct XSaveHi16_ZMM {
    uint8_t hi16_zmm[16][64];
} XSaveHi16_ZMM;

/* Ext. save area 9: PKRU state */
typedef struct XSavePKRU {
    uint32_t pkru;
    uint32_t padding;
} XSavePKRU;

QEMU_BUILD_BUG_ON(sizeof(XSaveAVX) != 0x100);
QEMU_BUILD_BUG_ON(sizeof(XSaveBNDREG) != 0x40);
QEMU_BUILD_BUG_ON(sizeof(XSaveBNDCSR) != 0x40);
QEMU_BUILD_BUG_ON(sizeof(XSaveOpmask) != 0x40);
QEMU_BUILD_BUG_ON(sizeof(XSaveZMM_Hi256) != 0x200);
QEMU_BUILD_BUG_ON(sizeof(XSaveHi16_ZMM) != 0x400);
QEMU_BUILD_BUG_ON(sizeof(XSavePKRU) != 0x8);

typedef struct ExtSaveArea {
    uint32_t feature, bits;
    uint32_t offset, size;
} ExtSaveArea;

#define XSAVE_STATE_AREA_COUNT (XSTATE_PKRU_BIT + 1)

extern ExtSaveArea x86_ext_save_areas[XSAVE_STATE_AREA_COUNT];

typedef enum TPRAccess {
    TPR_ACCESS_READ,
    TPR_ACCESS_WRITE,
} TPRAccess;

/* Cache information data structures: */

enum CacheType {
    DATA_CACHE,
    INSTRUCTION_CACHE,
    UNIFIED_CACHE
};

typedef struct CPUCacheInfo {
    enum CacheType type;
    uint8_t level;
    /* Size in bytes */
    uint32_t size;
    /* Line size, in bytes */
    uint16_t line_size;
    /*
     * Associativity.
     * Note: representation of fully-associative caches is not implemented
     */
    uint8_t associativity;
    /* Physical line partitions. CPUID[0x8000001D].EBX, CPUID[4].EBX */
    uint8_t partitions;
    /* Number of sets. CPUID[0x8000001D].ECX, CPUID[4].ECX */
    uint32_t sets;
    /*
     * Lines per tag.
     * AMD-specific: CPUID[0x80000005], CPUID[0x80000006].
     * (Is this synonym to @partitions?)
     */
    uint8_t lines_per_tag;

    /* Self-initializing cache */
    bool self_init;
    /*
     * WBINVD/INVD is not guaranteed to act upon lower level caches of
     * non-originating threads sharing this cache.
     * CPUID[4].EDX[bit 0], CPUID[0x8000001D].EDX[bit 0]
     */
    bool no_invd_sharing;
    /*
     * Cache is inclusive of lower cache levels.
     * CPUID[4].EDX[bit 1], CPUID[0x8000001D].EDX[bit 1].
     */
    bool inclusive;
    /*
     * A complex function is used to index the cache, potentially using all
     * address bits.  CPUID[4].EDX[bit 2].
     */
    bool complex_indexing;
} CPUCacheInfo;


typedef struct CPUCaches {
        CPUCacheInfo *l1d_cache;
        CPUCacheInfo *l1i_cache;
        CPUCacheInfo *l2_cache;
        CPUCacheInfo *l3_cache;
} CPUCaches;

typedef struct HVFX86LazyFlags {
    target_ulong result;
    target_ulong auxbits;
} HVFX86LazyFlags;

typedef struct CPUX86State {
    /* standard registers */
    target_ulong regs[CPU_NB_REGS];
    target_ulong eip;
    target_ulong eflags; /* eflags register. During CPU emulation, CC
                        flags and DF are set to zero because they are
                        stored elsewhere */

    /* emulator internal eflags handling */
    target_ulong cc_dst;
    target_ulong cc_src;
    target_ulong cc_src2;
    uint32_t cc_op;
    int32_t df; /* D flag : 1 if D = 0, -1 if D = 1 */
    uint32_t hflags; /* TB flags, see HF_xxx constants. These flags
                        are known at translation time. */
    uint32_t hflags2; /* various other flags, see HF2_xxx constants. */

    /* segments */
    SegmentCache segs[6]; /* selector values */
    SegmentCache ldt;
    SegmentCache tr;
    SegmentCache gdt; /* only base and limit are used */
    SegmentCache idt; /* only base and limit are used */

    target_ulong cr[5]; /* NOTE: cr1 is unused */
    int32_t a20_mask;

    BNDReg bnd_regs[4];
    BNDCSReg bndcs_regs;
    uint64_t msr_bndcfgs;
    uint64_t efer;

    /* Beginning of state preserved by INIT (dummy marker).  */
    struct {} start_init_save;

    /* FPU state */
    unsigned int fpstt; /* top of stack index */
    uint16_t fpus;
    uint16_t fpuc;
    uint8_t fptags[8];   /* 0 = valid, 1 = empty */
    FPReg fpregs[8];
    /* KVM-only so far */
    uint16_t fpop;
    uint16_t fpcs;
    uint16_t fpds;
    uint64_t fpip;
    uint64_t fpdp;

    /* emulator internal variables */
    float_status fp_status;
    floatx80 ft0;

    float_status mmx_status; /* for 3DNow! float ops */
    float_status sse_status;
    uint32_t mxcsr;
    ZMMReg xmm_regs[CPU_NB_REGS == 8 ? 8 : 32];
    ZMMReg xmm_t0;
    MMXReg mmx_t0;

    XMMReg ymmh_regs[CPU_NB_REGS];

    uint64_t opmask_regs[NB_OPMASK_REGS];
    YMMReg zmmh_regs[CPU_NB_REGS];
    ZMMReg hi16_zmm_regs[CPU_NB_REGS];

    /* sysenter registers */
    uint32_t sysenter_cs;
    target_ulong sysenter_esp;
    target_ulong sysenter_eip;
    uint64_t star;

    uint64_t vm_hsave;

#ifdef TARGET_X86_64
    target_ulong lstar;
    target_ulong cstar;
    target_ulong fmask;
    target_ulong kernelgsbase;
#endif

    uint64_t tsc;
    uint64_t tsc_adjust;
    uint64_t tsc_deadline;
    uint64_t tsc_aux;

    uint64_t xcr0;

    uint64_t mcg_status;
    uint64_t msr_ia32_misc_enable;
    uint64_t msr_ia32_feature_control;
    uint64_t msr_ia32_sgxlepubkeyhash[4];

    uint64_t msr_fixed_ctr_ctrl;
    uint64_t msr_global_ctrl;
    uint64_t msr_global_status;
    uint64_t msr_global_ovf_ctrl;
    uint64_t msr_fixed_counters[MAX_FIXED_COUNTERS];
    uint64_t msr_gp_counters[MAX_GP_COUNTERS];
    uint64_t msr_gp_evtsel[MAX_GP_COUNTERS];

    uint64_t pat;
    uint32_t smbase;
    uint64_t msr_smi_count;

    uint32_t pkru;
    uint32_t pkrs;
    uint32_t tsx_ctrl;

    uint64_t spec_ctrl;
    uint64_t virt_ssbd;

    /* End of state preserved by INIT (dummy marker).  */
    struct {} end_init_save;

    uint64_t system_time_msr;
    uint64_t wall_clock_msr;
    uint64_t steal_time_msr;
    uint64_t async_pf_en_msr;
    uint64_t async_pf_int_msr;
    uint64_t pv_eoi_en_msr;
    uint64_t poll_control_msr;

    /* Partition-wide HV MSRs, will be updated only on the first vcpu */
    uint64_t msr_hv_hypercall;
    uint64_t msr_hv_guest_os_id;
    uint64_t msr_hv_tsc;

    /* Per-VCPU HV MSRs */
    uint64_t msr_hv_vapic;
    uint64_t msr_hv_crash_params[HV_CRASH_PARAMS];
    uint64_t msr_hv_runtime;
    uint64_t msr_hv_synic_control;
    uint64_t msr_hv_synic_evt_page;
    uint64_t msr_hv_synic_msg_page;
    uint64_t msr_hv_synic_sint[HV_SINT_COUNT];
    uint64_t msr_hv_stimer_config[HV_STIMER_COUNT];
    uint64_t msr_hv_stimer_count[HV_STIMER_COUNT];
    uint64_t msr_hv_reenlightenment_control;
    uint64_t msr_hv_tsc_emulation_control;
    uint64_t msr_hv_tsc_emulation_status;

    uint64_t msr_rtit_ctrl;
    uint64_t msr_rtit_status;
    uint64_t msr_rtit_output_base;
    uint64_t msr_rtit_output_mask;
    uint64_t msr_rtit_cr3_match;
    uint64_t msr_rtit_addrs[MAX_RTIT_ADDRS];

    /* exception/interrupt handling */
    int error_code;
    int exception_is_int;
    target_ulong exception_next_eip;
    target_ulong dr[8]; /* debug registers; note dr4 and dr5 are unused */
    union {
        struct CPUBreakpoint *cpu_breakpoint[4];
        struct CPUWatchpoint *cpu_watchpoint[4];
    }; /* break/watchpoints for dr[0..3] */
    int old_exception;  /* exception in flight */

    uint64_t vm_vmcb;
    uint64_t tsc_offset;
    uint64_t intercept;
    uint16_t intercept_cr_read;
    uint16_t intercept_cr_write;
    uint16_t intercept_dr_read;
    uint16_t intercept_dr_write;
    uint32_t intercept_exceptions;
    uint64_t nested_cr3;
    uint32_t nested_pg_mode;
    uint8_t v_tpr;
    uint32_t int_ctl;

    /* KVM states, automatically cleared on reset */
    uint8_t nmi_injected;
    uint8_t nmi_pending;

    uintptr_t retaddr;

    /* Fields up to this point are cleared by a CPU reset */
    struct {} end_reset_fields;

    /* Fields after this point are preserved across CPU reset. */

    /* processor features (e.g. for CPUID insn) */
    /* Minimum cpuid leaf 7 value */
    uint32_t cpuid_level_func7;
    /* Actual cpuid leaf 7 value */
    uint32_t cpuid_min_level_func7;
    /* Minimum level/xlevel/xlevel2, based on CPU model + features */
    uint32_t cpuid_min_level, cpuid_min_xlevel, cpuid_min_xlevel2;
    /* Maximum level/xlevel/xlevel2 value for auto-assignment: */
    uint32_t cpuid_max_level, cpuid_max_xlevel, cpuid_max_xlevel2;
    /* Actual level/xlevel/xlevel2 value: */
    uint32_t cpuid_level, cpuid_xlevel, cpuid_xlevel2;
    uint32_t cpuid_vendor1;
    uint32_t cpuid_vendor2;
    uint32_t cpuid_vendor3;
    uint32_t cpuid_version;
    FeatureWordArray features;
    /* Features that were explicitly enabled/disabled */
    FeatureWordArray user_features;
    uint32_t cpuid_model[12];
    /* Cache information for CPUID.  When legacy-cache=on, the cache data
     * on each CPUID leaf will be different, because we keep compatibility
     * with old QEMU versions.
     */
    CPUCaches cache_info_cpuid2, cache_info_cpuid4, cache_info_amd;

    /* MTRRs */
    uint64_t mtrr_fixed[11];
    uint64_t mtrr_deftype;
    MTRRVar mtrr_var[MSR_MTRRcap_VCNT];

    /* For KVM */
    uint32_t mp_state;
    int32_t exception_nr;
    int32_t interrupt_injected;
    uint8_t soft_interrupt;
    uint8_t exception_pending;
    uint8_t exception_injected;
    uint8_t has_error_code;
    uint8_t exception_has_payload;
    uint64_t exception_payload;
    uint32_t ins_len;
    uint32_t sipi_vector;
    bool tsc_valid;
    int64_t tsc_khz;
    int64_t user_tsc_khz; /* for sanity check only */
    uint64_t apic_bus_freq;
#if defined(CONFIG_KVM) || defined(CONFIG_HVF)
    void *xsave_buf;
    uint32_t xsave_buf_len;
#endif
#if defined(CONFIG_KVM)
    struct kvm_nested_state *nested_state;
#endif
#if defined(CONFIG_HVF)
    HVFX86LazyFlags hvf_lflags;
    void *hvf_mmio_buf;
#endif

    uint64_t mcg_cap;
    uint64_t mcg_ctl;
    uint64_t mcg_ext_ctl;
    uint64_t mce_banks[MCE_BANKS_DEF*4];
    uint64_t xstate_bv;

    /* vmstate */
    uint16_t fpus_vmstate;
    uint16_t fptag_vmstate;
    uint16_t fpregs_format_vmstate;

    uint64_t xss;
    uint32_t umwait;

    TPRAccess tpr_access_type;

    unsigned nr_dies;
} CPUX86State;

struct kvm_msrs;

/**
 * X86CPU:
 * @env: #CPUX86State
 * @migratable: If set, only migratable flags will be accepted when "enforce"
 * mode is used, and only migratable flags will be included in the "host"
 * CPU model.
 *
 * An x86 CPU.
 */
struct X86CPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPUX86State env;
    VMChangeStateEntry *vmsentry;

    uint64_t ucode_rev;

    uint32_t hyperv_spinlock_attempts;
    char *hyperv_vendor;
    bool hyperv_synic_kvm_only;
    uint64_t hyperv_features;
    bool hyperv_passthrough;
    OnOffAuto hyperv_no_nonarch_cs;
    uint32_t hyperv_vendor_id[3];
    uint32_t hyperv_interface_id[4];
    uint32_t hyperv_limits[3];
    uint32_t hyperv_nested[4];
    bool hyperv_enforce_cpuid;
    uint32_t hyperv_ver_id_build;
    uint16_t hyperv_ver_id_major;
    uint16_t hyperv_ver_id_minor;
    uint32_t hyperv_ver_id_sp;
    uint8_t hyperv_ver_id_sb;
    uint32_t hyperv_ver_id_sn;

    bool check_cpuid;
    bool enforce_cpuid;
    /*
     * Force features to be enabled even if the host doesn't support them.
     * This is dangerous and should be done only for testing CPUID
     * compatibility.
     */
    bool force_features;
    bool expose_kvm;
    bool expose_tcg;
    bool migratable;
    bool migrate_smi_count;
    bool max_features; /* Enable all supported features automatically */
    uint32_t apic_id;

    /* Enables publishing of TSC increment and Local APIC bus frequencies to
     * the guest OS in CPUID page 0x40000010, the same way that VMWare does. */
    bool vmware_cpuid_freq;

    /* if true the CPUID code directly forward host cache leaves to the guest */
    bool cache_info_passthrough;

    /* if true the CPUID code directly forwards
     * host monitor/mwait leaves to the guest */
    struct {
        uint32_t eax;
        uint32_t ebx;
        uint32_t ecx;
        uint32_t edx;
    } mwait;

    /* Features that were filtered out because of missing host capabilities */
    FeatureWordArray filtered_features;

    /* Enable PMU CPUID bits. This can't be enabled by default yet because
     * it doesn't have ABI stability guarantees, as it passes all PMU CPUID
     * bits returned by GET_SUPPORTED_CPUID (that depend on host CPU and kernel
     * capabilities) directly to the guest.
     */
    bool enable_pmu;

    /* LMCE support can be enabled/disabled via cpu option 'lmce=on/off'. It is
     * disabled by default to avoid breaking migration between QEMU with
     * different LMCE configurations.
     */
    bool enable_lmce;

    /* Compatibility bits for old machine types.
     * If true present virtual l3 cache for VM, the vcpus in the same virtual
     * socket share an virtual l3 cache.
     */
    bool enable_l3_cache;

    /* Compatibility bits for old machine types.
     * If true present the old cache topology information
     */
    bool legacy_cache;

    /* Compatibility bits for old machine types: */
    bool enable_cpuid_0xb;

    /* Enable auto level-increase for all CPUID leaves */
    bool full_cpuid_auto_level;

    /* Only advertise CPUID leaves defined by the vendor */
    bool vendor_cpuid_only;

    /* Enable auto level-increase for Intel Processor Trace leave */
    bool intel_pt_auto_level;

    /* if true fill the top bits of the MTRR_PHYSMASKn variable range */
    bool fill_mtrr_mask;

    /* if true override the phys_bits value with a value read from the host */
    bool host_phys_bits;

    /* if set, limit maximum value for phys_bits when host_phys_bits is true */
    uint8_t host_phys_bits_limit;

    /* Stop SMI delivery for migration compatibility with old machines */
    bool kvm_no_smi_migration;

    /* Forcefully disable KVM PV features not exposed in guest CPUIDs */
    bool kvm_pv_enforce_cpuid;

    /* Number of physical address bits supported */
    uint32_t phys_bits;

    /* in order to simplify APIC support, we leave this pointer to the
       user */
    struct DeviceState *apic_state;
    struct MemoryRegion *cpu_as_root, *cpu_as_mem, *smram;
    Notifier machine_done;

    struct kvm_msrs *kvm_msr_buf;

    int32_t node_id; /* NUMA node this CPU belongs to */
    int32_t socket_id;
    int32_t die_id;
    int32_t core_id;
    int32_t thread_id;

    int32_t hv_max_vps;
};


#ifndef CONFIG_USER_ONLY
extern const VMStateDescription vmstate_x86_cpu;
#endif

int x86_cpu_pending_interrupt(CPUState *cs, int interrupt_request);

int x86_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cpu,
                             int cpuid, void *opaque);
int x86_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cpu,
                             int cpuid, void *opaque);
int x86_cpu_write_elf64_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                                 void *opaque);
int x86_cpu_write_elf32_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                                 void *opaque);

void x86_cpu_get_memory_mapping(CPUState *cpu, MemoryMappingList *list,
                                Error **errp);

void x86_cpu_dump_state(CPUState *cs, FILE *f, int flags);

hwaddr x86_cpu_get_phys_page_attrs_debug(CPUState *cpu, vaddr addr,
                                         MemTxAttrs *attrs);

int x86_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int x86_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

void x86_cpu_list(void);
int cpu_x86_support_mca_broadcast(CPUX86State *env);

#ifndef CONFIG_USER_ONLY
int cpu_get_pic_interrupt(CPUX86State *s);

/* MSDOS compatibility mode FPU exception support */
void x86_register_ferr_irq(qemu_irq irq);
void fpu_check_raise_ferr_irq(CPUX86State *s);
void cpu_set_ignne(void);
void cpu_clear_ignne(void);
#endif

/* mpx_helper.c */
void cpu_sync_bndcs_hflags(CPUX86State *env);

/* this function must always be used to load data in the segment
   cache: it synchronizes the hflags with the segment cache values */
static inline void cpu_x86_load_seg_cache(CPUX86State *env,
                                          X86Seg seg_reg, unsigned int selector,
                                          target_ulong base,
                                          unsigned int limit,
                                          unsigned int flags)
{
    SegmentCache *sc;
    unsigned int new_hflags;

    sc = &env->segs[seg_reg];
    sc->selector = selector;
    sc->base = base;
    sc->limit = limit;
    sc->flags = flags;

    /* update the hidden flags */
    {
        if (seg_reg == R_CS) {
#ifdef TARGET_X86_64
            if ((env->hflags & HF_LMA_MASK) && (flags & DESC_L_MASK)) {
                /* long mode */
                env->hflags |= HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK;
                env->hflags &= ~(HF_ADDSEG_MASK);
            } else
#endif
            {
                /* legacy / compatibility case */
                new_hflags = (env->segs[R_CS].flags & DESC_B_MASK)
                    >> (DESC_B_SHIFT - HF_CS32_SHIFT);
                env->hflags = (env->hflags & ~(HF_CS32_MASK | HF_CS64_MASK)) |
                    new_hflags;
            }
        }
        if (seg_reg == R_SS) {
            int cpl = (flags >> DESC_DPL_SHIFT) & 3;
#if HF_CPL_MASK != 3
#error HF_CPL_MASK is hardcoded
#endif
            env->hflags = (env->hflags & ~HF_CPL_MASK) | cpl;
            /* Possibly switch between BNDCFGS and BNDCFGU */
            cpu_sync_bndcs_hflags(env);
        }
        new_hflags = (env->segs[R_SS].flags & DESC_B_MASK)
            >> (DESC_B_SHIFT - HF_SS32_SHIFT);
        if (env->hflags & HF_CS64_MASK) {
            /* zero base assumed for DS, ES and SS in long mode */
        } else if (!(env->cr[0] & CR0_PE_MASK) ||
                   (env->eflags & VM_MASK) ||
                   !(env->hflags & HF_CS32_MASK)) {
            /* XXX: try to avoid this test. The problem comes from the
               fact that is real mode or vm86 mode we only modify the
               'base' and 'selector' fields of the segment cache to go
               faster. A solution may be to force addseg to one in
               translate-i386.c. */
            new_hflags |= HF_ADDSEG_MASK;
        } else {
            new_hflags |= ((env->segs[R_DS].base |
                            env->segs[R_ES].base |
                            env->segs[R_SS].base) != 0) <<
                HF_ADDSEG_SHIFT;
        }
        env->hflags = (env->hflags &
                       ~(HF_SS32_MASK | HF_ADDSEG_MASK)) | new_hflags;
    }
}

static inline void cpu_x86_load_seg_cache_sipi(X86CPU *cpu,
                                               uint8_t sipi_vector)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;

    env->eip = 0;
    cpu_x86_load_seg_cache(env, R_CS, sipi_vector << 8,
                           sipi_vector << 12,
                           env->segs[R_CS].limit,
                           env->segs[R_CS].flags);
    cs->halted = 0;
}

int cpu_x86_get_descr_debug(CPUX86State *env, unsigned int selector,
                            target_ulong *base, unsigned int *limit,
                            unsigned int *flags);

/* op_helper.c */
/* used for debug or cpu save/restore */

/* cpu-exec.c */
/* the following helpers are only usable in user mode simulation as
   they can trigger unexpected exceptions */
void cpu_x86_load_seg(CPUX86State *s, X86Seg seg_reg, int selector);
void cpu_x86_fsave(CPUX86State *s, target_ulong ptr, int data32);
void cpu_x86_frstor(CPUX86State *s, target_ulong ptr, int data32);
void cpu_x86_fxsave(CPUX86State *s, target_ulong ptr);
void cpu_x86_fxrstor(CPUX86State *s, target_ulong ptr);

/* cpu.c */
void x86_cpu_vendor_words2str(char *dst, uint32_t vendor1,
                              uint32_t vendor2, uint32_t vendor3);
typedef struct PropValue {
    const char *prop, *value;
} PropValue;
void x86_cpu_apply_props(X86CPU *cpu, PropValue *props);

uint32_t cpu_x86_virtual_addr_width(CPUX86State *env);

/* cpu.c other functions (cpuid) */
void cpu_x86_cpuid(CPUX86State *env, uint32_t index, uint32_t count,
                   uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx);
void cpu_clear_apic_feature(CPUX86State *env);
void host_cpuid(uint32_t function, uint32_t count,
                uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx);

/* helper.c */
void x86_cpu_set_a20(X86CPU *cpu, int a20_state);

#ifndef CONFIG_USER_ONLY
static inline int x86_asidx_from_attrs(CPUState *cs, MemTxAttrs attrs)
{
    return !!attrs.secure;
}

static inline AddressSpace *cpu_addressspace(CPUState *cs, MemTxAttrs attrs)
{
    return cpu_get_address_space(cs, cpu_asidx_from_attrs(cs, attrs));
}

/*
 * load efer and update the corresponding hflags. XXX: do consistency
 * checks with cpuid bits?
 */
void cpu_load_efer(CPUX86State *env, uint64_t val);
uint8_t x86_ldub_phys(CPUState *cs, hwaddr addr);
uint32_t x86_lduw_phys(CPUState *cs, hwaddr addr);
uint32_t x86_ldl_phys(CPUState *cs, hwaddr addr);
uint64_t x86_ldq_phys(CPUState *cs, hwaddr addr);
void x86_stb_phys(CPUState *cs, hwaddr addr, uint8_t val);
void x86_stl_phys_notdirty(CPUState *cs, hwaddr addr, uint32_t val);
void x86_stw_phys(CPUState *cs, hwaddr addr, uint32_t val);
void x86_stl_phys(CPUState *cs, hwaddr addr, uint32_t val);
void x86_stq_phys(CPUState *cs, hwaddr addr, uint64_t val);
#endif

/* will be suppressed */
void cpu_x86_update_cr0(CPUX86State *env, uint32_t new_cr0);
void cpu_x86_update_cr3(CPUX86State *env, target_ulong new_cr3);
void cpu_x86_update_cr4(CPUX86State *env, uint32_t new_cr4);
void cpu_x86_update_dr7(CPUX86State *env, uint32_t new_dr7);

/* hw/pc.c */
uint64_t cpu_get_tsc(CPUX86State *env);

#define X86_CPU_TYPE_SUFFIX "-" TYPE_X86_CPU
#define X86_CPU_TYPE_NAME(name) (name X86_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_X86_CPU

#ifdef TARGET_X86_64
#define TARGET_DEFAULT_CPU_TYPE X86_CPU_TYPE_NAME("qemu64")
#else
#define TARGET_DEFAULT_CPU_TYPE X86_CPU_TYPE_NAME("qemu32")
#endif

#define cpu_list x86_cpu_list

/* MMU modes definitions */
#define MMU_KSMAP_IDX   0
#define MMU_USER_IDX    1
#define MMU_KNOSMAP_IDX 2
static inline int cpu_mmu_index(CPUX86State *env, bool ifetch)
{
    return (env->hflags & HF_CPL_MASK) == 3 ? MMU_USER_IDX :
        (!(env->hflags & HF_SMAP_MASK) || (env->eflags & AC_MASK))
        ? MMU_KNOSMAP_IDX : MMU_KSMAP_IDX;
}

static inline int cpu_mmu_index_kernel(CPUX86State *env)
{
    return !(env->hflags & HF_SMAP_MASK) ? MMU_KNOSMAP_IDX :
        ((env->hflags & HF_CPL_MASK) < 3 && (env->eflags & AC_MASK))
        ? MMU_KNOSMAP_IDX : MMU_KSMAP_IDX;
}

#define CC_DST  (env->cc_dst)
#define CC_SRC  (env->cc_src)
#define CC_SRC2 (env->cc_src2)
#define CC_OP   (env->cc_op)

typedef CPUX86State CPUArchState;
typedef X86CPU ArchCPU;

#include "exec/cpu-all.h"
#include "svm.h"

#if !defined(CONFIG_USER_ONLY)
#include "hw/i386/apic.h"
#endif

static inline void cpu_get_tb_cpu_state(CPUX86State *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *cs_base = env->segs[R_CS].base;
    *pc = *cs_base + env->eip;
    *flags = env->hflags |
        (env->eflags & (IOPL_MASK | TF_MASK | RF_MASK | VM_MASK | AC_MASK));
}

void do_cpu_init(X86CPU *cpu);
void do_cpu_sipi(X86CPU *cpu);

#define MCE_INJECT_BROADCAST    1
#define MCE_INJECT_UNCOND_AO    2

void cpu_x86_inject_mce(Monitor *mon, X86CPU *cpu, int bank,
                        uint64_t status, uint64_t mcg_status, uint64_t addr,
                        uint64_t misc, int flags);

uint32_t cpu_cc_compute_all(CPUX86State *env1, int op);

static inline uint32_t cpu_compute_eflags(CPUX86State *env)
{
    uint32_t eflags = env->eflags;
    if (tcg_enabled()) {
        eflags |= cpu_cc_compute_all(env, CC_OP) | (env->df & DF_MASK);
    }
    return eflags;
}

static inline MemTxAttrs cpu_get_mem_attrs(CPUX86State *env)
{
    return ((MemTxAttrs) { .secure = (env->hflags & HF_SMM_MASK) != 0 });
}

static inline int32_t x86_get_a20_mask(CPUX86State *env)
{
    if (env->hflags & HF_SMM_MASK) {
        return -1;
    } else {
        return env->a20_mask;
    }
}

static inline bool cpu_has_vmx(CPUX86State *env)
{
    return env->features[FEAT_1_ECX] & CPUID_EXT_VMX;
}

static inline bool cpu_has_svm(CPUX86State *env)
{
    return env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM;
}

/*
 * In order for a vCPU to enter VMX operation it must have CR4.VMXE set.
 * Since it was set, CR4.VMXE must remain set as long as vCPU is in
 * VMX operation. This is because CR4.VMXE is one of the bits set
 * in MSR_IA32_VMX_CR4_FIXED1.
 *
 * There is one exception to above statement when vCPU enters SMM mode.
 * When a vCPU enters SMM mode, it temporarily exit VMX operation and
 * may also reset CR4.VMXE during execution in SMM mode.
 * When vCPU exits SMM mode, vCPU state is restored to be in VMX operation
 * and CR4.VMXE is restored to it's original value of being set.
 *
 * Therefore, when vCPU is not in SMM mode, we can infer whether
 * VMX is being used by examining CR4.VMXE. Otherwise, we cannot
 * know for certain.
 */
static inline bool cpu_vmx_maybe_enabled(CPUX86State *env)
{
    return cpu_has_vmx(env) &&
           ((env->cr[4] & CR4_VMXE_MASK) || (env->hflags & HF_SMM_MASK));
}

/* excp_helper.c */
int get_pg_mode(CPUX86State *env);

/* fpu_helper.c */
void update_fp_status(CPUX86State *env);
void update_mxcsr_status(CPUX86State *env);
void update_mxcsr_from_sse_status(CPUX86State *env);

static inline void cpu_set_mxcsr(CPUX86State *env, uint32_t mxcsr)
{
    env->mxcsr = mxcsr;
    if (tcg_enabled()) {
        update_mxcsr_status(env);
    }
}

static inline void cpu_set_fpuc(CPUX86State *env, uint16_t fpuc)
{
     env->fpuc = fpuc;
     if (tcg_enabled()) {
        update_fp_status(env);
     }
}

/* mem_helper.c */
void helper_lock_init(void);

/* svm_helper.c */
#ifdef CONFIG_USER_ONLY
static inline void
cpu_svm_check_intercept_param(CPUX86State *env1, uint32_t type,
                              uint64_t param, uintptr_t retaddr)
{ /* no-op */ }
static inline bool
cpu_svm_has_intercept(CPUX86State *env, uint32_t type)
{ return false; }
#else
void cpu_svm_check_intercept_param(CPUX86State *env1, uint32_t type,
                                   uint64_t param, uintptr_t retaddr);
bool cpu_svm_has_intercept(CPUX86State *env, uint32_t type);
#endif

/* apic.c */
void cpu_report_tpr_access(CPUX86State *env, TPRAccess access);
void apic_handle_tpr_access_report(DeviceState *d, target_ulong ip,
                                   TPRAccess access);

/* Special values for X86CPUVersion: */

/* Resolve to latest CPU version */
#define CPU_VERSION_LATEST -1

/*
 * Resolve to version defined by current machine type.
 * See x86_cpu_set_default_version()
 */
#define CPU_VERSION_AUTO   -2

/* Don't resolve to any versioned CPU models, like old QEMU versions */
#define CPU_VERSION_LEGACY  0

typedef int X86CPUVersion;

/*
 * Set default CPU model version for CPU models having
 * version == CPU_VERSION_AUTO.
 */
void x86_cpu_set_default_version(X86CPUVersion version);

#define APIC_DEFAULT_ADDRESS 0xfee00000
#define APIC_SPACE_SIZE      0x100000

/* cpu-dump.c */
void x86_cpu_dump_local_apic_state(CPUState *cs, int flags);

/* cpu.c */
bool cpu_is_bsp(X86CPU *cpu);

void x86_cpu_xrstor_all_areas(X86CPU *cpu, const void *buf, uint32_t buflen);
void x86_cpu_xsave_all_areas(X86CPU *cpu, void *buf, uint32_t buflen);
void x86_update_hflags(CPUX86State* env);

static inline bool hyperv_feat_enabled(X86CPU *cpu, int feat)
{
    return !!(cpu->hyperv_features & BIT(feat));
}

static inline uint64_t cr4_reserved_bits(CPUX86State *env)
{
    uint64_t reserved_bits = CR4_RESERVED_MASK;
    if (!env->features[FEAT_XSAVE]) {
        reserved_bits |= CR4_OSXSAVE_MASK;
    }
    if (!(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SMEP)) {
        reserved_bits |= CR4_SMEP_MASK;
    }
    if (!(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SMAP)) {
        reserved_bits |= CR4_SMAP_MASK;
    }
    if (!(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_FSGSBASE)) {
        reserved_bits |= CR4_FSGSBASE_MASK;
    }
    if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_PKU)) {
        reserved_bits |= CR4_PKE_MASK;
    }
    if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_LA57)) {
        reserved_bits |= CR4_LA57_MASK;
    }
    if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_UMIP)) {
        reserved_bits |= CR4_UMIP_MASK;
    }
    if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_PKS)) {
        reserved_bits |= CR4_PKS_MASK;
    }
    return reserved_bits;
}

static inline bool ctl_has_irq(CPUX86State *env)
{
    uint32_t int_prio;
    uint32_t tpr;

    int_prio = (env->int_ctl & V_INTR_PRIO_MASK) >> V_INTR_PRIO_SHIFT;
    tpr = env->int_ctl & V_TPR_MASK;

    if (env->int_ctl & V_IGN_TPR_MASK) {
        return (env->int_ctl & V_IRQ_MASK);
    }

    return (env->int_ctl & V_IRQ_MASK) && (int_prio >= tpr);
}

hwaddr get_hphys(CPUState *cs, hwaddr gphys, MMUAccessType access_type,
                        int *prot);
#if defined(TARGET_X86_64) && \
    defined(CONFIG_USER_ONLY) && \
    defined(CONFIG_LINUX)
# define TARGET_VSYSCALL_PAGE  (UINT64_C(-10) << 20)
#endif

#endif /* I386_CPU_H */
