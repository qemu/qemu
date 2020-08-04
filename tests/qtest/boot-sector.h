/*
 * QEMU boot sector testing helpers.
 *
 * Copyright (c) 2016 Red Hat Inc.
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>
 *  Victor Kaplansky <victork@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TEST_BOOT_SECTOR_H
#define TEST_BOOT_SECTOR_H

#include "libqos/libqtest.h"

/* Create boot disk file. fname must be a suitable string for mkstemp() */
int boot_sector_init(char *fname);

/* Loop until signature in memory is OK.  */
void boot_sector_test(QTestState *qts);

/* unlink boot disk file.  */
void boot_sector_cleanup(const char *fname);

#endif /* TEST_BOOT_SECTOR_H */
