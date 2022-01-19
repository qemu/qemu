#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define DEFINE_SHIFT_SINGLE_COMMON(_name, _insn_str) \
    static uint64_t _name(uint64_t op1, uint64_t op2, uint64_t *cc) \
    { \
        asm("    sll %[cc],28\n" \
            "    spm %[cc]\n" \
            "    " _insn_str "\n" \
            "    ipm %[cc]\n" \
            "    srl %[cc],28" \
            : [op1] "+&r" (op1), \
              [cc] "+&r" (*cc) \
            : [op2] "r" (op2) \
            : "cc"); \
        return op1; \
    }
#define DEFINE_SHIFT_SINGLE_2(_insn, _offset) \
    DEFINE_SHIFT_SINGLE_COMMON(_insn ## _ ## _offset, \
                               #_insn " %[op1]," #_offset "(%[op2])")
#define DEFINE_SHIFT_SINGLE_3(_insn, _offset) \
    DEFINE_SHIFT_SINGLE_COMMON(_insn ## _ ## _offset, \
                               #_insn " %[op1],%[op1]," #_offset "(%[op2])")
#define DEFINE_SHIFT_DOUBLE(_insn, _offset) \
    static uint64_t _insn ## _ ## _offset(uint64_t op1, uint64_t op2, \
                                          uint64_t *cc) \
    { \
        uint32_t op1h = op1 >> 32; \
        uint32_t op1l = op1 & 0xffffffff; \
        register uint32_t r2 asm("2") = op1h; \
        register uint32_t r3 asm("3") = op1l; \
        \
        asm("    sll %[cc],28\n" \
            "    spm %[cc]\n" \
            "    " #_insn " %[r2]," #_offset "(%[op2])\n" \
            "    ipm %[cc]\n" \
            "    srl %[cc],28" \
            : [r2] "+&r" (r2), \
              [r3] "+&r" (r3), \
              [cc] "+&r" (*cc) \
            : [op2] "r" (op2) \
            : "cc"); \
        op1h = r2; \
        op1l = r3; \
        return (((uint64_t)op1h) << 32) | op1l; \
    }

DEFINE_SHIFT_SINGLE_3(rll, 0x4cf3b);
DEFINE_SHIFT_SINGLE_3(rllg, 0x697c9);
DEFINE_SHIFT_SINGLE_2(sla, 0x4b0);
DEFINE_SHIFT_SINGLE_2(sla, 0xd54);
DEFINE_SHIFT_SINGLE_3(slak, 0x2832c);
DEFINE_SHIFT_SINGLE_3(slag, 0x66cc4);
DEFINE_SHIFT_SINGLE_3(slag, 0xd54);
DEFINE_SHIFT_SINGLE_2(sll, 0xd04);
DEFINE_SHIFT_SINGLE_3(sllk, 0x2699f);
DEFINE_SHIFT_SINGLE_3(sllg, 0x59df9);
DEFINE_SHIFT_SINGLE_2(sra, 0x67e);
DEFINE_SHIFT_SINGLE_3(srak, 0x60943);
DEFINE_SHIFT_SINGLE_3(srag, 0x6b048);
DEFINE_SHIFT_SINGLE_2(srl, 0x035);
DEFINE_SHIFT_SINGLE_3(srlk, 0x43dfc);
DEFINE_SHIFT_SINGLE_3(srlg, 0x27227);
DEFINE_SHIFT_DOUBLE(slda, 0x38b);
DEFINE_SHIFT_DOUBLE(sldl, 0x031);
DEFINE_SHIFT_DOUBLE(srda, 0x36f);
DEFINE_SHIFT_DOUBLE(srdl, 0x99a);

struct shift_test {
    const char *name;
    uint64_t (*insn)(uint64_t, uint64_t, uint64_t *);
    uint64_t op1;
    uint64_t op2;
    uint64_t exp_result;
    uint64_t exp_cc;
};

static const struct shift_test tests[] = {
    {
        .name = "rll",
        .insn = rll_0x4cf3b,
        .op1 = 0xecbd589a45c248f5ull,
        .op2 = 0x62e5508ccb4c99fdull,
        .exp_result = 0xecbd589af545c248ull,
        .exp_cc = 0,
    },
    {
        .name = "rllg",
        .insn = rllg_0x697c9,
        .op1 = 0xaa2d54c1b729f7f4ull,
        .op2 = 0x5ffcf7465f5cd71full,
        .exp_result = 0x29f7f4aa2d54c1b7ull,
        .exp_cc = 0,
    },
    {
        .name = "sla-1",
        .insn = sla_0x4b0,
        .op1 = 0x8bf21fb67cca0e96ull,
        .op2 = 0x3ddf2f53347d3030ull,
        .exp_result = 0x8bf21fb600000000ull,
        .exp_cc = 3,
    },
    {
        .name = "sla-2",
        .insn = sla_0xd54,
        .op1 = 0xe4faaed5def0e926ull,
        .op2 = 0x18d586fab239cbeeull,
        .exp_result = 0xe4faaed5fbc3a498ull,
        .exp_cc = 3,
    },
    {
        .name = "slak",
        .insn = slak_0x2832c,
        .op1 = 0x7300bf78707f09f9ull,
        .op2 = 0x4d193b85bb5cb39bull,
        .exp_result = 0x7300bf783f84fc80ull,
        .exp_cc = 3,
    },
    {
        .name = "slag-1",
        .insn = slag_0x66cc4,
        .op1 = 0xe805966de1a77762ull,
        .op2 = 0x0e92953f6aa91c6bull,
        .exp_result = 0xbbb1000000000000ull,
        .exp_cc = 3,
    },
    {
        .name = "slag-2",
        .insn = slag_0xd54,
        .op1 = 0xdef0e92600000000ull,
        .op2 = 0x18d586fab239cbeeull,
        .exp_result = 0xfbc3a49800000000ull,
        .exp_cc = 3,
    },
    {
        .name = "sll",
        .insn = sll_0xd04,
        .op1 = 0xb90281a3105939dfull,
        .op2 = 0xb5e4df7e082e4c5eull,
        .exp_result = 0xb90281a300000000ull,
        .exp_cc = 0,
    },
    {
        .name = "sllk",
        .insn = sllk_0x2699f,
        .op1 = 0x777c6cf116f99557ull,
        .op2 = 0xe0556cf112e5a458ull,
        .exp_result = 0x777c6cf100000000ull,
        .exp_cc = 0,
    },
    {
        .name = "sllg",
        .insn = sllg_0x59df9,
        .op1 = 0xcdf86cbfbc0f3557ull,
        .op2 = 0x325a45acf99c6d3dull,
        .exp_result = 0x55c0000000000000ull,
        .exp_cc = 0,
    },
    {
        .name = "sra",
        .insn = sra_0x67e,
        .op1 = 0xb878f048d5354183ull,
        .op2 = 0x9e27d13195931f79ull,
        .exp_result = 0xb878f048ffffffffull,
        .exp_cc = 1,
    },
    {
        .name = "srak",
        .insn = srak_0x60943,
        .op1 = 0xb6ceb5a429cedb35ull,
        .op2 = 0x352354900ae34d7aull,
        .exp_result = 0xb6ceb5a400000000ull,
        .exp_cc = 0,
    },
    {
        .name = "srag",
        .insn = srag_0x6b048,
        .op1 = 0xd54dd4468676c63bull,
        .op2 = 0x84d026db7b4dca28ull,
        .exp_result = 0xffffffffffffd54dull,
        .exp_cc = 1,
    },
    {
        .name = "srl",
        .insn = srl_0x035,
        .op1 = 0x09be503ef826815full,
        .op2 = 0xbba8d1a0e542d5c1ull,
        .exp_result = 0x9be503e00000000ull,
        .exp_cc = 0,
    },
    {
        .name = "srlk",
        .insn = srlk_0x43dfc,
        .op1 = 0x540d6c8de71aee2aull,
        .op2 = 0x0000000000000000ull,
        .exp_result = 0x540d6c8d00000000ull,
        .exp_cc = 0,
    },
    {
        .name = "srlg",
        .insn = srlg_0x27227,
        .op1 = 0x26f7123c1c447a34ull,
        .op2 = 0x0000000000000000ull,
        .exp_result = 0x00000000004dee24ull,
        .exp_cc = 0,
    },
    {
        .name = "slda",
        .insn = slda_0x38b,
        .op1 = 0x7988f722dd5bbe7cull,
        .op2 = 0x9aed3f95b4d78cc2ull,
        .exp_result = 0x1ee45bab77cf8000ull,
        .exp_cc = 3,
    },
    {
        .name = "sldl",
        .insn = sldl_0x031,
        .op1 = 0xaae2918dce2b049aull,
        .op2 = 0x0000000000000000ull,
        .exp_result = 0x0934000000000000ull,
        .exp_cc = 0,
    },
    {
        .name = "srda",
        .insn = srda_0x36f,
        .op1 = 0x0cd4ed9228a50978ull,
        .op2 = 0x72b046f0848b8cc9ull,
        .exp_result = 0x000000000000000cull,
        .exp_cc = 2,
    },
    {
        .name = "srdl",
        .insn = srdl_0x99a,
        .op1 = 0x1018611c41689a1dull,
        .op2 = 0x2907e150c50ba319ull,
        .exp_result = 0x0000000000000203ull,
        .exp_cc = 0,
    },
};

int main(void)
{
    int ret = 0;
    size_t i;

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        uint64_t result;
        uint64_t cc = 0;

        result = tests[i].insn(tests[i].op1, tests[i].op2, &cc);
        if (result != tests[i].exp_result) {
            fprintf(stderr,
                    "bad %s result:\n"
                    "actual   = 0x%" PRIx64 "\n"
                    "expected = 0x%" PRIx64 "\n",
                    tests[i].name, result, tests[i].exp_result);
            ret = 1;
        }
        if (cc != tests[i].exp_cc) {
            fprintf(stderr,
                    "bad %s cc:\n"
                    "actual   = %" PRIu64 "\n"
                    "expected = %" PRIu64 "\n",
                    tests[i].name, cc, tests[i].exp_cc);
            ret = 1;
        }
    }
    return ret;
}
