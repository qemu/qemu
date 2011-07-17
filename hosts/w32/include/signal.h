/*
 * QEMU w32 support
 *
 * Copyright (C) 2011 Stefan Weil
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef WIN32_SIGNAL_H
#define WIN32_SIGNAL_H

#include_next <signal.h>

#if !defined(WIN64)

#include <sys/types.h>    /* sigset_t */

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);
int sigfillset(sigset_t *set);

#endif /* W64 */

#endif /* WIN32_SIGNAL_H */
