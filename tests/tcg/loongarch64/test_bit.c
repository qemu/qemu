#include <assert.h>
#include <inttypes.h>

#define ARRAY_SIZE(X) (sizeof(X) / sizeof(*(X)))
#define TEST_CLO(N)                                     \
static uint64_t test_clo_##N(uint64_t rj)               \
{                                                       \
    uint64_t rd = 0;                                    \
                                                        \
    asm volatile("clo."#N" %0, %1\n\t"                  \
                 : "=r"(rd)                             \
                 : "r"(rj)                              \
                 : );                                   \
    return rd;                                          \
}

#define TEST_CLZ(N)                                     \
static uint64_t test_clz_##N(uint64_t rj)               \
{                                                       \
    uint64_t rd = 0;                                    \
                                                        \
    asm volatile("clz."#N" %0, %1\n\t"                  \
                 : "=r"(rd)                             \
                 : "r"(rj)                              \
                 : );                                   \
    return rd;                                          \
}

#define TEST_CTO(N)                                     \
static uint64_t test_cto_##N(uint64_t rj)               \
{                                                       \
    uint64_t rd = 0;                                    \
                                                        \
    asm volatile("cto."#N" %0, %1\n\t"                  \
                 : "=r"(rd)                             \
                 : "r"(rj)                              \
                 : );                                   \
    return rd;                                          \
}

#define TEST_CTZ(N)                                     \
static uint64_t test_ctz_##N(uint64_t rj)               \
{                                                       \
    uint64_t rd = 0;                                    \
                                                        \
    asm volatile("ctz."#N" %0, %1\n\t"                  \
                 : "=r"(rd)                             \
                 : "r"(rj)                              \
                 : );                                   \
    return rd;                                          \
}

TEST_CLO(w)
TEST_CLO(d)
TEST_CLZ(w)
TEST_CLZ(d)
TEST_CTO(w)
TEST_CTO(d)
TEST_CTZ(w)
TEST_CTZ(d)

struct vector {
    uint64_t (*func)(uint64_t);
    uint64_t u;
    uint64_t r;
};

static struct vector vectors[] = {
    {test_clo_w, 0xfff11fff392476ab, 0},
    {test_clo_d, 0xabd28a64000000, 0},
    {test_clz_w, 0xfaffff42392476ab, 2},
    {test_clz_d, 0xabd28a64000000, 8},
    {test_cto_w, 0xfff11fff392476ab, 2},
    {test_cto_d, 0xabd28a64000000, 0},
    {test_ctz_w, 0xfaffff42392476ab, 0},
    {test_ctz_d, 0xabd28a64000000, 26},
};

int main()
{
    int i;

    for (i = 0; i < ARRAY_SIZE(vectors); i++) {
        assert((*vectors[i].func)(vectors[i].u) == vectors[i].r);
    }

    return 0;
}
