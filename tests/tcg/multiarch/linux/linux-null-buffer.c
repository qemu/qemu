/*
 * A zero-length getrandom() needs no valid buffer: the kernel never looks at
 * the pointer, so getrandom(NULL, 0, 0) succeeds.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/random.h>

int main(void)
{
    errno = 0;
    assert(getrandom(NULL, 0, 0) == 0);

    return EXIT_SUCCESS;
}
