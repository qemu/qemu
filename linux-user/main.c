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

#include "gemu.h"

#include "i386/hsw_interp.h"

unsigned long x86_stack_size;
unsigned long stktop;

void gemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* virtual x86 CPU stuff */

extern int invoke_code16(Interp_ENV *, int, int);
extern int invoke_code32(Interp_ENV *, int);
extern char *e_print_cpuemu_regs(ENVPARAMS, int is32);
extern char *e_emu_disasm(ENVPARAMS, unsigned char *org, int is32);
extern void init_npu(void);

Interp_ENV env_global;
Interp_ENV *envp_global;

QWORD EMUtime = 0;

int CEmuStat = 0;

long instr_count;

/* who will initialize this? */
unsigned long io_bitmap[IO_BITMAP_SIZE+1];

/* debug flag, 0=disable 1..9=level */
int d_emu = 0;

unsigned long CRs[5] =
{
	0x00000013,	/* valid bits: 0xe005003f */
	0x00000000,	/* invalid */
	0x00000000,
	0x00000000,
	0x00000000
};

/*
 * DR0-3 = linear address of breakpoint 0-3
 * DR4=5 = reserved
 * DR6	b0-b3 = BP active
 *	b13   = BD
 *	b14   = BS
 *	b15   = BT
 * DR7	b0-b1 = G:L bp#0
 *	b2-b3 = G:L bp#1
 *	b4-b5 = G:L bp#2
 *	b6-b7 = G:L bp#3
 *	b8-b9 = GE:LE
 *	b13   = GD
 *	b16-19= LLRW bp#0	LL=00(1),01(2),11(4)
 *	b20-23= LLRW bp#1	RW=00(x),01(w),11(rw)
 *	b24-27= LLRW bp#2
 *	b28-31= LLRW bp#3
 */
unsigned long DRs[8] =
{
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0xffff1ff0,
	0x00000400,
	0xffff1ff0,
	0x00000400
};

unsigned long TRs[2] =
{
	0x00000000,
	0x00000000
};

void FatalAppExit(UINT wAction, LPCSTR lpText)
{
    fprintf(stderr, "Fatal error '%s' in CPU\n", lpText);
    exit(1);
}

int e_debug_check(unsigned char *PC)
{
    register unsigned long d7 = DRs[7];

    if (d7&0x03) {
	if (d7&0x30000) return 0;	/* only execute(00) bkp */
	if ((long)PC==DRs[0]) {
	    e_printf("DBRK: DR0 hit at %p\n",PC);
	    DRs[6] |= 1;
	    return 1;
	}
    }
    if (d7&0x0c) {
	if (d7&0x300000) return 0;
	if ((long)PC==DRs[1]) {
	    e_printf("DBRK: DR1 hit at %p\n",PC);
	    DRs[6] |= 2;
	    return 1;
	}
    }
    if (d7&0x30) {
	if (d7&0x3000000) return 0;
	if ((long)PC==DRs[2]) {
	    e_printf("DBRK: DR2 hit at %p\n",PC);
	    DRs[6] |= 4;
	    return 1;
	}
    }
    if (d7&0xc0) {
	if (d7&0x30000000) return 0;
	if ((long)PC==DRs[3]) {
	    e_printf("DBRK: DR3 hit at %p\n",PC);
	    DRs[6] |= 8;
	    return 1;
	}
    }
    return 0;
}

/* Debug stuff */
void logstr(unsigned long mask, const char *fmt,...) 
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* unconditional message into debug log and stderr */
#undef error
void error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

int PortIO(DWORD port, DWORD value, UINT size, BOOL is_write)
{
    fprintf(stderr, "IO: %s port=0x%lx value=0x%lx size=%d",
            is_write ? "write" : "read", port, value, size);
    return value;
}

void LogProcName(WORD wSel, WORD wOff, WORD wAction)
{

}

void INT_handler(int num, void *env)
{
  fprintf(stderr, "EM86: int %d\n", num);
}

