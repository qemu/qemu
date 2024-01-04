/*
 * Adapted from arch/arm64/kernel/asm-offsets.c
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
	OFFSET(S_X0, pt_regs, regs[0]);
	OFFSET(S_X1, pt_regs, regs[1]);
	OFFSET(S_X2, pt_regs, regs[2]);
	OFFSET(S_X3, pt_regs, regs[3]);
	OFFSET(S_X4, pt_regs, regs[4]);
	OFFSET(S_X5, pt_regs, regs[5]);
	OFFSET(S_X6, pt_regs, regs[6]);
	OFFSET(S_X7, pt_regs, regs[7]);
	OFFSET(S_LR, pt_regs, regs[30]);
	OFFSET(S_SP, pt_regs, sp);
	OFFSET(S_PC, pt_regs, pc);
	OFFSET(S_PSTATE, pt_regs, pstate);
	OFFSET(S_ORIG_X0, pt_regs, orig_x0);
	OFFSET(S_SYSCALLNO, pt_regs, syscallno);
	DEFINE(S_FRAME_SIZE, sizeof(struct pt_regs));
	return 0;
}
