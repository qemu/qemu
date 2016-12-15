#ifndef QEMU_H
#define QEMU_H

#include "hostdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

#undef DEBUG_REMAP
#ifdef DEBUG_REMAP
#endif /* DEBUG_REMAP */

#include "exec/user/abitypes.h"

#include "exec/user/thunk.h"
#include "syscall_defs.h"
#include "target_syscall.h"
#include "exec/gdbstub.h"
#include "qemu/queue.h"

#define THREAD __thread

/* This is the size of the host kernel's sigset_t, needed where we make
 * direct system calls that take a sigset_t pointer and a size.
 */
#define SIGSET_T_SIZE (_NSIG / 8)

/* This struct is used to hold certain information about the image.
 * Basically, it replicates in user space what would be certain
 * task_struct fields in the kernel
 */
struct image_info {
        abi_ulong       load_bias;
        abi_ulong       load_addr;
        abi_ulong       start_code;
        abi_ulong       end_code;
        abi_ulong       start_data;
        abi_ulong       end_data;
        abi_ulong       start_brk;
        abi_ulong       brk;
        abi_ulong       start_mmap;
        abi_ulong       start_stack;
        abi_ulong       stack_limit;
        abi_ulong       entry;
        abi_ulong       code_offset;
        abi_ulong       data_offset;
        abi_ulong       saved_auxv;
        abi_ulong       auxv_len;
        abi_ulong       arg_start;
        abi_ulong       arg_end;
        abi_ulong       arg_strings;
        abi_ulong       env_strings;
        abi_ulong       file_string;
        uint32_t        elf_flags;
	int		personality;
#ifdef CONFIG_USE_FDPIC
        abi_ulong       loadmap_addr;
        uint16_t        nsegs;
        void           *loadsegs;
        abi_ulong       pt_dynamic_addr;
        struct image_info *other_info;
#endif
};

#ifdef TARGET_I386
/* Information about the current linux thread */
struct vm86_saved_state {
    uint32_t eax; /* return code */
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eflags;
    uint32_t eip;
    uint16_t cs, ss, ds, es, fs, gs;
};
#endif

#if defined(TARGET_ARM) && defined(TARGET_ABI32)
/* FPU emulator */
#include "nwfpe/fpa11.h"
#endif

#define MAX_SIGQUEUE_SIZE 1024

struct emulated_sigtable {
    int pending; /* true if signal is pending */
    target_siginfo_t info;
};

/* NOTE: we force a big alignment so that the stack stored after is
   aligned too */
typedef struct TaskState {
    pid_t ts_tid;     /* tid (or pid) of this task */
#ifdef TARGET_ARM
# ifdef TARGET_ABI32
    /* FPA state */
    FPA11 fpa;
# endif
    int swi_errno;
#endif
#ifdef TARGET_UNICORE32
    int swi_errno;
#endif
#if defined(TARGET_I386) && !defined(TARGET_X86_64)
    abi_ulong target_v86;
    struct vm86_saved_state vm86_saved_regs;
    struct target_vm86plus_struct vm86plus;
    uint32_t v86flags;
    uint32_t v86mask;
#endif
    abi_ulong child_tidptr;
#ifdef TARGET_M68K
    int sim_syscalls;
    abi_ulong tp_value;
#endif
#if defined(TARGET_ARM) || defined(TARGET_M68K) || defined(TARGET_UNICORE32)
    /* Extra fields for semihosted binaries.  */
    abi_ulong heap_base;
    abi_ulong heap_limit;
#endif
    abi_ulong stack_base;
    int used; /* non zero if used */
    struct image_info *info;
    struct linux_binprm *bprm;

    struct emulated_sigtable sync_signal;
    struct emulated_sigtable sigtab[TARGET_NSIG];
    /* This thread's signal mask, as requested by the guest program.
     * The actual signal mask of this thread may differ:
     *  + we don't let SIGSEGV and SIGBUS be blocked while running guest code
     *  + sometimes we block all signals to avoid races
     */
    sigset_t signal_mask;
    /* The signal mask imposed by a guest sigsuspend syscall, if we are
     * currently in the middle of such a syscall
     */
    sigset_t sigsuspend_mask;
    /* Nonzero if we're leaving a sigsuspend and sigsuspend_mask is valid. */
    int in_sigsuspend;

    /* Nonzero if process_pending_signals() needs to do something (either
     * handle a pending signal or unblock signals).
     * This flag is written from a signal handler so should be accessed via
     * the atomic_read() and atomic_write() functions. (It is not accessed
     * from multiple threads.)
     */
    int signal_pending;

} __attribute__((aligned(16))) TaskState;

