/*
 *  i386 emulator main execution loop
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "exec-i386.h"
#include "disas.h"

//#define DEBUG_EXEC
#define DEBUG_FLUSH
//#define DEBUG_SIGNAL

/* main execution loop */

/* maximum total translate dcode allocated */
#define CODE_GEN_BUFFER_SIZE     (2048 * 1024)
//#define CODE_GEN_BUFFER_SIZE     (128 * 1024)
#define CODE_GEN_MAX_SIZE        65536
#define CODE_GEN_ALIGN           16 /* must be >= of the size of a icache line */

/* threshold to flush the translated code buffer */
#define CODE_GEN_BUFFER_MAX_SIZE (CODE_GEN_BUFFER_SIZE - CODE_GEN_MAX_SIZE)

#define CODE_GEN_MAX_BLOCKS    (CODE_GEN_BUFFER_SIZE / 64)
#define CODE_GEN_HASH_BITS     15
#define CODE_GEN_HASH_SIZE     (1 << CODE_GEN_HASH_BITS)

typedef struct TranslationBlock {
    unsigned long pc;   /* simulated PC corresponding to this block (EIP + CS base) */
    unsigned long cs_base; /* CS base for this block */
    unsigned int flags; /* flags defining in which context the code was generated */
    uint8_t *tc_ptr;    /* pointer to the translated code */
    struct TranslationBlock *hash_next; /* next matching block */
} TranslationBlock;

TranslationBlock tbs[CODE_GEN_MAX_BLOCKS];
TranslationBlock *tb_hash[CODE_GEN_HASH_SIZE];
int nb_tbs;

uint8_t code_gen_buffer[CODE_GEN_BUFFER_SIZE];
uint8_t *code_gen_ptr;

/* thread support */

#ifdef __powerpc__
static inline int testandset (int *p)
{
    int ret;
    __asm__ __volatile__ (
                          "0:    lwarx %0,0,%1 ;"
                          "      xor. %0,%3,%0;"
                          "      bne 1f;"
                          "      stwcx. %2,0,%1;"
                          "      bne- 0b;"
                          "1:    "
                          : "=&r" (ret)
                          : "r" (p), "r" (1), "r" (0)
                          : "cr0", "memory");
    return ret;
}
#endif

#ifdef __i386__
static inline int testandset (int *p)
{
    char ret;
    long int readval;
    
    __asm__ __volatile__ ("lock; cmpxchgl %3, %1; sete %0"
                          : "=q" (ret), "=m" (*p), "=a" (readval)
                          : "r" (1), "m" (*p), "a" (0)
                          : "memory");
    return ret;
}
#endif

#ifdef __s390__
static inline int testandset (int *p)
{
    int ret;

    __asm__ __volatile__ ("0: cs    %0,%1,0(%2)\n"
			  "   jl    0b"
			  : "=&d" (ret)
			  : "r" (1), "a" (p), "0" (*p) 
			  : "cc", "memory" );
    return ret;
}
#endif

int global_cpu_lock = 0;

void cpu_lock(void)
{
    while (testandset(&global_cpu_lock));
}

void cpu_unlock(void)
{
    global_cpu_lock = 0;
}

/* exception support */
/* NOTE: not static to force relocation generation by GCC */
void raise_exception(int exception_index)
{
    /* NOTE: the register at this point must be saved by hand because
       longjmp restore them */
#ifdef reg_EAX
    env->regs[R_EAX] = EAX;
#endif
#ifdef reg_ECX
    env->regs[R_ECX] = ECX;
#endif
#ifdef reg_EDX
    env->regs[R_EDX] = EDX;
#endif
#ifdef reg_EBX
    env->regs[R_EBX] = EBX;
#endif
#ifdef reg_ESP
    env->regs[R_ESP] = ESP;
#endif
#ifdef reg_EBP
    env->regs[R_EBP] = EBP;
#endif
#ifdef reg_ESI
    env->regs[R_ESI] = ESI;
#endif
#ifdef reg_EDI
    env->regs[R_EDI] = EDI;
#endif
    env->exception_index = exception_index;
    longjmp(env->jmp_env, 1);
}

