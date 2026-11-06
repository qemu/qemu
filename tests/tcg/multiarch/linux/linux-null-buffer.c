/*
 * Degenerate buffer pointers must behave like they do in the kernel.
 *
 * A zero-length getrandom() needs no valid buffer: the pointer is never
 * used, so getrandom(NULL, 0, 0) succeeds.  A required output buffer, on
 * the other hand, must not be silently skipped: sysinfo() and getitimer()
 * have to report EFAULT for a NULL pointer.
 *
 * The NULL cases go through syscall() rather than the libc wrappers: on
 * 32-bit x86, for example, getitimer() hands the kernel a local structure
 * and copies the result out itself, so it would follow the NULL pointer
 * before the kernel's answer could be seen.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <unistd.h>

int main(void)
{
    char buf[8];
    struct itimerval it;
    struct sysinfo si;

    /* A zero-length request is not rejected for a NULL buffer. */
    errno = 0;
    assert(syscall(SYS_getrandom, NULL, 0, 0) == 0);

    /* A NULL output pointer is reported, not silently skipped. */
    errno = 0;
    assert(syscall(SYS_sysinfo, NULL) == -1 && errno == EFAULT);
    errno = 0;
    assert(syscall(SYS_getitimer, ITIMER_REAL, NULL) == -1 && errno == EFAULT);

    /* The ordinary calls still work. */
    assert(getrandom(buf, sizeof(buf), 0) == sizeof(buf));
    assert(sysinfo(&si) == 0);
    assert(getitimer(ITIMER_REAL, &it) == 0);

    return EXIT_SUCCESS;
}
