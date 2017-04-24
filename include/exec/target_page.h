/*
 * Target page sizes and friends for non target files
 *
 * Copyright (c) 2017 Red Hat Inc
 *
 * Authors:
 *  David Alan Gilbert <dgilbert@redhat.com>
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef EXEC_TARGET_PAGE_H
#define EXEC_TARGET_PAGE_H

size_t qemu_target_page_size(void);
int qemu_target_page_bits(void);
int qemu_target_page_bits_min(void);

#endif
