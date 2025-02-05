/*
 * Verify vid reads/writes really update the register.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static inline uint32_t getvid()
{
    uint32_t reg;
    asm volatile("%0=vid;" : "=r"(reg));
    return reg;
}
static inline void setvid(uint32_t val)
{
    asm volatile("vid=%0;" : : "r"(val));
    return;
}
int main()
{
    uint32_t testval = 0x3ff03ff;
    setvid(testval);
    if (testval != getvid()) {
        printf("ERROR: vid read returned: 0x%x\n", getvid());
    }
    assert(testval == getvid());

    /* L2VIC_NO_PENDING (0xffffffff) should not update the vid */
    setvid(0xffffffff);
    if (testval != getvid()) {
        printf("ERROR: vid read returned: 0x%x\n", getvid());
    }

    assert(testval == getvid());
}
