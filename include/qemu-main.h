/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_MAIN_H
#define QEMU_MAIN_H

/*
 * The function to run on the main (initial) thread of the process.
 * NULL means QEMU's main event loop.
 * When non-NULL, QEMU's main event loop will run on a purposely created
 * thread, after which the provided function pointer will be invoked on
 * the initial thread.
 * This is useful on platforms which treat the main thread as special
 * (macOS/Darwin) and/or require all UI API calls to occur from the main
 * thread. Those platforms can initialise it to a specific function,
 * while UI implementations may reset it to NULL during their init if they
 * will handle system and UI events on the main thread via QEMU's own main
 * event loop.
 */
extern int (*qemu_main)(void);

#endif /* QEMU_MAIN_H */
