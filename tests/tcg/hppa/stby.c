/* Test STBY */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


struct S {
    unsigned a;
    unsigned b;
    unsigned c;
};

static void check(const struct S *s, unsigned e,
                  const char *which, const char *insn, int ofs)
{
    int err = 0;

    if (s->a != 0) {
        fprintf(stderr, "%s %s %d: garbage before word 0x%08x\n",
                which, insn, ofs, s->a);
        err = 1;
    }
    if (s->c != 0) {
        fprintf(stderr, "%s %s %d: garbage after word 0x%08x\n",
                which, insn, ofs, s->c);
        err = 1;
    }
    if (s->b != e) {
        fprintf(stderr, "%s %s %d: 0x%08x != 0x%08x\n",
                which, insn, ofs, s->b, e);
        err = 1;
    }

    if (err) {
        exit(1);
    }
}

#define TEST(INSN, OFS, E)                                         \
    do {                                                           \
        s.b = 0;                                                   \
        asm volatile(INSN " %1, " #OFS "(%0)"                      \
                     : : "r"(&s.b), "r" (0x11223344) : "memory");  \
        check(&s, E, which, INSN, OFS);                            \
    } while (0)

static void test(const char *which)
{
    struct S s = { };

    TEST("stby,b", 0, 0x11223344);
    TEST("stby,b", 1, 0x00223344);
    TEST("stby,b", 2, 0x00003344);
    TEST("stby,b", 3, 0x00000044);

    TEST("stby,e", 0, 0x00000000);
    TEST("stby,e", 1, 0x11000000);
    TEST("stby,e", 2, 0x11220000);
    TEST("stby,e", 3, 0x11223300);
}

static void *child(void *x)
{
    return NULL;
}

int main()
{
    int err;
    pthread_t thr;

    /* Run test in serial mode */
    test("serial");

    /* Create a dummy thread to start parallel mode. */
    err = pthread_create(&thr, NULL, child, NULL);
    if (err != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(err));
        return 2;
    }

    /* Run test in parallel mode */
    test("parallel");
    return 0;
}
