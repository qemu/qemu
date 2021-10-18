/*
 *  qemu bsd user mode definition
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef QEMU_H
#define QEMU_H

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/units.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"

#undef DEBUG_REMAP

#include "exec/user/abitypes.h"

extern char **environ;

enum BSDType {
    target_freebsd,
    target_netbsd,
    target_openbsd,
};
extern enum BSDType bsd_type;

#include "exec/user/thunk.h"
#include "target_arch.h"
#include "syscall_defs.h"
#include "target_syscall.h"
#include "target_os_vmparam.h"
#include "target_os_signal.h"
#include "exec/gdbstub.h"

/*
 * This struct is used to hold certain information about the image.  Basically,
 * it replicates in user space what would be certain task_struct fields in the
 * kernel
 */
struct image_info {
    abi_ulong load_bias;
    abi_ulong load_addr;
    abi_ulong start_code;
    abi_ulong end_code;
    abi_ulong start_data;
    abi_ulong end_data;
    abi_ulong start_brk;
    abi_ulong brk;
    abi_ulong start_mmap;
    abi_ulong mmap;
    abi_ulong rss;
    abi_ulong start_stack;
    abi_ulong entry;
    abi_ulong code_offset;
    abi_ulong data_offset;
    abi_ulong arg_start;
    abi_ulong arg_end;
    uint32_t  elf_flags;
};

#define MAX_SIGQUEUE_SIZE 1024

struct qemu_sigqueue {
    struct qemu_sigqueue *next;
    target_siginfo_t info;
};

struct emulated_sigtable {
    int pending; /* true if signal is pending */
    struct qemu_sigqueue *first;
    struct qemu_sigqueue info;  /* Put first signal info here */
};

/*
 * NOTE: we force a big alignment so that the stack stored after is aligned too
 */
typedef struct TaskState {
    pid_t ts_tid;     /* tid (or pid) of this task */

    struct TaskState *next;
    struct bsd_binprm *bprm;
    struct image_info *info;

    struct emulated_sigtable sigtab[TARGET_NSIG];
    struct qemu_sigqueue sigqueue_table[MAX_SIGQUEUE_SIZE]; /* siginfo queue */
    struct qemu_sigqueue *first_free; /* first free siginfo queue entry */
    int signal_pending; /* non zero if a signal may be pending */

    uint8_t stack[];
} __attribute__((aligned(16))) TaskState;

void init_task_state(TaskState *ts);
void stop_all_tasks(void);
extern const char *qemu_uname_release;

/*
 * TARGET_ARG_MAX defines the number of bytes allocated for arguments
 * and envelope for the new program. 256k should suffice for a reasonable
 * maxiumum env+arg in 32-bit environments, bump it up to 512k for !ILP32
 * platforms.
 */
#if TARGET_ABI_BITS > 32
#define TARGET_ARG_MAX (512 * KiB)
#else
#define TARGET_ARG_MAX (256 * KiB)
#endif
#define MAX_ARG_PAGES (TARGET_ARG_MAX / TARGET_PAGE_SIZE)

/*
 * This structure is used to hold the arguments that are
 * used when loading binaries.
 */
struct bsd_binprm {
        char buf[128];
        void *page[MAX_ARG_PAGES];
        abi_ulong p;
        abi_ulong stringp;
        int fd;
        int e_uid, e_gid;
        int argc, envc;
        char **argv;
        char **envp;
        char *filename;         /* (Given) Name of binary */
        char *fullpath;         /* Full path of binary */
        int (*core_dump)(int, CPUArchState *);
};

void do_init_thread(struct target_pt_regs *regs, struct image_info *infop);
abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp);
int loader_exec(const char *filename, char **argv, char **envp,
                struct target_pt_regs *regs, struct image_info *infop,
                struct bsd_binprm *bprm);

int load_elf_binary(struct bsd_binprm *bprm, struct target_pt_regs *regs,
                    struct image_info *info);
int load_flt_binary(struct bsd_binprm *bprm, struct target_pt_regs *regs,
                    struct image_info *info);
int is_target_elf_binary(int fd);

abi_long memcpy_to_target(abi_ulong dest, const void *src,
                          unsigned long len);
void target_set_brk(abi_ulong new_brk);
abi_long do_brk(abi_ulong new_brk);
void syscall_init(void);
abi_long do_freebsd_syscall(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6, abi_long arg7,
                            abi_long arg8);
abi_long do_netbsd_syscall(void *cpu_env, int num, abi_long arg1,
                           abi_long arg2, abi_long arg3, abi_long arg4,
                           abi_long arg5, abi_long arg6);
