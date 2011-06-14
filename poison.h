/* Poison identifiers that should not be used when building
   target independent device code.  */

#ifndef HW_POISON_H
#define HW_POISON_H
#ifdef __GNUC__

#pragma GCC poison TARGET_I386
#pragma GCC poison TARGET_X86_64
#pragma GCC poison TARGET_ALPHA
#pragma GCC poison TARGET_ARM
#pragma GCC poison TARGET_CRIS
#pragma GCC poison TARGET_LM32
#pragma GCC poison TARGET_M68K
#pragma GCC poison TARGET_MIPS
#pragma GCC poison TARGET_MIPS64
#pragma GCC poison TARGET_PPC
#pragma GCC poison TARGET_PPCEMB
#pragma GCC poison TARGET_PPC64
#pragma GCC poison TARGET_ABI32
#pragma GCC poison TARGET_SH4
#pragma GCC poison TARGET_SPARC
#pragma GCC poison TARGET_SPARC64

#pragma GCC poison TARGET_WORDS_BIGENDIAN
#pragma GCC poison BSWAP_NEEDED

#pragma GCC poison TARGET_LONG_BITS
#pragma GCC poison TARGET_FMT_lx
#pragma GCC poison TARGET_FMT_ld

#pragma GCC poison TARGET_PAGE_SIZE
#pragma GCC poison TARGET_PAGE_MASK
#pragma GCC poison TARGET_PAGE_BITS
#pragma GCC poison TARGET_PAGE_ALIGN

#pragma GCC poison CPUState
#pragma GCC poison env

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

#endif
#endif