/***********************************************************/

/* XXX: currently we use LDT entries */
#define __USER_CS	(0x23|4)
#define __USER_DS	(0x2B|4)

void usage(void)
{
    printf("gemu version 0.1, Copyright (c) 2003 Fabrice Bellard\n"
           "usage: gemu program [arguments...]\n"
           "Linux x86 emulator\n"
           );
    exit(1);
}

int main(int argc, char **argv)
{
    const char *filename;
    struct target_pt_regs regs1, *regs = &regs1;
    struct image_info info1, *info = &info1;
    Interp_ENV *env;

    if (argc <= 1)
        usage();
    
    filename = argv[1];

    /* Zero out regs */
    memset(regs, 0, sizeof(struct target_pt_regs));

    /* Zero out image_info */
    memset(info, 0, sizeof(struct image_info));

    if(elf_exec(filename, argv+1, environ, regs, info) != 0) {
	printf("Error loading %s\n", filename);
	exit(1);
    }
    
#if 0
    printf("start_brk   0x%08lx\n" , info->start_brk);
    printf("end_code    0x%08lx\n" , info->end_code);
    printf("start_code  0x%08lx\n" , info->start_code);
    printf("end_data    0x%08lx\n" , info->end_data);
    printf("start_stack 0x%08lx\n" , info->start_stack);
    printf("brk         0x%08lx\n" , info->brk);
    printf("esp         0x%08lx\n" , regs->esp);
    printf("eip         0x%08lx\n" , regs->eip);
#endif

    target_set_brk((char *)info->brk);
    syscall_init();

    env = &env_global;
    envp_global = env;
    memset(env, 0, sizeof(Interp_ENV));

    env->rax.e   = regs->eax;
    env->rbx.e   = regs->ebx;
    env->rcx.e   = regs->ecx;
    env->rdx.e   = regs->edx;
    env->rsi.esi = regs->esi;
    env->rdi.edi = regs->edi;
    env->rbp.ebp = regs->ebp;
    env->rsp.esp = regs->esp;
    env->cs.cs   = __USER_CS;
    env->ds.ds   = __USER_DS;
    env->es.es   = __USER_DS;
    env->ss.ss   = __USER_DS;
    env->fs.fs   = __USER_DS;
    env->gs.gs   = __USER_DS;
    env->trans_addr = regs->eip;

    LDT[__USER_CS >> 3].w86Flags = DF_PRESENT | DF_PAGES | DF_32;
    LDT[__USER_CS >> 3].dwSelLimit = 0xfffff;
    LDT[__USER_CS >> 3].lpSelBase = NULL;

    LDT[__USER_DS >> 3].w86Flags = DF_PRESENT | DF_PAGES | DF_32;
    LDT[__USER_DS >> 3].dwSelLimit = 0xfffff;
    LDT[__USER_DS >> 3].lpSelBase = NULL;
    init_npu();

    for(;;) {
        int err;
        uint8_t *pc;

        err = invoke_code32(env, -1);
        env->trans_addr = env->return_addr;
        pc = env->seg_regs[0] + env->trans_addr;
        switch(err) {
        case EXCP0D_GPF:
            if (pc[0] == 0xcd && pc[1] == 0x80) {
                /* syscall */
                env->trans_addr += 2;
                env->rax.e = do_syscall(env->rax.e, 
                                        env->rbx.e,
                                        env->rcx.e,
                                        env->rdx.e,
                                        env->rsi.esi,
                                        env->rdi.edi,
                                        env->rbp.ebp);
            } else {
                goto trap_error;
            }
            break;
        default:
        trap_error:
            fprintf(stderr, "GEMU: Unknown error %d, aborting\n", err);
#ifndef NO_TRACE_MSGS
            d_emu = 9;
            fprintf(stderr, "%s\n%s\n",
                    e_print_cpuemu_regs(env, 1), 
                    e_emu_disasm(env,pc,1));
#endif
            abort();
        }
    }
    return 0;
}
