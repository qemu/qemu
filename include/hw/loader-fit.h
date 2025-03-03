/*
 * Flattened Image Tree loader.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "exec/hwaddr.h"

struct fit_loader_match {
    const char *compatible;
    const void *data;
};

struct fit_loader {
    const struct fit_loader_match *matches;
    hwaddr (*addr_to_phys)(void *opaque, uint64_t addr);
    void *(*fdt_filter)(void *opaque, const void *fdt,
                        const void *match_data, hwaddr *load_addr);
    const void *(*kernel_filter)(void *opaque, const void *kernel,
                                 hwaddr *load_addr, hwaddr *entry_addr);
};

/**
 * load_fit: load a FIT format image
 * @ldr: structure defining board specific properties and hooks
 * @filename: image to load
 * @pfdt: pointer to update with address of FDT blob
 * @opaque: opaque value passed back to the hook functions in @ldr
 * Returns: 0 on success, or a negative errno on failure
 *
 * @pfdt is used to tell the caller about the FDT blob. On return, it
 * has been set to point to the FDT blob, and it is now the caller's
 * responsibility to free that memory with g_free(). Usually the caller
 * will want to pass in &machine->fdt here, to record the FDT blob for
 * the dumpdtb option and QMP/HMP commands.
 */
int load_fit(const struct fit_loader *ldr, const char *filename, void **pfdt,
             void *opaque);

#endif /* HW_LOADER_FIT_H */
