/*
 * Simple example of use of vm86: launch a basic .com DOS executable
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>

#include <linux/unistd.h>
#include <asm/vm86.h>

//#define SIGTEST

#undef __syscall_return
#define __syscall_return(type, res) \
do { \
	return (type) (res); \
} while (0)

_syscall2(int, vm86, int, func, struct vm86plus_struct *, v86)

#define COM_BASE_ADDR    0x10100

static void usage(void)
{
    printf("runcom version 0.1 (c) 2003 Fabrice Bellard\n"
           "usage: runcom file.com\n"
           "VM86 Run simple .com DOS executables (linux vm86 test mode)\n");
    exit(1);
}

static inline void set_bit(uint8_t *a, unsigned int bit)
{
    a[bit / 8] |= (1 << (bit % 8));
}

static inline uint8_t *seg_to_linear(unsigned int seg, unsigned int reg)
{
    return (uint8_t *)((seg << 4) + (reg & 0xffff));
}

static inline void pushw(struct vm86_regs *r, int val)
{
    r->esp = (r->esp & ~0xffff) | ((r->esp - 2) & 0xffff);
    *(uint16_t *)seg_to_linear(r->ss, r->esp) = val;
}

void dump_regs(struct vm86_regs *r)
{
    fprintf(stderr,
            "EAX=%08lx EBX=%08lx ECX=%08lx EDX=%08lx\n"
            "ESI=%08lx EDI=%08lx EBP=%08lx ESP=%08lx\n"
            "EIP=%08lx EFL=%08lx\n"
            "CS=%04x DS=%04x ES=%04x SS=%04x FS=%04x GS=%04x\n",
            r->eax, r->ebx, r->ecx, r->edx, r->esi, r->edi, r->ebp, r->esp,
            r->eip, r->eflags,
            r->cs, r->ds, r->es, r->ss, r->fs, r->gs);
}

#ifdef SIGTEST
void alarm_handler(int sig)
{
    fprintf(stderr, "alarm signal=%d\n", sig);
    alarm(1);
}
#endif

int main(int argc, char **argv)
{
    uint8_t *vm86_mem;
    const char *filename;
    int fd, ret, seg;
    struct vm86plus_struct ctx;
    struct vm86_regs *r;

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
#ifdef SIGTEST
    {
        struct sigaction act;

        act.sa_handler = alarm_handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        sigaction(SIGALRM, &act, NULL);
        alarm(1);
    }
#endif

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

    memset(&ctx, 0, sizeof(ctx));
    /* init basic registers */
    r = &ctx.regs;
    r->eip = 0x100;
    r->esp = 0xfffe;
    seg = (COM_BASE_ADDR - 0x100) >> 4;
    r->cs = seg;
    r->ss = seg;
    r->ds = seg;
    r->es = seg;
    r->fs = seg;
    r->gs = seg;
    r->eflags = VIF_MASK;

    /* put return code */
    set_bit((uint8_t *)&ctx.int_revectored, 0x21);
    *seg_to_linear(r->cs, 0) = 0xb4; /* mov ah, $0 */
    *seg_to_linear(r->cs, 1) = 0x00;
    *seg_to_linear(r->cs, 2) = 0xcd; /* int $0x21 */
    *seg_to_linear(r->cs, 3) = 0x21;
    pushw(&ctx.regs, 0x0000);

    /* the value of these registers seem to be assumed by pi_10.com */
    r->esi = 0x100;
    r->ecx = 0xff;
    r->ebp = 0x0900;
    r->edi = 0xfffe;

    for(;;) {
        ret = vm86(VM86_ENTER, &ctx);
        switch(VM86_TYPE(ret)) {
        case VM86_INTx:
            {
                int int_num, ah;

                int_num = VM86_ARG(ret);
                if (int_num != 0x21)
                    goto unknown_int;
                ah = (r->eax >> 8) & 0xff;
                switch(ah) {
                case 0x00: /* exit */
                    exit(0);
                case 0x02: /* write char */
                    {
                        uint8_t c = r->edx;
                        write(1, &c, 1);
                    }
                    break;
                case 0x09: /* write string */
                    {
                        uint8_t c;
                        for(;;) {
                            c = *seg_to_linear(r->ds, r->edx);
                            if (c == '$')
                                break;
                            write(1, &c, 1);
                        }
                        r->eax = (r->eax & ~0xff) | '$';
                    }
                    break;
                default:
                unknown_int:
                    fprintf(stderr, "unsupported int 0x%02x\n", int_num);
                    dump_regs(&ctx.regs);
                    //                    exit(1);
                }
            }
            break;
        case VM86_SIGNAL:
            /* a signal came, we just ignore that */
            break;
        case VM86_STI:
            break;
        default:
            fprintf(stderr, "unhandled vm86 return code (0x%x)\n", ret);
            dump_regs(&ctx.regs);
            exit(1);
        }
    }
}
