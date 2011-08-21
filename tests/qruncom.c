/*
 * Example of use of user mode libqemu: launch a basic .com DOS
 * executable
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <malloc.h>

#include "cpu.h"

//#define SIGTEST

int cpu_get_pic_interrupt(CPUState *env)
{
    return -1;
}

uint64_t cpu_get_tsc(CPUState *env)
{
    return 0;
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

uint64_t idt_table[256];

/* only dpl matters as we do only user space emulation */
static void set_idt(int n, unsigned int dpl)
{
    set_gate(idt_table + n, 0, dpl, 0, 0);
}

void g_free(void *ptr)
{
    free(ptr);
}

void *g_malloc(size_t size)
{
    return malloc(size);
}

void *g_malloc0(size_t size)
{
    void *ptr;
    ptr = g_malloc(size);
    if (!ptr)
        return NULL;
    memset(ptr, 0, size);
    return ptr;
}

void *qemu_vmalloc(size_t size)
{
    return memalign(4096, size);
}

void qemu_vfree(void *ptr)
{
    free(ptr);
}

void qemu_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* XXX: this is a bug in helper2.c */
int errno;

/**********************************************/

#define COM_BASE_ADDR    0x10100

static void usage(void)
{
    printf("qruncom version 0.1 (c) 2003 Fabrice Bellard\n"
           "usage: qruncom file.com\n"
           "user mode libqemu demo: run simple .com DOS executables\n");
    exit(1);
}

static inline uint8_t *seg_to_linear(unsigned int seg, unsigned int reg)
{
    return (uint8_t *)((seg << 4) + (reg & 0xffff));
}

static inline void pushw(CPUState *env, int val)
{
    env->regs[R_ESP] = (env->regs[R_ESP] & ~0xffff) | ((env->regs[R_ESP] - 2) & 0xffff);
    *(uint16_t *)seg_to_linear(env->segs[R_SS].selector, env->regs[R_ESP]) = val;
}

static void host_segv_handler(int host_signum, siginfo_t *info,
                              void *puc)
{
    if (cpu_signal_handler(host_signum, info, puc)) {
        return;
    }
    abort();
}

int main(int argc, char **argv)
{
    uint8_t *vm86_mem;
    const char *filename;
    int fd, ret, seg;
    CPUState *env;

    if (argc != 2)
        usage();
    filename = argv[1];

    vm86_mem = mmap((void *)0x00000000, 0x110000,
                    PROT_WRITE | PROT_READ | PROT_EXEC,
                    MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
    if (vm86_mem == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    /* load the MSDOS .com executable */
    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror(filename);
        exit(1);
    }
    ret = read(fd, vm86_mem + COM_BASE_ADDR, 65536 - 256);
    if (ret < 0) {
        perror("read");
        exit(1);
    }
    close(fd);

    /* install exception handler for CPU emulator */
    {
        struct sigaction act;

        sigfillset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO;
        //        act.sa_flags |= SA_ONSTACK;

        act.sa_sigaction = host_segv_handler;
        sigaction(SIGSEGV, &act, NULL);
        sigaction(SIGBUS, &act, NULL);
    }

    //    cpu_set_log(CPU_LOG_TB_IN_ASM | CPU_LOG_TB_OUT_ASM | CPU_LOG_EXEC);

    env = cpu_init("qemu32");

    cpu_x86_set_cpl(env, 3);

    env->cr[0] = CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK;
    /* NOTE: hflags duplicates some of the virtual CPU state */
    env->hflags |= HF_PE_MASK | VM_MASK;

    /* flags setup : we activate the IRQs by default as in user
       mode. We also activate the VM86 flag to run DOS code */
    env->eflags |= IF_MASK | VM_MASK;

    /* init basic registers */
    env->eip = 0x100;
    env->regs[R_ESP] = 0xfffe;
    seg = (COM_BASE_ADDR - 0x100) >> 4;

    cpu_x86_load_seg_cache(env, R_CS, seg,
                           (seg << 4), 0xffff, 0);
    cpu_x86_load_seg_cache(env, R_SS, seg,
                           (seg << 4), 0xffff, 0);
    cpu_x86_load_seg_cache(env, R_DS, seg,
                           (seg << 4), 0xffff, 0);
    cpu_x86_load_seg_cache(env, R_ES, seg,
                           (seg << 4), 0xffff, 0);
    cpu_x86_load_seg_cache(env, R_FS, seg,
                           (seg << 4), 0xffff, 0);
    cpu_x86_load_seg_cache(env, R_GS, seg,
                           (seg << 4), 0xffff, 0);

    /* exception support */
    env->idt.base = (unsigned long)idt_table;
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

    /* put return code */
    *seg_to_linear(env->segs[R_CS].selector, 0) = 0xb4; /* mov ah, $0 */
    *seg_to_linear(env->segs[R_CS].selector, 1) = 0x00;
    *seg_to_linear(env->segs[R_CS].selector, 2) = 0xcd; /* int $0x21 */
    *seg_to_linear(env->segs[R_CS].selector, 3) = 0x21;
    pushw(env, 0x0000);

    /* the value of these registers seem to be assumed by pi_10.com */
    env->regs[R_ESI] = 0x100;
    env->regs[R_ECX] = 0xff;
    env->regs[R_EBP] = 0x0900;
    env->regs[R_EDI] = 0xfffe;

    /* inform the emulator of the mmaped memory */
    page_set_flags(0x00000000, 0x110000,
                   PAGE_WRITE | PAGE_READ | PAGE_EXEC | PAGE_VALID);

    for(;;) {
        ret = cpu_x86_exec(env);
        switch(ret) {
        case EXCP0D_GPF:
            {
                int int_num, ah;
                int_num = *(uint8_t *)(env->segs[R_CS].base + env->eip + 1);
                if (int_num != 0x21)
                    goto unknown_int;
                ah = (env->regs[R_EAX] >> 8) & 0xff;
                switch(ah) {
                case 0x00: /* exit */
                    exit(0);
                case 0x02: /* write char */
                    {
                        uint8_t c = env->regs[R_EDX];
                        write(1, &c, 1);
                    }
                    break;
                case 0x09: /* write string */
                    {
                        uint8_t c;
                        for(;;) {
                            c = *seg_to_linear(env->segs[R_DS].selector, env->regs[R_EAX]);
                            if (c == '$')
                                break;
                            write(1, &c, 1);
                        }
                        env->regs[R_EAX] = (env->regs[R_EAX] & ~0xff) | '$';
                    }
                    break;
                default:
                unknown_int:
                    fprintf(stderr, "unsupported int 0x%02x\n", int_num);
                    cpu_dump_state(env, stderr, fprintf, 0);
                    //                    exit(1);
                }
                env->eip += 2;
            }
            break;
        default:
            fprintf(stderr, "unhandled cpu_exec return code (0x%x)\n", ret);
            cpu_dump_state(env, stderr, fprintf, 0);
            exit(1);
        }
    }
}
