/*
 *  qemu user main
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "qemu.h"

#define DEBUG_LOGFILE "/tmp/qemu.log"

static const char *interp_prefix = CONFIG_QEMU_PREFIX;

#if defined(__i386__) && !defined(CONFIG_STATIC)
/* Force usage of an ELF interpreter even if it is an ELF shared
   object ! */
const char interp[] __attribute__((section(".interp"))) = "/lib/ld-linux.so.2";
#endif

/* for recent libc, we add these dummy symbols which are not declared
   when generating a linked object (bug in ld ?) */
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 3)
long __init_array_start[0];
long __init_array_end[0];
long __fini_array_start[0];
long __fini_array_end[0];
#endif

/* XXX: on x86 MAP_GROWSDOWN only works if ESP <= address + 32, so
   we allocate a bigger stack. Need a better solution, for example
   by remapping the process stack directly at the right place */
unsigned long x86_stack_size = 512 * 1024;

void gemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#ifdef TARGET_I386
/***********************************************************/
/* CPUX86 core interface */

void cpu_x86_outb(CPUX86State *env, int addr, int val)
{
    fprintf(stderr, "outb: port=0x%04x, data=%02x\n", addr, val);
}

void cpu_x86_outw(CPUX86State *env, int addr, int val)
{
    fprintf(stderr, "outw: port=0x%04x, data=%04x\n", addr, val);
}

void cpu_x86_outl(CPUX86State *env, int addr, int val)
{
    fprintf(stderr, "outl: port=0x%04x, data=%08x\n", addr, val);
}

int cpu_x86_inb(CPUX86State *env, int addr)
{
    fprintf(stderr, "inb: port=0x%04x\n", addr);
    return 0;
}

int cpu_x86_inw(CPUX86State *env, int addr)
{
    fprintf(stderr, "inw: port=0x%04x\n", addr);
    return 0;
}

int cpu_x86_inl(CPUX86State *env, int addr)
{
    fprintf(stderr, "inl: port=0x%04x\n", addr);
    return 0;
}

int cpu_x86_get_pic_interrupt(CPUX86State *env)
{
    return -1;
}

static void write_dt(void *ptr, unsigned long addr, unsigned long limit, 
                     int flags)
{
    unsigned int e1, e2;
    e1 = (addr << 16) | (limit & 0xffff);
    e2 = ((addr >> 16) & 0xff) | (addr & 0xff000000) | (limit & 0x000f0000);
    e2 |= flags;
    stl((uint8_t *)ptr, e1);
    stl((uint8_t *)ptr + 4, e2);
}

static void set_gate(void *ptr, unsigned int type, unsigned int dpl, 
                     unsigned long addr, unsigned int sel)
{
    unsigned int e1, e2;
    e1 = (addr & 0xffff) | (sel << 16);
    e2 = (addr & 0xffff0000) | 0x8000 | (dpl << 13) | (type << 8);
    stl((uint8_t *)ptr, e1);
    stl((uint8_t *)ptr + 4, e2);
}

uint64_t gdt_table[6];
uint64_t idt_table[256];

/* only dpl matters as we do only user space emulation */
static void set_idt(int n, unsigned int dpl)
{
    set_gate(idt_table + n, 0, dpl, 0, 0);
}

