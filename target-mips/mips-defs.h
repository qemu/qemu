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
#undef MIPS_HAS_MIPS64
//#define MIPS_HAS_MIPS64 1
/* real pages are variable size... */
#define TARGET_PAGE_BITS 12
/* Uses MIPS R4Kx enhancements to MIPS32 architecture */
#define MIPS_USES_R4K_EXT
/* Uses MIPS R4Kc TLB model */
#define MIPS_USES_R4K_TLB
#define MIPS_TLB_NB 16
#define MIPS_TLB_MAX 128
/* basic FPU register support */
#define MIPS_USES_FPU 1
/* Define a implementation number of 1.
 * Define a major version 1, minor version 0.
 */
#define MIPS_FCR0 ((0 << 16) | (1 << 8) | (1 << 4) | 0)
  /* Have config1, is MIPS32R1, uses TLB, no virtual icache,
     uncached coherency */
#define MIPS_CONFIG0_1                                            \
  ((1 << CP0C0_M) | (0x0 << CP0C0_K23) | (0x0 << CP0C0_KU) |      \
   (0x0 << CP0C0_AT) | (0x0 << CP0C0_AR) | (0x1 << CP0C0_MT) |    \
   (0x2 << CP0C0_K0))
#ifdef TARGET_WORDS_BIGENDIAN
#define MIPS_CONFIG0 (MIPS_CONFIG0_1 | (1 << CP0C0_BE))
#else
#define MIPS_CONFIG0 MIPS_CONFIG0_1
#endif
/* Have config2, 16 TLB entries, 64 sets Icache, 16 bytes Icache line,
   2-way Icache, 64 sets Dcache, 16 bytes Dcache line, 2-way Dcache,
   no coprocessor2 attached, no MDMX support attached,
   no performance counters, watch registers present,
   no code compression, EJTAG present, FPU enable bit depending on
   MIPS_USES_FPU */
#define MIPS_CONFIG1_1                                            \
((1 << CP0C1_M) | ((MIPS_TLB_NB - 1) << CP0C1_MMU) |              \
 (0x0 << CP0C1_IS) | (0x3 << CP0C1_IL) | (0x1 << CP0C1_IA) |      \
 (0x0 << CP0C1_DS) | (0x3 << CP0C1_DL) | (0x1 << CP0C1_DA) |      \
 (0 << CP0C1_C2) | (0 << CP0C1_MD) | (0 << CP0C1_PC) |            \
 (1 << CP0C1_WR) | (0 << CP0C1_CA) | (1 << CP0C1_EP))
#ifdef MIPS_USES_FPU
#define MIPS_CONFIG1  (MIPS_CONFIG1_1 | (1 << CP0C1_FP))
#else
#define MIPS_CONFIG1  (MIPS_CONFIG1_1 | (0 << CP0C1_FP))
#endif
/* Have config3, no tertiary/secondary caches implemented */
#define MIPS_CONFIG2                                              \
((1 << CP0C2_M))
/* No config4, no DSP ASE, no large physaddr,
   no external interrupt controller, no vectored interupts,
   no 1kb pages, no MT ASE, no SmartMIPS ASE, no trace logic */
#define MIPS_CONFIG3                                              \
((0 << CP0C3_M) | (0 << CP0C3_DSPP) | (0 << CP0C3_LPA) |          \
 (0 << CP0C3_VEIC) | (0 << CP0C3_VInt) | (0 << CP0C3_SP) |        \
 (0 << CP0C3_MT) | (0 << CP0C3_SM) | (0 << CP0C3_TL))
#elif (MIPS_CPU == MIPS_R4Kp)
/* 32 bits target */
#undef MIPS_HAS_MIPS64
/* real pages are variable size... */
#define TARGET_PAGE_BITS 12
/* Uses MIPS R4Kx enhancements to MIPS32 architecture */
#define MIPS_USES_R4K_EXT
/* Uses MIPS R4Km FPM MMU model */
#define MIPS_USES_R4K_FPM
#else
#error "MIPS CPU not defined"
/* Reminder for other flags */
//#undef MIPS_HAS_MIPS64
//#define MIPS_USES_FPU
#endif

#ifdef MIPS_HAS_MIPS64
#define TARGET_LONG_BITS 64
#else
#define TARGET_LONG_BITS 32
#endif

#endif /* !defined (__QEMU_MIPS_DEFS_H__) */
