/*
 * Copyright (c) 2018 Red Hat, Inc. and/or its affiliates
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef _TEST_MIGRATION_H_
#define _TEST_MIGRATION_H_

/* Common */
#define TEST_MEM_PAGE_SIZE 4096

/* x86 */
#define X86_TEST_MEM_START (1 * 1024 * 1024)
#define X86_TEST_MEM_END   (100 * 1024 * 1024)

/* PPC */
#define PPC_TEST_MEM_START (1 * 1024 * 1024)
#define PPC_TEST_MEM_END   (100 * 1024 * 1024)

#endif /* _TEST_MIGRATION_H_ */
