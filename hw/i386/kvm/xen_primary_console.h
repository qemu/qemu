/*
 * QEMU Xen emulation: Primary console support
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_XEN_PRIMARY_CONSOLE_H
#define QEMU_XEN_PRIMARY_CONSOLE_H

void xen_primary_console_create(void);
int xen_primary_console_reset(void);

uint16_t xen_primary_console_get_port(void);
void xen_primary_console_set_be_port(uint16_t port);
uint64_t xen_primary_console_get_pfn(void);
void *xen_primary_console_get_map(void);

#endif /* QEMU_XEN_PRIMARY_CONSOLE_H */
