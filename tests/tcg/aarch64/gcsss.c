/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gcs.h"

#define IN_PROGRESS(X)  ((uint64_t)(X) | 5)
#define CAP(X)          (((uint64_t)(X) & ~0xfff) + 1)

static uint64_t * __attribute__((noinline)) recurse(size_t index)
{
    if (index == 0) {
        return gcspr();
    }
    return recurse(index - 1);
}

int main()
{
    void *tmp;
    uint64_t *alt_stack, *alt_cap;
    uint64_t *orig_pr, *orig_cap;
    uint64_t *bottom;
    size_t pagesize = getpagesize();
    size_t words;

    enable_gcs(0);
    orig_pr = gcspr();

    /* Allocate a guard page before and after. */
    tmp = mmap(0, 3 * pagesize, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    assert(tmp != MAP_FAILED);

    /* map_shadow_stack won't replace existing mappings */
    munmap(tmp + pagesize, pagesize);

    /* Allocate a new stack between the guards. */
    alt_stack = (uint64_t *)
        syscall(__NR_map_shadow_stack, tmp + pagesize, pagesize,
                SHADOW_STACK_SET_TOKEN);
    assert(alt_stack == tmp + pagesize);

    words = pagesize / 8;
    alt_cap = alt_stack + words - 1;

    /* SHADOW_STACK_SET_TOKEN set the cap. */
    assert(*alt_cap == CAP(alt_cap));

    /* Swap to the alt stack, one step at a time. */
    gcsss1(alt_cap);

    assert(gcspr() == alt_cap);
    assert(*alt_cap == IN_PROGRESS(orig_pr));

    orig_cap = gcsss2();

    assert(orig_cap == orig_pr - 1);
    assert(*orig_cap == CAP(orig_cap));
    assert(gcspr() == alt_stack + words);

    /* We should be able to use the whole stack. */
    bottom = recurse(words - 1);
    assert(bottom == alt_stack);

    /* We should be back where we started. */
    assert(gcspr() == alt_stack + words);

    /* Swap back to the original stack. */
    gcsss1(orig_cap);
    tmp = gcsss2();

    assert(gcspr() == orig_pr);
    assert(tmp == alt_cap);

    exit(0);
}
