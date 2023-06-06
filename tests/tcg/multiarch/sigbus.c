#define _GNU_SOURCE 1

#include <assert.h>
#include <stdlib.h>
#include <signal.h>
#include <endian.h>


char x[32] __attribute__((aligned(16))) = {
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};
void * volatile p = (void *)&x + 15;

void sigbus(int sig, siginfo_t *info, void *uc)
{
    assert(sig == SIGBUS);
    assert(info->si_signo == SIGBUS);
#ifdef BUS_ADRALN
    assert(info->si_code == BUS_ADRALN);
#endif
    assert(info->si_addr == p);
    exit(EXIT_SUCCESS);
}

int main()
{
    struct sigaction sa = {
        .sa_sigaction = sigbus,
        .sa_flags = SA_SIGINFO
    };
    int allow_fail = 0;
    int tmp;

    tmp = sigaction(SIGBUS, &sa, NULL);
    assert(tmp == 0);

    /*
     * Select an operation that's likely to enforce alignment.
     * On many guests that support unaligned accesses by default,
     * this is often an atomic operation.
     */
#if defined(__aarch64__)
    asm volatile("ldxr %w0,[%1]" : "=r"(tmp) : "r"(p) : "memory");
#elif defined(__alpha__)
    asm volatile("ldl_l %0,0(%1)" : "=r"(tmp) : "r"(p) : "memory");
#elif defined(__arm__)
    asm volatile("ldrex %0,[%1]" : "=r"(tmp) : "r"(p) : "memory");
#elif defined(__powerpc__)
    asm volatile("lwarx %0,0,%1" : "=r"(tmp) : "r"(p) : "memory");
#elif defined(__riscv_atomic)
    asm volatile("lr.w %0,(%1)" : "=r"(tmp) : "r"(p) : "memory");
#else
    /* No insn known to fault unaligned -- try for a straight load. */
    allow_fail = 1;
    tmp = *(volatile int *)p;
#endif

    assert(allow_fail);

    /*
     * We didn't see a signal.
     * We might as well validate the unaligned load worked.
     */
    if (BYTE_ORDER == LITTLE_ENDIAN) {
        assert(tmp == 0x13121110);
    } else {
        assert(tmp == 0x10111213);
    }
    return EXIT_SUCCESS;
}
