/*
 *  qemu main
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

#include "cpu-i386.h"

#define DEBUG_LOGFILE "/tmp/qemu.log"

FILE *logfile = NULL;
int loglevel;
const char *interp_prefix = CONFIG_QEMU_PREFIX "/qemu-i386";

/* XXX: on x86 MAP_GROWSDOWN only works if ESP <= address + 32, so
   we allocate a bigger stack. Need a better solution, for example
   by remapping the process stack directly at the right place */
unsigned long x86_stack_size = 512 * 1024;
unsigned long stktop;

void gemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/***********************************************************/
/* CPUX86 core interface */

void cpu_x86_outb(int addr, int val)
{
    fprintf(stderr, "outb: port=0x%04x, data=%02x\n", addr, val);
}

void cpu_x86_outw(int addr, int val)
{
    fprintf(stderr, "outw: port=0x%04x, data=%04x\n", addr, val);
}

void cpu_x86_outl(int addr, int val)
{
    fprintf(stderr, "outl: port=0x%04x, data=%08x\n", addr, val);
}

int cpu_x86_inb(int addr)
{
    fprintf(stderr, "inb: port=0x%04x\n", addr);
    return 0;
}

int cpu_x86_inw(int addr)
{
    fprintf(stderr, "inw: port=0x%04x\n", addr);
    return 0;
}

int cpu_x86_inl(int addr)
{
    fprintf(stderr, "inl: port=0x%04x\n", addr);
    return 0;
}

void write_dt(void *ptr, unsigned long addr, unsigned long limit, 
              int seg32_bit)
{
    unsigned int e1, e2, limit_in_pages;
    limit_in_pages = 0;
    if (limit > 0xffff) {
        limit = limit >> 12;
        limit_in_pages = 1;
    }
    e1 = (addr << 16) | (limit & 0xffff);
    e2 = ((addr >> 16) & 0xff) | (addr & 0xff000000) | (limit & 0x000f0000);
    e2 |= limit_in_pages << 23; /* byte granularity */
    e2 |= seg32_bit << 22; /* 32 bit segment */
    stl((uint8_t *)ptr, e1);
    stl((uint8_t *)ptr + 4, e2);
}

uint64_t gdt_table[6];

//#define DEBUG_VM86

void cpu_loop(struct CPUX86State *env)
{
    int err;
    uint8_t *pc;
    target_siginfo_t info;

    for(;;) {
        err = cpu_x86_exec(env);
        pc = env->seg_cache[R_CS].base + env->eip;
        switch(err) {
        case EXCP0D_GPF:
            if (env->eflags & VM_MASK) {
                TaskState *ts;
                int ret;
#ifdef DEBUG_VM86
                printf("VM86 exception %04x:%08x %02x\n",
                       env->segs[R_CS], env->eip, pc[0]);
#endif
                /* VM86 mode */
                ts = env->opaque;

                /* XXX: add all cases */
                switch(pc[0]) {
                case 0xcd: /* int */
                    env->eip += 2;
                    ret = TARGET_VM86_INTx | (pc[1] << 8);
                    break;
                default:
                    /* real VM86 GPF exception */
                    ret = TARGET_VM86_UNKNOWN;
                    break;
                }
#ifdef DEBUG_VM86
                printf("ret=0x%x\n", ret);
#endif
                /* put the VM86 registers in the userspace register structure */
                ts->target_v86->regs.eax = tswap32(env->regs[R_EAX]);
                ts->target_v86->regs.ebx = tswap32(env->regs[R_EBX]);
                ts->target_v86->regs.ecx = tswap32(env->regs[R_ECX]);
                ts->target_v86->regs.edx = tswap32(env->regs[R_EDX]);
                ts->target_v86->regs.esi = tswap32(env->regs[R_ESI]);
                ts->target_v86->regs.edi = tswap32(env->regs[R_EDI]);
                ts->target_v86->regs.ebp = tswap32(env->regs[R_EBP]);
                ts->target_v86->regs.esp = tswap32(env->regs[R_ESP]);
                ts->target_v86->regs.eip = tswap32(env->eip);
                ts->target_v86->regs.cs = tswap16(env->segs[R_CS]);
                ts->target_v86->regs.ss = tswap16(env->segs[R_SS]);
                ts->target_v86->regs.ds = tswap16(env->segs[R_DS]);
                ts->target_v86->regs.es = tswap16(env->segs[R_ES]);
                ts->target_v86->regs.fs = tswap16(env->segs[R_FS]);
                ts->target_v86->regs.gs = tswap16(env->segs[R_GS]);

                /* restore 32 bit registers */
                env->regs[R_EBX] = ts->vm86_saved_regs.ebx;
                env->regs[R_ECX] = ts->vm86_saved_regs.ecx;
                env->regs[R_EDX] = ts->vm86_saved_regs.edx;
                env->regs[R_ESI] = ts->vm86_saved_regs.esi;
                env->regs[R_EDI] = ts->vm86_saved_regs.edi;
                env->regs[R_EBP] = ts->vm86_saved_regs.ebp;
                env->regs[R_ESP] = ts->vm86_saved_regs.esp;
                env->eflags = ts->vm86_saved_regs.eflags;
                env->eip = ts->vm86_saved_regs.eip;

                cpu_x86_load_seg(env, R_CS, ts->vm86_saved_regs.cs);
                cpu_x86_load_seg(env, R_SS, ts->vm86_saved_regs.ss);
                cpu_x86_load_seg(env, R_DS, ts->vm86_saved_regs.ds);
                cpu_x86_load_seg(env, R_ES, ts->vm86_saved_regs.es);
                cpu_x86_load_seg(env, R_FS, ts->vm86_saved_regs.fs);
                cpu_x86_load_seg(env, R_GS, ts->vm86_saved_regs.gs);

                env->regs[R_EAX] = ret;
            } else {
                if (pc[0] == 0xcd && pc[1] == 0x80) {
                    /* syscall */
                    env->eip += 2;
                    env->regs[R_EAX] = do_syscall(env, 
                                                  env->regs[R_EAX], 
                                                  env->regs[R_EBX],
                                                  env->regs[R_ECX],
                                                  env->regs[R_EDX],
                                                  env->regs[R_ESI],
                                                  env->regs[R_EDI],
                                                  env->regs[R_EBP]);
                } else {
                    /* XXX: more precise info */
                    info.si_signo = SIGSEGV;
                    info.si_errno = 0;
                    info.si_code = 0;
                    info._sifields._sigfault._addr = 0;
                    queue_signal(info.si_signo, &info);
                }
            }
            break;
        case EXCP00_DIVZ:
            /* division by zero */
            info.si_signo = SIGFPE;
            info.si_errno = 0;
            info.si_code = TARGET_FPE_INTDIV;
            info._sifields._sigfault._addr = env->eip;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP04_INTO:
        case EXCP05_BOUND:
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            info.si_code = 0;
            info._sifields._sigfault._addr = 0;
            queue_signal(info.si_signo, &info);
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
            fprintf(stderr, "0x%08lx: Unknown exception CPU %d, aborting\n", 
                    (long)pc, err);
            abort();
        }
        process_pending_signals(env);
    }
}

