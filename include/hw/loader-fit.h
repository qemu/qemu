/*
 * Flattened Image Tree loader.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_LOADER_FIT_H
#define HW_LOADER_FIT_H

#include <exec/hwaddr.h>

struct fit_loader_match {
    const char *compatible;
    const void *data;
};

struct fit_loader {
    const struct fit_loader_match *matches;
    hwaddr (*addr_to_phys)(void *opaque, uint64_t addr);
    const void *(*fdt_filter)(void *opaque, const void *fdt,
                              const void *match_data, hwaddr *load_addr);
    const void *(*kernel_filter)(void *opaque, const void *kernel,
                                 hwaddr *load_addr, hwaddr *entry_addr);
};

int load_fit(const struct fit_loader *ldr, const char *filename, void *opaque);

#endif /* HW_LOADER_FIT_H */
