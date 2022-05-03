#include <assert.h>
#include <execinfo.h>
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

#define DEFINE_ASM_FUNCTION(name, body) \
    asm(".globl " #name "\n" \
        #name ":\n" \
        ".cfi_startproc\n" \
        body "\n" \
        "br %r14\n" \
        ".cfi_endproc");

void illegal_op(void);
extern const char after_illegal_op;
DEFINE_ASM_FUNCTION(illegal_op,
    ".byte 0x00,0x00\n"
    ".globl after_illegal_op\n"
    "after_illegal_op:")

void stg(void *dst, unsigned long src);
DEFINE_ASM_FUNCTION(stg, "stg %r3,0(%r2)")

void mvc_8(void *dst, void *src);
DEFINE_ASM_FUNCTION(mvc_8, "mvc 0(8,%r2),0(%r3)")

extern const char return_from_main_1;

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
    int err, i, n_frames;
    void *frames[16];
    void *page;

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

    n_frames = backtrace(frames, sizeof(frames) / sizeof(frames[0]));
    for (i = 0; i < n_frames; i++) {
        if (frames[i] == &return_from_main_1) {
            break;
        }
    }
    if (i == n_frames) {
        safe_puts("[  FAILED  ] backtrace() is broken");
        _exit(1);
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

int main_1(void)
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
    expected.psw_addr = (unsigned long)&after_illegal_op;
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

/*
 * Define main() in assembly in order to test that unwinding from signal
 * handlers until main() works. This way we can define a specific point that
 * the unwinder should reach. This is also better than defining main() in C
 * and using inline assembly to call main_1(), since it's not easy to get all
 * the clobbers right.
 */

DEFINE_ASM_FUNCTION(main,
    "stmg %r14,%r15,112(%r15)\n"
    ".cfi_offset 14,-48\n"
    ".cfi_offset 15,-40\n"
    "lay %r15,-160(%r15)\n"
    ".cfi_def_cfa_offset 320\n"
    "brasl %r14,main_1\n"
    ".globl return_from_main_1\n"
    "return_from_main_1:\n"
    "lmg %r14,%r15,272(%r15)\n"
    ".cfi_restore 15\n"
    ".cfi_restore 14\n"
    ".cfi_def_cfa_offset 160");