extern char *exec_path;
void init_task_state(TaskState *ts);
void task_settid(TaskState *);
void stop_all_tasks(void);
extern const char *qemu_uname_release;
extern unsigned long mmap_min_addr;

/* ??? See if we can avoid exposing so much of the loader internals.  */

/* Read a good amount of data initially, to hopefully get all the
   program headers loaded.  */
#define BPRM_BUF_SIZE  1024

/*
 * This structure is used to hold the arguments that are
 * used when loading binaries.
 */
struct linux_binprm {
        char buf[BPRM_BUF_SIZE] __attribute__((aligned));
        abi_ulong p;
	int fd;
        int e_uid, e_gid;
        int argc, envc;
        char **argv;
        char **envp;
        char * filename;        /* Name of binary */
        int (*core_dump)(int, const CPUArchState *); /* coredump routine */
};

void do_init_thread(struct target_pt_regs *regs, struct image_info *infop);
abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp, int push_ptr);
int loader_exec(int fdexec, const char *filename, char **argv, char **envp,
             struct target_pt_regs * regs, struct image_info *infop,
             struct linux_binprm *);

int load_elf_binary(struct linux_binprm *bprm, struct image_info *info);
int load_flt_binary(struct linux_binprm *bprm, struct image_info *info);

abi_long memcpy_to_target(abi_ulong dest, const void *src,
                          unsigned long len);
void target_set_brk(abi_ulong new_brk);
abi_long do_brk(abi_ulong new_brk);
void syscall_init(void);
abi_long do_syscall(void *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6, abi_long arg7,
                    abi_long arg8);
