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
TEST(bitrev_4b, "bitrev.4b")
TEST(bitrev_8b, "bitrev.8b")
TEST(bitrev_w, "bitrev.w")
TEST(bitrev_d, "bitrev.d")

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
    {test_bitrev_4b, 0xdeadbeef11223344, 0xffffffff8844cc22},
    {test_bitrev_8b, 0x1122334455667788, 0x8844cc22aa66ee11},
    {test_bitrev_w,  0xdeadbeef89abcdef, 0xfffffffff7b3d591},
    {test_bitrev_d,  0x0123456789abcdef, 0xf7b3d591e6a2c480},
};

int main()
{
    int i;

    for (i = 0; i < ARRAY_SIZE(vectors); i++) {
        assert((*vectors[i].func)(vectors[i].u) == vectors[i].r);
    }

    return 0;
}
