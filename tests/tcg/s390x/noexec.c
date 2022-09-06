#include "../multiarch/noexec.c.inc"

static void *arch_mcontext_pc(const mcontext_t *ctx)
{
    return (void *)ctx->psw.addr;
}

static int arch_mcontext_arg(const mcontext_t *ctx)
{
    return ctx->gregs[2];
}

static void arch_flush(void *p, int len)
{
}

extern char noexec_1[];
extern char noexec_2[];
extern char noexec_end[];

asm("noexec_1:\n"
    "   lgfi %r2,1\n"       /* %r2 is 0 on entry, set 1. */
    "noexec_2:\n"
    "   lgfi %r2,2\n"       /* %r2 is 0/1; set 2. */
    "   br %r14\n"          /* return */
    "noexec_end:");

extern char exrl_1[];
extern char exrl_2[];
extern char exrl_end[];

asm("exrl_1:\n"
    "   exrl %r0, exrl_2\n"
    "   br %r14\n"
    "exrl_2:\n"
    "   lgfi %r2,2\n"
    "exrl_end:");

int main(void)
{
    struct noexec_test noexec_tests[] = {
        {
            .name = "fallthrough",
            .test_code = noexec_1,
            .test_len = noexec_end - noexec_1,
            .page_ofs = noexec_1 - noexec_2,
            .entry_ofs = noexec_1 - noexec_2,
            .expected_si_ofs = 0,
            .expected_pc_ofs = 0,
            .expected_arg = 1,
        },
        {
            .name = "jump",
            .test_code = noexec_1,
            .test_len = noexec_end - noexec_1,
            .page_ofs = noexec_1 - noexec_2,
            .entry_ofs = 0,
            .expected_si_ofs = 0,
            .expected_pc_ofs = 0,
            .expected_arg = 0,
        },
        {
            .name = "exrl",
            .test_code = exrl_1,
            .test_len = exrl_end - exrl_1,
            .page_ofs = exrl_1 - exrl_2,
            .entry_ofs = exrl_1 - exrl_2,
            .expected_si_ofs = 0,
            .expected_pc_ofs = exrl_1 - exrl_2,
            .expected_arg = 0,
        },
        {
            .name = "fallthrough [cross]",
            .test_code = noexec_1,
            .test_len = noexec_end - noexec_1,
            .page_ofs = noexec_1 - noexec_2 - 2,
            .entry_ofs = noexec_1 - noexec_2 - 2,
            .expected_si_ofs = 0,
            .expected_pc_ofs = -2,
            .expected_arg = 1,
        },
        {
            .name = "jump [cross]",
            .test_code = noexec_1,
            .test_len = noexec_end - noexec_1,
            .page_ofs = noexec_1 - noexec_2 - 2,
            .entry_ofs = -2,
            .expected_si_ofs = 0,
            .expected_pc_ofs = -2,
            .expected_arg = 0,
        },
        {
            .name = "exrl [cross]",
            .test_code = exrl_1,
            .test_len = exrl_end - exrl_1,
            .page_ofs = exrl_1 - exrl_2 - 2,
            .entry_ofs = exrl_1 - exrl_2 - 2,
            .expected_si_ofs = 0,
            .expected_pc_ofs = exrl_1 - exrl_2 - 2,
            .expected_arg = 0,
        },
    };

    return test_noexec(noexec_tests,
                       sizeof(noexec_tests) / sizeof(noexec_tests[0]));
}
