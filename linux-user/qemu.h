#ifndef GEMU_H
#define GEMU_H

#include "thunk.h"

struct pt_regs {
	long ebx;
	long ecx;
	long edx;
	long esi;
	long edi;
	long ebp;
	long eax;
	int  xds;
	int  xes;
	long orig_eax;
	long eip;
	int  xcs;
	long eflags;
	long esp;
	int  xss;
};

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

int elf_exec(const char * filename, char ** argv, char ** envp, 
             struct pt_regs * regs, struct image_info *infop);

void target_set_brk(char *new_brk);
void syscall_init(void);
long do_syscall(int num, long arg1, long arg2, long arg3, 
                long arg4, long arg5, long arg6);
void gemu_log(const char *fmt, ...) __attribute__((format(printf,1,2)));



#endif
