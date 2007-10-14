#ifndef QEMU_H
#define QEMU_H

#include <signal.h>
#include <string.h>

#include "cpu.h"

#ifdef TARGET_ABI32
typedef uint32_t abi_ulong;
typedef int32_t abi_long;
#define TARGET_ABI_BITS 32
#else
typedef target_ulong abi_ulong;
typedef target_long abi_long;
#define TARGET_ABI_BITS TARGET_LONG_BITS
#endif

#include "thunk.h"
#include "syscall_defs.h"
#include "syscall.h"
#include "target_signal.h"
#include "gdbstub.h"

/* This struct is used to hold certain information about the image.
 * Basically, it replicates in user space what would be certain
 * task_struct fields in the kernel
 */
struct image_info {
        abi_ulong       load_addr;
        abi_ulong       start_code;
        abi_ulong       end_code;
        abi_ulong       start_data;
        abi_ulong       end_data;
        abi_ulong       start_brk;
        abi_ulong       brk;
        abi_ulong       start_mmap;
        abi_ulong       mmap;
        abi_ulong       rss;
        abi_ulong       start_stack;
        abi_ulong       entry;
        abi_ulong       code_offset;
        abi_ulong       data_offset;
        char            **host_argv;
	int		personality;
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

#ifdef TARGET_ARM
/* FPU emulator */
#include "nwfpe/fpa11.h"
#endif

/* NOTE: we force a big alignment so that the stack stored after is
   aligned too */
typedef struct TaskState {
    struct TaskState *next;
#ifdef TARGET_ARM
    /* FPA state */
    FPA11 fpa;
    int swi_errno;
#endif
#if defined(TARGET_I386) && !defined(TARGET_X86_64)
    abi_ulong target_v86;
    struct vm86_saved_state vm86_saved_regs;
    struct target_vm86plus_struct vm86plus;
    uint32_t v86flags;
    uint32_t v86mask;
#endif
#ifdef TARGET_M68K
    int sim_syscalls;
#endif
#if defined(TARGET_ARM) || defined(TARGET_M68K)
    /* Extra fields for semihosted binaries.  */
    uint32_t stack_base;
    uint32_t heap_base;
    uint32_t heap_limit;
#endif
    int used; /* non zero if used */
    struct image_info *info;
    uint8_t stack[0];
} __attribute__((aligned(16))) TaskState;

extern TaskState *first_task_state;
extern const char *qemu_uname_release;

/* ??? See if we can avoid exposing so much of the loader internals.  */
/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB w/4KB pages!
 */
#define MAX_ARG_PAGES 32

/*
 * This structure is used to hold the arguments that are
 * used when loading binaries.
 */
struct linux_binprm {
        char buf[128];
        void *page[MAX_ARG_PAGES];
        abi_ulong p;
	int fd;
        int e_uid, e_gid;
        int argc, envc;
        char **argv;
        char **envp;
        char * filename;        /* Name of binary */
};

void do_init_thread(struct target_pt_regs *regs, struct image_info *infop);
abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp, int push_ptr);
int loader_exec(const char * filename, char ** argv, char ** envp,
             struct target_pt_regs * regs, struct image_info *infop);

int load_elf_binary(struct linux_binprm * bprm, struct target_pt_regs * regs,
                    struct image_info * info);
int load_flt_binary(struct linux_binprm * bprm, struct target_pt_regs * regs,
                    struct image_info * info);
#ifdef TARGET_HAS_ELFLOAD32
int load_elf_binary_multi(struct linux_binprm *bprm,
                          struct target_pt_regs *regs,
                          struct image_info *info);
#endif

void memcpy_to_target(abi_ulong dest, const void *src,
                      unsigned long len);
void target_set_brk(abi_ulong new_brk);
abi_long do_brk(abi_ulong new_brk);
void syscall_init(void);
abi_long do_syscall(void *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6);
void gemu_log(const char *fmt, ...) __attribute__((format(printf,1,2)));
extern CPUState *global_env;
void cpu_loop(CPUState *env);
void init_paths(const char *prefix);
const char *path(const char *pathname);

extern int loglevel;
extern FILE *logfile;

/* signal.c */
void process_pending_signals(void *cpu_env);
void signal_init(void);
int queue_signal(int sig, target_siginfo_t *info);
void host_to_target_siginfo(target_siginfo_t *tinfo, const siginfo_t *info);
void target_to_host_siginfo(siginfo_t *info, const target_siginfo_t *tinfo);
long do_sigreturn(CPUState *env);
long do_rt_sigreturn(CPUState *env);
int do_sigaltstack(const struct target_sigaltstack *uss,
                   struct target_sigaltstack *uoss,
                   abi_ulong sp);

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

