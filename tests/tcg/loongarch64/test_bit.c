#include <assert.h>
#include <inttypes.h>

#define ARRAY_SIZE(X) (sizeof(X) / sizeof(*(X)))

#define TEST(C, I)                                       \
static uint64_t test_##C(uint64_t rj)                    \
{                                                        \
    uint64_t rd;                                         \
    asm volatile(I " %0, %1\n\t" : "=r"(rd) : "r"(rj));  \
    return rd;                                           \
}

TEST(clo_w, "clo.w")
TEST(clo_d, "clo.d")
TEST(clz_w, "clz.w")
TEST(clz_d, "clz.d")
TEST(cto_w, "cto.w")
TEST(cto_d, "cto.d")
TEST(ctz_w, "ctz.w")
TEST(ctz_d, "ctz.d")

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