void gemu_log(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
extern THREAD CPUState *thread_cpu;
void cpu_loop(CPUArchState *env);
const char *target_strerror(int err);
int get_osversion(void);
void init_qemu_uname_release(void);
void fork_start(void);
void fork_end(int child);

/* Creates the initial guest address space in the host memory space using
 * the given host start address hint and size.  The guest_start parameter
 * specifies the start address of the guest space.  guest_base will be the
 * difference between the host start address computed by this function and
 * guest_start.  If fixed is specified, then the mapped address space must
 * start at host_start.  The real start address of the mapped memory space is
 * returned or -1 if there was an error.
 */
unsigned long init_guest_space(unsigned long host_start,
                               unsigned long host_size,
                               unsigned long guest_start,
                               bool fixed);

#include "qemu/log.h"

/* safe_syscall.S */

/**
 * safe_syscall:
 * @int number: number of system call to make
 * ...: arguments to the system call
 *
 * Call a system call if guest signal not pending.
 * This has the same API as the libc syscall() function, except that it
 * may return -1 with errno == TARGET_ERESTARTSYS if a signal was pending.
 *
 * Returns: the system call result, or -1 with an error code in errno
 * (Errnos are host errnos; we rely on TARGET_ERESTARTSYS not clashing
 * with any of the host errno values.)
 */

/* A guide to using safe_syscall() to handle interactions between guest
 * syscalls and guest signals:
 *
 * Guest syscalls come in two flavours:
 *
 * (1) Non-interruptible syscalls
 *
 * These are guest syscalls that never get interrupted by signals and
 * so never return EINTR. They can be implemented straightforwardly in
 * QEMU: just make sure that if the implementation code has to make any
 * blocking calls that those calls are retried if they return EINTR.
 * It's also OK to implement these with safe_syscall, though it will be
 * a little less efficient if a signal is delivered at the 'wrong' moment.
 *
 * Some non-interruptible syscalls need to be handled using block_signals()
 * to block signals for the duration of the syscall. This mainly applies
 * to code which needs to modify the data structures used by the
 * host_signal_handler() function and the functions it calls, including
 * all syscalls which change the thread's signal mask.
 *
 * (2) Interruptible syscalls
 *
 * These are guest syscalls that can be interrupted by signals and
 * for which we need to either return EINTR or arrange for the guest
 * syscall to be restarted. This category includes both syscalls which
 * always restart (and in the kernel return -ERESTARTNOINTR), ones
 * which only restart if there is no handler (kernel returns -ERESTARTNOHAND
 * or -ERESTART_RESTARTBLOCK), and the most common kind which restart
 * if the handler was registered with SA_RESTART (kernel returns
 * -ERESTARTSYS). System calls which are only interruptible in some
 * situations (like 'open') also need to be handled this way.
 *
 * Here it is important that the host syscall is made
 * via this safe_syscall() function, and *not* via the host libc.
 * If the host libc is used then the implementation will appear to work
 * most of the time, but there will be a race condition where a
 * signal could arrive just before we make the host syscall inside libc,
 * and then then guest syscall will not correctly be interrupted.
 * Instead the implementation of the guest syscall can use the safe_syscall
 * function but otherwise just return the result or errno in the usual
 * way; the main loop code will take care of restarting the syscall
 * if appropriate.
 *
 * (If the implementation needs to make multiple host syscalls this is
 * OK; any which might really block must be via safe_syscall(); for those
 * which are only technically blocking (ie which we know in practice won't
 * stay in the host kernel indefinitely) it's OK to use libc if necessary.
 * You must be able to cope with backing out correctly if some safe_syscall
 * you make in the implementation returns either -TARGET_ERESTARTSYS or
 * EINTR though.)
 *
 * block_signals() cannot be used for interruptible syscalls.
 *
 *
 * How and why the safe_syscall implementation works:
 *
 * The basic setup is that we make the host syscall via a known
 * section of host native assembly. If a signal occurs, our signal
 * handler checks the interrupted host PC against the addresse of that
 * known section. If the PC is before or at the address of the syscall
 * instruction then we change the PC to point at a "return
 * -TARGET_ERESTARTSYS" code path instead, and then exit the signal handler
 * (causing the safe_syscall() call to immediately return that value).
 * Then in the main.c loop if we see this magic return value we adjust
 * the guest PC to wind it back to before the system call, and invoke
 * the guest signal handler as usual.
 *
 * This winding-back will happen in two cases:
 * (1) signal came in just before we took the host syscall (a race);
 *   in this case we'll take the guest signal and have another go
 *   at the syscall afterwards, and this is indistinguishable for the
 *   guest from the timing having been different such that the guest
 *   signal really did win the race
 * (2) signal came in while the host syscall was blocking, and the
 *   host kernel decided the syscall should be restarted;
 *   in this case we want to restart the guest syscall also, and so
 *   rewinding is the right thing. (Note that "restart" semantics mean
 *   "first call the signal handler, then reattempt the syscall".)
 * The other situation to consider is when a signal came in while the
 * host syscall was blocking, and the host kernel decided that the syscall
 * should not be restarted; in this case QEMU's host signal handler will
 * be invoked with the PC pointing just after the syscall instruction,
 * with registers indicating an EINTR return; the special code in the
 * handler will not kick in, and we will return EINTR to the guest as
 * we should.
 *
 * Notice that we can leave the host kernel to make the decision for
 * us about whether to do a restart of the syscall or not; we do not
 * need to check SA_RESTART flags in QEMU or distinguish the various
 * kinds of restartability.
 */
#ifdef HAVE_SAFE_SYSCALL
/* The core part of this function is implemented in assembly */
extern long safe_syscall_base(int *pending, long number, ...);

#define safe_syscall(...)                                               \
    ({                                                                  \
        long ret_;                                                      \
        int *psp_ = &((TaskState *)thread_cpu->opaque)->signal_pending; \
        ret_ = safe_syscall_base(psp_, __VA_ARGS__);                    \
        if (is_error(ret_)) {                                           \
            errno = -ret_;                                              \
            ret_ = -1;                                                  \
        }                                                               \
        ret_;                                                           \
    })

#else

/* Fallback for architectures which don't yet provide a safe-syscall assembly
 * fragment; note that this is racy!
 * This should go away when all host architectures have been updated.
 */
#define safe_syscall syscall

#endif

/* syscall.c */
int host_to_target_waitstatus(int status);

/* strace.c */
void print_syscall(int num,
                   abi_long arg1, abi_long arg2, abi_long arg3,
                   abi_long arg4, abi_long arg5, abi_long arg6);
void print_syscall_ret(int num, abi_long arg1);
/**
 * print_taken_signal:
 * @target_signum: target signal being taken
 * @tinfo: target_siginfo_t which will be passed to the guest for the signal
 *
 * Print strace output indicating that this signal is being taken by the guest,
 * in a format similar to:
 * --- SIGSEGV {si_signo=SIGSEGV, si_code=SI_KERNEL, si_addr=0} ---
 */
void print_taken_signal(int target_signum, const target_siginfo_t *tinfo);
extern int do_strace;

/* signal.c */
void process_pending_signals(CPUArchState *cpu_env);
void signal_init(void);
int queue_signal(CPUArchState *env, int sig, int si_type,
                 target_siginfo_t *info);
void host_to_target_siginfo(target_siginfo_t *tinfo, const siginfo_t *info);
void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo);
int target_to_host_signal(int sig);
int host_to_target_signal(int sig);
long do_sigreturn(CPUArchState *env);
long do_rt_sigreturn(CPUArchState *env);
abi_long do_sigaltstack(abi_ulong uss_addr, abi_ulong uoss_addr, abi_ulong sp);
int do_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
/**
 * block_signals: block all signals while handling this guest syscall
 *
 * Block all signals, and arrange that the signal mask is returned to
 * its correct value for the guest before we resume execution of guest code.
 * If this function returns non-zero, then the caller should immediately
 * return -TARGET_ERESTARTSYS to the main loop, which will take the pending
 * signal and restart execution of the syscall.
 * If block_signals() returns zero, then the caller can continue with
 * emulation of the system call knowing that no signals can be taken
 * (and therefore that no race conditions will result).
 * This should only be called once, because if it is called a second time
 * it will always return non-zero. (Think of it like a mutex that can't
 * be recursively locked.)
 * Signals will be unblocked again by process_pending_signals().
 *
 * Return value: non-zero if there was a pending signal, zero if not.
 */