/* user access */

#define VERIFY_READ 0
#define VERIFY_WRITE 1

#define access_ok(type,addr,size) (1)

/* NOTE get_user and put_user use host addresses.  */
#define __put_user(x,ptr)\
({\
    int size = sizeof(*ptr);\
    switch(size) {\
    case 1:\
        *(uint8_t *)(ptr) = (typeof(*ptr))(x);\
        break;\
    case 2:\
        *(uint16_t *)(ptr) = tswap16((typeof(*ptr))(x));\
        break;\
    case 4:\
        *(uint32_t *)(ptr) = tswap32((typeof(*ptr))(x));\
        break;\
    case 8:\
        *(uint64_t *)(ptr) = tswap64((typeof(*ptr))(x));\
        break;\
    default:\
        abort();\
    }\
    0;\
})

#define __get_user(x, ptr) \
({\
    int size = sizeof(*ptr);\
    switch(size) {\
    case 1:\
        x = (typeof(*ptr))*(uint8_t *)(ptr);\
        break;\
    case 2:\
        x = (typeof(*ptr))tswap16(*(uint16_t *)(ptr));\
        break;\
    case 4:\
        x = (typeof(*ptr))tswap32(*(uint32_t *)(ptr));\
        break;\
    case 8:\
        x = (typeof(*ptr))tswap64(*(uint64_t *)(ptr));\
        break;\
    default:\
        abort();\
    }\
    0;\
})

#define put_user(x,ptr)\
({\
    int __ret;\
    if (access_ok(VERIFY_WRITE, ptr, sizeof(*ptr)))\
        __ret = __put_user(x, ptr);\
    else\
        __ret = -EFAULT;\
    __ret;\
})

#define get_user(x,ptr)\
({\
    int __ret;\
    if (access_ok(VERIFY_READ, ptr, sizeof(*ptr)))\
        __ret = __get_user(x, ptr);\
    else\
        __ret = -EFAULT;\
    __ret;\
})

/* Functions for accessing guest memory.  The tget and tput functions
   read/write single values, byteswapping as neccessary.  The lock_user
   gets a pointer to a contiguous area of guest memory, but does not perform
   and byteswapping.  lock_user may return either a pointer to the guest
   memory, or a temporary buffer.  */

/* Lock an area of guest memory into the host.  If copy is true then the
   host area will have the same contents as the guest.  */
static inline void *lock_user(abi_ulong guest_addr, long len, int copy)
{
#ifdef DEBUG_REMAP
    void *addr;
    addr = malloc(len);
    if (copy)
        memcpy(addr, g2h(guest_addr), len);
    else
        memset(addr, 0, len);
    return addr;
#else
    return g2h(guest_addr);
#endif
}

/* Unlock an area of guest memory.  The first LEN bytes must be flushed back
   to guest memory.  */
static inline void unlock_user(void *host_addr, abi_ulong guest_addr,
                               long len)
{
#ifdef DEBUG_REMAP
    if (host_addr == g2h(guest_addr))
        return;
    if (len > 0)
        memcpy(g2h(guest_addr), host_addr, len);
    free(host_addr);
#endif
}

/* Return the length of a string in target memory.  */
static inline int target_strlen(abi_ulong ptr)
{
  return strlen(g2h(ptr));
}

/* Like lock_user but for null terminated strings.  */
static inline void *lock_user_string(abi_ulong guest_addr)
{
    long len;
    len = target_strlen(guest_addr) + 1;
    return lock_user(guest_addr, len, 1);
}

/* Helper macros for locking/ulocking a target struct.  */
#define lock_user_struct(host_ptr, guest_addr, copy) \
    host_ptr = lock_user(guest_addr, sizeof(*host_ptr), copy)
#define unlock_user_struct(host_ptr, guest_addr, copy) \
    unlock_user(host_ptr, guest_addr, (copy) ? sizeof(*host_ptr) : 0)

#define tget8(addr) ldub(addr)
#define tput8(addr, val) stb(addr, val)
#define tget16(addr) lduw(addr)
#define tput16(addr, val) stw(addr, val)
#define tget32(addr) ldl(addr)
#define tput32(addr, val) stl(addr, val)
#define tget64(addr) ldq(addr)
#define tput64(addr, val) stq(addr, val)
#if TARGET_ABI_BITS == 64
#define tgetl(addr) ldq(addr)
#define tputl(addr, val) stq(addr, val)
#else
#define tgetl(addr) ldl(addr)
#define tputl(addr, val) stl(addr, val)
#endif

#endif /* QEMU_H */
