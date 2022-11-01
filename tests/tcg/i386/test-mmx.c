#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef TEST_FILE
#define TEST_FILE "test-mmx.h"
#endif
#ifndef EMMS
#define EMMS "emms"
#endif

typedef void (*testfn)(void);

typedef struct {
    uint64_t q0, q1;
} __attribute__((aligned(16))) v2di;

typedef struct {
    uint64_t mm[8];
    v2di xmm[8];
    uint64_t r[16];
    uint64_t flags;
    uint32_t ff;
    uint64_t pad;
    v2di mem[4];
    v2di mem0[4];
} reg_state;

typedef struct {
    int n;
    testfn fn;
    const char *s;
    reg_state *init;
} TestDef;

reg_state initI;
reg_state initF32;
reg_state initF64;

static void dump_mmx(int n, const uint64_t *r, int ff)
{
    if (ff == 32) {
        float v[2];
        memcpy(v, r, sizeof(v));
        printf("MM%d = %016lx %8g %8g\n", n, *r, v[1], v[0]);
    } else {
        printf("MM%d = %016lx\n", n, *r);
    }
}

static void dump_xmm(const char *name, int n, const v2di *r, int ff)
{
    printf("%s%d = %016lx %016lx\n",
           name, n, r->q1, r->q0);
    if (ff == 32) {
        float v[4];
        memcpy(v, r, sizeof(v));
        printf(" %8g %8g %8g %8g\n",
                v[3], v[2], v[1], v[0]);
    }
}

static void dump_regs(reg_state *s, int ff)
{
    int i;

    for (i = 0; i < 8; i++) {
        dump_mmx(i, &s->mm[i], ff);
    }
    for (i = 0; i < 4; i++) {
        dump_xmm("mem", i, &s->mem0[i], 0);
    }
}

static void compare_state(const reg_state *a, const reg_state *b)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (a->mm[i] != b->mm[i]) {
            printf("MM%d = %016lx\n", i, b->mm[i]);
        }
    }
    for (i = 0; i < 16; i++) {
        if (a->r[i] != b->r[i]) {
            printf("r%d = %016lx\n", i, b->r[i]);
        }
    }
    for (i = 0; i < 8; i++) {
        if (memcmp(&a->xmm[i], &b->xmm[i], 8)) {
            dump_xmm("xmm", i, &b->xmm[i], a->ff);
        }
    }
    for (i = 0; i < 4; i++) {
        if (memcmp(&a->mem0[i], &a->mem[i], 16)) {
            dump_xmm("mem", i, &a->mem[i], a->ff);
        }
    }
    if (a->flags != b->flags) {
        printf("FLAGS = %016lx\n", b->flags);
    }
}

#define LOADMM(r, o) "movq " #r ", " #o "[%0]\n\t"
#define LOADXMM(r, o) "movdqa " #r ", " #o "[%0]\n\t"
#define STOREMM(r, o) "movq " #o "[%1], " #r "\n\t"
#define STOREXMM(r, o) "movdqa " #o "[%1], " #r "\n\t"
#define MMREG(F) \
    F(mm0, 0x00) \
    F(mm1, 0x08) \
    F(mm2, 0x10) \
    F(mm3, 0x18) \
    F(mm4, 0x20) \
    F(mm5, 0x28) \
    F(mm6, 0x30) \
    F(mm7, 0x38)
#define XMMREG(F) \
    F(xmm0, 0x040) \
    F(xmm1, 0x050) \
    F(xmm2, 0x060) \
    F(xmm3, 0x070) \
    F(xmm4, 0x080) \
    F(xmm5, 0x090) \
    F(xmm6, 0x0a0) \
    F(xmm7, 0x0b0)
#define LOADREG(r, o) "mov " #r ", " #o "[rax]\n\t"
#define STOREREG(r, o) "mov " #o "[rax], " #r "\n\t"
#define REG(F) \
    F(rbx, 0xc8) \
    F(rcx, 0xd0) \
    F(rdx, 0xd8) \
    F(rsi, 0xe0) \
    F(rdi, 0xe8) \
    F(r8, 0x100) \
    F(r9, 0x108) \
    F(r10, 0x110) \
    F(r11, 0x118) \
    F(r12, 0x120) \
    F(r13, 0x128) \
    F(r14, 0x130) \
    F(r15, 0x138) \

