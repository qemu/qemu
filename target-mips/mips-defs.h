#if !defined (__QEMU_MIPS_DEFS_H__)
#define __QEMU_MIPS_DEFS_H__

/* If we want to use host float regs... */
//#define USE_HOST_FLOAT_REGS

/* real pages are variable size... */
#define TARGET_PAGE_BITS 12
#define MIPS_TLB_MAX 128

#ifdef TARGET_MIPS64
#define TARGET_LONG_BITS 64
#else
#define TARGET_LONG_BITS 32
#endif

/* Strictly follow the architecture standard:
   - Disallow "special" instruction handling for PMON/SPIM.
   Note that we still maintain Count/Compare to match the host clock. */
//#define MIPS_STRICT_STANDARD 1

#endif /* !defined (__QEMU_MIPS_DEFS_H__) */
