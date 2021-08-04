#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

/*
 * Various instructions that generate SIGILL and SIGSEGV. They could have been
 * defined in a separate .s file, but this would complicate the build, so the
 * inline asm is used instead.
 */

void illegal_op(void);
void after_illegal_op(void);
asm(".globl\tillegal_op\n"
    "illegal_op:\t.byte\t0x00,0x00\n"
    "\t.globl\tafter_illegal_op\n"
    "after_illegal_op:\tbr\t%r14");

void stg(void *dst, unsigned long src);
asm(".globl\tstg\n"
    "stg:\tstg\t%r3,0(%r2)\n"
    "\tbr\t%r14");

void mvc_8(void *dst, void *src);
asm(".globl\tmvc_8\n"
    "mvc_8:\tmvc\t0(8,%r2),0(%r3)\n"
    "\tbr\t%r14");

static void safe_puts(const char *s)
{
    write(0, s, strlen(s));
    write(0, "\n", 1);
}

enum exception {
    exception_operation,
    exception_translation,
    exception_protection,
};

static struct {
    int sig;
    void *addr;
    unsigned long psw_addr;
    enum exception exception;
} expected;

static void handle_signal(int sig, siginfo_t *info, void *ucontext)
{
    void *page;
    int err;

    if (sig != expected.sig) {
        safe_puts("[  FAILED  ] wrong signal");
        _exit(1);
    }

    if (info->si_addr != expected.addr) {
        safe_puts("[  FAILED  ] wrong si_addr");
        _exit(1);
    }

    if (((ucontext_t *)ucontext)->uc_mcontext.psw.addr != expected.psw_addr) {
        safe_puts("[  FAILED  ] wrong psw.addr");
        _exit(1);
    }

    switch (expected.exception) {
    case exception_translation:
        page = mmap(expected.addr, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (page != expected.addr) {
            safe_puts("[  FAILED  ] mmap() failed");
            _exit(1);
        }
        break;
    case exception_protection:
        err = mprotect(expected.addr, 4096, PROT_READ | PROT_WRITE);
        if (err != 0) {
            safe_puts("[  FAILED  ] mprotect() failed");
            _exit(1);
        }
        break;
    default:
        break;
    }
}

static void check_sigsegv(void *func, enum exception exception,
                          unsigned long val)
{
    int prot;
    unsigned long *page;
    unsigned long *addr;
    int err;

    prot = exception == exception_translation ? PROT_NONE : PROT_READ;
    page = mmap(NULL, 4096, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    if (exception == exception_translation) {
        /* Hopefully nothing will be mapped at this address. */
        err = munmap(page, 4096);
        assert(err == 0);
    }
    addr = page + (val & 0x1ff);

    expected.sig = SIGSEGV;
    expected.addr = page;
    expected.psw_addr = (unsigned long)func;
    expected.exception = exception;
    if (func == stg) {
        stg(addr, val);
    } else {
        assert(func == mvc_8);
        mvc_8(addr, &val);
    }
    assert(*addr == val);

    err = munmap(page, 4096);
    assert(err == 0);
}

int main(void)
{
    struct sigaction act;
    int err;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_signal;
    act.sa_flags = SA_SIGINFO;
    err = sigaction(SIGILL, &act, NULL);
    assert(err == 0);
    err = sigaction(SIGSEGV, &act, NULL);
    assert(err == 0);

    safe_puts("[ RUN      ] Operation exception");
    expected.sig = SIGILL;
    expected.addr = illegal_op;
    expected.psw_addr = (unsigned long)after_illegal_op;
    expected.exception = exception_operation;
    illegal_op();
    safe_puts("[       OK ]");

    safe_puts("[ RUN      ] Translation exception from stg");
    check_sigsegv(stg, exception_translation, 42);
    safe_puts("[       OK ]");

    safe_puts("[ RUN      ] Translation exception from mvc");
    check_sigsegv(mvc_8, exception_translation, 4242);
    safe_puts("[       OK ]");

    safe_puts("[ RUN      ] Protection exception from stg");
    check_sigsegv(stg, exception_protection, 424242);
    safe_puts("[       OK ]");

    safe_puts("[ RUN      ] Protection exception from mvc");
    check_sigsegv(mvc_8, exception_protection, 42424242);
    safe_puts("[       OK ]");

    safe_puts("[  PASSED  ]");

    _exit(0);
}
