#if !defined (__QEMU_MIPS_DEFS_H__)
#define __QEMU_MIPS_DEFS_H__

/* If we want to use 64 bits host regs... */
//#define USE_64BITS_REGS
/* If we want to use host float regs... */
//#define USE_HOST_FLOAT_REGS

#define MIPS_R4Kc 0x00018000
#define MIPS_R4Kp 0x00018300

/* Emulate MIPS R4Kc for now */
#define MIPS_CPU MIPS_R4Kc

#if (MIPS_CPU == MIPS_R4Kc)
/* 32 bits target */
#define TARGET_LONG_BITS 32
/* real pages are variable size... */
#define TARGET_PAGE_BITS 12
/* Uses MIPS R4Kx enhancements to MIPS32 architecture */
#define MIPS_USES_R4K_EXT
/* Uses MIPS R4Kc TLB model */
#define MIPS_USES_R4K_TLB
#define MIPS_TLB_NB 16
/* basic FPU register support */
#define MIPS_USES_FPU 1
/* Define a implementation number of 1.
 * Define a major version 1, minor version 0.
 */
#define MIPS_FCR0 ((0 << 16) | (1 << 8) | (1 << 4) | 0)
/* Have config1, uses TLB */
#define MIPS_CONFIG0_1                                          \
((1 << CP0C0_M) | (0 << CP0C0_K23) | (0 << CP0C0_KU) |          \
 (1 << CP0C0_MT) | (2 << CP0C0_K0))
#ifdef TARGET_WORDS_BIGENDIAN
#define MIPS_CONFIG0 (MIPS_CONFIG0_1 | (1 << CP0C0_BE))
#else
#define MIPS_CONFIG0 MIPS_CONFIG0_1
#endif
/* 16 TLBs, 64 sets Icache, 16 bytes Icache line, 2-way Icache,
 * 64 sets Dcache, 16 bytes Dcache line, 2-way Dcache,
 * no performance counters, watch registers present, no code compression,
 * EJTAG present, FPU enable bit depending on MIPS_USES_FPU
 */
#define MIPS_CONFIG1                                            \
((15 << CP0C1_MMU) |                                            \
 (0x000 << CP0C1_IS) | (0x3 << CP0C1_IL) | (0x01 << CP0C1_IA) | \
 (0x000 << CP0C1_DS) | (0x3 << CP0C1_DL) | (0x01 << CP0C1_DA) | \
 (0 << CP0C1_PC) | (1 << CP0C1_WR) | (0 << CP0C1_CA) |          \
 (1 << CP0C1_EP) | (MIPS_USES_FPU << CP0C1_FP))
#elif (MIPS_CPU == MIPS_R4Kp)
/* 32 bits target */
#define TARGET_LONG_BITS 32
/* real pages are variable size... */
#define TARGET_PAGE_BITS 12
/* Uses MIPS R4Kx enhancements to MIPS32 architecture */
#define MIPS_USES_R4K_EXT
/* Uses MIPS R4Km FPM MMU model */
#define MIPS_USES_R4K_FPM
#else
#error "MIPS CPU not defined"
/* Remainder for other flags */
//#define TARGET_MIPS64
//#define MIPS_USES_FPU
#endif

#endif /* !defined (__QEMU_MIPS_DEFS_H__) */
