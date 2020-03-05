#include <assert.h>
#include <sys/prctl.h>
#include <stdio.h>

#ifndef PR_PAC_RESET_KEYS
#define PR_PAC_RESET_KEYS  54
#define PR_PAC_APDAKEY     (1 << 2)
#endif

#define TESTS 1000

int main()
{
    int x, i, count = 0;
    void *p0 = &x, *p1, *p2;
    float perc;

    for (i = 0; i < TESTS; i++) {
        asm volatile("pacdza %0" : "=r"(p1) : "0"(p0));
        prctl(PR_PAC_RESET_KEYS, PR_PAC_APDAKEY, 0, 0, 0);
        asm volatile("pacdza %0" : "=r"(p2) : "0"(p0));

        if (p1 != p0) {
            count++;
        }
        if (p1 != p2) {
            count++;
        }
    }

    perc = (float) count / (float) (TESTS * 2);
    printf("Ptr Check: %0.2f%%\n", perc * 100.0);
    assert(perc > 0.95);
    return 0;
}
