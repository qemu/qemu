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

#endif
#endif
