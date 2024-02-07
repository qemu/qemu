/*
 * Test GDB syscall catchpoints.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>

const char *catch_syscalls_state = "start";

void end_of_main(void)
{
}

int main(void)
{
    int ret = EXIT_FAILURE;
    char c0 = 'A', c1;
    int fd[2];

    catch_syscalls_state = "pipe2";
    if (pipe2(fd, 0)) {
        goto out;
    }

    catch_syscalls_state = "write";
    if (write(fd[1], &c0, sizeof(c0)) != sizeof(c0)) {
        goto out_close;
    }

    catch_syscalls_state = "read";
    if (read(fd[0], &c1, sizeof(c1)) != sizeof(c1)) {
        goto out_close;
    }

    catch_syscalls_state = "check";
    if (c0 == c1) {
        ret = EXIT_SUCCESS;
    }

out_close:
    catch_syscalls_state = "close";
    close(fd[0]);
    close(fd[1]);

out:
    catch_syscalls_state = "end";
    end_of_main();
    return ret;
}
