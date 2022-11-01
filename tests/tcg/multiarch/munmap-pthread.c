/* Test that munmap() and thread creation do not race. */
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const char nop_func[] = {
#if defined(__aarch64__)
    0xc0, 0x03, 0x5f, 0xd6,     /* ret */
#elif defined(__alpha__)
    0x01, 0x80, 0xFA, 0x6B,     /* ret */
#elif defined(__arm__)
    0x1e, 0xff, 0x2f, 0xe1,     /* bx lr */
#elif defined(__riscv)
    0x67, 0x80, 0x00, 0x00,     /* ret */
#elif defined(__s390__)
    0x07, 0xfe,                 /* br %r14 */
#elif defined(__i386__) || defined(__x86_64__)
    0xc3,                       /* ret */
#endif
};

static void *thread_mmap_munmap(void *arg)
{
    volatile bool *run = arg;
    char *p;
    int ret;

    while (*run) {
        p = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(p != MAP_FAILED);

        /* Create a small translation block.  */
        memcpy(p, nop_func, sizeof(nop_func));
        ((void(*)(void))p)();

        ret = munmap(p, getpagesize());
        assert(ret == 0);
    }

    return NULL;
}

static void *thread_dummy(void *arg)
{
    return NULL;
}

int main(void)
{
    pthread_t mmap_munmap, dummy;
    volatile bool run = true;
    int i, ret;

    /* Without a template, nothing to test. */
    if (sizeof(nop_func) == 0) {
        return EXIT_SUCCESS;
    }

    ret = pthread_create(&mmap_munmap, NULL, thread_mmap_munmap, (void *)&run);
    assert(ret == 0);

    for (i = 0; i < 1000; i++) {
        ret = pthread_create(&dummy, NULL, thread_dummy, NULL);
        assert(ret == 0);
        ret = pthread_join(dummy, NULL);
        assert(ret == 0);
    }

    run = false;
    ret = pthread_join(mmap_munmap, NULL);
    assert(ret == 0);

    return EXIT_SUCCESS;
}
