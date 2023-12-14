/* Check EXECUTE with relative branch instructions as targets. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct test {
    const char *name;
    void (*func)(long *link, long *magic);
    long exp_link;
};

/* Branch instructions and their expected effects. */
#define LINK_64(test) ((long)test ## _exp_link)
#define LINK_NONE(test) -1L
#define FOR_EACH_INSN(F)                                                       \
    F(bras,  "%[link]",     LINK_64)                                           \
    F(brasl, "%[link]",     LINK_64)                                           \
    F(brc,   "0x8",         LINK_NONE)                                         \
    F(brcl,  "0x8",         LINK_NONE)                                         \
    F(brct,  "%%r0",        LINK_NONE)                                         \
    F(brctg, "%%r0",        LINK_NONE)                                         \
    F(brxh,  "%%r2,%%r0",   LINK_NONE)                                         \
    F(brxhg, "%%r2,%%r0",   LINK_NONE)                                         \
    F(brxle, "%%r0,%%r1",   LINK_NONE)                                         \
    F(brxlg, "%%r0,%%r1",   LINK_NONE)                                         \
    F(crj,   "%%r0,%%r0,8", LINK_NONE)                                         \
    F(cgrj,  "%%r0,%%r0,8", LINK_NONE)                                         \
    F(cij,   "%%r0,0,8",    LINK_NONE)                                         \
    F(cgij,  "%%r0,0,8",    LINK_NONE)                                         \
    F(clrj,  "%%r0,%%r0,8", LINK_NONE)                                         \
    F(clgrj, "%%r0,%%r0,8", LINK_NONE)                                         \
    F(clij,  "%%r0,0,8",    LINK_NONE)                                         \
    F(clgij, "%%r0,0,8",    LINK_NONE)

#define INIT_TEST                                                              \
    "xgr %%r0,%%r0\n"  /* %r0 = 0; %cc = 0 */                                  \
    "lghi %%r1,1\n"    /* %r1 = 1 */                                           \
    "lghi %%r2,2\n"    /* %r2 = 2 */

#define CLOBBERS_TEST "cc", "0", "1", "2"

#define DEFINE_TEST(insn, args, exp_link)                                      \
    extern char insn ## _exp_link[];                                           \
    static void test_ ## insn(long *link, long *magic)                         \
    {                                                                          \
        asm(INIT_TEST                                                          \
            #insn " " args ",0f\n"                                             \
            ".globl " #insn "_exp_link\n"                                      \
            #insn "_exp_link:\n"                                               \
            ".org . + 90\n"                                                    \
            "0: lgfi %[magic],0x12345678\n"                                    \
            : [link] "+r" (*link)                                              \
            , [magic] "+r" (*magic)                                            \
            : : CLOBBERS_TEST);                                                \
    }                                                                          \
    extern char ex_ ## insn ## _exp_link[];                                    \
    static void test_ex_ ## insn(long *link, long *magic)                      \
    {                                                                          \
        unsigned long target;                                                  \
                                                                               \
        asm(INIT_TEST                                                          \
            "larl %[target],0f\n"                                              \
            "ex %%r0,0(%[target])\n"                                           \
            ".globl ex_" #insn "_exp_link\n"                                   \
            "ex_" #insn "_exp_link:\n"                                         \
            ".org . + 60\n"                                                    \
            "0: " #insn " " args ",1f\n"                                       \
            ".org . + 120\n"                                                   \
            "1: lgfi %[magic],0x12345678\n"                                    \
            : [target] "=r" (target)                                           \
            , [link] "+r" (*link)                                              \
            , [magic] "+r" (*magic)                                            \
            : : CLOBBERS_TEST);                                                \
    }                                                                          \
    extern char exrl_ ## insn ## _exp_link[];                                  \
    static void test_exrl_ ## insn(long *link, long *magic)                    \
    {                                                                          \
        asm(INIT_TEST                                                          \
            "exrl %%r0,0f\n"                                                   \
            ".globl exrl_" #insn "_exp_link\n"                                 \
            "exrl_" #insn "_exp_link:\n"                                       \
            ".org . + 60\n"                                                    \
            "0: " #insn " " args ",1f\n"                                       \
            ".org . + 120\n"                                                   \
            "1: lgfi %[magic],0x12345678\n"                                    \
            : [link] "+r" (*link)                                              \
            , [magic] "+r" (*magic)                                            \
            : : CLOBBERS_TEST);                                                \
    }

/* Test functions. */
FOR_EACH_INSN(DEFINE_TEST)

/* Test definitions. */
#define REGISTER_TEST(insn, args, _exp_link)                                   \
    {                                                                          \
        .name = #insn,                                                         \
        .func = test_ ## insn,                                                 \
        .exp_link = (_exp_link(insn)),                                         \
    },                                                                         \
    {                                                                          \
        .name = "ex " #insn,                                                   \
        .func = test_ex_ ## insn,                                              \
        .exp_link = (_exp_link(ex_ ## insn)),                                  \
    },                                                                         \
    {                                                                          \
        .name = "exrl " #insn,                                                 \
        .func = test_exrl_ ## insn,                                            \
        .exp_link = (_exp_link(exrl_ ## insn)),                                \
    },

static const struct test tests[] = {
    FOR_EACH_INSN(REGISTER_TEST)
};

int main(int argc, char **argv)
{
    const struct test *test;
    int ret = EXIT_SUCCESS;
    bool verbose = false;
    long link, magic;
    size_t i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        }
    }

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        test = &tests[i];
        if (verbose) {
            fprintf(stderr, "[ RUN      ] %s\n", test->name);
        }
        link = -1;
        magic = -1;
        test->func(&link, &magic);
#define ASSERT_EQ(expected, actual) do {                                       \
    if (expected != actual) {                                                  \
        fprintf(stderr, "%s: " #expected " (0x%lx) != " #actual " (0x%lx)\n",  \
                test->name, expected, actual);                                 \
        ret = EXIT_FAILURE;                                                    \
    }                                                                          \
} while (0)
        ASSERT_EQ(test->exp_link, link);
        ASSERT_EQ(0x12345678L, magic);
#undef ASSERT_EQ
    }

    if (verbose) {
        fprintf(stderr, ret == EXIT_SUCCESS ? "[  PASSED  ]\n" :
                                              "[  FAILED  ]\n");
    }

    return ret;
}
