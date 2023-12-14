/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_MAIN_H
#define QEMU_MAIN_H

int qemu_default_main(void);
extern int (*qemu_main)(void);

#endif /* QEMU_MAIN_H */
