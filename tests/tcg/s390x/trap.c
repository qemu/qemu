/*
 * Copyright 2021 IBM Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

static void error1(const char *filename, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d: ", filename, line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static int __chk_error(const char *filename, int line, int ret)
{
    if (ret < 0) {
        error1(filename, line, "%m (ret=%d, errno=%d/%s)",
               ret, errno, strerror(errno));
    }
    return ret;
}

#define error(fmt, ...) error1(__FILE__, __LINE__, fmt, ## __VA_ARGS__)

#define chk_error(ret) __chk_error(__FILE__, __LINE__, (ret))

int sigfpe_count;
int sigill_count;

static void sig_handler(int sig, siginfo_t *si, void *puc)
{
    if (sig == SIGFPE) {
        if (si->si_code != 0) {
            error("unexpected si_code: 0x%x != 0", si->si_code);
        }
        ++sigfpe_count;
        return;
    }

    if (sig == SIGILL) {
        ++sigill_count;
        return;
    }

    error("unexpected signal 0x%x\n", sig);
}

int main(int argc, char **argv)
{
    sigfpe_count = sigill_count = 0;

    struct sigaction act;

    /* Set up SIG handler */
    act.sa_sigaction = sig_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    chk_error(sigaction(SIGFPE, &act, NULL));
    chk_error(sigaction(SIGILL, &act, NULL));

    uint64_t z = 0x0ull;
    uint64_t lz = 0xffffffffffffffffull;
    asm volatile (
        "lg %%r13,%[lz]\n"
        "cgitne %%r13,0\n" /* SIGFPE */
        "lg %%r13,%[z]\n"
        "cgitne %%r13,0\n" /* no trap */
        "nopr\n"
        "lg %%r13,%[lz]\n"
        "citne %%r13,0\n" /* SIGFPE */
        "lg %%r13,%[z]\n"
        "citne %%r13,0\n" /* no trap */
        "nopr\n"
        :
        : [z] "m" (z), [lz] "m" (lz)
        : "memory", "r13");

    if (sigfpe_count != 2) {
        error("unexpected SIGFPE count: %d != 2", sigfpe_count);
    }
    if (sigill_count != 0) {
        error("unexpected SIGILL count: %d != 0", sigill_count);
    }

    printf("PASS\n");
    return 0;
}
