#ifndef ALPHA_TARGET_SYSCALL_H
#define ALPHA_TARGET_SYSCALL_H

/* default linux values for the selectors */
#define __USER_DS	(1)

struct target_pt_regs {
	abi_ulong r0;
	abi_ulong r1;
	abi_ulong r2;
	abi_ulong r3;
	abi_ulong r4;
	abi_ulong r5;
	abi_ulong r6;
	abi_ulong r7;
	abi_ulong r8;
	abi_ulong r19;
	abi_ulong r20;
	abi_ulong r21;
	abi_ulong r22;
	abi_ulong r23;
	abi_ulong r24;
	abi_ulong r25;
	abi_ulong r26;
	abi_ulong r27;
	abi_ulong r28;
	abi_ulong hae;
/* JRP - These are the values provided to a0-a2 by PALcode */
	abi_ulong trap_a0;
	abi_ulong trap_a1;
	abi_ulong trap_a2;
/* These are saved by PAL-code: */
	abi_ulong ps;
	abi_ulong pc;
	abi_ulong gp;
	abi_ulong r16;
	abi_ulong r17;
	abi_ulong r18;
/* Those is needed by qemu to temporary store the user stack pointer */
        abi_ulong usp;
        abi_ulong unique;
};

#define UNAME_MACHINE "alpha"
#define UNAME_MINIMUM_RELEASE "2.6.32"

// For sys_osf_getsysinfo
#define TARGET_GSI_UACPROC		8
#define TARGET_GSI_IEEE_FP_CONTROL	45
#define TARGET_GSI_IEEE_STATE_AT_SIGNAL	46
#define TARGET_GSI_PROC_TYPE		60
#define TARGET_GSI_GET_HWRPB		101

// For sys_ofs_setsysinfo
#define TARGET_SSI_NVPAIRS		1
#define TARGET_SSI_IEEE_FP_CONTROL	14
#define TARGET_SSI_IEEE_STATE_AT_SIGNAL	15
#define TARGET_SSI_IEEE_IGNORE_STATE_AT_SIGNAL 16
#define TARGET_SSI_IEEE_RAISE_EXCEPTION	1001

#define TARGET_SSIN_UACPROC		6

#define TARGET_UAC_NOPRINT		1
#define TARGET_UAC_NOFIX		2
#define TARGET_UAC_SIGBUS		4
#define TARGET_MINSIGSTKSZ              4096
#define TARGET_MCL_CURRENT     0x2000
#define TARGET_MCL_FUTURE      0x4000
#define TARGET_MCL_ONFAULT     0x8000

#endif /* ALPHA_TARGET_SYSCALL_H */
