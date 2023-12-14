#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef void (*testfn)(void);

typedef struct {
    uint64_t q0, q1, q2, q3;
} __attribute__((aligned(32))) v4di;

typedef struct {
    uint64_t mm[8];
    v4di ymm[16];
    uint64_t r[16];
    uint64_t flags;
    uint32_t ff;
    uint64_t pad;
    v4di mem[4];
    v4di mem0[4];
} reg_state;

typedef struct {
    int n;
    testfn fn;
    const char *s;
    reg_state *init;
} TestDef;

reg_state initI;
reg_state initF16;
reg_state initF32;
reg_state initF64;

static void dump_ymm(const char *name, int n, const v4di *r, int ff)
{
    printf("%s%d = %016lx %016lx %016lx %016lx\n",
           name, n, r->q3, r->q2, r->q1, r->q0);
    if (ff == 64) {
        double v[4];
        memcpy(v, r, sizeof(v));
        printf("        %16g %16g %16g %16g\n",
                v[3], v[2], v[1], v[0]);
    } else if (ff == 32) {
        float v[8];
        memcpy(v, r, sizeof(v));
        printf(" %8g %8g %8g %8g %8g %8g %8g %8g\n",
                v[7], v[6], v[5], v[4], v[3], v[2], v[1], v[0]);
    }
}

