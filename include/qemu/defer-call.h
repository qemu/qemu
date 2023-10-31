/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Deferred calls
 *
 * Copyright Red Hat.
 */

#ifndef QEMU_DEFER_CALL_H
#define QEMU_DEFER_CALL_H

/* See documentation in util/defer-call.c */
void defer_call_begin(void);
void defer_call_end(void);
void defer_call(void (*fn)(void *), void *opaque);

#endif /* QEMU_DEFER_CALL_H */
