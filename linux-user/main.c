/*
 *  emu main
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
#include <errno.h>
#include <unistd.h>

#include "gemu.h"

#include "cpu-i386.h"

#define DEBUG_LOGFILE "/tmp/gemu.log"

FILE *logfile = NULL;
int loglevel;

unsigned long x86_stack_size;
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


/* XXX: currently we use LDT entries */
#define __USER_CS	(0x23|4)
#define __USER_DS	(0x2B|4)

void usage(void)
{
    printf("gemu version 0.1, Copyright (c) 2003 Fabrice Bellard\n"
           "usage: gemu [-d] program [arguments...]\n"
           "Linux x86 emulator\n"
           );
    exit(1);
}

int main(int argc, char **argv)
{
    const char *filename;
    struct target_pt_regs regs1, *regs = &regs1;
    struct image_info info1, *info = &info1;
    CPUX86State *env;
    int optind;

    if (argc <= 1)
        usage();
    loglevel = 0;
    optind = 1;
    if (argv[optind] && !strcmp(argv[optind], "-d")) {
        loglevel = 1;
        optind++;
    }
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

    if(elf_exec(filename, argv+optind, environ, regs, info) != 0) {
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

    env = cpu_x86_init();

    env->regs[R_EAX] = regs->eax;
    env->regs[R_EBX] = regs->ebx;
    env->regs[R_ECX] = regs->ecx;
    env->regs[R_EDX] = regs->edx;
    env->regs[R_ESI] = regs->esi;
    env->regs[R_EDI] = regs->edi;
    env->regs[R_EBP] = regs->ebp;
    env->regs[R_ESP] = regs->esp;
    env->segs[R_CS] = __USER_CS;
    env->segs[R_DS] = __USER_DS;
    env->segs[R_ES] = __USER_DS;
    env->segs[R_SS] = __USER_DS;
    env->segs[R_FS] = __USER_DS;
    env->segs[R_GS] = __USER_DS;
    env->pc = regs->eip;

#if 0
    LDT[__USER_CS >> 3].w86Flags = DF_PRESENT | DF_PAGES | DF_32;
    LDT[__USER_CS >> 3].dwSelLimit = 0xfffff;
    LDT[__USER_CS >> 3].lpSelBase = NULL;

    LDT[__USER_DS >> 3].w86Flags = DF_PRESENT | DF_PAGES | DF_32;
    LDT[__USER_DS >> 3].dwSelLimit = 0xfffff;
    LDT[__USER_DS >> 3].lpSelBase = NULL;
#endif

    for(;;) {
        int err;
        uint8_t *pc;
        
        err = cpu_x86_exec(env);
        switch(err) {
        case EXCP0D_GPF:
            pc = (uint8_t *)env->pc;
            if (pc[0] == 0xcd && pc[1] == 0x80) {
                /* syscall */
                env->pc += 2;
                env->regs[R_EAX] = do_syscall(env->regs[R_EAX], 
                                              env->regs[R_EBX],
                                              env->regs[R_ECX],
                                              env->regs[R_EDX],
                                              env->regs[R_ESI],
                                              env->regs[R_EDI],
                                              env->regs[R_EBP]);
            } else {
                goto trap_error;
            }
            break;
        default:
        trap_error:
            fprintf(stderr, "0x%08lx: Unknown exception %d, aborting\n", 
                    (long)env->pc, err);
            abort();
        }
    }
    return 0;
}
