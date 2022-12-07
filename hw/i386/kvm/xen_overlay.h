/*
 * QEMU Xen emulation: Shared/overlay pages support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_XEN_OVERLAY_H
#define QEMU_XEN_OVERLAY_H

void xen_overlay_create(void);

int xen_overlay_map_shinfo_page(uint64_t gpa);
void *xen_overlay_get_shinfo_ptr(void);

#endif /* QEMU_XEN_OVERLAY_H */
