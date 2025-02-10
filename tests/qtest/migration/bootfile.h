/*
 * Copyright (c) 2018 Red Hat, Inc. and/or its affiliates
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BOOTFILE_H
#define BOOTFILE_H

/* Common */
#define TEST_MEM_PAGE_SIZE 4096

/* x86 */
#define X86_TEST_MEM_START (1 * 1024 * 1024)
#define X86_TEST_MEM_END   (100 * 1024 * 1024)

/* S390 */
#define S390_TEST_MEM_START (1 * 1024 * 1024)
#define S390_TEST_MEM_END   (100 * 1024 * 1024)

/* PPC */
#define PPC_TEST_MEM_START (1 * 1024 * 1024)
#define PPC_TEST_MEM_END   (100 * 1024 * 1024)
#define PPC_H_PUT_TERM_CHAR 0x58

/* ARM */
#define ARM_TEST_MEM_START (0x40000000 + 1 * 1024 * 1024)
#define ARM_TEST_MEM_END   (0x40000000 + 100 * 1024 * 1024)
#define ARM_MACH_VIRT_UART 0x09000000
/* AArch64 kernel load address is 0x40080000, and the test memory starts at
 * 0x40100000. So the maximum allowable kernel size is 512KB.
 */
#define ARM_TEST_MAX_KERNEL_SIZE (512 * 1024)

void bootfile_delete(void);
char *bootfile_create(const char *arch, const char *dir, bool suspend_me);

#endif /* BOOTFILE_H */
