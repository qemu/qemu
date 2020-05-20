/*
 * Thread Exerciser
 *
 * Unlike testthread which is mainly concerned about testing thread
 * semantics this test is used to exercise the thread creation and
 * accounting. A version of this test found a problem with clashing
 * cpu_indexes which caused a break in plugin handling.
 *
 * Based on the original test case by Nikolay Igotti.
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

int max_threads = 10;

typedef struct {
    int delay;
} ThreadArg;

static void *thread_fn(void* varg)
{
    ThreadArg *arg = varg;
    usleep(arg->delay);
    free(arg);
    return NULL;
}

int main(int argc, char **argv)
{
    int i;
    pthread_t *threads;

    if (argc > 1) {
        max_threads = atoi(argv[1]);
    }
    threads = calloc(sizeof(pthread_t), max_threads);

    for (i = 0; i < max_threads; i++) {
        ThreadArg *arg = calloc(sizeof(ThreadArg), 1);
        arg->delay = i * 100;
        pthread_create(threads + i, NULL, thread_fn, arg);
    }

    printf("Created %d threads\n", max_threads);

    /* sleep until roughly half the threads have "finished" */
    usleep(max_threads * 50);

    for (i = 0; i < max_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Done\n");

    return 0;
}
