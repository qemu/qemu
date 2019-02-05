#include <assert.h>
#include <sys/prctl.h>

asm(".arch armv8.4-a");

#ifndef PR_PAC_RESET_KEYS
#define PR_PAC_RESET_KEYS  54
#define PR_PAC_APDAKEY     (1 << 2)
#endif

int main()
{
    int x;
    void *p0 = &x, *p1, *p2;

    asm volatile("pacdza %0" : "=r"(p1) : "0"(p0));
    prctl(PR_PAC_RESET_KEYS, PR_PAC_APDAKEY, 0, 0, 0);
    asm volatile("pacdza %0" : "=r"(p2) : "0"(p0));

    assert(p1 != p0);
    assert(p1 != p2);
    return 0;
}
