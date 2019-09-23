#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>

jmp_buf jmp_env;

static void handle_sigsegv(int sig)
{
    siglongjmp(jmp_env, 1);
}

#define ALLOC_SIZE (2 * 4096)

static inline void mvc_256(const char *dst, const char *src)
{
    asm volatile (
        "    mvc 0(256,%[dst]),0(%[src])\n"
        :
        : [dst] "d" (dst),
          [src] "d" (src)
        : "memory");
}

int main(void)
{
    char *src, *dst;
    int i;

    /* register the SIGSEGV handler */
    if (signal(SIGSEGV, handle_sigsegv) == SIG_ERR) {
        fprintf(stderr, "SIGSEGV not registered\n");
        return 1;
    }

    /* prepare the buffers - two consecutive pages */
    src = valloc(ALLOC_SIZE);
    dst = valloc(ALLOC_SIZE);
    memset(src, 0xff, ALLOC_SIZE);
    memset(dst, 0x0, ALLOC_SIZE);

    /* protect the second pages */
    if (mprotect(src + 4096, 4096, PROT_NONE) ||
        mprotect(dst + 4096, 4096, PROT_NONE)) {
        fprintf(stderr, "mprotect failed\n");
        return 1;
    }

    /* fault on second destination page */
    if (sigsetjmp(jmp_env, 1) == 0) {
        mvc_256(dst + 4096 - 128, src);
        fprintf(stderr, "fault not triggered\n");
        return 1;
    }

    /* fault on second source page */
    if (sigsetjmp(jmp_env, 1) == 0) {
        mvc_256(dst, src + 4096 - 128);
        fprintf(stderr, "fault not triggered\n");
        return 1;
    }

    /* fault on second source and second destination page */
    if (sigsetjmp(jmp_env, 1) == 0) {
        mvc_256(dst + 4096 - 128, src + 4096 - 128);
        fprintf(stderr, "fault not triggered\n");
        return 1;
    }

    /* restore permissions */
    if (mprotect(src + 4096, 4096, PROT_READ | PROT_WRITE) ||
        mprotect(dst + 4096, 4096, PROT_READ | PROT_WRITE)) {
        fprintf(stderr, "mprotect failed\n");
        return 1;
    }

    /* no data must be touched during the faults */
    for (i = 0; i < ALLOC_SIZE; i++) {
        if (src[i] != 0xff || dst[i]) {
            fprintf(stderr, "data modified during a fault\n");
            return 1;
        }
    }

    /* test if MVC works now correctly accross page boundaries */
    mvc_256(dst + 4096 - 128, src + 4096 - 128);
    for (i = 0; i < ALLOC_SIZE; i++) {
        if (src[i] != 0xff) {
            fprintf(stderr, "src modified\n");
            return 1;
        }
        if (i < 4096 - 128 || i >= 4096 + 128) {
            if (dst[i]) {
                fprintf(stderr, "wrong dst modified\n");
                return 1;
            }
        } else {
            if (dst[i] != 0xff) {
                fprintf(stderr, "wrong data moved\n");
                return 1;
            }
        }
    }

    return 0;
}
