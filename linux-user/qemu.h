#ifndef QEMU_H
#define QEMU_H

#include "cpu.h"
#include "exec/cpu_ldst.h"

#undef DEBUG_REMAP

#include "exec/user/abitypes.h"

#include "syscall_defs.h"
#include "target_syscall.h"

/*
 * This is the size of the host kernel's sigset_t, needed where we make
 * direct system calls that take a sigset_t pointer and a size.
 */
#define SIGSET_T_SIZE (_NSIG / 8)

/*
 * This struct is used to hold certain information about the image.
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
        abi_ulong       reserve_brk;
        abi_ulong       start_mmap;
        abi_ulong       start_stack;
        abi_ulong       stack_limit;
        abi_ulong       entry;
        abi_ulong       code_offset;
        abi_ulong       data_offset;
        abi_ulong       saved_auxv;
        abi_ulong       auxv_len;
        abi_ulong       argc;
        abi_ulong       argv;
        abi_ulong       envc;
        abi_ulong       envp;
        abi_ulong       file_string;
        uint32_t        elf_flags;
        int             personality;
        abi_ulong       alignment;
        bool            exec_stack;

        /* Generic semihosting knows about these pointers. */
        abi_ulong       arg_strings;   /* strings for argv */
        abi_ulong       env_strings;   /* strings for envp; ends arg_strings */

        /* The fields below are used in FDPIC mode.  */
        abi_ulong       loadmap_addr;
        uint16_t        nsegs;
        void            *loadsegs;
        abi_ulong       pt_dynamic_addr;
        abi_ulong       interpreter_loadmap_addr;
        abi_ulong       interpreter_pt_dynamic_addr;
        struct image_info *other_info;

        /* For target-specific processing of NT_GNU_PROPERTY_TYPE_0. */
        uint32_t        note_flags;

#ifdef TARGET_MIPS
        int             fp_abi;
        int             interp_fp_abi;
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

struct emulated_sigtable {
    int pending; /* true if signal is pending */
    target_siginfo_t info;
};

typedef struct TaskState {
    pid_t ts_tid;     /* tid (or pid) of this task */
#ifdef TARGET_ARM
# ifdef TARGET_ABI32
    /* FPA state */
    FPA11 fpa;
# endif
#endif
#if defined(TARGET_ARM) || defined(TARGET_RISCV)
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
    abi_ulong tp_value;
#endif
#if defined(TARGET_ARM) || defined(TARGET_M68K) || defined(TARGET_RISCV)
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
    /*
     * This thread's signal mask, as requested by the guest program.
     * The actual signal mask of this thread may differ:
     *  + we don't let SIGSEGV and SIGBUS be blocked while running guest code
     *  + sometimes we block all signals to avoid races
     */
    sigset_t signal_mask;
    /*
     * The signal mask imposed by a guest sigsuspend syscall, if we are
     * currently in the middle of such a syscall
     */
    sigset_t sigsuspend_mask;
    /* Nonzero if we're leaving a sigsuspend and sigsuspend_mask is valid. */
    int in_sigsuspend;

    /*
     * Nonzero if process_pending_signals() needs to do something (either
     * handle a pending signal or unblock signals).
     * This flag is written from a signal handler so should be accessed via
     * the qatomic_read() and qatomic_set() functions. (It is not accessed
     * from multiple threads.)
     */
    int signal_pending;

    /* This thread's sigaltstack, if it has one */
    struct target_sigaltstack sigaltstack_used;

    /* Start time of task after system boot in clock ticks */
    uint64_t start_boottime;
} TaskState;

abi_long do_brk(abi_ulong new_brk);

/* user access */

#define VERIFY_READ  PAGE_READ
#define VERIFY_WRITE (PAGE_READ | PAGE_WRITE)

static inline bool access_ok_untagged(int type, abi_ulong addr, abi_ulong size)
{
    if (size == 0
        ? !guest_addr_valid_untagged(addr)
        : !guest_range_valid_untagged(addr, size)) {
        return false;
    }
    return page_check_range((target_ulong)addr, size, type) == 0;
}

static inline bool access_ok(CPUState *cpu, int type,
                             abi_ulong addr, abi_ulong size)
{
    return access_ok_untagged(type, cpu_untagged_addr(cpu, addr), size);
}

/* NOTE __get_user and __put_user use host pointers and don't check access.
   These are usually used to access struct data members once the struct has
   been locked - usually with lock_user_struct.  */

