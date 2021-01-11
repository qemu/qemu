#ifndef BSD_USER_ARCH_SYSCALL_H_
#define BSD_USER_ARCH_SYSCALL_H_

struct target_pt_regs {
    abi_long uregs[17];
};

#define ARM_cpsr    uregs[16]
#define ARM_pc      uregs[15]
#define ARM_lr      uregs[14]
#define ARM_sp      uregs[13]
#define ARM_ip      uregs[12]
#define ARM_fp      uregs[11]
#define ARM_r10     uregs[10]
#define ARM_r9      uregs[9]
#define ARM_r8      uregs[8]
#define ARM_r7      uregs[7]
#define ARM_r6      uregs[6]
#define ARM_r5      uregs[5]
#define ARM_r4      uregs[4]
#define ARM_r3      uregs[3]
#define ARM_r2      uregs[2]
#define ARM_r1      uregs[1]
#define ARM_r0      uregs[0]

#define ARM_SYSCALL_BASE    0 /* XXX: FreeBSD only */

#define TARGET_FREEBSD_ARM_SYNC_ICACHE      0
#define TARGET_FREEBSD_ARM_DRAIN_WRITEBUF   1
#define TARGET_FREEBSD_ARM_SET_TP       2
#define TARGET_FREEBSD_ARM_GET_TP       3

#define TARGET_HW_MACHINE       "arm"
#define TARGET_HW_MACHINE_ARCH  "armv6"

#endif /* !BSD_USER_ARCH_SYSCALL_H_ */
