#ifndef GEMU_H
#define GEMU_H

#include "thunk.h"

#include <signal.h>
#include "syscall_defs.h"

#ifdef TARGET_I386
#include "cpu-i386.h"
#include "syscall-i386.h"
#endif

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

/* NOTE: we force a big alignment so that the stack stored after is
   aligned too */
typedef struct TaskState {
    struct TaskState *next;
    struct target_vm86plus_struct *target_v86;
    struct vm86_saved_state vm86_saved_regs;
    int used; /* non zero if used */
    uint8_t stack[0];
} __attribute__((aligned(16))) TaskState;

extern TaskState *first_task_state;

int elf_exec(const char *interp_prefix, 
             const char * filename, char ** argv, char ** envp, 
             struct target_pt_regs * regs, struct image_info *infop);

void target_set_brk(char *new_brk);
void syscall_init(void);
long do_syscall(void *cpu_env, int num, long arg1, long arg2, long arg3, 
                long arg4, long arg5, long arg6);
void gemu_log(const char *fmt, ...) __attribute__((format(printf,1,2)));
extern CPUX86State *global_env;
void cpu_loop(CPUX86State *env);
void process_pending_signals(void *cpu_env);
void signal_init(void);
int queue_signal(int sig, target_siginfo_t *info);

#endif
