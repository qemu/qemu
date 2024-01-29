/*
 * Test that GDB can access PROT_NONE pages.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

void break_here(void *q)
{
}

int main(void)
{
    long pagesize = sysconf(_SC_PAGESIZE);
    void *p, *q;
    int err;

    p = mmap(NULL, pagesize * 2, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(p != MAP_FAILED);
    q = p + pagesize - 1;
    strcpy(q, "42");

    err = mprotect(p, pagesize * 2, PROT_NONE);
    assert(err == 0);

    break_here(q);

    err = mprotect(p, pagesize * 2, PROT_READ);
    assert(err == 0);
    if (getenv("PROT_NONE_PY")) {
        assert(strcmp(q, "24") == 0);
    }

    return EXIT_SUCCESS;
}
