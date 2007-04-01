#if !defined (__QEMU_MIPS_DEFS_H__)
#define __QEMU_MIPS_DEFS_H__

/* If we want to use 64 bits host regs... */
//#define USE_64BITS_REGS
/* If we want to use host float regs... */
//#define USE_HOST_FLOAT_REGS

/* real pages are variable size... */
#define TARGET_PAGE_BITS 12
/* Uses MIPS R4Kc TLB model */
#define MIPS_USES_R4K_TLB
#define MIPS_TLB_NB 16
#define MIPS_TLB_MAX 128

#ifdef TARGET_MIPS64
#define TARGET_LONG_BITS 64
#else
#define TARGET_LONG_BITS 32
#endif

#endif /* !defined (__QEMU_MIPS_DEFS_H__) */
