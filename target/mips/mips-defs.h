#ifndef QEMU_MIPS_DEFS_H
#define QEMU_MIPS_DEFS_H

/* If we want to use host float regs... */
//#define USE_HOST_FLOAT_REGS

/* Real pages are variable size... */
#define MIPS_TLB_MAX 128

/*
 * bit definitions for insn_flags (ISAs/ASEs flags)
 * ------------------------------------------------
 */
/*
 *   bits 0-31: MIPS base instruction sets
 */
#define ISA_MIPS1         0x0000000000000001ULL
#define ISA_MIPS2         0x0000000000000002ULL
#define ISA_MIPS3         0x0000000000000004ULL
#define ISA_MIPS4         0x0000000000000008ULL
#define ISA_MIPS5         0x0000000000000010ULL
#define ISA_MIPS32        0x0000000000000020ULL
#define ISA_MIPS32R2      0x0000000000000040ULL
#define ISA_MIPS64        0x0000000000000080ULL
#define ISA_MIPS64R2      0x0000000000000100ULL
#define ISA_MIPS32R3      0x0000000000000200ULL
#define ISA_MIPS64R3      0x0000000000000400ULL
#define ISA_MIPS32R5      0x0000000000000800ULL
#define ISA_MIPS64R5      0x0000000000001000ULL
#define ISA_MIPS32R6      0x0000000000002000ULL
#define ISA_MIPS64R6      0x0000000000004000ULL
#define ISA_NANOMIPS32    0x0000000000008000ULL
/*
 *   bits 32-47: MIPS ASEs
 */
#define ASE_MIPS16        0x0000000100000000ULL
#define ASE_MIPS3D        0x0000000200000000ULL
#define ASE_MDMX          0x0000000400000000ULL
#define ASE_DSP           0x0000000800000000ULL
#define ASE_DSP_R2        0x0000001000000000ULL
#define ASE_DSP_R3        0x0000002000000000ULL
#define ASE_MT            0x0000004000000000ULL
#define ASE_SMARTMIPS     0x0000008000000000ULL
#define ASE_MICROMIPS     0x0000010000000000ULL
#define ASE_MSA           0x0000020000000000ULL
/*
 *   bits 48-55: vendor-specific base instruction sets
 */
#define INSN_LOONGSON2E   0x0001000000000000ULL
#define INSN_LOONGSON2F   0x0002000000000000ULL
#define INSN_VR54XX       0x0004000000000000ULL
#define INSN_R5900        0x0008000000000000ULL
/*
 *   bits 56-63: vendor-specific ASEs
 */
#define ASE_MMI           0x0100000000000000ULL
#define ASE_MXU           0x0200000000000000ULL

/* MIPS CPU defines. */
#define		CPU_MIPS1	(ISA_MIPS1)
#define		CPU_MIPS2	(CPU_MIPS1 | ISA_MIPS2)
#define		CPU_MIPS3	(CPU_MIPS2 | ISA_MIPS3)
#define		CPU_MIPS4	(CPU_MIPS3 | ISA_MIPS4)
#define		CPU_VR54XX	(CPU_MIPS4 | INSN_VR54XX)
#define         CPU_R5900       (CPU_MIPS3 | INSN_R5900)
#define		CPU_LOONGSON2E  (CPU_MIPS3 | INSN_LOONGSON2E)
#define		CPU_LOONGSON2F  (CPU_MIPS3 | INSN_LOONGSON2F)

#define		CPU_MIPS5	(CPU_MIPS4 | ISA_MIPS5)

/* MIPS Technologies "Release 1" */
#define		CPU_MIPS32	(CPU_MIPS2 | ISA_MIPS32)
#define		CPU_MIPS64	(CPU_MIPS5 | CPU_MIPS32 | ISA_MIPS64)

/* MIPS Technologies "Release 2" */
#define		CPU_MIPS32R2	(CPU_MIPS32 | ISA_MIPS32R2)
#define		CPU_MIPS64R2	(CPU_MIPS64 | CPU_MIPS32R2 | ISA_MIPS64R2)

/* MIPS Technologies "Release 3" */
#define CPU_MIPS32R3 (CPU_MIPS32R2 | ISA_MIPS32R3)
#define CPU_MIPS64R3 (CPU_MIPS64R2 | CPU_MIPS32R3 | ISA_MIPS64R3)

/* MIPS Technologies "Release 5" */
#define CPU_MIPS32R5 (CPU_MIPS32R3 | ISA_MIPS32R5)
#define CPU_MIPS64R5 (CPU_MIPS64R3 | CPU_MIPS32R5 | ISA_MIPS64R5)

/* MIPS Technologies "Release 6" */
#define CPU_MIPS32R6 (CPU_MIPS32R5 | ISA_MIPS32R6)
#define CPU_MIPS64R6 (CPU_MIPS64R5 | CPU_MIPS32R6 | ISA_MIPS64R6)

/* Wave Computing: "nanoMIPS" */
#define CPU_NANOMIPS32 (CPU_MIPS32R6 | ISA_NANOMIPS32)

/* Strictly follow the architecture standard:
   - Disallow "special" instruction handling for PMON/SPIM.
   Note that we still maintain Count/Compare to match the host clock. */
//#define MIPS_STRICT_STANDARD 1

#endif /* QEMU_MIPS_DEFS_H */
