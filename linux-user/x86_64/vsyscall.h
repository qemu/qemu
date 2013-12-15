#ifndef VSYSCALL_H
#define VSYSCALL_H

/* This is based on asm/syscall.h in kernel 2.6.29. */

enum vsyscall_num {
    __NR_vgettimeofday,
    __NR_vtime,
    __NR_vgetcpu,
};

#define TARGET_VSYSCALL_START (-10UL << 20)
#define TARGET_VSYSCALL_SIZE 1024
#define TARGET_VSYSCALL_END (-2UL << 20)
#define TARGET_VSYSCALL_MAPPED_PAGES 1
#define TARGET_VSYSCALL_ADDR(vsyscall_nr) \
    (TARGET_VSYSCALL_START+TARGET_VSYSCALL_SIZE*(vsyscall_nr))

#endif /* !VSYSCALL_H */
