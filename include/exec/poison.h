/* Poison identifiers that should not be used when building
   target independent device code.  */

#ifndef HW_POISON_H
#define HW_POISON_H

#include "config-poison.h"

#pragma GCC poison TARGET_I386
#pragma GCC poison TARGET_X86_64
#pragma GCC poison TARGET_AARCH64
#pragma GCC poison TARGET_ALPHA
#pragma GCC poison TARGET_ARM
#pragma GCC poison TARGET_CRIS
#pragma GCC poison TARGET_HEXAGON
#pragma GCC poison TARGET_HPPA
#pragma GCC poison TARGET_M68K
#pragma GCC poison TARGET_MICROBLAZE
#pragma GCC poison TARGET_MIPS
#pragma GCC poison TARGET_ABI_MIPSN32
#pragma GCC poison TARGET_ABI_MIPSO32
#pragma GCC poison TARGET_MIPS64
#pragma GCC poison TARGET_ABI_MIPSN64
#pragma GCC poison TARGET_NIOS2
#pragma GCC poison TARGET_OPENRISC
#pragma GCC poison TARGET_PPC
#pragma GCC poison TARGET_PPC64
#pragma GCC poison TARGET_ABI32
#pragma GCC poison TARGET_RX
#pragma GCC poison TARGET_S390X
#pragma GCC poison TARGET_SH4
#pragma GCC poison TARGET_SPARC
#pragma GCC poison TARGET_SPARC64
#pragma GCC poison TARGET_TRICORE
#pragma GCC poison TARGET_XTENSA

#pragma GCC poison TARGET_ALIGNED_ONLY
#pragma GCC poison TARGET_HAS_BFLT
#pragma GCC poison TARGET_NAME
#pragma GCC poison TARGET_SUPPORTS_MTTCG
#pragma GCC poison TARGET_WORDS_BIGENDIAN
#pragma GCC poison BSWAP_NEEDED

#pragma GCC poison TARGET_LONG_BITS
#pragma GCC poison TARGET_FMT_lx
#pragma GCC poison TARGET_FMT_ld
#pragma GCC poison TARGET_FMT_lu

#pragma GCC poison TARGET_PAGE_SIZE
#pragma GCC poison TARGET_PAGE_MASK
#pragma GCC poison TARGET_PAGE_BITS
#pragma GCC poison TARGET_PAGE_ALIGN

#pragma GCC poison CPUArchState

#pragma GCC poison CPU_INTERRUPT_HARD
#pragma GCC poison CPU_INTERRUPT_EXITTB
#pragma GCC poison CPU_INTERRUPT_HALT
#pragma GCC poison CPU_INTERRUPT_DEBUG
#pragma GCC poison CPU_INTERRUPT_TGT_EXT_0
#pragma GCC poison CPU_INTERRUPT_TGT_EXT_1
#pragma GCC poison CPU_INTERRUPT_TGT_EXT_2
#pragma GCC poison CPU_INTERRUPT_TGT_EXT_3
#pragma GCC poison CPU_INTERRUPT_TGT_EXT_4
#pragma GCC poison CPU_INTERRUPT_TGT_INT_0
#pragma GCC poison CPU_INTERRUPT_TGT_INT_1
#pragma GCC poison CPU_INTERRUPT_TGT_INT_2

#pragma GCC poison CONFIG_ALPHA_DIS
#pragma GCC poison CONFIG_ARM_A64_DIS
#pragma GCC poison CONFIG_ARM_DIS
#pragma GCC poison CONFIG_CRIS_DIS
#pragma GCC poison CONFIG_HPPA_DIS
#pragma GCC poison CONFIG_I386_DIS
#pragma GCC poison CONFIG_HEXAGON_DIS
#pragma GCC poison CONFIG_M68K_DIS
#pragma GCC poison CONFIG_MICROBLAZE_DIS
#pragma GCC poison CONFIG_MIPS_DIS
#pragma GCC poison CONFIG_NANOMIPS_DIS
#pragma GCC poison CONFIG_NIOS2_DIS
#pragma GCC poison CONFIG_PPC_DIS
#pragma GCC poison CONFIG_RISCV_DIS
#pragma GCC poison CONFIG_S390_DIS
#pragma GCC poison CONFIG_SH4_DIS
#pragma GCC poison CONFIG_SPARC_DIS
#pragma GCC poison CONFIG_XTENSA_DIS

#pragma GCC poison CONFIG_HAX
#pragma GCC poison CONFIG_HVF
#pragma GCC poison CONFIG_LINUX_USER
#pragma GCC poison CONFIG_KVM
#pragma GCC poison CONFIG_SOFTMMU
#pragma GCC poison CONFIG_WHPX
#pragma GCC poison CONFIG_XEN

#endif
