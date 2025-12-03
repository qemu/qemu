/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_MEM_REENTRANCY_H
#define QEMU_MEM_REENTRANCY_H 1

typedef struct MemReentrancyGuard {
    bool engaged_in_io;
} MemReentrancyGuard;

#endif
