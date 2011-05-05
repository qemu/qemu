/*
 * QEMU progress printing utility functions
 *
 * Copyright (C) 2011 Jes Sorensen <Jes.Sorensen@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu-common.h"
#include "osdep.h"
#include "sysemu.h"
#include <stdio.h>
#include <signal.h>

struct progress_state {
    float current;
    float last_print;
    float min_skip;
    void (*print)(void);
    void (*end)(void);
};

static struct progress_state state;
static volatile sig_atomic_t print_pending;

/*
 * Simple progress print function.
 * @percent relative percent of current operation
 * @max percent of total operation
 */
static void progress_simple_print(void)
{
    printf("    (%3.2f/100%%)\r", state.current);
    fflush(stdout);
}

static void progress_simple_end(void)
{
    printf("\n");
}

static void progress_simple_init(void)
{
    state.print = progress_simple_print;
    state.end = progress_simple_end;
}

#ifdef CONFIG_POSIX
static void sigusr_print(int signal)
{
    print_pending = 1;
}
#endif

static void progress_dummy_print(void)
{
    if (print_pending) {
        fprintf(stderr, "    (%3.2f/100%%)\n", state.current);
        print_pending = 0;
    }
}

static void progress_dummy_end(void)
{
}

static void progress_dummy_init(void)
{
#ifdef CONFIG_POSIX
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    sigfillset(&action.sa_mask);
    action.sa_handler = sigusr_print;
    action.sa_flags = 0;
    sigaction(SIGUSR1, &action, NULL);
#endif

    state.print = progress_dummy_print;
    state.end = progress_dummy_end;
}

void qemu_progress_init(int enabled, float min_skip)
{
    state.min_skip = min_skip;
    if (enabled) {
        progress_simple_init();
    } else {
        progress_dummy_init();
    }
}

void qemu_progress_end(void)
{
    state.end();
}

void qemu_progress_print(float percent, int max)
{
    float current;

    if (max == 0) {
        current = percent;
    } else {
        current = state.current + percent / 100 * max;
    }
    if (current > 100) {
        current = 100;
    }
    state.current = current;

    if (current > (state.last_print + state.min_skip) ||
        (current == 100) || (current == 0)) {
        state.last_print = state.current;
        state.print();
    }
}