static void dump_regs(reg_state *s)
{
    int i;

    for (i = 0; i < 16; i++) {
        dump_ymm("ymm", i, &s->ymm[i], 0);
    }
    for (i = 0; i < 4; i++) {
        dump_ymm("mem", i, &s->mem0[i], 0);
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
    for (i = 0; i < 16; i++) {
        if (memcmp(&a->ymm[i], &b->ymm[i], 32)) {
            dump_ymm("ymm", i, &b->ymm[i], a->ff);
        }
    }
    for (i = 0; i < 4; i++) {
        if (memcmp(&a->mem0[i], &a->mem[i], 32)) {
            dump_ymm("mem", i, &a->mem[i], a->ff);
        }
    }
    if (a->flags != b->flags) {
        printf("FLAGS = %016lx\n", b->flags);
    }
}

#define LOADMM(r, o) "movq " #r ", " #o "[%0]\n\t"
#define LOADYMM(r, o) "vmovdqa " #r ", " #o "[%0]\n\t"
#define STOREMM(r, o) "movq " #o "[%1], " #r "\n\t"
#define STOREYMM(r, o) "vmovdqa " #o "[%1], " #r "\n\t"
#define MMREG(F) \
    F(mm0, 0x00) \
    F(mm1, 0x08) \
    F(mm2, 0x10) \
    F(mm3, 0x18) \
    F(mm4, 0x20) \
    F(mm5, 0x28) \
    F(mm6, 0x30) \
    F(mm7, 0x38)
#define YMMREG(F) \
    F(ymm0, 0x040) \
    F(ymm1, 0x060) \
    F(ymm2, 0x080) \
    F(ymm3, 0x0a0) \
    F(ymm4, 0x0c0) \
    F(ymm5, 0x0e0) \
    F(ymm6, 0x100) \
    F(ymm7, 0x120) \
    F(ymm8, 0x140) \
    F(ymm9, 0x160) \
    F(ymm10, 0x180) \
    F(ymm11, 0x1a0) \
    F(ymm12, 0x1c0) \
    F(ymm13, 0x1e0) \
    F(ymm14, 0x200) \
    F(ymm15, 0x220)
#define LOADREG(r, o) "mov " #r ", " #o "[rax]\n\t"
#define STOREREG(r, o) "mov " #o "[rax], " #r "\n\t"
#define REG(F) \
    F(rbx, 0x248) \
    F(rcx, 0x250) \
    F(rdx, 0x258) \
    F(rsi, 0x260) \
    F(rdi, 0x268) \
    F(r8, 0x280) \
    F(r9, 0x288) \
    F(r10, 0x290) \
    F(r11, 0x298) \
    F(r12, 0x2a0) \
    F(r13, 0x2a8) \
    F(r14, 0x2b0) \
    F(r15, 0x2b8) \

static void run_test(const TestDef *t)
{
    reg_state result;
    reg_state *init = t->init;
    memcpy(init->mem, init->mem0, sizeof(init->mem));
    printf("%5d %s\n", t->n, t->s);
    asm volatile(
            MMREG(LOADMM)
            YMMREG(LOADYMM)
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
            "mov rcx, 0x2c0[rax]\n\t"
            "and rcx, 0xff\n\t"
            "or rbx, rcx\n\t"
            "push rbx\n\t"
            "popf\n\t"
            REG(LOADREG)
            "mov rax, 0x240[rax]\n\t"
            "call [rsp]\n\t"
            "mov [rsp], rax\n\t"
            "mov rax, 8[rsp]\n\t"
            REG(STOREREG)
            "mov rbx, [rsp]\n\t"
            "mov 0x240[rax], rbx\n\t"
            "mov rbx, 0\n\t"
            "mov 0x270[rax], rbx\n\t"
            "mov 0x278[rax], rbx\n\t"
            "pushf\n\t"
            "pop rbx\n\t"
            "and rbx, 0xff\n\t"
            "mov 0x2c0[rax], rbx\n\t"
            "add rsp, 16\n\t"
            "pop rdx\n\t"
            "pop rcx\n\t"
            "pop rbx\n\t"
            "pop rax\n\t"
            "add rsp, 128\n\t"
            MMREG(STOREMM)
            YMMREG(STOREYMM)
            : : "r"(init), "r"(&result), "r"(t->fn)
            : "memory", "cc",
            "rsi", "rdi",
            "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
            "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
            "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5",
            "ymm6", "ymm7", "ymm8", "ymm9", "ymm10", "ymm11",
            "ymm12", "ymm13", "ymm14", "ymm15"
            );
    compare_state(init, &result);
}

#define TEST(n, cmd, type) \
static void __attribute__((naked)) test_##n(void) \
{ \
    asm volatile(cmd); \
    asm volatile("ret"); \
}
#include "test-avx.h"


static const TestDef test_table[] = {
#define TEST(n, cmd, type) {n, test_##n, cmd, &init##type},
#include "test-avx.h"
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

uint16_t val_f16[] = { 0x4000, 0xbc00, 0x44cd, 0x3a66, 0x4200, 0x7a1a, 0x4780, 0x4826 };
float val_f32[] = {2.0, -1.0, 4.8, 0.8, 3, -42.0, 5e6, 7.5, 8.3};
double val_f64[] = {2.0, -1.0, 4.8, 0.8, 3, -42.0, 5e6, 7.5};
v4di val_i64[] = {
    {0x3d6b3b6a9e4118f2lu, 0x355ae76d2774d78clu,
     0xac3ff76c4daa4b28lu, 0xe7fabd204cb54083lu},
    {0xd851c54a56bf1f29lu, 0x4a84d1d50bf4c4fflu,
     0x56621e553d52b56clu, 0xd0069553da8f584alu},
    {0x5826475e2c5fd799lu, 0xfd32edc01243f5e9lu,
     0x738ba2c66d3fe126lu, 0x5707219c6e6c26b4lu},
};

v4di deadbeef = {0xa5a5a5a5deadbeefull, 0xa5a5a5a5deadbeefull,
                 0xa5a5a5a5deadbeefull, 0xa5a5a5a5deadbeefull};
/* &gather_mem[0x10] is 512 bytes from the base; indices must be >=-64, <64
 * to account for scaling by 8 */
v4di indexq = {0x000000000000001full, 0x000000000000003dull,
               0xffffffffffffffffull, 0xffffffffffffffdfull};
v4di indexd = {0x00000002ffffffcdull, 0xfffffff500000010ull,
               0x0000003afffffff0ull, 0x000000000000000eull};

v4di gather_mem[0x20];
_Static_assert(sizeof(gather_mem) == 1024);

void init_f16reg(v4di *r)
{
    memset(r, 0, sizeof(*r));
    memcpy(r, val_f16, sizeof(val_f16));
}

void init_f32reg(v4di *r)
{
    static int n;
    float v[8];
    int i;
    for (i = 0; i < 8; i++) {
        v[i] = val_f32[n++];
        if (n == ARRAY_LEN(val_f32)) {
            n = 0;
        }
    }
    memcpy(r, v, sizeof(*r));
}

void init_f64reg(v4di *r)
{
    static int n;
    double v[4];
    int i;
    for (i = 0; i < 4; i++) {
        v[i] = val_f64[n++];
        if (n == ARRAY_LEN(val_f64)) {
            n = 0;
        }
    }
    memcpy(r, v, sizeof(*r));
}

void init_intreg(v4di *r)
{
    static uint64_t mask;
    static int n;

    r->q0 = val_i64[n].q0 ^ mask;
    r->q1 = val_i64[n].q1 ^ mask;
    r->q2 = val_i64[n].q2 ^ mask;
    r->q3 = val_i64[n].q3 ^ mask;
    n++;
    if (n == ARRAY_LEN(val_i64)) {
        n = 0;
        mask *= 0x104C11DB7;
    }
}

static void init_all(reg_state *s)
{
    int i;

    s->r[3] = (uint64_t)&s->mem[0]; /* rdx */
    s->r[4] = (uint64_t)&gather_mem[ARRAY_LEN(gather_mem) / 2]; /* rsi */
    s->r[5] = (uint64_t)&s->mem[2]; /* rdi */
    s->flags = 2;
    for (i = 0; i < 16; i++) {
        s->ymm[i] = deadbeef;
    }
    s->ymm[13] = indexd;
    s->ymm[14] = indexq;
    for (i = 0; i < 4; i++) {
        s->mem0[i] = deadbeef;
    }
}

int main(int argc, char *argv[])
{
    int i;

    init_all(&initI);
    init_intreg(&initI.ymm[0]);
    init_intreg(&initI.ymm[9]);
    init_intreg(&initI.ymm[10]);
    init_intreg(&initI.ymm[11]);
    init_intreg(&initI.ymm[12]);
    init_intreg(&initI.mem0[1]);
    printf("Int:\n");
    dump_regs(&initI);

    init_all(&initF16);
    init_f16reg(&initF16.ymm[0]);
    init_f16reg(&initF16.ymm[9]);
    init_f16reg(&initF16.ymm[10]);
    init_f16reg(&initF16.ymm[11]);
    init_f16reg(&initF16.ymm[12]);
    init_f16reg(&initF16.mem0[1]);
    initF16.ff = 16;
    printf("F16:\n");
    dump_regs(&initF16);

    init_all(&initF32);
    init_f32reg(&initF32.ymm[0]);
    init_f32reg(&initF32.ymm[9]);
    init_f32reg(&initF32.ymm[10]);
    init_f32reg(&initF32.ymm[11]);
    init_f32reg(&initF32.ymm[12]);
    init_f32reg(&initF32.mem0[1]);
    initF32.ff = 32;
    printf("F32:\n");
    dump_regs(&initF32);

    init_all(&initF64);
    init_f64reg(&initF64.ymm[0]);
    init_f64reg(&initF64.ymm[9]);
    init_f64reg(&initF64.ymm[10]);
    init_f64reg(&initF64.ymm[11]);
    init_f64reg(&initF64.ymm[12]);
    init_f64reg(&initF64.mem0[1]);
    initF64.ff = 64;
    printf("F64:\n");
    dump_regs(&initF64);

    for (i = 0; i < ARRAY_LEN(gather_mem); i++) {
        init_intreg(&gather_mem[i]);
    }

    if (argc > 1) {
        int n = atoi(argv[1]);
        run_test(&test_table[n]);
    } else {
        run_all();
    }
    return 0;
}
