/*
 * Common code for arch-specific MMU_INST_FETCH fault testing.
 */

#define _GNU_SOURCE

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ucontext.h>

/* Forward declarations. */

static void *arch_mcontext_pc(const mcontext_t *ctx);
static int arch_mcontext_arg(const mcontext_t *ctx);
static void arch_flush(void *p, int len);

/* Testing infrastructure. */

struct noexec_test {
    const char *name;
    const char *test_code;
    int test_len;
    int page_ofs;
    int entry_ofs;
    int expected_si_ofs;
    int expected_pc_ofs;
    int expected_arg;
};

static void *page_base;
static int page_size;
static const struct noexec_test *current_noexec_test;

static void handle_err(const char *syscall)
{
    printf("[  FAILED  ] %s: %s\n", syscall, strerror(errno));
    exit(EXIT_FAILURE);
}

static void handle_segv(int sig, siginfo_t *info, void *ucontext)
{
    const struct noexec_test *test = current_noexec_test;
    const mcontext_t *mc = &((ucontext_t *)ucontext)->uc_mcontext;
    void *expected_si;
    void *expected_pc;
    void *pc;
    int arg;

    if (test == NULL) {
        printf("[  FAILED  ] unexpected SEGV\n");
        exit(EXIT_FAILURE);
    }
    current_noexec_test = NULL;

    expected_si = page_base + test->expected_si_ofs;
    if (info->si_addr != expected_si) {
        printf("[  FAILED  ] wrong si_addr (%p != %p)\n",
               info->si_addr, expected_si);
        exit(EXIT_FAILURE);
    }

    pc = arch_mcontext_pc(mc);
    expected_pc = page_base + test->expected_pc_ofs;
    if (pc != expected_pc) {
        printf("[  FAILED  ] wrong pc (%p != %p)\n", pc, expected_pc);
        exit(EXIT_FAILURE);
    }

    arg = arch_mcontext_arg(mc);
    if (arg != test->expected_arg) {
        printf("[  FAILED  ] wrong arg (%d != %d)\n", arg, test->expected_arg);
        exit(EXIT_FAILURE);
    }

    if (mprotect(page_base, page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
        handle_err("mprotect");
    }
}

static void test_noexec_1(const struct noexec_test *test)
{
    void *start = page_base + test->page_ofs;
    void (*fn)(int arg) = page_base + test->entry_ofs;

    memcpy(start, test->test_code, test->test_len);
    arch_flush(start, test->test_len);

    /* Trigger TB creation in order to test invalidation. */
    fn(0);

    if (mprotect(page_base, page_size, PROT_NONE) < 0) {
        handle_err("mprotect");
    }

    /* Trigger SEGV and check that handle_segv() ran. */
    current_noexec_test = test;
    fn(0);
    assert(current_noexec_test == NULL);
}

static int test_noexec(struct noexec_test *tests, size_t n_tests)
{
    struct sigaction act;
    size_t i;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_segv;
    act.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &act, NULL) < 0) {
        handle_err("sigaction");
    }

    page_size = getpagesize();
    page_base = mmap(NULL, 2 * page_size,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (page_base == MAP_FAILED) {
        handle_err("mmap");
    }
    page_base += page_size;

    for (i = 0; i < n_tests; i++) {
        struct noexec_test *test = &tests[i];

        printf("[ RUN      ] %s\n", test->name);
        test_noexec_1(test);
        printf("[       OK ]\n");
    }

    printf("[  PASSED  ]\n");
    return EXIT_SUCCESS;
}
