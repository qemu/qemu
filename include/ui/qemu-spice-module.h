/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_SPICE_MODULE_H
#define QEMU_SPICE_MODULE_H

#ifdef CONFIG_SPICE
#include <spice.h>
#endif

struct QemuSpiceOps {
    void (*init)(void);
    void (*display_init)(void);
    int (*migrate_info)(const char *h, int p, int t, const char *s);
#ifdef CONFIG_SPICE
    int (*add_interface)(SpiceBaseInstance *sin);
#endif
};

extern int using_spice;
extern struct QemuSpiceOps qemu_spice;

#endif