#if defined(DEBUG_EXEC)
static const char *cc_op_str[] = {
    "DYNAMIC",
    "EFLAGS",
    "MUL",
    "ADDB",
    "ADDW",
    "ADDL",
    "ADCB",
    "ADCW",
    "ADCL",
    "SUBB",
    "SUBW",
    "SUBL",
    "SBBB",
    "SBBW",
    "SBBL",
    "LOGICB",
    "LOGICW",
    "LOGICL",
    "INCB",
    "INCW",
    "INCL",
    "DECB",
    "DECW",
    "DECL",
    "SHLB",
    "SHLW",
    "SHLL",
    "SARB",
    "SARW",
    "SARL",
};

static void cpu_x86_dump_state(FILE *f)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= (DF & DF_MASK);
    fprintf(f, 
            "EAX=%08x EBX=%08X ECX=%08x EDX=%08x\n"
            "ESI=%08x EDI=%08X EBP=%08x ESP=%08x\n"
            "CCS=%08x CCD=%08x CCO=%-8s EFL=%c%c%c%c%c%c%c\n"
            "EIP=%08x\n",
            env->regs[R_EAX], env->regs[R_EBX], env->regs[R_ECX], env->regs[R_EDX], 
            env->regs[R_ESI], env->regs[R_EDI], env->regs[R_EBP], env->regs[R_ESP], 
            env->cc_src, env->cc_dst, cc_op_str[env->cc_op],
            eflags & DF_MASK ? 'D' : '-',
            eflags & CC_O ? 'O' : '-',
            eflags & CC_S ? 'S' : '-',
            eflags & CC_Z ? 'Z' : '-',
            eflags & CC_A ? 'A' : '-',
            eflags & CC_P ? 'P' : '-',
            eflags & CC_C ? 'C' : '-',
            env->eip);
#if 1
    fprintf(f, "ST0=%f ST1=%f ST2=%f ST3=%f\n", 
            (double)ST0, (double)ST1, (double)ST(2), (double)ST(3));
#endif
}

#endif

void cpu_x86_tblocks_init(void)
{
    if (!code_gen_ptr) {
        code_gen_ptr = code_gen_buffer;
    }
}

/* flush all the translation blocks */
static void tb_flush(void)
{
    int i;
#ifdef DEBUG_FLUSH
    printf("gemu: flush code_size=%d nb_tbs=%d avg_tb_size=%d\n", 
           code_gen_ptr - code_gen_buffer, 
           nb_tbs, 
           (code_gen_ptr - code_gen_buffer) / nb_tbs);
#endif
    nb_tbs = 0;
    for(i = 0;i < CODE_GEN_HASH_SIZE; i++)
        tb_hash[i] = NULL;
    code_gen_ptr = code_gen_buffer;
    /* XXX: flush processor icache at this point */
}

/* find a translation block in the translation cache. If not found,
   return NULL and the pointer to the last element of the list in pptb */
static inline TranslationBlock *tb_find(TranslationBlock ***pptb,
                                        unsigned long pc, 
                                        unsigned long cs_base,
                                        unsigned int flags)
{
    TranslationBlock **ptb, *tb;
    unsigned int h;
 
    h = pc & (CODE_GEN_HASH_SIZE - 1);
    ptb = &tb_hash[h];
    for(;;) {
        tb = *ptb;
        if (!tb)
            break;
        if (tb->pc == pc && tb->cs_base == cs_base && tb->flags == flags)
            return tb;
        ptb = &tb->hash_next;
    }
    *pptb = ptb;
    return NULL;
}

/* allocate a new translation block. flush the translation buffer if
   too many translation blocks or too much generated code */
