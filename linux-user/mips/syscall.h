
/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct target_pt_regs {
#if 1
	/* Pad bytes for argument save space on the stack. */
	target_ulong pad0[6];
#endif

	/* Saved main processor registers. */
	target_ulong regs[32];

	/* Saved special registers. */
	target_ulong cp0_status;
	target_ulong lo;
	target_ulong hi;
	target_ulong cp0_badvaddr;
	target_ulong cp0_cause;
	target_ulong cp0_epc;
};

#define UNAME_MACHINE "mips"
