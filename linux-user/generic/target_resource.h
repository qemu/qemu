/*
 * Target definitions of RLIMIT_* constants. These may be overridden by an
 * architecture specific header if needed.
 */

#ifndef GENERIC_TARGET_RESOURCE_H
#define GENERIC_TARGET_RESOURCE_H

struct target_rlimit {
    abi_ulong rlim_cur;
    abi_ulong rlim_max;
};

struct target_rlimit64 {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

#define TARGET_RLIM_INFINITY    ((abi_ulong)-1)

#define TARGET_RLIMIT_CPU               0
#define TARGET_RLIMIT_FSIZE             1
#define TARGET_RLIMIT_DATA              2
#define TARGET_RLIMIT_STACK             3
#define TARGET_RLIMIT_CORE              4
#define TARGET_RLIMIT_RSS               5
#define TARGET_RLIMIT_NPROC             6
#define TARGET_RLIMIT_NOFILE            7
#define TARGET_RLIMIT_MEMLOCK           8
#define TARGET_RLIMIT_AS                9
#define TARGET_RLIMIT_LOCKS             10
#define TARGET_RLIMIT_SIGPENDING        11
#define TARGET_RLIMIT_MSGQUEUE          12
#define TARGET_RLIMIT_NICE              13
#define TARGET_RLIMIT_RTPRIO            14
#define TARGET_RLIMIT_RTTIME            15

#endif
