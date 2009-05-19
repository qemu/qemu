#if !defined (__QEMU_MIPS_DEFS_H__)
#define __QEMU_MIPS_DEFS_H__

/* If we want to use host float regs... */
//#define USE_HOST_FLOAT_REGS

/* Real pages are variable size... */
#define TARGET_PAGE_BITS 12
#define MIPS_TLB_MAX 128

#if defined(TARGET_MIPS64)
#define TARGET_LONG_BITS 64
#else
#define TARGET_LONG_BITS 32
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

/* MIPS ASEs. */
#define		ASE_MIPS16	0x00001000
#define		ASE_MIPS3D	0x00002000
#define		ASE_MDMX	0x00004000
#define		ASE_DSP		0x00008000
#define		ASE_DSPR2	0x00010000
#define		ASE_MT		0x00020000
#define		ASE_SMARTMIPS	0x00040000

/* Chip specific instructions. */
#define		INSN_VR54XX	0x80000000

/* MIPS CPU defines. */
#define		CPU_MIPS1	(ISA_MIPS1)
#define		CPU_MIPS2	(CPU_MIPS1 | ISA_MIPS2)
#define		CPU_MIPS3	(CPU_MIPS2 | ISA_MIPS3)
#define		CPU_MIPS4	(CPU_MIPS3 | ISA_MIPS4)
#define		CPU_VR54XX	(CPU_MIPS4 | INSN_VR54XX)

#define		CPU_MIPS5	(CPU_MIPS4 | ISA_MIPS5)

/* MIPS Technologies "Release 1" */
#define		CPU_MIPS32	(CPU_MIPS2 | ISA_MIPS32)
#define		CPU_MIPS64	(CPU_MIPS5 | CPU_MIPS32 | ISA_MIPS64)

/* MIPS Technologies "Release 2" */
#define		CPU_MIPS32R2	(CPU_MIPS32 | ISA_MIPS32R2)
#define		CPU_MIPS64R2	(CPU_MIPS64 | CPU_MIPS32R2 | ISA_MIPS64R2)

/* Strictly follow the architecture standard:
   - Disallow "special" instruction handling for PMON/SPIM.
   Note that we still maintain Count/Compare to match the host clock. */
//#define MIPS_STRICT_STANDARD 1

#endif /* !defined (__QEMU_MIPS_DEFS_H__) */
