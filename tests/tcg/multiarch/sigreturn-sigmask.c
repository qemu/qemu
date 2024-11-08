/*
 * Test that sigreturn() does not corrupt the signal mask.
 * Block SIGUSR2 and handle SIGUSR1.
 * Then sigwait() SIGUSR2, which relies on it remaining blocked.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

int seen_sig = -1;

static void signal_func(int sig)
{
    seen_sig = sig;
}

static void *thread_func(void *arg)
{
    kill(getpid(), SIGUSR2);
    return NULL;
}

int main(void)
{
    struct sigaction act = {
        .sa_handler = signal_func,
    };
    pthread_t thread;
    sigset_t set;
    int sig;

    assert(sigaction(SIGUSR1, &act, NULL) == 0);

    assert(sigemptyset(&set) == 0);
    assert(sigaddset(&set, SIGUSR2) == 0);
    assert(sigprocmask(SIG_BLOCK, &set, NULL) == 0);

    kill(getpid(), SIGUSR1);
    assert(seen_sig == SIGUSR1);

    assert(pthread_create(&thread, NULL, thread_func, NULL) == 0);
    assert(sigwait(&set, &sig) == 0);
    assert(sig == SIGUSR2);
    assert(pthread_join(thread, NULL) == 0);

    return EXIT_SUCCESS;
}