abi_long do_openbsd_syscall(void *cpu_env, int num, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5, abi_long arg6);
void gemu_log(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
extern __thread CPUState *thread_cpu;
void cpu_loop(CPUArchState *env);
char *target_strerror(int err);
int get_osversion(void);
void fork_start(void);
void fork_end(int child);

#include "qemu/log.h"

/* strace.c */
struct syscallname {
    int nr;
    const char *name;
    const char *format;
    void (*call)(const struct syscallname *,
                 abi_long, abi_long, abi_long,
                 abi_long, abi_long, abi_long);
    void (*result)(const struct syscallname *, abi_long);
};

void
print_freebsd_syscall(int num,
                      abi_long arg1, abi_long arg2, abi_long arg3,
                      abi_long arg4, abi_long arg5, abi_long arg6);
void print_freebsd_syscall_ret(int num, abi_long ret);
void
print_netbsd_syscall(int num,
                     abi_long arg1, abi_long arg2, abi_long arg3,
                     abi_long arg4, abi_long arg5, abi_long arg6);
void print_netbsd_syscall_ret(int num, abi_long ret);
void
print_openbsd_syscall(int num,
                      abi_long arg1, abi_long arg2, abi_long arg3,
                      abi_long arg4, abi_long arg5, abi_long arg6);
void print_openbsd_syscall_ret(int num, abi_long ret);
extern int do_strace;

/* signal.c */
void process_pending_signals(CPUArchState *cpu_env);
void signal_init(void);
long do_sigreturn(CPUArchState *env);
long do_rt_sigreturn(CPUArchState *env);
void queue_signal(CPUArchState *env, int sig, target_siginfo_t *info);
abi_long do_sigaltstack(abi_ulong uss_addr, abi_ulong uoss_addr, abi_ulong sp);

/* mmap.c */
int target_mprotect(abi_ulong start, abi_ulong len, int prot);
abi_long target_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, int fd, off_t offset);
int target_munmap(abi_ulong start, abi_ulong len);
abi_long target_mremap(abi_ulong old_addr, abi_ulong old_size,
                       abi_ulong new_size, unsigned long flags,
                       abi_ulong new_addr);
int target_msync(abi_ulong start, abi_ulong len, int flags);
extern unsigned long last_brk;
extern abi_ulong mmap_next_start;
abi_ulong mmap_find_vma(abi_ulong start, abi_ulong size);
void mmap_fork_start(void);
void mmap_fork_end(int child);

/* main.c */
extern char qemu_proc_pathname[];
extern unsigned long target_maxtsiz;
extern unsigned long target_dfldsiz;
extern unsigned long target_maxdsiz;
extern unsigned long target_dflssiz;
extern unsigned long target_maxssiz;
extern unsigned long target_sgrowsiz;

/* syscall.c */
abi_long get_errno(abi_long ret);
bool is_error(abi_long ret);

/* os-sys.c */
abi_long do_freebsd_sysarch(void *cpu_env, abi_long arg1, abi_long arg2);

/* user access */

#define VERIFY_READ  PAGE_READ
#define VERIFY_WRITE (PAGE_READ | PAGE_WRITE)

static inline bool access_ok(int type, abi_ulong addr, abi_ulong size)
{
    return page_check_range((target_ulong)addr, size, type) == 0;
}

/*
 * NOTE __get_user and __put_user use host pointers and don't check access.
 *
 * These are usually used to access struct data members once the struct has been
 * locked - usually with lock_user_struct().
 */
#define __put_user(x, hptr)\
({\
    int size = sizeof(*hptr);\
    switch (size) {\
    case 1:\
        *(uint8_t *)(hptr) = (uint8_t)(typeof(*hptr))(x);\
        break;\
    case 2:\
        *(uint16_t *)(hptr) = tswap16((typeof(*hptr))(x));\
        break;\
    case 4:\
        *(uint32_t *)(hptr) = tswap32((typeof(*hptr))(x));\
        break;\
    case 8:\
        *(uint64_t *)(hptr) = tswap64((typeof(*hptr))(x));\
        break;\
    default:\
        abort();\
    } \
    0;\
})

#define __get_user(x, hptr) \
({\
    int size = sizeof(*hptr);\
    switch (size) {\
    case 1:\
        x = (typeof(*hptr))*(uint8_t *)(hptr);\
        break;\
    case 2:\
        x = (typeof(*hptr))tswap16(*(uint16_t *)(hptr));\
        break;\
    case 4:\
        x = (typeof(*hptr))tswap32(*(uint32_t *)(hptr));\
        break;\
    case 8:\
        x = (typeof(*hptr))tswap64(*(uint64_t *)(hptr));\
        break;\
    default:\
        x = 0;\
        abort();\
    } \
    0;\
})