static inline TranslationBlock *tb_alloc(void)
{
    TranslationBlock *tb;
    if (nb_tbs >= CODE_GEN_MAX_BLOCKS || 
        (code_gen_ptr - code_gen_buffer) >= CODE_GEN_BUFFER_MAX_SIZE)
        tb_flush();
    tb = &tbs[nb_tbs++];
    return tb;
}

int cpu_x86_exec(CPUX86State *env1)
{
    int saved_T0, saved_T1, saved_A0;
    CPUX86State *saved_env;
#ifdef reg_EAX
    int saved_EAX;
#endif
#ifdef reg_ECX
    int saved_ECX;
#endif
#ifdef reg_EDX
    int saved_EDX;
#endif
#ifdef reg_EBX
    int saved_EBX;
#endif
#ifdef reg_ESP
    int saved_ESP;
#endif
#ifdef reg_EBP
    int saved_EBP;
#endif
#ifdef reg_ESI
    int saved_ESI;
#endif
#ifdef reg_EDI
    int saved_EDI;
#endif
    int code_gen_size, ret;
    void (*gen_func)(void);
    TranslationBlock *tb, **ptb;
    uint8_t *tc_ptr, *cs_base, *pc;
    unsigned int flags;

    /* first we save global registers */
    saved_T0 = T0;
    saved_T1 = T1;
    saved_A0 = A0;
    saved_env = env;
    env = env1;
#ifdef reg_EAX
    saved_EAX = EAX;
    EAX = env->regs[R_EAX];
#endif
#ifdef reg_ECX
    saved_ECX = ECX;
    ECX = env->regs[R_ECX];
#endif
#ifdef reg_EDX
    saved_EDX = EDX;
    EDX = env->regs[R_EDX];
#endif
#ifdef reg_EBX
    saved_EBX = EBX;
    EBX = env->regs[R_EBX];
#endif
#ifdef reg_ESP
    saved_ESP = ESP;
    ESP = env->regs[R_ESP];
#endif
#ifdef reg_EBP
    saved_EBP = EBP;
    EBP = env->regs[R_EBP];
#endif
#ifdef reg_ESI
    saved_ESI = ESI;
    ESI = env->regs[R_ESI];
#endif
#ifdef reg_EDI
    saved_EDI = EDI;
    EDI = env->regs[R_EDI];
#endif
    
    /* put eflags in CPU temporary format */
    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    env->interrupt_request = 0;
    
    /* prepare setjmp context for exception handling */
    if (setjmp(env->jmp_env) == 0) {
        for(;;) {
            if (env->interrupt_request) {
                raise_exception(EXCP_INTERRUPT);
            }
#ifdef DEBUG_EXEC
            if (loglevel) {
                cpu_x86_dump_state(logfile);
            }
#endif
            /* we compute the CPU state. We assume it will not
               change during the whole generated block. */
            flags = env->seg_cache[R_CS].seg_32bit << GEN_FLAG_CODE32_SHIFT;
            flags |= env->seg_cache[R_SS].seg_32bit << GEN_FLAG_SS32_SHIFT;
            flags |= (((unsigned long)env->seg_cache[R_DS].base | 
                       (unsigned long)env->seg_cache[R_ES].base |
                       (unsigned long)env->seg_cache[R_SS].base) != 0) << 
                GEN_FLAG_ADDSEG_SHIFT;
            flags |= (env->eflags & VM_MASK) >> (17 - GEN_FLAG_VM_SHIFT);
            cs_base = env->seg_cache[R_CS].base;
            pc = cs_base + env->eip;
            tb = tb_find(&ptb, (unsigned long)pc, (unsigned long)cs_base, 
                         flags);
            if (!tb) {
                /* if no translated code available, then translate it now */
                /* XXX: very inefficient: we lock all the cpus when
                   generating code */
                cpu_lock();
                tc_ptr = code_gen_ptr;
                ret = cpu_x86_gen_code(code_gen_ptr, CODE_GEN_MAX_SIZE, 
                                       &code_gen_size, pc, cs_base, flags);
                /* if invalid instruction, signal it */
                if (ret != 0) {
                    cpu_unlock();
                    raise_exception(EXCP06_ILLOP);
                }
                tb = tb_alloc();
                *ptb = tb;
                tb->pc = (unsigned long)pc;
                tb->cs_base = (unsigned long)cs_base;
                tb->flags = flags;
                tb->tc_ptr = tc_ptr;
                tb->hash_next = NULL;
                code_gen_ptr = (void *)(((unsigned long)code_gen_ptr + code_gen_size + CODE_GEN_ALIGN - 1) & ~(CODE_GEN_ALIGN - 1));
                cpu_unlock();
            }
	    if (loglevel) {
		fprintf(logfile, "Trace 0x%08lx [0x%08lx] %s\n",
			(long)tb->tc_ptr, (long)tb->pc,
			lookup_symbol((void *)tb->pc));
		fflush(logfile);
	    }
            /* execute the generated code */
            tc_ptr = tb->tc_ptr;
            gen_func = (void *)tc_ptr;
            gen_func();
        }
    }
    ret = env->exception_index;

    /* restore flags in standard format */
    env->eflags = env->eflags | cc_table[CC_OP].compute_all() | (DF & DF_MASK);

    /* restore global registers */
#ifdef reg_EAX
    EAX = saved_EAX;
#endif
#ifdef reg_ECX
    ECX = saved_ECX;
#endif
#ifdef reg_EDX
    EDX = saved_EDX;
#endif
#ifdef reg_EBX
    EBX = saved_EBX;
#endif
#ifdef reg_ESP
    ESP = saved_ESP;
#endif
#ifdef reg_EBP
    EBP = saved_EBP;
#endif
#ifdef reg_ESI
    ESI = saved_ESI;
#endif
#ifdef reg_EDI
    EDI = saved_EDI;
#endif
    T0 = saved_T0;
    T1 = saved_T1;
    A0 = saved_A0;
    env = saved_env;
    return ret;
}