int block_signals(void); /* Returns non zero if signal pending */

#ifdef TARGET_I386
/* vm86.c */
void save_v86_state(CPUX86State *env);
void handle_vm86_trap(CPUX86State *env, int trapno);
void handle_vm86_fault(CPUX86State *env);
int do_vm86(CPUX86State *env, long subfunction, abi_ulong v86_addr);
#elif defined(TARGET_SPARC64)
void sparc64_set_context(CPUSPARCState *env);
void sparc64_get_context(CPUSPARCState *env);
#endif

/* mmap.c */
int target_mprotect(abi_ulong start, abi_ulong len, int prot);
abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, abi_ulong offset);
int target_munmap(abi_ulong start, abi_ulong len);
abi_long target_mremap(abi_ulong old_addr, abi_ulong old_size,
                       abi_ulong new_size, unsigned long flags,
                       abi_ulong new_addr);
int target_msync(abi_ulong start, abi_ulong len, int flags);
extern unsigned long last_brk;
extern abi_ulong mmap_next_start;
abi_ulong mmap_find_vma(abi_ulong, abi_ulong);
void mmap_fork_start(void);
void mmap_fork_end(int child);

/* main.c */
extern unsigned long guest_stack_size;

/* user access */

#define VERIFY_READ 0
#define VERIFY_WRITE 1 /* implies read access */

