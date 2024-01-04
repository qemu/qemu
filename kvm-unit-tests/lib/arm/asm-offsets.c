/*
 * Adapted from arch/arm/kernel/asm-offsets.c
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <kbuild.h>
#include <asm/ptrace.h>

int main(void)
{
	OFFSET(S_R0, pt_regs, ARM_r0);
	OFFSET(S_R1, pt_regs, ARM_r1);
	OFFSET(S_R2, pt_regs, ARM_r2);
	OFFSET(S_R3, pt_regs, ARM_r3);
	OFFSET(S_R4, pt_regs, ARM_r4);
	OFFSET(S_R5, pt_regs, ARM_r5);
	OFFSET(S_R6, pt_regs, ARM_r6);
	OFFSET(S_R7, pt_regs, ARM_r7);
	OFFSET(S_R8, pt_regs, ARM_r8);
	OFFSET(S_R9, pt_regs, ARM_r9);
	OFFSET(S_R10, pt_regs, ARM_r10);
	OFFSET(S_FP, pt_regs, ARM_fp);
	OFFSET(S_IP, pt_regs, ARM_ip);
	OFFSET(S_SP, pt_regs, ARM_sp);
	OFFSET(S_LR, pt_regs, ARM_lr);
	OFFSET(S_PC, pt_regs, ARM_pc);
	OFFSET(S_PSR, pt_regs, ARM_cpsr);
	OFFSET(S_OLD_R0, pt_regs, ARM_ORIG_r0);
	DEFINE(S_FRAME_SIZE, sizeof(struct pt_regs));
	return 0;
}