static void run_test(const TestDef *t)
{
    reg_state result;
    reg_state *init = t->init;
    memcpy(init->mem, init->mem0, sizeof(init->mem));
    printf("%5d %s\n", t->n, t->s);
    asm volatile(
            MMREG(LOADMM)
            XMMREG(LOADXMM)
            "sub rsp, 128\n\t"
            "push rax\n\t"
            "push rbx\n\t"
            "push rcx\n\t"
            "push rdx\n\t"
            "push %1\n\t"
            "push %2\n\t"
            "mov rax, %0\n\t"
            "pushf\n\t"
            "pop rbx\n\t"
            "shr rbx, 8\n\t"
            "shl rbx, 8\n\t"
            "mov rcx, 0x140[rax]\n\t"
            "and rcx, 0xff\n\t"
            "or rbx, rcx\n\t"
            "push rbx\n\t"
            "popf\n\t"
            REG(LOADREG)
            "mov rax, 0xc0[rax]\n\t"
            "call [rsp]\n\t"
            "mov [rsp], rax\n\t"
            "mov rax, 8[rsp]\n\t"
            REG(STOREREG)
            "mov rbx, [rsp]\n\t"
            "mov 0xc0[rax], rbx\n\t"
            "mov rbx, 0\n\t"
            "mov 0xf0[rax], rbx\n\t"
            "mov 0xf8[rax], rbx\n\t"
            "pushf\n\t"
            "pop rbx\n\t"
            "and rbx, 0xff\n\t"
            "mov 0x140[rax], rbx\n\t"
            "add rsp, 16\n\t"
            "pop rdx\n\t"
            "pop rcx\n\t"
            "pop rbx\n\t"
            "pop rax\n\t"
            "add rsp, 128\n\t"
            MMREG(STOREMM)
	    EMMS "\n\t"
            XMMREG(STOREXMM)
            : : "r"(init), "r"(&result), "r"(t->fn)
            : "memory", "cc",
            "rsi", "rdi",
            "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
            "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
            "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5",
            "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11",
            "xmm12", "xmm13", "xmm14", "xmm15"
            );
    compare_state(init, &result);
}

#define TEST(n, cmd, type) \
static void __attribute__((naked)) test_##n(void) \
{ \
    asm volatile(cmd); \
    asm volatile("ret"); \
}
#include TEST_FILE


static const TestDef test_table[] = {
#define TEST(n, cmd, type) {n, test_##n, cmd, &init##type},
#include TEST_FILE
    {-1, NULL, "", NULL}
};

static void run_all(void)
{
    const TestDef *t;
    for (t = test_table; t->fn; t++) {
        run_test(t);
    }
}

#define ARRAY_LEN(x) (sizeof(x) / sizeof(x[0]))

float val_f32[] = {2.0, -1.0, 4.8, 0.8, 3, -42.0, 5e6, 7.5, 8.3};
uint64_t val_i64[] = {
    0x3d6b3b6a9e4118f2lu, 0x355ae76d2774d78clu,
    0xd851c54a56bf1f29lu, 0x4a84d1d50bf4c4fflu,
    0x5826475e2c5fd799lu, 0xfd32edc01243f5e9lu,
};

v2di deadbeef = {0xa5a5a5a5deadbeefull, 0xa5a5a5a5deadbeefull};

void init_f32reg(uint64_t *r)
{
    static int n;
    float v[2];
    int i;
    for (i = 0; i < 2; i++) {
        v[i] = val_f32[n++];
        if (n == ARRAY_LEN(val_f32)) {
            n = 0;
        }
    }
    memcpy(r, v, sizeof(*r));
}

void init_intreg(uint64_t *r)
{
    static uint64_t mask;
    static int n;

    *r = val_i64[n] ^ mask;
    n++;
    if (n == ARRAY_LEN(val_i64)) {
        n = 0;
        mask *= 0x104C11DB7;
    }
}

static void init_all(reg_state *s)
{
    int i;

    for (i = 0; i < 16; i++) {
        init_intreg(&s->r[i]);
    }
    s->r[3] = (uint64_t)&s->mem[0]; /* rdx */
    s->r[5] = (uint64_t)&s->mem[2]; /* rdi */
    s->r[6] = 0;
    s->r[7] = 0;
    s->flags = 2;
    for (i = 0; i < 8; i++) {
        s->xmm[i] = deadbeef;
	memcpy(&s->mm[i], &s->xmm[i], sizeof(s->mm[i]));
    }
    for (i = 0; i < 2; i++) {
        s->mem0[i] = deadbeef;
    }
}

int main(int argc, char *argv[])
{
    init_all(&initI);
    init_intreg(&initI.mm[5]);
    init_intreg(&initI.mm[6]);
    init_intreg(&initI.mm[7]);
    init_intreg(&initI.mem0[1].q0);
    init_intreg(&initI.mem0[1].q1);
    printf("Int:\n");
    dump_regs(&initI, 0);

    init_all(&initF32);
    init_f32reg(&initF32.mm[5]);
    init_f32reg(&initF32.mm[6]);
    init_f32reg(&initF32.mm[7]);
    init_f32reg(&initF32.mem0[1].q0);
    init_f32reg(&initF32.mem0[1].q1);
    initF32.ff = 32;
    printf("F32:\n");
    dump_regs(&initF32, 32);

    if (argc > 1) {
        int n = atoi(argv[1]);
        run_test(&test_table[n]);
    } else {
        run_all();
    }
    return 0;
}
