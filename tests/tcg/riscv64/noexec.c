#include "../multiarch/noexec.c.inc"

static void *arch_mcontext_pc(const mcontext_t *ctx)
{
    return (void *)ctx->__gregs[REG_PC];
}

static int arch_mcontext_arg(const mcontext_t *ctx)
{
    return ctx->__gregs[REG_A0];
}

static void arch_flush(void *p, int len)
{
    __builtin___clear_cache(p, p + len);
}

extern char noexec_1[];
extern char noexec_2[];
extern char noexec_end[];

asm(".option push\n"
    ".option norvc\n"
    "noexec_1:\n"
    "   li a0,1\n"       /* a0 is 0 on entry, set 1. */
    "noexec_2:\n"
    "   li a0,2\n"      /* a0 is 0/1; set 2. */
    "   ret\n"
    "noexec_end:\n"
    ".option pop");

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
    };

    return test_noexec(noexec_tests,
                       sizeof(noexec_tests) / sizeof(noexec_tests[0]));
}