/*
 * Tricky points:
 * - Use __builtin_choose_expr to avoid type promotion from ?:,
 * - Invalid sizes result in a compile time error stemming from
 *   the fact that abort has no parameters.
 * - It's easier to use the endian-specific unaligned load/store
 *   functions than host-endian unaligned load/store plus tswapN.
 * - The pragmas are necessary only to silence a clang false-positive
 *   warning: see https://bugs.llvm.org/show_bug.cgi?id=39113 .
 * - gcc has bugs in its _Pragma() support in some versions, eg
 *   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=83256 -- so we only
 *   include the warning-suppression pragmas for clang
 */
#if defined(__clang__) && __has_warning("-Waddress-of-packed-member")
#define PRAGMA_DISABLE_PACKED_WARNING                                   \
    _Pragma("GCC diagnostic push");                                     \
    _Pragma("GCC diagnostic ignored \"-Waddress-of-packed-member\"")

#define PRAGMA_REENABLE_PACKED_WARNING          \
    _Pragma("GCC diagnostic pop")

#else
#define PRAGMA_DISABLE_PACKED_WARNING
#define PRAGMA_REENABLE_PACKED_WARNING
#endif

#define __put_user_e(x, hptr, e)                                            \
    do {                                                                    \
        PRAGMA_DISABLE_PACKED_WARNING;                                      \
        (__builtin_choose_expr(sizeof(*(hptr)) == 1, stb_p,                 \
        __builtin_choose_expr(sizeof(*(hptr)) == 2, stw_##e##_p,            \
        __builtin_choose_expr(sizeof(*(hptr)) == 4, stl_##e##_p,            \
        __builtin_choose_expr(sizeof(*(hptr)) == 8, stq_##e##_p, abort))))  \
            ((hptr), (x)), (void)0);                                        \
        PRAGMA_REENABLE_PACKED_WARNING;                                     \
    } while (0)

#define __get_user_e(x, hptr, e)                                            \
    do {                                                                    \
        PRAGMA_DISABLE_PACKED_WARNING;                                      \
        ((x) = (typeof(*hptr))(                                             \
        __builtin_choose_expr(sizeof(*(hptr)) == 1, ldub_p,                 \
        __builtin_choose_expr(sizeof(*(hptr)) == 2, lduw_##e##_p,           \
        __builtin_choose_expr(sizeof(*(hptr)) == 4, ldl_##e##_p,            \
        __builtin_choose_expr(sizeof(*(hptr)) == 8, ldq_##e##_p, abort))))  \
            (hptr)), (void)0);                                              \
        PRAGMA_REENABLE_PACKED_WARNING;                                     \
    } while (0)


#if TARGET_BIG_ENDIAN
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
int copy_from_user(void *hptr, abi_ulong gaddr, ssize_t len);
int copy_to_user(abi_ulong gaddr, void *hptr, ssize_t len);

/* Functions for accessing guest memory.  The tget and tput functions
   read/write single values, byteswapping as necessary.  The lock_user function
   gets a pointer to a contiguous area of guest memory, but does not perform
   any byteswapping.  lock_user may return either a pointer to the guest
   memory, or a temporary buffer.  */

/* Lock an area of guest memory into the host.  If copy is true then the
   host area will have the same contents as the guest.  */
void *lock_user(int type, abi_ulong guest_addr, ssize_t len, bool copy);

/* Unlock an area of guest memory.  The first LEN bytes must be
   flushed back to guest memory. host_ptr = NULL is explicitly
   allowed and does nothing. */
#ifndef DEBUG_REMAP
static inline void unlock_user(void *host_ptr, abi_ulong guest_addr,
                               ssize_t len)
{
    /* no-op */
}
#else
void unlock_user(void *host_ptr, abi_ulong guest_addr, ssize_t len);
#endif

/* Return the length of a string in target memory or -TARGET_EFAULT if
   access error. */
ssize_t target_strlen(abi_ulong gaddr);

/* Like lock_user but for null terminated strings.  */
void *lock_user_string(abi_ulong guest_addr);

/* Helper macros for locking/unlocking a target struct.  */
#define lock_user_struct(type, host_ptr, guest_addr, copy)	\
    (host_ptr = lock_user(type, guest_addr, sizeof(*host_ptr), copy))
#define unlock_user_struct(host_ptr, guest_addr, copy)		\
    unlock_user(host_ptr, guest_addr, (copy) ? sizeof(*host_ptr) : 0)

#endif /* QEMU_H */
