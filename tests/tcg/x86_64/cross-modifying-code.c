/*
 * Test patching code, running in one thread, from another thread.
 *
 * Intel SDM calls this "cross-modifying code" and recommends a special
 * sequence, which requires both threads to cooperate.
 *
 * Linux kernel uses a different sequence that does not require cooperation and
 * involves patching the first byte with int3.
 *
 * Finally, there is user-mode software out there that simply uses atomics, and
 * that seems to be good enough in practice. Test that QEMU has no problems
 * with this as well.
 */

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

void add1_or_nop(long *x);
asm(".pushsection .rwx,\"awx\",@progbits\n"
    ".globl add1_or_nop\n"
    /* addq $0x1,(%rdi) */
    "add1_or_nop: .byte 0x48, 0x83, 0x07, 0x01\n"
    "ret\n"
    ".popsection\n");

#define THREAD_WAIT 0
#define THREAD_PATCH 1
#define THREAD_STOP 2

static void *thread_func(void *arg)
{
    int val = 0x0026748d; /* nop */

    while (true) {
        switch (__atomic_load_n((int *)arg, __ATOMIC_SEQ_CST)) {
        case THREAD_WAIT:
            break;
        case THREAD_PATCH:
            val = __atomic_exchange_n((int *)&add1_or_nop, val,
                                      __ATOMIC_SEQ_CST);
            break;
        case THREAD_STOP:
            return NULL;
        default:
            assert(false);
            __builtin_unreachable();
        }
    }
}

#define INITIAL 42
#define COUNT 1000000

int main(void)
{
    int command = THREAD_WAIT;
    pthread_t thread;
    long x = 0;
    int err;
    int i;

    err = pthread_create(&thread, NULL, &thread_func, &command);
    assert(err == 0);

    __atomic_store_n(&command, THREAD_PATCH, __ATOMIC_SEQ_CST);
    for (i = 0; i < COUNT; i++) {
        add1_or_nop(&x);
    }
    __atomic_store_n(&command, THREAD_STOP, __ATOMIC_SEQ_CST);

    err = pthread_join(thread, NULL);
    assert(err == 0);

    assert(x >= INITIAL);
    assert(x <= INITIAL + COUNT);

    return EXIT_SUCCESS;
}
