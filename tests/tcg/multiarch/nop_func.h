/*
 * No-op functions that can be safely copied.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NOP_FUNC_H
#define NOP_FUNC_H

static const char nop_func[] = {
#if defined(__aarch64__)
    0xc0, 0x03, 0x5f, 0xd6,     /* ret */
#elif defined(__alpha__)
    0x01, 0x80, 0xFA, 0x6B,     /* ret */
#elif defined(__arm__)
    0x1e, 0xff, 0x2f, 0xe1,     /* bx lr */
#elif defined(__riscv)
    0x67, 0x80, 0x00, 0x00,     /* ret */
#elif defined(__s390__)
    0x07, 0xfe,                 /* br %r14 */
#elif defined(__i386__) || defined(__x86_64__)
    0xc3,                       /* ret */
#endif
};

#endif
