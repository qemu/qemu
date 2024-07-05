/*
 * To be compiled with -march=armv8.5-a+memtag
 *
 * This test is adapted from a Linux test. Please see:
 *
 * https://www.kernel.org/doc/html/next/arch/arm64/memory-tagging-extension.html#example-of-correct-usage
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <string.h>
/*
 * From arch/arm64/include/uapi/asm/hwcap.h
 */
#define HWCAP2_MTE              (1 << 18)

/*
 * From arch/arm64/include/uapi/asm/mman.h
 */
#define PROT_MTE                 0x20

/*
 * Insert a random logical tag into the given pointer.
 */
#define insert_random_tag(ptr) ({                   \
    uint64_t __val;                                 \
    asm("irg %0, %1" : "=r" (__val) : "r" (ptr));   \
    __val;                                          \
})

/*
 * Set the allocation tag on the destination address.
 */
#define set_tag(tagged_addr) do {                                      \
        asm volatile("stg %0, [%0]" : : "r" (tagged_addr) : "memory"); \
} while (0)


int main(int argc, char *argv[])
{
    unsigned char *a;
    unsigned long page_sz = sysconf(_SC_PAGESIZE);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);

    /* check if MTE is present */
    if (!(hwcap2 & HWCAP2_MTE)) {
        return EXIT_FAILURE;
    }

    /*
     * Enable the tagged address ABI, synchronous or asynchronous MTE
     * tag check faults (based on per-CPU preference) and allow all
     * non-zero tags in the randomly generated set.
     */
    if (prctl(PR_SET_TAGGED_ADDR_CTRL,
              PR_TAGGED_ADDR_ENABLE | PR_MTE_TCF_SYNC | PR_MTE_TCF_ASYNC |
              (0xfffe << PR_MTE_TAG_SHIFT),
              0, 0, 0)) {
        perror("prctl() failed");
        return EXIT_FAILURE;
    }

    a = mmap(0, page_sz, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (a == MAP_FAILED) {
        perror("mmap() failed");
        return EXIT_FAILURE;
    }

    printf("a[] address is %p\n", a);

    /*
     * Enable MTE on the above anonymous mmap. The flag could be passed
     * directly to mmap() and skip this step.
     */
    if (mprotect(a, page_sz, PROT_READ | PROT_WRITE | PROT_MTE)) {
        perror("mprotect() failed");
        return EXIT_FAILURE;
    }

    /* access with the default tag (0) */
    a[0] = 1;
    a[1] = 2;

    printf("a[0] = %hhu a[1] = %hhu\n", a[0], a[1]);

    /* set the logical and allocation tags */
    a = (unsigned char *)insert_random_tag(a);
    set_tag(a);

    printf("%p\n", a);

    return 0;
}
