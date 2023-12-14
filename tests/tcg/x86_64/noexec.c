#include "../multiarch/noexec.c.inc"

static void *arch_mcontext_pc(const mcontext_t *ctx)
{
    return (void *)ctx->gregs[REG_RIP];
}

int arch_mcontext_arg(const mcontext_t *ctx)
{
    return ctx->gregs[REG_RDI];
}

static void arch_flush(void *p, int len)
{
}

extern char noexec_1[];
extern char noexec_2[];
extern char noexec_end[];

asm("noexec_1:\n"
    "    movq $1,%rdi\n"    /* %rdi is 0 on entry, set 1. */
    "noexec_2:\n"
    "    movq $2,%rdi\n"    /* %rdi is 0/1; set 2. */
    "    ret\n"
    "noexec_end:");

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
