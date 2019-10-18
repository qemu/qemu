/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef TARGET_SYSCALL_H
#define TARGET_SYSCALL_H

struct target_pt_regs {
    abi_long sepc;
    abi_long sp;
};

#define UNAME_MACHINE "hexagon"
#define UNAME_MINIMUM_RELEASE "4.15.0"

#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2

#endif
