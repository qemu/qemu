/*
 * linux-user signal handling tests.
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

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

/*
 * Thread handling
 */
typedef struct ThreadJob ThreadJob;

struct ThreadJob {
    int number;
    int sleep;
    int count;
};

static pthread_t *threads;
static int max_threads = 10;
__thread int signal_count;
int total_signal_count;

static void *background_thread_func(void *arg)
{
    ThreadJob *job = (ThreadJob *) arg;

    printf("thread%d: started\n", job->number);
    while (total_signal_count < job->count) {
        usleep(job->sleep);
    }
    printf("thread%d: saw %d alarms from %d\n", job->number,
           signal_count, total_signal_count);
    return NULL;
}

static void spawn_threads(void)
{
    int i;
    threads = calloc(sizeof(pthread_t), max_threads);

    for (i = 0; i < max_threads; i++) {
        ThreadJob *job = calloc(sizeof(ThreadJob), 1);
        job->number = i;
        job->sleep = i * 1000;
        job->count = i * 100;
        pthread_create(threads + i, NULL, background_thread_func, job);
    }
}

static void close_threads(void)
{
    int i;
    for (i = 0; i < max_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);
    threads = NULL;
}

static void sig_alarm(int sig, siginfo_t *info, void *puc)
{
    if (sig != SIGRTMIN) {
        error("unexpected signal");
    }
    signal_count++;
    __atomic_fetch_add(&total_signal_count, 1, __ATOMIC_SEQ_CST);
}

static void test_signals(void)
{
    struct sigaction act;
    struct itimerspec it;
    timer_t tid;
    struct sigevent sev;

    /* Set up SIG handler */
    act.sa_sigaction = sig_alarm;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    chk_error(sigaction(SIGRTMIN, &act, NULL));

    /* Create POSIX timer */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &tid;
    chk_error(timer_create(CLOCK_REALTIME, &sev, &tid));

    it.it_interval.tv_sec = 0;
    it.it_interval.tv_nsec = 1000000;
    it.it_value.tv_sec = 0;
    it.it_value.tv_nsec = 1000000;
    chk_error(timer_settime(tid, 0, &it, NULL));

    spawn_threads();

    do {
        usleep(1000);
    } while (total_signal_count < 2000);

    printf("shutting down after: %d signals\n", total_signal_count);

    close_threads();

    chk_error(timer_delete(tid));
}

int main(int argc, char **argv)
{
    test_signals();
    return 0;
}