void cpu_loop(CPUX86State *env)
{
    int trapnr;
    uint8_t *pc;
    target_siginfo_t info;

    for(;;) {
        trapnr = cpu_x86_exec(env);
        switch(trapnr) {
        case 0x80:
            /* linux syscall */
            env->regs[R_EAX] = do_syscall(env, 
                                          env->regs[R_EAX], 
                                          env->regs[R_EBX],
                                          env->regs[R_ECX],
                                          env->regs[R_EDX],
                                          env->regs[R_ESI],
                                          env->regs[R_EDI],
                                          env->regs[R_EBP]);
            break;
        case EXCP0B_NOSEG:
        case EXCP0C_STACK:
            info.si_signo = SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_SI_KERNEL;
            info._sifields._sigfault._addr = 0;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP0D_GPF:
            if (env->eflags & VM_MASK) {
                handle_vm86_fault(env);
            } else {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SI_KERNEL;
                info._sifields._sigfault._addr = 0;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP0E_PAGE:
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            if (!(env->error_code & 1))
                info.si_code = TARGET_SEGV_MAPERR;
            else
                info.si_code = TARGET_SEGV_ACCERR;
            info._sifields._sigfault._addr = env->cr[2];
            queue_signal(info.si_signo, &info);
            break;
        case EXCP00_DIVZ:
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else {
                /* division by zero */
                info.si_signo = SIGFPE;
                info.si_errno = 0;
                info.si_code = TARGET_FPE_INTDIV;
                info._sifields._sigfault._addr = env->eip;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP01_SSTP:
        case EXCP03_INT3:
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else {
                info.si_signo = SIGTRAP;
                info.si_errno = 0;
                if (trapnr == EXCP01_SSTP) {
                    info.si_code = TARGET_TRAP_BRKPT;
                    info._sifields._sigfault._addr = env->eip;
                } else {
                    info.si_code = TARGET_SI_KERNEL;
                    info._sifields._sigfault._addr = 0;
                }
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP04_INTO:
        case EXCP05_BOUND:
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SI_KERNEL;
                info._sifields._sigfault._addr = 0;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP06_ILLOP:
            info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->eip;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        default:
            pc = env->segs[R_CS].base + env->eip;
            fprintf(stderr, "qemu: 0x%08lx: unhandled CPU exception 0x%x - aborting\n", 
                    (long)pc, trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif

#ifdef TARGET_ARM

void cpu_loop(CPUARMState *env)
{
    int trapnr;
    unsigned int n, insn;
    target_siginfo_t info;
    
    for(;;) {
        trapnr = cpu_arm_exec(env);
        switch(trapnr) {
        case EXCP_UDEF:
            info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->regs[15];
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_SWI:
            {
                /* system call */
                insn = ldl((void *)(env->regs[15] - 4));
                n = insn & 0xffffff;
                if (n >= ARM_SYSCALL_BASE) {
                    /* linux syscall */
                    n -= ARM_SYSCALL_BASE;
                    env->regs[0] = do_syscall(env, 
                                              n, 
                                              env->regs[0],
                                              env->regs[1],
                                              env->regs[2],
                                              env->regs[3],
                                              env->regs[4],
                                              0);
                } else {
                    goto error;
                }
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        default:
        error:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n", 
                    trapnr);
            cpu_arm_dump_state(env, stderr, 0);
            abort();
        }
        process_pending_signals(env);
    }
}

#endif

#ifdef TARGET_SPARC

void cpu_loop (CPUSPARCState *env)
{
	int trapnr;

	while (1) {
		trapnr = cpu_sparc_exec (env);

		switch (trapnr) {
		  case 0x8: case 0x10:
			env->regwptr[0] = do_syscall (env, env->gregs[1],
				env->regwptr[0], env->regwptr[1], env->regwptr[2],
				env->regwptr[3], env->regwptr[4], env->regwptr[13]);
			if (env->regwptr[0] >= 0xffffffe0)
				env->psr |= PSR_CARRY;
			break;
		  default:
			printf ("Invalid trap: %d\n", trapnr);
			exit (1);
		}
		process_pending_signals (env);
	}
}

#endif

#ifdef TARGET_PPC

void cpu_loop(CPUPPCState *env)
{
    int trapnr;
    target_siginfo_t info;
    
    for(;;) {
        trapnr = cpu_ppc_exec(env);
        switch(trapnr) {
        case EXCP_NONE:
        case EXCP_INTERRUPT:
        case EXCP_MTMSR: /* mtmsr instruction:               */
        case EXCP_BRANCH: /* branch instruction               */
            /* Single step mode */
            break;
#if 0
        case EXCP_RESET: /* System reset                     */
            fprintf(stderr, "RESET asked... Stop emulation\n");
            cpu_ppc_dump_state(env, stderr, 0);
            abort();
#endif
        case EXCP_MACHINE_CHECK: /* Machine check exception          */
            fprintf(stderr, "Machine check exeption... "
                    "See you in kernel code !\n");
            cpu_ppc_dump_state(env, stderr, 0);
            abort();
        case EXCP_DSI:  /* Impossible memory access         */
            fprintf(stderr, "Invalid memory access\n");
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->nip;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_ISI: /* Impossible instruction fetch     */
            fprintf(stderr, "Invalid instruction fetch\n");
            info.si_signo = SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->nip;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_EXTERNAL: /* External interruption            */
            fprintf(stderr, "External access exeption\n");
            cpu_ppc_dump_state(env, stderr, 0);
            abort();
        case EXCP_ALIGN: /* Alignment exception              */
            fprintf(stderr, "Alignment exception\n");
            cpu_ppc_dump_state(env, stderr, 0);
            abort();
        case EXCP_PROGRAM: /* Program exception                */
            fprintf(stderr, "Program exception\n");
            cpu_ppc_dump_state(env, stderr, 0);
            abort();
            break;
        /* Trap */
        case EXCP_TRAP: /* Trap                             */
        case EXCP_TRACE: /* Trace exception (optional)       */
            info.si_signo = SIGTRAP;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->nip;
            queue_signal(info.si_signo, &info);
            break;
        /* Invalid instruction */
        case EXCP_INVAL:
            info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->nip;
            queue_signal(info.si_signo, &info);
            break;
        /* Privileged instruction */
        case EXCP_PRIV: /* Privileged instruction           */
            info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->nip;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_NO_FP: /* No floating point                */
        case EXCP_DECR: /* Decrementer exception            */
        case EXCP_RESA: /* Implementation specific          */
        case EXCP_RESB: /* Implementation specific          */
        case EXCP_FP_ASSIST: /* Floating-point assist (optional) */
            fprintf(stderr, "Misc expt...\n");
            cpu_ppc_dump_state(env, stderr, 0);
            abort();

        case EXCP_SYSCALL:
        {
            uint32_t ret;
            /* system call */
            /* WARNING:
             * PPC ABI uses overflow flag in cr0 to signal an error
             * in syscalls.
             */
            env->crf[0] &= ~0x1;
            ret = do_syscall(env, env->gpr[0], env->gpr[3], env->gpr[4],
                             env->gpr[5], env->gpr[6], env->gpr[7],
                             env->gpr[8]);
            if (ret > (uint32_t)(-515)) {
                env->crf[0] |= 0x1;
                ret = -ret;
            }
            env->gpr[3] = ret;
            break;
        }
        default:
//        error:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n", 
                    trapnr);
            cpu_ppc_dump_state(env, stderr, 0);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif

void usage(void)
{
    printf("qemu-" TARGET_ARCH " version " QEMU_VERSION ", Copyright (c) 2003 Fabrice Bellard\n"
           "usage: qemu-" TARGET_ARCH " [-h] [-d] [-L path] [-s size] program [arguments...]\n"
           "Linux CPU emulator (compiled for %s emulation)\n"
           "\n"
           "-h           print this help\n"
           "-L path      set the elf interpreter prefix (default=%s)\n"
           "-s size      set the stack size in bytes (default=%ld)\n"
           "\n"
           "debug options:\n"
           "-d           activate log (logfile=%s)\n"
           "-p pagesize  set the host page size to 'pagesize'\n",
           TARGET_ARCH,
           interp_prefix, 
           x86_stack_size,
           DEBUG_LOGFILE);
    _exit(1);
}

/* XXX: currently only used for async signals (see signal.c) */
CPUState *global_env;
/* used only if single thread */
CPUState *cpu_single_env = NULL;

/* used to free thread contexts */
TaskState *first_task_state;

int main(int argc, char **argv)
{
    const char *filename;
    struct target_pt_regs regs1, *regs = &regs1;
    struct image_info info1, *info = &info1;
    TaskState ts1, *ts = &ts1;
    CPUState *env;
    int optind;
    const char *r;
    
    if (argc <= 1)
        usage();

    /* init debug */
    cpu_set_log_filename(DEBUG_LOGFILE);

    optind = 1;
    for(;;) {
        if (optind >= argc)
            break;
        r = argv[optind];
        if (r[0] != '-')
            break;
        optind++;
        r++;
        if (!strcmp(r, "-")) {
            break;
        } else if (!strcmp(r, "d")) {
            cpu_set_log(CPU_LOG_ALL);
        } else if (!strcmp(r, "s")) {
            r = argv[optind++];
            x86_stack_size = strtol(r, (char **)&r, 0);
            if (x86_stack_size <= 0)
                usage();
            if (*r == 'M')
                x86_stack_size *= 1024 * 1024;
            else if (*r == 'k' || *r == 'K')
                x86_stack_size *= 1024;
        } else if (!strcmp(r, "L")) {
            interp_prefix = argv[optind++];
        } else if (!strcmp(r, "p")) {
            host_page_size = atoi(argv[optind++]);
            if (host_page_size == 0 ||
                (host_page_size & (host_page_size - 1)) != 0) {
                fprintf(stderr, "page size must be a power of two\n");
                exit(1);
            }
        } else {
            usage();
        }
    }
    if (optind >= argc)
        usage();
    filename = argv[optind];

    /* Zero out regs */
    memset(regs, 0, sizeof(struct target_pt_regs));

    /* Zero out image_info */
    memset(info, 0, sizeof(struct image_info));

    /* Scan interp_prefix dir for replacement files. */
    init_paths(interp_prefix);

    /* NOTE: we need to init the CPU at this stage to get the
       host_page_size */
    env = cpu_init();
    
    if (elf_exec(filename, argv+optind, environ, regs, info) != 0) {
	printf("Error loading %s\n", filename);
	_exit(1);
    }
    
    if (loglevel) {
        page_dump(logfile);
    
        fprintf(logfile, "start_brk   0x%08lx\n" , info->start_brk);
        fprintf(logfile, "end_code    0x%08lx\n" , info->end_code);
        fprintf(logfile, "start_code  0x%08lx\n" , info->start_code);
        fprintf(logfile, "end_data    0x%08lx\n" , info->end_data);
        fprintf(logfile, "start_stack 0x%08lx\n" , info->start_stack);
        fprintf(logfile, "brk         0x%08lx\n" , info->brk);
        fprintf(logfile, "entry       0x%08lx\n" , info->entry);
    }

    target_set_brk((char *)info->brk);
    syscall_init();
    signal_init();

    global_env = env;

    /* build Task State */
    memset(ts, 0, sizeof(TaskState));
    env->opaque = ts;
    ts->used = 1;
    env->user_mode_only = 1;
    
#if defined(TARGET_I386)
    cpu_x86_set_cpl(env, 3);

    env->cr[0] = CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK;

    /* linux register setup */
    env->regs[R_EAX] = regs->eax;
    env->regs[R_EBX] = regs->ebx;
    env->regs[R_ECX] = regs->ecx;
    env->regs[R_EDX] = regs->edx;
    env->regs[R_ESI] = regs->esi;
    env->regs[R_EDI] = regs->edi;
    env->regs[R_EBP] = regs->ebp;
    env->regs[R_ESP] = regs->esp;
    env->eip = regs->eip;

    /* linux interrupt setup */
    env->idt.base = (void *)idt_table;
    env->idt.limit = sizeof(idt_table) - 1;
    set_idt(0, 0);
    set_idt(1, 0);
    set_idt(2, 0);
    set_idt(3, 3);
    set_idt(4, 3);
    set_idt(5, 3);
    set_idt(6, 0);
    set_idt(7, 0);
    set_idt(8, 0);
    set_idt(9, 0);
    set_idt(10, 0);
    set_idt(11, 0);
    set_idt(12, 0);
    set_idt(13, 0);
    set_idt(14, 0);
    set_idt(15, 0);
    set_idt(16, 0);
    set_idt(17, 0);
    set_idt(18, 0);
    set_idt(19, 0);
    set_idt(0x80, 3);

    /* linux segment setup */
    env->gdt.base = (void *)gdt_table;
    env->gdt.limit = sizeof(gdt_table) - 1;
    write_dt(&gdt_table[__USER_CS >> 3], 0, 0xfffff,
             DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK | 
             (3 << DESC_DPL_SHIFT) | (0xa << DESC_TYPE_SHIFT));
    write_dt(&gdt_table[__USER_DS >> 3], 0, 0xfffff,
             DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK | 
             (3 << DESC_DPL_SHIFT) | (0x2 << DESC_TYPE_SHIFT));
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    cpu_x86_load_seg(env, R_FS, __USER_DS);
    cpu_x86_load_seg(env, R_GS, __USER_DS);

#elif defined(TARGET_ARM)
    {
        int i;
        for(i = 0; i < 16; i++) {
            env->regs[i] = regs->uregs[i];
        }
        env->cpsr = regs->uregs[16];
    }
#elif defined(TARGET_SPARC)
	env->pc = regs->u_regs[0];
	env->regwptr[6] = regs->u_regs[1]-0x40;
#elif defined(TARGET_PPC)
    {
        int i;
        for (i = 0; i < 32; i++)
            env->msr[i] = (regs->msr >> i) & 1;
        env->nip = regs->nip;
        for(i = 0; i < 32; i++) {
            env->gpr[i] = regs->gpr[i];
        }
    }
#else
#error unsupported target CPU
#endif

    cpu_loop(env);
    /* never exits */
    return 0;
}