void cpu_x86_interrupt(CPUX86State *s)
{
    s->interrupt_request = 1;
}


void cpu_x86_load_seg(CPUX86State *s, int seg_reg, int selector)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = s;
    load_seg(seg_reg, selector);
    env = saved_env;
}

#undef EAX
#undef ECX
#undef EDX
#undef EBX
#undef ESP
#undef EBP
#undef ESI
#undef EDI
#undef EIP
#include <signal.h>
#include <sys/ucontext.h>

static inline int handle_cpu_signal(unsigned long pc,
                                    sigset_t *old_set)
{
#ifdef DEBUG_SIGNAL
    printf("gemu: SIGSEGV pc=0x%08lx oldset=0x%08lx\n", 
           pc, *(unsigned long *)old_set);
#endif
    if (pc >= (unsigned long)code_gen_buffer &&
        pc < (unsigned long)code_gen_buffer + CODE_GEN_BUFFER_SIZE) {
        /* the PC is inside the translated code. It means that we have
           a virtual CPU fault */
        /* we restore the process signal mask as the sigreturn should
           do it */
        sigprocmask(SIG_SETMASK, old_set, NULL);
        /* XXX: need to compute virtual pc position by retranslating
           code. The rest of the CPU state should be correct. */
        raise_exception(EXCP0D_GPF);
        /* never comes here */
        return 1;
    } else {
        return 0;
    }
}

int cpu_x86_signal_handler(int host_signum, struct siginfo *info, 
                           void *puc)
{
#if defined(__i386__)
    struct ucontext *uc = puc;
    unsigned long pc;
    sigset_t *pold_set;
    
#ifndef REG_EIP
/* for glibc 2.1 */
#define REG_EIP EIP
#endif
    pc = uc->uc_mcontext.gregs[REG_EIP];
    pold_set = &uc->uc_sigmask;
    return handle_cpu_signal(pc, pold_set);
#else
#warning No CPU specific signal handler: cannot handle target SIGSEGV events
    return 0;
#endif
}
