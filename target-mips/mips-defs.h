#if !defined (__QEMU_MIPS_DEFS_H__)
#define __QEMU_MIPS_DEFS_H__

/* If we want to use host float regs... */
//#define USE_HOST_FLOAT_REGS

/* Real pages are variable size... */
#define TARGET_PAGE_BITS 12
#define MIPS_TLB_MAX 128

#if defined(TARGET_MIPS64)
#define TARGET_LONG_BITS 64
#define TARGET_PHYS_ADDR_SPACE_BITS 48
#define TARGET_VIRT_ADDR_SPACE_BITS 48
#else
#define TARGET_LONG_BITS 32
#define TARGET_PHYS_ADDR_SPACE_BITS 40
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif

/* Masks used to mark instructions to indicate which ISA level they
   were introduced in. */
#define		ISA_MIPS1	0x00000001
#define		ISA_MIPS2	0x00000002
#define		ISA_MIPS3	0x00000004
#define		ISA_MIPS4	0x00000008
#define		ISA_MIPS5	0x00000010
#define		ISA_MIPS32	0x00000020
#define		ISA_MIPS32R2	0x00000040
#define		ISA_MIPS64	0x00000080
#define		ISA_MIPS64R2	0x00000100
#define   ISA_MIPS32R3  0x00000200
#define   ISA_MIPS64R3  0x00000400
#define   ISA_MIPS32R5  0x00000800
#define   ISA_MIPS64R5  0x00001000
#define   ISA_MIPS32R6  0x00002000
#define   ISA_MIPS64R6  0x00004000

/* MIPS ASEs. */
#define   ASE_MIPS16    0x00010000
#define   ASE_MIPS3D    0x00020000
#define   ASE_MDMX      0x00040000
#define   ASE_DSP       0x00080000
#define   ASE_DSPR2     0x00100000
#define   ASE_MT        0x00200000
#define   ASE_SMARTMIPS 0x00400000
#define   ASE_MICROMIPS 0x00800000
#define   ASE_MSA       0x01000000

/* Chip specific instructions. */
#define		INSN_LOONGSON2E  0x20000000
#define		INSN_LOONGSON2F  0x40000000
#define		INSN_VR54XX	0x80000000

/* MIPS CPU defines. */
#define		CPU_MIPS1	(ISA_MIPS1)
#define		CPU_MIPS2	(CPU_MIPS1 | ISA_MIPS2)
#define		CPU_MIPS3	(CPU_MIPS2 | ISA_MIPS3)
#define		CPU_MIPS4	(CPU_MIPS3 | ISA_MIPS4)
#define		CPU_VR54XX	(CPU_MIPS4 | INSN_VR54XX)
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

/* Strictly follow the architecture standard:
   - Disallow "special" instruction handling for PMON/SPIM.
   Note that we still maintain Count/Compare to match the host clock. */
//#define MIPS_STRICT_STANDARD 1

#endif /* !defined (__QEMU_MIPS_DEFS_H__) */
