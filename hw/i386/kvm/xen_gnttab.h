/*
 * QEMU Xen emulation: Grant table support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_XEN_GNTTAB_H
#define QEMU_XEN_GNTTAB_H

void xen_gnttab_create(void);
int xen_gnttab_map_page(uint64_t idx, uint64_t gfn);

#endif /* QEMU_XEN_GNTTAB_H */
