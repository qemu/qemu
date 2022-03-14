#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define DEFINE_ASM(_name, _code) \
    extern const char _name[]; \
    extern const char _name ## _end[]; \
    asm("    .globl " #_name "\n" \
        #_name ":\n" \
        "    " _code "\n" \
        "    .globl " #_name "_end\n" \
        #_name "_end:\n");

DEFINE_ASM(br_r14, "br %r14");
DEFINE_ASM(brasl_r0, "brasl %r0,.-0x100000000");
DEFINE_ASM(brcl_0xf, "brcl 0xf,.-0x100000000");

struct test {
    const char *code;
    const char *code_end;
};

static const struct test tests[] = {
    {
        .code = brasl_r0,
        .code_end = brasl_r0_end,
    },
    {
        .code = brcl_0xf,
        .code_end = brcl_0xf_end,
    },
};

int main(void)
{
    unsigned char *buf;
    size_t length = 0;
    size_t i;

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        size_t test_length = 0x100000000 + (tests[i].code_end - tests[i].code);

        if (test_length > length) {
            length = test_length;
        }
    }

    buf = mmap(NULL, length, PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (buf == MAP_FAILED) {
        perror("SKIP: mmap() failed");
        return 0;
    }

    memcpy(buf, br_r14, br_r14_end - br_r14);
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        void (*code)(void) = (void *)(buf + 0x100000000);

        memcpy(code, tests[i].code, tests[i].code_end - tests[i].code);
        code();
        memset(code, 0, tests[i].code_end - tests[i].code);
    }

    munmap(buf, length);

    return 0;
}
