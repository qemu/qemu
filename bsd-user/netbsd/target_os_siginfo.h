#ifndef TARGET_OS_SIGINFO_H
#define TARGET_OS_SIGINFO_H

#define TARGET_NSIG     32  /* counting 0; could be 33 (mask is 1-32) */
#define TARGET_NSIG_BPW     (sizeof(uint32_t) * 8)
#define TARGET_NSIG_WORDS   (TARGET_NSIG / TARGET_NSIG_BPW)

/* this struct defines a stack used during syscall handling */
typedef struct target_sigaltstack {
    abi_long    ss_sp;
    abi_ulong   ss_size;
    abi_long    ss_flags;
} target_stack_t;

typedef struct {
    uint32_t __bits[TARGET_NSIG_WORDS];
} target_sigset_t

struct target_sigaction {
    abi_ulong   _sa_handler;
    int32_t     sa_flags;
    target_sigset_t sa_mask;
};

/* Compare to sys/siginfo.h */
typedef union target_sigval {
    int         sival_int;
    abi_ulong   sival_ptr;
} target_sigval_t;

struct target_ksiginfo {
    int32_t     _signo;
    int32_t     _code;
    int32_t     _errno;
#if TARGET_ABI_BITS == 64
    int32_t     _pad;
#endif
    union {
        struct {
            int32_t             _pid;
            int32_t             _uid;
            target_sigval_t    _value;
        } _rt;

        struct {
            int32_t             _pid;
            int32_t             _uid;
            int32_t             _struct;
            /* clock_t          _utime; */
            /* clock_t          _stime; */
        } _child;

        struct {
            abi_ulong           _addr;
            int32_t             _trap;
        } _fault;

        struct {
            long                _band;
            int                 _fd;
        } _poll;
    } _reason;
};

typedef union target_siginfo {
    int8_t     si_pad[128];
    struct     target_ksiginfo  _info;
} target_siginfo_t;

#define target_si_signo     _info._signo
#define target_si_code      _info._code
#define target_si_errno     _info._errno
#define target_si_addr      _info._reason._fault._addr

#define TARGET_SEGV_MAPERR  1
#define TARGET_SEGV_ACCERR  2

#define TARGET_TRAP_BRKPT   1
#define TARGET_TRAP_TRACE   2


#endif /* TARGET_OS_SIGINFO_H */
