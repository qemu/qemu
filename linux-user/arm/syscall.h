
/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct target_pt_regs {
    abi_long uregs[18];
};

#define ARM_cpsr	uregs[16]
#define ARM_pc		uregs[15]
#define ARM_lr		uregs[14]
#define ARM_sp		uregs[13]
#define ARM_ip		uregs[12]
#define ARM_fp		uregs[11]
#define ARM_r10		uregs[10]
#define ARM_r9		uregs[9]
#define ARM_r8		uregs[8]
#define ARM_r7		uregs[7]
#define ARM_r6		uregs[6]
#define ARM_r5		uregs[5]
#define ARM_r4		uregs[4]
#define ARM_r3		uregs[3]
#define ARM_r2		uregs[2]
#define ARM_r1		uregs[1]
#define ARM_r0		uregs[0]
#define ARM_ORIG_r0	uregs[17]

#define ARM_SYSCALL_BASE	0x900000
#define ARM_THUMB_SYSCALL	0

#define ARM_NR_BASE	  0xf0000
#define ARM_NR_breakpoint (ARM_NR_BASE + 1)
#define ARM_NR_cacheflush (ARM_NR_BASE + 2)
#define ARM_NR_set_tls	  (ARM_NR_BASE + 5)

#define ARM_NR_semihosting	  0x123456
#define ARM_NR_thumb_semihosting  0xAB

#if defined(TARGET_WORDS_BIGENDIAN)
#define UNAME_MACHINE "armv5teb"
#else
#define UNAME_MACHINE "armv5tel"
#endif
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_CLONE_BACKWARDS
