/*
 * QEMU mprotect functions
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_MPROTECT_H
#define QEMU_MPROTECT_H

int qemu_mprotect_rw(void *addr, size_t size);
int qemu_mprotect_rwx(void *addr, size_t size);
int qemu_mprotect_none(void *addr, size_t size);

#endif