void usage(void)
{
    printf("qemu version " QEMU_VERSION ", Copyright (c) 2003 Fabrice Bellard\n"
           "usage: qemu [-h] [-d] [-L path] [-s size] program [arguments...]\n"
           "Linux x86 emulator\n"
           "\n"
           "-h        print this help\n"
           "-d        activate log (logfile=%s)\n"
           "-L path   set the x86 elf interpreter prefix (default=%s)\n"
           "-s size   set the x86 stack size in bytes (default=%ld)\n",
           DEBUG_LOGFILE,
           interp_prefix, 
           x86_stack_size);
    exit(1);
}

/* XXX: currently only used for async signals (see signal.c) */
CPUX86State *global_env;
/* used to free thread contexts */
TaskState *first_task_state;

int main(int argc, char **argv)
{
    const char *filename;
    struct target_pt_regs regs1, *regs = &regs1;
    struct image_info info1, *info = &info1;
    TaskState ts1, *ts = &ts1;
    CPUX86State *env;
    int optind;
    const char *r;
    
    if (argc <= 1)
        usage();
    loglevel = 0;
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
            loglevel = 1;
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
        } else {
            usage();
        }
    }
    if (optind >= argc)
        usage();
    filename = argv[optind];

    /* init debug */
    if (loglevel) {
        logfile = fopen(DEBUG_LOGFILE, "w");
        if (!logfile) {
            perror(DEBUG_LOGFILE);
            exit(1);
        }
        setvbuf(logfile, NULL, _IOLBF, 0);
    }

    /* Zero out regs */
    memset(regs, 0, sizeof(struct target_pt_regs));

    /* Zero out image_info */
    memset(info, 0, sizeof(struct image_info));

    if(elf_exec(interp_prefix, filename, argv+optind, environ, regs, info) != 0) {
	printf("Error loading %s\n", filename);
	exit(1);
    }
    
    if (loglevel) {
        fprintf(logfile, "start_brk   0x%08lx\n" , info->start_brk);
        fprintf(logfile, "end_code    0x%08lx\n" , info->end_code);
        fprintf(logfile, "start_code  0x%08lx\n" , info->start_code);
        fprintf(logfile, "end_data    0x%08lx\n" , info->end_data);
        fprintf(logfile, "start_stack 0x%08lx\n" , info->start_stack);
        fprintf(logfile, "brk         0x%08lx\n" , info->brk);
        fprintf(logfile, "esp         0x%08lx\n" , regs->esp);
        fprintf(logfile, "eip         0x%08lx\n" , regs->eip);
    }

    target_set_brk((char *)info->brk);
    syscall_init();
    signal_init();

    env = cpu_x86_init();
    global_env = env;

    /* build Task State */
    memset(ts, 0, sizeof(TaskState));
    env->opaque = ts;
    ts->used = 1;
    
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

    /* linux segment setup */
    env->gdt.base = (void *)gdt_table;
    env->gdt.limit = sizeof(gdt_table) - 1;
    write_dt(&gdt_table[__USER_CS >> 3], 0, 0xffffffff, 1);
    write_dt(&gdt_table[__USER_DS >> 3], 0, 0xffffffff, 1);
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    cpu_x86_load_seg(env, R_FS, __USER_DS);
    cpu_x86_load_seg(env, R_GS, __USER_DS);

    cpu_loop(env);
    /* never exits */
    return 0;
}
