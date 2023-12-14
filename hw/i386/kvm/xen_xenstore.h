/*
 * QEMU Xen emulation: Xenstore emulation
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_XEN_XENSTORE_H
#define QEMU_XEN_XENSTORE_H

void xen_xenstore_create(void);
int xen_xenstore_reset(void);

uint16_t xen_xenstore_get_port(void);

#endif /* QEMU_XEN_XENSTORE_H */
