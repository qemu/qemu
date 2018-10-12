/*
 * S390 guest code used in migration tests
 *
 * Copyright 2018 Thomas Huth, Red Hat Inc.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#define LOADPARM_LEN 8  /* Needed for sclp.h */

#include <libc.h>
#include <s390-ccw.h>
#include <sclp.h>

char stack[0x8000] __attribute__((aligned(4096)));

#define START_ADDRESS  (1024 * 1024)
#define END_ADDRESS    (100 * 1024 * 1024)

void main(void)
{
    unsigned long addr;

    sclp_setup();
    sclp_print("A");

    while (1) {
        for (addr = START_ADDRESS; addr < END_ADDRESS; addr += 4096) {
            *(volatile char *)addr += 1;  /* Change pages */
        }
        sclp_print("B");
    }
}
