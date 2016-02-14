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

#ifndef TEST_BOOT_SECTOR
#define TEST_BOOT_SECTOR

/* Create boot disk file.  */
int boot_sector_init(const char *fname);

/* Loop until signature in memory is OK.  */
void boot_sector_test(void);

/* unlink boot disk file.  */
void boot_sector_cleanup(const char *fname);

#endif /* TEST_BOOT_SECTOR */