static inline int access_ok(int type, abi_ulong addr, abi_ulong size)
{
    return page_check_range((target_ulong)addr, size,
                            (type == VERIFY_READ) ? PAGE_READ : (PAGE_READ | PAGE_WRITE)) == 0;
}

/* NOTE __get_user and __put_user use host pointers and don't check access.
   These are usually used to access struct data members once the struct has
   been locked - usually with lock_user_struct.  */

/* Tricky points:
   - Use __builtin_choose_expr to avoid type promotion from ?:,
   - Invalid sizes result in a compile time error stemming from
     the fact that abort has no parameters.
   - It's easier to use the endian-specific unaligned load/store
     functions than host-endian unaligned load/store plus tswapN.  */

#define __put_user_e(x, hptr, e)                                        \
  (__builtin_choose_expr(sizeof(*(hptr)) == 1, stb_p,                   \
   __builtin_choose_expr(sizeof(*(hptr)) == 2, stw_##e##_p,             \
   __builtin_choose_expr(sizeof(*(hptr)) == 4, stl_##e##_p,             \
   __builtin_choose_expr(sizeof(*(hptr)) == 8, stq_##e##_p, abort))))   \
     ((hptr), (x)), (void)0)

#define __get_user_e(x, hptr, e)                                        \
  ((x) = (typeof(*hptr))(                                               \
   __builtin_choose_expr(sizeof(*(hptr)) == 1, ldub_p,                  \
   __builtin_choose_expr(sizeof(*(hptr)) == 2, lduw_##e##_p,            \
   __builtin_choose_expr(sizeof(*(hptr)) == 4, ldl_##e##_p,             \
   __builtin_choose_expr(sizeof(*(hptr)) == 8, ldq_##e##_p, abort))))   \
     (hptr)), (void)0)

#ifdef TARGET_WORDS_BIGENDIAN
# define __put_user(x, hptr)  __put_user_e(x, hptr, be)
# define __get_user(x, hptr)  __get_user_e(x, hptr, be)
#else
# define __put_user(x, hptr)  __put_user_e(x, hptr, le)
# define __get_user(x, hptr)  __get_user_e(x, hptr, le)
#endif

/* put_user()/get_user() take a guest address and check access */
/* These are usually used to access an atomic data type, such as an int,
 * that has been passed by address.  These internally perform locking
 * and unlocking on the data type.
 */
#define put_user(x, gaddr, target_type)					\
({									\
    abi_ulong __gaddr = (gaddr);					\
    target_type *__hptr;						\
    abi_long __ret = 0;							\
    if ((__hptr = lock_user(VERIFY_WRITE, __gaddr, sizeof(target_type), 0))) { \
        __put_user((x), __hptr);				\
        unlock_user(__hptr, __gaddr, sizeof(target_type));		\
    } else								\
        __ret = -TARGET_EFAULT;						\
    __ret;								\
})

#define get_user(x, gaddr, target_type)					\
({									\
    abi_ulong __gaddr = (gaddr);					\
    target_type *__hptr;						\
    abi_long __ret = 0;							\
    if ((__hptr = lock_user(VERIFY_READ, __gaddr, sizeof(target_type), 1))) { \
        __get_user((x), __hptr);				\
        unlock_user(__hptr, __gaddr, 0);				\
    } else {								\
        /* avoid warning */						\
        (x) = 0;							\
        __ret = -TARGET_EFAULT;						\
    }									\
    __ret;								\
})

#define put_user_ual(x, gaddr) put_user((x), (gaddr), abi_ulong)
#define put_user_sal(x, gaddr) put_user((x), (gaddr), abi_long)
#define put_user_u64(x, gaddr) put_user((x), (gaddr), uint64_t)
#define put_user_s64(x, gaddr) put_user((x), (gaddr), int64_t)
#define put_user_u32(x, gaddr) put_user((x), (gaddr), uint32_t)
#define put_user_s32(x, gaddr) put_user((x), (gaddr), int32_t)
#define put_user_u16(x, gaddr) put_user((x), (gaddr), uint16_t)
#define put_user_s16(x, gaddr) put_user((x), (gaddr), int16_t)
#define put_user_u8(x, gaddr)  put_user((x), (gaddr), uint8_t)
#define put_user_s8(x, gaddr)  put_user((x), (gaddr), int8_t)

#define get_user_ual(x, gaddr) get_user((x), (gaddr), abi_ulong)
#define get_user_sal(x, gaddr) get_user((x), (gaddr), abi_long)
#define get_user_u64(x, gaddr) get_user((x), (gaddr), uint64_t)
#define get_user_s64(x, gaddr) get_user((x), (gaddr), int64_t)
#define get_user_u32(x, gaddr) get_user((x), (gaddr), uint32_t)
#define get_user_s32(x, gaddr) get_user((x), (gaddr), int32_t)
#define get_user_u16(x, gaddr) get_user((x), (gaddr), uint16_t)
#define get_user_s16(x, gaddr) get_user((x), (gaddr), int16_t)
#define get_user_u8(x, gaddr)  get_user((x), (gaddr), uint8_t)
#define get_user_s8(x, gaddr)  get_user((x), (gaddr), int8_t)

/* copy_from_user() and copy_to_user() are usually used to copy data
 * buffers between the target and host.  These internally perform
 * locking/unlocking of the memory.
 */
abi_long copy_from_user(void *hptr, abi_ulong gaddr, size_t len);
abi_long copy_to_user(abi_ulong gaddr, void *hptr, size_t len);

/* Functions for accessing guest memory.  The tget and tput functions
   read/write single values, byteswapping as necessary.  The lock_user function
   gets a pointer to a contiguous area of guest memory, but does not perform
   any byteswapping.  lock_user may return either a pointer to the guest
   memory, or a temporary buffer.  */

/* Lock an area of guest memory into the host.  If copy is true then the
   host area will have the same contents as the guest.  */
static inline void *lock_user(int type, abi_ulong guest_addr, long len, int copy)
{
    if (!access_ok(type, guest_addr, len))
        return NULL;
#ifdef DEBUG_REMAP
    {
        void *addr;
        addr = g_malloc(len);
        if (copy)
            memcpy(addr, g2h(guest_addr), len);
        else
            memset(addr, 0, len);
        return addr;
    }
#else
    return g2h(guest_addr);
#endif
}

/* Unlock an area of guest memory.  The first LEN bytes must be
   flushed back to guest memory. host_ptr = NULL is explicitly
   allowed and does nothing. */
static inline void unlock_user(void *host_ptr, abi_ulong guest_addr,
                               long len)
{

#ifdef DEBUG_REMAP
    if (!host_ptr)
        return;
    if (host_ptr == g2h(guest_addr))
        return;
    if (len > 0)
        memcpy(g2h(guest_addr), host_ptr, len);
    g_free(host_ptr);
#endif
}

/* Return the length of a string in target memory or -TARGET_EFAULT if
   access error. */
abi_long target_strlen(abi_ulong gaddr);

/* Like lock_user but for null terminated strings.  */
static inline void *lock_user_string(abi_ulong guest_addr)
{
    abi_long len;
    len = target_strlen(guest_addr);
    if (len < 0)
        return NULL;
    return lock_user(VERIFY_READ, guest_addr, (long)(len + 1), 1);
}

/* Helper macros for locking/unlocking a target struct.  */
#define lock_user_struct(type, host_ptr, guest_addr, copy)	\
    (host_ptr = lock_user(type, guest_addr, sizeof(*host_ptr), copy))
#define unlock_user_struct(host_ptr, guest_addr, copy)		\
    unlock_user(host_ptr, guest_addr, (copy) ? sizeof(*host_ptr) : 0)

#include <pthread.h>

/* Include target-specific struct and function definitions;
 * they may need access to the target-independent structures
 * above, so include them last.
 */
#include "target_cpu.h"
#include "target_signal.h"
#include "target_structs.h"

#endif /* QEMU_H */
