/*
 * Copyright (C) 2023 Bastian Koppelmann <kbastian@mail.uni-paderborn.de>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

int *testdev = (int *)0xf0000000;

#define FAIL 1
static inline void testdev_assert(int condition)
{
    if (!condition) {
        *testdev = FAIL;
        asm("debug");
    }
}