/*
 * put_user()/get_user() take a guest address and check access
 *
 * These are usually used to access an atomic data type, such as an int, that
 * has been passed by address.  These internally perform locking and unlocking
 * on the data type.
 */
#define put_user(x, gaddr, target_type)                                 \
({                                                                      \
    abi_ulong __gaddr = (gaddr);                                        \
    target_type *__hptr;                                                \
    abi_long __ret;                                                     \
    __hptr = lock_user(VERIFY_WRITE, __gaddr, sizeof(target_type), 0);  \
    if (__hptr) {                                                       \
        __ret = __put_user((x), __hptr);                                \
        unlock_user(__hptr, __gaddr, sizeof(target_type));              \
    } else                                                              \
        __ret = -TARGET_EFAULT;                                         \
    __ret;                                                              \
})

#define get_user(x, gaddr, target_type)                                 \
({                                                                      \
    abi_ulong __gaddr = (gaddr);                                        \
    target_type *__hptr;                                                \
    abi_long __ret;                                                     \
    __hptr = lock_user(VERIFY_READ, __gaddr, sizeof(target_type), 1);   \
    if (__hptr) {                                                       \
        __ret = __get_user((x), __hptr);                                \
        unlock_user(__hptr, __gaddr, 0);                                \
    } else {                                                            \
        (x) = 0;                                                        \
        __ret = -TARGET_EFAULT;                                         \
    }                                                                   \
    __ret;                                                              \
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

/*
 * copy_from_user() and copy_to_user() are usually used to copy data
 * buffers between the target and host.  These internally perform
 * locking/unlocking of the memory.
 */
abi_long copy_from_user(void *hptr, abi_ulong gaddr, size_t len);
abi_long copy_to_user(abi_ulong gaddr, void *hptr, size_t len);

/*
 * Functions for accessing guest memory.  The tget and tput functions
 * read/write single values, byteswapping as necessary.  The lock_user function
 * gets a pointer to a contiguous area of guest memory, but does not perform
 * any byteswapping.  lock_user may return either a pointer to the guest
 * memory, or a temporary buffer.
 */

/*
 * Lock an area of guest memory into the host.  If copy is true then the
 * host area will have the same contents as the guest.
 */
static inline void *lock_user(int type, abi_ulong guest_addr, long len,
                              int copy)
{
    if (!access_ok(type, guest_addr, len)) {
        return NULL;
    }
#ifdef DEBUG_REMAP
    {
        void *addr;
        addr = g_malloc(len);
        if (copy) {
            memcpy(addr, g2h_untagged(guest_addr), len);
        } else {
            memset(addr, 0, len);
        }
        return addr;
    }
#else
    return g2h_untagged(guest_addr);
#endif
}

/*
 * Unlock an area of guest memory.  The first LEN bytes must be flushed back to
 * guest memory. host_ptr = NULL is explicitly allowed and does nothing.
 */
static inline void unlock_user(void *host_ptr, abi_ulong guest_addr,
                               long len)
{

#ifdef DEBUG_REMAP
    if (!host_ptr) {
        return;
    }
    if (host_ptr == g2h_untagged(guest_addr)) {
        return;
    }
    if (len > 0) {
        memcpy(g2h_untagged(guest_addr), host_ptr, len);
    }
    g_free(host_ptr);
#endif
}

/*
 * Return the length of a string in target memory or -TARGET_EFAULT if access
 * error.
 */
abi_long target_strlen(abi_ulong gaddr);

/* Like lock_user but for null terminated strings.  */
static inline void *lock_user_string(abi_ulong guest_addr)
{
    abi_long len;
    len = target_strlen(guest_addr);
    if (len < 0) {
        return NULL;
    }
    return lock_user(VERIFY_READ, guest_addr, (long)(len + 1), 1);
}

/* Helper macros for locking/unlocking a target struct.  */
#define lock_user_struct(type, host_ptr, guest_addr, copy)      \
    (host_ptr = lock_user(type, guest_addr, sizeof(*host_ptr), copy))
#define unlock_user_struct(host_ptr, guest_addr, copy)          \
    unlock_user(host_ptr, guest_addr, (copy) ? sizeof(*host_ptr) : 0)

#include <pthread.h>

#endif /* QEMU_H */
