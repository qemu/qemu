#ifndef QEMU_H
#define QEMU_H

#include "thunk.h"

#include <signal.h>
#include <string.h>
#include "syscall_defs.h"

#include "cpu.h"
#include "syscall.h"
#include "gdbstub.h"

/* This struct is used to hold certain information about the image.
 * Basically, it replicates in user space what would be certain
 * task_struct fields in the kernel
 */
struct image_info {
	unsigned long	start_code;
	unsigned long	end_code;
	unsigned long	end_data;
	unsigned long	start_brk;
	unsigned long	brk;
	unsigned long	start_mmap;
	unsigned long	mmap;
	unsigned long	rss;
	unsigned long	start_stack;
	unsigned long	arg_start;
	unsigned long	arg_end;
	unsigned long	env_start;
	unsigned long	env_end;
	unsigned long	entry;
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
#endif
#ifdef TARGET_I386
    struct target_vm86plus_struct *target_v86;
    struct vm86_saved_state vm86_saved_regs;
    struct target_vm86plus_struct vm86plus;
    uint32_t v86flags;
    uint32_t v86mask;
#endif
    int used; /* non zero if used */
    uint8_t stack[0];
} __attribute__((aligned(16))) TaskState;

extern TaskState *first_task_state;

int elf_exec(const char * filename, char ** argv, char ** envp, 
             struct target_pt_regs * regs, struct image_info *infop);

void target_set_brk(char *new_brk);
void syscall_init(void);
long do_syscall(void *cpu_env, int num, long arg1, long arg2, long arg3, 
                long arg4, long arg5, long arg6);
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

#ifdef TARGET_I386
/* vm86.c */
void save_v86_state(CPUX86State *env);
void handle_vm86_trap(CPUX86State *env, int trapno);
void handle_vm86_fault(CPUX86State *env);
int do_vm86(CPUX86State *env, long subfunction, 
            struct target_vm86plus_struct * target_v86);
#endif

/* mmap.c */
int target_mprotect(unsigned long start, unsigned long len, int prot);
long target_mmap(unsigned long start, unsigned long len, int prot, 
                 int flags, int fd, unsigned long offset);
int target_munmap(unsigned long start, unsigned long len);
long target_mremap(unsigned long old_addr, unsigned long old_size, 
                   unsigned long new_size, unsigned long flags,
                   unsigned long new_addr);
int target_msync(unsigned long start, unsigned long len, int flags);

/* user access */

#define VERIFY_READ 0
#define VERIFY_WRITE 1

#define access_ok(type,addr,size) (1)

#define __put_user(x,ptr)\
({\
    int size = sizeof(*ptr);\
    switch(size) {\
    case 1:\
        stb(ptr, (typeof(*ptr))(x));\
        break;\
    case 2:\
        stw(ptr, (typeof(*ptr))(x));\
        break;\
    case 4:\
        stl(ptr, (typeof(*ptr))(x));\
        break;\
    case 8:\
        stq(ptr, (typeof(*ptr))(x));\
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
        x = (typeof(*ptr))ldub((void *)ptr);\
        break;\
    case 2:\
        x = (typeof(*ptr))lduw((void *)ptr);\
        break;\
    case 4:\
        x = (typeof(*ptr))ldl((void *)ptr);\
        break;\
    case 8:\
        x = (typeof(*ptr))ldq((void *)ptr);\
        break;\
    default:\
        abort();\
    }\
    0;\
})

static inline unsigned long __copy_to_user(void *dst, const void *src, 
                                           unsigned long size)
{
    memcpy(dst, src, size);
    return 0;
}

static inline unsigned long __copy_from_user(void *dst, const void *src, 
                                             unsigned long size)
{
    memcpy(dst, src, size);
    return 0;
}

static inline unsigned long __clear_user(void *dst, unsigned long size)
{
    memset(dst, 0, size);
    return 0;
}

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

static inline unsigned long copy_to_user(void *dst, const void *src, 
                                         unsigned long size)
{
    if (access_ok(VERIFY_WRITE, dst, size))
        return __copy_to_user(dst, src, size);
    else
        return size;
}

static inline unsigned long copy_from_user(void *dst, const void *src, 
                                             unsigned long size)
{
    if (access_ok(VERIFY_READ, src, size))
        return __copy_from_user(dst, src, size);
    else
        return size;
}

static inline unsigned long clear_user(void *dst, unsigned long size)
{
    if (access_ok(VERIFY_WRITE, dst, size))
        return __clear_user(dst, size);
    else
        return size;
}

#endif /* QEMU_H */
