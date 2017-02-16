/*
 * mmio_interface.h
 *
 *  Copyright (C) 2017 : GreenSocs
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef MMIO_INTERFACE_H
#define MMIO_INTERFACE_H

#include "exec/memory.h"

#define TYPE_MMIO_INTERFACE "mmio_interface"
#define MMIO_INTERFACE(obj) OBJECT_CHECK(MMIOInterface, (obj),                 \
                                         TYPE_MMIO_INTERFACE)

typedef struct MMIOInterface {
    DeviceState parent_obj;

    MemoryRegion *subregion;
    MemoryRegion ram_mem;
    uint64_t start;
    uint64_t end;
    bool ro;
    uint64_t id;
    void *host_ptr;
} MMIOInterface;

void mmio_interface_map(MMIOInterface *s);
void mmio_interface_unmap(MMIOInterface *s);

#endif /* MMIO_INTERFACE_H */
