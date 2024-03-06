/*
 * Test GDB's follow-fork-mode.
 *
 * fork() a chain of processes.
 * Parents sends one byte to their children, and children return their
 * position in the chain, in order to prove that they survived GDB's fork()
 * handling.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

void break_after_fork(void)
{
}

int main(void)
{
    int depth = 42, err, i, fd[2], status;
    pid_t child, pid;
    ssize_t n;
    char b;

    for (i = 0; i < depth; i++) {
        err = pipe(fd);
        assert(err == 0);
        child = fork();
        break_after_fork();
        assert(child != -1);
        if (child == 0) {
            close(fd[1]);

            n = read(fd[0], &b, 1);
            close(fd[0]);
            assert(n == 1);
            assert(b == (char)i);
        } else {
            close(fd[0]);

            b = (char)i;
            n = write(fd[1], &b, 1);
            close(fd[1]);
            assert(n == 1);

            pid = waitpid(child, &status, 0);
            assert(pid == child);
            assert(WIFEXITED(status));
            return WEXITSTATUS(status) - 1;
        }
    }

    return depth;
}
